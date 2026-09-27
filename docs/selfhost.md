# Self-hosting, stage 1: `vex/lex.vx`

A Vexel lexer written in Vexel. Reads `lex_input.vx` (cwd), spills
one `KIND lexeme` line per token — the same KINDs as the C lexer,
including collapsed newlines. Covered by `tests/selfhost/`
(28 hand-verified tokens, 10/10 stable runs).

```text
vexel.exe !vex_run vex\lex.vx
```

## Rules the stage taught us (now in `docs/language.md`)

- Summon marks are greedy, twice over: `f # a + b` is `f # (a+b)`,
  and `f # g # a, b` feeds trailing commas to the innermost summon.
  Parenthesize nested summons or rest results in cells.
- `=` (give) outside a tool is a compile error, so early exits
  from circles only exist inside tools. Bench-level loops use a
  `stop` flag and spin the leftover rounds dry.
- Tools cannot write globals — scanners return `[pos, text]` vecs.

## Known VM bugs it caught (both fixed)

1. `=` inside `*` leaked the circle frame, hijacking outer `NEXT`s
   into infinite loops. Fix: `loop_depth` per frame, unwind on give.
2. (Same fix covered the lexer, which gives from inside circles
   all over: `is_digit`, scanners, comment skip.)

## Next stages (not started)

- Stage 2: parser in Vexel (beats + expressions → tree in vecs).
- Stage 3: bytecode emitter in Vexel (needs integer↔bytes packing —
  no such verbs yet; that gap is the honest blocker).
