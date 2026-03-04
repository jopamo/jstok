# Contributing to jstok

## Scope

`jstok` is a header-only tokenizer and structural JSON parser.

Core invariants:
- zero allocation (`malloc`-free)
- non-recursive parsing
- deterministic tokenization and bounds behavior

## Prerequisites

- C compiler (`clang` or `gcc`)
- `meson`, `ninja`, `pkg-config` (for optional install/testing flow)

## Build and Test

```sh
meson setup build
meson compile -C build
meson test -C build
```

Optional install verification:

```sh
meson install -C build
pkg-config --cflags jstok
```

## Change Requirements

- Preserve header-only usage and one-definition integration model.
- Add or update regression tests for parser behavior changes.
- Do not weaken strict-mode or bounds guarantees.

## Pull Request Checklist

- Describe parser invariant(s) affected by the change.
- Include exact local verification commands and outcomes.
- Update README docs for any API or semantic changes.
