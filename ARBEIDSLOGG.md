# BTC Ticker — arbeidslogg

Status per 19.09.2026. Skrevet for agenter som jobber videre på `ticker.c`.
Fase 1 er ferdig. Fase 2 del A (animasjonsklokke, backoff, stale-indikator)
del B (symbol/intervall, overlay, vannmerke, registret) og del C (siste-pris-
indikator, skalert vannmerke, view- og Y-akse-easing) er ferdige. **Hele fase 2
er levert.** Deretter er panelet flyttet fra rammeløst popup til et vanlig
OS-vindu, og så, i **fase 4**, tilbake til rammeløst — denne gangen med egne
kontrollknapper og nativ `HTCAPTION`-flytting. **Fase 5** la til
tilstandsavhengig gjenopprettingsglyf, inkrementell hover-opptegning og
firevegspeker under panorering. **Fase 6** strammet klippingen av grafen til
`rcChart` og flyttet gjenopprettingsglyfen til 2 px forskyvning. **Fase 7**
måler headerteksten og hever minstestørrelsen til 400×250. **Fase 8** bytter
`[ ↺ ]` mot `[ + ]`, som starter en ny instans, flytter nullstilling av zoom og
panorering til dobbeltklikk, `R` og `ESC`, og fjerner single-instance-mutexen.
**Fase 9** legger til `--desktop-mode`, der grafflaten ligger i skrivebordet
bak ikonene som barn av WorkerW.
**Fase 10** lar dobbeltbufferet leve mellom bildene: 5,1 ms mot 11,7 ms ved
3840×1600, og 1,33 ms mot 1,59 ms ved 1280×720, med samme piksler.
**Fase 11** deler flaten i graf, priskolonne og tidsbånd. Den legger til
tidsakse, egen monospace-aksefont (11 px sifre) i #A0AAB8 og et vannmerke med
alfa som følger vindusbredden.
**Fokus-blink** fjerner den klassiske rammen som `DefWindowProc` tegnet oppå
grafen ved hvert fokusbytte og tittelbytte. **Fase 12** bytter mellom panel og
skrivebordsmodus fra tray-menyen mens prosessen kjører, og husker valget i
registret. **Fase 13** legger «Start ved pålogging» i tray-menyen, med
`Ticker` i `HKCU\…\Run`. **Fase 14** gjør skrivebordsflaten tekstfri og kant
til kant: bare kurve, rutenett og vannmerke. **Fase 15** gir lysene 10 px luft
mot prisaksen og lar den stiplede siste-pris-linja bygge bro over den.
**Fase 16** setter ett skalert pris-stempel på skrivebordets høyre kant —
flatens eneste tekst. **Fase 17** gir tray-menyen undermenyene «Symbol» og
«Intervall», så skrivebordsmodus kan bytte uten å gå veien om panelet.
**Fase 18** henter eldre lys når brukeren panorerer inn i veggen: bufferet
fylles bakover til historikkens start eller til 6000 lys, uten at bildet
flytter seg.
**Fase 19** gir kontrollknappene tastatursnarveier — `Ctrl`+`N` for `[ + ]`,
`Ctrl`+`M`, `F11` og `Ctrl`+`W` — gjennom samme sti som klikkene, og
nullstiller `staleSecsShown` når forbindelsen er tilbake.
**Fase 20** gjør grafen tastaturstyrt — piltaster, `PgUp`/`PgDn`,
`Home`/`End` og `+`/`-` gjennom samme `PanView`/`ZoomView` som hjulet — og
slipper panoreringen når et annet vindu tar capture (`WM_CAPTURECHANGED`).
**Fase 21** legger volumstolper i de nederste 22 % av grafflaten, bak
lysene, med skala som eases som prisaksen, og en `V`-rad i hover-boksen.
**Fase 22** gjør headerens rad 2 til en verktøylinje: symbolpille (åpner
overlayet), en pille per intervall og en `VOL`-bryter som eases, med `V` og
`1`…`6` fra tastaturet og «Volumstolper» i tray-menyen.
**Fase 23** gir prisvarsler: et klikk i priskolonnen setter en rav linje på
den prisen, et klikk på merket fjerner den, og når prisen når nivået fyrer
varselet én gang med etterglød, ballong og lyd — også med panelet lukket.
Testbygget fikk **skrivende** probe-felt som injiserer en pris.
**Fase 24** gjør inndataene sunne (parserne forkaster nan, inf, 0 og usunne
lys) og vekker tråden etter dvale.
**Fase 25** legger glidende snitt over lysene — SMA 20 og EMA 50, regnet av
en stegmaskin uten egen tabell — med forklaring i grafens hjørne og en
`MA`-bryter i verktøylinja, på `M` og i tray-menyen.
**Fase 26** gir skrivebordsflaten egne overleggsvalg — volum og snitt er av
der som standard, etter tilbakemelding fra bruk — og legger flaten på nytt
ved `WM_DISPLAYCHANGE`.
**Fase 27** («Bloomberg Essentials») legger dagens session over lysene: VWAP
per UTC-døgn som en gyllen linje, dagens høy og lav som stiplede linjer med
dempede aksemerker, og SMA, EMA og VWAP som rader i hover-boksen — alt regnet
av `candles[]` under opptegning og alt bak indikatorbryteren, så
skrivebordet er urørt. Bufferet bakfyller seg selv til døgnskiftet.
Se **Vinduet** under. Planer:
`docs/superpowers/plans/2026-09-16-ticker-rammelost-vindu.md`,
`docs/superpowers/plans/2026-09-16-ticker-glyf-hover-cursor.md`,
`docs/superpowers/plans/2026-09-16-ticker-ny-instans.md`,
`docs/superpowers/plans/2026-09-17-ticker-skrivebordsmodus.md`,
`docs/superpowers/plans/2026-09-17-ticker-opptegning.md`,
`docs/superpowers/plans/2026-09-17-ticker-akser.md`,
`docs/superpowers/plans/2026-09-17-ticker-fokus-blink.md`,
`docs/superpowers/plans/2026-09-17-ticker-modusveksling.md` og
`docs/superpowers/plans/2026-09-17-ticker-autostart.md` og
`docs/superpowers/plans/2026-09-17-ticker-omgivelsesmodus.md` og
`docs/superpowers/plans/2026-09-17-ticker-prislinje-offset.md` og
`docs/superpowers/plans/2026-09-17-ticker-skrivebordsstempel.md` og
`docs/superpowers/plans/2026-09-18-ticker-tray-symbol-intervall.md` og
`docs/superpowers/plans/2026-09-18-ticker-historikk.md` og
`docs/superpowers/plans/2026-09-18-ticker-tastatursnarveier.md` og
`docs/superpowers/plans/2026-09-18-ticker-tastaturnavigasjon.md` og
`docs/superpowers/plans/2026-09-18-ticker-volum.md` og
`docs/superpowers/plans/2026-09-18-ticker-verktoylinje.md` og
`docs/superpowers/plans/2026-09-18-ticker-prisvarsler.md` og
`docs/superpowers/plans/2026-09-18-ticker-robuste-inndata.md` og
`docs/superpowers/plans/2026-09-19-ticker-indikatorer.md` og
`docs/superpowers/plans/2026-09-19-ticker-skrivebordsflate.md` og
`docs/superpowers/plans/2026-09-19-ticker-bloomberg-essentials.md`. Design:
`docs/superpowers/specs/2026-09-16-ticker-fase2-design.md`. Planer med
«Avvik under utførelse»:
`docs/superpowers/plans/2026-09-16-ticker-fase2-del-b.md` og `...-del-c.md`.

All kode ligger i **én fil**, `ticker.c` (~6290 linjer). Ved siden av ligger
`ticker.manifest`, som bygget bygger inn (fase 9). Ingen eksterne avhengigheter
utover Win32 og WinHTTP.

---

## Bygg

```
"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
cl /nologo /W4 /O2 ticker.c /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:ticker.manifest /OUT:ticker.exe
```

**Manifestet er ikke valgfritt** (fase 9). Uten `supportedOS` Windows 8+ blir
skrivebordsmodusens flate usynlig. Bygget lykkes likevel, og vanlig modus ser
lik ut, så feilen merkes ikke før `--desktop-mode` kjøres.

Bygger **rent på `/W4`** — hold det sånn. Målarkitektur er **x86** (matcher
den opprinnelige exe-en). Prosessen kan kjøre i flere instanser (fase 8), så
**stopp alle kjørende instanser før linking** — også duplikater startet med
`[ + ]` — ellers feiler
`LNK1104: cannot open file 'ticker.exe'`.

Fotavtrykk: ~3,5 MB private bytes, 199 KB exe etter fase 27 (203 776 byte;
199 680 etter fase 26, 199 168 etter fase 25, 195 584 etter fase 24;
195 072 etter fase 23, 187 KB etter fase 22, ~164 KB i fase 9). (Panelet er 1280×720 nå, mot
380×300 i fase 1 — dobbeltbufferet er 8× større.)

**`ticker.c` er også gyldig C++** (målt i fase 24): `cl /TP /W4 /O2` gir
0 feil, 0 advarsler og en exe på byte-identisk størrelse. Fila bygges fortsatt
som C — et språkbytte alene gir ingenting — men døra står åpen den dagen en
funksjon faktisk trenger en container. Se *Avviste forslag* for hva STL og
nlohmann/json koster.

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

- `candles[6000]` — fast statisk array, 288 KB (48 byte per lys med volum,
  fase 21; 240 KB til og med fase 20). Fylles framover mens panelet
  står åpent og bakover på forespørsel (fase 18). **Ingen malloc noe sted.**
- Første henting seeder 300 lys (`limit=300`, ~50 KB). Deretter `limit=3` (~500 byte).
- `MergeCandles()` fletter på `openTime`: samme tidsstempel **oppdaterer**
  (lyset under forming endrer seg), nyere **legges til**, eldste faller ut ved taket.
- Oppdages et tidshull (> 2 lysintervaller) nullstilles bufferet, slik at grafen
  ikke tegner en sammenhengende kurve over dødtid.
- **Alt fra nettet går gjennom en sunnhetssjekk** (fase 24): `PriceSane`
  (`0 < v < 1e15`, usant for NaN) og `CandleSane` (OHLC sunne, `high ≥ low`,
  open og close innenfor, volum `≥ 0`, `openTime > 0`). `ParseKlines` hopper
  over usunne lys og lys hvis tid ikke er strengt stigende, og beholder
  resten. `candles[]` og `lastPrice` holder derfor aldri NaN, inf eller 0 fra
  et svar.

### Tegning

Dobbeltbuffret gjennom en minne-DC, `WM_ERASEBKGND` returnerer 1.
Begge tegneloopene går over **`vc` (synlige lys)**, ikke hele historikken —
tegnearbeidet er derfor uavhengig av hvor mye historikk som er lagret.

GDI-objekter er bufret: elleve faste farger lages ved oppstart (ni til og
med fase 20, pluss de to stolpepenslene fra fase 21), de blandede
fade-fargene bare når `(chrome, closeHot)` endrer seg.

### Vinduet

Panelet er et **rammeløst vindu med egne kontrollknapper**:
`WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX`. Hele den
ikke-klientaktige rammen fjernes i `WM_NCCALCSIZE`, så klientflaten er
nøyaktig like stor som vindusrektangelet — målt 1280×720 mot 1280×720.
Knapp i oppgavelinja, ikke alltid øverst.

> **Historikk, to omganger.** Fase 1 var rammeløs med et hover-chrome som
> tonet inn. `c77b6eb` rev alt og gikk over til `WS_OVERLAPPEDWINDOW` med
> OS-tittellinje. Denne omgangen går tilbake til rammeløst, men *ikke* til
> fase 1-designet: knappene er alltid synlige, uten fade, og uten auto-skjul
> på fokustap. Se «Fjernet i fase 3».

#### Alt henger på `WM_NCHITTEST`

Uten OS-ramme er det vi som avgjør hva musa står på. Dette er det bærende
grepet — knapper, flytting og skalering faller alle ut av én funksjon:

| Sone | Retur | Hvem handler |
|---|---|---|
| < 6 px fra en kant (`RESIZE_BORDER`) | `HTLEFT` … `HTBOTTOMRIGHT` | `DefWindowProc` skalerer |
| Knappeboks i headeren | `HTCLIENT` | vi, i `WM_LBUTTONDOWN` |
| Ellers i headeren (`y < 44`) | `HTCAPTION` | `DefWindowProc` flytter |
| Resten | `HTCLIENT` | vi |

**`HTCAPTION` er hele flyttemekanismen.** `DefWindowProc` genererer selv
`WM_NCLBUTTONDOWN`/`HTCAPTION` og kjører OS-ets dra-løkke — vi skriver ingen
dra-kode, og får Aero Snap, dobbeltklikk-maksimering og `Win`+piltast gratis.
Alt tre er målt.

> **Rekkefølgen inne i headeren er ikke kosmetikk.** Et `HTCAPTION`-område får
> *aldri* `WM_LBUTTONDOWN`. Returnerer vi `HTCAPTION` for hele headeren, er
> knappene tegnet, men døde — og et klikk på krysset starter en
> vindusflytting. Se fallgruve 21.

Kantene skaleres **ikke** når vinduet er maksimert: der ville et klikk 2 px
fra skjermkanten startet en dra-skalering av noe som per definisjon fyller
skjermen.

#### Maksimering må oppgis manuelt

Et `WS_POPUP`-vindu maksimerer seg til hele **skjermen**, ikke til
arbeidsområdet — og OS-et legger rammebredden utenpå. Målt før `WM_GETMINMAXINFO`
fikk grensene: **−7,−7 3854×1614** mot `rcWork` **0,0 3840×1552**. Panelet
dekket oppgavelinja med 55 px, og krysset lå 7 px utenfor skjermkanten.

`WM_GETMINMAXINFO` setter derfor `ptMaxPosition` og `ptMaxSize` fra `rcWork`
selv. `ptMaxPosition` er relativ til *skjermens* hjørne, ikke til skrivebordet.
Etterpå treffer maksimert vindu `rcWork` eksakt.

> **To grener er uverifisert, begge fordi maskinen har én skjerm.** Har du
> flere, tar disse to sjekkene et minutt til sammen:
>
> 1. **`ptMaxPosition` på sekundærskjerm.** `rcWork` starter i `0,0` her, så
>    subtraksjonen `rcWork − rcMonitor` var null og ble aldri satt på prøve.
>    Dra panelet til den andre skjermen og trykk `□`. Forventet: den
>    skjermens `rcWork` eksakt.
> 2. **Vernet mot frakoblet skjerm.** Faller den gjenopprettede rekta utenfor
>    alle tilkoblede skjermer, skal `❐`-knappen sentrere vinduet i stedet for
>    å gjenopprette det ut i intet. Maksimer på den andre skjermen, koble den
>    fra, og trykk `❐`. Forventet: 1280×720 sentrert på den som er igjen.

> `ptMaxTrackSize` settes **ikke**. Den ville klemt *manuell* skalering til én
> skjerms arbeidsområde, så panelet ikke lenger kunne strekkes over to
> skjermer. Det er maksimert størrelse som skal følge `rcWork`, ikke største
> tillatte størrelse.

`DWMWA_WINDOW_CORNER_PREFERENCE = DONOTROUND`: Windows 11 runder hjørnene på
`WS_THICKFRAME`-vinduer også når rammen er fjernet, og radien klipper krysset.

#### Kontrollknappene

Fire knapper oppe til høyre, 26×18 px hver, 2 px mellomrom, 8 px fra høyre
kant. `ButtonLayout(W, out[4])` er **eneste sannhetskilde** — tegning,
`WM_NCHITTEST`, hover og klikk leser alle den (fallgruve 14).

| Knapp | Handling |
|---|---|
| `+` | `SpawnInstance` — ny prosess med samme symbol, intervall og størrelse, +30, +30 px (fase 8) |
| `–` | `ShowWindow(SW_MINIMIZE)` |
| `□` / `❑` | `SW_MAXIMIZE` / `SW_RESTORE` etter `IsZoomed`. **Glyfen folger tilstanden:** maksimert vindu viser to overlappende rektangler |
| `×` | `WM_CLOSE` → skjuler til systemstatusfeltet |

**Gjenopprettingsglyfen tegnes som to rektangler, ikke fire streker.** Det
bakre er en *apen* polylinje med fem punkter — kun de kantene som ikke ligger
bak det fremre — saa vi slipper aa fylle det fremre ugjennomsiktig for aa
skjule overlappet. To GDI-kall mot ett for `□`.

`zoomed` sendes inn i `DrawButtons` og caches **ikke** i `AppContext`:
tilstanden eies av OS-et, og en kopi ville vaert enda en ting som kan komme ut
av synk. `WM_SIZE` invaliderer hele vinduet ved maksimering, saa glyfen byttes
av seg selv.

**Gjenoppretting verner om geometrien.** Etter `SW_RESTORE` sjekkes at den
gjenopprettede rekta fortsatt treffer en tilkoblet skjerm (`PlacementIsVisible`
paa `GetWindowPlacement`-resultatet); ellers `ResetToDefaultView`. Samme sjekk
som `PlacePopupInitially` gjor ved apning. `SaveWindowPlacement` kalles kun i
maksimer-grenen — ved gjenoppretting er geometrien allerede lagret, og et kall
der ville lagret den maksimerte.

**Rene GDI-vektorer, ingen font.** En `DrawTextW` med et Unicode-tegn koster
langt mer enn fire `LineTo`, og ville vært avhengig av at fonten *har* glyfen
— samme problem som tray-ikonets manglende `k`.

**Ingen fade.** Fargen skifter momentant på hover, og treffet henger på
`btnHot`, ikke på noe fade-nivå (fallgruve 12). Hvile: glyf `#6E7681` på
panelbakgrunn, ingen knappebakgrunn. Hover: `#161D27` bak `+ – □`, `#C02A3E`
bak `×`, med hvit glyf.

**Tegnes fra `PaintPopup`, ikke fra `DrawChart`.** `DrawChart` returnerer
tidlig når bufferet er tomt — altså mens det står «Laster data fra
Binance...» og under hele en frakobling. Lå tegningen der, forsvant krysset
nettopp når man vil lukke panelet. Samme grunn som `DrawOverlay` ligger der.

**Headerteksten krymper med `BTN_STRIP_W`** (118 px), ellers lå den
høyrestilte prosenten rett under krysset.

#### Verktøylinja (fase 22)

Headerens rad 2 (y 28–42) er en verktøylinje der symbollinja sto som ren
tekst: `[BTC/USDT ▾]  [1m][5m][15m][1t][4t][1d]  [VOL]`, venstrestilt fra
`PAD_L`. `ToolbarLayout(W, out[TBAR_COUNT])` er **eneste sannhetskilde**,
som `ButtonLayout`: tegning, `WM_NCHITTEST`, hover og klikk leser den.
Bredder er **faste konstanter**, ikke målt tekst — `WM_NCHITTEST` har ingen
DC. En `C_ASSERT` holder hele linja innenfor `HeaderRow2Limit` ved
`POPUP_MIN_W`; under det skjules piller fra høyre, hele, aldri halve.

| Pille | Handling |
|---|---|
| symbol `▾` | åpner overlayet (det samme som høyreklikk i grafen); aktiv mens det står åpent |
| `1m` … `1d` | `ApplyConfigChoice` — samme sti som overlayet og tray-menyen; klikk på den aktive er en no-op |
| `VOL` | `SetShowVolume` — **ikke** `ApplyConfigChoice`: bufferet skal ikke tømmes for et tegnevalg |
| `MA` | `SetShowIndicators` (fase 25) — samme form. Eneste pille utenfor minstebredden: skjult under 426 px, `M` og tray-menyen virker uansett |

Pillene er `HTCLIENT`, mellomrommene og resten av headeren `HTCAPTION`
(fallgruve 21). `tbHot` følger `btnHot`s regler: satt etter
`TrackMouseEvent`-armeringen, sperret med overlayet åpent og under
panorering, nullstilt i `WM_MOUSELEAVE`. Ingen fade. Hvile er dempet tekst,
hover `CLR_BOX`-flate, aktiv `CLR_BOX` med `CLR_BOXEDGE`-ramme. Ingen nye
GDI-objekter. **Tegnes fra `PaintPopup`**, som knappene: rett etter et bytte
er bufferet tomt, og `DrawChart` returnerer tidlig.

`showVol` er valget (registret, `ShowVolume`); `dispVolF` ∈ [0, 1] er
visningen, eased i `WM_TIMER` som sjette verdi, uavhengig av om det finnes
lys. Stolpehøyden ganges med den; ved 1,0 er faktoren eksakt. Er flaten
ikke synlig når valget endres (tray-menyen med lukket panel), snapper den.

**Frakoblet-teksten** (`frakoblet Ns`) sto i symbollinja. Den tegnes nå til
høyre for siste pille, fra `DrawChart` (helsefeltene leses under låsen), og
bare når hele teksten får plass før `HeaderRow2Limit`.

#### Prisvarsler (fase 23)

Priskolonnen (`x > edge`, `y ∈ [top, bottom]`) er varslenes flate. Den var
en no-op for klikk til og med fase 22.

| Handling | Oppførsel |
|---|---|
| Peker i kolonnen, tom flate | hånd, **spøkelse**: dempet rav linje tvers over grafen og et *rammet* merke med den avrundede prisen. Grått når alle åtte plassene er brukt |
| Klikk på tom flate | `AlertAdd` på `AlertPriceAtY(y)` — varselet settes, skrives til registret |
| Peker på et merke | merket blir rødt som lukkeknappen (`alertHot`) — men ikke et *nysatt* merke før pekeren har forlatt det én gang (`alertFresh`) |
| Klikk på et merke | `AlertRemove` — nærmeste merke innenfor `[y − 8, y + 8)`, flaten som er tegnet |
| `A` med trådkors | varsel på trådkorsets pris; uten trådkors ingenting |
| «Fjern prisvarsler (N)» i tray-menyen | tømmer varslene for symbolet som vises; grått uten varsler |

**Siden lagres, ikke forrige pris.** `alerts[sym][i]` er en double med siden
i fortegnet: `+nivå` ble satt over prisen og fyrer når prisen er ≥, `−nivå`
under og fyrer når den er ≤. `AlertHit(now, signedLevel)` er en ren funksjon;
`now ≤ 0` fyrer aldri (rett etter et symbolbytte er `lastPrice` 0). Et nivå
som ble krysset mens appen sto av eller maskinen sov, fyrer derfor på første
pris etterpå, og et symbolbytte har ingen «forrige pris» å sammenlikne feil.

**Utløseren bor i `WM_APP_DATA`** (`CheckAlerts` etter `UpdateIcon`): det ene
stedet hver ny pris passerer på UI-tråden — panelet åpent, lukket og i
skrivebordsmodus. Alt varselrelatert er **UI-eid**; trådkontrakten er urørt.
Et varsel fyrer **én gang** og fjernes. Bare gjeldende symbols varsler
prøves — prisen vi har, er dets.

**Når det fyrer** (`FireAlert`): etterglød på nivået i full rav som toner ut
(`alertFlashF` 1 → 0, `ALERT_TAU_FLASH`, sjuende easede verdi, bare når
panelet er synlig), ballong fra tray-ikonet (`NIF_INFO`, `NIIF_NOSOUND`, fra
en **kopi** av `nid` — `UpdateIcon` eier originalen) og
`MessageBeep(MB_ICONASTERISK)`. Én lyd, og den kommer også når Windows holder
ballongen tilbake.

**Tegning og treff leser samme kilde** (fallgruve 14): `AlertY` og
`AlertPriceAtY` leser `dispMin`/`dispMax` med samme avkutting som lysene.
`AlertRound` runder til største tierpotens ≤ én piksel i pris, gulv 0,01
(fallgruve 16). Linjene tegnes **bak lysene**, over stolpene, innenfor
klippet; merkene på stempelets flate, og stempelet øverst. Rutenettetiketter
under 16 px fra et merke tegnes ikke. `DC_PEN`/`DC_BRUSH` — **ingen nye
GDI-objekter**. Skrivebordsmodus tegner linjene, ikke merkene.

**Dobbeltklikk-flaten er `[left, edge]`**, ikke `[left, W)` som til og med
fase 22: i kolonnen er et raskt dobbeltklikk sett + fjern (fallgruve 38).

#### Hurtigsti for hover-opptegning

Et hover-skifte invaliderer **kun knapperaden** (`ButtonStrip(W)`), ikke hele
vinduet. `PaintPopup` har én gren for det: ligger hele `ps.rcPaint` innenfor
stripa, bygges et 110×18-buffer og bare knappene tegnes — `DrawChart` og
`DrawOverlay` hoppes over.

> Uten grenen ville invalideringen ikke spart noe som helst utover den siste
> blitten. GDI klipper den, men hele 1280×720-bufferet ville fortsatt blitt
> bygget og grafen tegnet om. **Målt: 0,979 ms → 0,062 ms, altså 16×.**

Bakgrunnen i stripa hentes fra **vannmerkebitmapen**, ikke fra `FillRect`. Da
er den garantert identisk med det den trege stien ville lagt der, uten at vi
trenger å vite at vannmerketeksten aldri når opp i headeren. `SetViewportOrgEx`
lar `DrawButtons` fortsette å regne i vinduskoordinater, og nullstilles før
blitten — ellers ville kildepunktet `(0,0)` blitt tolket logisk.

To unntak, begge målt:

| Tilfelle | Hvorfor | Hva som skjer |
|---|---|---|
| Overlayet åpent | det dimmer **hele** klientflaten, headeren inkludert | betingelsen sjekker `!overlayOpen`, så vi tar den trege stien |
| Animasjonsklokka går | dens `InvalidateRect(NULL)` unionerer med stripa | `rcPaint` blir hele flaten, og vi faller til den trege stien av oss selv |

**Hurtigstien dekker ikke alle hover-overganger.** Knapp → knapp og knapp →
mellomrom går gjennom den. Knapp → *ledig headerflate* gjør det ikke: den går
via `WM_MOUSELEAVE`, som nullstiller `hoverIdx` og `overlayHot` i samme
melding — og de påvirker chart-flaten, så full opptegning er riktig der.
`InvalidateRect(NULL)` står altså igjen i `WM_MOUSELEAVE` med vilje.

#### Pekeren under panorering

`WM_SETCURSOR` er **delvis tilbake** — mot det «Fjernet i fase 3» sier. Den
griper inn når `panning` er sann **og** treffsonen er `HTCLIENT`, og setter
`IDC_SIZEALL`. Alt annet går videre med `break`, så `DefWindowProc` beholder
sine seks skaleringspekere i kantene og vanlig pil i headeren.

