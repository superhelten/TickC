# Ticker fase 2 — design

Dato: 2026-09-16. Bygger på fase 1 slik den er dokumentert i `ARBEIDSLOGG.md`.
Utgangspunkt: commit `f7d3997`.

## Formål

Fase 1 flyttet nettverket ut på en arbeidertråd og fikk opptegningen ned i
1,255 ms. Hovedtråden har dermed ledig kapasitet. Fase 2 bruker den til fire
ting: myk visuell respons, feiltoleranse mot ustabile linjer, valg av symbol
og intervall i kjøretid, og visuell kontekst i selve grafflaten.

Målet er å løfte grensesnittet fra en ren prisgraf til et helhetlig, stilrent
finansielt verktøy med direkte kontekst — uten å gi fra seg farten.

Rammene fra fase 1 står: én fil, ingen eksterne avhengigheter utover Win32 og
WinHTTP, ingen `malloc`, rent bygg på `/W4`, x86, ~3,3 MB fotavtrykk.

---

## Arkitektur

### Trådkontrakten er uendret

Fase 2 legger ikke til tråder og flytter ikke ansvar mellom de to som finnes.
Reglene fra fase 1 gjelder uendret:

- UI-tråden rører aldri WinHTTP.
- `PostMessage` skjer aldri inne i låsen.
- Arbeidertråden henter uten lås og låser kun rundt flettingen.

Låsedomenet **utvides** med: `symIdx`, `ivIdx`, `intervalMs`, `configGen`,
`lastOkTick`, `netFailures`, `evictedTotal`.

### Ny tilstandsdeling: mål vs. visning

Det bærende grepet i fase 2. `viewStart` og `viewCount` (int, låsebeskyttet)
forblir **målet** og eies fortsatt av begge tråder som i dag. Ved siden av
kommer fire **rene UI-doubler** som ingen annen tråd rører:

| Felt | Betydning |
|---|---|
| `dispStart` | animert posisjon, kan være brøk |
| `dispCount` | animert bredde, kan være brøk |
| `dispMin` | animert nedre priskant |
| `dispMax` | animert øvre priskant |

Arbeidertråden ser aldri disp-feltene. Den skriver mål; UI-tråden eases mot
det. Dette er grunnen til at animasjonen ikke berører trådkontrakten i det
hele tatt.

---

## Del A — animasjonsklokka, backoff og stale-indikator

### A1. Animasjonsklokka

`TIMER_FADE_ID` erstattes av `TIMER_ANIM_ID`. Én timer på 16 ms driver alt
tidsavhengig: chrome-fade, view-easing, Y-akse-easing, overlay-fade og
stale-telleren.

**Tidsbasert, ikke stegbasert.** `SetTimer(16)` fyrer i praksis hver ~15,6 ms
og slås sammen under last. Fast steglengde per tikk gir derfor ulik hastighet
avhengig av systembelastning. Hver tikk måler faktisk forløpt tid med
`GetTickCount64()` mot `lastAnimTick`.

Interpolasjon, per animert verdi:

```
d += (maal - d) * (1.0 - exp(-dt / TAU));
if (fabs(maal - d) < snapTerskel) d = maal;
```

`TAU = 90.0` ms. `dt` klemmes til maks 100 ms, slik at en lang pause (låst
skjerm, kraftig last) gir ett hopp i stedet for en tilsynelatende frossen
animasjon som så spretter.

Timeren startes av enhver tilstandsendring som gjør noe usettet, og **drepes**
når chrome, view, Y-akse og overlay alle har satt seg *og* forbindelsen er
frisk. I hvile går det ingen timer — fotavtrykket i ro er uendret.

`FADE_STEP` utgår. Chrome-faden bruker samme eksponentielle form med kortere
tau, og beholder dagens opplevde varighet på ~130 ms.

### A2. Eksponentiell backoff

I dag: arbeidertråden forkaster feil stille med `return` og venter alltid
`TIMER_INTERVAL` (3000 ms).

```
netFailures = 0 ved suksess og ved hWakeEvent
ventetid    = min(3000 * 2^netFailures, 60000) justert med +/- 12,5 % jitter
```

