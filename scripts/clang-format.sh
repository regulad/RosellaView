#!/usr/bin/env bash
# Formats (--write) or checks (--check) our own C and C++ with clang-format,
# using native/.clang-format and native-view/.clang-format. Vendored code
# (third_party/) and the shared bridge header's users are left to their style.
#
# Copyright (C) 2026 Parker Wahle
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mode=${1:---check}
mapfile -t files < <(cd "$ROOT" && git ls-files 'native/src/*.[ch]' 'native/include/*.h' 'native-view/src/*.cpp' 'native-view/src/*.h')
[ ${#files[@]} -gt 0 ] || exit 0
cd "$ROOT"
case "$mode" in
  --write) clang-format -i "${files[@]}" ;;
  --check) clang-format --dry-run --Werror "${files[@]}" ;;
  *) echo "usage: $0 [--check|--write]" >&2; exit 2 ;;
esac
