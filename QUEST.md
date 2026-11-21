# Quest standalone work

## Installed baseline, 2026-11-20

- Device verified over ADB: Quest 3S, ARM64, Android API 34.
- Installed publisher APK: T6 Requiem Beta 2.3, version code 27, package `com.t6requiem.bo2arm`.
- Source: https://github.com/SoldadoGhost/T6Requiem-APK/releases/tag/v2.3.0-beta
- APK SHA-256: `a434d2f6fe7c48ad57c5341689f797190cbb4edb6f8d0b3d86aa681db0e1f1b9`, matches GitHub release digest. APK v2 signature verifies; signer is an Android debug certificate. This establishes file consistency, not an independent security audit.
- Copied the user's Steam `zone`, `main`, `sound`, `video`, and two localization files into `/sdcard/BO2` (4,202,551,223 bytes). Did not copy PC saves or modify the Steam installation.
- Launcher config points to `/storage/emulated/0/BO2`, Zombies mode. Previous config is saved in `reports/t6-config-before.txt`.
- User confirmed Solo TranZit gameplay after restarting. Logs independently show the server running, client state reaching 10, and the bus station assets loading. Two earlier attempts stalled at "Awaiting Challenge".

## Runtime warning resolved

The installed app logs that the guest execution runtime is absent and that its load test prepares but does not execute the guest. Disassembly independently confirms `t6android::engine_runtime_available(GameMode)` unconditionally returns false in both Beta 2.3 and the comparison download of Beta 2.2. Beta 2.2 was not installed.

Actual execution disproves that warning as a description of the launch path: logs show entry into `nx_guest_start`, game menus, map asset loading, and successful frame presentation. The capability flag/message is misleading. It must not be used as evidence that the engine cannot execute. A separate intermittent loading bug can prevent match entry.

## Awaiting Challenge diagnosis

The user's Solo Play attempt starts `zm_transit` with `zstandard` and queues the Survival fastfiles. The client enters connection state 5 while `sv_running` remains zero. The main map and localized map complete asset loading; the subsequent load does not finish.

A live, read-only thread snapshot pinpoints a busy loop in `DB_DecompressIOStream` at guest PC `0xa10395b4` (image base `0xa0000000`). The stack contains zlib return code `-3` and message `invalid block type`. Disassembly shows the loop rereads that unchanged error code until it equals zero or one; it cannot make progress after this error. The database thread is waiting in `DB_WaitWorkerCmd` while loading a sound bank. This explains the waiting screen; the underlying invalid compressed input could be a port decoding bug or asset-version incompatibility and has not been resolved.

SHA-256 checks on Quest match the local Steam copies for `so_zsurvival_zm_transit.ff`, `zm_transit.ff`, and `patch_zm.ff`. Recopying those identical files will not fix this stall. No game binary or validation check was patched. In particular, skipping the error loop would consume invalid asset data.

Evidence is saved in `reports/awaiting-challenge-game.log`, `reports/quest-lldb-stacks.txt`, `reports/quest-hang-symbols.txt`, and `reports/quest-decompression-snapshot.txt`. The snapshot helper source is `src/quest/thread_snapshot.c`. Debuggers were detached and temporary debugger executables removed from Quest; no persistent debugger or port forward was left running.

The first TranZit retry also hung: a second worker snapshot captured the same `invalid block type` error, this time before the main `zm_transit` asset load completed. This is not confined to Survival assets and suggests an intermittent load/decode problem.

A later fresh launch reached TranZit gameplay. The user confirmed it and `reports/quest-gameplay-confirmed.log` records `sv_running 1`, connection transitions 5 -> 7 -> 6 -> 9 -> 10, and successful `zm_transit_gump_busstation` loading. An experimental `r_smp_worker_threads 1` setting had been applied before this launch, but the log explicitly rejects 1 as an invalid value and starts TWO workers. The setting cannot be credited for the successful launch; it was removed from the saved config afterward, preserving other current settings. The original pre-experiment config is in `reports/config_zm-before-worker-test.cfg`.

