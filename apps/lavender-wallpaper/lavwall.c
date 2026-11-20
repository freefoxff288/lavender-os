/* lavwall — генератор минималистичных обоев в цветах системы.
 *
 * Палитра берётся из активной схемы noctalia (~/.config/noctalia/palettes/<имя>.json),
 * так что при смене темы обои подстраиваются сами.
 *
 *   lavwall                         окно с превью
 *   lavwall list                    список стилей и акцентов
 *   lavwall render STYLE [опции]    нарисовать один вариант
 *   lavwall random [опции]          случайный стиль и вариант
 *     -o FILE   -s SEED   -a ACCENT   --light | --dark   --size WxH   --apply
 *
 *   сборка: make  (cc lavwall.c $(pkg-config --cflags --libs gtk4) -lm)
 */
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lavrender.h"

/* ── общее для CLI и окна ──────────────────────────────── */

static int SW = 1920, SH = 1080;

static void detect_screen(void) {
    FILE *f = popen("niri msg outputs 2>/dev/null", "r");
    if (!f) return;
    char line[256];
    int bw = 0, bh = 0, w, h;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, " Current mode: %dx%d", &w, &h) == 2 && w * h > bw * bh) { bw = w; bh = h; }
    pclose(f);
    if (bw > 0) { SW = bw; SH = bh; }
}

static char *auto_path(Params p) {
    char *dir = g_build_filename(g_get_user_special_dir(G_USER_DIRECTORY_PICTURES) ?: g_get_home_dir(),
                                 "Wallpapers", "lavwall", NULL);
    g_mkdir_with_parents(dir, 0755);
    char *path = g_strdup_printf("%s/%s-%s-%s-%u.png", dir, styles[p.style].id, acc_ids[p.accent],
                                 p.light ? "light" : "dark", p.seed);
    g_free(dir);
    return path;
}

static int save_png(Params p, const char *path) {
    cairo_surface_t *sf = render(SW, SH, p, 1);
    cairo_status_t st = cairo_surface_write_to_png(sf, path);
    cairo_surface_destroy(sf);
    return st == CAIRO_STATUS_SUCCESS;
}

static int set_wallpaper(const char *path) {
    const char *argv[] = {"noctalia", "msg", "wallpaper-set", path, NULL};
    int status = -1;
    g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL,
                 NULL, NULL, &status, NULL);
    return status == 0;
}

static int find_style(const char *s) {
    for (int i = 0; i < NSTYLES; i++)
        if (!g_ascii_strcasecmp(s, styles[i].id) || !g_utf8_collate(s, styles[i].name)) return i;
    return -1;
}
static int find_accent(const char *s) {
    for (int i = 0; i < NACC; i++)
        if (!g_ascii_strcasecmp(s, acc_ids[i]) || !g_utf8_collate(s, acc_names[i])) return i;
    return -1;
}

static int cli(int argc, char **argv) {
    const char *cmd = argv[1];
    if (!strcmp(cmd, "list")) {
        printf("стили:   ");
        for (int i = 0; i < NSTYLES; i++) printf("%s (%s)%s", styles[i].id, styles[i].name, i < NSTYLES - 1 ? ", " : "\n");
        printf("акценты: ");
        for (int i = 0; i < NACC; i++) printf("%s (%s)%s", acc_ids[i], acc_names[i], i < NACC - 1 ? ", " : "\n");
        printf("палитра: %s, экран %dx%d\n", palette_name, SW, SH);
        return 0;
    }
    int is_random = !strcmp(cmd, "random");
    if (strcmp(cmd, "render") && !is_random) {
        fprintf(stderr,
                "lavwall — минималистичные обои в цветах системы\n\n"
                "  lavwall                        открыть окно\n"
                "  lavwall list                   стили и акценты\n"
                "  lavwall render STYLE [опции]   нарисовать\n"
                "  lavwall random [опции]         случайный стиль\n\n"
                "  -o FILE  -s SEED  -a ACCENT  --light|--dark  --size WxH  --apply (поставить обоями)\n");
        return strcmp(cmd, "-h") && strcmp(cmd, "--help") ? 2 : 0;
    }
    Params p = {0, (unsigned)time(NULL) ^ ((unsigned)getpid() << 16), 0, initial_light()};
    int i = 2, apply = 0;
    const char *out = NULL;
    if (is_random) {
        p.style = g_random_int_range(0, NSTYLES);
    } else {
        if (argc < 3 || (p.style = find_style(argv[2])) < 0) {
            fprintf(stderr, "неизвестный стиль; см. lavwall list\n");
            return 2;
        }
        i = 3;
    }
    for (; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "-o") && v) out = argv[++i];
        else if (!strcmp(a, "-s") && v) p.seed = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "-a") && v) {
            if ((p.accent = find_accent(argv[++i])) < 0) { fprintf(stderr, "неизвестный акцент\n"); return 2; }
        } else if (!strcmp(a, "--light")) p.light = 1;
        else if (!strcmp(a, "--dark")) p.light = 0;
        else if (!strcmp(a, "--size") && v) {
            if (sscanf(argv[++i], "%dx%d", &SW, &SH) != 2 || SW < 16 || SH < 16) { fprintf(stderr, "--size WxH\n"); return 2; }
        } else if (!strcmp(a, "--apply")) apply = 1;
        else { fprintf(stderr, "непонятный аргумент: %s\n", a); return 2; }
    }
    char *path = out ? g_strdup(out) : auto_path(p);
    if (!save_png(p, path)) { fprintf(stderr, "не удалось записать %s\n", path); return 1; }
    printf("%s\n", path);
    if (apply && !set_wallpaper(path)) { fprintf(stderr, "noctalia не приняла обои\n"); return 1; }
    g_free(path);
    return 0;
}

