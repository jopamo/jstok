<div style="background-color:#1e1e1e; padding:1em; display:inline-block; border-radius:8px; text-align:left;">
  <img src=".github/jstok.png" alt="jstok logo" width="300" style="display:block; margin:0;">
</div>

**jstok** is a robust, single-header, zero-allocation JSON parser and tokenizer for C. It is designed for embedded systems, high-performance applications, and scenarios where memory allocation is undesirable.

## Features

- **Zero Allocation**: Works entirely with caller-provided buffers. No `malloc`/`free`.
- **Single Header**: Easy integration into any C/C++ project.
- **Strict & Permissive**: Configurable strictness (e.g., standard JSON vs. practical extensions).
- **Helper API**: Includes utilities for traversing objects, arrays, and unescaping strings.
- **Streaming Friendly**: Supports incremental parsing and "Server-Sent Events" (SSE) line parsing.
- **Deep Nesting Support**: Configurable recursion depth limit (default 64).

## Installation

Simply copy `jstok.h` into your project's include directory.

## Usage

### 1. Integration

In *one* C file, define the implementation:

```c
#define JSTOK_HEADER
#include "jstok.h"

// In exactly one source file:
#include "jstok.h"
```

In other files, just include it:

```c
#include "jstok.h"
```

### 2. Basic Parsing

```c
#include "jstok.h"
#include <stdio.h>
#include <string.h>

int main() {
    const char *json = "{\"name\": \"Alice\", \"id\": 1234}";
    jstok_parser parser;
    jstoktok_t tokens[32]; // Allocate tokens on stack

    jstok_init(&parser);
  
    int count = jstok_parse(&parser, json, strlen(json), tokens, 32);

    if (count < 0) {
        printf("Parse error: %d at pos %d\n", count, parser.error_pos);
        return 1;
    }

    printf("Parsed %d tokens.\n", count);
  
    // tokens[0] is the root object
    return 0;
}
```

### 3. Using Helpers

`jstok` provides helpers to navigate the token tree without manual iteration:

```c
// Get a value from an object
int id_idx = jstok_object_get(json, tokens, count, 0, "id");
if (id_idx > 0) {
    long long id_val;
    jstok_atoi64(json, &tokens[id_idx], &id_val);
    printf("ID: %lld\n", id_val);
}

// Deep traversal with jstok_path
// Equivalent to: root["users"][0]["name"]
int val_idx = jstok_path(json, tokens, count, 0, "users", 0, "name", NULL);
```

### 4. SSE Parsing

`jstok` includes a specialized parser for Server-Sent Events (data stream):

```c
int pos = 0;
jstok_span_t payload;
while (jstok_sse_next(buffer, len, &pos, &payload)) {
    // payload.p points to the data content
    // Parse the JSON payload...
}
```

## Configuration

Define these macros before including `jstok.h` to customize behavior:

| Macro | Description |
|-------|-------------|
| `JSTOK_STATIC` | Make all functions `static` (for single-file tools). |
| `JSTOK_PARENT_LINKS` | Add a `parent` index to `jstoktok_t` struct. |
| `JSTOK_MAX_DEPTH` | Maximum nesting depth (default 64). |
| `JSTOK_STRICT` | Enforce strict JSON (no trailing commas, single root). |
| `JSTOK_NO_HELPERS` | Disable the helper API (parsing core only). |

## License

MIT License. See the header file for details.
