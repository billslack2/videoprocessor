#if Ver != EncodeVer(6, 7, 3)
  #error Use the qualified Inno Setup 6.7.3 compiler.
#endif
; Identity and payload include are supplied by tools/build_installer.ps1.
#ifndef PayloadRoot
  #error Build this installer with tools/build_installer.ps1.
#endif
#ifdef ConfigOnly
  #define AppGuid "BA15DBE8-210F-42AA-AE86-B4628395E77F"
  #define AppIdentity "{{BA15DBE8-210F-42AA-AE86-B4628395E77F}"
  #define AppDisplay "VideoProcessor Config"
  #define AppFolder "VideoProcessorConfig"
  #define AppProduct "VideoProcessorConfig-BA15DBE8-210F-42AA-AE86-B4628395E77F"
  #define AppIcon "config\VideoProcessorConfig.exe"
  #define ConfigArguments "--discover"
#else
  #define AppGuid "42D852F1-70E9-43ED-8739-D61752106D59"
  #define AppIdentity "{{42D852F1-70E9-43ED-8739-D61752106D59}"
  #define AppDisplay "VideoProcessor"
  #define AppFolder "VideoProcessor"
  #define AppProduct "VideoProcessor-42D852F1-70E9-43ED-8739-D61752106D59"
  #define AppIcon "VideoProcessor.exe"
  #define ConfigArguments "--config """"{app}\VideoProcessor.cfg"""""