Restarting allowed this one successful load; it is a workaround with limited evidence, not a permanent repair of the decompression bug. The game subsequently returned to its menu. No binary patches were made.

There is no public engine source in the T6Requiem-APK repository. Locally decompiled launcher code and binary analysis are private research under ignored `research/`; they are not a source release from the maintainer.

## Current development build

The original `com.t6requiem.bo2arm` remains installed. The separately signed development package is `com.bo2vr.questdev`, labeled **BO2 Quest VR Dev**. Build it with `tools/build-quest-apk.ps1`; the output is `build/BO2QuestVR-dev.apk`. The preparation script copies the private APK decode, inserts our Java/native code, updates both signer checks to the local development certificate, and disables upstream auto-updates in the clone. Game data remains shared at `/sdcard/BO2`.

User-verified milestones:

- Phone touch controls are hidden. Their hidden publisher was disabled in the clone so it cannot repeatedly clear Quest input.
- OpenXR frame delivery and Quest thumbstick/button controls work, including navigating menus and reaching gameplay.
- The controller-binding settings page displays in the headset. It contains saved button remapping, stick swapping, deadzone, reset, and now separate punch mappings. Actual UI save/reload still needs device verification; native configuration parsing is tested.
- Switching the clone from its default DXVK renderer to the baseline's direct Vulkan renderer, replacing per-frame queue-idle waits with a reuse fence, and handling startup resolution changes resolved the user's reported black screen and lag spikes. Cold shader compilation still appears in logs; no permanent performance guarantee is established.
- Menus now use a fixed pose in local tracking space; the user confirmed they remain stationary.

Stereo remains experimental. Prior headset tests produced distorted geometry and mismatched eyes, including a mono diagnostic. Static analysis subsequently found the refdef axes are packed vec3s (0x44, 0x50, 0x5c), not 16-byte rows; the incorrect stride corrupted the camera. The corrected layout is installed and passes identity, rotation orthogonality, translation, anchor, adjacent-field preservation, and aim/body rotation tests. The view setup callback now preserves its returned pointer, and the stereo rebuild restores the visibility-cell index cleared by R_SetViewParmsForScene. The latest build defaults to same-frame stereo; Settings also offers head-tracked mono and a stationary screen. **The corrected build has not been visually validated. Do not describe proper VR as completed.** Exact association between backend frames and frontend poses, shared stereo rendering passes, and HUD placement remain areas to validate.

Tracked-controller aiming is now implemented behind guarded guest hooks for both Zombies and Multiplayer. Right-hand orientation supplies command aim angles; the weapon placement uses its tracked pose; the camera removes the applied hand rotation before adding head movement. An OpenXR direction marker replaces the original crosshair while VR tracking is active. It is a fixed-distance marker, not a collision-tested impact point. The engine still originates shots from its normal player viewpoint, so near-range hand/muzzle parallax and room-scale shot origin are unresolved. Aiming and weapon placement need gameplay verification. Punch gestures from either hand remain configurable and pass simulated tests. Focus loss clears controller/gesture tracking; stationary screen mode retains normal stick aiming. The user explicitly deferred the intermittent Awaiting Challenge issue.

The last verified flat-panel fallback APK is `build/BO2QuestVR-panel-resize-fixed.apk`. Stereo is enabled at launch only when the clone's private `files/quest_stereo.request` exists; removing it and restarting disables the experimental camera. The original app is also a recovery path.

Live logs are captured in `reports/quest-vr-live.log`. Captures `reports/stereo-world.png` and `reports/stereo-world2.png` document earlier tests; the latter was taken after returning to the menu. Current tests: `tools/test-quest-input.ps1` covers action focus/reset, mapping parsing/analog values/deadzones, punch rejection for body translation and lost tracking, and camera/aim math. Results are in `reports/quest-input-test.txt`.

## Online rooms and Multiplayer (November 21 development build)

### November 21 morning stereo follow-up

The user tested the packed-axis fix: eyes still did not match and the world still deformed with head motion, although less severely. A direct headset capture (`reports/quest-current.png`, before the next install) showed markedly different scene regions in each eye.

