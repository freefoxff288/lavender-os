/* lavrender.h — палитра системы и стили фонов, общие для lavwall и lavshot. */
#pragma once
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"

/* ── цвета ─────────────────────────────────────────────── */

typedef struct { double r, g, b; } C;
static C hex(unsigned v) { return (C){((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0}; }
static C mix(C a, C b, double t) { return (C){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
static const C BLACK = {0, 0, 0}, WHITE = {1, 1, 1};

enum { A_PRIMARY, A_SAGE, A_LILAC, A_ROSE, A_SKY, A_MINT, NACC };
static const char *acc_ids[NACC] = {"lavender", "sage", "lilac", "rose", "sky", "mint"};
static const char *acc_names[NACC] = {"Лаванда", "Шалфей", "Сирень", "Роза", "Небо", "Мята"};
static const char *acc_keys[NACC] = {"primary", "secondary", "tertiary", "error", "blue", "cyan"};

typedef struct { C bg, surf, sel, fg, sub, muted, on_acc; C acc[NACC]; } Pal;
static Pal pal[2] = { /* запасная «Лаванда», если файл палитры не прочитался */
    {.bg = {0.094, 0.086, 0.114}},
    {.bg = {0.961, 0.953, 0.976}},
};

static void builtin_palette(void) {
    unsigned d[] = {0x18161d, 0x211e28, 0x3a3545, 0xe6e1ef, 0xb9b2c6, 0x6e6880, 0x2a1f45,
                    0xbba7e3, 0xa7cdb6, 0xd9b8dc, 0xe3a7b5, 0xa9b8e8, 0x9fd0cf};
    unsigned l[] = {0xf5f3f9, 0xece8f3, 0xd0c9dc, 0x2b2734, 0x5f586e, 0x77708a, 0xffffff,
                    0x7a65ad, 0x4f8a6a, 0x9a5f8f, 0xb0566b, 0x5468a8, 0x3f8584};
    for (int m = 0; m < 2; m++) {
        unsigned *v = m ? l : d;
        Pal *p = &pal[m];
        p->bg = hex(v[0]); p->surf = hex(v[1]); p->sel = hex(v[2]); p->fg = hex(v[3]);
        p->sub = hex(v[4]); p->muted = hex(v[5]); p->on_acc = hex(v[6]);
        for (int i = 0; i < NACC; i++) p->acc[i] = hex(v[7 + i]);
    }
}

static int jcolor(const char *from, const char *to, const char *key, C *out) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = from ? strstr(from, pat) : NULL;
    if (!p || (to && p > to)) return 0;
    p = strchr(p + strlen(pat), '#');
    unsigned v;
    if (!p || sscanf(p + 1, "%6x", &v) != 1) return 0;
    *out = hex(v);
    return 1;
}

static char *run_line(const char *cmd) {
    FILE *f = popen(cmd, "r");
    if (!f) return NULL;
    char buf[512] = "";
    char *ok = fgets(buf, sizeof buf, f);
    pclose(f);
    if (!ok) return NULL;
    buf[strcspn(buf, "\n")] = 0;
    return g_strdup(buf);
}

static char palette_name[128] = "Лаванда (встроенная)";

static void load_palette(void) {
    builtin_palette();
    char *scheme = run_line("noctalia msg color-scheme-get 2>/dev/null"); /* "custom Lavender" */
    if (!scheme) return;
    char *name = strchr(scheme, ' ');
    if (!name || strncmp(scheme, "custom", 6)) { g_free(scheme); return; }
    name++;
    char *path = g_strdup_printf("%s/noctalia/palettes/%s.json", g_get_user_config_dir(), name);
    char *json = NULL;
    if (g_file_get_contents(path, &json, NULL, NULL)) {
        const char *dark = strstr(json, "\"dark\""), *light = strstr(json, "\"light\"");
        for (int m = 0; m < 2; m++) {
            const char *a = m ? light : dark, *b = m ? NULL : light;
            if (!a) continue;
            Pal *p = &pal[m];
            jcolor(a, b, "surface", &p->bg);
            jcolor(a, b, "surface_container", &p->surf);
            jcolor(a, b, "outline_variant", &p->sel);
            jcolor(a, b, "on_surface", &p->fg);
            jcolor(a, b, "on_surface_variant", &p->sub);
            jcolor(a, b, "on_primary", &p->on_acc);
            jcolor(strstr(a, "\"bright\""), b, "black", &p->muted);
            for (int i = 0; i < NACC; i++) jcolor(a, b, acc_keys[i], &p->acc[i]);
        }
        snprintf(palette_name, sizeof palette_name, "%s", name);
    }
    g_free(json); g_free(path); g_free(scheme);
}

/* ── состояние отрисовки ───────────────────────────────── */

static int W, H, LIGHT;
static double S;
static unsigned rs = 7;
static const Pal *P;
static C A;

static double rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs & 0xFFFFFF) / (double)0x1000000; }
static double rr(double a, double b) { return a + (b - a) * rnd(); }
static void src(cairo_t *cr, C c, double a) { cairo_set_source_rgba(cr, c.r, c.g, c.b, a); }
static void stop(cairo_pattern_t *p, double o, C c, double a) { cairo_pattern_add_color_stop_rgba(p, o, c.r, c.g, c.b, a); }

static void vgrad(cairo_t *cr, double y0, double y1, C c0, C c1) {
    cairo_pattern_t *p = cairo_pattern_create_linear(0, y0, 0, y1);
    stop(p, 0, c0, 1); stop(p, 1, c1, 1);
    cairo_set_source(cr, p);
    cairo_pattern_destroy(p);
}
static void halo(cairo_t *cr, double x, double y, double r, C c, double a) {
    cairo_pattern_t *p = cairo_pattern_create_radial(x, y, 0, x, y, r);
    stop(p, 0, c, a); stop(p, 0.35, c, a * 0.42); stop(p, 1, c, 0);
    cairo_set_source(cr, p); cairo_paint(cr);
    cairo_pattern_destroy(p);
}
static void soft(cairo_t *cr, double x, double y, double r, C c, double a) {
    cairo_pattern_t *p = cairo_pattern_create_radial(x, y, 0, x, y, r);
    stop(p, 0, c, a); stop(p, 0.3, c, a * 0.75); stop(p, 0.6, c, a * 0.3); stop(p, 1, c, 0);
    cairo_set_source(cr, p); cairo_paint(cr);
    cairo_pattern_destroy(p);
}
static void stars(cairo_t *cr, int n, double ymax) {
    if (LIGHT) return;
    for (int i = 0; i < n; i++) {
        double x = rnd() * W, y = rnd() * rnd() * ymax, r = 0.6 + rnd() * 1.1;
        cairo_arc(cr, x, y, r * S, 0, 2 * M_PI);
        src(cr, P->fg, 0.10 + rnd() * 0.32);
        cairo_fill(cr);
    }
}
static C sky_top(void) { return LIGHT ? mix(P->bg, WHITE, 0.45) : mix(P->bg, BLACK, 0.25); }
static C haze(double k) { return mix(P->bg, A, k); }
static C deep(void) { return LIGHT ? mix(P->bg, mix(A, P->fg, 0.45), 0.55) : mix(P->bg, BLACK, 0.25); }

/* ── стили ─────────────────────────────────────────────── */

static void st_hills(cairo_t *cr, cairo_surface_t *sf) {
    (void)sf;
    double hz = H * rr(0.6, 0.7);
    vgrad(cr, 0, hz, sky_top(), haze(LIGHT ? 0.22 : 0.2));
    cairo_paint(cr);
    stars(cr, 70, H * 0.55);

    double mx = W * rr(0.18, 0.82), my = H * rr(0.2, 0.34), mr = 54 * S;
    halo(cr, mx, my, 330 * S, A, LIGHT ? 0.22 : 0.13);
    cairo_push_group(cr);
    cairo_arc(cr, mx, my, mr, 0, 2 * M_PI);
    src(cr, LIGHT ? mix(A, P->bg, 0.35) : mix(P->fg, A, 0.25), 1);
    cairo_fill(cr);
    if (!LIGHT) { /* месяц */
        cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
        cairo_arc(cr, mx + mr * 0.42, my - mr * 0.22, mr * 0.92, 0, 2 * M_PI);
        cairo_fill(cr);
    }
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 0.92);

    C far = haze(LIGHT ? 0.30 : 0.24), near = deep();
    for (int i = 0; i < 5; i++) { /* от дальних светлых гряд к ближним тёмным */
        double base = hz + (i - 0.6) * H * 0.075, amp = (34 + i * 20) * S;
        double p1 = rr(0, 6.28), p2 = rr(0, 6.28), p3 = rr(0, 6.28);
        double l1 = rr(560, 820) * S, l2 = rr(230, 350) * S, l3 = rr(95, 135) * S;
        cairo_move_to(cr, 0, H);
        for (double x = 0; x <= W + 4; x += 4 * S + 0.5)
            cairo_line_to(cr, x, base + amp * sin(x / l1 + p1) + amp * 0.42 * sin(x / l2 + p2) + amp * 0.10 * sin(x / l3 + p3));
        cairo_line_to(cr, W, H);
        cairo_close_path(cr);
        C c = mix(far, near, i / 4.0);
        vgrad(cr, base - amp * 1.5, base + H * 0.2, c, mix(c, near, 0.35));
        cairo_fill(cr);
    }
}

