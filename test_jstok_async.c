#define JSTOK_STRICT
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
/* Async / Incremental JSON Parsing Tests */
/* -------------------------------------------------------------------------- */

/*
 * Test 1: Byte-by-byte incremental parsing
 * We feed the buffer one byte at a time (increasing the length view)
 * and ensure the parser state persists and eventually completes.
 */
int test_async_byte_by_byte(void) {
    const char* json = "{\"key\": \"value\", \"list\": [1, 2, 3], \"nested\": {\"a\": true}}";
    size_t total_len = strlen(json);

    jstok_parser p;
    jstoktok_t tokens[50];

    jstok_init(&p);

    int result = 0;
    int completed = 0;

    // Simulate growing buffer view
    for (size_t len = 1; len <= total_len; len++) {
        // We pass the full pointer but limit the length
        result = jstok_parse(&p, json, (int)len, tokens, 50);

        if (result >= 0) {
            completed = 1;
            // Should only complete at the very end for this strict JSON
            // But actually, it completes when the root object is closed.
            // Since our JSON is one object, it should match total_len (or close to it)
            if (len < total_len) {
                // If it claims completion early, ensure it's actually valid
                // (e.g. if we had "{} garbage", it might finish at })
            }
            break;
        } else {
            ASSERT_EQ(result, JSTOK_ERROR_PART);
        }
    }

    ASSERT(completed);
    ASSERT(result > 0);

    // Verify structure
    int key_idx = jstok_object_get(json, tokens, result, 0, "key");
    ASSERT(key_idx > 0);
    ASSERT(jstok_eq(json, &tokens[key_idx], "value"));

    int list_idx = jstok_object_get(json, tokens, result, 0, "list");
    ASSERT(list_idx > 0);
    ASSERT(tokens[list_idx].size == 3);

    return 1;
}

/*
 * Test 2: Random chunk sizes
 * Simulate network packets of random sizes.
 */
int test_async_random_chunks(void) {
    const char* json =
        "["
        "  {\"id\": 1, \"text\": \"chunk1\"},"
        "  {\"id\": 2, \"text\": \"chunk2\"},"
        "  {\"id\": 3, \"text\": \"chunk3\"},"
        "  {\"id\": 4, \"text\": \"chunk4\"}"
        "]";
    size_t total_len = strlen(json);

    jstok_parser p;
    jstoktok_t tokens[100];

    srand((unsigned int)time(NULL));

    // Try multiple runs with different random seeds implicity
    for (int run = 0; run < 10; run++) {
        jstok_init(&p);
        size_t current_len = 0;
        int result = JSTOK_ERROR_PART;

        while (current_len < total_len) {
            // Add random chunk 1-10 bytes
            int chunk = (rand() % 10) + 1;
            current_len += chunk;
            if (current_len > total_len) current_len = total_len;

            result = jstok_parse(&p, json, (int)current_len, tokens, 100);

            if (result >= 0) break;
            ASSERT_EQ(result, JSTOK_ERROR_PART);
        }

        ASSERT(result > 0);
        ASSERT(tokens[0].type == JSTOK_ARRAY);
        ASSERT(tokens[0].size == 4);
    }

    return 1;
}

/*
 * Test 3: Restarting from JSTOK_ERROR_PART
 * Specifically target split tokens (strings, numbers, keywords)
 */
int test_async_split_tokens(void) {
    const char* json = "{\"bool\": true, \"num\": -123.45, \"str\": \"escaped \\\" quote\"}";
    size_t total_len = strlen(json);

    jstok_parser p;
    jstoktok_t tokens[32];

    // Try stopping at EVERY position
    for (size_t split = 1; split < total_len; split++) {
        jstok_init(&p);

        // Pass 1: Partial
        int r1 = jstok_parse(&p, json, (int)split, tokens, 32);

        // If it finished early, that's weird for this single-object JSON unless split includes closing }
        if (r1 >= 0) {
            // It should only succeed if split covers the whole valid object
            // For this string, it really needs the last char '}'
            if (json[split - 1] != '}') {
                // It might have parsed "true" but we are inside an object...
                // The parser shouldn't return success until the root object is closed.
                printf("Unexpected success at split %zu / %zu\n", split, total_len);
                return 0;
            }
        } else {
            ASSERT_EQ(r1, JSTOK_ERROR_PART);

            // Pass 2: Complete
            // The parser should resume from where it left off (p.pos)
            // or rewind if it was in the middle of a token.
            int r2 = jstok_parse(&p, json, (int)total_len, tokens, 32);
            ASSERT(r2 > 0);

            // verification
            int idx = jstok_object_get(json, tokens, r2, 0, "bool");
            ASSERT(idx > 0);
            int b_val;
            jstok_atob(json, &tokens[idx], &b_val);
            ASSERT(b_val == 1);
        }
    }
    return 1;
}

/* -------------------------------------------------------------------------- */
/* SSE Async Tests */
/* -------------------------------------------------------------------------- */

/*
 * Test 4: SSE Stream fragmentation
 * jstok_sse_next maintains an external 'pos' cursor.
 * We must ensure it correctly handles "data:" spanning across buffer boundaries
 * if we were implementing a ring buffer, but here the API expects a contiguous buffer.
 * The test here simulates: buffer grows, we call sse_next again with updated len.
 */
int test_sse_fragmentation(void) {
    const char* stream =
        "data: first\n\n"
        "data: second\n\n"
        "event: ping\n"
        "data: third\n\n";
    size_t total_len = strlen(stream);

    size_t pos = 0;  // The parser cursor
    int events_found = 0;
    jstok_span_t payload;

    // We will advance the "available buffer" (limit) slowly
    // but the 'stream' pointer remains constant (simulating memory mapped file or growing buffer)

    size_t limit = 0;

    // While we haven't processed everything
    while (events_found < 3) {
        // Grow buffer by small random amount
        limit += (rand() % 5) + 1;
        if (limit > total_len) limit = total_len;

        // Try to parse next event
        // We loop because a single buffer expansion might reveal multiple events
        while (jstok_sse_next(stream, limit, &pos, &payload) == JSTOK_SSE_DATA) {
            events_found++;
            if (events_found == 1) {
                ASSERT(strncmp(payload.p, "first", payload.n) == 0);
            } else if (events_found == 2) {
                ASSERT(strncmp(payload.p, "second", payload.n) == 0);
            } else if (events_found == 3) {
                ASSERT(strncmp(payload.p, "third", payload.n) == 0);
            }
        }

        if (limit == total_len && events_found < 3) {
            // Should not happen if logic is correct
            printf("Stuck at limit=%zu, events=%d\n", limit, events_found);
            return 0;
        }
    }

    return 1;
}

int main(void) {
    printf("Starting jstok async tests...\n");

    TEST(async_byte_by_byte);
    TEST(async_random_chunks);
    TEST(async_split_tokens);
    TEST(sse_fragmentation);

    printf("\nTests run: %d, Failed: %d\n", tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
