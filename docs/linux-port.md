# Compositor ARC for Linux

The `linux/` directory contains the first C++17 / Qt 6 Widgets implementation. It runs as a native Wayland application on Omarchy. The original macOS implementation remains in `Compositor/`.

This is an early working editor, not feature parity with the macOS application.

## Build, run, and test

Required tools: a C++17 compiler, GNU Make, Qt 6 Core/Gui/Widgets, qmake6, and Qt Test for the tests. On Arch/Omarchy these are supplied by `gcc`, `make`, and `qt6-base`; native Wayland support also needs `qt6-wayland`. No new packages were needed on the development machine.

From the repository root:

```sh
./scripts/linux-build.sh
./scripts/linux-run.sh
./scripts/linux-test.sh
```

The executable is `build/linux/compositor-arc`. The run script builds if the executable is missing; run the build script again after source changes. Set `JOBS` to control build parallelism (default 4).

Pass image filenames to import them at startup, or one `.comp` folder to open a project:

```sh
./scripts/linux-run.sh picture.png overlay.png
./scripts/linux-run.sh /path/to/artwork.comp
```

Qt selects its platform from the desktop environment. If needed, force native Wayland with `QT_QPA_PLATFORM=wayland ./scripts/linux-run.sh`.

## Available now

- Import images through a file dialog, startup arguments, or drag and drop. PNG and JPEG are the baseline; other formats depend on installed Qt image plugins.
- The first image in a fresh startup document determines canvas dimensions. An explicitly created canvas retains its chosen dimensions.
- Layers with visibility, inline rename, duplication, deletion, and raising/lowering.
- Drag a layer to move it. Shift constrains movement to one axis. Escape cancels the drag.
- Numeric position, width, height, rotation, opacity, and horizontal/vertical flips. Transforming preserves source image pixels.
- Normal, Multiply, Screen, Overlay, Darken, Lighten, Difference, Color Dodge, Color Burn, and Soft Light blending.
- Wheel zoom, middle-drag or Space-drag pan, and Fit Canvas (`Ctrl+0`).
- Undo/redo (`Ctrl+Z`, `Ctrl+Shift+Z` or `Ctrl+Y`), with up to 100 edits. Source images are implicitly shared across history snapshots.
- Project saving (`Ctrl+S`), Save As, and unsaved-change prompts before closing or replacing a document.
- PNG export with transparency; JPEG export at quality 95 with white behind transparent areas. Export does not mark a project saved.

Use File → Open Project to select the `.comp` directory itself. Linux displays these document packages as folders.

## Project compatibility and safety

The current Swift implementation writes **version 8**, while the older `project-format.md` overview covers versions 1–6. The Qt loader currently accepts **versions 1–3 with flat raster layers only**. It writes version 3 using the existing manifest and embedded PNG structure, including original source pixels and separate transforms.

Groups, masks, effects, editable text, shape metadata, adjustment layers, unknown fields, and later format versions are rejected. Unsupported blend modes are rejected too. This prevents silent loss of features on save. Compatibility has been tested against hand-authored legacy-schema fixtures and Qt round trips; an actual macOS-to-Linux round trip has not yet been verified.

Saves stage a complete sibling directory, then use Linux `renameat2` to atomically install or exchange it. A failure leaves the previous project in place. Filesystems without the required rename operation report an error rather than falling back to a destructive overwrite. This guarantees atomic replacement, not power-loss durability. Existing destinations must be supported projects and must not contain unrelated files or extra image assets.

The loader validates IDs, transforms, sizes, filenames, asset containment, and combined image pixel counts before accepting a document. Limits are 30,000 pixels per side, 100 megapixels per canvas and for combined source images, 10,000 layers, 4 MiB for the manifest, and 512 MiB per encoded image. Invalid projects do not replace the current document.

## Architecture

- `linux/src/document.*`: document/layer values, rendering, bounded image import, project persistence, and export.
- `linux/src/canvas.*`: viewport, hit testing, temporary drag previews, zoom and pan.
- `linux/src/window.*`: editor controls, dialogs, and undo commands.
- `linux/tests/`: Qt tests for rendering, transforms, persistence, rejection of unsupported/malformed data, save preservation, canvas gestures, and layer controls/history.

Rendering currently uses QPainter on the CPU and caches a full-resolution composite for display. Layer changes and drag previews rebuild that composite synchronously. Large projects need a later tiled/dirty-region renderer and background processing. Qt's smooth image interpolation also does not yet reproduce the macOS high-quality downsampler exactly.

The upstream C pixel routines remain available, but this first milestone does not yet wire them into painting or selection tools.

## Remaining port work

1. Brushes, selections, masks, and grouping, with corresponding file-format support and regression tests.
2. Adjustments, filters, text, and shape tools.
3. Retouching and a Linux replacement for Apple Vision background removal.
4. Performance profiling, tiled rendering, and acceleration where measurements justify it.
5. Broader macOS fixture compatibility, desktop packaging, and release automation.

The existing Swift/XCTest suite still requires macOS/Xcode. Linux tests do not imply that suite passes.
