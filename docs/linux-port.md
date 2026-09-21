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
- Add transparent paint layers sized to the canvas. Brush (`B`) and eraser (`E`) paint on the selected visible raster layer, including transformed layers. Controls set color, diameter in document pixels, and opacity; the cursor outlines the brush footprint. Hardness controls the edge: 100% gives a hard round brush, lower values give a soft falloff. Soft coverage uses 24 nested strokes as an approximation of a linear radial falloff.
- Add, disable, or remove a layer mask. Choose **Paint image** or **Paint mask**; black hides and white reveals. Color buttons provide black/white presets; other colors become grayscale coverage. The eraser restores white on masks. Masks follow layer transforms and apply to exports. Mask painting requires an enabled mask.
- Each stroke is one undo step. Stroke opacity is applied once across the entire stroke, including self-overlaps. Escape, focus loss, or changing tools cancels an unfinished stroke.
- Rectangle (`M`), ellipse, freehand lasso (`L`), polygonal lasso, and magic wand (`W`) selections constrain painting and erasing in document coordinates. Drag to select; click polygon vertices and press Enter or double-click to finish. Shift adds, Alt subtracts. Wand samples the visible composite with adjustable tolerance. Use Select → Deselect (`Ctrl+D`). Escape cancels a selection drag and restores the previous selection. Selections are session-only and clear when opening a different document or changing canvas size.
- Adjust / Filter menu: selection-limited live previews for Levels (with Auto), Curves, Hue/Saturation, Exposure, Gradient Map, Grain, Invert, Gaussian Blur, Noise, and Lens Correction. Cancel restores the original image; Apply creates one undo step. Curves currently uses editable input/output pairs rather than draggable handles.
- Content-aware fill uses the original C implementation on the selected image region.
- Image menu: canvas size, image resampling/resolution, crop to selection bounds, and canvas flips. Image resampling bakes each layer transform into new source pixels.
- Layer menu: Merge Down for Normal-blend layers; mask reveal/hide all, invert, and feather/blur. Feather uses a three-pass box approximation of a Gaussian.
- Select All, Invert Selection, Select Layer Pixels (nonzero alpha outline), Copy Merged, and Paste Image as Layer.
- Drag a layer to move it. Shift constrains movement to one axis. Escape cancels the drag.
- Numeric position, width, height, rotation, opacity, and horizontal/vertical flips. Transforming preserves source image pixels.
- Normal, Multiply, Screen, Overlay, Darken, Lighten, Difference, Color Dodge, Color Burn, and Soft Light blending.
- Wheel zoom, middle-drag or Space-drag pan, and Fit Canvas (`Ctrl+0`).
- Undo/redo (`Ctrl+Z`, `Ctrl+Shift+Z` or `Ctrl+Y`), with up to 100 edits. Source images are implicitly shared across history snapshots.
- Project saving (`Ctrl+S`), Save As, and unsaved-change prompts before closing or replacing a document.
- PNG export with transparency; JPEG export at quality 95 with white behind transparent areas. Export does not mark a project saved.

Use File → Open Project to select the `.comp` directory itself. Linux displays these document packages as folders.

## Project compatibility and safety

The current Swift implementation writes **version 8**, while the older `project-format.md` overview covers versions 1–6. The Qt loader accepts **versions 1–8 containing supported flat layers**, including linked raster masks and editable shape/text metadata. It writes version 8 for guides or editable shapes/text, version 4 for masks, and version 3 otherwise, using the existing manifest and embedded PNG structure.

Groups, clipping masks, unlinked masks, effects, adjustment layers, unknown fields, and future format versions are rejected. Unsupported blend modes are rejected too. This prevents silent loss of features on save. Compatibility has been tested against hand-authored legacy-schema fixtures and Qt round trips; an actual macOS-to-Linux round trip has not yet been verified.

Saves stage a complete sibling directory, then use Linux `renameat2` to atomically install or exchange it. A failure leaves the previous project in place. Filesystems without the required rename operation report an error rather than falling back to a destructive overwrite. This guarantees atomic replacement, not power-loss durability. Existing destinations must be supported projects and must not contain unrelated files or extra image assets.

The loader validates IDs, transforms, sizes, filenames, asset containment, and combined image pixel counts before accepting a document. Limits are 30,000 pixels per side, 100 megapixels per canvas, for combined source images, and separately for combined masks, 10,000 layers, 4 MiB for the manifest, and 512 MiB per encoded image. Invalid projects do not replace the current document.

## Architecture

