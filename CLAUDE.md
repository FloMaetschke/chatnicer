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
build.bat            :: Standard-Release -> build\ChatNicer.exe (118.272 B)
build.bat compat     :: statische CRT inkl. Exceptions (215.552 B)
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

**Jeder Push auf `main` veröffentlicht.** Ein dritter Job hängt `ChatNicer.exe` an
das rollende Prerelease `latest` – Tag und Assets werden dabei ersetzt, die
Release-Notes enthalten Commit, Größe und SHA256 (das README verweist darauf, die
EXE ist unsigniert). Punkte, die daran hängen:

- Der Job läuft **nur nach beiden grünen Build-Jobs** (`needs`) und nur bei
  `push` auf `main` – ein Fork-PR bekommt so nie `contents: write` in die Hand.
- **`cancel-in-progress` ist auf `main` abgeschaltet.** Wird ein Lauf zwischen
  `gh release delete --cleanup-tag` und `gh release create` abgeschossen, steht das
  Repo ohne `latest` da. PR-Läufe dürfen sich weiter gegenseitig ersetzen.
- Gelöscht **und** neu erstellt wird bewusst: ein bloßes Asset-Update ließe den Tag
  `latest` auf dem alten Commit stehen.
- Der Commit-Titel kommt aus `git log`, nicht aus `github.event.head_commit.message` –
  eine Commit-Message in den Skripttext zu interpolieren ist der klassische Weg,
  sich Fremdcode in den Runner zu holen.
- Die compat-Variante wird **nicht** veröffentlicht; sie liegt nur im Build-Artefakt.

Wer stattdessen versionierte Releases will, hängt den Job an `push: tags: ['v*']`
und ersetzt den festen Tag `latest` durch `github.ref_name`.

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

**Größenbudget < 200 KB** (204.800 Bytes). Standard-Release aktuell 118.272 Bytes
– rund 84 KB Reserve; die compat-Variante liegt derzeit darüber (siehe unten). Erreicht wird das über `/NODEFAULTLIB:libucrt.lib` + `ucrt.lib`
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

**`build.bat compat` reißt das Budget derzeit: 215.552 B bei 204.800 B erlaubt –
10.752 B zu viel.** Aufgelaufen ist das in vier Schritten: die Emoji-Regel im
Standard-Prompt hat die Variante exakt auf die Grenze gesetzt, die tolerante
Rahmen-Erkennung (`IsFrameTagName` & Co.) hat sie darüber geschoben, der
Start-Warmup (`net::Warmup` + `WarmupProc`) noch einmal um 4.608 B, die beiden
Schalter (Warmup-Meldung, Autostart) um weitere 3.072 B. Der Standard-Release ist
davon unberührt (118.272 B, rund 84 KB Reserve).

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

Eine Übersetzungseinheit: `main.cpp` bindet `config.h` und `network.h` ein, beide
header-only (alles `inline`). Es gibt keine `.rc`-Datei – Tray-Icons werden in
`MakeIcon()` per DIB-Section pixelweise gezeichnet, das Manifest für Visual Styles
kommt über ein `#pragma comment(linker, "/manifestdependency:…")`.

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
`SendMessage(dlg, WM_COMMAND, 1011, 0)` speichert (`IDC_SAVE`). So lassen sich
Checkbox-Zustände und ihre Wirkung auf `config.ini`/Registry prüfen. Die Control-IDs
stehen im `IDC_*`-Enum in `main.cpp` – sie verschieben sich, sobald jemand einen
Eintrag einfügt, also nicht hart merken. Ein Screenshot des Dialogs gelingt mit
`PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)`.

**Tray-Ballons sind nicht automatisiert prüfbar.** Weder über UIAutomation
(`Windows.UI.Core.CoreWindow` im Desktop-Baum) noch über die Fenstersuche taucht
die Sprechblase auf. Wer eine Meldung verifizieren will, prüft ihre Nebenwirkungen
(INI-Wert, Registry, `ollama ps`) statt der Anzeige.
