# Design-Prompt: Produkt-Homepage für ChatNicer

Diese Datei ist als Prompt gedacht: Inhalt kopieren und Claude (Design/Artifact)
übergeben. Alles unterhalb von „**Prompt beginnt hier**" ist der Prompt selbst.

> **Stand: historisch.** Die Seite steht seit dem ersten Wurf in
> [`docs/index.html`](docs/index.html), und Version, Größen, Prüfsumme,
> Download-Links und Versionsliste pflegt seit v1.3 die CI
> ([`Publish-Release.ps1`](.github/scripts/Publish-Release.ps1)). Die Zahlen in
> diesem Prompt sind deshalb der Stand von damals und **nicht mehr die Wahrheit** –
> maßgeblich sind die gebaute EXE und `CHANGELOG.md`.
>
> Wer die Seite hiermit neu bauen lässt, muss die Kommentar-Marker
> (`cn:version`, `cn:size-bytes`, `cn:size-kb`, `cn:size-compat`, `cn:sha256`,
> `cn:releases`) wieder einsetzen: Fehlt einer, bricht der Release-Job ab. Siehe
> `CLAUDE.md`, Abschnitt „Landingpage (`docs/`)".

---

# Prompt beginnt hier

Entwirf und baue die vollständige Produkt-Homepage für **ChatNicer**. Ziel ist eine
statische Seite, die per **GitHub Pages** aus dem Repository
`FloMaetschke/chatnicer` ausgeliefert und unter der eigenen Domain
`chatnicer.de` erreichbar ist.

Sprache der Seite: **Deutsch** (mit korrekten Umlauten). Technische Begriffe,
Dateinamen, Codebeispiele und Bezeichner bleiben unverändert englisch bzw. in
Originalschreibweise.

## Harte Randbedingungen

Diese drei Punkte stehen über allen Designwünschen. Im Zweifel wird zu ihren
Gunsten entschieden, auch wenn dafür Inhalt oder Effekt wegfällt.

**1. Vollständig statisch.** Eine `index.html`, die ein Webserver unverändert
ausliefert. Kein Buildschritt, kein npm, kein Bundler, kein Jekyll, kein
Framework, kein Templating. Kein Request an einen fremden Host – nicht für CSS,
nicht für Schriften, nicht für Icons, nicht für Analytics. Keine Cookies, kein
`localStorage` außer für die Theme-Wahl.

**2. Lädt sofort.** Zielwerte, die eingehalten werden müssen:

- **`index.html` unter 50 KB** unkomprimiert, inklusive CSS, JS und aller SVGs.
- **Ein einziger Request** für die gesamte Seite. Alles inline – CSS im `<style>`,
  Grafiken als Inline-SVG. Keine separaten `.css`/`.js`/Bilddateien.
- **Kein Webfont.** Systemschrift-Stack, damit der erste Frame ohne Nachladen und
  ohne Textsprung steht.
- **JavaScript unter 2 KB**, und nur für Dinge, die ohne es nicht gehen:
  Theme-Umschalter und Copy-Buttons. Alles andere in CSS lösen – Accordions als
  `<details>`, Navigation als Anker-Links mit `scroll-behavior: smooth`.
- Die Seite muss **ohne JavaScript vollständig lesbar und benutzbar** sein.
  Download-Link, Anleitung, Versionshistorie: alles im HTML, nichts
  nachgeladen.

**3. Nicht überfrachtet.** Das ist eine Werkzeugseite, keine Kampagne. Konkret
verboten:

- Scroll-Animationen, Einblend-Effekte, Parallax, Zähler, die hochlaufen,
  Typewriter-Effekte, animierte Gradienten, Partikel, Mauszeiger-Spielereien.
  Einzige erlaubte Bewegung: Hover- und Fokus-Übergänge unter 150 ms und die
  Aufklapp-Animation der `<details>`.
- Wiederholte Call-to-Action-Blöcke. **Ein** Download-Button im Hero, **einer**
  im Download-Abschnitt, einer klein in der Navigation. Mehr nicht.
- Dekoration ohne Aussage: keine Stock-Illustrationen, keine gefüllten
  Hintergrundflächen nur zur Abwechslung, keine Icons neben jeder Überschrift.
  Ein SVG kommt auf die Seite, wenn es etwas erklärt, das der Text nicht so
  schnell erklärt.
