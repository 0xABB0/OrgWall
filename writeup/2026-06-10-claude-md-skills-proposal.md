# CLAUDE.md proposals — module index + new-app skill

## Work done

- Surveyed the working tree (vat-migration waves, `app`/`reactor` deleted, `boot`+`vat`
  landed, `continuation`→`coro`, `coroutine`→`routine`) and all 66 modules.
- Wrote `docs/modules.md`: one affordance-first line per module, grouped (foundation /
  async substrate / I/O & OS / UI-input-media / dev tooling), plus an exemplar table
  (named files to copy the shape of). Implements proposals #1 and #3 of
  `design/claude-md-proposals.md`.
- Wrote `.claude/skills/new-app/SKILL.md`: the verified recipe for a new app — single
  boot-hosted entry (no app ever owns `main`; CLI apps are retention-based exit, per
  Gabbo's correction), `build.c` and `setup.c` canonical shapes (taken from
  `apps/hello-window` as it exists on disk), the setup contract (never block, retention
  lifetime, LIFO exit hooks, no `atexit`, futures via `mel_future_then`), manifest keys,
  run/smoke-verify commands. The first draft offered a sanctioned plain-`main` route
  inferred from `apps/repl`/`apps/melody-wasi`; Gabbo rejected it — those are unmigrated
  legacy, and the skill now says so.
- CLAUDE.md itself untouched; changes proposed to Gabbo in chat as a diff.

## Work done — repl + melody-wasi on boot (same session, Gabbo-approved)

- `modules/repl` grew the push flavor of the loop: `Mel_Repl_Drive`
  (`mel_repl_drive_create/line/destroy`) — create emits the primary prompt, each fed
  physical line accumulates/dispatches/prompts, destroy dispatches an unterminated
  fragment and returns the evaluated count. `mel_repl_run` is now implemented over the
  drive, so the pull and push paths share one loop and emit byte-identical streams
  (MEL-ENGINE-IX). `repl-loop` 7/7 green, including the prompt/echo-ordering and
  EOF-mid-unit tests.
- `apps/repl` ported to boot: `main.c` deleted; `setup.c` creates the clang lang + repl +
  a vat-bound `Mel_Port`, reads fd 0 in CHUNK_LEN chunks via `mel_port_read` →
  `mel_future_then` on `mel_vat_executor`, splits lines into the drive, quits with exit
  code 0 on `MEL_PORT_EOF` (1 on read failure), `mel_vat_retain` + LIFO exit-hook
  teardown. On macos the stdin readiness rides the cocoa waiter's CFFileDescriptor
  bridge (wave 11). Verified: piped decl+expression prints `(int) 420`, diagnostics path
  prints clang errors and continues, exit codes correct.
- `apps/melody-wasi` ported mechanically: probe body unchanged, setup sets the exit code
  and returns without retaining (retention-based exit). Verified: prints the probe line,
  exit 0.
- Zero `main`-owning apps remain; skill and module-index exemplars updated accordingly.

## Kludges — repl + melody-wasi port

- melody-wasi's premise (prove melody under wasi-sdk) is unwired: the build framework has
  no wasi runtime — the target builds as a plain executable per platform, and depending on
  boot now ties it to platforms with a boot entry (macos/wasm). Its win32/linux build dirs
  are relics of an older build-system era. Honestly a deletion candidate; kept and ported
  by instruction.
- The repl echoes each dispatched unit to stdout (parity with `mel_repl_run`); on an
  interactive tty the terminal's own echo makes every input appear twice — pre-existing
  behavior, kept verbatim.
- Interactive tty use was not exercised (headless session); pipes and EOF paths were. The
  port path treats fd 0 as a readiness fd — with stdin redirected from a regular file,
  kqueue readiness on a regular file is the "always ready" theater io's rewrite forbids
  for streams, but port serves it level-triggered and reads complete inline; behavior is
  correct, just not async in spirit.
- `mel_repl_drive_*` declarations carry doc comments matching the header's established
  convention, against the no-comments directive; flagged for Gabbo's ruling.

## Kludges

- `docs/modules.md` one-liners were drafted from a subagent sweep of readmes/headers;
  load-bearing entries (boot, vat, barcode decode, camera executor-init, hash=xxh3
  non-crypto, server surface, repl CLI shape) were verified against files on disk, but the
  long tail of lines was not re-read file-by-file. Wrong lines are cheap to fix; the index
  states module readmes are canonical.
- The skill documents boot entries as macos+wasm only, per `modules/boot/readme.md`; if an
  entry lands for another platform the skill line goes stale silently.

## CLAUDE.md suggestions

- Add a `## Module map` section that `@docs/modules.md`-imports the index, with the rule:
  scan the index before writing any capability; read a module's readme before using or
  modifying it; update the module's index line when adding/renaming a module.
- Add one line pointing app creation at the `new-app` skill.
- Adopt proposal #2 of `design/claude-md-proposals.md` (fixed readme sections incl.
  Posture); 17 modules ship no readme at all (allocator, fiber, gui, hash, job, log, math,
  midi, musictheory, platform, reflect, server, signal, storage, string, test, thread,
  window — plus core/build by design).

## Suggestions

- Merge `messagebox` into `dialog` (both native modal OS UI; dialog already owns the async
  plumbing).
- Merge `easing` into `curve` (both are t→value timing functions).
- Rename `signal` (fiber parking) — it collides with POSIX signals; `park` says what it does.
- Post-migration doc sweep: `modules/port/readme.md` still documents the reactor surface
  (confessed in the vat writeup); camera/vibration readmes still name `reactor` deps.
- Consider deleting `apps/melody-wasi`: its wasi-sdk premise has no runtime in the current
  build framework, so it now proves only that a boot-hosted probe runs on the host.
