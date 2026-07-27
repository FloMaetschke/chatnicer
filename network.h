// =====================================================================================
//  network.h - WinHTTP-Anbindung an die Ollama-API + minimaler JSON-Bau/Parser
//
//  Keine externen Abhaengigkeiten: nur winhttp.lib (Teil von Windows).
//  JSON wird per String-Verkettung erzeugt und mit einem sehr kleinen,
//  robusten String-Extraktor wieder gelesen.
//
//  Verwendet wird POST /api/chat mit "stream": false:
//    {"model":"qwen3:4b-instruct","stream":false,
//     "options":{"repeat_penalty":1.05,"num_predict":512,"temperature":0.2},
//     "messages":[{"role":"system","content":"..."},{"role":"user","content":"..."}]}
//
//  Antwort:
//    {"model":"...","message":{"role":"assistant","thinking":"...","content":"..."},...}
//
//  Fuer den Tippmodus (Config::typingInput) laeuft dieselbe Anfrage mit
//  "stream": true. Ollama liefert dann NDJSON - eine JSON-Zeile pro Token-Haeppchen,
//  jede mit einem Teilstueck in message.content und die letzte mit "done":true.
//  ChatStream() reicht diese Teilstuecke einzeln nach oben durch; TagStream
//  schneidet dabei die <rewritten_text>-Klammer heraus, ohne den ganzen Text zu
//  kennen (siehe dort).
//
//  Wichtig bei denkenden Modellen (qwen3, deepseek-r1, ...): Ollama liefert den
//  Denkprozess im separaten Feld "thinking" und die eigentliche Antwort in
//  "content" - hier wird ausschliesslich "content" eingefuegt. Der Parameter
//  "think": false wird bewusst NICHT gesendet: damit hoert qwen3 nicht auf zu
//  denken, Ollama trennt die Felder dann aber nicht mehr und der komplette
//  Denkprozess landet im Antworttext.
// =====================================================================================
#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>

namespace net {

// -------------------------------------------------------------------------------------
// Encoding
// -------------------------------------------------------------------------------------
inline std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        &out[0], n, nullptr, nullptr);
    return out;
}

inline std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) { // kein gueltiges UTF-8 -> als ANSI interpretieren, statt Daten zu verlieren
        n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);
        return out;
    }
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

inline std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\r' || s[a] == L'\n' || s[a] == L'\t')) ++a;
    while (b > a && (s[b-1] == L' ' || s[b-1] == L'\r' || s[b-1] == L'\n' || s[b-1] == L'\t')) --b;
    return s.substr(a, b - a);
}

