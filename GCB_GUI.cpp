/*
 *  GCB_GUI.cpp  —  нативный интерфейс (неоморфизм) для GITHUBCLOAD.py
 *
 *  Сборка (i686-w64-mingw32-g++):
 *    g++.exe GCB_GUI.cpp -o GITHUBCLOAD_GUI.exe -mwindows -O2 -std=c++17 -static \
 *        -lgdiplus -lgdi32 -luser32 -lcomctl32 -lole32 -luuid -lshlwapi -lshell32 -ladvapi32
 *
 *  Оборачивает CLI-скрипт (upload / list / download / delete / wipe / selftest):
 *  команды исполняются в фоне, вывод показывается в нижней "консоли".
 *  Исходник — UTF-8, строки внутри переводятся в UTF-16.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <gdiplus.h>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

using namespace Gdiplus;

// ---------------------------------------------------------------------------
// Мини-утилиты
// ---------------------------------------------------------------------------
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::wstring wstr(const std::string& s) { return Utf8ToWide(s); }
static std::string u8str(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static const std::wstring& ScriptDir();

// Диагностика: дописывает в gcb_debug.log рядом с exe (для отладки «GUI не видит библиотек»)
static void DebugLog(const char* tag, const std::string& body) {
    FILE* f = _wfopen((ScriptDir() + L"\\gcb_debug.log").c_str(), L"ab");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char head[160];
    sprintf(head, "[%02d:%02d:%02d] %s:\n", st.wHour, st.wMinute, st.wSecond, tag);
    fwrite(head, 1, strlen(head), f);
    fwrite(body.data(), 1, body.size(), f);
    fputc('\n', f);
    fclose(f);
}
static const std::wstring& ScriptDir() {
    static std::wstring dir;
    if (dir.empty()) {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring exe(buf);
        size_t p = exe.find_last_of(L'\\');
        dir = (p == std::wstring::npos) ? L"." : exe.substr(0, p);
    }
    return dir;
}
static Color C(COLORREF c, BYTE a = 255) { return Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }
static void SetR(RECT& r, int l, int t, int ri, int b) { r.left = l; r.top = t; r.right = ri; r.bottom = b; }
static bool PtIn(const RECT& r, int x, int y) { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; }
static RECT Shift(RECT r, int dx, int dy) { r.left += dx; r.right += dx; r.top += dy; r.bottom += dy; return r; }
static float RW(const RECT& r) { return (float)(r.right - r.left); }
static float RH(const RECT& r) { return (float)(r.bottom - r.top); }

// ---------------------------------------------------------------------------
// Тема: неоморфизм, светлый
// ---------------------------------------------------------------------------
static const COLORREF BG         = RGB(0xE2, 0xE9, 0xF3);
static const COLORREF SH_DARK    = RGB(0x9F, 0xAD, 0xC6);
static const COLORREF SH_LIGHT   = RGB(0xFF, 0xFF, 0xFF);
static const COLORREF INNER      = RGB(0xF3, 0xF8, 0xFE);
static const COLORREF TXT        = RGB(0x44, 0x50, 0x61);
static const COLORREF TXT_MUTED  = RGB(0x86, 0x93, 0xA8);
static const COLORREF ACCENT     = RGB(0x4E, 0x8A, 0xD8);
static const COLORREF ACCENT_LT  = RGB(0x91, 0xB8, 0xEF);
static const COLORREF LOG_BG     = RGB(0xD8, 0xE0, 0xEC);
static const wchar_t* g_bodyFont = L"Segoe UI";
static const wchar_t* g_headFont = L"Segoe UI";
static const wchar_t* FontPick(std::initializer_list<const wchar_t*> cand);
static void InitFonts();
static const float LOG_H = 136.0f;

static const float NAV_H = 46.0f;
static const float SIDE_W = 246.0f;

// Подбор шрифтов (по наличию в системе): тело — Segoe UI Variable, заголовки — Display
static const wchar_t* FontPick(std::initializer_list<const wchar_t*> cand) {
    for (const wchar_t* f : cand) {
        Gdiplus::FontFamily ff(f);
        if (ff.GetLastStatus() == Ok) return f;
    }
    return L"Segoe UI";
}
static void InitFonts() {
    g_bodyFont = FontPick({ L"Segoe UI Variable Text", L"Segoe UI", L"Tahoma" });
    g_headFont = FontPick({ L"Segoe UI Variable Display", L"Segoe UI Semibold", L"Segoe UI" });
}

// ---------------------------------------------------------------------------
// Макет окна
// ---------------------------------------------------------------------------
struct Layout {
    RECT logo;
    RECT nav[3];
    RECT statusCard, statusTok, statusPass;
    RECT headerTitle;
    RECT content, console;
    RECT btnRefresh, btnDownload, btnDelete, btnWipe;
    std::vector<RECT> storeRows;
    RECT pathField, btnBrowseFile, btnBrowseDir, storeField, btnUpload, storeDrop;   // storeDrop — стрелочка «▾»
    RECT btnSelftest, selftestHint;
    int W = 1, H = 1;
} L;

static HWND g_hwnd;
static HINSTANCE g_hInst;
static ULONG_PTR g_gdip;

// конечный автомат UI
static int  g_tab = 0;                 // 0 хранилища, 1 загрузка, 2 проверка
static POINT g_mouse{ -100, -100 };
static bool g_down = false;
static int  g_downId = 0;
static int  g_listScroll = 0, g_logScroll = 0;
static std::string g_log;
static bool g_running = false;
static std::vector<std::string> g_stores;
static int g_sel = -1;
static int g_open = -1;            // индекс открытого хранилища в g_stores; -1 = список хранилищ
struct GcbItem { std::string name, id, sizeText; };
static std::vector<GcbItem> g_items;    // файлы открытого хранилища
static std::vector<char> g_checked;     // отметки «скачать» для g_items (1 = выбрано)
static HWND g_editPath = nullptr, g_editStore = nullptr;
static HWND g_phPath = nullptr, g_phStore = nullptr;
static WNDPROC g_editOldPath = nullptr, g_editOldStore = nullptr;
static bool g_storeDropOpen = false;   // раскрыт ли выпадающий список хранилищ во вкладке Загрузка
static int  g_storeDropHover = -1;     // подсветка строки при наведении
#define WM_APP_PH (WM_APP + 3)

static std::map<std::string, Gdiplus::Bitmap*> g_shCache;

enum CmdType { C_NONE = 0, C_LIST, C_UPLOAD, C_DOWNLOAD, C_DELETE, C_WIPE, C_SELFTEST, C_LIST_ITEMS };
enum BtnId {
    B_NONE = 0,
    NAV_STORES = 1000, NAV_UPLOAD, NAV_SELFTEST,
    ACT_REFRESH = 2000, ACT_DOWNLOAD, ACT_DELETE, ACT_WIPE,
    ACT_BROWSE_FILE, ACT_BROWSE_DIR, ACT_UPLOAD, ACT_SELFTEST_RUN, ACT_STORE_DROP,
    ACT_BACK = 2100, ACT_DL_SEL, ACT_DL_ONE, ACT_DL_ALL
};

#define WM_APP_DONE    (WM_APP + 1)
#define WM_APP_PROMPT  (WM_APP + 2)
enum InBtn { IN_OK = 7001, IN_CANCEL = 7002 };
#define IDC_IN_EDIT 7101

static void Repaint() { InvalidateRect(g_hwnd, nullptr, TRUE); }

// ---------------------------------------------------------------------------
// Рисование: скругления, мягкие тени, неоморфные элементы
// ---------------------------------------------------------------------------
static void RoundPath(GraphicsPath& p, RECT r, float rad) {
    if (rad < 2) { p.AddRectangle(RectF((float)r.left, (float)r.top, RW(r), RH(r))); return; }
    float x = (float)r.left, y = (float)r.top, w = RW(r), h = RH(r), d = rad * 2;
    if (d > w) d = w; if (d > h) d = h;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

// Плавное размытие по Гауссу (разделимое, ядро из экспоненты) — даёт мягкую,
// «без артефактов» тень вместо грубого box-blur.
static void BlurBitmap(Gdiplus::Bitmap* bmp, float sigma) {
    BitmapData bd;
    Rect rc(0, 0, bmp->GetWidth(), bmp->GetHeight());
    bmp->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppARGB, &bd);
    int w = bd.Width, h = bd.Height, stride = bd.Stride;
    BYTE* src = (BYTE*)bd.Scan0;

    int rad = (int)(sigma * 3.0f); if (rad < 1) rad = 1;
    float s2 = sigma * sigma * 2.0f;
    std::vector<float> k((size_t)2 * rad + 1);
    float sum = 0;
    for (int i = -rad; i <= rad; i++) { k[i + rad] = (float)expf(-(float)(i * i) / s2); sum += k[i + rad]; }
    for (auto& v : k) v /= sum;

    std::vector<BYTE> t1((size_t)stride * h);
    BYTE* src2 = (BYTE*)bd.Scan0;   // входная строка
    // 1) горизонтально: src -> t1
    for (int y = 0; y < h; y++) {
        const BYTE* ri = src + y * stride; BYTE* ro = t1.data() + y * stride;
        for (int x = 0; x < w; x++) {
            float a[4] = { 0, 0, 0, 0 };
            for (int i = -rad; i <= rad; i++) {
                int nx = x + i; if (nx < 0) nx = 0; else if (nx >= w) nx = w - 1;
                const BYTE* p = ri + nx * 4; float kv = k[i + rad];
                a[0] += p[0] * kv; a[1] += p[1] * kv; a[2] += p[2] * kv; a[3] += p[3] * kv;
            }
            BYTE* o = ro + x * 4;
            o[0] = (BYTE)(a[0] + 0.5f); o[1] = (BYTE)(a[1] + 0.5f);
            o[2] = (BYTE)(a[2] + 0.5f); o[3] = (BYTE)(a[3] + 0.5f);
        }
    }
    // 2) вертикально: t1 -> src
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float a[4] = { 0, 0, 0, 0 };
            for (int i = -rad; i <= rad; i++) {
                int ny = y + i; if (ny < 0) ny = 0; else if (ny >= h) ny = h - 1;
                const BYTE* p = t1.data() + ny * stride + x * 4; float kv = k[i + rad];
                a[0] += p[0] * kv; a[1] += p[1] * kv; a[2] += p[2] * kv; a[3] += p[3] * kv;
            }
            BYTE* o = src2 + y * stride + x * 4;
            o[0] = (BYTE)(a[0] + 0.5f); o[1] = (BYTE)(a[1] + 0.5f);
            o[2] = (BYTE)(a[2] + 0.5f); o[3] = (BYTE)(a[3] + 0.5f);
        }
    }
    bmp->UnlockBits(&bd);
}

static Gdiplus::Bitmap* GetShadow(float w, float h, float rad, float blur, Color col) {
    int W = (int)(w + blur * 2 + 0.5f), H = (int)(h + blur * 2 + 0.5f);
    if (W < 4) W = 4; if (H < 4) H = 4;
    char key[96];
    snprintf(key, sizeof(key), "%d:%d:%d:%08X", W, H, (int)rad, col.GetValue());
    auto it = g_shCache.find(key);
    if (it != g_shCache.end()) return it->second;
    Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(W, H, PixelFormat32bppARGB);
    {
        Graphics g(bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT sr; sr.left = (LONG)blur; sr.top = (LONG)blur;
        sr.right = sr.left + (LONG)w; sr.bottom = sr.top + (LONG)h;
        SolidBrush br(col);
        GraphicsPath p; RoundPath(p, sr, rad);
        g.FillPath(&br, &p);
    }
    BlurBitmap(bmp, blur * 0.55f);
    g_shCache[key] = bmp;
    return bmp;
}

static void DrawShadow(Graphics& g, RECT r, float rad, float blur, Color col, int dx, int dy) {
    if (blur > 6.0f) blur = 6.0f;              // компактный ореол: чёткие границы без «смазанности»
    Gdiplus::Bitmap* bmp = GetShadow(RW(r), RH(r), rad, blur, col);
    g.DrawImage(bmp, (REAL)(r.left - blur + dx), (REAL)(r.top - blur + dy),
                (REAL)bmp->GetWidth(), (REAL)bmp->GetHeight());
}

enum NState { N_RAISED, N_INSET, N_PRESSED, N_HOVER };

static void DrawNeumorphCore(Graphics& g, RECT r, float rad, NState st, COLORREF base,
                             float blur, bool stroke) {
    RECT rr = r;
    if (st == N_PRESSED) rr = Shift(rr, 0, 2);
    // широкие мягкие тени — фирменный приём неоморфизма (без жёстких граней)
    GraphicsPath p; RoundPath(p, rr, rad);
    // базовая заливка — лёгкий вертикальный градиент, объём без кантов
    COLORREF hi, lo;
    if (st == N_HOVER)      { hi = RGB(0xEF, 0xF5, 0xFD); lo = RGB(0xDE, 0xE7, 0xF2); }
    else if (st == N_PRESSED){ hi = RGB(0xD7, 0xE0, 0xED); lo = RGB(0xE6, 0xEE, 0xF7); }
    else if (st == N_INSET) { hi = RGB(0xE6, 0xED, 0xF7); lo = RGB(0xF3, 0xF8, 0xFD); }
    else                    { hi = RGB(0xF1, 0xF6, 0xFD); lo = RGB(0xDA, 0xE4, 0xEF); }
    LinearGradientBrush gr(PointF((REAL)rr.left, (REAL)rr.top),
                           PointF((REAL)rr.left, (REAL)rr.bottom), C(hi), C(lo));
    g.FillPath(&gr, &p);

    if (st == N_INSET) {
        // НАСТОЯЩИЙ inset: тени ВНУТРИ формы (тёмная сверху-слева, светлая снизу-справа), без внешней
        g.SetClip(&p);
        DrawShadow(g, rr, rad, blur, C(SH_DARK, 150), -3, -3);   // внутренняя тень сверху-слева
        DrawShadow(g, rr, rad, blur, C(SH_LIGHT, 150), 3, 3);    // внутренний свет снизу-справа
        g.ResetClip();
    } else {
        DrawShadow(g, r, rad, blur, C(SH_DARK, 120), 2, 3);      // лёгкая тень снизу-справа
        DrawShadow(g, r, rad, blur, C(SH_LIGHT, 160), -1, -1);   // свет сверху-слева
    }
    (void)base; (void)stroke;
}

static void DrawFieldBox(Graphics& g, RECT r) { DrawNeumorphCore(g, r, 13.0f, N_INSET, INNER, 9.0f, false); }

static void DrawTextStr(Graphics& g, const std::wstring& s, RECT r, const Font& f,
                        const Brush& br, int ha, int va) {
    StringFormat fmt;
    fmt.SetAlignment((StringAlignment)ha);
    fmt.SetLineAlignment((StringAlignment)va);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(s.c_str(), (INT)s.size(), &f,
                 RectF((REAL)r.left, (REAL)r.top, RW(r), RH(r)), &fmt, &br);
}

static void DrawTextC(Graphics& g, const std::string& s, RECT r, float size,
                      COLORREF col, bool bold, int ha, int va) {
    Font f(g_bodyFont, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush br(C(col));
    DrawTextStr(g, wstr(s), r, f, br, ha, va);
}

// Одна строка с многоточием (без переноса) — для имён в списках
static void DrawTextCOne(Graphics& g, const std::string& s, RECT r, float size,
                         COLORREF col, bool bold, int ha, int va) {
    Font f(g_bodyFont, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush br(C(col));
    StringFormat fmt;
    fmt.SetAlignment((StringAlignment)ha);
    fmt.SetLineAlignment((StringAlignment)va);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(wstr(s).c_str(), (INT)wstr(s).size(), &f,
                 RectF((REAL)r.left, (REAL)r.top, RW(r), RH(r)), &fmt, &br);
}

static void DrawHeadC(Graphics& g, const std::string& s, RECT r, float size,
                      COLORREF col, int va) {
    Font f(g_headFont, size, FontStyleBold, UnitPixel);
    SolidBrush br(C(col));
    DrawTextStr(g, wstr(s), r, f, br, 0, va);
}

static void DrawButton(Graphics& g, RECT r, const std::string& label, NState st,
                       bool accent, bool disabled) {
    float rad = RH(r) / 2.0f;
    if (accent) {
        DrawShadow(g, r, rad, 8, C(SH_DARK, 170), 3, 4);    // мягкая тень снизу
        RECT rr = st == N_PRESSED ? Shift(r, 0, 2) : r;
        GraphicsPath p; RoundPath(p, rr, rad);
        // вертикальный градиент + полупрозрачный блик сверху — объём без «плоского» drop-shadow
        COLORREF hi, lo;
        if (disabled)            { hi = RGB(0xB6, 0xC3, 0xD5); lo = RGB(0x9C, 0xAB, 0xC0); }
        else if (st == N_PRESSED){ hi = RGB(0x2F, 0x64, 0xA8); lo = RGB(0x4A, 0x7E, 0xC9); }
        else if (st == N_HOVER)  { hi = RGB(0x6A, 0xA2, 0xE6); lo = RGB(0x3F, 0x77, 0xC4); }
        else                     { hi = ACCENT_LT;            lo = ACCENT; }
        LinearGradientBrush gr(PointF((REAL)rr.left, (REAL)rr.top),
                               PointF((REAL)rr.left, (REAL)rr.bottom), C(hi), C(lo));
        g.FillPath(&gr, &p);
        // внутренний светлый блик по верхним двум третям — фирменный «блик» акцентной кнопки
        GraphicsPath hp; RECT hr = Shift(rr, 0, -2);
        RoundPath(hp, hr, rad);
        LinearGradientBrush hg(PointF((REAL)hr.left, (REAL)hr.top),
                               PointF((REAL)hr.left, (REAL)(hr.top + RH(hr) * 0.6f)),
                               C(0xFFFFFF, 105), C(0xFFFFFF, 0));
        g.FillPath(&hg, &hp);
        Font f(g_bodyFont, 15.5f, FontStyleBold, UnitPixel);
        SolidBrush tw(C(0xFFFFFF, 245)), sh2(C(0x000000, 45));
        DrawTextStr(g, wstr(label), Shift(rr, 0, 1), f, sh2, 1, 1);
        DrawTextStr(g, wstr(label), rr, f, tw, 1, 1);
    } else {
        DrawNeumorphCore(g, r, rad, disabled ? N_RAISED : st, BG, 17.0f, true);
        DrawTextC(g, label, r, 14.5f, disabled ? TXT_MUTED : TXT, true, 1, 1);
    }
}

// ---------------------------------------------------------------------------
// Макет (всё пересчитывается от размера клиентской области)
// ---------------------------------------------------------------------------
static void UpdateLayout(int w, int h, const std::vector<std::string>& stores) {
    L.W = w; L.H = h;
    const int sb = (int)SIDE_W;
    const int cx = sb / 2;
    SetR(L.logo, cx - 34, 40, cx + 34, 108);
    int nx = 26, nw = sb - 52;
    int y = 180;
    for (int i = 0; i < 3; i++) {
        SetR(L.nav[i], nx, y, nx + nw, y + (int)NAV_H);
        y += (int)NAV_H + 14;
    }
    SetR(L.statusCard, nx, h - 156, nx + nw, h - 40);
    // строки статуса — компактно: подпись, затем 2 строки с равными интервалами
    SetR(L.statusTok, L.statusCard.left + 18, L.statusCard.top + 42,
         L.statusCard.right - 18, L.statusCard.top + 42 + 28);
    SetR(L.statusPass, L.statusTok.left, L.statusTok.bottom + 8,
         L.statusTok.right, L.statusTok.bottom + 36);

    int bodyL = sb + 30, bodyT = 24, bodyR = w - 28, bodyB = h - 26;
    SetR(L.headerTitle, bodyL, bodyT, bodyR, bodyT + 34);
    int consH = (int)LOG_H;
    SetR(L.console, bodyL, bodyB - consH, bodyR, bodyB);
    SetR(L.content, bodyL, bodyT + 66, bodyR, L.console.top - 12);

    int cy = L.content.top;
    int bGap = 14;
    int bW = (RW(L.content) - bGap * 3) / 4;
    if (bW > 170) bW = 170;
    SetR(L.btnRefresh, L.content.left, cy, L.content.left + bW, cy + 42);
    SetR(L.btnDownload, L.btnRefresh.right + bGap, cy, L.btnRefresh.right + bGap + bW, cy + 42);
    SetR(L.btnDelete, L.btnDownload.right + bGap, cy, L.btnDownload.right + bGap + bW, cy + 42);
    SetR(L.btnWipe, L.btnDelete.right + bGap, cy, L.btnDelete.right + bGap + bW, cy + 42);

    int rowsTop = cy + 60;
    L.storeRows.clear();
    int rh = 56, gap = 16;
    for (size_t i = 0; i < stores.size(); i++) {
        RECT r; SetR(r, L.content.left, rowsTop + (int)i * (rh + gap),
                     L.content.right, rowsTop + (int)i * (rh + gap) + rh);
        L.storeRows.push_back(r);
    }

    int ftop = L.content.top + 26;
    int fw = (L.content.right - L.content.left) - 158;
    SetR(L.pathField, L.content.left, ftop, L.content.left + fw, ftop + 52);
    SetR(L.btnBrowseFile, L.pathField.right + 14, ftop, L.pathField.right + 14 + 144, ftop + 52);
    SetR(L.btnBrowseDir, L.btnBrowseFile.left, L.btnBrowseFile.top + 66,
         L.btnBrowseFile.right, L.btnBrowseFile.bottom + 66);
    int sfTop = L.btnBrowseDir.bottom + 22;
    SetR(L.storeField, L.content.left, sfTop, L.content.right, sfTop + 52);
    // кнопка «▾» для выпадающего списка хранилищ — правый край поля
    SetR(L.storeDrop, L.storeField.right - 54, sfTop, L.storeField.right - 14, sfTop + 52);
    int upW = 240, upX = L.content.left + (L.content.right - L.content.left - upW) / 2;
    SetR(L.btnUpload, upX, L.storeField.bottom + 34, upX + upW, L.storeField.bottom + 34 + 60);

    int half = (L.content.right - L.content.left) / 2;
    SetR(L.btnSelftest, L.content.left, L.content.top + 8,
         L.content.left + half - 24, L.content.top + 8 + 58);
    SetR(L.selftestHint, L.btnSelftest.right + 26, L.content.top + 8,
         L.content.right, L.btnSelftest.bottom);
}

static void UpdatePlaceholders();   // fwd (поля/плейсхолдеры)

static void RefreshLayout() {
    RECT cr; GetClientRect(g_hwnd, &cr);
    UpdateLayout(cr.right, cr.bottom, g_stores);
    if (g_editPath) MoveWindow(g_editPath, L.pathField.left + 2, L.pathField.top + 2,
                               L.pathField.right - L.pathField.left - 4,
                               L.pathField.bottom - L.pathField.top - 4, TRUE);
    if (g_editStore) MoveWindow(g_editStore, L.storeField.left + 2, L.storeField.top + 2,
                                L.storeField.right - L.storeField.left - 66,
                                L.storeField.bottom - L.storeField.top - 4, TRUE);
    // поля видны только на вкладке «Загрузка»
    ShowWindow(g_editPath, g_tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_editStore, g_tab == 1 ? SW_SHOW : SW_HIDE);
    if (g_phPath) MoveWindow(g_phPath, L.pathField.left + 4, L.pathField.top + 2,
                             L.pathField.right - L.pathField.left - 8,
                             L.pathField.bottom - L.pathField.top - 4, TRUE);
    if (g_phStore) MoveWindow(g_phStore, L.storeField.left + 4, L.storeField.top + 2,
                              L.storeField.right - L.storeField.left - 66,
                              L.storeField.bottom - L.storeField.top - 4, TRUE);
    UpdatePlaceholders();
}

// ---------------------------------------------------------------------------
// Хит-тест
// ---------------------------------------------------------------------------
// Кнопки верхнего ряда зависят от уровня: список хранилищ (g_open == -1)
// либо файлы внутри хранилища (g_open >= 0).
static BtnId TopBtnForSlot(int slot) {   // 0=btnRefresh 1=btnDownload 2=btnDelete 3=btnWipe
    if (g_open >= 0) {
        static const BtnId in[4] = { ACT_BACK, ACT_DL_SEL, ACT_DL_ALL, ACT_REFRESH };
        return in[slot];
    }
    static const BtnId out[4] = { ACT_REFRESH, ACT_DOWNLOAD, ACT_DELETE, ACT_WIPE };
    return out[slot];
}

static int CheckedCount() {
    int n = 0;
    for (char c : g_checked) if (c) n++;
    return n;
}

static bool IsDisabled(BtnId id) {
    if (g_open >= 0) {   // уровень «внутри хранилища»
        switch (id) {
        case ACT_DL_SEL:
            return g_running || CheckedCount() == 0;
        case ACT_DL_ALL:
            return g_running || g_items.empty();
        case ACT_BACK:
        case ACT_REFRESH:
            return g_running;
        default:
            return g_running;
        }
    }
    // уровень списка хранилищ
    if (id == ACT_DOWNLOAD || id == ACT_DELETE || id == ACT_WIPE)
        return g_running || g_sel < 0 || g_sel >= (int)g_stores.size();
    if (id == ACT_UPLOAD) {
        if (!g_editPath || !g_editStore) return true;
        wchar_t p[512]; GetWindowTextW(g_editPath, p, 512);
        wchar_t s[512]; GetWindowTextW(g_editStore, s, 512);
        if (p[0] == 0 || s[0] == 0) return true;
    }
    return g_running;
}

static int HitBtn(POINT p) {
    int x = p.x, y = p.y;
    for (int i = 0; i < 3; i++) if (PtIn(L.nav[i], x, y)) return NAV_STORES + i;
    if (g_tab == 0) {
        if (PtIn(L.btnRefresh, x, y)) return TopBtnForSlot(0);
        if (PtIn(L.btnDownload, x, y)) return TopBtnForSlot(1);
        if (PtIn(L.btnDelete, x, y)) return TopBtnForSlot(2);
        if (PtIn(L.btnWipe, x, y)) return TopBtnForSlot(3);
        return B_NONE;
    }
    if (PtIn(L.btnBrowseFile, x, y)) return ACT_BROWSE_FILE;
    if (PtIn(L.btnBrowseDir, x, y)) return ACT_BROWSE_DIR;
    if (PtIn(L.storeDrop, x, y)) return ACT_STORE_DROP;
    if (PtIn(L.btnUpload, x, y)) return ACT_UPLOAD;
    if (PtIn(L.btnSelftest, x, y)) return ACT_SELFTEST_RUN;
    return B_NONE;
}

static int HitStoreDropRow(POINT p) {
    if (!g_storeDropOpen) return -1;
    for (size_t i = 0; i < g_stores.size(); i++) {
        RECT r = { L.storeField.left, L.storeField.bottom + 6 + (int)i * 42,
                   L.storeField.right - 14, L.storeField.bottom + 6 + (int)i * 42 + 38 };
        if (PtIn(r, p.x, p.y)) return (int)i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Запуск python-скрипта в фоне
// ---------------------------------------------------------------------------
struct CmdJob { std::vector<std::wstring> args; int type; };

static bool Launch(const std::wstring& cmdline, std::string& out) {
    HANDLE hRead = nullptr, hWrite = nullptr;
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof sa; sa.bInheritHandle = TRUE;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hWrite, HANDLE_FLAG_INHERIT, 1);
    STARTUPINFOW si{}; si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = hWrite; si.hStdError = hWrite;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmdline.size() + 1);
    wcscpy(buf.data(), cmdline.c_str());
    std::wstring wd = ScriptDir();
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, wd.c_str(), &si, &pi);
    CloseHandle(hWrite);
    if (!ok) { CloseHandle(hRead); out.clear(); return false; }
    CloseHandle(pi.hThread);
    char tmp[8192]; DWORD rd; std::string acc;
    for (;;) {
        if (!ReadFile(hRead, tmp, sizeof tmp, &rd, nullptr) || rd == 0) break;
        acc.append(tmp, rd);
    }
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    out = acc;
    return true;
}

// ---------------------------------------------------------------------------
// Находит Python, в котором реально есть обе библиотеки, и кэширует его.
// Лечит «порождение» машин: нужный Python может не быть в PATH, но есть как
// зарегистрированный (py --list-paths) — перебираем все и берём рабочий.
// ---------------------------------------------------------------------------
static std::wstring g_py; // выбранный интерпретатор (инфикс с завершающим пробелом)

static bool ProbePy(const std::wstring& base) {
    std::string out;
    return Launch(base + L"-c \"import py7zr,github;print('GOK')\"", out)
        && out.find("GOK") != std::string::npos;
}

// Список установленных Python из py-лаунчера (пути к python.exe).
static std::vector<std::wstring> PyListPaths() {
    std::vector<std::wstring> res;
    std::string s;
    if (!Launch(L"py --list-paths", s)) return res;
    std::wstring w = Utf8ToWide(s);
    const wchar_t* kw = L".exe";
    size_t pos = 0;
    while ((pos = w.find(kw, pos)) != std::wstring::npos) {
        size_t start = pos;
        while (start > 0 && w[start - 1] != L'\n' && w[start - 1] != L'\r') start--;
        size_t mid = pos;
        while (mid > start && w[mid - 1] != L' ' && w[mid - 1] != L'\t') mid--;
        res.push_back(L"\"" + w.substr(mid, pos + 4 - mid) + L"\" ");
        pos += 4;
    }
    return res;
}

static std::wstring PickPython() {
    if (!g_py.empty()) return g_py;
    std::vector<std::wstring> cand;
    cand.emplace_back(L"python ");
    cand.emplace_back(L"py -3 ");
    cand.emplace_back(L"python3 ");
    for (auto& p : PyListPaths()) cand.push_back(p);
    for (auto& c : cand) {
        if (ProbePy(c)) { g_py = c; return g_py; }
    }
    return L"";
}

// ---------------------------------------------------------------------------
// Выбор движка команды (single-file!):
//   1) рядом лежит GITHUBCLOAD_core.exe — используем его (разработка/обновление);
//   2) иначе извлекаем вшитый в ресурсы GUI GITHUBCLOAD_core.exe во временную
//      папку и используем его — GUI становится ОДНИМ файлом;
//   3) иначе откат: python + скрипт рядом.
// Возвращает префикс командной строки движка (без аргументов команды),
// либо пустую строку, если ничего не вышло.
// ---------------------------------------------------------------------------
static std::wstring g_engine;          // кэш выбранного движка (префикс cmd)
static std::wstring g_engineNote;      // что выбрали (для лога)
static bool        g_engineIsEmbedded = false; // движок = извлечённый из ресурса

// Версия встроенного ядра в имени файла кэша: увеличивайте при пересборке GUI
// с новым ядром, чтобы на машинах гарантированно распаковалось СВЕЖЕЕ ядро
// (переиспользование по размеру может совпасть со старой версией).
static const int ENGINE_CACHE_VER = 4;

// Постоянная папка для извлечённого ядра: %LOCALAPPDATA%\GITHUBCLOAD_engine\
// (переживает перезагрузки; антивирус после первой проверки не трогает).
// %TEMP% не годится: temp-папки чистятся и чаще перепроверяются антивирусом.
static std::wstring EngineCacheDir() {
    wchar_t buf[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf) == S_OK) {
        std::wstring dir = std::wstring(buf) + L"\\GITHUBCLOAD_engine";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    return L"";
}

static std::wstring TempCorePath() {
    std::wstring dir = EngineCacheDir();
    if (!dir.empty()) {
        wchar_t name[64];
        swprintf(name, 64, L"GITHUBCLOAD_core_%d.exe", ENGINE_CACHE_VER);
        return dir + L"\\" + name;
    }
    wchar_t tmp[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tmp)) return L"";
    return std::wstring(tmp) + L"GITHUBCLOAD_core.exe";
}

// Извлекает вшитое ядро во временную папку. Если там уже лежит ядро того же
// размера — переиспользует (антивирус уже проверил; меньше гонок).
static std::wstring ExtractEmbeddedCore(bool force = false) {
    HRSRC hr = FindResourceW(nullptr, MAKEINTRESOURCEW(1), (LPCWSTR)RT_RCDATA);
    if (!hr) return L"";
    HGLOBAL hg = LoadResource(nullptr, hr);
    if (!hg) return L"";
    void* data = LockResource(hg);
    DWORD size = SizeofResource(nullptr, hr);
    if (!data || size == 0) return L"";
    std::wstring outPath = TempCorePath();
    if (outPath.empty()) return L"";
    if (!force) {
        HANDLE hEx = CreateFileW(outPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, 0, nullptr);
        if (hEx != INVALID_HANDLE_VALUE) {
            DWORD sz = GetFileSize(hEx, nullptr);
            CloseHandle(hEx);
            if (sz == size) return outPath;
        }
    }
    HANDLE hf = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return L"";
    DWORD written = 0;
    BOOL wok = WriteFile(hf, data, size, &written, nullptr);
    CloseHandle(hf);
    if (!wok || written != size) return L"";
    return outPath;
}

static std::wstring EngineCmd() {
    if (!g_engine.empty()) return g_engine;
    std::wstring external = ScriptDir() + L"\\GITHUBCLOAD_core.exe";
    if (GetFileAttributesW(external.c_str()) != INVALID_FILE_ATTRIBUTES) {
        g_engine = L"\"" + external + L"\"";
        g_engineNote = L"core.exe рядом";
        g_engineIsEmbedded = false;
        return g_engine;
    }
    std::wstring embedded = ExtractEmbeddedCore();
    if (!embedded.empty()) {
        g_engine = L"\"" + embedded + L"\"";
        g_engineNote = L"встроенное ядро (временная папка)";
        g_engineIsEmbedded = true;
        return g_engine;
    }
    g_engine = L"py"; // признак «python-режим»; настоящий префикс строится ниже
    g_engineNote = L"python + скрипт";
    g_engineIsEmbedded = false;
    return g_engine;
}

// Папка, где GUI ищет tokengh.txt / PBEpass.txt — сообщаем её ядру через
// переменную окружения (ядро может лежать во временной папке).
static void SetupEnvForCore() {
    SetEnvironmentVariableW(L"GCB_APP_DIR", ScriptDir().c_str());
}

static DWORD WINAPI CmdThread(LPVOID p) {
    std::unique_ptr<CmdJob> job((CmdJob*)p);
    std::wstring script = ScriptDir() + L"\\GITHUBCLOAD.py";
    std::wstring argsSuffix;
    for (auto& a : job->args) {
        bool needQ = a.find(L' ') != std::wstring::npos || a.find(L'\t') != std::wstring::npos || a.find(L'"') != std::wstring::npos;
        if (needQ) {
            argsSuffix += L" \"";
            for (wchar_t c : a) { if (c == L'"') argsSuffix += L"\\\""; else argsSuffix += c; }
            argsSuffix += L"\"";
        } else {
            argsSuffix += L" " + a;
        }
    }
    // Движок: рядом core.exe → встроенный core.exe → python + скрипт
    SetupEnvForCore();
    std::wstring prefix, cmdLabel;
    std::wstring eng = EngineCmd();
    if (eng == L"py") {
        prefix = PickPython() + L"\"" + script + L"\"";
        cmdLabel = PickPython();
        if (PickPython().empty()) prefix.clear();
    } else if (!eng.empty()) {
        prefix = eng;
        cmdLabel = eng;
    }
    std::string out;
    if (!prefix.empty() && prefix != L"\"\"") {
        Launch(prefix + argsSuffix, out);
        // Самолечение: антивирус или гонка распаковки могли дать «сбой импорта»
        // при ПЕРВОМ запуске извлечённого ядра. Перераспаковываем и пробуем ещё раз.
        bool importFail = out.find("НЕ УДАЛОСЬ ИМПОРТИРОВАТЬ") != std::string::npos
                       || out.find("НЕ УСТАНОВЛЕНА БИБЛИОТЕКА") != std::string::npos;
        if (g_engineIsEmbedded && importFail) {
            std::string out2;
            DeleteFileW(TempCorePath().c_str());
            std::wstring again = ExtractEmbeddedCore(true);
            if (!again.empty()) {
                g_engine = L"\"" + again + L"\"";
                cmdLabel = g_engine;
                DebugLog("RETRY", "повторный запуск извлечённого ядра");
                Launch(g_engine + argsSuffix, out2);
                if (!out2.empty()) out = out2;
            }
        }
    } else {
        out = "НЕ НАЙДЕН ДВИЖОК GITHUBCLOAD.\n"
              "GUI ищет (по порядку):\n"
              "  1) GITHUBCLOAD_core.exe рядом с собой;\n"
              "  2) встроенное в exe ядро (single-file сборка);\n"
              "  3) python с библиотеками py7zr/github.\n"
              "Если используется python-режим, установите:\n"
              "    python -m pip install py7zr PyGithub\n"
              "Затем перезапустите приложение.";
    }
    if (out.empty()) out = "Готово (пустой вывод).";
    DebugLog("CMD", u8str(cmdLabel + argsSuffix));
    DebugLog("OUT", out);
    PostMessageW(g_hwnd, WM_APP_DONE, (WPARAM)job->type, (LPARAM)new std::string(out));
    return 0;
}

static void RunCmd(std::vector<std::wstring> args, int type) {
    if (g_running) return;
    g_running = true;
    g_log.clear(); g_logScroll = 0;
    CmdJob* job = new CmdJob{ std::move(args), type };
    CreateThread(nullptr, 0, CmdThread, job, 0, nullptr);
    Repaint();
}

// ---------------------------------------------------------------------------
// Парсинг списка хранилищ
// ---------------------------------------------------------------------------
static void ParseStores(const std::string& out, std::vector<std::string>& stores) {
    stores.clear();
    const std::string m = "ХРАНИЛИЩЕ «";
    size_t pos = 0;
    while ((pos = out.find(m, pos)) != std::string::npos) {
        pos += m.size();
        size_t e = out.find("»", pos);
        if (e == std::string::npos) break;
        stores.push_back(out.substr(pos, e - pos));
        pos = e;
    }
}

// ---------------------------------------------------------------------------
// Парсинг списка файлов внутри хранилища (вывод: python ... list <store>)
// Строки вида:  - ИМЯ  [id XXXX]  РАЗМЕР
// ---------------------------------------------------------------------------
static void ParseItems(const std::string& out, std::vector<GcbItem>& items) {
    items.clear();
    size_t pos = 0;
    while (pos < out.size()) {
        size_t eol = out.find('\n', pos);
        if (eol == std::string::npos) eol = out.size();
        std::string line = out.substr(pos, eol - pos);
        pos = eol + 1;
        size_t b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos) continue;
        if (line.compare(b, 2, "- ") != 0) continue;
        std::string body = line.substr(b + 2);
        size_t m = body.find("  [id ");
        if (m == std::string::npos) continue;
        std::string name = body.substr(0, m);
        size_t i1 = m + 6;
        size_t i2 = body.find(']', i1);
        if (i2 == std::string::npos) continue;
        std::string id = body.substr(i1, i2 - i1);
        size_t r2 = body.find_first_not_of(" \t\r", i2 + 1);
        std::string sz = (r2 == std::string::npos) ? "" : body.substr(r2);
        if (!sz.empty() && sz.back() == '\r') sz.pop_back();
        if (!name.empty() && !id.empty()) items.push_back({ name, id, sz });
    }
}

// ---------------------------------------------------------------------------
// Выбор файла / папки
// ---------------------------------------------------------------------------
static bool PickFile(std::wstring& path) {
    wchar_t buf[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof ofn; ofn.hwndOwner = g_hwnd;
    ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Выберите файл для загрузки";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return false;
    path = buf; return true;
}
static bool PickFolder(std::wstring& path, const wchar_t* title = L"Выберите папку") {
    BROWSEINFOW bi{}; bi.hwndOwner = g_hwnd;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t buf[MAX_PATH];
    if (!SHGetPathFromIDListW(pidl, buf)) { CoTaskMemFree(pidl); return false; }
    CoTaskMemFree(pidl);
    path = buf; return true;
}

// ---------------------------------------------------------------------------
// Модальное окно ввода (аккуратный, с неоморфными кнопками)
// ---------------------------------------------------------------------------
static std::wstring g_inResult;
static bool g_inDone = false;
static const wchar_t* g_inCls = L"gcb_prompt";
static RECT g_inOk, g_inCancel;
static int g_inDown = 0;
static WNDPROC g_editOldProc = nullptr;

static const std::wstring& g_inTitle();
static const std::wstring& g_inLabel();

static LRESULT CALLBACK InEditSubclass(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && w == VK_RETURN) {
        PostMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(IN_OK, BN_CLICKED), (LPARAM)h);
        return 0;
    }
    return CallWindowProcW(g_editOldProc, h, m, w, l);
}

static void InLayout(HWND hwnd) {
    RECT cr; GetClientRect(hwnd, &cr);
    int bw = 120, by = cr.bottom - 82, bh = 46;
    SetR(g_inCancel, cr.right / 2 - bw - 10, by, cr.right / 2 - 10, by + bh);
    SetR(g_inOk, cr.right / 2 + 10, by, cr.right / 2 + 10 + bw, by + bh);
}

static LRESULT CALLBACK InWndProc(HWND hwnd, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        RECT cr; GetClientRect(hwnd, &cr);
        HWND ed = CreateWindowExW(0, L"EDIT", g_inResult.empty() ? L"" : g_inResult.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            30, 116, cr.right - 60, 40, hwnd, (HMENU)IDC_IN_EDIT, g_hInst, nullptr);
        HFONT f = CreateFontW(-18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, g_bodyFont);
        SendMessageW(ed, WM_SETFONT, (WPARAM)f, TRUE);
        g_editOldProc = (WNDPROC)SetWindowLongPtrW(ed, GWLP_WNDPROC, (LONG_PTR)InEditSubclass);
        g_inDown = 0; g_inOk = {}; g_inCancel = {};
        break;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetBkColor(dc, INNER);
        SetTextColor(dc, TXT);
        static HBRUSH br = CreateSolidBrush(INNER);
        return (LRESULT)br;
    }
    case WM_ERASEBKGND: return 1;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)l)->ptMinTrackSize.x = 430;
        ((MINMAXINFO*)l)->ptMinTrackSize.y = 250;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        RECT cr; GetClientRect(hwnd, &cr);
        HDC mdc = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        HGDIOBJ ob = SelectObject(mdc, bmp);
        {   Graphics g(mdc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            LinearGradientBrush bg(PointF(0, 0), PointF((REAL)cr.right, (REAL)cr.bottom),
                                   C(RGB(0xED, 0xF2, 0xFA)), C(RGB(0xDD, 0xE6, 0xF0)));
            g.FillRectangle(&bg, 0, 0, cr.right, cr.bottom);
            {   Font ft(g_bodyFont, 19.0f, FontStyleBold, UnitPixel);
                SolidBrush btb(C(TXT));
                DrawTextStr(g, g_inTitle(), RECT{ 60, 22, cr.right - 60, 58 }, ft, btb, 0, 0);
            }
            {   Font fl(g_bodyFont, 13.0f, FontStyleRegular, UnitPixel);
                SolidBrush blb(C(TXT_MUTED));
                DrawTextStr(g, g_inLabel(), RECT{ 60, 62, cr.right - 60, 90 }, fl, blb, 0, 0);
            }
            InLayout(hwnd);
            DrawButton(g, g_inCancel, "Отмена", g_inDown == IN_CANCEL ? N_PRESSED : N_RAISED, false, false);
            DrawButton(g, g_inOk, "ОК", g_inDown == IN_OK ? N_PRESSED : N_RAISED, true, g_inResult.empty());
            // тонкая рамка
            Pen pen(C(SH_DARK, 40), 1.0f);
            GraphicsPath cpath; RoundPath(cpath, { 2,2,cr.right-2,cr.bottom-2 }, 12);
            g.DrawPath(&pen, &cpath);
        }
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, ob); DeleteObject(bmp); DeleteDC(mdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        g_inDown = PtIn(g_inOk, p.x, p.y) ? IN_OK : (PtIn(g_inCancel, p.x, p.y) ? IN_CANCEL : 0);
        break;
    }
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        g_inDown = PtIn(g_inOk, p.x, p.y) ? IN_OK : (PtIn(g_inCancel, p.x, p.y) ? IN_CANCEL : 0);
        break;
    }
    case WM_LBUTTONUP: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        int id = PtIn(g_inOk, p.x, p.y) ? IN_OK : (PtIn(g_inCancel, p.x, p.y) ? IN_CANCEL : 0);
        if (id == IN_OK && g_inDown == IN_OK) {
            if (!g_inResult.empty()) { g_inDone = true; }
        } else if (id == IN_CANCEL || g_inDown == IN_CANCEL) {
            g_inResult.clear();
            g_inDone = true;
        }
        g_inDown = 0;
        break;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDC_IN_EDIT && HIWORD(w) == EN_CHANGE) {
            HWND ed = GetDlgItem(hwnd, IDC_IN_EDIT);
            wchar_t b[2048]; GetWindowTextW(ed, b, 2048); g_inResult = b;
        } else if (LOWORD(w) == IN_OK) {
            if (!g_inResult.empty()) g_inDone = true;
        }
        break;
    case WM_CLOSE:
        g_inResult.clear();
        g_inDone = true;
        return 0;
    }
    return DefWindowProcW(hwnd, m, w, l);
}

static const std::wstring& g_inTitle() {
    static std::wstring t = L"GITHUBCLOAD";
    return t;
}
static const std::wstring& g_inLabel() {
    static std::wstring l = L"Введите значение:";
    return l;
}

// Возвращает введённую строку либо пустую (отмена)
static std::wstring InputDialog(HWND owner, const std::wstring& title,
                                const std::wstring& label, const std::wstring& def) {
    static std::wstring t_, l_;
    t_ = title; l_ = label;
    g_inResult = def; g_inDone = false;

    HFONT f = CreateFontW(-18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                          CLEARTYPE_QUALITY, 0, g_bodyFont);
    HWND hwnd = CreateWindowExW(0, g_inCls, title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU /*| WS_THICKFRAME*/,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 260, owner, nullptr, g_hInst, nullptr);
    if (!hwnd) { DeleteObject(f); return L""; }
    // центрируем относительно владельца
    RECT ow; GetWindowRect(owner, &ow);
    RECT nr; GetWindowRect(hwnd, &nr);
    int ww = nr.right - nr.left, wh = nr.bottom - nr.top;
    int px = ow.left + (ow.right - ow.left - ww) / 2;
    int py = ow.top + (ow.bottom - ow.top - wh) / 2;
    SetWindowPos(hwnd, nullptr, px, py, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetFocus(GetDlgItem(hwnd, IDC_IN_EDIT));

    EnableWindow(owner, FALSE);
    MSG msg;
    while (!g_inDone) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) { g_inDone = true; break; }   // WM_QUIT
        if (r == -1) break;
        // не даём вложенности
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    DestroyWindow(hwnd);
    DeleteObject(f);
    return g_inResult;
}

