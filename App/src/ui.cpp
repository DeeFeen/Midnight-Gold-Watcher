#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "ui.h"
#include "watch.h"
#include "app.h"
#include "..\res\resource.h"

#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <string>
#include <vector>
#include <mutex>
#include <cstdio>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ---------------------------------------------------------------------------
// Palette (TradeSkillMaster-inspired dark graphite + gold)
// ---------------------------------------------------------------------------
static const COLORREF C_BG     = RGB(0x15, 0x18, 0x1E);
static const COLORREF C_PANEL  = RGB(0x1D, 0x22, 0x2B);
static const COLORREF C_EDGE   = RGB(0x2D, 0x35, 0x43);
static const COLORREF C_GOLD   = RGB(0xE8, 0xC0, 0x5F);
static const COLORREF C_TXT    = RGB(0xEA, 0xE8, 0xE3);
static const COLORREF C_DIM    = RGB(0x8C, 0x93, 0x9F);
static const COLORREF C_FAINT  = RGB(0x59, 0x60, 0x6E);
static const COLORREF C_BTN    = RGB(0x25, 0x2B, 0x36);
static const COLORREF C_BTNH   = RGB(0x31, 0x3A, 0x49);
static const COLORREF C_BTNP   = RGB(0x1E, 0x23, 0x2C);
static const COLORREF C_GRN    = RGB(0x4C, 0xD9, 0x64);
static const COLORREF C_AMB    = RGB(0xE8, 0xC0, 0x5F);
static const COLORREF C_RED    = RGB(0xE0, 0x5C, 0x5C);
static const COLORREF C_EDITBG = RGB(0x20, 0x24, 0x2E);

#define WM_APP_GOLD (WM_APP + 1)
#define WM_APP_ICON (WM_APP + 2)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static HINSTANCE g_hinst = NULL;
static HWND g_hwnd = NULL;
static Watcher g_watcher;
static Settings g_settings;
static std::wstring g_exeDir;

static std::mutex g_stateMu;
static int64_t g_total = -1;
static std::wstring g_formatted;
static std::wstring g_status;
static int g_statusKind = STATUS_WAITING;
static GoldBreakdown g_bd;

static bool g_paused = false;
static int g_hover = -1;
static int g_btnDown = -1;
static int g_dpi = 96;

static HICON g_icon32 = NULL;
static HICON g_icon16 = NULL;
static HICON g_iconTray = NULL;

// Checkbox check-state for the (owner-drawn) settings checkboxes. Pure
// BS_OWNERDRAW buttons don't retain check state via BM_*, so track here.
static bool g_chkRaw = false, g_chkMin = false, g_chkRun = false;

static HBRUSH g_brDlg = NULL;
static HBRUSH g_brEdit = NULL;
static HFONT g_fntUi = NULL;

enum View { VIEW_DASHBOARD = 0, VIEW_SETTINGS = 1 };
static View g_view = VIEW_DASHBOARD;

// Settings view child controls
enum {
    IDC_ED_FILE   = 2001, IDC_ED_ACCT = 2002, IDC_ED_OUT = 2003, IDC_ED_POLL = 2004,
    IDC_CHK_RAW   = 2005, IDC_CHK_MIN = 2006, IDC_CHK_RUN = 2007,
    IDC_BTN_BROWSE_GX = 2008, IDC_BTN_BROWSE_OUT = 2009, IDC_BTN_DETECT = 2010,
    IDC_BTN_SAVE  = 2011, IDC_BTN_BACK = 2012,
    IDC_LBL_FILE  = 2013, IDC_LBL_ACCT = 2014, IDC_LBL_OUT = 2015, IDC_LBL_POLL = 2016,
    IDC_ED_OUTPATH = 2017, IDC_BTN_COPY = 2018
};

// Forward declarations
static void ApplySettings();
static void UpdateOutputPathText();
static void ShowSettingsView();
static void HideSettingsView();
static void QuitApp();

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static int GetDpiOf(HWND h) {
    UINT d = GetDpiForWindow(h);
    return d ? (int)d : 96;
}

