#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
"$root/scripts/linux-build.sh"
name="compositor-arc-linux-$(uname -m)"
mkdir -p "$root/build/releases"
staging=$(mktemp -d "$root/build/releases/.package.XXXXXX")
trap 'rm -rf -- "$staging"' EXIT
bundle="$staging/$name"
install -Dm755 "$root/build/linux/compositor-arc" "$bundle/bin/compositor-arc"
strip "$bundle/bin/compositor-arc"
install -Dm644 "$root/linux/packaging/compositor-arc.png" "$bundle/share/icons/hicolor/256x256/apps/compositor-arc.png"
install -Dm644 "$root/linux/packaging/compositor-arc.desktop" "$bundle/share/applications/compositor-arc.desktop"
install -Dm755 "$root/scripts/linux-install.sh" "$bundle/scripts/linux-install.sh"
install -Dm644 "$root/LICENSE" "$bundle/LICENSE"
install -Dm644 "$root/docs/linux-port.md" "$bundle/Linux-guide.md"
cat > "$bundle/README.txt" <<'TEXT'
Compositor ARC — native Qt 6 Linux editor

Built for current Arch Linux / Omarchy on the architecture in the archive name.
This is a dynamically linked package, not a universal Linux binary.
Runtime packages: qt6-base qt6-wayland qt6-imageformats libheif
The libheif package must include a HEVC decoder for HEIC images.

Run: ./bin/compositor-arc [image files or one .comp project directory]
Install in your account: ./scripts/linux-install.sh
Custom prefix: ./scripts/linux-install.sh /absolute/prefix

No AI models or Python runtime are required.
See Linux-guide.md for tools, limitations, and project compatibility.
TEXT
archive="$root/build/releases/$name.tar.gz"
tar -C "$staging" -czf "$archive" "$name"
sha256sum "$archive" > "$archive.sha256"
printf 'Created %s\n' "$archive"
du -h "$archive"