/* ── окно ──────────────────────────────────────────────── */

static Params cur;
static GtkWidget *win, *preview, *status_lbl, *flow, *light_sw;
static GtkWidget *thumbs[16];
static cairo_surface_t *thumb_sf[16];
static Params thumb_key[16];
static int thumb_px[16];
static cairo_surface_t *prev_sf;
static Params prev_key;
static int prev_w, prev_h;
static guint prev_timer;
static int in_sync;

static int same(Params a, Params b) { return a.style == b.style && a.seed == b.seed && a.accent == b.accent && a.light == b.light; }

static void rounded(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
}

static void set_status(const char *msg) {
    char *s = msg ? g_strdup(msg)
                  : g_strdup_printf("%s · вариант %u · %d×%d", palette_name, cur.seed, SW, SH);
    gtk_label_set_text(GTK_LABEL(status_lbl), s);
    g_free(s);
}

static void refresh(void) {
    gtk_widget_queue_draw(preview);
    for (int i = 0; i < NSTYLES; i++) gtk_widget_queue_draw(thumbs[i]);
    set_status(NULL);
}

static void fit(int w, int h, int *pw, int *ph, int *ox, int *oy) {
    double ar = (double)SW / SH;
    *pw = w; *ph = (int)(w / ar);
    if (*ph > h) { *ph = h; *pw = (int)(h * ar); }
    *ox = (w - *pw) / 2; *oy = (h - *ph) / 2;
}

static gboolean prev_rerender(gpointer d) {
    (void)d;
    prev_timer = 0;
    if (prev_sf) { cairo_surface_destroy(prev_sf); prev_sf = NULL; }
    gtk_widget_queue_draw(preview);
    return G_SOURCE_REMOVE;
}