Jitteren hindrer at mange klienter synkroniserer seg mot serveren etter et
felles avbrudd. Kilde: `GetTickCount64()`-lavbiter — ingen `rand()`, ingen
seeding, ingen global tilstand.

Taket på 60 s er valgt slik at en lang nedetid ikke gir minuttlange hull i
gjenopptakelsen, samtidig som vi ikke hamrer på en død linje.

**Etter tre sammenhengende feil** lukkes `hConnect` og settes til NULL.
`HttpGet` oppretter den på nytt ved neste kall, og DNS slås dermed opp på
nytt. Uten dette henger vi fast på en IP som ikke lenger svarer.

`hWakeEvent` (panelet ble åpnet) nullstiller alltid backoffen — brukeren som
åpner panelet skal få et forsøk umiddelbart, ikke vente ut et minutt.

### A3. Stale-tilstand

`lastOkTick` (`ULONGLONG`, `GetTickCount64`) settes under lås ved hver
vellykket henting.

```
stale = (naa - lastOkTick) > 3 * TIMER_INTERVAL     // 9 s = to tapte sykluser
```

Terskelen på tre sykluser, ikke én, gjør at en enkelt treg forespørsel ikke
blinker indikatoren.

### A4. Synliggjøring

Bevisst dempet — brukeren skal kunne se det, ikke bli avbrutt av det.

| Sted | I ro | Frakoblet |
|---|---|---|
| Header-pris | `CLR_TEXT` | `CLR_DIM` |
| Undertittel | `BTC/USDT  -  1m` | `BTC/USDT  -  1m  -  frakoblet 42s` |
| Tray-tips | `BTC/USDT: $75872.21` | `BTC/USDT: $75872.21 (frakoblet)` |
| Tray-ikon | fulle siffer | dempede siffer |
| Tomt buffer | `Laster data fra Binance...` | `Ingen forbindelse - prover igjen om Ns` |

Den siste raden retter en reell feil: i dag står det «Laster data fra
Binance...» i all evighet dersom linja er nede ved første åpning. Meldingen
lyver om tilstanden.

`RenderMicroFontIcon` får en fargeparameter for det dempede ikonet.

Sekundtelleren krever at animasjonsklokka holdes i live mens vi er frakoblet.
Den tegner om **bare når sifferet faktisk endrer seg** — ikke 60 ganger i
sekundet.

---

## Del B — symbol, intervall, overlay, vannmerke og persistens

### B1. Kuraterte tabeller

```c
typedef struct { const wchar_t* api; const wchar_t* label; } SymbolDef;
typedef struct { const wchar_t* api; const wchar_t* label; long long ms; } IntervalDef;
```

Symboler: `BTCUSDT`, `ETHUSDT`, `SOLUSDT`, `BNBUSDT`.
Intervaller: `1m`, `5m`, `15m`, `1h`, `4h`, `1d` — merket `1m 5m 15m 1t 4t 1d`.

Kuratert, ikke fritekst. Fast liste betyr at vi kjenner prisområdet og kan
formatere ikon, header og prisakse riktig uten å gjette, og at ingen henting
kan feile på et ukjent symbol.

### B2. configGen — kappløpet som må løses

**Scenariet:** tråden er midt i en henting for BTC. Brukeren bytter til ETH.
UI-tråden tømmer bufferet. BTC-svaret kommer tilbake og flettes inn i et
buffer som nå tilhører ETH. Grafen viser BTC-priser under ETH-etikett.

Dette er den mest alvorlige feilmuligheten i hele fase 2 — den gir stille,
feil data i stedet for en synlig krasj.

**Løsning:** en `configGen`-teller.

```
UI-tråden ved bytte (under lås):
    symIdx/ivIdx = nytt
    intervalMs   = tabelloppslag
    configGen++
    candleCount = 0; viewStart = 0; viewCount = 0
    followLive = TRUE; lastPrice = 0
  → SetEvent(hWakeEvent)

Arbeidertråden:
    under lås:  gen = configGen; sym = symIdx; iv = ivIdx
    uten lås:   bygg URL fra (sym, iv); HttpGet; ParseKlines
    under lås:  if (configGen != gen) forkast; else MergeCandles
```