// ---------------------------------------------------------------------------
// Действия
// ---------------------------------------------------------------------------
static void HandleAction(int id) {
    if (id >= NAV_STORES && id <= NAV_SELFTEST) {
        g_tab = id - NAV_STORES;
        g_storeDropOpen = false;
        g_storeDropHover = -1;
        RefreshLayout();
        Repaint();
        return;
    }
    if (IsDisabled((BtnId)id)) return;

    switch (id) {
    case ACT_REFRESH:
        // на уровне хранилищ — обновить список; внутри — обновить список файлов
        if (g_open >= 0)
            RunCmd({ L"list", wstr(g_stores[g_open]) }, C_LIST_ITEMS);
        else
            RunCmd({ L"-" }, C_LIST);
        break;
    case ACT_BACK:
        g_open = -1;
        g_items.clear();
        g_checked.clear();
        g_listScroll = 0;
        g_sel = -1;
        RefreshLayout();
        Repaint();
        break;
    case ACT_DL_SEL: {
        std::vector<std::wstring> ids;
        for (size_t i = 0; i < g_items.size(); i++)
            if (g_checked[i]) ids.push_back(wstr(g_items[i].id));
        if (ids.empty()) break;
        std::wstring dest;
        if (!PickFolder(dest, L"Выберите папку для скачивания")) break;
        std::vector<std::wstring> args = { L"download", wstr(g_stores[g_open]), dest };
        args.insert(args.end(), ids.begin(), ids.end());
        RunCmd(std::move(args), C_DOWNLOAD);
        break;
    }
    case ACT_DL_ALL: {
        std::wstring dest;
        if (!PickFolder(dest, L"Выберите папку для скачивания")) break;
        if (g_open >= 0)
            RunCmd({ L"download", wstr(g_stores[g_open]), dest }, C_DOWNLOAD);
        else
            RunCmd({ L"download", wstr(g_stores[g_sel]), dest }, C_DOWNLOAD);
        break;
    }
    case ACT_DL_ONE: {
        if (g_open < 0 || g_sel < 0 || g_sel >= (int)g_items.size()) break;
        std::wstring dest;
        if (!PickFolder(dest, L"Выберите папку для скачивания")) break;
        RunCmd({ L"download", wstr(g_stores[g_open]), dest, wstr(g_items[g_sel].id) }, C_DOWNLOAD);
        break;
    }
    case ACT_DOWNLOAD: {
        if (g_open >= 0) break;   // на верхнем уровне «Скачать всё» — только для списка
        std::wstring dest;
        if (!PickFolder(dest, L"Выберите папку для скачивания")) break;
        RunCmd({ L"download", wstr(g_stores[g_sel]), dest }, C_DOWNLOAD);
        break;
    }
    case ACT_DELETE: {
        if (g_open >= 0) break;
        std::wstring sid = InputDialog(g_hwnd, L"Удаление элемента",
                          L"Введите id элемента из хранилища «" + wstr(g_stores[g_sel]) + L"»:",
                          L"");
        if (sid.empty()) break;
        RunCmd({ L"delete", wstr(g_stores[g_sel]), sid }, C_DELETE);
        break;
    }
    case ACT_WIPE: {
        if (g_open >= 0) break;
        std::wstring q = L"Удалить всё хранилище «" + wstr(g_stores[g_sel]) + L"» целиком?\nЭто удалит сами репозитории-тома на GitHub.";
        if (MessageBoxW(g_hwnd, q.c_str(), L"GITHUBCLOAD — wipe", MB_YESNO | MB_ICONWARNING) != IDYES)
            break;
        RunCmd({ L"wipe", wstr(g_stores[g_sel]) }, C_WIPE);
        break;
    }
    case ACT_BROWSE_FILE: {
        std::wstring p;
        if (PickFile(p)) { SetWindowTextW(g_editPath, p.c_str()); }
        break;
    }
    case ACT_BROWSE_DIR: {
        std::wstring p;
        if (PickFolder(p, L"Выберите папку для загрузки в облако")) { SetWindowTextW(g_editPath, p.c_str()); }
        break;
    }
    case ACT_UPLOAD: {
        if (!g_editPath || !g_editStore) break;
        wchar_t pa[2048], sa[512];
        GetWindowTextW(g_editPath, pa, 2048);
        GetWindowTextW(g_editStore, sa, 512);
        if (pa[0] == 0 || sa[0] == 0) break;
        RunCmd({ L"upload", pa, sa }, C_UPLOAD);
        break;
    }
    case ACT_SELFTEST_RUN:
        RunCmd({ L"selftest" }, C_SELFTEST);
        break;
    case ACT_STORE_DROP:
        g_storeDropOpen = !g_storeDropOpen;
        g_storeDropHover = -1;
        Repaint();
        break;
    }
}

