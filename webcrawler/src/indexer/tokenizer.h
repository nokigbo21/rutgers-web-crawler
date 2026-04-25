#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>

/* Tokenize text into lowercase words.
   Calls callback(word, userdata) for each token.
   Words contain only [a-z0-9] after normalisation. */
typedef void (*token_cb_t)(const char *word, void *userdata);
void tokenize(const char *text, token_cb_t cb, void *userdata);

#endif