Forkastingen skjer **ved fletting**, ikke ved henting — svaret kan ankomme
når som helst underveis.

### B3. intervalMs sprer seg

`KLINE_MS` er en konstant i dag og brukes tre steder. Alle blir
`ctx->intervalMs`:

1. Hull-sjekken i `MergeCandles` (`2 * KLINE_MS`)
2. Seed-sjekken i `WorkerFetchKlines` (`5 * KLINE_MS`)
3. Spenn-formateringen i headeren — `%dm`/`%dt` antar 1m-lys

Spenn-formateringen generaliseres: totalt antall minutter er
`vc * intervalMs / 60000`, formatert som minutter, timer eller døgn.

### B4. Adaptivt tray-ikon

Ikonet antar BTC-skala i dag: `price / 1000.0` gir `75.8`. SOL på $150 ville
gitt `0.2` — ubrukelig.

Divisor (1, 1000, 1 000 000) og antall desimaler velges slik at
`IconTextWidth() <= 16`. Dette er nøyaktig teknikken loggen beskriver under
feil #5: mål bredden på den **ferdig formaterte strengen**, ikke på en
terskel.

### B5. Overlayet

Tegnes **inne i popup-vinduets klientflate**. Ingen nytt HWND.

Dette er det viktigste arkitekturvalget i del B. Uten et nytt vindu finnes
det ingen aktiveringsendring, og vi går helt utenom territoriet der feil #1 og
#2 levde. En `TrackPopupMenu` ville sendt `WA_INACTIVE` til panelet og
trigget auto-skjul mens menyen sto åpen.

**Layout og treffdeteksjon deler én funksjon.** `OverlayLayout()` fyller et
array av rektangler; både tegning og museklikk kaller den. Samme disiplin som
`ChartGeometry` allerede følger, av nøyaktig samme grunn — loggen er tydelig
på at to uavhengige utregninger av samme flate ender med å peke forskjellige
steder.

Interaksjon:

| Handling | Virkning |
|---|---|
| Høyreklikk i chart-flaten | åpner overlayet |
| Klikk på valg | velger, lukker, trigger henting |
| Klikk utenfor | lukker uten endring |
| Hover | framhever raden |
| ESC | lukker overlayet **før** det lukker panelet |

Mens overlayet er åpent er chart-interaksjon (pan, zoom, crosshair) sperret.

Fade inn og ut via animasjonsklokka, med samme `Blend()` som resten av
chromet. Paletten er `CLR_BG`/`CLR_BOX`/`CLR_BOXEDGE` — identisk med
hover-boksen.

### B6. Dynamisk bakgrunnsvannmerke

Symbol og intervall preges inn i bakgrunnen med stor, ren typografi i en
fargetone som ligger ~3 % over bakgrunnen. Uttrykket er hentet fra
profesjonelle finansterminaler: teksten skal leses som en del av flaten, ikke
som et lag oppå den.

```c
#define CLR_WATERMARK  RGB(0x15, 0x19, 0x1F)   // #0D1117 + ~3 %
```

`#0D1117` er (13, 17, 23); vannmerket er (21, 25, 31). Differansen på 8 nivåer
er ~3,1 % av full skala — synlig nok til å lese, svakt nok til at
stearinlysene og rutenettet beholder all kontrast.

**Innhold:** aktivt symbol som hovedlinje (`BTCUSDT`), intervallet under
(`5m`). Begge leses fra samme `symIdx`/`ivIdx` som overlayet og headeren, så
de kan aldri komme i utakt.

**Plassering i tegnerekkefølgen:** umiddelbart etter at bakgrunnen er satt,
før rutenett, stearinlys og akser. Grafen flyter dermed rent over teksten uten
overlapping eller visuell støy.

**Ytelse — cachet som bitmap, ikke tegnet på nytt per bilde.** En `DrawTextW`
med stor font er ikke gratis; målt koster den typisk 0,05–0,30 ms, ikke
0,005 ms. Løsningen er å slå sammen bakgrunn og vannmerke i én cachet
`HBITMAP`:

