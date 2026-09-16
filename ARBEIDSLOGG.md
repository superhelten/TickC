# BTC Ticker — arbeidslogg

Status per 16.09.2026. Skrevet for agenter som jobber videre på `ticker.c`.
Fase 1 er ferdig. Fase 2 del A (animasjonsklokke, backoff, stale-indikator)
er ferdig; del B og C står igjen — se
`docs/superpowers/specs/2026-09-16-ticker-fase2-design.md`.

Alt ligger i **én fil**, `ticker.c` (~1516 linjer). Ingen eksterne avhengigheter
utover Win32 og WinHTTP.

---

## Bygg

```
"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
cl /nologo /W4 /O2 ticker.c /link /SUBSYSTEM:WINDOWS /OUT:ticker.exe
```

Bygger **rent på `/W4`** — hold det sånn. Målarkitektur er **x86** (matcher
den opprinnelige exe-en). Prosessen kjører single-instance via en navngitt
mutex, så **stopp den kjørende instansen før linking**, ellers feiler
`LNK1104: cannot open file 'ticker.exe'`.

Fotavtrykk: ~3,3 MB private bytes, ~140 KB exe.

---

## Hva appen gjør

Tray-ikon som viser BTC/USDT-kurs som piksel-tegnede siffer. Venstreklikk
åpner et rammeløst candlestick-panel med crosshair, zoom og panorering.
Høyreklikk gir «Avslutt».

---

## Arkitektur

### Tråder

| Tråd | Ansvar |
|---|---|
| UI | Alle vinduer, all tegning, all inndata. **Rører aldri nettverket.** |
| `NetworkThread` | Eier WinHTTP-håndtakene. Henter, parser, fletter. |

Arbeidertråden går i `WaitForMultipleObjects(hStopEvent, hWakeEvent, 3000)`.
Den henter **uten lås**, tar så `CRITICAL_SECTION` kun rundt flettingen, og
gjør `PostMessage(WM_APP_DATA)` **etter** at låsen er sluppet.

> Hvis du legger til noe her: `PostMessage` må aldri skje inne i låsen. UI-tråden
> kan sitte og vente på den samme låsen i `WM_PAINT`.

**Låsen dekker:** `candles[]`, `candleCount`, `viewStart`, `viewCount`,
`followLive`, `lastPrice`, `hPopup`. Alt annet i `AppContext` er UI-eid.

### Datalag

- `candles[1440]` — fast statisk array, 24 timer med 1m-lys. **Ingen malloc noe sted.**
- Første henting seeder 300 lys (`limit=300`, ~50 KB). Deretter `limit=3` (~500 byte).
- `MergeCandles()` fletter på `openTime`: samme tidsstempel **oppdaterer**
  (lyset under forming endrer seg), nyere **legges til**, eldste faller ut ved taket.
- Oppdages et tidshull (> 2 lysintervaller) nullstilles bufferet, slik at grafen
  ikke tegner en sammenhengende kurve over dødtid.

### Tegning

Dobbeltbuffret gjennom en minne-DC, `WM_ERASEBKGND` returnerer 1.
Begge tegneloopene går over **`vc` (synlige lys)**, ikke hele historikken —
tegnearbeidet er derfor uavhengig av hvor mye historikk som er lagret.

GDI-objekter er bufret: ni faste farger lages ved oppstart, de blandede
fade-fargene bare når `(chrome, closeHot)` endrer seg.

### Animasjonsklokka

Én timer (`TIMER_ANIM_ID`, 16 ms) driver alt tidsavhengig. Den er **tidsbasert,
ikke stegbasert**: hver tikk måler faktisk forløpt tid mot `lastAnimTick` og
interpolerer eksponentielt gjennom `AnimStep()`. `SetTimer(16)` fyrer i praksis
hver ~15,6 ms og slås sammen under last — fast steglengde ville gitt ulik
hastighet avhengig av systembelastning.

Klokka **lever bare mens noe er i bevegelse**. `StartAnim()` er idempotent og
nullstiller `lastAnimTick` kun når klokka faktisk var stanset; `WM_TIMER` dreper
seg selv når alt har satt seg. I hvile går det ingen timer — verifisert, se
målingene.

### Nettverkshelse

