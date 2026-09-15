; Inno Setup script. Defines can be passed from ISCC:
;   /DMyAppVersion=0.6.6
;   /DSourceDir=release\RelWithDebInfo
;   /DOutputDir=release
;   /DOutputBaseFilename=obs-remote-deck-0.6.6-windows-installer

#define MyAppName "Remote Deck for OBS"
#ifndef MyAppVersion
  #define MyAppVersion "0.6.6"
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
DefaultDirName={userappdata}\obs-studio\plugins\obs-remote-deck
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
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
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

function UninstallRegPath(): String;
begin
  Result := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#emit SetupSetting("AppId")}_is1';
end;

function GetUninstallStringFromRoot(RootKey: Integer): String;
begin
  Result := '';
  RegQueryStringValue(RootKey, UninstallRegPath(), 'UninstallString', Result);
end;

function GetUninstallString(): String;
begin
  Result := GetUninstallStringFromRoot(HKLM);
  if Result = '' then
    Result := GetUninstallStringFromRoot(HKCU);
end;

function LegacyProgramDataDir(): String;
begin
  Result := ExpandConstant('{commonappdata}\obs-studio\plugins\obs-remote-deck');
end;

function LegacyProgramDataExists(): Boolean;
begin
  Result := DirExists(LegacyProgramDataDir());
end;

function NeedsLegacyMigration(): Boolean;
begin
  Result := LegacyProgramDataExists() or (GetUninstallStringFromRoot(HKLM) <> '');
end;

function ParamMigrateLegacy(): Boolean;
begin
  Result := ExpandConstant('{param:MIGRATELEGACY|0}') = '1';
end;

function BuildRelaunchParams(): String;
var
  Params: String;
begin
  Params := '';
  if WizardSilent() then
    Params := Params + ' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART';
  Params := Params + ' /CURRENTUSER /MIGRATELEGACY=1';
  Result := Trim(Params);
end;

function RelaunchElevatedForMigration(): Boolean;
var
  ResultCode: Integer;
  Params: String;
begin
  Params := BuildRelaunchParams();
  if ShellExec('runas', ExpandConstant('{srcexe}'), Params, '', SW_SHOW, ewNoWait, ResultCode) then
    Result := True
  else
    Result := False;
end;

function InitializeSetup(): Boolean;
begin
  if IsAppRunning('obs64.exe') or IsAppRunning('obs.exe') then
  begin
    if not WizardSilent() then
      MsgBox('Quit OBS first. In OBS, click File, then Exit. Then run this installer again.',
        mbError, MB_OK);
    Result := False;
    exit;
  end;

  if NeedsLegacyMigration() and not IsAdminInstallMode then
  begin
    if ParamMigrateLegacy() then
    begin
      if not WizardSilent() then
        MsgBox('This installer must run as administrator once to remove the older all-users copy.',
          mbError, MB_OK);
      Result := False;
      exit;
    end;

    if RelaunchElevatedForMigration() then
    begin
      Result := False;
      exit;
    end;

    if not WizardSilent() then
      MsgBox('Could not elevate to remove the older all-users install. Run the installer as administrator.',
        mbError, MB_OK);
    Result := False;
    exit;
  end;

  Result := True;
end;

function UnInstallOldVersion(): Integer;
var
  sUnInstallString: String;
  iResultCode: Integer;
  UninstallParams: String;
begin
  Result := 0;
  sUnInstallString := GetUninstallString();
  if sUnInstallString <> '' then
  begin
    sUnInstallString := RemoveQuotes(sUnInstallString);
    UninstallParams := '/VERYSILENT /NORESTART /SUPPRESSMSGBOXES';
    if not IsAdminInstallMode then
      UninstallParams := UninstallParams + ' /CURRENTUSER';
    if Exec(sUnInstallString, UninstallParams, '', SW_HIDE, ewWaitUntilTerminated, iResultCode) then
      Result := 3
    else
      Result := 2;
  end
  else
    Result := 1;
end;

function UninstallLegacyProgramData(): Boolean;
var
  sUnInstallString: String;
  iResultCode: Integer;
  LegacyDir: String;
begin
  Result := True;
  sUnInstallString := GetUninstallStringFromRoot(HKLM);
  if sUnInstallString <> '' then
  begin
    sUnInstallString := RemoveQuotes(sUnInstallString);
    if not Exec(sUnInstallString, '/VERYSILENT /NORESTART /SUPPRESSMSGBOXES', '', SW_HIDE, ewWaitUntilTerminated, iResultCode) then
      Result := False;
  end;

  LegacyDir := LegacyProgramDataDir();
  if DirExists(LegacyDir) then
  begin
    if not DelTree(LegacyDir, True, True, True) then
      Result := False;
  end;
end;

function IsUpgrade(): Boolean;
begin
  Result := (GetUninstallString() <> '');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    if IsAdminInstallMode and NeedsLegacyMigration() then
      UninstallLegacyProgramData();
    if IsUpgrade() then
      UnInstallOldVersion();
  end;
end;
