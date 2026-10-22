// PortKiller - native Win32 port/process manager.
#include <windows.h>
#include <commctrl.h>
#include <iphlpapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ports.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

using namespace Gdiplus;

// ---------------------------------------------------------------- palette
// Monochrome: every colour collapses to its luminance, so the UI is pure black/white.
#define RGBCOL(r, g, b) Color(255, (BYTE)(((r) * 30 + (g) * 59 + (b) * 11) / 100),                                   (BYTE)(((r) * 30 + (g) * 59 + (b) * 11) / 100),                                   (BYTE)(((r) * 30 + (g) * 59 + (b) * 11) / 100))
static const Color kBg = RGBCOL(10, 10, 10);
static const Color kPanel = RGBCOL(17, 17, 17);
static const Color kPanel2 = RGBCOL(24, 24, 24);
static const Color kPanel3 = RGBCOL(34, 34, 34);
static const Color kBorder = RGBCOL(44, 44, 44);
static const Color kText = RGBCOL(244, 244, 244);
static const Color kMuted = RGBCOL(150, 150, 150);
static const Color kFaint = RGBCOL(100, 100, 100);
static const Color kAccent = RGBCOL(255, 255, 255);
static const Color kAccentSoft = Color(30, 255, 255, 255);
static const Color kGreen = RGBCOL(255, 255, 255);
static const Color kRed = RGBCOL(255, 255, 255);
static const Color kAmber = RGBCOL(168, 168, 168);
static const Color kSel = RGBCOL(38, 38, 38);
static const Color kSelInactive = RGBCOL(28, 28, 28);

static inline COLORREF Cr(const Color& c) { return RGB(c.GetR(), c.GetG(), c.GetB()); }

// ---------------------------------------------------------------- state
enum BtnId { B_REFRESH, B_KILL, B_TREE, B_EXPORT, B_ELEVATE, B_ALL, B_TCP, B_UDP, B_LISTEN, B_AUTO, B_COUNT };
enum BtnKind { BK_NORMAL, BK_PRIMARY, BK_DANGER, BK_GHOST, BK_TOGGLE };

struct BtnState {
    HWND hwnd = nullptr;
    BtnKind kind = BK_NORMAL;
    bool hover = false;
    bool checked = false;
    const wchar_t* label = L"";
};

static HINSTANCE g_inst;
static HWND g_hwnd, g_list, g_search;
static HBRUSH g_editBrush;
static UINT g_dpi = 96;
static HFONT g_fontUI, g_fontHeader;
static Font *g_fUI, *g_fUISemi, *g_fSmall;

static BtnState g_btn[B_COUNT];

static std::vector<PortConn> g_all;
static std::vector<int> g_view;
static ProcMap g_procs;
static std::wstring g_filter;      // lower-cased
static int g_sortCol = 0;
static bool g_sortAsc = true;
static int g_protoFilter = 0;      // 0 = all, 6 = TCP, 17 = UDP
static bool g_listenOnly = false;
static bool g_auto = false;
static int g_refreshMs = 0;
static int g_listening = 0;
static int g_processCount = 0;
static std::wstring g_flash;
static ULONGLONG g_flashUntil = 0;
static ULONGLONG g_qpcFreq = 1;

static RECT g_rcSearch, g_rcList, g_rcStatus;

static const int kColCount = 7;
struct ColumnDef { const wchar_t* name; int width; int fmt; };
static ColumnDef kCols[kColCount] = {
    {L"PORT", 78, LVCFMT_RIGHT},
    {L"PROTO", 70, LVCFMT_LEFT},
    {L"LOCAL ADDRESS", 190, LVCFMT_LEFT},
    {L"REMOTE ADDRESS", 190, LVCFMT_LEFT},
    {L"STATE", 118, LVCFMT_LEFT},
    {L"PID", 76, LVCFMT_RIGHT},
    {L"PROCESS", 210, LVCFMT_LEFT},
};

static int S(int v) { return MulDiv(v, (int)g_dpi, 96); }

static std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

static bool StartsWithI(const std::wstring& hay, const std::wstring& needle) {
    return hay.size() >= needle.size() && _wcsnicmp(hay.c_str(), needle.c_str(), needle.size()) == 0;
}

static std::wstring FormatBytes(ULONGLONG bytes) {
    wchar_t buf[64];
    if (bytes >= (1ull << 30)) _snwprintf_s(buf, _TRUNCATE, L"%.1f GB", bytes / (double)(1ull << 30));
    else if (bytes >= (1ull << 20)) _snwprintf_s(buf, _TRUNCATE, L"%.1f MB", bytes / (double)(1ull << 20));
    else _snwprintf_s(buf, _TRUNCATE, L"%.0f KB", bytes / 1024.0);
    return buf;
}

// ---------------------------------------------------------------- gdi helpers
static void RoundedPath(GraphicsPath& p, const RectF& r, REAL rad) {
    if (rad <= 0.0f) { p.AddRectangle(r); return; }
    REAL d = rad * 2.0f;
    p.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
}

