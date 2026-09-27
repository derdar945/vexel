# Vex3D — tiny 3D (1.0.0)

Native, Win32, software rasterizer, no engine libraries. Scenes,
objects, perspective camera, flat-shaded z-buffered triangles.

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

`frame # s, "step"` renders the whole scene, ticks once, summons
the step; a dry answer lands. Euler order X→Y→Z in degrees; camera
gazes +Z from its position (no cam rotation in 1.0.0); light is a
fixed sky-ish direction. `draw # s` renders a single frame without
the loop — that is how `tests/operators/t_op_3d.vx` asserts real
pixels (`12763842` — a shaded face).

Parenthesize nested summons in arg lists
(`turn # o, (tick # s), ...`) — greedy marks eat trailing commas.
See `docs/language.md`.

Install: `!vex_add Vex3D`. Example: `examples/game3d.vx`.
Unity parallels, honestly mapped: scene≈Scene, spawn≈Instantiate,
move/turn/scale≈Transform, frame+step≈Update loop, key≈Input polling.
No prefabs, no physics, no materials — that is the roadmap, not a claim.