- `linux/src/document.*`: document/layer values, rendering, bounded image import, project persistence, and export.
- `linux/src/canvas.*`: viewport, hit testing, temporary drag previews, zoom and pan.
- `linux/src/painting.*`: brush/eraser rasterization with document-to-source mapping and selection clipping.
- `linux/src/window.*`: editor controls, dialogs, and undo commands.
- `linux/tests/`: Qt tests for rendering, transforms, persistence, rejection of unsupported/malformed data, save preservation, canvas gestures, and layer controls/history.

Rendering currently uses QPainter on the CPU and caches a full-resolution composite for display. Paint previews redraw the affected region for pixel-aligned layers and restrict brush coverage allocation to the stroke bounds. Transformed layers fall back to a full redraw to avoid Qt interpolation differences. Other layer changes still rebuild the composite synchronously. Large projects need a later tiled/dirty-region renderer and background processing. Qt's smooth image interpolation also does not yet reproduce the macOS high-quality downsampler exactly.

The original C wand, levels, gradient-map, grain, noise, lens-correction, and content-fill kernels are used by the Qt build. The basic brush uses Qt rasterization; healing uses the original C kernel, while clone stamping uses Qt image composition. Painting is clipped to the canvas and the layer's existing source image bounds. Use a canvas-sized paint layer to paint outside an imported image. Pressure and feathered selection coverage are not implemented yet.

A local synthetic sample uses a 3840×2160 document with four full-size raster layers and a soft mask stroke. The original full preview averaged about 71 ms. Regional redraw and smaller brush scratch buffers reduced this to about **13 ms** (three samples, excluding UI presentation). Transformed layers still take the full redraw path. This is not a general benchmark: longer strokes, different masks, and larger documents can be slower.

## Remaining port work

1. Additional selection tools, unlinked/clipping masks, and grouping, with corresponding file-format support and regression tests.
2. Adjustments, filters, text, and shape tools.
3. Retouching and a Linux replacement for Apple Vision background removal.
4. Performance profiling, tiled rendering, and acceleration where measurements justify it.
5. Broader macOS fixture compatibility, desktop packaging, and release automation.

The existing Swift/XCTest suite still requires macOS/Xcode. Linux tests do not imply that suite passes.

Adjustment limitations: Hue/Saturation currently affects the full color range; selective color bands and Colorize remain. Levels Auto currently samples the whole source. Gaussian Blur uses a three-box approximation and does not expand layer bounds yet. Adjustment layers and the full Curves graphical editor remain separate parity work.

Retouching tools are available from the tool dropdown: Eyedropper (I), Gradient (G), Clone Stamp (S), Healing (J), and Blur Brush. Alt-click selects a clone source. Options controls aligned cloning, sampling all visible layers, and the gradient end color (white by default). Gradient, blur, brush, and eraser support masks. Shift-click with the brush or eraser connects to the previous stroke endpoint. Retouching previews cancel with Escape and commit as a single undo step.

Healing currently uses Content-Aware mode; Create Texture and Proximity Match remain to be exposed. Clone/heal currently target image pixels, not masks. Clone sources are captured at stroke start to avoid recursive smearing. The gradient is linear; radial and transparent-stop variants remain. Blur uses the same bounded three-box approximation as the filter.

Editable shapes: drag Rectangle (U), Ellipse, or Line from the tool menu. Shift constrains squares/circles or 45-degree lines; Alt grows from the center. Layer → Edit shape changes fill, corner radius, and line width. Resizing a shape in the inspector redraws its geometry at the new size.

Editable text: click with Text (T) for point text, or drag a paragraph box. Clicking existing text reopens its editor; Layer → Edit text also works. The preview dialog controls content, font, pixel size, alignment, tracking in pixels, leading, color, and paragraph dimensions. Escape/Cancel restores the original; Apply records one edit. Text uses Qt layout, so font metrics can differ from AppKit. Cached PNGs are retained on load until editing, including when the original font is unavailable. Inline canvas text editing remains to be ported.

Shapes and text survive save/reopen with the native metadata schema. Painting and image filters rasterize them; masks and ordinary transforms retain metadata. Undo restores editability. Image → Image size currently bakes all layers into rasters, including shapes/text. Loading v8 does not imply support for all v8 features: unknown data is still rejected.

Guides and snapping: View → Manage guides creates horizontal/vertical guides at exact pixel positions, edits them, or removes them. The dialog previews changes; Cancel restores the original and Apply is one undo step. Move (V) drags an unlocked guide; Escape cancels. View also toggles guide visibility, locking, and snapping to guides/canvas/layer edges and centers. Snapping is initially off, uses a six-screen-pixel tolerance, and Alt temporarily bypasses it. Shift-constrained movement stays on its chosen axis. Hidden guides do not attract snapping. Guides are saved in the native v8 schema, follow crop/resize/flip operations, and never render into exported pixels. Ruler-based guide creation and snapping for resize/shape/selection gestures remain to be ported.
