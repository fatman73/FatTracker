; FatTracker installer for Windows (Inno Setup 6): the program goes to Program Files, the user's settings and
; files live in %APPDATA%\FatTracker (made by the program itself).
; Build: "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\FatTracker.iss
;        (after cmake --build build --config Release); the setup lands in build\installer\.

#define AppVersion "0.1"

[Setup]
AppId={{6F1C2A9E-3B7D-4E21-9A55-FA77A0C0FEED}
AppName=FatTracker
AppVersion={#AppVersion}
AppPublisher=fatman73
AppPublisherURL=https://github.com/fatman73/FatTracker
DefaultDirName={autopf}\FatTracker
DefaultGroupName=FatTracker
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\build\installer
OutputBaseFilename=FatTracker-{#AppVersion}-setup
SetupIconFile=..\res\icon.ico
UninstallDisplayIcon={app}\FatTracker.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\Release\FatTracker.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\CREDITS.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\FatTracker"; Filename: "{app}\FatTracker.exe"
Name: "{group}\{cm:UninstallProgram,FatTracker}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\FatTracker"; Filename: "{app}\FatTracker.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\FatTracker.exe"; Description: "{cm:LaunchProgram,FatTracker}"; Flags: nowait postinstall skipifsilent
