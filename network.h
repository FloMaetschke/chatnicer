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
                                     bool capLength = true) {
    std::wstring j = L"{\"model\":\"";
    j += JsonEscape(model);
    j += L"\",\"stream\":false,\"options\":";
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

// Schneidet den Inhalt von <rewritten_text> heraus.
//
// Bewusst tolerant: kleine Modelle verhaspeln sich beim Tag regelmaessig und
// schreiben "<rewritten_text" ohne ">", lassen das schliessende Tag weg oder
// kleben den Text direkt an den Tag-Namen. Alle diese Faelle sind im Test gegen
// llama3.2:3b aufgetreten und liefern hier trotzdem den richtigen Text.
// Fehlt das Tag ganz, bleibt die Antwort unveraendert.
inline std::wstring ExtractTagged(const std::wstring& in) {
    static const wchar_t  kOpen[]  = L"<rewritten_text";
    static const wchar_t  kClose[] = L"</rewritten_text";
    const size_t openLen = 15;   // Laenge von "<rewritten_text"

    const size_t open = in.find(kOpen);
    if (open == std::wstring::npos) {
        // Nur das schliessende Tag da? Dann steht die Antwort davor.
        const size_t lone = in.find(kClose);
        return Trim(lone == std::wstring::npos ? in : in.substr(0, lone));
    }

    size_t start = open + openLen;
    if (start < in.size() && in[start] == L'>') ++start;   // normaler Fall

    size_t end = in.find(kClose, start);
    if (end == std::wstring::npos) end = in.size();        // Modell hat nicht geschlossen

    return Trim(in.substr(start, end - start));
}

// Liefert message.content aus der Ollama-Antwort, bereinigt um Denkprozess
// und XML-Rahmen.
inline std::wstring ExtractChatAnswer(const std::wstring& body) {
    std::wstring val;
    // gezielt innerhalb von "message" suchen, damit z. B. "thinking" nicht stoert
    const size_t msg = body.find(L"\"message\"");
    if (msg != std::wstring::npos && JsonFindStringFrom(body, L"content", msg, val))
        return ExtractTagged(StripThinking(val));
    if (JsonFindString(body, L"content", val))
        return ExtractTagged(StripThinking(val));
    if (JsonFindString(body, L"response", val))   // /api/generate
        return ExtractTagged(StripThinking(val));
    return std::wstring();
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

// Ein HTTP-Request (POST mit JSON-Body oder GET, wenn utf8Body leer ist).
inline Result Request(const std::wstring& url,
                      const wchar_t*      method,
                      const std::wstring& bearer,
                      const std::string&  utf8Body,
                      DWORD               timeoutMs) {
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

    std::string raw;
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
        if (raw.size() > 32u * 1024u * 1024u) break; // Schutz gegen Endlos-Streams
    }

    // BOM einer UTF-8-Antwort entfernen
    if (raw.size() >= 3 && static_cast<BYTE>(raw[0]) == 0xEF &&
        static_cast<BYTE>(raw[1]) == 0xBB && static_cast<BYTE>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }
    r.body = FromUtf8(raw);

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

// Fragt eine Antwort des Modells an.
inline Result Chat(const std::wstring& baseUrl, const std::wstring& bearer,
                   const std::wstring& model, const std::wstring& systemPrompt,
                   const std::wstring& userText, const std::wstring& temperature,
                   DWORD timeoutMs) {
    if (Trim(model).empty()) {
        Result r;
        r.error = L"Kein Modell konfiguriert (z. B. llama3.2:3b).";
        return r;
    }
    const std::wstring endpoint = BuildEndpoint(baseUrl, L"/api/chat");
    const std::wstring payload =
        BuildChatPayload(Trim(model), systemPrompt, userText, temperature);
    Result r = Request(endpoint, L"POST", bearer, ToUtf8(payload), timeoutMs);

    // Denkende Modelle koennen das gesamte num_predict-Budget im Denkprozess
    // verbrauchen und gar keine Antwort mehr schreiben - "content" ist dann leer
    // und "done_reason" steht auf "length". Real beobachtet bei qwen3.5:4b, das
    // fuer diesen einen Satz ueber 1024 Token nachgedacht hat. In dem Fall genau
    // einmal ohne Deckel nachfragen: langsam, aber besser als nichts einzufuegen.
    if (r.ok && HitTokenLimit(r.body) && ContentIsEmpty(r.body)) {
        const std::wstring again =
            BuildChatPayload(Trim(model), systemPrompt, userText, temperature, false);
        r = Request(endpoint, L"POST", bearer, ToUtf8(again), timeoutMs);
    }
    return r;
}

// Liest die installierten Modelle (GET /api/tags).
inline std::vector<std::wstring> FetchModels(const std::wstring& baseUrl,
                                             const std::wstring& bearer) {
    Result r = Request(BuildEndpoint(baseUrl, L"/api/tags"), L"GET", bearer, std::string(), 8000);
    if (!r.ok) return std::vector<std::wstring>();
    return JsonFindAllStrings(r.body, L"name");
}

} // namespace net