- Redundanz. Jede Aussage steht **an genau einer Stelle**. Wenn „113 KB" im Hero
  als Kennzahl steht, wird es nicht in drei weiteren Abschnitten wiederholt.

Was das positiv heißt: großzügiger Weißraum, eine Textspalte mit maximal ~70
Zeichen Zeilenlänge, klare Typo-Hierarchie aus wenigen Größen, und eine Seite,
die in unter einer Minute durchzulesen ist. Technische Tiefe gehört in
aufklappbare `<details>` – sichtbar für den, der sie sucht, unsichtbar für den,
der nur den Download will.

## Was ChatNicer ist

Ein Windows-Tray-Programm, das markierten Text per Hotkey umformulieren lässt –
und zwar von einem **lokal laufenden Ollama-Modell**. Der Text verlässt den
Rechner nicht.

Kernversprechen in drei Punkten, die auf der Seite auch als solche tragen sollen:

1. **Überall statt in einem Chatfenster.** Text markieren – in Outlook, Teams,
   Browser, Editor – `STRG + SHIFT + LEERTASTE` drücken, und die Markierung wird
   durch die bessere Fassung ersetzt. Kein Fenster wechseln, kein Copy-Paste-Pingpong.
2. **Lokal und privat.** Alles geht an `http://localhost:11434`. Keine Cloud, kein
   Konto, kein API-Schlüssel, keine Telemetrie.
3. **Absurd klein.** 113 KB EXE (115.712 Bytes). Reines Win32 + WinHTTP, keine
   einzige externe Bibliothek: kein .NET, kein Electron, kein curl, keine
   JSON-Bibliothek, kein VC++-Redistributable. Eine Datei, kein Installer.

Weitere Fakten, die auf der Seite vorkommen sollen:

- **Geschwindigkeit:** mit `qwen3:4b-instruct` rund **eine Sekunde** pro Anfrage
  (nach dem ersten Laden ~0,3 s Modellzeit).
- **Tray-Icon als Statusanzeige:** blau = bereit, orange = Modell arbeitet,
  rot = Fehler (mit Klartext-Benachrichtigung).
- **Warmup beim Start:** ChatNicer lädt das Modell direkt nach dem Start in
  Ollamas Speicher (leere Anfrage, kein einziges Token), damit die erste echte
  Anfrage nicht auf den Kaltstart wartet (~4 s gespart).
- **Zwei Modi:** *Einfügen* (Antwort kommt fertig, per Zwischenablage) und
  *Live tippen* (Streaming, der Text baut sich Zeichen für Zeichen auf, ~1,3 s
  für einen 271-Zeichen-Absatz).
- **Zwischenablage bleibt heil:** der vorherige Inhalt wird nach dem Einfügen
  wiederhergestellt – auch auf jedem Fehlerpfad.
- **Konfigurierbar:** Hotkey, Modell (Auswahlliste aus den installierten
  Modellen), System-Prompt, Temperature, Timeout – alles im nativen
  Einstellungsdialog, gespeichert in einer `config.ini` neben der EXE.
- **Prompt-Trick als Feature:** der markierte Text wird als
  `<text_to_process>` verpackt, damit ein kleines Modell ihn nicht als Anweisung
  an sich selbst missversteht (sonst antwortet es auf „was ist die hauptstadt von
  frankreich?" mit „Paris" statt die Frage zu glätten). Die Antwort muss in
  `<rewritten_text>` stehen und wird genau daraus herausgeschnitten – Vorreden wie
  „Hier ist mein Versuch:" landen nie im Dokument.
- **Voraussetzung:** Windows 10/11 (x64) und ein laufendes Ollama mit
  `ollama pull qwen3:4b-instruct`.

## Zielgruppe und Tonalität

Technisch interessierte Windows-Nutzer, die Ollama schon kennen oder bereit sind,
es zu installieren: Entwickler, Power-User, Datenschutzbewusste. Sie mögen
schlanke Werkzeuge und misstrauen Marketing.