static void draw_preview(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer d) {
    (void)d;
    int pw, ph, ox, oy, sc = gtk_widget_get_scale_factor(GTK_WIDGET(a));
    fit(w, h, &pw, &ph, &ox, &oy);
    if (pw < 8 || ph < 8) return;
    if (prev_sf && same(prev_key, cur) && (prev_w != pw * sc || prev_h != ph * sc)) {
        /* окно меняет размер: растянуть старую картинку, перерисовать чуть позже */
        if (!prev_timer) prev_timer = g_timeout_add(160, prev_rerender, NULL);
    } else if (!prev_sf || !same(prev_key, cur)) {
        if (prev_sf) cairo_surface_destroy(prev_sf);
        prev_sf = render(pw * sc, ph * sc, cur, 1);
        prev_key = cur; prev_w = pw * sc; prev_h = ph * sc;
    }
    cairo_translate(cr, ox, oy);
    rounded(cr, 0, 0, pw, ph, 16);
    cairo_clip(cr);
    cairo_scale(cr, (double)pw / prev_w, (double)ph / prev_h);
    cairo_set_source_surface(cr, prev_sf, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
}

static void draw_thumb(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer d) {
    int i = GPOINTER_TO_INT(d), sc = gtk_widget_get_scale_factor(GTK_WIDGET(a));
    Params p = cur;
    p.style = i;
    if (!thumb_sf[i] || !same(thumb_key[i], p) || thumb_px[i] != w * sc) {
        if (thumb_sf[i]) cairo_surface_destroy(thumb_sf[i]);
        thumb_sf[i] = render(w * sc, h * sc, p, 0);
        thumb_key[i] = p; thumb_px[i] = w * sc;
    }
    rounded(cr, 0, 0, w, h, 10);
    cairo_clip(cr);
    cairo_scale(cr, 1.0 / sc, 1.0 / sc);
    cairo_set_source_surface(cr, thumb_sf[i], 0, 0);
    cairo_paint(cr);
}

static void on_style(GtkFlowBox *fb, gpointer d) {
    (void)d;
    GList *sel = gtk_flow_box_get_selected_children(fb);
    if (sel) {
        cur.style = gtk_flow_box_child_get_index(sel->data);
        g_list_free(sel);
        refresh();
    }
}

static void new_variant(void) { cur.seed = g_random_int() % 1000000; refresh(); }
static void on_dice(GtkButton *b, gpointer d) { (void)b; (void)d; new_variant(); }

static void on_accent(GtkToggleButton *b, gpointer d) {
    if (!gtk_toggle_button_get_active(b)) return;
    cur.accent = GPOINTER_TO_INT(d);
    refresh();
}

static void on_light(GObject *o, GParamSpec *ps, gpointer d) {
    (void)ps; (void)d;
    if (in_sync) return;
    cur.light = gtk_switch_get_active(GTK_SWITCH(o));
    refresh();
}

static void do_apply(void) {
    char *path = auto_path(cur);
    if (!save_png(cur, path)) set_status("не удалось сохранить картинку");
    else if (!set_wallpaper(path)) set_status("noctalia не приняла обои");
    else {
        char *m = g_strdup_printf("✓ обои установлены · %s", path);
        set_status(m);
        g_free(m);
    }
    g_free(path);
}
static void on_apply(GtkButton *b, gpointer d) { (void)b; (void)d; do_apply(); }

static void save_done(GObject *o, GAsyncResult *res, gpointer d) {
    (void)d;
    GFile *f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(o), res, NULL);
    if (!f) return;
    char *path = g_file_get_path(f);
    char *m = g_strdup_printf(save_png(cur, path) ? "✓ сохранено · %s" : "не удалось сохранить %s", path);
    set_status(m);
    g_free(m); g_free(path);
    g_object_unref(f);
}
static void do_save(void) {
    GtkFileDialog *dlg = gtk_file_dialog_new();
    char *name = g_strdup_printf("%s-%s-%u.png", styles[cur.style].id, acc_ids[cur.accent], cur.seed);
    gtk_file_dialog_set_initial_name(dlg, name);
    char *dir = g_build_filename(g_get_user_special_dir(G_USER_DIRECTORY_PICTURES) ?: g_get_home_dir(), "Wallpapers", NULL);
    GFile *df = g_file_new_for_path(dir);
    gtk_file_dialog_set_initial_folder(dlg, df);
    gtk_file_dialog_save(dlg, GTK_WINDOW(win), NULL, save_done, NULL);
    g_object_unref(df); g_free(dir); g_free(name);
    g_object_unref(dlg);
}
static void on_save(GtkButton *b, gpointer d) { (void)b; (void)d; do_save(); }

static gboolean on_key(GtkEventControllerKey *k, guint kv, guint code, GdkModifierType st, gpointer d) {
    (void)k; (void)code; (void)d;
    if ((st & GDK_CONTROL_MASK) && (kv == GDK_KEY_s || kv == GDK_KEY_Cyrillic_yeru)) { do_save(); return TRUE; }
    if (st & (GDK_CONTROL_MASK | GDK_ALT_MASK)) return FALSE;
    switch (kv) {
    case GDK_KEY_r: case GDK_KEY_Cyrillic_ka: case GDK_KEY_space: new_variant(); return TRUE;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: do_apply(); return TRUE;
    case GDK_KEY_l: case GDK_KEY_Cyrillic_de:
        gtk_switch_set_active(GTK_SWITCH(light_sw), !gtk_switch_get_active(GTK_SWITCH(light_sw)));
        return TRUE;
    case GDK_KEY_q: case GDK_KEY_Cyrillic_shorti: case GDK_KEY_Escape: gtk_window_close(GTK_WINDOW(win)); return TRUE;
    }
    return FALSE;
}

static char *css_hex(C c) { return g_strdup_printf("#%02x%02x%02x", (int)lround(c.r * 255), (int)lround(c.g * 255), (int)lround(c.b * 255)); }