// -------------------------------------------------------------------------------------
// JSON schreiben
// -------------------------------------------------------------------------------------
inline std::wstring JsonEscape(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size() + 16);
    for (wchar_t c : in) {
        switch (c) {
        case L'\"': out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\b': out += L"\\b";  break;
        case L'\f': out += L"\\f";  break;
        case L'\n': out += L"\\n";  break;
        case L'\r': out += L"\\r";  break;
        case L'\t': out += L"\\t";  break;
        default:
            if (c < 0x20) {
                wchar_t esc[8];
                wsprintfW(esc, L"\\u%04x", static_cast<unsigned>(c));
                out += esc;
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

// Der markierte Text wird in ein XML-Tag gefasst, damit das Modell ihn als
// Material erkennt und nicht als Anweisung an sich selbst ausfuehrt. Das war im
// Test gegen llama3.2:3b der entscheidende Unterschied: ohne Tags beantwortet
// das Modell eine markierte Frage, statt sie umzuformulieren.
inline std::wstring WrapUserText(const std::wstring& userText) {
    return L"<text_to_process>" + Trim(userText) + L"</text_to_process>";
}

// Grobe Token-Schaetzung. Deutscher Text zerfaellt bei den ueblichen Tokenizern
// in rund drei Zeichen pro Token; absichtlich grosszuegig gerechnet, damit das
// Kontextfenster eher zu gross als zu klein ausfaellt.
inline size_t EstimateTokens(size_t chars) { return chars / 3 + 16; }

// Die "options" des Requests. Drei Werte, jeder mit einem konkreten Grund:
//
// - num_predict deckelt die Antwort. Ohne Deckel schreiben kleine Modelle nach
//   dem schliessenden Tag weiter (Erklaerungen, Alternativvorschlaege) - das
//   verwirft ExtractTagged zwar, aber die Anfrage dauert dann ein Vielfaches.
//   Bei denkenden Modellen ist der Deckel allerdings gefaehrlich, weil der
//   Denkprozess ihn allein aufbrauchen kann; deshalb laesst sich er ueber
//   capLength abschalten (siehe den Zweitversuch in Chat()).
// - num_ctx wird nur angehoben, wenn der markierte Text nicht in Ollamas
//   Standardfenster passt. Ein pauschaler Wert wuerde eine groessere globale
//   Einstellung (OLLAMA_CONTEXT_LENGTH) wieder verkleinern; ohne jede Angabe
//   wird langer markierter Text dagegen stillschweigend abgeschnitten.
// - repeat_penalty liegt unter Ollamas Standard 1.1: beim Umschreiben muessen
//   Eigennamen, Zahlen und Fachbegriffe wortgleich wiederkehren duerfen.
inline std::wstring BuildOptions(size_t promptChars, size_t textChars,
                                 const std::wstring& temperature, bool capLength) {
    size_t predict = EstimateTokens(textChars) * 2 + 128;
    if (predict < 256)  predict = 256;
    if (predict > 4096) predict = 4096;

    size_t ctx = 4096;
    const size_t need = EstimateTokens(promptChars + textChars + 64) + predict;
    while (ctx < need && ctx < 32768) ctx *= 2;

    wchar_t num[16];
    std::wstring o = L"{\"repeat_penalty\":1.05";
    if (capLength) {
        o += L",\"num_predict\":";
        wsprintfW(num, L"%u", static_cast<unsigned>(predict));
        o += num;
    }
    if (ctx > 4096) {
        wsprintfW(num, L"%u", static_cast<unsigned>(ctx));
        o += L",\"num_ctx\":";
        o += num;
    }
    if (!temperature.empty()) {
        o += L",\"temperature\":";
        o += temperature;      // in cfg::Load auf eine reine Dezimalzahl geprueft
    }
    o += L"}";
    return o;
}

// Baut den Request-Body fuer POST /api/chat
inline std::wstring BuildChatPayload(const std::wstring& model,
                                     const std::wstring& systemPrompt,
                                     const std::wstring& userText,
                                     const std::wstring& temperature,
                                     bool capLength = true,
                                     bool stream    = false) {
    std::wstring j = L"{\"model\":\"";
    j += JsonEscape(model);
    j += stream ? L"\",\"stream\":true,\"options\":" : L"\",\"stream\":false,\"options\":";
    j += BuildOptions(systemPrompt.size(), userText.size(), temperature, capLength);
    j += L",\"messages\":[";
    if (!Trim(systemPrompt).empty()) {
        j += L"{\"role\":\"system\",\"content\":\"";
        j += JsonEscape(systemPrompt);
        j += L"\"},";
    }
    j += L"{\"role\":\"user\",\"content\":\"";
    j += JsonEscape(WrapUserText(userText));
    j += L"\"}]}";
    return j;
}

// -------------------------------------------------------------------------------------
// JSON lesen (nur so viel wie noetig: String-Werte zu einem Schluessel)
// -------------------------------------------------------------------------------------
inline std::wstring JsonUnescape(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != L'\\' || i + 1 >= in.size()) { out += in[i]; continue; }
        wchar_t e = in[++i];
        switch (e) {
        case L'n': out += L'\n'; break;
        case L'r': out += L'\r'; break;
        case L't': out += L'\t'; break;
        case L'b': out += L'\b'; break;
        case L'f': out += L'\f'; break;
        case L'u': {
            unsigned v = 0;
            size_t got = 0;
            while (got < 4 && i + 1 < in.size()) {
                wchar_t h = in[i + 1];
                unsigned d;
                if      (h >= L'0' && h <= L'9') d = static_cast<unsigned>(h - L'0');
                else if (h >= L'a' && h <= L'f') d = static_cast<unsigned>(h - L'a' + 10);
                else if (h >= L'A' && h <= L'F') d = static_cast<unsigned>(h - L'A' + 10);
                else break;
                v = v * 16 + d;
                ++i; ++got;
            }
            if (got == 4) out += static_cast<wchar_t>(v); // Surrogate bleiben als UTF-16 erhalten
            break;
        }
        default: out += e; break;
        }
    }
    return out;
}

// Sucht ab "from" nach  "key" : "wert"  und liefert den entschluesselten Wert.
// Escapte Anfuehrungszeichen innerhalb von Werten werden korrekt uebersprungen,
// ein Treffer im Inhalt eines anderen Strings ist daher nicht moeglich.
inline bool JsonFindStringFrom(const std::wstring& json, const std::wstring& key,
                               size_t from, std::wstring& out) {
    const std::wstring needle = L"\"" + key + L"\"";
    size_t pos = from;
    while ((pos = json.find(needle, pos)) != std::wstring::npos) {
        size_t p = pos + needle.size();
        while (p < json.size() && (json[p] == L' ' || json[p] == L'\t' ||
                                   json[p] == L'\r' || json[p] == L'\n')) ++p;
        if (p >= json.size() || json[p] != L':') { pos += needle.size(); continue; }
        ++p;
        while (p < json.size() && (json[p] == L' ' || json[p] == L'\t' ||
                                   json[p] == L'\r' || json[p] == L'\n')) ++p;
        if (p >= json.size() || json[p] != L'\"') { pos += needle.size(); continue; }
        ++p;
        const size_t start = p;
        while (p < json.size()) {
            if (json[p] == L'\\') { p += 2; continue; }
            if (json[p] == L'\"') break;
            ++p;
        }
        if (p >= json.size()) return false;
        out = JsonUnescape(json.substr(start, p - start));
        return true;
    }
    return false;
}

inline bool JsonFindString(const std::wstring& json, const std::wstring& key, std::wstring& out) {
    return JsonFindStringFrom(json, key, 0, out);
}

// Alle Werte eines Schluessels sammeln - fuer die Modelliste aus /api/tags.
inline std::vector<std::wstring> JsonFindAllStrings(const std::wstring& json,
                                                    const std::wstring& key) {
    std::vector<std::wstring> all;
    const std::wstring needle = L"\"" + key + L"\"";
    size_t pos = 0;
    while (pos < json.size()) {
        size_t hit = json.find(needle, pos);
        if (hit == std::wstring::npos) break;
        std::wstring val;
        if (JsonFindStringFrom(json, key, hit, val)) all.push_back(val);
        pos = hit + needle.size();
    }
    return all;
}

// -------------------------------------------------------------------------------------
// Ollama-spezifische Auswertung
// -------------------------------------------------------------------------------------

// Manche Modelle schreiben ihren Denkprozess trotz allem in den Antworttext.
// Alles bis einschliesslich des letzten </think> wird deshalb verworfen.
inline std::wstring StripThinking(const std::wstring& in) {
    std::wstring t = in;
    const size_t end = t.rfind(L"</think>");
    if (end != std::wstring::npos) t = t.substr(end + 8);
    return Trim(t);
}

// Zeichen, aus denen ein Tag-Name bestehen darf. Das Leerzeichen ist Absicht:
// "</rewritten text>" gehoert zu den real beobachteten Verhunzungen.
inline bool IsTagNameChar(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' || c == L' ';
}

inline wchar_t LowerAscii(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + (L'a' - L'A')) : c;
}

