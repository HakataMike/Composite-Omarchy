#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ! -x "$repo_dir/build/linux/compositor-arc" ]]; then
    "$repo_dir/scripts/linux-build.sh"
fi
exec "$repo_dir/build/linux/compositor-arc" "$@"
