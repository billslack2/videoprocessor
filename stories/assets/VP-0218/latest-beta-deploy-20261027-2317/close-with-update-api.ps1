$ErrorActionPreference='Stop'
[Reflection.Assembly]::LoadFrom('C:\Videoprocessor\vp\VideoProcessorUpdate.exe') | Out-Null
$apps=[VideoProcessor.Update.AppLifecycle]::Find('C:\Videoprocessor\vp')
[VideoProcessor.Update.AppLifecycle]::Close($apps)
$apps | ForEach-Object { [pscustomobject]@{file=$_.file;exited=$_.process.HasExited;restoreInTray=$_.restoreInTray} } | ConvertTo-Json