// Meint der Tag-Name in [a,b) unseren Antwortrahmen?
//
// Der buchstabengenaue Vergleich reicht nicht: real aufgetreten ist
// "</rewrittening_text>", und ein solches Tag blieb dadurch im eingefuegten Text
// stehen. Umgekehrt darf ein echtes </div> aus dem Nutzertext nicht als Rahmen
// durchgehen - deshalb die Bedingung "beginnt mit rewrit oder enthaelt text"
// statt einer allgemeinen Tag-Erkennung.
inline bool IsFrameTagName(const std::wstring& s, size_t a, size_t b) {
    if (b <= a || b - a > 32) return false;
    std::wstring n;
    for (size_t i = a; i < b; ++i) {
        if (!IsTagNameChar(s[i])) return false;
        n += LowerAscii(s[i]);
    }
    return n.compare(0, 6, L"rewrit") == 0 || n.find(L"text") != std::wstring::npos;
}

// Kann das ab s[a] angefangene Tag noch unser Rahmen werden? Nur dann lohnt es,
// im Stream darauf zu warten. Geprueft wird gegen den Stamm "rewrit" - die
// Verhunzungen treten hinten im Namen auf, nicht am Anfang.
inline bool CouldBeFrameTag(const std::wstring& s, size_t a) {
    static const wchar_t kStem[] = L"rewrit";
    if (a < s.size() && s.size() - a > 32) return false;
    for (size_t i = a; i < s.size(); ++i) {
        const size_t k = i - a;
        if (!IsTagNameChar(s[i]))                        return false;
        if (k < 6 && LowerAscii(s[i]) != kStem[k])       return false;
    }
    return true;
}

// Entfernt einen Rahmen, den die buchstabengenaue Suche nicht erkannt hat -
// aber nur am Anfang oder am Ende. Mitten im Text waere ein <...> eher echter
// Inhalt als ein Modellfehler, und der darf nicht verschwinden.
inline void StripStrayFrame(std::wstring& t) {
    if (!t.empty() && t[t.size() - 1] == L'>') {
        const size_t p = t.rfind(L'<');
        if (p != std::wstring::npos) {
            size_t a = p + 1;
            if (a < t.size() && t[a] == L'/') ++a;
            if (IsFrameTagName(t, a, t.size() - 1)) { t.resize(p); t = Trim(t); }
        }
    }
    if (!t.empty() && t[0] == L'<') {
        const size_t gt = t.find(L'>');
        size_t a = 1;
        if (a < t.size() && t[a] == L'/') ++a;
        if (gt != std::wstring::npos && IsFrameTagName(t, a, gt)) {
            t.erase(0, gt + 1);
            t = Trim(t);
        }
    }
}

// Schneidet den Inhalt von <rewritten_text> heraus.
//
// Bewusst tolerant: kleine Modelle verhaspeln sich beim Tag regelmaessig und
// schreiben "<rewritten_text" ohne ">", lassen das schliessende Tag weg, kleben
// den Text direkt an den Tag-Namen oder vertippen sich im Namen selbst. Alle
// diese Faelle sind real aufgetreten und liefern hier trotzdem den richtigen
// Text. Fehlt das Tag ganz, bleibt die Antwort unveraendert.
inline std::wstring ExtractTagged(const std::wstring& in) {
    static const wchar_t  kOpen[]  = L"<rewritten_text";
    static const wchar_t  kClose[] = L"</rewritten_text";
    const size_t openLen = 15;   // Laenge von "<rewritten_text"

    std::wstring out;
    const size_t open = in.find(kOpen);
    if (open == std::wstring::npos) {
        // Nur das schliessende Tag da? Dann steht die Antwort davor.
        const size_t lone = in.find(kClose);
        out = Trim(lone == std::wstring::npos ? in : in.substr(0, lone));
    } else {
        size_t start = open + openLen;
        if (start < in.size() && in[start] == L'>') ++start;   // normaler Fall

        size_t end = in.find(kClose, start);
        if (end == std::wstring::npos) end = in.size();        // nicht geschlossen

        out = Trim(in.substr(start, end - start));
    }

    StripStrayFrame(out);
    return out;
}

