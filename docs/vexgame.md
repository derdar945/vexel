# VexGame — tiny games (1.0.0)

Native, Win32 GDI, no libraries. Pixel canvas + frame loop + keys.

```text
@ VexGame
@ g : game # 320, 200, "dots"
& step :
  fill # g, 0
  px # g, ((tick # g) * 2) % 320, 100, 16711680
  ? tick # g > 200 :
    = fail
  !
    = open # g
  .
.
> frame # g, "step"
```

`frame # g, "step"` pumps, ticks once per round, summons the step;
dry lands. Closing reads dry. No mutable state needed — positions
are pure functions of `tick # g` (tools cannot write globals, so
the tick *is* the memory).

Verbs: `game # w, h, title` (exact client area, shown at once),
`close/open/frame/tick`, `fill # g, color`, `px # g, x, y, color`
(0xRRGGBB; outside → `fail`), `getpx # g, x, y` (read back —
headless-testable), `key # g, code` (1 while down: 37–40 arrows,
32 space, 27 esc), `size # g`. Bad handles → `fail`, except `open`
which reads dead as 0.

Summon arguments are greedy — parenthesize nested summons in arg
lists: `px # g, (tick # g), 100, 1`, or the inner summon eats the
trailing commas. See `docs/language.md`.

Install: `!vex_add VexGame`. Example: `examples/game_dots.vx`.
