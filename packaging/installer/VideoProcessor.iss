#if Ver != EncodeVer(6, 7, 3)
  #error Use the qualified Inno Setup 6.7.3 compiler.
#endif
; Identity and payload include are supplied by tools/build_installer.ps1.
#ifndef PayloadRoot
  #error Build this installer with tools/build_installer.ps1.
#endif
[Setup]
AppId={{42D852F1-70E9-43ED-8739-D61752106D59}
AppName=VideoProcessor
AppVersion={#CoreVersion} ({#BuildCommit})
AppVerName=VideoProcessor {#CoreVersion} ({#BuildCommit})
AppPublisher=VideoProcessor contributors
AppPublisherURL=https://github.com/billslack2/videoprocessor
AppSupportURL=https://github.com/billslack2/videoprocessor/issues
DefaultDirName={localappdata}\Programs\VideoProcessor
UsePreviousAppDir=yes
DisableDirPage=no
DefaultGroupName=VideoProcessor
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
OutputDir={#OutputRoot}
OutputBaseFilename={#InstallerBaseName}
SetupIconFile={#SetupIcon}
VersionInfoVersion={#FileVersion}
VersionInfoTextVersion={#CoreVersion} ({#BuildCommit})
VersionInfoProductVersion={#FileVersion}
VersionInfoProductTextVersion={#CoreVersion} ({#BuildCommit})
VersionInfoDescription=VideoProcessor Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
CloseApplications=no
RestartApplications=no
UninstallDisplayIcon={app}\VideoProcessor.exe
SetupMutex=VideoProcessor-42D852F1-70E9-43ED-8739-D61752106D59-Setup
UninstallDisplayName=VideoProcessor
CreateUninstallRegKey=PayloadReady

[Files]
; Helpers run from setup's private temporary directory, never from the target.
Source: "{#PayloadRoot}\prerequisites\*"; Flags: dontcopy
Source: "{#PayloadRoot}\setup\install-support.ps1"; Flags: dontcopy
Source: "{#PayloadRoot}\INSTALL-MANIFEST.json"; Flags: dontcopy
#include PayloadInclude
; Verify before shortcuts and registration. Script exceptions do not roll back Inno.
Source: "{#PayloadRoot}\INSTALL-MANIFEST.json"; DestDir: "{tmp}"; DestName: "verification-marker.json"; Flags: ignoreversion deleteafterinstall; AfterInstall: VerifyPayload

[Dirs]
Name: "{app}\logs"; Flags: uninsneveruninstall
Name: "{app}\luts"; Flags: uninsneveruninstall

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Icons]
Name: "{userprograms}\VideoProcessor\VideoProcessor"; Filename: "{app}\VideoProcessor.exe"; WorkingDir: "{app}"; Check: PayloadReady
Name: "{userprograms}\VideoProcessor\VideoProcessor Config"; Filename: "{app}\config\VideoProcessorConfig.exe"; Parameters: "--config ""{app}\VideoProcessor.cfg"""; WorkingDir: "{app}"; Check: PayloadReady
Name: "{userdesktop}\VideoProcessor"; Filename: "{app}\VideoProcessor.exe"; WorkingDir: "{app}"; Tasks: desktopicon; Check: PayloadReady

[Run]
Filename: "{app}\config\VideoProcessorConfig.exe"; Parameters: "--config ""{app}\VideoProcessor.cfg"""; WorkingDir: "{app}"; Description: "Open VideoProcessor Config"; Flags: postinstall nowait skipifsilent unchecked; Check: CanLaunch

[Code]
var
  BackupPath, HelperMessage: String;
  Prepared, Verified, Committed: Boolean;

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
    ' -InstallRoot "' + ExpandConstant('{app}') +
    '" -PayloadManifest "' + ExpandConstant('{tmp}\INSTALL-MANIFEST.json') +
    '" -ResultPath "' + ResultFile + '"';
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
  if CurPageID = wpSelectDir then begin
    if RegQueryStringValue(HKCU64,
      'Software\Microsoft\Windows\CurrentVersion\Uninstall\{42D852F1-70E9-43ED-8739-D61752106D59}_is1',
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
var
  ExitCode: Integer;
begin
  Result := '';
  ExtractTemporaryFile('install-support.ps1');
  ExtractTemporaryFile('INSTALL-MANIFEST.json');
  ExtractTemporaryFile('setup-runtime.ps1');
  ExtractTemporaryFile('runtime-common.ps1');
  ExtractTemporaryFile('runtime-requirement.json');
  ExtractTemporaryFile('vc_redist.x64.exe');
  if not RunHelper('Check') then begin
    Result := HelperMessage + #13#10 + 'Correct the issue and click Retry, or cancel setup.';
    Exit;
  end;
  if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
    ExpandConstant('{tmp}\setup-runtime.ps1') + '"', '', SW_HIDE,
    ewWaitUntilTerminated, ExitCode) then begin
    Result := 'Could not check the Microsoft runtime. No application files were replaced.';
    Exit;
  end;
  if (ExitCode = 3010) or (ExitCode = 1641) then begin
    NeedsRestart := True;
    Result := 'The Microsoft runtime requires a Windows restart. Restart, then run this installer again. VideoProcessor has not been updated.';
    Exit;
  end;
  if ExitCode <> 0 then begin
    Result := 'Microsoft runtime setup was cancelled, denied elevation, or failed (code ' +
      IntToStr(ExitCode) + '). No application files were replaced. Check the Microsoft installer log in %TEMP%, then retry.';
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
      MsgBox('Automatic recovery could not finish: ' + HelperMessage + #13#10 +
        'Do not launch VP. Rerun setup after closing all VP applications. Backup: ' + BackupPath,
        mbError, MB_OK);
  end;
end;

function InitializeUninstall: Boolean;
var
  Locator, WMI, Processes: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    WMI := Locator.ConnectServer('', 'root\CIMV2');
    Processes := WMI.ExecQuery(
      'SELECT ProcessId FROM Win32_Process WHERE Name="VideoProcessor.exe" OR Name="VideoProcessor-GUI.exe" OR Name="VideoProcessorConfig.exe"');
    if Processes.Count > 0 then begin
      MsgBox('Save your work and close VP and Config, including the tray icon, before uninstalling. Your settings and state will be retained.', mbError, MB_OK);
      Exit;
    end;
    Result := True;
  except
    MsgBox('Unable to check running applications. Close VP and Config and retry uninstall.', mbError, MB_OK);
  end;
end;