static void FillRound(Graphics& g, const RECT& rc, REAL rad, const Color& c) {
    GraphicsPath p;
    RectF r((REAL)rc.left, (REAL)rc.top, (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top));
    RoundedPath(p, r, rad);
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

static void StrokeRound(Graphics& g, const RECT& rc, REAL rad, const Color& c, REAL w = 1.0f) {
    GraphicsPath p;
    RectF r((REAL)rc.left + 0.5f, (REAL)rc.top + 0.5f, (REAL)(rc.right - rc.left) - 1.0f, (REAL)(rc.bottom - rc.top) - 1.0f);
    RoundedPath(p, r, rad);
    Pen pen(c, w);
    g.DrawPath(&pen, &p);
}

static void DrawTextC(Graphics& g, const std::wstring& s, Font* font, const RECT& rc, const Color& c,
                      StringAlignment align = StringAlignmentCenter, const RECT* clip = nullptr) {
    StringFormat fmt;
    fmt.SetAlignment(align);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(c);
    RectF r((REAL)rc.left, (REAL)rc.top, (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top));
    if (clip) {
        Region reg(RectF((REAL)clip->left, (REAL)clip->top, (REAL)(clip->right - clip->left), (REAL)(clip->bottom - clip->top)));
        g.SetClip(&reg);
    }
    g.DrawString(s.c_str(), (INT)s.size(), font, r, &fmt, &b);
    if (clip) g.ResetClip();
}

// ---------------------------------------------------------------- fonts
static HFONT MakeGdiFont(int px, int weight) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void RecreateFonts() {
    if (g_fontUI) DeleteObject(g_fontUI);
    if (g_fontHeader) DeleteObject(g_fontHeader);
    g_fontUI = MakeGdiFont(S(14), FW_NORMAL);
    g_fontHeader = MakeGdiFont(S(12), FW_SEMIBOLD);

    delete g_fUI;
    delete g_fUISemi;
    delete g_fSmall;
    g_fUI = new Font(L"Segoe UI", (REAL)S(14), FontStyleRegular, UnitPixel);
    g_fUISemi = new Font(L"Segoe UI", (REAL)S(13), FontStyleBold, UnitPixel);
    g_fSmall = new Font(L"Segoe UI", (REAL)S(12), FontStyleRegular, UnitPixel);

    if (g_editBrush) DeleteObject(g_editBrush);
    g_editBrush = CreateSolidBrush(Cr(kPanel2));

    if (g_list) {
        SendMessageW(g_list, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        HWND hdr = ListView_GetHeader(g_list);
        if (hdr) SendMessageW(hdr, WM_SETFONT, (WPARAM)g_fontHeader, TRUE);
        HIMAGELIST il = ImageList_Create(1, S(28), ILC_COLOR32, 1, 1);
        ListView_SetImageList(g_list, il, LVSIL_SMALL); // controls row height
    }
    if (g_search) SendMessageW(g_search, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
}

// ---------------------------------------------------------------- buttons
static void SetChecked(BtnId id, bool on) {
    if (g_btn[id].checked == on) return;
    g_btn[id].checked = on;
    InvalidateRect(g_btn[id].hwnd, nullptr, TRUE);
    UpdateWindow(g_btn[id].hwnd);
}

static void EnableBtn(BtnId id, bool on) {
    if (!g_btn[id].hwnd) return;
    if ((GetWindowLongPtrW(g_btn[id].hwnd, GWL_STYLE) & WS_DISABLED) == (on ? WS_DISABLED : 0)) return;
    EnableWindow(g_btn[id].hwnd, on);
    InvalidateRect(g_btn[id].hwnd, nullptr, TRUE);
}

static void DrawButton(HDC hdc, const DRAWITEMSTRUCT* dis) {
    if (dis->CtlID >= B_COUNT) return;
    BtnState& b = g_btn[dis->CtlID];
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0;

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    RECT rc = dis->rcItem;
    REAL rad = (REAL)S(9);

    // start from the page background so rounded corners blend in
    SolidBrush bg(kBg);
    g.FillRectangle(&bg, RectF((REAL)rc.left, (REAL)rc.top, (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top)));

    Color fill, line, text;
    bool outline = true;
    switch (b.kind) {
        case BK_PRIMARY:
            fill = pressed ? RGBCOL(198, 198, 198) : (b.hover ? RGBCOL(238, 238, 238) : kText);
            line = fill;
            text = RGBCOL(12, 12, 12);
            break;
        case BK_DANGER:
            fill = pressed ? RGBCOL(198, 198, 198) : (b.hover ? RGBCOL(238, 238, 238) : kText);
            line = fill;
            text = RGBCOL(12, 12, 12);
            break;
        case BK_GHOST:
            fill = (pressed || b.hover) ? kPanel2 : kBg;
            line = (pressed || b.hover) ? kBorder : kBg;
            text = b.hover ? kText : kMuted;
            break;
        case BK_TOGGLE:
            if (b.checked) {
                fill = pressed ? RGBCOL(198, 198, 198) : (b.hover ? RGBCOL(238, 238, 238) : kText);
                line = fill;
                text = RGBCOL(12, 12, 12);
            } else {
                fill = (pressed || b.hover) ? kPanel3 : kPanel2;
                line = kBorder;
                text = b.hover ? kText : kMuted;
            }
            break;
        default:
            fill = pressed ? RGBCOL(14, 14, 14) : (b.hover ? kPanel3 : kPanel2);
            line = kBorder;
            text = kText;
            break;
    }
    if (disabled) {
        fill = kPanel2;
        line = kBorder;
        text = kFaint;
    }

    FillRound(g, rc, rad, fill);
    if (outline) StrokeRound(g, rc, rad, line);

    RECT textRc = rc;
    if (dis->CtlID == B_ELEVATE) {
        REAL cy = (REAL)(rc.top + rc.bottom) / 2.0f;
        REAL cx = (REAL)rc.left + (REAL)S(16);
        SolidBrush dot(IsAdmin() ? kGreen : kAmber);
        g.FillEllipse(&dot, (REAL)(cx - S(3)), (REAL)(cy - S(3)), (REAL)S(6), (REAL)S(6));
        textRc.left += S(18);
    }
    DrawTextC(g, b.label, g_fUISemi, textRc, text);

    if (focused) {
        RECT inner = rc;
        InflateRect(&inner, -S(3), -S(3));
        StrokeRound(g, inner, (REAL)S(6), Color(90, 255, 255, 255));
    }
}

static LRESULT CALLBACK BtnSubProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) {
    BtnId id = (BtnId)ref;
    switch (msg) {
        case WM_MOUSEMOVE:
            if (!g_btn[id].hover) {
                g_btn[id].hover = true;
                InvalidateRect(h, nullptr, TRUE);
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
            }
            break;
        case WM_MOUSELEAVE:
            g_btn[id].hover = false;
            InvalidateRect(h, nullptr, TRUE);
            break;
        case WM_LBUTTONDOWN:
            SetFocus(h);
            break;
    }
    return DefSubclassProc(h, msg, w, l);
}

static void AddButton(BtnId id, const wchar_t* label, BtnKind kind) {
    HWND h = CreateWindowExW(0, L"BUTTON", label,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                             0, 0, 10, 10, g_hwnd, (HMENU)(INT_PTR)id, g_inst, nullptr);
    g_btn[id].hwnd = h;
    g_btn[id].kind = kind;
    g_btn[id].label = label;
    SetWindowSubclass(h, BtnSubProc, 1, (DWORD_PTR)id);
}

// ---------------------------------------------------------------- layout
static void Place(HWND h, int x, int y, int w, int hgt) {
    if (w < 1) w = 1;
    if (hgt < 1) hgt = 1;
    MoveWindow(h, x, y, w, hgt, TRUE);
}

static void LayoutUI(int w, int h) {
    const int pad = S(12), gap = S(8), bh = S(32);
    int y = pad;

    int right = w - pad;
    int autoW = S(78), listenW = S(120), segW = S(52);
    right -= autoW;
    Place(g_btn[B_AUTO].hwnd, right, y, autoW, bh);
    right -= gap + listenW;
    Place(g_btn[B_LISTEN].hwnd, right, y, listenW, bh);
    right -= gap + segW * 3 + S(4) * 2;
    int sx = right;
    Place(g_btn[B_ALL].hwnd, sx, y, segW, bh);
    sx += segW + S(4);
    Place(g_btn[B_TCP].hwnd, sx, y, segW, bh);
    sx += segW + S(4);
    Place(g_btn[B_UDP].hwnd, sx, y, segW, bh);

    g_rcSearch = RECT{pad, y, right - gap, y + bh};
    if (g_rcSearch.right < g_rcSearch.left + S(160)) g_rcSearch.right = g_rcSearch.left + S(160);
    Place(g_search, g_rcSearch.left + S(34), y + S(8),
          (g_rcSearch.right - g_rcSearch.left) - S(34) - S(10), bh - S(16));

    y += bh + gap;
    int x = pad;
    Place(g_btn[B_REFRESH].hwnd, x, y, S(96), bh); x += S(96) + gap;
    Place(g_btn[B_KILL].hwnd, x, y, S(84), bh);    x += S(84) + gap;
    Place(g_btn[B_TREE].hwnd, x, y, S(104), bh);   x += S(104) + gap;
    Place(g_btn[B_EXPORT].hwnd, x, y, S(104), bh);
    Place(g_btn[B_ELEVATE].hwnd, w - pad - S(196), y, S(196), bh);

    int statusH = S(26);
    g_rcStatus = RECT{0, h - statusH, w, h};
    int top = y + bh + gap;
    int bottom = h - statusH;
    if (bottom < top + S(40)) bottom = top + S(40);
    g_rcList = RECT{pad, top, w - pad, bottom};
    Place(g_list, g_rcList.left, g_rcList.top, g_rcList.right - g_rcList.left, g_rcList.bottom - g_rcList.top);
}

// ---------------------------------------------------------------- list view
static std::wstring CellText(const PortConn& c, int col) {
    switch (col) {
        case 0: return std::to_wstring(c.localPort);
        case 1: return ProtoName(c.proto);
        case 2: return c.localAddr;
        case 3: return c.remoteAddr;
        case 4: return c.proto == PROTO_UDP ? L"BOUND" : TcpStateName(c.state);
        case 5: return std::to_wstring(c.pid);
        case 6: return c.processName;
        default: return L"";
    }
}

static Color StateColor(const PortConn& c) {
    if (c.proto == PROTO_UDP) return RGBCOL(150, 150, 150);
    switch (c.state) {
        case MIB_TCP_STATE_LISTEN: return RGBCOL(255, 255, 255);
        case MIB_TCP_STATE_ESTAB: return RGBCOL(205, 205, 205);
        case MIB_TCP_STATE_TIME_WAIT: return kFaint;
        case MIB_TCP_STATE_CLOSE_WAIT:
        case MIB_TCP_STATE_CLOSING:
        case MIB_TCP_STATE_LAST_ACK: return kAmber;
        case MIB_TCP_STATE_SYN_SENT:
        case MIB_TCP_STATE_SYN_RCVD: return RGBCOL(185, 185, 185);
        default: return kMuted;
    }
}

static LRESULT HeaderCustomDraw(NMCUSTOMDRAW* cd) {
    if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (cd->dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    int idx = (int)cd->dwItemSpec;
    if (idx < 0 || idx >= kColCount) return CDRF_DODEFAULT;

    HDC dc = cd->hdc;
    RECT rc = cd->rc;
    bool pressed = (cd->uItemState & CDIS_SELECTED) != 0;
    bool hot = (cd->uItemState & CDIS_HOT) != 0;

    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    SolidBrush bg(pressed ? kPanel3 : hot ? kPanel2 : kPanel);
    g.FillRectangle(&bg, RectF((REAL)rc.left, (REAL)rc.top, (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top)));

    SolidBrush line(kBorder);
    g.FillRectangle(&line, RectF((REAL)rc.left, (REAL)rc.bottom - 1.0f, (REAL)(rc.right - rc.left), 1.0f));

    bool sorted = idx == g_sortCol;
    bool alignRight = kCols[idx].fmt == LVCFMT_RIGHT;
    RECT textRc = rc;
    textRc.left += S(12);
    textRc.right -= sorted ? S(24) : S(10);
    DrawTextC(g, kCols[idx].name, g_fUISemi, textRc, sorted ? kText : kMuted,
              alignRight ? StringAlignmentFar : StringAlignmentNear);

    if (sorted) {
        REAL cx = (REAL)rc.right - S(13);
        REAL cy = (REAL)(rc.top + rc.bottom) / 2.0f;
        PointF pts[3];
        if (g_sortAsc) {
            pts[0] = PointF(cx - S(4), cy + S(2));
            pts[1] = PointF(cx + S(4), cy + S(2));
            pts[2] = PointF(cx, cy - S(3));
        } else {
            pts[0] = PointF(cx - S(4), cy - S(2));
            pts[1] = PointF(cx + S(4), cy - S(2));
            pts[2] = PointF(cx, cy + S(3));
        }
        SolidBrush ab(kAccent);
        g.FillPolygon(&ab, pts, 3);
    }
    return CDRF_SKIPDEFAULT;
}

static void DrawEmptyState(HDC dc) {
    RECT rc;
    GetClientRect(g_list, &rc);
    rc.top += S(40);
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    std::wstring title = g_all.empty() ? L"No connections were found"
                                       : L"Nothing matches this filter";
    std::wstring hint = g_all.empty() ? L"Press Refresh or F5 to scan again"
                                      : L"Clear the search box or loosen the filters";
    RECT t = rc, s = rc;
    t.top = rc.top + (rc.bottom - rc.top) / 2 - S(24);
    t.bottom = t.top + S(26);
    s.top = t.bottom;
    s.bottom = s.top + S(22);
    DrawTextC(g, title, g_fUISemi, t, kMuted);
    DrawTextC(g, hint, g_fSmall, s, kFaint);
}

static LRESULT CALLBACK ListSubProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (msg == WM_NOTIFY) {
        NMHDR* nh = (NMHDR*)l;
        HWND hdr = ListView_GetHeader(h);
        if (hdr && nh->hwndFrom == hdr && nh->code == NM_CUSTOMDRAW)
            return HeaderCustomDraw((NMCUSTOMDRAW*)l);
    } else if (msg == WM_ERASEBKGND) {
        return 1;
    }
    return DefSubclassProc(h, msg, w, l);
}

// ---------------------------------------------------------------- data / filtering
static int CompareRows(int ia, int ib) {
    const PortConn& a = g_all[ia];
    const PortConn& b = g_all[ib];
    int r = 0;
    switch (g_sortCol) {
        case 0: r = (int)a.localPort - (int)b.localPort; break;
        case 1: r = (int)a.proto - (int)b.proto; break;
        case 2: r = _wcsicmp(a.localAddr.c_str(), b.localAddr.c_str()); break;
        case 3: r = _wcsicmp(a.remoteAddr.c_str(), b.remoteAddr.c_str()); break;
        case 4: {
            int ra = TcpStateRank(a.state), rb = TcpStateRank(b.state);
            r = ra != rb ? ra - rb : _wcsicmp(TcpStateName(a.state).c_str(), TcpStateName(b.state).c_str());
            break;
        }
        case 5: r = (a.pid < b.pid) ? -1 : (a.pid > b.pid ? 1 : 0); break;
        case 6: r = _wcsicmp(a.processName.c_str(), b.processName.c_str()); break;
    }
    if (r != 0) return g_sortAsc ? r : -r;
    // stable tie-breakers so equal values do not jitter between refreshes
    if (a.localPort != b.localPort) return (int)a.localPort - (int)b.localPort;
    if (a.pid != b.pid) return (a.pid < b.pid) ? -1 : 1;
    return a.proto - b.proto;
}

static bool StartsWithNoCase(const std::wstring& s, const std::wstring& prefix) {
    if (prefix.size() > s.size()) return false;
    return _wcsnicmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
}

static bool ContainsNoCase(const std::wstring& s, const std::wstring& needle) {
    if (needle.empty()) return true;
    return StrStrIW(s.c_str(), needle.c_str()) != nullptr;
}

static bool Matches(const PortConn& c) {
    if (g_protoFilter && c.proto != g_protoFilter) return false;
    if (g_listenOnly && !(c.proto == PROTO_UDP || c.state == 2 /*LISTEN*/)) return false;
    if (g_filter.empty()) return true;

    std::wstring port = std::to_wstring(c.localPort);
    if (StartsWithNoCase(port, g_filter)) return true;
    if (ContainsNoCase(c.processName, g_filter)) return true;
    if (ContainsNoCase(c.localAddr, g_filter)) return true;
    if (ContainsNoCase(c.remoteAddr, g_filter)) return true;
    if (ContainsNoCase(TcpStateName(c.state), g_filter)) return true;
    if (ContainsNoCase(ProtoName(c.proto), g_filter)) return true;
    std::wstring pid = std::to_wstring(c.pid);
    return ContainsNoCase(pid, g_filter);
}

static void UpdateButtons() {
    bool has = ListView_GetSelectedCount(g_list) > 0;
    EnableBtn(B_KILL, has);
    EnableBtn(B_TREE, has);
    EnableBtn(B_EXPORT, !g_view.empty());
}

static void UpdateTitle() {
    wchar_t buf[160];
    _snwprintf_s(buf, _TRUNCATE, L"Port Killer  \u2014  %d connection%s  \u00b7  %d listening",
                 (int)g_all.size(), g_all.size() == 1 ? L"" : L"s", g_listening);
    SetWindowTextW(g_hwnd, buf);
}

static void RebuildView() {
    g_view.clear();
    g_view.reserve(g_all.size());
    for (int i = 0; i < (int)g_all.size(); ++i)
        if (Matches(g_all[i])) g_view.push_back(i);

    std::stable_sort(g_view.begin(), g_view.end(), [](int a, int b) { return CompareRows(a, b) < 0; });
    ListView_SetItemCountEx(g_list, (int)g_view.size(), LVSICF_NOSCROLL);
    InvalidateRect(g_list, nullptr, FALSE);
    UpdateButtons();
}

static std::vector<int> SelectedRows() {
    std::vector<int> out;
    int row = -1;
    while ((row = ListView_GetNextItem(g_list, row, LVIS_SELECTED)) != -1) out.push_back(row);
    return out;
}

static std::vector<DWORD> SelectedPids() {
    std::vector<DWORD> out;
    for (int row : SelectedRows()) {
        if (row < 0 || row >= (int)g_view.size()) continue;
        DWORD pid = g_all[g_view[row]].pid;
        if (std::find(out.begin(), out.end(), pid) == out.end()) out.push_back(pid);
    }
    return out;
}

static void RestoreSelection(const std::vector<DWORD>& pids) {
    if (pids.empty()) return;
    ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    int first = -1;
    for (int row = 0; row < (int)g_view.size(); ++row) {
        DWORD pid = g_all[g_view[row]].pid;
        if (std::find(pids.begin(), pids.end(), pid) != pids.end()) {
            ListView_SetItemState(g_list, row, LVIS_SELECTED, LVIS_SELECTED);
            if (first < 0) first = row;
        }
    }
    if (first >= 0) {
        ListView_SetItemState(g_list, first, LVIS_FOCUSED, LVIS_FOCUSED);
        ListView_EnsureVisible(g_list, first, FALSE);
    }
}

static void RefreshData(bool keepSelection) {
    std::vector<DWORD> sel = keepSelection ? SelectedPids() : std::vector<DWORD>();

    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    g_procs = EnumProcMap();
    g_all = EnumConnections();
    for (auto& c : g_all) {
        auto it = g_procs.find(c.pid);
        if (it != g_procs.end()) c.processName = it->second.name;
        if (c.processName.empty())
            c.processName = (c.pid == 0) ? L"System Idle" : (c.pid == 4) ? L"System" : L"Unknown";
    }
    g_listening = 0;
    for (const auto& c : g_all)
        if (c.proto == PROTO_TCP && c.state == 2) ++g_listening;
    g_processCount = (int)g_procs.size();

    RebuildView();
    RestoreSelection(sel);
    QueryPerformanceCounter(&t1);
    g_refreshMs = (int)((t1.QuadPart - t0.QuadPart) * 1000 / g_qpcFreq);
    UpdateTitle();
    InvalidateRect(g_hwnd, &g_rcStatus, FALSE);
}

static void SetFlash(const std::wstring& msg) {
    g_flash = msg;
    g_flashUntil = GetTickCount64() + 4500;
    SetTimer(g_hwnd, 2, 500, nullptr);
    InvalidateRect(g_hwnd, &g_rcStatus, FALSE);
}

// ---------------------------------------------------------------- painting
static void FrameRect1(Graphics& g, const RECT& rc) {
    SolidBrush b(kBorder);
    g.FillRectangle(&b, RectF((REAL)rc.left - 1.0f, (REAL)rc.top - 1.0f, (REAL)(rc.right - rc.left) + 2.0f, 1.0f));
    g.FillRectangle(&b, RectF((REAL)rc.left - 1.0f, (REAL)rc.bottom, (REAL)(rc.right - rc.left) + 2.0f, 1.0f));
    g.FillRectangle(&b, RectF((REAL)rc.left - 1.0f, (REAL)rc.top - 1.0f, 1.0f, (REAL)(rc.bottom - rc.top) + 2.0f));
    g.FillRectangle(&b, RectF((REAL)rc.right, (REAL)rc.top - 1.0f, 1.0f, (REAL)(rc.bottom - rc.top) + 2.0f));
}

static void DrawSearchCard(Graphics& g) {
    bool focused = GetFocus() == g_search;
    FillRound(g, g_rcSearch, (REAL)S(10), kPanel2);
    StrokeRound(g, g_rcSearch, (REAL)S(10), focused ? Color(150, 76, 132, 255) : kBorder);

    REAL cx = (REAL)g_rcSearch.left + S(19);
    REAL cy = (REAL)(g_rcSearch.top + g_rcSearch.bottom) / 2.0f;
    Pen pen(focused ? kAccent : kMuted, S(2) > 1 ? 1.6f : 1.0f);
    g.DrawEllipse(&pen, (REAL)(cx - S(6)), (REAL)(cy - S(6)), (REAL)S(11), (REAL)S(11));
    g.DrawLine(&pen, cx + S(4), cy + S(4), cx + S(8), cy + S(8));
}

static void DrawStatus(Graphics& g, const RECT& rc) {
    SolidBrush line(kBorder);
    g.FillRectangle(&line, RectF(0.0f, (REAL)rc.top, (REAL)(rc.right - rc.left), 1.0f));

    if (GetTickCount64() > g_flashUntil) g_flash.clear();

    std::wstring left = std::to_wstring(g_all.size()) + L" connections   \u00b7   " +
                        std::to_wstring(g_listening) + L" listening   \u00b7   " +
                        std::to_wstring(g_processCount) + L" processes   \u00b7   " +
                        std::to_wstring(g_view.size()) + L" shown   \u00b7   " +
                        std::to_wstring(g_refreshMs) + L" ms";
    RECT rcLeft = rc;
    rcLeft.left += S(14);
    rcLeft.right = rcLeft.left + S(700);

    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);
    fmtL.SetFormatFlags(StringFormatFlagsNoWrap);
    fmtL.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush brMuted(kMuted);
    RectF rfL((REAL)rcLeft.left, (REAL)rcLeft.top, (REAL)(rcLeft.right - rcLeft.left), (REAL)(rcLeft.bottom - rcLeft.top));
    g.DrawString(left.c_str(), (INT)left.size(), g_fSmall, rfL, &fmtL, &brMuted);

    if (!g_flash.empty()) {
        RectF bounds;
        g.MeasureString(left.c_str(), (INT)left.size(), g_fSmall, rfL, &fmtL, &bounds);
        RECT rcFlash = rc;
        rcFlash.left = rcLeft.left + (int)bounds.Width + S(10);
        rcFlash.right = rc.right - S(320);
        if (rcFlash.right > rcFlash.left + S(60)) {
            StringFormat fmtA;
            fmtA.SetAlignment(StringAlignmentNear);
            fmtA.SetLineAlignment(StringAlignmentCenter);
            fmtA.SetFormatFlags(StringFormatFlagsNoWrap);
            fmtA.SetTrimming(StringTrimmingEllipsisCharacter);
            SolidBrush brAccent(kAccent);
            RectF rfA((REAL)rcFlash.left, (REAL)rcFlash.top, (REAL)(rcFlash.right - rcFlash.left), (REAL)(rcFlash.bottom - rcFlash.top));
            g.DrawString(g_flash.c_str(), (INT)g_flash.size(), g_fSmall, rfA, &fmtA, &brAccent);
        }
    }

    std::wstring right = L"F5 refresh    Del kill    Ctrl+C copy    Ctrl+F filter";
    RECT rcRight = rc;
    rcRight.right -= S(14);
    rcRight.left = rcRight.right - S(420);
    if (rcRight.left > rcLeft.left + S(300)) {
        RECT measured = rcRight;
        StringFormat fmtR;
        fmtR.SetAlignment(StringAlignmentFar);
        fmtR.SetLineAlignment(StringAlignmentCenter);
        fmtR.SetFormatFlags(StringFormatFlagsNoWrap);
        SolidBrush brFaint(kFaint);
        RectF rfR((REAL)measured.left, (REAL)measured.top, (REAL)(measured.right - measured.left), (REAL)(measured.bottom - measured.top));
        g.DrawString(right.c_str(), (INT)right.size(), g_fSmall, rfR, &fmtR, &brFaint);
    }
}

static void PaintMain() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(g_hwnd, &ps);
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) { EndPaint(g_hwnd, &ps); return; }

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

        SolidBrush bg(kBg);
        g.FillRectangle(&bg, 0, 0, (INT)rc.right, (INT)rc.bottom);
        DrawSearchCard(g);
        FrameRect1(g, g_rcList);
        DrawStatus(g, g_rcStatus);
    }
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(g_hwnd, &ps);
}

