/* SPDX-License-Identifier: GPL-3.0-only */
/* Finton renderer: one full 240 x 240 frame in the pool, drawn from scratch each frame, then only
 * the rows that changed go to the panel (a hash per row; runs of changed rows as one DMA each).
 * Pixels are stored byte-swapped, as the ST7789 takes RGB565. Main loop only.
 *
 * The look (DESIGN.md) is the Matrix's: phosphor green on black, the white-hot head of the code for what
 * is selected and what sounds now, M PLUS 1 Code throughout (antialiased, 4-bit), mirrored katakana rain. */

#define W 240
#define H 240
static uint16_t scr[W * H] POOL;
static uint32_t row_hash[H];
static uint32_t frame_rows;                     /* rows sent last frame (for the CPU readout) */

#define C565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
/* the palette, as plain RGB565 (sw() swaps on store): the Matrix's phosphor, green on black */
#define K_BG C565(0, 6, 2)                      /* the ground: black, the faintest green */
#define K_PANEL C565(0, 16, 6)                  /* panels, every other lane */
#define K_KEY C565(0, 30, 12)                   /* tiles, the selected lane */
#define K_N1 C565(0, 40, 14)                    /* hairlines */
#define K_N2 C565(0, 72, 24)                    /* inactive, borders, old code */
#define K_N3 C565(0, 128, 40)                   /* labels */
#define K_N4 C565(0, 190, 60)                   /* secondary */
#define K_N5 C565(0, 255, 65)                   /* values: #00FF41 */
#define K_ACC C565(200, 255, 210)               /* the white-hot head of the code: selected, sounding now */
#define K_ACC_SOFT C565(0, 60, 20)
#define K_ACC_FG C565(0, 10, 3)                 /* type on the accent */
#define K_RED C565(255, 40, 40)                 /* the red pill: REC only */

static inline uint16_t sw(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

/* mix two RGB565 colours, a = 0..256 of c1 */
static inline uint32_t mix565(uint32_t c0, uint32_t c1, uint32_t a)
{
    uint32_t r = ((c0 >> 11) * (256u - a) + (c1 >> 11) * a) >> 8;
    uint32_t g = (((c0 >> 5) & 63u) * (256u - a) + ((c1 >> 5) & 63u) * a) >> 8;
    uint32_t b = ((c0 & 31u) * (256u - a) + (c1 & 31u) * a) >> 8;
    return r << 11 | g << 5 | b;
}
/* the colour at a share of full brightness (0..256), toward the background */
static inline uint32_t dim565(uint32_t c, uint32_t a) { return mix565(K_BG, c, a > 256u ? 256u : a); }

static void r_clear(uint32_t c)
{
    uint32_t i, v = sw(c) | (uint32_t)sw(c) << 16, *p = (uint32_t *)scr;
    for (i = 0; i < W * H / 2u; i++) p[i] = v;
}

static inline void r_px(int32_t x, int32_t y, uint32_t c)
{
    if ((uint32_t)x < W && (uint32_t)y < H) scr[y * W + x] = sw(c);
}
/* blend c over the pixel, a = 0..256 */
static inline void r_blend(int32_t x, int32_t y, uint32_t c, uint32_t a)
{
    uint16_t *p;
    if ((uint32_t)x >= W || (uint32_t)y >= H || !a) return;
    p = &scr[y * W + x];
    *p = sw(mix565(sw(*p), c, a));
}

static void r_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c)
{
    int32_t i, j, x1 = x + w, y1 = y + h;
    uint16_t s = sw(c);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (j = y; j < y1; j++)
        for (i = x; i < x1; i++) scr[j * W + i] = s;
}
static void r_hline(int32_t x0, int32_t x1, int32_t y, uint32_t c) { if (x1 < x0) { int32_t t = x0; x0 = x1; x1 = t; } r_fill(x0, y, x1 - x0 + 1, 1, c); }
static void r_vline(int32_t x, int32_t y0, int32_t y1, uint32_t c) { if (y1 < y0) { int32_t t = y0; y0 = y1; y1 = t; } r_fill(x, y0, 1, y1 - y0 + 1, c); }

