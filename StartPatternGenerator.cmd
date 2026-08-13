@echo off
setlocal
cd /d "%~dp0"

if not exist "VideoProcessorPatternGenerator.exe" (
    echo VideoProcessorPatternGenerator.exe is missing from this folder.
    echo Reinstall or extract the complete VideoProcessor release.
    pause
    exit /b 1
)

start "" "VideoProcessorPatternGenerator.exe" %*
