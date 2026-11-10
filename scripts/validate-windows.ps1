# Installs DS-COUNT silently on the (disposable) build machine, validates the installed VST3 with pluginval
# at strictness 5, runs the standalone app briefly, then uninstalls and checks that everything is gone.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logs = Join-Path $root 'outputs\windows-validation'
New-Item -ItemType Directory -Force $logs | Out-Null
$installer = Get-ChildItem (Join-Path $root 'dist') -Filter 'DS-COUNT-*-Windows-x64-Setup.exe' | Select-Object -First 1
if (-not $installer) { throw 'Installer not found' }
$bundle = Join-Path $env:CommonProgramFiles 'VST3\DS-COUNT.vst3'
$binary = Join-Path $bundle 'Contents\x86_64-win\DS-COUNT.vst3'
$appDir = Join-Path $env:ProgramFiles 'Yishay Raziel\DS-COUNT'
$app = Join-Path $appDir 'DS-COUNT.exe'
if (Test-Path $bundle) { throw 'Validation needs a clean machine.' }

$p = Start-Process -FilePath $installer.FullName -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=`"$logs\install.log`"") -Wait -PassThru
if ($p.ExitCode -ne 0 -or -not (Test-Path $binary) -or -not (Test-Path $app)) { throw 'Installation failed.' }
$bytes = [IO.File]::ReadAllBytes($binary)
$pe = [BitConverter]::ToInt32($bytes, 0x3c)
if ([BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664) { throw 'Installed plug-in is not Windows x64.' }

$tools = Join-Path $root 'validation-tools'
New-Item -ItemType Directory -Force $tools | Out-Null
Invoke-WebRequest 'https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Windows.zip' -OutFile "$tools\pluginval.zip"
Expand-Archive "$tools\pluginval.zip" "$tools\pluginval" -Force
$pv = Get-ChildItem "$tools\pluginval" -Filter pluginval.exe -Recurse | Select-Object -First 1
$v = Start-Process -FilePath $pv.FullName -ArgumentList @('--strictness-level','5','--validate-in-process','--timeout-ms','300000','--validate',"`"$bundle`"",'--output-dir',"`"$logs`"") -Wait -PassThru -RedirectStandardOutput "$logs\pluginval-stdout.txt" -RedirectStandardError "$logs\pluginval-stderr.txt"
Get-Content "$logs\pluginval-stdout.txt" | Select-Object -Last 15
if ($v.ExitCode -ne 0) { throw "pluginval failed ($($v.ExitCode))" }

$s = Start-Process -FilePath $app -PassThru
Start-Sleep -Seconds 8
if ($s.HasExited) { throw "Standalone app exited early ($($s.ExitCode))" }
Stop-Process -Id $s.Id -Force

$u = Start-Process -FilePath (Join-Path $appDir 'unins000.exe') -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=`"$logs\uninstall.log`"") -Wait -PassThru
Start-Sleep -Seconds 3
if ($u.ExitCode -ne 0 -or (Test-Path $bundle) -or (Test-Path $app)) { throw 'Uninstall failed.' }

'PASS: x64 binary, silent install, pluginval strictness 5, standalone launch, clean uninstall.' | Tee-Object "$logs\result.txt"
Get-FileHash $installer.FullName -Algorithm SHA256 | Format-List | Out-File "$logs\installer-sha256.txt"
