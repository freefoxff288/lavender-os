/* lavshot — красивые скриншоты: окно на фоне в цветах системы, со скруглением, тенью
 * и тремя точками в заголовке.
 *
 *   lavshot [window]          снять активное окно и открыть редактор
 *   lavshot region            выделить область (интерфейс niri) и открыть редактор
 *   lavshot FILE.png          оформить готовую картинку
 *   ... --quick               без редактора: оформить по последним настройкам,
 *                             сохранить в ~/Pictures/Screenshots и скопировать
 *
 * В редакторе: Enter — скопировать и закрыть, Ctrl+S — сохранить и скопировать,
 * R — другой вариант фона, Esc — закрыть.
 */
#include "lavrender.h"
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int bg;          /* 0 — градиент, 1..NSTYLES — стиль lavwall */
    unsigned seed;
    int accent, light;
    int pad;         /* отступ, % ширины снимка */
    int radius;      /* скругление окна, px */
    int shadow, dots;
    int aspect;      /* 0 авто, 1 16:9, 2 4:3, 3 1:1 */
} Opt;

static Opt opt = {0, 7, 0, 0, 7, 16, 1, 1, 0};
static cairo_surface_t *shot;
static int IW, IH, chrome_light;
static const char *aspect_names[] = {"Как есть", "16:9", "4:3", "1:1", NULL};
static const double aspect_val[] = {0, 16.0 / 9, 4.0 / 3, 1};

/* ── настройки ─────────────────────────────────────────── */

static char *conf_path(void) { return g_build_filename(g_get_user_config_dir(), "lavshot.ini", NULL); }

static void load_opt(void) {
    char *p = conf_path();
    GKeyFile *k = g_key_file_new();
    if (g_key_file_load_from_file(k, p, G_KEY_FILE_NONE, NULL)) {
#define GETI(f, lo, hi) do { GError *e = NULL; int v = g_key_file_get_integer(k, "lavshot", #f, &e); \
        if (!e && v >= (lo) && v <= (hi)) { opt.f = v; } g_clear_error(&e); } while (0)
        GETI(bg, 0, NSTYLES); GETI(accent, 0, NACC - 1); GETI(light, 0, 1); GETI(pad, 0, 25);
        GETI(radius, 0, 48); GETI(shadow, 0, 1); GETI(dots, 0, 1); GETI(aspect, 0, 3);
        GError *e = NULL;
        guint64 s = g_key_file_get_uint64(k, "lavshot", "seed", &e);
        if (!e) opt.seed = (unsigned)s;
        g_clear_error(&e);
#undef GETI
    }
    g_key_file_free(k);
    g_free(p);
}

static void save_opt(void) {
    GKeyFile *k = g_key_file_new();
    g_key_file_set_integer(k, "lavshot", "bg", opt.bg);
    g_key_file_set_uint64(k, "lavshot", "seed", opt.seed);
    g_key_file_set_integer(k, "lavshot", "accent", opt.accent);
    g_key_file_set_integer(k, "lavshot", "light", opt.light);
    g_key_file_set_integer(k, "lavshot", "pad", opt.pad);
    g_key_file_set_integer(k, "lavshot", "radius", opt.radius);
    g_key_file_set_integer(k, "lavshot", "shadow", opt.shadow);
    g_key_file_set_integer(k, "lavshot", "dots", opt.dots);
    g_key_file_set_integer(k, "lavshot", "aspect", opt.aspect);
    char *p = conf_path();
    g_key_file_save_to_file(k, p, NULL);
    g_free(p);
    g_key_file_free(k);
}

/* ── сборка картинки ───────────────────────────────────── */

