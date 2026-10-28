# VP-0213: Off bar-content fit ownership

Processing style Off uses source pixels to fit the picture plus visible content
in encoded black bars. It allows surrounding black space, preserves aspect,
and does not translate, reconstruct or repaint subtitles.

## Ownership boundary

The native model still determines the picture rectangle. The Off presentation
owner determines only how much additional visible content must fit around it.
Its state is tied to the exact native base, source generation, viewport,
policy/continuity generations, configured hold, and increasing source order.

The final Off decision is applied after generic admission/recovery and before
optional fill/NLS. Both an active envelope and an explicitly released native
view are owned decisions. A previously displayed larger envelope cannot override
either. Native admission retains the native picture, never the fitted envelope.
An accepted picture change, full-raster authority, moving-picture handoff,
disabled automatic crop, explicit fixed crop or non-Off style retires the owner.

Every eligible immutable source is inspected using existing excluded-band
retention measurements and dense vertical bar content. These are raw pixel
observations, without glyph or UI-shape qualification. Retention remains the
conservative outer coverage; dense content can only expand it. Retention's
own margins are not added twice. An unsafe excluded edge without bounded
extent requires full-raster coverage. Invalid measurement is unknown.

Each outer edge has its own evidence deadline. Fresh pixels at or beyond the
retained edge renew that edge. Nearer content does not renew an older farther
extent. A positive hold retains an edge until its deadline; zero releases on
the next reliable negative/nearer scan. Unknown or duplicate sources do not
renew or release evidence. Out-of-order work cannot replace current state.

## Output and other modes

Temporal source snapshots carry the resolved geometry unchanged. Successful
submission may record the actual displayed rectangle in the shared anchor,
but cannot author or renew Off state. This prevents two interleaved output
histories from feeding competing sizes back into source policy.

Active Off accommodation suppresses optional aspect-limit crops and uses
linear fit while NLS is selected, including a full-raster visible envelope.
A released native view can resume its ordinary optional fill/NLS policy.
Classic/reconstructed subtitle proof, translation, drift and recovery remain
unchanged. Their analogous ownership boundary is a separate VP-0214 audit.

## Validation and limitations

The regression replay prepares source n before committing n-1 and invokes the
production recovery, admission, uncertain-presentation, final-fill and successful
anchor-commit functions. Its control retains both 1974/2068 bottoms. The Off
source owner must keep the final crop and target stable with hold 0 and 2000.
Other tests cover per-edge release, gaps/reappearance, unknown/duplicate/old
sources, contexts, side/multiple bars, full raster and visibility union.

Renderer profile/transition/startup/NLS integration is also source-reviewed;
policy tests are not a complete live GPU/capture reproduction. Live acceptance
must inspect submitted source/target rectangles through active content and
release, real AR changes, and Classic/reconstruction switching. Check existing
measurement timing telemetry: Off now inspects every eligible source, and a
different black-reference argument may prevent reuse of an earlier scheduled
dense scan. Do not claim zero extra scans or live performance acceptance from
the deterministic tests alone. No active configuration change is required.