Deshalb: **sachlich, präzise, mit echten Zahlen statt Superlativen.** Keine
Buzzwords („revolutionär", „KI-gestützt", „nächste Generation"), keine erfundenen
Nutzerstimmen, keine Logo-Leiste von Firmen, keine Sterne-Bewertungen, keine
Zähler. Wo etwas eine Einschränkung hat, wird sie genannt – Offenheit ist bei
dieser Zielgruppe das stärkere Verkaufsargument.

## Designrichtung

Die Seite soll das Produkt formal spiegeln: **schnell, schlank, dicht an der
Sache.** Ein Tool von 113 KB darf keine 3-MB-Homepage haben – dass die Seite
selbst winzig ist, ist Teil der Botschaft und darf im Footer beziffert werden.
Konkret:

- Systemschriften (Segoe UI / system-ui als Stack), Monospace für alles
  Technische.
- Farbwelt aus dem Tray-Icon: **Blau als Primärfarbe** (bereit), **Orange als
  Akzent** (arbeitet), **Rot nur für Fehlerzustände**. Sparsam einsetzen – viel
  ruhige Fläche, klare Typo-Hierarchie.
- **Dark Mode und Light Mode**, beide vollwertig gestaltet: `prefers-color-scheme`
  als Ausgangssignal, dazu ein sichtbarer Umschalter, dessen Wahl gewinnt.
- Leichte Windows-11-Anmutung ist erlaubt (abgerundete Ecken, dezente Tiefe),
  aber kein Skeuomorphismus und keine nachgebauten Fensterrahmen.
- **Responsive** ohne Kompromisse: relative Einheiten, Flex/Grid, Tabellen und
  Codeblöcke scrollen in ihrem eigenen Container; der Seitenkörper scrollt
  niemals horizontal.
- **Barrierefrei:** Kontrast mindestens AA, sichtbarer Fokus, semantische
  Überschriftenstruktur, `aria-label` an icon-only Buttons, Tastaturbedienbarkeit
  durchgehend.

Illustrationen bitte als **Inline-SVG oder CSS** erzeugen, nicht als Bilddatei
verlangen – es liegen keine Screenshots vor. Sinnvolle Motive: das Tray-Icon in
seinen drei Zuständen, eine schematische Ablaufkette (Markieren → Hotkey →
Ollama → Ersetzen), ein stilisiertes Textfeld, in dem sich Text austauscht.
Wo ein echter Screenshot später besser wäre, einen klar erkennbaren Platzhalter
mit Kommentar vorsehen (`<!-- Screenshot: Einstellungsdialog -->`).

## Aufbau der Seite

**Sieben Abschnitte, nicht mehr** – wer scrollt, soll ankommen, nicht ermüden.
Eine Seite, ein Scroll, oben eine schlanke klebende Leiste (Produktname links,
zwei bis drei Sprungmarken, kleiner Download-Button rechts – keine ausgebaute
Navigation).

Die Reihenfolge ist bewusst so: Beweis vor Erklärung, Download früh. Alles was
Tiefe hat, steckt in Abschnitt 6 in `<details>` und ist zugeklappt.

**1. Hero.** Produktname, ein Satz, der es erklärt („Markierten Text per Hotkey
von einem lokalen Modell umformulieren lassen – überall in Windows"), primärer
Download-Button, sekundär „Auf GitHub ansehen". Direkt darunter drei
Kennzahlen-Chips: `113 KB`, `~1 Sekunde`, `100 % lokal`. Dazu die
Voraussetzungszeile: „Windows 10/11 · x64 · benötigt Ollama". Kein
Hintergrundbild, keine Animation – der Hero ist Text, Buttons und Weißraum.

**2. So funktioniert's.** Die vier Schritte des Ablaufs als eine schmale Kette
(ein Inline-SVG, keine vier Karten). Der entscheidende Punkt: die Markierung wird
*ersetzt*, es gibt keinen Umweg über ein Chatfenster.

Direkt im selben Abschnitt – ohne eigene Überschrift – die drei echten Beispiele
aus den Testläufen als Vorher/Nachher-Paare. Sie sind der Beweis und tragen mehr
als jede Feature-Beschreibung:
- „das ist ein test mit fehlan" → „Das ist ein Test mit Fehlern."
- „mach mir ne liste mit 5 gemüsesorten" → „Mach mir bitte eine Liste mit fünf
  Gemüsesorten." (mit dem Hinweis: umformuliert, **nicht** beantwortet – genau
  das ist der schwierige Teil)
- „ey das geht so nicht du hast das verkackt" → höflich gefasste Version

**3. Was es kann.** Kompakte zweispaltige Liste, **keine Karten mit Icons und
Rahmen**: Zwei Modi (Einfügen / Live tippen), Statusanzeige im Tray, Warmup beim
Start, freier Hotkey, Modellauswahl aus den installierten Modellen, eigener
System-Prompt, Zwischenablage-Wiederherstellung, Verbindungstest im Dialog. Pro
Punkt eine Zeile, höchstens zwei – wer mehr wissen will, findet es in Abschnitt 6.

**4. Installation.** Drei nummerierte Schritte mit kopierbaren Codeblöcken
(Copy-Button an jedem):
1. Ollama installieren, dann `ollama pull qwen3:4b-instruct`
2. `ChatNicer.exe` herunterladen und irgendwohin legen – kein Installer, keine
   Registry-Einträge
3. Starten; beim ersten Start öffnet sich der Einstellungsdialog
Plus als Nebennotiz: Autostart über `Win + R` → `shell:startup` → Verknüpfung
hineinlegen.

**5. Lokal und privat.** Vier, fünf Zeilen, keine Tabelle, kein Vergleich mit
Cloud-Anbietern: Anfragen gehen an `localhost:11434`, kein Konto, kein
Schlüssel, keine Telemetrie, kein Auto-Update, keine Netzverbindung außer der zu
Ollama. Das ist eine Feststellung und braucht keine Bühne.

**6. Download.** Eigener Abschnitt, prominent: großer Button auf
`https://github.com/FloMaetschke/chatnicer/releases/latest/download/ChatNicer.exe`,
darunter Version `1.0`, Größe, Zielplattform (Windows 10/11 x64) und ein
Feld für die SHA-256-Prüfsumme (als Platzhalter, kopierbar). Zweitlink „Alle
Releases" und „Selbst bauen" (verweist auf `build.bat`, Visual Studio 2022 nicht
zwingend nötig, weil das Skript die Buildtools selbst findet). Hinweis, dass
Windows SmartScreen bei einer unsignierten EXE warnt und wie man weiterkommt –
das lieber ehrlich sagen als den Nutzer überraschen.

Im selben Abschnitt direkt darunter die **Versionshistorie**: schlichte Liste,
neueste Version oben, jeder Eintrag eine Zeile Kopf und ein paar Stichpunkte.
Keine dekorative Timeline mit Punkten und Verbindungslinien – wenn mehr als vier
Einträge zusammenkommen, bleiben die neuesten drei offen und der Rest steckt in
einem `<details>` („Ältere Versionen").

Stand aus den Commits (die
Versionsnummern sind noch nicht getaggt – bitte als solche übernehmen und im
Repo entsprechend taggen):

- **v1.2 · 26.07.2026** – Start-Warmup lädt das Modell schon beim Programmstart;
  Emoji-Regel im System-Prompt (Smileys werden nicht mehr in Worte übersetzt);
  tolerantere Erkennung verschriebener Antwort-Tags.
- **v1.1 · 23.07.2026** – Tippmodus: Antwort wird live getippt, während das
  Modell streamt (`"stream": true` + inkrementeller Tag-Filter).
- **v1.0 · 23.07.2026** – Standardmodell auf `qwen3:4b-instruct` umgestellt
  (1–4 s statt 14–27 s); Modellauswahl aus den installierten Modellen;
  Verbindungstest.
- **v0.9 · 23.07.2026** – Erste Fassung: Tray-Icon, globaler Hotkey,
  WinHTTP-Anbindung an die Ollama-API, handgeschriebener JSON-Umgang,
  Einstellungsdialog, `config.ini`.

Struktur so anlegen, dass ein neuer Eintrag ein einzelner kopierter Block ist.

**7. Details für Interessierte.** Der einzige Abschnitt mit Tiefe – und deshalb
komplett aus zugeklappten `<details>` aufgebaut (`<summary>` als Frage oder
Stichwort, kein JavaScript). Zugeklappt ist der ganze Abschnitt eine Liste von
fünf Zeilen; wer nichts davon braucht, scrollt in zwei Sekunden vorbei. Inhalt
der Blöcke:

**7a) „Welches Modell soll ich nehmen?"** Die Vergleichstabelle, weil sie echten
Nutzen hat:

| Modell | Dauer pro Anfrage | Bewertung |
|---|---|---|
| **qwen3:4b-instruct** | ~0,3 s | Standard: hält sich am zuverlässigsten an die Tag-Regel, bestes Deutsch |
| llama3.2:3b | ~0,3–1 s | ähnlich schnell, verhaspelt sich aber häufiger beim Tag |
| qwen3:4b | 14–27 s | dieselbe Größe, denkt aber sichtbar mit – für Inline-Einfügen zu langsam |
| qwen3.5:4b | ~16 s | denkt noch ausdauernder |

Mit der Kernaussage darüber: **Die Endung entscheidet, nicht die Größe.**
`qwen3:4b` und `qwen3:4b-instruct` sind gleich groß – die Instruct-Variante hat
die Denkphase gar nicht erst.

**7b) „Warum ist die EXE nur 113 KB?"** Die Größentabelle plus die Erklärung,
dass die vcruntime statisch und die Universal CRT dynamisch gebunden wird
(`ucrtbase.dll` gehört seit Windows 10 zum System, daher kein Redistributable),
dazu `/O1 /Os`, `/GL` + `/LTCG`, `/GR-`, `/GS-`, `/OPT:REF /OPT:ICF`, Exceptions
aus. Icons werden zur Laufzeit gezeichnet, es gibt keine Ressourcendatei.

