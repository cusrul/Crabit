#ifndef BundleDir
  #error BundleDir must name a clean, complete release bundle including licenses.
#endif
#ifndef OutputDir
  #error OutputDir is required.
#endif
#ifndef AppVersion
  #define AppVersion "0.2.0-preview.1"
#endif
[Setup]
AppId={{D61484F2-D0F8-4B2C-9D61-B46F9BE6B7AB}
AppName=Crabit
AppVersion={#AppVersion}
AppPublisher=ZZOO
AppPublisherURL=https://github.com/cusrul/Crabit
DefaultDirName={localappdata}\Programs\Crabit
DefaultGroupName=Crabit
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Crabit-Setup-{#AppVersion}-x64
SetupIconFile=..\apps\mobile\windows\runner\resources\app_icon.ico
UninstallDisplayIcon={app}\crabit_watch_app.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
InfoBeforeFile=windows-installation.txt

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#BundleDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb,*.WebView2\*,*.log"

[Icons]
Name: "{group}\Crabit"; Filename: "{app}\crabit_watch_app.exe"
Name: "{autodesktop}\Crabit"; Filename: "{app}\crabit_watch_app.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\crabit_watch_app.exe"; Description: "Open Crabit"; Flags: nowait postinstall skipifsilent

