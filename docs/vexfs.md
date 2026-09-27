# VexFS — file operator (1.0.0)

Native, Win32, UTF-8 paths. Five verbs, no surprises.

```text
@ VexFS
> write # "note.txt", "line1\nline2"
> read # "note.txt"
> exists # "note.txt"
> dir # "."
> remove # "note.txt"
```

## Verbs

| Глагол | Смысл |
|--------|-------|
| `read # path` | весь файл текстом. Нет файла — `fail`. UTF-8 BOM срезается, лимит 64 МБ |
| `write # path, text` | заменить файл целиком → `1`, иначе `fail` |
| `dir # path` | вектор имён (без `.`/`..`), плохой путь — `fail` |
| `exists # path` | `1` / `0` (никогда не `fail`) |
| `remove # path` | `1`, не вышло — `fail` |

Все пути — UTF-8 (кириллица работает). Относительные — от cwd скамьи.

## Ошибки — как обычно, значениями

```text
> read # "nope.txt" ?? "empty"
> remove # "nope.txt" ?? "already gone"
```

## Откуда ставится

```text
!vex_add VexFS
!vex_info VexFS
```

Исходник: `operators/VexFS/` (манифест + `src/vfs.c` + README).
