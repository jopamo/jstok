/*
 * jstok - single-header JSON tokenizer + helpers
 *
 * Goals
 *   - Zero-allocation tokenization (tokens slice the original input)
 *   - Strict JSON grammar (optional, recommended)
 *   - Useful token semantics (object.size = pair count, array.size = element count)
 *   - Count-only mode (tokens == NULL)
 *   - Incremental-friendly (if you call again with the same buffer + more bytes)
 *   - Tiny helpers for navigating tokens (object get, array at, subtree skip)
 *
 * Usage
 *   // Decls only in most TUs
 *   #define JSTOK_HEADER
 *   #include "jstok.h"
 *
 *   // In exactly one TU
 *   #include "jstok.h"
 *
 * Config macros
 *   JSTOK_STATIC             make functions static for embedding
 *   JSTOK_PARENT_LINKS       add token.parent
 *   JSTOK_MAX_DEPTH          nesting depth (default 64)
 *   JSTOK_STRICT             enforce strict JSON (no trailing commas, single top-level value, strict numbers)
 *   JSTOK_NO_HELPERS         omit helper API
 *
 * Token boundaries
 *   - start/end are byte offsets into the original json buffer
 *   - end is exclusive, so slice is json[start:end]
 *   - string tokens exclude quotes: start is after ", end is at closing " (exclusive)
 *   - container tokens include delimiters: start at {/[ , end after }/]
 *
 * Return values
 *   >= 0: number of tokens used (or required if tokens == NULL)
 *   JSTOK_ERROR_*: negative error code, parser.error_pos set when possible
 *
 * This software is distributed under the MIT license
 */

#ifndef JSTOK_H
#define JSTOK_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef JSTOK_MAX_DEPTH
#define JSTOK_MAX_DEPTH 64
#endif

#ifdef JSTOK_STATIC
#define JSTOK_API static
#else
#define JSTOK_API extern
#endif

typedef enum {
    JSTOK_UNDEFINED = 0,
    JSTOK_OBJECT = 1u << 0,
    JSTOK_ARRAY = 1u << 1,
    JSTOK_STRING = 1u << 2,
    JSTOK_PRIMITIVE = 1u << 3
} jstoktype_t;

typedef enum {
    JSTOK_ERROR_NOMEM = -1,
    JSTOK_ERROR_INVAL = -2,
    JSTOK_ERROR_PART = -3,
    JSTOK_ERROR_DEPTH = -4
} jstokerr_t;

typedef struct jstoktok {
    jstoktype_t type;
    int start;
    int end;  /* exclusive */
    int size; /* object: pair count, array: element count, others: 0 */
#ifdef JSTOK_PARENT_LINKS
    int parent;
#endif
} jstoktok_t;

/* Parsing states per container frame */
typedef enum {
    /* Object states */
    JSTOK_ST_OBJ_KEY_OR_END = 1,
    JSTOK_ST_OBJ_KEY,
    JSTOK_ST_OBJ_COLON,
    JSTOK_ST_OBJ_VALUE,
    JSTOK_ST_OBJ_COMMA_OR_END,

    /* Array states */
    JSTOK_ST_ARR_VALUE_OR_END,
    JSTOK_ST_ARR_VALUE,
    JSTOK_ST_ARR_COMMA_OR_END
} jstok_state_t;

typedef struct jstok_frame {
    jstoktype_t type;
    jstok_state_t st;
    int tok; /* token index for this container, or -1 in count-only */
} jstok_frame_t;

typedef struct jstok_parser {
    int pos;       /* current scan position */
    int toknext;   /* next token index / token count in count-only */
    int depth;     /* number of active container frames */
    int root_done; /* parsed one top-level value */

    int error_pos;
    int error_code;

    jstok_frame_t stack[JSTOK_MAX_DEPTH];
} jstok_parser;

JSTOK_API void jstok_init(jstok_parser* p);

typedef enum {
    /* Input chunk is the final end-of-stream segment. */
    JSTOK_PARSE_FINAL = 1u << 0
} jstok_parse_flags_t;

JSTOK_API int jstok_parse_ex(jstok_parser* p, const char* json, int json_len, jstoktok_t* tokens, int max_tokens,
                             unsigned flags);

JSTOK_API int jstok_parse(jstok_parser* p, const char* json, int json_len, jstoktok_t* tokens, int max_tokens);

#ifndef JSTOK_NO_HELPERS

typedef struct jstok_span {
    const char* p;
    size_t n;
} jstok_span_t;

/* Returns json slice for token as {ptr,len} */
JSTOK_API jstok_span_t jstok_span(const char* json, const jstoktok_t* t);

/* Compare token slice with a NUL-terminated string */
JSTOK_API int jstok_eq(const char* json, const jstoktok_t* t, const char* s);

/* Skip token subtree, returns index of next sibling or count on end */
JSTOK_API int jstok_skip(const jstoktok_t* toks, int count, int i);

/* Get array element i (0-based), returns token index or -1 */
JSTOK_API int jstok_array_at(const jstoktok_t* toks, int count, int arr_tok, int idx);

/* Get object value by key, returns value token index or -1 */
JSTOK_API int jstok_object_get(const char* json, const jstoktok_t* toks, int count, int obj_tok, const char* key);

/* Parse primitive token as integer (base 10), returns 0 on success */
JSTOK_API int jstok_atoi64(const char* json, const jstoktok_t* t, long long* out);

/* Parse primitive token as boolean, returns 0 on success */
JSTOK_API int jstok_atob(const char* json, const jstoktok_t* t, int* out);

