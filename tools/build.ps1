$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$compilerPath = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compilerPath)) {
    throw '.NET Framework 4 C# compiler is required.'
}
$outputPath = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
& $compilerPath /nologo /platform:x86 /optimize+ /target:exe /reference:System.Web.Extensions.dll "/out:$outputPath\BO2VR.Check.exe" "$projectRoot\src\Check.cs" "$projectRoot\vendor\openvr\openvr_api.cs"
if ($LASTEXITCODE -ne 0) { throw "Compiler failed: $LASTEXITCODE" }
Copy-Item -LiteralPath "$projectRoot\vendor\openvr\openvr_api.dll" -Destination $outputPath
Copy-Item -LiteralPath "$projectRoot\vendor\openvr\LICENSE" -Destination "$outputPath\OpenVR-LICENSE.txt"
Write-Output "Built $outputPath\BO2VR.Check.exe (32-bit)"

