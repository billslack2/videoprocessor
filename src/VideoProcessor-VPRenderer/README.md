# Optional libplacebo renderer plugin

This project builds `VideoProcessorVPRenderer.dll` and stages it with its
private runtime dependencies under `x64\<configuration>\vprenderer`.

The normal VideoProcessor executable has no import from this plugin or from
`libplacebo-360.dll`. At startup it checks for:

```text
vprenderer\VideoProcessorVPRenderer.dll
```

The renderer is listed only when the plugin loads successfully and reports the
expected API version. The plugin must be rebuilt with VideoProcessor whenever
the shared C++ renderer interfaces change.
