#!/usr/bin/env bash
# Builds RosellaView and uploads what Sentry needs to read its reports:
# the Hermes source map (composed with Metro's) and the native debug files.
#
#   scripts/build.sh [environment] [--install]
#
# environment defaults to "development". Run on the host; the Vega builds run
# in the `vega` distrobox. Needs `sentry-cli login` (or SENTRY_AUTH_TOKEN).
#
# Copyright (C) 2026 Parker Wahle
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail

ENVIRONMENT=development
INSTALL=0
for arg in "$@"; do
  case "$arg" in
    --install) INSTALL=1 ;;
    *) ENVIRONMENT="$arg" ;;
  esac
done

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP="$ROOT/app"
ORG=regulad
PROJECT=rosellaview
SENTRY_CLI="npx -y @sentry/cli@3.8.2"

VERSION=$(node -p "require('$APP/package.json').version")
GIT=$(git -C "$ROOT" rev-parse --short=12 HEAD)
# app/package-lock.json changes on every build (the local module tarball's
# checksum), so it doesn't count.
if ! git -C "$ROOT" diff --quiet HEAD -- app native native-view ':!app/package-lock.json'; then
  # Uncommitted changes: make each build's release unique, so its source map
  # can't be confused with another build's.
  GIT="$GIT-dirty.$(date +%s)"
fi
RELEASE="rosellaview@$VERSION+$GIT"
DIST=vega

# Stamp the release into the bundle; restore the committed default afterwards.
BUILD_INFO="$APP/src/buildInfo.ts"
cp "$BUILD_INFO" "$BUILD_INFO.orig"
trap 'mv "$BUILD_INFO.orig" "$BUILD_INFO"' EXIT
cat > "$BUILD_INFO" <<EOF
// Written by scripts/build.sh before each build; these are the values for an
// ad-hoc \`npm run build\`.
export const BUILD = {
  release: '$RELEASE',
  dist: '$DIST',
  environment: '$ENVIRONMENT',
  git: '$GIT',
};
EOF

echo "== $RELEASE ($ENVIRONMENT)"
distrobox enter vega -- bash -lc "set -euo pipefail
  cd '$ROOT/native'
  npm run build > /tmp/rv-native-build.log 2>&1 || { grep -E 'error|FAILED' /tmp/rv-native-build.log | head -20; exit 1; }
  rm -f ./*.tgz
  npm pack --ignore-scripts > /dev/null 2>&1
  # The native UI component (GL surface view): its own package.
  cd '$ROOT/native-view'
  [ -d node_modules ] || npm install --no-audit --no-fund > /dev/null 2>&1
  npm run build > /tmp/rv-view-build.log 2>&1 || { grep -E 'error|FAILED' /tmp/rv-view-build.log | head -20; exit 1; }
  rm -f ./*.tgz
  npm pack --ignore-scripts > /dev/null 2>&1
  cd '$APP'
  npm uninstall rosellaview-native rosellaview-surface > /dev/null 2>&1
  npm install --no-audit --no-fund file:../native/rosellaview-native-0.1.0.tgz file:../native-view/rosellaview-surface-0.1.0.tgz > /dev/null
  # -output-source-map: Hermes writes index.hermes.bundle.map next to the bytecode.
  npx react-native build-vega --build-type Release --hermes-flags '-O,-w,-output-source-map' > /tmp/rv-app-build.log 2>&1 \
    || { grep -iE 'error|FAILED' /tmp/rv-app-build.log | head -20; exit 1; }
"

BUNDLES="$APP/build/lib/rn-bundles/Release"
OUT="$APP/build/sentry"
rm -rf "$OUT"
mkdir -p "$OUT"
METRO_MAP=$(ls "$APP"/build/debugging/Release/srcmap/index.bundle.map)
HERMES_MAP="$BUNDLES/index.hermes.bundle.map"
[ -f "$HERMES_MAP" ] || { echo "no Hermes source map at $HERMES_MAP" >&2; exit 1; }
cp "$BUNDLES/index.bundle" "$OUT/index.bundle"
node "$APP/node_modules/react-native/scripts/compose-source-maps.js" "$METRO_MAP" "$HERMES_MAP" -o "$OUT/index.bundle.map"

$SENTRY_CLI releases --org "$ORG" --project "$PROJECT" new "$RELEASE"
$SENTRY_CLI sourcemaps upload --org "$ORG" --project "$PROJECT" --release "$RELEASE" --dist "$DIST" \
  --url-prefix app:/// --no-rewrite "$OUT"
$SENTRY_CLI debug-files upload --org "$ORG" --project "$PROJECT" "$APP"/build/*-release/debug/
$SENTRY_CLI releases --org "$ORG" --project "$PROJECT" finalize "$RELEASE"

if [ "$INSTALL" = 1 ]; then
  distrobox enter vega -- bash -lc "cd '$APP'
    vega device terminate-app --appName xyz.regulad.rosellaview.main > /dev/null 2>&1 || true
    vega device install-app --packagePath build/armv7-release/rosellaview_armv7.vpkg | tail -1
    vega device launch-app --appName xyz.regulad.rosellaview.main | tail -1"
fi
echo "== built $RELEASE"
