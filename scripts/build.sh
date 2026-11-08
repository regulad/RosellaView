#!/usr/bin/env bash
# Builds RosellaView and uploads what Sentry needs to read its reports:
# the Hermes source map (composed with Metro's) and the native debug files.
#
#   scripts/build.sh [environment] [--install]
#
# environment defaults to "development". With the Vega CLI on PATH (CI: see
# .github/workflows/release.yml) the builds run directly; otherwise in the
# `vega` distrobox (the owner's laptop). Needs `sentry-cli login` or
# SENTRY_AUTH_TOKEN; SENTRY_UPLOAD=0 skips the upload (a dry-run build).
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
SENTRY_UPLOAD=${SENTRY_UPLOAD:-1}
if command -v vega > /dev/null; then
  in_vega() { bash -c "$1"; }
else
  in_vega() { distrobox enter vega -- bash -lc "$1"; }
fi

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
# A clean production build of the commit tagged v<version> (CI's release
# builds) is just the version: rosellaview@0.1.0. Development builds keep the
# sha, so their source maps never land in the release's.
TAG=${GITHUB_REF_TYPE:+${GITHUB_REF_NAME}}
[ -n "$TAG" ] || TAG=$(git -C "$ROOT" describe --exact-match --tags HEAD 2> /dev/null || true)
if [ "$ENVIRONMENT" = production ] && [ "$TAG" = "v$VERSION" ] && [ "${GIT%%-dirty*}" = "$GIT" ]; then
  RELEASE="rosellaview@$VERSION"
fi
DIST=vega

# Stamp the release into the bundle, and (development builds only) let the
# app reach the IAP App Tester; restore the committed files afterwards.
BUILD_INFO="$APP/src/buildInfo.ts"
MANIFEST="$APP/manifest.toml"
cp "$BUILD_INFO" "$BUILD_INFO.orig"
cp "$MANIFEST" "$MANIFEST.orig"
trap 'mv "$BUILD_INFO.orig" "$BUILD_INFO"; mv "$MANIFEST.orig" "$MANIFEST"' EXIT
if [ "$ENVIRONMENT" = development ]; then
  # Sandbox purchases (ProjectRosella/VEGA.md, In-app purchases). Inert
  # without App Tester installed, but kept out of production builds.
  cat >> "$MANIFEST" <<'TESTER'

# IAP App Tester (sandbox purchases): development builds only (scripts/build.sh).
[[wants.service]]
id = "com.amazon.iap.tester.service"

[[wants.module]]
id = "/com.amazonappstore.iap.tester@IIAPTesterUI"
TESTER
fi
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

# Production builds are what's submitted: armv7 only (the Fire TV Stick; the
# other targets are for the Vega Virtual Device) and unsigned, since Amazon
# re-signs submitted packages. Development builds keep every target and the
# developer signature that sideloading needs.
# RV_CCACHE=1 (CI) compiles through ccache, so a fresh checkout's new mtimes
# don't cost a full rebuild.
CMAKE_LAUNCHER=""
if [ "${RV_CCACHE:-0}" = 1 ]; then
  CMAKE_LAUNCHER=" --cmakeFlags=-DCMAKE_C_COMPILER_LAUNCHER=ccache --cmakeFlags=-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
fi
if [ "$ENVIRONMENT" = production ]; then
  NATIVE_BUILD="npm run build:js && vega build -b Release -t armv7$CMAKE_LAUNCHER"
  APP_TARGET="--target armv7 --additional-native-options \"--no-profile\""
else
  NATIVE_BUILD="npm run build"
  APP_TARGET=""
fi

echo "== $RELEASE ($ENVIRONMENT)"
in_vega "set -euo pipefail
  cd '$ROOT/native'
  [ -d node_modules ] || npm install --no-audit --no-fund > /dev/null 2>&1
  ($NATIVE_BUILD) > /tmp/rv-native-build.log 2>&1 || { grep -E 'error|FAILED' /tmp/rv-native-build.log | head -20; exit 1; }
  rm -f ./*.tgz
  npm pack --ignore-scripts > /dev/null 2>&1
  # The native UI component (GL surface view): its own package.
  cd '$ROOT/native-view'
  [ -d node_modules ] || npm install --no-audit --no-fund > /dev/null 2>&1
  ($NATIVE_BUILD) > /tmp/rv-view-build.log 2>&1 || { grep -E 'error|FAILED' /tmp/rv-view-build.log | head -20; exit 1; }
  rm -f ./*.tgz
  npm pack --ignore-scripts > /dev/null 2>&1
  cd '$APP'
  npm uninstall rosellaview-native rosellaview-surface > /dev/null 2>&1
  npm install --no-audit --no-fund file:../native/rosellaview-native-0.1.0.tgz file:../native-view/rosellaview-surface-0.1.0.tgz > /dev/null
  # -output-source-map: Hermes writes index.hermes.bundle.map next to the bytecode.
  npx react-native build-vega --build-type Release $APP_TARGET --hermes-flags '-O,-w,-output-source-map' > /tmp/rv-app-build.log 2>&1 \
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

if [ "$SENTRY_UPLOAD" = 1 ]; then
$SENTRY_CLI releases --org "$ORG" --project "$PROJECT" new "$RELEASE"
$SENTRY_CLI sourcemaps upload --org "$ORG" --project "$PROJECT" --release "$RELEASE" --dist "$DIST" \
  --url-prefix app:/// --no-rewrite "$OUT"
$SENTRY_CLI debug-files upload --org "$ORG" --project "$PROJECT" "$APP"/build/*-release/debug/
$SENTRY_CLI releases --org "$ORG" --project "$PROJECT" finalize "$RELEASE"
else
  echo "== Sentry upload skipped (SENTRY_UPLOAD=0)"
fi

if [ "$INSTALL" = 1 ]; then
  in_vega "cd '$APP'
    vega device terminate-app --appName xyz.regulad.rosellaview.main > /dev/null 2>&1 || true
    vega device install-app --packagePath build/armv7-release/rosellaview_armv7.vpkg | tail -1
    vega device launch-app --appName xyz.regulad.rosellaview.main | tail -1"
fi
echo "== built $RELEASE"