// ---------------------------------------------------------------------------
// Рендер главного окна
// ---------------------------------------------------------------------------
static void DrawSidebar(Graphics& g) {
    // логотип — неоморфное кольцо + акцентная окружность с "G"
    float rad = RH(L.logo) / 2.0f;
    DrawNeumorphCore(g, L.logo, rad, N_RAISED, BG, 18, true);
    RECT inner = L.logo; int m = 12;
    SetR(inner, L.logo.left + m, L.logo.top + m, L.logo.right - m, L.logo.bottom - m);
    {   GraphicsPath p; RoundPath(p, inner, RH(inner) / 2);
        SolidBrush b(C(ACCENT)); g.FillPath(&b, &p);
    }
    // буква G
    Font f(g_headFont, 30.0f, FontStyleBold, UnitPixel);
    SolidBrush tw(C(0xFFFFFF));
    DrawTextStr(g, L"G", inner, f, tw, 1, 1);

    RECT title = { L.logo.left - 70, L.logo.bottom + 16, L.logo.left + 70, L.logo.bottom + 46 };
    // ширина сайдбара ~246, логотип 68; заголовок по центру логотипа
    SetR(title, L.logo.left - 40, L.logo.bottom + 16, L.logo.right + 40, L.logo.bottom + 44);
    RECT sub = title; sub.top += 22; sub.bottom += 24;
    DrawTextC(g, "GITHUB", title, 17.0f, TXT, true, 1, 1);
    DrawTextC(g, "приватное облако", sub, 12.5f, RGB(0x6C, 0x7B, 0x92), false, 1, 1);

    // навигация
    const char* navs[] = { "Хранилища", "Загрузка", "Самопроверка" };
    for (int i = 0; i < 3; i++) {
        NState st = (g_tab == i) ? N_INSET
                                 : (g_down && g_downId == NAV_STORES + i && PtIn(L.nav[i], g_mouse.x, g_mouse.y)
                                       ? N_PRESSED
                                       : (HitBtn(g_mouse) == NAV_STORES + i ? N_HOVER : N_RAISED));
        if (g_tab == i) {
            DrawNeumorphCore(g, L.nav[i], 20.0f, N_INSET, INNER, 9.0f, false);
            DrawTextC(g, navs[i], L.nav[i], 15.0f, ACCENT, true, 1, 1);
        } else {
            DrawNeumorphCore(g, L.nav[i], 20.0f, st, BG, 17.0f, true);
            DrawTextC(g, navs[i], L.nav[i], 14.5f, TXT, true, 1, 1);
        }
    }

    // карточка статуса токена/пароля
    DrawNeumorphCore(g, L.statusCard, 16.0f, N_INSET, INNER, 8.0f, false);
    // проверяем наличие файлов
    auto exists = [](const wchar_t* fn) {
        return GetFileAttributesW((ScriptDir() + L"\\" + fn).c_str()) != INVALID_FILE_ATTRIBUTES;
    };
    bool tok = exists(L"tokengh.txt"), pass = exists(L"PBEpass.txt");
    DrawTextC(g, "Статус подключения", { L.statusCard.left + 16, L.statusCard.top + 12, L.statusCard.right - 16, L.statusCard.top + 34 },
              12.5f, TXT_MUTED, true, 0, 0);
    DrawTextC(g, tok ? "Токен GitHub: OK" : "Токен GitHub: нет",
              L.statusTok, 13.0f, tok ? RGB(0x3C, 0x9A, 0x6E) : RGB(0xC8, 0x6A, 0x5A), true, 0, 1);
    DrawTextC(g, pass ? "Пароль: OK" : "Пароль: нет",
              L.statusPass, 13.0f, pass ? RGB(0x3C, 0x9A, 0x6E) : RGB(0xC8, 0x6A, 0x5A), true, 0, 1);
}

