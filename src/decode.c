#include <stdio.h>

#include "decode.h"
#include "layers.h"
#include "flappie_stdlib.h"
#include "util.h"


float argmax_decoder(const_flappie_matrix logpost, int *seq) {
    RETURN_NULL_IF(NULL == logpost, NAN);
    RETURN_NULL_IF(NULL == seq, NAN);

    const int nblock = logpost->nc;
    const int nstate = logpost->nr;
    assert(nstate > 0);
    const int stride = logpost->stride;
    assert(stride > 0);
    int offset;

    float logscore = 0;
    int imax;
    for (int blk = 0; blk < nblock; blk++) {
        offset = blk * stride;
        imax = argmaxf(logpost->data.f + offset, nstate);
        logscore += logpost->data.f[offset + imax];
        seq[blk] = (imax == nstate - 1) ? -1 : imax;
    }

    return logscore;
}


char * collapse_repeats(int const * path, size_t npos, int modbase){
    assert(modbase > 0);
    RETURN_NULL_IF(NULL == path, NULL);

    int nbase = 1;
    for(size_t pos=1 ; pos < npos ; pos++){
        if(path[pos] != path[pos - 1]){
            nbase += 1;
        }
    }

    char * basecall = calloc(nbase + 1, sizeof(char));
    RETURN_NULL_IF(NULL == basecall, NULL);

    basecall[0] = base_lookup[path[0] % modbase];
    for(size_t pos=1, bpos=1 ; pos < npos ; pos++){
        if(path[pos] != path[pos - 1]){
            assert(bpos < nbase);
            basecall[bpos] = basechar(path[pos] % modbase);
            bpos += 1;
        }
    }

    return basecall;
}


size_t change_positions(int const * path, size_t npos, int * chpos){
    RETURN_NULL_IF(NULL == path, 0);
    RETURN_NULL_IF(NULL == chpos, 0);

    size_t nch = 0;
    for(size_t pos=1 ; pos < npos ; pos++){
        if(path[pos] == path[pos -1 ]){
            continue;
        }
        chpos[nch] = pos;
        nch += 1;
    }
    return nch;
}


void colmaxf(float * x, int nr, int nc, int * idx){
    assert(nr > 0);
    assert(nc > 0);
    RETURN_NULL_IF(NULL == x,);
    RETURN_NULL_IF(NULL == idx,);

    for(int r=0 ; r < nr ; r++){
        // Initialise
        idx[r] = 0;
    }

    for(int c=1 ; c < nc ; c++){
        const size_t offset2 = c * nr;
        for(int r=0 ; r<nr ; r++){
            if(x[offset2 + r] > x[idx[r] * nr + r]){
                idx[r] = c;
            }
        }
    }
}


inline size_t trans_lookup(size_t from, size_t to, size_t nbase){
    assert(nbase > = 0);
    assert(from >= 0 && from < nbase + nbase);
    assert(to >= 0 && to < nbase + nbase);
    assert(to < nbase || ((to % nbase) == (from % nbase)));

    const size_t nstate = nbase + nbase;
    const size_t offset = nbase * nstate;

    return (to < nbase) ? (to * nstate + from) : (offset + from);
}


/**   Viterbi decoding of CRF flipflop
 **/
