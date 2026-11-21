# BO2 Quest VR

Experimental standalone Quest VR integration for T6Requiem, with a small installer and an updating launcher. **BO2 game files are required separately.** This is an unofficial project, not affiliated with Activision, Treyarch or Meta.

## Download

[Download the Quest installer APK](https://159-223-145-188.sslip.io/bo2-install/BO2-Quest-Installer.apk)

[GitHub releases and direct game APK](https://github.com/DurusIudex/BO2-Quest-VR/releases/latest)

Sideload **BO2-Quest-Installer.apk** once, then open **BO2 Quest Launcher** from Unknown Sources. Select Install game and confirm any Quest installation prompts. The launcher downloads the VR app from this repository's releases. In the game's launcher, select the folder containing your own compatible BO2 data.

The launcher automatically checks for updates when opened and downloads newer game builds when automatic updates are enabled. It offers updates for itself too. Android may require an installation confirmation. It preserves existing game files and settings, and can launch an already installed game offline. Updates are verified by package name, version, SHA-256 and signing certificate. No GitHub login is required on the Quest.

## Current state

Standalone Zombies gameplay and stereo head tracking have been tested on Quest 3S. The development build includes controller-directed aiming, rigid tracked weapon positioning, aim stabilization, configurable controller bindings and punch gestures. Menus use a stationary screen.

This remains an experimental build:

- Loading can intermittently hang at Awaiting Challenge or during decompression.
- The latest fire-distortion mitigation still needs in-headset confirmation.
- Performance varies: a lighter indoor test measured approximately 57–62 FPS; Town near fire measured around 31–38 FPS. It is not a locked 60 FPS build.
- Exact physical sight/muzzle alignment, magnified optics, independent left-arm tracking and proper VR HUD placement remain unfinished.
- Multiplayer and custom-code room relay integration are experimental. A complete two-client online match has not been verified.

## Source and builds

`src/quest` contains the OpenXR integration, rendering hooks, input, bindings and tests. `src/installer` contains the Android installer. `src/relay` contains the room-code relay. The legacy PC diagnostics remain under `src/native` and `src/Check.cs`.

See [BUILDING.md](BUILDING.md) for dependency layout, signing requirements, build commands and release publishing. Private signing keys, game data, downloaded baseline packages, toolchains and device reports are excluded from Git. OpenXR and Dobby are pinned submodules with their own licenses. The OpenVR SDK files retain Valve's license in `vendor/openvr/LICENSE`.

[QUEST.md](QUEST.md) and [NATIVE.md](NATIVE.md) are chronological development notes and include superseded experiments. [Release notes](RELEASE_NOTES.md) describe this release's limits.
