; Inno Setup Script for Halo Desktop
; Builds native Windows installer packages for x64 and ARM64

#ifndef AppArch
  #define AppArch "x64"
#endif

#ifndef SourceDir
  #define SourceDir "..\build\Release"
#endif

#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#define MyAppName "Halo Desktop"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Halo Desktop Team"
#define MyAppExeName "LumnOverlay.exe"
#define MyAppId "{{E794D024-C272-4B6E-AE21-08579FF48E44}"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Halo Desktop
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=Halo-Desktop-Setup-{#AppArch}
SetupIconFile=..\resources\icons\halo.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline dialog
AppMutex=Local\HaloDesktop_SingleInstance_Mutex_9F8D3B
CloseApplications=yes
RestartApplications=no

#if AppArch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Primary Executable
Source: "{#SourceDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Required Runtime HLSL Shaders
Source: "..\src\render\shaders\plasmaRim.hlsl"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\src\render\shaders\liquidGlass.hlsl"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\src\render\shaders\glassmorphism.hlsl"; DestDir: "{app}"; Flags: ignoreversion

; Licences and Notices
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\licenses\Apache-2.0.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
; Start Menu Shortcut with explicit AppUserModelID for taskbar pinning and grouping
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"; AppUserModelID: "Halo.Desktop.App"

; Optional Desktop Shortcut
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; IconFilename: "{app}\{#MyAppExeName}"; AppUserModelID: "Halo.Desktop.App"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

