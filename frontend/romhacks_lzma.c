/* LZMA2 for xdelta's compressed sections (romhacks.h), on the LZMA SDK (public domain, as
   mGBA carries it). */
#include <stdlib.h>
#include "Lzma2Dec.h"

static void* RhAlloc(ISzAllocPtr p, size_t size) { (void)p; return malloc(size); }
static void RhFree(ISzAllocPtr p, void* address) { (void)p; free(address); }
static const ISzAlloc kRhAlloc = { RhAlloc, RhFree };

void* romhacks_lzma2_open(unsigned char prop)
{
    CLzma2Dec* dec = (CLzma2Dec*)malloc(sizeof(CLzma2Dec));
    if (!dec) return NULL;
    Lzma2Dec_Construct(dec);
    if (Lzma2Dec_Allocate(dec, prop, &kRhAlloc) != SZ_OK)
    {
        free(dec);
        return NULL;
    }
    Lzma2Dec_Init(dec);
    return dec;
}

int romhacks_lzma2_next(void* stream, const unsigned char* in, size_t inSize, unsigned char* out, size_t outSize)
{
    CLzma2Dec* dec = (CLzma2Dec*)stream;
    SizeT done = 0;
    while (done < outSize)
    {
        SizeT outLen = outSize - done, inLen = inSize;
        ELzmaStatus status;
        if (Lzma2Dec_DecodeToBuf(dec, out + done, &outLen, in, &inLen, LZMA_FINISH_ANY, &status) != SZ_OK) return 0;
        done += outLen;
        in += inLen;
        inSize -= inLen;
        if (outLen == 0 && inLen == 0) break;
    }
    return done == outSize;
}

void romhacks_lzma2_close(void* stream)
{
    if (!stream) return;
    Lzma2Dec_Free((CLzma2Dec*)stream, &kRhAlloc);
    free(stream);
}
