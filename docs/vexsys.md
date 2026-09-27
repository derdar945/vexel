# VexSYS — system battery (2.0.0)

The MEGA operator: os, time, paths, env, process, random, strings,
math. When a bench needs something small and platform-shaped, look
here before writing a new operator.

```text
@ VexSYS
> os #                 ; "windows"
> time_ms #            ; epoch ms
> user #               ; login name
> env # "PATH" ?? ""   ; variables: env/has_env/set_env/unset_env/env_all
> join # "a", "b"      ; paths: join/base/dir_name/ext/stem/abspath/...
> is_file # "main.vx"  ; is_dir/size/mtime/which/path_sep
> rand_range # 1, 10   ; rand/rand_bytes/choice/shuffle/uuid
> upper # "hi"         ; lower/trim/split/lines/str_join/starts/ends/...
> sqrt # 2             ; abs/floor/ceil/pow/min/max/clamp
> sleep # 50
```

Full verb list lives in `operator.vxop` (`provides`) — 60+ verbs,
`!vex_info VexSYS` prints them. Pure Win32 + C, no third party.
Source: `operators/VexSYS/`.
