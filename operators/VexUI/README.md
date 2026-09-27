# VexUI 1.0.0 — beautiful dark UI operator for Vexel

Win32 `user32`/`gdi32` only, no frameworks. DLL ~70 KB (budget 150 KB).

Dark window (dark title bar via dwmapi, best effort), Segoe UI,
rounded owner-drawn pill buttons (hover + press), cards, progress,
thin separators, dark inputs. All verbs `ui_`-prefixed — no collisions.

```text
!vex_operator_build operators/VexUI
!vex_add VexUI   (or !vex_operator_install <path>)
!vex_run uilook.vx
```

## Verbs

```text
ui_window # title, w, h     dark window handle (hidden until show)
ui_show # w                 place on screen
ui_open # w                 1 alive / 0 closed (pump messages)
ui_close # w                destroy now
ui_dark # w                 re-apply dark title bar
ui_run # w, "step"          loop until step runs dry or window dies
ui_title # w, s, x, y, size big bold header (light)
ui_label # w, s, x, y, color, size   styled label, 0xRRGGBB number
ui_btn # w, s, x, y, ww, hh, color   pill button
ui_input # w, x, y, ww      dark single-line field
ui_card # w, x, y, ww, hh, color     rounded panel — create FIRST
ui_prog # w, x, y, ww, color         progress track (value via pset)
ui_pset # h, pct            0..100, repaints
ui_sep # w, x, y, ww        divider line
ui_get # h                  text of any control
ui_set # h, s               set + repaint
ui_clicked # h              1 once per press (button), edge
```

Cards sit below siblings in z-order: create `ui_card` before labels.
Bad handles raise `fail` — rescue with `??`.