Further binary inspection found that `RB_RenderThreadFrame` calls `RB_Draw3D` once, and its normal path draws only `viewInfoIndex`. Creating two frontend views alone therefore does not draw two eyes. `GfxViewInfo` also has separate render/display/composite viewports at 0x150/0x160/0x170; the composite viewport was derived from an independent, unchanged refdef viewport. Finally, the compositor previously read the latest frontend pose instead of the pose associated with the backend frame being presented.

The follow-up build adds guarded `RB_RenderThreadFrameArm` and `RB_Draw3D` hooks, explicitly draws both recorded view indices, copies each eye's render viewport to its final composition viewport, and consumes pose metadata by the exact `GfxBackEndData` pointer. Metadata is consumed once to avoid stale stereo on buffer reuse. A regression test covers an older backend frame arriving after a newer frontend pose, missing frames, and buffer reuse. Camera/input/gesture/mapping and frame-association tests pass; the APK builds and installs successfully. Runtime hooks and menu startup were confirmed. The user tested this follow-up and still reported misaligned eyes and world deformation. It did not fix stereo.

The launcher now has **Online rooms** with Zombies/Multiplayer selection, a custom 6–24 character code, Host, Join, and Play offline. Hosting queues a private party; guests queue the port's `join` command to the virtual host. Static analysis verified this port's NetDirect lobby transport uses 4976–4979; it is distinct from retail port 3074. Code rooms disable public index publication, STUN, UPnP, and DHT requests for that session. Fully close the game before changing rooms or settings; native connection settings are loaded once per game process.

The user's owned Steam Multiplayer installation completed. Its added `zone` and `sound` assets were copied to `/sdcard/BO2`; video assets were already present. Both guest binaries have generated, prologue-checked camera/input profiles. **Multiplayer gameplay has not been verified on the Quest.**

The authorized droplet now runs an isolated `bo2vr-relay` systemd service as `bo2relay`, from `/opt/bo2vr-relay/relay.py`. HTTPS control is at `https://159-223-145-188.sslip.io/bo2vr/`, proxied to loopback port 18950. Authenticated UDP uses port 27850. Existing services remain in place; Caddy's previous configuration was backed up. Deployment source is `tools/deploy-relay.sh`; service implementation and tests are in `src/relay/`. No game assets are stored by the relay.

Rooms support four Zombies peers or eighteen Multiplayer peers. Codes act as invitations; per-peer random tokens authenticate UDP. A host disconnect expires its room after 180 seconds without activity; rooms also expire after 24 hours. Guests cannot send to other guests through this relay; game traffic uses the host. Rooms are memory-only and disappear on service restart. The engine's full lobby/party flow, including any required peer-to-peer traffic or embedded addresses, is not yet validated.

Validated:

- Six relay unit tests cover code/mode/capacity checks, expiry, token revocation, isolation, malformed packets, and rate limits.
- Public HTTPS create/join plus bidirectional UDP passed (`tools/test-live-relay.py`).
- The actual ARM64 native socket adapter ran on the Quest, exchanged packets with a separate client through the public droplet, translated virtual peer addresses, and passed direct UDP and oversized/truncated datagram checks (`tools/test-quest-relay.py`, result `reports/quest-relay-test.txt`). This includes fortified libc socket entry points used by the host. Test executables/credentials were removed afterward.

**These are transport tests, not evidence of a successful two-player BO2 match.** Full host/guest lobby joining and map synchronization remain unverified. Room tokens are stored only in the development app's private files. Play offline clears that app's saved connection; an abandoned code may remain occupied until its expiry.

## Morning headset check

The final experimental APK is installed as **BO2 Quest VR Dev**. The Quest is charged, but its sensor-lock screen requires a physical power-button press to enable cameras/microphones. ADB launch requests are currently blocked behind that lock. Proximity was restored to normal; no privacy lock was bypassed. This prevented visual verification and the latest Multiplayer launch smoke test.

