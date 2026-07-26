# Changelog

Alle nennenswerten Änderungen an ChatNicer. Format angelehnt an
[Keep a Changelog](https://keepachangelog.com/de/1.1.0/).

**Es gibt noch keine Versionsnummern.** Jeder Push auf `main` ersetzt das rollende
Prerelease [`latest`](../../releases/tag/latest); die Einträge sind deshalb nach
Datum gegliedert und nennen den Commit. Sobald versionierte Releases anfangen,
werden die Abschnitte auf `## [x.y.z] – Datum` umgestellt.

Die Größenangaben sind gemessene Werte des Standard-Release
(`build.bat`, x64, VS 2022 / Windows SDK 10.0.26100).

## [Unveröffentlicht]

Noch keine Änderungen seit `b599514`.

## 2026-07-27 – CI und automatische Veröffentlichung (`b599514`)

### Hinzugefügt

- GitHub-Actions-Workflow (`.github/workflows/build.yml`): baut bei Push auf
  `main`, bei jedem Pull Request und auf Knopfdruck (`workflow_dispatch`) auf
  `windows-latest`.
- Größenprüfung gegen das 200-KB-Budget (`SIZE_BUDGET` im Workflow), Größen und
  SHA256 landen in der Job-Zusammenfassung. Für die compat-Variante ist die
  Prüfung über `ENFORCE_COMPAT_BUDGET: 'false'` auf eine Warnung gedämpft, weil
  sie derzeit bewusst über dem Budget liegt.
- MSBuild-Gegenprobe gegen `ChatNicer.sln`: sie prüft nicht die EXE, sondern dass
  `ChatNicer.vcxproj` nicht verrottet – Flags und Bibliotheken stehen doppelt
  (so wäre `advapi32.lib` beinahe untergegangen).
- Release-Job: hängt `ChatNicer.exe` an das rollende Prerelease `latest`, mit
  Commit, Größe und SHA256 in den Release-Notes. Er läuft nur nach beiden grünen
  Build-Jobs und nur bei Push auf `main`, damit ein Fork-PR nie `contents: write`
  in die Hand bekommt. Die compat-Variante wird nicht veröffentlicht, sie liegt
  nur im Build-Artefakt `ChatNicer-x64`.

## 2026-07-26 – Start-Warmup und Autostart (`a78716d`)

### Hinzugefügt

- **Start-Warmup** (`net::Warmup`, `WarmupProc`): direkt nach dem Start lädt eine
  leere Anfrage (`POST /api/generate` mit leerem `prompt`) das Modell in Ollamas
  Speicher, damit die erste echte Anfrage nicht auf den Modellstart wartet (kalt
  gemessen: 4,2 s für `qwen3:4b-instruct`). Kein `keep_alive` im Payload – wie
  lange das Modell geladen bleibt, entscheidet weiterhin Ollama. Der Hotkey wird
  nicht gesperrt; beim Erststart ohne `config.ini` entfällt der Warmup.
- Rückmeldung über das Tray-Icon: orange samt „*Modell* wird geladen …“ während
  des Laufs, danach blau mit Erfolgs-Ballon oder rot mit dem Klartextfehler.
- Einstellung **„Benachrichtigen, wenn das Modell beim Start geladen ist"**
  (`IDC_WARMMSG`, INI-Schlüssel `WarmupNotify`). Sie schaltet nur die
  Erfolgsmeldung ab – Fehler werden immer gemeldet, sonst bliebe ein nicht
  laufendes Ollama still, bis der erste Hotkey ins Leere geht.
- Einstellung **„Mit Windows automatisch starten"** (`IDC_AUTOSTART`) über
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. Bewusst nicht zusätzlich
  in der `config.ini`: Windows liest die Registry, sie ist die Wahrheit. Der Pfad
  wird in Anführungszeichen geschrieben, beim Einschalten immer neu – so heilt
  sich ein Eintrag, der auf eine verschobene EXE zeigt.

### Geändert

- `advapi32.lib` ist Pflicht (in `build.bat`, `ChatNicer.vcxproj` und als
  `#pragma comment(lib, …)` in `main.cpp`).
- Schlägt der Registry-Zugriff fehl, meldet der Dialog das, speichert die übrigen
  Einstellungen aber trotzdem und setzt die Checkbox auf den echten Zustand
  zurück.

### Größe

- Standard-Release 118.272 B. Die compat-Variante wächst auf 215.552 B und liegt
  damit 10.752 B über dem Budget (Warmup 4.608 B, die beiden Schalter 3.072 B).

## 2026-07-26 – Tolerante Tag-Erkennung und Emoji-Regel (`ada0405`)

### Hinzugefügt

- `IsFrameTagName()` entscheidet über den Tagnamen statt über exakte Gleichheit:
  erkannt wird, was mit `rewrit` beginnt **oder** `text` enthält, bei höchstens 32
  Namenszeichen. Anlass war ein real aufgetretenes `</rewrittening_text>` am
  Antwortende – die buchstabengenaue Suche griff nicht, und das Tag landete im
  eingefügten Text. Ein `</div>` aus dem bearbeiteten Text fällt bewusst nicht
  darunter.
- `StripStrayFrame()` räumt einen Rahmen am Anfang oder Ende ab, den die
  Klammersuche verfehlt hat – nur dort, denn mitten im Text ist ein `<…>` eher
  Inhalt als Modellfehler.

### Geändert

- Standard-Prompt: Emojis und Emoticons müssen unverändert durchgereicht werden.
  Modelle ersetzten ein Smiley gern durch seine Beschreibung („ein grinsendes
  Gesicht"), weil das im Fließtext natürlicher klingt; „keep emoji" allein reichte
  nicht, das Verbot steht jetzt samt Beispiel der falschen Ausgabe da.
- Der Prompt verlangt zusätzlich ausdrücklich die exakte Schreibweise beider Tags.
  Das ist die Bitte, `IsFrameTagName()` ist die Absicherung.

## 2026-07-23 – Tippmodus (`fd45743`)

### Hinzugefügt

- Einstellung **„Antwort live tippen statt einfügen (Streaming)"**
  (`Config::typingInput`): `net::ChatStream()` liefert den Text häppchenweise,
  `TypeText()` schickt ihn sofort als Tastatureingabe – der Text baut sich auf,
  statt auf einen Schlag zu erscheinen (gemessen: 271 Zeichen über rund 1,3 s
  statt nach 1,5 s komplett).
- `TypeText()` sendet Zeichen mit `KEYEVENTF_UNICODE` (layoutunabhängig, Umlaute
  und Emojis inklusive), Zeilenumbrüche dagegen als echtes `VK_RETURN` – ein
  Unicode-0x0A ignorieren die meisten Zielprogramme. Dass ENTER in einem
  Chat-Eingabefeld die Nachricht abschickt, steht als Hinweis an der Checkbox.
- `TagStream`: inkrementeller Ersatz für `ExtractTagged()` im Stream. Zurückgehalten
  wird genau so viel, wie noch Teil eines Tags werden könnte – auch ein einzelnes
  `<` am Pufferende, weil bei Häppchen von einem Zeichen genau das eintrifft. Ein
  Modell ohne Tags läuft ohne Verzögerung durch.
- `Request()` zerlegt den NDJSON-Stream an `\n` und reicht vollständige Zeilen an
  einen `LineSink`; `FromUtf8()` läuft erst pro Zeile, weil ein WinHTTP-Häppchen
  mitten in einem UTF-8-Zeichen enden darf. Bei HTTP >= 400 wird nicht gestreamt,
  sondern der Body ganz gelesen – sonst ginge `{"error":"..."}` verloren.

### Geändert

- `Chat()` und `ChatStream()` teilen sich `ChatCore()`, damit der Zweitversuch für
  denkende Modelle nur an einer Stelle steht. Im Stream ist seine Bedingung
  schärfer: kam noch kein Zeichen beim Aufrufer an, wurde auch nichts getippt.
- `TagStream::Feed`/`Flush` hängen an einen Ausgabeparameter an, statt einen String
  zurückzugeben; `TrimLeftIn`/`TrimRightIn` ersetzen vier gleiche Schleifen. Beides
  war nötig, um die compat-Variante wieder unter das Budget zu bringen (sie stand
  beim ersten Wurf bei 207.360 B).

### Bekannte Grenze

- Ab dem ersten getippten Zeichen lässt sich nichts mehr zurücknehmen: bricht die
  Verbindung mitten in der Antwort ab, bleibt der halbe Text stehen. Die Meldung
  sagt das auch. Die Zwischenablage bekommt im Tippmodus nie die Antwort zu sehen.

## 2026-07-23 – Prompt-Regeln zu Grammatik und Wortwahl (`d840096`)

### Geändert

- Standard-Prompt um vier Regeln erweitert: vollständige Sätze ohne
  weggelassenes Objekt („Die Zeit wird es zeigen", nie „Die Zeit wird zeigen"),
  feste Wendungen nur in ihrer Standardform, keine hinzuerfundenen Abtönungen
  („eher", „sogar", „durchaus", „sehr"), und ein Korrekturdurchgang vor der
  Ausgabe. Als Regeln formuliert und nicht als weitere Tag-Beispiele – mehr
  Beispiele hatten die Tag-Ausgabe messbar verschlechtert.
- Größenangaben in `main.cpp`, `README.md` und `CLAUDE.md` nachgezogen
  (Standard-Release 102.400 B).

## 2026-07-23 – Modellwechsel und Zweitversuch (`1ca958c`)

### Geändert

- Standardmodell ist **`qwen3:4b-instruct`** statt `qwen3:4b`: gleiche Größe, aber
  ohne Denkphase – 1–4 s statt 14–27 s.
- `BuildOptions()` berechnet `num_predict` und `num_ctx` aus der Textlänge. `num_ctx`
  wird nur gesendet, wenn mehr als 4096 nötig sind – ein fester Wert würde ein
  größer konfiguriertes `OLLAMA_CONTEXT_LENGTH` wieder verkleinern. Ohne
  `num_predict` schreiben kleine Modelle nach dem schließenden Tag weiter.

### Behoben

- Leere Antwort bei denkenden Modellen: `num_predict` deckelt dort auch den
  Denkprozess, `done_reason` steht auf `length`, `thinking` ist vollgelaufen und
  `content` bleibt leer – auch bei 1024 Token. `Chat()` fragt jetzt genau einmal
  mit `capLength=false` nach (`HitTokenLimit()` + `ContentIsEmpty()`, bewusst ohne
  Tag-Extraktion).

## 2026-07-23 – Erste Fassung (`fdca91e`)

### Hinzugefügt

- Win32-Tray-Programm ohne externe Abhängigkeiten (reines Win32 + WinHTTP, x64,
  eine Übersetzungseinheit: `main.cpp` mit den header-only Einheiten `config.h`
  und `network.h`). Keine `.rc`-Datei – die Tray-Icons zeichnet `MakeIcon()` per
  DIB-Section pixelweise, das Manifest kommt über ein `#pragma comment(linker, …)`.
- Ablauf per Hotkey **STRG + SHIFT + LEERTASTE**: markierten Text kopieren, an
  Ollama schicken (`POST /api/chat`), Antwort an der Cursorposition einfügen,
  vorherige Zwischenablage wiederherstellen – auf jedem Fehlerpfad.
- Prompt-Vertrag: `WrapUserText()` verpackt den markierten Text in
  `<text_to_process>`, der Standard-Prompt verlangt die Antwort in
  `<rewritten_text>`, `ExtractTagged()` schneidet genau das heraus – fehlertolerant
  gegenüber `<rewritten_text` ohne `>`, fehlendem schließenden Tag und Text, der
  direkt am Tagnamen klebt. Ohne diese Klammer führt ein 3B-Modell den markierten
  Text als Anweisung aus.
- `StripThinking()` entfernt alles bis zum letzten `</think>`. `"think": false`
  wird bewusst nie gesendet: denkende Modelle hören dadurch nicht auf zu denken,
  Ollama trennt `thinking`/`content` dann aber nicht mehr.
- Einstellungsdialog zur Laufzeit aufgebaut (Layout in DIPs mit DPI-Skalierung),
  mit Verbindungstest und Modell-Liste aus `/api/tags`.
- `config.ini` neben der EXE, mit Ausweichen auf `%APPDATA%\ChatNicer\`.
  `EnsureUnicodeIni()` legt die BOM vorab an, weil `WritePrivateProfileStringW`
  sonst kein UTF-16 schreibt; Zeilenumbrüche im System-Prompt werden als `\n`
  escaped.
- Netzwerkarbeit in kurzlebigen Threads mit Heap-Kopie der Config, Ergebnisse per
  `PostMessage` an das Hauptfenster; `g_busy` verhindert parallele Durchläufe.
- Größenbudget < 200 KB, erreicht über `/NODEFAULTLIB:libucrt.lib` + `ucrt.lib`,
  `/O1 /Os`, `/GL`+`/LTCG`, `/GR-`, `/GS-`, `/OPT:REF /OPT:ICF` und abgeschaltete
  Exceptions (`_HAS_EXCEPTIONS=0`). `build.bat` lädt die VS-Umgebung selbst,
  `build.bat compat` baut die Variante mit statischer CRT für Systeme ohne
  aktuelle UCRT.
