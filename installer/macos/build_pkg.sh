#!/usr/bin/env bash
# ============================================================================
#  Rumble - macOS installer builder  (.pkg)
#  (c) 2026 Fuzzy Audio LLC
# ----------------------------------------------------------------------------
#  Produces a distribution .pkg that installs the AU and VST3 plug-ins (and the
#  Standalone .app if present) into the standard macOS locations, showing the
#  Fuzzy Audio EULA during install. The EULA and the embedded Inter font's
#  licence are also installed to
#      /Library/Application Support/Fuzzy Audio/Rumble/Licenses
#
#  CI builds this automatically (see .github/workflows/build.yml). By hand,
#  on macOS:
#    1. Put the built bundles in installer/macos/artifacts/:
#           Rumble.component   (AU)
#           Rumble.vst3        (VST3)
#           Rumble.app         (optional, Standalone)
#       (from build/Rumble_artefacts/Release/, or the Rumble-macOS zips.)
#    2. Run:
#           installer/macos/build_pkg.sh 0.26.0
#       The installer lands in installer/macos/output/.
#
#  OPTIONAL signing / notarization (for public distribution) via env vars:
#    APP_SIGN_ID        "Developer ID Application: Name (TEAMID)"  - signs bundles
#    INSTALLER_SIGN_ID  "Developer ID Installer: Name (TEAMID)"    - signs the .pkg
#    NOTARY_PROFILE     a notarytool keychain profile              - notarize+staple
#  Without APP_SIGN_ID the bundles get an ad-hoc signature, which recent macOS
#  needs before it will load a plug-in at all.
# ============================================================================
set -euo pipefail

VERSION="${1:-1.0.0}"
HERE="$(cd "$(dirname "$0")" && pwd)"
INSTALLER_DIR="$(cd "$HERE/.." && pwd)"
REPO_DIR="$(cd "$INSTALLER_DIR/.." && pwd)"
ART="$HERE/artifacts"
BUILD="$HERE/build"
OUT="$HERE/output"

APP_NAME="Rumble"
PKGID="com.fuzzyaudio.rumble"
TITLE="Rumble"

AU_SRC="$ART/$APP_NAME.component"
VST3_SRC="$ART/$APP_NAME.vst3"
APP_SRC="$ART/$APP_NAME.app"

rm -rf "$BUILD" "$OUT"
mkdir -p "$BUILD" "$OUT"

# --- helper: codesign a bundle (Developer ID if given, else ad-hoc) ----------
sign_bundle () {
  local bundle="$1"
  if [[ -n "${APP_SIGN_ID:-}" ]]; then
    echo "  codesign: $bundle"
    codesign --force --deep --options runtime --timestamp \
      --sign "$APP_SIGN_ID" "$bundle"
  else
    codesign --force --deep --sign - "$bundle"
  fi
}

# --- helper: build one component pkg (bundle -> install location) ------------
# args: <source-bundle> <install-location> <id-suffix> <out-var-name>
build_component () {
  local src="$1" dest="$2" suffix="$3" __outvar="$4"
  [[ -d "$src" ]] || { eval "$__outvar=''"; return; }
  local root="$BUILD/root-$suffix"
  mkdir -p "$root"
  cp -R "$src" "$root/"
  sign_bundle "$root/$(basename "$src")"

  # By default Installer "relocates" a bundle to wherever it finds an
  # existing copy with the same ID (e.g. an old build in ~/Library), so the
  # new version could silently land in the wrong place. Pin it.
  local plist="$BUILD/$suffix-components.plist"
  pkgbuild --analyze --root "$root" "$plist"
  local i=0
  while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist"
    i=$((i + 1))
  done

  local pkg="$BUILD/Rumble-$suffix.pkg"
  echo "pkgbuild: $suffix -> $dest"
  pkgbuild --identifier "$PKGID.$suffix" --version "$VERSION" \
           --install-location "$dest" --root "$root" \
           --component-plist "$plist" "$pkg"
  eval "$__outvar=\"\$pkg\""
}

AU_PKG=""; VST3_PKG=""; APP_PKG=""
build_component "$AU_SRC"   "/Library/Audio/Plug-Ins/Components" "au"         AU_PKG
build_component "$VST3_SRC" "/Library/Audio/Plug-Ins/VST3"       "vst3"       VST3_PKG
build_component "$APP_SRC"  "/Applications"                      "standalone" APP_PKG

if [[ -z "$AU_PKG$VST3_PKG$APP_PKG" ]]; then
  echo "ERROR: no artifacts found in $ART" >&2
  echo "       expected '$APP_NAME.component', '$APP_NAME.vst3' and/or '$APP_NAME.app'" >&2
  exit 1