```
Ved (W, H, symIdx, ivIdx)-endring:
    render bakgrunn + vannmerke inn i wmBmp

Per bilde:
    BitBlt(wmBmp) i stedet for FillRect(brBg)
```

Dette **erstatter** dagens `FillRect` — det legger altså ikke til et steg, det
bytter ut ett. Nettokostnaden er en `BitBlt` på ~380×300 mot en `FillRect` av
samme flate, som er innenfor målestøy. Samme disiplin som GDI-cachen fra
fase 1: bygg når inndata endrer seg, ikke per bilde.

Cachen invalideres av `WM_SIZE`, av konfigbytte, og ved oppstart.

**GDI-regnskap:** +1 `HBITMAP`, +1 `HFONT`. Fase 1 endte på 31 håndtak; fase 2
lander på ~33. Tallet skal være konstant etter oppstart — det er testen.

### B7. Persistens

`HKCU\Software\Ticker`, `REG_DWORD`: `SymbolIndex`, `IntervalIndex`,
`PanelWidth`, `PanelHeight`.

Skrives ved endring og ved avslutning. Leses ved oppstart, med
**bundet-sjekk** på indeksene — et registret som er redigert for hånd eller
etterlatt av en nyere versjon med flere symboler skal ikke kunne indeksere
utenfor tabellen.

Feiler lesningen, faller vi tilbake på BTC/USDT 1m. Registret er aldri en
forutsetning for at appen starter.

Ved oppstart fra registret settes `symIdx`/`ivIdx` **før** arbeidertråden
startes, slik at første henting går mot riktig par og vannmerket er korrekt
fra første bilde.

---

## Del C — view- og Y-akse-easing

### C1. Hva som eases, og hva som ikke gjør det

| Handling | Oppførsel |
|---|---|
| Hjul-panorering | eases |
| Ctrl + hjul (zoom) | eases |
| Symbol-/intervallbytte | ingen easing — bufferet er nytt |
| **Dra-panorering** | **følger musa direkte** |
| Y-akse ved nye data | eases |
| Indeksforskyvning ved utkasting | ingen easing |

Dra-panorering settes på både mål og visning samtidig. Eased dra føles treigt,
ikke mykt — fingeren og grafen må henge sammen.

### C2. Brøkdels-indekser i opptegningen

Dette — ikke easing-matematikken — er den reelle endringen i `DrawChart`.

Med `dispStart = 142.7` er det halve lys i begge kanter. Tegneløkka går fra
`floor(dispStart)` til `ceil(dispStart + dispCount)`, med

```
x = left + ((double)i - dispStart) / dispCount * cw
```

og `IntersectClipRect` mot chart-flaten, slik at kantlysene ikke blør ut i
prisaksen eller headeren. Klippregionen gjenopprettes før chromet tegnes.

Begge løkkene (kropper og veker) går fortsatt over synlige lys, ikke over hele
historikken — ytelseskarakteristikken fra fase 1 er bevart.

### C3. HitCandle må lese det samme

`DrawChart` og `HitCandle` må **begge** lese disp-verdiene. Leser den ene
målet og den andre visningen, peker crosshairet på feil lys midt i
animasjonen. Det er feil #7 fra loggen i ny drakt.

### C4. Indeksforskyvning ved utkasting

`MergeCandles` teller opp `evictedTotal` med antall lys som faller ut i front
når bufferet er fullt. UI-tråden holder sin egen `dispEvictedSeen`, og ved
hver opptegning:

```
delta = evictedTotal - dispEvictedSeen
dispStart     -= delta      // ingen easing
hoverIdx      -= delta
panAnchorView -= delta
dispEvictedSeen = evictedTotal
```

Uten dette hopper grafen ett lys til venstre hvert minutt så snart bufferet
har nådd 1440.

Dette rydder samtidig opp i den dokumenterte begrensningen om at `hoverIdx` og
`panAnchorView` henger ett lys etter til neste musebevegelse.

---

## Feilhåndtering

