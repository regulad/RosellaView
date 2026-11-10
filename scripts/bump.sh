#!/usr/bin/env bash
# Sets the build number everywhere it's written:
#
#   scripts/bump.sh [build]
#
# build defaults to the current one plus one. It goes into the manifest's
# version (the build number scripts/build.sh packages and CI tags), the
# ad-hoc release in app/src/buildInfo.ts (rosellaview@<build>+dev), and
# app/package.json as <build>.0.0, by npm version (which also updates the
# lock file): the Appstore showed build 3 by package.json's 0.2.0.
#
# Copyright (C) 2026 Parker Wahle
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP="$ROOT/app"

CURRENT=$(sed -n 's/^version *= *"\([0-9][0-9]*\)".*/\1/p' "$APP/manifest.toml")
[ -n "$CURRENT" ] || { echo "no integer version in $APP/manifest.toml" >&2; exit 1; }
BUILD=${1:-$((CURRENT + 1))}
[[ "$BUILD" =~ ^[1-9][0-9]*$ ]] || { echo "build must be a positive integer, not $BUILD" >&2; exit 1; }

sed -i "s/^version *= *\"[0-9]*\"/version = \"$BUILD\"/" "$APP/manifest.toml"
sed -i "s/release: 'rosellaview@[^']*'/release: 'rosellaview@$BUILD+dev'/" "$APP/src/buildInfo.ts"
(cd "$APP" && npm version "$BUILD.0.0" --no-git-tag-version --allow-same-version > /dev/null)

echo "build $CURRENT -> $BUILD: app/manifest.toml, app/package.json, app/src/buildInfo.ts"
