# VexGame 1.0.0 — tiny 2D engine for Vexel (native, Win32 GDI)

Same loop idea as VexGUI: the operator owns the window, Vexel owns
the step. One Vexel rule shapes the whole API: `@` inside a tool is
local, so frame-to-frame state lives natively — in **sprites** (x, y)
and **slots** (score, speeds), not in globals.

```text
@ VexGame
@ g : make # "Hi", 480, 320
@ bg : color # 12, 12, 24
@ b : spr # g, 40, 60, 28, 28, color # 255, 90, 90
put # g, 0, 3
& step :
  cls # g, bg
  move # b, (get # g, 0), 2
  draw # g
  flip # g
  = open # g
.
> show # g
> run # g, "step"
```

## Window + loop

- `version/0` — `"1.0.0"`
- `make/3` title, w, h — hidden handle (16..2048 px; tiny sizes allowed
  for headless pixel tests)
- `show/1` — place on screen + focus (keys need focus)
- `open/1` — 1 alive / 0 closed (dead reads as closed, like VexGUI)
- `close/1` — destroy now
- `run/2` g, "step" — 60fps loop; step answers wet to keep going
- `size/1` — `[w h]`, `dt/1` — ms of last frame, `fps/1` — smoothed fps,
  `tick/1` — frames run so far (stateless motion: `x = tick * 2`)

## Canvas (draw into the backbuffer, show with `flip`)

- `cls/2` g, color — fill all
- `flip/1` — present to the window (call once at the end of step)
- `rect/6` filled, `box/6` outline, `circle/5` disc, `ring/5` outline
- `line/6` x1 y1 x2 y2, `dot/4` one pixel, `getpx/3` g, x, y — read a pixel
  back (`fail` when outside), `text/5` s, x, y
- `color/3` r, g, b (0–255 each) — build paint numbers with it
- dots-style aliases: `game/3` w, h, title (= `make`), `fill/2` (= `cls`),
  `px/4` (= `dot`), `frame/2` (= `run`), `tick/1` frames run so far

## Sprites (they remember x, y between steps)

- `spr/6` g, x, y, w, h, color — id
- `move/3` id, dx, dy — relative; `place/3` id, x, y — absolute
- `pos/1` — `[x y]`; `spr_box/1` — `[x y w h]`
- `paint/2` — recolor (hit flash); `kill/1` — destroy (bullets, dots)
- `touch/2` a, b — sprite AABB overlap, 1/0
- `draw/1` g — paint every alive sprite (after `cls`, before `flip`)

## Shared slots (score, lives, speeds — 16 number cells per game)

- `put/3` g, i, v (i 0..15); `get/2` g, i

## Input (window must be focused — `show` focuses it)

- `key/2` g, code — 1 while held; `pressed/2` — 1 once per press
- codes: 37 left, 38 up, 39 right, 40 down, 32 space, 13 enter,
  27 esc, 48–57 digits, 65–90 letters (A–Z)
- `mouse/1` — `[x y]`; `mdown/1` — held; `mclick/1` — once per click

## Pure helpers + sound

- `hit/2` `[x y w h]`, `[x y w h]` — rect overlap without sprites
- `dist/4` x1, y1, x2, y2 — pixels between dots
- `beep/2` freq, ms — retro blip (keep ms short, it blocks)

Bad handles raise `fail` — rescue with `??` (except `open`,
which reads a dead game as `0`, so loops land softly).

## One grammar trap (core Vexel, not just games)

`#` args are greedy up to `??`/`,`/newline — a comparison after a
summon slides INSIDE it: `tick # g > 200` reads as `tick # (g > 200)`.
Parenthesize the call before comparing:

```text
? (tick # g) > 200 :
  = fail
!
  = open # g
.
```