static void EnableDarkMode(HWND h) {
    BOOL on = TRUE;
    DwmSetWindowAttribute(h, 20, &on, sizeof(on)); // DWMWA_USE_IMMERSIVE_DARK_MODE
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------
static HFONT g_fCaption = NULL, g_fBig = NULL, g_fValue = NULL,
             g_fSub = NULL, g_fTiny = NULL, g_fBtn = NULL;
static int g_fontDpi = 0;

static HFONT MakeFontPt(int pt, int weight) {
    int px = (int)((pt * (double)g_dpi) / 72.0 + 0.5);
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void EnsureFonts(HWND hwnd) {
    int dpi = GetDpiOf(hwnd);
    if (g_fontDpi && g_fontDpi == dpi) return;
    g_dpi = dpi;
    if (g_fontDpi) {
        DeleteObject(g_fCaption); DeleteObject(g_fBig);
        DeleteObject(g_fValue);  DeleteObject(g_fSub);
        DeleteObject(g_fTiny);   DeleteObject(g_fBtn);
    }
    g_fCaption = MakeFontPt(9, FW_SEMIBOLD);
    g_fBig     = MakeFontPt(30, FW_BOLD);
    g_fValue   = MakeFontPt(15, FW_SEMIBOLD);
    g_fSub     = MakeFontPt(8, FW_NORMAL);
    g_fTiny    = MakeFontPt(8, FW_NORMAL);
    g_fBtn     = MakeFontPt(9, FW_NORMAL);
    g_fontDpi = dpi;
}

static std::wstring TimeHMS() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32] = L"";
    swprintf(buf, 32, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
struct Layout {
    RECT bigCard, bigCap, bigVal, bigSub;
    RECT cardA, cardALbl, cardAVal, cardATiny;
    RECT cardB, cardBLbl, cardBVal, cardBTiny;
    RECT statusL, statusR;
    RECT btnSet, btnPause, btnRefresh, btnOpen;
};
static Layout g_layout;

static void ComputeLayout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int dpi = GetDpiOf(hwnd);
    double s = dpi / 96.0;
    int W = rc.right, H = rc.bottom;
    int pad = (int)(14 * s);

    Layout L;
    L.bigCard = { pad, (int)(12 * s), W - pad, (int)(116 * s) };
    L.bigCap  = { L.bigCard.left + (int)(20 * s), L.bigCard.top + (int)(12 * s),
                  L.bigCard.right - (int)(20 * s), L.bigCard.top + (int)(30 * s) };
    L.bigVal  = { L.bigCap.left, L.bigCard.top + (int)(28 * s),
                  L.bigCap.right, L.bigCard.top + (int)(86 * s) };
    L.bigSub  = { L.bigCap.left, L.bigCard.top + (int)(88 * s),
                  L.bigCap.right, L.bigCard.bottom - (int)(6 * s) };

    int gap = (int)(12 * s);
    int cw  = (W - 2 * pad - gap) / 2;
    int cy  = L.bigCard.bottom + (int)(12 * s);
    L.cardA = { pad, cy, pad + cw, cy + (int)(76 * s) };
    L.cardB = { pad + cw + gap, cy, pad + cw + gap + cw, cy + (int)(76 * s) };
    L.cardALbl  = { L.cardA.left + (int)(16 * s), L.cardA.top + (int)(10 * s),
                    L.cardA.right - (int)(12 * s), L.cardA.top + (int)(26 * s) };
    L.cardAVal  = { L.cardALbl.left, L.cardA.top + (int)(24 * s),
                    L.cardA.right - (int)(12 * s), L.cardA.top + (int)(56 * s) };
    L.cardATiny = { L.cardALbl.left, L.cardA.top + (int)(56 * s),
                    L.cardA.right - (int)(12 * s), L.cardA.bottom - (int)(6 * s) };
    L.cardBLbl  = { L.cardB.left + (int)(16 * s), L.cardB.top + (int)(10 * s),
                    L.cardB.right - (int)(12 * s), L.cardB.top + (int)(26 * s) };
    L.cardBVal  = { L.cardALbl.left + (L.cardB.left - L.cardA.left), L.cardB.top + (int)(24 * s),
                    L.cardB.right - (int)(12 * s), L.cardB.top + (int)(56 * s) };
    L.cardBTiny = { L.cardBVal.left, L.cardB.top + (int)(56 * s),
                    L.cardB.right - (int)(12 * s), L.cardB.bottom - (int)(6 * s) };

    int sy = L.cardB.bottom + (int)(12 * s);
    L.statusL = { pad, sy + (int)(4 * s), W / 2, sy + (int)(24 * s) };
    L.statusR = { W / 2, L.statusL.top, W - pad, L.statusL.bottom };

    int by = H - (int)(12 * s) - (int)(30 * s);
    int bgap = (int)(8 * s);
    int bw = (W - 2 * pad - 3 * bgap) / 4;
    L.btnSet     = { pad, by, pad + bw, by + (int)(30 * s) };
    L.btnPause   = { pad + bw + bgap, by, pad + 2 * bw + bgap, by + (int)(30 * s) };
    L.btnRefresh = { pad + 2 * bw + 2 * bgap, by, pad + 3 * bw + 2 * bgap, by + (int)(30 * s) };
    L.btnOpen    = { pad + 3 * bw + 3 * bgap, by, pad + 4 * bw + 3 * bgap, by + (int)(30 * s) };
    g_layout = L;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static void DrawCard(HDC dc, const RECT& rc, COLORREF fill, COLORREF edge) {
    int r = std::max(6, std::min(16, (int)((rc.right - rc.left) * 0.06)));
    HRGN rg = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, r, r);
    HBRUSH fb = CreateSolidBrush(fill);
    FillRgn(dc, rg, fb);
    DeleteObject(fb);

    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
    DeleteObject(rg);
}

static void DrawButton(HDC dc, const RECT& rc, const wchar_t* text,
                       bool hover, bool pressed) {
    COLORREF bg = pressed ? C_BTNP : (hover ? C_BTNH : C_BTN);
    int r = 0;
    if (rc.bottom > rc.top) r = (rc.bottom - rc.top) / 2;
    HRGN rg = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, r, r);
    HBRUSH fb = CreateSolidBrush(bg);
    FillRgn(dc, rg, fb);
    DeleteObject(fb);

    HPEN pen = CreatePen(PS_SOLID, 1, hover ? C_GOLD : C_EDGE);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
    DeleteObject(rg);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, hover ? C_TXT : C_DIM);
    HGDIOBJ of = SelectObject(dc, g_fBtn);
    RECT rr = rc;
    DrawTextW(dc, text, -1, &rr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}

