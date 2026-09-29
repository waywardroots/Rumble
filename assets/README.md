# assets

Drop brand artwork here. Nothing in this folder is compiled into the plugin
yet — say the word once your file is in and I'll wire it into the title bar.

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

## Where it will go

The title bar is 42 px tall. The left side currently holds the word "RUMBLE"
in 23 pt bold; the right side holds the preset selector. A logo can either
replace the wordmark or sit beside it — tell me which you want.

Roughly 26 px of vertical space is usable, so a wide, short logo fits best.
Anything close to square will end up small. If your logo is tall or square,
the title bar can be made taller to suit it.

## Other artwork

Background textures, panel graphics or an icon for the standalone app can go
here too. For the standalone and installer icon, a square PNG of 512x512 or
1024x1024 works for both macOS and Windows.
