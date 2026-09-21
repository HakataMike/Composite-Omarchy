# Compositor ARC for Linux

The `linux/` directory contains the C++17 / Qt 6 Widgets implementation. It runs as a native Wayland application on Omarchy. The required portable C kernels are in `linux/src/kernels/`, and the application icon is in `linux/packaging/`. The original Mac source and build files are no longer tracked; local reference copies are ignored.

The non-AI Linux editing implementation is complete and has passed the automated acceptance suite. Cross-platform comparison limits and remaining human acceptance checks are tracked in [port-parity.md](port-parity.md).

## Build, run, and test

Required tools: a C++17 compiler, GNU Make, Qt 6 Core/Gui/Widgets, qmake6, libheif (tested with 1.23.4), and Qt Test for the tests. On Arch/Omarchy these are supplied by `gcc`, `make`, `pkgconf`, `libheif`, and `qt6-base`; native Wayland support also needs `qt6-wayland`. TIFF and WebP support require `qt6-imageformats`. No new packages were needed on the development machine.

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

## Installation and packaging

Install the application and launcher for your account:

```sh
./scripts/linux-install.sh
```

The default prefix is `~/.local`. A custom absolute prefix may be supplied as the first argument. The script builds the application, installs its executable, desktop entry, icon, and MIT license. This does not change your default image application.

Build a distributable archive:

```sh
./scripts/linux-package.sh
```

Output: `build/releases/compositor-arc-linux-<architecture>.tar.gz` plus a SHA-256 checksum. Extract it and run `bin/compositor-arc`, or use its `scripts/linux-install.sh` without a compiler. The stripped package uses system Qt and libheif libraries; it targets current Arch/Omarchy on the build architecture and is not a self-contained or universal Linux bundle. The package README lists runtime dependencies. No Python, AI models, development objects, or test binaries are bundled.

## Background removal

The Linux port deliberately omits automatic AI background removal and has no Python runtime or AI model dependency. Use the lasso or magic wand to select the background, then erase within the selection or paint black on a layer mask to hide it. Feather the selection for softer edges; masks preserve the original image pixels.

## Available now

