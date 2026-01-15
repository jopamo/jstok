/* Enforce strict JSON compliance for tests to verify standard behavior */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jstok.h"

/* Minimal test framework */
int tests_run = 0;
int tests_failed = 0;

#define TEST(name)                       \
    do {                                 \
        printf("Running %s... ", #name); \
        if (test_##name()) {             \
            printf("PASS\n");            \
        } else {                         \
            printf("FAIL\n");            \
            tests_failed++;              \
        }                                \
        tests_run++;                     \
    } while (0)

#define ASSERT(cond)                                                                \
    do {                                                                            \
        if (!(cond)) {                                                              \
            printf("\nAssertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 0;                                                               \
        }                                                                           \
    } while (0)

#define ASSERT_EQ(val, expected)                                                                  \
    do {                                                                                          \
        int v = (val);                                                                            \
        int e = (expected);                                                                       \
        if (v != e) {                                                                             \
            printf("\nAssertion failed at %s:%d: %s (got %d, expected %d)\n", __FILE__, __LINE__, \
                   #val " == " #expected, v, e);                                                  \
            return 0;                                                                             \
        }                                                                                         \
    } while (0)

/* -------------------------------------------------------------------------- */
/* 1. Core Parsing & Tokenizer Basics */
/* -------------------------------------------------------------------------- */

int test_core_empty(void) {
    jstok_parser p;
    jstoktok_t t[10];

    // 1.1 Empty Object
    jstok_init(&p);
    ASSERT(jstok_parse(&p, "{}", 2, t, 10) == 1);
    ASSERT(t[0].type == JSTOK_OBJECT);
    ASSERT(t[0].size == 0);

    // 1.2 Empty Array
    jstok_init(&p);
    ASSERT(jstok_parse(&p, "[]", 2, t, 10) == 1);
    ASSERT(t[0].type == JSTOK_ARRAY);
    ASSERT(t[0].size == 0);

    return 1;
}

int test_core_literals(void) {
    jstok_parser p;
    jstoktok_t t[10];
    const char* json_str = "\"hello\"";
    const char* json_bools = "[true, false, null]";

    // 1.3 Single String
    jstok_init(&p);
    ASSERT(jstok_parse(&p, json_str, (int)strlen(json_str), t, 10) == 1);
    ASSERT(t[0].type == JSTOK_STRING);
    // start after ", end at closing "
    ASSERT(json_str[t[0].start] == 'h');
    ASSERT(json_str[t[0].end] == '"');

    // 1.4 Primitives (Literals)
    jstok_init(&p);
    ASSERT(jstok_parse(&p, json_bools, (int)strlen(json_bools), t, 10) == 4);

#ifndef JSTOK_NO_HELPERS
    int val;
    ASSERT(jstok_atob(json_bools, &t[1], &val) == 0);
    ASSERT(val == 1);  // true
    ASSERT(jstok_atob(json_bools, &t[2], &val) == 0);
    ASSERT(val == 0);                                   // false
    ASSERT(jstok_atob(json_bools, &t[3], &val) == -1);  // null (not boolean)
#endif

    return 1;
}

int test_core_numbers(void) {
    jstok_parser p;
    jstoktok_t t[10];
    const char* json_ints = "[0, 123, -456]";
#ifndef JSTOK_NO_HELPERS
    const char* json_floats = "[0.5, 123.456, -1.2e+10]";
#endif

    // 1.5 Numbers (Integer)
    jstok_init(&p);
    ASSERT(jstok_parse(&p, json_ints, (int)strlen(json_ints), t, 10) == 4);

#ifndef JSTOK_NO_HELPERS
    long long n;
    ASSERT(jstok_atoi64(json_ints, &t[1], &n) == 0);
    ASSERT(n == 0);
    ASSERT(jstok_atoi64(json_ints, &t[2], &n) == 0);
    ASSERT(n == 123);
    ASSERT(jstok_atoi64(json_ints, &t[3], &n) == 0);
    ASSERT(n == -456);

    // 1.6 Numbers (Floating Point)
    // We don't have jstok_atof in standard API, but we check token boundaries
    jstok_init(&p);
    ASSERT(jstok_parse(&p, json_floats, (int)strlen(json_floats), t, 10) == 4);

    // 0.5
    jstok_span_t s1 = jstok_span(json_floats, &t[1]);
    ASSERT(strncmp(s1.p, "0.5", s1.n) == 0);

    // 123.456
    jstok_span_t s2 = jstok_span(json_floats, &t[2]);
    ASSERT(strncmp(s2.p, "123.456", s2.n) == 0);

    // -1.2e+10
    jstok_span_t s3 = jstok_span(json_floats, &t[3]);
    ASSERT(strncmp(s3.p, "-1.2e+10", s3.n) == 0);
#endif

    return 1;
}

/* -------------------------------------------------------------------------- */
/* 2. Nesting & Hierarchy */
/* -------------------------------------------------------------------------- */

int test_nesting_structure(void) {
    jstok_parser p;
    jstoktok_t t[20];

    // 2.1 Simple Nesting
    const char* json = "{\"a\": [1, 2]}";
    jstok_init(&p);
    int count = jstok_parse(&p, json, (int)strlen(json), t, 20);
    ASSERT(count == 5);
    ASSERT(t[0].type == JSTOK_OBJECT);
    ASSERT(t[0].size == 1);  // 1 pair

    int key_idx = 1;
    ASSERT(t[key_idx].type == JSTOK_STRING);
#ifndef JSTOK_NO_HELPERS
    ASSERT(jstok_eq(json, &t[key_idx], "a"));
#endif

    int arr_idx = 2;
    ASSERT(t[arr_idx].type == JSTOK_ARRAY);
    ASSERT(t[arr_idx].size == 2);  // 2 elements

    return 1;
}

int test_nesting_depth(void) {
    jstok_parser p;
    jstoktok_t t[100];

    // 2.2 Deep Nesting (within limits)
    // [[[[["deep"]]]]] -> 5 arrays + 1 string
    const char* deep = "[[[[[\"deep\"]]]]]";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, deep, (int)strlen(deep), t, 100) > 0);

    // 2.3 Max Depth Exceeded
    // Create a string with > 64 nested arrays
    char huge[200];
    int i;
    for (i = 0; i < 70; i++) huge[i] = '[';
    huge[70] = '\0';

    jstok_init(&p);
    int r = jstok_parse(&p, huge, 70, t, 100);
    ASSERT(r == JSTOK_ERROR_DEPTH);

    return 1;
}

/* -------------------------------------------------------------------------- */
/* 3. Syntax Validation (Strict Mode) */
/* -------------------------------------------------------------------------- */

int test_syntax_errors(void) {
    jstok_parser p;
    jstoktok_t t[10];

    // 3.1 Trailing Comma (Object)
    const char* bad_obj = "{\"a\": 1,}";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, bad_obj, (int)strlen(bad_obj), t, 10) == JSTOK_ERROR_INVAL);

    // 3.2 Trailing Comma (Array)
    const char* bad_arr = "[1, 2,]";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, bad_arr, (int)strlen(bad_arr), t, 10) == JSTOK_ERROR_INVAL);

    // 3.3 Missing Colon
    const char* no_colon = "{\"a\" 1}";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, no_colon, (int)strlen(no_colon), t, 10) == JSTOK_ERROR_INVAL);

    // 3.4 Missing Value
    const char* no_val = "{\"a\": }";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, no_val, (int)strlen(no_val), t, 10) == JSTOK_ERROR_INVAL);

    // 3.5 Missing Comma
    const char* no_comma = "[1 2]";
    jstok_init(&p);
    // Depending on parser logic, it might see '2' as invalid delimiter after '1'
    ASSERT(jstok_parse(&p, no_comma, (int)strlen(no_comma), t, 10) == JSTOK_ERROR_INVAL);

    // 3.6 Unclosed Container
    const char* unclosed = "[1, 2";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, unclosed, (int)strlen(unclosed), t, 10) == JSTOK_ERROR_PART);

    return 1;
}