static void st_sunset(cairo_t *cr, cairo_surface_t *sf) {
    (void)sf;
    double hz = H * rr(0.6, 0.68), cx = W * rr(0.3, 0.7), r = rr(200, 260) * S;
    vgrad(cr, 0, hz, sky_top(), haze(LIGHT ? 0.2 : 0.16));
    cairo_paint(cr);
    stars(cr, 45, H * 0.45);
    halo(cr, cx, hz, 620 * S, A, LIGHT ? 0.2 : 0.12);

    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, W, hz);
    cairo_clip(cr);
    cairo_push_group(cr);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, hz - r, 0, hz);
    stop(p, 0, mix(A, LIGHT ? P->bg : P->fg, 0.35), 1);
    stop(p, 1, mix(A, LIGHT ? P->fg : P->bg, 0.25), 1);
    cairo_set_source(cr, p);
    cairo_arc(cr, cx, hz, r, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT); /* полосы на солнце */
    int n = 5 + (int)(rnd() * 3);
    for (int i = 0; i < n; i++) {
        double y = hz - r * (0.06 + 0.115 * i), h = (11 - i * 1.5) * S;
        if (h > 0.5 * S) { cairo_rectangle(cr, cx - r, y - h, 2 * r, h); cairo_fill(cr); }
    }
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 0.82);
    cairo_restore(cr);

    cairo_rectangle(cr, 0, hz, W, H - hz);
    vgrad(cr, hz, H, haze(LIGHT ? 0.14 : 0.1), deep());
    cairo_fill(cr);
    for (int i = 0; i < 9; i++) { /* отражение */
        double t = i / 8.0, y = hz + (10 + 300 * t * t) * S, w = r * (0.95 - 0.55 * t), h = (2.2 + 3.5 * t) * S;
        cairo_rectangle(cr, cx - w, y, 2 * w, h);
        src(cr, A, (LIGHT ? 0.4 : 0.26) * (1 - t) + 0.03);
        cairo_fill(cr);
    }
    cairo_rectangle(cr, 0, hz - 0.8 * S, W, 1.6 * S);
    src(cr, A, 0.22);
    cairo_fill(cr);
}

