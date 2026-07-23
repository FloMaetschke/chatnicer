# ChatNicer

Ein sehr leichtgewichtiges Windows-Tray-Programm (**107 KB**, reines Win32 + WinHTTP):
markierten Text per Hotkey an ein lokales **Ollama**-Modell schicken und die Antwort
direkt an der Cursorposition wieder einfügen – oder sie live tippen lassen, während
das Modell sie schreibt.

Keine externen Abhängigkeiten – kein .NET, kein curl, keine JSON-Bibliothek, kein
VC++-Redistributable. Der Text verlässt den Rechner nicht.

---

## Ablauf

1. Text irgendwo markieren (Browser, Outlook, Teams, Editor, …)
2. **STRG + SHIFT + LEERTASTE** drücken
3. ChatNicer kopiert den markierten Text, schickt ihn an Ollama und ersetzt die
   Markierung durch die umformulierte Fassung
4. Die vorherige Zwischenablage wird anschließend wiederhergestellt

Mit einem 3B/4B-Modell dauert das rund **eine Sekunde**.

Das Tray-Icon zeigt den Zustand: **blau** = bereit, **orange** = Modell arbeitet,
**rot** = Fehler (mit Benachrichtigung im Klartext).

### Tippmodus (optional)

Mit der Einstellung **„Antwort live tippen statt einfügen (Streaming)"** ändert
sich Schritt 3: Statt auf die fertige Antwort zu warten und sie einzufügen, holt
ChatNicer sie als Stream und tippt jedes Token sofort – so, wie man es aus dem
Ollama-Chat kennt. Gemessen an einem 271 Zeichen langen Absatz: der Text baut
sich über rund 1,3 Sekunden hinweg auf, statt nach 1,5 Sekunden auf einen Schlag
dazustehen.

Zwei Dinge, die man dabei wissen sollte:

* **Zeilenumbrüche werden als ENTER getippt.** In einem Chat-Eingabefeld
  (Teams, Slack, WhatsApp Web) schickt das die Nachricht ab. Für mehrzeilige
  Texte in solchen Feldern ist der Einfügemodus die sichere Wahl.
* **Kein Zurück ab dem ersten Zeichen.** Bricht die Verbindung mitten in der
  Antwort ab, bleibt der bereits getippte Teil stehen. Die Fehlermeldung sagt
  das ausdrücklich.

Die Zwischenablage bekommt im Tippmodus nie die Antwort zu sehen – nur den
markierten Originaltext, den ChatNicer ohnehin kopieren muss.

---

## Voraussetzungen

Ollama läuft lokal, und das gewünschte Modell ist installiert:

```
ollama pull qwen3:4b-instruct
```

Mehr ist nicht nötig – die Standardwerte (`http://localhost:11434`,
`qwen3:4b-instruct`) passen auf eine unveränderte Ollama-Installation.
Zur Modellwahl siehe unten; wichtig ist die Endung `-instruct`.

---

## Dateien

| Datei | Inhalt |
|---|---|
| `main.cpp` | Tray-Icon, Kontextmenü, Einstellungsfenster, Hotkey, Clipboard, Ablaufsteuerung |
| `config.h` | `config.ini`, Standard-System-Prompt, Hotkey-Parsing |
| `network.h` | WinHTTP, Ollama-API, JSON-Erzeugung und -Auswertung, Streaming-Filter |
| `ChatNicer.sln` / `.vcxproj` | Visual-Studio-2022-Projekt (x64, Debug + Release) |
| `build.bat` | Build ohne IDE – sucht die VS-Buildtools selbstständig |

Die Icons werden zur Laufzeit gezeichnet, deshalb gibt es **keine `.rc`-Datei**.

---

## Bauen

**Mit Visual Studio 2022:** `ChatNicer.sln` öffnen, `Release | x64`, F7.

**Ohne IDE:**

```bat
build.bat            :: Standard, 107 KB -> build\ChatNicer.exe
build.bat compat     :: komplett statische CRT inkl. Exceptions (200 KB)
```

### Warum die EXE so klein ist

Das Größenbudget liegt bei **200 KB**; beide Bauvarianten bleiben darunter.
Gemessen mit VS 2022 / Windows SDK 10.0.26100:

