# Optional subtitle-assisted AR reacquisition

`subtitle_ar_reacquisition` is a file-only renderer option. It defaults to **false** when omitted. There is no GUI control.

```ini
[vprenderer]
subtitle_ar_reacquisition: true
```

For a configuration using named Rendering profiles, put the key in the existing baseline section (for example `[vprenderer.Default]`) rather than adding another baseline. Named profiles inherit the baseline value unless they override it. An explicit `false` disables the option for that profile. The parser accepts the existing Boolean spellings (`true`/`false`, `yes`/`no`, `on`/`off`, `1`/`0`); other values fail configuration validation. Unrelated GUI edits and saves preserve this setting and its comments.

The option permits the additional subtitle-assisted reacquisition path when ordinary aspect detection cannot establish a safe picture boundary. It does not force a remembered aspect ratio: original source pixels, independent boundary evidence, current subtitle ownership, and fresh temporal confirmation must still pass. The excluded bands must pass the current black-level and owned-caption checks; unexplained bright or colored pixels reject the crop. Subtitle relocation must also succeed before this additional reacquisition crop can be presented. If relocation or its current-frame proof fails, this path applies no crop and the existing native presentation policy remains in force (full raster in the eligible reacquisition contexts).

This is presentation evidence only. It does not teach native aspect-ratio history, authorize scene changes, or use generated subtitle backgrounds as source evidence. Leave the option disabled for baseline comparison. Restart VP after changing the file to ensure the selected configuration is applied.
