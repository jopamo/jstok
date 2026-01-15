#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Include implementation for the fuzzer to compile everything in one go */
#include "jstok.h"

/* Helper to sanity check pointers returned by SSE/Span */
static void check_bounds(const char* base, int size, const char* ptr, size_t len) {
    if (ptr == NULL && len == 0) return;
    if (ptr < base || ptr + len > base + size) {
        /* This detects if the parser points outside the input buffer */
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    /* Cap size to INT_MAX for int-based APIs */
    if (Size > 2147483647) Size = 2147483647;

    const char* json_data = (const char*)Data;
    int json_len = (int)Size;

    /* ----------------------------------------------------------------------
     * 1. Fuzz SSE Parser (Independent of JSON validity)
     * ---------------------------------------------------------------------- */
    {
        size_t pos = 0;
        jstok_span_t span;
        int safety_loop = 0;

        /* Scan the entire buffer as if it were an event stream */
        while (jstok_sse_next(json_data, Size, &pos, &span) == JSTOK_SSE_DATA) {
            /* 1. Validate pointer bounds */
            check_bounds(json_data, json_len, span.p, span.n);

            /* 2. Prevent infinite loops if logic is broken */
            safety_loop++;
            if (safety_loop > json_len + 10) abort();
        }
    }

    /* ----------------------------------------------------------------------
     * 2. Fuzz JSON Parser
     * ---------------------------------------------------------------------- */
    jstok_parser p;
    jstoktok_t tokens[4096]; /* Larger buffer for deeper nesting fuzzing */

    jstok_init(&p);
    int count = jstok_parse(&p, json_data, json_len, tokens, 4096);

    /* ----------------------------------------------------------------------
     * 3. Count-Only Mode Consistency Check
     * ---------------------------------------------------------------------- */
    /* If the parse was successful (or partial), running it in count-only mode
       (tokens == NULL) should return the exact same number of tokens. */
    if (count != JSTOK_ERROR_NOMEM) {
        jstok_parser p_count;
        jstok_init(&p_count);
        int count_check = jstok_parse(&p_count, json_data, json_len, NULL, 4096);

        /* The counts must match exactly if the first parse succeeded completely */
        if (count >= 0) {
            assert(count == count_check);
        }
    }

    /* ----------------------------------------------------------------------
     * 4. Exercise Helpers on Valid/Partial Parse
     * ---------------------------------------------------------------------- */
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            /* Sanity check token boundaries */
            if (tokens[i].start < 0 || tokens[i].end > json_len) abort();
            if (tokens[i].start > tokens[i].end) abort();

            /* Test: Span */
            jstok_span_t s = jstok_span(json_data, &tokens[i]);
            check_bounds(json_data, json_len, s.p, s.n);

            /* Test: Unescape (for strings) */
            if (tokens[i].type == JSTOK_STRING) {
                char buf[256]; /* Small buffer to test capacity checks */
                size_t out_len = 0;
                jstok_unescape(json_data, &tokens[i], buf, sizeof(buf), &out_len);

                /* Test: Equality */
                jstok_eq(json_data, &tokens[i], "choices");
            }

            /* Test: Primitives */
            if (tokens[i].type == JSTOK_PRIMITIVE) {
                long long val_i;
                int val_b;
                jstok_atoi64(json_data, &tokens[i], &val_i);
                jstok_atob(json_data, &tokens[i], &val_b);
            }

            /* Test: Array Access */
            if (tokens[i].type == JSTOK_ARRAY) {
                /* Valid index */
                if (tokens[i].size > 0) {
                    jstok_array_at(tokens, count, i, 0);
                    jstok_array_at(tokens, count, i, tokens[i].size - 1);
                }
                /* Out of bounds index */
                jstok_array_at(tokens, count, i, tokens[i].size);
            }

            /* Test: Object Access */
            if (tokens[i].type == JSTOK_OBJECT) {
                /* Try to find common keys */
                jstok_object_get(json_data, tokens, count, i, "id");
                jstok_object_get(json_data, tokens, count, i, "choices");
                /* Try to find a key that likely doesn't exist */
                jstok_object_get(json_data, tokens, count, i, "xyz_fuzz_missing");
            }

            /* Test: Skip */
            int next = jstok_skip(tokens, count, i);
            if (next < i || next > count) abort(); /* Should never go backwards or way past end */
        }

        /* ------------------------------------------------------------------
         * 5. Exercise jstok_path (Variadic)
         * ------------------------------------------------------------------ */
        /* Valid path structure: Root -> "choices" (0) -> "message" */
        jstok_path(json_data, tokens, count, 0, "choices", 0, "message", NULL);

        /* Type mismatch test: Try treating root as array (index 0) then object */
        jstok_path(json_data, tokens, count, 0, 0, "content", NULL);

        /* Deep nesting test: Attempt deep access to see if it handles bounds gracefully */
        jstok_path(json_data, tokens, count, 0, "a", "b", "c", "d", 0, 1, 2, NULL);
    }

    return 0;
}