| Variante | Größe | Laufzeitabhängigkeit |
|---|---:|---|
| statische CRT + Exceptions (`build.bat compat`) | 204.288 B | keine |
| **Standard-Release** | **109.056 B** | nur `ucrtbase.dll` (Windows-Systemdatei) |
| dynamische CRT (`/MD`) | 97.280 B | VC++-Redistributable erforderlich |

Die compat-Variante nutzt das Budget inzwischen bis auf 512 Bytes aus; der
Standard-Release hat rund 93 KB Reserve.

Der entscheidende Trick ist `/NODEFAULTLIB:libucrt.lib` + `ucrt.lib`: die vcruntime
wird statisch eingebunden, die Universal CRT dagegen dynamisch – sie ist seit
Windows 10 Bestandteil des Betriebssystems. Dazu `/O1 /Os`, `/GL` + `/LTCG`,
`/GR-`, `/GS-`, `/Gy /Gw` mit `/OPT:REF /OPT:ICF` und abgeschaltete Exceptions.
Die vollständige Kommandozeile steht als Kommentar am Anfang von `main.cpp`.

---

## Konfiguration

Rechtsklick auf das Tray-Icon → **Einstellungen** (oder Doppelklick aufs Icon).
Beim ersten Start öffnet sich der Dialog automatisch.

Das Feld **Modell** ist eine Auswahlliste: ChatNicer holt die installierten Modelle
im Hintergrund über `/api/tags`. Eigene Eingaben bleiben trotzdem möglich.
**Verbindung testen** schickt eine Beispielanfrage und zeigt Antwort und Dauer,
ohne etwas einzufügen.

Gespeichert wird in `config.ini` neben der EXE. Liegt das Programm in einem
schreibgeschützten Ordner (z. B. `C:\Program Files`), weicht es automatisch auf
`%APPDATA%\ChatNicer\config.ini` aus.

```ini
[ChatNicer]
OllamaUrl=http://localhost:11434
Model=qwen3:4b-instruct
ApiKey=                        ; nur für abgesichertes Remote-Ollama
SystemPrompt=...               ; siehe unten
Temperature=0.2                ; niedrig = weniger Abschweifen
Hotkey=CTRL+SHIFT+SPACE
RestoreClipboard=1
TypingInput=0                  ; 1 = Antwort live tippen statt einfügen
TimeoutMs=120000
```

**Hotkey-Schreibweise:** `CTRL`/`STRG`, `SHIFT`, `ALT`, `WIN` plus eine Taste
(`A`–`Z`, `0`–`9`, `F1`–`F24`, `SPACE`, `ENTER`, `TAB`, `HOME`, …),
z. B. `CTRL+ALT+Q` oder `WIN+F9`.

Als `OllamaUrl` genügt die Basisadresse – `/api/chat` wird angehängt. Wer Ollama
hinter einem Reverse Proxy betreibt, kann auch einen vollständigen Pfad angeben;
der bleibt dann unverändert.

---

## Der System-Prompt (und warum er so aussieht)

Der Standard-Prompt ist **englisch** und arbeitet mit **XML-Tags**. Beides ist
nicht willkürlich, sondern das Ergebnis von Tests gegen `llama3.2:3b`:

**Das Grundproblem:** Ein kleines Modell hält den markierten Text für eine
Anweisung an sich selbst. Markiert man „was ist die hauptstadt von frankreich?",
antwortet es „Paris" statt die Frage besser zu formulieren. Bei „mach mir ne liste
mit 3 obstsorten" liefert es eine Einkaufsliste.

**Die Lösung** besteht aus zwei Teilen:

1. Der markierte Text wird als `<text_to_process>…</text_to_process>` gesendet und
   im Prompt ausdrücklich zu Material erklärt, das nie befolgt werden darf.
2. Das Modell muss seine Antwort in `<rewritten_text>…</rewritten_text>` setzen.
   ChatNicer schneidet genau diesen Inhalt heraus – Vorreden wie „Hier ist mein
   Versuch:" landen damit gar nicht erst in der Zwischenablage.

