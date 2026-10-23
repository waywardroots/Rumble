; ============================================================================
;  Rumble - Windows installer  (Inno Setup 6)
;  (c) 2026 Fuzzy Audio LLC
; ----------------------------------------------------------------------------
;  Builds a setup .exe that installs the VST3 plug-in (and, if present, the
;  Standalone app). The wizard shows the Fuzzy Audio EULA and lets the user
;  choose both the VST3 folder and the app folder. Inno Setup is free:
;  https://jrsoftware.org
;
;  CI builds this automatically (see .github/workflows/build.yml). By hand,
;  from the repo root on Windows:
;     1. Put the built plug-in in installer\windows\artifacts\:
;            installer\windows\artifacts\Rumble.vst3\   (the bundle folder)
;            installer\windows\artifacts\Rumble.exe     (optional, Standalone)
;        (from build\Rumble_artefacts\Release\, or the Rumble-Windows zips.)
;     2. Compile:
;            iscc /DAppVersion=0.26.0 installer\windows\Rumble.iss
;        The installer lands in installer\windows\output\.
;
;  The licence shown by the wizard is the Fuzzy Audio EULA (..\EULA.rtf,
;  generated from the PDF by installer\make_license_files.py).
; ============================================================================

#define AppName      "Rumble"
#define AppPublisher "Fuzzy Audio LLC"
#define AppURL       "https://fuzzyaudio.example"      ; TODO: real website
#define AppExe       "Rumble.exe"

; Version can be overridden on the command line:  iscc /DAppVersion=1.2.3 ...
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

; Where the built plug-in/app live, relative to this .iss file.
#ifndef ArtifactsDir
  #define ArtifactsDir "artifacts"
#endif
#define Vst3Src       ArtifactsDir + "\" + AppName + ".vst3"
#define StandaloneSrc ArtifactsDir + "\" + AppExe

; Only offer the Standalone app if it was provided.
#define HaveStandalone FileExists(AddBackslash(SourcePath) + StandaloneSrc)

[Setup]
; AppId identifies Rumble for upgrades/uninstall - never change it, and never
; reuse it for another product.
AppId={{B256A905-CECC-438D-9577-3311A537961D}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppCopyright=Copyright (C) 2026 Fuzzy Audio LLC
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Fuzzy Audio\{#AppName}
DefaultGroupName=Fuzzy Audio\{#AppName}
DisableProgramGroupPage=yes
; The standard destination page picks where the Standalone app, licences and
; uninstaller go; a custom page (see [Code]) picks the VST3 folder.
DisableDirPage=no
LicenseFile=..\EULA.rtf
OutputDir=output
OutputBaseFilename=Rumble-{#AppVersion}-Windows-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; VST3 goes to the shared Common Files\VST3 folder, which needs admin rights.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName={#AppName} {#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (64-bit)"; Types: full custom; Flags: fixed
#if HaveStandalone
Name: "standalone"; Description: "Standalone application"; Types: full custom
#endif

[Files]
; --- VST3 bundle -> user-selected VST3 folder (default: Common Files\VST3) ---
Source: "{#Vst3Src}\*"; DestDir: "{code:GetVST3Dir}\{#AppName}.vst3"; \
    Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
#if HaveStandalone
; --- Standalone application -> {app} ---------------------------------------
Source: "{#StandaloneSrc}"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
#endif
; --- Licences: the EULA, and the embedded Inter font's licence (SIL OFL 1.1),
;     which must accompany every copy of the font. ---------------------------
Source: "..\EULA.txt"; DestDir: "{app}\Licenses"; DestName: "Rumble EULA.txt"; Flags: ignoreversion
Source: "..\..\assets\fonts\Inter-OFL.txt"; DestDir: "{app}\Licenses"; Flags: ignoreversion

[Icons]
#if HaveStandalone
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; Components: standalone
#endif
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

[Code]
// A second wizard page (after the standard destination page) to choose the
// folder the VST3 plug-in goes into. Defaults to Common Files\VST3, which
// every VST3 host scans.
var
  Vst3DirPage: TInputDirWizardPage;

procedure InitializeWizard;
begin
  Vst3DirPage := CreateInputDirPage (wpSelectDir,
    'Select VST3 Location',
    'Where should the VST3 plug-in be installed?',
    'Setup will install the {#AppName} VST3 plug-in into the following folder.'
      + #13#10#13#10 +
    'Most DAWs scan the default folder shown below, so only change it if your host '
      + 'looks elsewhere. To continue, click Next. To pick a different folder, click Browse.',
    False, '');
  Vst3DirPage.Add ('');
  Vst3DirPage.Values[0] := ExpandConstant ('{commoncf64}\VST3');
end;

function GetVST3Dir (Param: String): String;
begin
  Result := Vst3DirPage.Values[0];
end;
