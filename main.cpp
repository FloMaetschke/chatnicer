// =====================================================================================
//  ChatNicer - leichtgewichtiges Win32-Tray-Tool fuer Ollama
//
//  Markierten Text per Hotkey (Standard: STRG+SHIFT+SPACE) kopieren, an ein lokales
//  Ollama-Modell schicken und die Antwort an der Cursorposition wieder einfuegen.
//  Wahlweise wird die Antwort stattdessen live getippt, waehrend das Modell sie
//  schreibt (Einstellung "Antwort live tippen", siehe TypeText/net::ChatStream).
//
//  Konfiguriert werden nur Ollama-URL (Standard http://localhost:11434) und der
//  Modellname in Ollama-Schreibweise (Standard qwen3:4b-instruct).
//
//  Ein zweiter Hotkey (Standard: STRG+ALT+SPACE) liest den offenen Chat des
//  Vordergrundfensters - Teams oder Discord, ueber die Accessibility-Schnittstelle
//  wie ein Screenreader (chatread.h) - und bietet drei Antworten als Sprechblasen
//  ueber dem Eingabefeld an. Ein Klick fuegt die gewaehlte Antwort dort ein.
//
//  Beim Start laedt ChatNicer das Modell vorab in den Ollama-Speicher (Warmup,
//  siehe WarmupProc/net::Warmup) und meldet das Ergebnis ueber das Tray-Icon.
//  Der Autostart mit Windows haengt an HKCU\...\Run (cfg::SetAutostart), nicht an
//  der config.ini - Windows liest die Registry, also ist sie die Wahrheit.
//
//  Keine externen Abhaengigkeiten: nur Win32, WinHTTP und die Accessibility-API
//  (oleacc) - alles Teil von Windows.
//
// -------------------------------------------------------------------------------------
//  BUILD (x64 Native Tools Command Prompt for VS 2022) - oder einfach: build.bat
//
//  Release, 151.040 Bytes (gemessen, VS 2022 17.x / Windows SDK 10.0.26100):
//
//    cl /nologo /std:c++17 /permissive- /W4 /MT /utf-8 /EHs-c- /D_HAS_EXCEPTIONS=0 ^
//       /O1 /Os /Oi /Oy /Gy /Gw /GL /GR- /GS- /Zc:inline /Zc:threadSafeInit- ^
//       /DNDEBUG /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 ^
//       main.cpp ^
//       /link /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /SUBSYSTEM:WINDOWS /RELEASE ^
//       /MANIFEST:EMBED /NODEFAULTLIB:libucrt.lib /DEFAULTLIB:ucrt.lib ^
//       /OUT:ChatNicer.exe ^
//       user32.lib gdi32.lib shell32.lib comctl32.lib winhttp.lib advapi32.lib ^
//       oleacc.lib oleaut32.lib ole32.lib
//
//  Bedeutung der wichtigsten Flags:
//    /O1 /Os              Optimierung auf Codegroesse statt Geschwindigkeit
//    /GL + /LTCG          Whole-Program-Optimization ueber alle Uebersetzungseinheiten
//    /Gy /Gw              Funktionen/Daten einzeln linkbar -> /OPT:REF wirft Ungenutztes raus
//    /GR-                 kein RTTI (wird hier nicht gebraucht)
//    /GS-                 keine Stack-Security-Cookies (spart die __security_*-Runtime)
//    /Zc:threadSafeInit-  keine Thread-Safe-Statics-Helfer der CRT
//    /EHs-c- + _HAS_EXCEPTIONS=0   keine Exception-Maschinerie (spart ~16 KB)
//    /OPT:REF /OPT:ICF    toten Code entfernen, identische Funktionen zusammenlegen
//    /utf-8               Quelldateien sind UTF-8 (Umlaute im Standard-System-Prompt)
//    /NODEFAULTLIB:libucrt.lib + ucrt.lib
//                         vcruntime statisch, UCRT dynamisch. ucrtbase.dll gehoert ab
//                         Windows 10 zum Betriebssystem -> KEIN VC++-Redist erforderlich.
//
//  Groessenvergleich derselben Quelle (gemessen):
//    komplett statische CRT, mit Exceptions (/MT /EHsc) ... 249.856 B - maximal robust
//    obige Release-Konfiguration .......................... 151.040 B - Standard
//
//  Die compat-Variante liegt damit deutlich ueber dem 200-KB-Budget; der
//  Standard-Release hat rund 52 KB Reserve (siehe CLAUDE.md).
//
//  Hinweis zu _HAS_EXCEPTIONS=0: bei Speichermangel bricht die STL hart ab, statt
//  std::bad_alloc zu werfen. Fuer ein Tool dieser Groesse akzeptabel - wer das nicht
//  moechte, baut mit "build.bat compat" (statische CRT inkl. Exceptions).
// =====================================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <math.h>
#include <string>

#include "config.h"
#include "network.h"
#include "chatread.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")   // Registry: Autostart-Eintrag (cfg::SetAutostart)

// Visual Styles ohne separate .rc-/.manifest-Datei
#pragma comment(linker, "/manifestdependency:\"type='win32' "                          \
                        "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "  \
                        "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
                        "language='*'\"")

// =====================================================================================
//  Konstanten
// =====================================================================================
static const wchar_t* kAppName    = L"ChatNicer";
static const wchar_t* kVersion    = L"1.1";
static const wchar_t* kWndClass   = L"ChatNicerHiddenWnd";
static const wchar_t* kCfgClass   = L"ChatNicerSettingsWnd";
static const wchar_t* kReplyClass = L"ChatNicerReplyWnd";
static const wchar_t* kMutexName  = L"Local\\ChatNicer_SingleInstance";

#define WM_TRAYICON     (WM_APP + 1)   // Tray-Callback
#define WM_APP_STATE    (WM_APP + 2)   // wParam = TrayState
#define WM_APP_DONE     (WM_APP + 3)   // wParam = ok?, lParam = std::wstring* (Meldung)
#define WM_APP_TESTDONE (WM_APP + 4)   // lParam = std::wstring* (Testergebnis)
#define WM_APP_MODELS   (WM_APP + 5)   // lParam = std::vector<std::wstring>* (Modelliste)
#define WM_APP_WARMUP   (WM_APP + 6)   // wParam = ok?, lParam = std::wstring* (Meldung)
#define WM_APP_REPLIES  (WM_APP + 7)   // lParam = ReplyResult* (Antwortvorschlaege)

enum { IDM_SETTINGS = 40001, IDM_ABOUT, IDM_EXIT };
enum { IDC_URL = 1001, IDC_MODEL, IDC_PROMPT, IDC_KEY, IDC_HOTKEY,
       IDC_REPLYKEY, IDC_REPLYON, IDC_REPLYPROMPT, IDC_REPLYCTX,
       IDC_TYPING, IDC_RESTORE, IDC_WARMMSG, IDC_AUTOSTART,
       IDC_TAB, IDC_TEST, IDC_SAVE, IDC_CANCEL };

// Registerkarten des Einstellungsfensters
enum { PAGE_CONNECT = 0, PAGE_REWRITE, PAGE_REPLY, PAGE_COUNT };
enum { HOTKEY_ID = 1, HOTKEY_REPLY = 2 };

enum TrayState { STATE_IDLE = 0, STATE_BUSY = 1, STATE_ERROR = 2 };

// =====================================================================================
//  Globale Zustaende
// =====================================================================================
static HINSTANCE       g_inst      = nullptr;
static HWND            g_hwnd      = nullptr;   // unsichtbares Hauptfenster
static HWND            g_settings  = nullptr;   // Einstellungsfenster (oder nullptr)
static HFONT           g_font      = nullptr;
static HICON           g_icons[3]  = {};
static NOTIFYICONDATAW g_nid       = {};
static cfg::Config     g_cfg;
static volatile LONG   g_busy      = 0;
static UINT            g_msgTaskbarCreated = 0;
static bool            g_hotkeyOk  = false;
static bool            g_replyKeyOk = false;
static volatile LONG   g_replyBusy = 0;
// Die Bedienelemente jeder Registerkarte. Sie sind Kinder des Fensters, nicht des
// Tab-Controls (das kann keine haben) - umgeschaltet wird ueber Sichtbarkeit.
// Unsichtbare Controls ueberspringt IsDialogMessage von selbst, damit stimmt auch
// die Tab-Reihenfolge ohne weiteres Zutun.
//
// Zu jedem Element wird seine Ausgangsgeometrie mitgeschrieben, denn das Fenster
// ist groessenveraenderlich: LayoutSettings() rechnet daraus die neue Lage. Die
// Flags sagen, was mit dem gewonnenen Platz geschehen soll.
enum {
    LF_W = 1,   // Breite waechst mit
    LF_H = 2,   // Hoehe waechst mit (die beiden Prompt-Felder)
    LF_Y = 4,   // rutscht nach unten, wenn ein Feld darueber gewachsen ist
    LF_X = 8    // bleibt am rechten Rand (Schaltflaechen)
};

struct Slot { HWND hwnd; int x, y, w, h; unsigned flags; };

// [PAGE_COUNT] sammelt die Elemente, die auf jeder Karte sichtbar bleiben.
static std::vector<Slot> g_slots[PAGE_COUNT + 1];
static int g_baseCX = 0, g_baseCY = 0;   // Client-Groesse, fuer die das Layout gilt
static int g_minCX  = 0, g_minCY = 0;    // Mindestgroesse des ganzen Fensters

