# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Projekt

ChatNicer: Win32-Tray-Tool. Markierten Text per Hotkey kopieren, an ein lokales
Ollama-Modell schicken, Antwort an der Cursorposition einfügen. Reines Win32 +
WinHTTP, x64, keine externen Bibliotheken. Bedienung und Konfiguration stehen im
[README.md](README.md).

## Build

```bat
build.bat            :: Standard-Release -> build\ChatNicer.exe (99.328 B)
build.bat compat     :: statische CRT inkl. Exceptions (194.048 B)
```

Alternativ MSBuild (liefert dieselbe EXE nach `build\Release\`):

```bat
msbuild ChatNicer.sln /p:Configuration=Release /p:Platform=x64
```

`build.bat` lädt die VS-Umgebung selbst; ein Developer Prompt ist nicht nötig.
Die Meldung „vswhere.exe … konnte nicht gefunden werden" stammt aus
`vcvars64.bat` selbst und ist harmlos.

**Läuft ChatNicer noch, scheitert der Link mit `LNK1104`.** Vor jedem Build:

```powershell
Get-Process ChatNicer -ErrorAction SilentlyContinue | Stop-Process -Force
```

## Harte Randbedingungen

**Größenbudget < 200 KB** (204.800 Bytes). Aktuell 101.376 Bytes – rund 100 KB
Reserve. Erreicht wird das über `/NODEFAULTLIB:libucrt.lib` + `ucrt.lib`
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
Auch `build.bat compat` (194.048 B, komplett statische CRT) liegt jetzt
innerhalb des Budgets.

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
(`WorkerProc`, `TestProc`, `ModelsProc`). Regeln, die durchgehend gelten:

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

## Testen

Es gibt keine Testsuite im Repo. Weil `config.h` und `network.h` header-only sind,
lässt sich ein Konsolentreiber direkt dagegen bauen und live gegen das lokale
Ollama laufen lassen:

```bat
cl /nologo /std:c++17 /EHsc /MD /utf-8 /I d:\chat-nicer selftest.cpp ^
   /link winhttp.lib user32.lib
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

Fallstrick bei PowerShell-Testskripten: `$null` wird an `string`-Parameter von
P/Invoke als `""` gebunden, `FindWindowW(cls, $null)` sucht dann ein Fenster mit
leerem Titel und findet nichts. `[NullString]::Value` verwenden.