static void st_gradient(cairo_t *cr, cairo_surface_t *sf) {
    (void)sf;
    C c0 = mix(P->bg, A, LIGHT ? 0.38 : 0.42);
    C c1 = mix(P->bg, mix(P->acc[A_SKY], P->acc[A_LILAC], 0.5), LIGHT ? 0.30 : 0.16);
    double ang = rr(0, 2 * M_PI), r = hypot(W, H) / 2;
    cairo_pattern_t *p = cairo_pattern_create_linear(W / 2.0 - cos(ang) * r, H / 2.0 - sin(ang) * r,
                                                     W / 2.0 + cos(ang) * r, H / 2.0 + sin(ang) * r);
    stop(p, 0, c0, 1); stop(p, 1, c1, 1);
    cairo_set_source(cr, p);
    cairo_paint(cr);
    cairo_pattern_destroy(p);
    soft(cr, W * rr(0, 1), H * rr(0, 1), W * 0.6, A, LIGHT ? 0.18 : 0.14);
}

static cairo_surface_t *background(int w, int h, int grain) {
    if (opt.bg > 0) return render(w, h, (Params){opt.bg - 1, opt.seed, opt.accent, opt.light}, grain);
    cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t *cr = cairo_create(sf);
    W = w; H = h; S = w / 1920.0;
    LIGHT = opt.light; P = &pal[opt.light]; A = P->acc[opt.accent];
    rs = opt.seed * 2654435761u ^ 0x85ebca6bu;
    if (!rs) rs = 1;
    st_gradient(cr, sf);
    cairo_destroy(cr);
    return sf;
}

static int bar_h(void) { return opt.dots ? (int)lround(fmax(30.0, IW * 0.02)) : 0; }

/* холст и прямоугольник окна в полном масштабе */
static void geometry(double *cw, double *ch, double *wx, double *wy, double *ww, double *wh) {
    *ww = IW;
    *wh = IH + bar_h();
    double pad = fmax(opt.pad ? 24.0 : 0.0, IW * opt.pad / 100.0);
    *cw = *ww + 2 * pad;
    *ch = *wh + 2 * pad;
    double r = aspect_val[opt.aspect];
    if (r > 0) {
        if (*cw / *ch < r) *cw = *ch * r;
        else *ch = *cw / r;
    }
    *wx = (*cw - *ww) / 2;
    *wy = (*ch - *wh) / 2;
}

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r) {
    r = fmin(r, fmin(w, h) / 2);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
}

static void box_blur(unsigned char *d, int w, int h, int stride, int r) {
    if (r < 1) return;
    unsigned char *tmp = g_malloc((size_t)(w > h ? w : h));
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < h; y++) { /* по горизонтали */
            unsigned char *row = d + (size_t)y * stride;
            int sum = 0;
            for (int x = -r; x <= r; x++) sum += row[x < 0 ? 0 : x >= w ? w - 1 : x];
            for (int x = 0; x < w; x++) {
                tmp[x] = (unsigned char)(sum / (2 * r + 1));
                int a = x + r + 1, b = x - r;
                sum += row[a >= w ? w - 1 : a] - row[b < 0 ? 0 : b];
            }
            memcpy(row, tmp, (size_t)w);
        }
        for (int x = 0; x < w; x++) { /* по вертикали */
            int sum = 0;
            for (int y = -r; y <= r; y++) sum += d[(size_t)(y < 0 ? 0 : y >= h ? h - 1 : y) * stride + x];
            for (int y = 0; y < h; y++) {
                tmp[y] = (unsigned char)(sum / (2 * r + 1));
                int a = y + r + 1, b = y - r;
                sum += d[(size_t)(a >= h ? h - 1 : a) * stride + x] - d[(size_t)(b < 0 ? 0 : b) * stride + x];
            }
            for (int y = 0; y < h; y++) d[(size_t)y * stride + x] = tmp[y];
        }
    }
    g_free(tmp);
}

