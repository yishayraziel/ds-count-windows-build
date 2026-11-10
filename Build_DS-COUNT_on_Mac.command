#!/bin/bash
# Builds, tests, validates and installs DS-COUNT on this Mac. Double-click to run.
# Everything is written to build_log.txt next to this file.
cd "$(dirname "$0")" || exit 1
ROOT="$PWD"
exec > >(tee "$ROOT/build_log.txt") 2>&1
step() { printf "\n=== %s ===\n" "$*"; }
finish() { echo; echo "DSCOUNT_BUILD_FINISHED status=$1"; echo "You can close this window."; exit "$1"; }

export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer

step "Tools"
sw_vers
uname -m
if ! xcodebuild -version; then
  echo "BLOCKER: xcodebuild failed. If it mentions the license, run:  sudo xcodebuild -license accept"
  finish 2
fi

CMAKE=""
for c in cmake /opt/homebrew/bin/cmake /usr/local/bin/cmake /Applications/CMake.app/Contents/bin/cmake "$ROOT/.tools/cmake/CMake.app/Contents/bin/cmake"; do
  if command -v "$c" >/dev/null 2>&1; then CMAKE="$c"; break; fi
done
if [ -z "$CMAKE" ]; then
  step "Downloading CMake (only into this folder)"
  mkdir -p .tools && curl -fsSL -o .tools/cmake.tgz https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-macos-universal.tar.gz || { echo "BLOCKER: CMake download failed"; finish 3; }
  tar xzf .tools/cmake.tgz -C .tools && mv .tools/cmake-3.31.6-macos-universal .tools/cmake
  CMAKE="$ROOT/.tools/cmake/CMake.app/Contents/bin/cmake"
fi
"$CMAKE" --version | head -1

ARCHS="arm64;x86_64"
if ! echo 'int main(void){return 0;}' | xcrun clang -arch x86_64 -mmacosx-version-min=12.0 -x c - -o /tmp/dscount_arch_check 2>/dev/null; then
  ARCHS="arm64"
  echo "NOTE: this Xcode cannot target Intel, building Apple Silicon only"
fi

step "Configure (Xcode project, $ARCHS)"
# Documents can be synced by iCloud or Dropbox, which tag files with attributes that break code signing.
# The build therefore lives in an unsynced folder.
B="$HOME/Library/Caches/DS-COUNT-build"
rm -rf "$ROOT/build-mac" "$B"
"$CMAKE" -S . -B "$B" -G Xcode "-DCMAKE_OSX_ARCHITECTURES=$ARCHS" || finish 4

step "Compile"
"$CMAKE" --build "$B" --config Release --parallel 8 --target DSCount_VST3 DSCount_AU DSCount_Standalone dscount_tests dscount_measure dscount_integration -- -quiet || finish 5

A="$B/DSCount_artefacts/Release"
for f in "$A/VST3/DS-COUNT.vst3/Contents/MacOS/DS-COUNT" "$A/AU/DS-COUNT.component/Contents/MacOS/DS-COUNT" "$A/Standalone/DS-COUNT.app/Contents/MacOS/DS-COUNT"; do
  lipo -info "$f" || finish 6
done

step "DSP test suite"
(cd "$B" && ./Release/dscount_tests) > "$ROOT/test_log.txt" 2>&1
TESTS=$?
grep -vE '^\[ ok' "$ROOT/test_log.txt"
cp "$B/dscount_results.csv" "$ROOT/Documentation/Measurements/mac_test_results.csv"

step "Plugin integration tests"
"$B/dscount_integration_artefacts/Release/dscount_integration" > "$ROOT/integration_log.txt" 2>&1
INTEGRATION=$?
grep -vE '^\[ ok' "$ROOT/integration_log.txt"
mkdir -p Documentation/Measurements/mac
"$B/dscount_integration_artefacts/Release/dscount_integration" --snapshot "$ROOT/Documentation/Measurements/mac"

step "Measurements on this machine"
mkdir -p Documentation/Measurements/mac && "$B/Release/dscount_measure" Documentation/Measurements/mac && cat Documentation/Measurements/mac/cpu_latency.csv

step "Signing locally (ad-hoc) and installing for this user"
for p in "$A/VST3/DS-COUNT.vst3" "$A/AU/DS-COUNT.component" "$A/Standalone/DS-COUNT.app"; do codesign --force --deep -s - "$p" || finish 7; done
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components Output
rm -rf ~/Library/Audio/Plug-Ins/VST3/DS-COUNT.vst3 ~/Library/Audio/Plug-Ins/Components/DS-COUNT.component
cp -R "$A/VST3/DS-COUNT.vst3" ~/Library/Audio/Plug-Ins/VST3/
cp -R "$A/AU/DS-COUNT.component" ~/Library/Audio/Plug-Ins/Components/
rm -f .tools/cmake.tgz
rm -rf Output/DS-COUNT.app Output/DS-COUNT.vst3 Output/DS-COUNT.component
cp -R "$A/Standalone/DS-COUNT.app" "$A/VST3/DS-COUNT.vst3" "$A/AU/DS-COUNT.component" Output/
killall -9 AudioComponentRegistrar 2>/dev/null

# Removes the earlier ETALON build, which this product replaces.
rm -rf ~/Library/Audio/Plug-Ins/VST3/ETALON.vst3 ~/Library/Audio/Plug-Ins/Components/ETALON.component ~/Library/Caches/ETALON-build
killall -9 AudioComponentRegistrar 2>/dev/null

step "auval"
auval -v aufx Dsct Yraz > auval_log.txt 2>&1
AUVAL=$?
tail -5 auval_log.txt

step "pluginval"
PV=""
if curl -fsSL -o .tools-pv.zip https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip; then
  mkdir -p .tools && unzip -qo .tools-pv.zip -d .tools && rm .tools-pv.zip
  PV="$ROOT/.tools/pluginval.app/Contents/MacOS/pluginval"
fi
PLUGINVAL=skipped
if [ -x "$PV" ]; then
  "$PV" --strictness-level 5 --validate-in-process --output-dir "$ROOT" --timeout-ms 300000 ~/Library/Audio/Plug-Ins/VST3/DS-COUNT.vst3 > pluginval_vst3_log.txt 2>&1
  PLUGINVAL=$?
  tail -2 pluginval_vst3_log.txt
  "$PV" --strictness-level 5 --validate-in-process --output-dir "$ROOT" --timeout-ms 300000 ~/Library/Audio/Plug-Ins/Components/DS-COUNT.component > pluginval_au_log.txt 2>&1
  PLUGINVAL_AU=$?
  tail -2 pluginval_au_log.txt
fi

step "Summary"
echo "dsp_tests=$TESTS integration=$INTEGRATION auval=$AUVAL pluginval_vst3=$PLUGINVAL pluginval_au=${PLUGINVAL_AU:-skipped}"
echo "Xcode project: $B/DSCount.xcodeproj"
[ "$TESTS" = 0 ] && [ "$INTEGRATION" = 0 ] && [ "$AUVAL" = 0 ] && finish 0
finish 8
