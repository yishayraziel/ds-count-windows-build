; DS-COUNT installer for Windows x64 (Inno Setup 6).
; Installs the VST3 for all users and the standalone app.
#ifndef AppVersion
 #define AppVersion "0.0.0"
#endif
#ifndef SourceVst3
 #error SourceVst3 must point to DS-COUNT.vst3
#endif
#ifndef SourceApp
 #error SourceApp must point to DS-COUNT.exe
#endif
[Setup]
AppId={{6F66198C-EC28-4273-A0F4-30C781766BF9}
AppName=DS-COUNT
AppVersion={#AppVersion}
AppVerName=DS-COUNT {#AppVersion}
AppPublisher=Yishay Raziel
AppPublisherURL=https://yraziel.com
DefaultDirName={commonpf64}\Yishay Raziel\DS-COUNT
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=..\..\dist
OutputBaseFilename=DS-COUNT-{#AppVersion}-Windows-x64-Setup
UninstallDisplayName=DS-COUNT {#AppVersion}
UninstallDisplayIcon={app}\DS-COUNT.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Types]
Name: "full"; Description: "VST3 plug-in and standalone app"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Common Files\VST3)"; Types: full custom; Flags: fixed
Name: "app"; Description: "Standalone app"; Types: full

[Files]
Source: "{#SourceVst3}\*"; DestDir: "{commoncf64}\VST3\DS-COUNT.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceApp}"; DestDir: "{app}"; Components: app; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\DS-COUNT"; Filename: "{app}\DS-COUNT.exe"; Components: app

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\DS-COUNT.vst3"
