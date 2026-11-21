$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
Set-Location $root
function Check([string]$step) { if ($LASTEXITCODE -ne 0) { throw "$step failed ($LASTEXITCODE)" } }
$out = "$root/build/installer"
$bt = "$root/toolchain/android/build-tools/android-16"
$android = "$root/toolchain/android/platform35/android-35-ext15/android.jar"
New-Item -ItemType Directory -Force "$out/classes","$out/dex" | Out-Null
$release = Get-Content release.json -Raw | ConvertFrom-Json
[xml]$manifest = Get-Content src/installer/AndroidManifest.xml
$ns = 'http://schemas.android.com/apk/res/android'
$manifest.manifest.SetAttribute('versionCode',$ns,[string]$release.installerVersionCode) | Out-Null
$manifest.manifest.SetAttribute('versionName',$ns,[string]$release.version) | Out-Null
$manifest.Save("$out/AndroidManifest.xml")
& "$bt/aapt.exe" package -f -M "$out/AndroidManifest.xml" -I $android -F "$out/base.apk"
Check 'Installer resources'
$sources = @(Get-ChildItem src/installer/java -Recurse -Filter '*.java' | ForEach-Object FullName)
& javac -source 8 -target 8 -classpath $android -d "$out/classes" @sources
Check 'Installer Java'
& jar cf "$out/installer.jar" -C "$out/classes" .
Check 'Installer archive'
& "$bt/d8.bat" --min-api 29 --lib $android --output "$out/dex" "$out/installer.jar"
Check 'Installer dex'
& py -3 -c 'import zipfile; z=zipfile.ZipFile("build/installer/base.apk","a",zipfile.ZIP_DEFLATED); z.write("build/installer/dex/classes.dex","classes.dex"); z.close()'
Check 'Installer package'
& "$bt/zipalign.exe" -f -p 4 "$out/base.apk" "$out/aligned.apk"
Check 'Installer alignment'
& "$bt/apksigner.bat" sign --ks build/quest-dev.keystore --ks-key-alias questdev --ks-pass pass:android --key-pass pass:android --out build/BO2-Quest-Installer.apk "$out/aligned.apk"
Check 'Installer signing'
& "$bt/apksigner.bat" verify --verbose build/BO2-Quest-Installer.apk
Check 'Installer signature'