// =====================================================================================
//  Tray-Icons zur Laufzeit erzeugen (keine .rc-Datei noetig)
//  Gezeichnet wird eine gefuellte Sprechblase mit drei Punkten, per Alpha
//  kantengeglaettet - Farbe signalisiert den Zustand.
// =====================================================================================
static HICON MakeIcon(COLORREF rgb) {
    const int S = 32;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = S;
    bi.bmiHeader.biHeight      = -S;          // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color || !bits) { if (color) DeleteObject(color); return nullptr; }

    const float baseR = static_cast<float>(GetRValue(rgb));
    const float baseG = static_cast<float>(GetGValue(rgb));
    const float baseB = static_cast<float>(GetBValue(rgb));
    const float cx = 16.0f, cy = 16.0f, radius = 15.0f;
    const float dotX[3] = { 9.5f, 16.0f, 22.5f };

    DWORD* px = static_cast<DWORD*>(bits);
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            const float fx = x + 0.5f, fy = y + 0.5f;
            float d = sqrtf((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy));

            float a = radius - d + 0.5f;              // Deckung am Kreisrand
            if (a <= 0.0f) { px[y * S + x] = 0; continue; }
            if (a > 1.0f) a = 1.0f;

            float r = baseR, g = baseG, b = baseB;
            for (int i = 0; i < 3; ++i) {
                float dd = sqrtf((fx - dotX[i]) * (fx - dotX[i]) + (fy - 17.0f) * (fy - 17.0f));
                float w = 2.4f - dd + 0.5f;           // weisser Punkt
                if (w <= 0.0f) continue;
                if (w > 1.0f) w = 1.0f;
                r += (255.0f - r) * w;
                g += (255.0f - g) * w;
                b += (255.0f - b) * w;
            }

            const BYTE A  = static_cast<BYTE>(a * 255.0f + 0.5f);
            const BYTE PR = static_cast<BYTE>(r * a + 0.5f);   // premultiplied
            const BYTE PG = static_cast<BYTE>(g * a + 0.5f);
            const BYTE PB = static_cast<BYTE>(b * a + 0.5f);
            px[y * S + x] = (static_cast<DWORD>(A)  << 24) | (static_cast<DWORD>(PR) << 16) |
                            (static_cast<DWORD>(PG) <<  8) |  static_cast<DWORD>(PB);
        }
    }

    BYTE maskBits[(S / 8) * S] = {};                  // 32bpp+Alpha -> Maske irrelevant
    HBITMAP mask = CreateBitmap(S, S, 1, 1, maskBits);

    ICONINFO ii = {};
    ii.fIcon    = TRUE;
    ii.hbmMask  = mask;
    ii.hbmColor = color;
    HICON icon  = CreateIconIndirect(&ii);

    DeleteObject(color);
    if (mask) DeleteObject(mask);
    return icon;
}

// =====================================================================================
//  Tray-Icon
// =====================================================================================
static void TrayAdd() {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = g_icons[STATE_IDLE];
    lstrcpynW(g_nid.szTip, kAppName, ARRAYSIZE(g_nid.szTip));

    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

static void TraySetState(TrayState state, const std::wstring& tip) {
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    g_nid.hIcon  = g_icons[state];
    lstrcpynW(g_nid.szTip, tip.c_str(), ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void TrayBalloon(const std::wstring& title, const std::wstring& text, bool error) {
    g_nid.uFlags     = NIF_INFO;
    g_nid.dwInfoFlags = error ? NIIF_ERROR : NIIF_INFO;
    lstrcpynW(g_nid.szInfoTitle, title.c_str(), ARRAYSIZE(g_nid.szInfoTitle));
    lstrcpynW(g_nid.szInfo,      text.c_str(),  ARRAYSIZE(g_nid.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static std::wstring IdleTip() {
    return std::wstring(kAppName) + L"  -  " + g_cfg.model + L"  -  " +
           cfg::HotkeyToString(g_cfg.hotkeyMods, g_cfg.hotkeyVk);
}

// =====================================================================================
//  Zwischenablage
// =====================================================================================
static bool ClipOpen() {
    for (int i = 0; i < 15; ++i) {                    // Clipboard kann kurz belegt sein
        if (OpenClipboard(nullptr)) return true;
        Sleep(40);
    }
    return false;
}

static bool ClipGetText(std::wstring& out) {
    if (!ClipOpen()) return false;
    bool ok = false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = p;
            GlobalUnlock(h);
            ok = true;
        }
    }
    CloseClipboard();
    return ok;
}

static bool ClipSetText(const std::wstring& text) {
    if (!ClipOpen()) return false;
    bool ok = false;
    if (EmptyClipboard()) {
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (void* dst = GlobalLock(mem)) {
                memcpy(dst, text.c_str(), bytes);
                GlobalUnlock(mem);
                if (SetClipboardData(CF_UNICODETEXT, mem)) ok = true;
                else GlobalFree(mem);
            } else {
                GlobalFree(mem);
            }
        }
    }
    CloseClipboard();
    return ok;
}

static bool ClipClear() {
    if (!ClipOpen()) return false;
    bool ok = EmptyClipboard() != FALSE;
    CloseClipboard();
    return ok;
}

// =====================================================================================
//  Tastatur-Simulation
// =====================================================================================
static void SendKeyRaw(WORD vk, bool up) {
    INPUT in = {};
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = vk;
    in.ki.wScan   = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof(INPUT));
}

static bool SendCtrlCombo(WORD key) {
    INPUT in[4] = {};
    for (int i = 0; i < 4; ++i) in[i].type = INPUT_KEYBOARD;

    in[0].ki.wVk = VK_CONTROL;
    in[1].ki.wVk = key;
    in[2].ki.wVk = key;        in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    for (int i = 0; i < 4; ++i)
        in[i].ki.wScan = static_cast<WORD>(MapVirtualKeyW(in[i].ki.wVk, MAPVK_VK_TO_VSC));

    return SendInput(4, in, sizeof(INPUT)) == 4;
}

// Text als Tastatureingabe schicken (Tippmodus).
//
// Zeichen gehen als KEYEVENTF_UNICODE raus - damit sind Umlaute und Emojis
// unabhaengig vom Tastaturlayout des Zielfensters richtig. Surrogatpaare
// erledigen sich von selbst, weil beide Haelften nacheinander gesendet werden.
// Zeilenumbrueche dagegen als echtes ENTER: ein Unicode-0x0A ignorieren die
// meisten Zielprogramme. In einem Chat-Eingabefeld schickt ENTER die Nachricht
// ab - deshalb steht der Hinweis auch an der Checkbox im Dialog.
static bool TypeText(const std::wstring& text) {
    INPUT batch[64];
    UINT  n  = 0;
    bool  ok = true;

    for (size_t i = 0; i <= text.size(); ++i) {
        // Batch voll oder Text zu Ende -> abschicken
        if (n > ARRAYSIZE(batch) - 2 || i == text.size()) {
            if (n && SendInput(n, batch, sizeof(INPUT)) != n) ok = false;
            n = 0;
            if (i == text.size()) break;
        }

        const wchar_t c = text[i];
        if (c == L'\r') continue;                 // ToCrLf-Paare nicht doppelt tippen

        INPUT down = {};
        down.type = INPUT_KEYBOARD;
        if (c == L'\n') {
            down.ki.wVk   = VK_RETURN;
            down.ki.wScan = static_cast<WORD>(MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC));
        } else {
            down.ki.wScan   = static_cast<WORD>(c);
            down.ki.dwFlags = KEYEVENTF_UNICODE;
        }
        INPUT up = down;
        up.ki.dwFlags |= KEYEVENTF_KEYUP;

        batch[n++] = down;
        batch[n++] = up;
    }
    return ok;
}

// Der Nutzer haelt beim Ausloesen des Hotkeys noch STRG+SHIFT gedrueckt. Wuerde man
// sofort STRG+C senden, kaeme beim Zielprogramm STRG+SHIFT+C an.
static void WaitForModifiersReleased(DWORD maxMs) {
    const DWORD start = GetTickCount();
    for (;;) {
        const bool down =
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_SHIFT) & 0x8000) ||
            (GetAsyncKeyState(VK_MENU)    & 0x8000) || (GetAsyncKeyState(VK_LWIN)  & 0x8000) ||
            (GetAsyncKeyState(VK_RWIN)    & 0x8000);
        if (!down) return;
        if (GetTickCount() - start > maxMs) break;
        Sleep(20);
    }
    // Notbremse: Modifier synthetisch loslassen
    SendKeyRaw(VK_SHIFT, true);
    SendKeyRaw(VK_MENU,  true);
    SendKeyRaw(VK_LWIN,  true);
    SendKeyRaw(VK_RWIN,  true);
    SendKeyRaw(VK_CONTROL, true);
    Sleep(30);
}

// =====================================================================================
//  Text-Helfer
// =====================================================================================
static std::wstring ToLf(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\r') { if (i + 1 < s.size() && s[i + 1] == L'\n') continue; out += L'\n'; }
        else out += s[i];
    }
    return out;
}

static std::wstring ToCrLf(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + s.size() / 16);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\n' && (i == 0 || s[i - 1] != L'\r')) out += L"\r\n";
        else out += s[i];
    }
    return out;
}

static void PostDone(bool ok, const std::wstring& msg) {
    PostMessageW(g_hwnd, WM_APP_DONE, ok ? 1 : 0,
                 reinterpret_cast<LPARAM>(new std::wstring(msg)));
}

// Ziel fuer net::ChatStream: jedes Haeppchen sofort tippen. "user" zeigt auf ein
// bool, das beim ersten blockierten SendInput auf false faellt.
static void TypeSink(const std::wstring& text, void* user) {
    if (!TypeText(text)) *static_cast<bool*>(user) = false;
}

