// =====================================================================================
//  chatread.h - den zuletzt sichtbaren Teil eines Chatverlaufs auslesen
//
//  ChatNicer tritt hier auf wie ein Screenreader: Teams und Discord sind beide
//  Chromium-Anwendungen und stellen ihren Renderer-Inhalt ueber MSAA bereit -
//  erreichbar ueber das Kindfenster der Klasse "Chrome_RenderWidgetHostHWND"
//  ("Chrome Legacy Window"). Damit reicht *eine* Codeschiene fuer beide
//  Programme; UI Automation (COM, uiautomationcore.dll) wird nicht gebraucht.
//
//  Bewusst wird nur ein schmaler Ausschnitt gelesen:
//
//    - nur das Vordergrundfenster, und nur wenn es Teams oder Discord gehoert,
//    - darin nur der *eine* geoeffnete Chat (ueber einen benannten Anker, siehe
//      FindAnchor), niemals die Chatliste in der Seitenleiste,
//    - und davon nur die letzten paar Nachrichten (maxMessages).
//
//  Das ist keine Sparsamkeit um der Sparsamkeit willen: Ein Werkzeug, das den
//  kompletten Verlauf aller offenen Unterhaltungen einsammelt, waere ein
//  Mitlesewerkzeug. Gelesen wird deshalb genau das, was der Nutzer in diesem
//  Moment ohnehin auf dem Bildschirm sieht - und nur, wenn er den Hotkey drueckt.
//
//  Findet sich der Anker nicht (Discord steht auf "Freunde", Teams zeigt die
//  Chatuebersicht), liefert ReadForeground() eine leere Unterhaltung. Der
//  Aufrufer macht dann nichts - so ist es gewollt.
// =====================================================================================
#pragma once

#include <windows.h>
#include <oleacc.h>
#include <string>
#include <vector>

#pragma comment(lib, "oleacc.lib")
#pragma comment(lib, "oleaut32.lib")   // SysFreeString (BSTR aus get_accName)
#pragma comment(lib, "ole32.lib")      // CoInitializeEx im Lese-Thread