float decode_crf_flipflop(const_flappie_matrix trans, bool combine_stays, int * path, float * qpath){
    RETURN_NULL_IF(NULL == trans, NAN);
    RETURN_NULL_IF(NULL == path, NAN);
    RETURN_NULL_IF(NULL == qpath, NAN);

    const size_t nblk = trans->nc;
    const size_t nbase = roundf((-1.0f + sqrtf(1.0f + 2.0f * trans->nr)) / 2.0f);
    const size_t nstate = nbase + nbase;
    assert(nstate == nbase + nbase);
    assert(nstate * (nbase + 1) == trans->nr);

    float * mem = calloc(2 * nstate, sizeof(float));
    flappie_imatrix tb = make_flappie_imatrix(nstate, nblk);
    if(NULL == mem || NULL == tb){
        tb = free_flappie_imatrix(tb);
        free(mem);
        return NAN;
    }

    float * curr = mem;
    float * prev = mem + nstate;


    //  Forwards Viterbi pass
    for(size_t blk=0 ; blk < nblk ; blk++){
        const size_t offset = blk * trans->stride;
        const size_t offset_flop = offset + nstate * nbase;
        const size_t tboffset = blk * tb->stride;
        {   // Swap
            float * tmp = curr;
            curr = prev;
            prev = tmp;
        }

        for(size_t b2=nbase ; b2 < nstate ; b2++){
            // Stay in flop state
            curr[b2] = prev[b2] + trans->data.f[offset_flop + b2];
            tb->data.f[tboffset + b2] = b2;
            // Move from flip to flop state
            const size_t from_base = b2 - nbase;
            const float score = prev[from_base] + trans->data.f[offset_flop + from_base];
            if(score > curr[b2]){
                curr[b2] = score;
                tb->data.f[tboffset + b2] = from_base;
            }
        }


        for(size_t b1=0 ; b1 < nbase ; b1++){
	    //   b1 -- flip state
            const size_t offset_state = offset + b1 * nstate;
            curr[b1] = trans->data.f[offset_state + 0] + prev[0];
            tb->data.f[tboffset + b1] = 0;
            for(size_t from_state=1 ; from_state < nstate ; from_state++){
                // from_state either flip or flop
                const float score = trans->data.f[offset_state + from_state] + prev[from_state];
                if(score > curr[b1]){
                    curr[b1] = score;
                    tb->data.f[tboffset + b1] = from_state;
                }
            }
        }
    }

    //  Traceback
    const float score = valmaxf(curr, nstate);
    path[nblk] = argmaxf(curr, nstate);
    for(size_t blk=nblk ; blk > 0 ; blk--){
        const size_t offset = (blk - 1) * tb->stride;
        const size_t qoffset = (blk - 1) * trans->stride;
        path[blk - 1] = tb->data.f[offset + path[blk]];
        qpath[blk] = trans->data.f[qoffset + trans_lookup(path[blk-1], path[blk], nbase)];
    }
    qpath[0] = NAN;

    if(combine_stays){
        for(size_t blk=0 ; blk <= nblk ; blk++){
            path[blk] = (path[blk] < nbase) ? path[blk] : -1;
        }
    }

    tb = free_flappie_imatrix(tb);
    free(mem);

    return score;
}


/**   Decoding of CRF flip-posteriors with transition constraint
 **/
float constrained_crf_flipflop(const_flappie_matrix post, int * path){
    RETURN_NULL_IF(NULL == post, NAN);
    RETURN_NULL_IF(NULL == path, NAN);

    const size_t nblk = post->nc;
    const size_t nstate = post->nr;
    const size_t nbase = nstate / 2;
    assert(nstate == nbase + nbase);

    float * mem = calloc(2 * nstate, sizeof(float));
    flappie_imatrix tb = make_flappie_imatrix(nstate, nblk);
    if(NULL == mem || NULL == tb){
        tb = free_flappie_imatrix(tb);
        free(mem);
        return NAN;
    }

    float * curr = mem;
    float * prev = mem + nstate;


    //  Forwards Viterbi pass
    for(size_t blk=0 ; blk < nblk ; blk++){
        const size_t offset = blk * post->stride;
        const size_t tboffset = blk * tb->stride;
        {   // Swap
            float * tmp = curr;
            curr = prev;
            prev = tmp;
        }

        for(size_t b2=nbase ; b2 < nstate ; b2++){
            const size_t best = (prev[b2] > prev[b2 - nbase]) ? b2 : (b2 - nbase);
            curr[b2] = prev[best];
            tb->data.f[tboffset + b2] = best;
        }


        for(size_t b1=0 ; b1 < nbase ; b1++){
            const int from_best = argmaxf(prev, nstate);
            curr[b1] = prev[from_best];
            tb->data.f[tboffset + b1] = from_best;
        }

        for(size_t st=0 ; st < nstate ; st++){
            curr[st] += post->data.f[offset + st];
        }
    }

    //  Traceback
    const float score = valmaxf(curr, nstate);
    path[nblk] = argmaxf(curr, nstate);
    for(size_t blk=nblk ; blk > 0 ; blk--){
        const size_t offset = (blk - 1) * tb->stride;
        path[blk - 1] = tb->data.f[offset + path[blk]];
    }

    tb = free_flappie_imatrix(tb);
    free(mem);

    return score;
}