/* Unescape a JSON string token into user buffer, returns 0 on success */
JSTOK_API int jstok_unescape(const char* json, const jstoktok_t* t, char* out, size_t out_cap, size_t* out_len);

/* * Variadic path helper.
 * Traverses nested structures based on args.
 * - If current node is Object: expects (const char*) key. Pass NULL to stop.
 * - If current node is Array: expects (int) index.
 * * Example: jstok_path(json, toks, count, root, "choices", 0, "message", NULL);
 */
JSTOK_API int jstok_path(const char* json, const jstoktok_t* toks, int count, int root, ...);

typedef enum { JSTOK_SSE_EOF = 0, JSTOK_SSE_DATA = 1, JSTOK_SSE_NEED_MORE = -1 } jstok_sse_res;

/*
 * SSE Line Parser.
 * Scans 'buf' starting at '*pos' for the next "data:" line.
 * - Updates '*pos' to the start of the next line.
 * - Sets 'out' to the span of the payload (excluding "data: " and newline).
 * - Returns JSTOK_SSE_DATA if found, JSTOK_SSE_NEED_MORE if incomplete or EOF.
 */
JSTOK_API jstok_sse_res jstok_sse_next(const char* buf, size_t len, size_t* pos, jstok_span_t* out);

#endif /* JSTOK_NO_HELPERS */

#ifdef __cplusplus
}
#endif

#endif /* JSTOK_H */

/* -------------------------------------------------------------------------- */
/* Implementation                                                             */
/* -------------------------------------------------------------------------- */
#ifndef JSTOK_HEADER

#include <limits.h>
#include <string.h>

/* Minimal helpers, avoid heavy deps */
#define jstok_is_space(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')

enum {
    JSTOK_CC_OTHER = 0,
    JSTOK_CC_SPACE = 1,
    JSTOK_CC_LBRACE = 2,
    JSTOK_CC_RBRACE = 3,
    JSTOK_CC_LBRACKET = 4,
    JSTOK_CC_RBRACKET = 5,
    JSTOK_CC_COLON = 6,
    JSTOK_CC_COMMA = 7,
    JSTOK_CC_QUOTE = 8
};

static const unsigned char jstok_char_class[256] = {
    [' '] = JSTOK_CC_SPACE,
    ['\t'] = JSTOK_CC_SPACE,
    ['\n'] = JSTOK_CC_SPACE,
    ['\r'] = JSTOK_CC_SPACE,
    ['{'] = JSTOK_CC_LBRACE,
    ['}'] = JSTOK_CC_RBRACE,
    ['['] = JSTOK_CC_LBRACKET,
    [']'] = JSTOK_CC_RBRACKET,
    [':'] = JSTOK_CC_COLON,
    [','] = JSTOK_CC_COMMA,
    ['"'] = JSTOK_CC_QUOTE
};

static const unsigned char jstok_digit_class[256] = {
    ['0'] = 1,
    ['1'] = 1,
    ['2'] = 1,
    ['3'] = 1,
    ['4'] = 1,
    ['5'] = 1,
    ['6'] = 1,
    ['7'] = 1,
    ['8'] = 1,
    ['9'] = 1
};

static const unsigned char jstok_hex_class[256] = {
    ['0'] = 1,
    ['1'] = 1,
    ['2'] = 1,
    ['3'] = 1,
    ['4'] = 1,
    ['5'] = 1,
    ['6'] = 1,
    ['7'] = 1,
    ['8'] = 1,
    ['9'] = 1,
    ['a'] = 1,
    ['b'] = 1,
    ['c'] = 1,
    ['d'] = 1,
    ['e'] = 1,
    ['f'] = 1,
    ['A'] = 1,
    ['B'] = 1,
    ['C'] = 1,
    ['D'] = 1,
    ['E'] = 1,
    ['F'] = 1
};

static const unsigned char jstok_delim_class[256] = {
    [' '] = 1,
    ['\t'] = 1,
    ['\n'] = 1,
    ['\r'] = 1,
    [','] = 1,
    [']'] = 1,
    ['}'] = 1
};

#define jstok_classify(c) (jstok_char_class[(unsigned char)(c)])
#define jstok_is_digit(c) (jstok_digit_class[(unsigned char)(c)] != 0u)
#define jstok_is_hex(c) (jstok_hex_class[(unsigned char)(c)] != 0u)
#define jstok_is_delim(c) (jstok_delim_class[(unsigned char)(c)] != 0u)

static void jstok_set_error(jstok_parser* p, int code, int pos) {
    p->error_code = code;
    p->error_pos = pos;
}

JSTOK_API void jstok_init(jstok_parser* p) {
    if (!p) return;
    p->pos = 0;
    p->toknext = 0;
    p->depth = 0;
    p->root_done = 0;
    p->error_pos = -1;
    p->error_code = 0;
}

static int jstok_push(jstok_parser* p, jstoktype_t type, jstok_state_t st, int tok) {
    if (p->depth >= JSTOK_MAX_DEPTH) {
        jstok_set_error(p, JSTOK_ERROR_DEPTH, p->pos);
        return JSTOK_ERROR_DEPTH;
    }
    p->stack[p->depth].type = type;
    p->stack[p->depth].st = st;
    p->stack[p->depth].tok = tok;
    p->depth++;
    return 0;
}

static void jstok_pop(jstok_parser* p) {
    if (p->depth > 0) {
        p->depth--;
    }
}

static jstok_frame_t* jstok_top(jstok_parser* p) {
    if (p->depth <= 0) return (jstok_frame_t*)0;
    return &p->stack[p->depth - 1];
}