static void DrawTxt(HDC dc, RECT rc, const wchar_t* s, HFONT f,
                    COLORREF col, UINT fmt) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, col);
    HGDIOBJ of = SelectObject(dc, f);
    DrawTextW(dc, s, -1, &rc, fmt);
    SelectObject(dc, of);
}

static void DrawCheckbox(HDC dc, const RECT& rc, const wchar_t* text, bool checked) {
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    double s = g_dpi / 96.0;
    int boxSz = (int)(14 * s);
    int gap   = (int)(8 * s);
    int vy    = rc.top + (rc.bottom - rc.top - boxSz) / 2;
    RECT box  = { rc.left, vy, rc.left + boxSz, vy + boxSz };

    // Outer border
    HPEN pen = CreatePen(PS_SOLID, 1, checked ? C_GOLD : C_EDGE);
    HBRUSH br = CreateSolidBrush(checked ? C_EDITBG : C_EDITBG);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, br);
    RoundRect(dc, box.left, box.top, box.right, box.bottom, 3, 3);
    DeleteObject(SelectObject(dc, ob));
    SelectObject(dc, op);
    DeleteObject(pen);

    // Checkmark
    if (checked) {
        HPEN cp = CreatePen(PS_SOLID, (int)(2.0 * s), C_GOLD);
        HGDIOBJ ocp = SelectObject(dc, cp);
        MoveToEx(dc, box.left + (int)(3 * s), box.top + boxSz / 2, NULL);
        LineTo(dc, box.left + boxSz / 2 - (int)(1 * s), box.bottom - (int)(3 * s));
        MoveToEx(dc, box.left + boxSz / 2 - (int)(1 * s), box.bottom - (int)(3 * s), NULL);
        LineTo(dc, box.right - (int)(3 * s), box.top + (int)(3 * s));
        SelectObject(dc, ocp);
        DeleteObject(cp);
    }

    // Label text
    RECT trc = { box.right + gap, rc.top, rc.right, rc.bottom };
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, C_DIM);
    HGDIOBJ of = SelectObject(dc, g_fBtn);
    DrawTextW(dc, text, -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}

// ---------------------------------------------------------------------------
// Main window painting
// ---------------------------------------------------------------------------
static void PaintMain(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
    HGDIOBJ oldB = SelectObject(mem, bmp);

    EnsureFonts(hwnd);
    ComputeLayout(hwnd);

    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    GoldBreakdown bd;
    std::wstring formatted, status;
    int statusKind = STATUS_WAITING;
    bool paused = false;
    {
        std::lock_guard<std::mutex> lk(g_stateMu);
        bd = g_bd;
        formatted = g_formatted;
        status = g_status;
        statusKind = g_statusKind;
        paused = g_paused;
    }

    // Big card
    DrawCard(mem, g_layout.bigCard, C_PANEL, C_EDGE);
    DrawTxt(mem, g_layout.bigCap, L"ACCOUNT GOLD TOTAL", g_fCaption, C_DIM,
            DT_LEFT | DT_SINGLELINE);
    std::wstring big = formatted.empty() ? L"—" : formatted;
    DrawTxt(mem, g_layout.bigVal, big.c_str(), g_fBig, C_GOLD,
            DT_LEFT | DT_SINGLELINE);
    std::wstring sub;
    if (formatted.empty()) {
        sub = L"waiting for SavedVariables \x2026";
    } else {
        sub = L"raw: " + format_num(bd.total()) + L" copper    \x00b7    "
            + std::to_wstring(bd.chars.size())
            + (bd.chars.size() == 1 ? L" character" : L" characters");
    }
    DrawTxt(mem, g_layout.bigSub, sub.c_str(), g_fSub, C_FAINT,
            DT_LEFT | DT_SINGLELINE);

    // Characters card
    DrawCard(mem, g_layout.cardA, C_PANEL, C_EDGE);
    DrawTxt(mem, g_layout.cardALbl, L"CHARACTERS", g_fCaption, C_DIM,
            DT_LEFT | DT_SINGLELINE);
    std::wstring va = format_gold(bd.characters);
    if (va.empty()) va = L"0c";
    DrawTxt(mem, g_layout.cardAVal, va.c_str(), g_fValue, C_TXT,
            DT_LEFT | DT_SINGLELINE);
    std::wstring ta = std::to_wstring(bd.chars.size()) + L" tracked";
    DrawTxt(mem, g_layout.cardATiny, ta.c_str(), g_fTiny, C_FAINT,
            DT_LEFT | DT_SINGLELINE);

    // Warband card
    DrawCard(mem, g_layout.cardB, C_PANEL, C_EDGE);
    DrawTxt(mem, g_layout.cardBLbl, L"WARBAND BANK", g_fCaption, C_DIM,
            DT_LEFT | DT_SINGLELINE);
    std::wstring vb = format_gold(bd.warband);
    if (vb.empty()) vb = L"0c";
    DrawTxt(mem, g_layout.cardBVal, vb.c_str(), g_fValue, C_TXT,
            DT_LEFT | DT_SINGLELINE);
    DrawTxt(mem, g_layout.cardBTiny, L"shared account bank", g_fTiny, C_FAINT,
            DT_LEFT | DT_SINGLELINE);

    // Status row
    COLORREF dotCol = paused ? C_AMB
                     : (statusKind == STATUS_ERROR ? C_RED
                        : statusKind == STATUS_WAITING ? C_AMB : C_GRN);
    int dotSize = (int)(8.0 * g_dpi / 96.0);
    int midY = (g_layout.statusL.top + g_layout.statusL.bottom) / 2;
    HBRUSH dot = CreateSolidBrush(dotCol);
    HGDIOBJ op = SelectObject(mem, GetStockObject(NULL_PEN));
    HGDIOBJ obr = SelectObject(mem, dot);
    Ellipse(mem, g_layout.statusL.left, midY - dotSize / 2,
            g_layout.statusL.left + dotSize, midY + dotSize / 2);
    SelectObject(mem, obr);
    SelectObject(mem, op);
    DeleteObject(dot);

    RECT statusText = g_layout.statusL;
    statusText.left += dotSize + (int)(8.0 * g_dpi / 96.0);
    std::wstring statusLine = paused ? L"Paused" : (status.empty() ? L"Starting…" : status);
    DrawTxt(mem, statusText, statusLine.c_str(), g_fSub,
            paused ? C_AMB : C_DIM, DT_LEFT | DT_SINGLELINE);

    std::wstring right;
    if (!formatted.empty())
        right = L"updated " + TimeHMS();
    DrawTxt(mem, g_layout.statusR, right.c_str(), g_fSub, C_FAINT,
            DT_RIGHT | DT_SINGLELINE);

    // Buttons
    DrawButton(mem, g_layout.btnSet, L"Settings", g_hover == 0, g_btnDown == 0);
    DrawButton(mem, g_layout.btnPause, paused ? L"Resume" : L"Pause",
               g_hover == 1, g_btnDown == 1);
    DrawButton(mem, g_layout.btnRefresh, L"Refresh", g_hover == 3, g_btnDown == 3);
    DrawButton(mem, g_layout.btnOpen, L"Open output", g_hover == 2, g_btnDown == 2);

    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldB);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static void PaintSettingsBg(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, g_brDlg);
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
enum { ACT_NONE = -1, ACT_SETTINGS = 0, ACT_PAUSE = 1, ACT_OPEN = 2, ACT_REFRESH = 3 };

