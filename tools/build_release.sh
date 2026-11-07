#!/usr/bin/env bash
# Builds FurUltimate for Windows x64 (MinGW-w64 cross toolchain), optionally runs the test suite under Wine,
# stages the release and produces a portable ZIP plus the NSIS installer in dist/.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(grep -oP 'project\(FurUltimate VERSION \K[0-9.]+' CMakeLists.txt)
BUILD=build-release
cmake -S . -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"
x86_64-w64-mingw32-strip "$BUILD/FurUltimate.exe"

if [[ "${RUN_TESTS:-1}" == "1" ]] && command -v wine >/dev/null; then
  echo "== Running tests under Wine =="
  WINEDEBUG=-all wine "$BUILD/aether_tests.exe"
fi

STAGE=dist/stage
rm -rf "$STAGE"; mkdir -p "$STAGE/docs"
cp "$BUILD/FurUltimate.exe" "$STAGE/"
cp README.md RELEASE_NOTES.md THIRD_PARTY_NOTICES.txt "$STAGE/"
printf '\xEF\xBB\xBF' > "$STAGE/LICENSE.txt"; cat LICENSE.txt >> "$STAGE/LICENSE.txt"
cp docs/*.md "$STAGE/docs/"
mkdir -p "$STAGE/server"
cp server/leaderboard/schema.sql server/leaderboard/README.md "$STAGE/server/"
if [[ -f leaderboard.json ]]; then
  echo "== leaderboard.json found: online leaderboard endpoint embedded in FurUltimate.exe"
else
  echo "== leaderboard.json not found: this build has no online leaderboard ('server not configured')"
fi

ZIP="dist/FurUltimate-${VERSION}-win64-portable.zip"
rm -f "$ZIP"
(cd "$STAGE" && python3 -c "
import zipfile, os, sys
with zipfile.ZipFile('../$(basename "$ZIP")', 'w', zipfile.ZIP_DEFLATED) as z:
    for root, _, files in os.walk('.'):
        for f in files:
            p = os.path.join(root, f)
            z.write(p, os.path.join('FurUltimate-${VERSION}', os.path.relpath(p, '.')))
")
python3 tools/make_icon.py assets/app.ico installer docs/images/banner.png >/dev/null
(cd installer && makensis -V2 -INPUTCHARSET UTF8 -DVERSION="$VERSION" -DSTAGE="../$STAGE" -DOUTFILE="../dist/FurUltimate-${VERSION}-Setup.exe" furultimate.nsi)
cp "$STAGE/FurUltimate.exe" "dist/FurUltimate-${VERSION}-win64.exe"   # standalone single-file build
(cd dist && sha256sum "FurUltimate-${VERSION}-Setup.exe" "FurUltimate-${VERSION}-win64.exe" "$(basename "$ZIP")" > SHA256SUMS.txt)
ls -la dist
