# Kalamine user-space rules include issue

This document captures an investigation triggered by `kalamine`/`xkalamine` user‑space installs on Wayland. It is not an upstream xkbcommon report yet; it is a working plan for narrowing down whether the issue lives in libxkbcommon or in the way Kalamine writes rules files.

## Summary

`xkalamine install` (non‑root, Wayland) writes `~/.config/xkb/rules/evdev` that delegates to the system rules via:

```
include %S/evdev
```

In practice, the include appears to be ignored or fails silently on some systems. The result is that the user rules file “wins” the lookup but does not load any mappings; layout resolution falls back to defaults and produces incorrect symbols. Removing the user rules file fixes the layout immediately.

This doc proposes a plan to isolate whether:

1) libxkbcommon mishandles `%S` expansion in user rules includes, or
2) the include path resolution for user rules is incomplete/incorrect, or
3) Kalamine should not write `rules/evdev` at all for user‑space installs.

## Known facts

- libxkbcommon resolves rules from the first matching `rules/evdev` on the include path.
- User path (`$XDG_CONFIG_HOME/xkb`) outranks system paths.
- The libxkbcommon docs show examples of user rules files delegating via `include %S/evdev`.
- The failure only happens when a user rules file exists; removing it restores correct layout resolution.

## Hypotheses

1) **%‑expansion bug**: `%S` expansion fails in includes when invoked from a user rules file.
2) **Include path bug**: includes are resolved against a reduced or wrong include path list when processing a user rules file.
3) **Error reporting gap**: include errors are swallowed and do not surface in diagnostics.

## Repro outline

1. Create `~/.config/xkb/rules/evdev`:

```
// user rules
include %S/evdev
```

2. Call:

```
xkbcli compile-keymap --rules evdev --layout fr --variant bepo-extended
```

3. Observe:
- If resolution is wrong, confirm that deleting the user rules file fixes it.

## Investigation plan

### 1) Trace include expansion in rules parser

Add temporary logging in `src/xkbcomp/rules.c` around include processing:

- After `%` expansion, log the expanded include path.
- Log the actual file open attempt(s) and their results.

Goal: verify whether `%S` expands to the expected rules directory and whether the include is opened successfully.

### 2) Validate include path ordering for rules

Confirm the include path list used when a user rules file is the entry point:

- Inspect the include path stack in `src/context.c` / `src/registry.c`.
- Compare with the path list for keymap compilation (should include user, extra, system).

Goal: ensure the include path list is the same for root and for user rules contexts.

### 3) Add a focused regression test

Add a new test in `test/rules-file-includes.c` that:

- Creates a synthetic `$XDG_CONFIG_HOME/xkb/rules/evdev` with `include %S/evdev`.
- Points `%S` to a test rules dir using `XKB_CONFIG_ROOT` or `DFLT_XKB_CONFIG_ROOT` override.
- Verifies that the included rules are actually parsed.

Goal: pin down whether the problem is in include expansion or in include path resolution.

### 4) Confirm diagnostics

If include fails, ensure the error is visible with `XKB_LOG_LEVEL=debug` or a test‑only diagnostic hook.

Goal: avoid silent failures; make include errors observable.

## Proposed outcomes

- **If include expansion is wrong**: fix `%S` expansion in rules parser and add tests.
- **If include path list is wrong**: adjust rules parser to share the same include paths as keymap compilation.
- **If the include fails by design**: update docs to clarify that user rules files should not include `%S/evdev` and recommend alternate configuration patterns.

## Notes for Kalamine maintainers

Kalamine can avoid the issue by not writing `rules/evdev` at all for user‑space installs. This is a local workaround that prevents user rules from shadowing system rules, but the underlying libxkbcommon behavior should still be verified.
