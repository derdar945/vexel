# VexGL 1.0.0 — true 3D for Vexel (native, Win32 + WGL)

Fixed-function OpenGL 1.1, no frameworks. Same loop idea as VexGame:
the operator owns the window and the GL context, Vexel owns the step.
Scene is stateless — every frame redraws from slots + `tick`.

```text
@ VexGL
@ g : make # "GL cube", 640, 480
& step :
  cls # g, 8, 10, 26
  cam # g, 0, 3, 8, 0, 0, 0
  cube # g, 0, 0, 0, 2, 30, (tick # g), 0, 16711680
  flip # g
  = open # g
.
> show # g
> run # g, "step"
```

## Window + loop (VexGame-style)

- `version/0` — `"1.0.0"`
- `make/3` title, w, h — hidden handle (16..2048 px)
- `show/1`, `open/1` (dead reads as `0`), `close/1`
- `run/2` g, `"step"` — 60fps loop; step answers wet to keep going
- `frame/2` — same loop (headless-testable)
- `size/1` — `[w h]`, `dt/1` ms, `fps/1` smoothed, `tick/1` frames
- `put/3` g, i, v / `get/2` g, i — 16number slots (i 0..15)
- `key/2` g, code — 1 while held; `pressed/2` — once per press
  (codes: 37–40 arrows, 32 space, 27 esc, 65–90 A–Z)

## 3D scene (drawn into the back buffer, shown with `flip`)

- `cls/4` g, r, g, b — clear color + depth (0–255 each)
- `cam/7` g, ex, ey, ez, cx, cy, cz — eye + look-at, up is +Y,
  fov 60, near 0.1, far 200. Default at `make`: eye `(0,3,8)`.
- `begin/2` g, mode — open a mesh: `0` triangles, `1` quads, `2` lines
- `col/4` g, r, g, b — current vertex color (0–255 each)
- `v/4` g, x, y, z — one vertex (finite numbers only)
- `end/1` g — close the mesh
- `cube/9` g, x, y, z, size, rx, ry, rz, color — solid cube,
  rotations in degrees, `color` is one `0xRRGGBB` number
  (red `16711680`, green `65280`, blue `255`), faces pre-shaded
  so the cube reads as 3D with no lights to set up
- `flip/1` g — swap buffers (call once at the end of step)

Bad handles / bad numbers raise `fail` — rescue with `??`
(except `open`, which reads a dead scene as `0`).

## One grammar trap (core Vexel, not just GL)

`#` args are greedy — parenthesize nested summons:

```text
? (tick # g) > 200 :
cam # g, (9 * (vsin # a)), 4, (9 * (vcos # a)), 0, 0, 0
```