static void st_orbits(cairo_t *cr, cairo_surface_t *sf) {
    (void)sf;
    vgrad(cr, 0, H, LIGHT ? mix(P->bg, WHITE, 0.3) : mix(P->bg, BLACK, 0.12), haze(0.06));
    cairo_paint(cr);
    stars(cr, 40, H * 0.9);
    int right = rnd() < 0.6;
    double cx = W * rr(0.68, 0.86), cy = H * rr(1.0, 1.12);
    if (!right) cx = W - cx;
    halo(cr, cx, cy, 1150 * S, A, LIGHT ? 0.2 : 0.11);
    for (int i = 0; i < 11; i++) {
        cairo_arc(cr, cx, cy, (190 + i * 104) * S, 0, 2 * M_PI);
        cairo_set_line_width(cr, 1.6 * S);
        src(cr, A, (LIGHT ? 0.55 : 0.34) * pow(0.8, i));
        cairo_stroke(cr);
    }
    cairo_arc(cr, cx, cy, 190 * S, 0, 2 * M_PI);
    src(cr, A, 0.16);
    cairo_fill(cr);
    C col[4] = {P->acc[A_SAGE], mix(A, P->fg, 0.3), P->acc[A_ROSE], LIGHT ? P->sub : P->fg};
    int ring[4] = {2, 4, 5, 7};
    double rad[4] = {7, 5, 4, 3};
    for (int i = 0; i < 4; i++) {
        double an = right ? rr(3.4, 4.5) : rr(4.95, 6.0);
        double r = (190 + ring[i] * 104) * S, x = cx + cos(an) * r, y = cy + sin(an) * r;
        halo(cr, x, y, 60 * S, col[i], 0.16);
        cairo_arc(cr, x, y, rad[i] * S, 0, 2 * M_PI);
        src(cr, col[i], 0.85);
        cairo_fill(cr);
    }
}