static int HitTest(int x, int y) {
    POINT p = { x, y };
    if (PtInRect(&g_layout.btnSet, p)) return ACT_SETTINGS;
    if (PtInRect(&g_layout.btnPause, p)) return ACT_PAUSE;
    if (PtInRect(&g_layout.btnRefresh, p)) return ACT_REFRESH;
    if (PtInRect(&g_layout.btnOpen, p)) return ACT_OPEN;
    return ACT_NONE;
}

static void TogglePause() {
    g_paused = !g_paused;
    g_watcher.setPaused(g_paused);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void DoRefresh() {
    {
        std::lock_guard<std::mutex> lk(g_stateMu);
        g_status = L"Refreshing\x2026";
        g_statusKind = STATUS_WATCHING;
    }
    InvalidateRect(g_hwnd, NULL, FALSE);
    g_watcher.refresh();
}

static void OpenOutputFile() {
    std::wstring path = g_settings.wc.output;
    if (path.empty()) path = default_output_file();
    std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(g_hwnd, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL);
}

static void QuitApp() {
    g_watcher.stop();
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyWindow(g_hwnd);
    PostQuitMessage(0);
}

// ---------------------------------------------------------------------------
// Tray
// ---------------------------------------------------------------------------
static void AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_ICON;
    nid.hIcon = g_iconTray;
    wcscpy_s(nid.szTip, L"GX Gold Monitor");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void UpdateTrayTip() {
    std::wstring tip;
    {
        std::lock_guard<std::mutex> lk(g_stateMu);
        if (g_paused)
            tip = L"GX Gold Monitor — paused";
        else if (g_formatted.empty())
            tip = L"GX Gold Monitor — waiting for SavedVariables";
        else
            tip = L"GX — " + g_formatted;
    }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    wcscpy_s(nid.szTip, tip.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void ShowTrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"Open GX Monitor");
    AppendMenuW(m, MF_STRING, 2, g_paused ? L"Resume" : L"Pause");
    AppendMenuW(m, MF_STRING, 3, L"Settings\x2026");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 4, L"Exit");

    SetForegroundWindow(g_hwnd);
    POINT pt;
    GetCursorPos(&pt);
    int id = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                            pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(m);

    switch (id) {
        case 1:
            ShowWindow(g_hwnd, SW_RESTORE);
            SetForegroundWindow(g_hwnd);
            break;
        case 2: TogglePause(); UpdateTrayTip(); break;
        case 3:
            ShowWindow(g_hwnd, SW_RESTORE);
            SetForegroundWindow(g_hwnd);
            ShowSettingsView();
            break;
        case 4: QuitApp(); break;
    }
}

static void OnTrayIcon(WPARAM, LPARAM lp) {
    switch (LOWORD(lp)) {
        case WM_LBUTTONDBLCLK:
            ShowWindow(g_hwnd, SW_RESTORE);
            SetForegroundWindow(g_hwnd);
            break;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            ShowTrayMenu();
            break;
    }
}

// ---------------------------------------------------------------------------
// Watcher -> UI callback
// ---------------------------------------------------------------------------
static void WatcherCallback(int64_t total, const std::wstring& formatted,
                            const std::wstring& status, int statusKind,
                            const GoldBreakdown& bd) {
    {
        std::lock_guard<std::mutex> lk(g_stateMu);
        if (total >= 0) {
            g_total = total;
            g_formatted = formatted;
            g_bd = bd;
        }
        g_status = status;
        g_statusKind = statusKind;
    }
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_GOLD, 0, 0);
}

