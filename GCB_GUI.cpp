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
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <ctime>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

using namespace Gdiplus;

// ---------------------------------------------------------------------------
// Идентификация продукта и правовые тексты — синхронно с Linux-GUI
// (GITHUBCLOAD_GUI.py). Держать в согласии с TERMS_TEXT и LEGAL.md.
// ---------------------------------------------------------------------------
static const char* GUI_NAME      = "GITHUBCLOAD_GUI";
static const char* GUI_VERSION   = "1.2.1";
static const char* TERMS_VERSION = "1.0";
static const char* TERMS_FILE    = "_gcb_terms.json";
static const char* LEGAL_FILE    = "LEGAL.md";
static const char* LICENSE_FILE  = "LICENSE";
static const char* PROJECT_URL   = "https://github.com/larin-ilya/GITHUBCLOAD";
static const int   MAX_LOG_LINES = 5000;

// Одна короткая строка правового предупреждения: печатается при запуске
// (в консоль и в журнал) ровно один раз, а не при каждой операции.
static const char* NOTICE_TEXT =
    "GITHUBCLOAD 1.2.1 · GNU AGPL-3.0-or-later · поставляется «как есть»; "
    "полные правовые предупреждения — LEGAL.md, лицензия — LICENSE";

static const char* USAGE =
    "GITHUBCLOAD_GUI 1.2.1 — графический интерфейс (Windows) для GITHUBCLOAD.py\n"
    "\n"
    "Запуск:            GITHUBCLOAD_GUI.exe\n"
    "Служебные флаги:   --version       версия GUI (код 0)\n"
    "                   --smoke         сборка окна и всех страниц без показа (код 0)\n"
    "                   --accept-terms  принять условия использования без диалога\n"
    "                                   (создаёт _gcb_terms.json рядом с программой)\n"
    "                   --help          эта справка\n"
    "\n"
    "При первом запуске один раз на папку приложения показывается окно подтверждения\n"
    "условий; согласие сохраняется в _gcb_terms.json. Удалите этот файл, чтобы\n"
    "увидеть окно снова. Флаги --version, --smoke и --accept-terms окно не показывают.\n"
    "\n"
    "Правовая информация: лицензия GNU AGPL-3.0-or-later, программа поставляется\n"
    "«как есть», без гарантий. Полные тексты — LEGAL.md и LICENSE рядом с программой,\n"
    "в репозитории проекта https://github.com/larin-ilya/GITHUBCLOAD и в релизе.\n";

// Текст подтверждения условий (clickwrap) — копия TERMS_TEXT из Linux-GUI.
static const char* TERMS_TEXT =
    "GITHUBCLOAD хранит зашифрованные файлы в ВАШИХ собственных репозиториях GitHub.\n"
    "Программа бесплатна и поставляется «как есть». До запуска прочитайте:\n"
    "1. Никаких гарантий («as is»): явных или подразумеваемых гарантий нет, включая\n"
    "   пригодность для конкретной цели и сохранность данных. Используете — на свой риск.\n"
    "2. Риск утраты данных — на вас: автор не отвечает за потерю или повреждение файлов,\n"
    "   утечку токена, а также за ограничения и удаление ваших репозиториев на GitHub.\n"
    "3. Пароль шифрования не восстанавливается: мастер-ключа и «кода восстановления» нет.\n"
    "   Потеря PBEpass.txt = необратимая потеря данных; резервные копии — ваша задача.\n"
    "4. Метаданные не шифруются: имя файла/папки, размер и список частей архива лежат\n"
    "   открытым текстом в _gcb_manifest.json внутри репозитория. Учитывайте это.\n"
    "5. Правила GitHub соблюдать обязательно: репозитории — не хранилище и не бэкап\n"
    "   общего назначения, обходить лимиты нельзя — аккаунт и репозитории ограничат.\n"
    "6. Запрещены нелегальные материалы (в том числе с участием несовершеннолетних),\n"
    "   вредоносный код, спам и фишинг, чужие данные и персональные данные третьих лиц\n"
    "   без законного основания. Шифрование не делает такую обработку законной.\n"
    "7. Ответственность — на пользователе: только вы отвечаете за то, что и куда\n"
    "   загружаете, за сохранность токена и пароля и за соблюдение законов своей\n"
    "   юрисдикции (включая экспортный контроль).\n"
    "8. Проект не связан с GitHub, Inc., не спонсируется и не поддерживается им;\n"
    "   «GitHub» — торговая марка GitHub, Inc.\n"
    "9. Лицензия — GNU AGPL-3.0-or-later (файл LICENSE). Полные правовые предупреждения\n"
    "   и правила допустимого использования — в файле LEGAL.md.\n"
    "\n"
    "Нажимая «Принимаю условия», вы подтверждаете, что прочитали и принимаете их.\n";

// Краткая справка для окна «Правовая информация» — копия LEGAL_BRIEF из Linux-GUI.
static const char* LEGAL_BRIEF =
    "GITHUBCLOAD — свободное ПО под лицензией GNU AGPL-3.0-or-later.\n"
    "Проект не аффилирован с GitHub, Inc. и не поддерживается GitHub.\n"
    "\n"
    "Коротко о рисках и обязанностях:\n"
    "  • Программа поставляется «как есть», без гарантий; утрата данных, утечка токена\n"
    "    и ограничения аккаунта GitHub — риск пользователя.\n"
    "  • Пароль шифрования не восстанавливается: потеря PBEpass.txt = потеря данных.\n"
    "  • Метаданные не шифруются: имя файла/папки, размер и список частей архива\n"
    "    хранятся открытым текстом в служебном файле _gcb_manifest.json.\n"
    "  • Правила GitHub обязательны: репозитории — не хранилище и не бэкап общего\n"
    "    назначения, лимиты обходить нельзя.\n"
    "  • Запрещены нелегальный контент, вредоносное ПО, чужие данные и персональные\n"
    "    данные третьих лиц без законного основания.\n"
    "  • За загружаемые данные и соблюдение законов отвечает пользователь.\n"
    "\n"
    "Полный текст правовых предупреждений — в файле LEGAL.md, текст лицензии —\n"
    "в файле LICENSE. Оба документа есть рядом с программой, в репозитории проекта\n"
    "и в составе релиза.";

static const char* UPLOAD_INFO =
    "Что произойдёт: файл/папка шифруются паролем из PBEpass.txt (7z, AES-256), "
    "при размере больше 8 МБ архив автоматически режется на части по 8 МБ, "
    "после чего части заливаются в приватный репозиторий-том GitHub по токену "
    "из tokengh.txt.\n"
    "Служебные файлы tokengh.txt и PBEpass.txt никогда не попадают в репозиторий, "
    "даже если лежат внутри загружаемой папки.";

static const char* SELFTEST_INFO =
    "Самопроверка не обращается к GitHub: движок создаёт тестовые файлы, шифрует их "
    "паролем, режет архив на части, склеивает обратно, расшифровывает, а затем "
    "сверяет содержимое и проверяет, что tokengh.txt / PBEpass.txt в архив не попали.\n"
    "При успехе в журнале появится строка «САМОПРОВЕРКА ПРОЙДЕНА УСПЕШНО» и код возврата 0.";

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
static const COLORREF TXT_MUTED  = RGB(0x5F, 0x6B, 0x7E);
static const COLORREF ACCENT     = RGB(0x4E, 0x8A, 0xD8);
static const COLORREF ACCENT_LT  = RGB(0x91, 0xB8, 0xEF);
static const COLORREF LOG_BG     = RGB(0xD8, 0xE0, 0xEC);
static const wchar_t* g_bodyFont = L"Segoe UI";
static const wchar_t* g_headFont = L"Segoe UI";
static const wchar_t* FontPick(std::initializer_list<const wchar_t*> cand);
static void InitFonts();
static const float LOG_H = 156.0f;

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
    RECT topBtn[5];                     // верхний ряд: 4 кнопки в списке, 5 внутри хранилища
    int  topCount = 4;
    RECT btnRefresh, btnDownload, btnDelete, btnWipe;   // = topBtn[0..3] (совместимость)
    std::vector<RECT> storeRows;
    RECT pathField, pathInfo, btnBrowseFile, btnBrowseDir, storeField, btnUpload, storeDrop;   // storeDrop — стрелочка «▾»
    RECT uploadInfo;
    RECT selftestInfo, btnSelftest, selftestHint;
    RECT diagCard, diagText, btnLegal;
    RECT logTitle, btnClearLog, chkAuto;
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