// ---------------------------------------------------------------- actions
static void CopyText(const std::wstring& s) {
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        void* p = GlobalLock(h);
        if (p) {
            memcpy(p, s.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
    }
    CloseClipboard();
}

static std::wstring RowLine(const PortConn& c) {
    return CellText(c, 0) + L"\t" + CellText(c, 1) + L"\t" + CellText(c, 2) + L"\t" + CellText(c, 3) +
           L"\t" + CellText(c, 4) + L"\t" + CellText(c, 5) + L"\t" + CellText(c, 6);
}

static void CopySelection(int mode) { // 0 = row(s), 1 = pid(s), 2 = port(s), 3 = path
    auto rows = SelectedRows();
    if (rows.empty()) { SetFlash(L"Nothing selected"); return; }
    std::wstring out;
    for (size_t i = 0; i < rows.size(); ++i) {
        int row = rows[i];
        if (row < 0 || row >= (int)g_view.size()) continue;
        const PortConn& c = g_all[g_view[row]];
        if (i) out += L"\r\n";
        if (mode == 0) out += RowLine(c);
        else if (mode == 1) out += std::to_wstring(c.pid);
        else if (mode == 2) out += std::to_wstring(c.localPort);
        else {
            auto it = g_procs.find(c.pid);
            out += (it != g_procs.end()) ? it->second.path : L"";
        }
    }
    CopyText(out);
    SetFlash(rows.size() == 1 ? L"Copied to clipboard" : L"Copied " + std::to_wstring(rows.size()) + L" rows");
}

static void DoKill(bool tree) {
    auto rows = SelectedRows();
    if (rows.empty()) {
        SetFlash(L"Select a connection first");
        MessageBeep(MB_ICONWARNING);
        return;
    }

    std::vector<DWORD> pids;
    std::wstring listing;
    int listed = 0;
    bool skippedSystem = false;
    for (int row : rows) {
        if (row < 0 || row >= (int)g_view.size()) continue;
        const PortConn& c = g_all[g_view[row]];
        if (c.pid <= 4) { skippedSystem = true; continue; }
        if (c.pid == GetCurrentProcessId()) { skippedSystem = true; continue; }
        if (std::find(pids.begin(), pids.end(), c.pid) != pids.end()) continue;
        pids.push_back(c.pid);
        if (listed < 8) {
            listing += L"  " + c.processName + L"   PID " + std::to_wstring(c.pid) +
                       L"   port " + std::to_wstring(c.localPort) + L"\n";
        }
        ++listed;
    }
    if (pids.empty()) {
        SetFlash(L"Only protected processes were selected");
        return;
    }

    std::wstring msg = L"Terminate " + std::to_wstring(pids.size()) +
                       (pids.size() == 1 ? L" process?\n\n" : L" processes?\n\n") + listing;
    if (listed > 8) msg += L"  \u2026and " + std::to_wstring(listed - 8) + L" more\n";
    if (tree) msg += L"\nChild processes are terminated first.";
    if (skippedSystem) msg += L"\n\nProtected system processes in the selection are skipped.";

    UINT flags = MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2;
    if (MessageBoxW(g_hwnd, msg.c_str(), tree ? L"Kill process tree" : L"Kill process", flags) != IDYES) return;

    int ok = 0, failed = 0;
    DWORD firstErr = 0;
    for (DWORD pid : pids) {
        if (tree) {
            auto kids = ChildPids(pid);
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
                DWORD e = 0;
                KillPid(*it, &e);
            }
        }
        DWORD e = 0;
        if (KillPid(pid, &e)) ++ok;
        else { ++failed; if (!firstErr) firstErr = e; }
    }

    RefreshData(true);
    if (failed) {
        wchar_t buf[320];
        _snwprintf_s(buf, _TRUNCATE, L"%d of %d process(es) could not be terminated.\n\nWindows error %lu.",
                     failed, (int)pids.size(), firstErr);
        std::wstring m = buf;
        if (!IsAdmin()) m += L"\n\nRun Port Killer as administrator (use Elevate) to terminate protected processes.";
        SetFlash(L"Failed to kill " + std::to_wstring(failed));
        MessageBoxW(g_hwnd, m.c_str(), L"Kill failed", MB_ICONERROR | MB_OK);
    } else {
        SetFlash(L"Killed " + std::to_wstring(ok) + (ok == 1 ? L" process" : L" processes"));
    }
}