| Variante | Größe | Laufzeitabhängigkeit |
|---|---:|---|
| **Standard-Release** | **115.712 B** | nur `ucrtbase.dll` (Windows-Systemdatei) |
| statische CRT + Exceptions | 212.480 B | keine |
| dynamische CRT (`/MD`) | 103.936 B | VC++-Redistributable erforderlich |

**7c) „Wie bringt man ein 4B-Modell dazu, den Text nicht zu beantworten?"** Der
Prompt-Vertrag: markierter Text als `<text_to_process>`, Antwort in
`<rewritten_text>`, das Herausschneiden fehlertolerant gegenüber verhaspelten
Tags. Für die Zielgruppe der interessanteste technische Teil – hier darf es
konkret werden, aber in höchstens zwei Absätzen.

**7d) „Was kann es nicht?"** Die bekannten Einschränkungen offen benennen, statt
sie zu verstecken – bei dieser Zielgruppe baut das Vertrauen auf:
- Ein kleines Modell bleibt ein kleines Modell: bei Texten, die stark nach einer
  Anweisung an eine KI klingen, kann es ausbrechen.
- Nur Text: gesichert und wiederhergestellt wird ausschließlich Unicode-Text; ein
  Bild in der Zwischenablage ist danach weg.
- Im Tippmodus werden Zeilenumbrüche als ENTER getippt – in Chat-Eingabefeldern
  (Teams, Slack, WhatsApp Web) schickt das die Nachricht ab. Für mehrzeilige
  Texte dort ist der Einfügemodus die sichere Wahl.
