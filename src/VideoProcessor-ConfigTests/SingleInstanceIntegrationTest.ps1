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
}
'@

$first = $null
try {
    $arguments = @('--config', ('"' + $ConfigPath + '"'))
    $first = Start-Process -FilePath $EditorPath -ArgumentList $arguments -PassThru

    $window = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 100 -and $window -eq [IntPtr]::Zero; ++$attempt) {
        Start-Sleep -Milliseconds 100
        $first.Refresh()
        $window = $first.MainWindowHandle
    }
    if ($window -eq [IntPtr]::Zero) {
        throw 'The first editor process did not create a main window.'
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
