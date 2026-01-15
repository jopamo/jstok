#define JSTOK_STATIC
#include "jstok.h"

void func2(void);

int main(void) {
    jstok_parser p;
    jstok_init(&p);  // Uses static impl

    // Silence unused warnings
    (void)jstok_parse;
    (void)jstok_atoi64;
    (void)jstok_atob;
    (void)jstok_unescape;
    (void)jstok_path;
    (void)jstok_sse_next;

    func2();
    return 0;
}
