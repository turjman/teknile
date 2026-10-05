; SPDX-License-Identifier: Apache-2.0
; The Windows installer of EVRe Studio (Inno Setup 6), made by .github/workflows/release.yml:
;   ISCC /DAppVersion=1.0.0 /DSourceDir=<the deployed folder> /DOutputDir=<where the setup goes> evre_studio.iss
; The deployed folder holds EVReStudio.exe, evre.exe, evre-sim.exe, the Qt runtime (windeployqt), maps\, README.md,
; LICENSE and NOTICE. Per user by default (no administrator), a Start menu entry, an optional desktop icon, an
; uninstaller.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #error Pass the deployed folder: /DSourceDir=<folder>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{5B92510A-D552-482B-B9DB-E0B93B4CA9A7}
AppName=EVRe Studio
AppVersion={#AppVersion}
AppVerName=EVRe Studio {#AppVersion}
AppPublisher=teknile
AppPublisherURL=https://github.com/turjman/teknile
DefaultDirName={autopf}\EVRe Studio
DefaultGroupName=EVRe Studio
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#SourceDir}\LICENSE
SetupIconFile=..\icons\evre-studio.ico
UninstallDisplayIcon={app}\EVReStudio.exe
UninstallDisplayName=EVRe Studio {#AppVersion}
OutputDir={#OutputDir}
OutputBaseFilename=EVReStudio-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\EVRe Studio"; Filename: "{app}\EVReStudio.exe"
Name: "{group}\{cm:UninstallProgram,EVRe Studio}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\EVRe Studio"; Filename: "{app}\EVReStudio.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\EVReStudio.exe"; Description: "{cm:LaunchProgram,EVRe Studio}"; Flags: nowait postinstall skipifsilent
