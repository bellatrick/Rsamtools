#include "bamfile.h"
#include "io_sam.h"
#include "bam_mate_iter.h"
#include "utilities.h"
#include "hts_utilities.h"

SEXP BAMFILE_TAG = NULL;

void _check_isbamfile(SEXP ext, const char *lbl)
{
    _checkext(ext, BAMFILE_TAG, lbl);
}

samfile_t *_bam_tryopen(const char *filename, const char *filemode, void *aux)
{
    samfile_t *sfile = samopen(filename, filemode, aux);
    if (sfile == 0)
        Rf_error("failed to open SAM/BAM file\n  file: '%s'", filename);
    if (sfile->header == 0) {
        samclose(sfile);
        Rf_error("SAM/BAM header missing or empty\n  file: '%s'", filename);
    }
    return sfile;
}

static bam_index_t *_bam_tryindexload(const char *file, const char *indexname)
{
    bam_index_t *index = bam_index_load(indexname);
    if (index == 0)
        index = hts_idx_load2(file, indexname);
    if (index == 0)
        Rf_error("failed to load BAM index\n  file: %s", indexname);
    return index;
}

static void _bamfile_close(SEXP ext)
{
    BAM_FILE bfile = BAMFILE(ext);
    if (NULL != bfile->file)
        samclose(bfile->file);
    if (NULL != bfile->index)
        bam_index_destroy(bfile->index);
    if (NULL != bfile->header)
        bam_hdr_destroy(bfile->header);
    if (NULL != bfile->iter)
        bam_mate_iter_destroy(bfile->iter);
    if (NULL != bfile->pbuffer)
        pileup_pbuffer_destroy(bfile->pbuffer);
    bfile->file = NULL;
    bfile->index = NULL;
    bfile->header = NULL;
    bfile->iter = NULL;
    bfile->pbuffer = NULL;
}

static void _bamfile_finalizer(SEXP ext)
{
    if (NULL == R_ExternalPtrAddr(ext))
        return;
    _bamfile_close(ext);
    BAM_FILE bfile = BAMFILE(ext);
    Free(bfile);
    R_SetExternalPtrAddr(ext, NULL);
}

SEXP bamfile_init()
{
    BAMFILE_TAG = install("BamFile");
    return R_NilValue;
}

static BAM_FILE _bamfile_open_r(SEXP filename, SEXP indexname, SEXP filemode)
{
    BAM_FILE bfile = (BAM_FILE) Calloc(1, _BAM_FILE);

    bfile->file = NULL;
    const char *cfile;
    if (0 != Rf_length(filename)) {
        cfile = translateChar(STRING_ELT(filename, 0));
        mode = CHAR(STRING_ELT(filemode, 0));
        bfile->file = _bam_tryopen(cfile, mode, NULL);
        bfile->header = sam_hdr_read(bfile->file);
        if (bfile->header == 0 || bfile->header->n_targets == 0) {
            sam_close(bfile->file);
            bam_hdr_destroy(bfile->header);
            Free(bfile);
            Rf_error("SAM/BAM header missing or empty\n  file: '%s'", filename);
        }

        bfile->pos0 = _hts_utilities_tell(bfile->file);
        const enum htsExactFormat format = hts_get_format(bfile->file)->format;
        if (format != sam && format != bam && format != cram) {
            sam_close(bfile->file);
            bam_hdr_destroy(bfile->header);
            Free(bfile);
            Rf_error("'filename' is not a BAM file\n  file: %s", cfile);
        }
        bfile->pos0 = bam_tell(bfile->file->x.bam);
        bfile->irange0 = 0;
    }

    bfile->index = NULL;
    if (0 != Rf_length(indexname)) {
        const char *cindex = translateChar(STRING_ELT(indexname, 0));
        bfile->index = _bam_tryindexload(cfile, cindex);
        if (NULL == bfile->index) {
            samclose(bfile->file);
            Free(bfile);
            Rf_error("failed to open BAM index\n  index: %s\n", cindex);
        }
    }

    bfile->iter = NULL;
    bfile->pbuffer = NULL;
    return bfile;
}