static int jstok_new_token(jstok_parser* p, jstoktok_t* toks, int max_tokens, jstoktype_t type, int start, int end,
                           int parent) {
    int idx;

    if (toks) {
        if (p->toknext >= max_tokens) {
            jstok_set_error(p, JSTOK_ERROR_NOMEM, p->pos);
            return JSTOK_ERROR_NOMEM;
        }
        idx = p->toknext++;
        toks[idx].type = type;
        toks[idx].start = start;
        toks[idx].end = end;
        toks[idx].size = 0;
#ifdef JSTOK_PARENT_LINKS
        toks[idx].parent = parent;
#else
        (void)parent;
#endif
        return idx;
    }

    /* Count-only mode */
    idx = p->toknext++;
    (void)type;
    (void)start;
    (void)end;
    (void)parent;
    return idx;
}

static void jstok_inc_container_size(jstok_parser* p, jstoktok_t* toks) {
    jstok_frame_t* fr = jstok_top(p);
    if (!fr) return;
    if (!toks) return;
    if (fr->tok < 0) return;
    toks[fr->tok].size++;
}

static void jstok_rollback_accept_value(jstok_parser* p, jstoktok_t* toks, jstok_frame_t* fr, jstok_state_t saved_st,
                                        int saved_root_done) {
    if (fr) {
        fr->st = saved_st;
        if (toks && fr->tok >= 0 && toks[fr->tok].size > 0) {
            toks[fr->tok].size--;
        }
    } else {
        p->root_done = saved_root_done;
    }
}

static int jstok_accept_value(jstok_parser* p, jstoktok_t* toks) {
    jstok_frame_t* fr = jstok_top(p);

    if (!fr) {
#ifdef JSTOK_STRICT
        if (p->root_done) {
            jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
            return JSTOK_ERROR_INVAL;
        }
#endif
        p->root_done = 1;
        return 0;
    }

    if (fr->type == JSTOK_ARRAY) {
        if (fr->st != JSTOK_ST_ARR_VALUE_OR_END && fr->st != JSTOK_ST_ARR_VALUE) {
            jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
            return JSTOK_ERROR_INVAL;
        }
        jstok_inc_container_size(p, toks);
        fr->st = JSTOK_ST_ARR_COMMA_OR_END;
        return 0;
    }

    if (fr->type == JSTOK_OBJECT) {
        if (fr->st != JSTOK_ST_OBJ_VALUE) {
            jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
            return JSTOK_ERROR_INVAL;
        }
        jstok_inc_container_size(p, toks);
        fr->st = JSTOK_ST_OBJ_COMMA_OR_END;
        return 0;
    }

    jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
    return JSTOK_ERROR_INVAL;
}

static int jstok_accept_key(jstok_parser* p) {
    jstok_frame_t* fr = jstok_top(p);
    if (!fr || fr->type != JSTOK_OBJECT) {
        jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
        return JSTOK_ERROR_INVAL;
    }
    if (fr->st != JSTOK_ST_OBJ_KEY_OR_END && fr->st != JSTOK_ST_OBJ_KEY) {
        jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
        return JSTOK_ERROR_INVAL;
    }
    fr->st = JSTOK_ST_OBJ_COLON;
    return 0;
}

static int jstok_parse_string_token(jstok_parser* p, const char* json, int json_len, jstoktok_t* toks, int max_tokens,
                                    int parent) {
    int start_quote;
    int i;

    start_quote = p->pos;
    if (p->pos >= json_len || json[p->pos] != '"') {
        jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
        return JSTOK_ERROR_INVAL;
    }

    p->pos++; /* after opening quote */

    while (p->pos < json_len) {
        char c;

        while (p->pos < json_len) {
            c = json[p->pos];
            if (c == '"' || c == '\\' || (unsigned char)c < 0x20) break;
            p->pos++;
        }

        if (p->pos >= json_len) break;

        c = json[p->pos];

        if ((unsigned char)c < 0x20) {
            jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
            return JSTOK_ERROR_INVAL;
        }

        if (c == '"') {
            /* end exclusive is at the closing quote position */
            i = jstok_new_token(p, toks, max_tokens, JSTOK_STRING, start_quote + 1, p->pos, parent);
            if (i < 0) return i;
            p->pos++; /* consume closing quote */
            return i;
        }

        if (c == '\\') {
            p->pos++;
            if (p->pos >= json_len) {
                jstok_set_error(p, JSTOK_ERROR_PART, p->pos);
                p->pos = start_quote; /* Rewind for resume */
                return JSTOK_ERROR_PART;
            }
            c = json[p->pos];

            if (c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't') {
                p->pos++;
                continue;
            }

            if (c == 'u') {
                /* \uXXXX */
                for (i = 0; i < 4; i++) {
                    p->pos++;
                    if (p->pos >= json_len) {
                        jstok_set_error(p, JSTOK_ERROR_PART, p->pos);
                        p->pos = start_quote; /* Rewind for resume */
                        return JSTOK_ERROR_PART;
                    }
                    if (!jstok_is_hex(json[p->pos])) {
                        jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
                        return JSTOK_ERROR_INVAL;
                    }
                }
                p->pos++;
                continue;
            }

            jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
            return JSTOK_ERROR_INVAL;
        }

        p->pos++;
    }

    jstok_set_error(p, JSTOK_ERROR_PART, p->pos);
    p->pos = start_quote; /* Rewind for resume */
    return JSTOK_ERROR_PART;
}