static cairo_surface_t *compose(double k, int grain) {
    double cw, ch, wx, wy, ww, wh;
    geometry(&cw, &ch, &wx, &wy, &ww, &wh);
    int ow = (int)lround(cw * k), oh = (int)lround(ch * k);
    if (ow < 1) ow = 1;
    if (oh < 1) oh = 1;
    cairo_surface_t *out = background(ow, oh, grain);
    cairo_t *cr = cairo_create(out);
    double rad = opt.radius, pad = (cw - ww) / 2;

    if (opt.shadow) {
        cairo_surface_t *sh = cairo_image_surface_create(CAIRO_FORMAT_A8, ow, oh);
        cairo_t *sc = cairo_create(sh);
        double off = fmax(6, pad * 0.12) * k;
        rrect(sc, wx * k, wy * k + off, ww * k, wh * k, rad * k);
        cairo_set_source_rgba(sc, 0, 0, 0, 1);
        cairo_fill(sc);
        cairo_destroy(sc);
        cairo_surface_flush(sh);
        box_blur(cairo_image_surface_get_data(sh), ow, oh, cairo_image_surface_get_stride(sh),
                 (int)lround(fmax(8, fmin(pad, 160) * 0.3) * k / 1.7));
        cairo_surface_mark_dirty(sh);
        cairo_set_source_rgba(cr, 0.03, 0.02, 0.05, opt.light ? 0.30 : 0.55);
        cairo_mask_surface(cr, sh, 0, 0);
        cairo_surface_destroy(sh);
    }

    const Pal *cp = &pal[chrome_light];
    int bar = bar_h();
    cairo_save(cr);
    cairo_translate(cr, wx * k, wy * k);
    cairo_scale(cr, k, k);
    rrect(cr, 0, 0, ww, wh, rad);
    cairo_clip(cr);
    cairo_rectangle(cr, 0, bar, ww, wh - bar);
    src(cr, cp->bg, 1);
    cairo_fill(cr);
    if (bar) {
        cairo_rectangle(cr, 0, 0, ww, bar);
        src(cr, cp->surf, 1);
        cairo_fill(cr);
        C dc[3] = {cp->acc[A_ROSE], chrome_light ? hex(0xd2b072) : hex(0xe3cfa6), cp->acc[A_SAGE]};
        for (int i = 0; i < 3; i++) {
            cairo_arc(cr, bar * (0.62 + i * 0.52), bar / 2.0, bar * 0.15, 0, 2 * M_PI);
            src(cr, dc[i], 1);
            cairo_fill(cr);
        }
    }
    cairo_set_source_surface(cr, shot, 0, bar);
    cairo_pattern_set_filter(cairo_get_source(cr), k < 1 ? CAIRO_FILTER_GOOD : CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
    rrect(cr, wx * k + 0.5, wy * k + 0.5, ww * k - 1, wh * k - 1, rad * k);
    cairo_set_line_width(cr, fmax(1, k));
    src(cr, chrome_light ? BLACK : WHITE, chrome_light ? 0.10 : 0.09);
    cairo_stroke(cr);
    cairo_destroy(cr);
    return out;
}

/* ── снимок, сохранение, буфер ─────────────────────────── */

/* GTK-окна рисуют вокруг себя прозрачные поля под тень — обрезаем по непрозрачной части */
static cairo_surface_t *autotrim(cairo_surface_t *in) {
    if (cairo_image_surface_get_format(in) != CAIRO_FORMAT_ARGB32) return in;
    cairo_surface_flush(in);
    int w = cairo_image_surface_get_width(in), h = cairo_image_surface_get_height(in);
    int stride = cairo_image_surface_get_stride(in);
    unsigned char *d = cairo_image_surface_get_data(in);
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (((uint32_t *)(d + (size_t)y * stride))[x] >> 24 > 200) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < 0 || (x0 == 0 && y0 == 0 && x1 == w - 1 && y1 == h - 1)) return in;
    int nw = x1 - x0 + 1, nh = y1 - y0 + 1;
    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, nw, nh);
    cairo_t *cr = cairo_create(out);
    cairo_set_source_surface(cr, in, -x0, -y0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(in);
    return out;
}