static BAM_FILE _bamfile_open_w(SEXP file0, SEXP file1)
{
    htsFile *fin, *fout;
    bam_hdr_t *header;
    BAM_FILE bfile;

    if (0 == Rf_length(file1))
        Rf_error("'file1' must be a character(1) path to a valid bam file");
    const char
        *cfile0 = translateChar(STRING_ELT(file0, 0)),
        *cfile1 = translateChar(STRING_ELT(file1, 0));
    fin = _bam_tryopen(cfile1, "r", NULL);
    if (fin == NULL)
        Rf_error("failed to open file '%s'", cfile1);
    header = sam_hdr_read(fin);
    sam_close(fin);
    fout = _bam_tryopen(cfile0, "wb", NULL);
    if (fout == NULL) {
        bam_hdr_destroy(header);
        Rf_error("failed to open file '%s'", cfile0);
    }

    if (sam_hdr_write(fout, header) < 0) {
        bam_hdr_destroy(header);
        sam_close(fout);
        Rf_error("failed to write header to output file");
    }

    bfile = (BAM_FILE) Calloc(1, _BAM_FILE);
    bfile->file = fout;
    bfile->header = header;
    bfile->pos0 = _hts_utilities_tell(bfile->file);
    bfile->irange0 = 0;

    return bfile;
}

SEXP bamfile_open(SEXP file0, SEXP file1, SEXP mode)
{
    _checknames(file0, file1, mode);
    BAM_FILE bfile;
    if (*CHAR(STRING_ELT(mode, 0)) == 'r')
        bfile = _bamfile_open_r(file0, file1, mode);
    else
        bfile = _bamfile_open_w(file0, file1);

    SEXP ext = PROTECT(R_MakeExternalPtr(bfile, BAMFILE_TAG, file0));
    R_RegisterCFinalizerEx(ext, _bamfile_finalizer, TRUE);
    UNPROTECT(1);

    return ext;
}

SEXP bamfile_close(SEXP ext)
{
    _checkext(ext, BAMFILE_TAG, "close");
    _bamfile_close(ext);
    return ext;
}

SEXP bamfile_isopen(SEXP ext)
{
    int ans = FALSE;
    if (NULL != BAMFILE(ext)) {
        _checkext(ext, BAMFILE_TAG, "isOpen");
        ans = NULL != BAMFILE(ext)->file;
    }
    return ScalarLogical(ans);
}

SEXP bamfile_isincomplete(SEXP ext)
{
    int ans = FALSE;
    BAM_FILE bfile;
    if (NULL != BAMFILE(ext)) {
        _checkext(ext, BAMFILE_TAG, "isIncomplete");
        bfile = BAMFILE(ext);
        if (NULL != bfile && NULL != bfile->file) {
            /* heuristic: can we read a record? */
            off_t offset = _hts_utilities_tell(bfile->file);
            bam1_t *bam = bam_init1();
            ans = sam_read1(bfile->file, bfile->header, bam) > 0;
            bam_destroy1(bam);
            _hts_utilities_seek(bfile->file, offset, SEEK_SET);
        }
    }
    return ScalarLogical(ans);
}

/* implementation */

SEXP read_bamfile_header(SEXP ext, SEXP what)
{
    _checkext(ext, BAMFILE_TAG, "scanBamHeader");
    if (!(IS_LOGICAL(what) && (2L == LENGTH(what))))
        Rf_error("'what' must be logical(2)");
    if (!LOGICAL(bamfile_isopen(ext))[0])
        Rf_error("open() BamFile before reading header");
    return _read_bam_header(ext, what);
}

