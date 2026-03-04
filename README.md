<div style="background-color:#1e1e1e; padding:1em; display:inline-block; border-radius:8px; text-align:left;">
  <img src=".github/jstok.png" alt="jstok logo" width="300" style="display:block; margin:0;">
</div>

**jstok** is a **zero-allocation, single-header JSON tokenizer and structural parser for C**.
It is built for environments where **predictable memory**, **input robustness**, and **low overhead** matter more than convenience abstractions.

`jstok` does not build a DOM, does not allocate, and does not recurse. It validates structure, emits compact tokens, and leaves all interpretation to the caller.

---

## Highlights

* **Zero Allocation**

  * Entirely caller-owned memory
  * No `malloc`, no hidden buffers, no surprises

* **Single Header**

  * Drop-in `jstok.h`
  * No build system coupling, no linker steps

* **Fail-Safe by Design**

  * Never crashes on malformed or hostile input
  * Explicit error codes and error position reporting

* **Fast, Single-Pass Parsing**

  * Linear scan
  * Minimal branching in the hot loop
  * Cache-friendly token layout

* **Configurable Strictness**

  * Strict RFC-style JSON or permissive “real-world” JSON
  * Compile-time feature control via macros

* **Helper API (Optional)**

  * Object and array traversal
  * Path lookup
  * Deferred string unescaping
  * Integer conversion helpers

* **Streaming Friendly**

  * Incremental parsing support
  * Built-in Server-Sent Events (SSE) line extraction

* **Deep Nesting Support**

  * Explicit, configurable depth limit
  * Default: 64
  * No risk of C stack exhaustion

---

## Installation

### Header-Only

Copy `jstok.h` into your include path and you are done.

---

### Meson (Optional)

```bash
meson setup build
meson install -C build
```

Installs:

* `jstok.h`
* `jstok.pc` (pkg-config)

Usage:

```bash
pkg-config --cflags jstok
```

---

### Consumed by `desid`

`desid` depends on installed `jstok` artifacts (`jstok.h` + `jstok.pc`) via `pkg-config`.

Recommended integration pattern:

1. Install `jstok` to a prefix used by your workspace.
2. Ensure that prefix is visible to `pkg-config`.
3. Build `desid` against the installed package instead of sibling source paths.

Example:

```bash
# in jstok repo
meson setup build --prefix /opt/desi
meson compile -C build
meson install -C build

# in desid repo
export PKG_CONFIG_PATH=/opt/desi/lib/pkgconfig:${PKG_CONFIG_PATH}
meson setup build
meson compile -C build
```

---

## Usage

### 1. Integration Model

In **exactly one** C or C++ file:

```c
#define JSTOK_HEADER
#include "jstok.h"

// implementation emitted here
#include "jstok.h"
```

Everywhere else:

```c
#include "jstok.h"
```

This avoids multiple-definition issues while keeping everything header-only.

Compatibility and release references:

- `API_SURFACE.md`
- `MACRO_COMPATIBILITY.md`
- `API_BREAK_CHECKLIST.md`
- `RELEASE_CHECKLIST.md`
- `CHANGELOG.md`

---

### 2. Basic Parsing

```c
#include "jstok.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *json = "{\"name\": \"Alice\", \"id\": 1234}";
    jstok_parser parser;
    jstoktok_t tokens[32];

    jstok_init(&parser);

    int count = jstok_parse(
        &parser,
        json,
        strlen(json),
        tokens,
        32
    );

    if (count < 0) {
        printf("parse error %d at byte %d\n",
               count,
               parser.error_pos);
        return 1;
    }

    printf("parsed %d tokens\n", count);

    // tokens[0] is always the root value
    return 0;
}
```

---

### 3. Incremental / Streaming Parsing

Use `jstok_parse_ex` for chunked input. Pass `0` while more bytes may arrive,
then pass `JSTOK_PARSE_FINAL` on the last call.

```c
int r = jstok_parse_ex(&parser, buf, (int)len, tokens, 32, 0);
if (r == JSTOK_ERROR_PART) {
    // need more bytes
}

r = jstok_parse_ex(&parser, buf, (int)len, tokens, 32, JSTOK_PARSE_FINAL);
```

Contract:

* `jstok_parse(...)` is equivalent to
  `jstok_parse_ex(..., JSTOK_PARSE_FINAL)`.
* In incremental mode (`flags == 0`), primitives ending exactly at the current
  buffer boundary may return `JSTOK_ERROR_PART` until a delimiter is seen or
  final mode is requested.

---

### 4. Helper API Examples

#### Object Lookup

```c
int id_idx = jstok_object_get(
    json,
    tokens,
    count,
    0,
    "id"
);

if (id_idx > 0) {
    long long id;
    jstok_atoi64(json, &tokens[id_idx], &id);
    printf("ID: %lld\n", id);
}
```

---

#### Path Traversal

```c
// Equivalent to: root["users"][0]["name"]
int name_idx = jstok_path(
    json,
    tokens,
    count,
    0,
    "users",
    0,
    "name",
    NULL
);
```

---

### 5. Server-Sent Events (SSE)

Extract JSON payloads from an SSE stream without copying:

```c
int pos = 0;
jstok_span_t payload;

while (jstok_sse_next(buffer, len, &pos, &payload)) {
    // payload.p   -> pointer to JSON data
    // payload.len -> length of JSON data

    // parse payload as normal JSON
}
```

---

## Configuration

Define these **before** including `jstok.h`:

| Macro                | Effect                             |
| -------------------- | ---------------------------------- |
| `JSTOK_STATIC`       | Emit all functions as `static`     |
| `JSTOK_PARENT_LINKS` | Add parent index to tokens         |
| `JSTOK_MAX_DEPTH`    | Maximum nesting depth (default 64) |
| `JSTOK_STRICT`       | Enforce strict RFC JSON            |
| `JSTOK_NO_HELPERS`   | Disable helper API entirely        |

All options are compile-time and zero-cost when disabled.

---

## What jstok Is (and Is Not)

**Is**

* A tokenizer
* A structural validator
* A zero-allocation parsing core
* A foundation for higher-level JSON tooling

**Is Not**

* A DOM builder
* A serializer
* A schema validator
* A dynamic JSON value system

---

## License

MIT
