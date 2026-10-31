// Commandos2-ModernPatch - proxy ddraw.dll for Commandos 2: Men of Courage
// (Steam build of Comm2.exe, fixed base 0x400000)
//
// Decouples rendering from the 25 Hz game logic:
//   * the game loop still runs exactly one logic tick every 1000/25 ms,
//   * between ticks the scene is re-rendered as often as allowed (MaxFps),
//   * while rendering, cameras and moving objects are temporarily placed at
//     positions interpolated between the previous and the current tick, then
//     restored, so game logic never sees them.
// Also: smooth cursor/inventory dragging, menus, configurable HD resolution,
// full-screen pause menu, optional cnc-ddraw backend (no display mode switches).
//
// Config: Commandos2-ModernPatch.ini next to Comm2.exe. F10 toggles the mod,
// F11 object smoothing. See README.md and docs/TECHNICAL.md.

#define MOD_NAME "Commandos2-ModernPatch"   // ini/log/folder name in the game directory
#define MOD_VERSION "1.1.3"

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <xmmintrin.h>
#include <string>

#pragma comment(lib, "winmm.lib")

// ---------------------------------------------------------------------------
// DirectDraw forwarding
// ---------------------------------------------------------------------------
static HMODULE g_realDDraw = nullptr;
typedef HRESULT(WINAPI* PFN_DirectDrawCreate)(GUID*, void**, IUnknown*);
static PFN_DirectDrawCreate g_realDirectDrawCreate = nullptr;
static bool g_useCnc = false;    // set from config before the first load
static char g_modDir[MAX_PATH];  // <game>\Commandos2-ModernPatch

static void Log(const char* fmt, ...);

// Loads the DirectDraw implementation we forward to: cnc-ddraw from
// <game>\Commandos2-ModernPatch\cnc-ddraw (borderless, scaled, no display mode switches) when
// enabled and present, otherwise the system ddraw.dll.
static void LoadRealDDraw()
{
    if (g_realDDraw) return;
    char path[MAX_PATH];
    if (g_useCnc) {
        char ini[MAX_PATH];
        sprintf_s(path, "%s\\cnc-ddraw\\ddraw.dll", g_modDir);
        sprintf_s(ini, "%s\\cnc-ddraw\\ddraw.ini", g_modDir);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
            SetEnvironmentVariableA("CNC_DDRAW_CONFIG_FILE", ini);
            g_realDDraw = LoadLibraryA(path);
            Log("cnc-ddraw %s\n", g_realDDraw ? "loaded" : "FAILED to load, using system ddraw");
        } else {
            Log("cnc-ddraw not found at %s, using system ddraw\n", path);
        }
    }
    if (!g_realDDraw) {
        GetSystemDirectoryA(path, MAX_PATH);  // SysWOW64 via redirection for 32-bit
        strcat_s(path, "\\ddraw.dll");
        g_realDDraw = LoadLibraryA(path);
    }
    if (g_realDDraw)
        g_realDirectDrawCreate = (PFN_DirectDrawCreate)GetProcAddress(g_realDDraw, "DirectDrawCreate");
}

extern "C" HRESULT WINAPI Proxy_DirectDrawCreate(GUID* guid, void** out, IUnknown* outer)
{
    LoadRealDDraw();
    if (!g_realDirectDrawCreate) return E_FAIL;
    return g_realDirectDrawCreate(guid, out, outer);
}

// ---------------------------------------------------------------------------
// Logging / config
// ---------------------------------------------------------------------------
static FILE* g_log = nullptr;
static char g_dir[MAX_PATH];

static void Log(const char* fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

struct Config {
    bool enabled = true;
    bool camera = true;
    bool objects = true;
    bool mouse = true;
    int hdW = 1920, hdH = 1080;   // resolution used for the game's HD video mode
    int pauseScale = 1;           // 0 = centred 640x480, 1 = stretched full screen, 2 = fit height (4:3)
    bool hdMenus = true;          // pause menu & briefing in game resolution (centred)
    bool menus = true;            // extra frames in menus (smooth cursor)
    bool cnc = true;              // use bundled cnc-ddraw (borderless, no mode switches)
    int maxFps = 0;        // 0 = monitor refresh rate
    int debug = 0;          // 1 = stats, 2 = + automatic memory diagnostics
    int toggleKey = VK_F10;
    bool cheats = false;          // cheat keys active without typing the password
} g_cfg;

static void LoadConfig()
{
    char ini[MAX_PATH];
    sprintf_s(ini, "%s\\" MOD_NAME ".ini", g_dir);
    // GetPrivateProfile* does not understand a UTF-8 byte order mark (added by some
    // editors): the section header would not match and every setting would fall
    // back to its default. Strip it.
    FILE* f = nullptr;
    if (fopen_s(&f, ini, "rb") == 0 && f) {
        std::string data;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
        fclose(f);
        if (data.size() >= 3 && (uint8_t)data[0] == 0xEF && (uint8_t)data[1] == 0xBB && (uint8_t)data[2] == 0xBF) {
            FILE* w = nullptr;
            if (fopen_s(&w, ini, "wb") == 0 && w) { fwrite(data.data() + 3, 1, data.size() - 3, w); fclose(w); }
        }
    }
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringA(MOD_NAME,"Enabled", "1", ini);
        WritePrivateProfileStringA(MOD_NAME,"Camera", "1", ini);
        WritePrivateProfileStringA(MOD_NAME,"Objects", "1", ini);
        WritePrivateProfileStringA(MOD_NAME,"Mouse", "1", ini);
        WritePrivateProfileStringA(MOD_NAME,"HDResolution", "1920x1080", ini);
        WritePrivateProfileStringA(MOD_NAME,"CncDDraw", "1", ini);
        WritePrivateProfileStringA(MOD_NAME,"Menus", "1", ini);
        WritePrivateProfileStringA(MOD_NAME,"HDMenus", "1", ini);
        WritePrivateProfileStringA(MOD_NAME,"PauseMenuScale", "1", ini);
        WritePrivateProfileStringA(MOD_NAME,"MaxFps", "0", ini);
        WritePrivateProfileStringA(MOD_NAME,"Cheats", "0", ini);
        WritePrivateProfileStringA(MOD_NAME,"Debug", "0", ini);
    }
    g_cfg.enabled = GetPrivateProfileIntA(MOD_NAME,"Enabled", 1, ini) != 0;
    g_cfg.camera = GetPrivateProfileIntA(MOD_NAME,"Camera", 1, ini) != 0;
    g_cfg.objects = GetPrivateProfileIntA(MOD_NAME,"Objects", 1, ini) != 0;
    g_cfg.mouse = GetPrivateProfileIntA(MOD_NAME,"Mouse", 1, ini) != 0;
    char res[32];
    g_cfg.cnc = GetPrivateProfileIntA(MOD_NAME,"CncDDraw", 1, ini) != 0;
    g_cfg.menus = GetPrivateProfileIntA(MOD_NAME,"Menus", 1, ini) != 0;
    g_cfg.hdMenus = GetPrivateProfileIntA(MOD_NAME,"HDMenus", 1, ini) != 0;
    g_cfg.pauseScale = GetPrivateProfileIntA(MOD_NAME,"PauseMenuScale", 1, ini);
    GetPrivateProfileStringA(MOD_NAME,"HDResolution", "1920x1080", res, sizeof(res), ini);
    if (sscanf_s(res, "%dx%d", &g_cfg.hdW, &g_cfg.hdH) != 2 || g_cfg.hdW < 640 || g_cfg.hdH < 480 ||
        g_cfg.hdW > 7680 || g_cfg.hdH > 4320) { g_cfg.hdW = 1280; g_cfg.hdH = 720; }
    g_cfg.maxFps = GetPrivateProfileIntA(MOD_NAME,"MaxFps", 0, ini);
    g_cfg.debug = GetPrivateProfileIntA(MOD_NAME,"Debug", 0, ini);
    g_cfg.cheats = GetPrivateProfileIntA(MOD_NAME,"Cheats", 0, ini) != 0;
    if (g_cfg.maxFps <= 0) {
        HDC dc = GetDC(nullptr);
        int hz = GetDeviceCaps(dc, VREFRESH);
        ReleaseDC(nullptr, dc);
        g_cfg.maxFps = (hz > 1) ? hz : 60;
    }
    if (g_cfg.maxFps < 25) g_cfg.maxFps = 25;
    if (g_cfg.maxFps > 1000) g_cfg.maxFps = 1000;
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
static double g_qpcToMs = 0.0;
static double NowMs()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * g_qpcToMs;
}

static void WaitUntil(double targetMs)
{
    for (;;) {
        double rem = targetMs - NowMs();
        if (rem <= 0) return;
        if (rem > 2.0) Sleep((DWORD)(rem - 1.5));
        else YieldProcessor();
    }
}

// ---------------------------------------------------------------------------
// Game addresses (Comm2.exe, Steam build)
// ---------------------------------------------------------------------------
namespace addr {
    // WinMain loop call sites
    constexpr uintptr_t Site_Update    = 0x405239;  // call CJuego::Update       (0x7956f0)
    constexpr uintptr_t Site_Frame     = 0x40529b;  // call CJuego::Frame(canvas) (0x792e10)
    constexpr uintptr_t Site_EndIter   = 0x40535c;  // call CCanvas::Reset        (0x7cacd0)

    constexpr uintptr_t Update         = 0x7956f0;
    constexpr uintptr_t Frame          = 0x792e10;
    constexpr uintptr_t CanvasInit     = 0x7caf00;  // thiscall(canvas, surfInfo*)
    constexpr uintptr_t CanvasReset    = 0x7cacd0;  // thiscall(canvas)
    constexpr uintptr_t VideoLock      = 0x7cde80;  // thiscall(video, lockInfo*)
    constexpr uintptr_t VideoUnlock    = 0x7cdf00;  // thiscall(video, lockInfo*)
    constexpr uintptr_t VideoPresent   = 0x7ce430;  // thiscall(video, vblankWaits)

    // WinMain stack frame layout (offsets from WinMain's esp inside the loop)
    constexpr int WM_LockInfo  = 0x14;
    constexpr int WM_NoDraw    = 0x1b;
    constexpr int WM_Canvas    = 0x44;
    constexpr int WM_Video     = 0x5c;
    constexpr int WM_HaveVideo = 0x68;
    constexpr int WM_SurfInfo  = 0xe8;

    // Globals
    constexpr uintptr_t G_Juego        = 0xBCBAD4;  // CJuego*
    constexpr uintptr_t G_Mision       = 0xBD57EC;  // current mission*
    constexpr uintptr_t G_FrameAdapt   = 0xBB432C;  // byte: adaptive frame skipping active
    constexpr uintptr_t G_DrewFrame    = 0xBB432D;  // byte: this iteration was drawn
    constexpr uintptr_t G_SkipPresent  = 0xBD57E4;  // byte
    constexpr float     DefaultFps     = 25.0f;
    constexpr uintptr_t PollCursorPos  = 0x402e00;  // cdecl void(): cursor sprite <- system cursor

    // CJuego fields
    constexpr int J_State        = 0x2c;   // current screen index
    constexpr int J_Transition   = 0xec;   // 1/2 = transition in progress
    constexpr int J_FpsLimited   = 0xc4;   // byte: frame limiter active
    // Mission fields
    constexpr int M_TargetFps    = 0x5b0;  // int, 25
    constexpr int M_TickCount    = 0x5a4;

    // CVentanaVisorMundo (world viewer window)
    constexpr uintptr_t VT_VisorMundo = 0xAF37C4;
    constexpr int VT_VisorMundo_Draw  = 2;      // thiscall(canvas) ret 4
    constexpr int VM_Camera           = 0xdc;
    // Camera object
    constexpr int CAM_X    = 0x68;   // float, world origin x
    constexpr int CAM_Y    = 0x6c;   // float, world origin y
    constexpr int CAM_Zoom = 0x74;   // float, scale
}

template <class T> static inline T& At(uintptr_t base, int off) { return *(T*)(base + off); }

// thiscall function types emulated via fastcall (ecx=this, edx=dummy, rest on stack)
typedef void(__fastcall* PFN_Void0)(void* self, void* edx);
typedef int(__fastcall* PFN_Int1)(void* self, void* edx, void* arg);
typedef void(__fastcall* PFN_Void1)(void* self, void* edx, void* arg);

static PFN_Void0 Game_Update       = (PFN_Void0)addr::Update;
static PFN_Void1 Game_Frame        = (PFN_Void1)addr::Frame;
static PFN_Int1  Game_CanvasInit   = (PFN_Int1)addr::CanvasInit;
static PFN_Void0 Game_CanvasReset  = (PFN_Void0)addr::CanvasReset;
static PFN_Int1  Game_VideoLock    = (PFN_Int1)addr::VideoLock;
static PFN_Int1  Game_VideoUnlock  = (PFN_Int1)addr::VideoUnlock;
static PFN_Int1  Game_VideoPresent = (PFN_Int1)addr::VideoPresent;
static PFN_Void1 Game_VisorDraw    = nullptr;  // original vtable entry

// ---------------------------------------------------------------------------
// Patching helpers
// ---------------------------------------------------------------------------
static bool PatchCallRel32(uintptr_t site, uintptr_t expectedTarget, void* newTarget)
{
    uint8_t* p = (uint8_t*)site;
    if (p[0] != 0xE8) { Log("patch @%08X: not a call (%02X)\n", (unsigned)site, p[0]); return false; }
    uintptr_t cur = site + 5 + *(int32_t*)(p + 1);
    if (cur != expectedTarget) { Log("patch @%08X: unexpected target %08X\n", (unsigned)site, (unsigned)cur); return false; }
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int32_t*)(p + 1) = (int32_t)((uintptr_t)newTarget - (site + 5));
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

