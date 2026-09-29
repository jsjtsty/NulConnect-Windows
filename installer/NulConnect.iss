; Inno Setup script for NulConnect. Build with tools\package.ps1, which passes
; the version and the folder that holds the Release build:
;   ISCC /DAppVersion=0.1.0 /DSourceDir=..\build\x64\Release installer\NulConnect.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\x64\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
AppId={{6F2C8E4A-3B7D-4C1E-9A55-7D0E2B4F1A93}
AppName=NulConnect
AppVersion={#AppVersion}
AppVerName=NulConnect {#AppVersion}
AppPublisher=NulStudio
AppPublisherURL=https://github.com/jsjtsty/NulConnect-Windows
AppSupportURL=https://github.com/jsjtsty/NulConnect-Windows/issues
AppUpdatesURL=https://github.com/jsjtsty/NulConnect-Windows/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\NulConnect
DefaultGroupName=NulConnect
DisableProgramGroupPage=yes
; The privileged helper service is installed system-wide, so setup needs
; administrator rights anyway.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\LICENSE
SetupIconFile=..\NulConnect\res\NulConnect.ico
UninstallDisplayIcon={app}\NulConnect.exe
OutputDir={#OutputDir}
OutputBaseFilename=NulConnect-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Close a running NulConnect during setup and uninstall instead of failing on
; locked files, and offer to start it again afterwards.
CloseApplications=yes
RestartApplications=no
UsePreviousLanguage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "Languages\ChineseSimplified.isl"
Name: "chinesetraditional"; MessagesFile: "Languages\ChineseTraditional.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\NulConnect.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\reatrust.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\wintun.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\nulconnect-helper.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\NulConnect"; Filename: "{app}\NulConnect.exe"
Name: "{autodesktop}\NulConnect"; Filename: "{app}\NulConnect.exe"; Tasks: desktopicon

[Run]
; Register the helper service now, while setup is already elevated, so the
; first VPN or system-proxy use does not need another authorization. A failure
; here is not fatal: the app offers to install it on demand.
Filename: "{app}\nulconnect-helper.exe"; Parameters: "install"; Flags: runhidden waituntilterminated; StatusMsg: "NulConnect Helper..."
Filename: "{app}\NulConnect.exe"; Description: "{cm:LaunchProgram,NulConnect}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\nulconnect-helper.exe"; Parameters: "uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveHelper"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep <> usPostUninstall then Exit;
  DataDir := ExpandConstant('{localappdata}\NulConnect');
  if not DirExists(DataDir) then Exit;
  if UninstallSilent then Exit;
  if MsgBox(CustomMessage('RemoveUserData'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    DelTree(DataDir, True, True, True);
end;

[CustomMessages]
english.RemoveUserData=Also remove your NulConnect settings and logs?
chinesesimplified.RemoveUserData=是否同时删除 NulConnect 的设置和日志？
chinesetraditional.RemoveUserData=是否同時刪除 NulConnect 的設定與日誌？
japanese.RemoveUserData=NulConnect の設定とログも削除しますか？
german.RemoveUserData=Sollen auch die NulConnect-Einstellungen und -Protokolle gelöscht werden?
french.RemoveUserData=Supprimer aussi les paramètres et les journaux de NulConnect ?
spanish.RemoveUserData=¿Eliminar también la configuración y los registros de NulConnect?