static void st_silk(cairo_t *cr, cairo_surface_t *sf) {
    (void)sf;
    vgrad(cr, 0, H, sky_top(), haze(LIGHT ? 0.12 : 0.08));
    cairo_paint(cr);
    double cy = H * rr(0.5, 0.66);
    halo(cr, W * rr(0.3, 0.7), cy, 950 * S, A, LIGHT ? 0.16 : 0.10);
    double p1 = rr(0, 6.28), p2 = rr(0, 6.28), twist = rr(0.6, 1.4), spread = rr(0.3, 0.46);
    double l1 = rr(480, 760) * S, l2 = rr(210, 330) * S;
    int n = 28;
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int i = 0; i < n; i++) {
        double t = i / (n - 1.0), base = cy + (t - 0.5) * H * spread, amp = (70 + 40 * sin(t * 3.1)) * S;
        cairo_new_path(cr);
        for (double x = -10 * S; x <= W + 10 * S; x += 6 * S + 0.5)
            cairo_line_to(cr, x, base + amp * sin(x / l1 + p1 + t * twist) + amp * 0.35 * sin(x / l2 + p2 - t * 2.2));
        cairo_set_line_width(cr, fmax(0.5, (0.8 + 0.9 * sin(t * M_PI)) * S));
        src(cr, mix(A, LIGHT ? P->fg : WHITE, 0.15 * (1 - t)), (LIGHT ? 0.6 : 0.55) * (0.12 + 0.88 * sin(t * M_PI)));
        cairo_stroke(cr);
    }
}

static void st_aurora(cairo_t *cr, cairo_surface_t *sf) {
    (void)sf;
    src(cr, LIGHT ? mix(P->bg, WHITE, 0.25) : mix(P->bg, BLACK, 0.12), 1);
    cairo_paint(cr);
    C lil = P->acc[A_LILAC], prim = P->acc[A_PRIMARY];
    C cs[5] = {A, mix(A, prim, 0.5), mix(P->acc[A_SKY], prim, 0.4), lil, mix(A, lil, 0.5)};
    for (int i = 0; i < 5; i++)
        soft(cr, W * rr(-0.1, 1.1), H * rr(-0.1, 1.1), W * rr(0.35, 0.7), cs[i], LIGHT ? rr(0.22, 0.36) : rr(0.2, 0.32));
    if (!LIGHT) { /* мягкая виньетка */
        cairo_pattern_t *p = cairo_pattern_create_radial(W / 2.0, H / 2.0, H * 0.3, W / 2.0, H / 2.0, W * 0.75);
        stop(p, 0, BLACK, 0); stop(p, 1, BLACK, 0.25);
        cairo_set_source(cr, p); cairo_paint(cr);
        cairo_pattern_destroy(p);
    }
}

