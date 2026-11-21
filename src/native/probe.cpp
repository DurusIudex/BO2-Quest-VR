// Read-only, in-process research probe. No code patches, camera writes, or rendering.
// Candidate addresses documented by MIT-licensed pndevo/bo2split, revision
// e6c944d03025e5097bb2122455ccd4a2d3ffc4b2. See native-notices.txt.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "openvr.h"

namespace {
HMODULE selfModule;
INIT_ONCE systemOnce = INIT_ONCE_STATIC_INIT;
HMODULE systemXInput;
SRWLOCK frameLock = SRWLOCK_INIT;
volatile LONG probeStatus; // 0 waiting, 1 unsupported, 2 awaiting offline, 3 mismatch, 4 sampling
DWORD nextSample;
bool inspected, fingerprintMatches, vrAttempted;
vr::IVRSystem* vrSystem;
wchar_t logPath[MAX_PATH];

bool Read(uintptr_t address, void* output, size_t size) {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), output, size, &copied)
        && copied == size;
}

void Log(const char* message) {
    if (!logPath[0]) {
        DWORD n = GetModuleFileNameW(selfModule, logPath, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) { logPath[0] = 0; return; }
        wchar_t* slash = wcsrchr(logPath, L'\\');
        if (!slash || (slash - logPath) + 20 >= MAX_PATH) { logPath[0] = 0; return; }
        wcscpy(slash + 1, L"bo2vr-probe.log");
    }
    FILE* file = _wfopen(logPath, L"a");
    if (file) { fprintf(file, "[%lu] %s\n", GetTickCount(), message); fclose(file); }
}

BOOL CALLBACK LoadSystem(PINIT_ONCE, void*, void**) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n + 16 >= MAX_PATH) return TRUE;
    wcscat(path, L"\\xinput1_3.dll");
    systemXInput = LoadLibraryW(path);
    return TRUE;
}

FARPROC SystemFunction(const char* name) {
    InitOnceExecuteOnce(&systemOnce, LoadSystem, nullptr, nullptr);
    return systemXInput ? GetProcAddress(systemXInput, name) : nullptr;
}

bool Fingerprint() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    wchar_t* name = wcsrchr(path, L'\\');
    if (!name || _wcsicmp(name + 1, L"t6zm.exe")) return false;
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (base != 0x00400000) return false;
    IMAGE_DOS_HEADER dos = {};
    IMAGE_NT_HEADERS32 pe = {};
    if (!Read(base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 64
        || dos.e_lfanew > 0x100000 || !Read(base + dos.e_lfanew, &pe, sizeof pe)
        || pe.Signature != IMAGE_NT_SIGNATURE || pe.FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || pe.FileHeader.TimeDateStamp != 0x6a8332d9) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    unsigned char bytes[65536], digest[32];
    DWORD count = 0;
    while (ok) {
        if (!ReadFile(file, bytes, sizeof bytes, &count, nullptr)) { ok = false; break; }
        if (!count) break;
        ok = BCryptHashData(hash, bytes, count, 0) >= 0;
    }
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof digest, 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    const unsigned char expected[32] = {
        0xf6,0xf7,0x10,0x4a,0xf2,0xbd,0x0c,0x2b,0x93,0x1e,0xa1,0xe7,0x39,0x96,0x9e,0x85,
        0x84,0x5e,0x49,0x9a,0xff,0xbf,0x68,0x61,0x44,0xc6,0xb4,0x90,0xb3,0x44,0xab,0x1e
    };
    return ok && !memcmp(digest, expected, sizeof digest);
}

// Fail closed if the Steam interface is absent or Steam is logged on.
bool SteamOffline() {
    HMODULE api = GetModuleHandleW(L"steam_api.dll");
    if (!api) return false;
    using GetUser = void* (__cdecl*)();
    auto getUser = reinterpret_cast<GetUser>(GetProcAddress(api, "SteamUser"));
    if (!getUser) return false;
    void* user = getUser();
    uintptr_t table = 0, method = 0;
    if (!user || !Read(reinterpret_cast<uintptr_t>(user), &table, sizeof table)
        || !Read(table + sizeof(uintptr_t), &method, sizeof method) || !method) return false;
    // ISteamUser::BLoggedOn is vtable slot 1 for the game's interface.
    using LoggedOn = bool (__thiscall*)(void*);
    return !reinterpret_cast<LoggedOn>(method)(user);
}

bool PoseValid(const vr::TrackedDevicePose_t* poses, uint32_t index) {
    return index < vr::k_unMaxTrackedDeviceCount && poses[index].bDeviceIsConnected
        && poses[index].bPoseIsValid && poses[index].eTrackingResult == vr::TrackingResult_Running_OK;
}