> `IDC_SIZEALL`, ikke `IDC_HAND`. Sistnevnte er lenkepekeren og betyr «dette
> kan klikkes», ikke «dette dras». Windows har ingen lukket-hånd blant
> standardpekerne.

`SetCursor` kalles også direkte ved panoreringsstart og -slutt: `WM_SETCURSOR`
fyrer først ved neste musebevegelse, så uten det viste første bilde av draget
fortsatt pil. Pekerne bufres i `AppContext`; `LoadCursorW` gir et **delt**
håndtak for standardpekere, så de telles ikke som våre og skal ikke gjennom
`DestroyCursor`.

#### Resten

| Handling | Oppførsel |
|---|---|
| «Avslutt Ticker» i tray-menyen | avslutter programmet |
| «Skrivebordsmodus» i tray-menyen | bytter mellom panel og skrivebordsflate; haken viser gjeldende modus, valget lagres (fase 12) |
| Tray-klikk | fremme og aktivt → skjul; ellers vis, gjenopprett og gi fokus |
| `Ctrl` + `0` / «Standardvisning» | sentrer 1280×720 på skjermen vinduet står på (grå i skrivebordsmodus) |
| Dobbeltklikk på grafen | nullstiller zoom og panorering (eases). Til og med fase 22 også på prisaksen; den er varslenes nå (fase 23) |
| `R` | nullstiller zoom og panorering (ikke mens overlayet er åpent) |
| `ESC` | lagvis: lukk overlayet → nullstill utsnittet → skjul til systemstatusfeltet (duplikat: avslutt) |
| Dobbeltklikk i ledig headerflate | maksimerer / gjenoppretter |
| `Win` + `↑` / `↓` / `←` | maksimer / gjenopprett / snap — virker uten `WS_SYSMENU` |
| `Ctrl` + `N` / `M` / `W`, `F11` | `[ + ]` / minimer / lukk / maksimer–gjenopprett — samme sti som knappene, sperret under panorering (fase 19) |
| `Alt` + `F4` | lukk (skjul) via `DefWindowProc` → `WM_CLOSE` — virker uten `WS_SYSMENU`, målt |
| `←` / `→` | ett hjulhakk bakover / framover (`vc / 8` lys), eases (fase 20) |
| `PgUp` / `PgDn` | et helt utsnitt bakover / framover (fase 20) |
| `Home` / `End` | eldste lys — veggen ber om historikk som et drag — / den levende kanten med `followLive` (fase 20) |
| `+` / `-` (også numerisk, også med `Ctrl`) | ett zoomtrinn inn / ut om **midten** av utsnittet (fase 20) |
| `V` | VOL-pillen: volumstolpene av/på, eased (fase 22) |
| `1` … `6` | intervallpillene i rekkefølge, 1m … 1d (fase 22) |
| «Volumstolper» i tray-menyen | bytter valget for **modusen prosessen står i**: panelets i panelmodus, skrivebordets i skrivebordsmodus (fase 22, per modus fra fase 26) |
| `M` / `MA`-pillen / «Indikatorer» i tray-menyen | SMA 20, EMA 50, VWAP og dagens høy/lav av/på, tonet (fase 25; VWAP og høy/lav fra fase 27, da tray-punktet byttet navn fra «Glidende snitt»). `Ctrl`+`M` er fortsatt minimer. Tray-punktet er per modus, som «Volumstolper» (fase 26) |
| Klikk i priskolonnen / `A` | setter eller fjerner et prisvarsel — se *Prisvarsler* over (fase 23) |
| «Fjern prisvarsler (N)» i tray-menyen | tømmer varslene for symbolet som vises (fase 23) |
| Tapt capture midt i et drag | `WM_CAPTURECHANGED` slipper panoreringen og setter pekeren tilbake (fase 20) |

**Standardvisningen er DPI-skalert:** `MulDiv(1280, GetDpiForWindow(hwnd), 96)`,
klemt til arbeidsområdet. Prosessen er fortsatt **DPI-uvitende**, så dette gir
nøyaktig 1280×720 i dag — men det blir riktig automatisk dersom DPI-bevissthet
senere skrus på. Se «Avviste forslag» for hvorfor den ikke er det.

**`UpdatePopupTitle` er beholdt** selv uten tittellinje: tittelen er det
oppgavelinja og `Alt`+`Tab` viser.

**`ForceForeground` er beholdt.** Feil #1 gjelder fortsatt: et tray-klikk gir
ikke prosessen forgrunnsrett, og uten dette får vinduet aldri tastaturfokus —
da når verken `ESC` eller `Ctrl`+`0` frem.

**Panelet eies ikke av hovedvinduet.** Eierskap ville fjernet knappen i
oppgavelinja, men det betyr også at Windows ikke river det ned for oss:
`WM_DESTROY` på hovedvinduet gjør det selv. Målt at knappen er der med panelet
og borte når det skjules — `WS_EX_APPWINDOW` er ikke nødvendig.

**Geometri i registret:** `PanelX`, `PanelY`, `PanelWidth`, `PanelHeight`,
pluss `PanelHasPos` som skiller «ikke lagret» fra «lagret som 0,0».
Koordinatene tolkes *signed* — en skjerm kan ligge til venstre for den
primære. Lagret posisjon brukes bare hvis den fortsatt treffer en tilkoblet
skjerm (`MonitorFromRect`), ellers sentreres vinduet. Lagringen går gjennom
`GetWindowPlacement`, så et minimert eller maksimert vindu ikke husker en
oppgavelinje-strimmel som «brukerens størrelse».

**`ResetToDefaultView` gjenoppretter både maksimert og minimert vindu først.**
Et minimert vindu er fortsatt `WS_VISIBLE`, så `IsWindowVisible` er `TRUE` og
tray-menyens «Standardvisning» hopper over `TogglePopup`; uten `IsIconic`-
sjekken satte `SetWindowPos` bare den gjenopprettede geometrien mens vinduet
ble stående minimert på −32000,−32000. Menypunktet gjorde altså ingenting
synlig. Feilen var der fra før, men minimer-knappen gjør stien lett å nå.

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

Fargen er hvit blandet inn med `WatermarkAlpha(W)` =
`clamp(0,08 · √(W/1920), 0,04, 0,10)` (fase 11). Siden `W` alt er en del av
cache-nøkkelen, koster alfaen ingenting per bilde.

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
| Indeksforskyvning ved utkasting og bakfylling | snapper (`ApplyFrontShift`) |

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
`PanelWidth`, `PanelHeight`, `PanelX`, `PanelY`, `PanelHasPos`,
`DesktopMode` (fase 12), `ShowVolume` (fase 22) og `ShowIndicators` (fase 25)
— de to siste standard 1 og **panelets** — og `ShowVolumeDesktop` og
`ShowIndicatorsDesktop` (fase 26), standard 0 og **skrivebordsflatens**. Alle
fire skrives av `SaveConfig` sammen med indeksene. Leses i `WinMain` **før `CreateThread`**, slik at
første henting går mot riktig par. Indeksene er bundet sjekket. Enhver feilsti
lander på BTC/USDT 1m.

Panelstørrelsen fanges i `WM_EXITSIZEMOVE`, ikke ved avslutning — se feil #11.

`DesktopMode` skrives i det brukeren velger i tray-menyen, ikke i `WM_DESTROY`,
som aldri kjører når prosessen drepes utenfra. Den leses bare når verken
`--desktop-mode` eller `--dup` er gitt. Flagget vinner for den kjøringen, og
et duplikat er alltid et panel og skriver aldri.

**Prisvarslene (fase 23)** er de eneste verdiene som ikke er `REG_DWORD`:
`Alerts_BTCUSDT`, `Alerts_ETHUSDT` … — én `REG_BINARY` per symbol, doubler
med siden i fortegnet. Navnet er API-symbolet, ikke indeksen, så en endret
symboltabell aldri flytter et varsel til et annet symbol. `SaveAlerts`
skriver i det et varsel settes, fjernes eller fyrer (som `DesktopMode`), og
sletter verdien med siste varsel. `LoadAlerts` forkaster enkeltvis: feil
type, lengde som ikke er et helt antall doubler, NaN, 0, ≥ 1e9 og alt over
åtte. Et duplikat verken leser eller skriver dem.

**Autostart (fase 13) ligger utenfor `Software\Ticker`:**
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, verdien `Ticker`,
`REG_SZ`, stien til exe-en i anførselstegn. Den leses hver gang tray-menyen
åpnes og skrives bare ved klikk. Appen leser den aldri ved oppstart. Nøkkelen
er makroen `AUTOSTART_KEY`, så testbygg bør peke den til en egen nøkkel.

---

## Sentrale konstanter

| Navn | Verdi | Betydning |
|---|---|---|
| `MAX_CANDLES` | 6000 | 4 dager på 1m, 16 år på 1d; var 1440 til fase 17 |
| `SEED_COUNT` / `DEFAULT_VIEW` | 360 / 300 | 6 t frø og bakfyllingsbolk / standard utsnitt. 60 lys oppvarming, så EMA 50 er definert fra venstre kant (fase 25; var 300 / 300) |
| `MIN_VIEW` | 8 | maks innzoom |
| `ZOOM_STEP` | 1.2 | per hjulhakk |
| `TIMER_INTERVAL` | 3000 | hentefrekvens (ms) |
| `KLINE_MS` | 60000 | ett 1m-lys |
| `REOPEN_GUARD_MS` | 250 | hindrer at lukkeklikk åpner igjen |
| `SHOW_GRACE_MS` | 400 | ignorer fokustap rett etter åpning |
| `s_httpBuf` | 98304 | ~1,6× margin mot ~60 KB svar (360 lys; var 1,94× mot 50,7 KB) |
| `ANIM_INTERVAL` | 16 | animasjonsklokke (~60 fps) |
| `ANIM_TAU_CHROME` | 55,0 | tidskonstant chrome-fade (ms) |
| `ANIM_DT_MAX` | 100,0 | klemmer `dt`, lang pause gir ett hopp |
| `NET_RETRY_MAX` | 60000 | tak for eksponentiell backoff (ms) |
| `NET_RECONNECT_AT` | 3 | antall feil før `hConnect` slippes |
| `STALE_AFTER` | 9000 | 3 × `TIMER_INTERVAL` = to tapte sykluser |
| `SYMBOL_COUNT` | 4 | BTC, ETH, SOL, BNB |
| `INTERVAL_COUNT` | 6 | 1m, 5m, 15m, 1t, 4t, 1d |
| `CLR_WM_INK` / `WM_ALPHA_*` | hvit / 0,08 · 0,04 · 0,10 | vannmerke, `WatermarkAlpha(W)`, nominelt ved 1920 px (fase 11; erstatter `CLR_WATERMARK` `#15191F`) |
| `CLR_AXIS` | `#A0AAB8` | pris- og tidsetiketter, 8,05:1 mot `CLR_BG` |
| `AXIS_Y_W` / `AXIS_PAD_R` / `PAD_R` | 76 / 8 / 84 | priskolonne (4 + 8 tegn × 9 px) / kantsikring / sum |
| `PAD_B` | 18 | tidsaksens bånd |
| `TIME_DX_MIN` / `TIME_LBL_GAP` | 80 / 12 | minste etikettavstand = max(80, bredde + 12) |
| `OVL_ROW_H` / `OVL_COL_W` | 22 / 104 | overlay-rad og kolonnebredde |
| `ANIM_TAU_VIEW` | 70,0 | tidskonstant view-easing (ms) |
| `SNAP_PX` | 0,25 | snapp når det gjenstår under en kvart piksel |
| `WM_FONT_DIV` / `MIN` / `MAX` | 5 / 32 / 120 | vannmerkets fonthøyde |
| `CHART_TOP_MIN` | 32 | minste `rcChart.top`, sjekket med `#error` |
| `POPUP_MIN_W` / `H` | 400 / 250 | minste størrelse, DPI-skalert i `WM_GETMINMAXINFO` |
| `HDR_GAP` | 8 | minste luft mellom headertekster og mot knapperaden |
| `VOL_FRAC` | 0,22 | volumstolpenes bånd, andel av grafflatens høyde (fase 21) |
| `CLR_VOL_UP` / `CLR_VOL_DOWN` | `#09542D` / `#51212D` | stolpefarger, `CLR_UP`/`CLR_DOWN` blandet ~28 % mot `CLR_BG` |
| `TBAR_TOP` / `TBAR_H` | 28 / 15 | verktøylinjas rad: y i [28, 43), under prisens grunnlinje og over grafflaten (fase 22) |
| `TBAR_SYM_W` / `TBAR_IV_W` / `TBAR_VOL_W` | 74 / 28 / 32 | faste pillebredder; sum med luft 300 px, slutt på x = 310 mot grensen 312 ved 400 px |
| `TBAR_GAP` / `TBAR_GROUP_GAP` | 2 / 8 | mellom intervallpiller / mellom gruppene |
| `TBAR_IND_W` | 26 | `MA`-pillen (fase 25), x i [312, 338): utenfor minstebredden med vilje, skjult under 426 px |
| `IND_SMA_PERIOD` / `IND_EMA_PERIOD` | 20 / 50 | glidende snitt på lukkekursen (fase 25). Prefiks `IND_`: `MA_*` tilhører `winuser.h` |
| `CLR_SMA` / `CLR_EMA` | `#3D8FBF` / `#A072D0` | dempet stålblå / dempet fiolett, 1 px over lysene. Finnes ikke ellers i flaten, så en probe kan telle dem |
| `IND_TAU_FADE` | 55,0 | `MA`-bryterens toning (= `ANIM_TAU_FADE`), snapp 0,02 |
| `IND_BATCH` | 1024 | punkter per `Polyline`; bufferet er volumstolpenes `s_volPts` |
| `ALERT_MAX` | 8 | prisvarsler per symbol, faste plasser, 256 byte i alt (fase 23) |
| `CLR_ALERT` / `CLR_ALERT_LINE` | `#FFB020` / `#86601B` | rav: merke og etterglød / linja over dataflaten, blandet halvveis mot `CLR_BG`. Ikke blant de elleve faste fargene, så en probe kan telle dem |
| `ALERT_HIT_PX` | 8 | halve merkehøyden: treffet er flaten som er tegnet |
| `ALERT_TAU_FLASH` | 900,0 | tidskonstant for ettergløden når et varsel fyrer (ms), snapp 0,02 |
| `ALERT_PRICE_MAX` | 1e9 | øvre grense for et nivå, vern mot et håndredigert register |

---

## Funksjoner, i den rekkefølgen de kom

