# Creating an operator — the whole path

```text
creation -> API -> code -> build -> install -> use
```

## 1. Creation

```text
!vex_operator_new MyOp
```

Gives `MyOp/operator.vxop` (edit `provides`: `ping/1, ...`),
`src/MyOp.c` (one working verb), `examples/hello.vx`, README.

## 2. API

Decide verbs + arities FIRST, mirror them in manifest `provides`
(compile-time checked) and the C `funcs` table (load-time checked).

## 3. Code

```c
#include "operator.h"

static int op_shout(const VxOpApi *api, VxOpVal **args, int argc,
                    VxOpVal **out) {
    const char *s = NULL;
    size_t n = 0;
    if (!api->as_text(args[0], &s, &n)) return 1; /* -> fail */
    /* ... build LOUD ... */
    *out = api->make_text(LOUD, LOUDN);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo funcs[] = { { "shout", 1, op_shout } };

VXOP_EXPORT const VxOpInfo *vxop_open(const VxOpApi *api) {
    static VxOpInfo info;
    (void)api;
    info.abi_version = VXOP_ABI_VERSION;
    info.name = "MyOp";
    info.version = "0.1.0";
    info.nfuncs = 1;
    info.funcs = funcs;
    return &info;
}
```

Rules: touch values only via api; `*out` fresh (constructors or
`clone`) or NULL; nonzero returns raise `fail`. Need Vexel to
decide per event? `api->summon("step", NULL, 0, &r)` + `api->is_true`.

## 4. Build

```text
!vex_operator_build MyOp     (needs gcc on PATH)
```

Compiles `src/*.c` shared against Core `include/`.

## 5. Install

```text
!vex_add MyOp
```

Validates manifest, core version, platform, entry binary, deps.
Missing pieces fail loudly (`Missing dependency:`, `Operator ABI
mismatch`, `Native module not built: ...`).

## 6. Use

```text
@ MyOp
> shout # "tide"
> shout # 42 ?? "loud numbers only"
```
