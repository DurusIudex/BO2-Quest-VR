#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <cstdio>

int main() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return 1;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return 1;
    wcscpy(slash + 1, L"XINPUT1_3.dll");
    HMODULE proxy = LoadLibraryW(path);
    if (!proxy) { printf("FAIL: load proxy, Windows error %lu\n", GetLastError()); return 1; }
    using State = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
    using Status = LONG (WINAPI*)();
    auto state = reinterpret_cast<State>(GetProcAddress(proxy, "XInputGetState"));
    auto status = reinterpret_cast<Status>(GetProcAddress(proxy, "BO2VRGetProbeStatus"));
    const char* names[] = {"XInputGetDSoundAudioDeviceGuids", "XInputGetState", "XInputSetState",
        "XInputGetCapabilities", "XInputEnable", "XInputGetBatteryInformation", "XInputGetKeystroke"};
    for (auto name : names) if (!GetProcAddress(proxy, name)) { printf("FAIL: missing %s\n", name); return 1; }
    for (unsigned i = 1; i <= 7; ++i)
        if (GetProcAddress(proxy, MAKEINTRESOURCEA(i)) != GetProcAddress(proxy, names[i-1])) {
            printf("FAIL: wrong ordinal %u\n", i); return 1;
        }
    if (!GetProcAddress(proxy, MAKEINTRESOURCEA(100)) || !status || status() != 0) return 1;
    XINPUT_STATE value = {};
    state(0, &value);
    if (status() != 1) { puts("FAIL: unknown host not rejected"); return 1; }
    if (!GetSystemDirectoryW(path, MAX_PATH)) return 1;
    wcscat(path, L"\\xinput1_3.dll");
    HMODULE original = LoadLibraryW(path);
    if (!original) return 1;
    auto originalState = reinterpret_cast<State>(GetProcAddress(original, "XInputGetState"));
    if (!originalState || state(99, &value) != originalState(99, &value)) {
        puts("FAIL: XInput forwarding differs"); return 1;
    }
    if (GetModuleHandleW(L"vrclient.dll")) { puts("FAIL: unsupported host initialized VR"); return 1; }
    puts("PASS: x86 DLL loads; names/ordinals verified; unknown host rejected; XInput forwarding preserved; VR not initialized.");
    FreeLibrary(proxy);
    FreeLibrary(original);
    return 0;
}