static void st_dots(cairo_t *cr, cairo_surface_t *sf) {
    (void)sf;
    vgrad(cr, 0, H, sky_top(), haze(LIGHT ? 0.08 : 0.05));
    cairo_paint(cr);
    double gx = W * rr(0.65, 1.0), gy = H * rr(0.6, 1.0), reach = W * rr(0.55, 0.85);
    if (rnd() < 0.5) gx = W - gx;
    halo(cr, gx, gy, 750 * S, A, LIGHT ? 0.16 : 0.09);
    double sp = 24 * S, maxr = sp * 0.33;
    int row = 0;
    for (double y = sp / 2; y < H + sp; y += sp * 0.866, row++)
        for (double x = (row & 1) ? sp / 2 : 0; x < W + sp; x += sp) {
            double k = 1 - hypot(x - gx, y - gy) / reach;
            if (k <= 0) continue;
            k = pow(k, 1.5);
            double r = maxr * k;
            if (r < 0.35 * S) continue;
            cairo_arc(cr, x, y, r, 0, 2 * M_PI);
            src(cr, A, (LIGHT ? 0.35 : 0.22) + 0.45 * k);
            cairo_fill(cr);
        }
}

static void st_topo(cairo_t *cr, cairo_surface_t *sf) {
    vgrad(cr, 0, H, sky_top(), haze(LIGHT ? 0.08 : 0.06));
    cairo_paint(cr);
    double bx[8], by[8], ba[8], br[8], asp = (double)W / H;
    for (int i = 0; i < 8; i++) { bx[i] = rr(-0.1, asp + 0.1); by[i] = rr(-0.1, 1.1); ba[i] = rr(-0.7, 1.0); br[i] = rr(0.16, 0.42); }
    double p1 = rr(0, 6.28), p2 = rr(0, 6.28), L = rr(12, 17);
    double hx = W * rr(0.2, 0.8), hy = H * rr(0.2, 0.8);
    float *f = g_malloc((size_t)W * H * sizeof(float));
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            double nx = x / (double)H, ny = y / (double)H, v = 0.12 * sin(nx * 3.1 + p1) * cos(ny * 2.7 + p2);
            for (int i = 0; i < 8; i++) {
                double dx = nx - bx[i], dy = ny - by[i];
                v += ba[i] * exp(-(dx * dx + dy * dy) / (br[i] * br[i]));
            }
            f[(size_t)y * W + x] = (float)(v * L);
        }
    cairo_surface_flush(sf);
    unsigned char *d = cairo_image_surface_get_data(sf);
    int stride = cairo_image_surface_get_stride(sf);
    double hw0 = fmax(S, 0.45);
    for (int y = 0; y < H - 1; y++)
        for (int x = 0; x < W - 1; x++) {
            size_t i = (size_t)y * W + x;
            double v = f[i], gx = f[i + 1] - v, gy = f[i + W] - v, g = hypot(gx, gy) + 1e-6;
            double fr = v - floor(v), dist = fmin(fr, 1 - fr) / g;
            int lvl = (int)floor(v + 0.5), major = ((lvl % 5) + 5) % 5 == 0;
            double hw = (major ? 1.1 : 0.6) * hw0, cov = hw + 0.5 - dist;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            double fade = 0.55 + 0.45 * exp(-(pow(x - hx, 2) + pow(y - hy, 2)) / pow(W * 0.45, 2));
            double a = cov * fade * (LIGHT ? 0.42 : 0.30) * (major ? 1.7 : 1.0);
            unsigned char *p = d + y * stride + x * 4;
            p[2] = (unsigned char)(p[2] + (A.r * 255 - p[2]) * a);
            p[1] = (unsigned char)(p[1] + (A.g * 255 - p[1]) * a);
            p[0] = (unsigned char)(p[0] + (A.b * 255 - p[0]) * a);
        }
    g_free(f);
    cairo_surface_mark_dirty(sf);
    halo(cr, hx, hy, 800 * S, A, LIGHT ? 0.10 : 0.07);
}