// Геометрия строк списка (используется и при отрисовке, и при кликах)
static int RowH() { return 56; }
static int RowGap() { return 16; }
static int RowTop(int i) { return L.btnRefresh.bottom + 24 + i * (RowH() + RowGap()) - g_listScroll; }
static RECT RowRect(int i) {
    RECT r; SetR(r, L.content.left, RowTop(i), L.content.right - 6, RowTop(i) + RowH());
    return r;
}
// Область списка (клип под кнопками)
static RECT ListClip() {
    RECT c; SetR(c, L.content.left, L.btnRefresh.bottom + 24, L.content.right - 6, L.content.bottom);
    return c;
}

static void DrawCheckBox(Graphics& g, RECT r, bool on) {
    DrawNeumorphCore(g, r, 7.0f, N_INSET, INNER, 6.0f, false);
    if (on) {
        GraphicsPath p; RoundPath(p, r, 7.0f);
        SolidBrush b(C(ACCENT));
        g.FillPath(&b, &p);
        Pen pen(Color(255, 255, 255), 2.2f);
        pen.SetLineJoin(LineJoinRound);
        float x = (float)r.left, y = (float)r.top;
        g.DrawLine(&pen, x + 4.0f, y + 11.0f, x + 8.5f, y + 16.0f);
        g.DrawLine(&pen, x + 8.5f, y + 16.0f, x + 17.0f, y + 5.5f);
    } else {
        GraphicsPath p; RoundPath(p, r, 7.0f);
        Pen pen(C(ACCENT, 170), 2.0f);
        g.DrawPath(&pen, &p);
    }
}

