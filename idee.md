Erstelle ein vollständiges, kompilierbares C++ Projekt für Visual Studio (Windows 10/11) mit der Win32 API. 

**Ziel des Projekts:**
Ein extrem leichtgewichtiges Hintergrundprogramm (Zielgröße < 100 KB, ohne externe Abhängigkeiten wie .NET oder dicke HTTP-Bibliotheken), das als Windows-Tray-Icon läuft. Es soll per Tastenkombination Text kopieren, an einen Webhook senden und die Antwort wieder einfügen.

**Architektur & Anforderungen:**
1. **Tray-Icon & GUI:** 
   - Das Programm läuft standardmäßig unsichtbar im Hintergrund und zeigt ein Tray-Icon (Shell_NotifyIconW).
   - Ein Rechtsklick auf das Tray-Icon öffnet ein Kontextmenü mit den Optionen: "Einstellungen", "Info" und "Beenden".
   - Die Option "Einstellungen" öffnet ein einfaches, natives Win32-Dialogfenster (ohne Ressourcen-Datei, dynamisch im Code erzeugt oder per einfachem CreateWindowEx). Hier kann die Webhook-URL, ein API-Key/Bearer-Token und der System-Prompt konfiguriert werden. Die Daten werden lokal in einer simplen `config.ini` oder `config.json` im selben Ordner gespeichert.

2. **Globaler Hotkey (z. B. STRG + SHIFT + SPACE):**
   - Registrierung über `RegisterHotKey`.
   - Beim Auslösen des Hotkeys wird folgende Sequenz ausgeführt:
     a) Simuliere ein `STRG + C` via `SendInput()`, um den aktuell vom Nutzer markierten Text in die Zwischenablage zu kopieren.
     b) Warte kurz (ca. 100-200ms), damit Windows das Clipboard aktualisieren kann.
     c) Lies den Text sicher aus der Zwischenablage aus (`OpenClipboard`, `GetClipboardData`).

3. **HTTP Webhook & Payload:**
   - Nutze ausschließlich die Windows-eigene Bibliothek `WinHTTP` (`winhttp.h`), um keine externen Abhängigkeiten (wie curl) einzuschleppen.
   - Sende einen HTTP-POST-Request an die konfigurierte Webhook-URL.
   - Der Payload soll ein JSON-Objekt sein, das den system_prompt und den kopierten Text (user_text) enthält. Baue das JSON-String-Format manuell oder über eine minimalistische String-Verkettung, um JSON-Bibliotheken zu vermeiden.

4. **Response & Einfügen:**
   - Lies die HTTP-Antwort (nur den Text/Inhalt der KI) aus.
   - Schreibe diese Antwort zurück in die Windows-Zwischenablage.
   - Simuliere ein `STRG + V` via `SendInput()`, um den Text an der Stelle des Cursors einzufügen.
   - Stelle den vorherigen Inhalt der Zwischenablage optional wieder her, falls möglich, oder leere sie nach dem Einfügen.

5. **Code-Qualität & Optimierung:**
   - Stelle den vollständigen, sauberen Code in einer einzigen Datei (`main.cpp`) oder sauber getrennt (`main.cpp`, `config.h`, `network.h`) bereit.
   - Nutze Unicode-Funktionen (`wchar_t`, `std::wstring`).
   - Füge am Anfang des Codes als Kommentar die exakten Compiler-Flags für MSVC (Visual Studio) hinzu, die benötigt werden, um die EXE-Größe zu minimieren (z. B. Optimierung auf Größe `/Os`, Deaktivierung von RTTI `/GR-`, etc.).
   - Implementiere sauberes Error-Handling (z. B. wenn das Clipboard blockiert ist oder der Webhook fehlschlägt), idealerweise mit einer kurzen visuellen Rückmeldung wie einem Windows-Notification-Toast oder einem sich ändernden Tray-Icon.
