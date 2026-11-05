// In-game settings menu (Ctrl+Shift+F10), included by ddraw_proxy.cpp.
//
// The panel is drawn with GDI into a 32-bit DIB whenever it changes, converted
// to the game's RGB565 and copied over every presented frame (the rest of the
// screen is darkened). While the menu is open the game's logic update is
// skipped (the game is paused) and mouse/keyboard messages to the game window
// are consumed by the menu. Every change is written to the ini at once; options
// that cannot change while the game runs are marked and apply after a restart.

struct MenuChoice { std::string ini, text; };
struct MenuOpt {
    const char* key;
    const char* label;
    const char* help;
    int tab;            // 0 = basic, 1 = advanced
    bool restart;       // applies after restarting the game
    std::vector<MenuChoice> choices;
    int cur = 0, initial = 0;
};

static struct MenuState {
    bool inited = false;
    bool open = false;
    bool releaseWait = false;   // closed, waiting until keys/buttons are released
    bool dirty = true;
    int tab = 0, sel = 0;
    std::vector<MenuOpt> opts;
    // layout (screen W x H, panel at px,py size pw x ph, font height f)
    int W = 0, H = 0, px = 0, py = 0, pw = 0, ph = 0, f = 0;
    int pad = 0, titleH = 0, tabsH = 0, rowH = 0, rowsY = 0, valX = 0, helpY = 0, footY = 0;
    RECT tabRc[2] = {}, closeRc = {};
    std::vector<int> rows;      // option indexes shown on the current tab
    std::vector<uint16_t> pix;  // rendered panel, RGB565
    // cursor (system arrow, premultiplied)
    int cs = 0, hotX = 0, hotY = 0;
    std::vector<uint8_t> curRGB, curA;
    bool keyPrev = false;
} g_menu;

static void MenuIniPath(char* out, size_t n) { sprintf_s(out, n, "%s\\" MOD_NAME ".ini", g_dir); }

static void MenuAdd(const char* key, const char* label, const char* help, int tab, bool restart,
                    std::initializer_list<std::pair<const char*, const char*>> ch)
{
    MenuOpt o;
    o.key = key; o.label = label; o.help = help; o.tab = tab; o.restart = restart;
    for (auto& c : ch) o.choices.push_back({ c.first, c.second });
    g_menu.opts.push_back(o);
}

static bool MenuValueEquals(const std::string& a, const std::string& b)
{
    if (_stricmp(a.c_str(), b.c_str()) == 0) return true;
    char* ea; char* eb;
    double da = strtod(a.c_str(), &ea), db = strtod(b.c_str(), &eb);
    return *ea == 0 && *eb == 0 && ea != a.c_str() && eb != b.c_str() && fabs(da - db) < 1e-6;
}