static void ShowDetails() {
    auto rows = SelectedRows();
    if (rows.empty()) return;
    int row = rows[0];
    if (row < 0 || row >= (int)g_view.size()) return;
    const PortConn& c = g_all[g_view[row]];

    std::wstring path;
    DWORD ppid = 0;
    auto it = g_procs.find(c.pid);
    if (it != g_procs.end()) { path = it->second.path; ppid = it->second.ppid; }
    if (path.empty()) path = L"(access denied - elevate for details)";

    wchar_t head[256];
    _snwprintf_s(head, _TRUNCATE, L"%s\r\nPID %lu   \u00b7   parent %lu   \u00b7   %s working set\r\n\r\n",
                 c.processName.c_str(), c.pid, ppid, FormatBytes(WorkingSetOf(c.pid)).c_str());

    std::wstring text = head;
    text += L"Protocol   " + ProtoName(c.proto) + ((c.v6) ? L" over IPv6" : L" over IPv4") + L"\r\n";
    text += L"State      " + (c.proto == PROTO_UDP ? std::wstring(L"BOUND") : TcpStateName(c.state)) + L"\r\n";
    text += L"Local      " + c.localAddr + L"\r\n";
    text += L"Remote     " + c.remoteAddr + L"\r\n\r\n";
    text += L"Image\r\n" + path;

    MessageBoxW(g_hwnd, text.c_str(), L"Process details", MB_ICONINFORMATION | MB_OK);
}

