# BTC Ticker — arbeidslogg

Status per 16.09.2026. Skrevet for agenter som jobber videre på `ticker.c`.
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
Se **Vinduet** under. Planer:
`docs/superpowers/plans/2026-09-16-ticker-rammelost-vindu.md`,
`docs/superpowers/plans/2026-09-16-ticker-glyf-hover-cursor.md` og
`docs/superpowers/plans/2026-09-16-ticker-ny-instans.md`. Design:
`docs/superpowers/specs/2026-09-16-ticker-fase2-design.md`. Planer med
«Avvik under utførelse»:
`docs/superpowers/plans/2026-09-16-ticker-fase2-del-b.md` og `...-del-c.md`.

Alt ligger i **én fil**, `ticker.c` (~3150 linjer). Ingen eksterne avhengigheter
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

Fotavtrykk: ~3,6 MB private bytes, ~164 KB exe. (Panelet er 1280×720 nå, mot
380×300 i fase 1 — dobbeltbufferet er 8× større.)

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
| Tray-klikk | fremme og aktivt → skjul; ellers vis, gjenopprett og gi fokus |
| `Ctrl` + `0` / «Standardvisning» | sentrer 1280×720 på skjermen vinduet står på |
| Dobbeltklikk på grafen eller prisaksen | nullstiller zoom og panorering (eases) |
| `R` | nullstiller zoom og panorering (ikke mens overlayet er åpent) |
| `ESC` | lagvis: lukk overlayet → nullstill utsnittet → skjul til systemstatusfeltet (duplikat: avslutt) |
| Dobbeltklikk i ledig headerflate | maksimerer / gjenoppretter |
| `Win` + `↑` / `↓` / `←` | maksimer / gjenopprett / snap — virker uten `WS_SYSMENU` |

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
| `CHART_TOP_MIN` | 32 | minste `rcChart.top`, sjekket med `#error` |
| `POPUP_MIN_W` / `H` | 400 / 250 | minste størrelse, DPI-skalert i `WM_GETMINMAXINFO` |
| `HDR_GAP` | 8 | minste luft mellom headertekster og mot knapperaden |

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

---

## Kjente begrensninger

- **Første gang panelet åpnes** vises «Laster data fra Binance...» i ~300 ms til
  tråden har hentet. Alle senere åpninger har data fra bufferet umiddelbart.
- **Størrelse og posisjon overlever omstart** (registret). `Ctrl`+`0` og
  tray-menyens «Standardvisning» setter tilbake til **1280×720** sentrert,
  klemt til arbeidsområdet om skjermen er mindre.
- **Opptegningen holder ikke 0,46 ms, og har ikke gjort det siden panelet
  ble 1280×720.** Median for hele bildet er ~0,85 ms ved 1280×720 og
  **~9,5 ms maksimert** (3840×1552), målt likt før og etter fase 7. Tallet
  0,462 ms fra del C ble målt på ~380×300, før `9785c3f`. Header-blokka er ~0,2 ms
  av det; resten er ikke undersøkt. Se Fase 7.
- **Symbollinjas ellipse er ikke sett i drift.** Selv den lengste formen,
  med «frakoblet Ns», får plass ved 400 px. Grenen er der for DPI-skalering
  og framtidige lengre etiketter.
- **Flere instanser deler registret.** Duplikater skriver ingenting, men
  startes flere *hovedinstanser* for hånd (to ganger `ticker.exe`), vinner den
  som lukkes sist. Hver instans har også sitt eget tray-ikon — et duplikat
  forsvinner når panelet lukkes, en hovedinstans blir liggende til
  «Avslutt Ticker».
- **Et duplikat som skjules fra sitt eget tray-ikon blir liggende skjult**
  (tray-klikk på et aktivt panel skjuler det, som for hovedinstansen). Det
  avsluttes med krysset, `ESC` eller tray-menyen.
- **`[ + ]` har ingen tastatursnarvei.**
- **Hover-opptegningen uteblir av og til i opptil ~2 s** i en probe som flytter
  den ekte pekeren og leser med `PrintWindow`. Sett i gammelt og nytt bygg
  (fase 8). Ikke sett for hånd, og årsaken er ikke undersøkt.
- **Kontrollknappene har ingen tastatursnarvei** ut over `Ctrl`+`0`, `ESC`
  og `Win`+piltast. Det finnes ingen systemmeny (`Alt`+mellomrom), fordi
  vinduet ikke har `WS_SYSMENU`.
- **`ESC` krever tastaturfokus.** Har du klikket i et annet vindu, må panelet
  klikkes først. Lukkeknappen virker uansett.
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
    lekkasje som ikke finnes.

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

---

## Sikkerhetskopier

`ticker.c.bak` … `ticker.c.bak7` ligger i mappa, ett per større endring.
De eldste kan trygt slettes.
