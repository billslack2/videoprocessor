param(
    [Parameter(Mandatory = $true)]
    [string] $EditorPath,

    [Parameter(Mandatory = $true)]
    [string] $ConfigPath
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $EditorPath -PathType Leaf)) {
    throw "Editor executable not found: $EditorPath"
}
if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    throw "Configuration fixture not found: $ConfigPath"
}

Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class VideoProcessorConfigWindowTest
{
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr window);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    public static extern IntPtr GetWindowLongPtr(IntPtr window, int index);
}
'@

$GwlExStyle = -20
$WsExTopmost = 0x00000008

$first = $null
try {
    $arguments = @('--config', ('"' + $ConfigPath + '"'))
    $backgroundArguments = @('--config', ('"' + $ConfigPath + '"'), '--background')
    $first = Start-Process -FilePath $EditorPath -ArgumentList $backgroundArguments -PassThru

    Start-Sleep -Milliseconds 500
    if ($first.HasExited) {
        throw 'The background editor process exited during warm-up.'
    }

    # A normal VP request must wake the already-running background instance,
    # rather than starting a second editor or making the warm-up visible early.
    $warmActivation = Start-Process -FilePath $EditorPath -ArgumentList $arguments -PassThru
    if (-not $warmActivation.WaitForExit(5000)) {
        throw 'The warm editor activation process did not hand off and exit.'
    }
    $window = [IntPtr]::Zero
    for ($attempt = 0;
        $attempt -lt 50 -and $window -eq [IntPtr]::Zero;
        ++$attempt) {
        Start-Sleep -Milliseconds 100
        $first.Refresh()
        $window = $first.MainWindowHandle
    }
    if ($window -eq [IntPtr]::Zero -or -not [VideoProcessorConfigWindowTest]::IsWindowVisible($window)) {
        throw 'The background editor was not restored by a normal launch.'
    }
    if (([VideoProcessorConfigWindowTest]::GetWindowLongPtr($window, $GwlExStyle).ToInt64() -band $WsExTopmost) -eq 0) {
        throw 'The restored editor was not promoted above fullscreen VP.'
    }

    # WM_CLOSE follows the same close-to-tray path as the title-bar close button.
    [void] [VideoProcessorConfigWindowTest]::PostMessage(
        $window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    for ($attempt = 0;
        $attempt -lt 30 -and [VideoProcessorConfigWindowTest]::IsWindowVisible($window);
        ++$attempt) {
        Start-Sleep -Milliseconds 100
    }
    if ([VideoProcessorConfigWindowTest]::IsWindowVisible($window)) {
        throw 'Closing the editor did not hide it in the notification area.'
    }
    if (([VideoProcessorConfigWindowTest]::GetWindowLongPtr($window, $GwlExStyle).ToInt64() -band $WsExTopmost) -ne 0) {
        throw 'The hidden editor retained topmost state.'
    }

    $second = Start-Process -FilePath $EditorPath -ArgumentList $arguments -PassThru
    if (-not $second.WaitForExit(5000)) {
        throw 'The second editor process did not hand off and exit.'
    }
    if ($second.ExitCode -ne 0) {
        throw "The second editor process returned exit code $($second.ExitCode)."
    }

    for ($attempt = 0;
        $attempt -lt 50 -and -not [VideoProcessorConfigWindowTest]::IsWindowVisible($window);
        ++$attempt) {
        Start-Sleep -Milliseconds 100
    }
    if (-not [VideoProcessorConfigWindowTest]::IsWindowVisible($window)) {
        throw 'The existing editor was not restored by the second launch.'
    }

    Write-Output 'PASS second launch restores the existing tray instance'
}
finally {
    if ($first -and -not $first.HasExited) {
        Stop-Process -Id $first.Id -Force
    }
}
