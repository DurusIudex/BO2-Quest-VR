Version 0.2.0 adds installation of both Zombies and Multiplayer game data from the owner's Steam installation over local Wi-Fi.

- New **Install Zombies + Multiplayer files from PC** screen, temporary pairing code and certificate-pinned HTTPS transfer.
- Per-file SHA-256 verification, skip for already matching files, partial-download resume, progress and cancellation. Existing saves and settings are excluded from transfer.
- Includes `BO2-PC-Transfer.zip`: extract on the PC, run `Start-BO2-Transfer.cmd`, and enter its code in the Quest installer. Requires Python 3 and Git for Windows. The current owned installation contains approximately 6.51 GB in 167 allowlisted data/localization files.
- Both game modes must already be installed on the PC. No game assets are publicly hosted, no missing DLC is downloaded, and unlock/progression state is not changed. After setup the game runs standalone.
- Desktop integration tests pass for pairing, TLS pin rejection, incorrect-code rejection, resume, checksum failure, existing-file preservation and exclusion of saves/logs. The updated APK was installed and its main activity launched on Quest; a full in-app 6.5 GB transfer has not yet been verified on the headset.

Existing experimental VR features:

- Same-frame stereo rendering, head tracking, controller-directed aiming, rigid tracked weapon model, aim stabilization and controller bindings.
- Punch gestures, stationary menu handling and experimental room-code relay integration.
- Installer checks GitHub releases on launch, downloads game updates automatically after the first installation, verifies SHA-256/package/version/signing certificate, and uses Android's package installer. System confirmation may be required. It also offers launcher updates when available.
- Existing package ID and signing certificate preserve installed app data/settings. No BO2 game data is included; supply your own compatible files and select their folder in the game launcher.

Known limitations: intermittent loading/Awaiting Challenge stalls; fire-distortion mitigation still awaiting headset confirmation; Town can run around 31–38 FPS while a lighter indoor test reached approximately 57–62 FPS. Physical scope rendering and exact sight/muzzle calibration are unfinished. Multiplayer and online room transport are experimental, with no completed two-client match validation. No claim of a finished commercial-quality VR conversion.

Download `BO2-Quest-Installer.apk` for the small bootstrap launcher, or `BO2-Quest-VR.apk` for direct installation. Both are ARM64 Quest/Android builds. Sideload the installer once; future app downloads use GitHub. The game app remains labeled “BO2 Quest VR Dev” to preserve the existing test installation.