static void load_css(void) {
    const Pal *p = &pal[0];
    char *bg = css_hex(p->bg), *surf = css_hex(p->surf), *sel = css_hex(p->sel), *fg = css_hex(p->fg),
         *sub = css_hex(p->sub), *mut = css_hex(p->muted), *acc = css_hex(p->acc[0]), *on = css_hex(p->on_acc);
    GString *css = g_string_new(NULL);
    g_string_append_printf(css,
        "window.lavwall, window.lavwall .body { background: %s; color: %s; }\n"
        "window.lavwall headerbar { background: %s; color: %s; box-shadow: none; border-bottom: 1px solid %s; min-height: 46px; }\n"
        ".lavwall .title { font-weight: 700; color: %s; }\n"
        ".lavwall .status { color: %s; font-size: 0.85em; }\n"
        ".lavwall .side { background: %s; border-radius: 18px; }\n"
        ".lavwall flowbox { background: transparent; }\n"
        ".lavwall flowboxchild { border-radius: 14px; padding: 7px; outline: none; transition: background 150ms; }\n"
        ".lavwall flowboxchild:hover { background: alpha(%s, 0.07); }\n"
        ".lavwall flowboxchild:selected { background: alpha(%s, 0.16); box-shadow: inset 0 0 0 2px alpha(%s, 0.8); }\n"
        ".lavwall .card-label { color: %s; font-size: 0.9em; margin-top: 6px; }\n"
        ".lavwall flowboxchild:selected .card-label { color: %s; font-weight: 700; }\n"
        ".lavwall button { border-radius: 11px; background: %s; color: %s; border: none; box-shadow: none; padding: 6px 14px; }\n"
        ".lavwall button:hover { background: %s; }\n"
        ".lavwall button.suggested-action { background: %s; color: %s; font-weight: 700; }\n"
        ".lavwall button.suggested-action:hover { background: alpha(%s, 0.85); }\n"
        ".lavwall .controls { background: %s; border-radius: 16px; padding: 10px 14px; }\n"
        ".lavwall .hint { color: %s; font-size: 0.85em; }\n"
        ".lavwall button.swatch { min-width: 24px; min-height: 24px; padding: 0; border-radius: 999px;"
        "  box-shadow: inset 0 0 0 2px alpha(black, 0.15); }\n"
        ".lavwall button.swatch:checked { box-shadow: 0 0 0 2px %s, 0 0 0 4px %s; }\n"
        ".lavwall switch { background: %s; border-radius: 999px; }\n"
        ".lavwall switch:checked { background: %s; }\n"
        ".lavwall switch slider { background: %s; border-radius: 999px; }\n",
        bg, fg, surf, fg, sel, fg, mut, surf, acc, acc, acc, sub, acc, sel, fg, mut, acc, on, acc, surf, mut,
        surf, fg, sel, acc, fg);
    for (int i = 0; i < NACC; i++) {
        char *c = css_hex(p->acc[i]);
        g_string_append_printf(css, ".lavwall button.sw%d, .lavwall button.sw%d:hover { background: %s; }\n", i, i, c);
        g_free(c);
    }
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_string(prov, css->str);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(prov),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
    g_string_free(css, TRUE);
    g_free(bg); g_free(surf); g_free(sel); g_free(fg); g_free(sub); g_free(mut); g_free(acc); g_free(on);
}

