# Install Zombies and Multiplayer data from your PC

This copies your installed Steam game data directly to the Quest over your local network. Nothing is uploaded to GitHub or the Internet. Your PC is needed only for setup; gameplay afterward is standalone. The app itself and later app updates still download from GitHub.

1. Install both BO2 Zombies and BO2 Multiplayer in Steam on the PC. The helper includes available game data, including installed maps; it does not download missing DLC or change unlock/progression state.
2. Install Python 3 and Git for Windows if they are not already installed. The helper uses Python's standard library and Git's bundled OpenSSL to create a temporary TLS certificate.
3. Extract the PC helper ZIP and double-click **Start-BO2-Transfer.cmd**. Wait while it hashes the game files. Keep that window open. If Windows asks about the firewall, allow the helper on your trusted local network.
4. Open **BO2 Quest Launcher** and select **Install Zombies + Multiplayer files from PC**. Connect the Quest to the same local network, enter the 12-character code shown on the PC, and optionally enter the PC's IPv4 address if discovery does not work.
5. Allow the installer to manage files when Quest asks. It writes `/storage/emulated/0/BO2`, verifies each file and shows progress. Keep this screen open until it completes. Reconnect with the helper's current code to resume interrupted transfers; verified files are skipped.
6. Select **Continue to game setup**. Install the VR app if needed. In the game's own launcher, allow its file access and select the BO2 folder, then choose Zombies or Multiplayer. You can stop the PC helper and turn off the PC once data installation completes.

For a Steam library elsewhere, run:

```powershell
py -3 tools/serve-game-files.py --game "D:\SteamLibrary\steamapps\common\Call of Duty Black Ops II"
```

The server expires after six hours by default. Starting it again generates a new pairing code and TLS certificate. It serves only allowlisted asset types from `zone`, `main`, `sound`, `video` and the localization files; it excludes saves, logs, credentials and PC executables. UDP discovery on port 27852 authenticates the TLS certificate with a challenge/HMAC using the code. File downloads use certificate-pinned HTTPS on port 27851 and require that same code. No account password is involved. Router client isolation, guest Wi-Fi, VPNs or firewall restrictions can prevent local pairing.

The Windows helper is source code requiring Python/Git, not a bundled Windows executable. The APK asks for all-files access because the separate VR game process needs a real shared-storage directory. Existing saves and settings are preserved. Partial data remains available for resume; a failed file verification does not replace the old destination file.