static int jstok_parse_literal(jstok_parser* p, const char* json, int json_len, const char* lit, int lit_len,
                               unsigned flags) {
    int i;
    int start = p->pos;
    int avail = json_len - start;

    if (avail < lit_len) {
        jstok_set_error(p, JSTOK_ERROR_PART, json_len);
        p->pos = start; /* Rewind for resume */
        return JSTOK_ERROR_PART;
    }

    if (memcmp(json + start, lit, (size_t)lit_len) != 0) {
        for (i = 0; i < lit_len; i++) {
            if (json[start + i] != lit[i]) {
                jstok_set_error(p, JSTOK_ERROR_INVAL, start + i);
                return JSTOK_ERROR_INVAL;
            }
        }
        jstok_set_error(p, JSTOK_ERROR_INVAL, start);
        return JSTOK_ERROR_INVAL;
    }

    /* Need delimiter, or final EOF when caller confirms end-of-stream. */
    if (start + lit_len >= json_len) {
        if ((flags & JSTOK_PARSE_FINAL) == 0u) {
            jstok_set_error(p, JSTOK_ERROR_PART, start + lit_len);
            p->pos = start; /* Rewind for resume */
            return JSTOK_ERROR_PART;
        }
        return lit_len;
    }
    if (!jstok_is_delim(json[start + lit_len])) {
        jstok_set_error(p, JSTOK_ERROR_INVAL, start + lit_len);
        return JSTOK_ERROR_INVAL;
    }
    return lit_len;
}

static int jstok_parse_number_span(jstok_parser* p, const char* json, int json_len, int* out_end, unsigned flags) {
    int i = p->pos;

    if (i >= json_len) {
        jstok_set_error(p, JSTOK_ERROR_PART, i);
        return JSTOK_ERROR_PART;
    }

    if (json[i] == '-') {
        i++;
        if (i >= json_len) {
            jstok_set_error(p, JSTOK_ERROR_PART, i);
            return JSTOK_ERROR_PART;
        }
    }

    if (json[i] == '0') {
        i++;
        /* no leading zeros in strict JSON, but allow 0.<frac> or 0e... */
        if (i < json_len && jstok_is_digit(json[i])) {
#ifdef JSTOK_STRICT
            jstok_set_error(p, JSTOK_ERROR_INVAL, i);
            return JSTOK_ERROR_INVAL;
#endif
        }
    } else if (json[i] >= '1' && json[i] <= '9') {
        i++;
        while (i < json_len && jstok_is_digit(json[i])) i++;
    } else {
        jstok_set_error(p, JSTOK_ERROR_INVAL, i);
        return JSTOK_ERROR_INVAL;
    }

    if (i < json_len && json[i] == '.') {
        i++;
        if (i >= json_len) {
            jstok_set_error(p, JSTOK_ERROR_PART, i);
            return JSTOK_ERROR_PART;
        }
        if (!jstok_is_digit(json[i])) {
            jstok_set_error(p, JSTOK_ERROR_INVAL, i);
            return JSTOK_ERROR_INVAL;
        }
        while (i < json_len && jstok_is_digit(json[i])) i++;
    }

    if (i < json_len && (json[i] == 'e' || json[i] == 'E')) {
        i++;
        if (i >= json_len) {
            jstok_set_error(p, JSTOK_ERROR_PART, i);
            return JSTOK_ERROR_PART;
        }
        if (json[i] == '+' || json[i] == '-') {
            i++;
            if (i >= json_len) {
                jstok_set_error(p, JSTOK_ERROR_PART, i);
                return JSTOK_ERROR_PART;
            }
        }
        if (!jstok_is_digit(json[i])) {
            jstok_set_error(p, JSTOK_ERROR_INVAL, i);
            return JSTOK_ERROR_INVAL;
        }
        while (i < json_len && jstok_is_digit(json[i])) i++;
    }

    /* EOF finalizes a number only when caller marks this chunk as final. */
    if (i >= json_len) {
        if ((flags & JSTOK_PARSE_FINAL) == 0u) {
            jstok_set_error(p, JSTOK_ERROR_PART, i);
            return JSTOK_ERROR_PART;
        }
        if (p->depth != 0) {
            jstok_set_error(p, JSTOK_ERROR_PART, i);
            return JSTOK_ERROR_PART;
        }
        *out_end = i;
        return 0;
    }

    if (i < json_len && !jstok_is_delim(json[i])) {
        jstok_set_error(p, JSTOK_ERROR_INVAL, i);
        return JSTOK_ERROR_INVAL;
    }

    *out_end = i;
    return 0;
}

static int jstok_parse_primitive_token(jstok_parser* p, const char* json, int json_len, jstoktok_t* toks,
                                       int max_tokens, int parent, unsigned flags) {
    int start = p->pos;
    int n, endpos;

    if (p->pos >= json_len) {
        jstok_set_error(p, JSTOK_ERROR_PART, p->pos);
        return JSTOK_ERROR_PART;
    }

    if (json[p->pos] == 't') {
        n = jstok_parse_literal(p, json, json_len, "true", 4, flags);
        if (n < 0) return n;
        p->pos += n;
        return jstok_new_token(p, toks, max_tokens, JSTOK_PRIMITIVE, start, p->pos, parent);
    } else if (json[p->pos] == 'f') {
        n = jstok_parse_literal(p, json, json_len, "false", 5, flags);
        if (n < 0) return n;
        p->pos += n;
        return jstok_new_token(p, toks, max_tokens, JSTOK_PRIMITIVE, start, p->pos, parent);
    } else if (json[p->pos] == 'n') {
        n = jstok_parse_literal(p, json, json_len, "null", 4, flags);
        if (n < 0) return n;
        p->pos += n;
        return jstok_new_token(p, toks, max_tokens, JSTOK_PRIMITIVE, start, p->pos, parent);
    } else {
        /* number */
        endpos = 0;
        n = jstok_parse_number_span(p, json, json_len, &endpos, flags);
        if (n < 0) return n;
        p->pos = endpos;
        return jstok_new_token(p, toks, max_tokens, JSTOK_PRIMITIVE, start, p->pos, parent);
    }
}

