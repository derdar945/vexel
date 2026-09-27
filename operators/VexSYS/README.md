# VexSYS 2.0.0 — MEGA system operator for Vexel (native, Win32, UTF-8)

Python-like battery: OS + time + path + env + process + random + strings + math.
Style — как VexFS: `@ VexSYS` сверху, дальше `глагол # args`.

```text
@ VexSYS
> os #
> os_ver #
> arch #
> cpu #
> mem #
> uptime #
> user #
> hostname #
> cwd #
> exe #
> pid #
> args #
> time #
> time_ms #
> date #
> date_utc #
> version #
```

## Система

- `version/0` — `"2.0.0"`
- `os/0` — `"windows"`, `arch/0` — `"x64"`/`"x86"`/`"arm64"`/`"any"`
- `os_ver/0` — `"10.0.22631"`, `cpu/0` — ядра, `mem/0` — `[всего_мб свободно_мб]`
- `uptime/0` — сек с загрузки, `tick/0` — мс с загрузки
- `user/0` логин, `hostname/0` машина — `fail` когда неизвестно
- `cwd/0`, `exe/0` путь к `vexel.exe`, `pid/0`, `args/0` vec слов командной строки
- `chdir/1` `1` или `fail`, `mkdir/1` `1` (есть — уже ок), `exit/1` — выход с кодом 0–255
- `path_sep/0` — `"\"`

## Время

- `time/0` unix-сек, `time_ms/0` unix-мс, `date/0` локально `"YYYY-MM-DD HH:MM:SS"`
- `date_utc/0` то же по UTC, `sleep/1` мс 0–600000 — всегда `1`

## Env

- `env/1` значение или `fail`, `has_env/1` 1/0
- `set_env/2` `1`, `unset_env/1` `1`, `env_all/0` vec `"K=V"`

## Путь и файлы (рядом с VexFS)

- `join/2` склейка (`abs` второй побеждает), `base/1` имя, `dir_name/1` папка
- `ext/1` `".txt"` или `""`, `stem/1` имя без ext, `abspath/1` полный путь
- `is_abs/1` `is_file/1` `is_dir/1` — 1/0, `size/1` байты, `mtime/1` unix-сек
- `which/1` полный путь по PATH или `fail`

## Random

- `rand/1` `[0,max)`, `rand_range/2` `[lo,hi)`, `rand_bytes/1` vec байтов
- `choice/1` элемент vec, `shuffle/1` копия vec, `uuid/0` `"xxxxxxxx-xxxx-..."`

## Строки

- `upper/1` `lower/1` (Unicode), `trim/1` пробелы с краёв
- `split/2` по подстроке, `lines/1` по строкам, `str_join/2` склейка vec
- `starts/2` `ends/2` `contains/2` — 1/0, `replace/3` все вхождения

## Математика

- `abs/1` `floor/1` `ceil/1` `sqrt/1` (`fail` при x<0), `pow/2`
- `min/2` `max/2` `clamp/3` (`x lo hi`, `fail` при hi<lo)

`fail` — значение, ловится `??`:

```text
@ VexSYS
> env # "NOPE_XYZ" ?? "dry"
> size # "нет.txt" ?? 0 - 1
> sqrt # 0 - 1 ?? "нет корня"
> which # "gcc" ?? "нет в PATH"
```
