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
DefaultDirName={commoncf64}\VST3\Keys.vst3
DisableDirPage=yes
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\release
OutputBaseFilename=KeysSetup-{#Version}
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
UninstallDisplayIcon={app}\Contents\x86_64-win\Keys.vst3

[Files]
Source: "..\build\Keys_artefacts\Release\VST3\Keys.vst3\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Messages]
SetupWindowTitle=Keys %1 Setup