/**   Posterior probabilities of CRF flipflop
 **/
flappie_matrix posterior_crf_flipflop(const_flappie_matrix trans, bool return_log){
    RETURN_NULL_IF(NULL == trans, NULL);

    const size_t nblk = trans->nc;
    const size_t nbase = roundf((-1.0f + sqrtf(1.0f + 2.0f * trans->nr)) / 2.0f);
    const size_t nstate = nbase + nbase;
    assert(nstate == nbase + nbase);
    assert(nstate * (nbase + 1) == trans->nr);

    flappie_matrix fwd = make_flappie_matrix(nstate, nblk + 1);
    RETURN_NULL_IF(NULL == fwd, NULL);


    //  Forwards pass
    for(size_t blk=0 ; blk < nblk ; blk++){
        const size_t offset = blk * trans->stride;
        const size_t offset_flop = offset + nstate * nbase;

        float * prev = fwd->data.f + blk * fwd->stride;
        float * curr = prev + fwd->stride;

        for(size_t b2=nbase ; b2 < nstate ; b2++){
            // Stay in flop state
            curr[b2] = prev[b2] + trans->data.f[offset_flop + b2];
            // Move from flip to flop state
            const size_t from_base = b2 - nbase;
            const float score = prev[from_base] + trans->data.f[offset_flop + from_base];
            curr[b2] = logsumexpf(curr[b2], score);
        }


        for(size_t b1=0 ; b1 < nbase ; b1++){
	    //   b1 -- flip state
            const size_t offset_state = offset + b1 * nstate;
            curr[b1] = trans->data.f[offset_state + 0] + prev[0];
            for(size_t from_state=1 ; from_state < nstate ; from_state++){
                // from_state either flip or flop
                const float score = trans->data.f[offset_state + from_state] + prev[from_state];
                curr[b1] = logsumexpf(curr[b1], score);
            }
        }
    }

    float * mem = calloc(2 * nstate, sizeof(float));
    if(NULL == mem){
        free(fwd);
        return NULL;
    }
    float * prev = mem;
    float * curr = mem + nstate;

    //  Backwards pass
    for(size_t blk=nblk ; blk > 0 ; blk--){
        const size_t foffset = (blk - 1) * fwd->stride;
        const size_t offset = (blk - 1) * trans->stride;
        const size_t offset_flop = offset + nstate * nbase;

        {  // Swap
           float * tmp = prev;
           prev = curr;
           curr = tmp;
        }

        for(size_t b2=nbase ; b2 < nstate ; b2++){
            const size_t from_base = b2 - nbase;
            // Stay in flop state
            curr[b2] = prev[b2] + trans->data.f[offset_flop + b2];
            // Move from flip to flop state
            curr[from_base] = prev[b2] + trans->data.f[offset_flop + from_base];
        }


        for(size_t b1=0 ; b1 < nbase ; b1++){
	    //   b1 -- flip state
            const size_t offset_state = offset + b1 * nstate;
            for(size_t from_state=0 ; from_state < nstate ; from_state++){
                // from_state either flip or flop
                const float score = trans->data.f[offset_state + from_state] + prev[b1];
                curr[from_state] = logsumexpf(curr[from_state], score);
            }
        }

        for(size_t st=0 ; st < nstate ; st++){
            // Add to fwd vector
            fwd->data.f[foffset + st] += curr[st];
        }
    }

    free(mem);

    if(!return_log){
        exp_activation_inplace(fwd);
        row_normalise_inplace(fwd);
    }


    return fwd;
}


/**   Posterior probabilities of CRF flipflop
 **/
