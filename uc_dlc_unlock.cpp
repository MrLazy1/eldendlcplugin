// ===========================================================================
//  uc_dlc_unlock.cpp  --  UnionCrax / uc-online2 plugin
//
//  Forces ELDEN RING: Shadow of the Erdtree (AppId 2778580) to report as
//  owned AND installed, so the DLC loads while the game runs as Spacewar.
//
//  Build (x64 Native Tools Command Prompt for VS 2022):
//     cl /LD /O2 /EHsc /std:c++17 uc_dlc_unlock.cpp /link /OUT:10_dlc_unlock.dll
//
//  Install:
//     copy 10_dlc_unlock.dll  ->  <ELDEN RING>\Game\plugins\
//     union-crax.ini          ->  PluginsFolder=plugins
// ===========================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Config - add more appids to BOTH arrays if you ever need more DLC.
// ---------------------------------------------------------------------------
static const uint32_t kBaseGame  = 1245620;                              // ELDEN RING
static const uint32_t kDlcIds[]  = { 2778580 };                          // Shadow of the Erdtree
static const char*    kDlcNames[] = { "ELDEN RING - Shadow of the Erdtree" };
static const int      kDlcCount  = (int)(sizeof(kDlcIds) / sizeof(kDlcIds[0]));

static bool IsOurs(uint32_t id) {
    for (int i = 0; i < kDlcCount; ++i) if (kDlcIds[i] == id) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Logging -> dlc_unlock.log next to the game exe
// ---------------------------------------------------------------------------
static void Log(const char* fmt, ...) {
    char path[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* s = strrchr(path, '\\');
    if (s) *(s + 1) = 0; else path[0] = 0;
    strcat_s(path, MAX_PATH, "dlc_unlock.log");

    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

// ---------------------------------------------------------------------------
// Originals
// ---------------------------------------------------------------------------
typedef bool     (*tBIsSubscribedApp)(void*, uint32_t);
typedef bool     (*tBIsDlcInstalled)(void*, uint32_t);
typedef uint32_t (*tGetEarliestPurchase)(void*, uint32_t);
typedef int      (*tGetDLCCount)(void*);
typedef bool     (*tBGetDLCDataByIndex)(void*, int, uint32_t*, bool*, char*, int);
typedef int      (*tUserHasLicenseForApp)(void*, uint64_t, uint32_t);

static tBIsSubscribedApp     o_BIsSubscribedApp     = nullptr;
static tBIsDlcInstalled      o_BIsDlcInstalled      = nullptr;
static tGetEarliestPurchase  o_GetEarliestPurchase  = nullptr;
static tGetDLCCount          o_GetDLCCount          = nullptr;
static tBGetDLCDataByIndex   o_BGetDLCDataByIndex   = nullptr;
static tUserHasLicenseForApp o_UserHasLicenseForApp = nullptr;

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
static bool H_BIsSubscribedApp(void* self, uint32_t appId) {
    if (appId == kBaseGame || IsOurs(appId)) return true;
    return o_BIsSubscribedApp ? o_BIsSubscribedApp(self, appId) : true;
}

static bool H_BIsDlcInstalled(void* self, uint32_t appId) {
    if (appId == kBaseGame || IsOurs(appId)) return true;
    return o_BIsDlcInstalled ? o_BIsDlcInstalled(self, appId) : true;
}

static uint32_t H_GetEarliestPurchase(void* self, uint32_t appId) {
    if (IsOurs(appId)) return 1609459200u;      // any non-zero date
    return o_GetEarliestPurchase ? o_GetEarliestPurchase(self, appId) : 0u;
}

// Our DLCs occupy indices [0 .. kDlcCount); the emulator's own entries follow.
static int H_GetDLCCount(void* self) {
    int n = o_GetDLCCount ? o_GetDLCCount(self) : 0;
    return kDlcCount + n;
}

static bool H_BGetDLCDataByIndex(void* self, int i, uint32_t* pAppID,
                                 bool* pbAvailable, char* pchName, int cch) {
    if (i >= 0 && i < kDlcCount) {
        if (pAppID)      *pAppID = kDlcIds[i];
        if (pbAvailable) *pbAvailable = true;
        if (pchName && cch > 0) strncpy_s(pchName, (size_t)cch, kDlcNames[i], _TRUNCATE);
        return true;
    }
    if (o_BGetDLCDataByIndex)
        return o_BGetDLCDataByIndex(self, i - kDlcCount, pAppID, pbAvailable, pchName, cch);
    return false;
}

static int H_UserHasLicenseForApp(void* self, uint64_t steamId, uint32_t appId) {
    if (appId == kBaseGame || IsOurs(appId)) return 1;      // k_EResultOK
    return o_UserHasLicenseForApp ? o_UserHasLicenseForApp(self, steamId, appId) : 1;
}

// ---------------------------------------------------------------------------
// vtable patching
// ---------------------------------------------------------------------------
static HMODULE g_steam = nullptr;

struct Patch { void** slot; void* original; };
static Patch  g_patches[16];
static int    g_patchCount = 0;
static volatile LONG g_state = 0;    // 0 = idle, 1 = installing, 2 = installed

static void* PatchSlot(void* obj, int index, void* hook, void** origOut) {
    if (!obj) return nullptr;
    void** vt = *reinterpret_cast<void***>(obj);
    if (!vt) return nullptr;

    void** slot = &vt[index];
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return nullptr;
    void* original = *slot;
    *slot = hook;
    VirtualProtect(slot, sizeof(void*), old, &old);

    if (origOut && !*origOut) *origOut = original;
    if (g_patchCount < 16) {
        g_patches[g_patchCount].slot     = slot;
        g_patches[g_patchCount].original = original;
        ++g_patchCount;
    }
    return original;
}

static void* Resolve(const char* const* names, int count) {
    for (int i = 0; i < count; ++i) {
        FARPROC p = GetProcAddress(g_steam, names[i]);
        if (p) { typedef void* (*fn)(); return reinterpret_cast<fn>(p)(); }
    }
    return nullptr;
}

// ISteamApps vtable:  6 BIsSubscribedApp | 7 BIsDlcInstalled
//                     8 GetEarliestPurchaseUnixTime | 10 GetDLCCount
//                     11 BGetDLCDataByIndex
// ISteamUser vtable: 16 UserHasLicenseForApp
static void Install() {
    if (InterlockedCompareExchange(&g_state, 1, 0) != 0) return;

    g_steam = GetModuleHandleA("steam_api64.dll");
    if (!g_steam) { Log("steam_api64.dll not loaded yet"); g_state = 0; return; }

    static const char* appsNames[] = { "SteamAPI_SteamApps_v008", "SteamAPI_SteamApps_v007" };
    static const char* userNames[] = { "SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022",
                                       "SteamAPI_SteamUser_v021" };

    void* apps = Resolve(appsNames, 2);
    void* user = Resolve(userNames, 3);

    if (!apps) { Log("ISteamApps interface not resolved"); g_state = 0; return; }

    PatchSlot(apps,  6, (void*)&H_BIsSubscribedApp,   (void**)&o_BIsSubscribedApp);
    PatchSlot(apps,  7, (void*)&H_BIsDlcInstalled,    (void**)&o_BIsDlcInstalled);
    PatchSlot(apps,  8, (void*)&H_GetEarliestPurchase,(void**)&o_GetEarliestPurchase);
    PatchSlot(apps, 10, (void*)&H_GetDLCCount,        (void**)&o_GetDLCCount);
    PatchSlot(apps, 11, (void*)&H_BGetDLCDataByIndex, (void**)&o_BGetDLCDataByIndex);

    if (user) PatchSlot(user, 16, (void*)&H_UserHasLicenseForApp, (void**)&o_UserHasLicenseForApp);
    else      Log("ISteamUser not resolved - leaving ticket checks to the emulator");

    Log("installed %d hooks; unlocking DLC %u on base %u", g_patchCount, kDlcIds[0], kBaseGame);
    g_state = 2;
}

static void Uninstall() {
    for (int i = 0; i < g_patchCount; ++i) {
        DWORD old = 0;
        if (VirtualProtect(g_patches[i].slot, sizeof(void*), PAGE_READWRITE, &old)) {
            *g_patches[i].slot = g_patches[i].original;
            VirtualProtect(g_patches[i].slot, sizeof(void*), old, &old);
        }
    }
    g_patchCount = 0;
    Log("uninstalled");
}

// ---------------------------------------------------------------------------
// Entry points: plugin ABI + DllMain fallback.
// ctx is treated as opaque so this compiles against any uco_plugin.h revision.
// ---------------------------------------------------------------------------
static DWORD WINAPI RetryThread(LPVOID) {
    for (int i = 0; i < 100; ++i) {          // ~10 s
        Install();
        if (g_state == 2) return 0;
        Sleep(100);
    }
    Log("gave up waiting for steam_api64.dll");
    return 0;
}

extern "C" __declspec(dllexport) int  UCO_PluginInit(const void* /*ctx*/) { Install(); return 0; }
extern "C" __declspec(dllexport) void UCO_PluginShutdown(void)            { Uninstall(); }

BOOL WINAPI DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        HANDLE t = CreateThread(nullptr, 0, RetryThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