int test_syntax_errors_extended(void) {
    jstok_parser p;

    jstoktok_t t[10];

    // Bad object forms

    const char* bad_obj_1 = "{\"a\"}";

    jstok_init(&p);

    ASSERT(jstok_parse(&p, bad_obj_1, (int)strlen(bad_obj_1), t, 10) == JSTOK_ERROR_INVAL);

    ASSERT(p.error_pos == 4);  // Error at '}'

    const char* bad_obj_2 = "{\"a\":}";

    jstok_init(&p);

    ASSERT(jstok_parse(&p, bad_obj_2, (int)strlen(bad_obj_2), t, 10) == JSTOK_ERROR_INVAL);

    ASSERT(p.error_pos == 5);  // Error at '}'

    const char* bad_obj_3 = "{,\"a\":1}";

    jstok_init(&p);

    ASSERT(jstok_parse(&p, bad_obj_3, (int)strlen(bad_obj_3), t, 10) == JSTOK_ERROR_INVAL);

    ASSERT(p.error_pos == 1);  // Error at ','

    const char* bad_obj_4 = "{\"a\":1 \"b\":2}";  // missing comma

    jstok_init(&p);

    ASSERT(jstok_parse(&p, bad_obj_4, (int)strlen(bad_obj_4), t, 10) == JSTOK_ERROR_INVAL);

    ASSERT(p.error_pos == 7);  // Error at '"' of "b"

    // Bad array forms

    const char* bad_arr_1 = "[,1]";

    jstok_init(&p);

    ASSERT(jstok_parse(&p, bad_arr_1, (int)strlen(bad_arr_1), t, 10) == JSTOK_ERROR_INVAL);

    ASSERT(p.error_pos == 1);

    return 1;
}

