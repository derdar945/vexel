# VexExec — run programs (1.0.0)

Native, Win32. Один глагол: запустить команду, забрать вывод.

```text
@ VexExec
> exec # "vexel.exe !vex_check main.vx", 10000
```

## `exec # cmd, timeout_ms`

- Выполняет командную строку, ждёт, отдаёт слитые stdout+stderr текстом.
- `fail`, если: не стартует, таймаут вышел (мс, clamp 100–600000 —
  зависший ребёнок прибивается), вывод упёрся в лимит 8 МБ.
- Наследуют cwd скамьи. Код возврата не отдаётся — только текст
  (типичный паттерн: гнать `!vex_check` и разбирать текст ошибки).

```text
@ VexExec
@ r : exec # "vexel.exe !vex_run main.vx", 15000 ?? "exec fail"
> r
```

## Зачем

Инструмент для инструментов: IDE (VexEd) жмёт Run/Check именно так.
Таймаут обязателен, чтобы чужая зависшая скамья не вешала твою.

## Откуда ставится

```text
!vex_add VexExec
!vex_info VexExec
```

Исходник: `operators/VexExec/` (манифест + `src/vexec.c` + README).