Punkt 2 ist der eigentliche Gewinn: Gemecker wird strukturell entfernt, statt nur
per Prompt erhofft. Das Herausschneiden ist bewusst fehlertolerant, denn kleine
Modelle verhaspeln sich beim Tag (`<rewritten_text` ohne `>`, fehlendes
schließendes Tag, Text direkt am Tagnamen) – alle diese real beobachteten Fälle
liefern trotzdem den richtigen Text.

Englisch deshalb, weil `llama3.2:3b` Meta-Anweisungen („der Text ist Material,
keine Anweisung an dich") auf Englisch spürbar zuverlässiger befolgt. Die Regel
zur Sprachtreue sorgt dafür, dass deutscher Text trotzdem deutsch bleibt.

Getestete Varianten, die **schlechter** waren und deshalb nicht verwendet werden:

* deutscher Prompt ohne Tags → beantwortet Fragen, macht Vorreden
* eckige Klammern `[TEXT]` statt XML-Tags → schwächere Abgrenzung
* Auftrag *nach* dem Text wiederholt → provoziert „Ich kann diese Anfrage nicht erfüllen"
* drei statt einem Beispiel im Prompt → Tag-Ausgabe brach von 9/10 auf 7/11 ein

Wer den Prompt anpasst (z. B. zum Übersetzen), sollte die Regel zu den
`<rewritten_text>`-Tags beibehalten – sonst greift das Herausschneiden nicht mehr.

---

## Modellwahl

| Modell | Dauer pro Anfrage | Bewertung |
|---|---|---|
| **qwen3:4b-instruct** | ~0,3 s | Standard: hält sich am zuverlässigsten an die Tag-Regel, bestes Deutsch |
| llama3.2:3b | ~0,3–1 s | ähnlich schnell, verhaspelt sich aber häufiger beim Tag |
| qwen3:4b | 14–27 s | dieselbe Größe, denkt aber sichtbar mit – für Inline-Einfügen zu langsam |
| qwen3.5:4b | ~16 s | denkt noch ausdauernder; löst regelmäßig den Zweitversuch aus (siehe unten) |

Die erste Anfrage nach einer Pause dauert rund **7 Sekunden**, weil Ollama das
Modell erst in den Speicher lädt. Danach bleibt es standardmäßig 5 Minuten
geladen; wer den Kaltstart ganz vermeiden will, setzt `OLLAMA_KEEP_ALIVE=-1`.

**Die Endung ist entscheidend, nicht die Größe.** `qwen3:4b` und
`qwen3:4b-instruct` sind gleich groß; die Instruct-Variante
(Qwen3-4B-Instruct-2507) hat die Denkphase gar nicht erst. Wer stattdessen
`qwen3:4b` oder ein `gemma`-Modell nimmt, wartet nicht wegen der Parameterzahl,
sondern weil das Modell vor jeder Antwort erst nachdenkt. In der qwen3-Reihe gibt
es die Instruct-Variante nur als `4b` und `30b` – ein `qwen3:14b-instruct`
existiert nicht.

Zu qwen3 und anderen denkenden Modellen: Ollama liefert den Denkprozess im
separaten Feld `thinking`, ChatNicer verwendet nur `content` – der eingefügte Text
ist also sauber. Der Parameter `"think": false` wird bewusst **nicht** gesendet:
qwen3 hört damit nicht auf zu denken, Ollama trennt die Felder dann aber nicht mehr
und der komplette Denkprozess landet im Antworttext. Als zusätzliche Absicherung
entfernt ChatNicer alles bis zum letzten `</think>`.

---

## Request-Optionen

Neben `Temperature` schickt ChatNicer zwei berechnete Grenzen mit, die sich nach
der Länge des markierten Textes richten. Beide sind absichtlich nicht
konfigurierbar – sie haben genau einen sinnvollen Wert pro Anfrage.

| Option | Wert | Zweck |
|---|---|---|
| `num_predict` | 256–4096 Token, nach Textlänge | Bremst Modelle, die nach dem schließenden Tag weiterschreiben. `ExtractTagged` würde das Nachgeplapper zwar verwerfen, aber die Anfrage dauert dann ein Vielfaches. |
| `num_ctx` | 8192–32768, **nur bei Bedarf** | Ollamas Standardfenster ist klein; längerer markierter Text würde sonst stillschweigend abgeschnitten. |
| `repeat_penalty` | `1.05` | Unter Ollamas Standard `1.1`, denn beim Umschreiben müssen Eigennamen, Zahlen und Fachbegriffe wortgleich wiederkehren dürfen. |

`num_ctx` wird nur gesendet, wenn mehr als 4096 Token gebraucht werden. Ein
pauschaler Wert würde eine größere globale Einstellung (`OLLAMA_CONTEXT_LENGTH`)
wieder verkleinern.

**Der Zweitversuch:** Bei einem denkenden Modell deckelt `num_predict` auch den
Denkprozess. `qwen3.5:4b` hat für einen einzigen Satz über 1024 Token nachgedacht
und dann gar keine Antwort mehr geschrieben – `content` war leer. ChatNicer
erkennt das (`done_reason` ist `length` und `content` leer) und fragt genau
**einmal ohne Deckel** nach. Das dauert dann zwar spürbar länger, ist aber besser
als gar nichts einzufügen. Mit einem `-instruct`-Modell tritt der Fall nicht auf.
Im Tippmodus gilt dasselbe: Solange kein einziges Zeichen getippt wurde, ist der
Zweitversuch gefahrlos – beide Wege teilen sich denselben Code.

---

## Fehlerbehandlung

Bei einem Problem wird nichts eingefügt, die ursprüngliche Zwischenablage
wiederhergestellt, das Tray-Icon kurz rot gefärbt und eine Benachrichtigung
angezeigt – zum Beispiel:

* „Kein Text markiert (oder die Zwischenablage ist blockiert)."
* „Senden fehlgeschlagen: Die Serververbindung konnte nicht hergestellt werden."
  (Ollama läuft nicht)
* `HTTP 404: model 'xyz' not found` (Modell nicht installiert)
* „Tastatureingabe blockiert. Läuft das Zielfenster mit Administratorrechten?"

Das Öffnen der Zwischenablage wird bis zu 15-mal wiederholt, falls ein anderes
Programm sie belegt. Die Anfrage läuft in einem eigenen Thread, das Programm
bleibt also bedienbar.

Im **Tippmodus** gilt „nichts eingefügt" nur bis zum ersten Zeichen. Danach
lässt sich ein Abbruch nicht mehr rückgängig machen; die Meldung lautet dann
„Abgebrochen, getippter Text bleibt stehen: …".

---

## Bekannte Grenzen

* **Ein kleines Modell bleibt ein kleines Modell.** Die XML-Tags fangen die meisten
  Fälle ab, aber bei Texten, die stark nach einer Anweisung an eine KI klingen, kann
  das Modell ausbrechen und eine markierte Wissensfrage beantworten statt sie
  umzuformulieren. Mit `llama3.2:3b` trat das gelegentlich auf; mit
  `qwen3:4b-instruct` in den Testläufen nicht mehr – ausgeschlossen ist es nicht.
* **Nur Text:** Gesichert und wiederhergestellt wird ausschließlich
  `CF_UNICODETEXT`. Ein Bild in der Zwischenablage ist danach weg.
* **Tippmodus, Vorreden:** Beim Einfügen schneidet ChatNicer den Inhalt der
  `<rewritten_text>`-Tags auch dann heraus, wenn das Modell noch „Hier ist mein
  Versuch:" davorsetzt. Im Stream ist das nicht möglich, ohne den Anfang zu
  puffern – also genau das aufzugeben, worum es in diesem Modus geht. Das
  öffnende Tag wird deshalb nur am Textanfang erkannt; eine Vorrede davor würde
  mitgetippt. Mit `qwen3:4b-instruct` ist der Fall in den Testläufen nicht
  aufgetreten.
* **Fenster mit Administratorrechten:** Dort nimmt Windows (UIPI) keine simulierten
  Tastendrücke an. Abhilfe: ChatNicer ebenfalls als Administrator starten.
* **JSON-Leser:** bewusst ein Extraktor für String-Werte, kein vollständiger Parser.
* **x64:** Für x86 genügt eine zusätzliche Plattform-Konfiguration im vcxproj.
* **Release ohne C++-Exceptions** (`_HAS_EXCEPTIONS=0`): Bei Speichermangel bricht
  die STL hart ab. Wer das nicht möchte, baut mit `build.bat compat`.

**Autostart:** `Win + R` → `shell:startup` → Verknüpfung auf `ChatNicer.exe` hineinlegen.

---

## Verifizierter Stand

Auf Windows 11 (Build 26200), VS 2022, Ollama 0.21.2 gebaut und geprüft:

* Release-Build fehlerfrei bei `/W4`, 107 KB (109.056 Bytes) – über `build.bat` und MSBuild
* 43 automatisierte Tests, davon der Großteil live gegen das lokale Ollama:
  JSON-Erzeugung, XML-Extraktion inklusive aller real beobachteten kaputten
  Tag-Schreibweisen, Denkprozess-Filter, `config.ini`-Roundtrip mit Umlauten,
  mehrzeiligem Prompt und XML im Text, Absicherung des Temperature-Werts,
  Fehlerfälle (unbekanntes Modell, Ollama aus)
* Kompletter Durchlauf gegen ein Testfenster: „das ist ein test mit fehlan" wurde
  markiert, per Hotkey verarbeitet und **nach 1 Sekunde** durch „Das ist ein Test
  mit Fehlern." ersetzt; die vorherige Zwischenablage war danach inklusive Umlauten
  wiederhergestellt
* Verhalten am lebenden Modell geprüft: „mach mir ne liste mit 5 gemüsesorten" wird
  zu „Mach mir bitte eine Liste mit fünf Gemüsesorten." umformuliert statt
  beantwortet; „ey das geht so nicht du hast das verkackt" wird höflich gefasst
* `qwen3:4b-instruct` gegen fünf Fälle live geprüft (je ~0,3 s nach dem Laden):
  Bitte, Aufforderung, Wissensfrage und Beschwerde wurden alle umformuliert statt
  beantwortet, englischer Text blieb englisch
* `num_predict`/`num_ctx` im erzeugten Payload geprüft: kurzer Text → 256 Token und
  kein `num_ctx`; 24.000 Zeichen → 4096 Token und `num_ctx` auf 16384 angehoben
* Zweitversuch live geprüft: `qwen3.5:4b` lieferte mit Deckel eine leere Antwort
  (`done_reason=length`, `thinking` vollgelaufen) und nach dem Zweitversuch
  „Könntest du mir bitte bis Freitag die Zahlen schicken?" – `qwen3:4b-instruct`
  löst den Zweitversuch nicht aus
* `"think": false` gegen Ollama 0.21.2 nachgemessen: weiterhin defekt, der
  Denkprozess landet vollständig im `content`

Zum Tippmodus zusätzlich geprüft:

* 32 Tests für den Streaming-Teil, davon 12 gegen den inkrementellen Tag-Filter
  mit **jeder** Häppchengröße von 1 bis 40 Zeichen – auch bei Aufteilung mitten
  im Tag kommt derselbe Text heraus wie beim Einfügen (kein schließendes Tag,
  `<rewritten_text` ohne `>`, nur schließendes Tag, gar kein Tag, Denkprozess im
  `content`, mehrzeilig, Umlaute, einzelne `<` im Text, Nachgeplapper nach dem Tag)
* live gegengeprüft: derselbe Rohstream einmal durch den Streaming-Filter und
  einmal komplett durch `ExtractTagged` – Ergebnis zeichengleich
* Kompletter Durchlauf gegen ein Testfenster: 271 Zeichen markiert, die Antwort
  traf als 284 einzelne Tastatureingaben über **1,3 Sekunden** verteilt ein
  (gemessen an den Zeitstempeln der `WM_CHAR`-Ereignisse), gleichmäßig über den
  ganzen Zeitraum – Umlaute korrekt
* Einfügemodus im selben Aufbau erneut geprüft: unverändert 5 Tastendrücke
  (STRG+C, STRG+V), Antwort nach 750 ms ersetzt