#ifndef JSTOK_NO_HELPERS

int test_unescape_unicode(void) {
    jstok_parser p;

    jstoktok_t t[10];

    // A: U+0041, Snowman: U+2603, Euro: U+20AC, Custom: U+07FF (edge of 2-byte)

    const char* json = "[\"\\u0041\", \"\\u2603\", \"\\u20AC\", \"\\u07FF\"]";

    jstok_init(&p);

    int count = jstok_parse(&p, json, (int)strlen(json), t, 10);

    ASSERT(count == 5);

    char buf[16];

    size_t len;

    // 1-byte UTF-8 (ASCII)

    ASSERT(jstok_unescape(json, &t[1], buf, sizeof(buf), &len) == 0);

    ASSERT(len == 1 && buf[0] == 'A');

    // 3-byte UTF-8 (Snowman)

    ASSERT(jstok_unescape(json, &t[2], buf, sizeof(buf), &len) == 0);

    ASSERT(len == 3);

    ASSERT((unsigned char)buf[0] == 0xE2 && (unsigned char)buf[1] == 0x98 && (unsigned char)buf[2] == 0x83);

    // 2-byte UTF-8 (U+07FF)

    ASSERT(jstok_unescape(json, &t[4], buf, sizeof(buf), &len) == 0);

    ASSERT(len == 2);

    ASSERT((unsigned char)buf[0] == 0xDF && (unsigned char)buf[1] == 0xBF);

    return 1;
}

#endif