/* an antialiased line (Xiaolin Wu) between points in 1/16 pixel; a = 0..256 overall strength */
static void r_line16(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c, uint32_t a)
{
    int32_t dx = x1 - x0, dy = y1 - y0, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy, n, i;
    int32_t steep = ady > adx, grad, pos, t;
    if (steep) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; t = dx; dx = dy; dy = t; }
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dx = -dx; dy = -dy; }
    if (dx == 0) { if (!steep) r_blend(x0 >> 4, y0 >> 4, c, a); else r_blend(y0 >> 4, x0 >> 4, c, a); return; }
    grad = (dy << 16) / dx;                     /* Q16 */
    n = (x1 >> 4) - (x0 >> 4);
    pos = (y0 << 12) + (int32_t)((((int64_t)grad) * (((x0 >> 4) << 4) + 8 - x0)) >> 4);   /* Q16 y at the first pixel centre */
    for (i = 0; i <= n; i++) {
        int32_t px = (x0 >> 4) + i, py = pos >> 16;
        uint32_t f = (uint32_t)(pos & 0xFFFF) >> 8, a0 = ((256u - f) * a) >> 8, a1 = (f * a) >> 8;
        if (!steep) { r_blend(px, py, c, a0); r_blend(px, py + 1, c, a1); }
        else { r_blend(py, px, c, a0); r_blend(py + 1, px, c, a1); }
        pos += grad;
    }
}
static void r_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c, uint32_t a)
{
    r_line16(x0 << 4, y0 << 4, x1 << 4, y1 << 4, c, a);
}

/* a convex polygon (n <= 6 points in 1/16 px), filled; a = 0..256 blend over what is there */
static void r_poly(const int32_t *px, const int32_t *py, uint32_t n, uint32_t c, uint32_t a)
{
    int32_t y0 = 1 << 30, y1 = -(1 << 30), y, i;
    uint16_t sc = sw(c);
    for (i = 0; i < (int32_t)n; i++) { if (py[i] < y0) y0 = py[i]; if (py[i] > y1) y1 = py[i]; }
    y0 = (y0 + 8) >> 4; y1 = (y1 - 8) >> 4;
    if (y0 < 0) y0 = 0;
    if (y1 > H - 1) y1 = H - 1;
    for (y = y0; y <= y1; y++) {
        int32_t yc = y * 16 + 8, xl = 1 << 30, xr = -(1 << 30), x;
        for (i = 0; i < (int32_t)n; i++) {
            int32_t j = i + 1 == (int32_t)n ? 0 : i + 1, ya = py[i], yb = py[j], xx;
            if ((yc < ya) == (yc < yb)) continue;      /* the scanline misses this edge */
            xx = px[i] + (px[j] - px[i]) * (yc - ya) / (yb - ya);   /* 32-bit: points stay within a few screens */
            if (xx < xl) xl = xx;
            if (xx > xr) xr = xx;
        }
        xl = (xl + 8) >> 4; xr = (xr - 8) >> 4;
        if (xl < 0) xl = 0;
        if (xr > W - 1) xr = W - 1;
        if (a >= 256u) for (x = xl; x <= xr; x++) scr[y * W + x] = sc;
        else for (x = xl; x <= xr; x++) scr[y * W + x] = sw(mix565(sw(scr[y * W + x]), c, a));
    }
}

