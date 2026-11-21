$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$compiler = "$root/toolchain/llvm-mingw-20261117-ucrt-x86_64/bin/x86_64-w64-mingw32-clang++.exe"
$out = "$root/build/quest"
New-Item -ItemType Directory -Force $out | Out-Null
& $compiler -std=c++17 -O2 -static -Wall -Wextra -Werror -Wno-missing-field-initializers `
    -I "$root/vendor/openxr/include" "$root/src/quest/quest_input.cpp" "$root/src/quest/quest_input_test.cpp" `
    -o "$out/quest_input_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Quest input test compilation failed' }
& "$out/quest_input_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Quest input tests failed' }
& $compiler -std=c++17 -O2 -static -Wall -Wextra -Werror `
    -I "$root/vendor/openxr/include" "$root/src/quest/controller_bindings.cpp" "$root/src/quest/controller_bindings_test.cpp" `
    -o "$out/controller_bindings_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Controller bindings test compilation failed' }
& "$out/controller_bindings_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Controller bindings tests failed' }
& $compiler -std=c++17 -O2 -static -Wall -Wextra -Werror `
    -I "$root/vendor/openxr/include" "$root/src/quest/controller_bindings.cpp" "$root/src/quest/motion_gestures.cpp" "$root/src/quest/motion_gestures_test.cpp" `
    -o "$out/motion_gestures_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Motion gesture test compilation failed' }
& "$out/motion_gestures_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Motion gesture tests failed' }
& $compiler -std=c++17 -O2 -static -Wall -Wextra -Werror `
    -I "$root/vendor/openxr/include" "$root/src/quest/camera_pose_test.cpp" `
    -o "$out/camera_pose_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Camera pose test compilation failed' }
& "$out/camera_pose_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Camera pose tests failed' }