namespace chat {

// IID_IAccessible von Hand, damit uuid.lib nicht mitgelinkt werden muss.
static const GUID kIidAccessible =
    { 0x618736e0, 0x3c3d, 0x11cf, { 0x81, 0x0c, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

enum App { APP_NONE = 0, APP_TEAMS, APP_DISCORD };

// Warum nichts gelesen wurde. Der Unterschied ist fuer die Bedienung wichtig:
// "kein Chatfenster im Vordergrund" und "kein Chat geoeffnet" sind normale
// Zustaende, bei denen der Hotkey stillschweigend nichts tut. NO_TREE dagegen
// heisst, dass die Anwendung ihren Accessibility-Baum gar nicht bereitstellt -
// bei Discord der Regelfall, solange es ohne --force-renderer-accessibility
// laeuft. Ohne diese Unterscheidung stuende der Nutzer vor einem Hotkey, der
// scheinbar grundlos nichts tut.
enum Status { ST_OK = 0, ST_NO_APP, ST_NO_TREE, ST_NO_CHAT };

struct Message {
    std::wstring sender;   // bei Discord gefuellt, bei Teams leer (siehe SplitDiscord)
    std::wstring text;     // roher Text der Nachricht
};

struct Conversation {
    App                  app    = APP_NONE;
    Status               status = ST_NO_APP;
    std::wstring         title;      // Name des Chats, soweit ermittelbar
    std::vector<Message> messages;   // aeltester zuerst
};

// -------------------------------------------------------------------------------------
// Kleine COM-Helfer. Kein try/catch - der Release baut ohne Exceptions.
// -------------------------------------------------------------------------------------

// accName eines Elements. Leerer String, wenn es keinen hat.
inline std::wstring NameOf(IAccessible* acc) {
    if (!acc) return std::wstring();
    VARIANT self;
    self.vt   = VT_I4;
    self.lVal = CHILDID_SELF;
    BSTR bs = nullptr;
    if (FAILED(acc->get_accName(self, &bs)) || !bs) {
        if (bs) SysFreeString(bs);
        return std::wstring();
    }
    std::wstring out(bs, SysStringLen(bs));
    SysFreeString(bs);
    return out;
}

inline long RoleOf(IAccessible* acc) {
    if (!acc) return 0;
    VARIANT self;
    self.vt   = VT_I4;
    self.lVal = CHILDID_SELF;
    VARIANT role;
    VariantInit(&role);
    long r = 0;
    if (SUCCEEDED(acc->get_accRole(self, &role)) && role.vt == VT_I4) r = role.lVal;
    VariantClear(&role);
    return r;
}

inline long ChildCountOf(IAccessible* acc) {
    long n = 0;
    if (!acc || FAILED(acc->get_accChildCount(&n))) return 0;
    return n < 0 ? 0 : n;
}

// Direkte Kinder als IAccessible. Kinder ohne eigenes Objekt (VT_I4 - reine
// "child ids" wie einzelne Textfragmente) werden uebersprungen; alles, was
// ChatNicer braucht, ist ein vollwertiges Element.
//
// Der Aufrufer gibt die Zeiger per Release() frei.
inline void ChildrenOf(IAccessible* acc, std::vector<IAccessible*>& out) {
    out.clear();
    const long count = ChildCountOf(acc);
    if (count <= 0) return;

    std::vector<VARIANT> raw(static_cast<size_t>(count));
    for (VARIANT& v : raw) VariantInit(&v);

    long got = 0;
    if (SUCCEEDED(AccessibleChildren(acc, 0, count, &raw[0], &got))) {
        for (long i = 0; i < got; ++i) {
            if (raw[static_cast<size_t>(i)].vt == VT_DISPATCH && raw[static_cast<size_t>(i)].pdispVal) {
                IAccessible* child = nullptr;
                if (SUCCEEDED(raw[static_cast<size_t>(i)].pdispVal->QueryInterface(
                        kIidAccessible, reinterpret_cast<void**>(&child))) && child) {
                    out.push_back(child);
                }
            }
        }
    }
    for (VARIANT& v : raw) VariantClear(&v);
}

inline void ReleaseAll(std::vector<IAccessible*>& v) {
    for (IAccessible* p : v) if (p) p->Release();
    v.clear();
}

// -------------------------------------------------------------------------------------
// Fenster finden
// -------------------------------------------------------------------------------------

// Der Renderer haengt an einem Kindfenster der Klasse "Chrome_RenderWidgetHostHWND".
// Es existiert nur, wenn die Anwendung ihren Accessibility-Baum aufgebaut hat -
// bei Discord ist das der Knackpunkt (siehe README: --force-renderer-accessibility).
struct FindCtx { HWND found; };

inline BOOL CALLBACK FindLegacyProc(HWND hwnd, LPARAM param) {
    FindCtx* ctx = reinterpret_cast<FindCtx*>(param);
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    if (lstrcmpW(cls, L"Chrome_RenderWidgetHostHWND") == 0) {
        ctx->found = hwnd;
        return FALSE;                      // erstes Treffer reicht
    }
    EnumChildWindows(hwnd, FindLegacyProc, param);
    return ctx->found == nullptr;
}

// Chromium baut den Accessibility-Baum erst, wenn sich ein Client dafuer meldet.
// Ein WM_GETOBJECT auf den Fensterbaum ist genau dieses Signal - bei Teams
// genuegt es, bei Discord nicht (dort muss die App mit dem passenden Schalter
// gestartet worden sein).
inline BOOL CALLBACK PokeProc(HWND hwnd, LPARAM) {
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    if (lstrcmpW(cls, L"Chrome_WidgetWin_1") == 0 ||
        lstrcmpW(cls, L"Chrome_RenderWidgetHostHWND") == 0) {
        DWORD_PTR res = 0;
        SendMessageTimeoutW(hwnd, WM_GETOBJECT, 0, OBJID_CLIENT,
                            SMTO_ABORTIFHUNG, 800, &res);
    }
    EnumChildWindows(hwnd, PokeProc, 0);
    return TRUE;
}

inline HWND FindLegacyWindow(HWND top) {
    FindCtx ctx = { nullptr };
    FindLegacyProc(top, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.found) return ctx.found;

    // Noch kein Baum -> Chromium anstupsen und ein zweites Mal schauen.
    PokeProc(top, 0);
    Sleep(120);
    ctx.found = nullptr;
    FindLegacyProc(top, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

// Welche Anwendung liegt im Vordergrund? Ueber den Prozessnamen, nicht ueber den
// Fenstertitel - der ist lokalisiert und wechselt mit dem geoeffneten Chat.
inline App AppOfWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return APP_NONE;

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return APP_NONE;

    wchar_t path[MAX_PATH] = {};
    DWORD   len = ARRAYSIZE(path);
    const BOOL ok = QueryFullProcessImageNameW(proc, 0, path, &len);
    CloseHandle(proc);
    if (!ok) return APP_NONE;

    std::wstring exe(path);
    const size_t slash = exe.find_last_of(L'\\');
    if (slash != std::wstring::npos) exe.erase(0, slash + 1);
    CharLowerBuffW(&exe[0], static_cast<DWORD>(exe.size()));

    if (exe == L"ms-teams.exe" || exe == L"teams.exe") return APP_TEAMS;
    if (exe == L"discord.exe"  || exe == L"discordptb.exe" ||
        exe == L"discordcanary.exe")                      return APP_DISCORD;
    return APP_NONE;
}

// -------------------------------------------------------------------------------------
// Den Anker des offenen Chats finden
//
//  Teams:   eine Gruppe namens "Nachrichtenliste" / "Message list".
//  Discord: eine Liste namens "Nachrichten in <Chat>" / "Messages in <Chat>".
//
//  Beide Namen kommen aus dem aria-label der Anwendung und sind damit
//  lokalisiert - deshalb je zwei Schreibweisen. Findet sich der Anker nicht,
//  ist kein Chat geoeffnet, und das ist ein gueltiges Ergebnis.
// -------------------------------------------------------------------------------------
inline bool StartsWith(const std::wstring& s, const wchar_t* prefix) {
    const size_t n = lstrlenW(prefix);
    if (s.size() < n) return false;
    return CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, s.c_str(),
                          static_cast<int>(n), prefix, static_cast<int>(n)) == CSTR_EQUAL;
}

inline bool IsAnchorName(App app, const std::wstring& name, std::wstring& titleOut) {
    if (app == APP_TEAMS) {
        if (name == L"Nachrichtenliste" || name == L"Message list" ||
            name == L"Nachrichten"      || name == L"Messages") {
            return true;
        }
        return false;
    }
    // Discord fuehrt den Chatnamen gleich mit: "Nachrichten in Hefti"
    const wchar_t* forms[2] = { L"Nachrichten in ", L"Messages in " };
    for (const wchar_t* f : forms) {
        if (StartsWith(name, f)) {
            titleOut = name.substr(lstrlenW(f));
            return true;
        }
    }
    return false;
}

// Suchzustand. "budget" begrenzt, wie viele Elemente insgesamt angefasst werden -
// ein Chatfenster hat schnell einige tausend, und jeder Zugriff ist ein
// Cross-Process-Aufruf.
struct Scan {
    App          app;
    int          budget;
    IAccessible* anchor;
    std::wstring title;
};

inline void FindAnchor(IAccessible* acc, int depth, Scan& sc) {
    if (!acc || sc.anchor || sc.budget <= 0 || depth > 40) return;
    --sc.budget;

    std::wstring title;
    if (IsAnchorName(sc.app, NameOf(acc), title)) {
        acc->AddRef();
        sc.anchor = acc;
        sc.title  = title;
        return;
    }

    std::vector<IAccessible*> kids;
    ChildrenOf(acc, kids);
    for (IAccessible* k : kids) {
        if (!sc.anchor) FindAnchor(k, depth + 1, sc);
    }
    ReleaseAll(kids);
}

// Unter dem Anker haengt der Container, dessen direkte Kinder die einzelnen
// Nachrichten sind. Bei Teams sitzt er einige Ebenen tiefer (der Anker selbst
// hat nur ein Kind), bei Discord ist der Anker bereits die Liste. Gesucht wird
// deshalb der Nachfahre mit den *meisten* texttragenden direkten Kindern.
struct Best { IAccessible* node; int score; };

inline void FindMessageBox(IAccessible* acc, int depth, int& budget, Best& best) {
    if (!acc || budget <= 0 || depth > 12) return;
    --budget;

    std::vector<IAccessible*> kids;
    ChildrenOf(acc, kids);

    int textKids = 0;
    for (IAccessible* k : kids)
        if (NameOf(k).size() >= 12) ++textKids;

    if (textKids > best.score) {
        if (best.node) best.node->Release();
        acc->AddRef();
        best.node  = acc;
        best.score = textKids;
    }

    for (IAccessible* k : kids) FindMessageBox(k, depth + 1, budget, best);
    ReleaseAll(kids);
}

// -------------------------------------------------------------------------------------
// Nachrichten aufbereiten
// -------------------------------------------------------------------------------------

// Discord legt Absender, Text und Zeit in *einen* Namen, getrennt durch " , ":
//     "RockyMullet , It's actually Unreal , 22.06.2026 12:56"
// Das erste Feld ist der Absender, das letzte die Uhrzeit, alles dazwischen der
// Text (der selbst Kommata enthalten darf).
//
// Teams haengt den Absender *hinter* den Text und trennt ihn nicht erkennbar ab.
// Dort bleibt sender leer und der volle Name steht als Text - das Modell bekommt
// den Verlauf ohnehin am Stueck und kommt damit zurecht. Lieber ein ehrlich
// ungetrennter Text als ein Absender, der bei jedem zweiten Namen falsch raet.
inline void SplitDiscord(const std::wstring& raw, Message& msg) {
    const std::wstring sep = L" , ";
    const size_t first = raw.find(sep);
    if (first == std::wstring::npos) { msg.text = raw; return; }

    msg.sender = raw.substr(0, first);
    const size_t bodyAt = first + sep.size();
    const size_t last   = raw.rfind(sep);
    msg.text = (last != std::wstring::npos && last > first)
                 ? raw.substr(bodyAt, last - bodyAt)
                 : raw.substr(bodyAt);
}

inline std::wstring Squeeze(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    bool space = false;
    for (wchar_t c : in) {
        const bool ws = (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n');
        if (ws) { space = true; continue; }
        if (space && !out.empty()) out += L' ';
        space = false;
        out += c;
    }
    return out;
}

// -------------------------------------------------------------------------------------
// Einstieg: den offenen Chat des Vordergrundfensters lesen
// -------------------------------------------------------------------------------------
inline Conversation ReadForeground(size_t maxMessages) {
    Conversation conv;

    HWND fg = GetForegroundWindow();
    if (!fg) return conv;

    const App app = AppOfWindow(fg);
    if (app == APP_NONE) return conv;      // kein Chatprogramm -> nichts tun
    conv.app    = app;
    conv.status = ST_NO_TREE;

    HWND legacy = FindLegacyWindow(fg);
    if (!legacy) return conv;              // kein Accessibility-Baum vorhanden
    conv.status = ST_NO_CHAT;

    // Der Aufrufer ist ein eigener Thread; COM gehoert dort initialisiert.
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // OBJID_CLIENT ist als LONG deklariert, die API nimmt DWORD - ohne den Cast
    // meldet /W4 einen signed/unsigned-Konflikt.
    IAccessible* root = nullptr;
    if (SUCCEEDED(AccessibleObjectFromWindow(legacy, static_cast<DWORD>(OBJID_CLIENT),
                                             kIidAccessible,
                                             reinterpret_cast<void**>(&root))) && root) {
        Scan sc;
        sc.app    = app;
        sc.budget = 6000;
        sc.anchor = nullptr;
        FindAnchor(root, 0, sc);

        if (sc.anchor) {
            conv.title = sc.title;

            int  budget = 400;
            Best best   = { nullptr, 0 };
            FindMessageBox(sc.anchor, 0, budget, best);

            IAccessible* box = best.node ? best.node : sc.anchor;
            std::vector<IAccessible*> items;
            ChildrenOf(box, items);

            // Nur den Schwanz der Liste lesen. Alles davor ist aelterer Verlauf,
            // den niemand angefordert hat - und jeder weitere Eintrag kostet
            // mehrere Cross-Process-Aufrufe.
            const size_t total = items.size();
            const size_t from  = (total > maxMessages) ? total - maxMessages : 0;
            for (size_t i = from; i < total; ++i) {
                const std::wstring raw = Squeeze(NameOf(items[i]));
                if (raw.size() < 2) continue;

                Message m;
                if (app == APP_DISCORD) SplitDiscord(raw, m);
                else                    m.text = raw;
                if (!m.text.empty()) conv.messages.push_back(m);
            }
            ReleaseAll(items);

            if (best.node) best.node->Release();
            sc.anchor->Release();
        }
        root->Release();
    }

    if (SUCCEEDED(hrInit)) CoUninitialize();

    if (!conv.messages.empty()) conv.status = ST_OK;

    // Ohne erkennbaren Chatnamen den Fenstertitel nehmen (Teams).
    //
    // Teams betitelt sein Fenster als
    //     "Chat | <Gespraechspartner> | <Organisation> | <Konto> | Microsoft Teams".
    // Das erste Segment ist nur der Bereich ("Chat", "Aktivität"); gemeint ist
    // das zweite. Fehlt es, bleibt der Bereichsname als Notnagel stehen.
    if (conv.title.empty()) {
        wchar_t caption[256] = {};
        GetWindowTextW(fg, caption, ARRAYSIZE(caption));
        std::wstring t(caption);

        const size_t first = t.find(L" | ");
        if (first != std::wstring::npos) {
            const size_t start  = first + 3;
            const size_t second = t.find(L" | ", start);
            std::wstring partner = t.substr(
                start, second == std::wstring::npos ? std::wstring::npos : second - start);
            t = partner.empty() ? t.substr(0, first) : partner;
        }
        conv.title = t;
    }
    return conv;
}

// Den Verlauf als Text fuer das Modell aufbereiten.
inline std::wstring ToTranscript(const Conversation& conv) {
    std::wstring out;
    for (const Message& m : conv.messages) {
        if (!m.sender.empty()) { out += m.sender; out += L": "; }
        out += m.text;
        out += L"\n";
    }
    return out;
}

} // namespace chat
