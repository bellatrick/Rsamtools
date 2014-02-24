#include <Rdefines.h>
#include "BamRangeIterator.h"
#include "BamFileIterator.h"
#include "bam_mate_iter.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _bam_mate_iter_t {
    BamIterator *b_iter;
};

// BamIterator methods
void bam_mate_iter_destroy(bam_mate_iter_t iter)
{
    delete iter->b_iter;
    Free(iter);
}

bam_mates_t *bam_mates_new()
{
    bam_mates_t *mates = Calloc(1, bam_mates_t);
    mates->n = 0;
    mates->mated = NA_INTEGER;
    mates->bams = NULL;
    return mates;
}

void bam_mates_realloc(bam_mates_t *result, int n, int mated)
{
    for (int i = 0; i < result->n; ++i) {
        bam_destroy1((bam1_t *) result->bams[i]);
        result->bams[i] = NULL;
    }

    // Realloc(p, 0, *) fails inappropriately
    if (n == 0) {
	Free(result->bams);
	result->bams = NULL;
    } else
	result->bams = Realloc(result->bams, n, const bam1_t *);
    result->n = n;
    result->mated = mated;
}

void bam_mates_destroy(bam_mates_t *mates)
{
    for (int i = 0; i < mates->n; ++i)
	bam_destroy1((bam1_t *) mates->bams[i]);
    Free(mates->bams);
    Free(mates);
}

int bam_mate_read(bamFile fb, bam_mate_iter_t iter, bam_mates_t *mates)
{
    iter->b_iter->yield(fb, mates);
    return mates->n;
}

// BamRangeIterator methods
bam_mate_iter_t bam_mate_range_iter_new(const bam_index_t *bindex,
                                        int tid, int beg, int end)
{
    bam_mate_iter_t iter = Calloc(1, struct _bam_mate_iter_t);
    iter->b_iter = new BamRangeIterator(bindex, tid, beg, end);
    return iter;
}

int bam_fetch_mate(bamFile bf, const bam_index_t *idx, int tid, int beg, 
                   int end, void *data, bam_fetch_mate_f func)
{
    int n_rec;
    bam_mates_t *mates = bam_mates_new();
    bam_mate_iter_t iter = bam_mate_range_iter_new(idx, tid, beg, end);
    while ((n_rec = bam_mate_read(bf, iter, mates) > 0))
        func(mates, data);
    bam_mate_iter_destroy(iter);
    bam_mates_destroy(mates);
    return n_rec;
}

// BamFileIterator methods
bam_mate_iter_t bam_mate_file_iter_new(const bam_index_t *bindex)
{
    bam_mate_iter_t iter = Calloc(1, struct _bam_mate_iter_t);
    iter->b_iter = new BamFileIterator(bindex);
    return iter;
}

int samread_mate(bamFile fb, const bam_index_t *bindex,
                 bam_mate_iter_t *iter_p, bam_mates_t *mates)
{
    bam_mate_iter_t iter;
    if (NULL == *iter_p)
        *iter_p = bam_mate_file_iter_new(bindex);
    iter = *iter_p;
    iter->b_iter->iter_done = false;
    // single yield
    return bam_mate_read(fb, iter, mates);
}


#ifdef __cplusplus
}
#endif
