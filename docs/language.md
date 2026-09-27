# Vexel language — marks bench (0.1 + operators)

Vexel program is a bench. Each line starts with a mark rune.

```text
@   cell:    @ x : 10            place value on the bench
>   show:    > x + 1             show outward (print)
? ! .  ask:  ? x > 1 : ... ! ... .    branch (no if/else)
* .  circle: * 3 : ... .         repeat; it is the round number
& .  tool:   & add a b : ... .   function (no fn/return)
=   give:    = a + b             hand back (inside tool only)
#   summon:  add # 2, 3          summon a tool (no parens)
??  rescue:  risky # 0 ?? 1      fail falls back to the right
;   note:    ; comment to end of line
```

No `main let if else for while return try catch {} ;`, no forced types.

## Values — one entity, four faces

`number` (always f64), `text`, `vec` (`[10 20]`), `fail`.
Truth is wet/dry: `fail`, `0`, `""`, `[]` are dry; everything else wet.
`+` adds numbers, else concatenates; `"ab" * 3` repeats;
`==` compares deeply; anything broken yields `fail`, never a crash.

## Operators on the bench

```text
@ VexGUI
```

`@` places things on the bench: `@ x : 10` a cell, `@ VexGUI`
an installed operator (top level only). After that its verbs summon
with `#`: `> version #`. Arity is checked at compile time from the
operator manifest. Operator fail rescues with `??` like any fail.

Unqualified, the first attached operator that provides the verb wins.
Name it to pick exactly:

```text
@ VexSYS
@ VexGame
> VexSYS.tick #       milliseconds here (VexSYS owns it)
> VexGame.tick # g    frame count there (VexGame owns it)
```

`Op.verb` needs `@ Op` on the bench; a missing operator or verb
fails the check with the place spelled out.

## Circles, asks, tools

```text
? x == 10 :
  > "ten"
!
  > "other"
.

* 3 :
  > it
.

& fact n :
  ? n <= 1 :
    = 1
  !
    = n * fact # n - 1
  .
.
```

Blocks open with `:` and land with `.` on its own line; `!` splits
an ask. `it` is the circle number (0-based, inner circles shadow).
`= e` inside a tool hands `e` back at once; a tool without `=`
hands back `fail`. Natives: `len #`, `at #`, `type #`, `str #`.

Summon marks are greedy: everything right of `#` on the line belongs
to the summon, so `add # 2 + 3` is `add # (2 + 3)`. Nested summons
eat trailing commas too: `px # g, tick # g, 100, 1` reads the last
three marks as `tick`'s. Parenthesize nested summons, or rest an
intermediate result in a cell first:

```text
px # g, (tick # g), 100, 16711680

@ a : fib # n - 1
@ b : fib # n - 2
= a + b
```
