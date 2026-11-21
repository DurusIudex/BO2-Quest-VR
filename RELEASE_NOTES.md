First public experimental Quest release, with a separate installer/updating launcher.

- Same-frame stereo rendering, head tracking, controller-directed aiming, rigid tracked weapon model, aim stabilization and controller bindings.
- Punch gestures, stationary menu handling and experimental room-code relay integration.
- Installer checks GitHub releases on launch, downloads game updates automatically after the first installation, verifies SHA-256/package/version/signing certificate, and uses Android's package installer. System confirmation may be required. It also offers launcher updates when available.
- Existing package ID and signing certificate preserve installed app data/settings. No BO2 game data is included; supply your own compatible files and select their folder in the game launcher.

Known limitations: intermittent loading/Awaiting Challenge stalls; fire-distortion mitigation still awaiting headset confirmation; Town can run around 31–38 FPS while a lighter indoor test reached approximately 57–62 FPS. Physical scope rendering and exact sight/muzzle calibration are unfinished. Multiplayer and online room transport are experimental, with no completed two-client match validation. No claim of a finished commercial-quality VR conversion.

Download `BO2-Quest-Installer.apk` for the small bootstrap launcher, or `BO2-Quest-VR.apk` for direct installation. Both are ARM64 Quest/Android builds. Sideload the installer once; future app downloads use GitHub. The game app remains labeled “BO2 Quest VR Dev” to preserve the existing test installation.