static void OpenFileLocation() {
    auto rows = SelectedRows();
    if (rows.empty()) return;
    int row = rows[0];
    if (row < 0 || row >= (int)g_view.size()) return;
    DWORD pid = g_all[g_view[row]].pid;
    auto it = g_procs.find(pid);
    if (it == g_procs.end() || it->second.path.empty()) {
        SetFlash(L"Path unavailable - elevate for access");
        return;
    }
    std::wstring args = L"/select,\"" + it->second.path + L"\"";
    ShellExecuteW(g_hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

static void OpenInBrowser() {
    auto rows = SelectedRows();
    if (rows.empty()) return;
    int row = rows[0];
    if (row < 0 || row >= (int)g_view.size()) return;
    const PortConn& c = g_all[g_view[row]];
    if (c.proto != PROTO_TCP || c.state != 2) {
        SetFlash(L"Only listening TCP ports can be opened");
        return;
    }
    std::wstring url = L"http://127.0.0.1:" + std::to_wstring(c.localPort);
    ShellExecuteW(g_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static std::string Utf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static std::string CsvCell(const std::wstring& s) {
    std::string v = Utf8(s);
    std::string out = "\"";
    for (char ch : v) {
        if (ch == '"') out += '"';
        out += ch;
    }
    out += '"';
    return out;
}

static void ExportCsv() {
    if (g_view.empty()) { SetFlash(L"Nothing to export"); return; }

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[128];
    _snwprintf_s(name, _TRUNCATE, L"ports-%04d%02d%02d-%02d%02d.csv", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);

    wchar_t path[MAX_PATH * 2] = L"";
    wcscpy_s(path, name);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"CSV spreadsheet (*.csv)\0*.csv\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = (DWORD)(sizeof(path) / sizeof(path[0]));
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return;

    std::string out = "\xEF\xBB\xBF";
    out += "port,protocol,local_address,remote_address,state,pid,process\r\n";
    for (int idx : g_view) {
        const PortConn& c = g_all[idx];
        out += CsvCell(CellText(c, 0)) + "," + CsvCell(CellText(c, 1)) + "," + CsvCell(c.localAddr) + "," +
               CsvCell(c.remoteAddr) + "," + CsvCell(CellText(c, 4)) + "," + CsvCell(CellText(c, 5)) + "," +
               CsvCell(c.processName) + "\r\n";
    }

    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_hwnd, L"Could not write the CSV file.", L"Export failed", MB_ICONERROR | MB_OK);
        return;
    }
    DWORD written = 0;
    WriteFile(f, out.data(), (DWORD)out.size(), &written, nullptr);
    CloseHandle(f);
    SetFlash(L"Exported " + std::to_wstring(g_view.size()) + L" rows");
}

static void Elevate() {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return;
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exe;
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) {
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    } else if (GetLastError() != ERROR_CANCELLED) {
        MessageBoxW(g_hwnd, L"Could not restart with administrator rights.", L"Elevation failed", MB_ICONERROR | MB_OK);
    }
}

// ---------------------------------------------------------------- settings
static const wchar_t* kRegKey = L"Software\\PortKiller";

// Bump when the default window geometry or chrome changes: stale saved rects are dropped once.
static const DWORD kUiVersion = 3;

static void SaveSettings() {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(g_hwnd, &wp)) {
        RECT r = wp.rcNormalPosition;
        RegSetValueExW(k, L"Window", 0, REG_BINARY, (const BYTE*)&r, sizeof(r));
    }
    DWORD d = kUiVersion;              RegSetValueExW(k, L"UiVersion", 0, REG_DWORD, (const BYTE*)&d, 4);
    d = g_auto ? 1 : 0;                RegSetValueExW(k, L"Auto", 0, REG_DWORD, (const BYTE*)&d, 4);
    d = g_listenOnly ? 1 : 0;          RegSetValueExW(k, L"ListenOnly", 0, REG_DWORD, (const BYTE*)&d, 4);
    d = (DWORD)g_protoFilter;          RegSetValueExW(k, L"Proto", 0, REG_DWORD, (const BYTE*)&d, 4);
    d = (DWORD)g_sortCol;              RegSetValueExW(k, L"SortCol", 0, REG_DWORD, (const BYTE*)&d, 4);
    d = g_sortAsc ? 1 : 0;             RegSetValueExW(k, L"SortAsc", 0, REG_DWORD, (const BYTE*)&d, 4);
    RegCloseKey(k);
}