1. Press the headset's power button, then open BO2 Quest VR Dev from Unknown Sources.
2. Start a Solo Zombies match. Check head rotation, both eyes, then stick turning. If the view is unstable, close the game and choose **Stationary game screen** in Quest controller bindings.
3. If geometry is stable, test right-controller aiming independently of your head and punch with each hand. Reticle impact depth and muzzle-origin accuracy are still experimental.
4. Change one binding, Save, close/reopen Settings, then restart the game to verify persistence.
5. For online testing, fully close the game first. A host creates a code and waits in Private Match; a second device with the same development build selects the same mode and joins that code. Start the map after the guest joins. This full flow still requires validation.

The original T6 Requiem package remains available as recovery. No test room credentials or queued developer map command are intended to remain active.

Dependencies: OpenXR SDK commit `24a1f2b92d212501a2b1b8d30ffd53abf55fcfa8`, Android loader AAR 1.1.54, Dobby commit `9c85e74f92eda36b99ddb0fddb17a9df2278a4d3` (Apache 2.0). Dobby's Android log header include was moved to file scope for NDK r30 compatibility. Local signing key stays in ignored `build/`.

## OpenXR input component

`src/quest/quest_input.cpp` implements an OpenXR action component with left/right aim and grip poses, analog triggers/grips, thumbsticks, A/B/X/Y buttons, stick clicks, and haptics. It clears input when unfocused and on runtime query failures. Pose validity flags are preserved for the eventual game adapter to check.

The component borrows the instance/session created by `quest_runtime.cpp`. It creates and attaches its own action set once; its lifetime ends before the session/instance. The development APK includes the input component, Vulkan interception, frame delivery, bindings page, and experimental guest camera integration.

Build:

```powershell
./tools/build-quest.ps1
./tools/test-quest-input.ps1
py -3 tools/inspect-android-vr.py research/t6requiem-apk/assets/guest/t6zm_guest.elf --out reports/android-vr-symbols.json
```

Validation: Android NDK r30 compiles the component as ARM64 API 29 with warnings treated as errors. Desktop simulated-runtime tests pass for focus loss, input reset, inactive poses, haptic parameter validation, and resource cleanup after failed initialization. These tests do not establish headset operation or game compatibility.

## Candidate integration points

The guest is ELF32 with AArch64 instructions and 32-bit pointers, loaded by a custom runtime. It is not an ordinary ARM64 shared library. Its SHA-256 is `ea0004e48e55d75da037fd951726faf648a9a595f61ae3a8b05a84447ed0eeba`.

The read-only inspection script maps these symbol values in the guest image:

| Function/data | Guest virtual address |
| --- | --- |
| `R_RenderScene(refdef_t*, GfxViewParms*)` | `0xb30a60` |
| `CG_CalcViewValues` | `0x5cad84` |
| `CG_DrawActiveFrame` | `0x3b9d20` |
| `CL_CreateNewCommands` | `0x638ef4` |
| `cgArray` | `0x1d41870` |

The development build verifies executable mappings and function prologues before hooking `R_RenderScene` and `R_SetupRenderSceneViewParms`. The live image base was verified as `0xa0000000`. `guest_bridge.S` keeps the guest's low-address stack and switches between guest TLS and host TLS (host TLS pointer at guest TLS + 0x10) around callbacks. The hook executed and logged a valid camera at runtime, but correct stereoscopic rendering remains unverified.

## November 21 final-output investigation

The opt-in Vulkan readback (`files/quest_capture.request`) captured the game image before OpenXR reprojection. `reports/quest-raw.png` is one continuous full-width camera view, not two eye images. The previous stereo path incorrectly submitted the left and right halves of this one view as different eyes. Backend camera captures (`reports/quest-camera.bin`, analyzed by `tools/inspect-quest-cameras.py`) show orthonormal, matching orientations and separate eye origins. Backend logs confirm both view indices were dispatched.