// =====================================================================================
//  Arbeitsablauf (eigener Thread, damit der Hotkey/das UI nie blockiert)
//    Kopieren -> POST -> Antwort -> Einfuegen -> Zwischenablage wiederherstellen
// =====================================================================================
static DWORD WINAPI WorkerProc(LPVOID param) {
    cfg::Config* c = static_cast<cfg::Config*>(param);

    std::wstring backup;
    const bool haveBackup = ClipGetText(backup);
    bool needRestore = haveBackup;

    auto finish = [&](bool ok, const std::wstring& msg) -> DWORD {
        if (needRestore && haveBackup) ClipSetText(backup);
        delete c;
        InterlockedExchange(&g_busy, 0);
        PostDone(ok, msg);
        return 0;
    };

    // --- 1) Markierten Text holen --------------------------------------------------
    WaitForModifiersReleased(1200);

    // Bewusst leeren: so laesst sich "nichts markiert" sicher von "Text kopiert"
    // unterscheiden, statt versehentlich alten Clipboard-Inhalt zu verschicken.
    ClipClear();
    const DWORD seqBefore = GetClipboardSequenceNumber();

    if (!SendCtrlCombo('C'))
        return finish(false, L"Tastatureingabe blockiert. Laeuft das Zielfenster mit "
                             L"Administratorrechten? Dann ChatNicer ebenfalls als Admin starten.");

    const DWORD waitStart = GetTickCount();
    while (GetClipboardSequenceNumber() == seqBefore && GetTickCount() - waitStart < 1000)
        Sleep(25);
    Sleep(80);   // Zielprogramm Zeit geben, die Daten bereitzustellen

    std::wstring userText;
    if (!ClipGetText(userText) || net::Trim(userText).empty())
        return finish(false, L"Kein Text markiert (oder die Zwischenablage ist blockiert).");

    // --- 2a) Tippmodus: streamen und dabei schon schreiben ---------------------------
    //
    // Der erste Tastendruck ersetzt die noch bestehende Markierung, genau wie es
    // sonst das Einfuegen tut. Ein Fehler mitten im Stream laesst sich hier aber
    // nicht mehr zuruecknehmen - dann steht bereits Text im Zielfenster.
    if (c->typingInput) {
        bool   typeOk = true;
        size_t chars  = 0;
        net::Result sres = net::ChatStream(c->ollamaUrl, c->apiKey, c->model, c->systemPrompt,
                                           ToLf(userText), c->temperature, c->timeoutMs,
                                           TypeSink, &typeOk, &chars);

        needRestore = c->restoreClipboard && haveBackup;   // die Antwort war nie im Clipboard

        if (!sres.ok) {
            if (chars == 0) return finish(false, sres.error);
            return finish(false, L"Abgebrochen, getippter Text bleibt stehen: " + sres.error);
        }
        if (!typeOk)
            return finish(false, L"Tastatureingabe blockiert. Laeuft das Zielfenster mit "
                                 L"Administratorrechten? Dann ChatNicer ebenfalls als Admin starten.");
        if (chars == 0)
            return finish(false, L"Das Modell hat eine leere Antwort geliefert.");

        wchar_t typed[96];
        wsprintfW(typed, L"Fertig (%u Zeichen getippt).", static_cast<unsigned>(chars));
        return finish(true, typed);
    }

    // --- 2b) Ollama fragen ------------------------------------------------------------
    net::Result res = net::Chat(c->ollamaUrl, c->apiKey, c->model, c->systemPrompt,
                                ToLf(userText), c->temperature, c->timeoutMs);
    if (!res.ok)
        return finish(false, res.error);

    const std::wstring answer = net::ExtractChatAnswer(res.body);
    if (answer.empty())
        return finish(false, L"Das Modell hat eine leere Antwort geliefert.");

    // --- 3) Antwort einfuegen --------------------------------------------------------
    if (!ClipSetText(ToCrLf(answer)))
        return finish(false, L"Zwischenablage ist blockiert - Antwort konnte nicht eingefuegt werden.");

    Sleep(60);
    if (!SendCtrlCombo('V'))
        return finish(false, L"Einfuegen fehlgeschlagen (SendInput wurde blockiert).");

    Sleep(400);   // erst einfuegen lassen, dann die Zwischenablage anfassen

    if (c->restoreClipboard) {
        needRestore = haveBackup;
    } else {
        needRestore = false;      // Antwort bleibt in der Zwischenablage
    }

    wchar_t info[96];
    wsprintfW(info, L"Fertig (%u Zeichen eingefuegt).", static_cast<unsigned>(answer.size()));
    return finish(true, info);
}

static void StartWork() {
    if (g_cfg.ollamaUrl.empty() || g_cfg.model.empty()) {
        TrayBalloon(kAppName, L"Bitte zuerst unter \"Einstellungen\" Ollama-URL und Modell eintragen.", true);
        return;
    }
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) {
        TrayBalloon(kAppName, L"Eine Anfrage laeuft bereits.", false);
        return;
    }

    TraySetState(STATE_BUSY, std::wstring(kAppName) + L"  -  " + g_cfg.model + L" arbeitet ...");

    cfg::Config* snapshot = new cfg::Config(g_cfg);   // Kopie -> keine Races mit dem UI
    if (HANDLE th = CreateThread(nullptr, 0, WorkerProc, snapshot, 0, nullptr)) {
        CloseHandle(th);
    } else {
        delete snapshot;
        InterlockedExchange(&g_busy, 0);
        TraySetState(STATE_ERROR, std::wstring(kAppName) + L"  -  Fehler");
        TrayBalloon(kAppName, L"Thread konnte nicht gestartet werden.", true);
    }
}

// =====================================================================================
//  Start-Warmup: das Modell vorab laden lassen
//
//  Ollama holt ein Modell erst bei der ersten Anfrage in den Speicher; je nach
//  Groesse dauert das mehrere Sekunden, die sonst beim ersten Hotkey anfallen -
//  also genau dann, wenn jemand auf die Antwort wartet. Der Lauf ist bewusst
//  *nicht* ueber g_busy gesperrt: wer waehrenddessen den Hotkey drueckt, soll das
//  duerfen (Ollama reiht die Anfragen selbst auf). Deshalb fasst der Handler das
//  Icon nur an, wenn gerade keine echte Anfrage laeuft.
// =====================================================================================
static DWORD WINAPI WarmupProc(LPVOID param) {
    cfg::Config* c = static_cast<cfg::Config*>(param);

    const DWORD start = GetTickCount();
    net::Result res = net::Warmup(c->ollamaUrl, c->apiKey, c->model, c->timeoutMs);
    const DWORD secs = (GetTickCount() - start + 500) / 1000;

    wchar_t tail[64];
    wsprintfW(tail, L" ist geladen und bereit (%u s).", secs);
    std::wstring msg = res.ok ? c->model + tail
                              : L"Modell konnte nicht vorgeladen werden:\n" + res.error;

    delete c;
    PostMessageW(g_hwnd, WM_APP_WARMUP, res.ok ? 1 : 0,
                 reinterpret_cast<LPARAM>(new std::wstring(msg)));
    return 0;
}

static void StartWarmup() {
    if (net::Trim(g_cfg.ollamaUrl).empty() || net::Trim(g_cfg.model).empty()) return;

    TraySetState(STATE_BUSY,
                 std::wstring(kAppName) + L"  -  " + g_cfg.model + L" wird geladen ...");

    cfg::Config* snapshot = new cfg::Config(g_cfg);   // Kopie -> keine Races mit dem UI
    if (HANDLE th = CreateThread(nullptr, 0, WarmupProc, snapshot, 0, nullptr)) {
        CloseHandle(th);
    } else {
        delete snapshot;
        TraySetState(STATE_IDLE, IdleTip());
    }
}

// =====================================================================================
//  Verbindungstest aus dem Einstellungsdialog
// =====================================================================================
static DWORD WINAPI TestProc(LPVOID param) {
    cfg::Config* c = static_cast<cfg::Config*>(param);

    const DWORD start = GetTickCount();
    net::Result res = net::Chat(c->ollamaUrl, c->apiKey, c->model, c->systemPrompt,
                                L"hallo das hier ist ein test ob die verbindung geht",
                                c->temperature, c->timeoutMs);
    const DWORD secs = (GetTickCount() - start) / 1000;

    std::wstring msg;
    if (!res.ok) {
        msg = L"Fehlgeschlagen:\r\n\r\n" + res.error;
        if (res.status == 404)
            msg += L"\r\n\r\nTipp: Ist das Modell installiert? Pruefen mit \"ollama list\".";
        else if (res.status == 0)
            msg += L"\r\n\r\nTipp: Laeuft Ollama? Pruefen mit \"ollama serve\" bzw. im Browser "
                   L"unter " + net::Trim(c->ollamaUrl) + L".";
    } else {
        std::wstring answer = net::ExtractChatAnswer(res.body);
        if (answer.size() > 800) { answer.resize(800); answer += L" ..."; }
        wchar_t head[128];
        wsprintfW(head, L"Erfolgreich - Modell hat nach %u s geantwortet.\r\n\r\nAntwort:\r\n", secs);
        msg = head + (answer.empty() ? std::wstring(L"(leer)") : ToCrLf(answer));
    }

    delete c;
    PostMessageW(g_hwnd, WM_APP_TESTDONE, 0, reinterpret_cast<LPARAM>(new std::wstring(msg)));
    return 0;
}

// Installierte Modelle im Hintergrund holen und an das Hauptfenster melden.
// (Das Ergebnis geht bewusst an g_hwnd, nicht an den Dialog - der koennte
//  inzwischen geschlossen sein.)
static DWORD WINAPI ModelsProc(LPVOID param) {
    cfg::Config* c = static_cast<cfg::Config*>(param);
    std::vector<std::wstring>* list =
        new std::vector<std::wstring>(net::FetchModels(c->ollamaUrl, c->apiKey));
    delete c;
    if (!PostMessageW(g_hwnd, WM_APP_MODELS, 0, reinterpret_cast<LPARAM>(list)))
        delete list;
    return 0;
}

static void RequestModelList(const std::wstring& url, const std::wstring& key) {
    cfg::Config* c = new cfg::Config();
    c->ollamaUrl = url;
    c->apiKey    = key;
    if (HANDLE th = CreateThread(nullptr, 0, ModelsProc, c, 0, nullptr)) CloseHandle(th);
    else delete c;
}

// =====================================================================================
//  Gemeinsame UI-Helfer
// =====================================================================================
static UINT DpiFor(HWND hwnd) {
    typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
    static PFN_GetDpiForWindow fn = reinterpret_cast<PFN_GetDpiForWindow>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn) {
        UINT dpi = fn(hwnd);
        if (dpi) return dpi;
    }
    HDC dc = GetDC(nullptr);
    UINT dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY));
    ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96;
}

static std::wstring GetText(HWND ctrl) {
    const int n = GetWindowTextLengthW(ctrl);
    if (n <= 0) return std::wstring();
    std::wstring s(static_cast<size_t>(n), L'\0');
    GetWindowTextW(ctrl, &s[0], n + 1);
    return s;
}

