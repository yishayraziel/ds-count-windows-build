# Build and install guide

## macOS (Apple Silicon or Intel)

Requirements: Xcode (target macOS 12 or later). CMake is optional: the script downloads it into the project
folder if it is missing. Internet access is needed once, to fetch JUCE 9.0.5.

1. Double-click `Build_DS-COUNT_on_Mac.command`. It writes `build_log.txt` and:
   * builds VST3, AU and Standalone in `~/Library/Caches/DS-COUNT-build` (outside synced folders: iCloud
     attributes break code signing)
   * runs the DSP test suite and the plug-in integration tests, and renders interface snapshots
   * ad-hoc signs the bundles and installs them for the current user:
     `~/Library/Audio/Plug-Ins/VST3/DS-COUNT.vst3` and `~/Library/Audio/Plug-Ins/Components/DS-COUNT.component`
   * copies the Standalone app and the plug-ins to `Output/`
   * runs `auval -v aufx Dsct Yraz` and pluginval at strictness 5 (VST3 and AU)
2. Rescan plug-ins in the host. DS-COUNT 2.0 replaces 1.x in place (same plug-in IDs); old sessions open with
   their settings in the Dynamics module.

Ad-hoc signing is enough on the machine that built it. To distribute, sign with a Developer ID and notarize.

Manual build:

    cmake -S . -B build-mac -G Xcode "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
    cmake --build build-mac --config Release --target DSCount_VST3 DSCount_AU DSCount_Standalone

## Windows (VST3)

Not built or tested in this delivery. The code has no platform-specific parts besides JUCE. To build:

    cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
    cmake --build build-win --config Release --target DSCount_VST3

`.github/workflows/windows-vst3.yml` builds the same on a GitHub-hosted Windows runner if the project is pushed
to a GitHub repository.

## Linux

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target DSCount_VST3 DSCount_Standalone

## Tests

    ./Tests/Unit/build_and_run.sh                          # DSP tests, no JUCE needed
    build/dscount_integration_artefacts/Release/dscount_integration            # plug-in tests
    build/dscount_integration_artefacts/Release/dscount_integration --snapshot <dir>   # interface images

## Release (signed and notarized, for distribution)

Double-click `Release_DS-COUNT_on_Mac.command`. It rebuilds if the sources changed, then:
* signs the VST3, AU and Standalone with your Developer ID Application certificate (hardened runtime,
  secure timestamp), has Apple notarize them and staples the tickets
* builds an installer (`.pkg`, signed with Developer ID Installer) that installs the plug-ins in
  `/Library/Audio/Plug-Ins` and the app in `/Applications`, notarizes and staples it
* wraps it with a read-me in a signed, notarized `.dmg`
* writes `dist/DS-COUNT-<version>-macOS.pkg`, `.dmg` and their SHA-256 checksums, and `release_log.txt`

Needs both Developer ID certificates in the Keychain (Xcode > Settings > Accounts > Manage Certificates). The
first run asks for your Apple ID and an app-specific password and stores them in the Keychain as the
notarytool profile `DSCOUNT_NOTARY`.
