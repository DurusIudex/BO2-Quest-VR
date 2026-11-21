param(
    [string]$NdkPath = "$PSScriptRoot/../toolchain/android/ndk/android-ndk-r30"
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$bin = Join-Path $NdkPath 'toolchains/llvm/prebuilt/windows-x86_64/bin'
$compiler = Join-Path $bin 'clang++.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw "Android NDK compiler missing: $compiler" }
$out = Join-Path $root 'build/quest'
New-Item -ItemType Directory -Force $out | Out-Null
& $compiler --target=aarch64-linux-android29 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror -Wno-missing-field-initializers `
    -I "$root/vendor/openxr/include" -c "$root/src/quest/quest_input.cpp" -o "$out/quest_input.o"
if ($LASTEXITCODE -ne 0) { throw 'Quest input compilation failed' }
& (Join-Path $bin 'llvm-ar.exe') rcs "$out/libbo2vr_quest_input.a" "$out/quest_input.o"
if ($LASTEXITCODE -ne 0) { throw 'Quest library archive failed' }
Write-Output "Built $out/libbo2vr_quest_input.a (component only; not a playable APK)."
