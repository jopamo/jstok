// test_jstok_sse_next.c
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "jstok.h"

static void expect_need_more(const char* buf, size_t len, size_t pos_in, size_t pos_out) {
    jstok_span_t sp = {0};
    size_t pos = pos_in;
    jstok_sse_res r = jstok_sse_next(buf, len, &pos, &sp);
    assert(r == JSTOK_SSE_NEED_MORE);
    assert(pos == pos_out);
}

static void expect_data(const char* buf, size_t len, size_t pos_in, size_t pos_out, const char* want) {
    jstok_span_t sp = {0};
    size_t pos = pos_in;
    jstok_sse_res r = jstok_sse_next(buf, len, &pos, &sp);
    assert(r == JSTOK_SSE_DATA);
    assert(pos == pos_out);

    size_t want_len = strlen(want);
    assert(sp.n == want_len);
    assert(memcmp(sp.p, want, want_len) == 0);
}

static void test_empty_buffer_need_more(void) {
    const char* s = "";
    expect_need_more(s, 0, 0, 0);
}

static void test_pos_clamped_to_len(void) {
    const char* s = "data: x\n";
    size_t len = strlen(s);
    size_t pos = len + 123;
    jstok_span_t sp = {0};
    jstok_sse_res r = jstok_sse_next(s, len, &pos, &sp);
    assert(r == JSTOK_SSE_NEED_MORE);
    assert(pos == len);
}

static void test_basic_data_line(void) {
    const char* s = "data: hello\n";
    expect_data(s, strlen(s), 0, strlen(s), "hello");
}

static void test_data_no_space(void) {
    const char* s = "data:hello\n";
    expect_data(s, strlen(s), 0, strlen(s), "hello");
}

static void test_data_empty_payload(void) {
    const char* s = "data:\n";
    expect_data(s, strlen(s), 0, strlen(s), "");
}

static void test_crlf(void) {
    const char* s = "data: hi\r\n";
    expect_data(s, strlen(s), 0, strlen(s), "hi");
}

static void test_skip_non_data_fields(void) {
    const char* s =
        "event: message\n"
        "id: 10\n"
        "retry: 1000\n"
        "data: ok\n";
    expect_data(s, strlen(s), 0, strlen(s), "ok");
}

static void test_skip_comments_and_blanks(void) {
    const char* s =
        ":\n"
        ": keepalive\n"
        "\n"
        "data: yep\n";
    expect_data(s, strlen(s), 0, strlen(s), "yep");
}

static void test_multiple_data_lines_returns_in_order(void) {
    const char* s =
        "data: one\n"
        "data: two\n";

    size_t pos = 0;
    jstok_span_t sp = {0};

    assert(jstok_sse_next(s, strlen(s), &pos, &sp) == JSTOK_SSE_DATA);
    assert(sp.n == 3 && memcmp(sp.p, "one", 3) == 0);

    assert(jstok_sse_next(s, strlen(s), &pos, &sp) == JSTOK_SSE_DATA);
    assert(sp.n == 3 && memcmp(sp.p, "two", 3) == 0);

    assert(jstok_sse_next(s, strlen(s), &pos, &sp) == JSTOK_SSE_NEED_MORE);
}

static void test_fragmentation_mid_line_sets_pos_to_line_start(void) {
    const char* s =
        "event: x\n"
        "data: he";
    size_t line_start = strlen("event: x\n");
    expect_need_more(s, strlen(s), 0, line_start);
}

static void test_fragmentation_mid_prefix_sets_pos_to_line_start(void) {
    const char* s = "da";
    expect_need_more(s, strlen(s), 0, 0);
}

static void test_non_matching_leading_space_ignored(void) {
    const char* s =
        " data: nope\n"
        "data: yep\n";
    expect_data(s, strlen(s), 0, strlen(s), "yep");
}

static void test_comment_then_partial_line_resume_point(void) {
    const char* s =
        ": keepalive\n"
        "da";
    size_t line_start = strlen(": keepalive\n");
    expect_need_more(s, strlen(s), 0, line_start);
}

int main(void) {
    test_empty_buffer_need_more();
    test_pos_clamped_to_len();
    test_basic_data_line();
    test_data_no_space();
    test_data_empty_payload();
    test_crlf();
    test_skip_non_data_fields();
    test_skip_comments_and_blanks();
    test_multiple_data_lines_returns_in_order();
    test_fragmentation_mid_line_sets_pos_to_line_start();
    test_fragmentation_mid_prefix_sets_pos_to_line_start();
    test_non_matching_leading_space_ignored();
    test_comment_then_partial_line_resume_point();

    fprintf(stderr, "ok: jstok_sse_next tests passed\n");
    return 0;
}
