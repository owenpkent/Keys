; Keys installer (Inno Setup 6). Build via: .\build.ps1 -Installer
; The auto-updater requires the output filename to be exactly KeysSetup-{version}.exe.

#ifndef Version
  #define Version "0.1.0"
#endif

[Setup]
AppId={{B2E7A9C4-1F5D-4A8E-9C31-7D0E6F2A4B15}
AppName=Keys
AppVersion={#Version}
AppPublisher=OK Studio
AppPublisherURL=https://github.com/owenpkent/Keys

; {app} is the VST3 *folder*; the bundle is installed into it as Keys.vst3.
;
; Up to and including 0.2.0 this was the other way round - {app} was the bundle
; itself (...\VST3\Keys.vst3) with the directory page disabled, so nobody was
; ever asked where the plug-in went. That is why UsePreviousAppDir must stay
; off: Inno would otherwise remember a 0.2.0 install's {app} and, since the
; [Files] entry now appends the bundle name itself, land the plug-in at
; ...\VST3\Keys.vst3\Keys.vst3, which no host would ever find.
;
; The default is the canonical location every VST3 host scans. It is a default
; rather than a fixed destination because a DAW can be pointed at a custom VST3
; folder instead (Ableton's "VST3 Plug-In Custom Folder" is the common case),
; and an installer that cannot reach it is one that installs where the user
; cannot load from.
DefaultDirName={commoncf64}\VST3
UsePreviousAppDir=no
; The VST3 folder always already exists, so Inno's "that folder exists, install
; anyway?" prompt would fire on the default path every single time.
DirExistsWarning=no

DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\release
OutputBaseFilename=KeysSetup-{#Version}

; Branding. All three are generated from assets/Keys.svg by
; installer/generate_brand.py and committed, so a build (and CI) never needs
; Pillow - rerun the script only when the mark itself changes.
SetupIconFile=..\assets\Keys.ico
WizardStyle=modern
WizardImageFile=wizard_large.bmp
WizardSmallImageFile=wizard_small.bmp
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
; If the DAW has Keys loaded, Inno's restart-manager page asks the user to close it.
; We never kill a DAW (unsaved projects). No silent-install support for the same reason.
CloseApplications=yes
RestartApplications=no
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
UninstallDisplayName=Keys VST3
UninstallDisplayIcon={app}\Keys.vst3\Contents\x86_64-win\Keys.vst3

[Files]
Source: "..\build\Keys_artefacts\Release\VST3\Keys.vst3\*"; DestDir: "{app}\Keys.vst3"; Flags: ignoreversion recursesubdirs

[UninstallDelete]
; The bundle is a folder Setup created inside {app}; remove it whole rather than
; leaving the empty Contents tree behind.
Type: filesandordirs; Name: "{app}\Keys.vst3"

[Messages]
SetupWindowTitle=Keys %1 Setup
WizardSelectDir=Select VST3 folder
SelectDirDesc=Where should the Keys plug-in be installed?
SelectDirLabel3=Setup will install Keys.vst3 into the folder below. The default is the standard VST3 location, which every host scans. If your DAW is set to scan a custom VST3 folder, point Setup at that folder instead.
SelectDirBrowseLabel=To continue, click Next. To choose a different folder, click Browse.