// Компактная акцентная «таблетка» (per-row действие)
static void DrawPill(Graphics& g, RECT r, const std::string& label, NState st) {
    float rad = RH(r) / 2.0f;
    DrawShadow(g, r, rad, 12, C(SH_DARK, 200), 4, 4);
    DrawShadow(g, r, rad, 12, C(SH_LIGHT, 190), -2, -2);
    COLORREF hi, lo;
    if (st == N_PRESSED)      { hi = RGB(0x2F, 0x64, 0xA8); lo = RGB(0x4A, 0x7E, 0xC9); }
    else if (st == N_HOVER)   { hi = RGB(0x6A, 0xA2, 0xE6); lo = RGB(0x3F, 0x77, 0xC4); }
    else                      { hi = ACCENT_LT;            lo = ACCENT; }
    GraphicsPath p; RoundPath(p, r, rad);
    LinearGradientBrush gr(PointF((REAL)r.left, (REAL)r.top),
                           PointF((REAL)r.left, (REAL)r.bottom), C(hi), C(lo));
    g.FillPath(&gr, &p);
    Font f(g_bodyFont, 12.5f, FontStyleBold, UnitPixel);
    SolidBrush tw(C(0xFFFFFF, 240)), sh2(C(0x000000, 45));
    DrawTextStr(g, wstr(label), Shift(r, 0, 1), f, sh2, 1, 1);
    DrawTextStr(g, wstr(label), r, f, tw, 1, 1);
}