int test_string_errors(void) {
    jstok_parser p;
    jstoktok_t t[10];

    // Control chars
    const char* bad_ctrl = "\"line\nbreak\"";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, bad_ctrl, (int)strlen(bad_ctrl), t, 10) == JSTOK_ERROR_INVAL);

    // Bad escapes
    const char* bad_esc_1 = "\"\\q\"";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, bad_esc_1, (int)strlen(bad_esc_1), t, 10) == JSTOK_ERROR_INVAL);

    const char* bad_esc_2 = "\"\\u12X4\"";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, bad_esc_2, (int)strlen(bad_esc_2), t, 10) == JSTOK_ERROR_INVAL);

    // Partial strings
    const char* partial_1 = "\"abc";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, partial_1, (int)strlen(partial_1), t, 10) == JSTOK_ERROR_PART);
    // Should rewind to start of token (opening quote)
    ASSERT(p.pos == 0);

    const char* partial_2 = "\"a\\";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, partial_2, (int)strlen(partial_2), t, 10) == JSTOK_ERROR_PART);
    ASSERT(p.pos == 0);

    const char* partial_3 = "\"a\\u12";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, partial_3, (int)strlen(partial_3), t, 10) == JSTOK_ERROR_PART);
    ASSERT(p.pos == 0);

    return 1;
}

int test_number_errors(void) {
    jstok_parser p;
    jstoktok_t t[10];

    // Invalid numbers
    const char* bad_num_1 = "-";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, bad_num_1, (int)strlen(bad_num_1), t, 10) == JSTOK_ERROR_PART);

    const char* bad_num_2 = "1.";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, bad_num_2, (int)strlen(bad_num_2), t, 10) == JSTOK_ERROR_PART);

    const char* bad_num_3 = "1e";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, bad_num_3, (int)strlen(bad_num_3), t, 10) == JSTOK_ERROR_PART);

    const char* bad_num_4 = "1a";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, bad_num_4, (int)strlen(bad_num_4), t, 10) == JSTOK_ERROR_INVAL);

    // Delimiter enforcement
    const char* ok_delim_1 = "[123]";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, ok_delim_1, (int)strlen(ok_delim_1), t, 10) == 2);  // [123] -> Array, 123 -> 2 tokens

    const char* bad_delim = "[123x]";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, bad_delim, (int)strlen(bad_delim), t, 10) == JSTOK_ERROR_INVAL);

#ifdef JSTOK_STRICT
    // Leading zeros
    const char* bad_lead = "01";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, bad_lead, (int)strlen(bad_lead), t, 10) == JSTOK_ERROR_INVAL);
#endif

    return 1;
}

// 3.7 Multiple Root Values
// Note: This requires JSTOK_STRICT to be effective for strictly one value.
// But standard parser stops after one top-level value if it's not EOF?
// Let's check behavior. jstok allows trailing whitespace, but not garbage.
int test_syntax_multiroot(void) {
    jstok_parser p;
    jstoktok_t t[10];
    const char* multi = "{} []";

    jstok_init(&p);
    // Standard implementation might parse {} and stop, returning count.
    // However, if we feed the whole string, it might error if it encounters junk.
    // jstok implementation returns count when root is done.
    // BUT strict mode checks for trailing garbage?
    // Looking at jstok.h: "In strict mode, require exactly one top-level value"
    // "root_done already ensures single value, scan ensures no extra tokens were accepted"
    // Actually, jstok loops while (p->pos < json_len). If it finishes root,
    // it will try to parse '[' and see `fr` is null (empty stack).
    // Let's see `jstok_start_container`: `fr = jstok_top(p)`. If !fr and root_done -> ERROR.

    int r = jstok_parse(&p, multi, (int)strlen(multi), t, 10);
    // Should fail because it sees '[' after first object is done
#ifdef JSTOK_STRICT
    ASSERT(r == JSTOK_ERROR_INVAL || r == JSTOK_ERROR_PART);
#else
    // Without strict, jstok allows multiple top-level values
    ASSERT(r > 0);
#endif

    return 1;
}

/* -------------------------------------------------------------------------- */
/* 4. Memory & Buffer Safety */
/* -------------------------------------------------------------------------- */