/* text in an Azeret Mono cut (4-bit alpha, blended over what is there); returns the x after it */
static int32_t r_text(const apex_font_t *f, int32_t x, int32_t y, const char *s, uint32_t c)
{
    uint16_t sc = sw(c);
    for (; *s; s++, x += f->w) {
        uint32_t ch = (uint8_t)*s, r, col;
        const uint8_t *g;
        if (ch < 32u || ch > 126u) ch = '?';
        g = f->data + (ch - 32u) * f->h * f->bpr;
        if (x >= W || x + f->w <= 0) continue;
        for (r = 0; r < f->h; r++) {
            int32_t yy = y + (int32_t)r;
            if ((uint32_t)yy >= H) continue;
            for (col = 0; col < f->w; col++) {
                uint32_t a = g[r * f->bpr + col / 2u];
                int32_t xx = x + (int32_t)col;
                a = (col & 1u) ? a & 15u : a >> 4;
                if (!a || (uint32_t)xx >= W) continue;
                if (a == 15u) scr[yy * W + xx] = sc;
                else scr[yy * W + xx] = sw(mix565(sw(scr[yy * W + xx]), c, a * 17u));
            }
        }
    }
    return x;
}
static int32_t r_textw(const apex_font_t *f, const char *s) { int32_t n = 0; while (*s++) n++; return n * f->w; }
static int32_t r_text_r(const apex_font_t *f, int32_t xr, int32_t y, const char *s, uint32_t c) { return r_text(f, xr - r_textw(f, s), y, s, c); }

/* small integer to text */
static char *r_int(char *b, int32_t v, uint32_t width)
{
    char t[12];
    uint32_t n = 0, i = 0, neg = v < 0;
    if (neg) v = -v;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11u);
    if (neg) b[i++] = '-';
    while (n < width) { b[i++] = ' '; width--; }
    while (n) b[i++] = t[--n];
    b[i] = 0;
    return b;
}

/* any simple polygon (concave too: even-odd scanlines), n <= 128 points in 1/16 px, blended at a */
static void r_shape(const int32_t *px, const int32_t *py, uint32_t n, uint32_t c, uint32_t a)
{
    int32_t y0 = 1 << 30, y1 = -(1 << 30), y, xs[32];
    uint32_t i;
    for (i = 0; i < n; i++) { if (py[i] < y0) y0 = py[i]; if (py[i] > y1) y1 = py[i]; }
    y0 = clampi((y0 + 8) >> 4, 0, H - 1); y1 = clampi((y1 - 8) >> 4, 0, H - 1);
    for (y = y0; y <= y1; y++) {
        int32_t yc = y * 16 + 8, m = 0, k, q;
        for (i = 0; i < n && m < 32; i++) {        /* where this scanline crosses the edges */
            uint32_t j = i + 1u == n ? 0u : i + 1u;
            int32_t ya = py[i], yb = py[j];
            if ((yc < ya) == (yc < yb)) continue;
            xs[m++] = px[i] + (px[j] - px[i]) * (yc - ya) / (yb - ya);
        }
        for (k = 1; k < m; k++)                    /* sort them (a handful) */
            for (q = k; q > 0 && xs[q - 1] > xs[q]; q--) { int32_t tmp = xs[q]; xs[q] = xs[q - 1]; xs[q - 1] = tmp; }
        for (k = 0; k + 1 < m; k += 2) {           /* fill inside, pair by pair */
            int32_t xa = clampi((xs[k] + 8) >> 4, 0, W), xb = clampi((xs[k + 1] - 8) >> 4, -1, W - 1), x;
            for (x = xa; x <= xb; x++) scr[y * W + x] = sw(mix565(sw(scr[y * W + x]), c, a));
        }
    }
}

/* text with a one-pixel dark shadow, legible over the glow */
static int32_t r_text_sh(const apex_font_t *f, int32_t x, int32_t y, const char *s, uint32_t c)
{
    r_text(f, x + 1, y + 1, s, K_BG);
    return r_text(f, x, y, s, c);
}

/* phosphor: rows y0..y1 keep k/256 of their light (the scene's afterglow between frames) */
static void r_fade(int32_t y0, int32_t y1, uint32_t k)
{
    uint32_t i, n = (uint32_t)(y1 - y0 + 1) * W;
    uint16_t *p = &scr[y0 * W];
    for (i = 0; i < n; i++) {
        uint32_t c = sw(p[i]);
        if (!c) continue;
        c = (((c >> 11) * k >> 8) << 11) | ((((c >> 5) & 63u) * k >> 8) << 5) | ((c & 31u) * k >> 8);
        p[i] = sw(c);
    }
}

