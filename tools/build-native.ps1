$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$compilerPath = Join-Path $projectRoot 'toolchain\llvm-mingw-20261117-ucrt-x86_64\bin\i686-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $compilerPath)) { throw 'Pinned LLVM-MinGW toolchain is missing. See NATIVE.md.' }
$outputPath = Join-Path $projectRoot 'build\native'
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$commonArgs = @('-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-isystem',"$projectRoot\vendor\openvr")
& $compilerPath @commonArgs '-shared' "$projectRoot\src\native\probe.cpp" "$projectRoot\src\native\probe.def" "$projectRoot\vendor\openvr\openvr_api.lib" '-lbcrypt' '-o' "$outputPath\XINPUT1_3.dll"
if ($LASTEXITCODE -ne 0) { throw "Native DLL build failed: $LASTEXITCODE" }
& $compilerPath @commonArgs "$projectRoot\src\native\proxy_test.cpp" '-o' "$outputPath\BO2VR.ProxyTest.exe"
if ($LASTEXITCODE -ne 0) { throw "Native test build failed: $LASTEXITCODE" }
Copy-Item -LiteralPath "$projectRoot\vendor\openvr\openvr_api.dll" -Destination $outputPath
Copy-Item -LiteralPath "$projectRoot\vendor\openvr\LICENSE" -Destination "$outputPath\OpenVR-LICENSE.txt"
Copy-Item -LiteralPath "$projectRoot\src\native\native-notices.txt" -Destination $outputPath
Write-Output "Built native x86 probe and test in $outputPath"

