/* gxm_instance.h -- sceGxmDrawInstanced as ordinary draws (NEW4-FW FW-20 2026-11-13).
 *
 * sceGxmDrawInstanced(context, primitive, index format, indices, count, indexWrap) draws count / indexWrap instances of
 * indexWrap indices each: the index counter wraps to the start of the index list after indexWrap indices and the
 * instance number goes up by one. A vertex stream whose SceGxmVertexStream.indexSource (the program's stream entry
 * {u16 stride, u16 indexSource}) is SCE_GXM_INDEX_SOURCE_INSTANCE_16BIT / _32BIT (2 / 3) is read with the instance
 * number, every other stream with the vertex index. Drawn as `instances` ordinary draws of `indices` indices each, the
 * instance streams based at their record for that instance with stride 0 (every vertex reads the one record). A wrap of
 * 0 or at least `count` is the whole list once, as before. */
#pragma once
#include <stdint.h>

static inline int gxm_index_source_is_instance(uint16_t src) { return src == 2 || src == 3; }
static inline uint32_t gxm_instance_count(uint32_t count, uint32_t wrap) { return wrap && wrap < count ? count / wrap : 1; }
static inline uint32_t gxm_instance_indices(uint32_t count, uint32_t wrap) { return wrap && wrap < count ? wrap : count; }
static inline uint32_t gxm_instance_stream(uint32_t base, uint16_t stride, uint32_t k) { return base + k * (uint32_t)stride; }
