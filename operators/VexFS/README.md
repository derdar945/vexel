# VexFS 1.0.0 — file operator for Vexel (native, Win32, UTF-8 paths)

```text
@ VexFS
> write # "note.txt", "line1\nline2"
> read # "note.txt"
> exists # "note.txt"
> dir # "."
> remove # "note.txt"
```

Verbs (`read/1 write/2 dir/1 exists/1 remove/1`): `read` gives the whole
file as text (UTF-8 BOM stripped, 64 MB cap, missing → `fail`);
`write` replaces the file, `dir` a vec of names (no `.`/`..`),
`exists` 1/0, `remove` 1 or `fail`. All paths are UTF-8.