Arbeidertråden teller sammenhengende feil i `netFailures` og venter
`NetBackoffMs()` i stedet for faste 3 s. Ved tre sammenhengende feil slippes
`hConnect`, slik at `HttpGet` bygger forbindelsen på nytt og DNS slås opp igjen
— uten det henger vi fast på en IP som ikke lenger svarer.

`lastOkTick` driver stale-tilstanden (`> 3 * TIMER_INTERVAL` = 9 s, altså to
tapte sykluser, slik at én treg forespørsel ikke blinker indikatoren). Stale
vises fire steder: dempet header-pris, sekundteller i undertittelen, `(frakoblet)`
i tray-tipset og dempede siffer i ikonet.

**Låsen dekker i tillegg:** `lastOkTick`, `nextRetryTick`, `netFailures`.
`hConnect` eies av arbeidertråden alene og trenger ingen lås.

---

## Sentrale konstanter

| Navn | Verdi | Betydning |
|---|---|---|
| `MAX_CANDLES` | 1440 | 24 t buffer |
| `SEED_COUNT` / `DEFAULT_VIEW` | 300 | 5 t frø / standard utsnitt |
| `MIN_VIEW` | 8 | maks innzoom |
| `ZOOM_STEP` | 1.2 | per hjulhakk |
| `TIMER_INTERVAL` | 3000 | hentefrekvens (ms) |
| `KLINE_MS` | 60000 | ett 1m-lys |
| `REOPEN_GUARD_MS` | 250 | hindrer at lukkeklikk åpner igjen |
| `SHOW_GRACE_MS` | 400 | ignorer fokustap rett etter åpning |
| `s_httpBuf` | 98304 | 1,94× margin mot 50,7 KB svar |
| `ANIM_INTERVAL` | 16 | animasjonsklokke (~60 fps) |
| `ANIM_TAU_CHROME` | 55,0 | tidskonstant chrome-fade (ms) |
| `ANIM_DT_MAX` | 100,0 | klemmer `dt`, lang pause gir ett hopp |
| `NET_RETRY_MAX` | 60000 | tak for eksponentiell backoff (ms) |
| `NET_RECONNECT_AT` | 3 | antall feil før `hConnect` slippes |
| `STALE_AFTER` | 9000 | 3 × `TIMER_INTERVAL` = to tapte sykluser |

---

## Funksjoner, i den rekkefølgen de kom

