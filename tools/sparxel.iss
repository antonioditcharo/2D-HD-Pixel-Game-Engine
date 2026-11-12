; Sparxel's Windows installer, built with Inno Setup 6 (ISCC) from the folder that
; tools/package_windows.cmake makes. CI builds it, installs it silently, starts the
; installed editor on the sample, and uninstalls it again (.github/workflows/ci.yml).
;
;   ISCC /DSourceDir=<package folder> /DOutputDir=<where to put it> tools\sparxel.iss
;
; It installs for the current user by default (no administrator rights, and the sample
; stays editable), into %LOCALAPPDATA%\Programs\Sparxel; it can install for everyone
; instead, from the first page. See docs/dev/editor.md, section 5.

#ifndef SourceDir
  #define SourceDir "..\package\Sparxel"
#endif
#ifndef OutputDir
  #define OutputDir "..\installer"
#endif
#ifndef AppVersion
  #define AppVersion "0.0.1"
#endif

[Setup]
; The same for every version, so a new one installs over the old.
AppId={{6F1B6E2A-9C3D-4E57-8A41-2D3C5B7E9F10}
AppName=Sparxel
AppVersion={#AppVersion}
AppPublisher=Sparxel contributors
DefaultDirName={autopf}\Sparxel
DefaultGroupName=Sparxel
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=Sparxel-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile={#SourceDir}\LICENSE.txt
UninstallDisplayIcon={app}\sparxel-editor.exe
UninstallDisplayName=Sparxel
SetupLogging=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Sparxel"; Filename: "{app}\sparxel-editor.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Sparxel"; Filename: "{app}\sparxel-editor.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\sparxel-editor.exe"; Parameters: """{app}\samples\hello"""; WorkingDir: "{app}"; Description: "Open Sparxel with the sample game"; Flags: postinstall nowait skipifsilent
Filename: "{app}\README.txt"; Description: "Read what to try first"; Flags: postinstall shellexec skipifsilent unchecked