// Der rohe Antworttext aus der Ollama-Antwort, ohne jede Tag-Auswertung.
// Gezielt innerhalb von "message" gesucht, damit ein vorangehendes "thinking"
// nicht stoert.
inline bool ChatContent(const std::wstring& body, std::wstring& out) {
    const size_t msg = body.find(L"\"message\"");
    if (msg != std::wstring::npos && JsonFindStringFrom(body, L"content", msg, out)) return true;
    if (JsonFindString(body, L"content",  out)) return true;
    if (JsonFindString(body, L"response", out)) return true;   // /api/generate
    return false;
}

// Liefert message.content aus der Ollama-Antwort, bereinigt um Denkprozess
// und XML-Rahmen.
inline std::wstring ExtractChatAnswer(const std::wstring& body) {
    std::wstring val;
    if (!ChatContent(body, val)) return std::wstring();
    return ExtractTagged(StripThinking(val));
}

// Schneidet die einzelnen <reply>-Bloecke der Vorschlagsantwort heraus
// (zweiter Hotkey, siehe cfg::kReplyPrompt).
//
// Tolerant wie ExtractTagged: ein fehlendes ">" oder ein fehlendes schliessendes
// Tag darf den Vorschlag nicht kosten. Liefert das Modell gar keine Tags, gilt
// die ganze Antwort als ein einzelner Vorschlag - eine Schaltflaeche ist immer
// noch besser als eine leere Auswahl.
inline std::vector<std::wstring> ExtractReplies(const std::wstring& body, size_t maxCount) {
    std::vector<std::wstring> out;
    std::wstring raw;
    if (!ChatContent(body, raw)) return out;

    const std::wstring text = StripThinking(raw);
    const size_t openLen = 6;                       // Laenge von "<reply"

    size_t pos = 0;
    while (out.size() < maxCount) {
        const size_t open = text.find(L"<reply", pos);
        if (open == std::wstring::npos) break;

        size_t start = open + openLen;
        if (start < text.size() && text[start] == L'>') {
            ++start;
        } else {
            // "<reply attr=..>" oder ein vergessenes ">" - nur in der Naehe suchen,
            // sonst verschluckt ein spaeteres ">" den halben Text.
            const size_t gt = text.find(L'>', open);
            if (gt != std::wstring::npos && gt - open <= 24) start = gt + 1;
        }

        const size_t close = text.find(L"</reply", start);
        const size_t stop  = (close == std::wstring::npos) ? text.size() : close;

        std::wstring one = Trim(text.substr(start, stop - start));
        StripStrayFrame(one);
        if (!one.empty()) out.push_back(one);

        if (close == std::wstring::npos) break;
        pos = close + 7;
    }

    if (out.empty()) {
        std::wstring one = Trim(text);
        StripStrayFrame(one);
        if (!one.empty()) out.push_back(one);
    }
    return out;
}

// -------------------------------------------------------------------------------------
// Streaming: dieselbe Klammer inkrementell aufschneiden
//
// ExtractTagged() sieht den fertigen Text und kann darin suchen. Beim Streaming
// kommt der Text haeppchenweise, und was einmal getippt wurde, laesst sich nicht
// zuruecknehmen. TagStream haelt deshalb genau so viel zurueck, wie noch Teil
// eines Tags werden koennte:
//
//   - am Anfang, bis klar ist, ob ein <rewritten_text> (oder <think>) beginnt,
//   - am Ende jedes Haeppchens ein moegliches Praefix von "</rewritten_text",
//   - abschliessende Leerzeichen (sonst bliebe der Umbruch vor dem Tag stehen).
//
// Ein Modell, das gar kein Tag schreibt, laeuft dadurch ohne Verzoegerung durch.
// -------------------------------------------------------------------------------------
inline bool IsWsChar(wchar_t c) {
    return c == L' ' || c == L'\r' || c == L'\n' || c == L'\t';
}

inline void TrimLeftIn(std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && IsWsChar(s[a])) ++a;
    if (a) s.erase(0, a);
}

inline void TrimRightIn(std::wstring& s) {
    size_t b = s.size();
    while (b > 0 && IsWsChar(s[b - 1])) --b;
    if (b < s.size()) s.resize(b);
}

// Ist s (noch) ein echtes Praefix von t? Dann muss weiter gewartet werden.
inline bool CouldBecome(const std::wstring& s, const wchar_t* t) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (t[i] == L'\0' || s[i] != t[i]) return false;
    }
    return true;
}

// Laenge des laengsten Suffixes von s, das ein echtes Praefix von t ist.
inline size_t TailPrefixLen(const std::wstring& s, const wchar_t* t) {
    size_t tlen = 0;
    while (t[tlen]) ++tlen;
    size_t n = (s.size() < tlen - 1) ? s.size() : tlen - 1;
    for (; n > 0; --n) {
        bool same = true;
        for (size_t i = 0; i < n; ++i) {
            if (s[s.size() - n + i] != t[i]) { same = false; break; }
        }
        if (same) return n;
    }
    return 0;
}

struct TagStream {
    std::wstring pending;          // zurueckgehalten, weil noch Tag werden koennte
    bool started = false;          // Oeffnungs-Tag abgehandelt
    bool emitted = false;          // schon sichtbarer Text herausgegeben
    bool done    = false;          // schliessendes Tag gesehen -> Rest verwerfen
    bool inThink = false;

    void Reset() { pending.clear(); started = emitted = done = inThink = false; }

