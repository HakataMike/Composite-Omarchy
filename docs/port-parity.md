# Linux port parity checklist

Scope: the macOS app in this checkout, including behavior described in README.md and its Swift implementations. A feature is complete only when usable from the Qt UI, preserved where applicable by project save/load, and covered by relevant verification. Similar-looking output alone does not establish parity.

## Foundation
- [x] Native Qt/Wayland editor, import, raster layers, basic transforms, history, PNG/JPEG export
- [x] Brush/eraser, hardness, opacity, linked raster masks
- [x] Regional painting redraw on pixel-aligned large documents (transformed layers retain exact full redraw)
- [ ] Full v1–8 project format with fixture verification
- [ ] Linux packaging, launcher, release build and installation instructions

## Layer workflow
- [ ] Folders, nesting/reordering, group transforms and masks (implemented; broader physical drag/drop acceptance remains)
- [x] Clipping stacks and dependency masks, bake/release, source deletion preservation
- [x] Independent mask transforms, link/unlink, painting in mask coordinates, persistence
- [x] Shear-producing group and linked-mask transforms (native rotated-rectangle projection)
- [x] Mask fill/invert/blur/feather (Gaussian approximation documented)
- [x] Merge Down / Merge Layers / Merge Group
- [x] Adjustment layers: Hue/Saturation, Levels, Curves, Exposure, Gradient Map, Grain
- [x] All 14 blend modes (reference-color and transparency tests; macOS cross-render fixtures remain)
- [x] Layer stroke, drop shadow, color overlay, and inner shadow effects
- [x] Multiple documents/tabs and inter-document layer copying (menu workflow; direct drag between tabs remains a UI refinement)

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
- [x] Clone stamp with source marker, three healing modes, content-aware fill, blur tool
- [x] Levels/Auto, graphical Curves, selective Hue/Saturation, Exposure, Gradient Map, Grain/Noise, Invert
- [x] Gaussian/motion blur with expanded bounds, lens correction (CPU approximations documented)
- Automatic AI background removal is intentionally excluded from the Linux port to keep installation small. Use selection tools and layer masks for manual background removal.
- [x] Live selection-limited previews

## Final acceptance
- [ ] Actual Wayland mouse/keyboard sessions for every tool and dialog dismissal
- [ ] Representative multilayer image editing, saving/reopening, and export comparisons
- [ ] HEIC/TIFF plugin availability and format behavior
- [x] High-quality downsampling, pixel grid, export preview
- [ ] No silent loss of unsupported document data
- [ ] Review all remaining deviations before calling this a full port
