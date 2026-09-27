# VexGUI guide — windows the Vexel way

No widgets-with-listeners. The operator owns the message loop,
Vexel owns the step:

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

`run # w, "step"` pumps messages and summons `step` each round;
answer wet (`open # w`) to keep going, dry to land. Closing the
window reads as dry, so circles land softly — no special close
handler needed (though `close # w` destroys on demand).

## Verbs

```text
version #            "1.0.0", no window needed
window # t, w, h     hidden handle
show # w             place on screen
open # w             1 alive / 0 closed (dead window, not crash)
close # w            destroy now
run # w, "step"      loop until step runs dry or window dies
text # w, s, x, y    label
button # w, s, x, y  push button
input # w, x, y      edit line
editor # w, x, y, ww, hh   multiline editor with scroll
list # w, x, y, ww, hh     listbox
list_set # h, vec    refill the listbox from a vec of texts
selected # h         chosen row text, fail if none
set_text # h, s      1
get_text # h         text
clicked # h          1 once per press (button)
changed # h          1 once per edit (input)
size # w             [w h] client vec
```

Bad handles raise `fail` — rescue with `??`.

`editor` reads/writes through the same `set_text`/`get_text`;
`list` pairs with `list_set` + `selected` (see VexEd, the IDE
written in Vexel itself).

See also: operators/VexGUI/README.md, examples/gui_hello.vx,
examples/gui_events.vx.
