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
pinned user-level Qt kit at `%LOCALAPPDATA%\VideoProcessor\dependencies`, so
all worktrees reuse one download. Set `VP_QT_ROOT` to use a different kit; the
previous ignored repository-local `.dependencies` path remains a compatibility
fallback. `windeployqt` runs after every Release build.

## Run

```powershell
.\src\VideoProcessor-Config\x64\Release\VideoProcessorConfig.exe `
  --config .\VideoProcessor.cfg
```

Supported integration arguments are `--config <path>`, `--owner <HWND>`, and
`--owner-process <PID>`. The owner pair is validated before Config adopts VP's
stable main window as its native transient owner; Config does not use global
always-on-top placement.
The `--page` and `--screenshot` arguments support automated visual review.