    // Haengt den freigegebenen Text an "out" an (Ausgabeparameter statt Rueckgabe:
    // spart im Build ohne Exceptions wie im compat-Build spuerbar Code).
    void Feed(const std::wstring& delta, std::wstring& out) {
        if (done) return;
        pending += delta;

        for (;;) {
            // Denkprozess im content (siehe StripThinking) - verwerfen bis </think>
            if (inThink) {
                const size_t end = pending.find(L"</think>");
                if (end == std::wstring::npos) {
                    const size_t keep = TailPrefixLen(pending, L"</think>");
                    pending.erase(0, pending.size() - keep);
                    return;
                }
                pending.erase(0, end + 8);
                inThink = false;
                continue;                       // danach kann das Oeffnungs-Tag folgen
            }

            if (!started) {
                TrimLeftIn(pending);
                if (pending.empty()) return;
                if (pending[0] == L'<') {
                    if (pending.compare(0, 7, L"<think>") == 0) {
                        pending.erase(0, 7);
                        inThink = true;
                        continue;
                    }
                    if (CouldBecome(pending, L"<think>")) return;   // warten
                    const size_t gt = pending.find(L'>');
                    if (gt == std::wstring::npos) {
                        if (CouldBeFrameTag(pending, 1)) return;    // warten
                    } else if (IsFrameTagName(pending, 1, gt)) {
                        pending.erase(0, gt + 1);
                    } else if (pending.compare(0, 15, L"<rewritten_text") == 0) {
                        pending.erase(0, 15);                       // Text klebt am Namen
                    }
                }
                started = true;
            }

            // Vor dem ersten sichtbaren Zeichen fuehrende Umbrueche schlucken
            if (!emitted) {
                TrimLeftIn(pending);
                if (pending.empty()) return;
            }

            // Bis zum schliessenden Tag bzw. bis zu dem, was noch eines werden kann.
            // Gesucht wird jedes "</...>", dessen Name unser Rahmen sein koennte -
            // "</rewrittening_text>" wuerde sonst mitgetippt und liesse sich nicht
            // mehr zuruecknehmen. Ein fremdes </div> im Text laeuft durch.
            // Ein abschliessender Umbruch bleibt liegen, sonst stuende er vor dem Tag.
            size_t close = std::wstring::npos;
            size_t cut   = pending.size();
            for (size_t p = pending.find(L"</"); p != std::wstring::npos;
                 p = pending.find(L"</", p + 1)) {
                const size_t gt = pending.find(L'>', p + 2);
                if (gt == std::wstring::npos) break;   // unvollstaendig, siehe unten
                if (IsFrameTagName(pending, p + 2, gt)) { close = p; cut = p; break; }
            }
            // Am Puffer-Ende kann ein Tag gerade erst angefangen haben - schon ein
            // einzelnes '<' muss zurueckgehalten werden, sonst ist es getippt,
            // bevor der Rest des Tags ueberhaupt eintrifft.
            if (close == std::wstring::npos) {
                const size_t lt = pending.rfind(L'<');
                if (lt != std::wstring::npos && pending.find(L'>', lt) == std::wstring::npos &&
                    (lt + 1 == pending.size() ||
                     (pending[lt + 1] == L'/' && CouldBeFrameTag(pending, lt + 2))))
                    cut = lt;
            }
            while (cut > 0 && IsWsChar(pending[cut - 1])) --cut;

            if (cut > 0) {
                emitted = true;
                out.append(pending, 0, cut);
            }
            if (close != std::wstring::npos) { pending.clear(); done = true; }
            else                               pending.erase(0, cut);
            return;
        }
    }

    // Stream zu Ende: was noch zurueckgehalten wird, war doch kein Tag - es sei
    // denn, es ist ein angefangenes. Haengt wie Feed() an "out" an.
    void Flush(std::wstring& out) {
        const bool wasDone = done;
        done = true;

        const bool tagFragment =
            (!started && pending.compare(0, 1, L"<") == 0 &&
                         (CouldBeFrameTag(pending, 1) ||
                          CouldBecome(pending, L"<think>"))) ||
            ( started && (pending == L"<" ||
                          (pending.compare(0, 2, L"</") == 0 &&
                           CouldBeFrameTag(pending, 2))));
        if (wasDone || tagFragment || inThink) { pending.clear(); return; }

        TrimLeftIn(pending);
        TrimRightIn(pending);
        if (!pending.empty()) { emitted = true; out += pending; }
        pending.clear();
    }
};

// Ein Teilstueck aus einer NDJSON-Zeile des Streams (message.content).
// Rueckgabe false heisst nur "kein Text in dieser Zeile" - die letzte Zeile
// (mit "done":true) enthaelt regulaer einen leeren content.
inline bool ExtractStreamDelta(const std::wstring& line, std::wstring& out) {
    const size_t msg = line.find(L"\"message\"");
    if (msg == std::wstring::npos) return false;
    return JsonFindStringFrom(line, L"content", msg, out) && !out.empty();
}

// Ollama meldet mit "done_reason":"length", dass num_predict aufgebraucht wurde.
inline bool HitTokenLimit(const std::wstring& body) {
    std::wstring reason;
    return JsonFindString(body, L"done_reason", reason) && reason == L"length";
}