Disassembly identifies why frontend half-viewports were insufficient: scratch target 4 uses full-target viewport behavior, and `RB_BloomLDR` explicitly selects a full-size backbuffer viewport for its final filter. The new experimental build keeps each eye's scratch render full size and maps only physical backbuffer viewport/scissor calls to the corresponding half during that eye's backend draw. Shader projection and full-eye texture coordinates remain unchanged. The hooks are prologue-guarded for both game modes. Build, signing verification, and camera/input/gesture/frame-association tests pass; the APK is installed. Raw-image verification passed: `reports/quest-packed.png` shows two complete corresponding room views, unlike the previous continuous single image. Runtime logs confirm target-2 packing. A second capture after turning also preserves both eye views. Feature matches in the first capture have median absolute vertical disparity 0 px and 95th percentile 1 px (729 matches); this is a stereo-image check, not a measurement of headset motion stability. In-headset head-motion stability is still awaiting user confirmation. The normal HUD still spans the combined surface and needs separate VR placement.

Captures are opt-in and cause a one-time GPU fence wait; normal frames retain the previous asynchronous copy path. `files/quest_camera_capture.request` separately saves the two backend view structures. The read-only thread snapshot helper now supports bounded memory reads for renderer inspection and always detaches afterward.

## November 21 clarity, weapon projection, and stats update

The user confirmed that the previous final-output packing fix now aligns the eyes correctly. Preserve that viewport/scissor packing path. This build defaults to a 2560×1440 combined output (1280×1440 per eye), four times the pixel count of the previous 1280×720 combined output. Quest settings offers Performance, Balanced, and High; the choice is saved with the controller bindings and applied to the launcher's resolution preference before it writes engine_options.cfg. The installed app's configuration confirms 2560×1440. Actual swapchain size and gameplay performance at this setting remain unverified because the headset was asleep; the user will test later.

Weapon rendering now disables the separate flat-game viewmodel FOV override while generating each VR scene, restoring it afterward. Disassembly of R_GenerateSortedDrawSurfs shows this makes the alternate weapon view inherit the world projection. The controller placement hook no longer depends on the timing of the input-command aimApplied flag. CG_AddPlayerWeapon copies that placement into the viewmodel pose. This targets head-induced weapon drift, but needs an in-headset stationary-controller test. The existing combined gun/arms model follows the right controller; independent left-arm skeletal tracking is not implemented.

Both verified guest profiles now expose cgArray. The runtime uses the engine's GetIsIntermission condition (cg snapshot pm_type == 6) to choose a stationary, full-width menu/stats quad at match end. This preserves normal menu interaction instead of freezing a screenshot. The stats transition still requires an actual end-of-match test.

Input, remapping, punch, camera math, and backend frame-association tests pass. The APK built, passed signature verification, and installed successfully. Zombies launch was requested; the sleeping headset paused startup before the Vulkan swapchain. No claim of on-device validation of these three changes yet.

## Black-screen regression follow-up

The user subsequently reported black gameplay. A capture taken after the match had already ended shows the game's black loading screen with the BO2 logo (`reports/quest-black-screen.png`), and the saved game log records a post-match shutdown/load stall. It does not establish what caused the earlier black gameplay. The older camera binary on-device was stale (08:48); it must not be treated as evidence for the new build.

The weapon FOV override introduced above has been removed as an isolation rollback. The new APK passed build/signature checks and was installed. A fresh raw capture confirms the Zombies menu renders after restart (`reports/quest-rollback-menu.png`). Gameplay confirmation is pending. Controller placement and intermission detection remain installed.

The native host explicitly rejects 2560×1440 and falls back to 1280×720 despite the requested launch configuration. The previous resolution claim describes the requested setting, not actual output. The resolution selector still needs to be restricted to supported values or the host restriction addressed; do not claim the high-resolution upgrade is working.

## Supported resolution and default arms

The user confirmed the rollback restored gameplay, then requested higher resolution and default arm placement with controller-directed shooting. The launcher now defaults to the supported 1920×1080 combined resolution (960×1080 per eye, 2.25× the previous pixel count). Unsupported 2560×1440 has been removed from the selector; existing unsupported saved values migrate to 1920×1080. Performance mode remains 1280×720 combined.

