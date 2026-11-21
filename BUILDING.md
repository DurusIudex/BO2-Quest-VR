# Building and publishing

The repository contains the original VR integration, Android installer, relay and tests. It does not include the T6Requiem baseline package, BO2 game files, downloaded toolchains, device captures, or signing keys. The APK release contains the modified Android runtime plus VR integration; it does not contain BO2 map/game data. T6Requiem and Call of Duty remain their respective owners' work. This is an unofficial experimental project.

Clone with `git clone --recurse-submodules https://github.com/DurusIudex/BO2-Quest-VR.git`. OpenXR and Dobby are pinned submodules. Apply `git -C vendor/dobby apply ../../tools/patches/dobby-android.patch` once for NDK r30 compatibility. Their licenses remain in their repositories; the legacy OpenVR files retain Valve's license in `vendor/openvr/LICENSE`.

The current Windows build scripts expect Java/Javac, Python 3, Android SDK platform 35 (`toolchain/android/platform35/android-35-ext15/android.jar`), build tools (`toolchain/android/build-tools/android-16`), NDK r30, CMake 4.5.1, Ninja, apktool 3.0.3, and the OpenXR Android loader AAR 1.1.54 unpacked into `toolchain/android/openxr-loader`. Paths are explicit in the build scripts. These dependencies must be installed separately; this is not a one-command clean checkout build.

For the game APK, provide the compatible T6Requiem Beta 2.3 baseline unpacked with apktool under `research/quest-apk`, and its guest ELFs under `research/t6requiem-apk/assets/guest`. The scripts verify and patch this particular layout and certificate marker; other versions are not assumed compatible. Generated guest profiles are checked in so hook addresses and prologue guards can be reviewed.

The maintainer's private signing key is intentionally excluded. Existing users require updates signed with that same key. For an independent fork, generate your own Android key/certificate, export its DER certificate to `build/quest-dev.der`, use your own signing configuration in both build scripts, and update the public certificate pin and repository in `ReleasePolicy.java` and `release.json`. Forks with a different signer cannot replace the existing installation in place.

Maintainer release sequence:

1. Increase `version` and `gameVersionCode` in `release.json`; increase `installerVersionCode` when installer code changes. Do not replace the assets of an already published version.
2. Run `tools/test-quest-input.ps1`. Compile/run `src/installer/test/ReleasePolicyTest.java` alongside `ReleasePolicy.java`; run `python tools/test-game-transfer.py` for the real Java-client/Python-server transfer integration test. Relay tests run with `python -m unittest discover -s src/relay`.
3. Run `tools/build-quest-apk.ps1`, `tools/build-installer.ps1`, then `python tools/make-release.py`. Inspect the APKs and test on a headset.
4. Commit and push the reviewed source. With Git Credential Manager signed in, run `python tools/publish-github-release.py`. It uploads the two APKs, PC helper ZIP, update manifest and checksums to a draft and publishes only after successful uploads.
5. Copy the installer APK to the server's existing download location using an atomic rename. The launcher itself obtains app updates from `releases/latest/download/update.json` on GitHub.

The update feed pins exact tagged asset URLs, sizes and SHA-256 hashes. The installer additionally verifies the APK's package, version and embedded signing certificate, rejects downgrades, bounds downloads, requires HTTPS, and restricts redirect hosts. It checks when opened, not as an always-running background daemon. Quest/Android can require user confirmation. Failed downloads leave the current app installed and available offline. No GitHub credential is embedded in either APK.

`QUEST.md` and `NATIVE.md` are chronological research notes, including superseded experiments. Use the README and release notes for current feature status.
