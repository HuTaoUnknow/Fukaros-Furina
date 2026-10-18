#ifndef PackageDir
  #define PackageDir "..\bin"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
[Setup]
AppId={{0FDE6CAB-CF9D-4B29-82B7-4D7C951C6141}
AppName=Fukaros
AppVersion=0.4.18
AppPublisher=HuTaoUnknow
AppPublisherURL=https://github.com/HuTaoUnknow/Fukaros-Furina
AppSupportURL=https://github.com/HuTaoUnknow/Fukaros-Furina/issues
AppUpdatesURL=https://github.com/HuTaoUnknow/Fukaros-Furina/releases
DefaultDirName={localappdata}\Programs\Fukaros
DefaultGroupName=Fukaros
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Fukaros-0.4.18-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile=app.ico
UninstallDisplayIcon={app}\Fukaros.exe
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Fukaros"; Filename: "{app}\Fukaros.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Fukaros"; Filename: "{app}\Fukaros.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Fukaros.exe"; Description: "Launch Fukaros"; Flags: nowait postinstall skipifsilent
