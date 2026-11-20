/* dropnotes — выпадающие заметки для niri (GTK4 + gtk4-layer-shell), палитра «Лаванда».
 *   dropnotes            — показать или спрятать (повторный запуск переключает)
 *   dropnotes --hidden   — запуститься в фоне (для spawn-at-startup)
 * Вкладки: «Сегодня», «Идеи», «К окну» (заметки к окну, которое было в фокусе).
 * Хранение: ~/notes/today.md, ~/notes/ideas.md, ~/notes/windows/<заголовок>.md — обычные чек-листы
 * «- [ ] пункт» / «- [x] пункт». Стикеры (закреплённые списки) помнятся в ~/notes/.stickies. */
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <math.h>

enum { T_TODAY, T_IDEAS, T_WIN, T_N };
static const char *TAB_NAME[T_N] = { "Сегодня", "Идеи", "К окну" };
static const char *TAB_FILE[T_N] = { "today", "ideas", NULL };

typedef struct { char *text; gboolean done; } Item;
typedef struct { int tab; char *key; GtkWidget *win, *list; } Sticky;

static GtkApplication *app;
static GtkWidget *win, *panel, *scrim, *tabs[T_N], *ctx_lbl, *listbox, *entry, *scroller;
static int tab = T_TODAY;
static char *win_key;          /* заголовок окна, бывшего в фокусе при открытии */
static gboolean shown;
static GPtrArray *stickies;
static char *notes_dir;

/* ---------- файлы ---------- */
static char *safe_name(const char *s) {
    GString *o = g_string_new(NULL);
    for (const char *p = s; *p && o->len < 80; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_isalnum(c) || c == ' ' || c == '.' || c == '-' || c == '_') g_string_append_unichar(o, c);
        else g_string_append_c(o, '_');
    }
    if (!o->len) g_string_append(o, "window");
    return g_string_free(o, FALSE);
}

static char *path_for(int t, const char *key) {
    if (t != T_WIN) { char *f = g_strconcat(TAB_FILE[t], ".md", NULL); char *p = g_build_filename(notes_dir, f, NULL); g_free(f); return p; }
    char *n = safe_name(key), *f = g_strconcat(n, ".md", NULL);
    char *p = g_build_filename(notes_dir, "windows", f, NULL);
    g_free(n); g_free(f);
    return p;
}

static void item_free(gpointer p) { Item *i = p; g_free(i->text); g_free(i); }

static GPtrArray *load(int t, const char *key) {
    GPtrArray *a = g_ptr_array_new_with_free_func(item_free);
    char *p = path_for(t, key), *txt = NULL;
    if (g_file_get_contents(p, &txt, NULL, NULL)) {
        char **lines = g_strsplit(txt, "\n", -1);
        for (char **l = lines; *l; l++) {
            gboolean d = g_str_has_prefix(*l, "- [x] ") || g_str_has_prefix(*l, "- [X] ");
            if (!d && !g_str_has_prefix(*l, "- [ ] ")) continue;
            Item *i = g_new0(Item, 1);
            i->text = g_strdup(*l + 6); i->done = d;
            g_ptr_array_add(a, i);
        }
        g_strfreev(lines); g_free(txt);
    }
    g_free(p);
    return a;
}

static void save(int t, const char *key, GPtrArray *a) {
    char *p = path_for(t, key), *dir = g_path_get_dirname(p);
    g_mkdir_with_parents(dir, 0700);
    GString *s = g_string_new(NULL);
    g_string_append_printf(s, "# %s\n\n", t == T_WIN ? key : TAB_NAME[t]);
    for (guint i = 0; i < a->len; i++) { Item *it = a->pdata[i]; g_string_append_printf(s, "- [%c] %s\n", it->done ? 'x' : ' ', it->text); }
    g_file_set_contents(p, s->str, s->len, NULL);
    g_string_free(s, TRUE); g_free(p); g_free(dir);
}

static const char *cur_key(void) { return tab == T_WIN ? win_key : NULL; }