#endif
[Setup]
AppId={#AppIdentity}
AppName={#AppDisplay}
AppVersion={#CoreVersion} ({#BuildCommit})
AppVerName={#AppDisplay} {#CoreVersion} ({#BuildCommit})
AppPublisher=Bill Slack
AppPublisherURL=https://github.com/billslack2/videoprocessor
AppSupportURL=https://github.com/billslack2/videoprocessor/issues
DefaultDirName={localappdata}\Programs\{#AppFolder}
UsePreviousAppDir=yes
DisableDirPage=no
DefaultGroupName={#AppDisplay}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
OutputDir={#OutputRoot}
OutputBaseFilename={#InstallerBaseName}
SetupIconFile={#SetupIcon}
VersionInfoVersion={#FileVersion}
VersionInfoTextVersion={#CoreVersion}
VersionInfoProductVersion={#FileVersion}
VersionInfoProductTextVersion={#CoreVersion} ({#BuildCommit})
VersionInfoDescription=VideoProcessor Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
CloseApplications=no
RestartApplications=no
UninstallDisplayIcon={app}\{#AppIcon}
SetupMutex={#AppProduct}-Setup
UninstallDisplayName={#AppDisplay}
Uninstallable=InstalledMode
CreateUninstallRegKey=RegisterInstallation

[Messages]
UninstallAppTitle=Uninstall {#AppDisplay}
UninstallAppFullTitle=Uninstall {#AppDisplay}

[Files]
; Helpers run from setup's private temporary directory, never from the target.
Source: "{#PayloadRoot}\setup\install-support.ps1"; Flags: dontcopy
Source: "{#PayloadRoot}\INSTALL-MANIFEST.json"; Flags: dontcopy
#include PayloadInclude
; Verify before shortcuts and registration. Script exceptions do not roll back Inno.
Source: "{#PayloadRoot}\INSTALL-MANIFEST.json"; DestDir: "{tmp}"; DestName: "verification-marker.json"; Flags: ignoreversion deleteafterinstall; AfterInstall: VerifyPayload

[Dirs]
Name: "{app}\logs"; Flags: uninsneveruninstall
Name: "{app}\luts"; Flags: uninsneveruninstall

[Icons]
#ifndef ConfigOnly
Name: "{userprograms}\VideoProcessor\VideoProcessor"; Filename: "{app}\VideoProcessor.exe"; WorkingDir: "{app}"; Check: RegisterInstallation
#endif
Name: "{userprograms}\{#AppDisplay}\VideoProcessor Config"; Filename: "{app}\config\VideoProcessorConfig.exe"; Parameters: "{#ConfigArguments}"; WorkingDir: "{app}"; Check: RegisterInstallation

; Keep the engine's paired EXE/DAT names for native upgrade history.
Name: "{app}\Uninstall {#AppDisplay}"; Filename: "{uninstallexe}"; WorkingDir: "{app}"; IconFilename: "{app}\{#AppIcon}"; Check: RegisterInstallation
Name: "{userprograms}\{#AppDisplay}\Uninstall {#AppDisplay}"; Filename: "{uninstallexe}"; WorkingDir: "{app}"; IconFilename: "{app}\{#AppIcon}"; Check: RegisterInstallation

[Run]
Filename: "{app}\config\VideoProcessorConfig.exe"; Parameters: "{#ConfigArguments}"; WorkingDir: "{app}"; Description: "Open VideoProcessor Config"; Flags: postinstall nowait skipifsilent unchecked; Check: CanLaunch

[Code]
var
  BackupPath, HelperMessage: String;
  Prepared, Verified, Committed: Boolean;

function GetFileAttributesW(FileName: String): LongWord;
  external 'GetFileAttributesW@kernel32.dll stdcall';
function SetFileAttributesW(FileName: String; Attributes: LongWord): Boolean;
  external 'SetFileAttributesW@kernel32.dll stdcall';

procedure HideUninstallSupport;
var
  FileName: String;
  Attributes: LongWord;
  Index: Integer;
begin
  for Index := 0 to 2 do begin
    FileName := ExpandConstant('{uninstallexe}');
    if Index = 1 then FileName := ChangeFileExt(FileName, '.dat');
    if Index = 2 then FileName := ChangeFileExt(FileName, '.msg');
    if FileExists(FileName) then begin
      Attributes := GetFileAttributesW(FileName);
      if (Attributes = $FFFFFFFF) or not SetFileAttributesW(FileName, Attributes or 2) then
        Log('Could not hide internal uninstall support file: ' + FileName);
    end;
  end;
end;

function PortableUpdate: Boolean;
begin
  Result := ExpandConstant('{param:PORTABLEUPDATE|0}') = '1';
end;

function InstalledMode: Boolean;
begin
  Result := not PortableUpdate;
end;

function RegisterInstallation: Boolean;
begin
  Result := Verified and not PortableUpdate;
end;

function RunHelper(Action: String): Boolean;
var
  ExitCode: Integer;
  Args, ResultFile: String;
  Output: AnsiString;
begin
  ResultFile := ExpandConstant('{tmp}\vp-installer-result.txt');
  DeleteFile(ResultFile);
  Args := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
    ExpandConstant('{tmp}\install-support.ps1') + '" -Action ' + Action +
    ' -ApplicationId "{#AppProduct}" -InstallRoot "' + ExpandConstant('{app}') +
    '" -PayloadManifest "' + ExpandConstant('{tmp}\INSTALL-MANIFEST.json') +
    '" -ResultPath "' + ResultFile + '"';
  if PortableUpdate then Args := Args + ' -PortableUpdate';
  if BackupPath <> '' then Args := Args + ' -BackupDirectory "' + BackupPath + '"';
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    Args, '', SW_HIDE, ewWaitUntilTerminated, ExitCode) and (ExitCode = 0);
  HelperMessage := 'Setup could not run its verification helper. Check Windows script policy and the setup log.';
  if LoadStringFromFile(ResultFile, Output) then HelperMessage := Utf8Decode(Output);
  Log(Action + ': ' + HelperMessage);
end;

function PayloadReady: Boolean;
begin
  Result := Verified;
end;

function CanLaunch: Boolean;
begin
  Result := Verified and Committed;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  PreviousDir: String;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and not PortableUpdate then begin
    if RegQueryStringValue(HKCU64,
      'Software\Microsoft\Windows\CurrentVersion\Uninstall\{{#AppGuid}}_is1',
      'Inno Setup: App Path', PreviousDir) then begin
      if CompareText(RemoveBackslashUnlessRoot(PreviousDir),
        RemoveBackslashUnlessRoot(ExpandConstant('{app}'))) <> 0 then begin
        MsgBox('VideoProcessor is already registered at ' + PreviousDir + '.' + #13#10 +
          'Updates must use that folder to preserve the current installation. To relocate, uninstall first (your settings are retained), move the preserved folder, and select it in setup.',
          mbError, MB_OK);
        Result := False;
      end;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  ExtractTemporaryFile('install-support.ps1');
  ExtractTemporaryFile('INSTALL-MANIFEST.json');
  if not RunHelper('Check') then begin
    Result := HelperMessage + #13#10 + 'Correct the issue and click Retry, or cancel setup.';
    Exit;
  end;
  if not RunHelper('Prepare') then begin
    Result := HelperMessage;
    Exit;
  end;
  BackupPath := Trim(HelperMessage);
  Prepared := True;
end;

procedure VerifyPayload;
begin
  Verified := RunHelper('Verify');
  if not Verified then
    SuppressibleMsgBox(HelperMessage + ' Setup will restore the previous application files.',
      mbError, MB_OK, IDOK);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and not Committed then begin
    WizardForm.FinishedHeadingLabel.Caption := 'VideoProcessor setup did not complete';
    WizardForm.FinishedLabel.Caption := 'Do not launch VP. Setup will restore application files when closed. Correct the reported error and rerun setup. Settings and state are preserved.';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    if not Verified then Exit;
    if not RunHelper('Commit') then
      RaiseException(HelperMessage + ' Setup will restore the previous application files.');
    Committed := True;
    Verified := True;
    if not PortableUpdate then HideUninstallSupport;
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  Result := 0;
  if not Committed then Result := 1;
end;

procedure DeinitializeSetup;
begin
  if Committed then begin
    { Cleanup failure must not roll back a verified, registered installation. }
    if not RunHelper('Finalize') then
      Log('Recovery files retained for later cleanup: ' + HelperMessage);
  end;
  if Prepared and not Committed then begin
    if not RunHelper('Restore') then
      SuppressibleMsgBox('Automatic recovery could not finish: ' + HelperMessage + #13#10 +
        'Do not launch VP. Rerun setup after closing all VP applications. Backup: ' + BackupPath,
        mbError, MB_OK, IDOK);
  end;
end;

function InstallationProcessCount(WMI: Variant; Names: String): Integer;
var
  Processes, Process: Variant;
  Index: Integer;
  FileName, Prefix: String;
begin
  Result := 0;
  Prefix := AddBackslash(ExpandConstant('{app}'));
  Processes := WMI.ExecQuery('SELECT ExecutablePath FROM Win32_Process WHERE ' + Names);
  for Index := 0 to Processes.Count - 1 do begin
    Process := Processes.ItemIndex(Index);
    if VarIsNull(Process.ExecutablePath) then
      RaiseException('Cannot inspect a running VP application. Close it and retry.');
    FileName := Process.ExecutablePath;
    if CompareText(Copy(FileName, 1, Length(Prefix)), Prefix) = 0 then
      Result := Result + 1;
  end;
end;

function InitializeUninstall: Boolean;
var
  Locator, WMI: Variant;
  PlayerCount, ConfigCount: Integer;
  Notice, Running: String;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    WMI := Locator.ConnectServer('', 'root\CIMV2');
    while True do begin
      PlayerCount := InstallationProcessCount(WMI, 'Name="VideoProcessor.exe" OR Name="VideoProcessor-GUI.exe"');
      ConfigCount := InstallationProcessCount(WMI, 'Name="VideoProcessorConfig.exe"');
      if (PlayerCount = 0) and (ConfigCount = 0) then begin
        Result := True;
        Exit;
      end;
      Running := '';
      if PlayerCount > 0 then Running := 'VideoProcessor';
      if ConfigCount > 0 then begin
        if Running <> '' then Running := Running + ' and ';
        Running := Running + 'VideoProcessor Config';
      end;
      Notice := 'Uninstall has not started because ' + Running + ' is still running.' + #13#10#13#10;
      if ConfigCount > 0 then
        Notice := Notice +
          'Save any changes in Config. Then right-click the "VideoProcessor Configuration" icon near the Windows clock and select Exit. Check the hidden-icons arrow if needed.' + #13#10#13#10 + 'Closing the Config window only hides it in the tray; it does not exit the application.' + #13#10#13#10;
      if PlayerCount > 0 then
        Notice := Notice + 'Close the VideoProcessor player window.' + #13#10#13#10;
      Notice := Notice + 'Click Retry after exiting the running apps, or Cancel to leave VideoProcessor installed.' + #13#10#13#10 + 'Your configuration and state files will be kept after uninstall.';
      Log(Notice);
      if SuppressibleMsgBox(Notice, mbInformation, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then Exit;
    end;
  except
    SuppressibleMsgBox('Uninstall could not check whether VP or Config is running. Nothing has been removed. Close VP and choose Exit from the Config tray icon, then run Uninstall VideoProcessor again.',
      mbError, MB_OK, IDOK);
  end;
end;