static char *capture(int region) {
    char *path = g_strdup_printf("%s/lavshot-src-%d.png", g_get_user_runtime_dir(), (int)getpid());
    unlink(path);
    const char *win[] = {"niri", "msg", "action", "screenshot-window", "--write-to-disk", "true", "--path", path, NULL};
    const char *reg[] = {"niri", "msg", "action", "screenshot", "--show-pointer", "false", "--path", path, NULL};
    if (!g_spawn_sync(NULL, (char **)(region ? reg : win), NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
                      NULL, NULL, NULL, NULL, NULL, NULL)) {
        g_free(path);
        return NULL;
    }
    /* niri пишет файл асинхронно; в режиме области ждём, пока пользователь выделит */
    gint64 until = g_get_monotonic_time() + (region ? 120 : 3) * G_USEC_PER_SEC;
    goffset last = -1;
    while (g_get_monotonic_time() < until) {
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > 0) {
            if (st.st_size == last) return path;
            last = st.st_size;
        }
        g_usleep(120 * 1000);
    }
    g_free(path);
    return NULL;
}

static char *shots_dir(void) {
    char *d = g_build_filename(g_get_user_special_dir(G_USER_DIRECTORY_PICTURES) ?: g_get_home_dir(), "Screenshots", NULL);
    g_mkdir_with_parents(d, 0755);
    return d;
}

static int write_full(const char *path) {
    cairo_surface_t *sf = compose(1.0, 1);
    int ok = cairo_surface_write_to_png(sf, path) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(sf);
    return ok;
}

static int clip_file(const char *path) {
    const char *argv[] = {"sh", "-c", "wl-copy -t image/png < \"$1\"", "sh", path, NULL};
    int st = -1;
    g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &st, NULL);
    return st == 0;
}

static char *save_and_copy(void) {
    char *dir = shots_dir();
    char stamp[64];
    time_t t = time(NULL);
    strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", localtime(&t));
    char *path = g_strdup_printf("%s/lavshot-%s.png", dir, stamp);
    g_free(dir);
    if (!write_full(path)) { g_free(path); return NULL; }
    clip_file(path);
    save_opt();
    return path;
}

static int copy_only(void) {
    char *path = g_strdup_printf("%s/lavshot-clip.png", g_get_user_runtime_dir());
    int ok = write_full(path) && clip_file(path);
    g_free(path);
    save_opt();
    return ok;
}

/* ── редактор ──────────────────────────────────────────── */

static GtkWidget *win, *preview, *status_lbl, *light_sw;
static cairo_surface_t *prev_sf;
static Opt prev_key;
static int prev_w, prev_h, prev_valid;
static guint redraw_timer;

static void set_status(const char *msg) {
    char *s = msg ? g_strdup(msg) : g_strdup_printf("%d×%d · Enter — копировать · Ctrl+S — сохранить", IW, IH);
    gtk_label_set_text(GTK_LABEL(status_lbl), s);
    g_free(s);
}

static gboolean redraw_now(gpointer d) {
    (void)d;
    redraw_timer = 0;
    gtk_widget_queue_draw(preview);
    return G_SOURCE_REMOVE;
}
static void changed(void) {
    if (!redraw_timer) redraw_timer = g_timeout_add(40, redraw_now, NULL);
}

static void draw_preview(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer d) {
    (void)d;
    double cw, ch, wx, wy, ww, wh;
    geometry(&cw, &ch, &wx, &wy, &ww, &wh);
    int sc = gtk_widget_get_scale_factor(GTK_WIDGET(a));
    double fitk = fmin((w - 8) / cw, (h - 8) / ch);
    if (fitk <= 0) return;
    int pw = (int)(cw * fitk), ph = (int)(ch * fitk);
    if (!prev_valid || memcmp(&prev_key, &opt, sizeof opt) || prev_w != pw * sc || prev_h != ph * sc) {
        if (prev_sf) cairo_surface_destroy(prev_sf);
        prev_sf = compose(fitk * sc, 0);
        prev_key = opt; prev_w = pw * sc; prev_h = ph * sc; prev_valid = 1;
    }
    cairo_translate(cr, (w - pw) / 2.0, (h - ph) / 2.0);
    rrect(cr, 0, 0, pw, ph, 12);
    cairo_clip(cr);
    cairo_scale(cr, (double)pw / cairo_image_surface_get_width(prev_sf),
                (double)ph / cairo_image_surface_get_height(prev_sf));
    cairo_set_source_surface(cr, prev_sf, 0, 0);
    cairo_paint(cr);
}