| Situasjon | Oppførsel |
|---|---|
| Nettverksfeil | backoff, stale-indikator, forrige data blir stående |
| Nettet nede ved første åpning | `Ingen forbindelse - prover igjen om Ns` |
| Svar ankommer etter konfigbytte | forkastes via `configGen` |
| Ugyldig registerinnhold | bundet-sjekk, fall tilbake på BTC/USDT 1m |
| Registret utilgjengelig | ignoreres, appen starter normalt |
| Vannmerke-bitmap feiler | hopp over, fall tilbake på `FillRect` |
| Buffer fullt (1440) | eldste faller ut, indekser forskyves synkront |
| Overlay åpent ved konfigbytte | lukkes, chart-interaksjon gjenopptas |

**Aldri:** krasj, heng, eller data vist under feil etikett.

---

## Testing

Teknikken fra fase 1 gjelder: trekk funksjonen ut av `ticker.c` med `sed` inn
i en liten harness, slik at testene kjører mot **den faktiske koden**, ikke en
kopi.

Enhetstestbart uten Win32:

| Enhet | Hva som verifiseres |
|---|---|
| Backoff-skjema | 3s, 6s, 12s, 24s, 48s, 60s, 60s; jitter innenfor ±12,5 %; nullstilling |
| Easing-steg | konvergens, snap, klemming av `dt`, ingen oversving |
| `configGen`-forkasting | svar fra forrige konfig flettes aldri inn |
| Adaptiv ikonformatering | alle fire symbolers prisområde gir `IconTextWidth() <= 16` |
| Spenn-formatering | alle seks intervaller × representative `vc` |
| Overlay-layout | tegnerektangler og trefferektangler er identiske |
| Indeksforskyvning | `dispStart`/`hoverIdx` følger `evictedTotal` eksakt |
| `MergeCandles` | de seks eksisterende tilfellene, nå med variabel `intervalMs` |

Empirisk, per delleveranse:

- `/W4` rent, x86.
- GDI- og USER-håndtak flate gjennom pan, zoom, overlay-åpning og konfigbytte.
  Forventet nivå etter fase 2: ~33, konstant.
- Opptegning holder seg under 1,3 ms. Easing legger ~60 opptegninger i
  sekundet **i bevegelse**, så taket per bilde er det som avgjør.
- Vannmerket måles isolert: opptegning med og uten, samme utsnitt, samme
  vindusstørrelse. Påstanden som skal etterprøves er at den cachede `BitBlt`
  ikke er dyrere enn dagens `FillRect`.
- Overlay- og vannmerkefarger måles med `GetPixel`, ikke med øyemål. Loggen
  dokumenterer at øyemål på nedskalerte skjermbilder har gitt feil konklusjon
  to ganger. Vannmerket på ~3 % kontrast er nøyaktig det tilfellet der øyemål
  ikke kan brukes.
- Backoff verifiseres ved å blokkere `api.binance.com` og logge faktiske
  ventetider.

---

## Rekkefølge

Tre leveranser, hver med eget bygg, egen måling og egen commit.

**A.** Animasjonsklokke + backoff + stale-indikator — minst, ingen avhengigheter
**B.** Symbol/intervall + overlay + vannmerke + registret — bygger på klokka for fade
**C.** View- og Y-akse-easing — sist, rører opptegningen mest

Vannmerket hører til B fordi det leser samme `symIdx`/`ivIdx` som overlayet og
lastes fra samme registeroppslag. Å bygge det separat ville betydd å innføre
den tilstanden to ganger.

---

## Fallgruver som gjelder alt arbeid i denne fila

Fra `ARBEIDSLOGG.md`, gjentatt fordi de har slått til før:

1. **Stopp `ticker.exe` før du linker.** Ellers `LNK1104`.
2. **Ingen forward-deklarasjoner i fila.** Nye hjelpefunksjoner må stå *før*
   første bruk. Dette har slått til tre ganger.
3. **Kun ASCII i C-kommentarer.** `æøå` gir `C4819`. Unicode i `L""`-strenger
   er greit.
4. **`lParam` i `WM_MOUSEWHEEL` er skjermkoordinater**, ikke klient.
5. **Syntetiske museklikk er upålitelige for testing.** Flytt den ekte
   pekeren og jiggle den.
6. **`PostMessage` aldri inne i låsen.**
