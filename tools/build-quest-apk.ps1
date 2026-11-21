$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
Set-Location $root
function Check([string]$step) { if ($LASTEXITCODE -ne 0) { throw "$step failed ($LASTEXITCODE)" } }
$cmake = "$root/toolchain/cmake-4.5.1-windows-x86_64/bin/cmake.exe"
& $cmake -S src/quest -B build/quest-native -G Ninja "-DCMAKE_MAKE_PROGRAM=$root/toolchain/ninja/ninja.exe" "-DCMAKE_TOOLCHAIN_FILE=$root/toolchain/android/ndk/android-ndk-r30/build/cmake/android.toolchain.cmake" '-DANDROID_ABI=arm64-v8a' '-DANDROID_PLATFORM=android-29' '-DANDROID_STL=c++_static' '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
Check 'CMake configuration'
& $cmake --build build/quest-native --target questvr -j6
Check 'Native build'
New-Item -ItemType Directory -Force build/quest-java/classes | Out-Null
$sources = @(Get-ChildItem src/quest/java -Recurse -Filter '*.java' | ForEach-Object FullName)
$android = "$root/toolchain/android/platform35/android-35-ext15/android.jar"
& javac -source 8 -target 8 -classpath $android -d build/quest-java/classes @sources
Check 'Java build'
& jar cf build/quest-java/quest.jar -C build/quest-java/classes .
Check 'Java archive'
& ./toolchain/android/build-tools/android-16/d8.bat --min-api 29 --lib $android --output build/quest-java build/quest-java/quest.jar
Check 'DEX build'
& py -3 tools/prepare-quest-apk.py
Check 'APK preparation'
& java -jar toolchain/android/apktool.jar b build/quest-apk -o build/BO2QuestVR-unsigned.apk
Check 'APK assembly'
& py -3 -c 'import zipfile; z=zipfile.ZipFile("build/BO2QuestVR-unsigned.apk","a",zipfile.ZIP_DEFLATED); z.write("build/quest-java/classes.dex","classes4.dex"); z.close()'
Check 'DEX insertion'
& ./toolchain/android/build-tools/android-16/zipalign.exe -f -p 4 build/BO2QuestVR-unsigned.apk build/BO2QuestVR-aligned.apk
Check 'APK alignment'
& ./toolchain/android/build-tools/android-16/apksigner.bat sign --ks build/quest-dev.keystore --ks-key-alias questdev --ks-pass pass:android --key-pass pass:android --out build/BO2QuestVR-dev.apk build/BO2QuestVR-aligned.apk
Check 'APK signing'
& ./toolchain/android/build-tools/android-16/apksigner.bat verify --verbose build/BO2QuestVR-dev.apk
Check 'APK signature verification'
