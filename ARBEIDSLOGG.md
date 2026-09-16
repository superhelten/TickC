# BTC Ticker — arbeidslogg

Status per 16.09.2026. Skrevet for agenter som jobber videre på `ticker.c`.
Fase 1 er ferdig. Fase 2 del A (animasjonsklokke, backoff, stale-indikator)
del B (symbol/intervall, overlay, vannmerke, registret) og del C (siste-pris-
indikator, skalert vannmerke, view- og Y-akse-easing) er ferdige. **Hele fase 2
er levert.** Design:
`docs/superpowers/specs/2026-09-16-ticker-fase2-design.md`. Planer med
«Avvik under utførelse»:
`docs/superpowers/plans/2026-09-16-ticker-fase2-del-b.md` og `...-del-c.md`.

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

### Runtime-konfig: symbol og intervall

`SYMBOLS[]` og `INTERVALS[]` er kuraterte tabeller. Kuratert, ikke fritekst:
en fast liste betyr at vi kjenner prisområdet, og at ingen henting kan feile
på et ukjent symbol. `KLINE_MS` finnes ikke lenger — alle tre bruksstedene
leser `ctx->intervalMs`.

**`configGen` er det som hindrer stille feil data.** Scenariet: tråden er midt
i en henting for BTC, brukeren bytter til ETH, UI-tråden tømmer bufferet, og
BTC-svaret kommer tilbake og flettes inn i et buffer som nå tilhører ETH.

Arbeidertråden tar en kopi av `configGen` før hentingen og sammenligner **ved
fletting**, ikke ved henting — svaret kan ankomme når som helst underveis.
Sjekken står i **begge** hentefunksjonene. Prisen har nøyaktig samme kappløp og
styrer tray-ikonet; designet nevnte bare lysene.

> Et forkastet svar returnerer `TRUE`. Det er ikke en nettverksfeil, og skal
> ikke telle opp backoffen hver gang brukeren bytter symbol.

`ApplyConfigChoice` teller opp `configGen` og tømmer bufferet i **samme
kritiske seksjon** som byttet.

**Låsen dekker i tillegg:** `symIdx`, `ivIdx`, `intervalMs`, `configGen`.

### Overlayet

Tegnes inne i panelets klientflate. **Ingen nytt HWND** — uten et nytt vindu
finnes det ingen aktiveringsendring, og vi går helt utenom territoriet der
feil #1 og #2 levde.

