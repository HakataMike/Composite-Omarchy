#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
prefix=${1:-"$HOME/.local"}
if [[ $prefix != /* || $prefix == *$'\n'* || $prefix == *'"'* || $prefix == *'`'* || $prefix == *'$'* || $prefix == *'\'* || $prefix == *'%'* ]]; then
    echo 'Use an absolute installation prefix without quotes, shell metacharacters, or percent signs.' >&2; exit 1
fi
if [[ -f "$root/linux/compositor.pro" ]]; then
    "$root/scripts/linux-build.sh"
    binary="$root/build/linux/compositor-arc"
    icon="$root/linux/packaging/compositor-arc.png"
    desktop="$root/linux/packaging/compositor-arc.desktop"
else
    binary="$root/bin/compositor-arc"
    icon="$root/share/icons/hicolor/256x256/apps/compositor-arc.png"
    desktop="$root/share/applications/compositor-arc.desktop"
fi
install -Dm755 "$binary" "$prefix/bin/compositor-arc"
install -Dm644 "$icon" "$prefix/share/icons/hicolor/256x256/apps/compositor-arc.png"
install -Dm644 "$root/LICENSE" "$prefix/share/licenses/compositor-arc/LICENSE"
mkdir -p "$prefix/share/applications"
# The desktop launcher also works when a custom prefix is not on PATH.
while IFS= read -r line; do
    if [[ $line == Exec=* ]]; then printf 'Exec="%s/bin/compositor-arc" %%F\n' "$prefix"; else printf '%s\n' "$line"; fi
done < "$desktop" > "$prefix/share/applications/compositor-arc.desktop"
if command -v update-desktop-database >/dev/null; then update-desktop-database "$prefix/share/applications"; fi
printf 'Installed Compositor ARC to %s\n' "$prefix"