int test_memory_bounds(void) {
    jstok_parser p;
    jstoktok_t t[3];
    const char* json = "[1, 2, 3]";  // Needs 4 tokens: Array, 1, 2, 3

    // 4.1 Buffer Overflow (Tokens)
    jstok_init(&p);
    ASSERT(jstok_parse(&p, json, (int)strlen(json), t, 3) == JSTOK_ERROR_NOMEM);

    // 4.2 Count-Only Mode
    jstok_init(&p);
    int count = jstok_parse(&p, json, (int)strlen(json), NULL, 0);
    ASSERT(count == 4);

    // 4.3 Partial String
    const char* partial = "\"uncaptured";
    jstok_init(&p);
    ASSERT(jstok_parse(&p, partial, (int)strlen(partial), t, 3) == JSTOK_ERROR_PART);

    return 1;
}

int test_count_only_correctness(void) {
    jstok_parser p;
    jstoktok_t t[100];
    const char* json = "{\"a\": [1, 2], \"b\": {\"c\": true}, \"d\": null}";

    // Normal parse
    jstok_init(&p);
    int count_normal = jstok_parse(&p, json, (int)strlen(json), t, 100);
    ASSERT(count_normal > 0);

    // Count-only parse
    jstok_init(&p);
    int count_only = jstok_parse(&p, json, (int)strlen(json), NULL, 0);

    ASSERT_EQ(count_only, count_normal);

    return 1;
}

/* -------------------------------------------------------------------------- */

/* 5. Helper Functions */

/* -------------------------------------------------------------------------- */

#ifndef JSTOK_NO_HELPERS

int test_helpers_edge_cases(void) {
    jstok_parser p;

    jstoktok_t t[20];

    const char* json = "{\"a\": [1], \"b\": true}";

    jstok_init(&p);

    int count = jstok_parse(&p, json, (int)strlen(json), t, 20);

    ASSERT(count > 0);

    // B1: jstok_span invalid

    jstok_span_t s = jstok_span(NULL, &t[0]);

    ASSERT(s.p == NULL && s.n == 0);

    s = jstok_span(json, NULL);

    ASSERT(s.p == NULL && s.n == 0);

    // B2: jstok_eq invalid

    ASSERT(jstok_eq(NULL, &t[0], "a") == 0);

    ASSERT(jstok_eq(json, NULL, "a") == 0);

    ASSERT(jstok_eq(json, &t[0], NULL) == 0);

    // B3: jstok_skip

    int arr_idx = jstok_object_get(json, t, count, 0, "a");

    // skip array (idx, 1) -> should land on "b" key

    int after_arr = jstok_skip(t, count, arr_idx);

    ASSERT(after_arr < count);

    ASSERT(jstok_eq(json, &t[after_arr], "b"));

    // B4: jstok_array_at

    ASSERT(jstok_array_at(t, count, arr_idx, 1) == -1);  // OOB

    ASSERT(jstok_array_at(t, count, 0, 0) == -1);  // Not an array (is object)

    // B5: jstok_object_get

    ASSERT(jstok_object_get(json, t, count, 0, "z") == -1);  // Missing

    ASSERT(jstok_object_get(json, t, count, arr_idx, "x") == -1);  // Not an object

    return 1;
}