/* ---------- стикеры ---------- */
static void save_stickies(void) {
    GString *s = g_string_new(NULL);
    for (guint i = 0; i < stickies->len; i++) { Sticky *k = stickies->pdata[i]; g_string_append_printf(s, "%d\t%s\n", k->tab, k->key ? k->key : ""); }
    char *p = g_build_filename(notes_dir, ".stickies", NULL);
    g_mkdir_with_parents(notes_dir, 0700);
    g_file_set_contents(p, s->str, s->len, NULL);
    g_free(p); g_string_free(s, TRUE);
}

static void fill_sticky(Sticky *k) {
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(k->list))) gtk_box_remove(GTK_BOX(k->list), c);
    GPtrArray *a = load(k->tab, k->key);
    int shownn = 0;
    for (int pass = 0; pass < 2; pass++)
        for (guint i = 0; i < a->len && shownn < 8; i++) {
            Item *it = a->pdata[i];
            if (it->done != (pass == 1)) continue;
            GtkWidget *l = gtk_label_new(it->text);
            gtk_label_set_xalign(GTK_LABEL(l), 0);
            gtk_label_set_wrap(GTK_LABEL(l), TRUE);
            gtk_widget_add_css_class(l, "si");
            if (it->done) gtk_widget_add_css_class(l, "done");
            gtk_box_append(GTK_BOX(k->list), l);
            shownn++;
        }
    if (!shownn) { GtkWidget *l = gtk_label_new("Пока пусто"); gtk_widget_add_css_class(l, "si"); gtk_widget_add_css_class(l, "done"); gtk_label_set_xalign(GTK_LABEL(l), 0); gtk_box_append(GTK_BOX(k->list), l); }
    g_ptr_array_unref(a);
}

static void refresh_stickies(void) { for (guint i = 0; i < stickies->len; i++) fill_sticky(stickies->pdata[i]); }

/* окно стикера закрывается (кнопкой × или через niri) — забыть его */
static gboolean sticky_closed(GtkWindow *w, Sticky *k) {
    g_ptr_array_remove(stickies, k);
    g_free(k->key); g_free(k);
    save_stickies();
    return FALSE;
}
static void sticky_x(GtkButton *b, Sticky *k) { gtk_window_close(GTK_WINDOW(k->win)); }

static void add_sticky(int t, const char *key) {
    Sticky *k = g_new0(Sticky, 1);
    k->tab = t; k->key = g_strdup(key);
    k->win = gtk_application_window_new(app);
    char *title = t == T_WIN ? g_strdup_printf("Стикер · %s", key) : g_strdup_printf("Стикер · %s", TAB_NAME[t]);
    gtk_window_set_title(GTK_WINDOW(k->win), title);
    gtk_window_set_default_size(GTK_WINDOW(k->win), 260, -1);
    gtk_widget_add_css_class(k->win, "sticky");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(box, "sbox");
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *h = gtk_label_new(title + strlen("Стикер · "));
    gtk_widget_add_css_class(h, "sh");
    gtk_label_set_ellipsize(GTK_LABEL(h), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(h), 0);
    gtk_widget_set_hexpand(h, TRUE);
    GtkWidget *x = gtk_button_new_with_label("×");
    gtk_widget_add_css_class(x, "x");
    gtk_widget_set_tooltip_text(x, "Убрать стикер");
    g_signal_connect(x, "clicked", G_CALLBACK(sticky_x), k);
    gtk_box_append(GTK_BOX(head), h);
    gtk_box_append(GTK_BOX(head), x);
    k->list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(box), head);
    gtk_box_append(GTK_BOX(box), k->list);
    gtk_window_set_child(GTK_WINDOW(k->win), box);
    g_signal_connect(k->win, "close-request", G_CALLBACK(sticky_closed), k);
    fill_sticky(k);
    g_ptr_array_add(stickies, k);
    gtk_window_present(GTK_WINDOW(k->win));
    g_free(title);
}