static void LoadSettings() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    DWORD type = 0, size = 0, d = 0;
    DWORD ver = 0;
    size = sizeof(ver);
    bool verOk = RegQueryValueExW(k, L"UiVersion", nullptr, &type, (BYTE*)&ver, &size) == ERROR_SUCCESS && ver == kUiVersion;

    RECT r{};
    size = sizeof(r);
    if (verOk && RegQueryValueExW(k, L"Window", nullptr, &type, (BYTE*)&r, &size) == ERROR_SUCCESS && type == REG_BINARY) {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        int w = r.right - r.left, h = r.bottom - r.top;
        int maxW = work.right - work.left, maxH = work.bottom - work.top;
        if (w < S(780)) w = S(780);
        if (h < S(430)) h = S(430);
        if (maxW > 0 && w > maxW) w = maxW;
        if (maxH > 0 && h > maxH) h = maxH;
        int x = r.left, y = r.top;
        if (maxW > 0) {
            if (x + w > work.right) x = work.right - w;
            if (x < work.left) x = work.left;
        }
        if (maxH > 0) {
            if (y + h > work.bottom) y = work.bottom - h;
            if (y < work.top) y = work.top;
        }
        SetWindowPos(g_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    size = sizeof(d);
    if (RegQueryValueExW(k, L"Auto", nullptr, &type, (BYTE*)&d, &size) == ERROR_SUCCESS) g_auto = d != 0;
    size = sizeof(d);
    if (RegQueryValueExW(k, L"ListenOnly", nullptr, &type, (BYTE*)&d, &size) == ERROR_SUCCESS) g_listenOnly = d != 0;
    size = sizeof(d);
    if (RegQueryValueExW(k, L"Proto", nullptr, &type, (BYTE*)&d, &size) == ERROR_SUCCESS && d <= 17) g_protoFilter = (int)d;
    size = sizeof(d);
    if (RegQueryValueExW(k, L"SortCol", nullptr, &type, (BYTE*)&d, &size) == ERROR_SUCCESS && d < (DWORD)kColCount) g_sortCol = (int)d;
    size = sizeof(d);
    if (RegQueryValueExW(k, L"SortAsc", nullptr, &type, (BYTE*)&d, &size) == ERROR_SUCCESS) g_sortAsc = d != 0;
    RegCloseKey(k);
}

// ---------------------------------------------------------------- ui helpers
static void UpdateHeaderHeight() {
    if (!g_list) return;
    HWND hdr = ListView_GetHeader(g_list);
    if (!hdr) return;
    RECT r;
    GetClientRect(g_list, &r);
    SetWindowPos(hdr, nullptr, 0, 0, r.right, S(32), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void SortByColumn(int col) {
    if (col < 0 || col >= kColCount) return;
    if (g_sortCol == col) g_sortAsc = !g_sortAsc;
    else { g_sortCol = col; g_sortAsc = true; }
    RebuildView();
    HWND hdr = ListView_GetHeader(g_list);
    if (hdr) InvalidateRect(hdr, nullptr, TRUE);
}

static void ApplyProtoFilter(int proto) {
    g_protoFilter = proto;
    SetChecked(B_ALL, proto == 0);
    SetChecked(B_TCP, proto == PROTO_TCP);
    SetChecked(B_UDP, proto == PROTO_UDP);
    RebuildView();
    InvalidateRect(g_hwnd, &g_rcStatus, FALSE);
}

static void ApplyListenOnly(bool on) {
    g_listenOnly = on;
    SetChecked(B_LISTEN, on);
    RebuildView();
    InvalidateRect(g_hwnd, &g_rcStatus, FALSE);
}

static void ApplyAuto(bool on) {
    g_auto = on;
    SetChecked(B_AUTO, on);
    if (on) SetTimer(g_hwnd, 1, 2000, nullptr);
    else KillTimer(g_hwnd, 1);
}

// ---------------------------------------------------------------- context menu
static void ShowContextMenu(int x, int y) {
    bool has = ListView_GetSelectedCount(g_list) > 0;
    bool listen = false;
    auto rows = SelectedRows();
    if (!rows.empty()) {
        int row = rows[0];
        if (row >= 0 && row < (int)g_view.size()) {
            const PortConn& c = g_all[g_view[row]];
            listen = (c.proto == PROTO_TCP && c.state == 2);
        }
    }

    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), 1, L"Kill process\tDel");
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), 2, L"Kill process tree");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), 3, L"Copy row(s)\tCtrl+C");
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), 4, L"Copy PID");
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), 5, L"Copy port");
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), 6, L"Copy image path");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (listen ? 0 : MF_GRAYED), 7, L"Open in browser");
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), 8, L"Open file location");
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), 9, L"Process details\tEnter");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 10, L"Refresh\tF5");
    SetMenuDefaultItem(m, 1, FALSE);

    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, x, y, 0, g_hwnd, nullptr);
    DestroyMenu(m);

    switch (cmd) {
        case 1: DoKill(false); break;
        case 2: DoKill(true); break;
        case 3: CopySelection(0); break;
        case 4: CopySelection(1); break;
        case 5: CopySelection(2); break;
        case 6: CopySelection(3); break;
        case 7: OpenInBrowser(); break;
        case 8: OpenFileLocation(); break;
        case 9: ShowDetails(); break;
        case 10: RefreshData(true); break;
    }
}

// ---------------------------------------------------------------- init
#define ID_SEARCH 1002

static LRESULT CALLBACK EditSubProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN) {
        if (w == VK_ESCAPE) {
            SetWindowTextW(h, L"");
            return 0;
        }
        if (w == VK_RETURN) {
            SetFocus(g_list);
            return 0;
        }
        if (w == VK_DOWN) {
            SetFocus(g_list);
            return 0;
        }
    }
    return DefSubclassProc(h, msg, w, l);
}