- Im Tippmodus gibt es ab dem ersten Zeichen kein Zurück: bricht die Verbindung
  ab, bleibt der halbe Text stehen.
- Fenster mit Administratorrechten nehmen keine simulierten Tastendrücke an
  (UIPI) – Abhilfe: ChatNicer ebenfalls als Administrator starten.
- Nur x64.

**7e) „Kurze Fragen"** – ein einzelnes `<details>` mit knappen Antwortpaaren,
je eine Zeile, keine eigene FAQ-Sektion: Braucht das Internet? Nein, nur Ollama.
Kostet es etwas? Nein, Open Source. Kann ich den Prompt ändern (z. B. zum
Übersetzen)? Ja, aber die `<rewritten_text>`-Regel muss bleiben. Warum warnt
Windows beim Start? Unsignierte EXE. Remote-Ollama? Ja, URL und optionaler
API-Key sind konfigurierbar. macOS/Linux? Nein, reines Win32.

**Footer.** Eine Zeile, zwei bei Umbruch: Link zum Repository, zur Lizenz, zu den
Issues; Hinweis „Nicht mit Ollama oder Anthropic affiliiert"; die Größe der Seite
selbst als kleiner Gag.

Was **nicht** auf die Seite kommt, obwohl es naheliegt: Roadmap, Vergleichsmatrix
gegen andere Tools, Spendenaufruf, Newsletter, „Featured on"-Leiste,
Social-Media-Buttons, ein zweiter Screenshot-Abschnitt. Jedes dieser Elemente
kostet Ladezeit und Aufmerksamkeit und bringt bei einem Gratis-Tool für
Power-User nichts.