`OverlayLayout()` fyller ett array som **både** tegning og treffdeteksjon
leser. Samme disiplin som `ChartGeometry`, av samme grunn (feil #7).

Kalles fra `PaintPopup`, ikke fra `DrawChart` — `DrawChart` returnerer tidlig
når bufferet er tomt, som er nøyaktig tilstanden rett etter et konfigbytte.

Treffdeteksjonen henger på `overlayOpen` (logisk tilstand), aldri på
`overlayF` (fade-nivå). Under uttoning er boksen fortsatt synlig, men klikk
skal gå til grafen igjen.

### Vannmerket

Bakgrunn og vannmerke bakes sammen i én cachet `HBITMAP` som **erstatter**
`FillRect`. Cachen invalideres av `WM_SIZE` og av konfigbytte, og river ned
det gamle før den bygger nytt. Feiler bitmapen, faller `DrawChart` tilbake på
`FillRect` — vannmerket er pynt og skal aldri hindre opptegning.

### Mål vs. visning — det bærende grepet i del C

`viewStart`/`viewCount` (int, låsebeskyttet) er **målet** og eies som før av
begge tråder. Ved siden av ligger fire **rene UI-doubler** som ingen annen
tråd rører:

| Felt | Betydning |
|---|---|
| `dispStart` | animert posisjon, kan være brøk |
| `dispCount` | animert bredde, kan være brøk |
| `dispMin` / `dispMax` | animert priskant |

Arbeidertråden skriver mål; UI-tråden eases mot det. **Derfor berører hele
easingen ikke trådkontrakten** — `disp*` skal aldri inn i låsedomenet.

**`DrawChart` og `HitCandle` må begge lese `disp*`.** Leser den ene målet og
den andre visningen, peker crosshairet på feil lys midt i animasjonen. Det er
feil #7 i ny drakt, og det er den ene regelen som ikke kan bøyes.

Tegneløkka går fra `floor(dispStart)` til `ceil(dispStart + dispCount)` med
`IntersectClipRect` mot chart-flaten, så kantlysene ikke blør ut i prisaksen.
Klippingen gjenopprettes før aksetekstene og chromet tegnes. Løkka går
fortsatt over **synlige** lys — `i1 - i0` er `dispCount + 1`, ikke
`candleCount`.

**Snapping er en kvart piksel**, omregnet til den enheten som eases ved hver
tikk — ikke et fast tall i lys eller dollar. Se målingene for hvorfor.

**Hva som eases og ikke:**

| Handling | Oppførsel |
|---|---|
| Hjul-panorering, Ctrl+hjul | eases |
| **Dra-panorering** | **X følger musa direkte**, Y eases |
| Y-akse ved nye data | eases |
| Symbol-/intervallbytte, panelåpning, buffer-reset | snapper (`dispValid = FALSE`) |
| Indeksforskyvning ved utkasting | snapper (`ApplyEviction`) |

`SyncDisp` markerer seg **ikke** som gyldig når bufferet er tomt. Gjorde den
det, sto aksen på `[0, 1]` gjennom et symbolbytte og gled opp til det ekte
spennet når dataene kom — se feil #15.

### Siste-pris-indikatoren

Stiplet linje fra siste lys til høyre kant, med et fylt, fargekodet stempel på
prisaksen. Tegnes etter lysene og **før** den tidlige returen i
crosshair-blokka, ellers ville den forsvunnet så snart musa var utenfor.

Fargen følger `dP = P_t - P_t-1` — siste lukkekurs mot den forrige. Det er en
annen regel enn lysenes egen (`close` mot `open` i *samme* lys), så de kan
peke hver sin vei. Tilsiktet: linja svarer på «hvor står vi mot forrige
lukking».

Er prisen utenfor synlig område tegnes ingenting. Et stempel klemt mot kanten
ville plassert prisen et sted den ikke er.

### Vannmerkets fontstørrelse

`klemt(chart-høyde / 5, 32, 120)`, og deretter **tilpasset bredden**: teksten
måles med `GetTextExtentPoint32W` og høyden skaleres ned i samme forhold hvis
den ikke får plass. Uten breddetilpasningen sto `BTCUSDT` klippet på smale
paneler.

DPI-skaleringen gjelder **klemmegrensene**, ikke `H/5`. `g.ch` er allerede
enhetspiksler, så den proporsjonale delen skalerer seg selv; ganger man `H/5`
med DPI også, teller man skaleringen to ganger.

Bygges i `EnsureWatermark`, som per definisjon bare kjører når
`(W, H, symIdx, ivIdx)` endrer seg. Kostnaden per bilde er null.

### Registret

`HKCU\Software\Ticker`, `REG_DWORD`: `SymbolIndex`, `IntervalIndex`,
`PanelWidth`, `PanelHeight`. Leses i `WinMain` **før `CreateThread`**, slik at
første henting går mot riktig par. Indeksene er bundet sjekket. Enhver feilsti
lander på BTC/USDT 1m.

Panelstørrelsen fanges i `WM_EXITSIZEMOVE`, ikke ved avslutning — se feil #11.

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
| `SYMBOL_COUNT` | 4 | BTC, ETH, SOL, BNB |
| `INTERVAL_COUNT` | 6 | 1m, 5m, 15m, 1t, 4t, 1d |
| `CLR_WATERMARK` | `#15191F` | `CLR_BG` + 8 nivåer = ~3,1 % |
| `OVL_ROW_H` / `OVL_COL_W` | 22 / 104 | overlay-rad og kolonnebredde |
| `ANIM_TAU_VIEW` | 70,0 | tidskonstant view-easing (ms) |
| `SNAP_PX` | 0,25 | snapp når det gjenstår under en kvart piksel |
| `WM_FONT_DIV` / `MIN` / `MAX` | 5 / 32 / 120 | vannmerkets fonthøyde |

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
11. **Runtime-valg av symbol og intervall**, overlay, vannmerke og registret
    (fase 2 B).
12. **Siste-pris-indikator, skalert vannmerke, view- og Y-akse-easing**
    (fase 2 C).

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


**11. Panelstørrelsen ble aldri lagret.** `SaveConfig` sto i `WM_DESTROY` og
leste `GetWindowRect(hPopup)` der. Panelet **eies** av hovedvinduet og er
allerede revet ned når `WM_DESTROY` når dit, så `GetWindowRect` hadde
ingenting å lese og `PanelWidth` ble aldri skrevet. Størrelsen fanges nå i
`WM_EXITSIZEMOVE` — når brukeren slipper.

> Resonnementet i den første versjonen var «les fra vinduet selv, så kan de to
> aldri komme i utakt». Riktig i prinsippet, feil i praksis: vinduet fantes
> ikke lenger. Registret sto tomt; det var det som avslørte den.

**12. Prisaksen antok BTC-skala.** `"%.0f"` på alle fem etikettene. SOL rundt
97 dollar har et spenn under én dollar, så alle fem leste `97`. Desimalene
velges nå fra *avstanden mellom etikettene* (`PriceDecimals`). Samme klasse
feil som #5: formatet må følge tallet som faktisk skal vises.

**13. `OverlayLayout` var ikke ren.** Radene bak `count` var stack-søppel, så
to kall med samme inndata ga ulikt innhold. Ufarlig i dag — `OverlayHit` går
bare til `count` — men enhetstesten «samme inn = samme ut» feilet, og det er
en klasse feil verdt å lukke. Structen nullstilles nå.


**14. Tray-ikonet ble hengende på forrige symbol.** `ApplyConfigChoice`
nullstiller `lastPrice`, og `UpdateIcon` returnerer tidlig på `price <= 0.0`.
Står panelet åpent henter tråden **bare lys** — og lysgrenen skrev aldri
`lastPrice`. Etter et symbolbytte ble ikonet *og* verktøytipset derfor
stående på forrige symbols pris og etikett så lenge panelet var åpent.

> Målt: 15 s etter bytte til SOL leste ikonet fortsatt `75.9` — BTC — mens
> panelet viste SOL. Nøyaktig den klassen designet forbyr: data under feil
> etikett. Alle symbolbyttene i testingen ble gjort med panelet åpent og
> uten å se på ikonet, så ingen av de tidligere målingene fanget den.
> `WorkerFetchKlines` setter nå `lastPrice` fra det siste lysets `close`.


**15. Y-aksen ville glidd opp fra null ved hvert symbolbytte.** `SyncDisp`
markerte seg som gyldig også med tomt buffer, og satte da `dispMin`/`dispMax`
til `[0, 1]`. Rett etter et bytte er bufferet nettopp tomt, så aksen ble
stående *gyldig* på `[0, 1]` — og når de nye lysene kom, eases den opp til det
ekte spennet. Altså en prisakse som glir opp fra null i et halvt sekund ved
hvert bytte, som er nøyaktig det `SyncDisp` finnes for å hindre.

> Fanget før det nådde brukeren, men først etter at koden var skrevet og
> commitet. Målingen som avslørte det: logg `dispMin`/`dispMax` per bilde og
> se på overgangen `candleCount` 0 → 300. Med tomt buffer blir vi nå stående
> *ugyldige*, så første bilde med data snapper.

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

### Fase 2 del B

Enhetstester: **24/24** i `test_b`, pluss del A sine **17/17** — begge kjørt
mot kode trukket ut av gjeldende `ticker.c`.

| Enhet | Hva som ble verifisert |
|---|---|
| `FormatSpan` | alle seks intervaller × representative `vc`, inkludert `vc = 0` og døgnovergang |
| `FormatIconPrice` | femten prisområder fra $0,85 til $12,5 M gir alle `IconTextWidth() <= 16` |
| `OverlayLayout` | ren funksjon, ingen overlapp, alt innenfor panelet, hjørner og midtpunkt treffer riktig rad, holder på minimumsstørrelse |
| `PriceDecimals` | fem etiketter er alltid innbyrdes forskjellige, seks spennklasser |

**Kappløpet (`configGen`).** Målt på et instrumentert bygg som logger hver
forkasting, med 80–100 raske symbolbytter:

| Gren | Forkastinger | Eksempel |
|---|---|---|
| Lys | 30 | `gen=1 naa=3` — to bytter rakk å skje under én henting |
| Pris | 56 | `gen=1 naa=2 pris=2407.47` — en **ETH**-pris som ankom etter byttet |

Prisgrenen måtte tvinges fram i testbygget (tråden henter lys så lenge panelet
er åpent). Loggen er det konkrete beviset for feilen designet ville sluppet
gjennom: uten sjekken hadde `lastPrice = 2407.47` blitt skrevet og tray-ikonet
vist ETH-prisen under et annet symbol.

**Vannmerket, målt med `GetPixel` — ikke med øyet:**

| Farge | Hva | Antall piksler |
|---|---|---|
| `#0D1117` | ren bakgrunn | 2481 |
| `#15191F` | vannmerket | 318 |

Nøyaktig `CLR_WATERMARK`. 8 nivåers differanse = 3,1 % av full skala.

**Opptegning, `BitBlt` mot `FillRect`.** Vekselvis annethvert bilde — samme
data, samme vindu, samme utsnitt, 306 par:

| | Median | Snitt |
|---|---|---|
| `BitBlt` (med vannmerke) | 0,341 ms | 0,355 ms |
| `FillRect` (uten) | 0,312 ms | 0,332 ms |

Parvis differanse **+0,0226 ms**, 95 % KI `[0,0062, 0,0390]`, t = 2,70.

> Designet påsto at den cachede `BitBlt` «ikke er dyrere enn dagens
> `FillRect`». Det stemmer ikke — den er målbart dyrere. Differansen er
> reell, men liten: 0,023 ms av en opptegning på 0,34 ms, mot 0,05–0,30 ms
> for å tegne teksten på nytt hvert bilde. Cachen er fortsatt riktig valg;
> påstanden var for sterk.

**Håndtak.** GDI 33 gjennom 30 overlay-åpninger og 20 resizer; 35 etter at
vannmerket er bygget (+1 `HBITMAP`, +1 `HFONT`, som designet forutsa), og
deretter flat gjennom 16 symbolbytter. USER 14 i ro — 15 mens
animasjonsklokka går, fordi **en timer er et USER-objekt**.

**Registret, tur-retur på ekte kjøring.** Valgt SOL + 520×380, avsluttet,
startet på nytt: tray-ikonet leste `97.5` **før panelet ble åpnet** — altså
gikk første henting til SOL. Panelet åpnet på 520×380 med vannmerket
`SOLUSDT` / `1m`.

| Feilsti | Resultat |
|---|---|
| `SymbolIndex=99`, `IntervalIndex=0x7FFFFFFF` | BTC/USDT 1m, ingen krasj |
| `SymbolIndex` som `REG_SZ` | BTC/USDT 1m, ingen krasj |
| Ingen nøkkel | BTC/USDT 1m, ingen krasj |

### Fase 2 del C

Enhetstester: **55/55** totalt (17 del A + 24 del B + 14 del C), alle kjørt mot
kode trukket ut av gjeldende `ticker.c`.

**Hvorfor snappet er en kvart piksel og ikke et fast tall.** Målt i harnessen:

| Terskel | Panorering 300 lys | SOL-spenn (1,5 $) | BTC-spenn (3000 $) |
|---|---|---|---|
| Fast 0,01 lys | 46 tikk (736 ms) | — | — |
| Fast 0,5 dollar | — | 5 tikk | 39 tikk |
| **Kvart piksel** | **33 tikk (528 ms)** | **32 tikk** | **32 tikk** |

Den faste terskelen i pris er ikke bare treg, den er ubrukelig: 0,5 dollar er
en tredel av SOLs hele spenn og under en tusendel av BTCs. `tau` er 70, ikke
110 som planen foreslo — 110 ga en hale på over et sekund.

**Crosshair mot tegning, midt i animasjonen.** Instrumentert bygg som logger
hvilket lys `HitCandle` mente, og hvilket tegneløkkas egen formel gir for
samme X:

| | |
|---|---|
| Samsvar | **163 av 163, 0 avvik** |
| Herav bilder midt i animasjonen | **43** (`dCount` 9,912 → 9,864 → … → 8,000) |

**Opptegning under animasjon**, drevet med hjul-panorering:

| | |
|---|---|
| Median | 0,462 ms |
| p95 | 0,637 ms |
| Maks | 0,864 ms |
| **Bilder over 1,3 ms** | **0 av 581** |
| Bilder med brøkdels-`dispStart` | 415 av 581 |

**Siste-pris-indikatoren**, målt med og uten blokka annethvert bilde,
351 par:

| | Median | Snitt |
|---|---|---|
| Med | 0,2713 ms | 0,2828 ms |
| Uten | 0,2489 ms | 0,2628 ms |

Parvis **+0,0195 ms**, 95 % KI `[0,0134, 0,0255]`, t = 6,28. Spesifikasjonen
anslo «under 0,001 ms» — det faktiske tallet er rundt tjue ganger høyere, og
det meste er `DrawTextW` for stempelteksten. Fortsatt uproblematisk.

Stempelet målt med `GetPixel`: 16 px høyt i eksakt `CLR_DOWN` `#FF4966`,
teksten med kjerne i eksakt `CLR_BG` `#0D1117` og 13 % dekning.

**Symbolbytte og Y-aksen.** Bytte SOL → BNB: `candleCount` går 302 → 0 → 300,
og **første bilde med data har allerede riktig akse** (713,46–714,77). Ingen
mellomverdier.

**Zoom-ankring etter easing:** samme lys (indeks 297) under pekeren før og
etter fem hakk pluss settling.

**Håndtak:** GDI **35**, USER 14. Del C legger ikke til GDI-objekter; C1 gjorde
det (to stiplede penner, 33 → 35), og vannmerkefonten bygges om ved hver
størrelsesendring uten å lekke — verifisert flat gjennom 20 resizer.

---

## Kjente begrensninger

- **Første gang panelet åpnes** vises «Laster data fra Binance...» i ~300 ms til
  tråden har hentet. Alle senere åpninger har data fra bufferet umiddelbart.
- **Ingen synlig indikator på at panelet er festet.** Regelen: dratt eller skalert
  = festet (auto-skjul av). Lukkes med kryss, ESC eller tray-klikk.
- **ESC krever fokus.** Er panelet festet og du har vært i et annet vindu, må du
  klikke panelet først.
- **Størrelsen overlever omstart** (registret); posisjonen gjør det ikke.
- **Tray-ikonets skala er implisitt.** SOL på $150 og BTC på $150 000 tegnes
  begge som `150`. Fonten har ingen `k`-glyf — fase 1 valgte bevisst `75.8`
  framfor `75k` — og verktøytipset bærer det eksakte tallet.
- **`staleSecsShown` nullstilles ikke ved gjenopprettet forbindelse.** En ny
  frakobling kan hoppe over én opptegning dersom sekundtallet tilfeldigvis er
  det samme. Kosmetisk, ett bilde. Ryddes når del C uansett rører `WM_TIMER`.
- **Animasjonsklokka går i korte støt når panelet står åpent.** Er panelet
  lukket går det ingen timer i det hele tatt. Med panelet åpent starter hver
  datahenting klokka på nytt, fordi det levende lyset kan flytte Y-målet:
  målt **23 tikk på 30 sekunder**, mot 1800 om den hadde gått kontinuerlig.
  Den dør altså mellom hentingene — dette er ikke en lekkasje.
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
11. **Panelet auto-skjuler seg midt i en måling.** Åpnet via `PostMessage`
    får det aldri forgrunnsrett, og `WA_INACTIVE` lukker det så snart
    fokus flytter seg — også mellom to PowerShell-kall. Bruk et testbygg som
    setter `pinned = TRUE` ved visning. Les alltid `GetWindowRect` **rett
    før** du fanger skjermbildet; vinduet kan ha flyttet eller endret
    størrelse siden sist.
12. **Treffdeteksjon må henge på logisk tilstand, ikke på fade-nivå.** Under
    uttoning er overlayet fortsatt synlig. Sjekker du `overlayF > 0`,
    svelger boksen klikk den ikke lenger eier.
13. **Sperrer i `WM_MOUSEMOVE` hører hjemme etter `TrackMouseEvent`.**
    Returnerer du før armeringen, slutter `WM_MOUSELEAVE` å fyre og
    `windowHot` henger fast på `TRUE`.
14. **Tegning og treffdeteksjon må lese samme kilde.** `DrawChart` og
    `HitCandle` leser begge `disp*`. Leser den ene målet og den andre
    visningen, peker crosshairet på feil lys midt i en animasjon — feil #7 i
    ny drakt. Gjelder alt som regner om mellom piksler og lysindekser.
15. **Gjenopprett klippingen.** `IntersectClipRect` rundt tegneløkka må
    følges av `SelectClipRgn(hdc, NULL)` før aksetekster og chrome tegnes —
    de ligger utenfor chart-flaten og ville blitt borte.
16. **En terskel i «enheter» virker ikke på tvers av symboler.** Fire symboler
    med tre størrelsesordener mellom seg gjør ethvert fast tall i dollar
    meningsløst. Regn om fra piksler i stedet, ved hver tikk.
17. **Hold øye med tomt buffer i all ny tilstand.** Rett etter et
    symbolbytte er `candleCount = 0`. Tilstand som «synkroniserer seg» da,
    synkroniserer seg mot ingenting — se feil #15.

---

## Sikkerhetskopier

`ticker.c.bak` … `ticker.c.bak6` ligger i mappa, ett per større endring.
De eldste kan trygt slettes.