static void restore_stickies(void) {
    char *p = g_build_filename(notes_dir, ".stickies", NULL), *txt = NULL;
    if (g_file_get_contents(p, &txt, NULL, NULL)) {
        char **lines = g_strsplit(txt, "\n", -1);
        for (char **l = lines; *l; l++) {
            char *tabc = strchr(*l, '\t');
            if (!tabc) continue;
            *tabc = 0;
            int t = atoi(*l);
            if (t >= 0 && t < T_N) add_sticky(t, t == T_WIN ? tabc + 1 : NULL);
        }
        g_strfreev(lines); g_free(txt);
    }
    g_free(p);
}

/* ---------- список ---------- */
static void render(void);

static void changed(GPtrArray *a) { save(tab, cur_key(), a); render(); refresh_stickies(); }

static void on_row(GtkListBox *lb, GtkListBoxRow *row, gpointer u) {
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "idx"));
    if (idx < 0) return;
    GPtrArray *a = load(tab, cur_key());
    if ((guint)idx < a->len) { Item *it = a->pdata[idx]; it->done = !it->done; changed(a); }
    g_ptr_array_unref(a);
}

static void on_del(GtkButton *b, gpointer idxp) {
    GPtrArray *a = load(tab, cur_key());
    guint idx = GPOINTER_TO_UINT(idxp);
    if (idx < a->len) { g_ptr_array_remove_index(a, idx); changed(a); }
    g_ptr_array_unref(a);
    gtk_widget_grab_focus(entry);
}

static void render(void) {
    for (int i = 0; i < T_N; i++) {
        if (i == tab) gtk_widget_add_css_class(tabs[i], "on"); else gtk_widget_remove_css_class(tabs[i], "on");
    }
    gtk_list_box_remove_all(GTK_LIST_BOX(listbox));
    GPtrArray *a = load(tab, cur_key());
    int left = 0;
    for (int pass = 0; pass < 2; pass++)
        for (guint i = 0; i < a->len; i++) {
            Item *it = a->pdata[i];
            if (it->done != (pass == 1)) continue;
            if (!it->done) left++;
            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            gtk_widget_add_css_class(row, "item");
            if (it->done) gtk_widget_add_css_class(row, "done");
            GtkWidget *ck = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_widget_add_css_class(ck, "ck");
            gtk_widget_set_valign(ck, GTK_ALIGN_START);
            GtkWidget *l = gtk_label_new(it->text);
            gtk_label_set_wrap(GTK_LABEL(l), TRUE);
            gtk_label_set_wrap_mode(GTK_LABEL(l), PANGO_WRAP_WORD_CHAR);
            gtk_label_set_xalign(GTK_LABEL(l), 0);
            gtk_widget_set_hexpand(l, TRUE);
            GtkWidget *del = gtk_button_new_with_label("×");
            gtk_widget_add_css_class(del, "del");
            gtk_widget_set_tooltip_text(del, "Удалить");
            gtk_widget_set_valign(del, GTK_ALIGN_START);
            gtk_widget_set_focus_on_click(del, FALSE);
            g_signal_connect(del, "clicked", G_CALLBACK(on_del), GUINT_TO_POINTER(i));
            gtk_box_append(GTK_BOX(row), ck);
            gtk_box_append(GTK_BOX(row), l);
            gtk_box_append(GTK_BOX(row), del);
            gtk_list_box_append(GTK_LIST_BOX(listbox), row);
            GtkWidget *lr = gtk_widget_get_parent(row);
            g_object_set_data(G_OBJECT(lr), "idx", GINT_TO_POINTER(i));
            gtk_widget_set_focusable(lr, FALSE);
        }
    if (!a->len) {
        char *s = tab == T_WIN ? g_strdup_printf("К «%s» заметок пока нет. Напиши первую ниже.", win_key)
                               : g_strdup("Пусто. Напиши первую заметку ниже.");
        GtkWidget *l = gtk_label_new(s);
        gtk_label_set_wrap(GTK_LABEL(l), TRUE);
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_widget_add_css_class(l, "empty");
        gtk_list_box_append(GTK_LIST_BOX(listbox), l);
        GtkWidget *lr = gtk_widget_get_parent(l);
        g_object_set_data(G_OBJECT(lr), "idx", GINT_TO_POINTER(-1));
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(lr), FALSE);
        g_free(s);
    }
    char *c = tab == T_WIN ? g_strdup_printf("для «%s»", win_key) : left ? g_strdup_printf("осталось %d", left) : g_strdup(a->len ? "всё сделано" : "");
    gtk_label_set_text(GTK_LABEL(ctx_lbl), c);
    g_free(c);
    g_ptr_array_unref(a);
}

