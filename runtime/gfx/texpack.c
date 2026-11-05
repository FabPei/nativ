/* Texture packs made for Vita3K: a folder of <16 hex digits>.dds / .png, each replacing the game texture whose
 * hash is that number -- Vita3K's own (vita3k/renderer/src/texture/cache.cpp hash_texture_nostride, 2023-10):
 *   (width << 16 | height | (gamma != 0) << 32 | swizzle << 33) ^ XXH3_64(the first level's used bytes) [^ the palette's]
 * width and height rounded up to the format's block; "used bytes": every byte of the level when its layout leaves no
 * padding, else only the pixels inside the texture (rows of a strided linear one; the used part of each tile of a
 * tiled one; the fully-used power-of-two blocks of an arbitrary-swizzled one, in memory order). A pack made by dumping
 * from Vita3K and editing matches here byte for byte, since the guest memory hashed is the same.
 * RECOMP_TEXTURE_PACK=<folder>: the pack, searched at any depth (a pack sorted into a folder per character works). */
#define XXH_INLINE_ALL
#include "third_party/xxhash/xxhash.h"
#include "texpack.h"
#include "rt.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static uint32_t base_bpp(uint32_t base) {   /* Vita3K gxm::bits_per_pixel, by SceGxmTextureBaseFormat */
	switch (base) {
	case 0x00000000u: case 0x01000000u: return 8;
	case 0x02000000u: case 0x03000000u: case 0x04000000u: case 0x05000000u: case 0x06000000u: case 0x07000000u: case 0x08000000u:
	case 0x09000000u: case 0x0A000000u: case 0x0B000000u: return 16;
	case 0x0C000000u: case 0x0D000000u: case 0x0E000000u: case 0x0F000000u: case 0x10000000u: case 0x11000000u: case 0x12000000u:
	case 0x13000000u: case 0x14000000u: case 0x15000000u: case 0x17000000u: case 0x18000000u: case 0x19000000u: case 0x1A000000u: case 0x9A000000u: return 32;
	case 0x1B000000u: case 0x1C000000u: case 0x1D000000u: case 0x1E000000u: case 0x1F000000u: return 64;
	case 0x80000000u: case 0x82000000u: return 2;
	case 0x81000000u: case 0x83000000u: return 4;
	case 0x85000000u: case 0x88000000u: case 0x89000000u: return 4;
	case 0x86000000u: case 0x87000000u: case 0x8A000000u: case 0x8B000000u: return 8;
	case 0x90000000u: case 0x91000000u: return 12;
	case 0x92000000u: return 16;
	case 0x94000000u: return 4;
	case 0x95000000u: return 8;
	case 0x98000000u: case 0x99000000u: return 24;
	default: return 1;
	}
}
static void block_size(uint32_t base, uint32_t *bw, uint32_t *bh) {   /* Vita3K gxm::get_block_size */
	*bw = *bh = 1;
	if ((base >= 0x85000000u && base <= 0x8B000000u) || base == 0x81000000u || base == 0x83000000u || base == 0x94000000u) *bw = *bh = 4;
	else if (base == 0x80000000u || base == 0x82000000u) { *bw = 8; *bh = 4; }
	else if (base == 0x92000000u) { *bw = 2; *bh = 1; }
	else if (base == 0x90000000u || base == 0x91000000u) *bw = *bh = 2;
}
static uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }
static uint32_t next_pow2(uint32_t v) { uint32_t p = 1; while (p < v) p <<= 1; return p; }

static void hash_arbitrary_swizzled(const uint8_t *data, uint32_t width, uint32_t height, uint32_t tw, uint32_t th, uint32_t size, XXH3_state_t *st) {
	if (width >= tw && height >= th) { XXH3_64bits_update(st, data, size); return; }
	const uint32_t bw = tw / 2, bh = th / 2, bs = size / 4;
	hash_arbitrary_swizzled(data, width, height, bw, bh, bs, st);
	if (height > bh) hash_arbitrary_swizzled(data + bs, width, height - bh, bw, bh, bs, st);
	if (width > bw) hash_arbitrary_swizzled(data + 2 * bs, width - bw, height, bw, bh, bs, st);
	if (height > bh && width > bw) hash_arbitrary_swizzled(data + 3 * bs, width - bw, height - bh, bw, bh, bs, st);
}
static void hash_unaligned_tiled(const uint8_t *data, uint32_t width, uint32_t height, uint32_t block_width, uint32_t block_height, uint32_t bpp, XXH3_state_t *st) {
	(void)block_width;
	const uint32_t tile_mask = 0x1F, wd = width & ~31u, hd = height & ~31u;
	const uint32_t tile_block_lines = 32 / block_height, total_line_size = (32 * block_height * bpp) / 8;
	if (width == wd) { const uint32_t n = (width * hd * bpp) / 8; XXH3_64bits_update(st, data, n); data += n; }
	else {
		const uint32_t block_lines = hd / 32, filled = (wd * 32 * bpp) / 8, end_used = ((width & tile_mask) * block_height * bpp) / 8;
		for (uint32_t bl = 0; bl < block_lines; bl++) {
			XXH3_64bits_update(st, data, filled); data += filled;
			for (uint32_t t = 0; t < tile_block_lines; t++) { XXH3_64bits_update(st, data, end_used); data += total_line_size; }
		}
	}
	if (height == hd) return;
	const uint32_t tile_size = (32 * 32 * bpp) / 8, used_tile = (32 * (height & tile_mask) * bpp) / 8, ntx = wd / 32;
	for (uint32_t x = 0; x < ntx; x++) { XXH3_64bits_update(st, data, used_tile); data += tile_size; }
	if (width == wd) return;
	const uint32_t end_used = ((width & tile_mask) * block_height * bpp) / 8, nlines = (height & tile_mask) / block_height;
	for (uint32_t l = 0; l < nlines; l++) { XXH3_64bits_update(st, data, end_used); data += total_line_size; }
}