static void activate(GtkApplication *app, gpointer d) {
    (void)d;
    load_css();
    win = gtk_application_window_new(app);
    gtk_widget_add_css_class(win, "lavwall");
    gtk_window_set_title(GTK_WINDOW(win), "Обои");
    gtk_window_set_default_size(GTK_WINDOW(win), 1180, 700);

    /* заголовок */
    GtkWidget *hb = gtk_header_bar_new();
    GtkWidget *tbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(tbox, GTK_ALIGN_CENTER);
    GtkWidget *title = gtk_label_new("✿ Обои");
    gtk_widget_add_css_class(title, "title");
    status_lbl = gtk_label_new("");
    gtk_widget_add_css_class(status_lbl, "status");
    gtk_label_set_ellipsize(GTK_LABEL(status_lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(tbox), title);
    gtk_box_append(GTK_BOX(tbox), status_lbl);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(hb), tbox);
    GtkWidget *apply = gtk_button_new_with_label("Поставить обоями");
    gtk_widget_add_css_class(apply, "suggested-action");
    gtk_widget_set_tooltip_text(apply, "Enter");
    g_signal_connect(apply, "clicked", G_CALLBACK(on_apply), NULL);
    GtkWidget *save = gtk_button_new_with_label("Сохранить…");
    gtk_widget_set_tooltip_text(save, "Ctrl+S");
    g_signal_connect(save, "clicked", G_CALLBACK(on_save), NULL);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), apply);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), save);
    gtk_window_set_titlebar(GTK_WINDOW(win), hb);

    /* слева — галерея стилей */
    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(body, "body");
    gtk_widget_set_margin_start(body, 16); gtk_widget_set_margin_end(body, 16);
    gtk_widget_set_margin_top(body, 16); gtk_widget_set_margin_bottom(body, 16);

    flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_SINGLE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 4);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 4);
    gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(flow), TRUE);
    gtk_widget_set_valign(flow, GTK_ALIGN_START);
    int tw = 176, th = (int)(176.0 * SH / SW);
    for (int i = 0; i < NSTYLES; i++) {
        GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        thumbs[i] = gtk_drawing_area_new();
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(thumbs[i]), tw);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(thumbs[i]), th);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(thumbs[i]), draw_thumb, GINT_TO_POINTER(i), NULL);
        GtkWidget *lbl = gtk_label_new(styles[i].name);
        gtk_widget_add_css_class(lbl, "card-label");
        gtk_box_append(GTK_BOX(card), thumbs[i]);
        gtk_box_append(GTK_BOX(card), lbl);
        gtk_flow_box_append(GTK_FLOW_BOX(flow), card);
    }
    gtk_flow_box_select_child(GTK_FLOW_BOX(flow), gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(flow), cur.style));
    g_signal_connect(flow, "selected-children-changed", G_CALLBACK(on_style), NULL);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), flow);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_widget_add_css_class(scroll, "side");
    gtk_widget_set_margin_top(flow, 8); gtk_widget_set_margin_bottom(flow, 8);
    gtk_widget_set_margin_start(flow, 8); gtk_widget_set_margin_end(flow, 8);
    gtk_box_append(GTK_BOX(body), scroll);

    /* справа — превью и настройки */
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_hexpand(right, TRUE);
    preview = gtk_drawing_area_new();
    gtk_widget_set_vexpand(preview, TRUE);
    gtk_widget_set_hexpand(preview, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(preview), draw_preview, NULL, NULL);
    gtk_box_append(GTK_BOX(right), preview);

    GtkWidget *ctl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(ctl, "controls");
    GtkWidget *dice = gtk_button_new_with_label("⚄  Другой вариант");
    gtk_widget_set_tooltip_text(dice, "R или пробел");
    g_signal_connect(dice, "clicked", G_CALLBACK(on_dice), NULL);
    gtk_box_append(GTK_BOX(ctl), dice);
    GtkWidget *hint = gtk_label_new("R — вариант · L — светлые · Enter — поставить");
    gtk_widget_add_css_class(hint, "hint");
    gtk_widget_set_hexpand(hint, TRUE);
    gtk_box_append(GTK_BOX(ctl), hint);
    GtkWidget *first = NULL;
    for (int i = 0; i < NACC; i++) {
        GtkWidget *b = gtk_toggle_button_new();
        char cls[8];
        snprintf(cls, sizeof cls, "sw%d", i);
        gtk_widget_add_css_class(b, "swatch");
        gtk_widget_add_css_class(b, cls);
        gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(b, acc_names[i]);
        if (first) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), GTK_TOGGLE_BUTTON(first));
        else first = b;
        if (i == cur.accent) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), TRUE);
        g_signal_connect(b, "toggled", G_CALLBACK(on_accent), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(ctl), b);
    }
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_start(sep, 6); gtk_widget_set_margin_end(sep, 6);
    gtk_box_append(GTK_BOX(ctl), sep);
    gtk_box_append(GTK_BOX(ctl), gtk_label_new("Светлые"));
    light_sw = gtk_switch_new();
    gtk_widget_set_valign(light_sw, GTK_ALIGN_CENTER);
    in_sync = 1;
    gtk_switch_set_active(GTK_SWITCH(light_sw), cur.light);
    in_sync = 0;
    g_signal_connect(light_sw, "notify::active", G_CALLBACK(on_light), NULL);
    gtk_box_append(GTK_BOX(ctl), light_sw);
    gtk_box_append(GTK_BOX(right), ctl);
    gtk_box_append(GTK_BOX(body), right);
    gtk_window_set_child(GTK_WINDOW(win), body);

    GtkEventController *kc = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_CAPTURE);
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(win, kc);

    set_status(NULL);
    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv) {
    load_palette();
    detect_screen();
    if (argc > 1) return cli(argc, argv);
    cur = (Params){0, g_random_int() % 1000000, 0, initial_light()};
    GtkApplication *app = gtk_application_new("lavender.lavwall", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int r = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    return r;
}