1. **GDI-graf i popup** — candlesticks, rutenett, prisakse, header.
2. **Riktig tray-ikon** — font-tabellen var ødelagt fra start (se feil #3).
3. **Én linje i ikonet** — 4×9-font, `75.8` i stedet for `75k`/`778`.
4. **Crosshair + hover-boks** — tid og OHLC for lyset under musa.
5. **Flyttbart / skalerbart panel** — `WS_THICKFRAME` + `WM_NCCALCSIZE`.
6. **Hover-kontroller** — kryss, grip-prikker, ramme og resize-grip som toner
   inn ved hover og er usynlige i hvile. **Fjernet i `c77b6eb`** — erstattet av
   OS-rammen.
7. **Ctrl + hjul = zoom**, ankret mot musepekeren.
8. **Akkumulerende historikk + panorering** — hjul og dra.
9. **Arbeidertråd + GDI-cache.**
10. **Animasjonsklokke, eksponentiell backoff og stale-indikator** (fase 2 A).
11. **Runtime-valg av symbol og intervall**, overlay, vannmerke og registret
    (fase 2 B).
12. **Siste-pris-indikator, skalert vannmerke, view- og Y-akse-easing**
    (fase 2 C).
13. **Nativ vindusramme**, standardvisning (`Ctrl`+`0`) og posisjonspersistens.

### Fjernet i fase 3

Beskrivelsene over står fordi de forklarer *hvorfor* koden ble som den ble.
Dette er hva som ikke lenger finnes, og hvor det ble borte:

| Hva | Hvorfor | Commit |
|---|---|---|
| Hover-chrome: kryss, grip-prikker, resize-grip, ramme, fade | OS-rammen har alt sammen | `c77b6eb` |
| `EnsureChromeCache` og seks bufrede GDI-objekter | fulgte med chromet; GDI 35 → 29 | `c77b6eb` |
| Auto-skjul på fokustap, `pinned`, `SHOW_GRACE_MS`, `REOPEN_GUARD_MS` | et vindu med tittellinje som forsvinner når man klikker i et annet vindu er ubrukelig | `c77b6eb` |
| `inSizeMove` (feil #6) | brukt til dragrammens farge, som er borte | `c77b6eb` |
| `WM_SETCURSOR` | `DefWindowProc` gjør jobben igjen | `c77b6eb` |
| `CLR_WHITE`, `CLR_HDRHOT` | hadde bare chromet som bruker | `c77b6eb` |

Målingene av fade-fargene (`#0D1117 → #333D4B`) gjelder kode som ikke finnes
lenger. De står igjen som metode: *mål pikselfarger, ikke øyemål* er fortsatt
regelen — se fallgruve #7.

> **Tre av radene over kom tilbake i fase 4.** `WM_NCCALCSIZE` og
> `WM_NCHITTEST` er nødvendige igjen så snart OS-rammen er borte, og
> `CLR_CLOSEHOT` er farven bak krysset. `WM_SETCURSOR` er **delvis** tilbake
> i fase 5 — kun under panorering, for å sette `IDC_SIZEALL`. Den er ikke
> tilbake for headeren:
> headeren trengte `IDC_SIZEALL` i fase 1 fordi hele vinduet var en dra-flate
> med skjult chrome; nå er vanlig pil riktig, slik den er i en tittellinje.
> Det som ikke kom tilbake er faden, auto-skjulet og de seks bufrede
> chrome-objektene.

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
| Fase 21, før volumstolper (1280×720, 300 lys, median 172 bilder) | 1,44 ms |
| Fase 21, med volumstolper (`PolyPolygon` i bolker) | **1,56 ms** (+0,12 ms; `FillRect` per lys ga 1,84) |
| Fase 22, før verktøylinja (rød kjøring, median 177 bilder) | 1,48 ms |
| Fase 22, med verktøylinja (to kjøringer, 171 og 168 bilder) | **1,61 / 1,52 ms** (p90 1,75 / 1,70 mot 1,62) |
| Fase 23, uten varsler (to kjøringer, median av 150 tvungne bilder) | 1,89 / 1,94 ms |
| Fase 23, med **åtte** varsler i utsnittet | **2,01 / 2,11 ms** (+0,11 / +0,17 ms) |

Zoomet helt ut er *raskere* (1,5 ms) fordi lysene da er 1 px brede.
Tallene fra fase 21 og 22 er målt med QPC rundt den trege stien i `PaintPopup`
i testbygget (probe-felt 15), ikke med `PrintWindow` i flukt (fallgruve 37).
Fase 23 tvinger bildene med `RedrawWindow(RDW_UPDATENOW)` fra proben i
stedet for å vente på animasjonsklokka; grunnlinja er derfor ikke
sammenliknbar med fase 22 (rød kjøring, samme metode: 1,73 ms) — bare
differansen innen samme kjøring er det.

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

### Fase 3 — nativt vindu

Alt målt på et ekte vindu på 3840×1600:

| Handling | Resultat |
|---|---|
| Første åpning, tomt register | sentrert `1730,626` 380×300 |
| `SC_MAXIMIZE` | `-8,-8` 3856×1568, `IsZoomed` |
| `SC_RESTORE` | tilbake til `1730,626` 380×300 |
| `SC_MINIMIZE` + tray-klikk | minimert, så gjenopprettet |
| `Ctrl`+`0` (**ekte** tastetrykk) | `120,90` 700×520 → `1730,626` 380×300 |
| Tray-menyens «Standardvisning» | samme |
| `WM_CLOSE` | skjult; `X=450 Y=320 W=560 H=420` i registret |
| Omstart | gjenåpnet på `450,320` 560×420 |
| Avslutning via tray-menyen | 0 etterlatte vinduer fra gammel PID |
| Ekte `ESC` | skjuler vinduet |
| `GetGUIThreadInfo` | `hwndActive == hwndFocus ==` panelet — feil #2 er ikke tilbake |

**Håndtak: GDI 35 → 29.** De seks bufrede chrome-objektene er borte. USER 14.

> Ytelsen i `WM_PAINT` er uendret — ingenting er lagt til i tegneløkka, og
> `DrawChrome` er fjernet fra den. Spesifikasjonens «0,000 ms» er riktig for
> *denne* endringen, i motsetning til anslagene for siste-pris-linja og
> `BitBlt`.

---

### Fase 4 — rammeløst vindu med egne kontrollknapper

**Knappetegningen** (`QueryPerformanceCounter` rundt *kun* `DrawButtons`, i et
instrumentert bygg med eget mutexnavn og egen vindusklasse — fallgruve 10).
Over 1500 opptegninger per runde, drevet av kontinuerlig zoom:

| Runde | Min | Median | Snitt | Maks |
|---|---|---|---|---|
| Peker vekk (99 %-tilfellet) | 0,0126 ms | **0,0153 ms** | 0,0158 ms | 0,1014 ms |
| Peker på en knapp (`FillRect` med) | 0,0119 ms | **0,0139 ms** | 0,0143 ms | 0,0873 ms |

QPC-parets egen kostnad ble målt til under 0,00001 ms (raskeste av 1000 tomme
par) og er altså ikke en faktor.

> **Mandatets budsjett var < 0,003 ms. Det holdes ikke — målt er ~0,015 ms,
> altså fem ganger over.** 3 µs tilsvarer to til fire GDI-kall på en minne-DC;
> de fire knappene er rundt ti, pluss en `FillRect` ved hover. Tallet står som
> det er i stedet for å rundes bort.
>
> I sammenheng: en hel opptegning ved 1280×720 tar **0,853 ms** (median),
> så knappene er **1,8 %** av den. Reserveveien — forhåndstegnet rad som
> `BitBlt` — står under «Avviste forslag» med begrunnelse.

**Hele `PaintPopup` ved 1280×720**, samme metode som tabellen over:

| | Min | Median | Snitt | Maks |
|---|---|---|---|---|
| `PaintPopup`, 1280×720 | 0,5367 ms | 0,8527 ms | 0,8988 ms | 5,9505 ms |

Ikke sammenliknbart med de 1,255 ms lenger oppe: den målingen var på et
380×300-panel. Flaten er her 8× større, og opptegningen er likevel raskere,
fordi utsnittet er det samme antallet lys fordelt over flere piksler.

**Håndtak: GDI 27 → 31** for de fire nye objektene (tre penner og en rød
lukkebakgrunn; hover-bakgrunnen gjenbruker `brBox`). Målt på samme bygg før og
etter, ikke antatt. Loggens tidligere 29 gjaldt bygget med OS-ramme —
rammefjerningen tok den 29 → 27. USER 14, uendret.

**Stresstest:** 2 × 20 s, ~3,1 millioner operasjoner per runde, med ~79 000
ekte musejiggler *på* krysset, så hover-stien med `FillRect` var med i lasten.
Ingen vranglås. Håndtak 31/14 før og etter begge rundene; se fallgruve 27 om
hvorfor de står på 34/15 *under* kjøring.

**Firkantede hjørner:** alle fire 8×8-blokker i hjørnene er ren
`#0D1117`, lest med `CopyFromScreen` — DWM-runding er en komposittoreffekt
og finnes *ikke* i `PrintWindow`-utdata, så den må leses fra skjermen.
`DWMWCP_DONOTROUND` virker altså.

> Første måling viste lyse piksler i de to venstre hjørnene. Det var ikke
> avrunding — en kvartsirkel rammer alle fire likt — men et annet vindu som
> lå oppe over panelet. Panelet er ikke `WS_EX_TOPMOST`. `BringWindowToTop`
> først, så måle.

**Hover slukkes under panorering.** Chart-flaten holder museknappen via
`SetCapture`, så en dra-bevegelse som passerer over headeren ville ellers
tent krysset rødt midt i panoreringen — uten at det gikk an å klikke det.
Målt `#0D1117` under draget og `#C02A3E` så snart knappen slippes.

**Treffsoner:** 22/22, både normalt og maksimert, spørt direkte med
`SendMessage(WM_NCHITTEST)`. Grensene er pikselnøyaktige: `y=5` gir `HTTOP`
og `y=6` gir `HTCLIENT` — knapperaden begynner nøyaktig der `RESIZE_BORDER`
slutter.

---

### Fase 5 — glyf, inkrementell hover og pan-cursor

**Hover-opptegning**, malt i et instrumentert bygg som logger hvilken gren i
`PaintPopup` som ble tatt, og `rcPaint` med:

| Gren | `rcPaint` | Min | Median | Snitt | Maks |
|---|---|---|---|---|---|
| `STRIP` (kun hover) | `1162,6,1272,24` | 0,0376 ms | **0,0616 ms** | 0,0654 ms | 0,1544 ms |
| `full` (hover under animasjon) | `0,0,1280,720` | 0,6487 ms | 0,9787 ms | 1,0639 ms | 6,3758 ms |

`rcPaint` er **nøyaktig** knapperaden ved 1280 bred: `W−118, 6, W−8, 24`.
Hover-opptegningen er altså **16× billigere** enn før. Den andre raden er
beviset på at animasjonsunionen virker: med klokka i gang blir `rcPaint` hele
klientflaten, og vi faller til den trege stien av oss selv.

**Hurtigstien gir piksel-identisk resultat, i begge vindustilstander.** Hele
knapperaden (110×18 = 1980 piksler) lest etter en tvunget full opptegning,
deretter 24–32 hurtigsti-opptegninger, så lest igjen: **0 avvik av 1980** både
ved 1280×720 (stripe `1162..1272`) og maksimert ved 3840×1552 (stripe
`3722..3832`, der vannmerkebitmapen nettopp er bygget om). En hurtigsti som
tegner *nesten* likt er verre enn ingen.

Og hurtigstien tegner riktig glyf: hover på minimer-knappen mens vinduet er
maksimert ga `❐` i maksimer-knappen, ikke `□`, med minimer lyst opp i
`#161D27`.

**Glyfen**, som pikselrutenett rundt knappesenteret `(W−49, 15)`:

```
     normal                   maksimert
   .............           .............
   .............           .............
   ..#########..           ....#######..
   ..#.......#..           ....#.....#..
   ..#.......#..           ..#######.#..
   ..#.......#..           ..#.....#.#..
   ..#.......#..           ..#.....#.#..
   ..#.......#..           ..#.....#.#..
   ..#.......#..           ..#.....###..
   ..#.......#..           ..#.....#....
   ..#########..           ..#######....
   .............           .............
   .............           .............
```

> Rutenettet over er etter fase 6: to 7×7-rektangler forskjøvet **2 px**.
> Fase 5 tegnet to 6×6 forskjøvet 3 px.

Ingen streker gjennom det fremre rektangelet: det bakre tegnes som en åpen
polylinje, ikke som et helt rektangel.

Verifisert både med `SC_MAXIMIZE`/`SC_RESTORE` og med ekte museklikk.

**Pekeren**, malt med `GetCursorInfo`, 11/11: `IDC_ARROW` i hvile,
`IDC_SIZEALL` midt i draget, `IDC_ARROW` etter slipp, og alle seks
skaleringspekere i kantene urort.

**Budsjetter.** GDI 31 / USER 14 i hvile, uendret — også etter en 20 s
stresstest med ~3,2 millioner operasjoner og ~79 000 ekte musejiggler *på* en
knapp, som er hurtigstiens tyngste last. Private bytes 3,53 MB, exe 164 KB,
`/W4` rent, x86. Alt uendret fra før omgangen.

**Regresjon:** 28/28 enhetstester, 22/22 treffsoner, fire knappeklikk med ekte
mus, 11/11 hover-farger, panorering, zoom, crosshair, overlay, ESC, tray,
skalering — alle grønne.

### Fase 6 — klipping av grafen og glyfpolering

Mandatet kom fra et skjermbilde i maksimert tilstand: lys som blør inn i
Y-aksemargen eller headeren, en utydelig `❐`, og krav om minst 32 px
klaring over grafen.

**Blødningen var ekte, men 1 px, og bare mens grafen beveger seg.** Et
stillbilde med `PrintWindow` ved 3840×1552 viste ingenting. Den ble funnet
med en stressprobe: postet `WM_MOUSEWHEEL` (panorering og Ctrl-zoom), fanget
med `PrintWindow` 15/30/45/90 ms etter hvert hakk, altså midt i easingen der
`dStart` er brøk og halve lys ligger i kantene. Hvert bilde skannes for
eksakt `CLR_UP`/`CLR_DOWN` i fem regioner utenfor `rcChart`: kolonnen
`x = right`, aksemargen `x > right` (utenom stempelradene), båndet
`y ∈ [31, 44)`, alt under `bottom` og venstre marg.

| Bygg | Tilstand | Bilder | Bilder med lyspiksler utenfor | Piksler |
|---|---|---|---|---|
| før | 3840×1552 | 240 | **21** — alle i kolonnen `x = right` | 1819 |
| etter | 3840×1552 | 240 + 480 | **0** | 0 |
| etter | 1030×581 | 480 | **0** | 0 |

Årsaken var `IntersectClipRect(..., right + 1, bottom + 1)`. Kolonnen
`x = right` hører til aksemargen — rutenettet slutter på `right − 1` — men
klippet slapp lyskropper gjennom der. Topp- og bunnbåndet var aldri berørt.

**Endret i `DrawChart`:**
- `RECT rcChart = { left, top, right, bottom + 1 }` settes med
  `IntersectClipRect` **før rutenettet**, ikke bare før lysene.
- Høyre kant er eksklusiv. Bunnen er **inklusiv** med vilje: rutenettlinje
  `i = 4` og veken til laveste pris ligger begge på `y = bottom`, og et
  `[top, bottom)`-klipp ville visket ut den nederste linja.
- Prisetikettene er skilt ut i en egen løkke etter `SelectClipRgn(NULL)`.
  Lå de igjen i rutenettløkka, ville klippet tatt dem.

**Endret i `DrawButtons`:** knappeflaten fylles **alltid** før vektorene —
`brBg` i hvile, `brBox`/`brClose` på hover. Pennene var allerede kosmetiske
1 px `PS_SOLID` (`CreatePen(PS_SOLID, 1, …)`) og er urørt. Gjenopprettings-
glyfen er to 7×7-rektangler forskjøvet 2 px innenfor samme 9×9-fotavtrykk,
fortsatt med det bakre som åpen polylinje. Se rutenettet under Fase 5.

**Topp-klaring:** `rcChart.top = HEADER_H = 44` oppfylte allerede kravet.
Det er nå sikret ved kompilering: `CHART_TOP_MIN 32` og
`#if HEADER_H < CHART_TOP_MIN #error`.

**Hurtigstien er fortsatt piksel-identisk** etter at `FillRect` kom inn i den
delte `DrawButtons`: knapperaden lest fra skjermen etter en tvunget full
opptegning, så etter 24 hover-inn/hover-ut: **0 avvik av 1980** i begge
tilstander, med hover faktisk opplyst 4/4 maksimert. Gjenopprettet lyste den
bare 1/4 og 3/4 — brukeren var aktiv ved maskinen under målingen og flyttet
både peker og vindu, så den raden er svakere bevis enn den maksimerte.

`/W4` rent, x86, 168 KB exe.

### Fase 7 — målt header-layout og minstestørrelse 400×250

**Feilen, målt før endringen.** Ved 300×200 smeltet pris og prosent sammen
til én blekkflate: `$75534.98-0.27%  (5t 1m)`. De delte ett rektangel,
venstre- og høyrestilt, og `DrawTextW` klipper mot rektangelet, ikke mot
naboteksten. Blekkklynger i raden y 6–27, med sammenslåing under 8 px:
`[11-173]` der det skulle vært to.

**Endret i `DrawChart`.** Hver headertekst måles med
`GetTextExtentPoint32W` på den ferdig formaterte strengen i sin egen font.
Kollisjonsregelen er den rene funksjonen
`HeaderFits(rightBound, leftBound) = rightBound < leftBound − HDR_GAP`, med
`HDR_GAP` = 8.
- **Rad 1:** prisen står til venstre, med høyre grense `PAD_L + bredde`.
  Prosenten står til høyre og slutter ved `ButtonStrip().left − 8`. Den
  prøves i tre trinn: hel (`−0,27 %  (5t 1m)`), kort (`−0,27 %`) og skjult.
  Den klippes aldri midt i et tall. Den korte formen måles bare når den hele
  ikke får plass. Prisens rektangel slutter også ved knapperaden.
- **Rad 2:** symbollinja begrenses mot prisaksens øverste etikett
  (`right + 4 − 8`), ikke mot knappene, som slutter på y = 24. Den får
  `DT_END_ELLIPSIS` når den målte bredden ikke får plass.
- `BTN_STRIP_W` er fjernet. Knapperadens kant leses fra `ButtonStrip`.

**`WM_GETMINMAXINFO`:** `ptMinTrackSize` =
`MulDiv(400|250, GetDpiForWindow, 96)`. `SetWindowPos` håndhever den også:
et forsøk på 300×200 ga **400×250**. Et lagret register med mindre størrelse
klemmes derfor av seg selv.

**Verifisert i testbygg** (fallgruve 10: eget mutexnavn, egne klasser, egen
registernøkkel), kjørt av `PrintWindow`:

| Størrelse | Rad 1, blekkklynger | Prosent | Klippestress |
|---|---|---|---|
| 1280×720 | pris `11-103`, prosent `1093-1153`, knapper fra `1171` | hel | 0/80 |
| 3840×1552 | pris `11-102`, prosent `3634-3713`, knapper fra `3731` | hel | 0/80 |
| 400×250 | pris `11-102`, prosent `218-273`, knapper fra `291` | hel | 0/80 |
| 300×200 forsøkt | klemt til 400×250 | hel | 0/80 |

Trinnene kan ikke nås over 400 px. De ble derfor kjørt i et eget bygg med
`POPUP_MIN_W` 200:

| Bredde | Pris | Prosent | Luft til neste |
|---|---|---|---|
| 340 | `11-100` | hel `153-213` | 52 / 17 px |
| 310 | `11-100` | hel `117-183` | 16 / 17 px |
| 280 | `11-103` | **kort** `120-153` | 16 / 17 px |
| 250 | `11-103` | **skjult** | 37 px til knappene |

**Opptegning.** QPC rundt `DrawChart` og hele den trege stien, og rundt
header-blokka alene. Samme markører i gammel og ny kode. To gjennomløp, med
gammel og ny vekselvis, drevet kun av hjulhakk uten samtidig skjermfangst.
Tallene er fra andre gjennomløp:

| | Header, median | Hele bildet, median | p95 |
|---|---|---|---|
| før, 1280×720 | 124 µs | 0,820 ms | 1,275 ms |
| **etter, 1280×720** | **192 µs** | **0,871 ms** | 1,404 ms |
| før, 3840×1552 | 129 µs | 9,281 ms | 10,667 ms |
| **etter, 3840×1552** | **221 µs** | **9,988 ms** | 10,800 ms |
| før, 400×250 | 115 µs | 0,952 ms | 1,243 ms |
| **etter, 400×250** | **187 µs** | **1,011 ms** | 1,328 ms |

> **Mandatets budsjett var < 0,46 ms. Det holdes ikke, og det holdt heller
> ikke før endringen.** Header-layouten koster **~70 µs** ekstra, hovedsakelig
> tre `GetTextExtentPoint32W`. Hele bildet var allerede 0,82 ms ved 1280×720
> og 9,3 ms maksimert. I første gjennomløp, før den korte målingen ble lat,
> var header-tillegget ~95 µs. Maksimert varierer hele bildet med ±0,7 ms
> mellom gjennomløp, så den raden viser ikke header-tillegget.

`/W4` rent, x86, 169 KB exe.

### Fase 8 — `[ + ]`, flere instanser og nullstilling på dobbeltklikk

Plan og avklarte tolkninger: `docs/superpowers/plans/2026-09-16-ticker-ny-instans.md`.

**`[ ↺ ]` er borte.** Den satte *vindusgeometrien* tilbake — det gjør
`Ctrl`+`0` og tray-menyen fortsatt. Plassen er overtatt av `[ + ]` i samme
enum-posisjon (`BTN_RESET` → `BTN_NEW`), så `ButtonLayout`, `ButtonHit`,
`ButtonStrip`, `WM_NCHITTEST` og hover leser de samme fire rektanglene som
før. Glyfen er et 7×7 plusstegn, to `LineTo` med eksklusivt sluttpunkt
(`cx−3 → cx+4`), mot `Arc` og tre streker før.

**Nullstilling av zoom og panorering** er `ResetView`: de siste 300 lysene,
festet til høyre kant, `followLive`. Den rører ikke `dispValid`, så utsnittet
eases tilbake som ved hjulzoom. `TogglePopup` bruker samme funksjon og setter
`dispValid = FALSE` selv, for snap ved åpning. Tre veier inn:

- **Dobbeltklikk** i `[g.left, W) × [g.top, g.bottom]` — grafen og aksemargen.
  Krever `CS_DBLCLKS` på `BTCPopupClass`. Alt annet — knappene, og hele panelet
  mens overlayet er åpent — faller gjennom til `WM_LBUTTONDOWN`, så andre klikk
  i et raskt dobbeltklikk på `–`/`□`/`×` oppfører seg som før. Knappeklikket
  er derfor trukket ut i `OnButtonClick`. Ledig headerflate er `HTCAPTION` og
  maksimerer fortsatt.
- **`R`**, ikke mens overlayet er åpent.
- **`ESC`, lagvis:** overlay åpent → lukk. Ellers, `!ViewIsDefault` →
  nullstill. Ellers → `HidePanel`.

**Flere instanser.** Mutexen er fjernet. `SpawnInstance` leser
`GetWindowRect` (eller `rcNormalPosition` når maksimert), legger til 30 px og
starter `ticker.exe --dup x y w h sym iv` med `CreateProcessW`. Kommandolinja
ligger i et skrivbart buffer. Går vinduet ut over arbeidsområdets høyre eller
nedre kant, kaskaderer det tilbake til hjørnet — ellers ville knapperaden
etter noen klikk havnet utenfor skjermen. Barnet validerer argumentene med
samme grenser som `LoadConfig`, setter `g_isDuplicate` og åpner panelet selv
etter at låsen og hendelsene finnes. Et **duplikat** skriver ingenting til
registret (`SaveConfig` og `SaveGeometry` returnerer tidlig) og avslutter
prosessen via tray-menyens egen sti når panelet lukkes (`HidePanel`).

**Avvik under utførelse: første åpning viste 8 lys, ikke 300.** Funnet fordi
proben brukte nyåpnet panel som fasit. `TogglePopup` setter `viewCount = 0`
når bufferet er tomt, og `MergeCandles` kalte `ClampView`, som klemmer 0 opp
til `MIN_VIEW` = 8, *før* `WorkerFetchKlines` rakk sin egen «0 →
`DEFAULT_VIEW`». Gammelt bygg (`75da78c`) viste også «(8m)» ved første
åpning, og det samme gjaldt etter symbolbytte. Feilen er fra fase 1, men hvert
duplikat åpner nettopp før det har data. Rettet i `MergeCandles`: når
utsnittet følger live og ikke er satt, blir det standardutsnittet der, før
klemmingen. Egen commit.

**Verifisert** med en probe som driver den ekte pekeren og leser med
`PrintWindow`, med panelet satt `HWND_TOPMOST` (fallgruve 30). Tre fulle
gjennomløp: 26/31 (før rettelsen av 8-lys-feilen, med feil fasit), 30/31 og
29/31. Alle røde i de to siste er hover-bilder som kom for sent i normal
tilstand rett etter åpning — se latensmålingen under tabellen. Hver påstand er
grønn i minst ett fullt gjennomløp, og de omstridte er kjørt isolert:

| Test | Resultat |
|---|---|
| Hover på hver kantpiksel av alle fire knapper, og én utenfor hver kant | 32/32 normal (1037×678), 32/32 maksimert (3840×1552) |
| Hurtigsti mot full opptegning, 5 hover-tilstander × 6 runder | **0 avvik** i 30 par (1980 px hver) |
| Glyf i hvile og hover | 13 piksler, eksakt symmetrisk 7×7, begge tilstander |
| Ekte dobbeltklikk på grafen / på prisaksen, `R`, `ESC` lag 2 | utsnittet tilbake, **0,00 %** avvik mot fasit (zoomet: 6,45 %) |
| `ESC` lag 1 / lag 3 | lukker overlay og beholder zoom / skjuler panelet |
| `WM_LBUTTONDBLCLK` på `–` | minimerer fortsatt |
| Ekte dobbeltklikk i ledig header | maksimerer fortsatt |
| Ekte klikk på `[ + ]` | ny prosess med synlig panel på **~285 ms**, nøyaktig +30, +30, samme størrelse, i forgrunnen |
| `×` i duplikatet | prosessen avsluttes, registret **uendret**, hovedinstansen lever |
| `[ + ]` nær nedre høyre hjørne | duplikatet havner i arbeidsområdets hjørne (0, 0) |
| `ESC` i standardvisning i et duplikat | prosessen avsluttes |
| Håndtak i hvile | GDI 31 / USER 14, som gammelt bygg målt på samme måte |

> **De røde var forsinkelse, ikke tegnefeil.** Hurtigsti-sammenlikningen
> avvek to ganger med nøyaktig 468 px = én hel knapp: de to bildene hadde
> ulik hover-tilstand, ikke ulike piksler. Isolert, med tilstanden sjekket i
> begge bildene, ga samme test 0 avvik i 30 par (fallgruve 31). Glyftesten i
> hover feilet én gang av samme grunn.
>
> **Latens fra `SetCursorPos` til riktig hover-bilde**, 40 skifter per
> kjøring, gammelt og nytt bygg vekselvis, 2,5 s etter åpning:
>
> | Bygg | Median | Utfall (> 100 ms) |
> |---|---|---|
> | gammelt (`75da78c`) | 24 ms | **6 av 40**, alle ~2 s, feil tilstand ved tidsgrensen |
> | nytt | 29 ms | 0 |
> | gammelt | 25 ms | 0 |
> | nytt | 24 ms | 1 av 40, 1,76 s, riktig til slutt |
>
> Utfallene finnes i begge bygg og er ikke innført i fase 8. Årsaken er ikke
> undersøkt. Kandidater er `PrintWindow`/DWM og at den ekte pekeren konkurrerer
> med `TrackMouseEvent` (fallgruve 35).

**Opptegning.** QPC rundt `DrawButtons` i begge stier, rundt hele
hurtigstien (DC, bitmap, blit og knapper, uten `BeginPaint`/`EndPaint`) og
rundt headerteksten i `DrawChart`. Samme markører satt inn med skript i
gammel (`75da78c`) og ny kode. Tre runder, gammel og ny vekselvis, 1037×678,
hver runde 5 s hover-jiggling og 5 s `RedrawWindow`, ingen skjermfangst
underveis:

| | Før, median | **Etter, median** | Etter, min | Etter, p90 |
|---|---|---|---|---|
| `DrawButtons`, hover-hurtigsti | 0,0292 ms | **0,0210 ms** | 0,0124 ms | 0,0237 ms |
| `DrawButtons`, full opptegning | 0,0252 ms | **0,0168 ms** | 0,0115 ms | 0,0252 ms |
| Hurtigsti totalt | 0,0958 ms | **0,0835 ms** | 0,0419 ms | 0,0968 ms |
| Headertekst | 0,1686 ms | 0,1402 ms | 0,0743 ms | 0,2129 ms |
| **Hele headeren** (tekst + knapper, samme bilde) | 0,1956 ms | **0,1586 ms** | 0,0896 ms | 0,2295 ms |

> **Mandatets budsjett var < 0,02 ms for hele headeren. Det holdes ikke, og
> det holdt heller ikke før endringen.** Bare vektortegningen alene ligger
> rundt budsjettet: median 0,017–0,021 ms. Plusstegnet sparte ~8 µs mot
> sirkelpilen, konsekvent i alle tre runder. Headerteksten er uendret kode;
> forskjellen i den raden er støy mellom runder (rundemedianer 107–239 µs).
> Hurtigstien domineres av `CreateCompatibleDC`/`CreateCompatibleBitmap` og
> blitten, ikke av knappene. Se «Forhåndstegnet knapperad» under
> *Avviste forslag* for hva et bokstavelig budsjett ville krevd.

`/W4` rent, x86, 174 KB exe.

### Fase 9 — skrivebordsmodus (`--desktop-mode`)

Plan, forundersøkelse og avvik:
`docs/superpowers/plans/2026-09-17-ticker-skrivebordsmodus.md`. Utviklet på
grenen `desktop-mode` (tre commits) og flettet inn med `--no-ff`.

**Hva den gjør.** `ticker.exe --desktop-mode` (eneste argument) starter uten
panel i systemstatusfeltet og legger grafflaten inn i skrivebordet: barn av
WorkerW, under `SHELLDLL_DefView` (ikonene), over hele primærskjermen. Musa og
tastaturet går til skrivebordet. Samme `BTCPopupClass`, `PopupProc` og
`PaintPopup` som panelet. Bare opprettelsen er annerledes:

- `TogglePopup` lager vinduet med `WS_POPUP` alene og kaller
  `AttachToDesktop` *før* `hPopup` publiseres. Ingen `SquareCorners`,
  `PlacePopupInitially` eller `ForceForeground`. `SW_SHOWNA`.
- `FindDesktopWorkerW` sender `0x052C` (`0xD,1` og `0,0`) med
  `SendMessageTimeoutW`, 1 s og `SMTO_ABORTIFHUNG`. Den tar WorkerW som
  barn av Progman (24H2), og ellers den klassiske søsken-WorkerW via
  `EnumWindows`.
- `AttachToDesktop`: `WS_POPUP` → `WS_CHILD`, `SetParent`, **deretter**
  `WS_EX_LAYERED | WS_EX_TRANSPARENT` og `SetLayeredWindowAttributes(255,
  LWA_ALPHA)`, og til slutt `SetWindowPos(HWND_BOTTOM)` med
  `SM_CXSCREEN`×`SM_CYSCREEN`. Origo regnes om med `MapWindowPoints`.
- `WM_NCHITTEST` → `HTTRANSPARENT`. `DrawButtons` hoppes over.
- `SaveGeometry` skriver ingenting. Tray: venstreklikk gjør ingenting, og
  menyen har bare «Avslutt Ticker».
- `WM_NCDESTROY` i `PopupProc` nuller `hPopup` og `animRunning` hvis flaten
  forsvinner uten at vi ba om det, og starter `TIMER_EMBED_ID` (250 ms).
  `TaskbarCreated` legger ikonet inn igjen (i begge modi) og bygger flaten på
  nytt, men bare hvis den ikke allerede sitter i dagens WorkerW.

**Manifestet er det som gjør flaten synlig.** Forundersøkelsen står i
planen. Kort fortalt: på 26100 blir et vanlig GDI-barn av WorkerW (eller
Progman) aldri synlig, fordi Progman har `WS_EX_NOREDIRECTIONBITMAP`. Et
lagdelt barn blir synlig, men bare når **begge** disse er oppfylt:
`supportedOS` Windows 8+ i manifestet (uten det er exstilen 0), og
`SetLayeredWindowAttributes` kalt *etter* `SetParent`. `WS_EX_LAYERED` er altså
ikke et valg mellom to måter å slippe musa gjennom. Uten den vises ingenting.

**Shell-treet, verifisert** med en probe som leser vindustreet fra utsiden
(`EnumChildWindows`, `GetParent`, `GetWindow`, `GetWindowLong`,
`GetLayeredWindowAttributes`, `WindowFromPoint`):

| Kontroll | Resultat |
|---|---|
| Forelder | `WorkerW` (Explorer-pid) |
| WorkerW sin forelder / flatens rot | Progman / Progman |
| Z-orden i Progman | `SHELLDLL_DefView` > `WorkerW` |
| Z-orden i WorkerW | flaten er siste barn (`HWND_BOTTOM`) |
| Stil / exstil | `0x54000000` (`WS_CHILD`, ingen `WS_POPUP`/`WS_THICKFRAME`) / `0x00080020` (`LAYERED`, `TRANSPARENT`) |
| Lag | alfa 255, `LWA_ALPHA` |
| Rekt | 0,0 3840×1600 = `SM_CXSCREEN`×`SM_CYSCREEN` |
| Synlige toppnivåvinduer (knapp i oppgavelinja) | 0 |
| `WindowFromPoint` på 27 synlige skrivebordspunkter | 27× `SysListView32`, **0** treff på flaten |
| `WM_NCHITTEST` på header, kryss, venstre kant og graf | −1/−1/−1/−1 (`HTTRANSPARENT`) |
| Knapperaden | én farge, ingen glyfer |
| Skjermbilde | lysene synlige mellom og bak ikonene, pris øverst til venstre |
| Avslutt via tray-stien | prosessen borte, tapetet tilbake, ingen rester |
| Ekte omstart av Explorer (etter `1dbc75f`) | ny flate i ny WorkerW etter **566 ms**, stående, 14/14 grønne, tray-ikonet tilbake (`Shell_NotifyIconGetRect` = `S_OK`) |

**Omstart av Explorer**, kjørt med brukerens klarsignal: `Stop-Process -Force`
på explorer.exe mens `--desktop-mode` kjørte, og `AutoRestartShell = 1` startet
den igjen. En probe fulgte vindustreet hvert 100. ms:

| Tid | 1. kjøring (`56b67b1`, retry 1 s) | 2. kjøring (`2e43190`, retry 1 s) | 3. kjøring (`1dbc75f`, retry 250 ms) |
|---|---|---|---|
| ~20 ms | gammel flate **borte** (`IsWindow` usann), prosessen lever | samme | samme |
| ~160 ms | ny explorer.exe | samme | samme |
| 285–480 ms | ny Progman | samme | samme |
| 285 ms | — | — | ny flate laget, venter på `0x052C` |
| 566 ms | — | — | **flaten i ny WorkerW, står** |
| ~1,1 s | ny flate i ny WorkerW (timeren) | ny flate i ny WorkerW, står | — |
| ~1,66 s | flaten **revet ned igjen** | — | — |
| ~2,68 s | ny flate igjen | — | — |

> 3. kjøring: `SendMessageTimeoutW` til en helt ny Progman brukte ~280 ms før
> WorkerW fantes. UI-tråden står så lenge, innenfor taket på 1 s. Vinduet er
> ikke synlig før det sitter i WorkerW.

**Vanlig modus ved omstart av Explorer** (hovedinstans med åpent panel,
`Shell_NotifyIconGetRect` for ikonet):

| Bygg | Prosess | Panel | Tray-ikon før → etter |
|---|---|---|---|
| master `084f724` | lever | lever | `S_OK` → **`E_FAIL`, for godt** |
| master `bc9e409` | lever | lever | `S_OK` → `S_OK` |

> Et bygg fra før fase 9 **dør ikke** når Explorer startes på nytt, men det
> mister tray-ikonet, og dermed «Avslutt Ticker». Brukerens instans (pid 30852)
> var borte etter første omstart, og hendelsesloggen viser ingen krasj. Siden
> gammelt bygg overlevde omstarten i testen, ble den trolig avsluttet på annen
> måte, men det er ikke bevist. UI Automation fant ikke ikonet til en ny
> hovedinstans selv *før* omstart. Bruk `Shell_NotifyIconGetRect`.

> **Windows river ned et barn fra en annen prosess når forelderen dør.**
> `WM_NCDESTROY` er altså stien som faktisk brukes, og timeren bygget flaten på
> nytt før `TaskbarCreated` kom. I første kjøring rev `TaskbarCreated` ned den
> ferske flaten, og skrivebordet sto uten graf i ett sekund til. Rettet i
> `2e43190` (egen gren, `desktop-mode-explorer-restart`): flaten rives nå bare
> ned hvis forelderen ikke er dagens WorkerW. Etter omstarten fant
> UI Automation `BTC/USDT: $76274.09` i `Shell_TrayWnd`, der ingen annen
> `ticker.exe` kjørte. Etter rettelsen ville et postet `TaskbarCreated` ikke
> lenger rive ned noe, så den tidligere testen med postet melding er ikke
> kjørt på nytt.

**Vanlig modus med manifestet**, nytt bygg mot master, samme `--dup`-geometri:
stil `0x94070000` og exstil `0x00000100` i begge. Toppnivå, samme rekt, samme
treff-test (`HTCAPTION`/`HTCLIENT`/`HTLEFT`/`HTCLIENT`), og knapperaden
**0 avvik av 3600 piksler** med `PrintWindow`.

**Opptegning.** Samme QPC-markører ble satt inn med skript i master
(`084f724`) og i nytt bygg. Total er fra før `BeginPaint` til etter
`EndPaint` i den trege stien. Tegning er fra `CreateCompatibleDC` til
`DeleteDC` og ligger 0,03–0,06 ms under total i alle rader. Skrivebordsmodus
ved 1280×720 er et målebygg der `DM_W`/`DM_H` overstyrer flatens størrelse,
så forankringen kan sammenliknes med samme pikselmengde. Fire runder, seks
konfigurasjoner vekselvis. Hver runde: 5 s etter start, deretter 5 s med
`InvalidateRect` hvert 16. ms fra en annen prosess. Ingen skjermfangst
underveis:

| Konfig | n | **Median** | Min | p90 | Rundemedianer |
|---|---|---|---|---|---|
| master, vanlig 1280×720 | 838 | **1,94 ms** | 0,92 | 2,68 | 1,79 / 2,11 / 1,83 / 2,29 |
| ny, vanlig 1280×720 | 795 | **1,89 ms** | 0,82 | 2,60 | 2,09 / 1,89 / 1,94 / 1,84 |
| ny, skrivebord 1280×720 | 705 | **2,24 ms** | 0,79 | 2,70 | 1,85 / 2,40 / 1,91 / 2,44 |
| master, vanlig 3840×1600 | 741 | **13,36 ms** | 10,79 | 14,87 | 12,86 / 13,66 / 13,51 / 13,14 |
| ny, vanlig 3840×1600 | 925 | **13,46 ms** | 10,67 | 14,86 | 12,58 / 13,29 / 14,56 / 13,10 |
| ny, skrivebord 3840×1600 | 705 | **13,49 ms** | 10,68 | 14,74 | 13,40 / 12,61 / 13,72 / 14,05 |

> **Mandatets < 0,85 ms holdes ikke i noen modus, og uendret master holder
> det heller ikke i dag.** Tallet i *Kjente begrensninger* (~0,85 ms ved
> 1280×720) er målt under andre forhold. Maskinen var i vanlig bruk, og
> brukeren hadde egne `ticker.exe` i gang: fire da arbeidet startet, én ved
> slutten. Bare de raskeste enkeltbildene er under
> 0,85 ms. **Forankringen koster ikke målbart ved full størrelse:** 13,49 mot
> 13,46 ms, med rundemedianer som overlapper. Ved 1280×720 er skrivebordsmodus
> 0,35 ms tregere i median, men også her overlapper rundemedianene
> (1,85–2,44 mot 1,84–2,09). Det som faktisk koster, er størrelsen: 6,9 ganger
> så mange piksler gir ~7 ganger tiden. Tidligere målt maksimert: ~9,5 ms ved
> 3840×1552.

**Minne og håndtak i hvile.** Ikke-instrumenterte bygg. Målt 15 s etter
start, deretter fem avlesninger med 2 s mellomrom, to runder.
`PrivateMemorySize64` og `GetGuiResources`:

| Konfig | Private bytes | GDI | USER | Kjernehåndtak |
|---|---|---|---|---|
| master, vanlig 1280×720 | 3,86–4,00 MB | 33–38 | 14–18 | 366–370 |
| **ny, vanlig 1280×720** | **3,59 MB** (alle 10) | **31** (alle 10) | **14** (alle 10) | 358–360 |
| **ny, skrivebord 3840×1600** | **3,36–3,42 MB** | **30** (alle 10) | **6** (alle 10) | 323 |

> **Skrivebordsmodus ligger under mandatets ~3,53 MB og 31/14.** Vanlig modus
> ligger på 3,59 MB, som «~3,6 MB» under *Bygg*. Masters rader er **ikke i
> hvile**: GDI 33–38 og USER opptil 18 er tellingen midt i en opptegning
> (fallgruve 27). Det åpne panelet fikk hover-trafikk under målingen, så de
> tallene sammenliknes ikke. At en flate på 3840×1600 ikke bruker mer privat
> minne enn panelet, stemmer med at dobbeltbufferet lages og slettes i hvert
> bilde, og med at kompatible bitmaper ikke telles i prosessens private
> bytes. Vannmerkebitmapen er det eneste som lever mellom bildene. USER 6 mot
> 14: flaten får ingen musemeldinger, og det finnes verken peker- eller
> hover-tilstand å holde på.

**Skalering over 100 %** (`e070e34`, egen gren `desktop-mode-dpi`), kjørt med
brukerens klarsignal. Skaleringen på primærskjermen ble endret med
`DisplayConfigSetDeviceInfo` (type −4, anbefalt 100 %) og satt tilbake til
100 % i en `finally`. Proben er per-monitor-bevisst og teller fysiske piksler.
Den tar punkter hvert 37. px der skrivebordet er øverst, og teller hvor mange
som har flatens bakgrunn `#0D1117`. Nede til høyre ligger lengst fra origo:

| Bygg | Tilfelle | Flatens rekt (fysisk) | Nede til høyre |
|---|---|---|---|
| før | 100 % | 3840×1600 | 784 / 791 |
| før | 100 % → 150 % mens den kjører | 3840×1600 | 204 / 220 |
| før | **startet ved 150 %** | **2560×1067** | **16 / 220** |
| etter | 100 % | 3840×1600 | 784 / 791 |
| etter | 100 % → 150 % mens den kjører | 3840×1600 | 205 / 220 |
| etter | startet ved 150 % | **3840×1600** | **203 / 220** |

> **Årsak:** en DPI-uvitende prosess får virtualiserte `SM_CXSCREEN`/
> `SM_CYSCREEN` (2560×1067 ved 150 %), og det var den størrelsen
> `AttachToDesktop` ga flaten. WorkerW er 3840×1600 fysisk. **Rettelse:**
> `TogglePopup` setter tråden til `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`
> rundt `CreateWindowExW` og `AttachToDesktop`, og setter den tilbake
> umiddelbart. Vinduet beholder konteksten, og `WM_PAINT` kjøres i den, så
> `GetClientRect` gir fysiske piksler. Resten av prosessen, også vanlig modus,
> er DPI-uvitende som før. Vindustre-proben ved 100 % etter rettelsen: 14/14.
> Etter testen: `anbefalt=100% naa=100% systemdpi=96`.

`/W4` rent, x86, 177 KB exe.

### Fase 10 — vedvarende dobbeltbuffer

Plan og avvik: `docs/superpowers/plans/2026-09-17-ticker-opptegning.md`.
Gren `opptegning`, flettet inn med `--no-ff`.

**Hvor tiden gikk.** QPC-markører rundt hvert ledd i `PaintPopup` og
`DrawChart`, satt inn med skript i både master og nytt bygg. Ved 3840×1600 gikk
**55–60 % til å blitte vannmerket inn i et helt nytt dobbeltbuffer**
(6,5–7,9 ms), og ytterligere 1,7 ms til å frigjøre det. `CreateCompatibleBitmap`
tok bare 0,07 ms. Den er lat, og kostnaden på 24 MB kommer ved første skriving
og ved frigjøring, i hvert bilde (fallgruve 48).

**Endringen:**
- **Vedvarende buffer:** `bbDC`/`bbBmp` lever mellom bildene og bygges på nytt
  bare når størrelsen endres (`EnsureBackBuffer`/`FreeBackBuffer`).
- **Hurtigstien:** knappene tegnes rett inn i bufferet, og stripa blittes
  derfra. Mellomrommene er forrige fulle bilde. Nytt vilkår: `bbValid`, og
  overlayet må verken være åpent eller synlig under uttoning.
- **Lysene:** tegnes med `DC_PEN`/`DC_BRUSH` og `SetDCPenColor`/
  `SetDCBrushColor`, satt bare ved fargeskifte, i stedet for `penUp`,
  `penDown`, `brUp` og `brDown`. Det er fire GDI-objekter mindre, og bufferet
  tar to av dem.
- **Vannmerke-cachen er beholdt.** Første versjon fjernet den og tegnet teksten
  per bilde. Det kostet 0,50–0,53 ms ved 1280×720 og spiste hele gevinsten i
  vanlig modus. Se planens avvik.

**Piksler.** Målebygg av master og nytt bygg med *samme* endringer: Binance-svaret
lest fra fil (faste data), `TrackMouseEvent` som no-op, og vinduet på x = −1270,
så den ekte pekeren ikke når det. All tilstand ble styrt med postede
meldinger, og hvert bilde ble fanget med `PrintWindow` først når to bilder på
rad var likt (fallgruve 50). 15 tilstander: hvile; hover på hver av fire knapper,
både hurtigsti og full opptegning; forlatt; trådkors; overlay; overlay med musa
over en knapp; overlay lukket; skrivebordsmodus 3840×1600. Hver tilstand er
bekreftet mot hvile: knapp 468 px, trådkors 9 417 px, overlay 40 460 px.

| Gjennomløp | Master mot nytt | Hurtigsti mot full (nytt / master) |
|---|---|---|
| 1 | **0 avvik**, 15/15 | 0 / 0, alle fire knapper |
| 2 | **0 avvik**, 15/15 | 0 / 0, alle fire knapper |

**Opptegning.** Tre runder, master og nytt vekselvis, 5 s `InvalidateRect`
hvert 16. ms. Vanlig modus på x = −1270, fordi en første kjøring på skjermen
ble forurenset av hover (0,2 ms trådkors og opptil 941 bilder per runde):

| Konfig | Master, median | **Nytt, median** | Rundemedianer master → nytt |
|---|---|---|---|
| vanlig 1280×720 | 1,585 ms | **1,330 ms (−16 %)** | 2,26 / 1,48 / 1,53 → 1,23 / 1,36 / 1,33 |
| skrivebord 3840×1600 | 11,696 ms | **5,128 ms (−56 %)** | 11,75 / 11,75 / 11,66 → 4,99 / 5,08 / 5,32 |

| Ledd (median) | Vanlig master → nytt | Skrivebord master → nytt |
|---|---|---|
| Buffer lages | 0,236 → 0,000 ms | 0,069 → 0,000 ms |
| Vannmerke-blit | 0,239 → 0,276 ms | **6,500 → 1,817 ms** |
| Header-tekst | 0,206 → 0,235 ms | 0,283 → 0,285 ms |
| Lys | 0,600 → 0,536 ms | 1,461 → 1,340 ms |
| Blit til vindu | 0,098 → 0,098 ms | 1,389 → 1,393 ms |
| Buffer slettes | 0,011 → 0,000 ms | **1,739 → 0,000 ms** |

> **Mandatets 0,85 ms nås fortsatt ikke.** Det som gjenstår ved 1280×720, er
> lysene (0,54), vannmerke-bliten (0,28) og headerteksten (0,24). Ved
> 3840×1600 er det to fullskjerms-blitter på 1,4–1,8 ms hver og lysene
> (1,34). Å bytte penn bare ved fargeskifte ga ingen målbar gevinst alene.

**Håndtak og minne i hvile.** Ikke-instrumenterte bygg, 15 s etter start, fem
avlesninger per runde, to runder:

| Konfig | Private bytes | GDI | USER |
|---|---|---|---|
| master, vanlig 1280×720 | 3,69–3,92 MB | 31 | 14 |
| **nytt, vanlig 1280×720** | 3,59–3,77 MB | **29** | 14 |
| master, skrivebord 3840×1600 | 3,37–3,48 MB | 28 | 6 |
| **nytt, skrivebord 3840×1600** | 3,37–3,41 MB | **26** | 6 |

> Bufferet på 24 MB vises ikke i private bytes, på samme måte som
> vannmerke-cachen ikke gjorde det. Skrivebordsmodus står nå på 28 GDI i
> master. Fase 9 målte 30, før DPI-rettelsen, og det er ikke undersøkt hva som
> utgjør forskjellen.

**Etter rådgiverens gjennomgang**, to ting som ellers bare var påstått:

| Test | Resultat |
|---|---|
| Ekte omstart av Explorer med nytt bygg: bufferet brukt av en *ny* flate | ny flate i WorkerW etter **594 ms**, vindustre 14/14, og **90–98 %** av synlige skrivebordspunkter i alle fire kvadranter har grafens bakgrunnsfarge (DPI-bevisst probe) |
| 30 størrelsesendringer i vanlig modus | GDI/USER **29/14 før og 29/14 etter** |
| Hover postet rett etter størrelsesendring (1280×720, 900×500, 1500×800) | hurtigstien identisk med full opptegning i alle tre (`bbValid` stenger hurtigstien til bufferet har riktig størrelse) |

> **DC-tilstanden lever nå mellom bildene.** Penner, pensler, fonter,
> tekstfarge og bakgrunnsmodus som siste bilde valgte, står fortsatt valgt i
> `bbDC` når neste bilde begynner. Pikseltesten fant ingen følger av det i de
> 15 tilstandene. Ny tegnekode må likevel selv velge alt den bruker, og kan
> ikke regne med en fersk DC.

`/W4` rent, x86.

---

### Fase 11 — akser, kontrast og tidsakse

Plan og avvik: `docs/superpowers/plans/2026-09-17-ticker-akser.md`. Gren `akser`.

**Endringen:**
- **Geometri:** `PAD_R` 54 → 84 (priskolonne 76 + kantsikring 8) og `PAD_B`
  10 → 18 (tidsbånd). All aksetekst leser `axL`/`axR`; de magiske
  `right + 4`, `W − 4`, `W − 2` og `W − 1` er borte.
- **Aksefont:** `hFontAxis` = Lucida Console em 15, `ANTIALIASED_QUALITY`.
  Målt: 11 px sifferhøyde, 9 px tegnbredde, tmHeight 15. Tabellen over andre
  fonter står i planen. `hFontSmall` er uendret for header, overlay og
  hover-boks, der `LINE_H = 13` er målt på den.
- **Aksefarge:** `CLR_AXIS` #A0AAB8, **8,05:1** (spesifikasjonen sa 4,85:1). De
  gamle aksene i `CLR_DIM` lå på 4,12:1, under AA.
- **Vannmerke:** `WatermarkAlpha(W)`, se *Vannmerket*.
- **Tidsakse:** bare tekst, `TimeTickStep` (med tak, ikke gulv — gulv
  kolliderer, se planens avvik 3), rundet opp med `NiceTimeStep` og forankret i
  lokal tid via `openTime`. Ingen allokering; O(N_x) per bilde.
- **Prisetikett under stempelet** tegnes ikke når de ville overlappet (< 16 px).

**Enhetstester** mot funksjonene trukket ut av `ticker.c` med `awk`:
**572 940 / 572 940**. `WatermarkAlpha` er testet på gulv, nominell bredde, tak,
W ≤ 0, og monotoni og grenser for alle W fra 1 til 8000. `TimeTickStep` er testet
på motbeviset mot gulv (M = 9, W = 320 → S = 3), degenererte tilfeller og
kollisjonsfrihet uttømmende over W_chart 160–4000, Δx 80–118 og dCount 1–1440.
`NiceTimeStep` avrunder aldri nedover for noe intervall, og treffer forventet
steg for 1m, 5m, 1t, 4t og 1d.

**Piksler**, `PrintWindow` av `--dup`-instanser (skriver ikke til registret),
live data:

| Størrelse, par/intervall | α (t) | Vannmerkepiksel funnet | Ikke-BG i `x ≥ W − 5` | Tidsbånd utenfor `[left, right]` | Rad `y = bottom` / `bottom + 1` | Tekstrader i båndet |
|---|---|---|---|---|---|---|
| 400×250 BTC 1m | 0,040 (10) | `#161A20` ✓ | 0 | 0 | 306 / 0 | 235–245 (11 px) |
| 1280×720 BTC 1m | 0,065 (17) | `#1D2026` ✓ | 0 | 0 | 1186 / 0 | 705–715 |
| 1280×720 BTC 1t | 0,065 (17) | `#1D2026` ✓ | 0 | 0 | 1186 / 0 | 705–715 |
| 1280×720 BNB 4t | 0,065 (17) | `#1D2026` ✓ | 0 | 0 | 1186 / 0 | 705–715 |
| 1920×900 ETH 1d | 0,080 (20) | `#1F2329` ✓ | 0 | 0 | 1826 / 0 | 885–895 |
| 3000×900 SOL 5m | 0,100 (26) | `#25292E` ✓ | 0 | 0 | 2906 / 0 | 885–895 |

Ingen piksler til høyre for `W − 5`. Mellom `W − 8` og `W − 5` ligger bare
stempelets flate, som får 3 px luft rundt teksten, og aldri tekst. Raden
`y = bottom` er hel (rutenettet), og `bottom + 1` er tom. Alle
tekstpiksler i båndet har nøyaktig `#A0AAB8`: Lucida Console i 15 px
kantutjevnes ikke, heller ikke med `ANTIALIASED_QUALITY`. Avstanden mellom
etikettene, målt i bildene: 118 px (1m, 1280), 189 px (1t, 2 døgn), 166 px
(4t, 7 døgn), 170 px (1d, 1920) og 183 px (400×250). Minste luft mellom to
etiketter er 67 px (4t). Mellomrommet *inne i* `DD.MM HH:MM` er 12 px, og det
må ikke forveksles med luften mellom to etiketter.

**Opptegning**, QPC rundt `DrawChart`, 400 bilder per kjøring,
`InvalidateRect` hvert 16. ms, vinduet nesten helt utenfor skjermen, live data,
master og nytt vekselvis:

| Konfig | Runde 1 master / nytt | Runde 2 | Runde 3 |
|---|---|---|---|
| 1280×720 | 1,613 / 1,990 ms | 1,323 / 1,242 ms | 1,325 / 1,289 ms |
| 3000×1200 | 2,306 / 2,769 ms | 2,663 / 2,611 ms | 2,508 / 2,525 ms |

Runde 1 er oppvarming. Etter den er forskjellen innenfor støyen: ~12–25
`ExtTextOutW` per bilde er ikke målbare mot resten.

**GDI i hvile** (1280×720, 5 og 8 s etter start): master 29 / 29, nytt
**30 / 30**. Den ene er `hFontAxis`.

`/W4` rent, x86.

---

### Fokus-blink — klassisk NC-ramme ved aktivering

Plan og avvik: `docs/superpowers/plans/2026-09-17-ticker-fokus-blink.md`.
Gren `fokus-blink`, flettet inn med `--no-ff` før fase 12.

**Årsaken, målt.** En probe leste panelets kant fra den ferdig sammensatte
skjermen (`BitBlt` fra skjerm-DC) hvert ~2. ms, mens fokus gikk til et
hjelpevindu og tilbake. Det var **ikke** et blink på ett bilde:
`DefWindowProc(WM_NCACTIVATE)` tegner den klassiske `WS_THICKFRAME`-rammen inn i
vindus-DC-en. `WM_NCCALCSIZE` gjør klienten like stor som vinduet, så rammen
havner *oppå* grafen, 3 px dyp: #E3E3E3 og #FFFFFF ytterst, deretter #B4B4B4
(`COLOR_ACTIVEBORDER`) eller #F4F7FC (`COLOR_INACTIVEBORDER`). Den blir stående
til neste fulle opptegning, altså til neste datahenting (målt 1,6 s uten støy,
opptil 3 s). `WM_SETTEXT`, som kommer ved symbolbytte, tegner den også.

**Endringen:** `WM_NCACTIVATE` → `DefWindowProcW(hwnd, msg, wParam, -1)`, og
`WM_NCPAINT` → 0 som vern. `DWMWA_NCRENDERING_POLICY = DWMNCRP_DISABLED`
fra mandatet er **ikke** brukt (fallgruve 52).

| Variant, 3 fokussykluser | Rammefargede px | `WM_SETTEXT` |
|---|---|---|
| Uten rettelse | 7 181 184 | 230 100 |
| `return TRUE` | 0 | 0 |
| `DefWindowProc(…, -1)` | 0 | 0 |
| `-1` + `WM_NCPAINT` | 0 | 0 |
| `-1` + `WM_NCPAINT` + `DWMNCRP_DISABLED` | **1 296**, forgrunnsbytte feilet 2 av 3 | 0 |

**Etter fletting med fase 12**, 5 fokussykluser × 1,5 s hver vei. Samples der et
annet vindu var i forgrunnen, eller dekket ett av 8 kantpunkter, er forkastet
(fallgruve 54):

| Bygg | Gyldige samples | Rammefargede px | `WM_SETTEXT` / størrelsesendring |
|---|---|---|---|
| Kontroll, uten rettelse | 984 | 7 910 988 | 230 100 / 0 |
| `ticker.exe` fra master | 955 | **0** | **0 / 0** |
| Panel laget på nytt etter to modusbytter | 758 | **0** | **0 / 0** |

GDI/USER i hvile: 30 / 14. `/W4` rent, x86.

### Fase 12 — modusveksling fra tray-menyen

Plan, målinger og avvik: `docs/superpowers/plans/2026-09-17-ticker-modusveksling.md`.
Gren `modusveksling`, flettet inn med `--no-ff`.

**Endringen:**
- `ID_TRAY_DESKTOP` (1003) og `BuildTrayMenu()`. Menyen bygges ved hvert
  høyreklikk: «Skrivebordsmodus» med hake, «Standardvisning» (grå i
  skrivebordsmodus), skillelinje, «Avslutt Ticker». Et duplikat får ikke
  modusvalget.
- `SetDesktopMode()` **river ned og lager vinduet på nytt** gjennom
  `TogglePopup`, i stedet for å flytte det med `SetParent` (fallgruve 53).
  Rekkefølgen er `KillTimer(TIMER_EMBED_ID)`, så `SaveWindowPlacement` mens
  flagget fortsatt sier panel, så `hPopup = NULL` under lås før
  `DestroyWindow` (da starter `WM_NCDESTROY` ingen timer). Deretter nullstilles
  `animRunning`, `trackingMouse`, `panning` og `bbValid`, flagget settes og
  lagres, og `TogglePopup` kalles.
- `SaveGeometry` oppdaterer også `g_savedPanelX/Y/W/H`. `PlacePopupInitially`
  leser dem, og panelet lages nå på nytt etter hver tur innom skrivebordet.
- `DesktopMode` i registret, se *Registret*.

**Verifisert** i testbygg med `REG_PATH` = `Software\TickerTest`, drevet med
`PostMessage(WM_COMMAND, 1003)`:

| Modus | Forelder | `WS_CHILD` | `WS_THICKFRAME` | Lagdelt / gjennomsiktig | TOPMOST | DPI |
|---|---|---|---|---|---|---|
| Skrivebord | WorkerW | 1 | 0 | 1 / 1 | 0 | per-monitor |
| Panel | ingen | 0 | 1 | 0 / 0 | 0 | uvitende |

- **Registret** følger byttene, og en ny prosess uten argumenter starter rett i
  skrivebordsmodus, også etter at forrige prosess ble drept i den modusen.
- **Geometri:** 300,200 900×500 inn, samme ut etter rundtur, også i registret.
- **Data:** `candleCount` 300 før og i første bilde etter hvert bytte.
- **Postet «Standardvisning» i skrivebordsmodus** endrer ikke flaten.
- **GDI/USER i hvile:** 30 / 14–15 etter 20, 40, 60 og 100 rundturer. Det er
  platå, ikke lekkasje.
- **Menyen** (`BuildTrayMenu` trukket ut med `awk`): 15/15.

**Byttetid**, fra `WM_COMMAND` til første fulle bilde er blittet:

| Retning | Median | Spenn | Største ledd |
|---|---|---|---|
| Til skrivebord 3840×1600 | **28 ms** | 26–43 ms | 16 ms: første skriving i ny vannmerke-cache og nytt buffer (fallgruve 48) |
| Til panel 1280×720 | **18 ms** | 16–37 ms | 6 ms: plassering, visning og forgrunn i `TogglePopup` |

> **Mandatets < 16 ms er ikke nådd, og det er et bevisst valg.** Det kan bare
> nås ved å beholde bitmapene for begge størrelser, rundt 48 MB mer mens
> panelet er i bruk. Brukeren valgte gjenoppbygging ved bytte: byttet er
> sjeldent og manuelt, og 28 ms er under to bilder ved 60 Hz.

### Fase 13 — start ved pålogging fra tray-menyen

Plan, målinger og avvik: `docs/superpowers/plans/2026-09-17-ticker-autostart.md`.
Gren `autostart`.

**Endringen:**
- `IDM_TOGGLE_AUTOSTART` (1004). «Start ved pålogging» med hake står rett over
  «Avslutt Ticker», med skillelinje på begge sider. Haken leses fra registret
  hver gang menyen bygges. Et duplikat får ikke valget.
- `AutostartPresent()` gir haken: verdien finnes, uansett type og innhold.
- `ToggleAutostart()`: er verdien lik gjeldende sitert sti (`_wcsicmp`), slettes
  den. Ellers skrives gjeldende sti, også når verdien peker på en flyttet exe,
  er usitert, har feil type eller er for lang. Et avkrysset valg med foreldet
  sti rettes derfor ved klikk i stedet for å slås av.
- `AutostartCommand()` bruker samme `MAX_PATH`-vakt som `SpawnInstance`. En
  avkuttet sti skrives aldri.
- Mandatet nevner `WM_CONTEXTMENU`, men tray-ikonet leverer `WM_RBUTTONUP`
  gjennom `WM_TRAYICON`. Punktet ligger derfor i `BuildTrayMenu()`.

**Verifisert** i testbygg med `AUTOSTART_KEY` = `Software\TickerTestRun`, fra
to mapper med mellomrom i navnet, **27/27 i tre kjøringer**. Testene dekker
menyrekkefølge og tekst, hake før og etter klikk, sitert `REG_SZ`, sletting,
flyttet exe (verdien oppdateres, ikke slettes), casing, usitert sti,
`REG_DWORD`, for lang verdi og duplikat. Kommandolinja er kjørt med
`CreateProcess`-tolkning og starter riktig exe. **GDI/USER 28/13**, uendret
gjennom 800 klikk og 80 menyer. En ekte pålogging er ikke testet.

**Etter fletting**, i bygget som ligger i rotmappa og mot den ekte
Run-nøkkelen: **14/14**. Menyen har riktig rekkefølge og tekst; autostart
skriver `"C:\Users\sysadmin\Desktop\Ticker\ticker.exe"` og sletter igjen;
skrivebordsmodus av og på i samme kjøring gir flate i WorkerW → toppnivåpanel →
flate i WorkerW, med `DesktopMode` 1 → 0 → 1 og «Standardvisning» grå bare i
skrivebordsmodus. Run-nøkkelen sto uten `Ticker`-verdi før og etter.

---

### Fase 14 — tekstfri flate på skrivebordet

Plan og målinger: `docs/superpowers/plans/2026-09-17-ticker-omgivelsesmodus.md`.
Gren `omgivelsesmodus`.

**Premisset:** et panel leses fovealt — brukeren stopper opp og dekoder tall.
En flate i skrivebordet leses perifert, og da er alfanumeriske stempler
interferens mot ikoner og mapper. Opptegningen deles derfor i to lag:
infrastruktur (kurve, rutenett, akselogikk) som består, og metadata (pris,
prosent, undertittel, akseetiketter, siste-pris-stempel) som deaktiveres når
vinduet er en bakgrunnsstruktur.

**Endringen:**
- `ChartGeometry()` gir hele flaten i skrivebordsmodus: `0, 0, W, H`. Ett sted,
  og vannmerkets sentrering, rutenettet, lysene, klipperegionen og
  siste-pris-linja følger etter.
- `DrawChart()`: header-blokka, prisaksens etiketter, hele tidsakse-blokka og
  siste-pris-**stempelet** er gated på `!g_desktopMode`. Den stiplede
  siste-pris-linja blir stående — den er geometri, ikke et tall. Vannmerket
  består som identitetsmarkør.
- Tom buffer i skrivebordsmodus returnerer uten melding. Flaten står med
  bakgrunn og vannmerke.
- Rutenettet tegner bare linje 1–3 der. Kant til kant ville lagt linje 0 og 4
  på `y = 0` og `y = H - 1`, altså en 1 px ramme rundt hele skjermen.
- `SetDesktopMode()` nullstiller `wmValid`: vannmerke-cachen ligger i `ctx`,
  overlever at vinduet lages på nytt, og er nøklet på `(W, H, symIdx, ivIdx)` —
  ikke på modus, som nå avgjør geometrien.
- Hurtigstien for knapperaden i `WM_PAINT` krever nå `!g_desktopMode`
  eksplisitt.

**Verifisert** med dump av bakbufferet fra testbygget (`WM_APP+7`), ikke
skjermdump: flaten er lagdelt og ligger bak ikonene. **22/22 i to kjøringer.**
Null piksler av `CLR_AXIS`, `CLR_TEXT` og `CLR_DIM` på skrivebordet, mot
1 514 / 230 / 105 i panelkontrollen i samme kjøring. Rutenettradene ligger på
nøyaktig 400, 800 og 1200 av 1600 med utstrekning x 0 … 3839 — det direkte
beviset på full flate — mot 44, 208, 373, 537, 702 og x 10 … 1195 i panelet.
Tom buffer gir 0 `CLR_DIM`-piksler på skrivebordet og 134 i panelet.
**GDI/USER 30/14**, uendret gjennom 50 modusbytter.

**Etter fletting**, med produksjonsbygget i drift på ekte skjerm: flaten ligger
i WorkerW, 3840×1600, og et utsnitt på 2400×1000 av skrivebordet har 0
`CLR_AXIS`- og 0 `CLR_DIM`-piksler, med 25 210 piksler rutenett og lys. Ett
`CLR_TEXT`-treff dukket opp i én av tre kjøringer; `WindowFromPoint` viste at
det tilhørte et `XamlWindow`, ikke tickerflaten (fallgruve 54 igjen, nå på en
enkelt piksel). Tray-menyen har fortsatt alle seks punktene.

---

### Fase 15 — luft mot aksen og prislinja som bro

Plan og målinger: `docs/superpowers/plans/2026-09-17-ticker-prislinje-offset.md`.
Gren `prislinje-offset`.

**Endringen:** `ChartRect` skiller nå mellom `right`/`cw` (lysenes flate) og
`edge` (aksekanten, der stempelet og etikettene begynner). `PLOT_PAD_R` = 10 px
er luftrommet mellom dem, med `#error`-vakt på [8, 12]. Rutenettet,
klipperegionen, den stiplede siste-pris-linja og trådkorsets vannrette linje går
til `edge`; lysene stopper på `right`, fordi `slot` regnes av `cw`. Alt som
mapper x ↔ lysindeks leser `cw` og følger med uten egne endringer. I
skrivebordsmodus er `right == edge`: der finnes ingen akse.

**Linja tegnes til `edge + 1`.** `LineTo` tegner ikke sluttpunktet, så med
`edge` sto kolonnen `x = edge` tom — ett svart hull mellom linja og stempelet.
Pikselmålingen fanget det; øyet gjorde det ikke.

**Verifisert** med dump av bakbufferet, **11/11** ved 1280×720: `right` = 1186,
`edge` = 1196, siste lyspiksel utenom stempelbåndet på **x = 1184**, og
nøyaktig **én** rad med lysfarge i luftrommet — den stiplede linja.
`x = edge` og `x = edge + 1` er begge `0x00FF66`: linja møter stempelet uten
brudd. **GDI/USER 30/14**, uendret gjennom 40 resizer og 40 tittelbytter.
Skrivebordsmodus uendret, høyeste lyspiksel x = 3839 av 3839.

**Stiplingen brøt likevel, og ble rettet.** Første versjon tegnet hele linja
med `PS_DASH` inn til `edge + 1`, og 20 av 20 målte bredder fikk kontakt.
Målingen var ikke representativ: i produksjonsbygget, ved 1004 px, sto
`x = edge` på bakgrunnsfargen — mønsteret endte i et «av»-intervall. `PS_DASH`
gir ingen kontroll over fasen ved linjeslutt. Linja er nå **stiplet over
dataflaten og heltrukket over luftrommet** (`right → edge + 1`, `DC_PEN` i
samme farge). Etter rettelsen: kontakt i 20 av 20 bredder og i 12 av 12 rundt
998 … 1009 px, altså også bredden som brøt. Det er konstruksjon, ikke flaks.

**I produksjonsbygget**, lest med `PrintWindow`: **5/5** ved 1280×720. Én rad
med lysfarge i luftrommet, siste lyspiksel x = 1184 mot kanten 1186, og
`x = edge` og `edge + 1` begge `0x00FF66`.

---

### Fase 16 — pris-stempel i skrivebordsmodus

Plan og målinger: `docs/superpowers/plans/2026-09-17-ticker-skrivebordsstempel.md`.
Gren `skrivebordsstempel`.

**Endringen:** skrivebordsflaten har fått tilbake en høyre marg — ikke til
akseetiketter, men til det ene stempelet med siste pris. `ChartGeometry` setter
`edge = W - DeskAxisW(H)` i skrivebordsmodus, og `right = edge - PLOT_PAD_R` i
begge modi. Venstre, topp og bunn er fortsatt kant til kant.

- `DeskPillH(H)` = `H / 40`, klemt til [16, 48]. `DeskPillFontH` er
  `MulDiv(pillH, 15, 16)` — samme forhold som panelets 16 px stempel rundt en
  15 px font. `DeskAxisW` runder tegnbredden **opp**; med nedrunding ville åtte
  tegn fått 6 px for lite, og prisen ville falt stille tilbake til aksens
  oppløsning.
- Alle tre er rene funksjoner av `H`, fordi `ChartGeometry` også kalles fra
  treffdeteksjon og panorering, der det ikke finnes noen DC å måle i.
- `hFontPill` bufres etter høyde, som `hFontWm`, og frigis med de andre fontene.
- Stempelet tegnes nå i begge modi. Akseetiketter, tidsakse og header er
  fortsatt borte fra skrivebordet.

**Fase 14s invariant er endret:** «null alfanumeriske piksler på skrivebordet»
er nå «nøyaktig ett tekstelement». `CLR_AXIS`, `CLR_TEXT` og `CLR_DIM` er
fortsatt null — stempelteksten er `CLR_BG` på mettet flate — men rutenettradene
spenner til `edge`, ikke til `W - 1`.

**Verifisert** med dump av bakbufferet, **15/15**, og panelproben fra fase 15
fortsatt **11/11**. Ved 3840×1600: stempel 40 px, font 38 px, marg 196 px, så
`edge` = 3644. Rutenettradene ligger på 400/800/1200 med utstrekning x 0 … 3643.
Én rad med lysfarge i luftrommet, siste lyspiksel på x = 3618, stempelhøyden
målt til 40 px i kolonnen `edge + 1`, og teksten innenfor `[3648, 3832)`.
**GDI/USER 31/14**, uendret gjennom 50 modusbytter.

**Teksten er trygt mørk:** flaten er lagdelt med `LWA_ALPHA 255`, ikke
fargenøkkel. Med `LWA_COLORKEY` på `CLR_BG` ville sifrene blitt hull ut til
tapetet.

### Fase 17 — symbol og intervall fra tray-menyen

Plan, målinger og avvik: `docs/superpowers/plans/2026-09-18-ticker-tray-symbol-intervall.md`.
Gren `tray-symbol-intervall`, flettet inn med `--no-ff`.

**Endringen:** tray-menyen har fått to undermenyer øverst, «Symbol» og
«Intervall», med skillelinje under. Ett punkt per rad i `SYMBOLS[]` /
`INTERVALS[]`, etikett lik overlayets, radiohake på gjeldende indeks
(`CheckMenuRadioItem`). Dermed kan skrivebordsmodus bytte uten å gå veien om
panelet, og panelet kan bytte uten å åpne overlayet.

- `ID_TRAY_SYMBOL_FIRST` (1100) og `ID_TRAY_INTERVAL_FIRST` (1200), begge
  100 brede. Tre `C_ASSERT` under tabellene stopper bygget om en tabell
  vokser forbi området sitt. `#if` går ikke: `SYMBOL_COUNT` er `sizeof`.
- `WM_COMMAND` gjør områdesjekk først, som `ID_TRAY_RESET`: en postet ID
  utenfor tabellene er en stille no-op. Innenfor oversettes den til samme
  `hit`-koding som `OverlayHit`, og `ApplyConfigChoice` er felles for begge
  veier.
- **`ApplyConfigChoice` tar ikke lenger noe HWND.** Eneste bruk var
  `InvalidateRect(hwnd)` til slutt. Fra menyen er riktig vindu `hPopup`, som
  er NULL når panelet er lukket, og `InvalidateRect(NULL, …)` tegner hele
  skrivebordet på nytt. Funksjonen leser `hPopup` selv og hopper over
  invalideringen uten vindu. Alt annet i den var allerede vindussikkert.
- Undermenyene henges på med `MF_POPUP` og eies av hovedmenyen; `DestroyMenu`
  i `WM_TRAYICON` river ned alle tre.
- **Duplikater får undermenyene.** Overlayet lar dem alt bytte sin egen
  visning, og `SaveConfig` hopper over duplikater selv.

**Verifisert** i testbygg mot `Software\TickerTest`, **40/40 i to kjøringer**:
menyinnhold, IDer og radiohaker; valg med panelet lukket (registret skrives,
ingen popup, ingen krasj, panelet åpner med riktig par); bytte tømmer
bufferet synkront og fyller det igjen; samme valg rører ingenting; IDer
utenfor området rører ingenting; skrivebordsmodus tegnes på nytt med nytt
symbol, og bildet med tomt buffer har **null** piksler over luminans 120;
duplikat bytter lokalt uten å røre registret. **GDI/USER 33/14** etter
oppvarming og etter hver av tre runder med 50 menyer og 40 valg.

**I produksjonsbygget**, som kjører i skrivebordsmodus og bare ble lest, ikke
klikket: menyen har 9 punkter i riktig rekkefølge, undermenyene 4 og 6, og
radiohakene står på BTC/USDT og 1m, lik `SymbolIndex` 0 og `IntervalIndex` 0
i `Software\Ticker`. Undermenyenes egne punkter har ID −1 (`MF_POPUP`), som
`WM_COMMAND` aldri ser.

### Fase 18 — historikk på forespørsel

Plan, målinger og avvik: `docs/superpowers/plans/2026-09-18-ticker-historikk.md`.
Gren `historikk`, flettet inn med `--no-ff`. Valgt av agenten etter fri
gjennomgang; begrunnelsen står i planen.

**Endringen:** panorerer brukeren inn i veggen (`viewStart` 0), ber UI-tråden
om eldre lys, og arbeidertråden henter `SEED_COUNT` lys med
`endTime = candles[0].openTime - 1` før den vanlige hentingen i samme syklus.
`PrependCandles` legger dem foran, flytter utsnittet like mye og teller
`frontShift` ned, så bildet ikke rører seg.

- **`evictedTotal` er blitt `frontShift`**, med fortegn: +1 per utkasting,
  −k per bakfylling. `ApplyEviction` er blitt `ApplyFrontShift` og flytter
  `dispStart`, `hoverIdx` og `panAnchorView` begge veier. Panoreringsblokka
  kaller den under lås før den leser ankeret.
- **Ankeret glir i veggen.** Klemmer `ClampView`, flyttes ankeret dit
  utsnittet faktisk står. Før husket det overskytingen: et drag 15 lys forbi
  veggen ga et hopp på 16 lys ved neste museflytt etter at lysene kom, og et
  drag tilbake fra veggen sto stille like lenge.
- **`HttpGet` sjekker statuskoden.** Ikke-2xx er FALSE. Før talte en 429 med
  JSON-kropp som suksess, og parserne fanget det stille. Bakfyllingen trenger
  skillet: 2xx med null lys er «historikken er slutt», alt annet backoff.
- **`RequestHistory` vekker tråden bare når `netFailures` er 0.**
  `hWakeEvent` nullstiller backoffen, og et drag i veggen under en frakobling
  skal ikke slå den av. I backoff ser tråden flagget på sin egen syklus.
- **`histDone`** på 2xx uten lys, på svar der ingenting var eldre, og på
  fullt buffer. Nullstilles med `histPending` der `candleCount` settes til 0.
- **`MAX_CANDLES` 6000.** Levende lys kastes aldri for å gi plass til gamle.
- **`WM_APP_PROBE`** bak `#ifdef TICKER_PROBE`: testbyggets vindu mot indre
  tilstand. Produksjonsbygget har ikke meldingen.

**Verifisert:** enhetsharness **36/36** mot de faktiske funksjonene
(`PrependCandles`, `ApplyFrontShift`, `MergeCandles`, `HttpGet` mot Binance
med 400, 404, 200 og `[]`). Ende til ende **33/33 i to kjøringer**: bakfylling
landet etter 353–372 ms, `viewStart` 0 → 300 med samme eldste synlige
tidsstempel og pikselidentisk bilde; drag i veggen gir ett lys per spor etter
landing, ikke hopp; hjulspam under henting; SOL/USDT 1d uttømmes på 8 runder
til **11.08.2020**, noteringsdagen; BTC/USDT 1m fyller 6000 på 19 runder;
`R` gir siste 300; **GDI/USER 30/14** flatt.

### Fase 19 — tastatursnarveier for kontrollknappene

Plan og målinger: `docs/superpowers/plans/2026-09-18-ticker-tastatursnarveier.md`.
Gren `tastatursnarveier`, flettet inn med `--no-ff`. Valgt av agenten blant
fire kandidater; begrunnelsen og det som ble lagt bort (DPI-skalering av
stempelet, maskinen står på 100 %) står i planen.

**Endringen:** `WM_KEYDOWN` i `PopupProc` kjenner `Ctrl`+`N` (`[ + ]`),
`Ctrl`+`M` (minimer), `F11` (maksimer/gjenopprett) og `Ctrl`+`W` (lukk).
Alle fire går gjennom `OnButtonClick`, så tast og klikk deler samme sti —
geometrien lagres før maksimering, en gjenopprettet rekt utenfor alt synlig
fanges, og et duplikat avsluttes av lukking. `Alt`+`F4` virket fra før:
`DefWindowProc` sender `SC_CLOSE` også uten `WS_SYSMENU`, målt.

- **Sperret** midt i en panorering (`panning`): en minimering under drag
  ville hoppet over `WM_LBUTTONUP`, som slipper capture og setter pekeren
  tilbake. Sperret i skrivebordsmodus, som aldri får tastaturfokus uansett.
- **`staleSecsShown`** nullstilles i `WM_APP_DATA` når linja er oppe.
  Telleren starter på 9, så 0 er aldri et ekte sekundtall.
- Snarveiene arver `ESC`-forbeholdet: de krever tastaturfokus i panelet.

**Verifisert** ende til ende, **23/23 i to kjøringer**, i testbygg mot
`Software\TickerTest`. Tastene sendes som ekte tastetrykk med `SendInput`
etter at proben har bekreftet forgrunn og fokus på panelet; en kontroll med
`Ctrl`+`0` (900×500 → 1280×720) beviser først at injeksjonen og
`Ctrl`-tilstanden når fram. Rød kjøring mot urørt kode: 14 OK, 6 FAIL, med
kontrollen grønn. `F11` → maksimert og tilbake til 1280×720; `Ctrl`+`M` →
minimert; `Ctrl`+`N` → én ny prosess med panel på +30/+30, avsluttet av
`WM_CLOSE`; `Alt`+`F4` og `Ctrl`+`W` → skjult, åpner igjen; 20 runder
`F11`/`F11`/`Ctrl`+`M`/gjenopprett; **GDI/USER 30/14** før og etter.

**Ikke testet:** `staleSecsShown`-grenen (krever kuttet nett, fallgruve 9)
og panoreringssperren (krever tast midt i et ekte drag). Begge er lest.

---

### Fase 20 — tastaturnavigasjon i grafen og tapt capture

Plan og målinger: `docs/superpowers/plans/2026-09-18-ticker-tastaturnavigasjon.md`.
Gren `tastaturnavigasjon`, flettet inn med `--no-ff`. Valgt av agenten blant
fem kandidater; det som ble lagt bort (volum, hvilemodus, oppløsningsbytte,
DPI-stempelet) står i planen med begrunnelse.

**Endringen, i to commits.** Først `PanView` og `ZoomView`, trukket ut av
`WM_MOUSEWHEEL` uten atferdsendring, pluss probe-felt 12 (`panning`) — det
er bygget rød-kjøringen gikk mot. Så tastene i `WM_KEYDOWN`, etter fase
19-blokka og før `ESC`-lagene: `←`/`→` ett hjulhakk, `PgUp`/`PgDn` et helt
utsnitt, `Home` til veggen (som ber om historikk, fase 18), `End` til den
levende kanten, `+`/`-` ett zoomtrinn om midten — `VK_OEM_PLUS`/`VK_ADD` og
`VK_OEM_MINUS`/`VK_SUBTRACT`, `Ctrl` tillatt på disse to og ikke på de
andre. Samme sperrer som hjulet: ikke med overlayet åpent, ikke i
skrivebordsmodus, ikke under panorering. Hover nullstilles som `R` gjør —
å regne lyset under pekeren på nytt ville latt trådkorset gli med lyset
under easingen og bli stående forskjøvet fra pekeren.

**`WM_CAPTURECHANGED`:** står `panning` og `lParam` er et annet vindu enn
oss, slippes panoreringen og pekeren settes tilbake. Vår egen
`ReleaseCapture` sender også meldingen, men da er `panning` alt `FALSE`.
Probe-felt 13 leser `GetCapture() == hwnd` fra appens tråd.

**Verifisert** ende til ende, **52/52 og 51/51 i to kjøringer** (den andre
uten `Alt`+`Tab`-grenen, se under), i testbygg mot `Software\TickerTest`.
Tastene sendes med `SendInput` etter forgrunns- og fokuskontroll som i fase
19; kontrollen først er `Ctrl`+hjul via `SendMessage` (300 seedede lys og
`DEFAULT_VIEW` 300 gir veggen fra start, så utsnittet må smalnes før
panorering kan måles). Tilstand leses med `WM_APP_PROBE`.

- **Rød kjøring** mot del 1: kontrollen grønn, alle tastene røde
  (16 OK, 23 FAIL). Rød kjøring av capture-stien mot et bygg med handleren
  koblet ut: menyen tok capture, `panning` ble stående, `Ctrl`+`M` sperret.
- `←` → `vs − vc/8`, `followLive` av, 46 000–51 000 ulike piksler i
  grafbåndet (`PrintWindow` før/etter); `→` tilbake; `PgUp` → veggen, 300
  lys kom, `PgDn` → kanten; `Home` → utsnittet starter på det eldste lyset
  før *og* etter bakfyllingen (samme `openTime`, `vs` = antall nye lys);
  `End` → `followLive`; `+` → `vc / 1,2` om midten, `-` tilbake, numerisk og
  `Ctrl`+`+` likeså. Layoutets `+` er `VK_OEM_PLUS` (`VkKeyScan` 0xBB).
- Overlayet åpent → `←` gjør ingenting; `ESC` → virker igjen. Hover med
  ekte peker → `←` → `hoverIdx = −1`.
- **Capture:** ekte drag med `SendInput` (`pan=1`, `cap=1`). `Alt`+`Tab`
  tok capture i to av fem kjøringer med ekte drag og lot panelet beholde
  den i tre — forgrunnen byttet hver gang. Den deterministiske tyven er
  **tray-menyen**:
  `TrackPopupMenu` i samme tråd tar capture hver gang, `panning` slippes,
  og `Ctrl`+`M` minimerer etterpå. `ESC` lukker menyen *før* museknappen
  slippes (fallgruve 56).
- **GDI/USER 30/14** i hvile ved start; **32/14** etter første overlay og
  **35/14** etter første tray-meny, begge uendret over tre sykluser til og
  gjennom 30 runder `←`/`→`/`+`/`-`. Hoppene er engangs (fallgruve 65),
  ikke lekkasjer, og de samme i bygget uten fase 20.

**Ikke testet:** capture tatt av et vindu i en annen prosess
(`SetCapture` på tvers), og `Win`-tasten. Begge går gjennom samme melding.
Hjulet *uten* `Ctrl` — panorering gjennom `PanView` — er heller ikke målt
etter refaktoreringen; kontrollen brukte bare `Ctrl`+hjul (`ZoomView`, som
ga `vs` 68 / `vc` 173 før og etter i hver kjøring). Stien er lest.

---

### Fase 21 — volumstolper under lysene

Plan og målinger: `docs/superpowers/plans/2026-09-18-ticker-volum.md`.
Gren `volum`, flettet inn med `--no-ff`. Valgt av agenten blant fire
kandidater; hvilemodus og oppløsningsbytte ble lagt bort fordi ingen av dem
kan observeres i en probe på denne maskinen (begrunnelsen står i planen).

**Endringen, i to commits.** Først instrumentering: `Candle.volume`
(48 byte per lys, bufferet 288 KB), probe-felt 14 (volum ved indeks, ×100 —
`LRESULT` er 32 bit på x86) og 15 (siste fulle opptegning i µs, QPC rundt
den trege stien i `PaintPopup`, bare testbygg). Så funksjonen:
`ParseKlines` leser felt 5 (sitert streng som OHLC); `VolumeMax` over
målutsnittet der `PriceRange` regnes; `dispVolMax` eases i `WM_TIMER` som
femte verdi, snapp = en kvart piksel av båndhøyden; stolpene tegnes etter
rutenettet og før lysene, innenfor samme klipp, i de nederste `VOL_FRAC` =
22 % av grafflaten, `bodyW` brede på lysets `cx`, nederste rad på
`y = bottom` inklusiv. Skalaen tegningen leser er *visningen*, aldri
målet. Retningen er lysets egen (close mot open). Ett `PolyPolygon` per
farge og bolk på 256 stolper med `NULL_PEN`, som fyller nøyaktig
`FillRect`-pikslene. Hover-boksen får en `V`-rad (74 → 87 px) med
`K`/`M`-format. **Skrivebordsmodus tegner stolpene også:** fase 14 fjernet
det som må leses fovealt (tall); stolper leses perifert som lysene.
`ChartGeometry`, `HitCandle`, aksene og stempelet er urørt.

**Verifisert** ende til ende, **24/24**, i testbygg mot
`Software\TickerTest`. Rød kjøring mot commit 1: volum 0 i alle seks lys,
0 stolpepiksler, boks 72 px (8 FAIL). Grønn: volum > 0 i seks lys spredt
over bufferet (3,16–14,44 BTC på 1m); **5 748** eksakt stolpefargede
piksler i panelet, alle i `[559, 702]` = nøyaktig båndet `[bottom + 1 −
144, bottom]`, ingen over det, ingen utenfor grafflaten i x, høyeste
stolpe når båndets topp; hover-boksens lengste loddrette `CLR_BOX`-løp
**85** (72 før); skrivebordsflaten 3840×1600 fanget med `PrintWindow`
under WorkerW: 47 019 stolpepiksler i `[1249, 1599]`, båndtopp 1248;
GDI/USER stabile (32/14 → 33/14 etter et modusbytte fram og tilbake).
Hvilenivået 32/14 er to høyere enn fase 20s 30/14 fordi de to
stolpepenslene lages ved oppstart; +1 etter modusbytte fantes også i
bygget uten fase 21 (30 → 31 i rød-kjøringen).

**Rettelse etter fletting:** `PolyPolygon` fylte med ALTERNATE. Med flere
lys enn piksler (`vc > cw`) er `slot` under 1, `bodyW` klemmes til 1, og
nabolys lander på samme `cx`; to like rektangler i samme bolk nuller
hverandre under partall/oddetall-regelen, så stolpen forsvant.
`SetPolyFillMode(WINDING)` rundt bolkene. Målt i proben: to like
rektangler 10×16 gir 0 piksler under ALTERNATE og 160 under WINDING; fullt
utzoomet med 1800 lys på 1176 px har raden `y = bottom` stolpefarge i
873 av 1176 kolonner under ALTERNATE (rød kjøring) og 1169 / 1166 av
1176 under WINDING (28/28 i to kjøringer). Fyllmodusen koster ikke målbart
(medianer 1,46–1,66 ms mot 1,58 i ALTERNATE-kjøringen).

**Opptegning ved 1280×720, 300 lys, median over 172 fulle bilder, samme
probe og samme kjøreforhold (ingen `PrintWindow` imens):** 1,44 ms uten
stolper (commit 1), 1,84 ms med ett `FillRect` per lys (+0,40 ms),
**1,56 ms** med `PolyPolygon` i bolker (+0,12 ms). p90 1,63 / 2,08 /
1,75 ms. Bolkevarianten ble valgt på tallene; `FillRect`-varianten hadde
også 24/24. Tabellen i *Målinger* er oppdatert.

**Ikke testet:** et par med volum 0 i hele utsnittet (`dispVolMax` 0 →
ingen stolper; grenen er lest), og `K`/`M`-formatet i hover-boksen
(BTC-volum på 1m er under tusen).

### Fase 22 — verktøylinje i headeren

Plan og målinger: `docs/superpowers/plans/2026-09-18-ticker-verktoylinje.md`.
Gren `verktoylinje`, flettet inn med `--no-ff`. Brukeren la fram to idéer —
verktøylinje og pris-varsler på prisaksen — og agenten valgte. Varslene er
lagt bort som kandidat: utløseren (levende pris krysser en linje) kan ikke
framprovoseres i en probe uten et *skrivende* probe-felt, og lyd og ballong
kan ikke observeres. Begrunnelsen står i planen.

**Endringen, i to commits.** Først instrumentering: `tbHot`, `showVol`,
`dispVolF` og probe-felt 16–21 (`ivIdx`, `symIdx`, `showVol`, `tbHot`,
`overlayOpen`, `dispVolF` × 1000). Så funksjonen: `ToolbarLayout` /
`ToolbarHit` / `ToolbarStrip` (ren funksjon av bredden, faste pillebredder,
`C_ASSERT` mot 400 px), `DrawToolbar` fra `PaintPopup`, `HTCLIENT` over
pillene i `WM_NCHITTEST`, hover og klikk ved siden av knappenes,
`OnToolbarClick` (intervall → `ApplyConfigChoice`, symbol → overlayet, VOL →
`SetShowVolume`), `dispVolF` som sjette easede verdi, `V` og `1`…`6`,
«Volumstolper» i tray-menyen (`ID_TRAY_VOLUME` 1005) og `ShowVolume` i
registret. Symbollinja som tekst er borte; `frakoblet Ns` står til høyre for
siste pille når hele teksten får plass. Se **Verktøylinja** under *Vinduet*.
`HEADER_H`, `ChartGeometry`, `HitCandle` og alle graf-y-er er urørt.

**Verifisert** ende til ende, **81/81 i to kjøringer**; rød kjøring mot
commit 1 ga 33 FAIL med kontrollene grønne. `HTCLIENT` på alle åtte piller
og `HTCAPTION` i hvert mellomrom; pilletilstander lest fra hjørnepiksler;
0 tekstpiksler innenfor 3 px fra en pillekant; pillene tegnet med tomt
buffer rett etter et bytte; `IntervalIndex` og `ShowVolume` i registret;
19 mellomverdier av `dispVolF` på veien til eksakt 0 og 0 stolpepiksler
(6 267 med VOL på); `DOWN` + `DBLCLK` = to vekslinger; hover med ekte
peker og `WM_MOUSELEAVE` ut i mellomrommet; alt tegnet og klikkbart ved
400×250; valgene overlever omstart uten animasjon. Opptegning 1 479 µs før,
1 606 / 1 517 µs etter (median, 168–177 bilder). GDI/USER 34/14 før og etter
(«før» tatt etter første overlay).

**Ikke testet:** den ekte tray-menyen (bare kommandoen), bryteren i
skrivebordsmodus, skjuling av piller under 400 px, og frakoblet-tekstens
nye plass.

### Fase 23 — prisvarsler på prisaksen

Plan og målinger: `docs/superpowers/plans/2026-09-18-ticker-prisvarsler.md`.
Gren `prisvarsler`, flettet inn med `--no-ff`. Brukeren la fram tre
kandidater — prisvarsler med prisinjeksjon i proben, re-initialisering etter
hvilemodus og oppløsnings-/DPI-bytte i skrivebordsmodus — og agenten valgte.
Hvilemodus er lagt bort som **anbefalt neste fase** (liten, men den ekte
hendelsen kan ikke drives fra en probe, fallgruve 47); DPI-byttet står som
kjent begrensning (én skjerm). Begrunnelsene står i planen.

**Avgjørelsen fase 22 ventet på:** testbygget har nå **skrivende**
probe-felt. De finnes bare bak `/DTICKER_PROBE`, bor på hovedvinduet, og
injeksjonen bærer prisen i meldingen og prøver utløseren synkront
(fallgruve 70). Produksjonsbygget har ikke meldingen.

**Endringen, i to commits.** Først instrumentering: tilstandsfeltene i
`AppContext`, lesende felt 22–32, skrivende 100 (injiser pris) og 101
(demp). Så funksjonen: `AlertHit` og `AlertRound` (rene), `AlertY` /
`AlertPriceAtY` / `AlertAxisHit` (leser `disp*`, fallgruve 14), `AlertAdd` /
`AlertRemove` / `AlertsClear`, `FireAlert`, `CheckAlerts` fra `WM_APP_DATA`,
`OnAxisClick`, hover i `WM_MOUSEMOVE` (`axisHotY`, `alertHot`, `alertFresh`),
hånden i `WM_SETCURSOR`, `A`-tasten, `alertFlashF` som sjuende easede
verdi, `SaveAlerts` / `LoadAlerts`, «Fjern prisvarsler (N)»
(`ID_TRAY_ALERTS_CLEAR` 1006), og tegningen i `DrawChart`: linjer bak
lysene, merker, spøkelse og etterglød. Se **Prisvarsler** under *Vinduet*.
`ChartGeometry`, `HitCandle`, låsens dekning og nettverkstråden er urørt.

**Rettet underveis, funnet på skjermbilde og ikke av proben:** stempelet for
siste pris lå oppå et varselmerke 12 px under, og et tall kuttet på langs
stakk fram. Et merke som er dekket av stempelet eller av et senere tegnet
merke (under 16 px) tegnes nå som ren flate. Proben fikk en sjekk for det.

**Rettet etter flettingen, funnet av produksjonsbygget:** exe-en vokste fra
187 392 til 216 064 byte. `AlertRound` brukte `pow(10, floor(log10(x)))`, og
de to kallene alene dro inn ~21 KB CRT-matematikk — for en avrunding med ni
mulige svar. Byttet mot en trapp (`q *= 10`) over 1 og deling på 10 eller
100 under 1 (deling, fordi 0,1 og 0,01 ikke finnes eksakt). **195 072 byte**
etterpå, +7,7 KB for hele fasen. Enhetstestene 20/20 og 109/109 i to nye
kjøringer på det bygget. Alle tidligere bygg i fasen var testbygg; bare
produksjonsbygget viste størrelsen (fallgruve 75).

**Verifisert.** Enhetstester på ekte kode (funksjonene limt ut av
`ticker.c`): **20/20** — `AlertHit` på begge sider, på nivået, pris 0 og
negativ, nivå 0; `AlertRound` for BTC 1m/1d og SOL, gulvet 0,01, og
egenskapen |avrundet − pris| ≤ en halv piksel over ca. 1 250 steglengder.
Ende til ende, **109/109 i to kjøringer**; rød kjøring mot commit 1 ga
**52 FAIL** med alle kontrollene grønne, også den skrivende proben selv
(felt 100 og 101 finnes i begge bygg). Klikk på rad 300 setter nivået
innenfor én piksel i pris (81 066,00 mot 81 065,82; linja på rad 299,
fallgruve 73), fortegnet følger siden, 1 074 ravpiksler i merket og 1 161 i
linja med lysene over; spøkelset er rammet (365 px) med linja på pekerens
rad; hånden over kolonnen; rødt merke under ekte peker, rav mens det er
nysatt; `A` med og uten trådkors; nærmeste merke fjernes; raskt dobbeltklikk
er sett + fjern og **nullstiller ikke** utsnittet, mens dobbeltklikk i
grafen fortsatt gjør det. Utløseren: en cent under fyrer ikke, *på* nivået
fyrer, én gang, registret ryddet med en gang; nedre varsel speilvendt;
pris 0 fyrer ikke; to av tre varsler forbi samme pris fyrer i samme prøve
og det riktige står igjen; nivå 0, duplikat og det niende avvises.
Etterglød 1 000 → mellomverdi → eksakt 0, 1 186 ravaktige piksler over
lysene og 0 etterpå. Symbolene er atskilt (ETH har ingen, BTC-varselet
overlever turen). Fyrer med panelet skjult, uten etterglød. **Ett varsel
udempet per kjøring:** `Shell_NotifyIconW(NIM_MODIFY, NIF_INFO)` svarte
`TRUE`. 400×250 virker. Overlever omstart med side; et register skrevet for
hånd (krysset nivå, NaN, −1e12, 0 og ett gyldig) gir én fyring på første
pris og ett varsel igjen. Opptegning med **åtte** varsler: +0,11 og
+0,17 ms (1 893 → 2 006 og 1 938 → 2 110 µs, median av 150) — åtte
`GetTextExtentPoint32W` + `DrawTextW`; ett eller to varsler er i støyen.
GDI/USER **34/14 før og etter** i alle tre kjøringer.

**Ikke testet:** at ballongen faktisk *vises* og lyden *høres* (bare svaret
fra `Shell_NotifyIconW`); den ekte tray-menyen (bare kommandoen);
varsellinjene i skrivebordsmodus (samme `DrawChart`, ikke fanget); utløseren
gjennom en ekte henting (bare injisert — stien fra `WM_APP_DATA` og ut er
den samme); et duplikats varsler; det grå spøkelset med fullt sett (klikket
er testet, fargen ikke); merke dekket av et *annet merke* (bare av
stempelet).

### Fase 24 — sunne inndata og oppvåkning fra dvale

Plan og målinger: `docs/superpowers/plans/2026-09-18-ticker-robuste-inndata.md`.
Gren `robuste-inndata`, flettet inn med `--no-ff`.

**Mandatet var et arkitekturdirektiv:** behold C nær Win32, flytt «høyere
logikk» til C++ — `std::vector` og RAII i stedet for `malloc`/`realloc`/`free`,
`std::string` og nlohmann/json for API-svarene, klasser rundt SMA/EMA/RSI —
med null lekkasjer og feiltoleranse mot nettbrudd og ugyldige svar som
overordnet krav, og full frihet til å forme det. **Direktivet beskriver en
annen kodebase enn denne.** Det finnes ingen `malloc` å erstatte (alle
buffere er statiske, se *Datalag*), ingen indikatorer å kapsle inn, og én
kildefil. Midlene ble derfor **målt, ikke adoptert** — tallene står under
*Avviste forslag* — mens målet i direktivets punkt 3 ble fasen: finn det som
faktisk kan gå galt med inndata og nett, og rett det.

**Funnet ved gjennomlesing:** parserne stolte på `atof`, som ikke kan feile.
`"price":"abc"` ga 0.0 og `TRUE`, `"1e999"` ga inf, `"nan"` ga NaN — rett inn
i `lastPrice` og `candles[]`. En inf i et lys sprenger Y-skalaen (og
`double → int` i koordinatene er udefinert oppførsel), en NaN-pris tegner et
blankt ikon. Et lys med usiterte felt lånte tallene fra *neste* lys, fordi
letingen etter hermetegn ikke stoppet ved klammene. `PrependCandles` antar
stigende tid uten at noe garanterte det. Og et 2xx-svar med bare søppel satte
`histDone` for godt. Ingenting av dette er sett fra Binance — men appen står
på i ukevis, og «serveren bestemmer» (kommentaren i `PrependCandles`).

**Endringen, i to commits.** Først instrumentering: tellerne 110–112 på
hovedvinduet (hentesykluser, oppvåkninger, forkastede verdier), bare i
testbygget. Så funksjonen: `PriceSane`, `CandleSane`, `ParseQuotedNumber`
(`strtod` med sluttpeker: ett helt tall mellom hermetegnene, ellers NULL);
`FastParsePrice` rører ikke ut-verdien uten en sunn pris; `ParseKlines`
hopper over usunne lys og teller dem i `*rejected`; `WorkerFetchHistory`
regner «null lys, noen forkastet» som en feil og ikke som slutten på
historikken. `WM_POWERBROADCAST` / `PBT_APMRESUMEAUTOMATIC` setter `dropConn`
(nytt felt i låsedomenet) og `hWakeEvent`; tråden slipper `hConnect` først i
neste syklus (teller 113). `MergeCandles`, `PrependCandles`, backoffen og
all tegning er urørt. **Exe 195 072 → 195 584 byte (+512).**

**Verifisert.** Enhetstester på ekte kode: **31/31**, rød kjøring mot
commit 1 ga **22 FAIL** — tekst, tom streng, 0, negativ, nan, inf, overflow,
avkuttet svar og søppel etter tallet for prisen; `high < low`, nan, inf,
nullpriser, negativt volum, open utenfor spennet, tid bakover, duplisert tid,
`openTime` 0, usiterte felt, for kort array og søppel etter et tall for
lysene. Ende til ende (`probe_resume.c`), **22/22 i to kjøringer**, rød
kjøring **7 FAIL**: oppvåkning gir ny henting etter **281–297 ms** tre av tre
i begge kjøringer (hvilesyklusen er 3 000 ms), forbindelsen slippes hver
gang og neste henting lykkes over den nye; `PBT_APMSUSPEND`, `PBT_APMRESUMESUSPEND` og
`PBT_APMPOWERSTATUSCHANGE` vekker ikke; ti oppvåkninger på rad gir 2
hentinger (auto-reset-hendelsen slår dem sammen); med panelet åpent går
**300 ekte lys gjennom den nye parseren med 0 forkastet**, og oppvåkning
vekker også lysgrenen (297 ms). GDI/USER **23/5 før og etter** 13
oppvåkninger; ren avslutning, kode 0. Proben sender ingen taster eller klikk
og trenger ikke en inaktiv maskin.

**Ikke testet:** ekte dvale. Proben *sender* `WM_POWERBROADCAST`; at Windows
leverer den til et skjult toppnivåvindu er dokumentert, men ikke målt her, og
fallgruve 47 er nettopp en sendt melding som var grønn mens den ekte
hendelsen avslørte en feil. Oppførselen når nettet ikke er oppe ved første
forsøk (forventet: én feil, så 6 s) er lest, ikke kjørt. Fase 23-proben
(prisvarsler, 109 sjekker) er **ikke kjørt på nytt** — den krever en inaktiv
maskin; stien fra `WM_APP_DATA` og ut er urørt. Et usunt lys *ende til ende*
(bare i enhetstestene — det finnes ikke noe skrivende probe-felt for et helt
svar), og bakfyllingen (fase 18) gjennom den nye parseren (samme funksjon som
seed-svaret, men stien med `rejected` er bare lest).

### Fase 25 — glidende snitt: SMA 20 og EMA 50

Plan og målinger: `docs/superpowers/plans/2026-09-19-ticker-indikatorer.md`.
Gren `indikatorer`, flettet inn med `--no-ff`.

**Mandatet** listet SMA/EMA i C, `WM_DISPLAYCHANGE`/`WM_DPICHANGED` i
skrivebordsmodus, og eget initiativ. **Valgt: SMA/EMA** — det eneste av de
tre brukeren ser hver gang panelet åpnes, og det fase 24 lovet («indikatorer
er ønsket, bygg dem i C»). Skjermbytte står som neste kandidat, med en
testbar utforming i planfila.

**Ingen tabell.** Snittene lagres ikke. `IndState` er en stegmaskin på 40
byte: `IndStep(&s, candles, i)` mates ett lys og gir verdien i det den
faller ut. SMA er en rullende sum, startet `period − 1` lys før første
tegnede lys (`IndFeedStart`), så summen aldri lever lenger enn ett bilde og
ikke kan drive. EMA er sådd med SMA av de første 50 lysene og går videre med
`v += k·(close − v)`, `k = 2/51`; den har uendelig hukommelse og mates
**alltid fra lys 0**, ellers ville linja avhenge av hvor utsnittet begynner
og flytte seg under panorering. Bare `+ − × ÷` — ingen `pow`/`log`
(fallgruve 75). Punktene legges rett i `s_volPts`, volumstolpenes buffer,
som er ferdig brukt når linjene begynner: **overlegget har null byte eget
statisk minne.**

**Tegningen** (`DrawIndicator`): `Polyline` med `DC_PEN` i bolker på 1024
punkter, siste punkt i en bolk er første i neste. Etter lysene og før
`SelectClipRgn(NULL)` — *over* lysene (en dempet 1 px linje bak mettede
lyskropper forsvinner der den krysser prisen), under siste-pris-linja og
trådkorset. Linja går ett lys ut på hver side og forlater flaten gjennom
klippet. `floor` på x, fordi lyset utenfor venstre kant har negativ
forskyvning der `(int)` runder mot null. `y` klemmes til ±16 flatehøyder
(GDI regner i 27 bit). **`PriceRange` er urørt:** prisaksen ser ikke
snittene, og en linje utenfor prisområdet klippes, som i TradingView. Begge
modi — en kurve er ikke tekst (fase 14).

**Forklaringen** står øverst til venstre i grafflaten i linjenes egne
farger — `SMA 20  81162.66    EMA 50  81196.25` — med verdien på lyset
under trådkorset, ellers siste synlige lys. Verdien faller ut av samme
gjennomløp som tegner linja. Bare i panelet, og bare når hele teksten får
plass (~323 px; et avkuttet tall er et feil tall).

**Bryteren** følger VOL (fase 22): `showInd` (registret, `ShowIndicators`,
på som standard), `dispIndF` ∈ [0, 1] eased i `WM_TIMER` — her som *farge*
mot `CLR_BG` (`Blend`), τ 55 ms, snapp 0,02 — og snappet når flaten ikke
synes. `MA`-pille, tasten `M` (uten Ctrl; `Ctrl`+`M` minimerer) og «Glidende
snitt» i tray-menyen (`ID_TRAY_INDICATORS` 1007), som er veien inn i
skrivebordsmodus. **Verktøylinja var full** (x = 310 av 312 på 400 px):
`MA`-pillen er det ene, bevisste unntaket fra `C_ASSERT`-regelen og skjules
under 426 px av den regelen som alltid har stått der. `POPUP_MIN_W` er ikke
hevet.

**`SEED_COUNT` 300 → 360.** Første skjermbilde viste EMA-linja begynne en
sjettedel inn i grafen: med 300 av 300 lys synlige er lys 0–48 udefinert.
Første henting (og hver bakfyllingsbolk) tar nå 360 lys; standardutsnittet
er fortsatt de siste 300, så oppvarmingen ligger utenfor venstre kant.
`C_ASSERT(SEED_COUNT >= DEFAULT_VIEW + IND_EMA_PERIOD)`. Svaret er ~60 KB av
`s_httpBuf` på 96.

**Verifisert.** Enhetstester på ekte kode (`unit_ind`): **20/20**, rød mot
commit 1 (funksjonene finnes ikke). 6000 pseudotilfeldige lukkekurser mot
uavhengige referanser: største avvik SMA 0, EMA 1,5·10⁻¹⁰; rullende sum
startet midt i bufferet over 3000 lys: 2,5·10⁻¹⁰. Testene fant én ekte feil
før den ble committet: periodevernet lå i `IndInit`, men ikke i
`IndFeedStart`. Ende til ende (`probe_ind.c`): **54/54 i to kjøringer**, rød
kjøring **29 FAIL**. Appens verdier (felt 36/37) mot probens egen utregning
av lukkekursene (felt 38) på fire lys, innen 2 cent; SMA i 1126–1130 og EMA i
1136 av 1136 kolonner, null linjepiksler utenfor grafflaten, en
linjepiksel innen 2 px av utregnet (x, y) på tre lys per linje; `M` toner
`dispIndF` gjennom mellomverdier til 0 på ~220–250 ms og skriver registret
med en gang; pillen på (325, 35) skrur på, mellomrommet VOL|MA treffer
ingenting; tray-kommandoen; omstart med `ShowIndicators` = 0 gir null linjer
fra første bilde; 410 px skjuler pillen mens `M` virker, 430 px viser den;
bakfylt til ~4700–5000 lys og zoomet helt ut er EMA sammenhengende i 1136
av 1136 kolonner. Skrivebordsmodus sett i `PrintWindow` ved 3840×1600: 4165 +
3730 linjepiksler, ingen forklaring. **Opptegning:** de to linjene koster
**52–59 µs** per bilde med 300 synlige lys og **179–181 µs** med 4300–4700
(median, QPC rundt blokka i testbygget, felt 39) — 3 % av en opptegning på
~1,9 ms. Å måle det som *differansen* mellom hele opptegningen med og uten
snitt lot seg ikke gjøre: fem vekslende runder ga −139, +63, +78, +80 og
+196 µs ved 300 lys i fem kjøringer — støygulvet er større enn det som måles
(fallgruve 80). **GDI/USER 32/14 før og etter** 12 bytter med toning og 1200 bilder. **Exe
195 584 → 199 168 byte (+3 584)**; commit 1 alene 0; `/TP` identisk; uten
`wcscat_s` samme tall — veksten er koden, ikke CRT.

**Proben trenger en inaktiv maskin likevel.** Den sender ingen `SendInput`
og flytter ikke pekeren, og første utgave startet derfor uten å vente. I
en grønn kjøring kom brukeren tilbake (siste inndata 0,3 s gammel da det
ble undersøkt) og må ha holdt `Ctrl` idet proben postet `M` — panelet sto
minimert med `showInd` urørt, og `Ctrl`+`M` er eneste vei dit: appen leste `Ctrl`+`M`, minimerte panelet som den skal, og
proben — som fanget et 0×0-vindu og talte piksler i 1280×720 — døde med
tilgangsfeil etter fire FAIL. Ingen produktfeil; proben venter nå på 25 s
uten inndata, venter ut modifikatortaster før hver postet tast, og avbryter
med kode 4 når fangsten ikke har den størrelsen den regner med (fallgruve 84).

**Ikke testet:** trådkorsets innvirkning på forklaringen er sett i et
skjermbilde (ekte peker, 80959.24 på lyset 02:22 mot 81157.62 på siste lys),
ikke i proben — en postet `WM_MOUSEMOVE` holder ikke hover (fallgruve 35).
Tray-*menyen* er ikke åpnet; kommandoen den sender, er. Utkasting i front
ved fullt buffer (6000 lys) mens linjene vises er lest, ikke kjørt: EMA
mates fra det nye lys 0 og flytter seg med under 10⁻⁹ av prisen etter ~1000
lys. Fase 23- og fase 24-probene er ikke kjørt på nytt; `WM_APP_DATA`-stien,
parserne og varslene er urørt, men **`SEED_COUNT` er endret** og eldre prober
som forventer 300 lys etter første henting, vil feile på det tallet.

### Fase 26 — skrivebordsflaten: egne overleggsvalg og skjermbytte

Plan og målinger: `docs/superpowers/plans/2026-09-19-ticker-skrivebordsflate.md`.
Gren `skrivebordsflate`, flettet inn med `--no-ff`.

**Tilbakemelding fra bruk: «nå vises volum og MA i bakgrunnsbildet».** Lest
som at det ikke hører hjemme der, og det stemmer med fase 14s egen regel:
flaten leses perifert bak ikonene, og alt som må dekodes ble fjernet.
Volumstolpene har likevel stått på skrivebordet siden fase 21 uten noen
modussjekk, og fase 25 la snittlinjene oppå med begrunnelsen «en kurve er
ikke tekst». Den begrunnelsen var feil — stolper og snitt er måleverktøy,
ikke tapet (fallgruve 85). Brukeren sto i panelmodus under fase 25 og så
begge på skrivebordet først etterpå.

**Ett valg per modus, ikke hardkodet bort.** Fase 22 la «Volumstolper» i
tray-menyen nettopp for at skrivebordsmodus skulle kunne bytte. `showVol` /
`showInd` er panelets (standard på, uendret); `showVolDesk` / `showIndDesk`
er skrivebordets (`ShowVolumeDesktop` / `ShowIndicatorsDesktop`, **standard
av**). `ShowVolNow()` / `ShowIndNow()` gir valget for modusen prosessen står
i — alt som tegner, easer, haker av i tray-menyen eller svarer en probe
(felt 18/34) leser dem, og `SetShowVolume` / `SetShowIndicators` skriver
modusens felt. `dispVolF` / `dispIndF` snapper ved modusbytte, og
oppstartssnappen er flyttet til etter at modusen er kjent (den sto rett
etter `LoadConfig`, før `--desktop-mode` og `DesktopMode` var lest). To
klikk i tray-menyen gir overleggene tilbake på skrivebordet.

**Skjermbytte.** Flaten er et `WS_CHILD` av WorkerW og får aldri
`WM_DISPLAYCHANGE`; det skjulte hovedvinduet er toppnivå og får den.
`PlaceDesktopSurface` er skilt ut av `AttachToDesktop`.
`RefitDesktopSurface` kjører i en per-monitor-v2-brakett som `TogglePopup`
(`GetSystemMetrics` følger trådens kontekst, og hovedtråden er uvitende):
sitter flaten ikke i dagens WorkerW, rives den og `WM_NCDESTROY` starter
gjenoppbyggingen (stien fra fase 9); ellers legges den på nytt. Uendret
geometri er en no-op; ny størrelse gir `WM_SIZE`, som kaster vannmerket,
og dobbeltbufferet og stempelfonten (H/40) er nøklet på størrelsen.
`TIMER_REFIT_ID` gjør det samme en gang til etter 1 s, fordi Explorer legger
sin egen WorkerW på nytt etter samme melding og origo regnes i dens
koordinater. `lParam` leses ikke (virtualisert). **`WM_DPICHANGED` håndteres
ikke, med vilje:** hovedvinduet får den aldri, og flaten regner i fysiske
piksler, så en ren skaleringsendring endrer ingenting for den.

**Verifisert** (`probe_desk.c`, per-monitor-bevisst som flaten, venter på
inaktiv maskin): **45/45 i to kjøringer**, rød kjøring **26 FAIL**. Rent
skrivebord: 0 stolpe- og 0 linjepiksler i en fangst på 3840×1600 med ~42 700
lyspiksler (rød: 35 112 / 4 144 / 3 725). Tray-kommandoene skriver
skrivebordets registerverdier og lar panelets stå; pikslene følger. Proben
krymper flaten til 1920×800 (stempelfont 38 → 19 px) og sender
`WM_DISPLAYCHANGE`: samme vindu tilbake på 0,0 3840×1600, fonten 38 igjen,
fullt bilde. Krympet uten melding: ettersjekken retter det innen 1,6 s.
Revet ut av WorkerW med `SetParent`: rives, bygges på nytt, valgene
overlever. I panelmodus rører meldingen ingenting. GDI/USER **30/6 før og
etter** sju skjermbytter. Modusbytte og omstart gir hver modus sitt valg fra
første bilde, uten animasjon. Fase 25-proben kjørt på nytt: **54/54**.
**Exe 199 168 → 199 680 byte (+512).**

**Ikke testet:** et ekte oppløsnings- eller skjermbytte — proben sender
meldingen og etterlikner virkningen (fallgruve 47). Om Explorer river
WorkerW ved et ekte bytte, om en skaleringsendring sender
`WM_DISPLAYCHANGE`, flere skjermer og bytte av primærskjerm er lest, ikke
kjørt; maskinen har én skjerm.

### Fase 27 — «Bloomberg Essentials»: VWAP, dagens høy/lav og verdier i hover-boksen

Plan og målinger: `docs/superpowers/plans/2026-09-19-ticker-bloomberg-essentials.md`.
Gren `fase27-bloomberg-essentials`, flettet inn med `--no-ff`.

**Bestillingen:** stiplede linjer for sessionens høy og lav bak lysene med
diskret etikett på aksen, VWAP som gyllen linje over lysene, og SMA/EMA/VWAP
som eksakte tall i hover-boksen — uten nytt minne og uten å røre
skrivebordet.

**Session = UTC-døgnet, ikke utsnittet.** Bestillingen sa «døgn/utsnitt» og
«VWAP for det synlige utsnittet». `PriceRange` legger 8 % luft rundt
utsnittets høy og lav, så linjer på *utsnittets* ekstremer ville stått på
samme sted i hvert bilde; og en VWAP forankret i første synlige lys ville
hoppet for hvert lys under panorering (fallgruve 90). `SessionStartAt` finner
døgnets første lys med binærsøk i `openTime`; på 1d-lys finnes ingen session.
**VWAP nullstilles per døgn** (`DrawVwap`), typisk pris (H + L + C) / 3, og
linja brytes ved døgnskiftet — så den er definert også når utsnittet står i
gårsdagen. Dagens høy/lav gjelder bare i dag og går fra døgnets første lys
inn til aksen. Alt er stegmaskiner og rene funksjoner som i fase 25: ingen
tabell, ingen nye buffere (`s_volPts` lånes til `Polyline` og `PolyPolyline`).

**Et ufullstendig døgn tegnes ikke — og hentes inn.** 360 lys er seks timer
ved 1m, og «dagens høy» av de siste seks timene er et feil tall.
`WM_APP_DATA` ber om eldre lys (`RequestHistory`, fase 18) til døgnet er
dekket: høyst fire hentinger, bare med panelet synlig og indikatorene på i
modusen prosessen står i.

**Bak indikatorbryteren.** Verktøylinja er full (`C_ASSERT`, fase 25), så alt
følger `ShowIndNow()` / `dispIndF`: `M`, `MA`-pillen og tray-punktet, som nå
heter «Indikatorer». Skrivebordet har `ShowIndicatorsDesktop` = 0 som
standard (fase 26) og er dermed urørt; ingen nye registernøkler. De stiplede
linjene tones med resten, og er derfor streker til `PolyPolyline` med
`DC_PEN` og ikke en `PS_DASH`-penn — GDI-tallet i hvile er uendret. Mønsteret
(6 på / 6 av) er forankret i flatens venstre kant. `CLR_VWAP` F2D14B er gulere
og lysere enn varslenes rav; `CLR_SESSION` 90939E er nøytral grå (fallgruve
87 forklarer hvorfor ikke 8A93A0). Aksemerkene er dempede (`CLR_BOX`-flate,
grå tekst, ingen ramme) og lavest i rang i kollisjonssystemet fra fase 23.
Forklaringen fikk et tredje ledd i gull, som faller ut alene når raden er
smal; krysser en sessionlinje forklaringens rad (lavt panel), får teksten
ugjennomsiktig bakgrunn. Hover-boksen er 126 px høy med indikatorene på
(87 + 3 × 13) og 87 som før uten.

**Verifisert.** `probe_sess.c` (1280×720, ekte peker for hover, venter
på inaktiv maskin): **54/54 i to kjøringer** (og 53/53 før 06:00 UTC, da
bakfyllingen ikke trengtes), rød kjøring **14 FAIL** før 06:00 og **18 FAIL**
etter. Kl. 06:01 UTC hentet bufferet seg fra 360 til 720 lys av seg selv og
stoppet; commit 1 samme minutt sto på `fullstendig = 0`. Dagens høy/lav og
VWAP stemmer med probens egen utregning av feltene 14 og 47–50 (VWAP innen
5 cent på fire lys). Begge stiplede linjer ligger på utregnet rad i eksakt
`CLR_SESSION` (548 av 1098 px, lengste strek 6 px, ingenting før døgnets
første lys), VWAP i 1081 av 1081 kolonner innen 2 px av utregnet (x, y), 0 px
utenfor grafflaten. Hover-boksen 126 px på / 87 av. 1d: ingen session, ingen
bakfylling, «VWAP  -». 1t: VWAP regnet fra hvert døgns start, 1088 av 1136
kolonner. **Kostnad, direkte målt (felt 45): 15–21 µs per bilde** (snittene
fra fase 25: 48–57 µs). GDI/USER **32/14 før og etter**. Enhetstester 24/24.
Skrivebordsproben (utvidet) 47/47: **0 px VWAP og 0 px høy/lav som
standard** på 3840×1600, 3860 / 1824 px når de skrus på. Fase 25-proben
54/54 etter at den lærte å vente på bakfyllingen. En fangst ved 560×300
viste dagens høy tvers gjennom forklaringens sifre; rettet i commit 3.
Byttet: bakgrunnen visker da også ut veker og snittpiksler under teksten.
**Exe 199 680 → 203 776 byte (+4 096)**, `/TP` byte-identisk.

**Ikke testet:** et ekte døgnskifte med panelet åpent, og bakfylling under
nettverksfeil (lest, ikke kjørt).

---

## Kjente begrensninger

- **Første gang panelet åpnes** vises «Laster data fra Binance...» i ~300 ms til
  tråden har hentet. Alle senere åpninger har data fra bufferet umiddelbart.
- **Størrelse og posisjon overlever omstart** (registret). `Ctrl`+`0` og
  tray-menyens «Standardvisning» setter tilbake til **1280×720** sentrert,
  klemt til arbeidsområdet om skjermen er mindre.
- **Opptegningen holder ikke 0,85 ms.** Etter fase 10 er medianen 1,33 ms
  ved 1280×720 og 5,13 ms ved 3840×1600, målt 17.09.2026. Etter fase 21
  er den 1,56 ms ved 1280×720 med 300 lys (1,44 uten stolpene, målt i
  samme kjøring med QPC i testbygget). Fordelingen per ledd står i fase 10. Eldre tall (0,462 ms på ~380×300 i del C, ~0,85 ms ved
  1280×720 i fase 7) er målt under andre forhold og lot seg ikke gjenskape med
  uendret kode i fase 9.
- **Prisvarslene prøves mot én pris per henting** (fase 23), altså hvert
  tredje sekund: lysets lukkekurs med panelet åpent, ticker-prisen ellers.
  En spiss som går forbi nivået og tilbake mellom to hentinger fyrer
  ingenting — lysets `high`/`low` leses ikke. Under en frakobling prøves
  ingenting; første pris etterpå fyrer det som er passert.
- **Bare varslene til symbolet som vises, er våkne** (fase 23). Appen henter
  ett symbol om gangen; et varsel på ETH sover mens BTC vises, og fyrer på
  første ETH-pris etter byttet dersom nivået er passert i mellomtiden.
- **Et varsel utenfor det synlige prisområdet tegnes ikke** (fase 23),
  heller ikke som en markør i kanten — samme regel som siste-pris-stempelet.
  Det er våkent likevel. Det fjernes ved å panorere eller zoome til det
  synes, eller med «Fjern prisvarsler (N)», som tar alle for symbolet og er
  stedet antallet vises.
- **Et varsel satt tett på prisen kan fyre med én gang** (fase 23). Siden
  velges mot siste lys' lukkekurs; med panelet lukket prøves ticker-prisen,
  og de to kan ligge noen cent fra hverandre.
- **Et duplikat har egne, flyktige varsler** (fase 23): det leser og skriver
  ikke registret, men varsler satt i det fyrer fra dets eget tray-ikon og
  dør med panelet. To *hovedinstanser* startet for hånd fyrer begge det
  samme varselet.
- **Ballongen kan holdes tilbake av «Ikke stør»**, og lyden er systemlyden
  «Stjerne» — er den slått av i Windows, er varselet stumt. Ettergløden
  vises bare når panelet er synlig i det varselet fyrer.
- **De ytterste 6 px av priskolonnen er skaleringskant** (`HTRIGHT`), ikke
  varselflate, når panelet ikke er maksimert.
- **Frakoblet-telleren vises ikke i headeren på smale paneler** (fase 22).
  Verktøylinja slutter på x = 338 (med `MA`-pillen, fase 25; 310 uten);
  teksten trenger ~75 px til før prisaksens etikett, altså et panel på
  ~510 px eller mer. Dempet pris,
  tray-tips og ikon bærer tilstanden uansett. Symbollinjas ellipse-gren
  er borte sammen med symbollinja.
- **Periodene er faste** (fase 25): SMA 20 og EMA 50, ikke valgbare, og
  begge eller ingen — én bryter.
- **`MA`-pillen finnes ikke under 426 px bredde** (fase 25). `M` og
  tray-menyen virker. Forklaringen trenger ~335 px grafbredde og er borte
  under ~440 px panelbredde; linjene tegnes uansett.
- **EMA avhenger av hvor bufferet begynner** (fase 25). Den mates fra lys 0,
  så en bakfylling (fase 18) eller en utkasting i front flytter såpunktet.
  Virkningen dør ut med (49/51)ⁿ: etter 300 lys er den under 10⁻⁵ av
  avviket i såpunktet. På de første ~150 lysene etter lys 49 i et *kort*
  buffer kan linja skille seg synlig fra TradingViews, som har lengre
  historikk.
- **Snittene er udefinert på de første 19 / 49 lysene i bufferet**, og
  linja begynner der. Standardutsnittet skjuler det (`SEED_COUNT` 360);
  panorert helt til historikkens start synes det.
- **Linjene er 1 px også på skrivebordet** når de er skrudd på der (fase 25;
  av som standard fra fase 26). Stempelet skalerer
  med H/40; linjene gjør ikke det (`DC_PEN` er alltid 1 px), og ved
  3840×1600 er de tynne. Rutenettet har samme egenskap.
- **Trådkorset tegnes over forklaringen** når pekeren står under den.
- **Et duplikat arver `ShowIndicators` fra registret**, som `ShowVolume`
  under, og skriver det aldri.
- **Et duplikat arver `ShowVolume` fra registret, ikke fra panelet det ble
  startet fra** (fase 22). `--dup` bærer symbol og intervall, ikke
  volumvalget; i praksis er de like, fordi hovedinstansen skriver valget
  i det det tas. Et duplikat skriver aldri.
- **Verktøylinja har ingen tastaturfokus-markør.** Pillene nås med `V` og
  `1`…`6`, ikke med `Tab`.
- **Flere instanser deler registret.** Duplikater skriver ingenting, men
  startes flere *hovedinstanser* for hånd (to ganger `ticker.exe`), vinner den
  som lukkes sist. Hver instans har også sitt eget tray-ikon — et duplikat
  forsvinner når panelet lukkes, en hovedinstans blir liggende til
  «Avslutt Ticker».
- **Et duplikat som skjules fra sitt eget tray-ikon blir liggende skjult**
  (tray-klikk på et aktivt panel skjuler det, som for hovedinstansen). Det
  avsluttes med krysset, `ESC` eller tray-menyen.
- **Hover-opptegningen uteblir av og til i opptil ~2 s** i en probe som flytter
  den ekte pekeren og leser med `PrintWindow`. Sett i gammelt og nytt bygg
  (fase 8). Ikke sett for hånd, og årsaken er ikke undersøkt.
- **Det finnes ingen systemmeny** (`Alt`+mellomrom), fordi vinduet ikke har
  `WS_SYSMENU`. Kontrollknappene nås fra tastaturet med `Ctrl`+`N`,
  `Ctrl`+`M`, `F11`, `Ctrl`+`W` og `Alt`+`F4` (fase 19), grafen med
  piltaster, `PgUp`/`PgDn`, `Home`/`End` og `+`/`-` (fase 20), i tillegg
  til `Ctrl`+`0`, `R`, `ESC` og `Win`+piltast.
- **`ESC`, snarveiene og navigasjonstastene krever tastaturfokus.** Har du
  klikket i et annet vindu, må panelet klikkes først. Knappene og hjulet
  virker uansett.
- **Et tastetrykk i grafen fjerner trådkorset** til neste musebevegelse
  (fase 20). Det er valgt framfor å la krysset gli med lyset under easingen.
- **`Alt`+`Tab` midt i et drag slipper ikke alltid capture.** Målt: i to
  av fem kjøringer tok oppgavebytteren capture, tre ganger beholdt panelet
  den, og draget fortsetter da til knappen slippes. `WM_CAPTURECHANGED`
  (fase 20) dekker tilfellene der capture faktisk tas — tray-menyen gjør
  det hver gang.
- **Tray-ikonets skala er implisitt.** SOL på $150 og BTC på $150 000 tegnes
  begge som `150`. Fonten har ingen `k`-glyf — fase 1 valgte bevisst `75.8`
  framfor `75k` — og verktøytipset bærer det eksakte tallet.
- **Animasjonsklokka går i korte støt når panelet står åpent.** Er panelet
  lukket går det ingen timer i det hele tatt. Med panelet åpent starter hver
  datahenting klokka på nytt, fordi det levende lyset kan flytte Y-målet:
  målt **23 tikk på 30 sekunder**, mot 1800 om den hadde gått kontinuerlig.
  Den dør altså mellom hentingene — dette er ikke en lekkasje.
- **Skrivebordsmodus står uten graf i ~0,6 s når Explorer startes på nytt**
  (målt 566 ms). Det meste er Explorer selv: ny Progman kommer etter ~0,3 s, og
  WorkerW etter ~0,3 s til.
- **Skrivebordsmodus: den klassiske WorkerW-grenen er ikke kjørt.** Maskinen
  har 24H2-treet, der WorkerW er barn av Progman.
- **Skrivebordsmodus dekker bare primærskjermen** (mandatet). Flere skjermer
  er ikke testet, fordi maskinen har én.
- **Skrivebordsmodus tegner i fysiske piksler ved skalering over 100 %.**
  Flaten dekker hele skjermen, men tekst og marger får samme pikselstørrelse
  som ved 100 %, altså mindre på skjermen. Det følger av at hele layouten er
  i rå piksler (se *Avviste forslag*, DPI-manifest).
- **Skjermbytte er bare prøvd med en sendt melding** (fase 26).
  `WM_DISPLAYCHANGE` legger skrivebordsflaten på nytt over primærskjermen,
  med en ettersjekk etter 1 s, men den ekte hendelsen kan ikke drives fra en
  probe og maskinen har én skjerm. `WM_DPICHANGED` håndteres ikke: flaten
  regner i fysiske piksler. Skulle flaten likevel stå feil, bygger en tur
  innom panelmodus og tilbake den på nytt.
- **Volum og glidende snitt er av på skrivebordet som standard** (fase 26)
  og skrus på fra tray-menyen *mens prosessen står i skrivebordsmodus*.
  Panelet har sine egne valg. Den som hadde dem på skrivebordet før fase
  26, må skru dem på igjen én gang.
- **Dagens session er UTC-døgnet** (fase 27), ikke lokal midnatt og ikke
  utsnittet: VWAP nullstilles og «dagens» høy/lav begynner 00:00 UTC (02:00
  norsk sommertid), som Binance sine dagslys. På 1d-lys finnes ingen session
  — VWAP viser en strek og linjene tegnes ikke. Dagens høy/lav tegnes bare
  når nivået ligger innenfor det synlige prisområdet og utsnittet rekker inn
  i dagen; aksemerket viker for stempelet, varsler og spøkelsesmerket.
- **Et døgn som ikke er helt i bufferet, får ingen VWAP** (fase 27). For
  *dagens* døgn hentes eldre lys av seg selv (høyst fire hentinger ved 1m,
  bare med panelet synlig og indikatorene på); for eldre døgn i venstre kant
  av bufferet står linja tom til neste døgnskifte, til brukeren drar i
  veggen. Bakfyllingen betyr at bufferet ved 1m har opptil 1800 lys kort
  etter åpning, ikke 360 — private bytes er uendret (`candles[]` er statisk).
- **Trådkorsets aksemerke skjuler ikke rutenettetiketten under seg** (sett i
  fangsten i fase 27, eldre enn fasen): står pekeren 8–15 px fra en etikett,
  stikker en stripe av tallet fram under merket. Stempelet, varslene og
  sessionmerkene har kollisjonsregelen; trådkorset har den ikke.
- **Oppvåkning fra dvale er bare prøvd med en sendt melding** (fase 24).
  `PBT_APMRESUMEAUTOMATIC` vekker tråden og slipper forbindelsen, men ekte
  dvale kan ikke drives fra en probe. Er nettet ikke oppe ved første forsøk,
  feiler det, og backoffen går 6 s, 12 s, … derfra. `frakoblet Ns` viser
  dvalens lengde til første vellykkede henting (`GetTickCount64` teller
  søvnen med). Prisvarslene tåler dvale: et passert nivå fyrer på første
  pris etterpå.
- **Skrivebordsmodus kobler input-køene sammen.** Et barn av et vindu i en
  annen prosess får Windows til å koble trådenes input (implisitt
  `AttachThreadInput`). Henger UI-tråden vår, kan skrivebordet henge med.
  Nettverket går på egen tråd, så UI-tråden gjør bare opptegning.
- **Modusbytte tar 18–28 ms (median)**, ikke < 16 ms. Se fase 12.
- **Et maksimert panel kommer tilbake gjenopprettet** etter en tur innom
  skrivebordsmodus. Geometrien som lagres, er den gjenopprettede.
- **Fokus-blink-rettelsen er ikke kjørt med maksimering, minimering eller Aero
  Snap.**
- **Over $999 999** klippes ikonteksten (4 sifre får ikke plass på 16 px).
  Trygt — opptegningen er bundet sjekket.
- **Tidsetiketter popper inn og ut i kantene under panorering** (fase 11).
  En etikett som ikke får plass innenfor `[left, right]`, tegnes ikke i det hele
  tatt, i stedet for å klippes midt i et tall.
- **Fase 11 er ikke pikselverifisert i skrivebordsmodus eller med trådkors.**
  Begge går gjennom samme `DrawChart`, og pristaggen bruker de samme `axL`/`axR`
  som stempelet. Men ingen av dem er fanget, fordi det ville flyttet den ekte
  pekeren eller lagt en flate på skrivebordet.
- **Historikken hentes bare når brukeren ber om den** (fase 18). Nyåpnet:
  5 timer på 1m. Hvert vegg-treff gir 300 lys til, opp til 6000 eller
  historikkens start. Skrivebordsmodus har ingen input og får aldri mer enn
  det den har sett. Et hull i Binance' egen historikk (vedlikehold) prependes
  som det er: lysene er indeksbaserte, så tiden komprimeres over hullet.

---

## Avviste forslag, med begrunnelse

- **Sirkulær buffer:** det finnes ingen dynamiske reallokeringer å fjerne.
  Gevinsten ville vært én `memmove` på 57 KB i minuttet (~5 µs) mot
  modulo-aritmetikk i all indeksering.
- **Begrensning til synlig utsnitt:** allerede på plass siden zoom-arbeidet.
- **DPI-manifest / `SetProcessDpiAwarenessContext`:** «DPI-skalert 1280×720»
  i mandatet ble tolket som `MulDiv` mot `GetDpiForWindow`, ikke som å gjøre
  prosessen DPI-bevisst. Hele layouten er i rå piksler, og vannmerkets
  klemmegrenser ville talt skaleringen to ganger — se DPI-kommentaren i
  `EnsureWatermark`. Skal det gjøres, er det en egen jobb som må gjennom hver
  eneste konstant.
- **Forhåndstegnet knapperad som `BitBlt`:** ville brakt knappetegningen
  innenfor 0,003 ms (ett kall, ~1–2 µs) mot to GDI-håndtak til og en cache som
  må ugyldiggjøres ved `WM_SIZE` og ved hvert hover-skifte. Målt koster
  vektorene 0,015 ms, altså 1,8 % av en opptegning. Ikke verdt kompleksiteten
  — men mekanismen finnes allerede i `EnsureWatermark` om budsjettet skal
  holdes bokstavelig.
- **C++ med STL og nlohmann/json** (arkitekturdirektivet bak fase 24). Målt
  med prosjektets egne flagg (`/W4 /O2`, x86, statisk CRT) på et minimalt
  program: C med `strstr` + `atof` **102 400 byte**; samme med `std::vector`
  og `std::string` **114 176** (+11,8 KB, krever `/EHsc`); samme med
  nlohmann/json 3.11.3 **222 720** (+120 KB — 62 % av hele `ticker.exe` — og
  headeren bygger ikke rent på `/W4`). Det nlohmann skulle kjøpe, trygg
  parsing, koster 512 byte som `strtod` med sluttpeker og `CandleSane`.
  `std::vector` har ingenting å erstatte: det finnes ingen `malloc`, og en
  fast `candles[6000]` kan verken lekke eller feile i en allokering etter tre
  uker i drift — det kan en vektor. Unntak på tvers av `WndProc` er udefinert,
  så hver meldingshåndterer måtte hatt sin egen `try`. Språkbyttet i seg selv
  er gratis (`ticker.c` er gyldig C++, se *Bygg*), så avgjørelsen kan tas på
  nytt når en funksjon trenger en container med ukjent størrelse. Indikatorer
  (SMA/EMA/RSI) er ikke avvist — de finnes bare ikke ennå, og er en løkke over
  `candles[]` inn i en statisk `double[MAX_CANDLES]`.
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
10. **Flere prosesser deler vindusklassenavn.** Mutexen er borte (fase 8),
    så et testbygg starter fint side om side med den ekte appen. Men alle
    instanser har `BTCPopupClass` og `BTCTickerWindowClass`, og de deler
    registernøkkelen. Filtrer vinduer på prosess-id
    (`GetWindowThreadProcessId`), aldri bare på klassenavn. Fase 7 og eldre
    nevner «eget mutexnavn» — det gjaldt før fase 8.
11. **Les `GetWindowRect` rett før du fanger skjermbildet.** Vinduet kan ha
    flyttet eller endret størrelse siden sist. (Auto-skjul-problemet som
    gjorde dette til en plage i fase 2 er borte med OS-rammen — `pinned`
    finnes ikke lenger.)
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
18. **`CopyFromScreen` fanger det som ligger øverst.** Uten `WS_EX_TOPMOST`
    kan et annet vindu dekke panelet, og skjermbildet blir av *det*. Bruk
    `PrintWindow` med `PW_RENDERFULLCONTENT` — den tegner vinduet uansett
    stablerekkefølge.
19. **`GetGUIThreadInfo` med feil `cbSize` lyver stille.** Den returnerte
    `TRUE` og `hwndFocus = 0`, altså nøyaktig symptomet på feil #2, mens
    fokus i virkeligheten var riktig. Sett `cbSize` fra *typen*, ikke fra en
    bokset instans — og stol mer på et ekte tastetrykk enn på proben.
20. **Eierskap og oppgavelinje henger sammen.** Et eid vindu får ikke egen
    knapp i oppgavelinja. Vil du ha knappen, kan vinduet ikke eies — og da
    må du rive det ned selv ved avslutning.

21. **En `HTCAPTION`-flate får aldri `WM_LBUTTONDOWN`.** Tegner du egne
    knapper i en header som returnerer `HTCAPTION`, er de synlige og døde —
    og et klikk på dem starter en vindusflytting. `WM_NCHITTEST` må
    returnere `HTCLIENT` over hver knappeboks. Rekkefølgen i den ene
    funksjonen *er* mekanismen.
22. **Et rammeløst `WS_POPUP` maksimerer seg til hele skjermen.** Ikke til
    arbeidsområdet, og OS-et legger rammebredden utenpå: målt
    −7,−7 3854×1614 mot `rcWork` 0,0 3840×1552 — panelet dekket
    oppgavelinja. `WM_GETMINMAXINFO` må oppgi `ptMaxPosition` og `ptMaxSize`
    selv. Å trekke rammebredden fra i `WM_NCCALCSIZE` retter *ikke* dette;
    det var første forsøk, og de 7 pikslene er ikke de 55 som mangler.
    La `ptMaxTrackSize` være — den klemmer manuell skalering også.
23. **`WM_MOUSELEAVE` fyrer når pekeren går fra `HTCLIENT` til `HTCAPTION`
    i samme vindu.** Den forlater klientområdet uten å forlate vinduet. All
    hover-tilstand må nullstilles der, ellers blir en knapp stående opplyst
    når musa går fra den og ut i headeren.
24. **`Arc` går mot klokka *sett på skjermen*.** Start 3 og slutt 12 gir en
    bue i øvre høyre kvadrant, ikke tre fjerdedeler. Vil du ha en
    sirkelpil, start på 12 og slutt på 2. Mål det, ikke resonner om
    logiske koordinater.
25. **`FindWindow` fant ikke appens egne vindusklasser** i dette oppsettet,
    mens den fant `Shell_TrayWnd`. `EnumWindows` med `GetClassName` fungerte
    hver gang. Bruk den i prober.
26. **Knappene ligger relativt til høyre kant.** En probe med hardkodede
    x-verdier gjelder bare den bredden den ble skrevet for, og treffer tom
    flate så snart vinduet maksimeres. Regn bakover fra `GetWindowRect`,
    slik `ButtonLayout` gjør.
27. **Håndtakstellingen må leses i hvile.** Under pågående opptegning står
    GDI på 34 og USER på 15 — dobbeltbufferet og vannmerket i flukt — mot
    31/14 når alt har satt seg. Måler du midt i en stresstest, ser du en
    lekkasje som ikke finnes. **Fra fase 10 er hviletallet 29/14** i vanlig
    modus og 26/6 i skrivebordsmodus. Dobbeltbufferet lever nå mellom bildene,
    og fire penner og pensler er erstattet av `DC_PEN`/`DC_BRUSH`. Fase 19
    og 20 målte 30/14; **fra fase 21 er det 32/14** — de to stolpepenslene
    lages i `WinMain`. Et modusbytte fram og tilbake gir +1 (33/14), sett
    også i bygget uten fase 21. Fase 22 la ikke til noe: 34/14 før og
    etter i både rødt og grønt bygg, målt *etter* første overlay (+2,
    fallgruve 65). Fase 23 heller ikke: 34/14 i rød og begge grønne
    kjøringer, etter ti sett/fjern, en ballong og en `MessageBeep`.

28. **`WM_SETCURSOR` må returnere `TRUE` for å holde pekeren, og `break` for
    alt annet.** Returnerer du `0` i default-grenen, mister kantsonene sine
    skaleringspekere — de kommer fra `DefWindowProc`. Og uten `return TRUE`
    setter OS-et vindusklassens peker tilbake ved neste musebevegelse, så et
    `SetCursor` fra `WM_MOUSEMOVE` blir overskrevet med én gang.
29. **En hurtigsti må lese bakgrunnen fra samme kilde som den trege.**
    `FillRect(brBg)` i knappestripa *ville* gitt riktig resultat i dag, men
    bare fordi vannmerketeksten tilfeldigvis aldri når opp i headeren. Vi
    blitter fra vannmerkebitmapen i stedet, og beviser likheten: 0 avvik av
    1980 piksler.
30. **Et vindu foran panelet ugyldiggjør all skjermbasert måling.** `GetPixel`
    på skjermen, `GetCursorInfo` og muse-/hjulmeldinger går alle til det som
    faktisk ligger øverst. Målt: `IDC_HAND` over grafen og `IDC_IBEAM` over
    headeren — begge fra et Chrome-vindu bak, bekreftet med `WindowFromPoint`.
    `BringWindowToTop` og `SetForegroundWindow` virker **ikke** fra en probe
    som ikke eier forgrunnen; `SetWindowPos` med `HWND_TOPMOST` gjør, og må
    settes tilbake etterpå. `PrintWindow` er immun og skal brukes når man kan.
31. **Ett piksel er ikke en test.** Tre påstander i regresjonsproben feilet
    falskt fordi de hang på én koordinat: ett punkt som lå på bakgrunn i
    begge tilstander, en telling som tilfeldigvis ga samme tall, og ett punkt
    som var crosshair-farget både før og etter en panorering. Sammenlikn en
    **region**, og etablér fasit med `PrintWindow` før du tror på et rødt
    resultat.
32. **`GetCursorInfo` med feil `cbSize` lyver stille** — returnerer `TRUE` og
    `hCursor = 0`. Samme felle som fallgruve 19, ny melding. `CURSORINFO` er
    24 byte i en 64-bits prosess, ikke 20. Sett `cbSize` fra **typen**.
33. **`IntersectClipRect` er eksklusiv i høyre og nedre kant — sjekk hva som
    faktisk bor på grensen.** `right + 1` slapp lys inn i aksemargens første
    kolonne; `bottom` uten `+ 1` ville fjernet nederste rutenettlinje. Samme
    funksjon, motsatt svar i de to aksene, fordi grafen er `[left, right)` i
    x og `[top, bottom]` i y.
34. **En klippefeil viser seg ikke i et stillbilde.** Halve lys i kantene
    finnes bare når `dStart` er brøk, altså midt i easingen. Post hjulhakk og
    fang innen ~90 ms, mange ganger, og skann regioner for eksakte lysfarger.
35. **En postet `WM_MOUSEMOVE` holder ikke hover hvis den ekte pekeren står
    utenfor klientflaten.** `TrackMouseEvent` ser at pekeren ikke er der og
    sender `WM_MOUSELEAVE` med én gang, som nullstiller `btnHot`. Det gjelder
    også når pekeren står i headeren, som er `HTCAPTION` (fallgruve 23).
    Parker den ekte pekeren på en knapp — `HTCLIENT` — og legg den tilbake
    etterpå.
36. **`DrawTextW` klipper mot sitt eget rektangel, ikke mot naboteksten.** To
    tekster i samme rektangel, venstre- og høyrestilt, tegnes oppi hverandre
    så snart de møtes. Mål begge og regn grensene selv.
37. **Et latensbudsjett uten målebetingelser er ikke et budsjett.** 0,462 ms
    fra del C var median under animasjon på ~380×300. Ved 1280×720 er
    hele bildet ~0,85 ms, og maksimert ~9,5 ms. Mål alltid før og etter i
    samme kjøring, vekselvis, og ikke mens en probe tar skjermbilder
    samtidig: `PrintWindow` gjorde hver runde merkbart tregere.
38. **`WM_LBUTTONDBLCLK` kommer aldri uten `CS_DBLCLKS`** på vindusklassen —
    stille. Og når den *er* satt, blir andre klikk i hvert raske dobbeltklikk
    en `DBLCLK` i stedet for `WM_LBUTTONDOWN`, overalt i klientflaten. Alt som
    reagerer på klikk, og som ikke skal nullstille visningen, må få
    dobbeltklikket også — ellers spiser knappene annethvert raske klikk.
39. **`viewCount == 0` er ikke «vis alt» etter `ClampView`.** `GetView` tolker
    0 som hele bufferet, men `ClampView` klemmer 0 opp til `MIN_VIEW`. Et
    utsnitt som skal bli standard når data kommer, må settes *før* klemmingen.
    Se fase 8.
40. **Et nyåpnet panel er ingen fasit for standardvisning.** Ta referansen
    etter en eksplisitt nullstilling, og se på bildet før du tror på et avvik
    i prosent: 5 % forskjell i grafflaten var 8 lys mot 300.
41. **Bash-verktøyets heredoc spiser backslash** i dette oppsettet, også med
    `<<'EOF'`. `'\\'` ble én backslash, og en `rep()` som skulle matche
    `L"Global\\..."` fant ingenting. Skriv skript med Write-verktøyet og kjør
    fila.
42. **`rcNormalPosition` er arbeidsområde-koordinater, ikke skjerm.** De er
    like så lenge oppgavelinja står nederst eller til høyre. `SpawnInstance`
    bruker `GetWindowRect` for et vanlig vindu og faller til
    `rcNormalPosition` bare når vinduet er maksimert.
43. **PowerShell sender `$null` som `""` til en P/Invoke-`string`.**
    `FindWindow("Progman", $null)` ga 0, mens `FindWindow("Progman",
    "Program Manager")` fant vinduet: den første leter etter et vindu med
    *tom* tittel. Bruk `[NullString]::Value`. Dette er trolig hele forklaringen
    på fallgruve 25.
44. **Et vanlig barnevindu under WorkerW blir usynlig på 24H2, og ingenting
    feiler.** `SetParent` lykkes, `GetParent` stemmer, `IsWindowVisible` er
    `TRUE`, `WM_PAINT` kommer, og tapetet ligger likevel øverst. Flaten må være
    lagdelt, med `SetLayeredWindowAttributes` kalt *etter* `SetParent`, og
    exe-en må ha `supportedOS` Windows 8+ i manifestet. En tilstandsprobe
    beviser ingenting her. Sjekk piksler på skjermen, der skrivebordet faktisk
    er synlig.
45. **`WindowFromPoint`-tester av skrivebordet krever at skrivebordet er
    synlig.** Brukerens vinduer flytter seg mellom to kjøringer. Tell hvor
    mange punkter som har Progman som rot, og behandle 0 som «ikke testet»,
    ikke som grønt eller rødt.
46. **`0x052C` endrer skrivebordet til alle.** Meldingen lager en WorkerW som
    blir liggende etter at prosessen er avsluttet. Det er ufarlig, og tapetet
    ser likt ut, men treet er ikke det samme som før første kjøring. En probe
    som ser «før»-tilstanden, må kjøre før noe har sendt meldingen.
47. **En postet `TaskbarCreated` er ikke en omstart av Explorer.** Den postede
    meldingen kommer med WorkerW intakt. Ved en ekte omstart er flaten allerede
    borte og gjenoppbygget før meldingen kommer. Testen med postet melding var
    grønn og skjulte en dobbel gjenoppbygging som bare den ekte omstarten
    avslørte (fase 9).
48. **`CreateCompatibleBitmap` er lat.** Kallet tok 0,07 ms for 3840×1600,
    mens første `BitBlt` inn i bitmapen tok 6,5–7,9 ms og `DeleteObject`
    1,7 ms. En QPC-markør rundt kallet alene ser en billig allokering. Mål
    første skriving og frigjøringen også.
49. **PowerShell-funksjoner kan kollidere med innebygde alias.** En
    hjelpefunksjon kalt `Move` ble aldri kalt: `Move` er et alias for
    `Move-Item`, og aliaset vinner. Det kom bare feilmeldinger, og proben
    fortsatte uten hover. Gi probefunksjoner navn som ikke finnes fra før
    (`PostMove`).
50. **Pikselsammenlikning mellom to bygg krever at data og input står stille.**
    Levende priser endrer bildet, og en ekte peker konkurrerer med
    `TrackMouseEvent` (fallgruve 35). Hvilken av dem som vinner, varierer fra
    kjøring til kjøring, og master avvek fra seg selv. Det som virket: lik
    patch i begge bygg, med `HttpGet` som leser en fil og `TrackMouseEvent`
    som no-op, og vinduet nesten helt utenfor skjermen. Fang først når to
    bilder på rad er like, og bekreft tilstanden mot hvile før du tror på et
    avvik. Testvinduet tar forgrunnen, så et tastetrykk fra brukeren (ESC)
    kan lukke et duplikat midt i en kjøring.
51. **Et rammeløst `WS_THICKFRAME`-vindu får den klassiske rammen tegnet oppå
    klientflaten.** Når `WM_NCCALCSIZE` returnerer 0, er vindus-DC og klient
    samme flate, og `DefWindowProc` for `WM_NCACTIVATE` og `WM_SETTEXT` tegner
    rammen rett inn i grafen. Den blir stående til neste `WM_PAINT`. Svar
    `WM_NCACTIVATE` med `DefWindowProc(…, -1)`. Den er målt fra skjermen;
    om `PrintWindow` ser den, er ikke prøvd.
52. **`DWMNCRP_DISABLED` fjerner ikke NC-tegning — den slår av DWM-rammen og
    slipper den klassiske til.** Målt: rammepiksler tilbake, og
    `SetForegroundWindow` feilet i to av tre sykluser.
53. **DPI-konteksten til et vindu settes idet det lages.** Et vindu som skal
    bytte mellom DPI-uvitende panel og per-monitor-bevisst skrivebordsflate,
    må lages på nytt. `SetParent` og stilendringer flytter det, men konteksten
    følger ikke med.
54. **En skjermprobe må forkaste samples der andre vinduer er med.** Brukeren
    kan ha Chrome eller andre vinduer i bruk mens proben går. Den første
    kjøringen etter fletting viste 4 121 «rammepiksler», som var Chrome over
    panelets høyre kant, og `SetForegroundWindow` feilet i 3 av 5 sykluser.
    Sjekk per sample at forgrunnen er ditt eget vindu og at `WindowFromPoint`
    i kantpunktene treffer panelet. Kjør en kontroll uten rettelsen i samme
    kjøring, så du vet at proben ser det den skal.
55. **Ikke skriv C-escapes gjennom en bash-heredoc.** På veien gjennom
    Bash-verktøyet og heredocen ble `\\x00e5` til `\x00e5` før Python så
    strengen, og Python skrev en ekte
    NUL-byte inn i `ticker.c`. Bygget var rent på `/W4`, og `file` kalte
    kilden «ASCII text». Bare `grep` («Binary file matches») og menyteksten
    («pe5logging») viste feilen. Skriv skriptet til fil med Write-verktøyet,
    og sjekk `grep -c $'\x00'` etter maskinelle endringer.
56. **En menyprobe åpner ekte menyer ved pekeren.** `TrackPopupMenu` viser
    menyen der brukerens peker står, og et ekte museklikk velger et punkt. Én
    kjøring med 80 menyer fikk ett autostart-klikk for mye, og det lot seg ikke
    gjenskape. Logg tilstanden etter hver blokk, og kjør proben flere ganger
    før du tror på et avvik.
57. **`EnumWindows` finner ikke skrivebordsflaten.** Den er et barn av WorkerW,
    ikke et toppnivåvindu, så en probe som bare enumererer toppnivå ser
    «ingen flate» i skrivebordsmodus — og `GetParent(NULL)` gir 0, som ser ut
    som «panel uten forelder». Fire falske feil og én falsk grønn kom av dette.
    Søk også i barna av `Progman` og `WorkerW` med `EnumChildWindows`.
58. **En probe må lese modus fra registret, ikke anta panel.** Appen starter i
    den modusen `DesktopMode` sier. Testen antok panel, mens appen startet i
    skrivebordsmodus, og alle modus-assertene ble speilvendt.
59. **Siste-pris-stempelet har samme farge som lysene.** Det er fylt med
    `CLR_UP`/`CLR_DOWN` og dekker `yLast ± 8`. En probe som leter etter
    «ytterste lyspiksel» måler derfor stempelet, ikke lysene, og fase 15 ga
    falskt rødt til hele båndet ble utelatt. Prisen inni stempelet er tegnet i
    `CLR_BG`, så raden er heller ikke heldekket.
60. **En probe mot panelet må bruke `PrintWindow`, ikke skjermdump.** Et vindu
    som ligger oppå panelet måles ellers i stedet for panelet, og fase 15 fikk
    null lyspiksler i luftrommet av den grunn. `PrintWindow(hwnd, dc, 2)` ber
    vinduet tegne seg selv. Samme lærdom som fallgruve 54, nå på et panel i
    stedet for skjermbildet.
61. **Seks sekunder er ikke nok til at lysene er på plass.** En dump tatt for
    tidlig har bakgrunn, vannmerke og ingen lys, og alle pikselsjekker blir
    røde uten at noe er galt. Fase 15 traff dette én gang; med 14 sekunder var
    de samme sjekkene grønne. Vent på data, ikke på klokka.
62. **PowerShell `[int]` runder, den gulver ikke.** BMP-radlengden
    `[int]((800 * 3 + 3) / 4) * 4` ga 2404 i stedet for 2400, og bildet ble
    skjevt og fargeforvridd. Bruk `[Math]::Floor` der C ville brukt
    heltallsdivisjon.
63. **En postet `WM_KEYDOWN` kan ikke teste `Ctrl`-kombinasjoner.**
    `GetKeyState(VK_CONTROL)` leser trådens virkelige tastetilstand, som en
    postet melding ikke rører. Bruk `SendInput` med panelet i forgrunnen — og
    bekreft forgrunn *og* `GetGUIThreadInfo`-fokus før hvert trykk, ellers
    havner `Ctrl`+`W` i det vinduet som tilfeldigvis står foran. Legg en
    **kontroll med en snarvei som finnes fra før** (`Ctrl`+`0`) først i
    proben: uten den kan en rød kjøring ikke skille «funksjonen mangler» fra
    «proben leverer ikke taster».
64. **`Alt`+`Tab` er ingen pålitelig capture-tyv.** I fem kjøringer tok
    oppgavebytteren capture fra et panel midt i et drag to ganger og lot det
    være tre ganger, mens forgrunnen byttet hver gang. En test av
    `WM_CAPTURECHANGED` som henger på `Alt`+`Tab` er derfor rød eller grønn
    etter vær. Bruk noe som tar capture *hver* gang: appens egen
    `TrackPopupMenu` (tray-menyen) i samme tråd. Og les capture-tilstanden
    fra appens tråd (`GetCapture` er per tråd), ikke fra proben. Husk at
    menyen åpner ved pekeren med museknappen nede: `ESC` før slipp, ellers
    kan slippet velge «Avslutt Ticker» (fallgruve 56).
65. **GDI-tallet hopper én gang ved første overlay og første meny.** +2
    etter første overlay (to pensler lages og slettes per bilde; GDI holder
    slettede pensler i en liten cache per prosess) og +3 etter første
    tray-meny (USER tegner den i vår prosess). Begge er uendret over tre
    sykluser til og gjennom 30 runder tastetrykk, og like i bygget uten
    endringen. En «før/etter»-sjekk som tar «før» før første overlay og
    «etter» etter første meny, ser en lekkasje som ikke finnes. Ta «før»
    etter at hver mekanisme har vært brukt én gang, og legg til en
    syklus-test (åpne/lukke ×3) som skiller engangshopp fra vekst. Les
    dessuten minimum over flere sekunder, ikke ett sample: midt i en
    opptegning ligger tallet to høyere (fallgruve 27).
66. **Proben må vente på at maskinen er inaktiv, og fokuskontrollen må ha
    en reservesti.** En kjøring gikk rød på 18 sjekker fordi et Chrome-vindu
    tok forgrunnen etter kontrollen; proben nektet korrekt å sende taster,
    men `SetForegroundWindow` fra proben virket ikke lenger. Reserven som
    virker er appens egen `ForceForeground` via et postet tray-klikk (bare
    når panelet *ikke* er forgrunn, ellers skjuler klikket det). Og
    `GetLastInputInfo` før start: 25 s uten inndata, ellers vent. Proben
    lager selv inndata med `SendInput`, så målingen gjelder bare før den
    begynner.

67. **Egne makronavn kan kollidere med `commctrl.h`.** `TB_TOP` finnes der
    (`TB_*` er verktøylinje-meldingene), og `windows.h` drar den inn selv
    med `WIN32_LEAN_AND_MEAN`. Resultatet er `C4005`, ikke en feil — bygget
    lykkes med *deres* verdi om rekkefølgen er en annen. Fase 22 bruker
    `TBAR_*`. Hold deg unna `TB_`, `LV_`, `TV_`, `SB_`, `WM_`, `CB_`, `LB_`.
68. **Python `read_text`/`write_text` normaliserer linjeskift.** Repoet har
    `core.autocrlf=true`: alle tekstfiler er LF i indeksen og **CRLF i
    arbeidskopien** (`git ls-files --eol`), `ticker.c` inkludert. Et
    redigeringsskript som leser med `read_text` og skriver med
    `newline="\n"`, gjør fila til LF på disk. Git skjuler det (diffen er
    ren, bare advarselen «LF will be replaced by CRLF» røper det), `cl`
    bryr seg ikke, og neste `checkout`/`merge` skriver CRLF tilbake — men en
    sikkerhetskopi tatt imellom har feil linjeskift. Fase 22 tok `bak17`
    slik og måtte ta den på nytt. Skriv med `newline="\r\n"`, og ta
    `.bakN` etter flettingen.
69. **En probe som tar «GDI før» må varme opp det *røde* bygget med noe det
    har.** Overlayet åpnes med postet `WM_RBUTTONUP` i grafen og lukkes med
    postet `ESC` — begge finnes i alle bygg siden fase 2 — så «før» er
    sammenliknbart mellom rød og grønn kjøring (fallgruve 65).
70. **En skrivende probe må bære verdien i meldingen, ikke legge den i et
    delt felt.** Den nærliggende løsningen er å skrive `lastPrice` under
    låsen og så sende `WM_APP_DATA`, som leser feltet på nytt.
    Arbeidertråden skriver det samme feltet hvert tredje sekund, så
    injeksjonen ville blitt borte når en ekte henting landet imellom —
    sjelden, altså en test som feiler av og til (resonnert, ikke målt).
    Fase 23 sender prisen i `lParam` (`wParam` = 1), og
    `SendMessage` returnerer først når utløseren er prøvd. De skrivende
    feltene (100–103) bor på **hovedvinduet**, så de virker med panelet
    skjult; de lesende bor fortsatt på panelet.
71. **Et postet klikk etterlater hover-tilstand.** `OnAxisClick` setter
    `axisHotY` fordi et ekte klikk har pekeren der; et postet klikk har
    ingen `WM_MOUSEMOVE` foran seg og ingen `WM_MOUSELEAVE` etter. Bildet
    etter et postet klikk i priskolonnen har derfor et spøkelse (linje og
    rammet merke) på klikkets rad. Pikselsjekker må enten regne med det
    eller poste `WM_MOUSELEAVE` først.
72. **`grep -c $'\x00'` teller alle linjer.** I bash er `$'\x00'` en tom
    streng, og den tomme strengen finnes på hver linje — sjekken fra
    fallgruve 55 svarer «4837» på en ren fil. `grep -P '\x00'` virker ikke
    i dette oppsettet («supports only unibyte and UTF-8 locales»). Bruk
    Python: `open(f, 'rb').read().count(b'\x00')`, og tell ikke-ASCII og
    rene LF i samme slengen (fallgruve 3 og 68).
73. **Pris → y → pris går ikke rundt.** Lysene kutter y med `(int)`, og
    varslene må gjøre det samme for å ligge på lysenes rader (fallgruve
    14). Et klikk på rad 300 ga nivået 80 832,00, som tegnes på rad 299.
    En probe som leter etter linja, må lete i `y ± 2` og godta ± 1.
74. **Beskrivelsen i en `Check` er ikke en formatstreng.** `%%` skrives ut
    som to prosenttegn. Rød kjøring sa «2 %% over prisen».
75. **Se på exe-størrelsen etter produksjonsbygget, og bygg det før
    flettingen.** Ett kall til `pow` og ett til `log10` la 21 KB på exe-en
    (statisk CRT, `pow` har tabeller); `exp`, `sqrt`, `floor`, `ceil` og
    `fabs` var der fra før og koster lite. `/W4` sier ingenting, testbygget
    er større av andre grunner, og fase 23 oppdaget det først etter
    `--no-ff`-flettingen. Sammenlikn `ticker.exe` med forrige fases tall
    (187 392 etter fase 22, 195 072 etter fase 23, 195 584 etter fase 24)
    før du fletter.
76. **`atof` kan ikke feile.** Tekst gir 0.0, `"1e999"` gir inf, `"nan"` gir
    NaN (UCRT leser den), og `"12x"` gir 12 — alt uten et ord. Bruk `strtod`
    med sluttpeker og krev at den står på det lukkende hermetegnet, og slipp
    verdien gjennom et *område* (`v > 0.0 && v < 1e15`): sammenlikningene er
    usanne for NaN og taket tar inf, uten `isnan`/`isfinite` og uten noe nytt
    fra CRT-en (fallgruve 75).
77. **Hovedvinduet lages før `InitializeCriticalSection`.** En ny
    meldingshåndterer i `WndProc` som går inn i låsen, kan få en *sendt*
    melding i vinduet mellom `CreateWindowExW` og låsen i `WinMain`.
    `WM_POWERBROADCAST` verner seg med `g_Ctx.hWakeEvent` — den settes etter
    låsen, så er den satt, finnes låsen.
78. **Sammenlikn håndtak i samme tilstand.** Fase 24-proben målte GDI/USER
    med panelet lukket, åpnet panelet, og målte igjen: 23/5 → 32/12, rød
    sjekk, ingen lekkasje. «Før» og «etter» må være samme vindussett.
79. **Les koden før du tar et direktiv på ordet.** Fase 24-mandatet ba om å
    erstatte `malloc` med `std::vector` i en kodebase uten `malloc`. To
    `grep` og tre små bygg i scratchpad avgjorde det; tallene står i
    *Avviste forslag*. Mål midlene, lever målet.
80. **Mål det lille direkte, ikke som differansen mellom to store.** Fase
    25-proben målte hele opptegningen (~1,9 ms) med og uten glidende snitt
    og fikk «overlegget koster −61 µs» — og −239 µs mot et bygg *uten*
    overlegg. Fem vekslende runder med minste median per tilstand hjalp
    ikke: −139, +63, +78, +80, +196 µs i fem kjøringer. QPC rundt selve
    blokka (probe-felt 39) gir 52 og 59 µs i to kjøringer.
81. **`MA_` er tatt, som `TB_`.** `winuser.h` definerer `MA_ACTIVATE` …
    `MA_NOACTIVATEANDEAT` (svarene på `WM_MOUSEACTIVATE`). Indikatorene heter
    `IND_*`. Fallgruve 67 i ny drakt: sjekk prefikset mot SDK-et før du
    velger det.
82. **Se på første skjermbilde før du skriver proben.** Tallene var riktige
    og linjene sammenhengende, men EMA 50 begynte en sjettedel inn i
    standardutsnittet — 300 av 300 lys synlige, de første 49 udefinert.
    Ingen pikselsjekk ville lett etter det. Rettet ved kilden
    (`SEED_COUNT` 360), og *så* fikk proben sjekken «definert på første
    synlige lys».
83. **Et vern i `Init` verner ikke søsknene.** `IndInit` klemte perioden til
    1; `IndFeedStart` tok den rå og regnet startindeksen forbi målet, så
    løkka aldri gikk. Funnet av en enhetstest på periode 0, ikke av noe
    brukeren kan nå — men neste fase gjør periodene valgbare.
84. **En postet tast er ikke uavhengig av brukeren.** `WM_KEYDOWN` kan
    postes til et vindu uten fokus, men appen leser `Ctrl` med
    `GetKeyState` — den EKTE tasten. Holder brukeren `Ctrl` i et annet
    vindu, blir probens `M` til `Ctrl`+`M` (fallgruve 63 sa at postede
    taster ikke kan *teste* Ctrl; dette er motsatsen: de kan heller ikke
    *unngå* den). «Ingen `SendInput`» betyr ikke «trenger ikke inaktiv
    maskin». Og en fangst skal sjekke størrelsen den fikk før den indekserer
    med størrelsen den ventet: et minimert panel er 0×0.
85. **Prøv en ny ting mot regelen, ikke mot en formulering av den.** Fase 25
    tegnet snittlinjene på skrivebordet fordi «en kurve er ikke tekst» —
    sant, men fase 14s regel er at flaten leses *perifert*, og et snitt er
    noe man leser av. Brukeren sa fra dagen etter. Og volumstolpene hadde
    stått der siden fase 21 fordi ingen spurte i det hele tatt. Ny tegning
    i `DrawChart`: avgjør for skrivebordet uttrykkelig, og se på flaten.
86. **Oppstartstilstand som avhenger av modus, må settes etter at modusen
    er kjent.** `dispVolF` ble snappet rett etter `LoadConfig`, men
    `g_desktopMode` leses 35 linjer lenger ned (`--desktop-mode`, så
    `DesktopMode`). Usynlig så lenge valget var felles for begge modi.
87. **En linjefarge må ikke ligge på blandingslinja mellom bakgrunnen og en
    tekstfarge.** Første valg for dagens høy/lav (fase 27) var 8A93A0 — som
    er *eksakt* `CLR_BG` + 0,85 × (`CLR_AXIS` − `CLR_BG`) i alle tre kanaler.
    Kantutjevnede aksetall inneholder da samme farge, og en pikselprobe som
    teller «eksakt linjefarge» teller tekst. Regn ut t per kanal før fargen
    tas i bruk; 90939E ligger ikke på linja til noen av tekstfargene.
88. **En forklaring i linjas farge ER piksler i linjas farge.** Proben i fase
    27 telte gull i hele vinduet for å vise at VWAP *ikke* tegnes på 1d — og
    fant forklaringens «VWAP  -», som er gull med vilje. Rød kjøring var
    grønn på den sjekken av feil grunn (ingen forklaring i commit 1). Let
    etter linja der linja går, ikke i hele fangsten.
89. **En sti som avhenger av klokka, må øves med vilje.** Bakfyllingen til
    døgnskiftet (fase 27) kjører bare når døgnet er eldre enn de 360 lysene
    fra første henting — ved 1m etter 06:00 UTC. De første kjøringene gikk
    05:30 og øvde den aldri; proben skriver derfor UTC-tiden og om stien ble
    tatt, og én grønn kjøring ble lagt etter 06:00.
90. **«For det synlige utsnittet» i en bestilling er et forslag til
    forankring, ikke et krav** (samme klasse som 79). VWAP fra første synlige
    lys hopper for hvert lys under panorering, og høy/lav for utsnittet står
    alltid 8 % fra kantene (`PriceRange`). Les hva koden gjør med utsnittet
    før et tall forankres i det.

---

## Sikkerhetskopier

**Bare `ticker.c.bak22` ligger igjen** (19.09.2026). Den er identisk med
`ticker.c` slik den står etter fase 27, og er rollback-referansen for bygget som
kjører. `ticker.c.bak` … `.bak21` er slettet: de dekket fase 1 til 26, og den
historikken ligger i git.

Rekkefølgen var `.bak` … `.bak7` (fase 1–8), `.bak8` (fase 13), `.bak9`
(fase 14), `.bak10` (fase 15), `.bak11` (fase 16), `.bak12` (fase 17),
`.bak13` (fase 18), `.bak14` (fase 19), `.bak15` (fase 20), `.bak16`
(fase 21), `.bak17` (fase 22), `.bak18` (fase 23), `.bak19` (fase 24), `.bak20` (fase 25), `.bak21` (fase 26) og `.bak22` (fase 27). Filene er ignorert av
git; mønsteret
er `*.bak[0-9]*`, med stjerne, fordi `*.bak[0-9]` alene slapp de tosifrede
gjennom.