## Technische Rahmenbedingungen (GitHub Pages)

Das muss die Umsetzung einhalten, sonst funktioniert das Hosting nicht:

- **Statisch, kein Buildschritt.** Ausgabe ist eine `index.html`, die direkt
  ausgeliefert werden kann – kein Jekyll-Frontmatter, kein npm, kein Bundler.
- Zusätzlich eine `.nojekyll`-Datei (leer) vorsehen, damit GitHub Pages nichts
  verarbeitet, und eine **`CNAME`-Datei mit genau einer Zeile:** `chatnicer.de`.
- **Alle Pfade relativ**, niemals absolut mit `/` beginnend – sonst bricht eine
  Vorschau unter `FloMaetschke.github.io/chatnicer/`. Am besten gibt es gar keine
  internen Pfade außer Ankern.
- **Keine Requests an fremde Hosts:** CSS und JS inline im Dokument,
  Grafiken als Inline-SVG oder Data-URI, Schriften aus dem System. Das ist
  gleichzeitig die Datenschutz-Aussage der Seite – eine Google-Font würde ihr
  widersprechen.
- **SEO und Sharing:** sprechender `<title>`, `<meta name="description">`,
  Open-Graph- und Twitter-Card-Tags, `<html lang="de">`, `canonical` auf
  `https://chatnicer.de/`, ein Favicon als Inline-SVG-Data-URI (das Tray-Icon in
  Blau). Dazu eine `robots.txt` und eine kleine `sitemap.xml`.
- Am Ende die **Einrichtungsschritte** dazuschreiben (als Kommentar im HTML oder
  als separate `DEPLOY.md`): Repository-Einstellungen → Pages → Branch wählen,
  Custom Domain eintragen, DNS setzen (`CNAME` auf
  `FloMaetschke.github.io` für die `www`-Variante, vier `A`-Records auf
  `185.199.108–111.153` für die Apex-Domain), „Enforce HTTPS" aktivieren.

## Lieferung

- Eine vollständige, sofort deploybare `index.html` mit allem Inhalt (keine
  Lorem-ipsum-Stellen, keine „hier Text einfügen"-Lücken – der Inhalt steht oben).
- Die Begleitdateien `CNAME`, `.nojekyll`, `robots.txt`, `sitemap.xml`.
- Eine kurze `DEPLOY.md` mit den Schritten für Pages und DNS.
- Wo eine Angabe erst später feststeht (Prüfsumme, Release-URL, Screenshots),
  einen eindeutig als solchen erkennbaren Platzhalter setzen und im HTML
  kommentieren.
- **Zum Schluss die Zahlen nennen:** Größe der `index.html` in KB, Anzahl der
  HTTP-Requests, Zeilen JavaScript. Liegt die Datei über 50 KB, nicht
  ausliefern, sondern kürzen – zuerst bei den SVGs und den `<details>`-Inhalten,
  nicht bei der Lesbarkeit des CSS.

## Prüfliste vor der Abgabe

Jeder Punkt muss zutreffen:

1. Ein Request, `index.html` unter 50 KB, kein `<link>` oder `<script src>` auf
   eine externe Adresse.
2. Mit deaktiviertem JavaScript ist die Seite vollständig lesbar; Download-Link,
   Anleitung und Versionshistorie funktionieren.
3. Keine Animation außer Hover/Fokus und `<details>`.
4. Sieben Abschnitte plus Footer, nicht mehr. Keine Aussage steht doppelt.
5. Light und Dark sehen beide fertig aus, der Umschalter überschreibt die
   Systemeinstellung in beide Richtungen.
6. Bei 320 px Breite scrollt der Seitenkörper nicht horizontal; Tabellen und
   Codeblöcke scrollen in ihrem eigenen Container.
7. Die Datei ist als UTF-8 gespeichert und alle Umlaute stehen korrekt.