// ---------------------------------------------------------------------------
// Settings view (embedded, responsive)
// ---------------------------------------------------------------------------
static HWND MakeChild(HWND parent, const wchar_t* cls, const wchar_t* text,
                      DWORD style, DWORD exstyle, int id, LPRECT r, HFONT font,
                      bool visible) {
    DWORD st = style | WS_CHILD | (visible ? WS_VISIBLE : 0);
    HWND h = CreateWindowExW(exstyle, cls, text ? text : L"", st,
                             r->left, r->top, r->right - r->left, r->bottom - r->top,
                             parent, (HMENU)(INT_PTR)id, g_hinst, NULL);
    if (h) {
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
        if (id) SetWindowLongPtrW(h, GWLP_ID, id);
    }
    return h;
}

static std::wstring BrowseOpen(HWND owner, bool mustExist) {
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    wchar_t file[MAX_PATH] = L"";
    ofn.lpstrFilter = L"All files\0*.*\0Lua files (*.lua)\0*.lua\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = mustExist ? L"Select GX.lua" : L"Output text file (for OBS)";
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY |
                (mustExist ? (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST) : 0);
    if (GetOpenFileNameW(&ofn)) return std::wstring(file);
    return std::wstring();
}

static void PopulateSettings() {
    HWND h = g_hwnd;
    wchar_t buf[1024] = L"";
    SetWindowTextW(GetDlgItem(h, IDC_ED_FILE), g_settings.wc.file.c_str());
    SetWindowTextW(GetDlgItem(h, IDC_ED_OUT), g_settings.wc.output.c_str());
    SetWindowTextW(GetDlgItem(h, IDC_ED_ACCT), g_settings.wc.account.c_str());
    swprintf(buf, 1024, L"%g", g_settings.wc.pollSeconds);
    SetWindowTextW(GetDlgItem(h, IDC_ED_POLL), buf);
    g_chkRaw = g_settings.wc.raw;
    g_chkMin = g_settings.startMinimized;
    g_chkRun = run_at_startup_enabled();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void ApplySettings() {
    HWND h = g_hwnd;
    wchar_t buf[1024] = L"";
    WatchConfig wc = g_settings.wc;

    GetWindowTextW(GetDlgItem(h, IDC_ED_FILE), buf, 1024);  wc.file = buf;
    GetWindowTextW(GetDlgItem(h, IDC_ED_OUT), buf, 1024);   wc.output = buf;
    GetWindowTextW(GetDlgItem(h, IDC_ED_ACCT), buf, 1024);  wc.account = buf;
    GetWindowTextW(GetDlgItem(h, IDC_ED_POLL), buf, 1024);  wc.pollSeconds = wcstod(buf, NULL);
    if (!(wc.pollSeconds >= 0.5)) wc.pollSeconds = 2.0;
    wc.raw = g_chkRaw;
    g_settings.startMinimized = g_chkMin;
    bool run = g_chkRun;

    if (wc.output.empty()) wc.output = default_output_file();
    g_settings.wc = wc;

    g_watcher.setConfig(wc);
    save_settings(g_settings);
    run_at_startup(run);

    UpdateOutputPathText();
    InvalidateRect(g_hwnd, NULL, TRUE);
    UpdateTrayTip();
}

static void PositionSettingsControls(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int dpi = GetDpiOf(hwnd);
    double s = dpi / 96.0;
    int W = rc.right, H = rc.bottom;
    int pad = (int)(14 * s);

    double fy = std::clamp((double)(H - 40 * s) / (320 * s), 0.7, 1.0);
    int rowH = (int)((14 + 4 + 24 + 6) * s * fy);

    int backH = (int)(28 * s);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_BACK), pad, (int)(10 * s),
               (int)(72 * s), backH, TRUE);

    int y = (int)(10 * s) + backH + (int)(8 * s);

    auto Row = [&](int lbl, int ed, int btn, int btnW) {
        MoveWindow(GetDlgItem(hwnd, lbl), pad, y, W - 2 * pad, (int)(14 * s), TRUE);
        int ey = y + (int)(18 * s);
        int ew = W - 2 * pad;
        if (btn) ew = W - 2 * pad - btnW - (int)(10 * s);
        MoveWindow(GetDlgItem(hwnd, ed), pad, ey, ew, (int)(24 * s), TRUE);
        if (btn) MoveWindow(GetDlgItem(hwnd, btn), pad + ew + (int)(10 * s),
                            ey, btnW, (int)(24 * s), TRUE);
        y += rowH;
    };

    Row(IDC_LBL_FILE, IDC_ED_FILE, IDC_BTN_BROWSE_GX, (int)(88 * s));
    Row(IDC_LBL_ACCT, IDC_ED_ACCT, IDC_BTN_DETECT, (int)(110 * s));
    Row(IDC_LBL_OUT, IDC_ED_OUT, IDC_BTN_BROWSE_OUT, (int)(88 * s));
    Row(IDC_LBL_POLL, IDC_ED_POLL, 0, 0);

    MoveWindow(GetDlgItem(hwnd, IDC_CHK_RAW), pad, y, W - 2 * pad, (int)(22 * s), TRUE);
    y += (int)(26 * s);
    MoveWindow(GetDlgItem(hwnd, IDC_CHK_MIN), pad, y, W - 2 * pad, (int)(22 * s), TRUE);
    y += (int)(26 * s);
    MoveWindow(GetDlgItem(hwnd, IDC_CHK_RUN), pad, y, W - 2 * pad, (int)(22 * s), TRUE);
    y += (int)(30 * s);

    int bw = (int)(110 * s), bh = (int)(30 * s);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_SAVE), W - pad - bw, H - pad - bh, bw, bh, TRUE);
}