static void* PatchVtable(uintptr_t vtable, int index, void* newFn)
{
    void** slot = (void**)(vtable + index * 4);
    DWORD old;
    VirtualProtect(slot, 4, PAGE_READWRITE, &old);
    void* prev = *slot;
    *slot = newFn;
    VirtualProtect(slot, 4, old, &old);
    return prev;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool   g_active = true;       // runtime toggle (F10)
static bool   g_keyPrev = false;
static uint32_t g_tick = 0;          // incremented after each logic update while in game
static double g_tickTime = 0.0;      // scheduled time of the current tick
static double g_tickInterval = 40.0; // ms
static bool   g_inGame = false;
static bool   g_inMenu = false;      // menu-type screen with extra frames (cursor only)
static float  g_alpha = 1.0f;        // interpolation factor used by viewer draws
static bool   g_interpolating = false;
static uintptr_t g_lastMission = 0;

// stats
static double g_statStart = 0.0;
static int g_statFrames = 0, g_statTicks = 0;

struct CamState {
    uintptr_t cam;
    uint32_t tick;      // g_tick when 'cur' was captured
    float prev[3];
    float cur[3];
};
static CamState g_cams[16];
static int g_camCount = 0;

static void ResetObjs();
static void ResetCams() { g_camCount = 0; ResetObjs(); }

static CamState* GetCamState(uintptr_t cam)
{
    for (int i = 0; i < g_camCount; i++)
        if (g_cams[i].cam == cam) return &g_cams[i];
    if (g_camCount == _countof(g_cams)) g_camCount = 0;  // overflow: start over
    CamState* s = &g_cams[g_camCount++];
    s->cam = cam;
    s->tick = 0xFFFFFFFF;
    return s;
}

// Current screen object (CMA*): CJuego::GetScreen at 0x790f40 (thiscall, no args).
static uintptr_t CurrentScreen(uintptr_t juego)
{
    typedef uintptr_t(__fastcall* PFN_GetScreen)(void* self, void* edx);
    return ((PFN_GetScreen)0x790f40)((void*)juego, nullptr);
}

// Screens whose animations advance per draw call and are not handled yet:
// briefings (map fly-over camera).
static bool IsExcludedScreen(uintptr_t screen)
{
    if (!screen) return true;
    uintptr_t vt = *(uintptr_t*)screen;
    return vt == 0xB1333C /* CMABriefing */ || vt == 0xB133EC /* CMABriefingRed */;
}

// Menus, pause menu etc.: any stable screen other than mission/inventory.
static bool IsMenuScreen()
{
    uintptr_t juego = *(uintptr_t*)addr::G_Juego;
    if (!juego) return false;
    int state = At<int>(juego, addr::J_State);
    if (state <= 0 || state > 19 || state == 1 || state == 7) return false;
    int tr = At<int>(juego, addr::J_Transition);
    if (tr == 1 || tr == 2) return false;
    return !IsExcludedScreen(CurrentScreen(juego));
}

static bool IsInGame()
{
    uintptr_t juego = *(uintptr_t*)addr::G_Juego;
    uintptr_t mision = *(uintptr_t*)addr::G_Mision;
    if (!juego || !mision) return false;
    int state = At<int>(juego, addr::J_State);
    if (state != 1 && state != 7) return false;   // 1 = mission, 7 = inventory (CMAInventario)
    int tr = At<int>(juego, addr::J_Transition);
    if (tr == 1 || tr == 2) return false;
    return true;
}

static float TargetFps()
{
    uintptr_t mision = *(uintptr_t*)addr::G_Mision;
    if (mision) {
        int f = At<int>(mision, addr::M_TargetFps);
        if (f > 0 && f <= 1000) return (float)f;
    }
    return addr::DefaultFps;
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

// World viewer draw: render with an interpolated camera, then restore it.
static void __fastcall Hook_VisorDraw(void* self, void* edx, void* canvas)
{
    uintptr_t viewer = (uintptr_t)self;
    uintptr_t cam = At<uintptr_t>(viewer, addr::VM_Camera);
    if (!g_interpolating || !g_cfg.camera || !cam) {
        Game_VisorDraw(self, edx, canvas);
        return;
    }

    float* c = (float*)(cam + addr::CAM_X);
    float actual[3] = { At<float>(cam, addr::CAM_X), At<float>(cam, addr::CAM_Y), At<float>(cam, addr::CAM_Zoom) };

    CamState* s = GetCamState(cam);
    if (s->tick != g_tick) {
        if (s->tick == 0xFFFFFFFF || s->tick + 1 != g_tick) {
            memcpy(s->prev, actual, sizeof(actual));  // first sight / missed ticks: no interpolation
        } else {
            memcpy(s->prev, s->cur, sizeof(actual));
        }
        memcpy(s->cur, actual, sizeof(actual));
        s->tick = g_tick;
        // Large jumps (centering on a unit, switching views): snap.
        float dx = s->cur[0] - s->prev[0], dy = s->cur[1] - s->prev[1];
        float zoomRatio = s->cur[2] / (s->prev[2] != 0 ? s->prev[2] : 1.0f);
        if (dx * dx + dy * dy > 300.0f * 300.0f || zoomRatio > 1.5f || zoomRatio < 0.66f)
            memcpy(s->prev, s->cur, sizeof(actual));
    }

    float a = g_alpha;
    At<float>(cam, addr::CAM_X) = s->prev[0] + (s->cur[0] - s->prev[0]) * a;
    At<float>(cam, addr::CAM_Y) = s->prev[1] + (s->cur[1] - s->prev[1]) * a;
    At<float>(cam, addr::CAM_Zoom) = s->prev[2] + (s->cur[2] - s->prev[2]) * a;

    Game_VisorDraw(self, edx, canvas);

    // Restore unless the draw itself changed the camera (then keep the game's value).
    if (At<float>(cam, addr::CAM_X) == s->prev[0] + (s->cur[0] - s->prev[0]) * a) At<float>(cam, addr::CAM_X) = actual[0];
    if (At<float>(cam, addr::CAM_Y) == s->prev[1] + (s->cur[1] - s->prev[1]) * a) At<float>(cam, addr::CAM_Y) = actual[1];
    if (At<float>(cam, addr::CAM_Zoom) == s->prev[2] + (s->cur[2] - s->prev[2]) * a) At<float>(cam, addr::CAM_Zoom) = actual[2];
    (void)c;
}

static void DiagPoll(bool inGame);

// Deliver pending mouse-move messages to the game between ticks, so the cursor,
// dragged inventory items and selection boxes follow the mouse at full frame
// rate. Only moves that are at the head of the mouse queue are taken, so the
// order relative to clicks is preserved.
// ---------------------------------------------------------------------------
// Mouse-following UI. The game reads the mouse through DirectInput once per
// loop iteration; the arrow sprite ([Juego+0xD0], pos at +0xC/+0x10 in game
// pixels) is positioned from GetCursorPos by 0x402e00.
// ---------------------------------------------------------------------------
// Inventory drag: the inventory manager [[0xBD57F0]] owns the grid window
// (CVentanaCuadriculaInv) at +0x4C. While an item is dragged the grid holds it
// at +0xA0 and draws it at +0xA4/+0xA8 (= mouse - grab offset, set per tick by
// the grid's mouse-move handler at 0x93081b).
namespace addr {
    constexpr uintptr_t G_InventarioMgr = 0xBD57F0;
    constexpr int INV_Grid = 0x4c;
    constexpr uintptr_t VT_GridInv = 0xB360B4;       // CVentanaCuadriculaInv
    constexpr int GRID_DragItem = 0xa0;
    constexpr int GRID_DragX = 0xa4;
    constexpr int GRID_DragY = 0xa8;
}
static int g_tickMouseX = 0, g_tickMouseY = 0;

static bool CursorPos(int* x, int* y)
{
    uintptr_t j = *(uintptr_t*)addr::G_Juego;
    uintptr_t cur = j ? At<uintptr_t>(j, 0xd0) : 0;
    if (!cur) return false;
    *x = At<int>(cur, 0xc);
    *y = At<int>(cur, 0x10);
    return true;
}

// debug counters for the inventory drag path
static int g_dbgInvFrames = 0, g_dbgInvMgr = 0, g_dbgInvWin = 0, g_dbgInvMoved = 0;
static uintptr_t g_dbgMgr = 0, g_dbgWin = 0, g_dbgWinVt = 0;
static int g_dbgDX = 0, g_dbgDY = 0, g_dbgMX = 0, g_dbgMY = 0;

// Drag position fields shifted during extra frames (even index = x, odd = y):
// grid+0xA4/+0xA8, mgr+0x50/+0x54 (mouse - grab offset), mgr+0x58/+0x5C (drawn).
static int* DragField(uintptr_t grid, int k)
{
    if (k < 2) return (int*)(grid + addr::GRID_DragX + 4 * k);
    uintptr_t mgr = At<uintptr_t>(*(uintptr_t*)addr::G_InventarioMgr, 0);
    return (int*)(mgr + 0x50 + 4 * (k - 2));
}

// Returns the inventory grid window if an item is being dragged.
static uintptr_t InventoryDragGrid()
{
    uintptr_t j = *(uintptr_t*)addr::G_Juego;
    if (!j || At<int>(j, addr::J_State) != 7) return 0;
    g_dbgInvFrames++;
    uintptr_t w = *(uintptr_t*)addr::G_InventarioMgr;
    uintptr_t mgr = w ? At<uintptr_t>(w, 0) : 0;
    g_dbgMgr = mgr;
    if (!mgr) return 0;
    g_dbgInvMgr++;
    uintptr_t grid = At<uintptr_t>(mgr, addr::INV_Grid);
    g_dbgWin = grid;
    g_dbgWinVt = grid ? *(uintptr_t*)grid : 0;
    if (!grid || *(uintptr_t*)grid != addr::VT_GridInv) return 0;
    g_dbgDX = At<int>(grid, addr::GRID_DragX);
    g_dbgDY = At<int>(grid, addr::GRID_DragY);
    if (!At<uintptr_t>(grid, addr::GRID_DragItem)) return 0;
    g_dbgInvWin++;
    return grid;
}

static bool g_objKeyPrev = false;
static void PauseUpdate();
static void PauseComposite(void* canvas);

// After the logic update of every loop iteration.
static void __fastcall Hook_Update(void* self, void* edx)
{
    Game_Update(self, edx);
    PauseUpdate();

    bool key = (GetAsyncKeyState(g_cfg.toggleKey) & 0x8000) != 0;
    if (key && !g_keyPrev) {
        g_active = !g_active;
        Log("smoothing %s\n", g_active ? "ON" : "OFF");
        MessageBeep(g_active ? MB_OK : MB_ICONHAND);
    }
    g_keyPrev = key;

    bool okey = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (okey && !g_objKeyPrev) {
        g_cfg.objects = !g_cfg.objects;
        Log("object interpolation %s\n", g_cfg.objects ? "ON" : "OFF");
        MessageBeep(g_cfg.objects ? MB_OK : MB_ICONHAND);
    }
    g_objKeyPrev = okey;

    uintptr_t mision = *(uintptr_t*)addr::G_Mision;
    if (mision != g_lastMission) { ResetCams(); g_lastMission = mision; }

    bool inGame = g_active && IsInGame();
    if (inGame != g_inGame) {
        ResetCams(); g_inGame = inGame;
        uintptr_t j = *(uintptr_t*)addr::G_Juego;
        Log("in game: %d (state %d, transition %d)\n", (int)inGame,
            j ? At<int>(j, addr::J_State) : -1, j ? At<int>(j, addr::J_Transition) : -1);
    }
    if (g_cfg.debug >= 2) DiagPoll(inGame);

    bool inMenu = !inGame && g_active && g_cfg.menus && IsMenuScreen();
    if (inMenu != g_inMenu) {
        g_inMenu = inMenu;
        uintptr_t j = *(uintptr_t*)addr::G_Juego;
        Log("in menu: %d (state %d)\n", (int)inMenu, j ? At<int>(j, addr::J_State) : -1);
    }
    if (!inGame && !inMenu) return;

    // The game turns adaptive frame skipping on every update; it fights with our pacing.
    *(uint8_t*)addr::G_FrameAdapt = 0;

    g_tickInterval = 1000.0 / TargetFps();
    double now = NowMs();
    // Keep a fixed schedule; resync if we fell behind by more than one tick.
    double next = g_tickTime + g_tickInterval;
    g_tickTime = (now - next > g_tickInterval || next > now + g_tickInterval) ? now : next;
    g_tick++;
    g_statTicks++;
    CursorPos(&g_tickMouseX, &g_tickMouseY);   // where the cursor was when logic ran
}

static void DrawFrameExtra(uintptr_t juego, uintptr_t wm, float alpha);

// The game's frame (draw + built-in frame limiter).
static void NoteFrameSurface(void* canvas);
static void __fastcall Hook_Frame(void* self, void* edx, void* canvas)
{
    NoteFrameSurface(canvas);
    if (!g_inGame && !g_inMenu) { Game_Frame(self, edx, canvas); PauseComposite(canvas); return; }
    uintptr_t juego = (uintptr_t)self;
    uint8_t limited = At<uint8_t>(juego, addr::J_FpsLimited);
    At<uint8_t>(juego, addr::J_FpsLimited) = 0;   // we do the pacing
    g_alpha = 0.0f;
    g_interpolating = true;
    Game_Frame(self, edx, canvas);
    PauseComposite(canvas);
    g_interpolating = false;
    At<uint8_t>(juego, addr::J_FpsLimited) = limited;
    g_statFrames++;
}

// ---------------------------------------------------------------------------
// Diagnostics (Debug=1, F9 in game): find object fields modified by extra frames
// ---------------------------------------------------------------------------
#include "rtti_vtables.h"
#include <vector>
#include <map>
#include <algorithm>

static volatile uint8_t g_inExtraFrame = 0;

namespace diag {
    // ---- hardware watchpoints -------------------------------------------
    static DWORD mainTid = 0;
    static uintptr_t wpAddr[4];
    static int wpCount = 0;
    static volatile LONG wpHits[4];
    static int wpRW[4];   // 0 = write, 1 = read/write

    static bool IsCallReturn(uint32_t v)
    {
        if (v < 0x401006 || v >= 0xAE8000) return false;
        const uint8_t* p = (const uint8_t*)v;
        if (p[-5] == 0xE8) return true;                       // call rel32
        if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) return true;  // call [mem]
        if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0) return true;  // call reg
        if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) return true;  // call [reg+d8]
        return false;
    }

    static void LogStack(uint32_t esp, char* out, size_t outSize)
    {
        int found = 0;
        size_t len = strlen(out);
        __try {
            for (int i = 0; i < 400 && found < 10; i++) {
                uint32_t v = ((uint32_t*)esp)[i];
                if (IsCallReturn(v)) {
                    len += sprintf_s(out + len, outSize - len, " %08X", v);
                    found++;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static LONG CALLBACK Veh(EXCEPTION_POINTERS* e)
    {
        if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
        CONTEXT* c = e->ContextRecord;
        DWORD dr6 = (DWORD)c->Dr6;
        if (!(dr6 & 0xF)) return EXCEPTION_CONTINUE_SEARCH;
        for (int i = 0; i < 4; i++) {
            if (!(dr6 & (1u << i))) continue;
            LONG n = InterlockedIncrement(&wpHits[i]);
            if (n <= 25) {
                char buf[512];
                sprintf_s(buf, "WP%d eip=%08X extra=%d val=%08X stack:", i, (unsigned)c->Eip, (int)g_inExtraFrame,
                          *(uint32_t*)wpAddr[i]);
                LogStack((uint32_t)c->Esp, buf, sizeof(buf));
                Log("%s\n", buf);
            }
            if (n >= 25) c->Dr7 &= ~(3u << (i * 2));
        }
        c->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    static DWORD WINAPI ArmThread(LPVOID)
    {
        HANDLE h = OpenThread(THREAD_ALL_ACCESS, FALSE, mainTid);
        if (!h) return 1;
        SuspendThread(h);
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        GetThreadContext(h, &c);
        c.Dr0 = wpAddr[0]; c.Dr1 = wpAddr[1]; c.Dr2 = wpAddr[2]; c.Dr3 = wpAddr[3];
        DWORD dr7 = 0;
        for (int i = 0; i < wpCount; i++)
            dr7 |= (1u << (i * 2)) | ((wpRW[i] ? 3u : 1u) << (16 + i * 4)) | (3u << (18 + i * 4));  // enable, W or RW, 4 bytes
        c.Dr7 = dr7;
        c.Dr6 = 0;
        SetThreadContext(h, &c);
        ResumeThread(h);
        CloseHandle(h);
        return 0;
    }

    static void ArmWatchpoints(const uintptr_t* a, int n)
    {
        static bool vehAdded = false;
        if (!vehAdded) { AddVectoredExceptionHandler(1, Veh); vehAdded = true; }
        for (int i = 0; i < 4; i++) { wpAddr[i] = i < n ? (a[i] & ~3u) : 0; wpHits[i] = 0; }
        wpCount = n;
        mainTid = GetCurrentThreadId();
        HANDLE t = CreateThread(nullptr, 0, ArmThread, nullptr, 0, nullptr);
        if (t) { WaitForSingleObject(t, 2000); CloseHandle(t); }
    }

    constexpr uint32_t ObjSize = 0x400;
    struct Obj { uintptr_t p; int cls; std::vector<uint8_t> snap; };
    static std::vector<Obj> objs;
    static std::vector<uint8_t> dataSnap;
    static std::map<std::pair<int, uint32_t>, int> hits;   // (class, offset) -> count
    static std::map<uint32_t, int> dataHits;               // .data offset -> count
    static int framesLeft = 0, framesDone = 0;
    static bool keyPrev = false;

    static int FindClass(uint32_t vt)
    {
        int lo = 0, hi = _countof(g_vtNames) - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (g_vtNames[mid].vt == vt) return mid;
            if (g_vtNames[mid].vt < vt) lo = mid + 1; else hi = mid - 1;
        }
        return -1;
    }

    static bool SafeCopy(void* dst, uintptr_t src, uint32_t n)
    {
        __try { memcpy(dst, (void*)src, n); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static __declspec(noinline) void AddObj(uintptr_t q, int c) { objs.push_back({ q, c, std::vector<uint8_t>() }); }

    static void ScanRegion(uintptr_t base, uintptr_t end)
    {
        __try {
            for (uintptr_t q = base; q + ObjSize <= end; q += 4) {
                uint32_t v = *(uint32_t*)q;
                if (v < 0xAE8000 || v > 0xB91000) continue;
                int c = FindClass(v);
                if (c >= 0) AddObj(q, c);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void Start()
    {
        Log("diag: F9 pressed, scanning heap\n");
        objs.clear(); hits.clear(); dataHits.clear();
        uintptr_t p = 0x10000;
        MEMORY_BASIC_INFORMATION mbi;
        while (p < 0x7FFF0000 && VirtualQuery((void*)p, &mbi, sizeof(mbi))) {
            uintptr_t base = (uintptr_t)mbi.BaseAddress, end = base + mbi.RegionSize;
            uintptr_t stackProbe = (uintptr_t)&mbi;   // skip this thread's stack
            bool isStack = stackProbe >= base - 0x100000 && stackProbe < end + 0x100000;
            if (!isStack && mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect & PAGE_READWRITE) &&
                !(mbi.Protect & PAGE_GUARD))
                ScanRegion(base, end);
            p = end;
        }
        // Limit each object's window to the start of the next object (owner attribution).
        std::sort(objs.begin(), objs.end(), [](const Obj& a, const Obj& b) { return a.p < b.p; });
        for (size_t i = 0; i < objs.size(); i++) {
            uint32_t sz = ObjSize;
            if (i + 1 < objs.size() && objs[i + 1].p - objs[i].p < sz) sz = (uint32_t)(objs[i + 1].p - objs[i].p);
            objs[i].snap.resize(sz);
        }
        // Explicit non-polymorphic objects of interest (class index -1/-2/-3/-4).
        uintptr_t j = *(uintptr_t*)addr::G_Juego;
        if (j) {
            objs.push_back({ j, -1, std::vector<uint8_t>(0x200) });
            uintptr_t cur = At<uintptr_t>(j, 0xd0);
            if (cur) {
                objs.push_back({ cur, -2, std::vector<uint8_t>(0x40) });
                uintptr_t img = At<uintptr_t>(cur, 8);
                if (img) objs.push_back({ img, -3, std::vector<uint8_t>(0x40) });
            }
        }
        uintptr_t m = *(uintptr_t*)addr::G_Mision;
        if (m) objs.push_back({ m, -4, std::vector<uint8_t>(0x700) });
        dataSnap.resize(0x87410);
        framesLeft = 60; framesDone = 0;
        Log("diag: %u objects found, capturing\n", (unsigned)objs.size());
    }

    static std::map<std::pair<int, uint32_t>, uintptr_t> example;

    static const char* ClassName(int c)
    {
        switch (c) {
        case -1: return "[Juego]";
        case -2: return "[Juego+D0 cursor]";
        case -3: return "[cursor image]";
        case -4: return "[Mision]";
        default: return g_vtNames[c].name;
        }
    }

    static void Before()
    {
        for (auto& o : objs) SafeCopy(o.snap.data(), o.p, (uint32_t)o.snap.size());
        SafeCopy(dataSnap.data(), 0xB91000, (uint32_t)dataSnap.size());
    }

    static void After()
    {
        static uint8_t buf[0x800];
        static std::vector<uint8_t> dnow(0x87410);
        for (auto& o : objs) {
            uint32_t sz = (uint32_t)o.snap.size();
            if (!SafeCopy(buf, o.p, sz)) continue;
            for (uint32_t i = (o.cls >= 0 ? 4 : 0); i + 4 <= sz; i += 4)
                if (*(uint32_t*)(buf + i) != *(uint32_t*)(o.snap.data() + i)) {
                    hits[{o.cls, i}]++;
                    example[{o.cls, i}] = o.p;
                }
        }
        if (SafeCopy(dnow.data(), 0xB91000, (uint32_t)dnow.size()))
            for (uint32_t i = 0; i < dnow.size(); i += 4)
                if (*(uint32_t*)(dnow.data() + i) != *(uint32_t*)(dataSnap.data() + i)) dataHits[i]++;
        framesDone++;
        if (--framesLeft == 0) {
            Log("diag: results over %d extra frames (class +offset : frames changed)\n", framesDone);
            std::vector<std::pair<int, std::pair<int, uint32_t>>> v;
            for (auto& h : hits) v.push_back({ h.second, h.first });
            std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
            for (size_t i = 0; i < v.size() && i < 300; i++)
                if (v[i].second.first < 0 || strncmp(g_vtNames[v[i].second.first].name, ".?AVCMsb", 8) != 0)
                Log("  %-50s +%03X : %d  (obj %08X)\n", ClassName(v[i].second.first), v[i].second.second, v[i].first,
                    (unsigned)example[v[i].second]);
            int n = 0;
            for (auto& h : dataHits)
                if (h.second >= (g_inMenu ? 2 : 20) && n++ < 100) Log("  .data %08X : %d\n", 0xB91000 + h.first, h.second);

            // Arm hardware write-watchpoints on the most suspicious polymorphic fields.
            int slot = 0;
            uintptr_t addrs[4] = {};
            for (size_t i = 0; i < v.size() && slot < 4; i++) {
                int cls = v[i].second.first;
                if (cls < 0 || v[i].first < (g_inMenu ? 2 : 20)) continue;   // menus: also rarely toggling state
                const char* nm = g_vtNames[cls].name;
                // Skip known per-draw bookkeeping (config tree, render caches, draw ids).
                if (strncmp(nm, ".?AVCMsb", 8) == 0 || strstr(nm, "CManual") || strstr(nm, "Bitmap") ||
                    strstr(nm, "ListaMascaras") || strstr(nm, "MascaraGrafica") || strstr(nm, "AnimCache"))
                    continue;
                addrs[slot] = example[v[i].second] + v[i].second.second;
                Log("diag: watchpoint %d on %s +%03X (%08X)\n", slot, nm, v[i].second.second, (unsigned)addrs[slot]);
                slot++;
            }
            // Globals that change in extra frames (excluding known frame statistics).
            static const uint32_t knownGlobals[] = { 0xBD57C8, 0xBD57DC, 0xBD57E0, 0xBEDFF8, 0xBEE004, 0xC08B8C };
            for (auto& h : dataHits) {
                if (slot >= 4) break;
                uint32_t a = 0xB91000 + h.first;
                if (h.second < 2 || std::find(std::begin(knownGlobals), std::end(knownGlobals), a) != std::end(knownGlobals))
                    continue;
                addrs[slot] = a;
                Log("diag: watchpoint %d on global %08X (%d frames)\n", slot, a, h.second);
                slot++;
            }
            if (slot) ArmWatchpoints(addrs, slot);
            objs.clear();
            MessageBeep(MB_ICONASTERISK);
        }
    }

    // ---- inventory: find fields that follow the mouse (dragged item) -------
    struct Track { int lastDx, lastDy, consX, consY, changes; };
    static std::map<uint64_t, Track> track;   // key: objIndex<<20 | off<<1 | isFloat
    static int invTicks = 0, recTicks = 0;
    static bool invDone = false;

    static void InvRecordTick()
    {
        uintptr_t j = *(uintptr_t*)addr::G_Juego;
        uintptr_t cur = j ? At<uintptr_t>(j, 0xd0) : 0;
        if (!cur) return;
        int mx = At<int>(cur, 0xc), my = At<int>(cur, 0x10);
        static uint8_t buf[0x2000];
        for (size_t oi = 0; oi < objs.size(); oi++) {
            Obj& o = objs[oi];
            if (o.cls == -1 || o.cls == -2 || o.cls == -3) continue;
            uint32_t sz = (uint32_t)o.snap.size();
            if (!SafeCopy(buf, o.p, sz)) continue;
            for (uint32_t i = 0; i + 4 <= sz; i += 4) {
                uint32_t nv = *(uint32_t*)(buf + i);
                if (nv == *(uint32_t*)(o.snap.data() + i)) continue;
                for (int isF = 0; isF < 2; isF++) {
                    int v;
                    if (isF) {
                        float f = *(float*)&nv;
                        if (!(f > -5000.f && f < 5000.f) || f == 0.f) continue;
                        v = (int)f;
                    } else {
                        v = (int)nv;
                        if (v < -5000 || v > 5000) continue;
                    }
                    Track& t = track[((uint64_t)oi << 20) | (i << 1) | isF];
                    int dx = v - mx, dy = v - my;
                    if (t.changes > 0) {
                        if (abs(dx - t.lastDx) <= 1) t.consX++;
                        if (abs(dy - t.lastDy) <= 1) t.consY++;
                    }
                    t.lastDx = dx; t.lastDy = dy; t.changes++;
                }
            }
            memcpy(o.snap.data(), buf, sz);
        }
    }

    static void InvFinish()
    {
        std::vector<std::pair<int, uint64_t>> v;
        for (auto& t : track) {
            int c = (std::max)(t.second.consX, t.second.consY);
            if (t.second.changes >= 8 && c >= t.second.changes * 3 / 4) v.push_back({ c, t.first });
        }
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
        Log("diag: inventory mouse-following fields (%u candidates)\n", (unsigned)v.size());
        uintptr_t wps[4] = {};
        int nwp = 0;
        for (size_t k = 0; k < v.size() && k < 40; k++) {
            uint64_t key = v[k].second;
            size_t oi = (size_t)(key >> 20);
            uint32_t off = (uint32_t)((key >> 1) & 0x7FFFF);
            int isF = (int)(key & 1);
            Track& t = track[key];
            const char* nm = objs[oi].cls >= 0 ? g_vtNames[objs[oi].cls].name : "[Mision/other]";
            Log("  %-45s obj %08X +%03X %s changes=%d consX=%d consY=%d dx=%d dy=%d\n", nm, (unsigned)objs[oi].p, off,
                isF ? "float" : "int", t.changes, t.consX, t.consY, t.lastDx, t.lastDy);
            if (nwp < 4 && t.consX >= t.consY) {   // watch X coordinates (write, then read/write)
                wps[nwp] = objs[oi].p + off; wpRW[nwp] = 0; nwp++;
                wps[nwp] = objs[oi].p + off; wpRW[nwp] = 1; nwp++;
            }
        }
        if (nwp) {
            for (int i = 0; i < nwp; i++) Log("diag: watchpoint %d %s on %08X\n", i, wpRW[i] ? "RW" : "W", (unsigned)wps[i]);
            ArmWatchpoints(wps, nwp);
        }
        track.clear();
        objs.clear();
        MessageBeep(MB_ICONASTERISK);
    }

    static void Poll(bool inGame)
    {
        uintptr_t j = *(uintptr_t*)addr::G_Juego;
        int state = j ? At<int>(j, addr::J_State) : -1;
        invTicks = (inGame && state == 7) ? invTicks + 1 : 0;
        if (invTicks == 25 * 3 && !invDone && recTicks == 0) {
            MessageBeep(MB_OK);
            Start();
            framesLeft = 0;   // no extra-frame capture, tick recording instead
            uintptr_t inv = *(uintptr_t*)0xBD57F0;
            if (inv && At<uintptr_t>(inv, 0)) objs.push_back({ At<uintptr_t>(inv, 0), -5, std::vector<uint8_t>(0x1000) });
            for (auto& o : objs) SafeCopy(o.snap.data(), o.p, (uint32_t)o.snap.size());
            recTicks = 25 * 15;
            Log("diag: inventory recording started\n");
        } else if (recTicks > 0) {
            if (state != 7) { recTicks = 0; InvFinish(); invDone = true; return; }
            InvRecordTick();
            if (--recTicks == 0) { InvFinish(); invDone = true; }
        }
        // Menus: capture once per screen after it has been shown for 6 seconds.
        static uintptr_t lastScreen = 0, lastVt = 0;
        static int screenTicks = 0;
        static std::vector<uintptr_t> capturedVt;
        if (g_inMenu && j) {
            uintptr_t scr = CurrentScreen(j);
            uintptr_t vt = scr ? *(uintptr_t*)scr : 0;
            if (scr != lastScreen || vt != lastVt) { lastScreen = scr; lastVt = vt; screenTicks = 0; }
            if (++screenTicks % (25 * 10) == 25 * 6 && framesLeft == 0) {
                int c = FindClass((uint32_t)vt);
                Log("diag: menu screen %s, capturing\n", c >= 0 ? g_vtNames[c].name : "?");
                MessageBeep(MB_OK);
                Start();
            }
        } else {
            screenTicks = 0;
        }
        bool k = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (k && !keyPrev && framesLeft == 0 && inGame) { MessageBeep(MB_OK); Start(); }
        keyPrev = k;
    }
}

static void DiagPoll(bool inGame) { diag::Poll(inGame); }

// ---------------------------------------------------------------------------
// 3D face sequences (CVisorSecuencias3D::Draw at 0x92a5f0) advance one keyframe
// per draw call. In extra frames, skip the advance part and only draw the
// current keyframe (jump straight to the drawing tail at 0x92a673).
// ---------------------------------------------------------------------------

namespace addr {
    constexpr uintptr_t SeqDraw          = 0x92a5f0;
    constexpr uintptr_t SeqDraw_Continue = 0x92a5fd;  // after the prologue + 'je exit'
    constexpr uintptr_t SeqDraw_DrawOnly = 0x92a673;  // expects esi=this, [esp]=edi, [esp+4]=esi
    constexpr uintptr_t SeqDraw_Exit     = 0x92a6cb;  // pop esi; ret 4
}

__declspec(naked) static void Stub_SeqDraw()
{
    __asm {
        push esi
        mov esi, ecx
        cmp byte ptr [esi + 0x44], 0
        je exit_
        cmp byte ptr [g_inExtraFrame], 0
        jne extra_
        mov eax, 0x0092A5FD
        jmp eax
    extra_:
        push edi
        mov eax, 0x0092A673
        jmp eax
    exit_:
        mov eax, 0x0092A6CB
        jmp eax
    }
}

// ---------------------------------------------------------------------------
// Animated arrow cursor: the mission draw (0x44bcf0) ticks a 15-frame sprite
// animation through [mission+0x1c]->0x437d30 on every draw call. Skip that tick
// in extra frames so the cursor animates at the original rate.
// ---------------------------------------------------------------------------
namespace addr {
    constexpr uintptr_t Site_CursorAnimTick = 0x44bd10;
    constexpr uintptr_t CursorAnimTick      = 0x437d30;
}
static PFN_Void0 Game_CursorAnimTick = (PFN_Void0)addr::CursorAnimTick;

static void __fastcall Hook_CursorAnimTick(void* self, void* edx)
{
    if (!g_inExtraFrame) Game_CursorAnimTick(self, edx);
}

// ---------------------------------------------------------------------------
// Moving objects: every renderable object (container of sprites/models) is
// projected to screen space by 0x7f7240 (thiscall, 4 args), which reads the
// object's world position at this+0x14 (x, y, z floats). During our draws the
// position is replaced by one interpolated between the last two ticks.
// ---------------------------------------------------------------------------
#include <unordered_map>

namespace addr {
    constexpr uintptr_t ObjProject = 0x7f7240;
    constexpr uintptr_t Site_ObjProject[] = { 0x7e6960, 0x7e8d80, 0x80382f };
    constexpr int OBJ_Pos = 0x14;
}

typedef void(__fastcall* PFN_Project)(void* self, void* edx, uint32_t a1, uint32_t a2, void* a3, uint32_t a4);
static PFN_Project Game_ObjProject = (PFN_Project)addr::ObjProject;

struct ObjHist {
    uint32_t tick;
    float prev[3];
    float cur[3];
    bool shared;       // drawn at several positions within one tick: never interpolate
    int8_t polygon;    // -1 unknown, 0 no, 1 contains polygon visuals (vision cones)
};
static std::unordered_map<uintptr_t, ObjHist> g_objs;
static float g_snapDist = 150.0f;
static int g_statShared = 0, g_statPolygon = 0;

static void ResetObjs() { g_objs.clear(); }

namespace addr {
    constexpr uintptr_t VT_VisualPolygon = 0xAF7024;  // CVisualGraficoPolygon
    constexpr int OBJ_Visuals = 0x8c;                 // CVisual** array
    constexpr int OBJ_VisualCount = 0x90;
}

static std::map<uintptr_t, int> g_movingTrace;   // debug: interpolated objects that moved

static void DescribeVisuals(uintptr_t obj, char* out, size_t n)
{
    out[0] = 0;
    __try {
        uintptr_t arr = At<uintptr_t>(obj, addr::OBJ_Visuals);
        uint32_t cnt = At<uint32_t>(obj, addr::OBJ_VisualCount);
        size_t len = 0;
        for (uint32_t i = 0; arr && i < cnt && i < 6; i++) {
            uintptr_t v = ((uintptr_t*)arr)[i];
            int c = v ? diag::FindClass(*(uint32_t*)v) : -1;
            const char* nm = c >= 0 ? g_vtNames[c].name + 4 : (v ? "?" : "null");
            len += sprintf_s(out + len, n - len, "%s ", nm);
            if (len > n - 64) break;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void DumpMovingTrace()
{
    int k = 0;
    for (auto& m : g_movingTrace) {
        if (k++ >= 40) break;
        char vis[400];
        DescribeVisuals(m.first, vis, sizeof(vis));
        auto it = g_objs.find(m.first);
        float* c = it != g_objs.end() ? it->second.cur : nullptr;
        Log("  moving obj %08X draws=%d pos=(%.1f %.1f %.1f) step=(%.2f %.2f %.2f) visuals: %s\n", (unsigned)m.first, m.second,
            c ? c[0] : 0, c ? c[1] : 0, c ? c[2] : 0,
            c ? c[0] - it->second.prev[0] : 0, c ? c[1] - it->second.prev[1] : 0, c ? c[2] - it->second.prev[2] : 0, vis);
    }
    g_movingTrace.clear();
}

static bool HasPolygonVisual(uintptr_t obj)
{
    __try {
        uintptr_t arr = At<uintptr_t>(obj, addr::OBJ_Visuals);
        uint32_t n = At<uint32_t>(obj, addr::OBJ_VisualCount);
        if (!arr || n > 64) return false;
        for (uint32_t i = 0; i < n; i++) {
            uintptr_t v = ((uintptr_t*)arr)[i];
            if (v && *(uintptr_t*)v == addr::VT_VisualPolygon) return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

static void __fastcall Hook_ObjProject(void* self, void* edx, uint32_t a1, uint32_t a2, void* a3, uint32_t a4)
{
    if (!g_interpolating || !g_cfg.objects) { Game_ObjProject(self, edx, a1, a2, a3, a4); return; }

    float* pos = (float*)((uintptr_t)self + addr::OBJ_Pos);
    float actual[3] = { pos[0], pos[1], pos[2] };

    if (g_objs.size() > 50000) g_objs.clear();
    auto ins = g_objs.try_emplace((uintptr_t)self);
    ObjHist& h = ins.first->second;
    if (ins.second) { h.tick = 0; h.shared = false; h.polygon = -1; }

    // Containers get their visuals assigned after creation and are reused, so
    // re-check for polygons (vision cones) once per tick.
    if (h.polygon < 0 || h.tick != g_tick) {
        int8_t p = HasPolygonVisual((uintptr_t)self) ? 1 : 0;
        if (p && h.polygon != 1) g_statPolygon++;
        h.polygon = p;
    }
    if (h.polygon == 1 || h.shared) {
        h.tick = g_tick;
        memcpy(h.prev, actual, sizeof(actual));
        memcpy(h.cur, actual, sizeof(actual));
        Game_ObjProject(self, edx, a1, a2, a3, a4);
        return;
    }

    if (h.tick != g_tick) {
        bool contiguous = (h.tick != 0 && h.tick + 1 == g_tick);
        memcpy(h.prev, contiguous ? h.cur : actual, sizeof(actual));
        memcpy(h.cur, actual, sizeof(actual));
        h.tick = g_tick;
        float dx = h.cur[0] - h.prev[0], dy = h.cur[1] - h.prev[1], dz = h.cur[2] - h.prev[2];
        if (dx * dx + dy * dy + dz * dz > g_snapDist * g_snapDist) memcpy(h.prev, h.cur, sizeof(actual));
    } else if (memcmp(h.cur, actual, sizeof(actual)) != 0) {
        // Positioned differently again within the same tick: the game reuses this
        // object to draw several things. Interpolating it would mix them up.
        h.shared = true;
        g_statShared++;
        Game_ObjProject(self, edx, a1, a2, a3, a4);
        return;
    }

    float a = g_alpha;
    float lerp[3];
    for (int i = 0; i < 3; i++) lerp[i] = h.prev[i] + (h.cur[i] - h.prev[i]) * a;
    pos[0] = lerp[0]; pos[1] = lerp[1]; pos[2] = lerp[2];

    if (g_cfg.debug && memcmp(h.prev, h.cur, sizeof(h.cur)) != 0) g_movingTrace[(uintptr_t)self]++;

    Game_ObjProject(self, edx, a1, a2, a3, a4);

    for (int i = 0; i < 3; i++) if (pos[i] == lerp[i]) pos[i] = actual[i];
}

// ---------------------------------------------------------------------------
// Text input caret (save game name, "keyed mission", ...): the text field draw
// at 0x87f090 does 'inc [this+0x1c]' on every call and toggles the caret
// ([this+0x20]) every 4 calls. In extra frames the counter is pre-decremented
// so the game's increment leaves it unchanged and the caret blinks at the
// original rate.
// ---------------------------------------------------------------------------
namespace addr {
    constexpr uintptr_t TextFieldDraw = 0x87f090;   // thiscall(field, canvas) ret 4
    constexpr uintptr_t Sites_TextFieldDraw[] = { 0x79c057, 0x7b7ecf, 0x7ba3de, 0x7bb1f8, 0x87f19a };
}
static void __fastcall Hook_TextFieldDraw(void* self, void* edx, void* canvas)
{
    if (g_inExtraFrame) At<int>((uintptr_t)self, 0x1c) -= 1;
    ((PFN_Void1)addr::TextFieldDraw)(self, edx, canvas);
}

static bool PatchJmp(uintptr_t at, const uint8_t* expect, size_t n, void* target)
{
    if (memcmp((void*)at, expect, n) != 0) { Log("patch @%08X: unexpected bytes\n", (unsigned)at); return false; }
    DWORD old;
    VirtualProtect((void*)at, 5, PAGE_EXECUTE_READWRITE, &old);
    *(uint8_t*)at = 0xE9;
    *(int32_t*)(at + 1) = (int32_t)((uintptr_t)target - (at + 5));
    VirtualProtect((void*)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, 5);
    return true;
}

// One extra rendered frame between logic ticks, replicating WinMain's sequence.
static void DrawFrameExtra(uintptr_t juego, uintptr_t wm, float alpha)
{
    bool diagNow = g_cfg.debug >= 2 && diag::framesLeft > 0;
    if (diagNow) diag::Before();
    void* canvas = (void*)(wm + addr::WM_Canvas);
    void* video = (void*)(wm + addr::WM_Video);
    void* lockInfo = (void*)(wm + addr::WM_LockInfo);

    At<uint32_t>(wm, addr::WM_Canvas) = 0;
    if (Game_CanvasInit(canvas, nullptr, (void*)(wm + addr::WM_SurfInfo)) != 0) return;
    Game_VideoLock(video, nullptr, lockInfo);
    NoteFrameSurface(canvas);

    uint8_t limited = At<uint8_t>(juego, addr::J_FpsLimited);
    At<uint8_t>(juego, addr::J_FpsLimited) = 0;
    g_alpha = alpha;
    g_interpolating = true;
    g_inExtraFrame = 1;
    Game_Frame((void*)juego, nullptr, canvas);
    PauseComposite(canvas);
    g_inExtraFrame = 0;
    g_interpolating = false;
    At<uint8_t>(juego, addr::J_FpsLimited) = limited;

    Game_VideoUnlock(video, nullptr, lockInfo);
    if (*(uint8_t*)addr::G_SkipPresent == 0 && *(uint8_t*)addr::G_DrewFrame != 0)
        Game_VideoPresent(video, nullptr, (void*)0);
    Game_CanvasReset(canvas, nullptr);
    g_statFrames++;
    if (diagNow) diag::After();
}

// End of a WinMain loop iteration: render extra frames until the next tick is due.
static void __fastcall Hook_EndIter(void* canvas, void* edx)
{
    Game_CanvasReset(canvas, edx);
    if (!(g_inGame && IsInGame()) && !(g_inMenu && IsMenuScreen())) return;

    uintptr_t wm = (uintptr_t)canvas - addr::WM_Canvas;
    uintptr_t juego = *(uintptr_t*)addr::G_Juego;
    double deadline = g_tickTime + g_tickInterval;
    double frameMs = 1000.0 / g_cfg.maxFps;
    bool canDraw = At<uint32_t>(wm, addr::WM_HaveVideo) != 0 && At<uint8_t>(wm, addr::WM_NoDraw) == 0;

    double lastFrame = NowMs();
    for (;;) {
        double now = NowMs();
        double nextFrame = lastFrame + frameMs;
        if (!canDraw || nextFrame + frameMs * 0.5 > deadline) break;
        WaitUntil(nextFrame);
        now = NowMs();
        lastFrame = now;
        float alpha = (float)((now - g_tickTime) / g_tickInterval);
        if (alpha < 0.f) alpha = 0.f;
        if (alpha > 1.f) alpha = 1.f;
        uintptr_t grid = 0;
        int dragSaved[6];
        if (g_cfg.mouse) {
            // The arrow's screen position is normally polled once per tick
            // (CJuego::Update -> 0x402e00: GetCursorPos -> cursor sprite). Poll it
            // for every frame; it only moves the cursor sprite, not logic state.
            ((void(*)())addr::PollCursorPos)();

            // Inventory: draw the dragged item where the cursor is now. The item
            // itself is drawn by the manager's draw (0x8a00b0) at mgr+0x58/+0x5C,
            // its footprint outline by the grid at grid+0xA4/+0xA8.
            grid = InventoryDragGrid();
            if (grid) {
                int mx, my;
                bool ok = CursorPos(&mx, &my);
                g_dbgMX = mx; g_dbgMY = my;
                if (ok && (mx != g_tickMouseX || my != g_tickMouseY)) {
                    int dx = mx - g_tickMouseX, dy = my - g_tickMouseY;
                    for (int k = 0; k < 6; k++) {
                        int* f = DragField(grid, k);
                        dragSaved[k] = *f;
                        *f += (k & 1) ? dy : dx;
                    }
                    g_dbgInvMoved++;
                } else
                    grid = 0;
            }
        }
        DrawFrameExtra(juego, wm, alpha);
        if (grid)
            for (int k = 0; k < 6; k++) *DragField(grid, k) = dragSaved[k];
    }
    WaitUntil(deadline);

    if (g_cfg.debug) {
        double now = NowMs();
        if (g_statStart == 0) g_statStart = now;
        if (now - g_statStart >= 5000.0) {
            double s = (now - g_statStart) / 1000.0;
            Log("ticks/s %.2f  frames/s %.1f  objects tracked %u, polygon %d, shared %d\n", g_statTicks / s,
                g_statFrames / s, (unsigned)g_objs.size(), g_statPolygon, g_statShared);
            DumpMovingTrace();
            if (g_dbgInvFrames) {
                int ws = 0, wy = 0;
                if (g_dbgWin) { __try { ws = At<int>(g_dbgWin, 0xa4); wy = At<int>(g_dbgWin, 0xa8); } __except (1) {} }
                Log("  inventory: frames %d, mgr %d (%08X), dragwin %d (%08X vt %08X), moved %d; drag pos %d,%d  "
                    "mouse now %d,%d tick %d,%d  win screen %d,%d\n",
                    g_dbgInvFrames, g_dbgInvMgr, (unsigned)g_dbgMgr, g_dbgInvWin, (unsigned)g_dbgWin, (unsigned)g_dbgWinVt,
                    g_dbgInvMoved, g_dbgDX, g_dbgDY, g_dbgMX, g_dbgMY, g_tickMouseX, g_tickMouseY, ws, wy);
                g_dbgInvFrames = g_dbgInvMgr = g_dbgInvWin = g_dbgInvMoved = 0;
            }
            g_statStart = now; g_statTicks = g_statFrames = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Resolution: video mode 3 ("1280x720 (16:9)", added by the Steam release) is
// replaced by HDResolution from the ini. Mode index lives at options+0x54;
// width/height come from switch tables in 0x413f20..0x4140b0 and 0x7b38c0.
// ---------------------------------------------------------------------------
static void PatchImm32(uintptr_t at, uint8_t opcode, uint32_t expect, uint32_t value)
{
    uint8_t* p = (uint8_t*)at;
    if (p[0] != opcode || *(uint32_t*)(p + 1) != expect) {
        Log("resolution patch @%08X: unexpected bytes\n", (unsigned)at);
        return;
    }
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    *(uint32_t*)(p + 1) = value;
    VirtualProtect(p, 5, old, &old);
}

// Pause menu (CMAMenuInGame) and briefing (CMABriefing) switch the display to
// the fixed menu size 640x480 on activation (vtbl[7]). Their draw code already
// renders the world at the current size and centres the 640x480 menu/windows
// when the screen is larger, so keep the game resolution instead.
// ---------------------------------------------------------------------------
// Full-screen pause menu. In HD the pause menu (CMAMenuInGame, 640x480 layout)
// is made to lay itself out in the top-left 640x480 corner (as on a 640x480
// screen). After the world is drawn the corner is saved; after the whole frame
// is drawn (menu, message boxes, arrow) the pixels that differ from the saved
// world are the menu layer. The world is restored and the menu layer is drawn
// scaled over the whole screen. GetCursorPos is mapped back to menu space.
// ---------------------------------------------------------------------------
namespace addr {
    constexpr uintptr_t VT_MenuInGame       = 0xB176AC;
    constexpr uintptr_t Site_PauseWorld     = 0x7aaed8;  // call MisionDraw (0x44bcf0) in CMAMenuInGame::Draw
    constexpr uintptr_t MisionDraw          = 0x44bcf0;
    constexpr uintptr_t Site_PauseScreenSz  = 0x7aaee8;  // call GetScreenSize (0x8ed510) in CMAMenuInGame::Draw
    constexpr uintptr_t Site_MenuLayoutSz   = 0x7ae026;  // call GetScreenSize in menu layout (0x7ae010)
    constexpr uintptr_t Site_PauseLayout    = 0x7ab077;  // call 0x7ae010 from CMAMenuInGame
    constexpr uintptr_t MenuLayout          = 0x7ae010;
    constexpr uintptr_t GetScreenSize       = 0x8ed510;  // thiscall(video, int out[2+])
    constexpr uintptr_t SurfLock            = 0x81d390;  // thiscall(surface, info**) -> 0 ok
    constexpr uintptr_t SurfUnlock          = 0x81d3c0;  // thiscall(surface, info**)
    constexpr uintptr_t IAT_GetCursorPos    = 0xC19DF8;
    // call [IAT] sites that feed menu/game logic. 0x402e41 (arrow sprite poll) is
    // deliberately left alone: the arrow must stay at the real mouse position,
    // also when the game draws it immediately on mouse moves (0x81e830).
    constexpr uintptr_t Sites_GetCursorPos[] = { 0x8e1567, 0x8ed9f5 };
    constexpr uintptr_t Thunk_GetCursorPos  = 0x4067dc;                             // jmp [IAT]
}

static const uint16_t kPauseKey = 0xF81F; // magenta: "no menu pixel here"
static bool g_pauseScaleActive = false;   // pause menu shown in HD with scaling
static bool g_forceMenuSize = false;      // menu layout should assume a 640x480 screen
static bool g_pauseCaptured = false;      // this frame's world corner was saved
static uintptr_t g_pauseSurface = 0;
static int g_pauseW = 0, g_pauseH = 0;
static std::vector<uint16_t> g_pauseUnder, g_pauseLayer, g_pauseBg;
static float g_pauseSX = 1, g_pauseSY = 1;
static int g_pauseDX = 0, g_pauseDY = 0, g_pauseDW = 0, g_pauseDH = 0;

typedef int(__fastcall* PFN_SurfLock)(void* surf, void* edx, void* out);
typedef void(__fastcall* PFN_GetScreenSize)(void* self, void* edx, int* out);

static uint16_t* LockSurface(uintptr_t surf, int* pitchPx)
{
    void* info = nullptr;
    if (((PFN_SurfLock)addr::SurfLock)((void*)surf, nullptr, &info) != 0 || !info) return nullptr;
    *pitchPx = ((int*)info)[1] / 2;
    return *(uint16_t**)info;
}
static void UnlockSurface(uintptr_t surf)
{
    void* info = (void*)(surf + 4);
    ((PFN_SurfLock)addr::SurfUnlock)((void*)surf, nullptr, &info);
}

// Mouse messages to the game window carry client coordinates in lParam; map
// them to menu space too while the scaled pause menu is shown.
static WNDPROC g_origWndProc = nullptr;
static HWND g_gameWnd = nullptr;

static LRESULT CALLBACK PauseWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_pauseScaleActive && g_pauseDW > 0 && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL) {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        x = (int)((x - g_pauseDX) / g_pauseSX);
        y = (int)((y - g_pauseDY) / g_pauseSY);
        x = x < 0 ? 0 : (x > 639 ? 639 : x);
        y = y < 0 ? 0 : (y > 479 ? 479 : y);
        lp = MAKELPARAM(x, y);
    }
    return CallWindowProcA(g_origWndProc, h, msg, wp, lp);
}

static BOOL CALLBACK FindGameWnd(HWND h, LPARAM)
{
    if (IsWindowVisible(h) && GetParent(h) == nullptr) { g_gameWnd = h; return FALSE; }
    return TRUE;
}

static void PauseUpdate()
{
    if (!g_origWndProc && g_cfg.hdMenus && g_cfg.pauseScale) {
        EnumThreadWindows(GetCurrentThreadId(), FindGameWnd, 0);
        if (g_gameWnd) {
            g_origWndProc = (WNDPROC)SetWindowLongA(g_gameWnd, GWL_WNDPROC, (LONG)(LONG_PTR)PauseWndProc);
            Log("game window %08X subclassed\n", (unsigned)(uintptr_t)g_gameWnd);
        }
    }
    bool active = false;
    uintptr_t j = *(uintptr_t*)addr::G_Juego;
    if (g_cfg.hdMenus && g_cfg.pauseScale && j) {
        uintptr_t scr = CurrentScreen(j);
        active = scr && *(uintptr_t*)scr == addr::VT_MenuInGame;
    }
    if (active != g_pauseScaleActive) Log("scaled pause menu: %d\n", (int)active);
    g_pauseScaleActive = active;
}

// Screen-size queries made by the pause menu: pretend the screen is 640x480.
static void __fastcall Hook_PauseScreenSize(void* self, void* edx, int* out)
{
    ((PFN_GetScreenSize)addr::GetScreenSize)(self, edx, out);
    if (g_cfg.pauseScale && (g_pauseScaleActive || g_forceMenuSize)) { out[0] = 640; out[1] = 480; }
}
static void __fastcall Hook_MenuLayoutScreenSize(void* self, void* edx, int* out)
{
    ((PFN_GetScreenSize)addr::GetScreenSize)(self, edx, out);
    if (g_cfg.pauseScale && g_forceMenuSize) { out[0] = 640; out[1] = 480; }
}
static void __fastcall Hook_PauseLayout(void* self, void* edx)
{
    g_forceMenuSize = true;
    ((PFN_Void0)addr::MenuLayout)(self, edx);
    g_forceMenuSize = false;
}

// World drawn under the pause menu: save the top-left 640x480 corner.
static void __fastcall Hook_PauseWorld(void* self, void* edx, void* canvas)
{
    ((PFN_Void1)addr::MisionDraw)(self, edx, canvas);
    g_pauseCaptured = false;
    if (!g_pauseScaleActive || !canvas) return;
    uintptr_t surf = *(uintptr_t*)canvas;
    if (!surf) return;
    int w = At<int>(surf, 0x10), h = At<int>(surf, 0x14), pitch;
    if (w < 640 || h < 480 || (w == 640 && h == 480)) return;
    uint16_t* px = LockSurface(surf, &pitch);
    if (!px) return;
    g_pauseUnder.resize(640 * 480);
    for (int y = 0; y < 480; y++) {
        memcpy(&g_pauseUnder[y * 640], px + y * pitch, 640 * 2);
    }
    g_pauseBg = g_pauseUnder;   // background the menu windows are drawn on (+ backdrop, see below)
    UnlockSurface(surf);
    g_pauseSurface = surf; g_pauseW = w; g_pauseH = h;
    if (g_cfg.pauseScale == 2) {           // fit: keep 4:3
        g_pauseSY = g_pauseSX = h / 480.0f;
        g_pauseDW = (int)(640 * g_pauseSX + 0.5f); g_pauseDH = h;
        g_pauseDX = (w - g_pauseDW) / 2; g_pauseDY = 0;
    } else {                               // stretch to the whole screen
        g_pauseSX = w / 640.0f; g_pauseSY = h / 480.0f;
        g_pauseDX = g_pauseDY = 0; g_pauseDW = w; g_pauseDH = h;
    }
    g_pauseCaptured = true;
}

static inline void Unpack565(uint16_t c, int& r, int& g, int& b) { r = c >> 11; g = (c >> 5) & 63; b = c & 31; }

// Debug: write a 16-bit buffer as a 24-bit BMP (565 or 555 interpretation).
static void DumpBmp(const char* name, const uint16_t* src, int w, int h, int pitchPx, bool is555)
{
    char path[MAX_PATH];
    sprintf_s(path, "%s\\%s", g_modDir, name);
    FILE* f = nullptr;
    fopen_s(&f, path, "wb");
    if (!f) return;
    int rowBytes = (w * 3 + 3) & ~3;
    uint32_t size = 54 + rowBytes * h;
    uint8_t hdr[54] = { 'B', 'M' };
    *(uint32_t*)(hdr + 2) = size; *(uint32_t*)(hdr + 10) = 54; *(uint32_t*)(hdr + 14) = 40;
    *(int32_t*)(hdr + 18) = w; *(int32_t*)(hdr + 22) = -h; *(uint16_t*)(hdr + 26) = 1; *(uint16_t*)(hdr + 28) = 24;
    fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row(rowBytes);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t c = src[y * pitchPx + x];
            int r, g, b;
            if (is555) { r = (c >> 10) & 31; g = (c >> 5) & 31; b = c & 31; r <<= 3; g <<= 3; b <<= 3; }
            else { r = (c >> 11) << 3; g = ((c >> 5) & 63) << 2; b = (c & 31) << 3; }
            row[x * 3] = (uint8_t)b; row[x * 3 + 1] = (uint8_t)g; row[x * 3 + 2] = (uint8_t)r;
        }
        fwrite(row.data(), 1, rowBytes, f);
    }
    fclose(f);
}

// ---- translucent backdrop of the pause menu ---------------------------------
// The backdrop bitmap is blended by the game (mode 3). We render it once onto
// black and once onto white to recover per-pixel premultiplied colour and
// alpha, then keep a version pre-scaled to the destination rectangle.
struct Backdrop {
    uintptr_t bmp = 0;
    int dw = 0, dh = 0;
    std::vector<uint16_t> color;   // premultiplied colour, 565, scaled to dw x dh
    std::vector<uint8_t> inv;      // (1 - alpha) * 32, scaled to dw x dh
};
static Backdrop g_backdrop;
static bool g_backdropThisFrame = false;

typedef void(__fastcall* PFN_Blit)(void* canvas, void* edx, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
    uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10);
namespace addr {
    constexpr uintptr_t Site_PauseBackdrop = 0x7aaf61;  // call Blit (0x7d3fe0) in CMAMenuInGame::Draw
    constexpr uintptr_t Blit = 0x7d3fe0;
}

static void FillCorner(uintptr_t surf, uint16_t c)
{
    int pitch;
    uint16_t* px = LockSurface(surf, &pitch);
    if (!px) return;
    for (int y = 0; y < 480; y++) for (int x = 0; x < 640; x++) px[y * pitch + x] = c;
    UnlockSurface(surf);
}
static void ReadCorner(uintptr_t surf, std::vector<uint16_t>& out)
{
    int pitch;
    out.resize(640 * 480);
    uint16_t* px = LockSurface(surf, &pitch);
    if (!px) return;
    for (int y = 0; y < 480; y++) memcpy(&out[y * 640], px + y * pitch, 640 * 2);
    UnlockSurface(surf);
}

static void BuildBackdrop(void* canvas, uintptr_t bmp, const uint32_t* args)
{
    uintptr_t surf = *(uintptr_t*)canvas;
    std::vector<uint16_t> r0, r1;
    FillCorner(surf, 0x0000);
    ((PFN_Blit)addr::Blit)(canvas, nullptr, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
    ReadCorner(surf, r0);
    FillCorner(surf, 0xFFFF);
    ((PFN_Blit)addr::Blit)(canvas, nullptr, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
    ReadCorner(surf, r1);
    FillCorner(surf, kPauseKey);

    // Source alpha (0..1) and premultiplied colour (0..1 per channel).
    std::vector<float> A(640 * 480), CR(640 * 480), CG(640 * 480), CB(640 * 480);
    for (int i = 0; i < 640 * 480; i++) {
        int ar, ag, ab, br, bg, bb;
        Unpack565(r0[i], ar, ag, ab);
        Unpack565(r1[i], br, bg, bb);
        float inv = ((br - ar) / 31.0f + (bg - ag) / 63.0f + (bb - ab) / 31.0f) / 3.0f;   // = 1 - alpha
        if (inv < 0) inv = 0; if (inv > 1) inv = 1;
        A[i] = 1 - inv;
        CR[i] = ar / 31.0f; CG[i] = ag / 63.0f; CB[i] = ab / 31.0f;
    }
    // Pre-scale to the destination rectangle (bilinear).
    int dw = g_pauseDW, dh = g_pauseDH;
    g_backdrop.color.resize((size_t)dw * dh);
    g_backdrop.inv.resize((size_t)dw * dh);
    for (int dy = 0; dy < dh; dy++) {
        float fy = (dy + 0.5f) / g_pauseSY - 0.5f; if (fy < 0) fy = 0;
        int y0 = (int)fy; if (y0 > 478) y0 = 478;
        float ty = fy - y0; if (ty > 1) ty = 1;
        for (int dx = 0; dx < dw; dx++) {
            float fx = (dx + 0.5f) / g_pauseSX - 0.5f; if (fx < 0) fx = 0;
            int x0 = (int)fx; if (x0 > 638) x0 = 638;
            float tx = fx - x0; if (tx > 1) tx = 1;
            int i = y0 * 640 + x0;
            float w00 = (1 - tx) * (1 - ty), w01 = tx * (1 - ty), w10 = (1 - tx) * ty, w11 = tx * ty;
            auto lerp = [&](const std::vector<float>& v) { return v[i] * w00 + v[i + 1] * w01 + v[i + 640] * w10 + v[i + 641] * w11; };
            float a = lerp(A), r = lerp(CR), g = lerp(CG), b = lerp(CB);
            int R = (int)(r * 31 + 0.5f), G = (int)(g * 63 + 0.5f), B = (int)(b * 31 + 0.5f);
            g_backdrop.color[(size_t)dy * dw + dx] = (uint16_t)((R << 11) | (G << 5) | B);
            g_backdrop.inv[(size_t)dy * dw + dx] = (uint8_t)((1 - a) * 32 + 0.5f);
        }
    }
    g_backdrop.bmp = bmp; g_backdrop.dw = dw; g_backdrop.dh = dh;
    Log("pause backdrop %08X captured (%dx%d)\n", (unsigned)bmp, dw, dh);
}

// CMAMenuInGame's backdrop blit: in scaled mode it is composited by us instead.
static void __fastcall Hook_PauseBackdrop(void* canvas, void* edx, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
    uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10)
{
    if (!g_pauseScaleActive || !g_pauseCaptured || a1 != 0 || a2 != 0) {
        ((PFN_Blit)addr::Blit)(canvas, edx, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
        return;
    }
    uint32_t args[10] = { a1, a2, a3, a4, a5, a6, a7, a8, a9, a10 };
    uintptr_t surf = *(uintptr_t*)canvas;
    if (g_backdrop.bmp != a3 || g_backdrop.dw != g_pauseDW || g_backdrop.dh != g_pauseDH) {
        BuildBackdrop(canvas, a3, args);
        int pitch;   // put the world back into the corner
        uint16_t* px = LockSurface(surf, &pitch);
        if (px) {
            for (int y = 0; y < 480; y++) memcpy(px + y * pitch, &g_pauseUnder[y * 640], 640 * 2);
            UnlockSurface(surf);
        }
    }
    // Draw the backdrop normally (so menu text is anti-aliased against it) and
    // remember the result: menu windows are what differs from it afterwards.
    ((PFN_Blit)addr::Blit)(canvas, edx, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    ReadCorner(surf, g_pauseBg);
    g_backdropThisFrame = true;
}

// out = premultiplied backdrop + world * (1 - alpha), on packed 565 pixels.
static inline uint16_t BlendBackdrop(uint16_t world, uint16_t color, uint32_t inv32)
{
    uint32_t w = world;
    w = (w | (w << 16)) & 0x07E0F81F;
    w = ((w * inv32) >> 5) & 0x07E0F81F;
    w = (w | (w >> 16)) & 0xFFFF;
    uint32_t r = (w & 0xF800) + (color & 0xF800); if (r > 0xF800) r = 0xF800;
    uint32_t g = (w & 0x07E0) + (color & 0x07E0); if (g > 0x07E0) g = 0x07E0;
    uint32_t b = (w & 0x001F) + (color & 0x001F); if (b > 0x001F) b = 0x001F;
    return (uint16_t)(r | g | b);
}

// The arrow is not drawn into the menu layer (its soft edges would mix with
// the key colour); it is drawn after compositing at the real mouse position.
namespace addr {
    constexpr uintptr_t DrawCursor = 0x81e990;        // thiscall(cursor, canvas) ret 4
    constexpr uintptr_t Site_DrawCursorFrame = 0x793199;  // call in CJuego::Frame
    constexpr uintptr_t Site_DrawCursorJmp = 0x791138;    // jmp in CJuego::DrawCursor
}
static void DrawRealCursor(void* canvas);
static void __fastcall Hook_DrawCursor(void* cur, void* edx, void* canvas)
{
    if (g_pauseScaleActive) {
        // Never draw the arrow at the menu-space position; with compositing it is
        // drawn by PauseComposite, otherwise draw it at the real position now.
        if (!g_pauseCaptured) DrawRealCursor(canvas);
        return;
    }
    ((PFN_Void1)addr::DrawCursor)(cur, edx, canvas);
}

static void DrawRealCursor(void* canvas)
{
    uintptr_t j = *(uintptr_t*)addr::G_Juego;
    uintptr_t cur = j ? At<uintptr_t>(j, 0xd0) : 0;
    if (!cur || !canvas) return;
    POINT p;
    if (!(*(BOOL(WINAPI**)(POINT*))addr::IAT_GetCursorPos)(&p)) return;
    if (g_gameWnd) ScreenToClient(g_gameWnd, &p);
    int sx = At<int>(cur, 0xc), sy = At<int>(cur, 0x10);
    At<int>(cur, 0xc) = p.x; At<int>(cur, 0x10) = p.y;
    ((PFN_Void1)addr::DrawCursor)((void*)cur, nullptr, canvas);
    At<int>(cur, 0xc) = sx; At<int>(cur, 0x10) = sy;
}

// After the whole frame is drawn: extract the menu layer and draw it scaled.
static void PauseComposite(void* canvas)
{
    if (!g_pauseCaptured) return;
    g_pauseCaptured = false;
    bool backdrop = g_backdropThisFrame;
    g_backdropThisFrame = false;
    int pitch;
    uint16_t* px = LockSurface(g_pauseSurface, &pitch);
    if (!px) return;

    static int dumpCountdown = 50;   // ~ a moment after the pause menu appeared
    bool dumpNow = g_cfg.debug && dumpCountdown > 0 && --dumpCountdown == 0;
    if (dumpNow) {
        DumpBmp("pause_world.bmp", g_pauseUnder.data(), 640, 480, 640, false);
        DumpBmp("pause_menu.bmp", px, 640, 480, pitch, false);
    }
    g_pauseLayer.resize(640 * 480);
    static std::vector<uint8_t> mask;
    mask.resize(640 * 480);
    for (int y = 0; y < 480; y++) {
        uint16_t* row = px + y * pitch;
        for (int x = 0; x < 640; x++) {
            uint16_t c = row[x];
            g_pauseLayer[y * 640 + x] = c;
            mask[y * 640 + x] = c != g_pauseBg[y * 640 + x];
        }
        memcpy(row, &g_pauseUnder[y * 640], 640 * 2);   // clean world back
    }
    // Translucent backdrop over the sharp world.
    if (backdrop && g_backdrop.dw == g_pauseDW && g_backdrop.dh == g_pauseDH) {
        for (int dy = 0; dy < g_pauseDH; dy++) {
            uint16_t* out = px + (g_pauseDY + dy) * pitch + g_pauseDX;
            const uint16_t* col = &g_backdrop.color[(size_t)dy * g_pauseDW];
            const uint8_t* inv = &g_backdrop.inv[(size_t)dy * g_pauseDW];
            for (int dx = 0; dx < g_pauseDW; dx++) out[dx] = BlendBackdrop(out[dx], col[dx], inv[dx]);
        }
    }
    // Scaled draw with bilinear filtering inside the menu, nearest at its edges.
    for (int dy = 0; dy < g_pauseDH; dy++) {
        float fy = (dy + 0.5f) / g_pauseSY - 0.5f;
        if (fy < 0) fy = 0;
        int y0 = (int)fy; if (y0 > 478) y0 = 478;
        float ty = fy - y0; if (ty > 1) ty = 1;
        int yn = (int)((dy + 0.5f) / g_pauseSY); if (yn > 479) yn = 479;
        uint16_t* out = px + (g_pauseDY + dy) * pitch + g_pauseDX;
        for (int dx = 0; dx < g_pauseDW; dx++) {
            float fx = (dx + 0.5f) / g_pauseSX - 0.5f;
            if (fx < 0) fx = 0;
            int x0 = (int)fx; if (x0 > 638) x0 = 638;
            float tx = fx - x0; if (tx > 1) tx = 1;
            int xn = (int)((dx + 0.5f) / g_pauseSX); if (xn > 639) xn = 639;
            int i00 = y0 * 640 + x0;
            if (!mask[yn * 640 + xn]) continue;
            if (mask[i00] && mask[i00 + 1] && mask[i00 + 640] && mask[i00 + 641]) {
                int r[4], g[4], b[4];
                Unpack565(g_pauseLayer[i00], r[0], g[0], b[0]);
                Unpack565(g_pauseLayer[i00 + 1], r[1], g[1], b[1]);
                Unpack565(g_pauseLayer[i00 + 640], r[2], g[2], b[2]);
                Unpack565(g_pauseLayer[i00 + 641], r[3], g[3], b[3]);
                float w00 = (1 - tx) * (1 - ty), w01 = tx * (1 - ty), w10 = (1 - tx) * ty, w11 = tx * ty;
                int R = (int)(r[0] * w00 + r[1] * w01 + r[2] * w10 + r[3] * w11 + 0.5f);
                int G = (int)(g[0] * w00 + g[1] * w01 + g[2] * w10 + g[3] * w11 + 0.5f);
                int B = (int)(b[0] * w00 + b[1] * w01 + b[2] * w10 + b[3] * w11 + 0.5f);
                out[dx] = (uint16_t)((R << 11) | (G << 5) | B);
            } else {
                out[dx] = g_pauseLayer[yn * 640 + xn];
            }
        }
    }
    UnlockSurface(g_pauseSurface);
    DrawRealCursor(canvas);
    static int finalDumps = 0, finalCountdown = 120;
    if (g_cfg.debug && finalDumps < 4 && --finalCountdown <= 0) {
        finalCountdown = 120;
        char name[32];
        sprintf_s(name, "pause_final_%d.bmp", ++finalDumps);
        px = LockSurface(g_pauseSurface, &pitch);
        if (px) { DumpBmp(name, px, g_pauseW, g_pauseH, pitch, false); UnlockSurface(g_pauseSurface); }
    }
}

// All of the game's cursor reads go through here: in the scaled pause menu,
// map the screen position back into the 640x480 menu space.
static BOOL WINAPI Hook_GetCursorPos(POINT* p)
{
    BOOL r = (*(BOOL(WINAPI**)(POINT*))addr::IAT_GetCursorPos)(p);
    if (r && p && g_pauseScaleActive && g_pauseDW > 0) {
        int x = (int)((p->x - g_pauseDX) / g_pauseSX);
        int y = (int)((p->y - g_pauseDY) / g_pauseSY);
        p->x = x < 0 ? 0 : (x > 639 ? 639 : x);
        p->y = y < 0 ? 0 : (y > 479 ? 479 : y);
    }
    return r;
}

static void PatchIndirectToDirect(uintptr_t at, uint8_t modrm, uint8_t newOp, void* target)
{
    uint8_t* p = (uint8_t*)at;
    if (p[0] != 0xFF || p[1] != modrm || *(uint32_t*)(p + 2) != addr::IAT_GetCursorPos) {
        Log("cursor patch @%08X: unexpected bytes\n", (unsigned)at);
        return;
    }
    DWORD old;
    VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old);
    p[0] = newOp;
    *(int32_t*)(p + 1) = (int32_t)((uintptr_t)target - (at + 5));
    p[5] = 0x90;
    VirtualProtect(p, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 6);
}

static void PatchScaledPause()
{
    bool ok = PatchCallRel32(addr::Site_PauseWorld, addr::MisionDraw, (void*)&Hook_PauseWorld);
    ok &= PatchCallRel32(addr::Site_PauseScreenSz, addr::GetScreenSize, (void*)&Hook_PauseScreenSize);
    ok &= PatchCallRel32(addr::Site_MenuLayoutSz, addr::GetScreenSize, (void*)&Hook_MenuLayoutScreenSize);
    ok &= PatchCallRel32(addr::Site_PauseLayout, addr::MenuLayout, (void*)&Hook_PauseLayout);
    ok &= PatchCallRel32(addr::Site_PauseBackdrop, addr::Blit, (void*)&Hook_PauseBackdrop);
    ok &= PatchCallRel32(addr::Site_DrawCursorFrame, addr::DrawCursor, (void*)&Hook_DrawCursor);
    {   // jmp rel32 at 0x791138 -> Hook_DrawCursor
        uint8_t* p = (uint8_t*)addr::Site_DrawCursorJmp;
        if (p[0] == 0xE9 && (uintptr_t)(addr::Site_DrawCursorJmp + 5 + *(int32_t*)(p + 1)) == addr::DrawCursor) {
            DWORD old; VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
            *(int32_t*)(p + 1) = (int32_t)((uintptr_t)&Hook_DrawCursor - (addr::Site_DrawCursorJmp + 5));
            VirtualProtect(p, 5, old, &old);
        } else { ok = false; Log("cursor jmp patch: unexpected bytes\n"); }
    }
    for (uintptr_t s : addr::Sites_GetCursorPos) PatchIndirectToDirect(s, 0x15, 0xE8, (void*)&Hook_GetCursorPos);
    PatchIndirectToDirect(addr::Thunk_GetCursorPos, 0x25, 0xE9, (void*)&Hook_GetCursorPos);
    Log("scaled pause menu hooks: %s\n", ok ? "ok" : "FAILED");
}

// ---------------------------------------------------------------------------
// Game bug at high resolutions: the scaled map-background blitters 0x856880 and
// 0x856ea0 split the source area into 64x64 tiles and store one entry per tile
// column / row in fixed stack arrays (24 columns at esp+0x64, 22 rows at
// esp+0x188). A large screen (e.g. 1920x1080) combined with zoom needs more
// tiles than that, the arrays overflow and the game crashes. Large blits are
// split into rectangles that each fit.
//
// Signature (thiscall, ret 0x1c; the function pushes ecx early, which shifts
// its own [esp+...] argument references by 4):
//   bool Blit(uint16_t* dst, int pitchPx, float srcX, float srcY, int w, int h, uint32_t extra)
// this+0x30 = source pixels per screen pixel, this+0x20 = source pixels per
// world unit (srcX/srcY are world units). 'extra' is passed through unchanged.
// ---------------------------------------------------------------------------
namespace addr {
    constexpr uintptr_t TileBlit       = 0x856880;
    constexpr uintptr_t TileBlitBlend  = 0x856ea0;
    constexpr uintptr_t Site_TileBlit      = 0x857851;
    constexpr uintptr_t Site_TileBlitBlend = 0x85788f;
    constexpr uintptr_t TileCacheColCap    = 0x8587ce;
}
typedef uint32_t(__fastcall* PFN_TileBlit)(void* self, void* edx, uint16_t* dst, int pitch,
    float srcX, float srcY, int w, int h, uint32_t extra);

static uint32_t SplitTileBlit(PFN_TileBlit orig, void* self, uint16_t* dst, int pitch,
    float srcX, float srcY, int w, int h, uint32_t extra)
{
    const float step = *(float*)((uintptr_t)self + 0x30);   // source px per screen px
    const float pxPerUnit = *(float*)((uintptr_t)self + 0x20);
    const float maxSrc = 64.0f * 16;                         // keep each call well within the arrays
    int chunk = (step > 0) ? (int)(maxSrc / step) : w;
    if (chunk < 16) chunk = 16;
    // Anything that does not look like a sane screen rectangle goes to the game unchanged.
    if ((w <= chunk && h <= chunk) || pxPerUnit <= 0 || w <= 0 || h <= 0 || w > 8192 || h > 8192)
        return orig(self, nullptr, dst, pitch, srcX, srcY, w, h, extra);

    const float unitsPerScreenPx = step / pxPerUnit;
    uint32_t result = 0;
    for (int y = 0; y < h; y += chunk) {
        int ch = (h - y < chunk) ? h - y : chunk;
        for (int x = 0; x < w; x += chunk) {
            int cw = (w - x < chunk) ? w - x : chunk;
            result |= orig(self, nullptr, dst + (size_t)y * pitch + x, pitch,
                           srcX + x * unitsPerScreenPx, srcY + y * unitsPerScreenPx, cw, ch, extra) & 0xFF;
        }
    }
    return result;
}

// ---- crash forensics: ring buffer of recent blits + access-violation logger ----
struct BlitRec {
    uint32_t fn, self, dst, pitch, flags; float srcX, srcY; int w, h;
    float step, ppu, f2c; int f48, f4c, f50, f54, f64, f68, f6c;
    uint32_t surfBase; int surfPitch, surfW, surfH; uint32_t tick;
};
static BlitRec g_blitRing[64];
static volatile LONG g_blitRingPos = 0;
static uint32_t g_surfBase = 0;
static int g_surfPitch = 0, g_surfW = 0, g_surfH = 0;

static void RecordBlit(uint32_t fn, void* self, uint16_t* dst, int pitch, float srcX, float srcY, int w, int h, uint32_t flags)
{
    BlitRec& r = g_blitRing[InterlockedIncrement(&g_blitRingPos) & 63];
    uintptr_t s = (uintptr_t)self;
    r = { fn, (uint32_t)s, (uint32_t)(uintptr_t)dst, (uint32_t)pitch, (uint32_t)flags, srcX, srcY, w, h,
          At<float>(s, 0x30), At<float>(s, 0x20), At<float>(s, 0x2c),
          At<int>(s, 0x48), At<int>(s, 0x4c), At<int>(s, 0x50), At<int>(s, 0x54),
          At<int>(s, 0x64), At<int>(s, 0x68), At<int>(s, 0x6c),
          g_surfBase, g_surfPitch, g_surfW, g_surfH, g_tick };
}

static LONG CALLBACK CrashVeh(EXCEPTION_POINTERS* e)
{
    static LONG done = 0;
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    uint32_t eip = (uint32_t)e->ContextRecord->Eip;
    if (eip < 0x401000 || eip >= 0xAE8000) return EXCEPTION_CONTINUE_SEARCH;   // only the game's own code
    if (InterlockedIncrement(&done) > 3) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = e->ContextRecord;
    Log("\n*** CRASH: access violation at %08X (%s %08X) ***\n", eip,
        e->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
        (unsigned)e->ExceptionRecord->ExceptionInformation[1]);
    Log("eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X\n",
        (unsigned)c->Eax, (unsigned)c->Ebx, (unsigned)c->Ecx, (unsigned)c->Edx, (unsigned)c->Esi,
        (unsigned)c->Edi, (unsigned)c->Ebp, (unsigned)c->Esp);
    LONG pos = g_blitRingPos;
    for (int i = 15; i >= 0; i--) {
        const BlitRec& r = g_blitRing[(pos - i) & 63];
        if (!r.fn) continue;
        long long off = (long long)r.dst - r.surfBase;
        int row = r.surfPitch ? (int)(off / 2 / r.surfPitch) : 0, col = r.surfPitch ? (int)(off / 2 % r.surfPitch) : 0;
        Log("  blit[-%d] fn=%06X tick=%u self=%08X dst=%08X (row %d col %d of %dx%d pitch %d) pitch=%d extra=%08X "
            "src=(%.2f %.2f) wh=%dx%d step=%.4f ppu=%.4f f2c=%.4f b=(%d %d %d %d) map=(%d %d %d)\n",
            i, r.fn, r.tick, r.self, r.dst, row, col, r.surfW, r.surfH, r.surfPitch, (int)r.pitch, r.flags,
            r.srcX, r.srcY, r.w, r.h, r.step, r.ppu, r.f2c, r.f48, r.f4c, r.f50, r.f54, r.f64, r.f68, r.f6c);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Called with the frame canvas before the game draws: remember the screen surface.
static void NoteFrameSurface(void* canvas)
{
    if (!canvas) return;
    uintptr_t surf = *(uintptr_t*)canvas;
    if (!surf) return;
    __try {
        g_surfBase = At<uint32_t>(surf, 4);
        g_surfPitch = At<int>(surf, 8) / 2;
        g_surfW = At<int>(surf, 0x10);
        g_surfH = At<int>(surf, 0x14);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static uint32_t __fastcall Hook_TileBlit(void* self, void* edx, uint16_t* dst, int pitch,
    float srcX, float srcY, int w, int h, uint32_t extra)
{
    RecordBlit(0x856880, self, dst, pitch, srcX, srcY, w, h, extra);
    return SplitTileBlit((PFN_TileBlit)addr::TileBlit, self, dst, pitch, srcX, srcY, w, h, extra);
}
static uint32_t __fastcall Hook_TileBlitBlend(void* self, void* edx, uint16_t* dst, int pitch,
    float srcX, float srcY, int w, int h, uint32_t extra)
{
    RecordBlit(0x856ea0, self, dst, pitch, srcX, srcY, w, h, extra);
    return SplitTileBlit((PFN_TileBlit)addr::TileBlitBlend, self, dst, pitch, srcX, srcY, w, h, extra);
}

// ---------------------------------------------------------------------------
// Cheats. The mission input handler 0x4a87b0 (called at 0x4477ae) feeds typed
// characters (WM_CHAR) to a password matcher (0x5ca430 on 0xBCF134, at 0x4a8826);
// on a match it sets CJuego+0xEA, which enables the cheat keys (table 0xB933A0).
// The Steam build changed the password from GONZOANDJON (original and Legacy) to
// PYROFOREVER. Both are accepted here, in any letter case. Cheats=1 sets the
// flag before every input event, so no password is needed.
// ---------------------------------------------------------------------------
namespace addr {
    constexpr uintptr_t Site_CheatInput = 0x4477ae;
    constexpr uintptr_t CheatInput      = 0x4a87b0;
    constexpr uintptr_t Site_CheatMatch = 0x4a8826;
    constexpr uintptr_t CheatMatch      = 0x5ca430;
    constexpr int J_CheatsOn            = 0xea;
}
static const char* const kCheatWords[] = { "PYROFOREVER", "GONZOANDJON" };
static int g_cheatPos[_countof(kCheatWords)];

static bool __fastcall Hook_CheatMatch(void* self, void* edx, int ch)
{
    char c = (char)toupper(ch & 0xFF);
    bool hit = false;
    for (int i = 0; i < (int)_countof(kCheatWords); i++) {
        const char* w = kCheatWords[i];
        int& pos = g_cheatPos[i];
        if (w[pos] == c) pos++;
        else pos = (w[0] == c) ? 1 : 0;
        if (!w[pos]) { pos = 0; hit = true; }
    }
    if (hit) Log("cheats enabled by password\n");
    return hit;
}

typedef bool(__fastcall* PFN_CheatInput)(void* self, void* edx, void* msg);
static bool __fastcall Hook_CheatInput(void* self, void* edx, void* msg)
{
    if (g_cfg.cheats) {
        uintptr_t juego = *(uintptr_t*)addr::G_Juego;
        if (juego) At<uint8_t>(juego, addr::J_CheatsOn) = 1;
    }
    return ((PFN_CheatInput)addr::CheatInput)(self, edx, msg);
}

// ---------------------------------------------------------------------------
// Performance: textured polygon rasterizer (water). For every polygon edge on
// every scanline, 0x839840 evaluates the texture coordinates at the edge from
// scratch with x87 code and a slow manual float->int truncation. Equivalent
// SSE version (thiscall, 3 args, ret 0xC):
//   void EdgeUV(uint32_t y, int32_t* u, int32_t* v)
//   word [this+0x46] = y
//   edge = (byte [this+0x48] && [this+8]->y <= y) ? second edge : first edge
//   [this+0x24] = (y - edge.y) * slopeU + edge.u     (slopes +0x14/+0x18 or +0x1C/+0x20)
//   [this+0x28] = (y - edge.y) * slopeV + edge.v
//   *u = trunc(float([this+0x24] * 65536)), *v = trunc(float([this+0x28] * 65536))
// Edge record: +4 y, +8 u, +0xC v (floats).
// ---------------------------------------------------------------------------
namespace addr {
    constexpr uintptr_t RastEdgeUV = 0x839840;
}
static inline int32_t TruncLow32(float f)
{
    // x87 fistp to a 64-bit integer, low 32 bits kept
    if (f > -2147483648.0f && f < 2147483648.0f) return _mm_cvtt_ss2si(_mm_set_ss(f));
    return (int32_t)(int64_t)f;
}
static void __fastcall Fast_RastEdgeUV(uint8_t* self, void*, uint32_t y, int32_t* outU, int32_t* outV)
{
    *(uint16_t*)(self + 0x46) = (uint16_t)y;
    const float fy = (float)(double)y;
    const float* e = *(const float**)(self + 8);
    float su, sv;
    if (self[0x48] && e[1] <= fy) {
        su = *(float*)(self + 0x1c); sv = *(float*)(self + 0x20);
    } else {
        e = *(const float**)(self + 0);
        su = *(float*)(self + 0x14); sv = *(float*)(self + 0x18);
    }
    const double d = (double)fy - e[1];
    const float u = (float)(d * su + e[2]);
    const float v = (float)(d * sv + e[3]);
    *(float*)(self + 0x24) = u;
    *(float*)(self + 0x28) = v;
    *outU = TruncLow32((float)((double)u * 65536.0));
    *outV = TruncLow32((float)((double)v * 65536.0));
}

// Water span renderer 0x83a2d0 (thiscall, 7 args, ret 0x1C): draws the part of
// one screen row that lies inside a run of the water mask, textured from a 256x256
// 8-bit texture through a 16-bit palette. Layer (this): +0x18 texture, +0x1C
// palette, +0x44 polygons (0x4C each: +0xC du/dx, +0x10 dv/dx, 16.16), +0x50 row
// table (span list pointer, span count), +0x54 rows, +0x58 first row, +0x5C/+0x60/
// +0x64 cached row/count/span. Span: word poly, short x0, short x1.
// Literal C translation of the original (same results, including its quirks),
// with the pixel loop kept in registers instead of global variables.
namespace addr {
    constexpr uintptr_t WaterSpanRow = 0x83a2d0;
}
#pragma pack(push, 1)
struct WaterSpan { uint16_t poly; int16_t x0, x1; };
#pragma pack(pop)

static void __fastcall Fast_WaterSpanRow(uint8_t* L, void*, uint32_t y, uint32_t step, uint32_t runLen,
    uint32_t xBase, uint32_t* count, uint32_t* xpos, uint16_t** pdst)
{
    const uint32_t y0 = *(uint32_t*)(L + 0x58);
    if (y < y0 || y >= *(uint32_t*)(L + 0x54) + y0) return;
    const uint32_t X = *xpos;
    const uint32_t runLen16 = runLen << 16;
    const int32_t xs = (int32_t)((X + xBase) >> 16);
    const int32_t xe = (int32_t)((runLen16 + X + xBase) >> 16);

    const WaterSpan* p;
    int32_t n;
    const WaterSpan* cp = *(const WaterSpan**)(L + 0x64);
    if (*(uint32_t*)(L + 0x5c) == y && *(int32_t*)(L + 0x60) > 0 && cp && cp->x0 < xs) {
        p = cp; n = *(int32_t*)(L + 0x60);
    } else {
        const uint32_t* rec = (const uint32_t*)(*(uintptr_t*)(L + 0x50) + (y - y0) * 8);
        p = (const WaterSpan*)(uintptr_t)rec[0]; n = (int32_t)rec[1];
    }
    if (p->x0 >= xe) return;
    if (n > 0) {
        while (xs >= p->x1) { n--; p++; if (n <= 0) break; }
    }
    if (n == 0) return;
    if (p->x0 >= xe) return;
    *(int32_t*)(L + 0x60) = n;
    *(const WaterSpan**)(L + 0x64) = p;
    *(uint32_t*)(L + 0x5c) = y;

    uint8_t* const polys = *(uint8_t**)(L + 0x44);
    const uint8_t* const tex = *(const uint8_t**)(L + 0x18);
    const uint16_t* const pal = *(const uint16_t**)(L + 0x1c);
    uint8_t* poly = polys + p->poly * 0x4c;
    int32_t U, V;
    Fast_RastEdgeUV(poly, nullptr, y, &U, &V);
    {
        const int32_t du = *(int32_t*)(poly + 0xc), dv = *(int32_t*)(poly + 0x10);
        U += (xs - p->x0) * du;
        V += (xs - p->x0) * dv;
    }
    int32_t duStep = (int32_t)(((int64_t)*(int32_t*)(poly + 0xc) * (int32_t)step) >> 16);
    int32_t dvStep = (int32_t)(((int64_t)*(int32_t*)(poly + 0x10) * (int32_t)step) >> 16);
    uint16_t* dst = *pdst;
    uint32_t pixLeft = (runLen16 - X) / step;
    if (pixLeft > *count) pixLeft = *count;

#define WATER_PIXEL(u, v) pal[tex[(((uint32_t)(u) >> 16) & 0xff) | ((((uint32_t)(v) >> 16) & 0xff) << 8)]]
    if (pixLeft > 2) {
        int32_t x1 = p->x1;
        uint32_t xb = xBase + X;
        *xpos = X + pixLeft * step;
        for (;;) {
            if ((xb >> 16) >= (uint32_t)x1) break;
            uint32_t m = (((uint32_t)x1 << 16) - xb) / step;
            if (m > pixLeft) { m = pixLeft; pixLeft = 0; }
            else pixLeft -= m;
            if (m != 0) {
                *count -= m;
                xb += m * step;
                for (uint32_t i = 0; i < m; i++) {
                    dst[i] = WATER_PIXEL(U, V);
                    U += duStep; V += dvStep;
                }
                dst += m;
            }
            // next span of this row
            if (--n <= 0) break;
            p++;
            *(int32_t*)(L + 0x60) = n;
            *(const WaterSpan**)(L + 0x64) = p;
            poly = polys + p->poly * 0x4c;
            duStep = (int32_t)(((int64_t)*(int32_t*)(poly + 0xc) * (int32_t)step) >> 16);
            dvStep = (int32_t)(((int64_t)*(int32_t*)(poly + 0x10) * (int32_t)step) >> 16);
            x1 = p->x1;
            Fast_RastEdgeUV(poly, nullptr, y, &U, &V);
            if (pixLeft == 0) break;
        }
    }
    // remaining pixels of the run with the current span's texture coordinates
    while (*count != 0 && (*xpos >> 16) < runLen) {
        *dst++ = WATER_PIXEL(U, V);
        U += duStep; V += dvStep;
        (*count)--;
        *xpos += step;
    }
#undef WATER_PIXEL
    *pdst = dst;
}

// Compares the replacement with the original on random inputs (before patching).
static int SelfTestWaterSpanRow()
{
    typedef void(__fastcall* PFN)(uint8_t*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t*, uint16_t**);
    PFN orig = (PFN)addr::WaterSpanRow;
    uint32_t seed = 777;
    auto r32 = [&]() { seed = seed * 1664525u + 1013904223u; return seed; };
    auto rf = [&]() { return (r32() >> 8) / 16777216.0f; };
    std::vector<uint8_t> tex(65536);
    std::vector<uint16_t> pal(256);
    for (auto& t : tex) t = (uint8_t)r32();
    for (auto& c : pal) c = (uint16_t)r32();
    int bad = 0;
    for (int it = 0; it < 4000; it++) {
        // polygons with edges
        const int NP = 8;
        float edges[NP][2][4];
        uint8_t polysA[NP * 0x4c], polysB[NP * 0x4c];
        memset(polysA, 0, sizeof(polysA));
        for (int i = 0; i < NP; i++) {
            uint8_t* q = polysA + i * 0x4c;
            for (int e = 0; e < 2; e++) {
                edges[i][e][0] = 0; edges[i][e][1] = rf() * 600;
                edges[i][e][2] = rf() * 400 - 50; edges[i][e][3] = rf() * 400 - 50;
            }
            *(float**)(q + 0) = edges[i][0]; *(float**)(q + 8) = edges[i][1];
            for (int k = 0x14; k <= 0x20; k += 4) *(float*)(q + k) = rf() * 4 - 2;
            q[0x48] = (uint8_t)(r32() & 1);
            *(int32_t*)(q + 0xc) = (int32_t)(r32() % 0x40000) - 0x20000;
            *(int32_t*)(q + 0x10) = (int32_t)(r32() % 0x40000) - 0x20000;
        }
        memcpy(polysB, polysA, sizeof(polysA));
        // spans for 4 rows
        WaterSpan spans[4][10];
        uint32_t rowsA[4][2], rowsB[4][2];
        for (int rr = 0; rr < 4; rr++) {
            int ns = 1 + (int)(r32() % 10), x = (int)(r32() % 200);
            for (int s = 0; s < ns; s++) {
                int w = 1 + (int)(r32() % 80);
                spans[rr][s] = { (uint16_t)(r32() % NP), (int16_t)x, (int16_t)(x + w) };
                x += w + (int)(r32() % 3 == 0 ? r32() % 20 : 0);
            }
            rowsA[rr][0] = (uint32_t)(uintptr_t)spans[rr]; rowsA[rr][1] = (uint32_t)ns;
        }
        memcpy(rowsB, rowsA, sizeof(rowsA));
        alignas(16) uint8_t LA[0x6c] = {}, LB[0x6c] = {};
        *(const uint8_t**)(LA + 0x18) = tex.data(); *(const uint16_t**)(LA + 0x1c) = pal.data();
        *(uint32_t*)(LA + 0x54) = 4; *(uint32_t*)(LA + 0x58) = 100;
        *(uint32_t*)(LA + 0x5c) = 0xFFFFFFFF;
        memcpy(LB, LA, sizeof(LA));
        *(uint8_t**)(LA + 0x44) = polysA; *(uint8_t**)(LB + 0x44) = polysB;
        *(uint32_t**)(LA + 0x50) = &rowsA[0][0]; *(uint32_t**)(LB + 0x50) = &rowsB[0][0];
        // several runs on the same layer (exercises the span cache)
        for (int run = 0; run < 4; run++) {
            uint32_t y = 99 + r32() % 6;
            uint32_t step = 0x4000 + r32() % 0x30000;
            uint32_t runLen = 1 + r32() % 300;
            uint32_t xBase = (r32() % 300) << 16;
            uint32_t X = r32() % (runLen << 16);
            uint32_t cnt = 1 + r32() % 400;
            uint16_t bufA[1024], bufB[1024];
            memset(bufA, 0xCD, sizeof(bufA)); memset(bufB, 0xCD, sizeof(bufB));
            uint32_t cA = cnt, cB = cnt, xA = X, xB = X;
            uint16_t* dA = bufA + 8; uint16_t* dB = bufB + 8;
            orig(LA, nullptr, y, step, runLen, xBase, &cA, &xA, &dA);
            Fast_WaterSpanRow(LB, nullptr, y, step, runLen, xBase, &cB, &xB, &dB);
            bool same = cA == cB && xA == xB && (dA - bufA) == (dB - bufB) &&
                        memcmp(bufA, bufB, sizeof(bufA)) == 0 && memcmp(polysA, polysB, sizeof(polysA)) == 0 &&
                        memcmp(LA + 0x5c, LB + 0x5c, 4) == 0 && *(int32_t*)(LA + 0x60) == *(int32_t*)(LB + 0x60) &&
                        (uint8_t*)*(WaterSpan**)(LA + 0x64) - (uint8_t*)spans == (uint8_t*)*(WaterSpan**)(LB + 0x64) - (uint8_t*)spans;
            if (!same) {
                if (bad < 5) Log("  water span mismatch it=%d run=%d: count %u/%u xpos %08X/%08X dst %d/%d\n", it, run,
                                 cA, cB, xA, xB, (int)(dA - bufA), (int)(dB - bufB));
                bad++;
            }
        }
    }
    return bad;
}

// Compares the replacement with the original on random inputs (before patching).
static int SelfTestRastEdgeUV()
{
    typedef void(__fastcall* PFN)(uint8_t*, void*, uint32_t, int32_t*, int32_t*);
    PFN orig = (PFN)addr::RastEdgeUV;
    uint32_t seed = 12345;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
    int bad = 0;
    for (int i = 0; i < 20000; i++) {
        alignas(16) uint8_t a[0x50] = {}, b[0x50] = {};
        float e1[4] = { 0, rnd() * 1200, rnd() * 600 - 100, rnd() * 600 - 100 };
        float e2[4] = { 0, rnd() * 1200, rnd() * 600 - 100, rnd() * 600 - 100 };
        *(float**)(a + 0) = e1; *(float**)(a + 8) = e2;
        for (int k = 0x14; k <= 0x20; k += 4) *(float*)(a + k) = rnd() * 8 - 4;
        a[0x48] = (i & 1) ? 1 : 0;
        memcpy(b, a, sizeof(a));
        uint32_t y = (uint32_t)(rnd() * 1300);
        int32_t u1, v1, u2, v2;
        orig(a, nullptr, y, &u1, &v1);
        Fast_RastEdgeUV(b, nullptr, y, &u2, &v2);
        if (u1 != u2 || v1 != v2 || memcmp(a, b, sizeof(a)) != 0) {
            if (bad < 5) Log("  edge uv mismatch y=%u: %d,%d vs %d,%d\n", y, u1, v1, u2, v2);
            bad++;
        }
    }
    return bad;
}

static void PatchPerformance()
{
    int bad = SelfTestRastEdgeUV();
    Log("rasterizer self-test: %d/20000 mismatches\n", bad);
    if (bad > 20) { Log("rasterizer optimizations NOT applied\n"); return; }
    int badSpan = SelfTestWaterSpanRow();
    Log("water span self-test: %d/16000 mismatches\n", badSpan);
    static const uint8_t prolog[] = { 0x83, 0xEC, 0x08, 0x8B, 0x44, 0x24, 0x0C, 0x8B, 0xD0 };
    static const uint8_t prologSpan[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28, 0x8B, 0x45, 0x08 };
    bool ok = PatchJmp(addr::RastEdgeUV, prolog, sizeof(prolog), (void*)&Fast_RastEdgeUV);
    if (badSpan == 0)
        ok &= PatchJmp(addr::WaterSpanRow, prologSpan, sizeof(prologSpan), (void*)&Fast_WaterSpanRow);
    Log("rasterizer optimizations: %s%s\n", ok ? "ok" : "FAILED", badSpan ? " (water spans NOT replaced)" : "");
}

static bool IsGameCode(uint32_t a) { return a >= 0x401000 && a < 0xAE8000; }

// ---------------------------------------------------------------------------
// Map background cache. The camera's map draw 0x7e7de0 (thiscall(canvas, ctx),
// ret 8; called from the world draw at 0x7eaeab, before the objects) draws the
// map background tiles and the animated water. Between two logic ticks it only
// depends on the camera, so when an extra frame has exactly the same camera as
// the tick frame, the background drawn in the tick frame is copied back instead
// of being drawn again. Canvas: +0 surface, +4 x, +8 y, +0xC w, +0x10 h (clip
// rectangle in surface pixels); surface: +4 pixels, +8 pitch (bytes), +0x10 w,
// +0x14 h.
// ---------------------------------------------------------------------------
namespace addr {
    constexpr uintptr_t Site_MapDraw = 0x7eaeab;
    constexpr uintptr_t MapDraw      = 0x7e7de0;
}
struct BgCache {
    uintptr_t cam = 0;
    uint32_t tick = 0xFFFFFFFF;
    bool valid = false;
    uint32_t key[16] = {};
    int x = 0, y = 0, w = 0, h = 0;
    std::vector<uint16_t> px;
};
static BgCache g_bg[4];
static uint32_t g_bgHits = 0, g_bgMisses = 0;

static int BgKey(uintptr_t cam, const int32_t* cv, uintptr_t surf, uintptr_t ctx, uint32_t* k)
{
    int n = 0;
    for (int off = 0x68; off < 0x78; off += 4) k[n++] = At<uint32_t>(cam, off);   // position, 1/zoom, zoom
    k[n++] = At<uint32_t>(cam, 0x30); k[n++] = At<uint32_t>(cam, 0x34);
    k[n++] = At<uint32_t>(cam, 0x94); k[n++] = At<uint32_t>(cam, 0x98);
    k[n++] = At<uint32_t>(cam, 0x90);
    for (int i = 1; i < 5; i++) k[n++] = (uint32_t)cv[i];
    k[n++] = At<uint32_t>(surf, 8);
    k[n++] = ctx ? At<uint32_t>(ctx, 0x18) : 0;
    return n;
}

typedef void(__fastcall* PFN_MapDraw)(void* cam, void* edx, void* canvas, void* ctx);
static void __fastcall Hook_MapDraw(void* self, void* edx, void* canvas, void* ctx)
{
    uintptr_t cam = (uintptr_t)self;
    const int32_t* cv = (const int32_t*)canvas;
    uintptr_t surf = cv ? (uintptr_t)cv[0] : 0;
    uint8_t* base = surf ? At<uint8_t*>(surf, 4) : nullptr;
    if (!g_active || !base || !g_inGame && !g_inMenu) {
        ((PFN_MapDraw)addr::MapDraw)(self, edx, canvas, ctx);
        return;
    }
    const int pitch = At<int>(surf, 8), sw = At<int>(surf, 0x10), sh = At<int>(surf, 0x14);
    int x0 = cv[1], y0 = cv[2], x1 = cv[1] + cv[3], y1 = cv[2] + cv[4];
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sw) x1 = sw;
    if (y1 > sh) y1 = sh;
    if (x1 <= x0 || y1 <= y0) { ((PFN_MapDraw)addr::MapDraw)(self, edx, canvas, ctx); return; }

    BgCache* c = nullptr;
    for (auto& e : g_bg) if (e.cam == cam) { c = &e; break; }
    if (!c) { static int rr = 0; c = &g_bg[rr++ & 3]; c->cam = cam; c->valid = false; }

    uint32_t key[16];
    int nk = BgKey(cam, cv, surf, (uintptr_t)ctx, key);
    const int w = x1 - x0, h = y1 - y0;
    if (g_inExtraFrame && c->valid && c->tick == g_tick && memcmp(key, c->key, nk * 4) == 0 &&
        c->x == x0 && c->y == y0 && c->w == w && c->h == h) {
        for (int y = 0; y < h; y++)
            memcpy(base + (size_t)(y0 + y) * pitch + x0 * 2, &c->px[(size_t)y * w], (size_t)w * 2);
        g_bgHits++;
        return;
    }
    ((PFN_MapDraw)addr::MapDraw)(self, edx, canvas, ctx);
    g_bgMisses++;
    if (g_cfg.debug && g_bgMisses % 500 == 0)
        Log("map background cache: %u copied, %u drawn\n", g_bgHits, g_bgMisses);
    if (!g_inExtraFrame) {
        // tick frame: remember the background for this tick's extra frames
        c->px.resize((size_t)w * h);
        for (int y = 0; y < h; y++)
            memcpy(&c->px[(size_t)y * w], base + (size_t)(y0 + y) * pitch + x0 * 2, (size_t)w * 2);
        memcpy(c->key, key, nk * 4);
        c->x = x0; c->y = y0; c->w = w; c->h = h;
        c->tick = g_tick;
        c->valid = true;
    }
}

static void PatchMapCache()
{
    bool ok = PatchCallRel32(addr::Site_MapDraw, addr::MapDraw, (void*)&Hook_MapDraw);
    Log("map background cache: %s\n", ok ? "ok" : "FAILED");
}

// ---------------------------------------------------------------------------
// Game bug: quick clicks in the inventory are ignored. The game turns a left click
// that follows the previous one within the double-click time and 5 pixels into a
// double-click event (0x208 instead of 0x202, see 0x7967b0). The inventory grid
// (CVentanaCuadriculaInv, event handler 0x930790) only reacts to 0x202, so picking
// up and dropping an item quickly in the same place does nothing. Its switch maps
// event - 0x201 through the byte table at 0x93092C; send 0x208 to the 0x202 case
// (as the game already does for 0x20A / 0x204).
// ---------------------------------------------------------------------------
static void PatchInventoryDoubleClick()
{
    uint8_t* p = (uint8_t*)(0x93092c + (0x208 - 0x201));
    const uint8_t* click = (const uint8_t*)(0x93092c + (0x202 - 0x201));
    if (*p != 3 || *click != 1) { Log("inventory double-click fix: unexpected bytes\n"); return; }
    DWORD old;
    VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old);
    *p = 1;
    VirtualProtect(p, 1, old, &old);
    Log("inventory double-click fix: ok\n");
}

static void PatchCheats()
{
    bool ok = PatchCallRel32(addr::Site_CheatMatch, addr::CheatMatch, (void*)&Hook_CheatMatch);
    ok &= PatchCallRel32(addr::Site_CheatInput, addr::CheatInput, (void*)&Hook_CheatInput);
    Log("cheat hooks: %s (Cheats=%d)\n", ok ? "ok" : "FAILED", g_cfg.cheats ? 1 : 0);
}

static void PatchTileBlitOverflow()
{
    bool ok = PatchCallRel32(addr::Site_TileBlit, addr::TileBlit, (void*)&Hook_TileBlit);
    ok &= PatchCallRel32(addr::Site_TileBlitBlend, addr::TileBlitBlend, (void*)&Hook_TileBlitBlend);
    Log("map background tile-array overflow fix: %s\n", ok ? "ok" : "FAILED");
    // The map tile cache (set up at 0x858730) holds (screen tiles + 1) * zoom-out
    // factor columns, capped at 24 (mov eax, 0x18 at 0x8587ce) to match the blitter
    // arrays above. Zoomed out at 1920x1080 about 62 columns are visible, so tiles
    // outside the cache are skipped and leave holes (old frame contents) in the
    // map. With the blits split, the cap can go: allow 96 columns (rows are not capped).
    PatchImm32(addr::TileCacheColCap, 0xB8, 0x18, 96);
    AddVectoredExceptionHandler(0, CrashVeh);   // last in line: logs crashes in game code
}

static void PatchHDMenus()
{
    struct { uintptr_t site; uintptr_t from; uintptr_t to; } sites[] = {
        { 0x7ab1a9, 0x414030, 0x413fe0 }, { 0x7ab1b5, 0x414020, 0x413fa0 },   // CMAMenuInGame::Activate
        // CMABriefing (0x7a1eea/0x7a1ef6) is NOT patched: its overlay and map
        // viewer have a fixed 640x480 layout without centring.
    };
    int ok = 0;
    for (auto& s : sites) ok += PatchCallRel32(s.site, s.from, (void*)s.to) ? 1 : 0;
    Log("HD pause menu: %d/2 patched\n", ok);
}

static void PatchHDResolution()
{
    int w = g_cfg.hdW, h = g_cfg.hdH;
    if (w <= 0 || h <= 0 || (w == 1280 && h == 720)) return;
    struct { uintptr_t at; uint8_t op; bool height; } sites[] = {
        { 0x413f45, 0xB8, false }, { 0x413f85, 0xB8, true },
        { 0x413fc0, 0xB8, false }, { 0x414000, 0xB8, true },
        { 0x414062, 0xBA, false }, { 0x414094, 0xBA, true },
        { 0x7b38f2, 0xB8, false }, { 0x7b38f7, 0xB9, true },
    };
    for (auto& s : sites) PatchImm32(s.at, s.op, s.height ? 720u : 1280u, s.height ? (uint32_t)h : (uint32_t)w);

    // Menu label (room up to 0xBB9938 in the original data).
    char* label = (char*)0xBB98E8;
    if (memcmp(label, "1280x720 (16:9)", 16) == 0) {
        char text[32];
        int g = w, a = h;
        while (a) { int t = g % a; g = a; a = t; }
        sprintf_s(text, "%dx%d (%d:%d)", w, h, w / g, h / g);
        DWORD old;
        VirtualProtect(label, 32, PAGE_READWRITE, &old);
        strcpy_s(label, 32, text);
        VirtualProtect(label, 32, old, &old);
    }
    Log("HD video mode set to %dx%d\n", w, h);
}

// ---------------------------------------------------------------------------
// Sampling profiler (ini: Profile=1, development only). A background thread
// samples the main thread's EIP about every millisecond and logs the hottest
// code addresses every 10 seconds. Samples outside the game's code are also
// attributed to the nearest game return address on the stack.
// ---------------------------------------------------------------------------
static DWORD g_mainThreadId = 0;


// First plausible game return address on a stack (preceded by a call instruction).
static uint32_t FindGameReturn(uint32_t esp)
{
    __try {
        const uint32_t* sp = (const uint32_t*)esp;
        for (int i = 0; i < 256; i++) {
            uint32_t v = sp[i];
            if (!IsGameCode(v)) continue;
            const uint8_t* p = (const uint8_t*)v;
            if (p[-5] == 0xE8 || p[-6] == 0xFF || p[-2] == 0xFF || p[-3] == 0xFF) return v;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

static DWORD WINAPI ProfilerThread(void*)
{
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, g_mainThreadId);
    if (!th) { Log("profiler: OpenThread failed\n"); return 0; }
    std::unordered_map<uint32_t, uint32_t> eip, caller;
    uint32_t total = 0, inGame = 0, inMod = 0, other = 0;
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&ProfilerThread, &self);
    uintptr_t selfLo = (uintptr_t)self, selfHi = selfLo + 0x200000;
    DWORD last = GetTickCount();
    for (;;) {
        Sleep(1);
        if (SuspendThread(th) == (DWORD)-1) break;
        CONTEXT c = {}; c.ContextFlags = CONTEXT_CONTROL;
        bool ok = GetThreadContext(th, &c) != 0;
        uint32_t ret = 0;
        if (ok && !IsGameCode(c.Eip)) {
            ret = FindGameReturn(c.Esp);
        }
        ResumeThread(th);
        if (!ok) continue;
        total++;
        if (IsGameCode(c.Eip)) { inGame++; eip[c.Eip & ~0xFu]++; }
        else {
            if (c.Eip >= selfLo && c.Eip < selfHi) inMod++; else other++;
            caller[ret & ~0xFu]++;
        }
        DWORD now = GetTickCount();
        if (now - last >= 10000 && total) {
            last = now;
            Log("--- profile: %u samples, game %.1f%%, patch %.1f%%, other (DirectDraw/system) %.1f%%\n", total,
                100.0 * inGame / total, 100.0 * inMod / total, 100.0 * other / total);
            auto dump = [&](std::unordered_map<uint32_t, uint32_t>& m, const char* title) {
                std::vector<std::pair<uint32_t, uint32_t>> v(m.begin(), m.end());
                std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
                Log("  %s:\n", title);
                for (size_t i = 0; i < v.size() && i < 25; i++)
                    Log("    %08X  %5.1f%%\n", v[i].first, 100.0 * v[i].second / total);
            };
            dump(eip, "hottest game code (16-byte buckets)");
            dump(caller, "outside game code, by game caller");
            eip.clear(); caller.clear(); total = inGame = inMod = other = 0;
        }
    }
    return 0;
}

static void Init()
{
    g_mainThreadId = GetCurrentThreadId();
    GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
    char* slash = strrchr(g_dir, '\\');
    if (slash) *slash = 0;

    char path[MAX_PATH];
    sprintf_s(path, "%s\\" MOD_NAME ".log", g_dir);
    fopen_s(&g_log, path, "a");
    LoadConfig();
    sprintf_s(g_modDir, "%s\\" MOD_NAME, g_dir);
    g_useCnc = g_cfg.cnc;
    SYSTEMTIME st; GetLocalTime(&st);
    Log("\n=== %02d:%02d:%02d ===\n", st.wHour, st.wMinute, st.wSecond);
    {
        char ini[MAX_PATH];
        sprintf_s(ini, "%s\\" MOD_NAME ".ini", g_dir);
        if (GetPrivateProfileIntA(MOD_NAME, "Profile", 0, ini)) {
            CreateThread(nullptr, 0, ProfilerThread, nullptr, 0, nullptr);
            Log("profiler started (main thread %u)\n", g_mainThreadId);
        }
    }
    Log(MOD_NAME " " MOD_VERSION " loaded. enabled=%d camera=%d objects=%d maxFps=%d hd=%dx%d\n", g_cfg.enabled, g_cfg.camera,
        g_cfg.objects, g_cfg.maxFps, g_cfg.hdW, g_cfg.hdH);

    // Sanity check that this is the expected executable build.
    const char* sig = (const char*)0xAEC13C;
    if (IsBadReadPtr(sig, 13) || memcmp(sig, "NoLimitedFps", 13) != 0 ||
        *(uint32_t*)(addr::VT_VisorMundo + addr::VT_VisorMundo_Draw * 4) != 0x484820) {
        Log("unexpected Comm2.exe build - hooks disabled\n");
        return;
    }
    PatchHDResolution();
    PatchTileBlitOverflow();   // game bug fix, needed for resolutions above 1280x720
    PatchCheats();
    PatchInventoryDoubleClick();   // game bug fix
    {
        char ini[MAX_PATH];
        sprintf_s(ini, "%s\\" MOD_NAME ".ini", g_dir);
        if (GetPrivateProfileIntA(MOD_NAME, "Optimizations", 1, ini)) PatchPerformance();
    }
    if (g_cfg.hdMenus) PatchHDMenus();
    if (g_cfg.hdMenus && g_cfg.pauseScale) PatchScaledPause();
    // Load the DirectDraw backend now, so cnc-ddraw can install its input
    // hooks before the game creates its DirectInput devices.
    LoadRealDDraw();
    if (!g_cfg.enabled) return;

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_qpcToMs = 1000.0 / (double)f.QuadPart;
    timeBeginPeriod(1);

    bool ok = PatchCallRel32(addr::Site_Update, addr::Update, (void*)&Hook_Update);
    ok = ok && PatchCallRel32(addr::Site_Frame, addr::Frame, (void*)&Hook_Frame);
    ok = ok && PatchCallRel32(addr::Site_EndIter, addr::CanvasReset, (void*)&Hook_EndIter);
    if (!ok) { Log("patching failed\n"); return; }
    Game_VisorDraw = (PFN_Void1)PatchVtable(addr::VT_VisorMundo, addr::VT_VisorMundo_Draw, (void*)&Hook_VisorDraw);
    static const uint8_t seqProlog[] = { 0x56, 0x8B, 0xF1, 0x80, 0x7E, 0x44, 0x00, 0x0F, 0x84 };
    if (PatchJmp(addr::SeqDraw, seqProlog, sizeof(seqProlog), (void*)&Stub_SeqDraw))
        Log("face sequence hook installed\n");
    if (PatchCallRel32(addr::Site_CursorAnimTick, addr::CursorAnimTick, (void*)&Hook_CursorAnimTick))
        Log("cursor animation hook installed\n");
    int caretOk = 0;
    for (uintptr_t site : addr::Sites_TextFieldDraw)
        caretOk += PatchCallRel32(site, addr::TextFieldDraw, (void*)&Hook_TextFieldDraw) ? 1 : 0;
    Log("text caret hooks installed: %d/5\n", caretOk);
    int projOk = 0;
    for (uintptr_t site : addr::Site_ObjProject)
        projOk += PatchCallRel32(site, addr::ObjProject, (void*)&Hook_ObjProject) ? 1 : 0;
    Log("object projection hooks installed: %d/3\n", projOk);
    {
        char ini[MAX_PATH];
        sprintf_s(ini, "%s\\" MOD_NAME ".ini", g_dir);
        if (GetPrivateProfileIntA(MOD_NAME, "Optimizations", 1, ini)) PatchMapCache();
    }
    Log("hooks installed\n");
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        Init();
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_log) fclose(g_log);
    }
    return TRUE;
}