static void InitUI() {
    LoadSettings();

    g_search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                               0, 0, 10, 10, g_hwnd, (HMENU)ID_SEARCH, g_inst, nullptr);
    SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by port, process, PID, state or address");
    SetWindowTheme(g_search, L"DarkMode_CFD", nullptr);
    SetWindowSubclass(g_search, EditSubProc, 1, 0);

    AddButton(B_REFRESH, L"Refresh", BK_NORMAL);
    AddButton(B_KILL, L"Kill", BK_DANGER);
    AddButton(B_TREE, L"Kill tree", BK_NORMAL);
    AddButton(B_EXPORT, L"Export CSV", BK_GHOST);
    AddButton(B_ELEVATE, IsAdmin() ? L"Administrator" : L"Standard user  \u00b7  Elevate", BK_GHOST);
    AddButton(B_ALL, L"All", BK_TOGGLE);
    AddButton(B_TCP, L"TCP", BK_TOGGLE);
    AddButton(B_UDP, L"UDP", BK_TOGGLE);
    AddButton(B_LISTEN, L"Listen only", BK_TOGGLE);
    AddButton(B_AUTO, L"Auto 2s", BK_TOGGLE);

    g_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
                             0, 0, 10, 10, g_hwnd, (HMENU)1001, g_inst, nullptr);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    ListView_SetBkColor(g_list, Cr(kPanel));
    ListView_SetTextBkColor(g_list, Cr(kPanel));
    ListView_SetTextColor(g_list, Cr(kText));
    SetWindowTheme(g_list, L"DarkMode_Explorer", nullptr);
    SetWindowSubclass(g_list, ListSubProc, 1, 0);

    for (int i = 0; i < kColCount; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
        col.pszText = (LPWSTR)kCols[i].name;
        col.cx = S(kCols[i].width);
        col.fmt = kCols[i].fmt;
        col.iSubItem = i;
        ListView_InsertColumn(g_list, i, &col);
    }

    RecreateFonts();
    UpdateHeaderHeight();

    SetChecked(B_ALL, g_protoFilter == 0);
    SetChecked(B_TCP, g_protoFilter == PROTO_TCP);
    SetChecked(B_UDP, g_protoFilter == PROTO_UDP);
    SetChecked(B_LISTEN, g_listenOnly);
    SetChecked(B_AUTO, g_auto);
    EnableBtn(B_KILL, false);
    EnableBtn(B_TREE, false);
    EnableBtn(B_EXPORT, false);
    if (g_auto) SetTimer(g_hwnd, 1, 2000, nullptr);
}

// ---------------------------------------------------------------- list owner data
// Rows are painted entirely by us: grayscale text, monochrome selection, no system theming.
static void PaintRow(NMLVCUSTOMDRAW* cd) {
    int row = (int)cd->nmcd.dwItemSpec;
    if (row < 0) return;

    HDC dc = cd->nmcd.hdc;
    RECT client;
    GetClientRect(g_list, &client);

    RECT rowRc = cd->nmcd.rc;
    rowRc.right = client.right;
    if (rowRc.bottom - rowRc.top < S(12)) return;

    bool inView = row < (int)g_view.size();
    bool selected = inView && (ListView_GetItemState(g_list, row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    bool focused = GetFocus() == g_list;

    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeNone);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    Color bgc = selected ? (focused ? kSel : kSelInactive) : ((row & 1) ? kPanel2 : kPanel);
    SolidBrush bg(bgc);
    g.FillRectangle(&bg, RectF((REAL)rowRc.left, (REAL)rowRc.top,
                               (REAL)(rowRc.right - rowRc.left), (REAL)(rowRc.bottom - rowRc.top)));
    if (!inView) return;

    const PortConn& c = g_all[g_view[row]];
    int left = rowRc.left;
    for (int i = 0; i < kColCount; ++i) {
        int cw = ListView_GetColumnWidth(g_list, i);
        RECT cell{left, rowRc.top, left + cw, rowRc.bottom};
        left += cw;
        if (cell.left >= rowRc.right) break;
        if (cell.right > rowRc.right) cell.right = rowRc.right;

        RECT textRc = cell;
        textRc.left += S(12);
        textRc.right -= S(8);
        bool right = kCols[i].fmt == LVCFMT_RIGHT;

        Color fg = kText;
        switch (i) {
            case 0:  fg = (c.proto == PROTO_TCP && c.state == MIB_TCP_STATE_LISTEN) ? kText : RGBCOL(210, 210, 210); break;
            case 1:  fg = kFaint; break;
            case 2:  fg = kText; break;
            case 3:  fg = kMuted; break;
            case 4:  fg = StateColor(c); break;
            case 5:  fg = kMuted; break;
            case 6:  fg = selected ? kText : RGBCOL(198, 198, 198); break;
        }
        DrawTextC(g, CellText(c, i), (i == 0 || i == 4) ? g_fUISemi : g_fUI, textRc, fg,
                  right ? StringAlignmentFar : StringAlignmentNear, &cell);
    }
}

static LRESULT HandleRowCustomDraw(NMLVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW | (g_view.empty() ? CDRF_NOTIFYPOSTPAINT : 0);
        case CDDS_ITEMPREPAINT:
            PaintRow(cd);
            return CDRF_SKIPDEFAULT;
        case CDDS_POSTPAINT:
            DrawEmptyState(cd->nmcd.hdc);
            return CDRF_DODEFAULT;
    }
    return CDRF_DODEFAULT;
}

