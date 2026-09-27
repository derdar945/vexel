# VexGUI 1.0.0 — official native GUI operator for Vexel

Lightweight Win32 windows, text, buttons and inputs. No Qt, no Electron,
no SDL, no browser — only `user32`/`gdi32`. The DLL is ~60 KB.

## Install

```text
!vex_operator_build operators/VexGUI   (already built by build.bat)
!vex_add VexGUI
!vex_info VexGUI
```

## Bench idea

No listeners, no callbacks in the JS sense. The operator owns the
message loop (`run`), Vexel owns the step: each round the operator
summons your `step` tool; answer wet to keep going, dry to land.

```text
@ VexGUI

@ w : window # "Vexel — hello", 420, 260
@ hello : text # w, "press the mark", 20, 20
@ name : input # w, 20, 60
@ go : button # w, "Say hello", 20, 110

& step :
  ? clicked # go :
    @ who : get_text # name ?? "tide"
    set_text # hello, "Hello, " + who + "!"
  .
  = open # w
.

> show # w
> run # w, "step"

> "bye"
```

## Verbs (all summoned with `#`)

```text
version #            text, "1.0.0" — no window needed
window # t, w, h     handle — hidden until show
show # w             1 — place window on screen
open # w             1 while alive, else 0 (pumps messages)
close # w            1 — destroy now
run # w, "step"      1 — message loop; summons step each round
text # w, s, x, y    handle — STATIC label
button # w, s, x, y  handle — push button
input # w, x, y      handle — EDIT line
set_text # h, s      1
get_text # h         text
clicked # h          1 once per press, else 0 (button)
changed # h          1 once per edit, else 0 (input)
size # w             [w h] client vec
```

Bad handles raise `fail` — rescue with `??` (except `open`, which
reads a dead window as `0`, so circles land softly).

## Layout

Everything is Win32 children with plain coordinates — no layout engine
in 1.0.0 (Panels, Canvas, List, Menu, Dialog, Theme, Animation are
roadmap, not stubs: they simply do not exist yet).

## Files

```text
operators/VexGUI/
  operator.vxop   manifest (name, version, provides, deps)
  src/vgui.c      the whole backend, C17
  examples/       hello + events benches
  README.md       this file
```