// =====================================================================================
//  Antwortvorschlaege (zweiter Hotkey)
//
//  Ablauf: Hotkey -> offenen Chat des Vordergrundfensters lesen (chatread.h) ->
//  Ollama nach drei Antworten fragen -> Vorschlaege als Sprechblasen ueber dem
//  Eingabefeld anbieten -> ein Klick fuegt den gewaehlten Text dort ein.
//
//  Das Popup traegt WS_EX_NOACTIVATE, und daran haengt der ganze Ablauf: Wuerde
//  es den Fokus nehmen, verloere das Chat-Eingabefeld seinen Cursor und das
//  anschliessende STRG+V ginge ins Leere. So bleibt der Chat durchgehend das
//  aktive Fenster - das Popup schwebt nur darueber und faengt Mausklicks ab.
//
//  Aus demselben Grund kann das Popup keine Tastendruecke empfangen; ESC wird
//  deshalb im Timer abgefragt (siehe WM_TIMER). Geschlossen wird per ESC,
//  Rechtsklick, beim Wechsel des Vordergrundfensters (Timer 3) oder nach
//  45 Sekunden (Timer 4).
// =====================================================================================
struct ReplyResult {
    std::wstring              title;
    std::vector<std::wstring> items;
    std::wstring              error;   // gesetzt = melden; leer = stillschweigend nichts
};

static std::vector<std::wstring> g_replyItems;
static std::vector<RECT>         g_replyRects;   // Client-Koordinaten der Sprechblasen
static std::wstring              g_replyTitle;
static HWND  g_reply       = nullptr;
static HWND  g_replyTarget = nullptr;            // Chatfenster, in das eingefuegt wird
static int   g_replyHot    = -1;                 // Vorschlag unter der Maus
static HFONT g_replyFont   = nullptr;
static HFONT g_replyHead   = nullptr;
static bool  g_replyTrack  = false;              // laeuft ein TrackMouseEvent?

// Den gewaehlten Vorschlag einfuegen. Eigener Thread, weil das Warten auf
// Zwischenablage und Zielprogramm den UI-Thread sonst sichtbar einfriert.
struct PasteJob {
    std::wstring text;
    HWND         target;
    bool         restore;
};

static DWORD WINAPI PasteProc(LPVOID param) {
    PasteJob* job = static_cast<PasteJob*>(param);

    std::wstring backup;
    const bool haveBackup = ClipGetText(backup);

    // Der Chat ist wegen WS_EX_NOACTIVATE noch das aktive Fenster; das hier ist
    // nur die Absicherung, falls doch etwas dazwischengekommen ist.
    if (job->target && IsWindow(job->target)) {
        SetForegroundWindow(job->target);
        Sleep(60);
    }

    bool ok = ClipSetText(ToCrLf(job->text));
    if (ok) {
        Sleep(40);
        ok = SendCtrlCombo('V');
        Sleep(300);              // erst einfuegen lassen, dann die Ablage anfassen
    }
    if (job->restore && haveBackup) ClipSetText(backup);

    const bool failed = !ok;
    delete job;
    if (failed)
        PostDone(false, L"Einfuegen fehlgeschlagen (Zwischenablage oder SendInput blockiert).");
    return 0;
}

static void CloseReplyPopup() {
    if (g_reply) DestroyWindow(g_reply);
}

static int ReplyHitTest(POINT pt) {
    for (size_t i = 0; i < g_replyRects.size(); ++i)
        if (PtInRect(&g_replyRects[i], pt)) return static_cast<int>(i);
    return -1;
}

