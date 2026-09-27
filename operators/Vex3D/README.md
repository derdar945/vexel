# Vex3D 1.0.0 — tiny 3D (native, Win32, software rasterizer, no libs)

Scenes, objects, perspective camera, flat-shaded z-buffered fill.
The operator owns the loop, Vexel owns the step — same idea as
VexGUI `run`:

```text
@ Vex3D
@ s : scene # 320, 200, "cube"
@ m : cube #
@ o : spawn # s, m, 0, 0, 5
color # o, 16744448

& step :
  turn # o, (tick # s), ((tick # s) * 2), 0
  ? tick # s > 300 :
    = fail
  !
    = open # s
  .
.

> frame # s, "step"
```

`frame # s, "step"` renders, ticks once, summons; dry lands.
Closing reads dry. No mutable state needed — angles are pure
functions of `tick # s` (tools cannot write globals, so the
tick *is* the memory).

## Verbs

`scene # w, h, title` (exact client area, shown at once),
`draw # s` (one frame now — tests, thumbnails), `close/open/
frame/tick`, `mesh # flatvec` (9 numbers per triangle),
`cube #` (unit cube), `spawn # s, m, x, y, z`, `kill`,
`move/turn (degrees, X then Y then Z)/scale/color (0xRRGGBB)`,
`cam # s, x, y, z, fov` (fixed +Z gaze, no cam rotation in 1.0.0),
`getpx # s, x, y` (read back — headless-testable),
`key # s, code` (37–40 arrows, 32 space, 27 esc),
`size # s`. Bad handles → `fail` (except `open`: dead reads 0).

Summon marks are greedy — parenthesize nested summons in arg
lists (`turn # o, (tick # s), ...`), or the inner summon eats
the trailing commas. See `docs/language.md`.
