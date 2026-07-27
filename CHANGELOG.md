# Changelog

Alle nennenswerten Änderungen an ChatNicer. Format angelehnt an
[Keep a Changelog](https://keepachangelog.com/de/1.1.0/).

**Die Versionsnummern vergibt die CI.** Jeder Push auf `main` veröffentlicht ein
Release `v1.x`, zählt dabei die zweite Stelle hoch (`1.3` → `1.4` → … → `1.10`)
und macht aus dem Abschnitt „Unveröffentlicht" den Abschnitt der neuen Version.
Wer etwas ändert, trägt es also **vorher hier ein**; siehe [CLAUDE.md](CLAUDE.md),
Abschnitt „Changelog und Versionen". Bleibt „Unveröffentlicht" leer, füllt die CI
den Abschnitt mit den Commit-Titeln seit dem letzten Release – lesbar wird das
selten, deshalb ist es der Notnagel und nicht der Normalfall.

Jeder Versionsabschnitt beginnt mit einer Zeile `> **Kurzfassung:** …`. Sie ist
kein Schmuck: Die Landingpage unter [`docs/`](docs/) baut ihre Versionsliste
daraus und trennt die Stichpunkte an `;`. Fehlt die Zeile, nimmt die CI die ersten
drei Stichpunkte des Abschnitts und kürzt sie auf den ersten Satz.

Die Versionen **0.9 bis 1.2** sind nachträglich zugeordnet und haben kein
Release-Tag: Sie entstanden, bevor die Veröffentlichung automatisch lief. Die
Nummern stammen von der Landingpage, die sie bereits nannte.

Die Größenangaben sind gemessene Werte des Standard-Release
(`build.bat`, x64, VS 2022 / Windows SDK 10.0.26100).

## [Unveröffentlicht]

_Noch nichts eingetragen. Wer etwas ändert, schreibt es hierhin – der nächste Push auf `main` macht daraus Version 1.4._

## [1.3] – 2026-07-27

> **Kurzfassung:** Jeder Push auf `main` veröffentlicht jetzt ein durchnummeriertes Release `v1.x`; Changelog und Landingpage werden dabei automatisch nachgezogen

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
- **Durchnummerierte Releases.** Der Release-Job vergibt `v1.x` fortlaufend: Die
  nächste Nummer ist das Maximum aus den vorhandenen `v1.*`-Tags und den
  Abschnitten dieser Datei, plus eins. Beide Quellen zu befragen war Absicht – ein
  von Hand gesetzter Tag und ein von Hand geschriebener Abschnitt sollen beide
  gelten, und der höhere gewinnt.
- **`.github/scripts/Publish-Release.ps1`** erledigt die Fleißarbeit: Version
  bestimmen, „Unveröffentlicht" zum Versionsabschnitt machen, `docs/index.html`
  auf Version, Datum, Größen, SHA256 und Download-Links nachziehen, Release-Notes
  schreiben. Es liegt als eigene Datei statt inline im YAML, damit es sich mit
  `-DryRun` gegen eine Kopie testen lässt.
- Die Landingpage trägt jetzt die echten Werte: Prüfsumme, Größe beider Varianten
  und eine Versionsliste, die aus den `> **Kurzfassung:**`-Zeilen dieser Datei
  entsteht. Gesteuert wird das über Kommentar-Marker (`cn:version`, `cn:sha256`,
  `cn:releases`, …); fehlt einer, bricht der Release-Job ab. Eine Seite, die still
  veraltete Zahlen zeigt, ist schlimmer als ein roter Lauf – vor der Umstellung
  stand dort Version 1.2 mit einer Platzhalter-Prüfsumme.
- Bleibt „Unveröffentlicht" leer, veröffentlicht die CI trotzdem und füllt den
  Abschnitt mit den Commit-Titeln seit dem letzten Tag, samt Warnung in der
  Job-Zusammenfassung. Ein Tippfehler-Fix soll keinen Push blockieren.

### Geändert

- **Das rollende Prerelease `latest` entfällt.** An seine Stelle treten
  vollwertige Releases `v1.x`. Damit zeigt
  `…/releases/latest/download/ChatNicer.exe` von selbst immer auf die neueste
  Version – ein Tag namens `latest` stand diesem GitHub-eigenen Begriff nur im
  Weg. Der erste Lauf räumt das alte Release samt Tag ab.
- Die Download-Knöpfe auf der Landingpage zeigen auf die **konkrete** Version
  statt auf `latest`: Daneben steht eine Prüfsumme, und die gehört zu genau einer
  Datei.
- Der Release-Job schreibt Changelog und Landingpage als Commit nach `main`
  zurück (`[skip ci]`, Autor `github-actions[bot]`) und taggt erst danach – so
  enthält der getaggte Stand seinen eigenen Changelog-Eintrag. Der Commit ändert
  ausschließlich `CHANGELOG.md` und `docs/index.html`, die EXE bleibt also die
  gebaute.
- Dieses Changelog ist damit Eingabe für die CI und nicht mehr nur Dokumentation:
  Der Abschnitt „Unveröffentlicht" wird beim Release zum Abschnitt der neuen
  Version. Wie das gepflegt wird, steht im Kopf dieser Datei und in `CLAUDE.md`.

### Behoben

- Die Landingpage nannte Version 1.2 mit 115.712 Bytes und „113 KB", die Tabelle
  212.480 B für die compat-Variante – alles Stände von vor dem Warmup. Statt der
  Prüfsumme stand ein Platzhalter. Diese Werte pflegt jetzt die CI.

## [1.2] – 2026-07-26

> **Kurzfassung:** Start-Warmup lädt das Modell schon beim Programmstart; Autostart-Schalter in den Einstellungen; Smileys werden nicht mehr in Worte übersetzt; Verschriebene Antwort-Tags werden toleriert

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
- `advapi32.lib` ist Pflicht (in `build.bat`, `ChatNicer.vcxproj` und als
  `#pragma comment(lib, …)` in `main.cpp`).
- Schlägt der Registry-Zugriff fehl, meldet der Dialog das, speichert die übrigen
  Einstellungen aber trotzdem und setzt die Checkbox auf den echten Zustand
  zurück.

### Größe

- Standard-Release 118.272 B. Die compat-Variante wächst auf 215.552 B und liegt
  damit 10.752 B über dem Budget (Warmup 4.608 B, die beiden Schalter 3.072 B).

## [1.1] – 2026-07-23

> **Kurzfassung:** Tippmodus: Die Antwort wird live getippt, während das Modell streamt; Vier neue Regeln im System-Prompt gegen holprige Sätze

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
- Standard-Prompt um vier Regeln erweitert: vollständige Sätze ohne
  weggelassenes Objekt („Die Zeit wird es zeigen", nie „Die Zeit wird zeigen"),
  feste Wendungen nur in ihrer Standardform, keine hinzuerfundenen Abtönungen
  („eher", „sogar", „durchaus", „sehr"), und ein Korrekturdurchgang vor der
  Ausgabe. Als Regeln formuliert und nicht als weitere Tag-Beispiele – mehr
  Beispiele hatten die Tag-Ausgabe messbar verschlechtert.

### Bekannte Grenze

- Ab dem ersten getippten Zeichen lässt sich nichts mehr zurücknehmen: bricht die
  Verbindung mitten in der Antwort ab, bleibt der halbe Text stehen. Die Meldung
  sagt das auch. Die Zwischenablage bekommt im Tippmodus nie die Antwort zu sehen.

## [1.0] – 2026-07-23

> **Kurzfassung:** Standardmodell ist `qwen3:4b-instruct`: 1–4 s statt 14–27 s; Die Token-Grenzen richten sich nach der Textlänge; Ein Zweitversuch rettet leere Antworten denkender Modelle

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

## [0.9] – 2026-07-23

> **Kurzfassung:** Erste Fassung: Tray-Icon, globaler Hotkey, WinHTTP-Anbindung an die Ollama-API; Einstellungsdialog mit Verbindungstest und Modell-Liste; Handgeschriebener JSON-Umgang statt Bibliothek

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