// Кнопка «▾» справа от поля хранилища + выпадающий список имён хранилищ
static void DrawStoreDrop(Graphics& g) {
    // зона «▾» — правый край поля; рисуем чёткий треугольник
    RECT b = L.storeDrop;
    bool over = PtIn(b, g_mouse.x, g_mouse.y);
    // разделитель между полем ввода и зоной стрелки
    Pen sep(C(g_storeDropOpen ? ACCENT : RGB(0xBF, 0xCB, 0xDC), 210), 1.0f);
    g.DrawLine(&sep, (REAL)b.left, (REAL)b.top + 10, (REAL)b.left, (REAL)b.bottom - 10);
    // треугольник (верх/низ) акцентного цвета
    int cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2;
    COLORREF tri = (over || g_storeDropOpen) ? ACCENT : TXT;
    PointF pts[3];
    if (g_storeDropOpen) {  // вверх
        pts[0] = PointF((REAL)cx - 9, (REAL)cy + 6);
        pts[1] = PointF((REAL)cx + 9, (REAL)cy + 6);
        pts[2] = PointF((REAL)cx, (REAL)cy - 8);
    } else {                // вниз
        pts[0] = PointF((REAL)cx - 9, (REAL)cy - 6);
        pts[1] = PointF((REAL)cx + 9, (REAL)cy - 6);
        pts[2] = PointF((REAL)cx, (REAL)cy + 8);
    }
    SolidBrush tb(C(tri));
    g.FillPolygon(&tb, pts, 3);

    if (!g_storeDropOpen || g_stores.empty()) return;
    // панель под полем — рисуется ПОСЛЕДНЕЙ, поверх кнопки загрузки (как меню)
    int panelH = (int)g_stores.size() * 40 + 8;
    RECT panel;
    SetR(panel, L.storeField.left, L.storeField.bottom + 6, L.storeField.right,
         L.storeField.bottom + 6 + panelH);
    DrawShadow(g, panel, 16.0f, 22, C(SH_DARK, 60), 6, 8);
    DrawNeumorphCore(g, panel, 14.0f, N_RAISED, INNER, 16.0f, true);
    for (size_t i = 0; i < g_stores.size(); i++) {
        RECT r = { panel.left + 6, panel.top + 4 + (int)i * 40,
                   panel.right - 6, panel.top + 4 + (int)i * 40 + 38 };
        bool hov = ((int)i == g_storeDropHover) || PtIn(r, g_mouse.x, g_mouse.y);
        if (hov) { DrawNeumorphCore(g, r, 9.0f, N_HOVER, BG, 8.0f, true); }
        // галочка на выбранной строке
        RECT chk = { r.left + 10, r.top + 10, r.left + 26, r.top + 26 };
        bool isSel = !g_editStore ? false : ([&]{ wchar_t t[512]; GetWindowTextW(g_editStore, t, 512);
            return wcscmp(t, wstr(g_stores[i]).c_str()) == 0; })();
        if (isSel) {
            GraphicsPath pp; RoundPath(pp, chk, 6.0f);
            SolidBrush bb(C(ACCENT));
            g.FillPath(&bb, &pp);
            Pen pen(Color(255,255,255),1.8f); pen.SetLineJoin(LineJoinRound);
            g.DrawLine(&pen,(REAL)chk.left+5,(REAL)chk.top+9,(REAL)chk.left+9,(REAL)chk.top+13);
            g.DrawLine(&pen,(REAL)chk.left+9,(REAL)chk.top+13,(REAL)chk.left+15,(REAL)chk.top+5);
        }
        RECT tx = { r.left + 34, r.top, r.right - 14, r.bottom };
        DrawTextC(g, g_stores[i], tx, 14.0f, TXT, true, 0, 1);
    }
}

// Центрированная «пустая карточка» — аккуратный empty-state вместо голой строки
static void DrawEmptyCard(Graphics& g, RECT area, const std::string& big, const std::string& small) {
    int cw = (int)(RW(area) * 0.72f); if (cw > 520) cw = 520;
    int ch = 132;
    RECT card;
    SetR(card, area.left + (int)(RW(area) - cw) / 2,
         area.top + ((int)RH(area) - ch) / 2,
         area.left + (int)(RW(area) - cw) / 2 + cw,
         area.top + ((int)RH(area) - ch) / 2 + ch);
    DrawNeumorphCore(g, card, 18.0f, N_INSET, INNER, 7.0f, false);
    // мягкая иконка-«облако» над текстом
    int ic = 40;
    RECT cir; SetR(cir, area.left + (int)RW(area) / 2 - ic / 2, card.top + 12, area.left + (int)RW(area) / 2 + ic / 2, card.top + 12 + ic);
    GraphicsPath cp; RoundPath(cp, cir, RH(cir) / 2);
    SolidBrush cb(C(ACCENT, 34)); g.FillPath(&cb, &cp);
    DrawTextC(g, "☁", cir, 17.0f, ACCENT, true, 1, 1);
    RECT t1 = { card.left + 20, card.top + 56, card.right - 20, card.top + 82 };
    DrawTextC(g, big, t1, 16.5f, TXT, true, 1, 1);
    if (!small.empty()) {
        RECT t2 = { card.left + 20, card.top + 84, card.right - 20, card.top + 112 };
        DrawTextC(g, small, t2, 12.5f, TXT_MUTED, false, 1, 0);
    }
}

