#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$repo_dir/build/linux"
cd "$repo_dir/build/linux"
qmake6 "$repo_dir/linux/compositor.pro"
make -j"${JOBS:-4}"
