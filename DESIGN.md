# jstok — Design Document

## Overview

`jstok` is a **non-recursive, single-pass JSON tokenizer/parser** designed for **predictable memory usage**, **hard failure safety**, and **high throughput**.

Rather than relying on the C call stack, `jstok` implements an explicit state machine with a user-supplied stack. This makes behavior deterministic under all inputs, including adversarial or deeply nested JSON.

The parser **never allocates memory**, **never recurses**, and **never crashes on malformed input**.

---

## Design Goals

### 1. Deterministic Memory

* All memory is owned and provided by the caller
* No `malloc`, `realloc`, or implicit allocations
* Stack depth and token capacity are fixed and explicit

### 2. Total Input Safety

* Any byte sequence is valid input
* Malformed JSON results in a clean error code, never UB
* Stack overflow, token overflow, and syntax errors are detected early

### 3. Performance

* Single linear scan
* Minimal branching in the hot loop
* Zero string copies
* Cache-friendly token layout

---

## Core Data Structures

### `jstoktok_t` — Token

Represents a node in the JSON structure.

Fields:

* `start`, `end`
  Byte offsets into the input buffer
  Strings and numbers are never copied

* `size`
  Semantic size

  * `OBJECT`: number of key-value pairs
  * `ARRAY`: number of elements
  * Other types: always 0

* `type`
  Token type bitmask
  Structured as flags for future extensibility, though currently used as an enum

**Invariant**
A token is immutable once closed except for `size` updates while its container is active.

---

### `jstok_parser` — Parser State

Holds all mutable parsing state.

* `stack`
  Array of `jstok_frame_t` representing nested containers

* `depth`
  Current stack depth
  Always points to the active frame

* `pos`
  Current byte offset in the input

* `toknext`
  Index of the next token slot to fill
  Always monotonically increasing

**Invariant**
`toknext` is incremented exactly once per syntactic value, even in count-only mode.

---

### `jstok_frame_t` — Stack Frame

Represents an open container (object or array).

* `type`
  `JSTOK_OBJECT` or `JSTOK_ARRAY`

* `st`
  Fine-grained internal state
  Examples:

  * object expecting key
  * object expecting colon
  * object expecting value
  * array expecting value or close

* `tok`
  Index of the token representing this container
  Used to update `size` and finalize `end`

**Invariant**
The top frame always corresponds to the most recently opened container token.

---

## Parsing Model

The parser executes a single loop over the input buffer.

### 1. Whitespace

* Skipped unconditionally
* Never generates tokens
* Legal in all states where JSON permits it

---

### 2. Open Delimiters (`{`, `[`)

* Validate against current state
* Emit a container token
* Push a new frame onto the stack
* Initialize container state machine

Failure cases:

* Stack overflow
* Token buffer overflow
* Invalid placement

---

### 3. Close Delimiters (`}`, `]`)

* Validate against top frame type
* Validate container state
* Finalize token `end`
* Pop stack frame
* Increment parent container `size`

Failure cases:

* Mismatched closer
* Premature close
* Empty stack

---

### 4. Strings (`"`)

* Parsed linearly until closing quote
* Escape sequences are validated, not decoded
* UTF-8 correctness is validated only as required by JSON

Decoding is deferred to `jstok_unescape`, allowing zero-copy parsing.

Failure cases:

* Unterminated string
* Invalid escape
* Invalid UTF-8 escape form

---

### 5. Primitives

Handled by prefix dispatch:

* `t`, `f`, `n`
  Literal verification against `true`, `false`, `null`

* `-`, `0–9`
  Number grammar validation only
  No numeric conversion is performed

Failure cases:

* Invalid literal spelling
* Invalid number grammar
* Premature termination

---

## Count-Only / Validation Mode

If the `tokens` pointer is `NULL`:

* No token writes occur
* `toknext` is still incremented
* Full syntax validation is performed

This allows:

* Exact token count discovery
* Single allocation strategy
* Optional two-pass parsing without duplication of logic

---

## Error Handling Model

* All errors are detected synchronously
* The parser never reads out of bounds
* Partial tokens are never committed
* Stack and token limits are enforced before mutation

The parser exits immediately on error, leaving the parser state consistent and inspectable.

---

## Decision Log

### Non-Recursive Descent

**Decision**
Use an explicit stack instead of recursion

**Rationale**

* Eliminates stack overflow risk
* Makes maximum depth explicit and enforceable
* Enables suspend/resume or incremental parsing
* Suitable for embedded and hostile-input environments

---

### Flat Token Array

**Decision**
Store tokens in a contiguous array

**Rationale**

* Excellent cache locality
* O(1) index-based navigation
* No per-node allocation overhead
* Single allocation simplifies lifetime management

---

### Single-Header Distribution

**Decision**
Ship as a single `.h` file

**Rationale**

* Zero build-system friction
* Easy vendoring
* No ABI or linker concerns
* Encourages use in low-level and embedded codebases

---

## Non-Goals

* No DOM construction
* No string decoding during parse
* No numeric conversion
* No schema validation

`jstok` is a **tokenizer and structural validator**, not a semantic interpreter.