static void DrawStoreRows(Graphics& g) {    bool inside = (g_open >= 0);
    RECT thead = { L.content.left, L.btnRefresh.bottom + 4, L.content.right - 6, L.btnRefresh.bottom + 22 };
    if (!inside)
        DrawTextC(g, "Ваши хранилища — клик по строке открывает том", thead, 13.0f, TXT_MUTED, false, 0, 0);
    // внутри хранилища подсказка уже в заголовке («клик…», или «отмечено файлов: N») — не дублируем

    RECT clipR = ListClip();
    if (clipR.bottom > clipR.top) {
        GraphicsPath cp; RoundPath(cp, clipR, 0);
        g.SetClip(&cp);
    }

    if (inside) {
        if (g_items.empty()) {
            DrawEmptyCard(g, clipR,
                g_running ? "Загружаем файлы хранилища…" : "В хранилище нет файлов.",
                g_running ? "" : "Загрузите файл во вкладке «Загрузка», указав это хранилище.");
            g.ResetClip();
            return;
        }
        int rowH = RowH(), gap = RowGap();
        int maxScroll = (int)g_items.size() * (rowH + gap) - (clipR.bottom - clipR.top);
        if (maxScroll < 0) maxScroll = 0;
        if (g_listScroll > maxScroll) g_listScroll = maxScroll;
        if (g_listScroll < 0) g_listScroll = 0;

        for (size_t i = 0; i < g_items.size(); i++) {
            RECT r = RowRect((int)i);
            bool on = g_checked[i] != 0;
            bool hover = PtIn(r, g_mouse.x, g_mouse.y);
            NState st = on ? N_INSET : (hover ? N_HOVER : N_RAISED);
            DrawNeumorphCore(g, r, 16.0f, st, on ? INNER : BG, 12.0f, true);
            // чекбокс
            RECT chk = { r.left + 14, r.top + 14, r.left + 38, r.top + 38 };
            DrawCheckBox(g, chk, on);
            // имя и id
            RECT nm = { chk.right + 14, r.top + 6, r.right - 132, r.top + 30 };
            DrawTextCOne(g, g_items[i].name, nm, 14.5f, TXT, true, 0, 1);
            RECT sub = { nm.left, r.bottom - 20, r.right - 132, r.bottom - 8 };
            DrawTextCOne(g, "id " + g_items[i].id + (g_items[i].sizeText.empty() ? "" : "   ·   " + g_items[i].sizeText),
                         sub, 11.0f, TXT_MUTED, false, 0, 1);
            // кнопка «Скачать» на строке
            RECT pill = { r.right - 116, r.top + 12, r.right - 14, r.bottom - 12 };
            bool pHover = PtIn(pill, g_mouse.x, g_mouse.y);
            DrawPill(g, pill, "Скачать", pHover ? N_HOVER : N_RAISED);
        }
    } else {
        if (g_stores.empty()) {
            DrawEmptyCard(g, clipR,
                g_running ? "Ищем хранилища…" : "Хранилищ ещё нет.",
                g_running ? "" : "Нажмите «Обновить» или загрузите первый файл во вкладке «Загрузка».");
            g.ResetClip();
            return;
        }
        int rowH = RowH(), gap = RowGap();
        int maxScroll = (int)g_stores.size() * (rowH + gap) - (clipR.bottom - clipR.top);
        if (maxScroll < 0) maxScroll = 0;
        if (g_listScroll > maxScroll) g_listScroll = maxScroll;
        if (g_listScroll < 0) g_listScroll = 0;

        for (size_t i = 0; i < g_stores.size(); i++) {
            RECT r = RowRect((int)i);
            bool sel = (int)i == g_sel;
            bool hover = PtIn(r, g_mouse.x, g_mouse.y);
            NState st = sel ? N_INSET : (hover ? N_HOVER : N_RAISED);
            DrawNeumorphCore(g, r, 16.0f, st, sel ? INNER : BG, 12.0f, true);
            if (sel) {
                RECT mark = { r.left + 4, r.top + 12, r.left + 8, r.bottom - 12 };
                SolidBrush mb(C(ACCENT));
                GraphicsPath mp; RoundPath(mp, mark, 2);
                g.FillPath(&mb, &mp);
            }
            RECT nm = { r.left + (sel ? 24 : 20), r.top, r.right - 100, r.top + 32 };
            DrawTextC(g, g_stores[i], nm, 15.0f, TXT, true, 0, 1);
            RECT sub = Shift(nm, 0, 24);
            DrawTextC(g, "хранилище GITHUBCLOAD", sub, 11.5f, TXT_MUTED, false, 0, 0);
            RECT arrow = { r.right - 96, r.top, r.right - 16, r.bottom };
            DrawTextC(g, sel ? "открыто ›" : "открыть ›", arrow, 12.0f,
                      sel ? ACCENT : TXT_MUTED, false, 1, 1);
        }
    }
    g.ResetClip();
}

static void DrawConsole(Graphics& g) {
    DrawNeumorphCore(g, L.console, 16.0f, N_INSET, LOG_BG, 9.0f, false);
    RECT head = { L.console.left + 20, L.console.top + 12, L.console.right - 20, L.console.top + 38 };
    std::string title = g_running ? "Консоль • выполняется…" : "Консоль — вывод GITHUBCLOAD";
    DrawTextC(g, title, head, 13.0f, g_running ? ACCENT : TXT, true, 0, 1);

    RECT inner = { L.console.left + 20, L.console.top + 44,
                   L.console.right - 20, L.console.bottom - 16 };
    int cw = (int)RW(inner) - 10, ch = (int)RH(inner);
    if (cw < 10 || ch < 10) return;
    if (g_log.empty()) {
        DrawTextC(g, g_running ? "Запускаем команду…" : "Здесь появится вывод команд.",
                  inner, 13.0f, TXT_MUTED, false, 0, 0);
        return;
    }
    // перенос строк
    Font f(g_bodyFont, 13.5f, FontStyleRegular, UnitPixel);
    std::wstring all = Utf8ToWide(g_log);
    std::vector<std::wstring> lines;
    std::wstring cur;
    auto measure = [&](const std::wstring& s) {
        RectF rr; g.MeasureString(s.c_str(), (INT)s.size(), &f, PointF(0, 0), &rr); return rr.Width;
    };
    auto flush = [&]() {
        if (cur.empty()) { lines.push_back(L""); return; }
        std::wstring line = cur, out;
        for (size_t i = 0; i < line.size(); i++) {
            std::wstring cand = out + line[i];
            if (measure(cand) > (REAL)cw && !out.empty()) {
                lines.push_back(out); out = line[i];
            } else out = cand;
        }
        lines.push_back(out);
    };
    for (wchar_t ch : all) {
        if (ch == L'\n') { flush(); cur.clear(); }
        else cur += ch;
    }
    flush();

    int lineH = 20;
    int total = (int)lines.size() * lineH;
    int maxScroll = total > ch ? total - ch : 0;
    if (g_logScroll > maxScroll) g_logScroll = maxScroll;
    if (g_logScroll < 0) g_logScroll = 0;

    Gdiplus::Bitmap* cb = new Gdiplus::Bitmap(cw, (total > 0 ? total : 1), PixelFormat32bppARGB);
    {
        Graphics gc(cb);
        gc.Clear(Color(0, 0, 0, 0));
        gc.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        SolidBrush tb(C(TXT));
        for (int i = 0; i < (int)lines.size(); i++) {
            RECT nr = { 0, i * lineH, cw, i * lineH + lineH };
            DrawTextStr(gc, lines[i], nr, f, tb, 0, 0);
        }
    }
    RECT clip = Shift(inner, 0, -4);
    GraphicsPath cp; RoundPath(cp, clip, 10);
    g.SetClip(&cp);
    g.DrawImage(cb, (REAL)inner.left, (REAL)inner.top - (REAL)g_logScroll,
                (REAL)cw, (REAL)total);
    g.ResetClip();
    delete cb;
}

static NState BtnSt(BtnId id);