int test_helpers_extended(void) {
    jstok_parser p;

    jstoktok_t t[20];

    const char* json = "{\"users\": [{\"id\": 10, \"name\": \"bob\"}], \"esc\": \"line\\nbreak\"}";

    jstok_init(&p);

    int count = jstok_parse(&p, json, (int)strlen(json), t, 20);

    ASSERT(count > 0);

    // 5.1 jstok_unescape

    // Find "esc" key

    int esc_val_idx = jstok_object_get(json, t, count, 0, "esc");

    ASSERT(esc_val_idx > 0);

    char buf[32];

    size_t len;

    ASSERT(jstok_unescape(json, &t[esc_val_idx], buf, sizeof(buf), &len) == 0);

    buf[len] = 0;

    ASSERT(strcmp(buf, "line\nbreak") == 0);

    // 5.2 jstok_object_get

    ASSERT(jstok_object_get(json, t, count, 0, "users") > 0);

    ASSERT(jstok_object_get(json, t, count, 0, "missing") == -1);

    // 5.3 jstok_array_at

    int users_arr = jstok_object_get(json, t, count, 0, "users");

    ASSERT(jstok_array_at(t, count, users_arr, 0) > 0);

    ASSERT(jstok_array_at(t, count, users_arr, 1) == -1);  // Only 1 element

    // 5.4 jstok_path

    // users -> 0 -> id

    int id_idx = jstok_path(json, t, count, 0, "users", 0, "id", NULL);

    ASSERT(id_idx > 0);

    long long id_val;

    ASSERT(jstok_atoi64(json, &t[id_idx], &id_val) == 0);

    ASSERT(id_val == 10);

    // 5.5 jstok_skip

    // users array is at index `users_arr`

    // Next sibling of users key ("users") is the array value

    // users key is at users_arr - 1

    int users_key = users_arr - 1;

    int after_key = jstok_skip(t, count, users_key);

    ASSERT(after_key == users_arr);  // Skip string key -> value

    int after_arr = jstok_skip(t, count, users_arr);

    // Should point to "esc" key

    ASSERT(t[after_arr].type == JSTOK_STRING);

    ASSERT(jstok_eq(json, &t[after_arr], "esc"));

    return 1;
}

/* -------------------------------------------------------------------------- */

/* 6. SSE Stream Parser */

/* -------------------------------------------------------------------------- */

int test_sse_extended(void) {
    jstok_span_t span;

    size_t pos = 0;

    // 6.1 Standard Line

    const char* s1 = "data: {\"foo\":\"bar\"}\n\n";

    ASSERT(jstok_sse_next(s1, strlen(s1), &pos, &span) == JSTOK_SSE_DATA);

    ASSERT(strncmp(span.p, "{\"foo\":\"bar\"}", span.n) == 0);

    // 6.2 CRLF Line Endings

    const char* s2 = "data: payload\r\n\r\n";

    pos = 0;

    ASSERT(jstok_sse_next(s2, strlen(s2), &pos, &span) == JSTOK_SSE_DATA);

    ASSERT(strncmp(span.p, "payload", span.n) == 0);

    // 6.3 Keep-Alive Comments

    const char* s3 = ": comment\n\ndata: real\n\n";

    pos = 0;

    ASSERT(jstok_sse_next(s3, strlen(s3), &pos, &span) == JSTOK_SSE_DATA);

    ASSERT(strncmp(span.p, "real", span.n) == 0);

    // 6.4 Partial Data

    const char* s4 = "data: incompl";

    pos = 0;

    ASSERT(jstok_sse_next(s4, strlen(s4), &pos, &span) == JSTOK_SSE_NEED_MORE);

    // 6.5 Empty Data

    const char* s5 = "data: \n\n";

    pos = 0;

    ASSERT(jstok_sse_next(s5, strlen(s5), &pos, &span) == JSTOK_SSE_DATA);

    ASSERT(span.n == 0);

    // 6.6 Event/Id Lines (Ignored, finds next data)

    const char* s6 = "event: update\ndata: payload\n\n";

    pos = 0;

    ASSERT(jstok_sse_next(s6, strlen(s6), &pos, &span) == JSTOK_SSE_DATA);

    ASSERT(strncmp(span.p, "payload", span.n) == 0);

    return 1;
}

#endif /* JSTOK_NO_HELPERS */

/* -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- */

/* 7. Fuzzing Scenarios */

/* -------------------------------------------------------------------------- */