static int jstok_start_container(jstok_parser* p, const char* json, int json_len, jstoktok_t* toks, int max_tokens,
                                 jstoktype_t type) {
    int parent_idx = -1;
    int tok_idx;
    jstok_state_t st;
    jstok_state_t saved_parent_st = 0;
    int saved_root_done = p->root_done;
    int saved_pos = p->pos;
    int toknext_before = p->toknext;
    jstok_frame_t* fr;

    (void)json;
    (void)json_len;

    fr = jstok_top(p);
    if (fr) {
        parent_idx = fr->tok;
        saved_parent_st = fr->st;
    }

    /* This container token is a value for its parent */
    {
        int r = jstok_accept_value(p, toks);
        if (r < 0) return r;
    }

    tok_idx = jstok_new_token(p, toks, max_tokens, type, p->pos, -1, parent_idx);
    if (tok_idx < 0) {
        jstok_rollback_accept_value(p, toks, fr, saved_parent_st, saved_root_done);
        if (tok_idx == JSTOK_ERROR_NOMEM) {
            p->pos = saved_pos;
        }
        return tok_idx;
    }

    st = (type == JSTOK_OBJECT) ? JSTOK_ST_OBJ_KEY_OR_END : JSTOK_ST_ARR_VALUE_OR_END;

    /* Push new frame, tok_idx is -1 in count-only but that is fine */
    {
        int pushed = jstok_push(p, type, st, toks ? tok_idx : -1);
        if (pushed < 0) {
            p->toknext = toknext_before;
            jstok_rollback_accept_value(p, toks, fr, saved_parent_st, saved_root_done);
            p->pos = saved_pos;
            return pushed;
        }
    }

    p->pos++; /* consume '{' or '[' */
    return tok_idx;
}

static int jstok_end_container(jstok_parser* p, const char* json, int json_len, jstoktok_t* toks, jstoktype_t type,
                               char closer) {
    jstok_frame_t* fr;
    int tok_idx;

    (void)json;
    (void)json_len;

    fr = jstok_top(p);
    if (!fr || fr->type != type) {
        jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
        return JSTOK_ERROR_INVAL;
    }

    /* Enforce valid closing state */
    if (type == JSTOK_OBJECT) {
        if (fr->st == JSTOK_ST_OBJ_KEY) {
            jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
            return JSTOK_ERROR_INVAL;
        }
        if (fr->st == JSTOK_ST_OBJ_COLON || fr->st == JSTOK_ST_OBJ_VALUE) {
            jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
            return JSTOK_ERROR_INVAL;
        }
        if (fr->st == JSTOK_ST_OBJ_KEY_OR_END || fr->st == JSTOK_ST_OBJ_COMMA_OR_END) {
            /* ok */
        } else {
            jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
            return JSTOK_ERROR_INVAL;
        }
    } else {
        if (fr->st == JSTOK_ST_ARR_VALUE) {
            jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
            return JSTOK_ERROR_INVAL;
        }
        if (fr->st == JSTOK_ST_ARR_VALUE_OR_END || fr->st == JSTOK_ST_ARR_COMMA_OR_END) {
            /* ok */
        } else {
            jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
            return JSTOK_ERROR_INVAL;
        }
    }

    tok_idx = fr->tok;

    if (toks && tok_idx >= 0) {
        /* end is exclusive, so end after the closer */
        toks[tok_idx].end = p->pos + 1;
        if (toks[tok_idx].start < 0) toks[tok_idx].start = p->pos; /* defensive */
        if (toks[tok_idx].type != type) toks[tok_idx].type = type;
    }

    /* Pop frame and consume closer */
    jstok_pop(p);
    if (closer != '\0') {
        p->pos++;
    }
    return 0;
}

