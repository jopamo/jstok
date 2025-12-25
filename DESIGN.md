# jstok Design Document

## High-Level Architecture

`jstok` is a **non-recursive descent parser**. Instead of using the C stack for recursion, it maintains an explicit state stack within the `jstok_parser` struct.

This design ensures:
1. **Predictable Memory Usage**: The user controls memory allocation. The parser never calls `malloc`.
2. **Robustness**: The parser must never crash on invalid input, no matter how malformed.
3. **Performance**: Single-pass scanning with minimal branching in the hot loop.

## Core Components

### `jstoktok_t` (Token)
Represents a node in the JSON tree.
- `start`/`end`: Indices into the source string. This avoids copying strings.
- `size`: Semantic size.
  - For `OBJECT`: Number of key-value pairs.
  - For `ARRAY`: Number of elements.
  - For others: 0.
- `type`: Bitmask (though currently used as enum) for type checking.

### `jstok_parser` (State Machine)
- `stack`: An array of `jstok_frame_t` that tracks the current nesting context.
- `depth`: Current index in the stack.
- `pos`: Current scanning position in the input buffer.
- `toknext`: Index of the next token to write.

### `jstok_frame_t` (Stack Frame)
Tracks the state of the current container (Object or Array).
- `type`: `JSTOK_OBJECT` or `JSTOK_ARRAY`.
- `st`: The current state within the container (e.g., `JSTOK_ST_OBJ_KEY`, `JSTOK_ST_OBJ_COLON`).
- `tok`: Index of the token representing this container (so we can update its `size` and `end`).

## Data Flow

The parser runs a loop over the input characters:

1. **Whitespace**: Skipped globally.
2. **Delimiters** (`{`, `[`): Push a new frame onto the stack. Create a new token.
3. **Closers** (`}`, `]`): Validate against the top stack frame. If valid, pop the frame and finalize the container token (set `end` position).
4. **Strings** (`"`): Parsed linearly. Escapes are validated but not decoded (decoding happens lazily via `jstok_unescape`).
5. **Primitives** (`t`, `f`, `n`, `0-9`, `-`): Parsed by verifying literal characters or number syntax.

### Count-Only Mode
If `tokens` argument is `NULL`, the parser acts as a validator and token counter. It increments `toknext` but does not write to the token array. This allows users to determine the required buffer size in a first pass.

## Decision Log

### Non-Recursive Descent
**Decision**: Use an explicit stack instead of recursion.
**Rationale**: Prevents stack overflow on deeply nested JSON, which is critical for embedded systems and security. Enables full async/resume support (incremental parsing) by preserving state on the heap/stack structure.

### Single-Header
**Decision**: Distribute as a single `.h` file.
**Rationale**: Simplifies integration for C projects. No need to manage link paths or separate object files.

### Token Array vs Linked List
**Decision**: Use a flat array for tokens.
**Rationale**: Better cache locality and allows O(1) random access by index if the structure is known. Simplifies memory management (single block allocation).
