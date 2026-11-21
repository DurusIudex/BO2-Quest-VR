@echo off
pushd "%~dp0"
if not exist "build\BO2VR.Check.exe" (
    echo Run tools\build.ps1 first.
) else (
    "build\BO2VR.Check.exe" --out "reports\quest-check.json"
)
pause
popd