static void do_copy(void) {
    if (copy_only()) gtk_window_destroy(GTK_WINDOW(win));
    else set_status("не удалось скопировать (wl-copy?)");
}
static void do_save(void) {
    char *p = save_and_copy();
    if (!p) { set_status("не удалось сохранить"); return; }
    char *body = g_strdup_printf("Сохранено и скопировано: %s", p);
    const char *argv[] = {"notify-send", "-a", "Скриншот", "-i", p, "Скриншот готов", body, NULL};
    g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
    g_free(body); g_free(p);
    gtk_window_destroy(GTK_WINDOW(win));
}
static void on_copy(GtkButton *b, gpointer d) { (void)b; (void)d; do_copy(); }
static void on_save(GtkButton *b, gpointer d) { (void)b; (void)d; do_save(); }

static void new_variant(void) { opt.seed = g_random_int() % 1000000; changed(); }
static void on_dice(GtkButton *b, gpointer d) { (void)b; (void)d; new_variant(); }

static void on_bg(GtkFlowBox *fb, gpointer d) {
    (void)d;
    GList *sel = gtk_flow_box_get_selected_children(fb);
    if (sel) { opt.bg = gtk_flow_box_child_get_index(sel->data); g_list_free(sel); changed(); }
}
static void on_accent(GtkToggleButton *b, gpointer d) {
    if (gtk_toggle_button_get_active(b)) { opt.accent = GPOINTER_TO_INT(d); changed(); }
}
static void on_switch(GObject *o, GParamSpec *ps, gpointer d) {
    (void)ps;
    *(int *)d = gtk_switch_get_active(GTK_SWITCH(o));
    changed();
}
static void on_scale(GtkRange *r, gpointer d) { *(int *)d = (int)lround(gtk_range_get_value(r)); changed(); }
static void on_aspect(GObject *o, GParamSpec *ps, gpointer d) {
    (void)ps; (void)d;
    opt.aspect = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(o));
    changed();
}

static gboolean on_key(GtkEventControllerKey *k, guint kv, guint code, GdkModifierType st, gpointer d) {
    (void)k; (void)code; (void)d;
    if ((st & GDK_CONTROL_MASK) && (kv == GDK_KEY_s || kv == GDK_KEY_Cyrillic_yeru)) { do_save(); return TRUE; }
    if ((st & GDK_CONTROL_MASK) && (kv == GDK_KEY_c || kv == GDK_KEY_Cyrillic_es)) { do_copy(); return TRUE; }
    if (st & (GDK_CONTROL_MASK | GDK_ALT_MASK)) return FALSE;
    switch (kv) {
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: do_copy(); return TRUE;
    case GDK_KEY_r: case GDK_KEY_Cyrillic_ka: new_variant(); return TRUE;
    case GDK_KEY_Escape: case GDK_KEY_q: case GDK_KEY_Cyrillic_shorti: gtk_window_destroy(GTK_WINDOW(win)); return TRUE;
    }
    return FALSE;
}

static char *chex(C c) { return g_strdup_printf("#%02x%02x%02x", (int)lround(c.r * 255), (int)lround(c.g * 255), (int)lround(c.b * 255)); }