// Журнал — список строк с тегами (cmd / err / warn / ok / gui), как в Linux-GUI.
struct LogLine { std::string text; std::string tag; };
static std::vector<LogLine> g_logLines;
static bool        g_autoscroll = true;
static std::string g_statusText = "Готово.";
static COLORREF    g_statusColor = TXT_MUTED;

// Сведения о выбранном пути (вкладка «Загрузка»).
static std::string g_pathInfo = "—";
static COLORREF    g_pathInfoCol = TXT_MUTED;
static int         g_pathSeq = 0;

// Диагностика и описание выбранного движка.
static std::string g_engineDesc = "не определён";
static std::string g_termsAcceptedAt;

// Очередь удаления выбранных элементов (по одному id за команду).
static std::vector<std::wstring> g_delQueue;
static std::wstring g_delStore;

static void LogAdd(const std::string& text, const std::string& tag = "");
static void SetStatus(const std::string& text, COLORREF col);
static std::string CommandLabel(const std::vector<std::wstring>& args);
static void KickPathInfo();
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
    ACT_BACK = 2100, ACT_DL_SEL, ACT_DL_ONE, ACT_DL_ALL,
    ACT_DEL_SEL = 2200, ACT_LOG_CLEAR, ACT_AUTO_SCROLL, ACT_LEGAL
};

#define WM_APP_DONE     (WM_APP + 1)
#define WM_APP_PROMPT   (WM_APP + 2)
#define WM_APP_PATHINFO (WM_APP + 4)
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
    // карточка статуса — по содержимому, низ выровнен с низом журнала
    SetR(L.statusCard, nx, h - 130, nx + nw, h - 26);
    SetR(L.statusTok, L.statusCard.left + 18, L.statusCard.top + 38,
         L.statusCard.right - 18, L.statusCard.top + 38 + 26);
    SetR(L.statusPass, L.statusTok.left, L.statusTok.bottom + 8,
         L.statusTok.right, L.statusTok.bottom + 8 + 26);

    int bodyL = sb + 30, bodyT = 24, bodyR = w - 28, bodyB = h - 26;
    SetR(L.headerTitle, bodyL, bodyT, bodyR, bodyT + 34);
    int consH = (int)LOG_H;
    SetR(L.console, bodyL, bodyB - consH, bodyR, bodyB);
    SetR(L.content, bodyL, bodyT + 66, bodyR, L.console.top - 12);

    int cy = L.content.top;
    int bGap = 14;
    L.topCount = (g_open >= 0) ? 5 : 4;
    int bW = (int)((RW(L.content) - bGap * (L.topCount - 1)) / L.topCount);
    if (bW > 170) {   // ограничили ширину — растянем промежутки, чтобы ряд занял всю ширину
        bW = 170;
        if (L.topCount > 1) bGap = (int)((RW(L.content) - bW * L.topCount) / (L.topCount - 1));
    }
    for (int i = 0; i < L.topCount; i++) {
        int x = L.content.left + i * (bW + bGap);
        SetR(L.topBtn[i], x, cy, x + bW, cy + 42);
    }
    for (int i = L.topCount; i < 5; i++) SetR(L.topBtn[i], 0, 0, 0, 0);
    L.btnRefresh  = L.topBtn[0];
    L.btnDownload = L.topBtn[1];
    L.btnDelete   = L.topBtn[2];
    L.btnWipe     = L.topBtn[3];

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
    // сведения о выбранном пути — отдельной строкой под колонкой кнопок
    SetR(L.pathInfo, L.content.left, L.btnBrowseDir.bottom + 10,
         L.content.right, L.btnBrowseDir.bottom + 34);
    int sfTop = L.pathInfo.bottom + 18;
    SetR(L.storeField, L.content.left, sfTop, L.content.right, sfTop + 52);
    // кнопка «▾» для выпадающего списка хранилищ — правый край поля
    SetR(L.storeDrop, L.storeField.right - 54, sfTop, L.storeField.right - 14, sfTop + 52);
    int upW = 240, upX = L.content.left + (L.content.right - L.content.left - upW) / 2;
    SetR(L.btnUpload, upX, L.storeField.bottom + 34, upX + upW, L.storeField.bottom + 34 + 60);
    // пояснение «что произойдёт» — ниже блока загрузки
    SetR(L.uploadInfo, L.content.left, L.btnUpload.bottom + 24,
         L.content.right, L.content.bottom);

    // --- страница «Самопроверка» ---
    int stTop = L.content.top + 4;
    SetR(L.selftestInfo, L.content.left, stTop, L.content.right, stTop + 140);
    SetR(L.btnSelftest, L.content.left, L.selftestInfo.bottom + 14,
         L.content.left + 280, L.selftestInfo.bottom + 14 + 52);
    int diagTop = L.btnSelftest.bottom + 18;
    SetR(L.diagCard, L.content.left, diagTop, L.content.right, diagTop + 188);
    SetR(L.btnLegal, L.diagCard.right - 224, L.diagCard.top + 18,
         L.diagCard.right - 24, L.diagCard.top + 18 + 40);
    SetR(L.diagText, L.diagCard.left + 22, L.diagCard.top + 70,
         L.diagCard.right - 22, L.diagCard.bottom - 16);
    SetR(L.selftestHint, L.btnSelftest.right + 20, L.btnSelftest.top,
         L.content.right, L.btnSelftest.bottom);

    // --- заголовок журнала: «Автопрокрутка» и «Очистить» ---
    int chW = 150, clW = 110, hh = 28;
    SetR(L.btnClearLog, L.console.right - 20 - clW, L.console.top + 13,
         L.console.right - 20, L.console.top + 13 + hh);
    SetR(L.chkAuto, L.btnClearLog.left - 12 - chW, L.console.top + 13,
         L.btnClearLog.left - 12, L.console.top + 13 + hh);
    SetR(L.logTitle, L.console.left + 20, L.console.top + 10,
         L.chkAuto.left - 350, L.console.top + 42);
}

static void UpdatePlaceholders();   // fwd (поля/плейсхолдеры)

static void RefreshLayout() {
    RECT cr; GetClientRect(g_hwnd, &cr);
    UpdateLayout(cr.right, cr.bottom, g_stores);
    if (g_editPath) MoveWindow(g_editPath, L.pathField.left + 12, L.pathField.top + 8,
                               L.pathField.right - L.pathField.left - 24,
                               L.pathField.bottom - L.pathField.top - 16, TRUE);
    if (g_editStore) MoveWindow(g_editStore, L.storeField.left + 12, L.storeField.top + 8,
                                L.storeField.right - L.storeField.left - 76,
                                L.storeField.bottom - L.storeField.top - 16, TRUE);
    // поля видны только на вкладке «Загрузка»
    ShowWindow(g_editPath, g_tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_editStore, g_tab == 1 ? SW_SHOW : SW_HIDE);
    if (g_phPath) MoveWindow(g_phPath, L.pathField.left + 13, L.pathField.top + 8,
                             L.pathField.right - L.pathField.left - 26,
                             L.pathField.bottom - L.pathField.top - 16, TRUE);
    if (g_phStore) MoveWindow(g_phStore, L.storeField.left + 13, L.storeField.top + 8,
                              L.storeField.right - L.storeField.left - 78,
                              L.storeField.bottom - L.storeField.top - 16, TRUE);
    UpdatePlaceholders();
}

