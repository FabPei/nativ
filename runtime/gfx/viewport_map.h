/* viewport_map.h -- SCE_GXM_VIEWPORT_DISABLED on a host GPU (VPDIS-01).
 *
 * With the viewport transform disabled, a vertex program's output position IS the device position: x/w and y/w are
 * target pixels (Toukiden Kiwami's KTGL sprites: F16 pixel coordinates passed straight through with w = 1). A host GPU
 * clips every primitive to |x|,|y| <= w BEFORE its viewport, so mapping DISABLED to a 2x2 viewport (-1,-1,2,2) left a
 * 0..960 quad cut to its first pixel -- the whole picture black. Instead such a draw gets the whole target as its
 * viewport and the vertex epilogue (usse_spirv.c emit_vert_epilogue) moves pixels into NDC itself:
 *     x' = x * ax - w        y' = y * ay + w        with ax = 2/W, ay = -2/H
 * (SDL_GPU's NDC +y is the top of the viewport). ax = 0 means "no mapping": every other draw is left as it was. */
#ifndef VIEWPORT_MAP_H
#define VIEWPORT_MAP_H
/* the two factors a DISABLED draw on a W x H target hands its vertex shader (UsseDrawUniforms.viewport_z[2..3]) */
static inline void vp_disabled_factors(float W, float H, float *ax, float *ay) {
	*ax = W > 0 ? 2.0f / W : 0.0f; *ay = H > 0 ? -2.0f / H : 0.0f;
}
/* the epilogue's arithmetic (what the SPIR-V computes): clip x, y from the program's x, y, w */
static inline void vp_disabled_clip(float x, float y, float w, float ax, float ay, float *ox, float *oy) {
	if (ax != 0.0f) { *ox = x * ax - w; *oy = y * ay + w; } else { *ox = x; *oy = y; }
}
/* where SDL_GPU puts a clip position on a viewport (vx, vy, vw, vh): device pixels (NDC +y = top) */
static inline void vp_device(float cx, float cy, float w, float vx, float vy, float vw, float vh, float *px, float *py) {
	*px = vx + (cx / w + 1.0f) * 0.5f * vw; *py = vy + (1.0f - cy / w) * 0.5f * vh;
}
#endif
