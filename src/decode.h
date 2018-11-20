#pragma once
#ifndef DECODE_H
#    define DECODE_H
#    include <stdbool.h>
#    include "flappie_matrix.h"
#    include "flappie_structures.h"

static const char base_lookup[5] = {'A', 'C', 'G', 'T', 'Z' };
static inline char basechar(int b){
    return base_lookup[b];
}

float argmax_decoder(const_flappie_matrix logpost, int *seq);
char * collapse_repeats(int const * path, size_t npos, int modbase);
size_t change_positions(int const * path, size_t npos, int * chpos);

float decode_crf_flipflop(const_flappie_matrix trans, bool combine_stays, int * path, float * qpath);
float constrained_crf_flipflop(const_flappie_matrix post, int * path);

flappie_matrix posterior_crf_flipflop(const_flappie_matrix trans, bool return_log);
flappie_matrix transpost_crf_flipflop(const_flappie_matrix trans, bool return_log);

#endif                          /* DECODE_H */