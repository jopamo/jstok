# Hacking on jstok

## Development Setup

`jstok` is a self-contained C library. You need the following dependencies:

- **Compiler**: GCC or Clang (standard C99 support).
- **Build System**: `meson` and `ninja`.
- **Fuzzing (Optional)**: `clang` with `libfuzzer`.

### Directory Structure

- `jstok.h`: The entire source code (interface + implementation).
- `tests/`: Unit tests and fuzzing code.
- `README.md`: User documentation.
- `DESIGN.md`: Architecture and internal logic.

## Build and Test

### Using Meson

`jstok` uses the Meson build system for running tests.

```bash
meson setup build
meson test -C build
```

### Manual Compilation

You can also compile the test driver manually:

```bash
gcc -Wall -Wextra -std=c99 -I. -o test_jstok tests/test_jstok.c
./test_jstok
```

### Fuzzing

To run the fuzzer, you need `clang` and `libfuzzer`.

```bash
# Configure with Clang
CC=clang meson setup build-fuzz

# Build the fuzzer
meson compile -C build-fuzz fuzz_jstok

# Run it
./build-fuzz/fuzz_jstok
```

## Coding Standards

- **Language**: C89/C90 compatible, optionally C99/C11 features if guarded.
- **Indentation**: 4 spaces.
- **Style**:
  - `snake_case` for functions and variables.
  - `JSTOK_UPPER_CASE` for macros and enums.
  - Opening braces `{` on the same line as the statement.
- **Safety**:
  - Always check bounds when accessing arrays or buffers.
  - Use `const` correctness for input buffers.

## Contribution Flow

1. **Branch**: Create a branch for your feature or fix.
2. **Verify**: Ensure code compiles without warnings (`-Wall -Wextra`) and pass all tests.
3. **Fuzz**: Verify that `jstok_parse` does not crash on malformed input.
4. **Update Docs**: Update `README.md` if the API changes.
5. **Submit**: Open a Pull Request.
