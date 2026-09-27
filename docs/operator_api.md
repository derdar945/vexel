# operator_api — verbs from the bench side

Operator verbs are summoned exactly like tools:

```text
@ VexGUI
@ w : window # "Hi", 400, 300
> show # w
```

Resolution order at compile time: user `&` tools first, then
attached operators (first attached wins on name clash), then
built-ins (`len/at/type/str`). Arity mismatches fail compilation:

```text
vexel: 2:3: 'window' wants 3, summoned with 1
```

Unknown operators fail too:

```text
vexel: 1:1: unknown operator '@ Nope' — install it first (!vex_add Nope)
```

At runtime `@ Name` loads `<store>/Name/<entry>` once
(LoadLibrary + `vxop_open` + identity check). Missing module:

```text
vexel: Failed to load Operator: Demo
Reason: cannot open Demo.dll (code 126)
```

Operator results are first-class values: store them in `@` cells,
pass them into tools, compare, rescue:

```text
@ b : button # w, "OK", 20, 60
> close # 999 ?? "no window"
```

Bytecode: `VX_ATTACH op` places, `VX_CALL_OP ref` summons
(`.vxb` VXB2 carries the op table; VXB1 files still load).