void Sample() {
    if (!inspected) {
        inspected = true;
        fingerprintMatches = Fingerprint();
        if (!fingerprintMatches) {
            InterlockedExchange(&probeStatus, 1);
            Log("Unsupported host. XInput forwarding only; no engine or VR access.");
        }
    }
    if (!fingerprintMatches) return;
    DWORD now = GetTickCount();
    if (static_cast<LONG>(now - nextSample) < 0) return;
    nextSample = now + 1000;
    if (!SteamOffline()) {
        if (InterlockedExchange(&probeStatus, 2) != 2)
            Log("Steam is online or offline state is unknown. Engine/VR sampling withheld.");
        if (vrSystem) { vr::VR_Shutdown(); vrSystem = nullptr; vrAttempted = false; }
        return;
    }
    // Reads only: the engine code is decrypted by this point in the reference build.
    const unsigned char expected[] = {0xa1, 0x30, 0x50, 0x4c, 0x03, 0x50};
    unsigned char actual[sizeof expected];
    if (!Read(0x0041bb10, actual, sizeof actual) || memcmp(actual, expected, sizeof actual)) {
        if (InterlockedExchange(&probeStatus, 3) != 3)
            Log("R_RenderScene candidate bytes do not match. No engine/VR sampling performed.");
        return;
    }
    if (InterlockedExchange(&probeStatus, 4) != 4)
        Log("Exact executable and R_RenderScene prologue verified. Read-only sampling; no hooks installed.");
    // Keep this diagnostic module resident if the host attempts a dynamic unload after initialization.
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Sample), &pinned);
    if (!vrAttempted) {
        vrAttempted = true;
        vr::EVRInitError error = vr::VRInitError_None;
        vrSystem = vr::VR_Init(&error, vr::VRApplication_Background);
        char message[256];
        snprintf(message, sizeof message, "OpenVR initialization: %s", vr::VR_GetVRInitErrorAsEnglishDescription(error));
        Log(message);
    }
    uintptr_t cg = 0;
    struct Camera { float origin[3], padding, axes[9]; } camera = {};
    bool readable = Read(0x0105f75c, &cg, sizeof cg) && cg
        && Read(cg + 0x4d890 + 0x34, &camera, sizeof camera);
    // A readable pointer is not proof that a live game camera exists.
    bool plausible = readable;
    for (float value : camera.origin) plausible = plausible && std::isfinite(value);
    for (float value : camera.axes) plausible = plausible && std::isfinite(value) && std::fabs(value) <= 1.01f;
    for (int i = 0; i < 3; ++i) {
        float* axis = &camera.axes[i * 3];
        float length = axis[0]*axis[0] + axis[1]*axis[1] + axis[2]*axis[2];
        plausible = plausible && length > .9f && length < 1.1f;
    }
    bool head = false, left = false, right = false;
    if (vrSystem) {
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount] = {};
        vrSystem->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, poses,
            vr::k_unMaxTrackedDeviceCount);
        head = PoseValid(poses, vr::k_unTrackedDeviceIndex_Hmd);
        left = PoseValid(poses, vrSystem->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand));
        right = PoseValid(poses, vrSystem->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand));
    }
    char message[320];
    snprintf(message, sizeof message,
        "candidateCameraReadable=%d candidateCameraPlausible=%d origin=(%.3f %.3f %.3f) head=%d left=%d right=%d",
        readable, plausible, camera.origin[0], camera.origin[1], camera.origin[2], head, left, right);
    Log(message);
}

void Frame() {
    if (!TryAcquireSRWLockExclusive(&frameLock)) return;
    Sample();
    ReleaseSRWLockExclusive(&frameLock);
}
}

extern "C" DWORD WINAPI ProxyGetState(DWORD user, XINPUT_STATE* state) {
    using F = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
    auto function = reinterpret_cast<F>(SystemFunction("XInputGetState"));
    if (user == 0) Frame();
    return function ? function(user, state) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI ProxySetState(DWORD user, XINPUT_VIBRATION* vibration) {
    using F = DWORD (WINAPI*)(DWORD, XINPUT_VIBRATION*);
    auto function = reinterpret_cast<F>(SystemFunction("XInputSetState"));
    return function ? function(user, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI ProxyGetCapabilities(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps) {
    using F = DWORD (WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    auto function = reinterpret_cast<F>(SystemFunction("XInputGetCapabilities"));
    return function ? function(user, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" void WINAPI ProxyEnable(BOOL enable) {
    using F = void (WINAPI*)(BOOL);
    auto function = reinterpret_cast<F>(SystemFunction("XInputEnable"));
    if (function) function(enable);
}
extern "C" DWORD WINAPI ProxyGetDSound(DWORD user, GUID* render, GUID* capture) {
    using F = DWORD (WINAPI*)(DWORD, GUID*, GUID*);
    auto function = reinterpret_cast<F>(SystemFunction("XInputGetDSoundAudioDeviceGuids"));
    return function ? function(user, render, capture) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI ProxyGetBattery(DWORD user, BYTE type, XINPUT_BATTERY_INFORMATION* battery) {
    using F = DWORD (WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
    auto function = reinterpret_cast<F>(SystemFunction("XInputGetBatteryInformation"));
    return function ? function(user, type, battery) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI ProxyGetKeystroke(DWORD user, DWORD reserved, XINPUT_KEYSTROKE* stroke) {
    using F = DWORD (WINAPI*)(DWORD, DWORD, XINPUT_KEYSTROKE*);
    auto function = reinterpret_cast<F>(SystemFunction("XInputGetKeystroke"));
    return function ? function(user, reserved, stroke) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI ProxyGetStateEx(DWORD user, XINPUT_STATE* state) {
    using F = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
    auto function = reinterpret_cast<F>(SystemFunction(MAKEINTRESOURCEA(100)));
    if (user == 0) Frame();
    return function ? function(user, state) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" LONG WINAPI BO2VRGetProbeStatus() { return InterlockedCompareExchange(&probeStatus, 0, 0); }

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) { selfModule = module; DisableThreadLibraryCalls(module); }
    return TRUE;
}

