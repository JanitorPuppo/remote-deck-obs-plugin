; Inno Setup script. Defines can be passed from ISCC:
;   /DMyAppVersion=0.5.0
;   /DSourceDir=release\RelWithDebInfo
;   /DOutputDir=release
;   /DOutputBaseFilename=obs-remote-deck-0.5.0-windows-installer

#define MyAppName "Remote Deck for OBS"
#ifndef MyAppVersion
  #define MyAppVersion "0.5.0"
#endif
#define MyAppPublisher "Remote Deck"
#ifndef SourceDir
  #define SourceDir "..\..\..\release\RelWithDebInfo"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\..\release"
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "obs-remote-deck-windows-installer"
#endif

[Setup]
AppId={{B7E4C91A-3F2D-4A68-9E1B-6C5D8A0F2E14}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://remotedeck.gg
AppSupportURL=https://github.com/JanitorPuppo/remote-deck-obs-plugin
AppMutex={#MyAppName}
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} Setup
DefaultDirName={commonappdata}\obs-studio\plugins\obs-remote-deck
DisableDirPage=yes
DisableProgramGroupPage=yes
AllowNoIcons=yes
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma2/ultra64
SolidCompression=yes
LZMAAlgorithm=1
WizardStyle=modern
WizardResizable=yes
PrivilegesRequired=admin
DirExistsWarning=no
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={uninstallexe}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
FinishedLabel=Remote Deck is installed.%n%nOpen OBS. Click Tools, then Remote Deck, and sign in with your browser.

[Files]
Source: "{#SourceDir}\obs-remote-deck\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
function IsAppRunning(const FileName: string): Boolean;
var
  FSWbemLocator: Variant;
  FWMIService: Variant;
  FWbemObjectSet: Variant;
begin
  Result := False;
  try
    FSWbemLocator := CreateOleObject('WBEMScripting.SWbemLocator');
    FWMIService := FSWbemLocator.ConnectServer('', 'root\CIMV2', '', '');
    FWbemObjectSet := FWMIService.ExecQuery(
      Format('SELECT Name FROM Win32_Process WHERE Name="%s"', [FileName]));
    Result := (FWbemObjectSet.Count > 0);
  except
  end;
end;

function InitializeSetup(): Boolean;
begin
  if IsAppRunning('obs64.exe') or IsAppRunning('obs.exe') then
  begin
    MsgBox('Quit OBS first. In OBS, click File, then Exit. Then run this installer again.',
      mbError, MB_OK);
    Result := False;
    exit;
  end;
  Result := True;
end;

function GetUninstallString(): String;
var
  sUnInstPath: String;
  sUnInstallString: String;
begin
  sUnInstPath := ExpandConstant('Software\Microsoft\Windows\CurrentVersion\Uninstall\{#emit SetupSetting("AppId")}_is1');
  sUnInstallString := '';
  if not RegQueryStringValue(HKLM, sUnInstPath, 'UninstallString', sUnInstallString) then
    RegQueryStringValue(HKCU, sUnInstPath, 'UninstallString', sUnInstallString);
  Result := sUnInstallString;
end;

function UnInstallOldVersion(): Integer;
var
  sUnInstallString: String;
  iResultCode: Integer;
begin
  Result := 0;
  sUnInstallString := GetUninstallString();
  if sUnInstallString <> '' then
  begin
    sUnInstallString := RemoveQuotes(sUnInstallString);
    if Exec(sUnInstallString, '/VERYSILENT /NORESTART /SUPPRESSMSGBOXES', '', SW_HIDE, ewWaitUntilTerminated, iResultCode) then
      Result := 3
    else
      Result := 2;
  end
  else
    Result := 1;
end;

function IsUpgrade(): Boolean;
begin
  Result := (GetUninstallString() <> '');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    if IsUpgrade() then
      UnInstallOldVersion();
  end;
end;
