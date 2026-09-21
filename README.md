# Composite Omarchy

A free, small file-size (<10MB) open-source image editor for Linux and Omarchy, built with C++ and Qt 6. Combine images, paint and retouch, add text and shapes, and adjust colors in a layered workspace.

## Features

- Layers, folders, masks, blend modes, and effects.
- Brushes, selections, cloning, healing, smudge, and liquify.
- Editable text and shapes, cropping, transforms, and color adjustments.
- Multiple document tabs, undo/redo, and editable project files.
- PNG, JPEG, HEIC, and TIFF import; PNG and JPEG export.

## Download and run

Extract the Linux archive from [Releases](https://github.com/HakataMike/Composite-Omarchy/releases), then run `bin/compositor-arc`.

On Arch/Omarchy, the runtime packages are `qt6-base`, `qt6-wayland`, `qt6-imageformats`, and `libheif`.

To install the app and launcher for your account, run this from the extracted folder:

```sh
./scripts/linux-install.sh
```

## Build from source

Install the runtime packages above plus `gcc`, `make`, and `pkgconf`. From the repository root:

```sh
./scripts/linux-build.sh
./scripts/linux-run.sh
```

See the [Linux guide](docs/linux-port.md) for testing, packaging, and compatibility details.

## License

MIT — see [LICENSE](LICENSE). Based on [Compositor](https://github.com/robbietilton/Compositor).
