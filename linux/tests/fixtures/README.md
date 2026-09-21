# Import fixtures

These 32 × 24 synthetic color images were generated for this project; they contain no third-party photographs.

```sh
magick -size 32x24 xc:red -fill blue -draw 'rectangle 16,0 31,23' -fill '#00ff0080' -draw 'rectangle 0,12 15,23' source.png
heif-enc -L source.png -o colors.heic
magick source.png colors.tiff
```

Generated using libheif 1.23.4. Tests check dimensions, color samples, malformed input rejection, and persistence in a native project. The commands are fixture-generation tools only; ImageMagick and the encoder are not runtime dependencies.