- Import images through a file dialog, startup arguments, or drag and drop. PNG/JPEG use Qt; HEIC/HEIF/AVIF use the shared [libheif library](https://github.com/strukturag/libheif); TIFF/WebP use `qt6-imageformats`. HEIF imports the primary image, applies its orientation, and converts supported SDR color profiles to sRGB. Unsupported HDR transfer functions report an error asking for SDR conversion. Editing remains 8-bit SDR; depth maps, image sequences, and camera metadata are not retained.
- The first image in a fresh startup document determines canvas dimensions. An explicitly created canvas retains its chosen dimensions.
- Layers with visibility, inline rename, duplication, deletion, and raising/lowering.
- Add transparent paint layers sized to the canvas. Brush (`B`) and eraser (`E`) paint on the selected visible raster layer, including transformed layers. Controls set color, diameter in document pixels, and opacity; the cursor outlines the brush footprint. Hardness controls the edge: 100% gives a hard round brush, lower values give a soft falloff. Soft coverage uses 24 nested strokes as an approximation of a linear radial falloff.
- Add, disable, or remove a layer mask. Choose **Paint image** or **Paint mask**; black hides and white reveals. Color buttons provide black/white presets; other colors become grayscale coverage. The eraser restores white on masks. Masks follow layer transforms and apply to exports. Mask painting requires an enabled mask.
- Each stroke is one undo step. Stroke opacity is applied once across the entire stroke, including self-overlaps. Escape, focus loss, or changing tools cancels an unfinished stroke.
- Rectangle (`M`), ellipse, freehand lasso (`L`), polygonal lasso, and magic wand (`W`) selections constrain painting and erasing in document coordinates. Drag to select; click polygon vertices and press Enter or double-click to finish. Shift adds, Alt subtracts. Wand samples the visible composite with adjustable tolerance. Use Select → Deselect (`Ctrl+D`). Escape cancels a selection drag and restores the previous selection. Selections are session-only and clear when opening a different document or changing canvas size.
- Adjust / Filter menu: selection-limited live previews for Levels (with Auto), Curves, Hue/Saturation, Exposure, Gradient Map, Grain, Invert, Gaussian Blur, Motion Blur, Noise, and Lens Correction. Cancel restores the original image; Apply creates one undo step. Curves uses a draggable graph with per-channel points and keyboard nudging.
- Content-aware fill uses the original C implementation and expands the source image to cover selections outside its original bounds. Rotated/flipped layers and independently placed masks retain their placement.
- Image menu: canvas size, image resampling/resolution, crop to selection bounds, and canvas flips. Image resampling bakes each layer transform into new source pixels.
- Layer menu: Merge Down, Merge Layers, and Merge Folder; mask reveal/hide all, invert, and feather/blur. Feather uses a three-pass box approximation of a Gaussian.
- Select All, Invert Selection, Select Layer Pixels (grayscale alpha coverage), Copy Merged, and Paste Image as Layer.
- Drag a layer to move it. Shift constrains movement to one axis. Escape cancels the drag.
- Numeric position, width, height, rotation, opacity, and horizontal/vertical flips. Transforming preserves source image pixels.
- Normal, Multiply, Screen, Overlay, Darken, Lighten, Difference, Color Dodge, Color Burn, Soft Light, Hue, Saturation, Color, and Luminosity blending.
- Wheel zoom, middle-drag or Space-drag pan, and Fit Canvas (`Ctrl+0`).
- Undo/redo (`Ctrl+Z`, `Ctrl+Shift+Z` or `Ctrl+Y`), with up to 100 edits. Source images are implicitly shared across history snapshots.
- Project saving (`Ctrl+S`), Save As, and unsaved-change prompts before closing a document or quitting.
- PNG export with transparency; JPEG export with adjustable quality and white behind transparent areas, plus a live export preview for JPEG and PNG. Export does not mark a project saved.

Use File → Open Project to select the `.comp` directory itself. Linux displays these document packages as folders.

## Project compatibility and safety

The current Swift implementation writes **version 8**, while the older `project-format.md` overview covers versions 1–6. The Qt loader accepts **versions 1–8 containing supported layers and folders**, including linked raster masks and editable shape/text metadata. It writes version 8 for guides or editable shapes/text, version 7 for adjustment layers, version 6 for folder masks, version 5 for clipping, version 4 for raster masks, and version 3 otherwise, using the existing manifest and embedded PNG structure.

Unknown fields and future format versions are rejected. Unsupported blend modes are rejected too. This prevents silent loss of features on save. Compatibility has been tested against hand-authored legacy-schema fixtures and Qt round trips; an actual macOS-to-Linux round trip has not yet been verified.

Saves stage a complete sibling directory, then use Linux `renameat2` to atomically install or exchange it. A failure leaves the previous project in place. Filesystems without the required rename operation report an error rather than falling back to a destructive overwrite. This guarantees atomic replacement, not power-loss durability. Existing destinations must be supported projects and must not contain unrelated files or extra image assets.

The loader validates IDs, transforms, sizes, filenames, asset containment, and combined image pixel counts before accepting a document. Limits are 30,000 pixels per side, 100 megapixels per canvas, for combined source images, and separately for combined masks, 10,000 layers, 4 MiB for the manifest, and 512 MiB per encoded image. Invalid projects do not replace the current document.

## Architecture

- `linux/src/document.*`: document/layer values, rendering, bounded image import, project persistence, and export.
- `linux/src/canvas.*`: viewport, hit testing, temporary drag previews, zoom and pan.
- `linux/src/painting.*`: brush/eraser rasterization with document-to-source mapping and selection clipping.
- `linux/src/window.*`: editor controls, dialogs, and undo commands.
- `linux/tests/`: Qt tests for rendering, transforms, persistence, rejection of unsupported/malformed data, save preservation, canvas gestures, and layer controls/history.

Rendering currently uses QPainter on the CPU and caches a full-resolution composite for display. Paint previews redraw the affected region for pixel-aligned layers and restrict brush coverage allocation to the stroke bounds. Transformed layers fall back to a full redraw to avoid Qt interpolation differences. Other layer changes still rebuild the composite synchronously. Large projects need a later tiled/dirty-region renderer and background processing. Qt's smooth image interpolation also does not yet reproduce the macOS high-quality downsampler exactly.

The original C wand, levels, gradient-map, grain, noise, lens-correction, and content-fill kernels are used by the Qt build. The basic brush uses Qt rasterization; healing uses the original C kernel, while clone stamping uses Qt image composition. Brush strokes are clipped to the canvas and selection, and transparently expand the layer’s source pixels when needed, including after enlarging the canvas. Existing pixels and mask placement stay fixed; the expansion and stroke share one undo step. Other retouching tools remain bounded by the current source image. Feathered selection coverage is supported. Tablet pressure is not implemented.

A local synthetic sample uses a 3840×2160 document with four full-size raster layers and a soft mask stroke. The original full preview averaged about 71 ms. Regional redraw and smaller brush scratch buffers reduced this to about **13 ms** (three samples, excluding UI presentation). Transformed layers still take the full redraw path. This is not a general benchmark: longer strokes, different masks, and larger documents can be slower.

## Remaining acceptance work

The current checklist is [port-parity.md](port-parity.md). Actual macOS-generated document round trips and pixel-for-pixel Core Graphics comparisons still need a macOS test environment. Linux testing covers native schema fixtures, project persistence, and Qt editing workflows.

Automatic background removal and AI object selection are deliberately excluded; use manual selections and masks. Performance work on tiled rendering remains an optimization, not a dependency of the current editor.

The historical Swift/XCTest suite is no longer included in this Linux repository; its results have not been verified.

Adjustment controls: destructive adjustments share the layer-adjustment controls, including selective Hue/Saturation bands, Colorize, and independent RGB/color-channel Levels and Curves. Auto Levels uses the selected channel and weights the selected region. Gaussian Blur uses a three-box approximation calibrated to the requested standard deviation. Gaussian and Motion Blur expand unselected image layers while retaining mask placement; masked or selection-limited filtering keeps the original bounds. Motion Blur uses a uniform directional streak; Core Image uses a tapered profile, so cross-platform output differs.

Retouching tools are available from the tool dropdown: Eyedropper (I), Gradient (G), Clone Stamp (S), Healing (J), Blur Brush, Smudge, and Liquify. Alt-click selects a clone source. Options controls aligned cloning, sampling all visible layers, and the gradient end color (white by default). Gradient, blur, brush, and eraser support masks. Shift-click with the brush or eraser connects to the previous stroke endpoint. Retouching previews cancel with Escape and commit as a single undo step.

Smudge carries colors along the brush path; Liquify pushes pixels with a feathered displacement. Both use the native dab equations in document coordinates, respect selections, and support cancellation and undo. They edit image pixels, not masks. Long strokes on large canvases can be slower because previews replay the stroke.

Options → Healing mode offers Content-Aware, Create Texture, and Proximity Match. Clone/heal currently target image pixels, not masks. Clone sources are captured at stroke start to avoid recursive smearing; the crosshair shows the sampled location. Options also controls whether the magic wand selects contiguous regions. Options → Gradient offers radial shape, foreground-to-transparent stops, and reversal. Blur uses the same bounded three-box approximation as the filter.

Editable shapes: drag Rectangle (U), Ellipse, or Line from the tool menu. Shift constrains squares/circles or 45-degree lines; Alt grows from the center. Layer → Edit shape changes fill, corner radius, and line width. Resizing a shape in the inspector redraws its geometry at the new size.

Editable text: click with Text (T) for point text, or drag a paragraph box. Clicking existing text starts inline content editing; Layer → Edit text also works. The preview dialog controls content, font, pixel size, alignment, tracking in pixels, leading, color, and paragraph dimensions. Escape/Cancel restores the original; Apply records one edit. Text uses Qt layout, so font metrics can differ from AppKit. Cached PNGs are retained on load until editing, including when the original font is unavailable. Inline content editing applies with Ctrl+Enter or focus loss, cancels with Escape, and is committed before Save or Close. Rotated text is edited in an upright overlay and retains its transform when applied. Font and paragraph controls remain in the dialog.

Shapes and text survive save/reopen with the native metadata schema. Painting and image filters rasterize them; masks and ordinary transforms retain metadata. Undo restores editability. Image → Image size currently bakes all layers into rasters, including shapes/text. Loading v8 does not imply support for all v8 features: unknown data is still rejected.

Guides and snapping: View → Manage guides creates horizontal/vertical guides at exact pixel positions, edits them, or removes them. The dialog previews changes; Cancel restores the original and Apply is one undo step. Move (V) drags an unlocked guide; Escape cancels. View also toggles guide visibility, locking, and snapping to guides/canvas/layer edges and centers. Snapping is initially off, uses a six-screen-pixel tolerance, and Alt temporarily bypasses it. Shift-constrained movement stays on its chosen axis. Hidden guides do not attract snapping. Guides are saved in the native v8 schema, follow crop/resize/flip operations, and never render into exported pixels. View → Show rulers enables dragging guides from either ruler. Snapping also applies to resizing, shape creation, selections, and crop.

All 14 blend modes from the macOS app are now selectable and saved with the project. Hue/Saturation/Color/Luminosity use the [W3C nonseparable blend equations](https://www.w3.org/TR/compositing-1/#blendingnonseparable), with straight sRGB for the blend and premultiplied source-over composition for transparency. Layer opacity and masks are applied before blending. These four modes currently require an extra full-canvas surface and a CPU pixel pass; painting in documents using them falls back to full redraw. Reference-color and transparency tests cover the implementation, but pixel-for-pixel comparisons against Core Graphics remain part of final macOS fixture acceptance.

Layer folders: Layer → Add folder or Group selected layers (Ctrl+G) creates folders. The layer tree supports nesting/reordering by dragging, Alt-drag duplication, multi-selection for grouping, expansion/collapse, renaming, and visibility. Copy/delete includes descendants; raise/lower works among siblings. Folder opacity multiplies into each child. This is a Linux extension: the original macOS project validator requires folders to have 100% opacity, so keep folders at 100% for Mac interchange. Raster-layer opacity and blending use the shared format. Folder masks can be painted, filled, inverted, or filtered. Numeric transforms and canvas dragging move folder contents; Scale layer / folder scales proportionally. Nonuniform scaling uses the native rotated-rectangle projection, discarding shear while retaining full source pixels. Merge folder converts its contents to one raster while preserving its parent and visibility. Folder rendering currently uses a full redraw; hierarchy validation rejects cycles and missing/non-folder parents.

Clipping masks: Layer → Create clipping mask (Ctrl+Alt+G) clips the active raster/text/shape layer to the sibling below, reusing an existing stack's base. The layer tree marks clipped layers with ↳ and names the source in its tooltip. Contiguous clipping stacks preserve the base alpha once, including translucent edges, opacity, and folder masks. Independent dependency links from existing projects are also supported; source visibility does not disable their alpha. Release clipping mask detaches the active layer and the contiguous clipped layers above it. Bake clipping mask converts the source coverage into the target pixels. Deleting or merging a source bakes surviving dependents first, preserving coverage even outside the canvas. Dependency cycles and missing sources are rejected. Clipping currently renders on full-canvas CPU surfaces; large stacks need later performance work.

Independent masks: uncheck Link mask to layer, choose Paint mask, then use Move or the numeric transform controls to position the mask independently. Painting, gradients, blur, and selection-limited filters use the mask's own coordinate system. Relinking retains its offset and carries it along with subsequent image transforms. Crop, resize, and canvas flips update placed masks. Outside a moved mask, coverage follows its predominant edge color, matching the original reveal/hide background rule. Placement and link state survive project save/reopen. Nonuniform transforms follow the native rotated-rectangle projection for offset, rotated linked masks. Exact pixel-grid flips now bypass interpolation to preserve pixel values.

Adjustment layers: Layer → New adjustment layer offers Hue/Saturation, Levels, Curves, Exposure, Gradient Map, and Grain. Edit adjustment reopens their controls with live preview, Cancel, and one-step undo. Levels/Curves retain independent RGB and color-channel settings; Hue/Saturation retains seven color ranges, editable falloff bands, Colorize, and range inversion. Masks, selection-based creation, opacity, blend modes, folders, clipping, and native v7 persistence are supported. Source pixels remain unchanged. Hue/Saturation evaluates the native HSL formulas directly; Core Image cube interpolation may produce small cross-platform differences.

Layer effects: Layer → Layer effects offers editable Stroke (inside/outside), Drop Shadow, Color Overlay, and Inner Shadow. Each effect can be included, hidden, recolored, or removed independently. Effects follow the layer’s visible masked shape, expand beyond its source bounds, and participate in transformations, clipping, merge operations, undo, exports, and project persistence. Live preview can be disabled on large layers. Shadows use the existing three-box Gaussian approximation; wide strokes use linear-time square morphology, matching the native stroke shape.

Document tabs: New canvas and Open project create separate tabs; reopening the same project focuses its existing tab. Each editor retains its selection, view, tools, and undo history. Close document (Ctrl+W), the tab close button, and Quit check unsaved changes. Layer → Copy selected layers to document copies the selection and folder descendants into another tab as one undo step, remaps IDs, and bakes external clipping dependencies. Geometry is retained in document coordinates. Tabs may be reordered. Drag selected layer rows onto another document’s tab to copy them there; the source stays intact, and undo in the destination removes the copy.

Selection editing: Select → Feather selection, Invert selection, Select layer pixels, and Select layer mask retain grayscale coverage. Painting, retouching, filter previews, adjustment creation, and clipboard extraction respect soft coverage. Drag inside a selection with a marquee/lasso tool to move its outline. Drag inside it with Move to lift and move the selected pixels onto a new layer; Alt-drag duplicates them and Shift constrains the axis. Escape restores the gesture. Copy (Ctrl+C), Cut (Ctrl+X), Copy Merged, Paste, and Duplicate selected pixels (Ctrl+J) are available in Edit. Pixel edits have undo history; selection outlines remain session state.

Transform handles: Move displays eight resize handles and a rotation handle. Shift keeps corner scaling proportional or snaps rotation to 15 degrees; Alt resizes around the center. Ctrl-drag a corner applies free distortion, rasterizing the edited shape at commit. Select multiple layers in the layer tree to move, resize, rotate, or edit their combined numeric geometry. Folder descendants and linked masks follow; an unlinked Paint mask target transforms independently. Escape cancels the draft and a completed gesture is one undo step. Convex shapes use perspective; folded and concave shapes use two affine triangles, matching the native editor’s approach. Collapsed or unbounded corners are rejected without changing the document.

Merging: Ctrl+E merges selected layers, a selected folder, or the active layer with its sibling below. Merge Layers supports blend modes, adjustments, effects, nested folders, and masks, and trims transparent margins. Like the native app, blend modes are evaluated within the merged subset; blending that depended on unselected backdrop layers can change. External clipping dependencies are baked before sources are removed.

Canvas navigation: Crop (C) drags an integral crop rectangle; Alt makes it symmetric around the start and Shift constrains it to a square. View → Show rulers exposes document coordinates and guide creation. View → Show pixel grid draws pixel boundaries at 8× zoom and above. Strong downsampling uses a cached area-filtered image for sharper reduced previews. Export shows the full output dimensions and a scaled PNG/JPEG preview; the JPEG quality control updates the compressed preview before saving.

## Acceptance status (2026-09-21)

All 69 QtTest cases pass with both the offscreen and native Wayland backends. The suite covers tools, selection constraints, undo/cancellation, masks, hierarchy, clipping, adjustments/effects, native-schema persistence and unknown-field rejection, imported HEIC/TIFF fixtures, tab copying, and export preview. QtTest dispatches widget input; it is not a replacement for testing every physical mouse gesture with a tablet or mouse.

A real Wayland keyboard smoke test verified Fit Canvas, export-dialog opening and Escape dismissal, tool shortcuts, and clean quit. A representative folder/text/shape/effect/adjustment project survived save/reopen with matching pixels and lossless PNG export. The packaged executable was installed to a temporary prefix containing spaces, its desktop entry validated, and its startup checked. The normal user installation has not been changed.

To regenerate the representative project, exported images, and editor screenshot after building tests:

```sh
COMPOSITOR_ACCEPTANCE_OUTPUT="$PWD/build/acceptance" QT_QPA_PLATFORM=wayland ./build/linux-tests/compositor-tests representativeProjectAndExports
```

The original Swift suite and actual Mac-generated cross-platform render comparisons require macOS and remain unverified. Qt font substitution/rasterization, Gaussian and motion blur approximations, 8-bit SDR color handling, folder opacity, and CPU rendering are documented differences. AI background removal and AI object selection are excluded by the lightweight/manual-selection scope. Tablet pressure is not implemented in either the inspected original brush controls or this port.