flappie_matrix transpost_crf_flipflop(const_flappie_matrix trans, bool return_log){
    RETURN_NULL_IF(NULL == trans, NULL);

    const size_t nblk = trans->nc;
    const size_t nbase = roundf((-1.0f + sqrtf(1.0f + 2.0f * trans->nr)) / 2.0f);
    const size_t nstate = nbase + nbase;
    assert(nstate == nbase + nbase);
    assert(nstate * (nbase + 1) == trans->nr);

    flappie_matrix fwd = make_flappie_matrix(nstate, nblk + 1);
    flappie_matrix tpost = make_flappie_matrix(trans->nr, nblk);
    if(NULL == fwd || NULL == tpost){
        fwd = free_flappie_matrix(fwd);
        tpost = free_flappie_matrix(tpost);
        return NULL;
    }


    //  Forwards pass
    for(size_t blk=0 ; blk < nblk ; blk++){
        const size_t offset = blk * trans->stride;
        const size_t offset_flop = offset + nstate * nbase;

        float * prev = fwd->data.f + blk * fwd->stride;
        float * curr = prev + fwd->stride;

        for(size_t b2=nbase ; b2 < nstate ; b2++){
            // Stay in flop state
            curr[b2] = prev[b2] + trans->data.f[offset_flop + b2];
            // Move from flip to flop state
            const size_t from_base = b2 - nbase;
            const float score = prev[from_base] + trans->data.f[offset_flop + from_base];
            curr[b2] = logsumexpf(curr[b2], score);
        }


        for(size_t b1=0 ; b1 < nbase ; b1++){
	    //   b1 -- flip state
            const size_t offset_state = offset + b1 * nstate;
            curr[b1] = trans->data.f[offset_state + 0] + prev[0];
            for(size_t from_state=1 ; from_state < nstate ; from_state++){
                // from_state either flip or flop
                const float score = trans->data.f[offset_state + from_state] + prev[from_state];
                curr[b1] = logsumexpf(curr[b1], score);
            }
        }
    }

    float * mem = calloc(2 * nstate, sizeof(float));
    if(NULL == mem){
        free(fwd);
        return NULL;
    }
    float * prev = mem;
    float * curr = mem + nstate;

    //  Backwards pass
    for(size_t blk=nblk ; blk > 0 ; blk--){
        const size_t foffset = (blk - 1) * fwd->stride;
        const size_t offset = (blk - 1) * trans->stride;
        const size_t offset_flop = offset + nstate * nbase;

        {  // Swap
           float * tmp = prev;
           prev = curr;
           curr = tmp;
        }


        //  Create tpost
        for(size_t b1=0 ; b1 < nbase ; b1++){
            //  End up in flip state
            const size_t offset_state = offset + b1 * nstate;
            for(size_t st=0 ; st < nstate ; st++){
                tpost->data.f[offset_state + st] = fwd->data.f[foffset + st] + prev[b1]
                                                 + trans->data.f[offset_state + st];
            }
        }
        for(size_t b=nbase ; b < nstate ; b++){
            //  End up in flop state
            const size_t fb = b - nbase;
            tpost->data.f[offset_flop + b] = fwd->data.f[foffset + b] + prev[b]
                                           + trans->data.f[offset_flop + b];
            tpost->data.f[offset_flop + fb] = fwd->data.f[foffset + fb] + prev[b]
                                           + trans->data.f[offset_flop + fb];
        }


        //  Update backwards vector
        for(size_t b2=nbase ; b2 < nstate ; b2++){
            const size_t from_base = b2 - nbase;
            // Stay in flop state
            curr[b2] = prev[b2] + trans->data.f[offset_flop + b2];
            // Move from flip to flop state
            curr[from_base] = prev[b2] + trans->data.f[offset_flop + from_base];
        }


        for(size_t b1=0 ; b1 < nbase ; b1++){
	    //   b1 -- flip state
            const size_t offset_state = offset + b1 * nstate;
            for(size_t from_state=0 ; from_state < nstate ; from_state++){
                // from_state either flip or flop
                const float score = trans->data.f[offset_state + from_state] + prev[b1];
                curr[from_state] = logsumexpf(curr[from_state], score);
            }
        }
    }


    free(mem);
    fwd = free_flappie_matrix(fwd);


    log_row_normalise_inplace(tpost);
    if(!return_log){
        exp_activation_inplace(tpost);
    }

    return tpost;
}