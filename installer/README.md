# Installer resources

The files needed to build the **Rumble** installers for Windows and macOS. The
licence text each installer shows lives here, so both platforms draw from one
source.

| Path                    | Purpose                                                     |
|-------------------------|-------------------------------------------------------------|
| `EULA.txt`              | Licence text, pure ASCII (macOS `.pkg`, installed copy)      |
| `EULA.rtf`              | Licence text, rich text (Windows installer licence page)     |
| `make_license_files.py` | Regenerates `EULA.txt` / `EULA.rtf` from the PDF             |
| `windows/Rumble.iss`    | Inno Setup 6 script -> Windows setup `.exe`                  |
| `macos/build_pkg.sh`    | `pkgbuild` + `productbuild` script -> macOS `.pkg`           |

The **canonical EULA** is the PDF in the repository root,
`End User License Agreement - FuzzyAudio - Rumble.pdf`. Installers cannot show
a PDF, so `EULA.txt` and `EULA.rtf` are generated from it. After changing the
PDF, regenerate them (needs `pdftotext` from poppler) and commit the result:

```sh
python3 installer/make_license_files.py
```

The text is checked word-for-word against the PDF (2708 words, exact match).

## Building the installers

**CI builds both on every push** (the *Installer* step in
`.github/workflows/build.yml`): they are in the **Rumble-Windows** /
**Rumble-macOS** artifacts of each run, and attached to the GitHub Release when
you push a version tag.

To build one by hand, put the built plugin in an `artifacts/` folder next to
the script (git-ignored), from `build/Rumble_artefacts/Release/`:

### Windows — Inno Setup 6

```
installer\windows\artifacts\Rumble.vst3\   (the bundle folder)
installer\windows\artifacts\Rumble.exe     (optional, Standalone)

iscc /DAppVersion=0.26.0 installer\windows\Rumble.iss
```

Output: `installer\windows\output\Rumble-0.26.0-Windows-x64.exe`. It shows
`EULA.rtf`, installs the VST3 to a folder the user can change (default
`C:\Program Files\Common Files\VST3\Rumble.vst3`), the Standalone to
`C:\Program Files\Fuzzy Audio\Rumble\`, and the EULA and Inter font licence to
`...\Fuzzy Audio\Rumble\Licenses\`. It registers an uninstaller.

The `AppId` GUID in `Rumble.iss` is what Windows uses to recognise upgrades —
never change it, and never reuse it for another product.

### macOS — pkgbuild + productbuild

```
installer/macos/artifacts/Rumble.component   (AU)
installer/macos/artifacts/Rumble.vst3        (VST3)
installer/macos/artifacts/Rumble.app         (optional, Standalone)

installer/macos/build_pkg.sh 0.26.0
```

Output: `installer/macos/output/Rumble-0.26.0-macOS.pkg`. It shows `EULA.txt`
and installs the AU to `/Library/Audio/Plug-Ins/Components`, the VST3 to
`/Library/Audio/Plug-Ins/VST3`, the Standalone to `/Applications`, and the
licences to `/Library/Application Support/Fuzzy Audio/Rumble/Licenses`. The
user can untick AU, VST3 or Standalone under *Customize*.

## Signing

Both installers are **unsigned** until you add certificates, so users see a
warning the first time (Windows SmartScreen: *More info → Run anyway*; macOS:
right-click the `.pkg` → *Open*). To remove it:

- **macOS** — needs an Apple Developer account. Set these before running
  `build_pkg.sh`:
  `APP_SIGN_ID="Developer ID Application: Name (TEAMID)"` (signs the bundles),
  `INSTALLER_SIGN_ID="Developer ID Installer: Name (TEAMID)"` (signs the
  `.pkg`) and `NOTARY_PROFILE=<notarytool keychain profile>` (notarises and
  staples it).
- **Windows** — needs an Authenticode code-signing certificate; sign the setup
  `.exe` (and ideally `Rumble.vst3` / `Rumble.exe` before packaging) with
  `signtool sign`.

To do this in CI, store the certificates as GitHub secrets, import them in a
step before *Installer*, and set the variables above.

## Before the first public release

- [ ] Fill in the placeholders still in the EULA PDF: `[RUMBLE]`,
      `[Colorado]` and `[download/install]`, then regenerate the text files.
- [ ] Put the real licence public key in `src/LicenseKey.h`
      (`docs/LICENSING.md`). CI will not publish a release until you do.
- [ ] Replace `fuzzyaudio.example` with the real website: `AppURL` in
      `windows/Rumble.iss`, and `websiteName` / `websiteURL` in
      `RumbleAudioProcessor::makeLicenseConfig()`.
- [ ] Code signing (above).
