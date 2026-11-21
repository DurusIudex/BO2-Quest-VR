# Native game research probe

`build/native/XINPUT1_3.dll` is a **read-only diagnostic**, not a VR conversion. It forwards the system XInput API and uses the game's XInput polling to inspect candidate engine data on the calling game thread. It does not install hooks, change the camera, modify weapons, submit eye textures, or create a playable VR experience.

## Current validation

Built and tested on 2026-11-20:

- The resulting DLL is 32-bit COFF/i386, matching the installed game.
- A separate 32-bit test process loads the DLL successfully.
- The harness checks the seven named XInput exports, ordinals 1–7, and ordinal 100.
- The proxy forwards an invalid controller query with the same result as the original system DLL.
- The harness is rejected as an unsupported host, and the probe does not initialize OpenVR there.
- Game execution is **not yet tested**. A separate standalone diagnostic verified live Quest 3S headset and controller poses.

## Gating and data

The native probe requires the exact `t6zm.exe` filename, expected image base, x86 PE header, timestamp and SHA-256 recorded in README. It next checks `ISteamUser::BLoggedOn`; missing or uncertain Steam state is rejected, as is an online session. It then checks six expected bytes at the candidate `R_RenderScene` entry before reading engine data.

Once those checks pass, it logs camera readability, a basic plausibility check, and head/hand pose validity at most once a second in `bo2vr-probe.log` beside the DLL. A matching prologue or plausible camera snapshot is supporting evidence, not full validation of a camera hook. The probe has no render callback, so snapshots are not guaranteed to coincide with scene submission.

OpenVR initializes as a background application. If that initialization fails, restart the game after fixing SteamVR. If Steam transitions online, engine sampling stops and the probe releases its OpenVR session.

The proxy stays loaded until game exit after successful initialization. Close the game before replacing or removing it. No worker threads or live code patches are created. No files have yet been placed into the BO2 game folder.

## Build and test

```powershell
.\tools\build-native.ps1
.\build\native\BO2VR.ProxyTest.exe
```

The compiler is pinned to [LLVM-MinGW 20261117](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261117), archive `llvm-mingw-20261117-ucrt-x86_64.zip` from that release. Extract it into `toolchain/`, retaining its root folder. The downloaded archive's SHA-256 is:

```text
4D002E330A0F703625AA5CCBC37E96F8F824413E163BFC7336774AABD2C3A412
```

The workspace-local toolchain does not alter Visual Studio or the system PATH. Its license is in the extracted root. The build treats our compiler warnings as errors and treats Valve's unmodified SDK as a system include.

The OpenVR header, import library and DLL use the same revision as the C# diagnostic, recorded in `vendor/openvr/REVISION`. Attribution for the engine research is in `src/native/native-notices.txt` and copied into the native build directory.

## Next integration test

Use an offline Steam session with the game closed. Check that the game directory has no existing XInput proxy or OpenVR loader before placing this probe there; preserve existing mods. Launch Zombies and inspect the probe log for fingerprint, Steam state and signature results. The unmodified Steam game's offline menu may not permit a playable match; resolving that is separate work, and no authentication or offline-menu patches are implemented here.

After the test, close the game and remove only this probe's files, retaining the logs. Restore Steam's prior online mode when the probe is no longer in the game directory.