// Hat das Modell ueberhaupt eine Antwort geschrieben? Bewusst nur message.content
// geprueft, ohne Tag-Extraktion - ein denkendes Modell liefert hier einen leeren
// String, waehrend "thinking" vollgelaufen ist.
inline bool ContentIsEmpty(const std::wstring& body) {
    std::wstring val;
    size_t from = body.find(L"\"message\"");
    if (from == std::wstring::npos) from = 0;
    return !JsonFindStringFrom(body, L"content", from, val) || Trim(val).empty();
}

// Ollama meldet Probleme als {"error":"..."} - daraus eine lesbare Meldung machen.
inline bool ExtractOllamaError(const std::wstring& body, std::wstring& out) {
    return JsonFindString(body, L"error", out);
}

// Aus "http://localhost:11434" wird "http://localhost:11434/api/chat".
// Ein bereits angegebener Pfad (z. B. hinter einem Reverse Proxy) bleibt erhalten.
inline std::wstring BuildEndpoint(const std::wstring& base, const std::wstring& apiPath) {
    std::wstring url = Trim(base);
    if (url.empty()) return url;
    while (!url.empty() && url.back() == L'/') url.pop_back();

    // Steht hinter dem Host schon ein Pfad? Dann nicht hineinpfuschen.
    const size_t schemeEnd = url.find(L"://");
    const size_t hostStart = (schemeEnd == std::wstring::npos) ? 0 : schemeEnd + 3;
    if (url.find(L'/', hostStart) != std::wstring::npos) return url;

    return url + apiPath;
}

// -------------------------------------------------------------------------------------
// HTTP
// -------------------------------------------------------------------------------------
struct Result {
    bool         ok     = false;
    DWORD        status = 0;
    std::wstring body;
    std::wstring error;
};

inline std::wstring WinHttpErrorText(DWORD code) {
    HMODULE mod = GetModuleHandleW(L"winhttp.dll");
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_FROM_HMODULE   | FORMAT_MESSAGE_IGNORE_INSERTS,
                             mod, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring msg;
    if (n && buf) {
        msg.assign(buf, n);
        while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L' '))
            msg.pop_back();
    }
    if (buf) LocalFree(buf);
    if (msg.empty()) {
        wchar_t tmp[64];
        wsprintfW(tmp, L"WinHTTP-Fehler %u", code);
        msg = tmp;
    }
    return msg;
}

// RAII fuer WinHTTP-Handles (kein Leak bei fruehem return)
struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET x) : h(x) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    operator HINTERNET() const { return h; }
};

// Wird beim Streaming fuer jede vollstaendige NDJSON-Zeile aufgerufen.
// Bewusst ein roher Funktionszeiger + void*: <functional> kostet mehrere zehn KB.
typedef void (*LineSink)(const std::wstring& jsonLine, void* user);

// Zerlegt den Lesepuffer an '\n' und reicht jede vollstaendige Zeile weiter.
// Zeilenenden sind ASCII, ein Haeppchen darf also mitten in einem UTF-8-Zeichen
// enden - die Zeile selbst ist immer vollstaendig.
inline void PumpLines(std::string& raw, std::wstring& lastLine, LineSink sink, void* user) {
    for (;;) {
        const size_t nl = raw.find('\n');
        if (nl == std::string::npos) return;
        std::string line = raw.substr(0, nl);
        raw.erase(0, nl + 1);
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        lastLine = FromUtf8(line);
        sink(lastLine, user);
    }
}

