### 0) Test matrix and harness

* Build/run the same suite under these configs

  * default
  * `-DJSTOK_STRICT`
  * `-DJSTOK_PARENT_LINKS`
  * `-DJSTOK_STRICT -DJSTOK_PARENT_LINKS`
  * `-DJSTOK_NO_HELPERS` (compile-only: header compiles; core parse tests still run)
  * `-DJSTOK_STATIC` (compile/link check: no multiple-defs)
* Run tests in two parse modes

  * normal token mode (tokens array)
  * count-only mode (`tokens == NULL`)
* For parser tests, always assert

  * return code (>=0 or exact error)
  * `p.error_code` and `p.error_pos` for error paths
  * `p.pos`, `p.depth`, `p.root_done`, `p.toknext` invariants after each call
* Common helper assertions

  * token `type` is correct
  * token `start/end` boundaries match spec (strings exclude quotes, containers include delimiters, `end` exclusive)
  * container `size` matches semantics (object pair count, array element count)
* “One-shot vs incremental” equality checks where applicable

  * for success cases, compare tokens from incremental parse with one-shot parse of full input (structure + sizes; spans may match if buffer identical)

---

## A) Core tokenizer / parser (`jstok_parse`)

### A1) Basic valid JSON acceptance

* Primitives & literals

  * `null`, `true`, `false`
  * integers: `0`, `-1`, `123`
  * floats: `1.0`, `0.1`, `-0.25`
  * exponent: `1e3`, `1E3`, `1e+3`, `1e-3`
* Strings

  * empty `""`
  * ASCII `"abc"`
  * escapes: `\" \\ \/ \b \f \n \r \t`
  * unicode escape acceptance: `"\u0041"`, `"\u2603"`
* Containers

  * `[]`, `{}`
  * flat: `[1,2,3]`, `{"a":1,"b":2}`
  * nested: `{"a":[1,{"b":"c"}]}`

Assertions

* token count + token types
* boundary rules for string/container tokens
* size semantics for arrays/objects
* whitespace robustness: same JSON with varied whitespace parses to same structure

### A2) Token boundary and size semantics

* Objects: `{"a":1,"b":2}`

  * object.size == 2
  * children layout is `key,value,key,value`
  * key tokens are always `JSTOK_STRING`
* Arrays: `[1,2,[3]]`

  * outer.size == 3; inner array.size == 1
* End boundaries

  * container `end` points to after `}` / `]`

### A3) Syntax/grammar errors (`JSTOK_ERROR_INVAL` / `JSTOK_ERROR_PART`)

Bad object forms

* `{` EOF -> PART
* `{"a"}` -> INVAL (missing colon/value)
* `{"a":}` -> INVAL
* `{,"a":1}` -> INVAL
* `{"a":1,}` -> INVAL
* `{"a":1 "b":2}` -> INVAL (missing comma)
* `{"a":1,, "b":2}` -> INVAL

Bad array forms

* `[` EOF -> PART
* `[1,]` -> INVAL
* `[,1]` -> INVAL
* `[1 2]` -> INVAL
* `[1,,2]` -> INVAL

Bad closers / mismatched nesting

* `]` at root -> INVAL
* `{]`, `["a"}`, `{"a":1]` -> INVAL

Assertions

* return is correct error code
* `error_pos` points at offending byte (best-effort) and is stable across reruns

### A4) String scanning errors and partials

* Control chars in strings (literal newline, <0x20) -> INVAL
* Bad escapes

  * `"\q"` -> INVAL
  * `"\u12X4"` -> INVAL
* Partial strings (incremental)

  * `"abc` -> PART and `p.pos` rewinds to opening quote
  * `"a\` end -> PART and rewind
  * `"a\u12` end -> PART and rewind
* Verify no “half-committed” accept_value side effects on PART

  * container sizes not incremented permanently
  * `root_done` not permanently set incorrectly on PART

### A5) Number scanning errors and delimiter enforcement

Invalid numbers

* `-` -> PART
* `1.` -> PART
* `1e` / `1e+` -> PART
* `1a` -> INVAL

Delimiter enforcement

* `123]`, `123}`, `123,` allowed
* `123x` -> INVAL

Leading zero behavior

* non-strict: pin current behavior with tests (document if accepted)
* strict: `01`, `-01` -> INVAL

### A6) Strict-mode specific behavior (`JSTOK_STRICT`)

* Exactly one top-level value

  * `true false` -> INVAL
  * `[] {}` -> INVAL
* Leading zeros rejected (as above)
* Trailing whitespace after root is allowed

### A7) Depth and token/memory limits

Depth limit

* nesting `JSTOK_MAX_DEPTH + 1` -> `JSTOK_ERROR_DEPTH`
* exactly `JSTOK_MAX_DEPTH` succeeds

Token limit / NOMEM

* input requiring N tokens with `max_tokens = N-1` -> `JSTOK_ERROR_NOMEM`
* verify parser stops safely; `error_pos` near where token would be emitted

Count-only mode correctness

* representative JSON set: returned count matches token-mode count

### A8) Incremental parsing (resume) behavior

Split-at-interesting boundaries

* mid-string, after backslash, mid-`\u` escape
* mid-number (`1e`, `1e+`)
* mid-container after `{` and after `"key"`
* between tokens / inside whitespace runs

Procedure