JSTOK_API int jstok_parse_ex(jstok_parser* p, const char* json, int json_len, jstoktok_t* tokens, int max_tokens,
                             unsigned flags) {
    int r;

    if (!p || !json || json_len < 0) {
        if (p) jstok_set_error(p, JSTOK_ERROR_INVAL, 0);
        return JSTOK_ERROR_INVAL;
    }

    /* Reset error reporting for this call */
    p->error_pos = -1;
    p->error_code = 0;

    while (p->pos < json_len) {
        char c;
        unsigned char cls;
        jstok_frame_t* fr;
        int parent_idx;

        while (p->pos < json_len && jstok_classify(json[p->pos]) == JSTOK_CC_SPACE) {
            p->pos++;
        }
        if (p->pos >= json_len) break;

        c = json[p->pos];
        cls = jstok_classify(c);

        fr = jstok_top(p);
        parent_idx = fr ? fr->tok : -1;

        if (cls == JSTOK_CC_LBRACE) {
            /* Container start also calls accept_value internally and rolls back on failure. */
            r = jstok_start_container(p, json, json_len, tokens, max_tokens, JSTOK_OBJECT);
            if (r < 0) return r;
            continue;
        }

        if (cls == JSTOK_CC_LBRACKET) {
            r = jstok_start_container(p, json, json_len, tokens, max_tokens, JSTOK_ARRAY);
            if (r < 0) return r;
            continue;
        }

        if (cls == JSTOK_CC_RBRACE || cls == JSTOK_CC_RBRACKET) {
            if (cls == JSTOK_CC_RBRACE) {
                r = jstok_end_container(p, json, json_len, tokens, JSTOK_OBJECT, '}');
            } else {
                r = jstok_end_container(p, json, json_len, tokens, JSTOK_ARRAY, ']');
            }
            if (r < 0) return r;
            continue;
        }

        if (cls == JSTOK_CC_COLON) {
            if (!fr || fr->type != JSTOK_OBJECT || fr->st != JSTOK_ST_OBJ_COLON) {
                jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
                return JSTOK_ERROR_INVAL;
            }
            fr->st = JSTOK_ST_OBJ_VALUE;
            p->pos++;
            continue;
        }

        if (cls == JSTOK_CC_COMMA) {
            if (!fr) {
                jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
                return JSTOK_ERROR_INVAL;
            }
            if (fr->type == JSTOK_OBJECT) {
                if (fr->st != JSTOK_ST_OBJ_COMMA_OR_END) {
                    jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
                    return JSTOK_ERROR_INVAL;
                }
                fr->st = JSTOK_ST_OBJ_KEY;
                p->pos++;
                continue;
            } else {
                if (fr->st != JSTOK_ST_ARR_COMMA_OR_END) {
                    jstok_set_error(p, JSTOK_ERROR_INVAL, p->pos);
                    return JSTOK_ERROR_INVAL;
                }
                fr->st = JSTOK_ST_ARR_VALUE;
                p->pos++;
                continue;
            }
        }

        if (cls == JSTOK_CC_QUOTE) {
            int tok_idx;

            /* If we're in an object expecting a key, treat as key */
            if (fr && fr->type == JSTOK_OBJECT && (fr->st == JSTOK_ST_OBJ_KEY_OR_END || fr->st == JSTOK_ST_OBJ_KEY)) {
                jstok_state_t saved_st = fr->st;

                tok_idx = jstok_parse_string_token(p, json, json_len, tokens, max_tokens, parent_idx);
                if (tok_idx < 0) {
                    if (tok_idx == JSTOK_ERROR_PART) {
                        fr->st = saved_st; /* Rollback state (likely irrelevant as parse_string didn't change it, but
                                              consistent) */
                    }
                    return tok_idx;
                }
                r = jstok_accept_key(p);
                if (r < 0) return r;
                continue;
            }

            /* Otherwise treat as a value */
            {
                jstok_state_t saved_st = fr ? fr->st : 0;
                int saved_root_done = p->root_done;
                int saved_pos = p->pos;

                r = jstok_accept_value(p, tokens);
                if (r < 0) return r;

                tok_idx = jstok_parse_string_token(p, json, json_len, tokens, max_tokens, parent_idx);
                if (tok_idx < 0) {
                    jstok_rollback_accept_value(p, tokens, fr, saved_st, saved_root_done);
                    if (tok_idx == JSTOK_ERROR_PART || tok_idx == JSTOK_ERROR_NOMEM) {
                        p->pos = saved_pos;
                    }
                    return tok_idx;
                }
            }
            continue;
        }

        /* Primitive value */
        {
            jstok_state_t saved_st = fr ? fr->st : 0;
            int saved_root_done = p->root_done;
            int saved_pos = p->pos;

            r = jstok_accept_value(p, tokens);
            if (r < 0) return r;

            r = jstok_parse_primitive_token(p, json, json_len, tokens, max_tokens, parent_idx, flags);
            if (r < 0) {
                jstok_rollback_accept_value(p, tokens, fr, saved_st, saved_root_done);
                if (r == JSTOK_ERROR_PART || r == JSTOK_ERROR_NOMEM) {
                    p->pos = saved_pos;
                }
                return r;
            }
        }
        continue;
    }

#ifdef JSTOK_STRICT
    /* In strict mode, require exactly one top-level value */
    if (p->depth == 0 && !p->root_done) {
        jstok_set_error(p, JSTOK_ERROR_PART, p->pos);
        return JSTOK_ERROR_PART;
    }
#endif

    /* If input ended but containers are open, it's incomplete */
    if (p->depth != 0) {
        jstok_set_error(p, JSTOK_ERROR_PART, p->pos);
        return JSTOK_ERROR_PART;
    }

    /* If we parsed a top-level value, allow trailing whitespace only in strict mode */
#ifdef JSTOK_STRICT
    /* root_done already ensures single value, scan ensures no extra tokens were accepted */
#endif

    /* Success: tokens used or required token count */
    return p->toknext;
}

JSTOK_API int jstok_parse(jstok_parser* p, const char* json, int json_len, jstoktok_t* tokens, int max_tokens) {
    return jstok_parse_ex(p, json, json_len, tokens, max_tokens, JSTOK_PARSE_FINAL);
}

#ifndef JSTOK_NO_HELPERS

JSTOK_API jstok_span_t jstok_span(const char* json, const jstoktok_t* t) {
    jstok_span_t s;
    if (!json || !t || t->start < 0 || t->end < t->start) {
        s.p = (const char*)0;
        s.n = 0;
        return s;
    }
    s.p = json + t->start;
    s.n = (size_t)(t->end - t->start);
    return s;
}

JSTOK_API int jstok_eq(const char* json, const jstoktok_t* t, const char* s) {
    size_t n;
    jstok_span_t sp;

    if (!json || !t || !s) return 0;
    sp = jstok_span(json, t);
    if (!sp.p) return 0;

    n = strlen(s);
    if (sp.n != n) return 0;

    return memcmp(sp.p, s, n) == 0;
}

