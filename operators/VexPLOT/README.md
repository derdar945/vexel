# VexPLOT 1.0.0 — ASCII charts for Vexel

Pure C, no deps. Console charts from vecs of numbers — quick data
checks without windows. Non-numbers fail the call.

```text
> plot_bar # [3 1 4 1 5], 12
####### 3
## 1
...

> plot_line # [0 1 2 1 0], 9, 4
```

## Verbs

```text
plot_bar # v, w       horizontal bars, one row per number (w 4..200)
plot_line # v, w, h   vertical sparkline canvas (w 2..200, h 2..60)
```