static void load_css(void) {
    const Pal *p = &pal[0];
    char *bg = chex(p->bg), *surf = chex(p->surf), *sel = chex(p->sel), *fg = chex(p->fg), *sub = chex(p->sub),
         *mut = chex(p->muted), *acc = chex(p->acc[0]), *on = chex(p->on_acc);
    GString *css = g_string_new(NULL);
    g_string_append_printf(css,
        "window.lavshot, window.lavshot .body { background: %s; color: %s; }\n"
        "window.lavshot headerbar { background: %s; color: %s; box-shadow: none; border-bottom: 1px solid %s; min-height: 46px; }\n"
        ".lavshot .title { font-weight: 700; } .lavshot .status { color: %s; font-size: 0.85em; }\n"
        ".lavshot .side { background: %s; border-radius: 18px; padding: 16px; }\n"
        ".lavshot .section { color: %s; font-size: 0.85em; font-weight: 700; margin-top: 6px; }\n"
        ".lavshot flowbox, .lavshot flowboxchild { background: transparent; }\n"
        ".lavshot flowboxchild { padding: 0; border-radius: 10px; outline: none; }\n"
        ".lavshot .chip { padding: 5px 4px; border-radius: 10px; background: %s; color: %s; font-size: 0.88em; }\n"
        ".lavshot flowboxchild:selected .chip { background: %s; color: %s; font-weight: 700; }\n"
        ".lavshot button { border-radius: 11px; background: %s; color: %s; border: none; box-shadow: none; padding: 6px 14px; }\n"
        ".lavshot button:hover { background: alpha(%s, 0.8); }\n"
        ".lavshot button.suggested-action { background: %s; color: %s; font-weight: 700; }\n"
        ".lavshot button.swatch { min-width: 24px; min-height: 24px; padding: 0; border-radius: 999px; }\n"
        ".lavshot button.swatch:checked { box-shadow: 0 0 0 2px %s, 0 0 0 4px %s; }\n"
        ".lavshot switch { background: %s; border-radius: 999px; } .lavshot switch:checked { background: %s; }\n"
        ".lavshot switch slider { background: %s; border-radius: 999px; }\n"
        ".lavshot scale trough { background: %s; border-radius: 99px; min-height: 6px; }\n"
        ".lavshot scale highlight { background: %s; border-radius: 99px; }\n"
        ".lavshot scale slider { background: %s; border-radius: 99px; min-width: 16px; min-height: 16px; }\n"
        ".lavshot dropdown button { padding: 4px 10px; }\n"
        ".lavshot .hint { color: %s; font-size: 0.8em; }\n",
        bg, fg, surf, fg, sel, mut, surf, sub, sel, fg, acc, on, sel, fg, sel, acc, on, surf, fg, sel, acc, fg, sel,
        acc, fg, mut);
    for (int i = 0; i < NACC; i++) {
        char *c = chex(p->acc[i]);
        g_string_append_printf(css, ".lavshot button.sw%d, .lavshot button.sw%d:hover { background: %s; }\n", i, i, c);
        g_free(c);
    }
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_string(prov, css->str);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(prov),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
    g_string_free(css, TRUE);
    g_free(bg); g_free(surf); g_free(sel); g_free(fg); g_free(sub); g_free(mut); g_free(acc); g_free(on);
}

static GtkWidget *section(const char *t) {
    GtkWidget *l = gtk_label_new(t);
    gtk_widget_add_css_class(l, "section");
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    return l;
}
static GtkWidget *switch_row(const char *t, int *field, GtkWidget **out) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *l = gtk_label_new(t);
    gtk_widget_set_hexpand(l, TRUE);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), *field);
    g_signal_connect(sw, "notify::active", G_CALLBACK(on_switch), field);
    gtk_box_append(GTK_BOX(row), l);
    gtk_box_append(GTK_BOX(row), sw);
    if (out) *out = sw;
    return row;
}
static GtkWidget *scale_row(int lo, int hi, int *field) {
    GtkWidget *s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, lo, hi, 1);
    gtk_range_set_value(GTK_RANGE(s), *field);
    gtk_scale_set_draw_value(GTK_SCALE(s), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(s), GTK_POS_RIGHT);
    g_signal_connect(s, "value-changed", G_CALLBACK(on_scale), field);
    return s;
}