The controller-position override of the gun/arms has been replaced with a rigid rebase of the original animated viewmodel placement from the engine aim camera to the tracked center camera. This preserves its normal camera-relative offset, orientation, and animations while CL_FinishMove continues to serialize the right controller's aim into the game command. The compositor reticle still uses the controller's direction. It is a fixed-distance direction marker, not a collision-depth hit indicator; bullet spread and room-scale origin differences remain relevant to exact impact alignment. Weapon projection overrides remain removed.

Tests cover viewmodel local-position preservation and round trips across pitch, yaw, and translation, plus existing input, mapping, punch, camera, and frame-association tests. All pass. The APK built, passed signature verification, and installed. Headset confirmation of clarity, arm placement, and actual shot alignment is pending.

## Rigid weapon aiming and stabilization

The user confirmed the supported-resolution/default-arms build works. New behavior rotates the complete animated gun/arms viewmodel toward the right controller's filtered orientation, retaining the original model offsets and scale. This is one rigid model, not independent left/right hand IK. Controller translation is deliberately not used to pull the arms. Viewmodel projection/VP/inverse matrices are copied from each eye's valid world matrices after scene generation; refdef FOV fields are not zeroed. This is intended to remove the flat viewmodel lens distortion, but on-device confirmation is pending.

AimStabilizer applies adaptive, normalized quaternion smoothing once per timestamped XR input sample. Shots, reticle, and the frontend viewmodel consume that shared filtered orientation. It uses shortest-arc quaternion signs, resets on focus/tracking loss or time discontinuities, and reduces damping for large turns. Quest settings provides Off/Light/Balanced/Strong (Balanced default). Head tracking and punch gesture detection remain unsmoothed. Automated tests pass for tremor suppression at 60/72/90/120 Hz, fast-turn convergence, quaternion sign changes, focus reset, and disabled smoothing, alongside existing viewmodel/input/camera/frame tests. APK build and signing verification pass; installed on Quest.

## 60 FPS investigation and changes

The saved pre-change log (`reports/quest-fps-before.log`) confirms actual internal 1920×1080 rendering. The game already had com_maxfps=60 and r_vsync=0. Active gameplay measured roughly 44–58 FPS with GPU work reaching about 21 ms; menu measurements near 55 FPS should not be used as gameplay evidence. There was no 30 FPS config cap to raise.

The new runtime enables XR_EXT_performance_settings only when advertised and requests SUSTAINED_HIGH for CPU and GPU when the XR session begins. Results are logged and failures do not prevent rendering. Dynamic shadows were disabled in the Quest's Zombies config (sm_enable=0), preserving resolution and the 60 FPS cap. Original config backup: reports/quest-config-before-60fps.cfg. Request acceptance, post-change gameplay FPS, and visual quality still need live validation; no locked-60 claim is established. The device was asleep immediately after installation.

## Live validation and pending headset checks

## Fire comfort and physical sight positioning

The user reported severe view disruption when stepping in fire, and requested physical iron sights and magnified scopes like Population: One. The installed follow-up hooks the verified RB_FlameFX and CG_ShakeCamera functions. RB_FlameFX ends in RB_SplitScreenFilter/RB_FullScreenFilter and draws the screen-space burn warp to target 2. It is now skipped for rendered VR frames; game camera shake is suppressed during tracked VR gameplay. World flame geometry, burn gameplay and damage functions are not modified. Both ZM/MP function prologues are generated and checked before hooking. In-headset fire verification is pending.

Physical weapon mode now translates the complete rigid model with the right grip position as well as rotating with the shared stabilized aim. It applies a provisional camera-to-grip offset (-9 cm X, +15 cm Y, +20 cm Z in controller axes); this is an initial calibration, not a verified per-weapon grip transform. Position tracking loss falls back to the previous head-relative position. Quest settings includes Position and rotation / Rotation only. Tests confirm controller translation produces equal model translation without scale changes; all existing tests and the APK build/signature verification pass. Installed and Zombies launch requested, but the headset is asleep.

Physical weapon translation is only a first step toward physical sights. Exact barrel/sight-to-shot alignment still needs per-weapon calibration and controller-origin bullet handling; current gameplay commands still use engine shot origins. Magnified scope rendering is NOT implemented. Do not describe this build as complete Population: One-style aiming. The user has been asked which scoped weapon is available and to test fire/physical grip alignment.

