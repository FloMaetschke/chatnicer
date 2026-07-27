# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Projekt

ChatNicer: Win32-Tray-Tool. Markierten Text per Hotkey kopieren, an ein lokales
Ollama-Modell schicken, Antwort an der Cursorposition einfügen – wahlweise
komplett per Zwischenablage oder live getippt, während das Modell streamt. Reines
Win32 + WinHTTP, x64, keine externen Bibliotheken. Bedienung und Konfiguration
stehen im [README.md](README.md).

## Build

```bat
build.bat            :: Standard-Release -> build\ChatNicer.exe (151.040 B)
build.bat compat     :: statische CRT inkl. Exceptions (249.856 B)
```

Alternativ MSBuild (liefert dieselbe EXE nach `build\Release\`):

```bat
msbuild ChatNicer.sln /p:Configuration=Release /p:Platform=x64
```

`build.bat` lädt die VS-Umgebung selbst; ein Developer Prompt ist nicht nötig.
Die Meldung „vswhere.exe … konnte nicht gefunden werden" stammt aus
`vcvars64.bat` selbst und ist harmlos.

### CI (`.github/workflows/build.yml`)

Bei Push auf `main`, bei jedem Pull Request und von Hand (`workflow_dispatch`)
laufen zwei Build-Jobs auf `windows-latest`:

- **`build.bat` (Standard + compat)** – derselbe Weg wie lokal (`build.bat` findet
  die vorinstallierten VC++-Tools über vswhere selbst), danach Größenprüfung gegen
  das 200-KB-Budget und Upload beider EXEn als Artefakt `ChatNicer-x64`. Größen und
  SHA256 stehen in der Job-Zusammenfassung.
- **MSBuild-Gegenprobe** – baut `ChatNicer.sln`. Sie prüft nicht die EXE, sondern
  dass `ChatNicer.vcxproj` nicht verrottet: Flags und Bibliotheken stehen doppelt,
  und wer nur `build.bat` pflegt, merkt es sonst erst beim Bauen aus der IDE (so
  ist `advapi32.lib` beinahe untergegangen).

Das Budget steht als `SIZE_BUDGET` im Workflow – wird die Grenze in diesem Dokument
je verschoben, muss sie dort mit. Für die compat-Variante ist die Prüfung über
`ENFORCE_COMPAT_BUDGET: 'false'` auf eine Warnung gedämpft, weil sie derzeit
bewusst 10.752 B darüber liegt (siehe unten); sobald sie wieder unter das Budget
kommt, gehört der Schalter auf `'true'`.

**Jeder Push auf `main` veröffentlicht ein Release `v1.x`.** Ein dritter Job
vergibt die nächste Nummer, schreibt Changelog und Landingpage fort, committet
beides zurück nach `main` und hängt `ChatNicer.exe` an den neuen Tag. Die
Fleißarbeit steckt in `.github/scripts/Publish-Release.ps1`; im YAML steht nur,
was ohne GitHub-Token nicht ginge. Punkte, die daran hängen:

- Der Job läuft **nur nach beiden grünen Build-Jobs** (`needs`) und nur bei
  `push` auf `main` – ein Fork-PR bekommt so nie `contents: write` in die Hand.
  Ein `workflow_dispatch` veröffentlicht **nicht**; wer ein Release will, pusht.
- **Die nächste Nummer ist das Maximum aus `v1.*`-Tags und den Abschnitten im
  `CHANGELOG.md`, plus eins.** Beide Quellen zu befragen ist Absicht: Die
  Versionen 0.9 bis 1.2 existieren nur im Changelog (sie stammen aus der Zeit vor
  der Automatik), und ein von Hand gesetzter Tag soll trotzdem gelten. Einen
  Sprung auf 2.0 macht man, indem man `v2.0` von Hand taggt – ab dann zählt das
  Skript von dort weiter.
- **Erst committen, dann taggen.** Der Release-Commit (`[skip ci]`, Autor
  `github-actions[bot]`) enthält nur `CHANGELOG.md` und `docs/index.html`; der Tag
  zeigt auf ihn und nicht auf den Build-Commit. Nur weil dieser Commit den Code
  nicht anfasst, ist die veröffentlichte EXE trotzdem der Stand des Tags – wer
  dort je etwas anderes hineinschreibt, bricht genau diese Zusage.
- **`cancel-in-progress` ist auf `main` abgeschaltet**, und der Release-Job hat
  zusätzlich eine eigene Concurrency-Gruppe: Zwei schnell aufeinanderfolgende
  Pushes würden sonst dieselbe Nummer vergeben.
- Der Commit-Titel kommt aus `git log`, nicht aus `github.event.head_commit.message` –
  eine Commit-Message in den Skripttext zu interpolieren ist der klassische Weg,
  sich Fremdcode in den Runner zu holen.
- Die Releases sind **keine Prereleases** mehr. Damit zeigt
  `…/releases/latest/download/ChatNicer.exe` von selbst auf die neueste Version;
  der frühere Tag `latest` stand diesem GitHub-eigenen Begriff nur im Weg. Der
  Aufräumschritt am Ende des Jobs löscht ihn einmalig und kann danach entfallen.
- Die compat-Variante wird **nicht** veröffentlicht; sie liegt nur im Build-Artefakt.

Schlägt der Push fehl (geschützter Branch), bricht der Job ab, **bevor** getaggt
wird – es entsteht dann kein Release ohne passenden Changelog-Eintrag.

### Changelog und Versionen

`CHANGELOG.md` ist keine Kür, sondern Eingabe für die CI. Wer etwas ändert,
**trägt es vor dem Push unter `## [Unveröffentlicht]` ein.** Beim Release wird
genau dieser Abschnitt zu `## [1.x] – Datum`, und ein frischer, leerer Abschnitt
rückt nach.

Jeder Abschnitt beginnt mit `> **Kurzfassung:** a; b; c`. Diese Zeile ist kein
Schmuck: Die Landingpage baut ihre Versionsliste daraus und trennt an `;`. Jeder
Punkt fängt deshalb groß an und steht für sich – er landet als `<li>` auf der
Seite, nicht im Fließtext.

Bleibt „Unveröffentlicht" leer, **veröffentlicht die CI trotzdem** und füllt den
Abschnitt mit den Commit-Titeln seit dem letzten Tag (`::warning::` in der
Zusammenfassung). Das ist der Notnagel, nicht der Normalfall: Ein Tippfehler-Fix
soll keinen Push blockieren, aber „Groessenangaben nachgezogen" als einziger
Changelog-Eintrag ist niemandem eine Hilfe.

### Landingpage (`docs/`)

`docs/index.html` ist eine einzelne Datei ohne Build-Schritt (GitHub Pages, CNAME
`chatnicer.de`). Version, Größen, Prüfsumme, Download-Links und die Versionsliste
pflegt die CI – **von Hand geänderte Werte sind beim nächsten Push wieder weg.**
Gesteuert wird das über Kommentar-Marker im HTML:

| Marker | Inhalt |
|---|---|
| `cn:version` | `1.3` |
| `cn:size-bytes` / `cn:size-kb` | Größe der gebauten `ChatNicer.exe` |
| `cn:size-compat` | Größe der compat-Variante |
| `cn:sha256` | Prüfsumme des Release-Assets |
| `cn:releases` | komplette Versionsliste aus dem CHANGELOG |

Dazu ohne Marker, weil in Attributen kein Kommentar stehen kann: die KB-Angabe in
den drei `description`-Metatags und jeder `releases/download/vX.Y/ChatNicer.exe`.

**Fehlt ein Marker, bricht der Release-Job ab.** Das ist so gewollt – eine
Landingpage, die still veraltete Zahlen zeigt, ist schlimmer als ein roter Lauf.
Wer im HTML umbaut, lässt die Marker also stehen und prüft mit einem Trockenlauf:

```powershell
.\.github\scripts\Publish-Release.ps1 -ExePath build\ChatNicer.exe -DryRun
```

Das Skript ist **UTF-8 mit BOM** gespeichert. Ohne BOM liest Windows PowerShell
5.1 es als ANSI, und die Umlaute darin zerlegen den Parser – auf dem Runner
(`pwsh`, PowerShell 7) fällt das nicht auf, lokal sofort.

**Läuft ChatNicer noch, scheitert der Link mit `LNK1104`.** Vor jedem Build:

```powershell
Get-Process ChatNicer,ChatNicer-compat -ErrorAction SilentlyContinue | Stop-Process -Force
```

Die compat-Variante **muss** dabei stehen: sie heißt im Prozessbaum
`ChatNicer-compat`, `Get-Process ChatNicer` erwischt sie also nicht. Beide teilen
sich aber denselben Single-Instance-Mutex – eine übersehene compat-Instanz lässt
die frisch gebaute EXE kommentarlos wieder aussteigen, und man testet weiter den
alten Stand. Das Fehlerbild ist tückisch: der Dialog öffnet sich normal, nur die
neuen Bedienelemente fehlen. Wer unsicher ist, prüft, wem das Fenster gehört:
`GetWindowThreadProcessId(FindWindowW("ChatNicerSettingsWnd", …))` → PID →
`Get-CimInstance Win32_Process`.

## Harte Randbedingungen

**Größenbudget < 200 KB** (204.800 Bytes). Standard-Release aktuell 151.040 Bytes
– rund 52 KB Reserve; die compat-Variante liegt derzeit darüber (siehe unten). Erreicht wird das über `/NODEFAULTLIB:libucrt.lib` + `ucrt.lib`
(vcruntime statisch, UCRT dynamisch – `ucrtbase.dll` gehört ab Windows 10 zum
System, deshalb kein VC++-Redist), dazu `/O1 /Os`, `/GL`+`/LTCG`, `/GR-`, `/GS-`,
`/OPT:REF /OPT:ICF` und abgeschaltete Exceptions. Nach Änderungen die Größe
prüfen und bei Abweichung die Zahlen in `main.cpp` (Kopfkommentar) und
`README.md` nachziehen.

Das Budget wurde von 100 KB auf 200 KB angehoben, weil der Zweitversuch in
`Chat()` die alte Grenze bis auf 1 KB ausgereizt hatte. Der schlanke Aufbau
bleibt trotzdem das Ziel: eingebunden sind weiterhin nur `<string>` und
`<vector>`. `<sstream>` oder `<iostream>` kosten je einige zehn KB und passen
jetzt zwar hinein, `<regex>` allein frisst aber einen Großteil des neuen
Spielraums – im Zweifel von Hand parsen, so wie es `network.h` schon tut.

**`build.bat compat` reißt das Budget derzeit: 249.856 B bei 204.800 B erlaubt –
45.056 B zu viel.** Aufgelaufen ist das in fünf Schritten: die Emoji-Regel im
Standard-Prompt hat die Variante exakt auf die Grenze gesetzt, die tolerante
Rahmen-Erkennung (`IsFrameTagName` & Co.) hat sie darüber geschoben, der
Start-Warmup (`net::Warmup` + `WarmupProc`) noch einmal um 4.608 B, die beiden
Schalter (Warmup-Meldung, Autostart) um weitere 3.072 B – und zuletzt die
Antwortvorschläge samt umgebautem Dialog (`chatread.h`, Popup, `kReplyPrompt`,
Registerkarten, Größenänderung) um 34.304 B. Der Standard-Release ist davon
unberührt (151.040 B, rund 52 KB Reserve).

Die Antwortvorschläge sind damit der mit Abstand größte Einzelposten. Wer sie
zurückbauen will, findet den Hebel bei `kReplyPrompt` (rund 2 KB als
`wchar_t`-Literal) und beim Popup-Zeichencode – die MSAA-Schicht selbst ist
schlank, weil sie ohne UI Automation auskommt.

Wer das zurückholen will, hat drei Hebel:

1. **Standard-Prompt auslagern.** Er kostet als `wchar_t`-Literal rund 3 KB im
   Binary und würde allein reichen. Spart aber nur, wenn `kDefaultPrompt`
   *ersatzlos* entfällt – bleibt ein Fallback im Code, bleibt auch der Platzbedarf.
   Preis: die EXE ist ohne Begleitdatei nicht mehr lauffähig.
2. **Code straffen** wie schon beim Tippmodus (siehe unten).
3. **Das Budget für `compat` aufgeben.** Die Variante ist ohnehin nicht der
   Standard-Release, sondern der Notnagel für Systeme ohne aktuelle UCRT.

Bis das entschieden ist: Wer hier etwas ergänzt, misst weiter **beide** Varianten
und schreibt die Zahlen mit – der Rückstand darf nicht unbemerkt wachsen.

Der Tippmodus hat diese Variante beim
ersten Wurf auf 207.360 B getrieben; wieder unter das Budget gebracht haben es
drei Änderungen, die auch für sich sinnvoll sind und deshalb nicht
zurückgebaut werden sollten: `Chat()`/`ChatStream()` teilen sich `ChatCore()`,
`TagStream::Feed`/`Flush` hängen an einen Ausgabeparameter an statt einen String
zurückzugeben, und `TrimLeftIn`/`TrimRightIn` ersetzen vier gleiche Schleifen.
Wer hier etwas hinzufügt, misst **beide** Varianten – der Standard-Release hat
84 KB Reserve und merkt nichts davon.

**`/utf-8` ist Pflicht** (in `build.bat` und `vcxproj`): Der Standard-System-Prompt
in `config.h` enthält echte Umlaute. Ohne das Flag werden sie falsch kodiert.
Ältere UI-Strings in `main.cpp` verwenden noch ASCII-Umschreibungen („Ungueltige"),
neue Texte dürfen echte Umlaute nutzen.

**Release baut ohne C++-Exceptions** (`_HAS_EXCEPTIONS=0`). Kein `try`/`catch`,
kein `throw`; STL-Allokationsfehler brechen hart ab.

## Architektur

Eine Übersetzungseinheit: `main.cpp` bindet `config.h`, `network.h` und
`chatread.h` ein, alle header-only (alles `inline`). Es gibt keine `.rc`-Datei –
Tray-Icons werden in `MakeIcon()` per DIB-Section pixelweise gezeichnet, das
Manifest für Visual Styles kommt über ein
`#pragma comment(linker, "/manifestdependency:…")`.

### Threading

Der UI-Thread besitzt alle Fenster. Netzwerkarbeit läuft in kurzlebigen Threads
(`WorkerProc`, `TestProc`, `ModelsProc`, `WarmupProc`). Regeln, die durchgehend gelten:

- Jeder Thread bekommt eine **Heap-Kopie der Config** (`new cfg::Config(g_cfg)`)
  und löscht sie selbst. Dadurch keine Locks und keine Races mit dem Dialog.
- Ergebnisse gehen per `PostMessage` **an `g_hwnd`**, nie an `g_settings` – das
  Einstellungsfenster kann inzwischen geschlossen sein. Der Handler prüft
  `g_settings` und aktualisiert die Controls nur, wenn es noch existiert.
- `lParam` transportiert einen heap-allokierten `std::wstring*` bzw.
  `std::vector<std::wstring>*`; **der Empfänger löscht ihn**. Schlägt
  `PostMessage` fehl, löscht der Sender (siehe `ModelsProc`).
- `g_busy` (Interlocked) verhindert parallele Durchläufe.

### Hauptfenster

Bewusst **kein `HWND_MESSAGE`**-Fenster, sondern ein normales, nie sichtbares
Fenster mit `WS_EX_TOOLWINDOW`: message-only Windows empfangen keine Broadcasts
und würden `TaskbarCreated` verpassen – das Tray-Icon wäre nach einem
Explorer-Neustart dauerhaft weg.

Das Einstellungsfenster wird zur Laufzeit aufgebaut (`BuildSettingsControls`),
Layout in DIPs mit DPI-Skalierung. Tab/Enter/Esc funktionieren nur, weil die
Message-Loop `IsDialogMessage(g_settings, …)` aufruft.

### Registerkarten und Größenänderung

Drei Karten (`PAGE_CONNECT`, `PAGE_REWRITE`, `PAGE_REPLY`). Die Bedienelemente
sind **Kinder des Fensters, nicht des Tab-Controls** – ein Tab-Control kann keine
haben. Umgeschaltet wird über Sichtbarkeit (`ShowPage`); unsichtbare Controls
überspringt `IsDialogMessage` von selbst, damit stimmt die Tab-Reihenfolge ohne
weiteres Zutun.

`ICC_TAB_CLASSES` muss bei `InitCommonControlsEx` stehen, sonst entsteht das
Tab-Control schlicht nicht.

**Meldungen aus `ApplySettings()` gehen über `FocusOnPage()`**, nicht über
`SetFocus`. Sonst landet der Cursor in einem unsichtbaren Feld, und der Dialog
sieht aus, als habe die Fehlermeldung nichts bewirkt.

Das Fenster ist **größenveränderlich** (`WS_THICKFRAME`), weil in den beiden
Prompt-Feldern der längste Text des Programms steht. Jedes Element merkt sich in
`g_slots` seine Ausgangsgeometrie plus Flags; `LayoutSettings()` rechnet daraus
die neue Lage:

| Flag | Wirkung |
|---|---|
| `LF_W` / `LF_H` | Breite bzw. Höhe wächst mit |
| `LF_Y` | rutscht nach unten, wenn das Feld darüber gewachsen ist |
| `LF_X` | bleibt am rechten Rand (die Schaltflächen) |

Die Höhe bekommt **ausschließlich** das jeweilige Prompt-Feld – genau dafür zieht
man das Fenster auf. `WM_GETMINMAXINFO` verhindert, dass es unter die
Ausgangsgröße geht; ohne das überlappen sich die Elemente. Das `InvalidateRect`
in `WM_SIZE` ist nötig, weil das Tab-Control seinen Rahmen beim Wachsen nicht
selbst nachzeichnet.

### Ablauf in `WorkerProc` – die Reihenfolge ist heikel

1. `WaitForModifiersReleased()` – der Nutzer hält beim Hotkey noch STRG+SHIFT;
   ein sofortiges STRG+C käme als STRG+SHIFT+C an.
2. `ClipClear()` **vor** dem Kopieren – nur so ist „nichts markiert" von „Text
   kopiert" unterscheidbar, statt versehentlich alten Clipboard-Inhalt zu senden.
3. STRG+C, dann auf Änderung der `GetClipboardSequenceNumber()` warten.
4. Ollama fragen, Antwort in die Zwischenablage, STRG+V.
5. Backup zurückschreiben. Bei **jedem** Fehlerpfad wird restauriert – die
   `finish`-Lambda kapselt das, deshalb überall `return finish(...)` verwenden.

Ist `Config::typingInput` gesetzt, ersetzt ein eigener Zweig die Schritte 4 und 5:
`net::ChatStream()` liefert den Text häppchenweise, `TypeText()` schickt ihn
sofort als Tastatureingabe. Schritte 1–3 bleiben identisch – die Markierung wird
hier vom **ersten getippten Zeichen** ersetzt statt vom Einfügen. Der Preis ist,
dass ab dem ersten Zeichen nichts mehr zurückgenommen werden kann: bricht die
Verbindung mitten in der Antwort ab, bleibt der halbe Text stehen, und die
Meldung sagt das auch. Die Zwischenablage bekommt im Tippmodus nie die Antwort
zu sehen, nur den kopierten Originaltext.

`TypeText()` sendet Zeichen mit `KEYEVENTF_UNICODE` (layoutunabhängig, Umlaute
und Emojis inklusive; Surrogatpaare gehen als zwei aufeinanderfolgende Events
automatisch durch), Zeilenumbrüche dagegen als echtes `VK_RETURN` – ein
Unicode-0x0A ignorieren die meisten Zielprogramme. In einem Chat-Eingabefeld
schickt ENTER die Nachricht ab; das steht deshalb als Hinweis an der Checkbox.

## Ollama-Anbindung (`network.h`)

`POST /api/chat` mit `"stream": false`. Zwei Details, die je einen Testlauf
gekostet haben:

- **`"think": false` niemals senden.** Denkende Modelle (qwen3) hören dadurch
  nicht auf zu denken, aber Ollama trennt `thinking`/`content` dann nicht mehr –
  der komplette Denkprozess landet im Antworttext. Ohne den Parameter liefert
  Ollama sauber getrenntes `message.content`. `StripThinking()` entfernt zusätzlich
  alles bis zum letzten `</think>`.
  Gegen **Ollama 0.21.2 erneut geprüft und weiterhin defekt**: `qwen3:4b` mit
  `think:false` liefert `thinking`-Länge 0 und einen `content`, der komplett aus
  „Okay, let's see. The user wants me to…" besteht. Nicht wieder ausprobieren.
- **Der Prompt-Vertrag ist Code, nicht nur Text.** `WrapUserText()` verpackt den
  markierten Text in `<text_to_process>`, und der Standard-Prompt verlangt die
  Antwort in `<rewritten_text>`; `ExtractTagged()` schneidet genau das heraus.
  Ohne diese Klammer führt ein 3B-Modell den markierten Text als Anweisung aus
  („mach mir ne liste" → es liefert eine Liste). Wer `kDefaultPrompt` ändert, muss
  die Tag-Regel beibehalten. `ExtractTagged()` ist absichtlich fehlertolerant
  gegenüber `<rewritten_text` ohne `>`, fehlendem schließenden Tag und Text, der
  direkt am Tagnamen klebt – alle drei treten real auf.

- **Der Tagname selbst wird verschrieben.** Real aufgetreten:
  `</rewrittening_text>` am Ende der Antwort. Die buchstabengenaue Suche greift
  dann nicht, und das Tag landet im eingefügten Text – für den Nutzer der
  schlimmste Fehlerfall, weil sichtbarer Müll im fremden Dokument steht.
  `IsFrameTagName()` entscheidet deshalb über den Namen statt über exakte
  Gleichheit: erkannt wird, was mit `rewrit` beginnt **oder** `text` enthält, aus
  höchstens 32 Namenszeichen besteht. Ein `</div>` aus dem bearbeiteten Text
  fällt bewusst nicht darunter und bleibt stehen. `StripStrayFrame()` räumt
  zusätzlich einen Rahmen am Anfang oder Ende ab, den die Klammersuche verfehlt
  hat – nur dort, denn mitten im Text ist ein `<…>` eher Inhalt als Modellfehler.
  Der Prompt verlangt zusätzlich ausdrücklich die exakte Schreibweise; das ist
  die Bitte, der Code ist die Absicherung.

- **`BuildOptions()` berechnet `num_predict` und `num_ctx` aus der Textlänge.**
  `num_ctx` wird nur gesendet, wenn > 4096 nötig – ein fester Wert würde ein
  größer konfiguriertes `OLLAMA_CONTEXT_LENGTH` wieder verkleinern. Ohne
  `num_predict` schreiben kleine Modelle nach dem schließenden Tag weiter; das
  Ergebnis bleibt korrekt, die Anfrage dauert aber ein Vielfaches.
- **`num_predict` deckelt bei denkenden Modellen auch den Denkprozess.** Real
  gemessen an `qwen3.5:4b`: `done_reason` steht auf `length`, `thinking` ist
  vollgelaufen, `content` ist **leer** – und zwar auch bei 1024 Token. Deshalb
  fragt `Chat()` genau einmal mit `capLength=false` nach, wenn die Antwort leer
  ist und das Limit erreicht wurde. Wer den Zweitversuch entfernt, bricht das
  Tool für jedes denkende Modell. Der Fall wird über `HitTokenLimit()` +
  `ContentIsEmpty()` erkannt, bewusst ohne Tag-Extraktion.
- **Standardmodell ist `qwen3:4b-instruct`**, nicht `qwen3:4b`. Gleiche Größe,
  aber ohne Denkphase (Qwen3-4B-Instruct-2507): 1–4 s statt 14–27 s. Ein
  `qwen3:14b-instruct` gibt es in der Ollama-Library nicht, nur `4b` und `30b`.

### Start-Warmup (`net::Warmup`, `WarmupProc`)

Direkt nach `TrayAdd()` lädt `StartWarmup()` das Modell in Ollamas Speicher, damit
die erste echte Anfrage nicht auf den Modellstart wartet (kalt real gemessen:
4,2 s für `qwen3:4b-instruct`). Vier Entscheidungen dahinter:

- **`POST /api/generate` mit leerem `"prompt"`**, nicht `/api/chat`. Ollama lädt
  das Modell und antwortet sofort mit `"done":true`, `"done_reason":"load"` und
  leerem `response` – kein einziges Token wird erzeugt. Über `/api/chat` bräuchte
  es eine echte Nachricht und damit eine echte Antwortzeit.
- **Kein `keep_alive` im Payload.** Wie lange das Modell geladen bleibt, gehört
  Ollama (Standard 5 min bzw. `OLLAMA_KEEP_ALIVE`); ein eigener Wert würde eine
  bewusste Nutzereinstellung überschreiben.
- **Kein `g_busy`.** Der Warmup sperrt den Hotkey nicht – Ollama reiht parallele
  Anfragen selbst auf. Umgekehrt heißt das: der Handler für `WM_APP_WARMUP` darf
  das Tray-Icon nur anfassen, wenn `g_busy == 0`, sonst überschreibt ein spät
  eintreffendes Warmup-Ergebnis den Zustand eines laufenden Durchlaufs.
- **Nicht beim Erststart.** Fehlt die `config.ini`, öffnet sich stattdessen der
  Einstellungsdialog; das Modell steht dort erst danach fest.

Gemeldet wird über das Tray-Icon: orange + „… wird geladen …" während des Laufs,
danach blau mit Erfolgs-Ballon oder rot mit dem Klartextfehler (Icon-Reset über
denselben Timer 1 wie bei `WM_APP_DONE`).

`Config::warmupNotify` (Checkbox `IDC_WARMMSG`, INI-Schlüssel `WarmupNotify`)
schaltet **nur die Erfolgsmeldung** ab – der Fehler-Ballon hängt bewusst nicht
daran (`if (!ok || g_cfg.warmupNotify)`). Sonst bliebe ein nicht laufendes Ollama
still, bis der erste Hotkey ins Leere geht, und genau dann steht der Nutzer vor
einem Programm, das „nichts tut".

### Autostart (`cfg::AutostartEnabled` / `cfg::SetAutostart`)

Checkbox `IDC_AUTOSTART` schreibt `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`,
Wert `ChatNicer`. Bewusst **nicht** zusätzlich in der `config.ini`: Windows liest
die Registry, sie ist die Wahrheit. Ein gespiegelter INI-Wert würde auseinander­
laufen, sobald jemand den Eintrag im Task-Manager unter „Autostart" deaktiviert.
Der Dialog liest den Zustand deshalb bei jedem Öffnen frisch aus der Registry, und
`ApplySettings()` fasst sie nur an, wenn Checkbox und Ist-Zustand voneinander
abweichen.

Drei Details, die leicht verloren gehen:

- **Der Pfad steht in Anführungszeichen.** Ohne sie zerlegt Windows einen Pfad mit
  Leerzeichen (`C:\Program Files\…`) in Programm plus Argumente.
- **Beim Einschalten wird immer neu geschrieben**, auch wenn der Wert schon da war
  – so heilt sich ein Eintrag, der auf eine verschobene EXE zeigt.
- **`advapi32.lib` ist seitdem Pflicht** (in `build.bat`, `vcxproj` und als
  `#pragma comment(lib, …)` in `main.cpp`, damit auch ein Konsolentreiber linkt).

Schlägt der Registry-Zugriff fehl, meldet der Dialog das, **speichert die übrigen
Einstellungen aber trotzdem** und setzt die Checkbox auf den echten Zustand zurück.

### Streaming (`ChatStream`, nur im Tippmodus)

Dieselbe Anfrage mit `"stream": true`. Ollama antwortet dann mit NDJSON – eine
Zeile pro Token-Häppchen. `Request()` sammelt den Body in diesem Fall nicht,
sondern reicht jede vollständige Zeile an einen `LineSink` weiter; `r.body`
enthält danach nur noch die letzte Zeile (die mit `"done":true`), aus der
`HitTokenLimit()` liest. Drei Punkte, die nicht verhandelbar sind:

- **Zerlegt wird an `\n`, nicht an Puffergrenzen.** Ein WinHTTP-Häppchen darf
  mitten in einem UTF-8-Zeichen enden; Zeilenenden sind ASCII, eine vollständige
  Zeile ist deshalb immer gültiges UTF-8. `FromUtf8()` erst pro Zeile aufrufen.
- **Bei HTTP >= 400 wird nicht gestreamt**, sondern der Body ganz gelesen – sonst
  ginge die Fehlermeldung aus `{"error":"..."}` verloren.
- **`TagStream` ersetzt `ExtractTagged()` für den Stream.** Getippter Text ist
  endgültig, also hält der Filter genau so viel zurück, wie noch Teil eines Tags
  werden könnte: am Anfang bis klar ist, ob ein Rahmen-Tag (oder `<think>`)
  beginnt; am Ende jedes Häppchens ein angefangenes `</…`, das noch ein Rahmen
  werden könnte, und abschließender Leerraum. Ein Modell ohne Tags läuft ohne
  Verzögerung durch.
  **Schon ein einzelnes `<` am Pufferende muss zurückgehalten werden** – bei
  Häppchen von einem Zeichen trifft genau das ein, und wer es durchreicht, hat
  das Tag getippt, bevor der Rest überhaupt ankommt. Kommt danach kein `/`, ist
  es kein Rahmen-Ende und der Text läuft sofort weiter; die Verzögerung beträgt
  also ein Zeichen. Getestet wird das mit Deltas von 1, 3, 7 und 1000 Zeichen –
  die Fehler treten ausschließlich bei den kleinen auf.
  Absichtlicher Unterschied zu `ExtractTagged()`: das öffnende Tag wird nur am
  Textanfang erkannt, nicht hinter einer Vorrede – im Stream ließe sich das nur
  durch Puffern kaufen, und genau das soll der Modus ja vermeiden.

`ChatStream()` und `Chat()` teilen sich `ChatCore()`, damit der Zweitversuch für
denkende Modelle nur an einer Stelle steht. Im Stream ist seine Bedingung sogar
schärfer: kam noch kein Zeichen beim Aufrufer an, wurde auch nichts getippt, das
Nachfragen ist also gefahrlos.

Der JSON-Leser ist ein String-Extraktor, kein Parser. `JsonFindStringFrom()`
überspringt escapte Anführungszeichen korrekt; `ExtractChatAnswer()` sucht
gezielt ab `"message"`, damit ein vorangehendes `thinking`-Feld nicht stört.

`Temperature` wird als String direkt ins JSON geschrieben und deshalb in
`cfg::Load()` auf reine Dezimalzeichen geprüft – diese Prüfung nicht entfernen.

## Antwortvorschläge (`chatread.h`, Popup in `main.cpp`)

Zweiter Hotkey (Standard `STRG+ALT+SPACE`, `HOTKEY_REPLY`): den offenen Chat des
Vordergrundfensters lesen, das Modell nach drei Antworten fragen, sie als
Sprechblasen über dem Eingabefeld anbieten, per Klick einfügen.

**Gelesen wird über MSAA, nicht über UI Automation.** Teams und Discord sind
beide Chromium-Anwendungen und stellen ihren Renderer-Inhalt am Kindfenster
`Chrome_RenderWidgetHostHWND` („Chrome Legacy Window") bereit. Damit reicht
*eine* Codeschiene für beide; `uiautomationcore.dll` wird nicht gebraucht. Der
Weg war nicht offensichtlich: Teams hat zusätzlich einen UIA-Provider, Discord
antwortet auf `UiaRootObjectId` mit 0 und hat **nur** MSAA. Wer hier auf UIA
umbaut, verliert Discord.

**Der Anker entscheidet, welcher Chat gelesen wird** (`IsAnchorName`):

| App | Anker | Rolle |
|---|---|---|
| Teams | `Nachrichtenliste` / `Message list` | `ROLE_SYSTEM_GROUPING` (20) |
| Discord | `Nachrichten in <Chat>` / `Messages in …` | `ROLE_SYSTEM_LIST` (33) |

Beide Namen kommen aus dem `aria-label` und sind **lokalisiert** – deshalb je
zwei Schreibweisen. Discord führt den Chatnamen gleich mit, daher stammt dort der
Titel aus dem Anker; bei Teams kommt er aus dem Fenstertitel, und zwar aus dem
**zweiten** Segment (`Chat | <Partner> | <Organisation> | …`) – das erste ist nur
der Bereichsname.

Unter dem Anker sucht `FindMessageBox()` den Nachfahren mit den meisten
texttragenden direkten Kindern. Bei Discord ist der Anker schon die Liste, bei
Teams sitzt sie einige Ebenen tiefer.

**Findet sich kein Anker, ist kein Chat offen – und dann passiert bewusst
nichts.** Discord auf „Freunde" und Teams in der Chatübersicht sind damit
automatisch abgedeckt, ohne Sonderfall im Code.

**Der Unterschied zwischen „kein Chat" und „kein Baum" ist Bedienung, nicht
Kosmetik** (`chat::Status`). `ST_NO_APP` und `ST_NO_CHAT` bleiben stumm.
`ST_NO_TREE` dagegen meldet Klartext – sonst stünde der Nutzer vor einem Hotkey,
der scheinbar grundlos nichts tut.

**Discord gibt seinen Inhalt nur nach `--force-renderer-accessibility` preis.**
Gemessen: weder `WM_GETOBJECT` (an `Chrome_WidgetWin_1` *und* am Legacy-Fenster)
noch `SPI_SETSCREENREADER` aktivieren den Baum zur Laufzeit – 24 s lang keine
Reaktion, das Legacy-Fenster entsteht gar nicht erst. Teams braucht nichts
dergleichen; dort genügt das `WM_GETOBJECT` aus `FindLegacyWindow()`. Nicht
erneut mit dem systemweiten Screenreader-Flag versuchen, das war ergebnislos.

**Nur der sichtbare Ausschnitt wird gelesen**: nur das Vordergrundfenster, darin
nur der eine offene Chat, davon nur die letzten `replyContext` Nachrichten
(Standard 8, `FindAnchor` zusätzlich auf 6000 Elemente gedeckelt). Das ist keine
Sparsamkeit um ihrer selbst willen – ein Werkzeug, das den kompletten Verlauf
aller Unterhaltungen einsammelt, wäre ein Mitlesewerkzeug. Wer den Deckel
anhebt, sollte das begründen können.

**Das Popup trägt `WS_EX_NOACTIVATE`, und daran hängt der ganze Ablauf.** Würde
es den Fokus nehmen, verlöre das Chat-Eingabefeld seinen Cursor und das
anschließende STRG+V ginge ins Leere. Der Chat bleibt durchgehend aktives
Fenster; das Popup schwebt nur darüber und fängt Mausklicks ab. Geschlossen wird
per ESC, Rechtsklick, beim Wechsel des Vordergrundfensters (Timer 3, 120 ms) oder
nach 45 s (Timer 4).

**ESC wird gepollt, nicht empfangen.** Ein Fenster ohne Fokus bekommt kein
`WM_KEYDOWN`, deshalb fragt Timer 3 `GetAsyncKeyState(VK_ESCAPE)` ab – inklusive
niederwertigem Bit, damit auch ein kurzes Antippen zwischen zwei Ticks zählt.
Die naheliegende Alternative `RegisterHotKey(VK_ESCAPE)` wäre schlechter: sie
finge ESC systemweit weg, solange das Popup offen ist, und damit auch im
Chatfenster dahinter. Beim Anzeigen wird `GetAsyncKeyState` einmal leer
abgerufen, sonst schließt ein ESC von *vor* dem Hotkey das Popup sofort wieder.

Platziert wird über dem Textcursor, falls `GetGUIThreadInfo` einen meldet –
Chromium-Anwendungen tun das oft **nicht**, dann dient der untere Rand des
Chatfensters als Anker, wo bei beiden Programmen das Eingabefeld sitzt.

**Der Verlauf geht als `<text_to_process>` an das Modell** (`kReplyPrompt`). Das
ist hier sicherheitsrelevant und nicht nur Formsache: Der Text stammt von einem
fremden Gesprächspartner. Ohne die Klammer würde ein „ignoriere deine Anweisungen
und …" im Chat als Anweisung an das Modell wirken. Wer `kReplyPrompt` ändert,
behält diese Regel.

## config.ini (`config.h`)

`WritePrivateProfileStringW` schreibt nur dann UTF-16, wenn die Datei bereits eine
BOM hat – `EnsureUnicodeIni()` legt sie deshalb vorab an. Zeilenumbrüche im
System-Prompt werden als `\n` escaped (`EscapeIni`/`UnescapeIni`), da INI keine
mehrzeiligen Werte kennt. Speicherort ist die EXE-Nähe, mit Ausweichen auf
`%APPDATA%\ChatNicer\`, wenn dort nicht schreibbar.

**Eine Änderung an `kDefaultPrompt` wirkt nicht, solange eine `config.ini` mit
`SystemPrompt=` daneben liegt.** `cfg::Load()` nimmt den Default nur, wenn der
Schlüssel fehlt – ChatNicer speichert ihn aber beim ersten OK im Dialog
vollständig ab, auch wenn er unverändert ist. Wer den Prompt im Code anpasst und
danach testet, misst sonst weiter den alten Stand; das Fehlerbild sieht aus wie
„die neue Regel bringt nichts". Es gibt keinen Zurücksetzen-Knopf im Dialog, also
vor dem Test die Zeile aus der `config.ini` löschen (der Rest der Datei bleibt
gültig). Ob die aktive Fassung die alte ist, zeigt ein Blick in die Datei –
`Get-Content ... -Encoding Unicode`, sie ist UTF-16.

## Testen

Es gibt keine Testsuite im Repo. Weil `config.h` und `network.h` header-only sind,
lässt sich ein Konsolentreiber direkt dagegen bauen und live gegen das lokale
Ollama laufen lassen:

```bat
cl /nologo /std:c++17 /EHsc /MD /utf-8 /I d:\chat-nicer selftest.cpp ^
   /link winhttp.lib user32.lib advapi32.lib
```

Schnellprüfung der API von Hand:

```powershell
(Invoke-RestMethod http://localhost:11434/api/tags).models | Select-Object name
```

Beim Testen des Gesamtablaufs: STRG+C/STRG+V gehen an das **Vordergrundfenster**.
`SetForegroundWindow` von außen ist wegen des Windows-Fokusschutzes unzuverlässig –
ein Testzielfenster muss sich den Fokus beim Start selbst holen und den Ablauf
selbst anstoßen (`PostMessage(hwnd, WM_HOTKEY, 1, 0)` an `ChatNicerHiddenWnd`),
sonst landen die Tastendrücke in fremden Fenstern. Ohne Fokus lieber abbrechen.

**Vordergrund ist nicht gleich Eingabefokus.** `SetForegroundWindow` (auch mit
`AttachThreadInput` erzwungen) macht das Fenster zum Vordergrundfenster, gibt dem
Eingabefeld darin aber nicht zuverlässig den Tastaturfokus. SendInput läuft dann
ins Leere und ChatNicer meldet „Kein Text markiert" – ein Fehlerbild, das leicht
für einen echten Regress gehalten wird. Ein simulierter **Mausklick** ins Feld
(`SetCursorPos` + `mouse_event`) vor `SelectAll()` behebt es zuverlässig.

Der Tippmodus lässt sich mit einem Timer **nicht** beobachten: `WM_TIMER` hat die
niedrigste Priorität und wird erst zugestellt, wenn die Tastatur-Queue leer ist –
ein Snapshot-Timer sieht deshalb nur „vorher" und „fertig". Messbar wird der
Verlauf über die Zeitstempel der einzelnen `KeyPress`/`WM_CHAR`-Ereignisse.

Fallstrick bei PowerShell-Testskripten: `$null` wird an `string`-Parameter von
P/Invoke als `""` gebunden, `FindWindowW(cls, $null)` sucht dann ein Fenster mit
leerem Titel und findet nichts. `[NullString]::Value` verwenden.

Der **Einstellungsdialog lässt sich vollständig fernsteuern**, ohne Fokusprobleme:
`PostMessage(g_hwnd, WM_COMMAND, 40001, 0)` öffnet ihn (`IDM_SETTINGS`), `BM_CLICK`
(0x00F5) schaltet eine Checkbox, `BM_GETCHECK` (0x00F0) liest sie, und
`SendMessage(dlg, WM_COMMAND, IDC_SAVE, 0)` speichert. So lassen sich
Checkbox-Zustände und ihre Wirkung auf `config.ini`/Registry prüfen. Die Control-IDs
stehen im `IDC_*`-Enum in `main.cpp` – sie verschieben sich, sobald jemand einen
Eintrag einfügt, also nicht hart merken. `WM_SETTEXT`/`WM_GETTEXT` marshallt
Windows über Prozessgrenzen, ein Prompt lässt sich also von außen setzen und
danach in der `config.ini` gegenprüfen.

**Die Registerkarte wechselt man per Tastatur, nicht per `TCM_SETCURSEL`.**
Letzteres löst **kein** `TCN_SELCHANGE` aus – der Reiter springt um, die
Bedienelemente bleiben aber die der alten Karte, und der Test fotografiert
Unsinn. Stattdessen `WM_KEYDOWN`/`WM_KEYUP` mit `VK_RIGHT` an das Tab-Control
schicken und mit `TCM_GETCURSEL` gegenprüfen (beides ohne Zeiger, also
prozessübergreifend unbedenklich). Ein `WM_NOTIFY` von Hand zu senden scheitert:
der `NMHDR`-Zeiger läge im falschen Adressraum. Ein Screenshot des Dialogs gelingt mit
`PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)`.

**Die Antwortvorschläge lassen sich fernsteuern**, anders als der Haupt-Hotkey:
Das Zielfenster muss nur im **Vordergrund** sein (Eingabefokus braucht es zum
Lesen nicht), dann genügt `PostMessage(g_hwnd, WM_HOTKEY, 2, 0)`. Für den
Vordergrundwechsel scheitert `SetForegroundWindow` allein am Fokusschutz –
zuverlässig wird es, wenn man vorher **ALT antippt** (`keybd_event(0x12, …)`),
danach gilt der eigene Prozess als zuletzt eingabeberechtigt; `SwitchToThisWindow`
ist der Fallback. Das Popup findet man über `FindWindowW("ChatNicerReplyWnd", …)`
und fotografiert es mit `PrintWindow(…, PW_RENDERFULLCONTENT)`.

**Das Einfügen per Klick nicht automatisiert testen.** Der Klick schreibt Text in
ein echtes Chat-Eingabefeld und löst beim Gegenüber den „tippt gerade …"-Hinweis
aus – eine für Dritte sichtbare Nebenwirkung. Der Pfad (`PasteProc`) entspricht
dem des Haupt-Hotkeys; von Hand ist er in Sekunden geprüft.

**Tray-Ballons sind nicht automatisiert prüfbar.** Weder über UIAutomation
(`Windows.UI.Core.CoreWindow` im Desktop-Baum) noch über die Fenstersuche taucht
die Sprechblase auf. Wer eine Meldung verifizieren will, prüft ihre Nebenwirkungen
(INI-Wert, Registry, `ollama ps`) statt der Anzeige.
