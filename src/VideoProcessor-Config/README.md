# VideoProcessor configuration editor

This is the Qt 6 Widgets configuration editor for VP-0097. General,
DirectShow, Shortcuts, and the ordered Queue, VP Renderer, Viewport, and LLDV
profile surfaces are editable and use the shared safe-save/validation core.
Unknown settings and manual sections are preserved. Shader and action editing
remain the next structured-editor slices.

## Build

From the repository root:

```powershell
.\scripts\bootstrap-config-editor-qt.ps1
& 'C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  .\src\VideoProcessor-Config\VideoProcessor-Config.vcxproj `
  /m /p:Configuration=Release /p:Platform=x64
```

Qt Widgets is the editor's implementation framework. The project uses the
pinned repository-local Qt kit and runs `windeployqt`
after every Release build. Qt and generated runtime files live under ignored
`.dependencies` and build-output directories.

## Run

```powershell
.\src\VideoProcessor-Config\x64\Release\VideoProcessorConfig.exe `
  --config .\VideoProcessor.cfg
```

Supported integration arguments are `--config <path>` and `--owner <HWND>`.
The `--page` and `--screenshot` arguments support automated visual review.