static void on_add(GtkEntry *e, gpointer u) {
    char *t = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(e))));
    if (*t) {
        GPtrArray *a = load(tab, cur_key());
        Item *it = g_new0(Item, 1); it->text = g_strdup(t);
        g_ptr_array_insert(a, 0, it);
        changed(a);
        g_ptr_array_unref(a);
        gtk_editable_set_text(GTK_EDITABLE(e), "");
    }
    g_free(t);
}

static void set_tab(int t) { tab = t; render(); gtk_widget_grab_focus(entry); }
static void on_tab(GtkButton *b, gpointer t) { set_tab(GPOINTER_TO_INT(t)); }

/* ---------- показать / спрятать ---------- */
static void read_focused_window(void) {
    g_free(win_key); win_key = NULL;
    char *out = NULL;
    char *argv[] = { "niri", "msg", "--json", "focused-window", NULL };
    if (g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, NULL, NULL) && out) {
        JsonParser *p = json_parser_new();
        if (json_parser_load_from_data(p, out, -1, NULL)) {
            JsonNode *r = json_parser_get_root(p);
            if (JSON_NODE_HOLDS_OBJECT(r)) {
                JsonObject *o = json_node_get_object(r);
                const char *t = json_object_has_member(o, "title") && !json_object_get_null_member(o, "title") ? json_object_get_string_member(o, "title") : NULL;
                const char *id = json_object_has_member(o, "app_id") && !json_object_get_null_member(o, "app_id") ? json_object_get_string_member(o, "app_id") : NULL;
                if (t && *t) win_key = g_strdup(t); else if (id && *id) win_key = g_strdup(id);
            }
        }
        g_object_unref(p);
    }
    g_free(out);
    if (!win_key) win_key = g_strdup("Рабочий стол");
}

/* ---------- анимация на часах кадров: каждый кадр синхронизирован с обновлением экрана ---------- */
static double vis;              /* 0 — спрятано, 1 — показано */
static double a_from, a_to;
static gint64 a_t0;             /* время первого кадра анимации, мкс; 0 — ещё не было */
static double a_dur;            /* мс */
static guint tick_id;
static int panel_h = 320;
static GtkWidget *fixed;

static double ease_out(double t) { double u = 1 - t; return 1 - u * u * u * u * u; }   /* мягкое торможение */
static double ease_in(double t)  { return t * t * t; }                                  /* мягкий разгон */

static int screen_w(void) {
    int w = gtk_widget_get_width(win);
    if (w > 0) return w;
    GdkDisplay *d = gdk_display_get_default();
    GdkSurface *sf = gtk_native_get_surface(GTK_NATIVE(win));
    GdkMonitor *m = sf ? gdk_display_get_monitor_at_surface(d, sf) : NULL;
    if (!m) m = g_list_model_get_item(gdk_display_get_monitors(d), 0), g_object_unref(m);
    GdkRectangle r; gdk_monitor_get_geometry(m, &r);
    return r.width;
}