// Ein HTTP-Request (POST mit JSON-Body oder GET, wenn utf8Body leer ist).
//
// Ist "sink" gesetzt und der Status < 400, wird der Body nicht gesammelt, sondern
// zeilenweise durchgereicht; r.body enthaelt danach nur noch die letzte Zeile -
// genau die mit "done":true, aus der HitTokenLimit() liest. Bei Status >= 400
// bleibt es beim vollstaendigen Body, sonst ginge die Fehlermeldung verloren.
inline Result Request(const std::wstring& url,
                      const wchar_t*      method,
                      const std::wstring& bearer,
                      const std::string&  utf8Body,
                      DWORD               timeoutMs,
                      LineSink            sink = nullptr,
                      void*               user = nullptr) {
    Result r;

    if (Trim(url).empty()) {
        r.error = L"Keine Ollama-URL konfiguriert.";
        return r;
    }

    // --- URL zerlegen -----------------------------------------------------------------
    wchar_t host[256] = {};
    wchar_t path[4096] = {};
    URL_COMPONENTS uc = {};
    uc.dwStructSize      = sizeof(uc);
    uc.lpszHostName      = host;  uc.dwHostNameLength     = ARRAYSIZE(host);
    uc.lpszUrlPath       = path;  uc.dwUrlPathLength      = ARRAYSIZE(path);
    uc.dwSchemeLength    = 1;     // nur nScheme wird benoetigt
    uc.dwExtraInfoLength = 1;

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &uc)) {
        r.error = L"Ungueltige URL: " + WinHttpErrorText(GetLastError());
        return r;
    }
    const bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    std::wstring fullPath = path;
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength)
        fullPath.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (fullPath.empty()) fullPath = L"/";

    // --- Session / Verbindung / Request ------------------------------------------------
    Handle session(WinHttpOpen(L"ChatNicer/1.1",
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        r.error = L"WinHttpOpen: " + WinHttpErrorText(GetLastError());
        return r;
    }
    WinHttpSetTimeouts(session, 10000, 10000, static_cast<int>(timeoutMs), static_cast<int>(timeoutMs));

    Handle connect(WinHttpConnect(session, host, uc.nPort, 0));
    if (!connect) {
        r.error = L"Verbindung fehlgeschlagen: " + WinHttpErrorText(GetLastError());
        return r;
    }

    Handle request(WinHttpOpenRequest(connect, method, fullPath.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) {
        r.error = L"WinHttpOpenRequest: " + WinHttpErrorText(GetLastError());
        return r;
    }

    std::wstring headers = L"Accept: */*\r\n";
    if (!utf8Body.empty())
        headers += L"Content-Type: application/json; charset=utf-8\r\n";
    if (!bearer.empty()) {
        // Ein bereits vollstaendig angegebenes Schema (z. B. "Basic ...") nicht doppelt praefixen.
        const bool hasScheme = bearer.find(L' ') != std::wstring::npos;
        headers += L"Authorization: ";
        if (!hasScheme) headers += L"Bearer ";
        headers += bearer;
        headers += L"\r\n";
    }

    if (!WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()),
                            utf8Body.empty() ? WINHTTP_NO_REQUEST_DATA
                                             : const_cast<char*>(utf8Body.data()),
                            static_cast<DWORD>(utf8Body.size()),
                            static_cast<DWORD>(utf8Body.size()), 0)) {
        r.error = L"Senden fehlgeschlagen: " + WinHttpErrorText(GetLastError());
        return r;
    }

    if (!WinHttpReceiveResponse(request, nullptr)) {
        r.error = L"Keine Antwort erhalten: " + WinHttpErrorText(GetLastError());
        return r;
    }

    // --- Status + Body ------------------------------------------------------------------
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    r.status = status;

    const bool streaming = (sink != nullptr && status < 400);
    std::string  raw;          // beim Streaming nur der Rest hinter der letzten Zeile
    std::wstring lastLine;
    bool bomChecked = false;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) {
            r.error = L"Lesefehler: " + WinHttpErrorText(GetLastError());
            return r;
        }
        if (avail == 0) break;
        const size_t old = raw.size();
        raw.resize(old + avail);
        DWORD read = 0;
        if (!WinHttpReadData(request, &raw[old], avail, &read)) {
            r.error = L"Lesefehler: " + WinHttpErrorText(GetLastError());
            return r;
        }
        raw.resize(old + read);
        if (read == 0) break;

        if (streaming) {
            if (!bomChecked && raw.size() >= 3) {
                if (static_cast<BYTE>(raw[0]) == 0xEF && static_cast<BYTE>(raw[1]) == 0xBB &&
                    static_cast<BYTE>(raw[2]) == 0xBF) raw.erase(0, 3);
                bomChecked = true;
            }
            PumpLines(raw, lastLine, sink, user);
        } else if (raw.size() > 32u * 1024u * 1024u) {
            break;                                   // Schutz gegen Endlos-Streams
        }
    }

    if (streaming) {
        raw += '\n';                                 // letzte Zeile ohne Umbruch
        PumpLines(raw, lastLine, sink, user);
        r.body = lastLine;
    } else {
        // BOM einer UTF-8-Antwort entfernen
        if (raw.size() >= 3 && static_cast<BYTE>(raw[0]) == 0xEF &&
            static_cast<BYTE>(raw[1]) == 0xBB && static_cast<BYTE>(raw[2]) == 0xBF) {
            raw.erase(0, 3);
        }
        r.body = FromUtf8(raw);
    }

    if (status >= 400) {
        std::wstring detail;
        if (!ExtractOllamaError(r.body, detail)) detail = Trim(r.body);
        if (detail.size() > 300) detail.resize(300);
        wchar_t code[32];
        wsprintfW(code, L"HTTP %u", status);
        r.error = detail.empty() ? code : (std::wstring(code) + L": " + detail);
        return r;
    }

    r.ok = true;
    return r;
}

// -------------------------------------------------------------------------------------
// Anfrage ans Modell - einmal als Ganzes (Chat) und einmal als Stream (ChatStream).
// Beide teilen sich ChatCore, damit vor allem der Zweitversuch nur an einer Stelle
// steht: er ist fuer jedes denkende Modell ueberlebenswichtig.
// -------------------------------------------------------------------------------------
typedef void (*TextSink)(const std::wstring& text, void* user);

struct StreamCtx {
    TagStream filter;
    TextSink  sink  = nullptr;
    void*     user  = nullptr;
    size_t    chars = 0;        // sichtbar herausgegebene Zeichen
};

inline void StreamOnLine(const std::wstring& line, void* user) {
    StreamCtx* ctx = static_cast<StreamCtx*>(user);
    std::wstring delta;
    if (!ExtractStreamDelta(line, delta)) return;
    std::wstring visible;
    ctx->filter.Feed(delta, visible);
    if (!visible.empty()) {
        ctx->chars += visible.size();
        ctx->sink(visible, ctx->user);
    }
}

inline void StreamFlush(StreamCtx& ctx) {
    std::wstring rest;
    ctx.filter.Flush(rest);
    if (!rest.empty()) {
        ctx.chars += rest.size();
        ctx.sink(rest, ctx.user);
    }
}

