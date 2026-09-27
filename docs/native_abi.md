# Native ABI v1 — include/operator.h

A native operator is a DLL exporting exactly one symbol:

```c
const VxOpInfo *vxop_open(const VxOpApi *api);
```

The Core hands an api table; the operator touches Vexel values
ONLY through it — it can never corrupt VM memory directly.

## Ownership (the whole contract)

- args belong to the Core. Never release them.
- `*out` must be FRESH: built via api constructors or `api->clone`.
  Never hand back an args pointer (it borrows VM stack memory).
- `retain` only values kept past the call; release them by unload.
- `func` returns 0, or nonzero to raise `fail` (rescuable by `??`).

## Table

```c
make_num / make_text / make_fail / make_vec   fresh values
vec_push(vec, item)   0 ok; takes ownership of item on success
retain / release / clone
face                  VXF_FAIL / VXF_NUM / VXF_TEXT / VXF_VEC
as_num / as_text      borrowed bytes, valid while value lives
vec_len / vec_get     vec_get returns a retained copy
is_true               wet test
summon(tool, args, argc, &out)
  summon a Vexel TOOL by name (event steps, timers).
  Takes ownership of args. Only valid inside an operator call.
```

`funcs` table: `{ "verb", arity, fn }` — names/arities must match
`provides` in operator.vxop (compile-time checked, load-time
identity checked: name + version).

ABI mismatch (wrong `abi_version`, missing export, identity drift)
fails loudly: `Operator ABI mismatch` / `Failed to load Operator`.
