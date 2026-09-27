# VexNet 1.0.0 — plain HTTP for Vexel (native, Win32 Winsock)

```text
@ VexNet
> get # "http://example.com/"
```

## `get # url`

GET over plain HTTP (`http://host[:port]/path`), body as text.
`fail` on: non-http scheme, DNS/connect timeout (10 s), send errors.
No TLS in 1.0.0 — `https://` is an honest `fail`, not half-TLS.

## `serve # port, "step"` — the server

Owns the TCP loop (localhost only); Vexel owns the step:

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
> "landed"
```

- `step` takes ONE mark: the raw request text (request line + headers).
- Wet text answer → served as `200` with correct length.
- Non-text wet answer → served as `fail` text (still 200).
- Dry answer (`fail`, `0`, `""`) → **lands the server** (`serve` gives 1).
- Step crash → `500`, server keeps going.

See `examples/net_serve.vx`.