// ctx == nullptr: eine Antwort am Stueck. Sonst: streamen und dabei durchreichen.
inline Result ChatCore(const std::wstring& baseUrl, const std::wstring& bearer,
                       const std::wstring& model, const std::wstring& systemPrompt,
                       const std::wstring& userText, const std::wstring& temperature,
                       DWORD timeoutMs, StreamCtx* ctx) {
    if (Trim(model).empty()) {
        Result r;
        r.error = L"Kein Modell konfiguriert (z. B. llama3.2:3b).";
        return r;
    }

    const bool         stream   = (ctx != nullptr);
    const std::wstring endpoint = BuildEndpoint(baseUrl, L"/api/chat");
    const LineSink     sink     = stream ? StreamOnLine : nullptr;

    std::wstring payload =
        BuildChatPayload(Trim(model), systemPrompt, userText, temperature, true, stream);
    Result r = Request(endpoint, L"POST", bearer, ToUtf8(payload), timeoutMs, sink, ctx);
    if (stream && r.ok) StreamFlush(*ctx);

    // Denkende Modelle koennen das gesamte num_predict-Budget im Denkprozess
    // verbrauchen und gar keine Antwort mehr schreiben - "content" ist dann leer
    // und "done_reason" steht auf "length". Real beobachtet bei qwen3.5:4b, das
    // fuer diesen einen Satz ueber 1024 Token nachgedacht hat. In dem Fall genau
    // einmal ohne Deckel nachfragen: langsam, aber besser als nichts einzufuegen.
    // Im Stream ist die Bedingung sogar praeziser - ist nichts beim Aufrufer
    // angekommen, wurde auch nichts getippt, das Nachfragen ist also gefahrlos.
    const bool nothing = stream ? (ctx->chars == 0) : ContentIsEmpty(r.body);
    if (r.ok && nothing && HitTokenLimit(r.body)) {
        if (stream) ctx->filter.Reset();
        payload = BuildChatPayload(Trim(model), systemPrompt, userText, temperature, false, stream);
        r = Request(endpoint, L"POST", bearer, ToUtf8(payload), timeoutMs, sink, ctx);
        if (stream && r.ok) StreamFlush(*ctx);
    }
    return r;
}

// Fragt eine Antwort des Modells an.
inline Result Chat(const std::wstring& baseUrl, const std::wstring& bearer,
                   const std::wstring& model, const std::wstring& systemPrompt,
                   const std::wstring& userText, const std::wstring& temperature,
                   DWORD timeoutMs) {
    return ChatCore(baseUrl, bearer, model, systemPrompt, userText, temperature,
                    timeoutMs, nullptr);
}

// Dasselbe als Stream: der Aufrufer bekommt den Text haeppchenweise, waehrend das
// Modell ihn schreibt (Tippmodus). Bereits herausgegebener Text ist endgueltig -
// deshalb steckt die gesamte Aufraeumarbeit in TagStream statt in ExtractTagged.
// charsOut meldet, wie viel Text tatsaechlich beim Aufrufer angekommen ist.
inline Result ChatStream(const std::wstring& baseUrl, const std::wstring& bearer,
                         const std::wstring& model, const std::wstring& systemPrompt,
                         const std::wstring& userText, const std::wstring& temperature,
                         DWORD timeoutMs, TextSink sink, void* user, size_t* charsOut) {
    StreamCtx ctx;
    ctx.sink = sink;
    ctx.user = user;
    Result r = ChatCore(baseUrl, bearer, model, systemPrompt, userText, temperature,
                        timeoutMs, &ctx);
    if (charsOut) *charsOut = ctx.chars;
    return r;
}

// Laedt das Modell in den Speicher, ohne Text erzeugen zu lassen (Start-Warmup).
//
// POST /api/generate mit leerem "prompt" ist der dafuer vorgesehene Weg: Ollama
// laedt das Modell und antwortet sofort mit "done":true, ohne ein einziges Token.
// Bewusst nicht /api/chat - das braeuchte eine Nachricht und wuerde eine echte
// Antwort erzeugen, also Zeit kosten, die niemand liest.
//
// "keep_alive" wird nicht gesetzt: Ollamas Standard (bzw. ein per
// OLLAMA_KEEP_ALIVE konfigurierter Wert) soll gelten, sonst wuerde ChatNicer
// fremde Einstellungen ueberschreiben.
inline Result Warmup(const std::wstring& baseUrl, const std::wstring& bearer,
                     const std::wstring& model, DWORD timeoutMs) {
    Result r;
    if (Trim(model).empty()) {
        r.error = L"Kein Modell konfiguriert (z. B. llama3.2:3b).";
        return r;
    }
    // Ein fehlendes Modell meldet Ollama mit 404 und {"error":"model ... not found"};
    // Request() reicht das bereits als r.error durch.
    const std::wstring payload =
        L"{\"model\":\"" + JsonEscape(Trim(model)) + L"\",\"prompt\":\"\",\"stream\":false}";
    return Request(BuildEndpoint(baseUrl, L"/api/generate"), L"POST", bearer,
                   ToUtf8(payload), timeoutMs);
}

// Liest die installierten Modelle (GET /api/tags).
inline std::vector<std::wstring> FetchModels(const std::wstring& baseUrl,
                                             const std::wstring& bearer) {
    Result r = Request(BuildEndpoint(baseUrl, L"/api/tags"), L"GET", bearer, std::string(), 8000);
    if (!r.ok) return std::vector<std::wstring>();
    return JsonFindAllStrings(r.body, L"name");
}

} // namespace net