fi

# --- licences: always installed, not a user choice ---------------------------
DOCS_ROOT="$BUILD/root-licenses"
mkdir -p "$DOCS_ROOT"
cp "$INSTALLER_DIR/EULA.txt" "$DOCS_ROOT/Rumble EULA.txt"
cp "$REPO_DIR/assets/fonts/Inter-OFL.txt" "$DOCS_ROOT/Inter-OFL.txt"
DOCS_PKG="$BUILD/Rumble-licenses.pkg"
pkgbuild --identifier "$PKGID.licenses" --version "$VERSION" \
         --install-location "/Library/Application Support/Fuzzy Audio/Rumble/Licenses" \
         --root "$DOCS_ROOT" "$DOCS_PKG"

# --- generate distribution.xml (only the components we actually built) -------
DIST="$BUILD/distribution.xml"
{
  echo '<?xml version="1.0" encoding="utf-8"?>'
  echo '<installer-gui-script minSpecVersion="2">'
  echo "  <title>$TITLE</title>"
  echo "  <organization>com.fuzzyaudio</organization>"
  echo '  <license file="EULA.txt"/>'
  echo '  <domains enable_localSystem="true" enable_currentUserHome="false" enable_anywhere="false"/>'
  echo '  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>'
  echo '  <choices-outline>'
  [[ -n "$AU_PKG"   ]] && echo '    <line choice="au"/>'
  [[ -n "$VST3_PKG" ]] && echo '    <line choice="vst3"/>'
  [[ -n "$APP_PKG"  ]] && echo '    <line choice="standalone"/>'
  echo '    <line choice="licenses"/>'
  echo '  </choices-outline>'
  if [[ -n "$AU_PKG" ]]; then
    echo '  <choice id="au" title="Audio Unit (AU)">'
    echo "    <pkg-ref id=\"$PKGID.au\"/>"
    echo '  </choice>'
    echo "  <pkg-ref id=\"$PKGID.au\" version=\"$VERSION\" onConclusion=\"none\">$(basename "$AU_PKG")</pkg-ref>"
  fi
  if [[ -n "$VST3_PKG" ]]; then
    echo '  <choice id="vst3" title="VST3">'
    echo "    <pkg-ref id=\"$PKGID.vst3\"/>"
    echo '  </choice>'
    echo "  <pkg-ref id=\"$PKGID.vst3\" version=\"$VERSION\" onConclusion=\"none\">$(basename "$VST3_PKG")</pkg-ref>"
  fi
  if [[ -n "$APP_PKG" ]]; then
    echo '  <choice id="standalone" title="Standalone application">'
    echo "    <pkg-ref id=\"$PKGID.standalone\"/>"
    echo '  </choice>'
    echo "  <pkg-ref id=\"$PKGID.standalone\" version=\"$VERSION\" onConclusion=\"none\">$(basename "$APP_PKG")</pkg-ref>"
  fi
  echo '  <choice id="licenses" title="Licences" visible="false" enabled="false" selected="true">'
  echo "    <pkg-ref id=\"$PKGID.licenses\"/>"
  echo '  </choice>'
  echo "  <pkg-ref id=\"$PKGID.licenses\" version=\"$VERSION\" onConclusion=\"none\">$(basename "$DOCS_PKG")</pkg-ref>"
  echo '</installer-gui-script>'
} > "$DIST"

# --- combine into a single distribution .pkg --------------------------------
# Everything in --resources is copied into the .pkg, so give it a folder
# holding just the EULA rather than the whole installer/ tree.
RES="$BUILD/resources"
mkdir -p "$RES"
cp "$INSTALLER_DIR/EULA.txt" "$RES/EULA.txt"
FINAL="$OUT/Rumble-$VERSION-macOS.pkg"
echo "productbuild: $FINAL"
if [[ -n "${INSTALLER_SIGN_ID:-}" ]]; then
  productbuild --distribution "$DIST" --package-path "$BUILD" \
    --resources "$RES" --sign "$INSTALLER_SIGN_ID" "$FINAL"
else
  productbuild --distribution "$DIST" --package-path "$BUILD" \
    --resources "$RES" "$FINAL"
fi

# --- optional notarization + stapling ---------------------------------------
if [[ -n "${NOTARY_PROFILE:-}" ]]; then
  echo "notarytool: submitting $FINAL"
  xcrun notarytool submit "$FINAL" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$FINAL"
fi

echo "Done: $FINAL"
