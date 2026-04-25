#include "tokenizer.h"
#include <ctype.h>
#include <string.h>

void tokenize(const char *text, token_cb_t cb, void *userdata) {
    if (!text) return;
    char word[256];
    int  wi = 0;
    for (const char *p = text; ; p++) {
        int c = (unsigned char)*p;
        if (isalnum(c)) {
            if (wi < (int)sizeof(word) - 1)
                word[wi++] = (char)tolower(c);
        } else {
            if (wi >= 2) {   /* skip single-char tokens */
                word[wi] = '\0';
                cb(word, userdata);
            }
            wi = 0;
        }
        if (!c) break;
    }
}