static void apply(double v) {
    float x = (screen_w() - 560) / 2.0f;
    float y = 14 - (1 - v) * (panel_h + 40);   /* дробные пиксели — движение без ступенек */
    GskTransform *t = gsk_transform_translate(NULL, &GRAPHENE_POINT_INIT(x, y));
    gtk_fixed_set_child_transform(GTK_FIXED(fixed), panel, t);
    gsk_transform_unref(t);
    gtk_widget_set_opacity(panel, 0.35 + 0.65 * v);
    gtk_widget_set_opacity(scrim, v);
}

static gboolean tick(GtkWidget *w, GdkFrameClock *clock, gpointer u) {
    gint64 now = gdk_frame_clock_get_frame_time(clock);
    if (!a_t0) a_t0 = now;   /* отсчёт с первого реально показанного кадра — начало не теряется */
    double t = MIN(1.0, (now - a_t0) / (a_dur * 1000.0));
    double e = a_to > a_from ? ease_out(t) : ease_in(t);
    vis = a_from + (a_to - a_from) * e;
    apply(vis);
    if (t < 1.0) return G_SOURCE_CONTINUE;
    tick_id = 0;
    if (a_to == 0 && !shown) gtk_widget_set_visible(win, FALSE);
    return G_SOURCE_REMOVE;
}

static void animate_to(double to) {
    a_from = vis; a_to = to; a_t0 = 0;
    a_dur = (to > vis ? 420.0 : 240.0) * MAX(0.35, fabs(to - vis));   /* разворот с полпути — короче */
    if (!tick_id) tick_id = gtk_widget_add_tick_callback(win, tick, NULL, NULL);
}

