/* texpack.c: texture packs made for Vita3K (textures/import/<title>/<hash>.dds|png), by the same hash */
#ifndef RECOMP_TEXPACK_H
#define RECOMP_TEXPACK_H
#include <stdint.h>
#include "gfx.h"
uint64_t texpack_hash(const GfxTexture *t);   /* Vita3K's hash_texture_nostride: the first level's used bytes, size, swizzle, palette */
int texpack_on(void);                          /* RECOMP_TEXTURE_PACK names a folder (searched at any depth) */
const char *texpack_find(uint64_t hash);       /* the replacement's path (.dds preferred over .png), 0 none */
#endif
