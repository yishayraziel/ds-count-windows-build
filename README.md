# DS-COUNT for Windows

Windows x64 build of DS-COUNT by Yishay Raziel. Every push to `main` runs `.github/workflows/windows.yml` on a
GitHub Windows runner:

1. `scripts/build-windows.ps1` builds the VST3 and the standalone app, runs the DSP and plug-in tests, and
   builds `dist/DS-COUNT-<version>-Windows-x64-Setup.exe` with Inno Setup.
2. `scripts/validate-windows.ps1` installs it silently, validates the installed VST3 with pluginval at
   strictness 5, starts the app, uninstalls and checks nothing is left.
3. The installer is uploaded as the run artifact `DS-COUNT-Windows-x64`.

The installer puts the VST3 in `C:\Program Files\Common Files\VST3` and the app in
`C:\Program Files\Yishay Raziel\DS-COUNT`. It is not Authenticode signed: Windows SmartScreen shows
"Windows protected your PC" on download (More info > Run anyway) until it is signed with a Windows code-signing
certificate.

The source is the same as the Mac version (see `Documentation`).