// ---------------------------------------------------------------------------
// OBS output path row (dashboard)
// ---------------------------------------------------------------------------
static std::wstring GetOutputPath() {
    std::wstring p = g_settings.wc.output;
    if (p.empty()) p = default_output_file();
    return p;
}

static void UpdateOutputPathText() {
    if (!g_hwnd) return;
    HWND hEd = GetDlgItem(g_hwnd, IDC_ED_OUTPATH);
    if (hEd) SetWindowTextW(hEd, GetOutputPath().c_str());
}

static void CopyOutputPath() {
    std::wstring path = GetOutputPath();
    if (path.empty()) return;
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    size_t bytes = (path.size() + 1) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hg) {
        wchar_t* p = (wchar_t*)GlobalLock(hg);
        wcscpy_s(p, path.size() + 1, path.c_str());
        GlobalUnlock(hg);
        SetClipboardData(CF_UNICODETEXT, hg);
    }
    CloseClipboard();
}

static void PositionOutputPath(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int dpi = GetDpiOf(hwnd);
    double s = dpi / 96.0;
    int W = rc.right, H = rc.bottom;
    int pad = (int)(14 * s);

    int btnW = (int)(70 * s);
    int hh   = (int)(24 * s);
    int by   = H - (int)(12 * s) - (int)(30 * s);   // button row top
    int y    = by - (int)(8 * s) - hh;

    HWND hEd  = GetDlgItem(hwnd, IDC_ED_OUTPATH);
    HWND hBtn = GetDlgItem(hwnd, IDC_BTN_COPY);
    if (hEd)  MoveWindow(hEd,  pad, y, W - 2 * pad - btnW - (int)(8 * s), hh, TRUE);
    if (hBtn) MoveWindow(hBtn, W - pad - btnW, y, btnW, hh, TRUE);
}

static void ShowSettingsChildren(int show) {
    const int ids[] = {
        IDC_BTN_BACK, IDC_LBL_FILE, IDC_ED_FILE, IDC_BTN_BROWSE_GX,
        IDC_LBL_ACCT, IDC_ED_ACCT, IDC_BTN_DETECT,
        IDC_LBL_OUT, IDC_ED_OUT, IDC_BTN_BROWSE_OUT,
        IDC_LBL_POLL, IDC_ED_POLL,
        IDC_CHK_RAW, IDC_CHK_MIN, IDC_CHK_RUN, IDC_BTN_SAVE
    };
    for (int id : ids)
        ShowWindow(GetDlgItem(g_hwnd, id), show ? SW_SHOW : SW_HIDE);
}

static void ShowSettingsView() {
    if (g_view == VIEW_SETTINGS) return;
    g_view = VIEW_SETTINGS;
    PopulateSettings();
    PositionSettingsControls(g_hwnd);
    ShowSettingsChildren(TRUE);
    ShowWindow(GetDlgItem(g_hwnd, IDC_ED_OUTPATH), SW_HIDE);
    ShowWindow(GetDlgItem(g_hwnd, IDC_BTN_COPY), SW_HIDE);
    SendMessageW(g_hwnd, DM_SETDEFID, IDC_BTN_SAVE, 0);
    g_hover = g_btnDown = ACT_NONE;
    SetFocus(GetDlgItem(g_hwnd, IDC_ED_FILE));
    InvalidateRect(g_hwnd, NULL, TRUE);
}

