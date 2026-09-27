# VexNet — plain HTTP (1.0.0)

Native, Win32 Winsock. `get/1`, `serve/2`. No TLS in 1.0.0 —
`https://` is an honest `fail`, not half-TLS.

```text
@ VexNet
> get # "http://example.com/"
```

`serve # port, "step"` owns the TCP loop (localhost); Vexel owns
the step, like VexGUI `run`: the step takes the raw request text,
a wet text answer serves `200` with correct length, a dry answer
lands the server, a step crash serves `500` and continues.

```text
@ VexNet
@ VexSYS
& step req :
  ? contains # req, "/bye" :
    = fail
  !
    = "knock knock"
  .
.
> serve # 19311, "step"
```

A landed connection with zero bytes reads as `fail` (not `""`),
so `??` rescue tells "server went away" apart from "empty 200".

Install: `!vex_add VexNet`. Source: `operators/VexNet/`.
