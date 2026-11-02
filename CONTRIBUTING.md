# Contributing to Commandos2-ModernPatch

Thanks for your interest! Bug reports, testing on other setups and code changes are all
welcome.

## Reporting bugs

Use the **Bug report** form on the [Issues](../../issues) page and attach
`Commandos2-ModernPatch.log` (next to `Comm2.exe`). For crashes or performance problems set
`Debug=1` in `Commandos2-ModernPatch.ini`, reproduce the problem, then attach the log.
Please check the Troubleshooting section of the [README](README.md) first, and say whether
the problem also happens with the patch turned off (**F10** in game or `Enabled=0`).

Testing reports from setups that are not covered yet are also very useful: other
resolutions (2560×1440, 4K, ultrawide), refresh rates, graphics cards and laptops.

## Suggesting features

Open an issue and describe what you would like to see and why. The patch keeps the game's
logic untouched (25 ticks per second), so features that change gameplay rules are usually
out of scope.

## Code changes

### Building

Requirements: Visual Studio 2019 or newer with *Desktop development with C++*.

```
build.bat                                             (builds build\ddraw.dll)
powershell -ExecutionPolicy Bypass -File package.ps1  (builds the release zip)
```

For testing, copy `build\ddraw.dll` and `dist\Commandos2-ModernPatch.ini` into the game
folder and the cnc-ddraw files into `<game>\Commandos2-ModernPatch\cnc-ddraw\` (or set
`CncDDraw=0`).

### How the patch works

Read [docs/TECHNICAL.md](docs/TECHNICAL.md) first. The patch hooks fixed addresses of the
Steam `Comm2.exe`. Every new hook should:

- check the expected bytes or call target before patching (`PatchCallRel32`, `PatchJmp`,
  `PatchImm32`) and log whether it was applied;
- only change game state temporarily during a draw and restore it afterwards, or run only
  in the logic tick, so the game's logic and timing stay unchanged;
- if it replaces game code (optimizations), be checked against the original code at
  startup, like the rasterizer self-tests.

Diagnostics for finding the code behind a problem are described in the *Diagnostics*
section of `docs/TECHNICAL.md` (`Debug=2`, `Profile=1`).

### Guidelines

- Everything in the repository is in English: code, comments, documentation, commit
  messages.
- Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/),
  e.g. `fix: prevent crash when zooming at high resolutions`, `feat: ...`, `perf: ...`,
  `docs: ...`.
- Describe new findings about the game (addresses, structures, call sites) in
  `docs/TECHNICAL.md`, and user-visible options in `README.md` and
  `dist/Commandos2-ModernPatch.ini`.
- Keep pull requests focused on one change and describe how you tested it (missions,
  resolution, refresh rate, with and without the patch).

## Code of Conduct

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md). By participating you are
expected to uphold it.
