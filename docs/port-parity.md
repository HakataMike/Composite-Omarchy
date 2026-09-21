# Linux port parity checklist

Scope: the macOS app in this checkout, including behavior described in README.md and its Swift implementations. A feature is complete only when usable from the Qt UI, preserved where applicable by project save/load, and covered by relevant verification. Similar-looking output alone does not establish parity. The non-AI Linux implementation is complete; the external acceptance items below remain explicitly unverified.

## Foundation
- [x] Native Qt/Wayland editor, import, raster layers, basic transforms, history, PNG/JPEG export
- [x] Brush/eraser, hardness, opacity, linked raster masks
- [x] Regional painting redraw on pixel-aligned large documents (transformed layers retain exact full redraw)
- [x] Version 1–8 schema loading, hand-authored legacy fixtures, supported feature records, and save/reopen regression checks; actual Mac-generated comparisons remain below
- [x] Linux packaging, launcher, stripped release archive, and installation instructions (system Qt/libheif dependencies)

## Layer workflow
- [x] Folders, nesting/reordering, Alt-drag duplication, group transforms and masks; stable selection indexes during dragging
- [x] Clipping stacks and dependency masks, bake/release, source deletion preservation
- [x] Independent mask transforms, link/unlink, painting in mask coordinates, persistence
- [x] Shear-producing group and linked-mask transforms (native rotated-rectangle projection)
- [x] Mask fill/invert/blur/feather (Gaussian approximation documented)
- [x] Merge Down / Merge Layers / Merge Group
- [x] Adjustment layers: Hue/Saturation, Levels, Curves, Exposure, Gradient Map, Grain
- [x] All 14 blend modes (reference-color and transparency tests; macOS cross-render fixtures remain)
- [x] Layer stroke, drop shadow, color overlay, and inner shadow effects
- [x] Multiple documents/tabs and inter-document layer copying through the menu or direct drops on destination tabs

## Selection and geometry
- [x] Rectangle selection constraining painting
- [x] Ellipse, freehand/polygonal lasso, magic wand
- [x] Add/subtract/invert/feather selections; load image/mask coverage
- [x] Move outlines/pixels, duplicate selected pixels, clipboard
- [x] Transform handles, multiple-layer transforms, perspective and folded/concave free distortion
- [x] Persistent guides, guide editing/locking/visibility, canvas flips
- [x] Snapping for movement, resize, shapes, selections, and crop; rulers and ruler guides
- [x] Interactive/symmetric crop, crop to selection bounds, canvas size, image size/resolution

## Tools and filters
- [x] Straight-line brush strokes
- [x] Eyedropper
- [x] Linear/radial, opaque/transparent/reversed gradients and editable shapes
- [x] Editable paragraph text with inline editing and font controls
- [x] Clone stamp with source marker, three healing modes, extending content-aware fill, blur, smudge, and liquify
- [x] Levels/Auto, graphical Curves, selective Hue/Saturation, Exposure, Gradient Map, Grain/Noise, Invert
- [x] Gaussian/motion blur with expanded bounds, lens correction (CPU approximations documented)
- Automatic AI background removal and AI object selection are intentionally excluded from the Linux port to keep installation small. Use selection tools and layer masks for manual background removal.
- [x] Live selection-limited previews

## Final acceptance
- [x] Complete QtTest suite on Wayland and offscreen (68 cases); real keyboard smoke test for shortcuts, export dismissal, and quit
- [ ] Exhaustive physical mouse/tablet acceptance for every tool and drag/drop gesture (QtTest input is widget-injected)
- [ ] Actual macOS-generated project and cross-render comparisons (requires macOS)
- [x] Representative multilayer project with editable text/shapes, folder, effect, and adjustment: save/reopen, PNG pixel equality, JPEG dimensions, and editor screenshot
- [x] HEIC/TIFF import with checked-in color fixtures, size limits, and project round trips (8-bit SDR; unsupported HEIF HDR reports an error)
- [x] High-quality downsampling, pixel grid, export preview
- [x] Unknown project data rejected; unsupported source data and format differences documented
- [x] Remaining deviations reviewed and documented in linux-port.md: Qt rendering/font differences, CPU performance, 8-bit SDR, Linux folder opacity extension, and excluded AI features