SEXP scan_bamfile(SEXP ext, SEXP regions, SEXP keepFlags, SEXP isSimpleCigar,
                  SEXP tagFilter, SEXP mapqFilter, SEXP reverseComplement,
                  SEXP yieldSize,
                  SEXP template_list, SEXP obeyQname, SEXP asMates,
                  SEXP qnamePrefixEnd, SEXP qnameSuffixStart)
{
    _checkext(ext, BAMFILE_TAG, "scanBam");
    _checkparams(regions, keepFlags, isSimpleCigar);
    if (!(IS_LOGICAL(reverseComplement) && (1L == LENGTH(reverseComplement))))
        Rf_error("'reverseComplement' must be logical(1)");
    if (!(IS_INTEGER(yieldSize) && (1L == LENGTH(yieldSize))))
        Rf_error("'yieldSize' must be integer(1)");
    if (!(IS_LOGICAL(obeyQname) && (1L == LENGTH(obeyQname))))
        Rf_error("'obeyQname' must be logical(1)");
    if (!(IS_LOGICAL(asMates) && (1L == LENGTH(asMates))))
        Rf_error("'asMates' must be logical(1)");
    _bam_check_template_list(template_list);
    return _scan_bam(ext, regions, keepFlags, isSimpleCigar,
                     tagFilter, mapqFilter, reverseComplement, yieldSize,
                     template_list, obeyQname, asMates, qnamePrefixEnd,
                     qnameSuffixStart);
}

SEXP count_bamfile(SEXP ext, SEXP regions, SEXP keepFlags, SEXP isSimpleCigar,
                   SEXP tagFilter, SEXP mapqFilter)
{
    _checkext(ext, BAMFILE_TAG, "countBam");
    _checkparams(regions, keepFlags, isSimpleCigar);
    SEXP count = _count_bam(ext, regions, keepFlags, isSimpleCigar, tagFilter,
                            mapqFilter);
    if (R_NilValue == count)
        Rf_error("'countBam' failed");
    return count;
}

SEXP prefilter_bamfile(SEXP ext, SEXP regions, SEXP keepFlags,
                       SEXP isSimpleCigar, SEXP tagFilter, SEXP mapqFilter,
                       SEXP yieldSize, SEXP obeyQname, SEXP asMates,
                       SEXP qnamePrefixEnd, SEXP qnameSuffixStart)
{
    _checkext(ext, BAMFILE_TAG, "filterBam");
    _checkparams(regions, keepFlags, isSimpleCigar);
    if (!(IS_INTEGER(yieldSize) && (1L == LENGTH(yieldSize))))
        Rf_error("'yieldSize' must be integer(1)");
    if (!(IS_LOGICAL(obeyQname) && (1L == LENGTH(obeyQname))))
        Rf_error("'obeyQname' must be logical(1)");
    if (!(IS_LOGICAL(asMates) && (1L == LENGTH(asMates))))
        Rf_error("'asMates' must be logical(1)");
    SEXP result =
        _prefilter_bam(ext, regions, keepFlags, isSimpleCigar, tagFilter,
                       mapqFilter, yieldSize, obeyQname, asMates,
                       qnamePrefixEnd, qnameSuffixStart);
    if (R_NilValue == result)
        Rf_error("'filterBam' failed during pre-filtering");
    return result;
}

SEXP filter_bamfile(SEXP ext, SEXP regions, SEXP keepFlags, SEXP isSimpleCigar,
                    SEXP tagFilter, SEXP mapqFilter,
                    SEXP ext_out)
{
    _checkext(ext, BAMFILE_TAG, "filterBam");
    _checkext(ext_out, BAMFILE_TAG, "filterBam");
    _checkparams(regions, keepFlags, isSimpleCigar);
    SEXP result = _filter_bam(ext, regions, keepFlags, isSimpleCigar,
                              tagFilter, mapqFilter,
                              ext_out);
    if (R_NilValue == result)
        Rf_error("'filterBam' failed");
    return result;
}