// ---------------------------------------------------------------------------
// Хит-тест
// ---------------------------------------------------------------------------
// Кнопки верхнего ряда зависят от уровня: список хранилищ (g_open == -1)
// либо файлы внутри хранилища (g_open >= 0).
static BtnId TopBtnForSlot(int slot) {   // раскладка зависит от уровня (список / внутри хранилища)
    if (g_open >= 0) {
        static const BtnId in[5] = { ACT_BACK, ACT_DL_SEL, ACT_DL_ALL, ACT_DEL_SEL, ACT_REFRESH };
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
    if (id == ACT_LOG_CLEAR || id == ACT_AUTO_SCROLL) return false;
    if (id == ACT_LEGAL) return g_running;
    if (g_open >= 0) {   // уровень «внутри хранилища»
        switch (id) {
        case ACT_DL_SEL:
            return g_running || CheckedCount() == 0;
        case ACT_DEL_SEL:
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
    if (g_tab == 1) {
        if (PtIn(L.btnBrowseFile, x, y)) return ACT_BROWSE_FILE;
        if (PtIn(L.btnBrowseDir, x, y)) return ACT_BROWSE_DIR;
        if (PtIn(L.storeDrop, x, y)) return ACT_STORE_DROP;
        if (PtIn(L.btnUpload, x, y)) return ACT_UPLOAD;
    }
    if (g_tab == 2) {
        if (PtIn(L.btnSelftest, x, y)) return ACT_SELFTEST_RUN;
        if (PtIn(L.btnLegal, x, y)) return ACT_LEGAL;
    }
    if (PtIn(L.btnClearLog, x, y)) return ACT_LOG_CLEAR;
    if (PtIn(L.chkAuto, x, y)) return ACT_AUTO_SCROLL;
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

static bool Launch(const std::wstring& cmdline, std::string& out, DWORD* exitCode = nullptr) {
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
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (exitCode) *exitCode = code;
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
    DWORD exitCode = (DWORD)-1;
    if (!prefix.empty() && prefix != L"\"\"") {
        Launch(prefix + argsSuffix, out, &exitCode);
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
                Launch(g_engine + argsSuffix, out2, &exitCode);
                if (!out2.empty()) out = out2;
            }
        }
    } else {
        exitCode = 1;
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
    out += "\n[код возврата: " + std::to_string((long long)exitCode) + "]";
    DebugLog("CMD", u8str(cmdLabel + argsSuffix));
    DebugLog("OUT", out);
    PostMessageW(g_hwnd, WM_APP_DONE, (WPARAM)job->type, (LPARAM)new std::string(out));
    return 0;
}

static void RunCmd(std::vector<std::wstring> args, int type) {
    if (g_running) return;
    g_running = true;
    g_log.clear();
    std::string label = CommandLabel(args);
    LogAdd("$ GITHUBCLOAD " + label, "cmd");
    SetStatus("Выполняется: " + label, ACCENT);
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
// Журнал, статус, разбор вывода
// ---------------------------------------------------------------------------
static bool StartsWith(const std::string& s, const char* prefix) {
    size_t n = strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

static std::string TrimCR(const std::string& s) {
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
    return s.substr(0, e);
}

static std::string TrimSpaces(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string HumanBytes(unsigned long long n) {
    const char* u[] = { "Б", "КБ", "МБ", "ГБ", "ТБ" };
    double v = (double)n;
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
    char b[64];
    if (i == 0) snprintf(b, sizeof b, "%llu %s", n, u[i]);
    else        snprintf(b, sizeof b, "%.1f %s", v, u[i]);
    return std::string(b);
}

// «12.5 МБ» -> байты (как parse_human в Linux-GUI)
static unsigned long long ParseHuman(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == ',')) i++;
    if (i == 0) return 0;
    std::string num = s.substr(0, i);
    for (char& c : num) if (c == ',') c = '.';
    double v = strtod(num.c_str(), nullptr);
    std::string rest = s.substr(i);
    if (rest.find("ТБ") != std::string::npos)      v *= 1024.0 * 1024.0 * 1024.0 * 1024.0;
    else if (rest.find("ГБ") != std::string::npos) v *= 1024.0 * 1024.0 * 1024.0;
    else if (rest.find("МБ") != std::string::npos) v *= 1024.0 * 1024.0;
    else if (rest.find("КБ") != std::string::npos) v *= 1024.0;
    return (unsigned long long)(v + 0.5);
}

static COLORREF TagColor(const std::string& tag) {
    if (tag == "cmd")  return RGB(0x2F, 0x6F, 0xED);
    if (tag == "err")  return RGB(0xC0, 0x39, 0x2B);
    if (tag == "warn") return RGB(0x9A, 0x6B, 0x00);
    if (tag == "ok")   return RGB(0x1A, 0x9E, 0x4B);
    if (tag == "gui")  return RGB(0x4F, 0x58, 0x66);
    return TXT;
}

static std::string ClassifyLine(const std::string& line) {
    std::string t = TrimSpaces(line);
    if (t.empty()) return "";
    if (StartsWith(t, "ОШИБКА") || StartsWith(t, "ВНУТРЕННЯЯ ОШИБКА")) return "err";
    if (StartsWith(t, "ВНИМАНИЕ")) return "warn";
    if (t.find("САМОПРОВЕРКА ПРОЙДЕНА") != std::string::npos) return "ok";
    if (StartsWith(t, "ГОТОВО")) return "ok";
    return "";
}

static void LogAdd(const std::string& text, const std::string& tag) {
    LogLine ln;
    ln.text = text;
    ln.tag = tag;
    g_logLines.push_back(ln);
    if ((int)g_logLines.size() > MAX_LOG_LINES)
        g_logLines.erase(g_logLines.begin(), g_logLines.begin() + (g_logLines.size() - MAX_LOG_LINES));
}

// Разбивает блок вывода на строки и добавляет их в журнал.
// forcedTag пуст — тег определяется классификацией каждой строки.
static void LogAddBlock(const std::string& text, const std::string& forcedTag) {
    std::string cur;
    for (size_t i = 0; i <= text.size(); i++) {
        if (i == text.size() || text[i] == '\n') {
            std::string line = TrimCR(cur);
            cur.clear();
            LogAdd(line, forcedTag.empty() ? ClassifyLine(line) : forcedTag);
        } else {
            cur += text[i];
        }
    }
}

static void SetStatus(const std::string& text, COLORREF col) {
    g_statusText = text;
    g_statusColor = col;
}

static std::string CommandLabel(const std::vector<std::wstring>& args) {
    std::string s;
    for (size_t i = 0; i < args.size(); i++) {
        if (i) s += " ";
        s += u8str(args[i]);
    }
    return s;
}

// ---------------------------------------------------------------------------
// Условия использования (_gcb_terms.json) — как в Linux-GUI
// ---------------------------------------------------------------------------
static bool FileExistsW(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string ReadTextFileW(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return std::string();
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

static bool WriteTextFileAtomicW(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return false;
    bool ok = (fwrite(data.data(), 1, data.size(), f) == data.size());
    fflush(f);
    fclose(f);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

static std::wstring TermsPath() { return ScriptDir() + L"\\" + wstr(TERMS_FILE); }

static std::string JsonStringField(const std::string& json, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return "";
    p = json.find(':', p + k.size());
    if (p == std::string::npos) return "";
    size_t q1 = json.find('"', p);
    if (q1 == std::string::npos) return "";
    size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return json.substr(q1 + 1, q2 - q1 - 1);
}

static bool TermsAccepted(std::string* acceptedAt = nullptr) {
    std::string j = ReadTextFileW(TermsPath());
    if (j.empty()) return false;
    if (JsonStringField(j, "terms_version") != TERMS_VERSION) return false;
    if (acceptedAt) *acceptedAt = JsonStringField(j, "accepted_at");
    return true;
}

static bool WriteTermsAcceptance(std::string& error) {
    SYSTEMTIME st;
    GetSystemTime(&st);
    char stamp[64];
    snprintf(stamp, sizeof stamp, "%04d-%02d-%02dT%02d:%02d:%02d+00:00",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::string payload = std::string("{\n  \"terms_version\": \"") + TERMS_VERSION +
                          "\",\n  \"accepted_at\": \"" + stamp + "\"\n}\n";
    if (!WriteTextFileAtomicW(TermsPath(), payload)) {
        error = std::string("не удалось записать ") + TERMS_FILE;
        return false;
    }
    g_termsAcceptedAt = stamp;
    return true;
}

// ---------------------------------------------------------------------------
// Правовые документы: локальный файл рядом с программой -> ссылка в репозитории
// ---------------------------------------------------------------------------
static std::wstring DocumentPathW(const char* filename) {
    std::wstring p = ScriptDir() + L"\\" + wstr(filename);
    return FileExistsW(p) ? p : std::wstring();
}

static bool OpenLocalPathW(const std::wstring& path) {
    HINSTANCE r = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

static void OpenDocument(HWND owner, const char* filename) {
    std::wstring local = DocumentPathW(filename);
    if (!local.empty() && OpenLocalPathW(local)) {
        LogAdd(std::string("Правовая информация: открыт файл ") + u8str(local), "gui");
        return;
    }
    std::string urlS = std::string(PROJECT_URL) + "/blob/main/" + filename;
    std::wstring url = wstr(urlS);
    if (OpenLocalPathW(url)) {
        LogAdd(std::string("Правовая информация: открыт ") + urlS +
               " в репозитории проекта", "gui");
        return;
    }
    LogAdd(std::string("ВНИМАНИЕ: не удалось открыть ") + filename + " — ссылка: " + urlS, "warn");
    if (OpenClipboard(owner)) {
        EmptyClipboard();
        size_t n = (url.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
        if (h) {
            void* d = GlobalLock(h);
            if (d) {
                memcpy(d, url.c_str(), n);
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
            }
        }
        CloseClipboard();
    }
    std::wstring msg = L"Не удалось открыть " + wstr(filename) +
                       L" автоматически.\nСсылка скопирована в буфер обмена:\n" + url;
    MessageBoxW(owner, msg.c_str(), L"Правовая информация", MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// Модальное окно с прокручиваемым текстом (условия / правовая информация)
// ---------------------------------------------------------------------------
static std::wstring g_dlgTitle, g_dlgSub, g_dlgBody, g_dlgNote;
static std::vector<std::wstring> g_dlgBtns;
static std::vector<RECT> g_dlgBtnRects;
static int  g_dlgHot = -1, g_dlgDown = -1, g_dlgResult = -1, g_dlgDefault = -1;
static bool g_dlgDone = false;
static HWND g_dlgEdit = nullptr;
static HFONT g_dlgFont = nullptr, g_dlgTitleFont = nullptr;
static HBRUSH g_dlgEditBrush = nullptr;
static WNDPROC g_dlgEditOld = nullptr;
static const wchar_t* g_dlgCls = L"gcb_dialog";

static void DlgLayout(HWND hwnd) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int pad = 18, btnH = 44, gap = 10;
    int n = (int)g_dlgBtns.size();
    if (n < 1) n = 0;
    int bw = 190;
    if (n > 0) {
        int totalW = n * bw + (n - 1) * gap;
        if (totalW > cr.right - 2 * pad) {
            bw = (cr.right - 2 * pad - (n - 1) * gap) / n;
            if (bw < 80) bw = 80;
        }
        int x = cr.right - pad - (n * bw + (n - 1) * gap);
        if (x < pad) x = pad;
        g_dlgBtnRects.assign(n, RECT{ 0, 0, 0, 0 });
        for (int i = 0; i < n; i++) {
            int y = cr.bottom - pad - btnH;
            SetR(g_dlgBtnRects[i], x, y, x + bw, y + btnH);
            x += bw + gap;
        }
    } else {
        g_dlgBtnRects.clear();
    }
    int top = 84;
    int noteH = g_dlgNote.empty() ? 0 : 40;
    int editBottom = cr.bottom - pad - (n ? btnH + 14 : 0) - noteH;
    if (g_dlgEdit)
        MoveWindow(g_dlgEdit, pad, top, cr.right - 2 * pad, editBottom - top, TRUE);
}

static int DlgHit(POINT p) {
    for (size_t i = 0; i < g_dlgBtnRects.size(); i++)
        if (PtIn(g_dlgBtnRects[i], p.x, p.y)) return (int)i;
    return -1;
}

static LRESULT CALLBACK DlgWndProc(HWND hwnd, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        RECT cr;
        GetClientRect(hwnd, &cr);
        g_dlgEdit = CreateWindowExW(0, L"EDIT", g_dlgBody.c_str(),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            cr.left + 18, 84, cr.right - 36, cr.bottom - 220,
            hwnd, (HMENU)9101, g_hInst, nullptr);
        if (g_dlgFont) SendMessageW(g_dlgEdit, WM_SETFONT, (WPARAM)g_dlgFont, TRUE);
        g_dlgDone = false; g_dlgResult = -1; g_dlgHot = -1; g_dlgDown = -1;
        DlgLayout(hwnd);
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        SetBkColor(dc, INNER);
        SetTextColor(dc, TXT);
        return (LRESULT)(g_dlgEditBrush ? g_dlgEditBrush : GetStockObject(WHITE_BRUSH));
    }
    case WM_SIZE: DlgLayout(hwnd); return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)l)->ptMinTrackSize.x = 560;
        ((MINMAXINFO*)l)->ptMinTrackSize.y = 420;
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT cr;
        GetClientRect(hwnd, &cr);
        HDC mdc = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        HGDIOBJ ob = SelectObject(mdc, bmp);
        {
            Graphics g(mdc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            LinearGradientBrush bg(PointF(0, 0), PointF((REAL)cr.right, (REAL)cr.bottom),
                                   C(RGB(0xED, 0xF2, 0xFA)), C(RGB(0xDD, 0xE6, 0xF0)));
            g.FillRectangle(&bg, 0, 0, cr.right, cr.bottom);
            {
                Font ft(g_headFont, 19.0f, FontStyleBold, UnitPixel);
                SolidBrush btb(C(TXT));
                DrawTextStr(g, g_dlgTitle, RECT{ 18, 18, cr.right - 18, 50 }, ft, btb, 0, 0);
            }
            {
                Font fl(g_bodyFont, 12.5f, FontStyleRegular, UnitPixel);
                SolidBrush blb(C(TXT_MUTED));
                DrawTextStr(g, g_dlgSub, RECT{ 18, 52, cr.right - 18, 76 }, fl, blb, 0, 0);
            }
            if (!g_dlgNote.empty()) {
                RECT nr;
                int y = cr.bottom - 18 - (g_dlgBtns.empty() ? 0 : 58) - 40;
                SetR(nr, 18, y, cr.right - 18, y + 40);
                Font fn(g_bodyFont, 12.0f, FontStyleRegular, UnitPixel);
                SolidBrush nb(C(TXT_MUTED));
                DrawTextStr(g, g_dlgNote, nr, fn, nb, 0, 0);
            }
            for (size_t i = 0; i < g_dlgBtnRects.size(); i++) {
                NState st = ((int)i == g_dlgDown && g_dlgDown == g_dlgHot) ? N_PRESSED
                          : ((int)i == g_dlgHot ? N_HOVER : N_RAISED);
                bool accent = ((int)i == g_dlgDefault);
                DrawButton(g, g_dlgBtnRects[i], u8str(g_dlgBtns[i]), st, accent, false);
            }
            Pen pen(C(SH_DARK, 40), 1.0f);
            GraphicsPath cpath;
            RoundPath(cpath, RECT{ 2, 2, cr.right - 2, cr.bottom - 2 }, 12);
            g.DrawPath(&pen, &cpath);
        }
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, ob);
        DeleteObject(bmp);
        DeleteDC(mdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        int h = DlgHit(p);
        if (h != g_dlgHot) { g_dlgHot = h; InvalidateRect(hwnd, nullptr, TRUE); }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        g_dlgDown = DlgHit(p);
        g_dlgHot = g_dlgDown;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        int h = DlgHit(p);
        if (h >= 0 && h == g_dlgDown) { g_dlgResult = h; g_dlgDone = true; }
        g_dlgDown = -1;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_CLOSE:
        g_dlgResult = -1;
        g_dlgDone = true;
        return 0;
    case WM_DESTROY:
        g_dlgEdit = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, m, w, l);
}

// Показывает модальное окно; возвращает индекс нажатой кнопки или -1 (закрытие/отказ).
static int ShowDialogWindow(HWND owner, const std::wstring& title, const std::wstring& sub,
                            const std::wstring& body, const std::wstring& note,
                            const std::vector<std::wstring>& buttons, int defaultIndex) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DlgWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = g_dlgCls;
        RegisterClassW(&wc);
        reg = true;
    }
    if (!g_dlgFont) {
        g_dlgFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                CLEARTYPE_QUALITY, 0, g_bodyFont);
        g_dlgTitleFont = CreateFontW(-19, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                     CLEARTYPE_QUALITY, 0, g_headFont);
    }
    if (!g_dlgEditBrush) g_dlgEditBrush = CreateSolidBrush(INNER);

    g_dlgTitle = title; g_dlgSub = sub; g_dlgBody = body; g_dlgNote = note;
    g_dlgBtns = buttons; g_dlgDefault = defaultIndex;
    g_dlgResult = -1; g_dlgDone = false; g_dlgHot = -1; g_dlgDown = -1;

    HWND hwnd = CreateWindowExW(0, g_dlgCls, title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 860, 620, owner, nullptr, g_hInst, nullptr);
    if (!hwnd) return -1;

    RECT ow;
    if (owner && GetWindowRect(owner, &ow)) {
        RECT nr;
        GetWindowRect(hwnd, &nr);
        int ww = nr.right - nr.left, wh = nr.bottom - nr.top;
        int px = ow.left + (ow.right - ow.left - ww) / 2;
        int py = ow.top + (ow.bottom - ow.top - wh) / 3;
        if (px < 0) px = 0;
        if (py < 0) py = 0;
        SetWindowPos(hwnd, nullptr, px, py, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    if (owner) EnableWindow(owner, FALSE);

    MSG msg;
    while (!g_dlgDone) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) { g_dlgDone = true; break; }
        if (r == -1) break;
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    DestroyWindow(hwnd);
    return g_dlgResult;
}

static bool ShowTermsDialog(HWND owner) {
    std::vector<std::wstring> btns;
    btns.push_back(L"Выход");
    btns.push_back(L"Принимаю условия");
    std::wstring sub = std::wstring(L"Версия условий ") + wstr(TERMS_VERSION) +
                       L" · текст прокручивается";
    int r = ShowDialogWindow(owner, L"Подтверждение условий использования", sub,
                             wstr(TERMS_TEXT),
                             L"«Выход» или закрытие окна — программа завершит работу, "
                             L"ничего не изменяя.",
                             btns, 1);
    return r == 1;
}

// Окно «Правовая информация»: остаётся открытым для повторного открытия документов.
static void ShowLegalDialog(HWND owner) {
    std::vector<std::wstring> btns;
    btns.push_back(std::wstring(L"Открыть ") + wstr(LEGAL_FILE));
    btns.push_back(std::wstring(L"Открыть ") + wstr(LICENSE_FILE));
    btns.push_back(L"Закрыть");
    std::wstring sub = std::wstring(L"Версия условий ") + wstr(TERMS_VERSION) +
                       L" · лицензия GNU AGPL-3.0-or-later";
    bool lp = !DocumentPathW(LEGAL_FILE).empty();
    bool cp = !DocumentPathW(LICENSE_FILE).empty();
    std::wstring note;
    if (lp && cp) note = L"Файлы LEGAL.md и LICENSE найдены рядом с программой.";
    else if (lp)  note = L"Рядом с программой есть LEGAL.md; LICENSE откроется в репозитории проекта.";
    else if (cp)  note = L"Рядом с программой есть LICENSE; LEGAL.md откроется в репозитории проекта.";
    else          note = L"Файлов LEGAL.md и LICENSE рядом с программой нет — кнопки откроют их "
                         L"в репозитории проекта.";
    for (;;) {
        int r = ShowDialogWindow(owner, L"Правовая информация", sub, wstr(LEGAL_BRIEF),
                                 note, btns, 2);
        if (r == 0) { OpenDocument(owner, LEGAL_FILE); continue; }
        if (r == 1) { OpenDocument(owner, LICENSE_FILE); continue; }
        break;
    }
}

// ---------------------------------------------------------------------------
// Диагностика и описание движка
// ---------------------------------------------------------------------------
static std::string EngineDescription() {
    if (g_engine.empty()) {
        std::wstring ext = ScriptDir() + L"\\GITHUBCLOAD_core.exe";
        if (FileExistsW(ext)) return "внешний GITHUBCLOAD_core.exe рядом с программой";
        return "не определён (выберется при первой операции)";
    }
    if (g_engineIsEmbedded) return "встроенное в exe ядро (извлечено во временную папку)";
    if (g_engine == L"py")  return "python + GITHUBCLOAD.py";
    return "внешний GITHUBCLOAD_core.exe рядом с программой";
}

static std::string DiagnosticsText() {
    bool tok = FileExistsW(ScriptDir() + L"\\" + wstr("tokengh.txt"));
    bool pw = FileExistsW(ScriptDir() + L"\\" + wstr("PBEpass.txt"));
    std::string acc;
    bool accepted = TermsAccepted(&acc);
    std::string accText = accepted ? "да" : "нет";
    if (accepted && !acc.empty()) accText += " (" + acc + ")";
    std::string s;
    s += "Папка приложения: " + u8str(ScriptDir()) + "\n";
    s += "Условия приняты: " + accText + " · версия условий " + TERMS_VERSION + "\n";
    s += "Движок: " + EngineDescription() + "\n";
    s += std::string("tokengh.txt: ") + (tok ? "найден" : "НЕ найден") +
         " · PBEpass.txt: " + (pw ? "найден" : "НЕ найден") + "\n";
    s += std::string("GUI ") + GUI_VERSION + " · Windows (Win32 + GDI+)";
    return s;
}

// ---------------------------------------------------------------------------
// Сведения о выбранном пути (вкладка «Загрузка»)
// ---------------------------------------------------------------------------
static void DirStats(const std::wstring& dir, int& count, unsigned long long& total) {
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring child = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DirStats(child, count, total);
        } else {
            count++;
            total += ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

struct PathInfoJob { std::wstring path; int seq; };

static DWORD WINAPI PathInfoThread(LPVOID p) {
    std::unique_ptr<PathInfoJob> job((PathInfoJob*)p);
    std::string text;
    DWORD attr = GetFileAttributesW(job->path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        text = "путь не найден";
    } else if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (GetFileAttributesExW(job->path.c_str(), GetFileExInfoStandard, &d))
            text = "файл · " + HumanBytes(((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow);
        else
            text = "не удалось измерить размер";
    } else {
        int count = 0;
        unsigned long long total = 0;
        DirStats(job->path, count, total);
        char b[96];
        snprintf(b, sizeof b, "папка · файлов: %d · ", count);
        text = std::string(b) + HumanBytes(total);
    }
    PostMessageW(g_hwnd, WM_APP_PATHINFO, (WPARAM)job->seq, (LPARAM)new std::string(text));
    return 0;
}

static void KickPathInfo() {
    if (!g_editPath) return;
    wchar_t buf[2048];
    GetWindowTextW(g_editPath, buf, 2048);
    std::wstring p = buf;
    if (p.empty()) {
        g_pathInfo = "—";
        g_pathInfoCol = TXT_MUTED;
        Repaint();
        return;
    }
    g_pathSeq++;
    g_pathInfo = "измеряю размер…";
    g_pathInfoCol = TXT_MUTED;
    PathInfoJob* job = new PathInfoJob{ p, g_pathSeq };
    CreateThread(nullptr, 0, PathInfoThread, job, 0, nullptr);
    Repaint();
}

// ---------------------------------------------------------------------------
// Группировка содержимого хранилища по томам (как _render_detail в Linux-GUI)
// ---------------------------------------------------------------------------
struct DetailRow { int kind; int item; std::string repo, used, total; };   // kind: 0 — том, 1 — элемент
static std::vector<DetailRow> g_drows;
static std::string g_detailSummary;

static void RebuildStoreDetailRows() {
    g_drows.clear();
    g_detailSummary.clear();
    if (g_open < 0) return;

    int itemIdx = 0;
    int volumeRows = 0;
    unsigned long long usedSum = 0;
    const std::string out = g_log;
    size_t pos = 0;
    while (pos < out.size()) {
        size_t eol = out.find('\n', pos);
        if (eol == std::string::npos) eol = out.size();
        std::string line = TrimCR(out.substr(pos, eol - pos));
        pos = eol + 1;
        size_t b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos) continue;
        std::string t = line.substr(b);

        size_t vp = t.find("ТОМ ");
        if (vp == 0) {
            std::string rest = t.substr(t.find(' ') + 1);
            std::string repo = rest, used, total;
            size_t z = rest.find("занято ");
            size_t iz = rest.find(" из ~", z == std::string::npos ? 0 : z);
            if (z != std::string::npos && iz != std::string::npos) {
                std::string u = rest.substr(z, iz - z);
                size_t sp = u.find(' ');
                used = (sp == std::string::npos) ? u : TrimSpaces(u.substr(sp + 1));
                std::string tail = rest.substr(iz);
                size_t tz = tail.find('~');
                total = (tz == std::string::npos) ? "" : tail.substr(tz + 1);
                size_t rp = total.find(')');
                if (rp != std::string::npos) total = total.substr(0, rp);
                total = TrimSpaces(total);
            }
            size_t par = rest.find('(');
            repo = TrimSpaces(par == std::string::npos ? rest : rest.substr(0, par));
            DetailRow r;
            r.kind = 0; r.item = -1; r.repo = repo; r.used = used; r.total = total;
            g_drows.push_back(r);
            volumeRows++;
            usedSum += ParseHuman(used);
            continue;
        }
        if (t.compare(0, 2, "- ") == 0) {
            std::string body = t.substr(2);
            size_t m = body.find("  [id ");
            if (m == std::string::npos) continue;
            if (itemIdx < (int)g_items.size()) {
                DetailRow r;
                r.kind = 1; r.item = itemIdx;
                r.repo = g_items[itemIdx].name;
                g_drows.push_back(r);
            }
            itemIdx++;
        }
    }
    if ((int)g_drows.size() == 0 || volumeRows == 0) {
        // вывод не распознан — плоский список, как раньше
        g_drows.clear();
        for (size_t i = 0; i < g_items.size(); i++) {
            DetailRow r;
            r.kind = 1; r.item = (int)i;
            g_drows.push_back(r);
        }
        g_detailSummary.clear();
        return;
    }
    char b[160];
    snprintf(b, sizeof b, "Томов: %d · элементов: %d · занято: %s",
             volumeRows, (int)g_items.size(), HumanBytes(usedSum).c_str());
    g_detailSummary = b;
}

static int DetailRowH(int i) { return g_drows[i].kind == 0 ? 38 : 56; }
static int DetailGap() { return 12; }

static int DetailTop(int i) {
    int y = L.btnRefresh.bottom + 24 - g_listScroll;
    for (int k = 0; k < i; k++) y += DetailRowH(k) + DetailGap();
    return y;
}

static RECT DetailRect(int i) {
    RECT r;
    SetR(r, L.content.left, DetailTop(i), L.content.right - 6, DetailTop(i) + DetailRowH(i));
    return r;
}

static int DetailTotalHeight() {
    int h = 0;
    for (size_t i = 0; i < g_drows.size(); i++) h += DetailRowH((int)i) + DetailGap();
    return h;
}

static int DetailIndexAt(POINT p) {
    for (size_t i = 0; i < g_drows.size(); i++) {
        RECT r = DetailRect((int)i);
        if (p.y >= r.top && p.y <= r.bottom) return (int)i;
    }
    return -1;
}

static void ItemPills(RECT row, RECT& dl, RECT& del) {
    int pw = 92, gap = 8, right = row.right - 14;
    SetR(del, right - pw, row.top + 12, right, row.bottom - 12);
    SetR(dl, del.left - gap - pw, row.top + 12, del.left - gap, row.bottom - 12);
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
        if (PickFile(p)) { SetWindowTextW(g_editPath, p.c_str()); KickPathInfo(); }
        break;
    }
    case ACT_BROWSE_DIR: {
        std::wstring p;
        if (PickFolder(p, L"Выберите папку для загрузки в облако")) { SetWindowTextW(g_editPath, p.c_str()); KickPathInfo(); }
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
    case ACT_DEL_SEL: {
        if (g_open < 0 || g_open >= (int)g_stores.size()) break;
        std::vector<std::wstring> ids;
        for (size_t i = 0; i < g_items.size(); i++)
            if (i < g_checked.size() && g_checked[i]) ids.push_back(wstr(g_items[i].id));
        if (ids.empty()) break;
        char cnt[32];
        snprintf(cnt, sizeof cnt, "%d", (int)ids.size());
        std::wstring q = L"Удалить " + wstr(cnt) + L" элем. из хранилища «" +
                         wstr(g_stores[g_open]) + L"»?\nДействие необратимо.";
        if (MessageBoxW(g_hwnd, q.c_str(), L"Удаление элементов",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) break;
        g_delStore = wstr(g_stores[g_open]);
        g_delQueue = ids;
        std::wstring first = g_delQueue.front();
        g_delQueue.erase(g_delQueue.begin());
        RunCmd({ L"delete", g_delStore, first }, C_DELETE);
        break;
    }
    case ACT_LOG_CLEAR:
        g_logLines.clear();
        SetStatus("Журнал очищен.", TXT_MUTED);
        Repaint();
        break;
    case ACT_AUTO_SCROLL:
        g_autoscroll = !g_autoscroll;
        Repaint();
        break;
    case ACT_LEGAL:
        ShowLegalDialog(g_hwnd);
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
            // акцентная полоса слева — активная вкладка читается однозначно
            RECT bar = { L.nav[i].left + 5, L.nav[i].top + 10,
                         L.nav[i].left + 8, L.nav[i].bottom - 10 };
            GraphicsPath bp; RoundPath(bp, bar, 2);
            SolidBrush bb(C(ACCENT));
            g.FillPath(&bb, &bp);
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
              L.statusTok, 13.0f, tok ? RGB(0x2E, 0x7D, 0x57) : RGB(0xB0, 0x3F, 0x33), true, 0, 1);
    DrawTextC(g, pass ? "Пароль: OK" : "Пароль: нет",
              L.statusPass, 13.0f, pass ? RGB(0x2E, 0x7D, 0x57) : RGB(0xB0, 0x3F, 0x33), true, 0, 1);
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
    else if (!g_detailSummary.empty())
        DrawTextC(g, g_detailSummary, thead, 12.5f, TXT_MUTED, false, 0, 0);

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
        int maxScroll = DetailTotalHeight() - (clipR.bottom - clipR.top);
        if (maxScroll < 0) maxScroll = 0;
        if (g_listScroll > maxScroll) g_listScroll = maxScroll;
        if (g_listScroll < 0) g_listScroll = 0;

        for (size_t i = 0; i < g_drows.size(); i++) {
            RECT r = DetailRect((int)i);
            if (g_drows[i].kind == 0) {
                // заголовок тома — мягкая акцентная плашка
                GraphicsPath vp; RoundPath(vp, r, 10.0f);
                SolidBrush vb(C(RGB(0xE4, 0xEC, 0xFB)));
                g.FillPath(&vb, &vp);
                Pen vpen(C(RGB(0xC6, 0xDA, 0xF6)), 1.0f);
                g.DrawPath(&vpen, &vp);
                RECT th = { r.left + 16, r.top, r.right - 16, r.bottom };
                std::string label = "ТОМ " + g_drows[i].repo;
                if (!g_drows[i].used.empty())
                    label += "   (занято " + g_drows[i].used + " из ~" + g_drows[i].total + ")";
                DrawTextCOne(g, label, th, 12.5f, ACCENT, true, 0, 1);
                continue;
            }
            int idx = g_drows[i].item;
            if (idx < 0 || idx >= (int)g_items.size()) continue;
            bool on = (idx < (int)g_checked.size()) && g_checked[idx] != 0;
            bool hover = PtIn(r, g_mouse.x, g_mouse.y);
            NState st = on ? N_INSET : (hover ? N_HOVER : N_RAISED);
            DrawNeumorphCore(g, r, 16.0f, st, on ? INNER : BG, 12.0f, true);
            // чекбокс
            RECT chk = { r.left + 14, r.top + 14, r.left + 38, r.top + 38 };
            DrawCheckBox(g, chk, on);
            RECT dl, del;
            ItemPills(r, dl, del);
            // имя и id
            RECT nm = { chk.right + 14, r.top + 6, dl.left - 14, r.top + 30 };
            DrawTextCOne(g, g_items[idx].name, nm, 14.5f, TXT, true, 0, 1);
            RECT sub = { nm.left, r.bottom - 20, dl.left - 14, r.bottom - 8 };
            DrawTextCOne(g, "id " + g_items[idx].id + (g_items[idx].sizeText.empty() ? "" : "   ·   " + g_items[idx].sizeText),
                         sub, 11.0f, TXT_MUTED, false, 0, 1);
            // кнопка «Скачать» на строке
            bool dlH = PtIn(dl, g_mouse.x, g_mouse.y);
            bool delH = PtIn(del, g_mouse.x, g_mouse.y);
            DrawPill(g, dl, "Скачать", dlH ? N_HOVER : N_RAISED);
            DrawPill(g, del, "Удалить", delH ? N_HOVER : N_RAISED);
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

static NState BtnSt(BtnId id);

static void DrawConsole(Graphics& g) {
    DrawNeumorphCore(g, L.console, 16.0f, N_INSET, LOG_BG, 9.0f, false);

    // заголовок журнала
    RECT head = { L.console.left + 20, L.console.top + 10,
                  L.console.left + 340, L.console.top + 42 };
    std::string title = g_running ? "Журнал • выполняется…" : "Журнал — вывод GITHUBCLOAD";
    DrawTextC(g, title, head, 13.0f, g_running ? ACCENT : TXT, true, 0, 1);

    // статус операции — правее кнопок
    RECT stR = { L.chkAuto.left - 340, L.console.top + 10, L.chkAuto.left - 14, L.console.top + 42 };
    if (stR.left > head.right)
        DrawTextC(g, g_statusText, stR, 12.0f, g_statusColor, false, 2, 1);

    // «Автопрокрутка»
    RECT cb = { L.chkAuto.left, L.chkAuto.top + 4, L.chkAuto.left + 18, L.chkAuto.top + 22 };
    DrawCheckBox(g, cb, g_autoscroll);
    RECT ct = { cb.right + 8, L.chkAuto.top, L.chkAuto.right, L.chkAuto.bottom };
    DrawTextC(g, "Автопрокрутка", ct, 12.0f, TXT_MUTED, false, 0, 1);

    // «Очистить»
    DrawButton(g, L.btnClearLog, "Очистить", BtnSt(ACT_LOG_CLEAR), false, false);

    RECT inner = { L.console.left + 20, L.console.top + 52,
                   L.console.right - 20, L.console.bottom - 16 };
    int cw = (int)RW(inner) - 10, ch = (int)RH(inner);
    if (cw < 10 || ch < 10) return;
    if (g_logLines.empty()) {
        DrawTextC(g, g_running ? "Запускаем команду…" : "Здесь появится вывод команд.",
                  inner, 13.0f, TXT_MUTED, false, 0, 0);
        return;
    }

    Font f(g_bodyFont, 13.0f, FontStyleRegular, UnitPixel);
    auto measure = [&](const std::wstring& s) {
        RectF rr; g.MeasureString(s.c_str(), (INT)s.size(), &f, PointF(0, 0), &rr); return rr.Width;
    };
    // перенос по словам; слишком длинное слово режется по символам
    std::vector<std::wstring> lines;
    std::vector<COLORREF> cols;
    for (size_t li = 0; li < g_logLines.size(); li++) {
        std::wstring all = Utf8ToWide(g_logLines[li].text);
        COLORREF col = TagColor(g_logLines[li].tag);
        std::wstring cur;
        size_t i = 0;
        while (true) {
            size_t sp = all.find(L' ', i);
            bool last = (sp == std::wstring::npos);
            std::wstring word = last ? all.substr(i) : all.substr(i, sp - i);
            if (!word.empty()) {
                std::wstring cand = cur.empty() ? word : cur + L" " + word;
                if (measure(cand) <= (REAL)cw) {
                    cur = cand;
                } else {
                    if (!cur.empty()) { lines.push_back(cur); cols.push_back(col); cur.clear(); }
                    std::wstring w = word;
                    while (measure(w) > (REAL)cw && w.size() > 1) {
                        size_t k = w.size();
                        while (k > 1 && measure(w.substr(0, k)) > (REAL)cw) k--;
                        lines.push_back(w.substr(0, k));
                        cols.push_back(col);
                        w = w.substr(k);
                    }
                    cur = w;
                }
            }
            if (last) break;
            i = sp + 1;
        }
        lines.push_back(cur);
        cols.push_back(col);
    }

    int lineH = 20;
    int total = (int)lines.size() * lineH;
    int maxScroll = total > ch ? total - ch : 0;
    if (g_autoscroll) g_logScroll = maxScroll;
    if (g_logScroll > maxScroll) g_logScroll = maxScroll;
    if (g_logScroll < 0) g_logScroll = 0;

    Gdiplus::Bitmap* cbmp = new Gdiplus::Bitmap(cw, (total > 0 ? total : 1), PixelFormat32bppARGB);
    {
        Graphics gc(cbmp);
        gc.Clear(Color(0, 0, 0, 0));
        gc.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        for (int i = 0; i < (int)lines.size(); i++) {
            RECT nr = { 0, i * lineH, cw, i * lineH + lineH };
            SolidBrush tb(C(cols[i]));
            DrawTextStr(gc, lines[i], nr, f, tb, 0, 0);
        }
    }
    RECT clip = Shift(inner, 0, -4);
    GraphicsPath cp; RoundPath(cp, clip, 10);
    g.SetClip(&cp);
    g.DrawImage(cbmp, (REAL)inner.left, (REAL)inner.top - (REAL)g_logScroll,
                (REAL)cw, (REAL)total);
    g.ResetClip();
    delete cbmp;
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
            const char* label0[4] = { "Обновить", "Скачать всё", "Удалить", "Стереть" };
            const char* labelIn[5] = { "← Хранилища", "Скачать выбранные", "Скачать всё",
                                       "Удалить выбранные", "Обновить" };
            for (int s = 0; s < L.topCount; s++) {
                BtnId id = TopBtnForSlot(s);
                bool accent = (id == ACT_REFRESH);
                std::string lab;
                if (g_open >= 0) {
                    lab = labelIn[s];
                    if (id == ACT_DL_SEL) accent = true;
                } else {
                    lab = label0[s];
                }
                DrawButton(g, L.topBtn[s], lab, BtnSt(id), accent, IsDisabled(id));
            }
            DrawStoreRows(g);
        } else if (g_tab == 1) {
            RECT lab = { L.pathField.left, L.pathField.top - 22, L.pathField.right, L.pathField.top - 4 };
            DrawTextC(g, "Файл или папка", lab, 12.5f, TXT_MUTED, true, 0, 0);
            DrawFieldBox(g, L.pathField);
            DrawButton(g, L.btnBrowseFile, "Выбрать файл", BtnSt(ACT_BROWSE_FILE), false, false);
            DrawButton(g, L.btnBrowseDir, "Выбрать папку", BtnSt(ACT_BROWSE_DIR), false, false);
            if (!g_pathInfo.empty() && g_pathInfo != "—")
                DrawTextC(g, g_pathInfo, L.pathInfo, 12.5f, g_pathInfoCol, false, 0, 1);

            RECT lab2 = { L.storeField.left, L.storeField.top - 22, L.storeField.right - 60, L.storeField.top - 4 };
            DrawTextC(g, "Имя хранилища (репозиторий)", lab2, 12.5f, TXT_MUTED, true, 0, 0);
            DrawFieldBox(g, L.storeField);
            DrawButton(g, L.btnUpload, "Загрузить в облако", BtnSt(ACT_UPLOAD), true, IsDisabled((BtnId)ACT_UPLOAD));
            DrawStoreDrop(g);   // список поверх кнопки загрузки, как обычное меню
            DrawTextC(g, UPLOAD_INFO, L.uploadInfo, 12.0f, TXT_MUTED, false, 0, 0);
        } else {
            DrawNeumorphCore(g, L.selftestInfo, 14.0f, N_INSET, INNER, 7.0f, false);
            RECT info = Shift(L.selftestInfo, 22, 16);
            info.bottom -= 12;
            DrawTextC(g, SELFTEST_INFO, info, 12.5f, TXT_MUTED, false, 0, 0);

            DrawButton(g, L.btnSelftest, "Запустить самопроверку", BtnSt(ACT_SELFTEST_RUN), true, IsDisabled((BtnId)ACT_SELFTEST_RUN));

            DrawNeumorphCore(g, L.diagCard, 14.0f, N_INSET, INNER, 7.0f, false);
            RECT dh = { L.diagCard.left + 22, L.diagCard.top + 20,
                        L.diagCard.right - 250, L.diagCard.top + 52 };
            DrawTextC(g, "Диагностика", dh, 13.5f, TXT, true, 0, 1);
            DrawButton(g, L.btnLegal, "Правовая информация", BtnSt(ACT_LEGAL), false, IsDisabled((BtnId)ACT_LEGAL));
            DrawTextC(g, DiagnosticsText(), L.diagText, 12.0f, TXT_MUTED, false, 0, 0);
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
                // уровень «внутри хранилища»: клик по строке элемента (тома — только заголовки)
                int row = DetailIndexAt(p);
                if (row >= 0 && row < (int)g_drows.size() && g_drows[row].kind == 1) {
                    int idx = g_drows[row].item;
                    if (idx >= 0 && idx < (int)g_items.size()) {
                        RECT r = DetailRect(row);
                        RECT dl, del;
                        ItemPills(r, dl, del);
                        if (PtIn(dl, p.x, p.y)) {
                            g_sel = idx;
                            HandleAction(ACT_DL_ONE);   // скачать именно этот файл
                        } else if (PtIn(del, p.x, p.y)) {
                            std::wstring q = L"Удалить элемент «" + wstr(g_items[idx].name) +
                                             L"» (id " + wstr(g_items[idx].id) +
                                             L") из хранилища «" + wstr(g_stores[g_open]) +
                                             L"»?\nДействие необратимо.";
                            if (MessageBoxW(hwnd, q.c_str(), L"Удаление элемента",
                                            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                                RunCmd({ L"delete", wstr(g_stores[g_open]), wstr(g_items[idx].id) },
                                       C_DELETE);
                            }
                        } else {
                            if (idx < (int)g_checked.size()) g_checked[idx] = g_checked[idx] ? 0 : 1;
                        }
                        g_down = false; g_downId = 0;
                        Repaint();
                        return 0;
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
        g_log = *out;
        g_running = false;
        LogAddBlock(g_log, "");
        int op = (int)w;
        if (op == C_LIST) {
            ParseStores(g_log, g_stores);
            g_sel = -1; g_open = -1;
            g_items.clear(); g_checked.clear();
            g_drows.clear(); g_detailSummary.clear();
            RefreshLayout();
        } else if (op == C_LIST_ITEMS) {
            ParseItems(g_log, g_items);
            g_checked.assign(g_items.size(), 0);
            g_sel = -1;
            g_listScroll = 0;
            RebuildStoreDetailRows();
            RefreshLayout();
        } else if (op == C_DELETE && !g_delQueue.empty()) {
            std::wstring next = g_delQueue.front();
            g_delQueue.erase(g_delQueue.begin());
            if (!g_running) RunCmd({ L"delete", g_delStore, next }, C_DELETE);
        } else if (op == C_DELETE) {
            if (g_open >= 0) RunCmd({ L"list", wstr(g_stores[g_open]) }, C_LIST_ITEMS);
        } else if (op == C_WIPE) {
            g_open = -1;
            g_items.clear(); g_checked.clear();
            g_drows.clear(); g_detailSummary.clear();
            RunCmd({ L"-" }, C_LIST);
        }
        SetStatus(g_running ? "Выполняется…" : "Готово.", g_running ? ACCENT : TXT_MUTED);
        delete out;
        Repaint();
        return 0;
    }
    case WM_APP_PATHINFO: {
        std::string* t = (std::string*)l;
        if ((int)w == g_pathSeq) {
            g_pathInfo = *t;
            g_pathInfoCol = (*t == "путь не найден") ? RGB(0xC0, 0x39, 0x2B) : TXT_MUTED;
        }
        delete t;
        Repaint();
        return 0;
    }
    case WM_APP_PH:
        UpdatePlaceholders();
        if ((int)w == 1 && l == 1) KickPathInfo();
        return 0;
    case WM_DROPFILES: {
        HDROP hd = (HDROP)w;
        wchar_t path[MAX_PATH];
        if (DragQueryFileW(hd, 0, path, MAX_PATH) > 0) {
            g_tab = 1;                       // переключаемся во вкладку «Загрузка»
            g_storeDropOpen = false;
            if (g_editPath) SetWindowTextW(g_editPath, path);
            UpdatePlaceholders();
            KickPathInfo();
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
static void PrintConsole(const std::string& text) {
    std::wstring w = Utf8ToWide(text);
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written = 0;
        if (h && h != INVALID_HANDLE_VALUE) {
            // В консоль — Unicode; если stdout перенаправлен (файл/пайп),
            // WriteConsoleW невозможен — пишем UTF-8 байтами.
            if (!WriteConsoleW(h, w.c_str(), (DWORD)w.size(), &written, nullptr))
                WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr);
        }
        FreeConsole();
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    g_hInst = hInstance;

    // --- служебные флаги командной строки ---
    std::string rawArgs = lpCmdLine ? lpCmdLine : "";
    std::vector<std::string> toks;
    {
        std::string cur;
        for (size_t i = 0; i <= rawArgs.size(); i++) {
            if (i == rawArgs.size() || rawArgs[i] == ' ' || rawArgs[i] == '\t' || rawArgs[i] == '"') {
                if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
            } else cur += rawArgs[i];
        }
    }
    bool fVersion = false, fHelp = false, fSmoke = false, fAccept = false, fUnknown = false;
    for (size_t i = 0; i < toks.size(); i++) {
        const std::string& t = toks[i];
        if (t == "--version" || t == "-V") fVersion = true;
        else if (t == "--help" || t == "-h") fHelp = true;
        else if (t == "--smoke") fSmoke = true;
        else if (t == "--accept-terms") fAccept = true;
        else fUnknown = true;
    }
    if (fVersion) { PrintConsole(std::string(GUI_NAME) + " " + GUI_VERSION + "\n"); return 0; }
    if (fHelp)    { PrintConsole(USAGE); return 0; }
    if (fUnknown) { PrintConsole("Неизвестные аргументы командной строки.\n"); PrintConsole(USAGE); return 2; }

    GdiplusStartupInput gsi;
    GdiplusStartup(&g_gdip, &gsi, nullptr);
    InitFonts();

    // Правовое предупреждение — одна строка (в консоль и в журнал, один раз за запуск).
    PrintConsole(std::string(NOTICE_TEXT) + "\n");
    LogAdd(NOTICE_TEXT, "gui");

    std::string termsError;
    if (fAccept) {
        if (WriteTermsAcceptance(termsError))
            PrintConsole(std::string("Условия использования приняты: записан ") + TERMS_FILE + "\n");
        else
            LogAdd("ВНИМАНИЕ: " + termsError, "warn");
    }

    // Подтверждение условий: диалог не показывается при --smoke / --accept-terms.
    if (!fSmoke && !fAccept && !TermsAccepted()) {
        if (!ShowTermsDialog(nullptr)) {
            PrintConsole("Условия использования не приняты — работа прекращена, ничего не изменено.\n");
            GdiplusShutdown(g_gdip);
            return 3;
        }
        if (!WriteTermsAcceptance(termsError))
            LogAdd("ВНИМАНИЕ: " + termsError + " — условия подтверждены только на этот запуск", "warn");
    }

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

    if (fSmoke) {
        // headless-проверка: строим все три страницы и закрываем окно без цикла сообщений
        for (int t = 0; t <= 2; t++) {
            g_tab = t;
            RefreshLayout();
            DoPaint();
        }
        DestroyWindow(g_hwnd);
        GdiplusShutdown(g_gdip);
        return 0;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(g_gdip);
    return (int)msg.wParam;
}