static void HideSettingsView() {
    if (g_view != VIEW_SETTINGS) return;
    g_view = VIEW_DASHBOARD;
    ShowSettingsChildren(FALSE);
    ShowWindow(GetDlgItem(g_hwnd, IDC_ED_OUTPATH), SW_SHOW);
    ShowWindow(GetDlgItem(g_hwnd, IDC_BTN_COPY), SW_SHOW);
    UpdateOutputPathText();
    PositionOutputPath(g_hwnd);
    g_hover = g_btnDown = ACT_NONE;
    SetFocus(g_hwnd);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

// ---------------------------------------------------------------------------
// Main window proc
// ---------------------------------------------------------------------------
static void OnGoldUpdate() {
    UpdateTrayTip();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static LRESULT CALLBACK GoldWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        EnableDarkMode(hwnd);
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_icon32);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_icon16);
        AddTrayIcon();

        RECT r{ 0, 0, 100, 24 };
        MakeChild(hwnd, L"BUTTON", L"\x2039 Back", WS_TABSTOP | BS_OWNERDRAW, 0,
                  IDC_BTN_BACK, &r, g_fntUi, false);
        MakeChild(hwnd, L"STATIC",
                  L"SAVED VARIABLES \x2014 GX.lua  (empty = auto-detect)",
                  SS_LEFT, 0, IDC_LBL_FILE, &r, g_fntUi, false);
        HWND hEd = MakeChild(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
                  IDC_ED_FILE, &r, g_fntUi, false);
        if (hEd) SetWindowTheme(hEd, L"DarkMode_Explorer", NULL);
        MakeChild(hwnd, L"BUTTON", L"Browse", WS_TABSTOP | BS_OWNERDRAW, 0,
                  IDC_BTN_BROWSE_GX, &r, g_fntUi, false);
        MakeChild(hwnd, L"STATIC",
                  L"ACCOUNT FILTER  (optional, e.g. 410566417#1)",
                  SS_LEFT, 0, IDC_LBL_ACCT, &r, g_fntUi, false);
        hEd = MakeChild(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
                  IDC_ED_ACCT, &r, g_fntUi, false);
        if (hEd) SetWindowTheme(hEd, L"DarkMode_Explorer", NULL);
        MakeChild(hwnd, L"BUTTON", L"Auto-detect", WS_TABSTOP | BS_OWNERDRAW, 0,
                  IDC_BTN_DETECT, &r, g_fntUi, false);
        MakeChild(hwnd, L"STATIC",
                  L"OUTPUT TEXT FILE  (OBS reads this)",
                  SS_LEFT, 0, IDC_LBL_OUT, &r, g_fntUi, false);
        hEd = MakeChild(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
                  IDC_ED_OUT, &r, g_fntUi, false);
        if (hEd) SetWindowTheme(hEd, L"DarkMode_Explorer", NULL);
        MakeChild(hwnd, L"BUTTON", L"Browse", WS_TABSTOP | BS_OWNERDRAW, 0,
                  IDC_BTN_BROWSE_OUT, &r, g_fntUi, false);
        MakeChild(hwnd, L"STATIC",
                  L"POLL INTERVAL SECONDS  (default 2, min 0.5)",
                  SS_LEFT, 0, IDC_LBL_POLL, &r, g_fntUi, false);
        hEd = MakeChild(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
                  IDC_ED_POLL, &r, g_fntUi, false);
        if (hEd) SetWindowTheme(hEd, L"DarkMode_Explorer", NULL);
        MakeChild(hwnd, L"BUTTON", L"Raw numeric output (bare copper number)",
                  WS_TABSTOP | BS_OWNERDRAW, 0, IDC_CHK_RAW, &r, g_fntUi, false);
        MakeChild(hwnd, L"BUTTON", L"Start minimized to tray",
                  WS_TABSTOP | BS_OWNERDRAW, 0, IDC_CHK_MIN, &r, g_fntUi, false);
        MakeChild(hwnd, L"BUTTON", L"Run at Windows startup",
                  WS_TABSTOP | BS_OWNERDRAW, 0, IDC_CHK_RUN, &r, g_fntUi, false);
        MakeChild(hwnd, L"BUTTON", L"Save && Apply", WS_TABSTOP | BS_OWNERDRAW, 0,
                  IDC_BTN_SAVE, &r, g_fntUi, false);

        hEd = MakeChild(hwnd, L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
                  IDC_ED_OUTPATH, &r, g_fntUi, true);
        if (hEd) SetWindowTheme(hEd, L"DarkMode_Explorer", NULL);
        MakeChild(hwnd, L"BUTTON", L"Copy", WS_TABSTOP | BS_OWNERDRAW, 0,
                  IDC_BTN_COPY, &r, g_fntUi, true);
        UpdateOutputPathText();

        ComputeLayout(hwnd);
        return 0;
    }

    case WM_PAINT:
        if (g_view == VIEW_SETTINGS) PaintSettingsBg(hwnd);
        else PaintMain(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, C_TXT);
        SetBkColor((HDC)wp, C_EDITBG);
        return (LRESULT)g_brEdit;

    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, C_DIM);
        SetBkColor((HDC)wp, C_BG);
        return (LRESULT)g_brDlg;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* ds = (DRAWITEMSTRUCT*)lp;
        if (ds->CtlType != ODT_BUTTON) break;
        int id = (int)ds->CtlID;
        wchar_t txt[64] = L"";
        GetWindowTextW(ds->hwndItem, txt, 64);
        bool isChk = (id == IDC_CHK_RAW || id == IDC_CHK_MIN || id == IDC_CHK_RUN);
        if (isChk) {
            bool checked = id == IDC_CHK_RAW ? g_chkRaw :
                           id == IDC_CHK_MIN ? g_chkMin : g_chkRun;
            DrawCheckbox(ds->hDC, ds->rcItem, txt, checked);
        } else {
            HBRUSH bb = CreateSolidBrush(C_BG);
            FillRect(ds->hDC, &ds->rcItem, bb);
            DeleteObject(bb);
            bool pressed = (ds->itemState & ODS_SELECTED) != 0;
            DrawButton(ds->hDC, ds->rcItem, txt, pressed, pressed);
        }
        return TRUE;
    }

    case WM_SIZE:
        ComputeLayout(hwnd);
        PositionSettingsControls(hwnd);
        PositionOutputPath(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        int dpi = GetDpiOf(hwnd);
        mm->ptMinTrackSize.x = (LONG)(380.0 * dpi / 96.0);
        mm->ptMinTrackSize.y = (LONG)(420.0 * dpi / 96.0);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (HIWORD(wp) != BN_CLICKED) break;
        switch (id) {
        case IDC_BTN_BACK:
            HideSettingsView();
            break;
        case IDC_BTN_SAVE:
            ApplySettings();
            HideSettingsView();
            break;
        case IDC_BTN_BROWSE_GX: {
            std::wstring f = BrowseOpen(g_hwnd, true);
            if (!f.empty()) SetWindowTextW(GetDlgItem(g_hwnd, IDC_ED_FILE), f.c_str());
            break;
        }
        case IDC_BTN_DETECT: {
            wchar_t acct[1024] = L"", out[1024] = L"";
            GetWindowTextW(GetDlgItem(g_hwnd, IDC_ED_ACCT), acct, 1024);
            std::wstring f = auto_discover(g_exeDir, L"", acct);
            SetWindowTextW(GetDlgItem(g_hwnd, IDC_ED_FILE), f.c_str());
            GetWindowTextW(GetDlgItem(g_hwnd, IDC_ED_OUT), out, 1024);
            if (out[0] == L'\0') {
                SetWindowTextW(GetDlgItem(g_hwnd, IDC_ED_OUT), default_output_file().c_str());
            }
            break;
        }
        case IDC_BTN_BROWSE_OUT: {
            std::wstring f = BrowseOpen(g_hwnd, false);
            if (!f.empty()) SetWindowTextW(GetDlgItem(g_hwnd, IDC_ED_OUT), f.c_str());
            break;
        }
        case IDC_BTN_COPY:
            CopyOutputPath();
            break;
        case IDC_CHK_RAW:
        case IDC_CHK_MIN:
        case IDC_CHK_RUN: {
            bool* p = id == IDC_CHK_RAW ? &g_chkRaw :
                      id == IDC_CHK_MIN ? &g_chkMin : &g_chkRun;
            *p = !*p;
            InvalidateRect(GetDlgItem(hwnd, id), NULL, FALSE);
            break;
        }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_view == VIEW_SETTINGS) return 0;
        int h = HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (h != g_hover) {
            g_hover = h;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_hover != -1 || g_btnDown != -1) {
            g_hover = -1;
            g_btnDown = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        if (g_view == VIEW_SETTINGS) return 0;
        int h = HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (h != ACT_NONE) {
            g_btnDown = h;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_view == VIEW_SETTINGS) return 0;
        if (g_btnDown != ACT_NONE) {
            int down = g_btnDown;
            g_btnDown = ACT_NONE;
            ReleaseCapture();
            if (HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)) == down) {
                switch (down) {
                case ACT_SETTINGS: ShowSettingsView(); break;
                case ACT_PAUSE: TogglePause(); UpdateTrayTip(); break;
                case ACT_REFRESH: DoRefresh(); break;
                case ACT_OPEN: OpenOutputFile(); break;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        g_btnDown = ACT_NONE;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_APP_GOLD:
        OnGoldUpdate();
        return 0;

    case WM_APP_ICON:
        OnTrayIcon(wp, lp);
        return 0;

    case WM_DPICHANGED: {
        RECT* pr = (RECT*)lp;
        SetWindowPos(hwnd, NULL, pr->left, pr->top,
                     pr->right - pr->left, pr->bottom - pr->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------
int RunApp(HINSTANCE hinst) {
    g_hinst = hinst;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    g_exeDir = get_exe_dir();
    load_settings(g_settings);

    if (g_settings.wc.file.empty()) {
        std::wstring f = auto_discover(g_exeDir, g_settings.wc.wowRoot, g_settings.wc.account);
        if (!f.empty()) g_settings.wc.file = f;
    }

    g_dpi = GetDpiForSystem();
    g_icon32 = (HICON)LoadImageW(hinst, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                 32, 32, LR_DEFAULTCOLOR);
    g_icon16 = (HICON)LoadImageW(hinst, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                 16, 16, LR_DEFAULTCOLOR);
    g_iconTray = g_icon16;

    g_brDlg = CreateSolidBrush(C_BG);
    g_brEdit = CreateSolidBrush(C_EDITBG);
    g_fntUi = MakeFontPt(9, FW_NORMAL);

    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = GoldWndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = g_icon32;
    wc.lpszClassName = L"GXGoldWnd";
    RegisterClassW(&wc);

    double s = g_dpi / 96.0;
    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"GXGoldWnd", L"GX — Gold Export",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             (int)(520 * s), (int)(470 * s),
                             NULL, NULL, hinst, NULL);
    if (!g_hwnd) return 1;

    g_watcher.start(g_exeDir, g_settings.wc, WatcherCallback);

    if (g_settings.startMinimized) {
        ShowWindow(g_hwnd, SW_HIDE);
    } else {
        ShowWindow(g_hwnd, SW_SHOW);
        UpdateWindow(g_hwnd);
    }

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (g_view == VIEW_SETTINGS && IsDialogMessageW(g_hwnd, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    g_watcher.stop();
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);

    if (g_icon32) DestroyIcon(g_icon32);
    if (g_icon16) DestroyIcon(g_icon16);
    if (g_brDlg) DeleteObject(g_brDlg);
    if (g_brEdit) DeleteObject(g_brEdit);
    if (g_fntUi) DeleteObject(g_fntUi);
    if (g_fontDpi) {
        DeleteObject(g_fCaption); DeleteObject(g_fBig);
        DeleteObject(g_fValue); DeleteObject(g_fSub);
        DeleteObject(g_fTiny); DeleteObject(g_fBtn);
    }
    return (int)m.wParam;
}