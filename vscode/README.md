# VS Code extension lives next door

The Vexel extension for Visual Studio Code is a **separate repository**:

```text
../vexel-vscode/
```

It provides `.vx` recognition, rune-first highlighting, snippets,
`!vex_run` / `!vex_check` / `!vex_build` / REPL commands, diagnostics
from real compiler errors, and operator-aware completion.

Build it there:

```text
cd ../vexel-vscode
npm install
npm run compile
npm run package
```

The `.github/workflows/vscode.yml` workflow in this repo guards the
CLI contract the extension relies on.
