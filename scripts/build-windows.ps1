# Builds DS-COUNT for Windows x64, runs the DSP and plug-in tests, and builds the installer.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
New-Item -ItemType Directory -Force outputs | Out-Null
$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(DSCount VERSION ([0-9.]+)').Matches[0].Groups[1].Value
"DS-COUNT $version"
cmake -S . -B build-windows -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build build-windows --config Release --parallel 4 --target DSCount_VST3 DSCount_Standalone dscount_tests dscount_integration
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
ctest --test-dir build-windows -C Release --output-on-failure 2>&1 | Tee-Object outputs/tests.txt
if ($LASTEXITCODE -ne 0) { throw 'DSP or plug-in tests failed' }
$a = "$root/build-windows/DSCount_artefacts/Release"
$vst3 = "$a/VST3/DS-COUNT.vst3"
$app = "$a/Standalone/DS-COUNT.exe"
if (-not (Test-Path $vst3) -or -not (Test-Path $app)) { throw 'Build products missing' }
$iscc = "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe"
if (-not (Test-Path $iscc)) { choco install innosetup -y --no-progress; }
if (-not (Test-Path $iscc)) { throw 'Inno Setup 6 not found' }
& $iscc "/DAppVersion=$version" "/DSourceVst3=$vst3" "/DSourceApp=$app" "$root/Installer/windows/DS-COUNT.iss"
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed' }
Get-ChildItem dist