static void moon(cairo_t *cr, double cx, double cy, double r, double t, C lit) {
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    src(cr, mix(P->bg, P->fg, LIGHT ? 0.10 : 0.08), 1);
    cairo_fill(cr);
    double tt = t <= 0.5 ? t : 1 - t, sg = t <= 0.5 ? 1 : -1, k = cos(2 * M_PI * tt);
    cairo_new_path(cr);
    for (int j = 0; j <= 48; j++) { /* освещённый край */
        double th = -M_PI / 2 + M_PI * j / 48.0;
        cairo_line_to(cr, cx + sg * r * cos(th), cy + r * sin(th));
    }
    for (int j = 0; j <= 48; j++) { /* терминатор */
        double yy = r - 2 * r * j / 48.0;
        cairo_line_to(cr, cx + sg * k * sqrt(fmax(0, r * r - yy * yy)), cy + yy);
    }
    cairo_close_path(cr);
    src(cr, lit, 1);
    cairo_fill(cr);
}

static void st_phases(cairo_t *cr, cairo_surface_t *sf) {
    (void)sf;
    vgrad(cr, 0, H, sky_top(), haze(LIGHT ? 0.12 : 0.1));
    cairo_paint(cr);
    stars(cr, 90, H);
    double cy = H * rr(0.4, 0.58), cx = W * rr(0.38, 0.62), sp = rr(165, 200) * S, r = 34 * S;
    C lit = LIGHT ? mix(A, P->fg, 0.25) : mix(P->fg, A, 0.4);
    halo(cr, cx, cy, 380 * S, A, LIGHT ? 0.2 : 0.14);
    for (int i = 0; i < 7; i++) {
        double x = cx + (i - 3) * sp;
        moon(cr, x, cy, i == 3 ? r * 1.55 : r, (i + 1) / 8.0, lit);
    }
    cairo_rectangle(cr, cx - sp * 3.2, cy + 120 * S, sp * 6.4, 1.2 * S);
    src(cr, A, 0.18);
    cairo_fill(cr);
}

typedef struct { const char *id, *name; void (*fn)(cairo_t *, cairo_surface_t *); } Style;
static const Style styles[] = {
    {"hills", "Холмы", st_hills},   {"sunset", "Закат", st_sunset}, {"orbits", "Орбиты", st_orbits},
    {"silk", "Шёлк", st_silk},      {"aurora", "Сияние", st_aurora}, {"dots", "Растр", st_dots},
    {"topo", "Рельеф", st_topo},    {"phases", "Фазы луны", st_phases},
};
#define NSTYLES ((int)G_N_ELEMENTS(styles))

typedef struct { int style; unsigned seed; int accent, light; } Params;

static cairo_surface_t *render(int w, int h, Params p, int grain) {
    cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t *cr = cairo_create(sf);
    W = w; H = h; S = w / 1920.0;
    LIGHT = p.light; P = &pal[p.light]; A = P->acc[p.accent];
    rs = p.seed * 2654435761u ^ 0x9e3779b9u;
    if (!rs) rs = 1;
    for (int i = 0; i < 4; i++) rnd();
    styles[p.style].fn(cr, sf);
    cairo_destroy(cr);
    if (grain) { /* лёгкое зерно, чтобы градиенты не шли полосами */
        cairo_surface_flush(sf);
        unsigned char *d = cairo_image_surface_get_data(sf);
        int stride = cairo_image_surface_get_stride(sf);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                unsigned char *px = d + y * stride + x * 4;
                int n = (int)floor((rnd() + rnd() - 1.0) * 2.6 + 0.5);
                for (int k = 0; k < 3; k++) { int v = px[k] + n; px[k] = v < 0 ? 0 : v > 255 ? 255 : v; }
            }
        cairo_surface_mark_dirty(sf);
    }
    return sf;
}

static int initial_light(void) {
    char *m = run_line("noctalia msg theme-mode-get 2>/dev/null");
    int l = m && !strcmp(m, "light");
    g_free(m);
    return l;
}