/* Skip subtree without recursion using a small stack */
JSTOK_API int jstok_skip(const jstoktok_t* toks, int count, int i) {
    int idx;
    int sp;
    int rem[JSTOK_MAX_DEPTH];

    if (!toks || i < 0 || i >= count) return count;
    if (toks[i].type == JSTOK_STRING || toks[i].type == JSTOK_PRIMITIVE) return i + 1;

    idx = i + 1;
    sp = 0;

    /* immediate children count */
    if (toks[i].type == JSTOK_ARRAY) {
        rem[sp++] = toks[i].size;
    } else if (toks[i].type == JSTOK_OBJECT) {
        rem[sp++] = toks[i].size * 2;
    } else {
        return i + 1;
    }

    while (sp > 0) {
        if (rem[sp - 1] == 0) {
            sp--;
            continue;
        }

        rem[sp - 1]--;

        if (idx >= count) return count;

        if (toks[idx].type == JSTOK_STRING || toks[idx].type == JSTOK_PRIMITIVE) {
            idx++;
            continue;
        }

        if (sp >= JSTOK_MAX_DEPTH) return count;

        if (toks[idx].type == JSTOK_ARRAY) {
            rem[sp++] = toks[idx].size;
            idx++;
            continue;
        }

        if (toks[idx].type == JSTOK_OBJECT) {
            rem[sp++] = toks[idx].size * 2;
            idx++;
            continue;
        }

        idx++;
    }

    return idx;
}

JSTOK_API int jstok_array_at(const jstoktok_t* toks, int count, int arr_tok, int idx) {
    int i;
    int cur;

    if (!toks || arr_tok < 0 || arr_tok >= count) return -1;
    if (toks[arr_tok].type != JSTOK_ARRAY) return -1;
    if (idx < 0 || idx >= toks[arr_tok].size) return -1;

    cur = arr_tok + 1;
    for (i = 0; i < idx; i++) {
        cur = jstok_skip(toks, count, cur);
        if (cur >= count) return -1;
    }
    return cur;
}

JSTOK_API int jstok_object_get(const char* json, const jstoktok_t* toks, int count, int obj_tok, const char* key) {
    int pair;
    int cur;
    int k;
    int v;
    size_t key_len;

    if (!json || !toks || !key) return -1;
    if (obj_tok < 0 || obj_tok >= count) return -1;
    if (toks[obj_tok].type != JSTOK_OBJECT) return -1;

    key_len = strlen(key);
    cur = obj_tok + 1;
    for (pair = 0; pair < toks[obj_tok].size; pair++) {
        k = cur;
        v = k + 1;
        if (v >= count) return -1;

        if (toks[k].type == JSTOK_STRING) {
            int ks = toks[k].start;
            int ke = toks[k].end;
            if (ks >= 0 && ke >= ks) {
                size_t span_len = (size_t)(ke - ks);
                if (span_len == key_len && memcmp(json + ks, key, key_len) == 0) {
                    return v;
                }
            }
        }

        cur = jstok_skip(toks, count, v);
        if (cur >= count) return -1;
    }
    return -1;
}

JSTOK_API int jstok_atoi64(const char* json, const jstoktok_t* t, long long* out) {
    jstok_span_t sp;
    unsigned long long mag;
    unsigned long long limit;
    size_t i;
    int neg;

    if (!json || !t || !out) return -1;
    if (t->type != JSTOK_PRIMITIVE) return -1;

    sp = jstok_span(json, t);
    if (!sp.p || sp.n == 0) return -1;

    mag = 0;
    i = 0;
    neg = 0;

    if (sp.p[0] == '-') {
        neg = 1;
        i = 1;
        if (i >= sp.n) return -1;
    }

    limit = neg ? ((unsigned long long)LLONG_MAX + 1ULL) : (unsigned long long)LLONG_MAX;

    for (; i < sp.n; i++) {
        char c = sp.p[i];
        unsigned long long digit;
        if (c < '0' || c > '9') return -1;
        digit = (unsigned long long)(c - '0');

        if (mag > limit / 10ULL) return -1;
        if (mag == limit / 10ULL && digit > (limit % 10ULL)) return -1;
        mag = mag * 10ULL + digit;
    }

    if (neg) {
        if (mag == ((unsigned long long)LLONG_MAX + 1ULL)) {
            *out = LLONG_MIN;
        } else {
            *out = -(long long)mag;
        }
    } else {
        *out = (long long)mag;
    }

    return 0;
}

JSTOK_API int jstok_atob(const char* json, const jstoktok_t* t, int* out) {
    if (!json || !t || !out) return -1;
    if (t->type != JSTOK_PRIMITIVE) return -1;
    if (jstok_eq(json, t, "true")) {
        *out = 1;
        return 0;
    }
    if (jstok_eq(json, t, "false")) {
        *out = 0;
        return 0;
    }
    return -1;
}