static void ReplyPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    RECT cr;
    GetClientRect(hwnd, &cr);

    // Doppelpuffer: ohne ihn flackert bei jedem Hover-Wechsel die ganze Flaeche.
    HDC     mem    = CreateCompatibleDC(dc);
    HBITMAP bmp    = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, bmp));

    const UINT dpi = DpiFor(hwnd);
    auto S = [dpi](int dip) { return MulDiv(dip, static_cast<int>(dpi), 96); };
    const int PAD = S(10), BPAD = S(9);

    SetBkMode(mem, TRANSPARENT);

    // Traegerflaeche
    HBRUSH  back = CreateSolidBrush(RGB(250, 250, 252));
    HPEN    edge = CreatePen(PS_SOLID, 1, RGB(198, 198, 206));
    HGDIOBJ ob   = SelectObject(mem, back);
    HGDIOBJ op   = SelectObject(mem, edge);
    RoundRect(mem, 0, 0, cr.right, cr.bottom, S(10), S(10));
    SelectObject(mem, ob);
    SelectObject(mem, op);
    DeleteObject(back);
    DeleteObject(edge);

    // Kopfzeile
    HGDIOBJ oldFont = SelectObject(mem, g_replyHead);
    SetTextColor(mem, RGB(104, 102, 100));
    RECT hr = { PAD, PAD, cr.right - PAD, PAD + S(16) };
    std::wstring head = L"Antwort auf: " + g_replyTitle;
    DrawTextW(mem, head.c_str(), -1, &hr,
              DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    // Sprechblasen
    SelectObject(mem, g_replyFont);
    for (size_t i = 0; i < g_replyRects.size() && i < g_replyItems.size(); ++i) {
        const bool hot = (static_cast<int>(i) == g_replyHot);
        HBRUSH bb = CreateSolidBrush(hot ? RGB(237, 241, 252) : RGB(255, 255, 255));
        HPEN   bp = CreatePen(PS_SOLID, 1, hot ? RGB(98, 100, 167) : RGB(216, 216, 224));
        HGDIOBJ o1 = SelectObject(mem, bb);
        HGDIOBJ o2 = SelectObject(mem, bp);

        const RECT& r = g_replyRects[i];
        RoundRect(mem, r.left, r.top, r.right, r.bottom, S(8), S(8));

        SelectObject(mem, o1);
        SelectObject(mem, o2);
        DeleteObject(bb);
        DeleteObject(bp);

        RECT t = { r.left + BPAD, r.top + BPAD, r.right - BPAD, r.bottom - BPAD };
        SetTextColor(mem, RGB(32, 31, 30));
        DrawTextW(mem, g_replyItems[i].c_str(), -1, &t, DT_WORDBREAK | DT_NOPREFIX);
    }
    SelectObject(mem, oldFont);

    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK ReplyProcWnd(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        ReplyPaint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;                       // alles Zeichnen erledigt WM_PAINT

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        const int hit = ReplyHitTest(pt);
        if (hit != g_replyHot) {
            g_replyHot = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (!g_replyTrack) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            g_replyTrack = TrackMouseEvent(&tme) != FALSE;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_replyTrack = false;
        if (g_replyHot != -1) { g_replyHot = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetCursor(LoadCursorW(nullptr, ReplyHitTest(pt) >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_LBUTTONUP: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        const int hit = ReplyHitTest(pt);
        if (hit >= 0 && hit < static_cast<int>(g_replyItems.size())) {
            PasteJob* job = new PasteJob();
            job->text    = g_replyItems[static_cast<size_t>(hit)];
            job->target  = g_replyTarget;
            job->restore = g_cfg.restoreClipboard;
            DestroyWindow(hwnd);        // erst weg, dann tippen - sonst klebt es im Bild
            if (HANDLE th = CreateThread(nullptr, 0, PasteProc, job, 0, nullptr)) CloseHandle(th);
            else delete job;
        }
        return 0;
    }

    case WM_RBUTTONUP:
        DestroyWindow(hwnd);
        return 0;

    case WM_TIMER:
        if (wp == 3) {
            // ESC bricht ab. Das Popup hat wegen WS_EX_NOACTIVATE keinen
            // Tastaturfokus und bekommt deshalb kein WM_KEYDOWN - die Taste wird
            // hier abgefragt statt ueber einen globalen Hotkey oder einen
            // Tastatur-Hook. Beides waere schwerer: ein RegisterHotKey(VK_ESCAPE)
            // wuerde ESC systemweit wegfangen, solange das Popup offen ist, und
            // damit auch im Chatfenster dahinter. So sieht das Zielprogramm sein
            // ESC weiterhin.
            //
            // Das niederwertige Bit meldet "seit dem letzten Aufruf gedrueckt" und
            // faengt damit auch ein kurzes Antippen zwischen zwei Ticks ab.
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8001) { DestroyWindow(hwnd); return 0; }
            // Der Nutzer ist woanders hin - dann hat sich der Vorschlag erledigt.
            if (GetForegroundWindow() != g_replyTarget) DestroyWindow(hwnd);
        }
        if (wp == 4) DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 3);
        KillTimer(hwnd, 4);
        if (g_replyFont) { DeleteObject(g_replyFont); g_replyFont = nullptr; }
        if (g_replyHead) { DeleteObject(g_replyHead); g_replyHead = nullptr; }
        g_reply      = nullptr;
        g_replyHot   = -1;
        g_replyTrack = false;
        g_replyRects.clear();
        g_replyItems.clear();
        return 0;

    default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowReplyPopup(const ReplyResult& res, HWND target) {
    CloseReplyPopup();
    if (res.items.empty()) return;

    g_replyItems  = res.items;
    g_replyTitle  = res.title;
    g_replyTarget = target;
    g_replyHot    = -1;
    g_replyRects.clear();

    const UINT dpi = DpiFor(target ? target : g_hwnd);
    auto S = [dpi](int dip) { return MulDiv(dip, static_cast<int>(dpi), 96); };

    g_replyFont = CreateFontW(-MulDiv(10, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL,
                              FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_replyHead = CreateFontW(-MulDiv(8, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL,
                              FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    const int PAD = S(10), BPAD = S(9), GAP = S(6);
    const int W     = S(430);
    const int textW = W - 2 * PAD - 2 * BPAD;

    // Hoehe jeder Blase aus dem umbrochenen Text berechnen
    HDC     dc  = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, g_replyFont);

    int y = PAD + S(16) + S(6);
    for (const std::wstring& s : g_replyItems) {
        RECT rc = { 0, 0, textW, 0 };
        DrawTextW(dc, s.c_str(), -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        const int h = rc.bottom + 2 * BPAD;
        RECT b = { PAD, y, W - PAD, y + h };
        g_replyRects.push_back(b);
        y += h + GAP;
    }
    const int H = y - GAP + PAD;

    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);

    // Platzierung: bevorzugt ueber dem Textcursor. Chromium-Anwendungen melden
    // aber oft gar keinen Win32-Caret - dann wird das Popup ueber dem unteren
    // Rand des Chatfensters gezeigt, wo bei Teams wie Discord das Eingabefeld
    // sitzt.
    POINT anchor    = { 0, 0 };
    bool  haveCaret = false;

    GUITHREADINFO gti = {};
    gti.cbSize = sizeof(gti);
    if (target && GetGUIThreadInfo(GetWindowThreadProcessId(target, nullptr), &gti) &&
        gti.hwndCaret) {
        POINT p = { gti.rcCaret.left, gti.rcCaret.top };
        if (ClientToScreen(gti.hwndCaret, &p)) { anchor = p; haveCaret = true; }
    }
    if (!haveCaret) {
        RECT tr = {};
        if (target) GetWindowRect(target, &tr);
        anchor.x = (tr.left + tr.right) / 2;
        anchor.y = tr.bottom - S(110);
    }

    int px = haveCaret ? anchor.x : anchor.x - W / 2;
    int py = anchor.y - H - S(8);

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &mi)) {
        if (px + W > mi.rcWork.right) px = mi.rcWork.right - W;
        if (px < mi.rcWork.left)      px = mi.rcWork.left;
        if (py < mi.rcWork.top)       py = anchor.y + S(24);   // oben kein Platz -> darunter
        if (py + H > mi.rcWork.bottom) py = mi.rcWork.bottom - H;
    }

    g_reply = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                              kReplyClass, L"", WS_POPUP,
                              px, py, W, H, nullptr, nullptr, g_inst, nullptr);
    if (!g_reply) return;

    ShowWindow(g_reply, SW_SHOWNOACTIVATE);

    // Ein noch anliegendes ESC aus der Zeit vor dem Popup abholen, sonst schliesst
    // sich das Fenster beim ersten Timer-Tick sofort wieder.
    GetAsyncKeyState(VK_ESCAPE);

    SetTimer(g_reply, 3,   120, nullptr);    // Wachhund: ESC? Fenster gewechselt?
    SetTimer(g_reply, 4, 45000, nullptr);    // spaetestens dann von selbst zu
}

// Chat lesen und das Modell nach Vorschlaegen fragen (eigener Thread).
static DWORD WINAPI ReplyWorker(LPVOID param) {
    cfg::Config* c   = static_cast<cfg::Config*>(param);
    ReplyResult* out = new ReplyResult();

    chat::Conversation conv = chat::ReadForeground(static_cast<size_t>(c->replyContext));

    if (conv.status == chat::ST_NO_TREE) {
        // Der einzige Fall, der erklaert werden muss: die Anwendung stimmt, gibt
        // ihren Inhalt aber nicht heraus. Ohne Hinweis sucht der Nutzer den
        // Fehler bei sich.
        out->error = (conv.app == chat::APP_DISCORD)
            ? L"Discord gibt seinen Inhalt nicht heraus. Discord einmal mit dem Schalter "
              L"--force-renderer-accessibility starten (siehe README)."
            : L"Dieses Fenster stellt keinen Accessibility-Baum bereit.";
    } else if (conv.status == chat::ST_OK) {
        net::Result res = net::Chat(c->ollamaUrl, c->apiKey, c->model, c->replyPrompt,
                                    chat::ToTranscript(conv), c->temperature, c->timeoutMs);
        if (!res.ok) {
            out->error = res.error;
        } else {
            out->items = net::ExtractReplies(res.body, 3);
            out->title = conv.title;
            if (out->items.empty()) out->error = L"Das Modell hat keinen Vorschlag geliefert.";
        }
    }
    // ST_NO_APP (kein Chatprogramm) und ST_NO_CHAT (keine Unterhaltung offen)
    // bleiben bewusst stumm - der Hotkey soll dort einfach nichts tun.

    delete c;
    InterlockedExchange(&g_replyBusy, 0);
    if (!PostMessageW(g_hwnd, WM_APP_REPLIES, 0, reinterpret_cast<LPARAM>(out)))
        delete out;
    return 0;
}

static void StartReply() {
    if (!g_cfg.replyEnabled) return;
    if (g_cfg.ollamaUrl.empty() || g_cfg.model.empty()) return;
    if (InterlockedCompareExchange(&g_replyBusy, 1, 0) != 0) return;

    CloseReplyPopup();
    g_replyTarget = GetForegroundWindow();

    TraySetState(STATE_BUSY, std::wstring(kAppName) + L"  -  Antwortvorschlaege ...");

    cfg::Config* snapshot = new cfg::Config(g_cfg);
    if (HANDLE th = CreateThread(nullptr, 0, ReplyWorker, snapshot, 0, nullptr)) {
        CloseHandle(th);
    } else {
        delete snapshot;
        InterlockedExchange(&g_replyBusy, 0);
        TraySetState(STATE_IDLE, IdleTip());
    }
}

// =====================================================================================
//  Einstellungsfenster (dynamisch erzeugt, ohne Ressourcendatei)
// =====================================================================================
// Ein Bedienelement anlegen und der Registerkarte zuordnen. page < 0 = immer
// sichtbar (Tab-Control und die Schaltflaechen darunter).
static HWND AddCtrl(int page, HWND parent, const wchar_t* cls, const wchar_t* text,
                    DWORD style, DWORD exStyle, int x, int y, int w, int h, int id,
                    unsigned flags = 0) {
    HWND c = CreateWindowExW(exStyle, cls, text, WS_CHILD | style, x, y, w, h, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             g_inst, nullptr);
    if (c) {
        const Slot s = { c, x, y, w, h, flags };
        g_slots[(page < 0) ? PAGE_COUNT : page].push_back(s);
    }
    return c;
}

static void ShowPage(int page) {
    for (int p = 0; p < PAGE_COUNT; ++p) {
        const int cmd = (p == page) ? SW_SHOW : SW_HIDE;
        for (const Slot& s : g_slots[p]) ShowWindow(s.hwnd, cmd);
    }
}

// Alle Elemente auf die aktuelle Fenstergroesse umrechnen.
//
// Der zusaetzliche Platz geht in die Breite an fast alles und in die Hoehe
// ausschliesslich an das jeweilige Prompt-Feld - genau dafuer zieht man das
// Fenster auf. Was unter einem gewachsenen Feld sitzt, rutscht um denselben
// Betrag nach unten (LF_Y).
static void LayoutSettings(HWND hwnd) {
    if (!g_baseCX || !g_baseCY) return;

    RECT cr;
    GetClientRect(hwnd, &cr);
    const int dW = cr.right  - g_baseCX;
    const int dH = cr.bottom - g_baseCY;

    for (int p = 0; p <= PAGE_COUNT; ++p) {
        for (const Slot& s : g_slots[p]) {
            int x = s.x, y = s.y, w = s.w, h = s.h;
            if (s.flags & LF_W) w += dW;
            if (s.flags & LF_X) x += dW;
            if (s.flags & LF_H) h += dH;
            if (s.flags & LF_Y) y += dH;
            SetWindowPos(s.hwnd, nullptr, x, y, w, h,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        }
    }
}

// Fuer Meldungen aus der Pruefung: erst die Registerkarte nach vorn holen, dann
// den Fokus setzen. Ohne das landete der Cursor in einem unsichtbaren Feld, und
// der Dialog saehe aus, als habe die Meldung gar nichts bewirkt.
static void FocusOnPage(HWND hwnd, int page, int ctrlId) {
    if (HWND tab = GetDlgItem(hwnd, IDC_TAB))
        SendMessageW(tab, TCM_SETCURSEL, static_cast<WPARAM>(page), 0);
    ShowPage(page);
    SetFocus(GetDlgItem(hwnd, ctrlId));
}

static void BuildSettingsControls(HWND hwnd) {
    const UINT dpi = DpiFor(hwnd);
    auto S = [dpi](int dip) { return MulDiv(dip, static_cast<int>(dpi), 96); };

    for (int p = 0; p <= PAGE_COUNT; ++p) g_slots[p].clear();

    g_font = CreateFontW(-MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL,
                         FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    // Layout in DIPs. TH ist an der hoechsten Seite bemessen (Antwortvorschlaege).
    const int W  = 520;
    const int TX = 12, TY = 10, TW = W - 24, TH = 344;
    const int PX = TX + 12, CW = TW - 24;      // Innenrand der Registerkarte
    const int HALF = (CW - 16) / 2;

    HWND tab = AddCtrl(-1, hwnd, WC_TABCONTROLW, L"",
                       WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0,
                       S(TX), S(TY), S(TW), S(TH), IDC_TAB, LF_W | LF_H);
    if (tab) {
        TCITEMW ti = {};
        ti.mask = TCIF_TEXT;
        const wchar_t* names[PAGE_COUNT] = { L"Verbindung", L"Umformulieren",
                                             L"Antwortvorschläge" };
        for (int i = 0; i < PAGE_COUNT; ++i) {
            ti.pszText = const_cast<LPWSTR>(names[i]);
            SendMessageW(tab, TCM_INSERTITEMW, static_cast<WPARAM>(i),
                         reinterpret_cast<LPARAM>(&ti));
        }
    }

    auto label = [&](int page, const wchar_t* t, int y, unsigned f = LF_W) {
        AddCtrl(page, hwnd, L"STATIC", t, SS_LEFT, 0, S(PX), S(y), S(CW), S(16), 0, f);
    };
    auto edit = [&](int page, int id, int y, int h, DWORD extra, unsigned f = LF_W) {
        AddCtrl(page, hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | extra,
                WS_EX_CLIENTEDGE, S(PX), S(y), S(CW), S(h), id, f);
    };
    auto check = [&](int page, int id, const wchar_t* t, int y, unsigned f = LF_W) {
        AddCtrl(page, hwnd, L"BUTTON", t, WS_TABSTOP | BS_AUTOCHECKBOX, 0,
                S(PX), S(y), S(CW), S(20), id, f);
    };
    auto hint = [&](int page, const wchar_t* t, int y, unsigned f = LF_W) {
        AddCtrl(page, hwnd, L"STATIC", t, SS_LEFT, 0,
                S(PX + 18), S(y), S(CW - 18), S(16), 0, f);
    };
    const DWORD kMulti = ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;

    // --- Registerkarte "Verbindung" ---------------------------------------------
    label(PAGE_CONNECT, L"Ollama-URL:", 46);
    edit (PAGE_CONNECT, IDC_URL, 64, 24, 0);

    label(PAGE_CONNECT, L"Modell (Schreibweise wie in Ollama):", 100);
    // Editierbare Combobox: freie Eingabe bleibt moeglich, die installierten
    // Modelle erscheinen als Vorschlagsliste. Die Hoehe legt hier die
    // aufgeklappte Liste fest, nicht das sichtbare Feld.
    AddCtrl(PAGE_CONNECT, hwnd, L"COMBOBOX", L"",
            WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, 0,
            S(PX), S(118), S(CW), S(220), IDC_MODEL);

    label(PAGE_CONNECT, L"API-Key / Bearer-Token (bei lokalem Ollama leer):", 154);
    edit (PAGE_CONNECT, IDC_KEY, 172, 24, 0);

    AddCtrl(PAGE_CONNECT, hwnd, L"BUTTON", L"Verbindung testen",
            WS_TABSTOP | BS_PUSHBUTTON, 0, S(PX), S(212), S(140), S(28), IDC_TEST);

    check(PAGE_CONNECT, IDC_WARMMSG,
          L"Benachrichtigen, wenn das Modell beim Start geladen ist", 260);
    check(PAGE_CONNECT, IDC_AUTOSTART, L"Mit Windows automatisch starten", 284);

    // --- Registerkarte "Umformulieren" ------------------------------------------
    label(PAGE_REWRITE, L"Hotkey (z. B. CTRL+SHIFT+SPACE):", 46);
    edit (PAGE_REWRITE, IDC_HOTKEY, 64, 24, 0);

    label(PAGE_REWRITE, L"System-Prompt:", 100);
    edit (PAGE_REWRITE, IDC_PROMPT, 118, 140, kMulti, LF_W | LF_H);

    check(PAGE_REWRITE, IDC_TYPING, L"Antwort live tippen statt einfügen (Streaming)",
          268, LF_W | LF_Y);
    hint (PAGE_REWRITE, L"Antwort erscheint Token für Token. Zeilenumbrüche als ENTER.",
          288, LF_W | LF_Y);
    check(PAGE_REWRITE, IDC_RESTORE,
          L"Zwischenablage nach dem Einfügen wiederherstellen", 310, LF_W | LF_Y);

    // --- Registerkarte "Antwortvorschläge" ---------------------------------------
    check(PAGE_REPLY, IDC_REPLYON,
          L"Antwortvorschläge in Teams und Discord anbieten", 46);
    hint (PAGE_REPLY, L"Liest den offenen Chat des Vordergrundfensters. "
                      L"ESC schließt die Vorschläge.", 66);

    // Hotkey und Kontexttiefe teilen sich eine Zeile und behalten ihre Breite,
    // wenn das Fenster waechst - zwei kurze Eingaben muessen nicht mitwachsen.
    AddCtrl(PAGE_REPLY, hwnd, L"STATIC", L"Hotkey:", SS_LEFT, 0,
            S(PX), S(96), S(HALF), S(16), 0);
    AddCtrl(PAGE_REPLY, hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
            S(PX), S(114), S(HALF), S(24), IDC_REPLYKEY);

    AddCtrl(PAGE_REPLY, hwnd, L"STATIC", L"Gelesene Nachrichten (2-40):", SS_LEFT, 0,
            S(PX + HALF + 16), S(96), S(HALF), S(16), 0);
    AddCtrl(PAGE_REPLY, hwnd, L"EDIT", L"", WS_TABSTOP | ES_NUMBER, WS_EX_CLIENTEDGE,
            S(PX + HALF + 16), S(114), S(60), S(24), IDC_REPLYCTX);

    label(PAGE_REPLY, L"System-Prompt für die Vorschläge:", 150);
    edit (PAGE_REPLY, IDC_REPLYPROMPT, 168, 140, kMulti, LF_W | LF_H);

    // Einzeilig halten: das STATIC bricht sonst um und wird unten abgeschnitten.
    AddCtrl(PAGE_REPLY, hwnd, L"STATIC",
            L"Jeder Vorschlag muss in <reply>-Tags stehen - daraus liest ChatNicer sie.",
            SS_LEFT, 0, S(PX), S(316), S(CW), S(16), 0, LF_W | LF_Y);

    // --- Schaltflaechen (immer sichtbar) -----------------------------------------
    const int BY = TY + TH + 12, BW = 110, BH = 28;
    AddCtrl(-1, hwnd, L"BUTTON", L"Speichern", WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0,
            S(W - 12 - 2 * BW - 8), S(BY), S(BW), S(BH), IDC_SAVE, LF_X | LF_Y);
    AddCtrl(-1, hwnd, L"BUTTON", L"Abbrechen", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
            S(W - 12 - BW), S(BY), S(BW), S(BH), IDC_CANCEL, LF_X | LF_Y);

    EnumChildWindows(hwnd, [](HWND child, LPARAM) -> BOOL {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        return TRUE;
    }, 0);

    // Werte aus der Konfiguration uebernehmen
    SetWindowTextW(GetDlgItem(hwnd, IDC_URL),    g_cfg.ollamaUrl.c_str());
    SetWindowTextW(GetDlgItem(hwnd, IDC_MODEL),  g_cfg.model.c_str());
    SetWindowTextW(GetDlgItem(hwnd, IDC_KEY),    g_cfg.apiKey.c_str());
    SetWindowTextW(GetDlgItem(hwnd, IDC_PROMPT), ToCrLf(g_cfg.systemPrompt).c_str());
    SetWindowTextW(GetDlgItem(hwnd, IDC_HOTKEY),
                   cfg::HotkeyToString(g_cfg.hotkeyMods, g_cfg.hotkeyVk).c_str());
    SetWindowTextW(GetDlgItem(hwnd, IDC_REPLYKEY),
                   cfg::HotkeyToString(g_cfg.replyMods, g_cfg.replyVk).c_str());
    SetWindowTextW(GetDlgItem(hwnd, IDC_REPLYPROMPT), ToCrLf(g_cfg.replyPrompt).c_str());

    wchar_t ctxText[16];
    wsprintfW(ctxText, L"%d", g_cfg.replyContext);
    SetWindowTextW(GetDlgItem(hwnd, IDC_REPLYCTX), ctxText);

    CheckDlgButton(hwnd, IDC_REPLYON, g_cfg.replyEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_RESTORE, g_cfg.restoreClipboard ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_TYPING,  g_cfg.typingInput      ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_WARMMSG, g_cfg.warmupNotify     ? BST_CHECKED : BST_UNCHECKED);
    // Autostart steht in der Registry, nicht in g_cfg - immer den echten Zustand zeigen.
    CheckDlgButton(hwnd, IDC_AUTOSTART, cfg::AutostartEnabled() ? BST_CHECKED : BST_UNCHECKED);

    ShowPage(PAGE_CONNECT);

    // installierte Modelle nebenher holen - blockiert den Dialog nicht
    RequestModelList(g_cfg.ollamaUrl, g_cfg.apiKey);

    // Ausgangsgroesse merken: LayoutSettings() rechnet alle Abweichungen davon.
    g_baseCX = S(W);
    g_baseCY = S(BY + BH + 12);

    // Fenster auf den Inhalt anpassen und zentrieren
    RECT want = { 0, 0, g_baseCX, g_baseCY };
    AdjustWindowRectEx(&want, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)));
    const int ww = want.right - want.left, wh = want.bottom - want.top;
    g_minCX = ww;                       // kleiner darf es nicht werden
    g_minCY = wh;
    const int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
    const int sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
    SetWindowPos(hwnd, nullptr, sx, sy, ww, wh, SWP_NOZORDER);

    SetFocus(GetDlgItem(hwnd, IDC_URL));
}

// Liest die Eingaben, validiert sie und uebernimmt sie in g_cfg + config.ini
static bool ApplySettings(HWND hwnd) {
    cfg::Config next = g_cfg;
    next.ollamaUrl   = net::Trim(GetText(GetDlgItem(hwnd, IDC_URL)));
    next.model       = net::Trim(GetText(GetDlgItem(hwnd, IDC_MODEL)));
    next.apiKey      = net::Trim(GetText(GetDlgItem(hwnd, IDC_KEY)));
    next.systemPrompt= ToLf(GetText(GetDlgItem(hwnd, IDC_PROMPT)));
    next.restoreClipboard = IsDlgButtonChecked(hwnd, IDC_RESTORE) == BST_CHECKED;
    next.typingInput      = IsDlgButtonChecked(hwnd, IDC_TYPING)  == BST_CHECKED;
    next.warmupNotify     = IsDlgButtonChecked(hwnd, IDC_WARMMSG) == BST_CHECKED;

    if (next.ollamaUrl.empty()) next.ollamaUrl = cfg::kDefaultUrl;

    if (next.ollamaUrl.compare(0, 7, L"http://") != 0 &&
        next.ollamaUrl.compare(0, 8, L"https://") != 0) {
        MessageBoxW(hwnd, L"Die Ollama-URL muss mit http:// oder https:// beginnen.\n\n"
                          L"Standard einer lokalen Installation:\nhttp://localhost:11434",
                    kAppName, MB_ICONWARNING | MB_OK);
        FocusOnPage(hwnd, PAGE_CONNECT, IDC_URL);
        return false;
    }

    if (next.model.empty()) {
        MessageBoxW(hwnd, L"Bitte ein Modell angeben, z. B. qwen3:4b.\n\n"
                          L"Die installierten Modelle zeigt \"ollama list\".",
                    kAppName, MB_ICONWARNING | MB_OK);
        FocusOnPage(hwnd, PAGE_CONNECT, IDC_MODEL);
        return false;
    }

    UINT mods = 0, vk = 0;
    const std::wstring hkText = net::Trim(GetText(GetDlgItem(hwnd, IDC_HOTKEY)));
    if (!cfg::ParseHotkey(hkText, mods, vk)) {
        MessageBoxW(hwnd, L"Hotkey nicht verstanden.\n\nBeispiele:\n"
                          L"CTRL+SHIFT+SPACE\nCTRL+ALT+Q\nWIN+F9",
                    kAppName, MB_ICONWARNING | MB_OK);
        FocusOnPage(hwnd, PAGE_REWRITE, IDC_HOTKEY);
        return false;
    }

    // Hotkey nur neu registrieren, wenn er sich geaendert hat
    if (mods != g_cfg.hotkeyMods || vk != g_cfg.hotkeyVk || !g_hotkeyOk) {
        UnregisterHotKey(g_hwnd, HOTKEY_ID);
        if (!RegisterHotKey(g_hwnd, HOTKEY_ID, mods | MOD_NOREPEAT, vk)) {
            MessageBoxW(hwnd, L"Dieser Hotkey ist bereits von einem anderen Programm belegt.\n"
                              L"Bitte eine andere Kombination waehlen.",
                        kAppName, MB_ICONWARNING | MB_OK);
            g_hotkeyOk = RegisterHotKey(g_hwnd, HOTKEY_ID, g_cfg.hotkeyMods | MOD_NOREPEAT,
                                        g_cfg.hotkeyVk) != FALSE;
            FocusOnPage(hwnd, PAGE_REWRITE, IDC_HOTKEY);
            return false;
        }
        g_hotkeyOk = true;
    }
    next.hotkeyMods = mods;
    next.hotkeyVk   = vk;

    // Zweiter Hotkey (Antwortvorschlaege). Er wird auch dann registriert, wenn
    // die Funktion abgeschaltet ist - so bleibt die Kombination reserviert und
    // ein spaeteres Einschalten braucht keinen Neustart. StartReply() prueft
    // replyEnabled selbst.
    UINT rmods = 0, rvk = 0;
    const std::wstring rkText = net::Trim(GetText(GetDlgItem(hwnd, IDC_REPLYKEY)));
    if (!cfg::ParseHotkey(rkText, rmods, rvk)) {
        MessageBoxW(hwnd, L"Hotkey fuer Antwortvorschlaege nicht verstanden.\n\nBeispiele:\n"
                          L"CTRL+ALT+SPACE\nCTRL+ALT+A\nWIN+F10",
                    kAppName, MB_ICONWARNING | MB_OK);
        FocusOnPage(hwnd, PAGE_REPLY, IDC_REPLYKEY);
        return false;
    }
    if (rmods == mods && rvk == vk) {
        MessageBoxW(hwnd, L"Beide Hotkeys sind gleich belegt.\n\n"
                          L"Bitte fuer die Antwortvorschlaege eine andere Kombination waehlen.",
                    kAppName, MB_ICONWARNING | MB_OK);
        FocusOnPage(hwnd, PAGE_REPLY, IDC_REPLYKEY);
        return false;
    }
    if (rmods != g_cfg.replyMods || rvk != g_cfg.replyVk || !g_replyKeyOk) {
        UnregisterHotKey(g_hwnd, HOTKEY_REPLY);
        if (!RegisterHotKey(g_hwnd, HOTKEY_REPLY, rmods | MOD_NOREPEAT, rvk)) {
            MessageBoxW(hwnd, L"Der Hotkey fuer die Antwortvorschlaege ist bereits von einem "
                              L"anderen Programm belegt.\nBitte eine andere Kombination waehlen.",
                        kAppName, MB_ICONWARNING | MB_OK);
            g_replyKeyOk = RegisterHotKey(g_hwnd, HOTKEY_REPLY, g_cfg.replyMods | MOD_NOREPEAT,
                                          g_cfg.replyVk) != FALSE;
            FocusOnPage(hwnd, PAGE_REPLY, IDC_REPLYKEY);
            return false;
        }
        g_replyKeyOk = true;
    }
    next.replyMods    = rmods;
    next.replyVk      = rvk;
    next.replyEnabled = IsDlgButtonChecked(hwnd, IDC_REPLYON) == BST_CHECKED;
    next.replyPrompt  = ToLf(GetText(GetDlgItem(hwnd, IDC_REPLYPROMPT)));

    // Kontexttiefe auf den erlaubten Bereich klemmen statt den Nutzer mit einer
    // Meldung aufzuhalten - ein leeres Feld heisst schlicht "Standard".
    const std::wstring ctxText = net::Trim(GetText(GetDlgItem(hwnd, IDC_REPLYCTX)));
    int ctx = 0;
    for (wchar_t ch : ctxText)
        if (ch >= L'0' && ch <= L'9') ctx = ctx * 10 + (ch - L'0');
    if (ctx <= 0) ctx = 8;
    next.replyContext = (ctx < 2) ? 2 : (ctx > 40 ? 40 : ctx);

    // Autostart landet in der Registry, nicht in der config.ini. Nur anfassen, wenn
    // die Checkbox vom Ist-Zustand abweicht - sonst wuerde jedes Speichern den
    // Wert neu schreiben, auch wenn niemand etwas daran geaendert hat.
    const bool wantAuto = IsDlgButtonChecked(hwnd, IDC_AUTOSTART) == BST_CHECKED;
    if (wantAuto != cfg::AutostartEnabled() && !cfg::SetAutostart(wantAuto)) {
        MessageBoxW(hwnd, wantAuto
                        ? L"Der Autostart konnte nicht eingetragen werden.\n\n"
                          L"Windows hat den Registry-Zugriff auf\n"
                          L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run abgelehnt."
                        : L"Der Autostart-Eintrag konnte nicht entfernt werden.\n\n"
                          L"Windows hat den Registry-Zugriff auf\n"
                          L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run abgelehnt.",
                    kAppName, MB_ICONWARNING | MB_OK);
        CheckDlgButton(hwnd, IDC_AUTOSTART,
                       cfg::AutostartEnabled() ? BST_CHECKED : BST_UNCHECKED);
        // Bewusst kein return: die uebrigen Einstellungen sind gueltig und sollen
        // gespeichert werden. Die Checkbox zeigt danach wieder den echten Zustand.
    }

    g_cfg = next;
    if (!cfg::Save(g_cfg)) {
        MessageBoxW(hwnd, (L"Die Einstellungen konnten nicht gespeichert werden:\n" +
                           cfg::ConfigPath()).c_str(), kAppName, MB_ICONERROR | MB_OK);
        return false;
    }

    TraySetState(STATE_IDLE, IdleTip());
    return true;
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        BuildSettingsControls(hwnd);
        return 0;

    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));

    case WM_SIZE:
        LayoutSettings(hwnd);
        // Das Tab-Control zeichnet seinen Rahmen nicht von selbst nach, wenn es
        // waechst - ohne das bleiben beim Aufziehen Reste des alten Rands stehen.
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_GETMINMAXINFO:
        if (g_minCX && g_minCY) {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = g_minCX;
            mmi->ptMinTrackSize.y = g_minCY;
        }
        return 0;

    case WM_NOTIFY: {
        const NMHDR* nm = reinterpret_cast<const NMHDR*>(lp);
        if (nm && nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGE) {
            ShowPage(static_cast<int>(SendMessageW(nm->hwndFrom, TCM_GETCURSEL, 0, 0)));
            return 0;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_SAVE:
            if (ApplySettings(hwnd)) DestroyWindow(hwnd);
            return 0;

        case IDC_CANCEL:
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;

        case IDC_TEST: {
            cfg::Config* t = new cfg::Config(g_cfg);
            t->ollamaUrl    = net::Trim(GetText(GetDlgItem(hwnd, IDC_URL)));
            t->model        = net::Trim(GetText(GetDlgItem(hwnd, IDC_MODEL)));
            t->apiKey       = net::Trim(GetText(GetDlgItem(hwnd, IDC_KEY)));
            t->systemPrompt = ToLf(GetText(GetDlgItem(hwnd, IDC_PROMPT)));
            if (t->ollamaUrl.empty()) t->ollamaUrl = cfg::kDefaultUrl;
            if (t->model.empty()) {
                delete t;
                MessageBoxW(hwnd, L"Bitte zuerst ein Modell angeben, z. B. qwen3:4b.",
                            kAppName, MB_ICONINFORMATION | MB_OK);
                FocusOnPage(hwnd, PAGE_CONNECT, IDC_MODEL);
                return 0;
            }
            // Der Test kann je nach Modell einige Sekunden dauern.
            SetWindowTextW(GetDlgItem(hwnd, IDC_TEST), L"Test laeuft ...");
            EnableWindow(GetDlgItem(hwnd, IDC_TEST), FALSE);
            RequestModelList(t->ollamaUrl, t->apiKey);
            if (HANDLE th = CreateThread(nullptr, 0, TestProc, t, 0, nullptr)) CloseHandle(th);
            else { delete t; EnableWindow(GetDlgItem(hwnd, IDC_TEST), TRUE); }
            return 0;
        }

        case IDC_MODEL:
            // Beim ersten Aufklappen die Liste holen, falls sie noch leer ist
            // (z. B. weil Ollama beim Öffnen des Dialogs noch nicht lief).
            if (HIWORD(wp) == CBN_DROPDOWN &&
                SendMessageW(GetDlgItem(hwnd, IDC_MODEL), CB_GETCOUNT, 0, 0) == 0) {
                RequestModelList(net::Trim(GetText(GetDlgItem(hwnd, IDC_URL))),
                                 net::Trim(GetText(GetDlgItem(hwnd, IDC_KEY))));
            }
            return 0;

        default: break;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_font) { DeleteObject(g_font); g_font = nullptr; }
        for (int p = 0; p <= PAGE_COUNT; ++p) g_slots[p].clear();
        g_settings = nullptr;
        return 0;

    default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void OpenSettings() {
    if (g_settings) {
        SetForegroundWindow(g_settings);
        return;
    }
    // Groessenveraenderlich (WS_THICKFRAME), damit sich die beiden Prompt-Felder
    // aufziehen lassen - darin steht der laengste Text des ganzen Programms.
    // WS_CLIPCHILDREN haelt das Flackern beim Ziehen in Grenzen.
    g_settings = CreateWindowExW(
        WS_EX_CONTROLPARENT | WS_EX_APPWINDOW, kCfgClass,
        L"ChatNicer - Einstellungen",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
        WS_THICKFRAME | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 520,
        nullptr, nullptr, g_inst, nullptr);

    if (g_settings) {
        SendMessageW(g_settings, WM_SETICON, ICON_BIG,
                     reinterpret_cast<LPARAM>(g_icons[STATE_IDLE]));
        ShowWindow(g_settings, SW_SHOW);
        SetForegroundWindow(g_settings);
    }
}

// =====================================================================================
//  Kontextmenue + Info
// =====================================================================================
static void ShowTrayMenu(int x, int y) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"&Einstellungen ...");
    AppendMenuW(menu, MF_STRING, IDM_ABOUT,    L"&Info");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT,     L"&Beenden");
    SetMenuDefaultItem(menu, IDM_SETTINGS, FALSE);

    SetForegroundWindow(g_hwnd);                 // sonst bleibt das Menue offen haengen
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, x, y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static void ShowAbout() {
    std::wstring text;
    text += kAppName;  text += L" ";  text += kVersion;
    text += L"\n\nMarkierten Text per Hotkey an ein lokales Ollama-Modell schicken "
            L"und die Antwort direkt wieder einfuegen.\n\n";
    text += L"Hotkey:\t" + cfg::HotkeyToString(g_cfg.hotkeyMods, g_cfg.hotkeyVk) + L"\n";
    text += L"Ollama:\t" + g_cfg.ollamaUrl + L"\n";
    text += L"Modell:\t" + g_cfg.model + L"\n";
    text += L"Konfig:\t" + cfg::ConfigPath() + L"\n\n";
    text += L"Win32 + WinHTTP, keine externen Abhaengigkeiten.";
    MessageBoxW(nullptr, text.c_str(), kAppName, MB_ICONINFORMATION | MB_OK | MB_SETFOREGROUND);
}

// =====================================================================================
//  Hauptfenster (unsichtbar, nur Nachrichtenempfaenger)
// =====================================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_msgTaskbarCreated) {            // Explorer neu gestartet
        TrayAdd();
        TraySetState(g_busy ? STATE_BUSY : STATE_IDLE, IdleTip());
        return 0;
    }

    switch (msg) {
    case WM_TRAYICON:
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            ShowTrayMenu(GET_X_LPARAM(wp), GET_Y_LPARAM(wp));
            return 0;
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONDBLCLK:
            OpenSettings();
            return 0;
        default: break;
        }
        return 0;

    case WM_HOTKEY:
        if      (wp == HOTKEY_ID)    StartWork();
        else if (wp == HOTKEY_REPLY) StartReply();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_SETTINGS: OpenSettings();      return 0;
        case IDM_ABOUT:    ShowAbout();         return 0;
        case IDM_EXIT:     DestroyWindow(hwnd); return 0;
        default: break;
        }
        break;

    case WM_APP_STATE:
        TraySetState(static_cast<TrayState>(wp), IdleTip());
        return 0;

    case WM_APP_DONE: {
        std::wstring* msgText = reinterpret_cast<std::wstring*>(lp);
        const bool ok = (wp != 0);
        if (ok) {
            TraySetState(STATE_IDLE, IdleTip());
        } else {
            TraySetState(STATE_ERROR, std::wstring(kAppName) + L"  -  Fehler");
            TrayBalloon(kAppName, msgText ? *msgText : L"Unbekannter Fehler", true);
            SetTimer(hwnd, 1, 4000, nullptr);   // Icon nach kurzer Zeit zuruecksetzen
        }
        delete msgText;
        return 0;
    }

    case WM_APP_TESTDONE: {
        std::wstring* msgText = reinterpret_cast<std::wstring*>(lp);
        if (g_settings) {
            SetWindowTextW(GetDlgItem(g_settings, IDC_TEST), L"Verbindung testen");
            EnableWindow(GetDlgItem(g_settings, IDC_TEST), TRUE);
        }
        MessageBoxW(g_settings ? g_settings : nullptr,
                    msgText ? msgText->c_str() : L"", L"ChatNicer - Verbindungstest",
                    MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
        delete msgText;
        return 0;
    }

    case WM_APP_WARMUP: {
        std::wstring* msgText = reinterpret_cast<std::wstring*>(lp);
        const bool ok = (wp != 0);
        if (!g_busy) {                      // laeuft eine echte Anfrage, gehoert ihr das Icon
            if (ok) {
                TraySetState(STATE_IDLE, IdleTip());
            } else {
                TraySetState(STATE_ERROR, std::wstring(kAppName) + L"  -  Fehler");
                SetTimer(hwnd, 1, 4000, nullptr);
            }
        }
        // Die Erfolgsmeldung laesst sich abschalten (Checkbox im Dialog); ein Fehler
        // wird immer gemeldet, sonst faellt ein nicht laufendes Ollama erst beim
        // ersten Hotkey auf.
        if (!ok || g_cfg.warmupNotify)
            TrayBalloon(kAppName, msgText ? *msgText : L"", !ok);
        delete msgText;
        return 0;
    }

    case WM_APP_REPLIES: {
        ReplyResult* r = reinterpret_cast<ReplyResult*>(lp);
        if (!g_busy) TraySetState(STATE_IDLE, IdleTip());
        if (r) {
            if (!r->items.empty()) {
                ShowReplyPopup(*r, g_replyTarget);
            } else if (!r->error.empty()) {
                TraySetState(STATE_ERROR, std::wstring(kAppName) + L"  -  Fehler");
                TrayBalloon(kAppName, r->error, true);
                SetTimer(hwnd, 1, 4000, nullptr);
            }
            // Weder Vorschlag noch Fehler: kein Chat offen - dann bleibt es still.
        }
        delete r;
        return 0;
    }

    case WM_APP_MODELS: {
        std::vector<std::wstring>* list = reinterpret_cast<std::vector<std::wstring>*>(lp);
        if (list && g_settings) {
            HWND combo = GetDlgItem(g_settings, IDC_MODEL);
            // Die freie Eingabe des Nutzers darf durch das Fuellen nicht verloren gehen.
            const std::wstring current = GetText(combo);
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            for (const std::wstring& m : *list)
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(m.c_str()));
            SetWindowTextW(combo, current.c_str());
        }
        delete list;
        return 0;
    }

    case WM_TIMER:
        if (wp == 1) {
            KillTimer(hwnd, 1);
            if (!g_busy) TraySetState(STATE_IDLE, IdleTip());
        }
        return 0;

    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_ID);
        UnregisterHotKey(hwnd, HOTKEY_REPLY);
        CloseReplyPopup();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;

    default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =====================================================================================