static void ApplyDarkFrame(HWND h) {
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(h, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(h, 19, &dark, sizeof(dark));
    COLORREF cap = Cr(kBg), txt = Cr(kText), bd = Cr(kBorder);
    DwmSetWindowAttribute(h, 34, &bd, sizeof(bd));
    DwmSetWindowAttribute(h, 35, &cap, sizeof(cap));
    DwmSetWindowAttribute(h, 36, &txt, sizeof(txt));
}

static LRESULT HandleShortcut(MSG& msg) {
    if (msg.message != WM_KEYDOWN) return 0;
    if (GetAncestor(msg.hwnd, GA_ROOT) != g_hwnd) return 0;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool inList = (msg.hwnd == g_list);
    bool inEdit = (msg.hwnd == g_search);

    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (msg.wParam == VK_F5) { RefreshData(true); return 1; }
    if (ctrl && (msg.wParam == 'F' || msg.wParam == 'L')) {
        SetFocus(g_search);
        SendMessageW(g_search, EM_SETSEL, 0, -1);
        return 1;
    }
    if (ctrl && msg.wParam == 'E') { ExportCsv(); return 1; }
    if (ctrl && msg.wParam == 'R') {
        if (shift) ApplyAuto(!g_auto); else RefreshData(true);
        return 1;
    }
    if (ctrl && msg.wParam == 'K' && inList) { DoKill(shift); return 1; }
    if (ctrl && msg.wParam == 'D') { ShowDetails(); return 1; }
    if (msg.wParam == VK_DELETE && inList) { DoKill(false); return 1; }
    if (ctrl && msg.wParam == 'C' && inList) { CopySelection(0); return 1; }
    if (ctrl && msg.wParam == 'A' && inList) {
        ListView_SetItemState(g_list, -1, LVIS_SELECTED, LVIS_SELECTED);
        UpdateButtons();
        return 1;
    }
    if (msg.wParam == VK_ESCAPE) {
        if (inEdit) { SetWindowTextW(g_search, L""); return 1; }
        if (inList) {
            ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            UpdateButtons();
            return 1;
        }
    }
    if (msg.wParam == VK_RETURN && inList) { ShowDetails(); return 1; }
    return 0;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g_hwnd = hwnd;
            return 0;

        case WM_SIZE:
            if (g_list) {
                LayoutUI(LOWORD(lParam), HIWORD(lParam));
                UpdateHeaderHeight();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lParam;
            mm->ptMinTrackSize.x = S(780);
            mm->ptMinTrackSize.y = S(430);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            PaintMain();
            return 0;

        case WM_DPICHANGED: {
            g_dpi = HIWORD(wParam);
            RECT* pr = (RECT*)lParam;
            SetWindowPos(hwnd, nullptr, pr->left, pr->top, pr->right - pr->left, pr->bottom - pr->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RECT rc;
            GetClientRect(hwnd, &rc);
            for (int i = 0; i < kColCount; ++i)
                ListView_SetColumnWidth(g_list, i, S(kCols[i].width));
            RecreateFonts();
            LayoutUI(rc.right, rc.bottom);
            UpdateHeaderHeight();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }

        case WM_NOTIFY: {
            NMHDR* nh = (NMHDR*)lParam;
            if (!nh || nh->hwndFrom != g_list) break;
            switch (nh->code) {
                case LVN_GETDISPINFOW: {
                    NMLVDISPINFOW* di = (NMLVDISPINFOW*)lParam;
                    if (!(di->item.mask & LVIF_TEXT)) return 0;
                    int row = di->item.iItem;
                    int col = di->item.iSubItem;
                    if (row < 0 || row >= (int)g_view.size() || col < 0 || col >= kColCount) return 0;
                    static std::wstring scratch;
                    scratch = CellText(g_all[g_view[row]], col);
                    di->item.pszText = (LPWSTR)scratch.c_str();
                    return 0;
                }
                case NM_CUSTOMDRAW:
                    return HandleRowCustomDraw((NMLVCUSTOMDRAW*)lParam);
                case LVN_COLUMNCLICK:
                    SortByColumn(((NMLISTVIEW*)lParam)->iSubItem);
                    return 0;
                case LVN_ITEMCHANGED:
                    UpdateButtons();
                    return 0;
                case NM_DBLCLK:
                    if (ListView_GetSelectedCount(g_list) > 0) DoKill(false);
                    return 0;
                case NM_RCLICK:
                    if (ListView_GetSelectedCount(g_list) == 0) {
                        LVHITTESTINFO ht{};
                        GetCursorPos(&ht.pt);
                        ScreenToClient(g_list, &ht.pt);
                        int row = ListView_HitTest(g_list, &ht);
                        if (row < 0) return 0;
                    }
                    {
                        POINT pt;
                        GetCursorPos(&pt);
                        ShowContextMenu(pt.x, pt.y);
                    }
                    return 0;
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (id == ID_SEARCH) {
                if (code == EN_CHANGE) {
                    wchar_t buf[256];
                    GetWindowTextW(g_search, buf, 256);
                    g_filter = Lower(buf);
                    RebuildView();
                    InvalidateRect(hwnd, &g_rcStatus, FALSE);
                } else if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                    InvalidateRect(hwnd, &g_rcSearch, FALSE);
                }
                return 0;
            }
            if (code == BN_CLICKED) {
                switch (id) {
                    case B_REFRESH: RefreshData(true); return 0;
                    case B_KILL: DoKill(false); return 0;
                    case B_TREE: DoKill(true); return 0;
                    case B_EXPORT: ExportCsv(); return 0;
                    case B_ELEVATE: Elevate(); return 0;
                    case B_ALL: ApplyProtoFilter(0); return 0;
                    case B_TCP: ApplyProtoFilter(PROTO_TCP); return 0;
                    case B_UDP: ApplyProtoFilter(PROTO_UDP); return 0;
                    case B_LISTEN: ApplyListenOnly(!g_listenOnly); return 0;
                    case B_AUTO: ApplyAuto(!g_auto); return 0;
                }
            }
            break;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis && dis->CtlType == ODT_BUTTON) {
                DrawButton(dis->hDC, dis);
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, Cr(kText));
            SetBkColor(dc, Cr(kPanel2));
            return (LRESULT)g_editBrush;
        }

        case WM_TIMER:
            if (wParam == 1) {
                if (g_auto && !IsIconic(hwnd)) RefreshData(true);
                return 0;
            }
            if (wParam == 2) {
                if (GetTickCount64() > g_flashUntil) {
                    g_flash.clear();
                    KillTimer(hwnd, 2);
                    InvalidateRect(hwnd, &g_rcStatus, FALSE);
                }
                return 0;
            }
            break;

        case WM_CLOSE:
            SaveSettings();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------- self test
static int SelfTest() {
    std::wstring report;
    int failures = 0;
    auto fail = [&](const std::wstring& m) {
        report += L"FAIL " + m + L"\r\n";
        ++failures;
    };

    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    ProcMap procs = EnumProcMap();
    std::vector<PortConn> conns = EnumConnections();
    QueryPerformanceCounter(&t1);
    double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
    int listeners = 0, v6 = 0, udp = 0;
    for (const auto& c : conns) {
        if (c.proto != PROTO_TCP && c.proto != PROTO_UDP)
            fail(L"unknown protocol on port " + std::to_wstring(c.localPort));
        if (c.localAddr.empty() || c.localAddr.find(L':') == std::wstring::npos)
            fail(L"malformed local address '" + c.localAddr + L"'");
        if (c.proto == PROTO_TCP && c.state == 0) fail(L"tcp row without state");
        if (c.proto == PROTO_TCP && c.state == 2) ++listeners;
        if (c.proto == PROTO_UDP) ++udp;
        if (c.v6) ++v6;
    }
    if (conns.empty()) fail(L"no connections enumerated");
    if (procs.empty()) fail(L"no processes enumerated");

    // the sort comparator must be a strict weak ordering
    g_all = conns;
    for (size_t i = 0; i + 1 < g_all.size() && i < 200; ++i) {
        int ab = CompareRows((int)i, (int)i + 1);
        int ba = CompareRows((int)i + 1, (int)i);
        if (ab != -ba) fail(L"comparator not antisymmetric at row " + std::to_wstring(i));
    }
    g_all.clear();

    wchar_t line[256];
    _snwprintf_s(line, _TRUNCATE, L"connections=%zu processes=%zu listening=%d udp=%d ipv6=%d admin=%d scan=%.1fms\r\n",
                 conns.size(), procs.size(), listeners, udp, v6, IsAdmin() ? 1 : 0, ms);
    report += line;
    report += failures ? L"RESULT FAIL\r\n" : L"RESULT PASS\r\n";

    std::string utf8 = Utf8(report);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    return failures ? 1 : 0;
}


// ---------------------------------------------------------------- entry point
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int showCmd) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (wcsstr(GetCommandLineW(), L"--selftest")) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        return SelfTest();
    }
    g_inst = inst;
    g_dpi = GetDpiForSystem();

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &gsi, nullptr) != Ok) return 1;

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcFreq = (ULONGLONG)(freq.QuadPart ? freq.QuadPart : 1);
    EnableDebugPrivilege();

    HICON icon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(Cr(kBg));
    wc.lpszClassName = L"PortKillerMain";
    wc.hIcon = icon;
    wc.hIconSm = icon;
    if (!RegisterClassExW(&wc)) {
        GdiplusShutdown(token);
        return 1;
    }

    int w = S(1040), h = S(660);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + ((work.right - work.left) - w) / 2;
    int y = work.top + ((work.bottom - work.top) - h) / 2;
    if (y < work.top) y = work.top;

    HWND hwnd = CreateWindowExW(0, L"PortKillerMain", L"Port Killer",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) {
        GdiplusShutdown(token);
        return 1;
    }
    g_hwnd = hwnd;
    ApplyDarkFrame(hwnd);
    InitUI();
    RefreshData(false);
    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);
    SetFocus(g_list);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (HandleShortcut(msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    delete g_fUI;
    delete g_fUISemi;
    delete g_fSmall;
    GdiplusShutdown(token);
    return (int)msg.wParam;
}