static void DoPaint() {
    RECT cr; GetClientRect(g_hwnd, &cr);
    int w = cr.right, h = cr.bottom;
    if (w <= 0 || h <= 0) return;
    HDC hdc = GetDC(g_hwnd);
    HDC mdc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ ob = SelectObject(mdc, bmp);
    {
        Graphics g(mdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        LinearGradientBrush bg(PointF(0, 0), PointF((REAL)w, (REAL)h),
                               C(RGB(0xEC, 0xF2, 0xFA)), C(RGB(0xDD, 0xE6, 0xF0)));
        g.FillRectangle(&bg, 0, 0, w, h);
        DrawSidebar(g);

        // заголовок вкладки
        const char* titles[] = { "Хранилища", "Загрузка", "Самопроверка" };
        const char* subs[] = {
            "ваши приватные тома в GitHub",
            "зашифровать (AES-256) и отправить в облако",
            "проверка шифрования и многотомности без GitHub"
        };
        std::string title = titles[g_tab], sub = subs[g_tab];
        if (g_tab == 0 && g_open >= 0) {
            title = "Файлы — «" + g_stores[g_open] + "»";
            int n = CheckedCount();
            sub = n > 0
                ? "отмечено файлов: " + std::to_string(n) + " — «Скачать выбранные»"
                : "клик по строке отмечает файл, затем «Скачать выбранные»";
        }
        DrawHeadC(g, title, L.headerTitle, 24.0f, TXT, 0);
        RECT hrSub; SetR(hrSub, L.headerTitle.left, L.headerTitle.bottom + 4,
                         L.headerTitle.right, L.headerTitle.bottom + 20);
        DrawTextC(g, sub, hrSub, 12.5f, TXT_MUTED, false, 0, 0);

        if (g_tab == 0) {
            // Верхний ряд кнопок зависит от уровня (хранилища / файлы внутри)
            RECT btnRects[4] = { L.btnRefresh, L.btnDownload, L.btnDelete, L.btnWipe };
            const char* label0[4] = { "Обновить", "Скачать всё", "Удалить", "Стереть" };
            std::string labelIn[4] = { "← Хранилища", "Скачать выбранные", "Скачать всё", "Обновить" };
            for (int s = 0; s < 4; s++) {
                BtnId id = TopBtnForSlot(s);
                bool accent = (id == ACT_REFRESH);
                std::string lab;
                if (g_open >= 0) {
                    lab = labelIn[s];
                    if (id == ACT_DL_SEL) accent = true;
                } else {
                    lab = label0[s];
                }
                DrawButton(g, btnRects[s], lab, BtnSt(id), accent, IsDisabled(id));
            }
            DrawStoreRows(g);
        } else if (g_tab == 1) {
            RECT lab = { L.pathField.left, L.pathField.top - 22, L.pathField.right, L.pathField.top - 4 };
            DrawTextC(g, "Файл или папка", lab, 12.5f, TXT_MUTED, true, 0, 0);
            DrawFieldBox(g, L.pathField);
            DrawButton(g, L.btnBrowseFile, "Выбрать файл", BtnSt(ACT_BROWSE_FILE), false, false);
            DrawButton(g, L.btnBrowseDir, "Выбрать папку", BtnSt(ACT_BROWSE_DIR), false, false);

            RECT lab2 = { L.storeField.left, L.storeField.top - 22, L.storeField.right - 60, L.storeField.top - 4 };
            DrawTextC(g, "Имя хранилища (репозиторий)", lab2, 12.5f, TXT_MUTED, true, 0, 0);
            DrawFieldBox(g, L.storeField);
            DrawButton(g, L.btnUpload, "Загрузить в облако", BtnSt(ACT_UPLOAD), true, IsDisabled((BtnId)ACT_UPLOAD));
            DrawStoreDrop(g);   // список поверх кнопки загрузки, как обычное меню
        } else {
            DrawButton(g, L.btnSelftest, "Запустить самопроверку", BtnSt(ACT_SELFTEST_RUN), true, IsDisabled((BtnId)ACT_SELFTEST_RUN));
            DrawNeumorphCore(g, L.selftestHint, 14.0f, N_INSET, INNER, 7.0f, false);
            RECT hint = Shift(L.selftestHint, 18, 8);
            DrawTextC(g, "Создаёт тестовый архив, шифрует его (7z + AES-256), режет на части,\nрасшифровывает обратно и проверяет, что служебные файлы (tokengh.txt,\nPBEpass.txt) не попали внутрь. GitHub не требуется.", hint, 13.0f, TXT_MUTED, false, 0, 0);
        }
        DrawConsole(g);
    }
    BitBlt(hdc, 0, 0, w, h, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, ob);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(g_hwnd, hdc);
}

static NState BtnSt(BtnId id) {
    if (IsDisabled(id)) return N_RAISED;
    bool over = HitBtn(g_mouse) == id;
    bool press = g_down && g_downId == id && over;
    if (press) return N_PRESSED;
    if (over) return N_HOVER;
    return N_RAISED;
}

// ---------------------------------------------------------------------------
// Поля ввода + подсказки (placeholder)
// ---------------------------------------------------------------------------
static LRESULT CALLBACK EditNotify(HWND h, UINT m, WPARAM w, LPARAM l) {
    WNDPROC old = (h == g_editPath) ? g_editOldPath : g_editOldStore;
    if (m == WM_SETFOCUS || m == WM_KILLFOCUS)
        PostMessageW(g_hwnd, WM_APP_PH, (h == g_editPath) ? 1 : 2, 0);
    LRESULT r = CallWindowProcW(old, h, m, w, l);
    if (m == WM_CHAR || m == WM_CLEAR || m == WM_PASTE || m == WM_CUT || m == WM_UNDO)
        PostMessageW(g_hwnd, WM_APP_PH, (h == g_editPath) ? 1 : 2, 1);
    return r;
}

static void UpdatePlaceholders() {
    auto f = [](HWND ed, HWND ph) {
        if (!ph) return;
        bool on = (g_tab == 1);
        bool empty = true;
        if (ed) { wchar_t b[1024]; GetWindowTextW(ed, b, 1024); empty = (b[0] == 0); }
        bool focused = ed && GetFocus() && GetFocus() == ed;
        ShowWindow(ph, (on && empty && !focused) ? SW_SHOW : SW_HIDE);
    };
    f(g_editPath, g_phPath);
    f(g_editStore, g_phStore);
}

// ---------------------------------------------------------------------------
// Процедура главного окна
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icc{ sizeof icc, ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);
        DragAcceptFiles(hwnd, TRUE);
        // edit'ы со стилем "карман"
        g_editPath = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 100, 100, hwnd, (HMENU)1201, g_hInst, nullptr);
        g_editStore = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 100, 100, hwnd, (HMENU)1202, g_hInst, nullptr);
        HFONT f1 = CreateFontW(-18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, g_bodyFont);
        SendMessageW(g_editPath, WM_SETFONT, (WPARAM)f1, TRUE);
        SendMessageW(g_editStore, WM_SETFONT, (WPARAM)f1, TRUE);
        g_editOldPath = (WNDPROC)SetWindowLongPtrW(g_editPath, GWLP_WNDPROC, (LONG_PTR)EditNotify);
        g_editOldStore = (WNDPROC)SetWindowLongPtrW(g_editStore, GWLP_WNDPROC, (LONG_PTR)EditNotify);
        // подсказки (placeholder): прозрачные для мыши, показываются когда поле пустое
        g_phPath = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC",
            L"укажите путь к файлу или папке…", WS_CHILD | WS_VISIBLE,
            0, 0, 100, 100, hwnd, (HMENU)1301, g_hInst, nullptr);
        g_phStore = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC",
            L"имя хранилища, напр. photos", WS_CHILD | WS_VISIBLE,
            0, 0, 100, 100, hwnd, (HMENU)1302, g_hInst, nullptr);
        SendMessageW(g_phPath, WM_SETFONT, (WPARAM)f1, TRUE);
        SendMessageW(g_phStore, WM_SETFONT, (WPARAM)f1, TRUE);
        break;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetBkColor(dc, INNER);
        SetTextColor(dc, TXT);
        static HBRUSH br = CreateSolidBrush(INNER);
        return (LRESULT)br;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        SetBkColor(dc, INNER);
        SetTextColor(dc, TXT_MUTED);
        static HBRUSH br = CreateSolidBrush(INNER);
        return (LRESULT)br;
    }
    case WM_ERASEBKGND: return 1;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)l)->ptMinTrackSize.x = 900;
        ((MINMAXINFO*)l)->ptMinTrackSize.y = 620;
        return 0;
    case WM_SIZE:
    case WM_SIZING:
        RefreshLayout();
        Repaint();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
        DoPaint();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int nx = GET_X_LPARAM(l), ny = GET_Y_LPARAM(l);
        if (nx == g_mouse.x && ny == g_mouse.y) return 0;   // не перерисовываем без движения
        g_mouse.x = nx; g_mouse.y = ny;
        if (g_tab == 1 && g_storeDropOpen) {
            int h = HitStoreDropRow(g_mouse);
            if (h != g_storeDropHover) { g_storeDropHover = h; }
        }
        Repaint();
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        // выпадающий список хранилищ во вкладке Загрузка
        if (g_tab == 1 && g_storeDropOpen) {
            int ri = HitStoreDropRow(p);
            if (ri >= 0 && ri < (int)g_stores.size()) {
                SetWindowTextW(g_editStore, wstr(g_stores[ri]).c_str());
                UpdatePlaceholders();
            }
            g_storeDropOpen = false; g_storeDropHover = -1;
            g_down = false; g_downId = 0; Repaint();
            return 0;
        }
        if (g_tab == 0) {
            if (g_open >= 0) {
                // уровень «внутри хранилища»: клик по строке = отметить файл
                if (!g_items.empty()) {
                    for (size_t i = 0; i < g_items.size(); i++) {
                        RECT r = RowRect((int)i);
                        if (PtIn(r, p.x, p.y)) {
                            RECT pill = { r.right - 116, r.top + 11, r.right - 14, r.bottom - 11 };
                            g_sel = (int)i;
                            if (PtIn(pill, p.x, p.y)) {
                                HandleAction(ACT_DL_ONE);   // скачать именно этот файл
                            } else {
                                g_checked[i] = g_checked[i] ? 0 : 1;
                            }
                            g_down = false; g_downId = 0;
                            Repaint();
                            return 0;
                        }
                    }
                }
            } else if (!g_stores.empty()) {
                // список хранилищ: клик по строке = выбрать хранилище
                for (size_t i = 0; i < g_stores.size(); i++) {
                    RECT r = RowRect((int)i);
                    if (PtIn(r, p.x, p.y)) { g_sel = (int)i; g_down = false; g_downId = 0; Repaint(); return 0; }
                }
            }
        }
        g_downId = HitBtn(p);
        g_down = g_downId != B_NONE;
        Repaint();
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        // двойной клик по хранилищу — «провалиться» внутрь (список файлов)
        if (g_running) return 0;
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (g_tab == 0 && g_open < 0 && !g_stores.empty()) {
            for (size_t i = 0; i < g_stores.size(); i++) {
                RECT r = RowRect((int)i);
                if (PtIn(r, p.x, p.y)) {
                    g_open = (int)i;
                    g_sel = (int)i;
                    g_items.clear();
                    g_checked.clear();
                    g_listScroll = 0;
                    RefreshLayout();
                    Repaint();
                    RunCmd({ L"list", wstr(g_stores[i]) }, C_LIST_ITEMS);
                    return 0;
                }
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (g_down && g_downId != B_NONE) {
            int b = HitBtn(g_mouse);
            if (b == g_downId) HandleAction(b);
        }
        g_down = false; g_downId = 0;
        Repaint();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(w) > 0 ? -40 : 40;
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(hwnd, &pt);
        if (PtIn(L.console, pt.x, pt.y)) g_logScroll += delta;
        else if (PtIn(L.content, pt.x, pt.y)) g_listScroll += delta;
        Repaint();
        return 0;
    }
    case WM_APP_DONE: {
        std::string* out = (std::string*)l;
        g_log = *out; delete out;
        g_running = false;
        if ((int)w == C_LIST) { ParseStores(g_log, g_stores); g_sel = -1; g_open = -1; g_items.clear(); g_checked.clear(); RefreshLayout(); }
        else if ((int)w == C_LIST_ITEMS) {
            ParseItems(g_log, g_items);
            g_checked.assign(g_items.size(), 0);
            g_sel = -1;
            g_listScroll = 0;
            RefreshLayout();
        }
        Repaint();
        return 0;
    }
    case WM_APP_PH:
        UpdatePlaceholders();
        return 0;
    case WM_DROPFILES: {
        HDROP hd = (HDROP)w;
        wchar_t path[MAX_PATH];
        if (DragQueryFileW(hd, 0, path, MAX_PATH) > 0) {
            g_tab = 1;                       // переключаемся во вкладку «Загрузка»
            g_storeDropOpen = false;
            if (g_editPath) SetWindowTextW(g_editPath, path);
            UpdatePlaceholders();
            RefreshLayout();
            Repaint();
        }
        DragFinish(hd);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, m, w, l);
}

// ---------------------------------------------------------------------------
// Точка входа
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    g_hInst = hInstance;
    GdiplusStartupInput gsi;
    GdiplusStartup(&g_gdip, &gsi, nullptr);
    InitFonts();

    WNDCLASSW wc{}; wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"gcb_main";
    RegisterClassW(&wc);

    WNDCLASSW c2{}; c2.lpfnWndProc = InWndProc;
    c2.hInstance = hInstance;
    c2.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    c2.hbrBackground = nullptr;
    c2.lpszClassName = g_inCls;
    RegisterClassW(&c2);

    g_hwnd = CreateWindowExW(0, L"gcb_main",
        L"GITHUBCLOAD — приватное облако на GitHub",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1060, 780,
        nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) { GdiplusShutdown(g_gdip); return 1; }
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);
    SetFocus(g_hwnd);   // не даём курсору мигать в поле ввода при запуске

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(g_gdip);
    return (int)msg.wParam;
}