* parse prefix -> expect PART
* append more bytes, call again without `jstok_init` -> expect still PART or success
* on success, compare against one-shot parse of full buffer

No-progress safeguards

* calling again without adding bytes returns PART consistently with stable `error_pos` and rewound `p.pos`

---

## B) Helper APIs (when `!JSTOK_NO_HELPERS`)

### B1) `jstok_span`

* For each token type, returned {ptr,len} matches expected slice
* Invalid inputs return {NULL,0}

  * NULL json/token, negative start/end, end < start

### B2) `jstok_eq`

* Exact match behavior

  * object key token equals expected C-string
  * primitive token equals `"true"`, `"false"`, `"null"`, digits
* Non-match behavior

  * length differs, prefix differs, case differs
* NULL handling returns 0

### B3) `jstok_skip`

* Leaf tokens return `i+1`
* Array skipping

  * `[1,[2,3],4]` skipping element 1’s subtree lands on `4`
* Object skipping

  * `{"a":1,"b":{"c":2},"d":3}` skipping `b` value lands on `d` key
* Stress within `JSTOK_MAX_DEPTH`
* Robustness for truncated token arrays

  * never OOB; returns `count` when it can’t complete

### B4) `jstok_array_at`

* Valid indices (0/mid/last) return correct token index, including nested containers
* Out-of-range returns -1
* Non-array token returns -1

### B5) `jstok_object_get`

* Finds key at beginning/middle/end
* Missing key returns -1
* “Prefix key” case

  * ensure searching `"a"` does not match `"apple"`
* Values that are containers are handled via skip (correct next search position)
* Non-object token returns -1

### B6) `jstok_atoi64`

* Success: `0`, `-1`, `12345`
* Failure

  * non-primitive token
  * primitive not pure base10 digits (`1.0`, `1e3`, `true`, `null`)
* Overflow policy test

  * pick and pin a policy

    * either “unchecked/wrap is acceptable” (avoid overflow tests)
    * or add checked version later and test overflow failure

### B7) `jstok_atob`

* `true` -> 1
* `false` -> 0
* anything else -> -1

### B8) `jstok_unescape`

* Basic escapes and mixed text
* `\/` preserved as `/`
* `\uXXXX` UTF-8 encoding coverage

  * 1-byte (U+0000..U+007F)
  * 2-byte (U+0080..U+07FF)
  * 3-byte (U+0800..U+FFFF)
* Buffer sizing

  * exact fit succeeds
  * too small fails
  * `out_len` matches bytes written
* Invalid escape sequences fail
* Surrogates policy test

  * document current behavior (treat independently or reject) and pin it

### B9) `jstok_path`

* Happy path traversal like OpenAI responses

  * object -> key -> array -> index -> object -> key
* Early stop sentinel NULL returns current token
* Mismatch cases

  * key when current is array
  * index when current is object
  * attempt to traverse beyond primitive/string
* Missing key / out-of-range index returns -1

---

## C) SSE helper (`jstok_sse_next`)

### C1) Basic extraction

* `data: hi\n` -> DATA, payload `hi`
* `data:hi\n` -> DATA, payload `hi`
* `data:  hello\n` -> payload `" hello"` (only strips one optional space)
* Multiple fields

  * `event:x\nid:1\ndata: ok\n` -> payload `ok`
* CRLF

  * `data: ok\r\n` -> payload `ok`

### C2) Skip behavior

* Empty lines are skipped

  * `\n\ndata: x\n` -> payload `x`
* Non-data lines skipped

  * `event: x\nretry: 1\ndata: y\n` -> payload `y`
* Optional future comment support (`:`)

  * if re-added, test `:comment\ndata: z\n` -> payload `z`

### C3) Fragmentation / NEED_MORE semantics

* No newline yet

  * `data: hi` -> NEED_MORE and `pos` rewound to start of that line
* Partial prefix across boundary

  * earlier complete lines, then buffer ends with `da` -> NEED_MORE, `pos` == start of `da...`
* Ensure already-consumed lines aren’t re-parsed

  * advance through many non-data lines; cut mid-line; `pos` must point to incomplete line start, not 0

### C4) `pos` clamping / invariants

* `*pos > len` clamps
* `*pos == len` -> NEED_MORE
* After DATA, `*pos` == byte after `\n` (start of next line)

### C5) Output span correctness

* Excludes `data:` and optional single space
* Excludes trailing `\r` on CRLF lines
* `out == NULL` still advances and returns DATA

---

## D) Compile-time / ODR / packaging tests

* Header-only pattern compiles

  * one TU with `#define JSTOK_HEADER` (decls only)
  * one TU without it (impl)
* `JSTOK_STATIC` compiles/links cleanly across multiple TUs
* `JSTOK_NO_HELPERS` compiles and helper symbols are absent

---

## E) Deterministic regression + fuzz-style unit hooks

* Corpus of small JSON snippets (valid + invalid) that represent prior bugs
* “Split-at-every-byte” incremental regression for a few representative JSON docs (kept small for CI)
* Optional: generator-based “round trip” (generate valid JSON AST -> serialize -> parse -> structural checks)
* Optional: large-size / overflow-adjacent checks

  * ensure APIs that accept `int json_len` reject negative and behave correctly near `INT_MAX`
  * SSE uses `size_t`; include a “very large length” style test via constructed buffers (no actual huge allocation) where possible in your harness