Scope investigation: CG_DrawAdsOverlay is the flat overlay path; CG_ScopeIsOverlayed calls CG_GetWeapReticleZoom. The engine also has R_RenderExtraCam (0xb306b4 ZM), R_ExtraCam_SetSceneParms (0x462be8), R_SetSceneComposition (0x483f8c) and R_InitExtraCamRenderTargets (0x111b0b0). The latter calls the implemented t6rt allocator at 0x111a360, creating targets 29/30/31 etc. This is a potential separate magnified scene path, preferable to enlarging the existing eye texture (which includes the gun). Disassembly is saved in reports/quest-extra-camera.txt, quest-scope-overlay.txt and quest-render-scope-entry.txt. No extra-camera hooks have been installed.

The user has only the starting pistol. A pistol/fire test was requested after installation. Subsequent launches hit asset-loading failures before any Camera sample/VR gameplay log: crash PCs 0xa0c0863c and LR 0xa1039c4c resolve to memcpy and DB_LoadXFileData. Saved crash log: reports/quest-fire-crashes.txt; loading log: reports/quest-physical-sights-loading.log. This resembles the known intermittent loader problem but does not prove the same root cause. Do not attribute it to the new fire hooks without evidence. Live fire suppression and physical sight alignment remain unverified. Pending camera capture request was removed to avoid an unexpected diagnostic stall on a later launch.

Live validation: the restarted session (PID 778) reached gameplay. Both sustained-performance calls returned XR_SUCCESS, and the runtime confirmed 1920×1080. `reports/quest-aim-gameplay.png` shows coherent stereo with the gun/arms rendered together and no obvious stretched geometry in the captured pose. Fresh camera dump `reports/quest-aim-camera.bin` confirms the weapon and world projection/VP matrices now match for both eyes. The shot command logs change with controller pitch/yaw. Four consecutive gameplay timing intervals measured 61–62 FPS, with P50 15.9–16.3 ms, P95 18.1–19.8 ms, GPU 10.97–13.33 ms. One interval had a 75.3 ms outlier while pipelines were being compiled. These are short measurements, not a locked-60 guarantee for all maps or long thermal runs. Subjective model alignment and smoothing feel are awaiting the user's feedback. A copy of this APK is saved as build/BO2QuestVR-aim-stabilized-60fps.apk.

The extended sample includes a heavier interval averaging 57 FPS (P50 17.4 ms, P95 21.3 ms), followed by 61 FPS. Report approximately 57–62 FPS for this test rather than a locked 60.

Next validate subjective controller aim/weapon placement, stationary stats, punch melee, and saved settings on device. Verify Multiplayer startup and a real two-client room join/map transition. Correct collision-depth reticle, shot origin, HUD placement, and any remaining frame/pose mismatch revealed by testing. Independent left-arm skeletal tracking remains outstanding.

## Fire follow-up: first suppression was insufficient

The user stepped into Town fire and reported that the view still distorts. The running log confirmed RB_FlameFX suppression was active, so that hook alone does not solve the issue. A captured frame has orthonormal eye camera bases, but a still capture does not establish stability during damage. Town measured approximately 31–38 FPS at the current resolution (CPU 26–32 ms, GPU 20–23 ms); the earlier 57–62 FPS indoor sample does not describe this map/load.

The next build additionally suppresses CG_PerturbCamera, BG_CalculateViewMovement_DamageKick and DrawShellshockBlend only during tracked VR gameplay. Local disassembly shows these respectively rotate the camera axes, add damage pitch/roll to the view-angle accumulator, and draw saved-screen blurred/flashed images. The damage accumulator is left intact rather than zeroed; game damage and shellshock state/audio updates are outside these functions. Both guest profiles include generated prologue guards for the new hooks. Existing stereo packing, projection, resolution and weapon placement are unchanged. Input, pose, viewmodel, stabilization and frame-association tests pass; APK build and signing verification pass. This is a second targeted mitigation requiring live verification, not a confirmed fire fix. Magnified optics and calibrated physical sight alignment remain unfinished.