//  Einstieg
// =====================================================================================
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    g_inst = inst;

    // Nur eine Instanz - sonst kaeme es zu Hotkey-Konflikten mit sich selbst.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"ChatNicer laeuft bereits (siehe Infobereich der Taskleiste).",
                    kAppName, MB_ICONINFORMATION | MB_OK);
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    // Scharfe Darstellung auf HiDPI-Monitoren, sofern vom System unterstuetzt
    typedef BOOL (WINAPI *PFN_SetCtx)(DPI_AWARENESS_CONTEXT);
    if (PFN_SetCtx setCtx = reinterpret_cast<PFN_SetCtx>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))) {
        setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    } else {
        SetProcessDPIAware();
    }

    // ICC_TAB_CLASSES ist Pflicht, seit der Einstellungsdialog Registerkarten hat -
    // ohne sie entsteht das Tab-Control schlicht nicht.
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);

    cfg::Load(g_cfg);

    g_icons[STATE_IDLE]  = MakeIcon(RGB(0x2D, 0x7F, 0xF9));   // blau  - bereit
    g_icons[STATE_BUSY]  = MakeIcon(RGB(0xF5, 0xA6, 0x23));   // orange - Anfrage laeuft
    g_icons[STATE_ERROR] = MakeIcon(RGB(0xE5, 0x47, 0x4D));   // rot   - Fehler

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = inst;
    wc.lpszClassName = kWndClass;
    wc.hIcon         = g_icons[STATE_IDLE];
    RegisterClassExW(&wc);

    WNDCLASSEXW wcCfg = {};
    wcCfg.cbSize        = sizeof(wcCfg);
    wcCfg.lpfnWndProc   = SettingsProc;
    wcCfg.hInstance     = inst;
    wcCfg.lpszClassName = kCfgClass;
    wcCfg.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wcCfg.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wcCfg.hIcon         = g_icons[STATE_IDLE];
    RegisterClassExW(&wcCfg);

    // Popup der Antwortvorschlaege. Kein Hintergrund-Brush: gezeichnet wird
    // komplett in WM_PAINT (Doppelpuffer), sonst blitzt die Flaeche auf.
    WNDCLASSEXW wcRep = {};
    wcRep.cbSize        = sizeof(wcRep);
    wcRep.lpfnWndProc   = ReplyProcWnd;
    wcRep.hInstance     = inst;
    wcRep.lpszClassName = kReplyClass;
    wcRep.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wcRep);

    // Bewusst KEIN HWND_MESSAGE-Fenster: message-only Windows empfangen keine
    // Broadcasts und wuerden die "TaskbarCreated"-Nachricht verpassen.
    // Stattdessen ein normales Fenster, das nie sichtbar gemacht wird.
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWndClass, kAppName, WS_OVERLAPPED,
                             0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) {
        MessageBoxW(nullptr, L"Fenster konnte nicht erstellt werden.", kAppName, MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    TrayAdd();
    TraySetState(STATE_IDLE, IdleTip());

    g_hotkeyOk = RegisterHotKey(g_hwnd, HOTKEY_ID, g_cfg.hotkeyMods | MOD_NOREPEAT,
                                g_cfg.hotkeyVk) != FALSE;
    if (!g_hotkeyOk) {
        TrayBalloon(kAppName,
                    L"Der Hotkey " + cfg::HotkeyToString(g_cfg.hotkeyMods, g_cfg.hotkeyVk) +
                    L" ist belegt. Bitte unter \"Einstellungen\" eine andere Kombination waehlen.",
                    true);
    }

    g_replyKeyOk = RegisterHotKey(g_hwnd, HOTKEY_REPLY, g_cfg.replyMods | MOD_NOREPEAT,
                                  g_cfg.replyVk) != FALSE;
    if (!g_replyKeyOk && g_cfg.replyEnabled) {
        TrayBalloon(kAppName,
                    L"Der Hotkey " + cfg::HotkeyToString(g_cfg.replyMods, g_cfg.replyVk) +
                    L" fuer Antwortvorschlaege ist belegt. Bitte unter \"Einstellungen\" "
                    L"eine andere Kombination waehlen.",
                    true);
    }
    // Erststart (noch keine config.ini): direkt konfigurieren lassen. Die Standardwerte
    // passen auf eine unveraenderte lokale Ollama-Installation.
    if (GetFileAttributesW(cfg::ConfigPath().c_str()) == INVALID_FILE_ATTRIBUTES) {
        OpenSettings();
    } else {
        // Konfiguriertes System: das Modell gleich laden lassen, damit der erste
        // Hotkey nicht auf den Modellstart wartet. Beim Erststart hat das keinen
        // Sinn - dort steht das Modell ja erst nach dem Dialog fest.
        StartWarmup();
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g_settings && IsDialogMessage(g_settings, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    for (HICON ic : g_icons) if (ic) DestroyIcon(ic);
    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
