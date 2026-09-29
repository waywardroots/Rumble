# assets

Brand artwork. `logo.png` is compiled into the plugin by
`juce_add_binary_data` in the top-level `CMakeLists.txt` and drawn in the
top-left of the title bar. Replacing the file and rebuilding is enough to
change it; nothing else needs editing.

## Logo

Name the file `logo.png` or `logo.svg` so the build can find it without
further configuration.

**SVG is preferred.** It scales to any window size and any display density
with no blurring, and JUCE renders it directly via `Drawable::createFromSVG`.

**If you supply a PNG:**

| | |
|---|---|
| Format | PNG with a transparent background |
| Height | at least 150 px (it displays at ~26 px, and the extra covers 2x/3x displays) |
| Colour | light artwork — it sits on a near-black title bar (`#23242A`) |
| Padding | trim tight to the artwork; the layout adds its own spacing |

A dark-on-transparent logo will be invisible against the background. If your
logo only exists in a dark version, send it anyway and I'll invert or
recolour it for the dark UI.

## Where it goes

The title bar is 64 px tall and the logo is drawn as a 44 px square badge at
the top left, with the "RUMBLE" wordmark to its right and the preset selector
at the far right. The image is scaled to fit while preserving its aspect
ratio, so a non-square replacement will letterbox inside the 44 px box rather
than distort.

## Other artwork

Background textures, panel graphics or an icon for the standalone app can go
here too. For the standalone and installer icon, a square PNG of 512x512 or
1024x1024 works for both macOS and Windows.