/* a disc at x, y (1/16 px), radius r (1/16 px), antialiased at its edge */
static void r_disc(int32_t x, int32_t y, int32_t r, uint32_t c, uint32_t a)
{
    int32_t px, py, x0 = (x - r - 16) >> 4, x1 = (x + r + 16) >> 4, y0 = (y - r - 16) >> 4, y1 = (y + r + 16) >> 4;
    int32_t ro = (r + 8) * (r + 8), ri = r > 8 ? (r - 8) * (r - 8) : 0;
    for (py = y0; py <= y1; py++)
        for (px = x0; px <= x1; px++) {
            int32_t dx = px * 16 + 8 - x, dy = py * 16 + 8 - y, d2 = dx * dx + dy * dy;
            if (d2 >= ro) continue;
            r_blend(px, py, c, d2 <= ri ? a : (uint32_t)((int64_t)a * (ro - d2) / (ro - ri)));
        }
}

/* a line with a glow: a soft halo either side, then the crisp core */
static void r_glow16(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c, uint32_t a)
{
    int32_t dx = x1 - x0, dy = y1 - y0, ox = (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx) ? 16 : 0, oy = 16 - ox;
    r_line16(x0 + ox, y0 + oy, x1 + ox, y1 + oy, c, a / 4u);
    r_line16(x0 - ox, y0 - oy, x1 - ox, y1 - oy, c, a / 4u);
    r_line16(x0, y0, x1, y1, c, a);
}

/* glyph g of the rain (F_RAIN) at x, y in colour c, a = 0..256 */
static void r_glyph(uint32_t g, int32_t x, int32_t y, uint32_t c, uint32_t a)
{
    const uint8_t *d = F_RAIN.data + (g % NRAIN) * 48u;
    uint32_t r, col;
    if (x <= -8 || x >= W || y <= -12 || y >= H || !a) return;
    for (r = 0; r < 12u; r++) {
        int32_t yy = y + (int32_t)r;
        if ((uint32_t)yy >= H) continue;
        for (col = 0; col < 8u; col++) {
            uint32_t v = d[r * 4u + col / 2u];
            int32_t xx = x + (int32_t)col;
            v = (col & 1u) ? v & 15u : v >> 4;
            if (!v || (uint32_t)xx >= W) continue;
            v = v * 17u * a >> 8;
            scr[yy * W + xx] = sw(mix565(sw(scr[yy * W + xx]), c, v));
        }
    }
}

/* an RGB565 image (plain, not swapped) at x, y */
static void r_image(int32_t x, int32_t y, const uint16_t *px, int32_t w, int32_t h)
{
    int32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if ((uint32_t)(x + i) < W && (uint32_t)(y + j) < H) scr[(y + j) * W + x + i] = sw(px[j * w + i]);
}

/* Rollwerk's tile: a 1 px border with the corners cut (its 5 px radius at this size), filled */
static void r_tile(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t fill, uint32_t border)
{
    r_fill(x + 1, y + 1, w - 2, h - 2, fill);
    r_hline(x + 1, x + w - 2, y, border);
    r_hline(x + 1, x + w - 2, y + h - 1, border);
    r_vline(x, y + 1, y + h - 2, border);
    r_vline(x + w - 1, y + 1, y + h - 2, border);
}

/* send the rows that changed */
static void r_flush(void)
{
    uint32_t y = 0, sent = 0;
    while (y < H) {
        uint32_t h, x, y0;
        const uint32_t *p = (const uint32_t *)&scr[y * W];
        for (h = 0x811C9DC5u, x = 0; x < W / 2u; x++) h = (h ^ p[x]) * 16777619u;
        if (h == row_hash[y]) { y++; continue; }
        row_hash[y] = h;
        y0 = y++;
        while (y < H) {                         /* the run of changed rows */
            p = (const uint32_t *)&scr[y * W];
            for (h = 0x811C9DC5u, x = 0; x < W / 2u; x++) h = (h ^ p[x]) * 16777619u;
            if (h == row_hash[y]) break;
            row_hash[y] = h;
            y++;
        }
        lcd_blit(0, y0, W, y - y0, &scr[y0 * W]);
        sent += y - y0;
    }
    frame_rows = sent;
}
