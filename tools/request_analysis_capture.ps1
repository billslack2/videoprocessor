[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateRange(1,2147483647)][int]$TargetProcessId,
    [ValidateSet('Status','Records','SampledFrames','ConsecutiveBurst')][string]$Action='Status',
    [string]$ExpectedExecutable='C:\Videoprocessor\vp\VideoProcessor.exe',
    [ValidateRange(100,5000)][int]$TimeoutMs=2000
)
$ErrorActionPreference='Stop'
# This is the application's explicit diagnostic IPC. No input simulation,
# focus/window changes, process restart, profile changes, or modal dialogs.
$vpTarget=Get-Process -Id $TargetProcessId -ErrorAction Stop
$vpExpected=[IO.Path]::GetFullPath($ExpectedExecutable)
if (![string]::Equals($vpTarget.Path,$vpExpected,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Target process does not match the explicitly expected VP executable.'
}
if (-not ('VpAnalysisCaptureControlV1' -as [type])) {
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class VpAnalysisCaptureControlV1 {
    private delegate bool EnumProc(IntPtr window, IntPtr state);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr state);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern uint RegisterWindowMessageW(string name);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern IntPtr SendMessageTimeoutW(IntPtr window, uint message, UIntPtr request,
        IntPtr version, uint flags, uint timeout, out UIntPtr result);
    public static IntPtr[] Windows(uint process) {
        var found=new List<IntPtr>();
        EnumWindows((window,state)=> { uint owner; GetWindowThreadProcessId(window,out owner);
            if(owner==process)found.Add(window); return true; },IntPtr.Zero);
        return found.ToArray();
    }
    public static ulong Request(IntPtr window,uint process,uint request,uint timeout) {
        uint owner; GetWindowThreadProcessId(window,out owner);
        if(owner!=process)throw new InvalidOperationException("Window no longer belongs to target process");
        uint message=RegisterWindowMessageW("VideoProcessor.AnalysisCapture.v1");
        if(message==0)throw new InvalidOperationException("Cannot register diagnostic control message");
        UIntPtr response;
        if(SendMessageTimeoutW(window,message,new UIntPtr(request),new IntPtr(1),2,timeout,out response)==IntPtr.Zero)
            return 0;
        return response.ToUInt64();
    }
}
"@
}
$vpWindow=[IntPtr]::Zero
foreach($vpCandidate in [VpAnalysisCaptureControlV1]::Windows([uint32]$TargetProcessId)) {
    $vpProbe=[VpAnalysisCaptureControlV1]::Request($vpCandidate,[uint32]$TargetProcessId,3,[uint32]$TimeoutMs)
    if (($vpProbe -band 0xffff0000) -eq 0x56500000 -and ($vpProbe -band 255) -eq 6) {
        $vpWindow=$vpCandidate;break
    }
}
if($vpWindow -eq [IntPtr]::Zero) { throw 'This VP process has no responsive AnalysisCapture.v1 endpoint; no capture was requested.' }
if($vpTarget.HasExited) { throw 'The selected VP process exited before the request.' }
$vpRequest=@{Records=0;SampledFrames=1;ConsecutiveBurst=2;Status=3}[$Action]
$vpResponse=[VpAnalysisCaptureControlV1]::Request($vpWindow,[uint32]$TargetProcessId,[uint32]$vpRequest,[uint32]$TimeoutMs)
if(($vpResponse -band 0xffff0000) -ne 0x56500000) { throw 'VP did not acknowledge the diagnostic request; check its log before retrying.' }
$vpCode=[int]($vpResponse -band 255)
$vpNames=@('Started','AlreadyRecording','NoProducer','FrameCaptureUnavailable','Busy','InvalidRequest','Status')
if($vpCode -gt 6) { throw 'Unknown diagnostic response version.' }
$vpFlags=$vpResponse -shr 8
$result=[pscustomobject]@{process=$TargetProcessId;executable=$vpExpected;action=$Action;result=$vpNames[$vpCode];recording=([bool]($vpFlags -band 1));captureWriting=([bool]($vpFlags -band 2));producerConnected=([bool]($vpFlags -band 4));frameCaptureSupported=([bool]($vpFlags -band 8));writerReady=([bool]($vpFlags -band 16));includeFrames=([bool]($vpFlags -band 32));requestedUtc=[DateTime]::UtcNow.ToString('o')}
$result
if($vpCode -in 2,3,4,5) { throw ('VP diagnostic capture was not started: '+$vpNames[$vpCode]) }
