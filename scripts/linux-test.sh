#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$repo_dir/build/linux-tests"
cd "$repo_dir/build/linux-tests"
qmake6 "$repo_dir/linux/tests/tests.pro"
make -j"${JOBS:-4}"
# Avoid loading a desktop GTK theme plugin in a display-free test environment.
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion ./compositor-tests "$@"