static void show(void) {
    read_focused_window();
    render();
    int nat = 0;
    gtk_widget_measure(panel, GTK_ORIENTATION_VERTICAL, 560, NULL, &nat, NULL, NULL);
    if (nat > 0) panel_h = nat;
    gtk_layer_set_keyboard_mode(GTK_WINDOW(win), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    shown = TRUE;
    apply(vis);                  /* первый кадр — с текущего положения (обычно спрятано вверху) */
    gtk_widget_set_visible(win, TRUE);
    gtk_window_present(GTK_WINDOW(win));
    gtk_widget_grab_focus(entry);
    animate_to(1);
}

static void hide(void) {
    if (!shown) return;
    shown = FALSE;
    animate_to(0);
}

static gboolean on_key(GtkEventControllerKey *c, guint kv, guint kc, GdkModifierType st, gpointer u) {
    if (kv == GDK_KEY_Escape) { hide(); return TRUE; }
    if (st & GDK_CONTROL_MASK) {
        if (kv >= GDK_KEY_1 && kv <= GDK_KEY_3) { set_tab(kv - GDK_KEY_1); return TRUE; }
        if (kv == GDK_KEY_Tab || kv == GDK_KEY_ISO_Left_Tab) { set_tab((tab + (kv == GDK_KEY_Tab ? 1 : T_N - 1)) % T_N); return TRUE; }
    }
    return FALSE;
}

static void on_scrim(GtkGestureClick *g, int n, double x, double y, gpointer u) {
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    GtkWidget *hit = gtk_widget_pick(w, x, y, GTK_PICK_DEFAULT);
    if (!hit || hit == w || hit == scrim) hide();   /* клик мимо панели — закрыть */
}
static void on_pin(GtkButton *b, gpointer u) { add_sticky(tab, cur_key()); save_stickies(); hide(); }

/* ---------- оформление ---------- */
static const char *CSS =
    "window.dropnotes { background: transparent; }"
    ".scrim { background: alpha(#100e14, 0.55); }"   /* без размытия — затемнение чуть плотнее */
    ".panel { background: #211e28; color: #e6e1ef; border-radius: 22px; padding: 14px 8px 12px;"
    "         box-shadow: 0 30px 60px -20px alpha(black, 0.6), 0 0 0 1px alpha(#bba7e3, 0.10); font-family: 'Inter', sans-serif; }"
    ".tab { font-size: 12.5px; padding: 4px 12px; min-height: 0; border-radius: 999px; background: #2a2633; color: #9a93a8; border: none; box-shadow: none;"
    "       transition: background 200ms, color 200ms; }"
    ".tab:hover { color: #e6e1ef; }"
    ".tab.on { background: mix(#bba7e3, #2a2633, 0.72); color: #e6e1ef; }"
    ".ctx { font-size: 11.5px; color: #9a93a8; }"
    "list { background: transparent; }"
    "list > row { padding: 0; border-radius: 12px; background: transparent; transition: background 150ms; }"
    "list > row:hover { background: #2a2633; }"
    ".item { padding: 8px 10px; font-size: 14px; }"
    ".ck { min-width: 14px; min-height: 14px; border-radius: 99px; border: 1.5px solid #9a93a8; margin-top: 2px; transition: background 200ms, border-color 200ms; }"
    ".item.done .ck { background: #a7cdb6; border-color: #a7cdb6; }"
    ".item.done label { color: #9a93a8; text-decoration: line-through; }"
    ".del { min-width: 22px; min-height: 22px; padding: 0; border-radius: 8px; background: none; border: none; box-shadow: none; color: #9a93a8; opacity: 0; transition: opacity 150ms; }"
    "list > row:hover .del { opacity: 1; } .del:hover { background: #3a3545; color: #e3a7b5; }"
    ".empty { color: #9a93a8; font-size: 13px; padding: 10px; }"
    "entry.add { min-height: 40px; border-radius: 12px; background: #2a2633; color: #e6e1ef; border: none; box-shadow: none; padding: 0 12px; outline: none;"
    "            transition: box-shadow 250ms; }"
    "entry.add:focus-within { box-shadow: 0 0 0 1.5px #bba7e3; }"
    ".hint { font-size: 11.5px; color: #9a93a8; }"
    ".pin { font-size: 12px; padding: 5px 11px; min-height: 0; border-radius: 10px; background: #2a2633; color: #e6e1ef; border: none; box-shadow: none; }"
    ".pin:hover { background: mix(#bba7e3, #2a2633, 0.75); }"
    /* стикер */
    "window.sticky { background: mix(#bba7e3, #211e28, 0.84); color: #e6e1ef; border-radius: 16px; font-family: 'Inter', sans-serif; }"
    ".sbox { padding: 10px 14px 14px; }"
    ".sh { font-weight: 600; font-size: 13px; }"
    ".si { font-size: 13px; } .si.done { color: #9a93a8; text-decoration: line-through; }"
    ".x { min-width: 22px; min-height: 22px; padding: 0; border-radius: 8px; background: none; border: none; box-shadow: none; color: #9a93a8; }"
    ".x:hover { background: alpha(#18161d, 0.4); color: #e6e1ef; }";

static gboolean keep_open(GtkWindow *w, gpointer u) { hide(); return TRUE; }   /* не уничтожать, только прятать */

static void build(void) {
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, CSS);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);

    win = gtk_window_new();
    gtk_window_set_application(GTK_WINDOW(win), app);
    gtk_widget_add_css_class(win, "dropnotes");
    gtk_layer_init_for_window(GTK_WINDOW(win));
    gtk_layer_set_namespace(GTK_WINDOW(win), "dropnotes");
    gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    for (int e = 0; e < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; e++) gtk_layer_set_anchor(GTK_WINDOW(win), e, TRUE);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(win), -1);

    GtkWidget *ov = gtk_overlay_new();
    scrim = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(scrim, "scrim");
    GtkGesture *g = gtk_gesture_click_new();
    g_signal_connect(g, "released", G_CALLBACK(on_scrim), NULL);

    gtk_overlay_set_child(GTK_OVERLAY(ov), scrim);

    panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(panel, "panel");
    gtk_widget_set_size_request(panel, 560, -1);

    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(head, 8); gtk_widget_set_margin_end(head, 8);
    for (int i = 0; i < T_N; i++) {
        tabs[i] = gtk_button_new_with_label(TAB_NAME[i]);
        gtk_widget_add_css_class(tabs[i], "tab");
        gtk_widget_set_focus_on_click(tabs[i], FALSE);
        gtk_widget_set_tooltip_text(tabs[i], i == 0 ? "Ctrl+1" : i == 1 ? "Ctrl+2" : "Ctrl+3");
        g_signal_connect(tabs[i], "clicked", G_CALLBACK(on_tab), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(head), tabs[i]);
    }
    ctx_lbl = gtk_label_new("");
    gtk_widget_add_css_class(ctx_lbl, "ctx");
    gtk_widget_set_hexpand(ctx_lbl, TRUE);
    gtk_label_set_xalign(GTK_LABEL(ctx_lbl), 1);
    gtk_label_set_ellipsize(GTK_LABEL(ctx_lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(ctx_lbl), 34);
    gtk_box_append(GTK_BOX(head), ctx_lbl);

    listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), GTK_SELECTION_NONE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(listbox), TRUE);
    g_signal_connect(listbox, "row-activated", G_CALLBACK(on_row), NULL);
    scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), 420);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), listbox);

    entry = gtk_entry_new();
    gtk_widget_add_css_class(entry, "add");
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Новая заметка, Enter чтобы добавить");
    gtk_widget_set_margin_start(entry, 8); gtk_widget_set_margin_end(entry, 8);
    g_signal_connect(entry, "activate", G_CALLBACK(on_add), NULL);

    GtkWidget *foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(foot, 10); gtk_widget_set_margin_end(foot, 8);
    GtkWidget *hint = gtk_label_new("Esc закрыть · Ctrl+Tab вкладки · клик отмечает");
    gtk_widget_add_css_class(hint, "hint");
    gtk_widget_set_hexpand(hint, TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    GtkWidget *pin = gtk_button_new_with_label("Закрепить стикером");
    gtk_widget_add_css_class(pin, "pin");
    gtk_widget_set_focus_on_click(pin, FALSE);
    g_signal_connect(pin, "clicked", G_CALLBACK(on_pin), NULL);
    gtk_box_append(GTK_BOX(foot), hint);
    gtk_box_append(GTK_BOX(foot), pin);

    gtk_box_append(GTK_BOX(panel), head);
    gtk_box_append(GTK_BOX(panel), scroller);
    gtk_box_append(GTK_BOX(panel), entry);
    gtk_box_append(GTK_BOX(panel), foot);
    /* панель живёт в GtkFixed и двигается трансформацией — без пересчёта раскладки на каждом кадре */
    fixed = gtk_fixed_new();
    gtk_widget_set_can_target(fixed, TRUE);
    gtk_fixed_put(GTK_FIXED(fixed), panel, 0, 0);
    gtk_overlay_add_overlay(GTK_OVERLAY(ov), fixed);
    gtk_widget_add_controller(fixed, GTK_EVENT_CONTROLLER(g));
    gtk_window_set_child(GTK_WINDOW(win), ov);

    GtkEventController *kc = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_CAPTURE);
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(win, kc);
    g_signal_connect(win, "close-request", G_CALLBACK(keep_open), NULL);
}

static int on_cmdline(GApplication *a, GApplicationCommandLine *cl, gpointer u) {
    int argc; char **argv = g_application_command_line_get_arguments(cl, &argc);
    gboolean hidden = FALSE;
    for (int i = 1; i < argc; i++) if (g_str_equal(argv[i], "--hidden")) hidden = TRUE;
    g_strfreev(argv);
    if (!win) {
        build();
        g_application_hold(a);   /* жить в фоне, даже когда окно спрятано */
        restore_stickies();
        if (!hidden) show();
    } else if (!hidden) {
        if (shown) hide(); else show();
    }
    return 0;
}

int main(int argc, char **argv) {
    notes_dir = g_build_filename(g_get_home_dir(), "notes", NULL);
    stickies = g_ptr_array_new();
    app = gtk_application_new("local.dropnotes", G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(app, "command-line", G_CALLBACK(on_cmdline), NULL);
    int rc = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return rc;
}