uint64_t texpack_hash(const GfxTexture *t) {
	if (!t->data) return 0;
	const uint32_t base = t->format & 0x9f000000u;
	uint32_t bw, bh; block_size(base, &bw, &bh);
	uint32_t width = align_up(t->width, bw), height = align_up(t->height, bh);
	uint64_t hash = (uint64_t)width << 16 | height | (uint64_t)(GFX_TEX_GAMMA(t->format) != 0) << 32 | (uint64_t)((t->format >> 12) & 7) << 33;
	if (base == 0x94000000u || base == 0x95000000u) {
		if (t->palette) hash ^= XXH3_64bits(G(t->palette), (base == 0x94000000u ? 16 : 256) * 4);
		if (base == 0x94000000u) { bw = 2; bh = 1; }
	}
	const uint8_t *data = (const uint8_t *)G(t->data);
	if (base >= 0x90000000u && base <= 0x92000000u) {   /* YUV: the first level whole (Vita3K texture_size_first_mip) */
		uint32_t n = base == 0x92000000u ? width * height * 2 : width * height * 3 / 2;
		return hash ^ XXH3_64bits(data, n);
	}
	const uint32_t bpp = base_bpp(base), layout = t->u_mode >> 4;
	const uint32_t stride_px_fmt = t->format & 0xfffu;
	int no_stride = 1, linear = 0; uint32_t stride = width;
	if (layout == 0) {   /* linear: LINEAR_STRIDED carries its stride in format bits 0-11 (gxm_sony.c), LINEAR rows of (w+7)&~7 */
		linear = 1;
		stride = stride_px_fmt ? stride_px_fmt : align_up(width, 8);
		no_stride = stride == width;
	} else if (layout == 2) { stride = align_up(width, 32); no_stride = !(width & 31) && !(height & 31); }
	else no_stride = !(width & (width - 1)) && !(height & (height - 1));   /* swizzled / cube: power-of-two sides */
	if (no_stride) return hash ^ XXH3_64bits(data, (size_t)width * height * bpp / 8);
	XXH3_state_t st; XXH3_64bits_reset(&st);
	if (linear) {
		const uint32_t row = (stride * bh * bpp) / 8, used = (width * bh * bpp) / 8;
		for (uint32_t y = 0; y < height / bh; y++) { XXH3_64bits_update(&st, data, used); data += row; }
	} else if (layout == 2) hash_unaligned_tiled(data, width, height, bw, bh, bpp, &st);
	else { const uint32_t tw = next_pow2(width), th = next_pow2(height); hash_arbitrary_swizzled(data, width, height, tw, th, (tw * th * bpp) / 8, &st); }
	return hash ^ XXH3_64bits_digest(&st);
}

/* the pack's index: hash -> path, sorted for a binary search */
typedef struct { uint64_t hash; char *path; int dds; } PackFile;
static PackFile *files; static int nfiles, capfiles, state;   /* state: 0 not scanned, 1 on, -1 off */
static int hexname(const char *n, uint64_t *h, int *dds) {
	const char *dot = strrchr(n, '.'); if (!dot || dot - n != 16) return 0;
	if (!strcasecmp(dot, ".dds")) *dds = 1; else if (!strcasecmp(dot, ".png")) *dds = 0; else return 0;
	uint64_t v = 0; for (int i = 0; i < 16; i++) { char c = n[i]; int d = c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1; if (d < 0) return 0; v = v << 4 | (uint64_t)d; }
	*h = v; return 1;
}
static void scan(const char *dir, int depth) {
	DIR *d = opendir(dir); if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[1200]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		struct stat sb; if (stat(p, &sb)) continue;
		if (S_ISDIR(sb.st_mode)) { if (depth < 6) scan(p, depth + 1); continue; }
		uint64_t h; int dds; if (!hexname(e->d_name, &h, &dds)) continue;
		if (nfiles == capfiles) { capfiles = capfiles ? 2 * capfiles : 1024; files = realloc(files, (size_t)capfiles * sizeof *files); }
		files[nfiles].hash = h; files[nfiles].path = strdup(p); files[nfiles].dds = dds; nfiles++;
	}
	closedir(d);
}
static int cmp(const void *a, const void *b) {
	const PackFile *x = a, *y = b;
	if (x->hash != y->hash) return x->hash < y->hash ? -1 : 1;
	return y->dds - x->dds;   /* (the .dds first: Vita3K prefers it) */
}
int texpack_on(void) {
	if (!state) {
		const char *dir = getenv("RECOMP_TEXTURE_PACK");
		state = -1;
		if (dir && *dir) {
			scan(dir, 0);
			if (nfiles) { qsort(files, (size_t)nfiles, sizeof *files, cmp); state = 1; }
			fprintf(stderr, "    texpack: %d replacement files in %s\n", nfiles, dir);
		}
	}
	return state > 0;
}
const char *texpack_find(uint64_t hash) {
	if (!texpack_on()) return 0;
	int lo = 0, hi = nfiles - 1;
	while (lo <= hi) {   /* the first entry of that hash: its .dds if it has one */
		int mid = (lo + hi) / 2;
		if (files[mid].hash < hash) lo = mid + 1; else hi = mid - 1;
	}
	return lo < nfiles && files[lo].hash == hash ? files[lo].path : 0;
}