static void activate(GtkApplication *app, gpointer d) {
    (void)d;
    load_css();
    win = gtk_application_window_new(app);
    gtk_widget_add_css_class(win, "lavshot");
    gtk_window_set_title(GTK_WINDOW(win), "Скриншот");
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 740);

    GtkWidget *hb = gtk_header_bar_new();
    GtkWidget *tbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(tbox, GTK_ALIGN_CENTER);
    GtkWidget *title = gtk_label_new("✿ Скриншот");
    gtk_widget_add_css_class(title, "title");
    status_lbl = gtk_label_new("");
    gtk_widget_add_css_class(status_lbl, "status");
    gtk_box_append(GTK_BOX(tbox), title);
    gtk_box_append(GTK_BOX(tbox), status_lbl);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(hb), tbox);
    GtkWidget *copy = gtk_button_new_with_label("Копировать");
    gtk_widget_add_css_class(copy, "suggested-action");
    gtk_widget_set_tooltip_text(copy, "Enter — скопировать в буфер и закрыть");
    g_signal_connect(copy, "clicked", G_CALLBACK(on_copy), NULL);
    GtkWidget *save = gtk_button_new_with_label("Сохранить");
    gtk_widget_set_tooltip_text(save, "Ctrl+S — сохранить в Screenshots и скопировать");
    g_signal_connect(save, "clicked", G_CALLBACK(on_save), NULL);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), copy);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), save);
    gtk_window_set_titlebar(GTK_WINDOW(win), hb);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(body, "body");
    gtk_widget_set_margin_start(body, 16); gtk_widget_set_margin_end(body, 16);
    gtk_widget_set_margin_top(body, 16); gtk_widget_set_margin_bottom(body, 16);

    preview = gtk_drawing_area_new();
    gtk_widget_set_hexpand(preview, TRUE);
    gtk_widget_set_vexpand(preview, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(preview), draw_preview, NULL, NULL);
    gtk_box_append(GTK_BOX(body), preview);

    GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(side, "side");

    gtk_box_append(GTK_BOX(side), section("Фон"));
    GtkWidget *fb = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(fb), GTK_SELECTION_SINGLE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(fb), 3);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(fb), 3);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(fb), TRUE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(fb), 6);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(fb), 6);
    for (int i = 0; i <= NSTYLES; i++) {
        GtkWidget *l = gtk_label_new(i ? styles[i - 1].name : "Градиент");
        gtk_widget_add_css_class(l, "chip");
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
        gtk_flow_box_append(GTK_FLOW_BOX(fb), l);
    }
    gtk_flow_box_select_child(GTK_FLOW_BOX(fb), gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(fb), opt.bg));
    g_signal_connect(fb, "selected-children-changed", G_CALLBACK(on_bg), NULL);
    gtk_box_append(GTK_BOX(side), fb);

    GtkWidget *dice = gtk_button_new_with_label("⚄  Другой вариант фона");
    gtk_widget_set_tooltip_text(dice, "R");
    g_signal_connect(dice, "clicked", G_CALLBACK(on_dice), NULL);
    gtk_box_append(GTK_BOX(side), dice);

    gtk_box_append(GTK_BOX(side), section("Цвет"));
    GtkWidget *sw_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *first = NULL;
    for (int i = 0; i < NACC; i++) {
        GtkWidget *b = gtk_toggle_button_new();
        char cls[8];
        snprintf(cls, sizeof cls, "sw%d", i);
        gtk_widget_add_css_class(b, "swatch");
        gtk_widget_add_css_class(b, cls);
        gtk_widget_set_tooltip_text(b, acc_names[i]);
        if (first) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), GTK_TOGGLE_BUTTON(first));
        else first = b;
        if (i == opt.accent) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), TRUE);
        g_signal_connect(b, "toggled", G_CALLBACK(on_accent), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(sw_row), b);
    }
    gtk_box_append(GTK_BOX(side), sw_row);
    gtk_box_append(GTK_BOX(side), switch_row("Светлый фон", &opt.light, &light_sw));

    gtk_box_append(GTK_BOX(side), section("Отступ, %"));
    gtk_box_append(GTK_BOX(side), scale_row(0, 20, &opt.pad));
    gtk_box_append(GTK_BOX(side), section("Скругление"));
    gtk_box_append(GTK_BOX(side), scale_row(0, 40, &opt.radius));
    gtk_box_append(GTK_BOX(side), switch_row("Тень", &opt.shadow, NULL));
    gtk_box_append(GTK_BOX(side), switch_row("Три точки сверху", &opt.dots, NULL));

    GtkWidget *arow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *al = gtk_label_new("Формат");
    gtk_widget_set_hexpand(al, TRUE);
    gtk_label_set_xalign(GTK_LABEL(al), 0);
    GtkWidget *dd = gtk_drop_down_new_from_strings(aspect_names);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), (guint)opt.aspect);
    g_signal_connect(dd, "notify::selected", G_CALLBACK(on_aspect), NULL);
    gtk_box_append(GTK_BOX(arow), al);
    gtk_box_append(GTK_BOX(arow), dd);
    gtk_box_append(GTK_BOX(side), arow);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(side), spacer);
    GtkWidget *hint = gtk_label_new("Enter — копировать · Ctrl+S — сохранить\nR — другой фон · Esc — закрыть");
    gtk_widget_add_css_class(hint, "hint");
    gtk_label_set_justify(GTK_LABEL(hint), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(side), hint);
    GtkWidget *side_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(side_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(side_scroll), FALSE);
    gtk_widget_set_size_request(side_scroll, 320, -1);
    gtk_widget_set_hexpand(side_scroll, FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(side_scroll), side);
    gtk_box_append(GTK_BOX(body), side_scroll);
    gtk_window_set_child(GTK_WINDOW(win), body);

    GtkEventController *kc = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_CAPTURE);
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(win, kc);

    set_status(NULL);
    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv) {
    int quick = 0, region = 0;
    const char *file = NULL, *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quick")) quick = 1;
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "region")) region = 1;
        else if (!strcmp(argv[i], "window")) region = 0;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lavshot [window|region|FILE.png] [--quick]\n"
                   "  window  — активное окно (по умолчанию), region — выделить область,\n"
                   "  FILE    — оформить готовую картинку, --quick — без редактора\n"
                   "  -o OUT  — записать результат в OUT (без редактора и буфера)\n");
            return 0;
        } else file = argv[i];
    }
    load_palette();
    chrome_light = initial_light();
    load_opt();

    char *src = file ? g_strdup(file) : capture(region);
    if (!src) { fprintf(stderr, "lavshot: снимок не получен\n"); return 1; }
    shot = cairo_image_surface_create_from_png(src);
    if (cairo_surface_status(shot) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "lavshot: не удалось открыть %s\n", src);
        return 1;
    }
    shot = autotrim(shot);
    IW = cairo_image_surface_get_width(shot);
    IH = cairo_image_surface_get_height(shot);
    if (!file) unlink(src);
    g_free(src);

    if (out) {
        if (!write_full(out)) { fprintf(stderr, "lavshot: не удалось записать %s\n", out); return 1; }
        return 0;
    }
    if (quick) {
        char *p = save_and_copy();
        if (!p) { fprintf(stderr, "lavshot: не удалось сохранить\n"); return 1; }
        printf("%s\n", p);
        const char *nargv[] = {"notify-send", "-a", "Скриншот", "-i", p, "Скриншот готов", "Сохранён и скопирован в буфер", NULL};
        g_spawn_async(NULL, (char **)nargv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
        g_free(p);
        return 0;
    }
    GtkApplication *app = gtk_application_new("lavender.lavshot", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int r = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    return r;
}