1. **GDI-graf i popup** — candlesticks, rutenett, prisakse, header.
2. **Riktig tray-ikon** — font-tabellen var ødelagt fra start (se feil #3).
3. **Én linje i ikonet** — 4×9-font, `75.8` i stedet for `75k`/`778`.
4. **Crosshair + hover-boks** — tid og OHLC for lyset under musa.
5. **Flyttbart / skalerbart panel** — `WS_THICKFRAME` + `WM_NCCALCSIZE`.
6. **Hover-kontroller** — kryss, grip-prikker, ramme og resize-grip som toner
   inn ved hover og er usynlige i hvile.
7. **Ctrl + hjul = zoom**, ankret mot musepekeren.
8. **Akkumulerende historikk + panorering** — hjul og dra.
9. **Arbeidertråd + GDI-cache.**
10. **Animasjonsklokke, eksponentiell backoff og stale-indikator** (fase 2 A).

---

## Feil som ble funnet og rettet

Disse er verdt å kjenne til — flere var ikke synlige uten måling.

**1. `SetForegroundWindow` ble nektet.** Windows' forgrunnslås gjorde at
popupen ble vist men aldri aktivert, fikk `WA_INACTIVE` umiddelbart og skjulte
seg selv. Løst med `AttachThreadInput` rundt byttet (`ForceForeground()`),
pluss en nådeperiode på 400 ms i `WM_ACTIVATE`.

**2. ESC virket ikke.** `WM_ACTIVATE` returnerte 0 også ved *aktivering*, så
`DefWindowProc` — som setter tastaturfokus — kjørte aldri. `GetGUIThreadInfo`
viste `hwndFocus = 0`. Nå faller aktiveringsgrenen gjennom til `DefWindowProc`.

> Testfelle: `PostMessage(WM_KEYDOWN)` går utenom fokus og gir falsk positiv.
> Bruk `keybd_event`.

**3. Font-tabellen var ødelagt.** Alle siffer-verdiene var 19-bit, men koden
leser 15 bit (`bitPos = 14 - (r*3+c)`). De øvre bitene falt utenfor, og ikonet
tegnet støy. Ti av tolv glyfer var feil. Verdiene ble regnet på nytt fra
tabellens egne kommentarer og verifisert ved å dekode dem tilbake.

**4. 512-byte lesebuffer.** Ett `WinHttpReadData`-kall returnerer bare det som
tilfeldigvis er buffret. Ekte klines-svar er 10–50 KB. Nå leses alt i løkke
(`HttpGet()`).

**5. `%.1f` rundet 99950–99999 opp til «100.0».** Terskelen `price >= 100000`
fanget det ikke — det er den *formaterte strengen* som må få plass. Nå måles
bredden med `IconTextWidth()` etter formatering.

**6. Drag frøs hvert 3. sekund.** `WM_TIMER` fyrer også inne i den modale
flytte-/resize-løkka. Løst med `inSizeMove`-flagget — mindre relevant etter
tråden, men fortsatt riktig.

**7. Crosshair pekte feil etter zoom.** `hoverIdx` er en *absolutt* indeks;
zoomer man uten å flytte musa, flyttet utsnittet seg under en indeks som ikke
ble oppdatert. Zoom og panorering regner nå om via `HitCandle()`.

**8. WinHTTP-timeouts manglet.** Standard mottakstimeout er 30 s; ved avslutning
venter vi bare 3 s på tråden og lukket sesjonen under den. Nå satt til 5 s.

**9. Backoffen eskalerte aldri.** Nullstillingen av `netFailures` sto etter hele
`WaitForMultipleObjects`-kallet, med bare en sjekk på `WAIT_OBJECT_0` (stopp)
over seg. Den traff derfor også `WAIT_TIMEOUT` — altså hver eneste syklus.
`netFailures` kom aldri høyere enn 1, ventetiden sto fast på ~6 s, og
`hConnect` ble aldri sluppet fordi `failures == NET_RECONNECT_AT` aldri ble
sant. Koden så riktig ut ved lesing; loggen viste `feil=1` i tjue sykluser på
rad. Nullstillingen henger nå på `wr == WAIT_OBJECT_0 + 1` alene.

> Dette er grunnen til at del A ble målt mot en faktisk blokkert linje og ikke
> bare enhetstestet. `NetBackoffMs()` var grønn på alle sytten testene hele
> tiden — feilen lå i *hvem som kalte den med hvilken teller*.

**10. Animasjonsklokka gikk videre på et skjult panel.** Stale-grenen satte
`settled = FALSE` for å holde sekundtelleren i live. Begge skjulestiene
(`WM_ACTIVATE` og `TogglePopup`) dreper ikke timeren, så en frakoblet linje ga
60 tikk i sekundet på et panel ingen så. Grenen er nå betinget av
`IsWindowVisible(hwnd)`, og `TogglePopup` starter klokka igjen ved visning
dersom vi er frakoblet — ellers sto telleren stille til neste `WM_APP_DATA`,
som under backoff kan være et helt minutt unna.

---

## Målinger

### UI-latens (maks, målt med `SendMessageTimeout` mot UI-tråden)

| Versjon | Snitt | Maks | Pauser >50 ms |
|---|---|---|---|
| 300 lys, full henting hvert 3. s | 648 ms | 929 ms | 4–5 per 13 s |
| Inkrementell henting (`limit=3`) | 486 ms | — | — |
| Fjernet dobbelt priskall | 233 ms | 247 ms | — |
| **Arbeidertråd** | — | **2,1–4,2 ms** | **ingen** |

Nettverkskallet var 130× dyrere enn hele opptegningen. Tråden var hele gevinsten.

### Opptegning

| | Netto per opptegning |
|---|---|
| Før GDI-cache | 1,796 ms |
| Etter GDI-cache | **1,255 ms** (−30 %) |

Zoomet helt ut er *raskere* (1,5 ms) fordi lysene da er 1 px brede.

### Enhetstester på ekte kode

Teknikken som ble brukt: trekk funksjonen ut av `ticker.c` med `sed` inn i en
liten harness, så testene kjører mot **den faktiske koden**, ikke en kopi.

- `ParseKlines` mot ekte Binance-svar: **300/300 lys, 0 avvik** mot `ConvertFrom-Json`.
- `MergeCandles`, 6 tilfeller, alle grønne: frø på 300 · 200 påfølgende hentinger
  (→ 500 sammenhengende lys) · dobbel henting uten duplikat · lys under forming
  oppdatert i stedet for lagt til · tak på 1440 med eldste ut · tidshull nullstiller.
- Zoom-ankring analytisk: største avvik **0,50 lys** (ren avrunding).

### Empirisk verifisert

- **Zoom-ankring:** samme lys (`01:35`, identiske OHLC) lå under musa både før og
  etter 5 hakk, mens spenn gikk 5t → 2t og Y-aksen strammet seg inn.
- **Hover-boks mot fasit:** `02:39`, O 75872.21 / H 75915.49 / L 75872.20 /
  C 75915.45 — eksakt treff mot uavhengig hentede Binance-data.
- **Pekere i alle ni soner:** header `IDC_SIZEALL` (må tvinges — `DefWindowProc`
  gir vanlig pil for `HTCAPTION`), kanter `IDC_SIZEWE`/`SIZENS`/`SIZENWSE`/`SIZENESW`
  (disse ordner `DefWindowProc` selv).
- **Hvile vs. hover:** rammepiksel `#0D1117` (ren bakgrunn) i hvile, `#333D4B` ved
  hover, med målt fade `#0D1117 → #171D25 → #222934 → #27303B → #333D4B`.
- **Stresstest:** 15 965 pan/zoom/tegne-operasjoner på 20 s samtidig med ~7
  fletteoperasjoner fra tråden. Ingen vranglås.
- **Lekkasjer:** GDI og USER flate gjennom alle tester. GDI står konstant på **31**
  (opp fra 18 — tilsiktet, objektene holdes nå permanent i stedet for å opprettes
  16 ganger per bilde).
- **Avslutning:** 134–228 ms, ingen etterlatt prosess.

### Fase 2 del A

Enhetstester mot kode trukket ut av `ticker.c` med `sed` — **17/17 grønne**:

| Enhet | Hva som ble verifisert |
|---|---|
| `NetBackoffMs` | skjema 3/6/12/24/48/60/60/60 s, jitter innenfor ±12,5 %, aldri over taket, `failures` 8–40 uten overflow |
| `AnimStep` | konvergens med snap, ingen oversving, `dt == 2 × dt/2` (rammeratefri), `dt` klemt til 100 ms, `dt = 0` er no-op, ~90 % av veien på 130 ms |

Backoff målt mot en faktisk blokkert linje (`api.binance.com` blokkert, først
via hosts, så via brannmurregel mot den oppslåtte IP-en):

| Feil nr. | Ventetid målt | Forventet ±12,5 % | `hConnect` |
|---|---|---|---|
| 1 | 6 491 ms | 5 250–6 750 | beholdt |
| 2 | 10 770 ms | 10 500–13 500 | beholdt |
| 3 | 21 470 ms | 21 000–27 000 | **sluppet** |
| 4 | 52 541 ms | 42 000–54 000 | ny |
| 5 | 52 617 ms | 52 500–60 000 (klemt) | ny |
| 6 | 59 264 ms | 52 500–60 000 | ny |
| 7 | 60 000 ms | tak | ny |

Ved taket er jitteren ensidig — klemmingen kutter alt over 60 000 ms. Ved
gjenopprettet linje: `feil=0` og 3 s-kadens tilbake på første vellykkede kall.

**Animasjonsklokka i hvile.** Egen teller på `WM_TIMER` logget sammen med
nettverkssyklusene:

| Tilstand | Tikk |
|---|---|
| Panel åpnet, alt satt seg | 14, deretter flat |
| Panel åpent + frakoblet | 624 → 1 527 (~35 tikk/s, driver sekundtelleren) |
| **Panel skjult + frakoblet, 54 s** | **2 124 → 2 124 (null bevegelse)** |
| Linje gjenopprettet | 2 124, fortsatt flat |

- **Fotavtrykk etter del A:** GDI 30 før panelet åpnes, 31 etter — samme nivå som
  fase 1. USER 14. Private bytes 3,98–4,04 MB. CPU i hvile 312 ms per 30 s
  (~1 %), som er hentingen hvert 3. sekund, ikke klokka.
- **`/W4` rent, x86** (PE-maskintype `0x14C`, verifisert på den bygde exe-en).

---

## Kjente begrensninger

- **Første gang panelet åpnes** vises «Laster data fra Binance...» i ~300 ms til
  tråden har hentet. Alle senere åpninger har data fra bufferet umiddelbart.
- **Ingen synlig indikator på at panelet er festet.** Regelen: dratt eller skalert
  = festet (auto-skjul av). Lukkes med kryss, ESC eller tray-klikk.
- **ESC krever fokus.** Er panelet festet og du har vært i et annet vindu, må du
  klikke panelet først.
- **Størrelsen huskes mellom åpninger, posisjonen ikke.**
- **`hoverIdx` og `panAnchorView` er absolutte indekser.** Når bufferet når 1440
  (~19 t åpent) og eldste faller ut, henger de ett lys etter til neste
  musebevegelse. Bundet sjekket, så ingen krasj.
- **Over $999 999** klippes ikonteksten (4 sifre får ikke plass på 16 px).
  Trygt — opptegningen er bundet sjekket.
- **Historikken er «hva panelet har sett».** Nyåpnet: 5 timer. Åpent en
  arbeidsdag: opp mot 24.

---

## Avviste forslag, med begrunnelse

- **Sirkulær buffer:** det finnes ingen dynamiske reallokeringer å fjerne.
  Gevinsten ville vært én `memmove` på 57 KB i minuttet (~5 µs) mot
  modulo-aritmetikk i all indeksering.
- **Begrensning til synlig utsnitt:** allerede på plass siden zoom-arbeidet.
- **WebView2 + Lightweight Charts:** ville brutt målet om lavt fotavtrykk med
  50–100× (Edge-subprosesser bruker 100–200 MB mot våre 3,3 MB).

---

## Fallgruver for agenter

1. **Stopp `ticker.exe` før du linker.** Ellers `LNK1104`.
2. **Funksjonsrekkefølge.** Filen har ingen forward-deklarasjoner. Nye
   hjelpefunksjoner må stå *før* første bruk, ellers `C2371: redefinition`.
   Dette har slått til tre ganger.
3. **Kun ASCII i kommentarer.** `æøå` gir `C4819` og brøt en heredoc under arbeidet.
4. **`lParam` i `WM_MOUSEWHEEL` er skjermkoordinater**, ikke klientkoordinater
   som i `WM_MOUSEMOVE`. `ScreenToClient` er nødvendig.
5. **Syntetiske museklikk er upålitelige for testing.** `PostMessage(WM_MOUSEMOVE)`
   trigger `WM_MOUSELEAVE` med én gang fordi den ekte pekeren er utenfor. Flytt
   den ekte pekeren og *jiggle* den (flere bevegelser) før du måler.
6. **Tray-klikk via `PostMessage` gir ikke forgrunnsrett.** Panelet kan åpne og
   straks skjule seg i testoppsett. Sjekk `IsWindowVisible` og prøv på nytt i løkke.
7. **Ikke stol på øyemål for de subtile kontrollene.** Mål pikselfarger med
   `GetPixel` — jeg konkluderte feil to ganger på nedskalerte skjermbilder.
8. **`WaitForMultipleObjects` returnerer mer enn to ting.** `WAIT_TIMEOUT`
   (`0x102`) er ikke `WAIT_OBJECT_0 + n`. Sjekk den faktiske returverdien;
   kode som bare tester for stopp-hendelsen og lar resten falle gjennom,
   behandler hver eneste timeout som en vekking. Se feil #9.
9. **Blokkering via hosts-fila stopper ikke en åpen forbindelse.** WinHTTP
   holder på `hConnect` og keep-alive mot en IP som allerede er slått opp, så
   hentingen fortsetter å lykkes. Skal du bryte linja på en app som *allerede
   kjører*, må du blokkere IP-en i brannmuren. Hosts-fila virker bare hvis du
   starter appen etterpå.
10. **Én navngitt mutex, ett vindusklassenavn.** Skal du kjøre et
    instrumentert testbygg side om side med den ekte appen, må du endre både
    mutexnavnet og vindusklassen — ellers avslutter testbygget seg selv, eller
    `FindWindow` treffer feil prosess.

---

## Sikkerhetskopier

`ticker.c.bak` … `ticker.c.bak6` ligger i mappa, ett per større endring.
De eldste kan trygt slettes.