static void MenuInit()
{
    if (g_menu.inited) return;
    g_menu.inited = true;
#define ONOFF { { "1", "On" }, { "0", "Off" } }
    // ---- basic
    MenuAdd("Enabled", "Smoothing (whole patch)", "Off = the game runs like the original (resolution, "
            "interface and bug fixes stay).", 0, false, ONOFF);
    MenuAdd("Camera", "Smooth camera", "Smooth camera scrolling and zoom.", 0, false, ONOFF);
    MenuAdd("Objects", "Smooth units and projectiles", "Smooth movement of soldiers, vehicles and projectiles.",
            0, false, ONOFF);
    MenuAdd("Mouse", "Smooth cursor", "The arrow and dragged inventory items follow the mouse every frame.",
            0, false, ONOFF);
    MenuAdd("MaxFps", "Frame rate limit", "Maximum frames per second. The speed of the game does not change.",
            0, false, { { "0", "Monitor refresh rate" }, { "30", "30" }, { "60", "60" }, { "75", "75" },
                        { "100", "100" }, { "120", "120" }, { "144", "144" }, { "165", "165" },
                        { "240", "240" }, { "1000", "Unlimited" } });
    MenuAdd("UIScale", "Interface size", "Size of the command panel, inventory and notebook in missions. "
            "Automatic = screen height / 720.", 0, false,
            { { "0", "Automatic" }, { "1", "Original" }, { "1.25", "1.25x" }, { "1.5", "1.5x" },
              { "1.75", "1.75x" }, { "2", "2x" }, { "2.5", "2.5x" }, { "3", "3x" } });
    MenuAdd("HDResolution", "HD resolution", "Used by the game's \"1280x720 (16:9)\" video mode - pick that "
            "mode in the game's video options.", 0, true,
            { { "1280x720", "1280x720" }, { "1366x768", "1366x768" }, { "1600x900", "1600x900" },
              { "1920x1080", "1920x1080" }, { "2560x1080", "2560x1080" }, { "2560x1440", "2560x1440" },
              { "3440x1440", "3440x1440" }, { "3840x2160", "3840x2160" } });
    MenuAdd("Cheats", "Cheat keys", "Cheat keys work without typing the password.", 0, false, ONOFF);
    // ---- advanced
    MenuAdd("Menus", "Smooth cursor in menus", "Extra frames in the game's menus and the pause menu.",
            1, false, ONOFF);
    MenuAdd("HDMenus", "Pause menu in HD", "Pause menu in the game resolution with a sharp view of the game "
            "behind it.", 1, true, ONOFF);
    MenuAdd("PauseMenuScale", "Pause menu size", "Applies the next time the pause menu is opened.", 1, false,
            { { "1", "Full screen" }, { "2", "Enlarged 4:3" }, { "0", "Small, centred" } });
    MenuAdd("CncDDraw", "Borderless full screen", "Through the bundled cnc-ddraw (no black screens on "
            "resolution changes). Off = the system's exclusive full screen.", 1, true, ONOFF);
    MenuAdd("Optimizations", "Faster rendering", "Faster water drawing and a map background cache.", 1, true,
            ONOFF);
    MenuAdd("Debug", "Log statistics", "Statistics in " MOD_NAME ".log. Memory diagnostics cause stutter "
            "(for development only).", 1, true,
            { { "0", "Off" }, { "1", "Statistics" }, { "2", "Memory diagnostics" } });
    MenuAdd("Profile", "Profiler", "Logs the slowest parts of the game every 10 seconds (for development).",
            1, true, { { "0", "Off" }, { "1", "On" } });
#undef ONOFF

    // Desktop resolution as an HD resolution choice
    {
        char desk[32];
        sprintf_s(desk, "%dx%d", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        for (auto& o : g_menu.opts)
            if (!strcmp(o.key, "HDResolution")) {
                bool have = false;
                for (auto& c : o.choices) have |= _stricmp(c.ini.c_str(), desk) == 0;
                if (!have) o.choices.push_back({ desk, std::string(desk) + " (desktop)" });
            }
    }
    // Current values; values not in the list are kept as an extra choice.
    char ini[MAX_PATH];
    MenuIniPath(ini, sizeof(ini));
    for (auto& o : g_menu.opts) {
        char def[16], val[64];
        strcpy_s(def, o.choices[0].ini.c_str());
        if (!strcmp(o.key, "Optimizations") || !strcmp(o.key, "Enabled") || !strcmp(o.key, "Camera") ||
            !strcmp(o.key, "Objects") || !strcmp(o.key, "Mouse") || !strcmp(o.key, "Menus") ||
            !strcmp(o.key, "HDMenus") || !strcmp(o.key, "CncDDraw")) strcpy_s(def, "1");
        if (!strcmp(o.key, "Cheats")) strcpy_s(def, "0");
        if (!strcmp(o.key, "HDResolution")) strcpy_s(def, "1920x1080");
        GetPrivateProfileStringA(MOD_NAME, o.key, def, val, sizeof(val), ini);
        int found = -1;
        for (int i = 0; i < (int)o.choices.size(); i++)
            if (MenuValueEquals(o.choices[i].ini, val)) { found = i; break; }
        if (found < 0) { o.choices.push_back({ val, val }); found = (int)o.choices.size() - 1; }
        o.cur = o.initial = found;
    }
}

static bool PatchScaledPauseOnce();
static bool PatchUiScaleOnce();

// Takes an option's new value into effect while the game runs.
static void MenuApply(const MenuOpt& o)
{
    const char* v = o.choices[o.cur].ini.c_str();
    int n = atoi(v);
    if (!strcmp(o.key, "Enabled"))      { g_cfg.enabled = n != 0; g_active = g_cfg.enabled; }
    else if (!strcmp(o.key, "Camera"))  g_cfg.camera = n != 0;
    else if (!strcmp(o.key, "Objects")) g_cfg.objects = n != 0;
    else if (!strcmp(o.key, "Mouse"))   g_cfg.mouse = n != 0;
    else if (!strcmp(o.key, "Menus"))   g_cfg.menus = n != 0;
    else if (!strcmp(o.key, "Cheats"))  g_cfg.cheats = n != 0;
    else if (!strcmp(o.key, "MaxFps"))  g_cfg.maxFps = ResolveMaxFps(n);
    else if (!strcmp(o.key, "UIScale")) {
        g_cfg.uiScale = (float)atof(v);
        if (g_cfg.uiScale != 1.0f) PatchUiScaleOnce();
    } else if (!strcmp(o.key, "PauseMenuScale")) {
        g_cfg.pauseScale = n;
        if (n && g_cfg.hdMenus) PatchScaledPauseOnce();
    }
    Log("settings menu: %s=%s\n", o.key, v);
}

static void MenuChange(int optIndex, int delta)
{
    MenuOpt& o = g_menu.opts[optIndex];
    int n = (int)o.choices.size();
    o.cur = ((o.cur + delta) % n + n) % n;
    char ini[MAX_PATH];
    MenuIniPath(ini, sizeof(ini));
    WritePrivateProfileStringA(MOD_NAME, o.key, o.choices[o.cur].ini.c_str(), ini);
    if (!o.restart) MenuApply(o);
    g_menu.dirty = true;
}

static bool MenuRestartPending()
{
    for (auto& o : g_menu.opts) if (o.restart && o.cur != o.initial) return true;
    return false;
}

static void MenuSetTab(int tab)
{
    g_menu.tab = tab;
    g_menu.rows.clear();
    for (int i = 0; i < (int)g_menu.opts.size(); i++) if (g_menu.opts[i].tab == tab) g_menu.rows.push_back(i);
    if (g_menu.sel >= (int)g_menu.rows.size()) g_menu.sel = (int)g_menu.rows.size() - 1;
    if (g_menu.sel < 0) g_menu.sel = 0;
    g_menu.dirty = true;
}

static void MenuLayoutFor(int W, int H)
{
    MenuState& m = g_menu;
    m.W = W; m.H = H;
    int maxRows = 0;
    for (int t = 0; t < 2; t++) {
        int c = 0;
        for (auto& o : m.opts) c += o.tab == t;
        if (c > maxRows) maxRows = c;
    }
    int f = H * 24 / 1080;
    if (f < 14) f = 14;
    for (;;) {
        m.f = f;
        m.pad = f;
        m.titleH = f * 2;
        m.tabsH = f * 2;
        m.rowH = f * 16 / 10;
        m.pw = f * 34;
        m.ph = m.titleH + m.tabsH + f / 2 + maxRows * m.rowH + f / 2 + f * 3 + f * 2 + f / 2;
        if ((m.pw <= W - 16 && m.ph <= H - 16) || f <= 8) break;
        f--;
    }
    if (m.pw > W) m.pw = W;
    m.px = (W - m.pw) / 2;
    m.py = (H - m.ph) / 2;
    m.rowsY = m.titleH + m.tabsH + f / 2;
    m.valX = m.pw * 56 / 100;
    m.helpY = m.rowsY + maxRows * m.rowH + f / 2;
    m.footY = m.helpY + f * 3;
    int tw = f * 8;
    SetRect(&m.tabRc[0], m.pad, m.titleH, m.pad + tw, m.titleH + m.tabsH);
    SetRect(&m.tabRc[1], m.pad + tw, m.titleH, m.pad + 2 * tw, m.titleH + m.tabsH);
    SetRect(&m.closeRc, m.pw - m.pad - f * 7, m.footY + f / 4, m.pw - m.pad, m.footY + f * 2 - f / 4);
    m.cs = 0;   // cursor re-rendered for the new size
    m.dirty = true;
}

static void MenuFill(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    RECT rc = { l, t, r, b };
    SetDCBrushColor(dc, c);
    FillRect(dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void MenuText(HDC dc, const char* s, int l, int t, int r, int b, COLORREF c, UINT fmt)
{
    RECT rc = { l, t, r, b };
    SetTextColor(dc, c);
    DrawTextA(dc, s, -1, &rc, fmt | DT_NOPREFIX);
}

// Renders the panel into m.pix (RGB565).
static void MenuRender()
{
    MenuState& m = g_menu;
    const int pw = m.pw, ph = m.ph, f = m.f;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = pw;
    bi.bmiHeader.biHeight = -ph;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bmp || !bits) { if (bmp) DeleteObject(bmp); if (dc) DeleteDC(dc); return; }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HFONT font = CreateFontA(-f * 8 / 10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Segoe UI");
    HFONT bold = CreateFontA(-f * 9 / 10, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Segoe UI");
    HFONT fontSm = CreateFontA(-f * 7 / 10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    const COLORREF bg = RGB(24, 26, 30), bar = RGB(38, 42, 48), accent = RGB(206, 172, 92),
                   text = RGB(232, 232, 228), dim = RGB(140, 142, 146), selBg = RGB(50, 56, 66),
                   warn = RGB(232, 150, 70), on = RGB(150, 210, 120), off = RGB(210, 130, 120);
    const int b = f / 12 > 1 ? f / 12 : 1;   // border width

    MenuFill(dc, 0, 0, pw, ph, accent);
    MenuFill(dc, b, b, pw - b, ph - b, bg);
    // title bar
    MenuFill(dc, b, b, pw - b, m.titleH, bar);
    SelectObject(dc, bold);
    MenuText(dc, MOD_NAME " " MOD_VERSION, m.pad, 0, pw - m.pad, m.titleH, accent, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    SelectObject(dc, fontSm);
    MenuText(dc, "Ctrl+Shift+F10", m.pad, 0, pw - m.pad, m.titleH, dim, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    // tabs
    static const char* const tabNames[2] = { "Basic", "Advanced" };
    SelectObject(dc, font);
    for (int t = 0; t < 2; t++) {
        const RECT& r = m.tabRc[t];
        bool act = t == m.tab;
        if (act) {
            MenuFill(dc, r.left, r.top + f / 4, r.right, r.bottom, selBg);
            MenuFill(dc, r.left, r.bottom - f / 8, r.right, r.bottom, accent);
        }
        MenuText(dc, tabNames[t], r.left, r.top + f / 4, r.right, r.bottom, act ? text : dim,
                 DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }
    MenuFill(dc, m.pad, m.titleH + m.tabsH - 1, pw - m.pad, m.titleH + m.tabsH, RGB(60, 64, 72));
    // rows
    for (int i = 0; i < (int)m.rows.size(); i++) {
        const MenuOpt& o = m.opts[m.rows[i]];
        int y = m.rowsY + i * m.rowH;
        bool sel = i == m.sel;
        if (sel) MenuFill(dc, m.pad, y, pw - m.pad, y + m.rowH, selBg);
        SelectObject(dc, font);
        MenuText(dc, o.label, m.pad + f / 2, y, m.valX, y + m.rowH, text, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        if (o.restart) {
            SIZE sz;
            GetTextExtentPoint32A(dc, o.label, (int)strlen(o.label), &sz);
            MenuText(dc, " *", m.pad + f / 2 + sz.cx, y, m.valX, y + m.rowH, warn, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        }
        const std::string& val = o.choices[o.cur].text;
        COLORREF vc = val == "On" ? on : val == "Off" ? off : text;
        int vr = pw - m.pad - f / 2;
        MenuText(dc, "<", m.valX, y, vr, y + m.rowH, sel ? accent : dim, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        MenuText(dc, ">", m.valX, y, vr, y + m.rowH, sel ? accent : dim, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        MenuText(dc, val.c_str(), m.valX + f, y, vr - f, y + m.rowH, vc, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }
    // help for the selected option
    MenuFill(dc, m.pad, m.helpY, pw - m.pad, m.helpY + 1, RGB(60, 64, 72));
    if (m.sel >= 0 && m.sel < (int)m.rows.size()) {
        const MenuOpt& o = m.opts[m.rows[m.sel]];
        SelectObject(dc, fontSm);
        std::string h = o.help;
        if (o.restart) h += " (* applies after restarting the game)";
        MenuText(dc, h.c_str(), m.pad + f / 2, m.helpY + f / 3, pw - m.pad - f / 2, m.footY, dim, DT_WORDBREAK | DT_LEFT);
    }
    // footer
    SelectObject(dc, fontSm);
    if (MenuRestartPending())
        MenuText(dc, "* Restart the game to apply the marked changes.", m.pad + f / 2, m.footY, m.closeRc.left,
                 m.footY + f * 2, warn, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    else
        MenuText(dc, "Changes are saved automatically.", m.pad + f / 2, m.footY, m.closeRc.left, m.footY + f * 2, dim,
                 DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    const RECT& c = m.closeRc;
    MenuFill(dc, c.left, c.top, c.right, c.bottom, accent);
    MenuFill(dc, c.left + b, c.top + b, c.right - b, c.bottom - b, bar);
    SelectObject(dc, font);
    MenuText(dc, "Close (Esc)", c.left, c.top, c.right, c.bottom, text, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

    GdiFlush();
    m.pix.resize((size_t)pw * ph);
    const uint32_t* src = (const uint32_t*)bits;
    for (size_t i = 0; i < m.pix.size(); i++) {
        uint32_t p = src[i];
        m.pix[i] = (uint16_t)((((p >> 16) & 0xF8) << 8) | (((p >> 8) & 0xFC) << 3) | ((p & 0xFF) >> 3));
    }
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(font); DeleteObject(bold); DeleteObject(fontSm);
    DeleteObject(bmp);
    DeleteDC(dc);
    m.dirty = false;
}

// System arrow cursor, drawn over black and over white to get alpha.
static void MenuRenderCursor()
{
    MenuState& m = g_menu;
    int cs = 32 * m.H / 1080;
    if (cs < 32) cs = 32;
    HCURSOR hc = LoadCursor(nullptr, IDC_ARROW);
    ICONINFO ii = {};
    m.hotX = m.hotY = 0;
    if (GetIconInfo(hc, &ii)) {
        m.hotX = ii.xHotspot * cs / 32; m.hotY = ii.yHotspot * cs / 32;
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
    }
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = cs;
    bi.bmiHeader.biHeight = -cs;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits[2] = {};
    HBITMAP bmp[2];
    for (int k = 0; k < 2; k++) {
        bmp[k] = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits[k], nullptr, 0);
        if (!bmp[k]) { DeleteDC(dc); return; }
        HGDIOBJ old = SelectObject(dc, bmp[k]);
        MenuFill(dc, 0, 0, cs, cs, k ? RGB(255, 255, 255) : RGB(0, 0, 0));
        DrawIconEx(dc, 0, 0, hc, cs, cs, 0, nullptr, DI_NORMAL);
        GdiFlush();
        SelectObject(dc, old);
    }
    m.curRGB.resize((size_t)cs * cs * 3);
    m.curA.resize((size_t)cs * cs);
    const uint32_t* pb = (const uint32_t*)bits[0];
    const uint32_t* pw = (const uint32_t*)bits[1];
    for (int i = 0; i < cs * cs; i++) {
        int bk[3] = { (int)(pb[i] >> 16) & 255, (int)(pb[i] >> 8) & 255, (int)pb[i] & 255 };
        int wh[3] = { (int)(pw[i] >> 16) & 255, (int)(pw[i] >> 8) & 255, (int)pw[i] & 255 };
        int a = 255 - ((wh[0] - bk[0]) + (wh[1] - bk[1]) + (wh[2] - bk[2])) / 3;
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        m.curA[i] = (uint8_t)a;
        for (int c = 0; c < 3; c++) m.curRGB[(size_t)i * 3 + c] = (uint8_t)bk[c];   // premultiplied
    }
    DeleteObject(bmp[0]); DeleteObject(bmp[1]);
    DeleteDC(dc);
    m.cs = cs;
}

// Mouse position in game surface coordinates.
static bool MenuMouse(int& x, int& y)
{
    POINT p;
    if (!(*(BOOL(WINAPI**)(POINT*))addr::IAT_GetCursorPos)(&p)) return false;
    if (g_gameWnd) ScreenToClient(g_gameWnd, &p);
    x = p.x - g_menu.px; y = p.y - g_menu.py;   // panel coordinates
    return true;
}

static void MenuOpen(bool open)
{
    if (open == g_menu.open) return;
    MenuInit();
    g_menu.open = open;
    if (open) { MenuSetTab(g_menu.tab); g_menu.dirty = true; }
    else g_menu.releaseWait = true;
    Log("settings menu %s\n", open ? "opened" : "closed");
}

// Called every loop iteration (main thread): the hotkey.
static void MenuPoll()
{
    bool key = (GetAsyncKeyState(VK_F10) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
               (GetAsyncKeyState(VK_SHIFT) & 0x8000);
    if (key && !g_menu.keyPrev) MenuOpen(!g_menu.open);
    g_menu.keyPrev = key;
    if (g_menu.releaseWait) {
        static const int keys[] = { VK_ESCAPE, VK_RETURN, VK_SPACE, VK_F10, VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
        bool held = false;
        for (int k : keys) held |= (GetAsyncKeyState(k) & 0x8000) != 0;
        if (!held) g_menu.releaseWait = false;
    }
}

// The game's logic is paused while the menu is open (and until the keys or
// buttons used to close it are released).
static bool MenuBlocksGame() { return g_menu.open || g_menu.releaseWait; }

static void MenuHover(int x, int y)
{
    MenuState& m = g_menu;
    if (x < m.pad || x >= m.pw - m.pad || y < m.rowsY) return;
    int i = (y - m.rowsY) / m.rowH;
    if (i >= 0 && i < (int)m.rows.size() && i != m.sel) { m.sel = i; m.dirty = true; }
}

static void MenuClick(int x, int y, int button)   // button: 0 left, 1 right, 2 wheel up, 3 wheel down
{
    MenuState& m = g_menu;
    POINT p = { x, y };
    if (button == 0) {
        if (PtInRect(&m.closeRc, p)) { MenuOpen(false); return; }
        for (int t = 0; t < 2; t++)
            if (PtInRect(&m.tabRc[t], p)) { if (t != m.tab) MenuSetTab(t); return; }
    }
    if (x < m.pad || x >= m.pw - m.pad || y < m.rowsY) return;
    int i = (y - m.rowsY) / m.rowH;
    if (i < 0 || i >= (int)m.rows.size()) return;
    m.sel = i;
    int delta = 1;
    if (button == 1 || button == 3) delta = -1;
    else if (button == 0 && x >= m.valX && x < m.valX + m.f * 2) delta = -1;   // "<"
    MenuChange(m.rows[i], delta);
}

// Window messages while the menu is open (or being closed): returns true when
// the message is consumed.
static bool MenuHandleMessage(UINT msg, WPARAM wp, LPARAM)
{
    MenuState& m = g_menu;
    if (!m.open) {
        // after closing: the release of the button/key that closed the menu
        if (m.releaseWait && ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEMOVE) ||
                              (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)))
            return true;
        return false;
    }
    int x = 0, y = 0;
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) {
        if (!MenuMouse(x, y)) return true;
        switch (msg) {
        case WM_MOUSEMOVE: MenuHover(x, y); break;
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: MenuClick(x, y, 0); break;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: MenuClick(x, y, 1); break;
        case WM_MOUSEWHEEL: MenuClick(x, y, (short)HIWORD(wp) > 0 ? 2 : 3); break;
        }
        return true;
    }
    if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) {
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
            int n = (int)m.rows.size();
            switch (wp) {
            case VK_ESCAPE: MenuOpen(false); break;
            case VK_UP: if (n) { m.sel = (m.sel + n - 1) % n; m.dirty = true; } break;
            case VK_DOWN: if (n) { m.sel = (m.sel + 1) % n; m.dirty = true; } break;
            case VK_LEFT: if (n) MenuChange(m.rows[m.sel], -1); break;
            case VK_RIGHT: case VK_RETURN: case VK_SPACE: if (n) MenuChange(m.rows[m.sel], 1); break;
            case VK_TAB: case VK_PRIOR: case VK_NEXT: MenuSetTab(1 - m.tab); break;
            }
        }
        return true;
    }
    return false;
}

// Over every presented frame while open: darken the game, draw the panel and the arrow.
static void MenuDraw(void* canvas)
{
    MenuState& m = g_menu;
    if (!m.open || !canvas) return;
    uintptr_t surf = *(uintptr_t*)canvas;
    uint8_t* base = surf ? At<uint8_t*>(surf, 4) : nullptr;
    if (!base) return;
    const int pitch = At<int>(surf, 8), W = At<int>(surf, 0x10), H = At<int>(surf, 0x14);
    if (W < 64 || H < 64) return;
    if (W != m.W || H != m.H) MenuLayoutFor(W, H);
    if (m.dirty) MenuRender();
    if (m.pix.size() != (size_t)m.pw * m.ph) return;
    for (int y = 0; y < H; y++) {
        uint16_t* row = (uint16_t*)(base + (size_t)y * pitch);
        if (y >= m.py && y < m.py + m.ph) {
            for (int x = 0; x < m.px; x++) row[x] = (row[x] >> 1) & 0x7BEF;
            memcpy(row + m.px, &m.pix[(size_t)(y - m.py) * m.pw], (size_t)m.pw * 2);
            for (int x = m.px + m.pw; x < W; x++) row[x] = (row[x] >> 1) & 0x7BEF;
        } else {
            for (int x = 0; x < W; x++) row[x] = (row[x] >> 1) & 0x7BEF;
        }
    }
    // arrow
    if (!m.cs) MenuRenderCursor();
    int mx, my;
    if (m.cs && MenuMouse(mx, my)) {
        mx += m.px - m.hotX; my += m.py - m.hotY;
        for (int y = 0; y < m.cs; y++) {
            int sy = my + y;
            if (sy < 0 || sy >= H) continue;
            uint16_t* row = (uint16_t*)(base + (size_t)sy * pitch);
            for (int x = 0; x < m.cs; x++) {
                int sx = mx + x;
                if (sx < 0 || sx >= W) continue;
                int i = y * m.cs + x, a = m.curA[i];
                if (!a) continue;
                uint16_t d = row[sx];
                int dr = (d >> 11) << 3, dg = ((d >> 5) & 63) << 2, db = (d & 31) << 3;
                int r = m.curRGB[i * 3] + dr * (255 - a) / 255;
                int g = m.curRGB[i * 3 + 1] + dg * (255 - a) / 255;
                int b = m.curRGB[i * 3 + 2] + db * (255 - a) / 255;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
                row[sx] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            }
        }
    }
}