int test_fuzzing_scenarios(void) {
    jstok_parser p;

    jstoktok_t t[100];

    char garbage[1024];

    int i;

    // 7.1 Garbage Data

    // Fill with deterministic random bytes (avoiding null terminator for simplicity of strlen,

    // though jstok handles binary if len is passed)

    srand(42);

    for (i = 0; i < 1023; i++) {
        garbage[i] = (char)((rand() % 254) + 1);
    }

    garbage[1023] = '\0';

    jstok_init(&p);

    int r = jstok_parse(&p, garbage, 1023, t, 100);

    // Should return error, not crash

    ASSERT(r < 0);

    // 7.2 Deeply recursive structures (Stack overflow check)

    // 1000 nested arrays

    int deep_len = 1000;

    char* deep = (char*)malloc(deep_len + 1);

    if (!deep) return 0;  // Should not happen in test env

    memset(deep, '[', deep_len);

    deep[deep_len] = '\0';

    jstok_init(&p);

    r = jstok_parse(&p, deep, deep_len, t, 100);

    free(deep);

    ASSERT(r == JSTOK_ERROR_DEPTH);

    return 1;
}

/* -------------------------------------------------------------------------- */

/* 8. Async / Incremental Parsing */

/* -------------------------------------------------------------------------- */

int test_async_chunked(void) {
    // We will parse this JSON in chunks

    const char* full_json = "{\"async\": \"working\", \"num\": 1234, \"arr\": [1, 2]}";

    size_t total_len = strlen(full_json);

    jstok_parser p;

    jstoktok_t t[32];

    jstok_init(&p);

    // Chunk 1: "{\"asy" (Split inside string key)

    int chunk1_len = 6;

    int r = jstok_parse(&p, full_json, chunk1_len, t, 32);

    ASSERT(r == JSTOK_ERROR_PART);

    // Chunk 2: "{\"async\": \"work" (Split inside string value)

    int chunk2_len = 16;

    r = jstok_parse(&p, full_json, chunk2_len, t, 32);

    ASSERT(r == JSTOK_ERROR_PART);

    // Chunk 3: "{\"async\": \"working\", \"num\": 12" (Split inside number)

    int chunk3_len = 31;

    r = jstok_parse(&p, full_json, chunk3_len, t, 32);

    ASSERT_EQ(r, JSTOK_ERROR_PART);

    // Chunk 4: Full JSON

    r = jstok_parse(&p, full_json, (int)total_len, t, 32);

    ASSERT(r > 0);

    ASSERT(t[0].type == JSTOK_OBJECT);

#ifndef JSTOK_NO_HELPERS
    // Verify contents
    int val_idx = jstok_object_get(full_json, t, r, 0, "async");
    ASSERT(val_idx > 0);
    ASSERT(jstok_eq(full_json, &t[val_idx], "working"));

    int num_idx = jstok_object_get(full_json, t, r, 0, "num");
    ASSERT(num_idx > 0);
    long long n;
    ASSERT(jstok_atoi64(full_json, &t[num_idx], &n) == 0);
    ASSERT(n == 1234);  // If number splitting failed, this might be 12 or 123
#endif

    return 1;
}

int main(void) {
    printf("Starting jstok comprehensive tests...\n");

    TEST(core_empty);

    TEST(core_literals);

    TEST(core_numbers);

    TEST(nesting_structure);

    TEST(nesting_depth);

    TEST(syntax_errors);

    TEST(syntax_errors_extended);
    TEST(string_errors);
    TEST(number_errors);

    TEST(syntax_multiroot);

    TEST(memory_bounds);
    TEST(count_only_correctness);

#ifndef JSTOK_NO_HELPERS
    TEST(helpers_edge_cases);
    TEST(helpers_extended);
    TEST(unescape_unicode);

    TEST(sse_extended);
#endif

    TEST(fuzzing_scenarios);

    TEST(async_chunked);

    printf("\nTests run: %d, Failed: %d\n", tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