static int jstok_hexval(char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

JSTOK_API int jstok_unescape(const char* json, const jstoktok_t* t, char* out, size_t out_cap, size_t* out_len) {
    jstok_span_t sp;
    size_t i;
    size_t w;

    if (!json || !t || !out || !out_len) return -1;
    if (t->type != JSTOK_STRING) return -1;

    sp = jstok_span(json, t);
    if (!sp.p) return -1;

    w = 0;
    for (i = 0; i < sp.n; i++) {
        char c = sp.p[i];

        if (c != '\\') {
            if (w >= out_cap) return -1;
            out[w++] = c;
            continue;
        }

        i++;
        if (i >= sp.n) return -1;
        c = sp.p[i];

        if (c == '"' || c == '\\' || c == '/') {
            if (w >= out_cap) return -1;
            out[w++] = c;
            continue;
        }
        if (c == 'b') {
            if (w >= out_cap) return -1;
            out[w++] = '\b';
            continue;
        }
        if (c == 'f') {
            if (w >= out_cap) return -1;
            out[w++] = '\f';
            continue;
        }
        if (c == 'n') {
            if (w >= out_cap) return -1;
            out[w++] = '\n';
            continue;
        }
        if (c == 'r') {
            if (w >= out_cap) return -1;
            out[w++] = '\r';
            continue;
        }
        if (c == 't') {
            if (w >= out_cap) return -1;
            out[w++] = '\t';
            continue;
        }

        if (c == 'u') {
            int v0, v1, v2, v3;
            unsigned code;
            unsigned low;

            if (i + 4 >= sp.n) return -1;

            v0 = jstok_hexval(sp.p[i + 1]);
            v1 = jstok_hexval(sp.p[i + 2]);
            v2 = jstok_hexval(sp.p[i + 3]);
            v3 = jstok_hexval(sp.p[i + 4]);
            if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return -1;

            code = (unsigned)((v0 << 12) | (v1 << 8) | (v2 << 4) | v3);

            if (code >= 0xD800u && code <= 0xDBFFu) {
                int w0, w1, w2, w3;

                /* High surrogate must be followed by \uDC00..\uDFFF */
                if (i + 10 >= sp.n) return -1;
                if (sp.p[i + 5] != '\\' || sp.p[i + 6] != 'u') return -1;

                w0 = jstok_hexval(sp.p[i + 7]);
                w1 = jstok_hexval(sp.p[i + 8]);
                w2 = jstok_hexval(sp.p[i + 9]);
                w3 = jstok_hexval(sp.p[i + 10]);
                if (w0 < 0 || w1 < 0 || w2 < 0 || w3 < 0) return -1;

                low = (unsigned)((w0 << 12) | (w1 << 8) | (w2 << 4) | w3);
                if (low < 0xDC00u || low > 0xDFFFu) return -1;

                code = 0x10000u + (((code - 0xD800u) << 10) | (low - 0xDC00u));
                i += 10;
            } else {
                if (code >= 0xDC00u && code <= 0xDFFFu) return -1;
                i += 4;
            }

            /* Encode as UTF-8, minimal */
            if (code <= 0x7F) {
                if (w + 1 > out_cap) return -1;
                out[w++] = (char)code;
            } else if (code <= 0x7FF) {
                if (w + 2 > out_cap) return -1;
                out[w++] = (char)(0xC0 | ((code >> 6) & 0x1F));
                out[w++] = (char)(0x80 | (code & 0x3F));
            } else if (code <= 0xFFFF) {
                if (w + 3 > out_cap) return -1;
                out[w++] = (char)(0xE0 | ((code >> 12) & 0x0F));
                out[w++] = (char)(0x80 | ((code >> 6) & 0x3F));
                out[w++] = (char)(0x80 | (code & 0x3F));
            } else {
                if (code > 0x10FFFFu) return -1;
                if (w + 4 > out_cap) return -1;
                out[w++] = (char)(0xF0 | ((code >> 18) & 0x07));
                out[w++] = (char)(0x80 | ((code >> 12) & 0x3F));
                out[w++] = (char)(0x80 | ((code >> 6) & 0x3F));
                out[w++] = (char)(0x80 | (code & 0x3F));
            }
            continue;
        }

        return -1;
    }

    *out_len = w;
    return 0;
}

JSTOK_API int jstok_path(const char* json, const jstoktok_t* toks, int count, int root, ...) {
    int curr = root;
    va_list args;

    if (!json || !toks || root < 0 || root >= count) return -1;

    va_start(args, root);

    while (curr >= 0 && curr < count) {
        jstoktype_t type = toks[curr].type;

        if (type == JSTOK_OBJECT) {
            const char* key = va_arg(args, const char*);
            if (key == NULL) break; /* Sentinel reached */
            curr = jstok_object_get(json, toks, count, curr, key);
        } else if (type == JSTOK_ARRAY) {
            int idx = va_arg(args, int);
            curr = jstok_array_at(toks, count, curr, idx);
        } else {
            /* Primitive or String: cannot traverse deeper */
            break;
        }
    }

    va_end(args);
    return curr;
}

JSTOK_API jstok_sse_res jstok_sse_next(const char* buf, size_t len, size_t* pos, jstok_span_t* out) {
    if (*pos > len) *pos = len;
    size_t cur = *pos;
    if (cur >= len) return JSTOK_SSE_NEED_MORE;

    while (cur < len) {
        size_t line_start = cur;
        const char* p_newline = (const char*)memchr(buf + cur, '\n', len - cur);

        if (!p_newline) {
            *pos = line_start;
            return JSTOK_SSE_NEED_MORE;
        }

        size_t next_line_start = (size_t)(p_newline - buf) + 1;
        size_t line_len = next_line_start - line_start - 1;

        if (line_len > 0 && buf[line_start + line_len - 1] == '\r') {
            line_len--;
        }

        if (line_len >= 5 && buf[line_start] == 'd' && buf[line_start + 1] == 'a' && buf[line_start + 2] == 't' &&
            buf[line_start + 3] == 'a' && buf[line_start + 4] == ':') {
            size_t payload_off = 5;
            if (payload_off < line_len && buf[line_start + payload_off] == ' ') {
                payload_off++;
            }

            if (out) {
                out->p = buf + line_start + payload_off;
                out->n = line_len - payload_off;
            }
            *pos = next_line_start;
            return JSTOK_SSE_DATA;
        }

        cur = next_line_start;
        *pos = cur;
    }

    return JSTOK_SSE_NEED_MORE;
}

#endif /* JSTOK_NO_HELPERS */
#endif /* JSTOK_HEADER */
