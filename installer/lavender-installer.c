/* lavender-installer — графический установщик Lavender OS.
 *
 * Собирает ответы, показывает понятный план и запускает install.sh в режиме LAVENDER_AUTO;
 * сам ничего на диски не пишет. Запускается в live-ISO внутри cage от root.
 *
 *   lavender-installer                      обычный запуск
 *   lavender-installer --demo FILE.json     демо: выдуманные диски, установка понарошку
 *     [--page welcome|disk|account|extras|summary|progress] [--pick /dev/X] [--mode free|disk|part]
 */
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define GIB (1024.0 * 1024 * 1024)
#define MIN_ROOT (20ULL << 30)   /* меньше 20 ГБ под систему не даём */
#define ESP_SIZE (512ULL << 20)
#define ESP_MIN_FREE (15ULL << 20)

/* ── палитра «Лаванда» ─────────────────────────────────── */
#define C_BG "#18161d"
#define C_SURF "#211e28"
#define C_LINE "#2a2633"
#define C_SEL "#3a3545"
#define C_MUTED "#6e6880"
#define C_SUB "#b9b2c6"
#define C_FG "#e6e1ef"
#define C_LAV "#bba7e3"
#define C_ROSE "#e3a7b5"
#define C_AMBER "#e3cfa6"
#define C_SAGE "#a7cdb6"
#define C_BLUE "#a9b8e8"
#define C_PINK "#d9b8dc"

enum { K_WIN, K_ESP, K_LINUX, K_DATA, K_OTHER, K_FREE };
enum { M_NONE = -1, M_FREE, M_DISK, M_PART };

typedef struct {
    char *path, *name, *fstype, *label, *parttype;
    guint64 start, size;   /* байты */
    int kind;
    gboolean win_boot, bitlocker;
    gint64 esp_free;       /* -1 — неизвестно */
} Part;

typedef struct {
    char *path, *name, *model, *tran;
    guint64 size;
    gboolean live, has_win, bitlocker, has_data;
    GPtrArray *parts;      /* Part* */
    guint64 free_start, free_size; /* самая большая свободная область */
    GtkWidget *card, *bar;
} Disk;

static GPtrArray *disks;
static gboolean demo, have_nvidia, uefi, online;
static JsonObject *demo_obj;
static const char *inst_dir = "/opt/lavender-os";

/* выбор пользователя */
static Disk *sel_disk;
static int sel_mode = M_NONE;
static Part *sel_part;

/* виджеты */
static GtkWidget *win, *stack, *btn_back, *btn_next, *dots[6];
static GtkWidget *w_checks, *w_wifi_box, *w_wifi_dd, *w_wifi_pw, *w_wifi_status;
static GtkWidget *w_disk_list, *w_method_box, *w_rb[3], *w_rb_desc[3], *w_part_dd, *w_danger, *w_danger_lbl,
    *w_confirm_entry, *w_confirm_hint, *w_part_check, *w_disk_hint;
static GtkWidget *w_host, *w_user, *w_pw1, *w_pw2, *w_tz;
static GtkWidget *w_opt[3], *w_gpu_box, *w_gpu_rb[2];
static GtkWidget *w_summary;
static GtkWidget *w_prog_title, *w_prog_step, *w_prog_bar, *w_log, *w_log_exp, *w_prog_done, *w_prog_err;
static GPtrArray *part_choices; /* Part* для выпадающего списка «заменить раздел» */
static gboolean installing;

static const char *pages[] = {"welcome", "disk", "account", "extras", "summary", "progress"};
#define NPAGES 6
static int page;

/* ── утилиты ───────────────────────────────────────────── */

static char *human(guint64 b) {
    double g = b / GIB;
    if (g >= 1000) return g_strdup_printf("%.1f ТБ", g / 1024);
    if (g >= 10) return g_strdup_printf("%.0f ГБ", g);
    if (g >= 1) return g_strdup_printf("%.1f ГБ", g);
    return g_strdup_printf("%.0f МБ", b / (1024.0 * 1024));
}

static const char *jstr(JsonObject *o, const char *k) {
    if (!o || !json_object_has_member(o, k)) return NULL;
    JsonNode *n = json_object_get_member(o, k);
    if (JSON_NODE_HOLDS_NULL(n) || json_node_get_value_type(n) != G_TYPE_STRING) return NULL;
    const char *s = json_node_get_string(n);
    return s && *s ? s : NULL;
}
static guint64 jnum(JsonObject *o, const char *k) {
    if (!o || !json_object_has_member(o, k)) return 0;
    JsonNode *n = json_object_get_member(o, k);
    if (JSON_NODE_HOLDS_NULL(n)) return 0;
    GType t = json_node_get_value_type(n);
    if (t == G_TYPE_INT64) return (guint64)json_node_get_int(n);
    if (t == G_TYPE_DOUBLE) return (guint64)json_node_get_double(n);
    if (t == G_TYPE_STRING) return g_ascii_strtoull(json_node_get_string(n), NULL, 10);
    return 0;
}
static gboolean jbool(JsonObject *o, const char *k) {
    if (!o || !json_object_has_member(o, k)) return FALSE;
    JsonNode *n = json_object_get_member(o, k);
    if (json_node_get_value_type(n) == G_TYPE_BOOLEAN) return json_node_get_boolean(n);
    if (json_node_get_value_type(n) == G_TYPE_STRING) return !g_strcmp0(json_node_get_string(n), "1");
    return FALSE;
}

static char *run(const char *const *argv) {
    char *out = NULL;
    if (!g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out,
                      NULL, NULL, NULL))
        return NULL;
    return out;
}

static void add_class(GtkWidget *w, const char *c) { gtk_widget_add_css_class(w, c); }

static GtkWidget *label(const char *text, const char *cls, gboolean wrap) {
    GtkWidget *l = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(l), text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    if (wrap) { gtk_label_set_wrap(GTK_LABEL(l), TRUE); gtk_label_set_wrap_mode(GTK_LABEL(l), PANGO_WRAP_WORD_CHAR); }
    if (cls) add_class(l, cls);
    return l;
}

/* ── диски ─────────────────────────────────────────────── */

static const char *kind_color(int k) {
    switch (k) {
    case K_WIN: return C_BLUE;
    case K_ESP: return C_AMBER;
    case K_LINUX: return C_SAGE;
    case K_DATA: return C_PINK;
    case K_FREE: return C_LINE;
    default: return C_MUTED;
    }
}

static char *part_title(Part *p) {
    const char *l = p->label;
    switch (p->kind) {
    case K_WIN:
        if (p->bitlocker) return g_strdup_printf("Windows (BitLocker)%s%s", l ? " · " : "", l ? l : "");
        if (p->parttype && g_str_has_prefix(p->parttype, "e3c9e316")) return g_strdup("служебный Microsoft");
        if (p->parttype && g_str_has_prefix(p->parttype, "de94bba4")) return g_strdup("восстановление Windows");
        if (!l || !g_ascii_strcasecmp(l, "Windows")) return g_strdup("Windows");
        return g_strdup_printf("Windows «%s»", l);
    case K_ESP: return g_strdup(p->win_boot ? "EFI (загрузчик Windows)" : "EFI");
    case K_LINUX: return g_strdup_printf("Linux %s%s%s%s", p->fstype ? p->fstype : "", l ? " «" : "", l ? l : "", l ? "»" : "");
    case K_DATA: return g_strdup_printf("%s%s%s%s", p->fstype ? p->fstype : "данные", l ? " «" : "", l ? l : "", l ? "»" : "");
    case K_FREE: return g_strdup("свободно");
    default: return g_strdup(p->fstype ? p->fstype : "без файловой системы");
    }
}

static void classify(Part *p) {
    char *pt = p->parttype ? g_ascii_strdown(p->parttype, -1) : g_strdup("");
    const char *fs = p->fstype ? p->fstype : "";
    g_free(p->parttype);
    p->parttype = pt;
    p->bitlocker = !g_ascii_strcasecmp(fs, "BitLocker");
    if (g_str_has_prefix(pt, "c12a7328")) p->kind = K_ESP;
    else if (p->bitlocker || !strcmp(fs, "ntfs") || g_str_has_prefix(pt, "e3c9e316") || g_str_has_prefix(pt, "de94bba4"))
        p->kind = K_WIN;
    else if (g_str_has_prefix(pt, "ebd0a0a2") && !*fs) p->kind = K_WIN;
    else if (!strcmp(fs, "ext4") || !strcmp(fs, "ext3") || !strcmp(fs, "ext2") || !strcmp(fs, "btrfs") ||
             !strcmp(fs, "xfs") || !strcmp(fs, "f2fs") || !strcmp(fs, "swap") || g_str_has_prefix(pt, "0fc63daf") ||
             g_str_has_prefix(pt, "0657fd6d"))
        p->kind = K_LINUX;
    else if (*fs) p->kind = K_DATA;
    else p->kind = K_OTHER;
}

/* проверка EFI-раздела: есть ли загрузчик Windows и сколько свободно (только root, только чтение) */
static void probe_esp(Part *p) {
    if (demo) {
        JsonArray *wb = json_object_has_member(demo_obj, "winboot") ? json_object_get_array_member(demo_obj, "winboot") : NULL;
        for (guint i = 0; wb && i < json_array_get_length(wb); i++)
            if (!g_strcmp0(json_array_get_string_element(wb, i), p->path)) p->win_boot = TRUE;
        JsonObject *ef = json_object_has_member(demo_obj, "espfree") ? json_object_get_object_member(demo_obj, "espfree") : NULL;
        p->esp_free = ef && json_object_has_member(ef, p->path) ? (gint64)jnum(ef, p->path) : 50 << 20;
        return;
    }
    p->esp_free = -1;
    char dir[] = "/run/lavender-esp-XXXXXX";
    if (!g_mkdtemp(dir)) return;
    const char *mnt[] = {"mount", "-o", "ro", p->path, dir, NULL};
    int st = -1;
    g_spawn_sync(NULL, (char **)mnt, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                 NULL, NULL, NULL, NULL, &st, NULL);
    if (st == 0) {
        char *f = g_build_filename(dir, "EFI", "Microsoft", "Boot", "bootmgfw.efi", NULL);
        p->win_boot = g_file_test(f, G_FILE_TEST_EXISTS);
        g_free(f);
        struct statvfs sv;
        if (statvfs(dir, &sv) == 0) p->esp_free = (gint64)sv.f_bavail * sv.f_frsize;
        const char *um[] = {"umount", dir, NULL};
        g_spawn_sync(NULL, (char **)um, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
    }
    rmdir(dir);
}

static void find_free(Disk *d) {
    d->free_start = d->free_size = 0;
    if (demo) {
        JsonObject *fr = json_object_has_member(demo_obj, "free") ? json_object_get_object_member(demo_obj, "free") : NULL;
        JsonArray *a = fr && json_object_has_member(fr, d->path) ? json_object_get_array_member(fr, d->path) : NULL;
        for (guint i = 0; a && i < json_array_get_length(a); i++) {
            JsonArray *r = json_array_get_array_element(a, i);
            guint64 s = json_array_get_int_element(r, 0), z = json_array_get_int_element(r, 1);
            if (z > d->free_size) { d->free_start = s; d->free_size = z; }
        }
        return;
    }
    if (d->parts->len == 0) { d->free_start = 1 << 20; d->free_size = d->size > (2u << 20) ? d->size - (2u << 20) : 0; return; }
    const char *argv[] = {"sfdisk", "-F", "-q", d->path, NULL};
    char *out = run(argv);
    if (!out) return;
    const char *ssv[] = {"blockdev", "--getss", d->path, NULL};
    char *ss = run(ssv);
    guint64 sec = ss ? g_ascii_strtoull(ss, NULL, 10) : 512;
    if (!sec) sec = 512;
    g_free(ss);
    char **lines = g_strsplit(out, "\n", -1);
    for (int i = 1; lines[i]; i++) {
        guint64 a, b, n;
        if (sscanf(lines[i], " %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT, &a, &b, &n) == 3 &&
            n * sec > d->free_size) {
            d->free_start = a * sec;
            d->free_size = n * sec;
        }
    }
    g_strfreev(lines);
    g_free(out);
}

static char *live_disk_path(void) {
    if (demo) return g_strdup(jstr(demo_obj, "live"));
    const char *fm[] = {"findmnt", "-no", "SOURCE", "/run/initramfs/live", NULL};
    char *src = run(fm);
    if (!src) return NULL;
    g_strstrip(src);
    if (!*src) { g_free(src); return NULL; }
    const char *pk[] = {"lsblk", "-no", "PKNAME", src, NULL};
    char *p = run(pk);
    char *res;
    if (p && *g_strstrip(p)) res = g_strdup_printf("/dev/%s", p);
    else res = g_strdup(src);
    g_free(p);
    g_free(src);
    return res;
}

static gint by_start(gconstpointer a, gconstpointer b) {
    const Part *x = *(Part **)a, *y = *(Part **)b;
    return x->start < y->start ? -1 : x->start > y->start;
}

static void load_disks(void) {
    disks = g_ptr_array_new();
    JsonParser *jp = json_parser_new();
    JsonObject *root = NULL;
    if (demo) {
        root = json_object_get_object_member(demo_obj, "lsblk");
    } else {
        const char *argv[] = {"lsblk", "-J", "-b", "-o", "NAME,PATH,TYPE,SIZE,FSTYPE,LABEL,PARTTYPE,MODEL,TRAN,START", NULL};
        char *out = run(argv);
        if (out && json_parser_load_from_data(jp, out, -1, NULL)) root = json_node_get_object(json_parser_get_root(jp));
        g_free(out);
    }
    char *live = live_disk_path();
    JsonArray *bd = root && json_object_has_member(root, "blockdevices") ? json_object_get_array_member(root, "blockdevices") : NULL;
    for (guint i = 0; bd && i < json_array_get_length(bd); i++) {
        JsonObject *o = json_array_get_object_element(bd, i);
        if (g_strcmp0(jstr(o, "type"), "disk")) continue;
        if (jnum(o, "size") < (1ULL << 30)) continue;
        Disk *d = g_new0(Disk, 1);
        d->path = g_strdup(jstr(o, "path"));
        d->name = g_strdup(jstr(o, "name"));
        d->model = g_strdup(jstr(o, "model") ? jstr(o, "model") : "Диск");
        d->tran = g_strdup(jstr(o, "tran"));
        d->size = jnum(o, "size");
        d->live = live && !g_strcmp0(live, d->path);
        d->parts = g_ptr_array_new();
        JsonArray *ch = json_object_has_member(o, "children") ? json_object_get_array_member(o, "children") : NULL;
        for (guint j = 0; ch && j < json_array_get_length(ch); j++) {
            JsonObject *c = json_array_get_object_element(ch, j);
            if (g_strcmp0(jstr(c, "type"), "part")) continue;
            Part *p = g_new0(Part, 1);
            p->path = g_strdup(jstr(c, "path"));
            p->name = g_strdup(jstr(c, "name"));
            p->fstype = g_strdup(jstr(c, "fstype"));
            p->label = g_strdup(jstr(c, "label"));
            p->parttype = g_strdup(jstr(c, "parttype"));
            p->size = jnum(c, "size");
            p->start = jnum(c, "start") * 512;
            p->esp_free = -1;
            classify(p);
            if (p->kind == K_ESP && !d->live) probe_esp(p);
            if (p->kind == K_WIN || p->win_boot) d->has_win = TRUE;
            if (p->bitlocker) d->bitlocker = TRUE;
            if (p->kind != K_OTHER && p->kind != K_FREE) d->has_data = TRUE;
            g_ptr_array_add(d->parts, p);
        }
        g_ptr_array_sort(d->parts, by_start);
        if (!d->live) find_free(d);
        g_ptr_array_add(disks, d);
    }
    g_free(live);
    g_object_unref(jp);
}

/* EFI-раздел, который можно переиспользовать: сначала на этом же диске, потом с Windows на других */
static Part *pick_esp(Disk *prefer) {
    Part *best = NULL;
    int best_score = -1;
    for (guint i = 0; i < disks->len; i++) {
        Disk *d = disks->pdata[i];
        if (d->live) continue;
        for (guint j = 0; j < d->parts->len; j++) {
            Part *p = d->parts->pdata[j];
            if (p->kind != K_ESP || p->esp_free < (gint64)ESP_MIN_FREE) continue;
            int score = (d == prefer ? 2 : 0) + (p->win_boot ? 1 : 0);
            if (score > best_score) { best = p; best_score = score; }
        }
    }
    return best;
}

static gboolean free_ok(Disk *d, Part **esp_out) {
    Part *esp = pick_esp(d);
    if (esp && esp->esp_free >= 0) {
        Disk *ed = NULL;
        for (guint i = 0; i < disks->len; i++)
            for (guint j = 0; j < ((Disk *)disks->pdata[i])->parts->len; j++)
                if (((Disk *)disks->pdata[i])->parts->pdata[j] == esp) ed = disks->pdata[i];
        if (ed != d) esp = NULL; /* в режиме «свободное место» берём EFI только с этого же диска */
    }
    if (esp_out) *esp_out = esp;
    return d->free_size >= MIN_ROOT + (esp ? 0 : ESP_SIZE);
}

static gboolean part_eligible(Part *p) {
    return (p->kind == K_LINUX || p->kind == K_DATA || p->kind == K_OTHER) && p->size >= MIN_ROOT &&
           g_strcmp0(p->fstype, "vfat") != 0;
}

/* ── полоска разделов ──────────────────────────────────── */

static void set_rgb(cairo_t *cr, const char *hexs, double a) {
    unsigned v = (unsigned)strtoul(hexs + 1, NULL, 16);
    cairo_set_source_rgba(cr, ((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0, a);
}

static void rounded(cairo_t *cr, double x, double y, double w, double h, double r) {
    r = fmin(r, fmin(w, h) / 2);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 1.5 * G_PI);
    cairo_close_path(cr);
}

static void seg_text(cairo_t *cr, GtkWidget *w, double x, double wpx, double h, const char *txt, const char *col) {
    if (wpx < 40) return;
    PangoLayout *pl = gtk_widget_create_pango_layout(w, txt);
    PangoFontDescription *fd = pango_font_description_from_string("Inter Bold 9");
    pango_layout_set_font_description(pl, fd);
    pango_layout_set_width(pl, (int)((wpx - 10) * PANGO_SCALE));
    pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
    int tw, th;
    pango_layout_get_pixel_size(pl, &tw, &th);
    set_rgb(cr, col, 1);
    cairo_move_to(cr, x + 6, (h - th) / 2.0);
    pango_cairo_show_layout(cr, pl);
    pango_font_description_free(fd);
    g_object_unref(pl);
}

static void draw_bar(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer data) {
    Disk *d = data;
    rounded(cr, 0, 0, w, h, 9);
    cairo_clip(cr);
    set_rgb(cr, C_LINE, 1);
    cairo_paint(cr);
    gboolean plan = d == sel_disk && sel_mode != M_NONE;
    if (plan && sel_mode == M_DISK) {
        set_rgb(cr, C_LAV, 1);
        cairo_paint(cr);
        seg_text(cr, GTK_WIDGET(a), 0, w, h, "Lavender OS — весь диск", C_BG);
        return;
    }
    double k = (double)w / d->size;
    for (guint i = 0; i < d->parts->len; i++) {
        Part *p = d->parts->pdata[i];
        double x = p->start * k, pw = fmax(6, p->size * k);
        gboolean repl = plan && sel_mode == M_PART && p == sel_part;
        set_rgb(cr, repl ? C_LAV : kind_color(p->kind), repl ? 1 : 0.85);
        cairo_rectangle(cr, x, 0, pw - 1.5, h);
        cairo_fill(cr);
        char *t = repl ? g_strdup("Lavender OS") : part_title(p);
        seg_text(cr, GTK_WIDGET(a), x, pw, h, t, C_BG);
        g_free(t);
    }
    if (plan && sel_mode == M_FREE && d->free_size) {
        double x = d->free_start * k, pw = d->free_size * k;
        set_rgb(cr, C_LAV, 1);
        cairo_rectangle(cr, x, 0, pw, h);
        cairo_fill(cr);
        seg_text(cr, GTK_WIDGET(a), x, pw, h, "Lavender OS (новый раздел)", C_BG);
    } else if (d->free_size >= (1ULL << 30)) {
        double x = d->free_start * k, pw = d->free_size * k;
        set_rgb(cr, C_MUTED, 0.9);
        cairo_set_line_width(cr, 1);
        double dash[] = {4, 3};
        cairo_set_dash(cr, dash, 2, 0);
        cairo_rectangle(cr, x + 1.5, 1.5, pw - 3, h - 3);
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0);
        char *s = human(d->free_size), *t = g_strdup_printf("свободно %s", s);
        seg_text(cr, GTK_WIDGET(a), x, pw, h, t, C_SUB);
        g_free(s); g_free(t);
    }
}

static void redraw_bars(void) {
    for (guint i = 0; i < disks->len; i++) gtk_widget_queue_draw(((Disk *)disks->pdata[i])->bar);
}

/* ── проверка готовности страниц ───────────────────────── */

static gboolean acc_valid(char **why);
static gboolean disk_valid(char **why);

static void update_nav(void) {
    gboolean ok = TRUE;
    char *why = NULL;
    if (page == 0) ok = uefi && online;
    else if (page == 1) ok = disk_valid(&why);
    else if (page == 2) {
        ok = acc_valid(&why);
    }
    gtk_label_set_markup(GTK_LABEL(w_disk_hint), why && !ok ? why : "");
    g_free(why);
    gtk_widget_set_sensitive(btn_next, ok && !installing);
    gtk_widget_set_opacity(btn_back, page > 0 && page < NPAGES - 1);
    gtk_widget_set_sensitive(btn_back, page > 0 && page < NPAGES - 1);
    gtk_widget_set_visible(btn_next, page < NPAGES - 1);
    gtk_button_set_label(GTK_BUTTON(btn_next), page == NPAGES - 2 ? "Установить" : "Далее");
    if (page == NPAGES - 2) add_class(btn_next, "danger-action");
    else gtk_widget_remove_css_class(btn_next, "danger-action");
    for (int i = 0; i < NPAGES; i++) {
        if (i == page) add_class(dots[i], "active");
        else gtk_widget_remove_css_class(dots[i], "active");
    }
}

/* ── страница 1: приветствие ───────────────────────────── */

static void render_checks(void);

/* сеть по кабелю поднимается через несколько секунд после загрузки —
   пока интернета нет, проверяем снова каждые 4 секунды */
static guint online_timer;
static void check_online(void);
static gboolean online_retry(gpointer d) {
    (void)d;
    online_timer = 0;
    if (!online && !installing) check_online();
    return G_SOURCE_REMOVE;
}

static void online_done(GObject *o, GAsyncResult *r, gpointer d) {
    (void)d;
    GError *e = NULL;
    g_subprocess_wait_check_finish(G_SUBPROCESS(o), r, &e);
    online = e == NULL;
    g_clear_error(&e);
    render_checks();
    update_nav();
    if (!online && !online_timer) online_timer = g_timeout_add_seconds(4, online_retry, NULL);
}

static void check_online(void) {
    if (demo) { online = jbool(demo_obj, "online"); render_checks(); update_nav(); return; }
    GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL,
                                      "curl", "-sI", "--max-time", "6", "https://repo-default.voidlinux.org/current/", NULL);
    if (!p) { online = FALSE; render_checks(); update_nav(); return; }
    g_subprocess_wait_check_async(p, NULL, online_done, NULL);
    g_object_unref(p);
}

static void wifi_list_done(GObject *o, GAsyncResult *r, gpointer d) {
    (void)d;
    char *out = NULL;
    g_subprocess_communicate_utf8_finish(G_SUBPROCESS(o), r, &out, NULL, NULL);
    GtkStringList *sl = gtk_string_list_new(NULL);
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    char **lines = g_strsplit(out ? out : "", "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *ssid = lines[i];
        char *c = strchr(ssid, ':');
        if (c) *c = 0;
        if (*ssid && !g_hash_table_contains(seen, ssid)) {
            g_hash_table_add(seen, ssid);
            gtk_string_list_append(sl, ssid);
        }
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(w_wifi_dd), G_LIST_MODEL(sl));
    gtk_label_set_text(GTK_LABEL(w_wifi_status),
                       g_list_model_get_n_items(G_LIST_MODEL(sl)) ? "" : "Сети не найдены. Подключи кабель или нажми «Найти» ещё раз.");
    g_object_unref(sl);
    g_hash_table_destroy(seen);
    g_strfreev(lines);
    g_free(out);
}

static void wifi_scan(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    gtk_label_set_text(GTK_LABEL(w_wifi_status), "Ищу сети…");
    if (demo) {
        const char *n[] = {"Felix.NET", "Home_5G", "Кафе", NULL};
        GtkStringList *sl = gtk_string_list_new(n);
        gtk_drop_down_set_model(GTK_DROP_DOWN(w_wifi_dd), G_LIST_MODEL(sl));
        g_object_unref(sl);
        gtk_label_set_text(GTK_LABEL(w_wifi_status), "");
        return;
    }
    GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL, "nmcli",
                                      "-t", "-f", "SSID,SIGNAL", "dev", "wifi", "list", "--rescan", "yes", NULL);
    if (p) { g_subprocess_communicate_utf8_async(p, NULL, NULL, wifi_list_done, NULL); g_object_unref(p); }
}

static void wifi_conn_done(GObject *o, GAsyncResult *r, gpointer d) {
    (void)d;
    char *out = NULL, *err = NULL;
    g_subprocess_communicate_utf8_finish(G_SUBPROCESS(o), r, &out, &err, NULL);
    gboolean ok = g_subprocess_get_successful(G_SUBPROCESS(o));
    gtk_label_set_text(GTK_LABEL(w_wifi_status), ok ? "Подключено. Проверяю интернет…" : "Не удалось подключиться — проверь пароль.");
    g_free(out); g_free(err);
    check_online();
}

static void wifi_connect(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    GtkStringObject *so = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(w_wifi_dd));
    if (!so || g_str_has_prefix(gtk_string_object_get_string(so), "—")) return;
    const char *ssid = gtk_string_object_get_string(so), *pw = gtk_editable_get_text(GTK_EDITABLE(w_wifi_pw));
    gtk_label_set_text(GTK_LABEL(w_wifi_status), "Подключаюсь…");
    if (demo) { online = TRUE; render_checks(); update_nav(); return; }
    GSubprocess *p;
    if (*pw) p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, NULL, "nmcli", "dev",
                                  "wifi", "connect", ssid, "password", pw, NULL);
    else p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, NULL, "nmcli", "dev",
                              "wifi", "connect", ssid, NULL);
    if (p) { g_subprocess_communicate_utf8_async(p, NULL, NULL, wifi_conn_done, NULL); g_object_unref(p); }
}

static void recheck(GtkButton *b, gpointer d) { (void)b; (void)d; check_online(); }

static GtkWidget *check_row(gboolean ok, const char *title, const char *desc) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    GtkWidget *ic = gtk_label_new(ok ? "✓" : "✗");
    add_class(ic, ok ? "ok-mark" : "bad-mark");
    gtk_widget_set_valign(ic, GTK_ALIGN_START);
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_append(GTK_BOX(col), label(title, "row-title", FALSE));
    if (desc) gtk_box_append(GTK_BOX(col), label(desc, "dim", TRUE));
    gtk_box_append(GTK_BOX(row), ic);
    gtk_box_append(GTK_BOX(row), col);
    return row;
}

static void render_checks(void) {
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(w_checks))) gtk_box_remove(GTK_BOX(w_checks), c);
    gtk_box_append(GTK_BOX(w_checks), check_row(uefi, "Режим загрузки UEFI",
        uefi ? NULL : "Компьютер загрузился в устаревшем режиме (Legacy/CSM). Перезагрузись и в меню загрузки выбери флешку с пометкой «UEFI»."));
    gtk_box_append(GTK_BOX(w_checks), check_row(online, "Интернет",
        online ? NULL : "Пакеты скачиваются из сети. Если кабель подключён — подожди несколько секунд, проверка повторяется сама. Или выбери Wi-Fi ниже."));
    int nwin = 0;
    for (guint i = 0; i < disks->len; i++) nwin += ((Disk *)disks->pdata[i])->has_win;
    if (nwin) gtk_box_append(GTK_BOX(w_checks), check_row(TRUE, "Найдена Windows",
        "Её можно оставить: при включении компьютера появится меню выбора — Lavender OS или Windows."));
    gtk_widget_set_visible(w_wifi_box, !online);
}

static GtkWidget *page_welcome(void) {
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_box_append(GTK_BOX(b), label("✿ Lavender OS", "hero", FALSE));
    gtk_box_append(GTK_BOX(b), label("Мягкий рабочий стол niri + noctalia на основе Void Linux.\n"
                                      "Установщик спросит, куда ставить систему, и перед началом покажет, что именно произойдёт. "
                                      "До нажатия «Установить» на дисках ничего не меняется.", "lead", TRUE));
    w_checks = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    add_class(w_checks, "card");
    gtk_box_append(GTK_BOX(b), w_checks);

    w_wifi_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    add_class(w_wifi_box, "card");
    gtk_box_append(GTK_BOX(w_wifi_box), label("<b>Wi-Fi</b>", NULL, FALSE));
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    const char *nonets[] = {"— нажми «Найти сети» —", NULL};
    w_wifi_dd = gtk_drop_down_new(G_LIST_MODEL(gtk_string_list_new(nonets)), NULL);
    gtk_widget_set_hexpand(w_wifi_dd, TRUE);
    GtkWidget *scan = gtk_button_new_with_label("Найти сети");
    g_signal_connect(scan, "clicked", G_CALLBACK(wifi_scan), NULL);
    gtk_box_append(GTK_BOX(row), w_wifi_dd);
    gtk_box_append(GTK_BOX(row), scan);
    gtk_box_append(GTK_BOX(w_wifi_box), row);
    GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    w_wifi_pw = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(w_wifi_pw), TRUE);
    g_object_set(w_wifi_pw, "placeholder-text", "пароль сети", NULL);
    gtk_widget_set_hexpand(w_wifi_pw, TRUE);
    GtkWidget *conn = gtk_button_new_with_label("Подключить");
    add_class(conn, "suggested-action");
    g_signal_connect(conn, "clicked", G_CALLBACK(wifi_connect), NULL);
    GtkWidget *re = gtk_button_new_with_label("Проверить снова");
    g_signal_connect(re, "clicked", G_CALLBACK(recheck), NULL);
    gtk_box_append(GTK_BOX(row2), w_wifi_pw);
    gtk_box_append(GTK_BOX(row2), conn);
    gtk_box_append(GTK_BOX(row2), re);
    gtk_box_append(GTK_BOX(w_wifi_box), row2);
    w_wifi_status = label("", "dim", TRUE);
    gtk_box_append(GTK_BOX(w_wifi_box), w_wifi_status);
    gtk_box_append(GTK_BOX(b), w_wifi_box);
    return b;
}

/* ── страница 2: диск ──────────────────────────────────── */

static void rebuild_method(void);

/* после выбора диска/способа прокрутить к блоку «Способ», чтобы его не пропустили */
static gboolean scroll_to_method(gpointer d) {
    (void)d;
    GtkWidget *vp = gtk_widget_get_ancestor(w_method_box, GTK_TYPE_VIEWPORT);
    if (vp && gtk_widget_get_visible(w_method_box))
        gtk_viewport_scroll_to(GTK_VIEWPORT(vp), gtk_widget_is_visible(w_danger) ? w_danger : w_method_box, NULL);
    return G_SOURCE_REMOVE;
}

static void on_disk_toggled(GtkToggleButton *t, gpointer data) {
    if (!gtk_toggle_button_get_active(t)) return;
    if (sel_disk != data) { sel_disk = data; sel_mode = M_NONE; sel_part = NULL; }
    rebuild_method();
    redraw_bars();
    update_nav();
    g_timeout_add(120, scroll_to_method, NULL);
}

static GtkWidget *legend_item(const char *color, const char *text) {
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *dot = gtk_label_new("●");
    char *css = g_strdup_printf("<span foreground='%s'>●</span>", color);
    gtk_label_set_markup(GTK_LABEL(dot), css);
    g_free(css);
    gtk_box_append(GTK_BOX(b), dot);
    gtk_box_append(GTK_BOX(b), label(text, "legend", FALSE));
    return b;
}

static GtkWidget *disk_card(Disk *d, GtkWidget *group) {
    GtkWidget *tb = gtk_toggle_button_new();
    add_class(tb, "disk-card");
    if (group) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(tb), GTK_TOGGLE_BUTTON(group));
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    char *sz = human(d->size);
    const char *tr = d->tran ? (!strcmp(d->tran, "nvme") ? "NVMe" : !strcmp(d->tran, "usb") ? "USB" :
                                !strcmp(d->tran, "sata") ? "SATA" : d->tran) : "";
    char *t = g_markup_printf_escaped("<b>%s</b>  <span foreground='" C_SUB "'>%s · %s%s%s</span>", d->model, d->name, sz,
                                      *tr ? " · " : "", tr);
    GtkWidget *tl = label(t, "disk-title", FALSE);
    gtk_label_set_ellipsize(GTK_LABEL(tl), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tl, TRUE);
    gtk_box_append(GTK_BOX(top), tl);
    if (d->live) gtk_box_append(GTK_BOX(top), label("флешка с установщиком", "badge-muted", FALSE));
    if (d->has_win) gtk_box_append(GTK_BOX(top), label("Windows", "badge-blue", FALSE));
    if (d->bitlocker) gtk_box_append(GTK_BOX(top), label("BitLocker", "badge-rose", FALSE));
    if (!d->has_data && !d->live) gtk_box_append(GTK_BOX(top), label("пустой", "badge-sage", FALSE));
    gtk_box_append(GTK_BOX(v), top);
    g_free(t); g_free(sz);

    d->bar = gtk_drawing_area_new();
    gtk_widget_set_size_request(d->bar, -1, 30);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(d->bar), draw_bar, d, NULL);
    gtk_box_append(GTK_BOX(v), d->bar);

    GtkWidget *leg = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(leg), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(leg), 6);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(leg), 18);
    gtk_widget_set_can_target(leg, FALSE);
    gtk_widget_set_can_focus(leg, FALSE);   /* Tab прыгает по дискам и кнопкам, а не по подписям */
    for (guint i = 0; i < d->parts->len; i++) {
        Part *p = d->parts->pdata[i];
        char *pt = part_title(p), *s = human(p->size), *txt = g_markup_printf_escaped("%s  <span foreground='" C_MUTED "'>%s · %s</span>", pt, p->name, s);
        gtk_flow_box_append(GTK_FLOW_BOX(leg), legend_item(kind_color(p->kind), txt));
        g_free(pt); g_free(s); g_free(txt);
    }
    if (d->free_size >= (1ULL << 30)) {
        char *s = human(d->free_size), *txt = g_strdup_printf("свободно  <span foreground='" C_MUTED "'>%s</span>", s);
        gtk_flow_box_append(GTK_FLOW_BOX(leg), legend_item(C_MUTED, txt));
        g_free(s); g_free(txt);
    }
    if (d->live) gtk_flow_box_append(GTK_FLOW_BOX(leg), label("Сюда ставить нельзя: с этой флешки сейчас работает установщик.", "dim", FALSE));
    gtk_box_append(GTK_BOX(v), leg);
    gtk_button_set_child(GTK_BUTTON(tb), v);
    gtk_widget_set_sensitive(tb, !d->live);
    g_signal_connect(tb, "toggled", G_CALLBACK(on_disk_toggled), d);
    d->card = tb;
    return tb;
}

static void on_mode(GtkCheckButton *c, gpointer data) {
    if (!gtk_check_button_get_active(c)) return;
    sel_mode = GPOINTER_TO_INT(data);
    if (sel_mode == M_PART && part_choices && part_choices->len) {
        guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(w_part_dd));
        sel_part = i < part_choices->len ? part_choices->pdata[i] : part_choices->pdata[0];
    } else sel_part = NULL;
    gtk_editable_set_text(GTK_EDITABLE(w_confirm_entry), "");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(w_part_check), FALSE);
    rebuild_method();
    redraw_bars();
    update_nav();
    g_timeout_add(120, scroll_to_method, NULL);
}

static void on_part_dd(GObject *o, GParamSpec *ps, gpointer d) {
    (void)ps; (void)d;
    guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(o));
    if (sel_mode == M_PART && part_choices && i < part_choices->len) sel_part = part_choices->pdata[i];
    gtk_check_button_set_active(GTK_CHECK_BUTTON(w_part_check), FALSE);
    rebuild_method();
    redraw_bars();
    update_nav();
}

static void on_confirm_changed(GtkEditable *e, gpointer d) { (void)e; (void)d; update_nav(); }
static void on_part_check(GtkCheckButton *c, gpointer d) { (void)c; (void)d; update_nav(); }

/* что пропадёт при стирании — список для красного предупреждения */
static char *loss_list(Disk *d) {
    GString *s = g_string_new(NULL);
    for (guint i = 0; i < d->parts->len; i++) {
        Part *p = d->parts->pdata[i];
        char *t = part_title(p), *z = human(p->size), *e = g_markup_printf_escaped("\n  • %s — %s (%s)", t, z, p->name);
        g_string_append(s, e);
        g_free(t); g_free(z); g_free(e);
    }
    return g_string_free(s, FALSE);
}

static gboolean disk_has_winboot(Disk *d) {
    for (guint i = 0; i < d->parts->len; i++)
        if (((Part *)d->parts->pdata[i])->win_boot) return TRUE;
    return FALSE;
}

static void rebuild_method(void) {
    gtk_widget_set_visible(w_method_box, sel_disk != NULL);
    if (!sel_disk) return;
    Disk *d = sel_disk;
    Part *esp = NULL;
    gboolean f_ok = free_ok(d, &esp);
    char *fs = human(d->free_size);
    char *desc0 = f_ok ? g_strdup_printf("Займёт свободные %s. Windows и остальные разделы не изменятся.%s", fs,
                                         esp ? " Загрузчик встанет рядом с загрузчиком Windows." : "")
                       : g_strdup_printf("Нужно хотя бы %s неразмеченного места, %s%s. "
                                         "Освободить место можно в Windows: «Управление дисками» → «Сжать том».",
                                         esp ? "20 ГБ" : "20,5 ГБ", d->free_size < (1ULL << 30) ? "а на диске его нет" : "сейчас есть только ",
                                         d->free_size < (1ULL << 30) ? "" : fs);
    g_free(fs);
    gtk_label_set_text(GTK_LABEL(w_rb_desc[M_FREE]), desc0);
    g_free(desc0);
    gtk_widget_set_sensitive(w_rb[M_FREE], f_ok);

    gboolean d_ok = d->size >= MIN_ROOT + ESP_SIZE;
    gtk_label_set_text(GTK_LABEL(w_rb_desc[M_DISK]),
                       d->has_data ? "Всё, что есть на этом диске, будет удалено. Подходит для отдельного диска под Lavender OS."
                                   : "Диск пустой — система займёт его целиком. Другие диски не затрагиваются.");
    gtk_widget_set_sensitive(w_rb[M_DISK], d_ok);

    if (part_choices) g_ptr_array_free(part_choices, TRUE);
    part_choices = g_ptr_array_new();
    GtkStringList *sl = gtk_string_list_new(NULL);
    for (guint i = 0; i < d->parts->len; i++) {
        Part *p = d->parts->pdata[i];
        if (!part_eligible(p)) continue;
        char *t = part_title(p), *z = human(p->size), *s = g_strdup_printf("%s — %s, %s", p->name, t, z);
        gtk_string_list_append(sl, s);
        g_ptr_array_add(part_choices, p);
        g_free(t); g_free(z); g_free(s);
    }
    Part *any_esp = pick_esp(d);
    gboolean p_ok = part_choices->len > 0 && any_esp;
    g_signal_handlers_block_by_func(w_part_dd, on_part_dd, NULL);
    guint keep = sel_part ? 0 : 0;
    for (guint i = 0; sel_part && i < part_choices->len; i++)
        if (part_choices->pdata[i] == sel_part) keep = i;
    gtk_drop_down_set_model(GTK_DROP_DOWN(w_part_dd), G_LIST_MODEL(sl));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(w_part_dd), keep);
    g_signal_handlers_unblock_by_func(w_part_dd, on_part_dd, NULL);
    g_object_unref(sl);
    gtk_label_set_text(GTK_LABEL(w_rb_desc[M_PART]),
                       p_ok ? "Выбранный раздел будет отформатирован, остальные не изменятся. Разделы Windows и EFI в списке не показываются."
                       : part_choices->len ? "Нет EFI-раздела, куда можно добавить загрузчик."
                                           : "Нет подходящих разделов (не Windows, не EFI, от 20 ГБ).");
    gtk_widget_set_sensitive(w_rb[M_PART], p_ok);
    gtk_widget_set_visible(w_part_dd, sel_mode == M_PART);

    /* если выбранный способ стал недоступен — сбросить */
    if (sel_mode != M_NONE && !gtk_widget_get_sensitive(w_rb[sel_mode])) sel_mode = M_NONE;
    for (int i = 0; i < 3; i++) {
        g_signal_handlers_block_by_func(w_rb[i], on_mode, GINT_TO_POINTER(i));
        gtk_check_button_set_active(GTK_CHECK_BUTTON(w_rb[i]), sel_mode == i);
        g_signal_handlers_unblock_by_func(w_rb[i], on_mode, GINT_TO_POINTER(i));
    }

    /* красное предупреждение */
    gboolean danger = (sel_mode == M_DISK && d->has_data) || (sel_mode == M_PART && sel_part);
    gtk_widget_set_visible(w_danger, danger);
    gtk_widget_set_visible(w_confirm_entry, sel_mode == M_DISK && d->has_data);
    gtk_widget_set_visible(w_confirm_hint, sel_mode == M_DISK && d->has_data);
    gtk_widget_set_visible(w_part_check, sel_mode == M_PART);
    if (sel_mode == M_DISK && d->has_data) {
        char *ll = loss_list(d);
        char *m = g_strdup_printf("<b>Будет удалено всё на диске %s:</b>%s%s", d->name, ll,
                                  disk_has_winboot(d) ? "\n\n<b>Windows на этом диске перестанет загружаться и будет удалена.</b>"
                                  : d->has_win ? "\n\n<b>Файлы на разделах Windows (NTFS) тоже пропадут.</b>" : "");
        gtk_label_set_markup(GTK_LABEL(w_danger_lbl), m);
        char *h = g_markup_printf_escaped("Если это точно тот диск, впиши его имя: <b>%s</b>", d->name);
        gtk_label_set_markup(GTK_LABEL(w_confirm_hint), h);
        g_free(ll); g_free(m); g_free(h);
    } else if (sel_mode == M_PART && sel_part) {
        char *t = part_title(sel_part), *z = human(sel_part->size);
        char *m = g_markup_printf_escaped("<b>Раздел %s (%s, %s) будет отформатирован — данные на нём пропадут.</b>",
                                          sel_part->name, t, z);
        gtk_label_set_markup(GTK_LABEL(w_danger_lbl), m);
        g_free(t); g_free(z); g_free(m);
    }
}

static gboolean disk_valid(char **why) {
    if (!sel_disk) { *why = g_strdup("Выбери диск."); return FALSE; }
    if (sel_mode == M_NONE) { *why = g_strdup("Выбери способ установки."); return FALSE; }
    if (sel_mode == M_DISK && sel_disk->has_data &&
        g_strcmp0(g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(w_confirm_entry)))), sel_disk->name)) {
        *why = g_markup_printf_escaped("Чтобы стереть диск, впиши его имя <b>%s</b> в поле выше.", sel_disk->name);
        return FALSE;
    }
    if (sel_mode == M_PART && (!sel_part || !gtk_check_button_get_active(GTK_CHECK_BUTTON(w_part_check)))) {
        *why = g_strdup("Подтверди, что данные на разделе не нужны.");
        return FALSE;
    }
    return TRUE;
}

static GtkWidget *page_disk(void) {
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_box_append(GTK_BOX(b), label("Куда установить?", "title", FALSE));
    gtk_box_append(GTK_BOX(b), label("Выбери диск. Цветная полоска — его разделы; после выбора способа она покажет, где появится Lavender OS.",
                                      "dim", TRUE));
    w_disk_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *group = NULL;
    for (guint i = 0; i < disks->len; i++) {
        GtkWidget *c = disk_card(disks->pdata[i], group);
        if (!group && gtk_widget_get_sensitive(c)) group = c;
        else if (!group) group = c;
        gtk_box_append(GTK_BOX(w_disk_list), c);
    }
    if (!disks->len) gtk_box_append(GTK_BOX(w_disk_list), label("Диски не найдены.", "dim", FALSE));
    gtk_box_append(GTK_BOX(b), w_disk_list);

    w_method_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    add_class(w_method_box, "card");
    gtk_box_append(GTK_BOX(w_method_box), label("<b>Способ</b>", NULL, FALSE));
    const char *titles[3] = {"Рядом с тем, что есть — в свободное место", "Стереть весь диск", "Заменить один раздел"};
    for (int i = 0; i < 3; i++) {
        w_rb[i] = gtk_check_button_new_with_label(titles[i]);
        add_class(w_rb[i], "method");
        if (i) gtk_check_button_set_group(GTK_CHECK_BUTTON(w_rb[i]), GTK_CHECK_BUTTON(w_rb[0]));
        g_signal_connect(w_rb[i], "toggled", G_CALLBACK(on_mode), GINT_TO_POINTER(i));
        w_rb_desc[i] = label("", "method-desc", TRUE);
        gtk_box_append(GTK_BOX(w_method_box), w_rb[i]);
        gtk_box_append(GTK_BOX(w_method_box), w_rb_desc[i]);
        if (i == M_PART) {
            w_part_dd = gtk_drop_down_new(G_LIST_MODEL(gtk_string_list_new(NULL)), NULL);
            gtk_widget_set_margin_start(w_part_dd, 30);
            g_signal_connect(w_part_dd, "notify::selected", G_CALLBACK(on_part_dd), NULL);
            gtk_box_append(GTK_BOX(w_method_box), w_part_dd);
        }
    }
    w_danger = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    add_class(w_danger, "danger");
    w_danger_lbl = label("", NULL, TRUE);
    w_confirm_hint = label("", NULL, TRUE);
    w_confirm_entry = gtk_entry_new();
    g_signal_connect(w_confirm_entry, "changed", G_CALLBACK(on_confirm_changed), NULL);
    w_part_check = gtk_check_button_new_with_label("Да, данные на этом разделе мне не нужны");
    g_signal_connect(w_part_check, "toggled", G_CALLBACK(on_part_check), NULL);
    gtk_box_append(GTK_BOX(w_danger), w_danger_lbl);
    gtk_box_append(GTK_BOX(w_danger), w_confirm_hint);
    gtk_box_append(GTK_BOX(w_danger), w_confirm_entry);
    gtk_box_append(GTK_BOX(w_danger), w_part_check);
    gtk_box_append(GTK_BOX(w_method_box), w_danger);
    gtk_widget_set_visible(w_method_box, FALSE);
    gtk_box_append(GTK_BOX(b), w_method_box);
    return b;
}

/* ── страница 3: аккаунт ───────────────────────────────── */

static gboolean acc_valid(char **why) {
    const char *h = gtk_editable_get_text(GTK_EDITABLE(w_host)), *u = gtk_editable_get_text(GTK_EDITABLE(w_user)),
               *p1 = gtk_editable_get_text(GTK_EDITABLE(w_pw1)), *p2 = gtk_editable_get_text(GTK_EDITABLE(w_pw2));
    if (!g_regex_match_simple("^[a-zA-Z0-9][a-zA-Z0-9-]{0,62}$", h, 0, 0)) {
        *why = g_strdup("Имя компьютера: латиница, цифры и дефис.");
        return FALSE;
    }
    if (!*u) { *why = g_strdup("Впиши имя пользователя."); return FALSE; }
    if (!g_regex_match_simple("^[a-z_][a-z0-9_-]{0,31}$", u, 0, 0) || !strcmp(u, "root")) {
        *why = g_strdup("Имя пользователя: строчная латиница, цифры, _ и -, начинается с буквы.");
        return FALSE;
    }
    if (!*p1) { *why = g_strdup("Придумай пароль."); return FALSE; }
    if (strcmp(p1, p2)) { *why = g_strdup("Пароли не совпадают."); return FALSE; }
    if (!gtk_drop_down_get_selected_item(GTK_DROP_DOWN(w_tz))) { *why = g_strdup("Выбери часовой пояс."); return FALSE; }
    return TRUE;
}

static void on_acc_changed(GtkEditable *e, gpointer d) { (void)e; (void)d; update_nav(); }

static void collect_zones(const char *base, const char *rel, GPtrArray *out) {
    char *dir = g_build_filename(base, rel, NULL);
    GDir *dh = g_dir_open(dir, 0, NULL);
    const char *n;
    while (dh && (n = g_dir_read_name(dh))) {
        char *r = g_build_filename(rel, n, NULL), *full = g_build_filename(base, r, NULL);
        if (g_file_test(full, G_FILE_TEST_IS_DIR)) { collect_zones(base, r, out); g_free(r); }
        else g_ptr_array_add(out, r);
        g_free(full);
    }
    if (dh) g_dir_close(dh);
    g_free(dir);
}

static gint cmpstr(gconstpointer a, gconstpointer b) { return g_strcmp0(*(char **)a, *(char **)b); }

static GtkWidget *field(GtkWidget *grid, int row, const char *name, GtkWidget *w, const char *hint) {
    GtkWidget *l = label(name, "field", FALSE);
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
    if (hint) gtk_grid_attach(GTK_GRID(grid), label(hint, "dim", TRUE), 1, row + 1, 1, 1);
    return w;
}

static GtkWidget *page_account(void) {
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_box_append(GTK_BOX(b), label("Ты и компьютер", "title", FALSE));
    GtkWidget *g = gtk_grid_new();
    add_class(g, "card");
    gtk_grid_set_row_spacing(GTK_GRID(g), 8);
    gtk_grid_set_column_spacing(GTK_GRID(g), 18);
    w_host = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(w_host), "lavender");
    field(g, 0, "Имя компьютера", w_host, "Как компьютер будет виден в сети.");
    w_user = gtk_entry_new();
    field(g, 2, "Имя пользователя", w_user, "Для входа: строчная латиница, например <i>alex</i>.");
    w_pw1 = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(w_pw1), TRUE);
    field(g, 4, "Пароль", w_pw1, "Нужен для входа и для команд администратора (sudo).");
    w_pw2 = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(w_pw2), TRUE);
    field(g, 6, "Ещё раз", w_pw2, NULL);

    GPtrArray *zones = g_ptr_array_new_with_free_func(g_free);
    const char *zdir = demo && !g_file_test("/usr/share/zoneinfo", G_FILE_TEST_IS_DIR) ? NULL : "/usr/share/zoneinfo";
    const char *areas[] = {"Europe", "Asia", "America", "Africa", "Australia", "Atlantic", "Indian", "Pacific", NULL};
    for (int i = 0; zdir && areas[i]; i++) collect_zones(zdir, areas[i], zones);
    if (!zones->len) g_ptr_array_add(zones, g_strdup("Europe/Moscow"));
    g_ptr_array_sort(zones, cmpstr);
    g_ptr_array_add(zones, NULL);
    GtkStringList *sl = gtk_string_list_new((const char *const *)zones->pdata);
    w_tz = gtk_drop_down_new(G_LIST_MODEL(sl), gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, NULL, "string"));
    gtk_drop_down_set_enable_search(GTK_DROP_DOWN(w_tz), TRUE);
    for (guint i = 0; i + 1 < zones->len; i++)
        if (!strcmp(zones->pdata[i], "Europe/Moscow")) gtk_drop_down_set_selected(GTK_DROP_DOWN(w_tz), i);
    field(g, 8, "Часовой пояс", w_tz, "Можно начать печатать название города.");
    g_ptr_array_free(zones, TRUE);
    gtk_box_append(GTK_BOX(b), g);
    GtkWidget *ents[] = {w_host, w_user, w_pw1, w_pw2};
    for (int i = 0; i < 4; i++) g_signal_connect(ents[i], "changed", G_CALLBACK(on_acc_changed), NULL);
    return b;
}

/* ── страница 4: дополнительно ─────────────────────────── */

static GtkWidget *switch_row(const char *title, const char *desc, gboolean on, GtkWidget **out) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(col, TRUE);
    gtk_box_append(GTK_BOX(col), label(title, "row-title", FALSE));
    gtk_box_append(GTK_BOX(col), label(desc, "dim", TRUE));
    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), on);
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), col);
    gtk_box_append(GTK_BOX(row), sw);
    *out = sw;
    return row;
}

static GtkWidget *page_extras(void) {
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_box_append(GTK_BOX(b), label("Дополнительно", "title", FALSE));
    GtkWidget *c = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    add_class(c, "card");
    gtk_box_append(GTK_BOX(c), switch_row("Медиа", "OBS, ffmpeg, cava, yt-dlp, qBittorrent", TRUE, &w_opt[0]));
    gtk_box_append(GTK_BOX(c), switch_row("Разработка", "компиляторы, cmake, clang, nodejs, black", FALSE, &w_opt[1]));
    gtk_box_append(GTK_BOX(c), switch_row("Развлечения", "cbonsai, tty-clock, genact, sl", FALSE, &w_opt[2]));
    gtk_box_append(GTK_BOX(b), c);

    w_gpu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    add_class(w_gpu_box, "card");
    gtk_box_append(GTK_BOX(w_gpu_box), label("<b>Найдена видеокарта NVIDIA</b>", NULL, FALSE));
    w_gpu_rb[0] = gtk_check_button_new_with_label("Проприетарный драйвер NVIDIA — быстрее, лучше для игр (несвободный)");
    w_gpu_rb[1] = gtk_check_button_new_with_label("Свободный nouveau + NVK — для GTX 16xx / RTX и новее");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(w_gpu_rb[1]), GTK_CHECK_BUTTON(w_gpu_rb[0]));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(w_gpu_rb[0]), TRUE);
    gtk_box_append(GTK_BOX(w_gpu_box), w_gpu_rb[0]);
    gtk_box_append(GTK_BOX(w_gpu_box), w_gpu_rb[1]);
    gtk_widget_set_visible(w_gpu_box, have_nvidia);
    gtk_box_append(GTK_BOX(b), w_gpu_box);
    return b;
}

/* ── страница 5: что произойдёт ────────────────────────── */

static Part *plan_esp(void) {
    if (sel_mode == M_DISK) return NULL;          /* свой EFI на этом диске */
    if (sel_mode == M_FREE) { Part *e = NULL; free_ok(sel_disk, &e); return e; }
    return pick_esp(sel_disk);
}

static gboolean any_windows(void) {
    for (guint i = 0; i < disks->len; i++) {
        Disk *d = disks->pdata[i];
        if (d->live || (sel_mode == M_DISK && d == sel_disk)) continue;
        for (guint j = 0; j < d->parts->len; j++)
            if (((Part *)d->parts->pdata[j])->win_boot) return TRUE;
    }
    return FALSE;
}

static gboolean any_bitlocker(void) {
    for (guint i = 0; i < disks->len; i++) if (((Disk *)disks->pdata[i])->bitlocker) return TRUE;
    return FALSE;
}

static void build_summary(void) {
    GString *s = g_string_new(NULL);
    Disk *d = sel_disk;
    char *dz = human(d->size);
    char *line = g_markup_printf_escaped("<b>Диск:</b> %s — %s, %s\n\n", d->name, d->model, dz);
    g_string_append(s, line);
    g_free(line); g_free(dz);
    Part *esp = plan_esp();
    if (sel_mode == M_FREE) {
        char *fz = human(d->free_size - (esp ? 0 : ESP_SIZE));
        line = g_markup_printf_escaped("<span foreground='" C_SAGE "'>●</span>  В свободном месте будет создан раздел для Lavender OS — <b>%s</b>.\n"
                                       "<span foreground='" C_SAGE "'>●</span>  Существующие разделы <b>не изменятся</b>%s.\n",
                                       fz, d->has_win ? " — Windows останется на месте" : "");
        g_string_append(s, line);
        g_free(line); g_free(fz);
    } else if (sel_mode == M_DISK) {
        line = d->has_data ? g_markup_printf_escaped("<span foreground='" C_ROSE "'>●</span>  <b><span foreground='" C_ROSE "'>Весь диск %s будет стёрт.</span></b> Всё, что на нём было, пропадёт.\n", d->name)
                           : g_markup_printf_escaped("<span foreground='" C_SAGE "'>●</span>  Пустой диск %s будет размечен целиком под Lavender OS.\n", d->name);
        g_string_append(s, line);
        g_free(line);
        g_string_append(s, "<span foreground='" C_SAGE "'>●</span>  Другие диски <b>не затрагиваются</b>.\n");
    } else {
        char *t = part_title(sel_part), *z = human(sel_part->size);
        line = g_markup_printf_escaped("<span foreground='" C_ROSE "'>●</span>  Раздел <b>%s</b> (%s, %s) будет отформатирован под Lavender OS.\n"
                                       "<span foreground='" C_SAGE "'>●</span>  Остальные разделы не изменятся.\n", sel_part->name, t, z);
        g_string_append(s, line);
        g_free(line); g_free(t); g_free(z);
    }
    if (esp) {
        line = g_markup_printf_escaped("<span foreground='" C_SAGE "'>●</span>  Загрузчик GRUB добавится в существующий EFI-раздел <b>%s</b>%s — без форматирования.\n",
                                       esp->name, esp->win_boot ? ", файлы Windows не трогаются" : "");
    } else {
        line = g_markup_printf_escaped("<span foreground='" C_SAGE "'>●</span>  На диске %s будет создан свой EFI-раздел 512 МБ для загрузчика.\n", d->name);
    }
    g_string_append(s, line);
    g_free(line);
    if (any_windows())
        g_string_append(s, "<span foreground='" C_LAV "'>●</span>  При включении появится меню: <b>Lavender OS</b> или <b>Windows</b> "
                           "(5 секунд, запоминает последний выбор). Часы будут идти по местному времени, как в Windows.\n");
    if (any_bitlocker())
        g_string_append(s, "\n<span foreground='" C_AMBER "'><b>BitLocker:</b> Windows может один раз попросить ключ восстановления после установки. "
                           "Убедись, что он у тебя есть (account.microsoft.com/devices/recoverykey).</span>\n");

    const char *opts[3] = {"медиа", "разработка", "развлечения"};
    GString *o = g_string_new(NULL);
    for (int i = 0; i < 3; i++)
        if (gtk_switch_get_active(GTK_SWITCH(w_opt[i]))) g_string_append_printf(o, "%s%s", o->len ? ", " : "", opts[i]);
    GtkStringObject *tz = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(w_tz));
    line = g_markup_printf_escaped("\n<b>Пользователь:</b> %s   <b>Компьютер:</b> %s   <b>Пояс:</b> %s\n<b>Наборы:</b> %s",
                                   gtk_editable_get_text(GTK_EDITABLE(w_user)), gtk_editable_get_text(GTK_EDITABLE(w_host)),
                                   tz ? gtk_string_object_get_string(tz) : "", o->len ? o->str : "нет");
    g_string_append(s, line);
    if (have_nvidia)
        g_string_append(s, gtk_check_button_get_active(GTK_CHECK_BUTTON(w_gpu_rb[0])) ? "   <b>NVIDIA:</b> проприетарный драйвер"
                                                                                      : "   <b>NVIDIA:</b> nouveau");
    g_free(line);
    g_string_free(o, TRUE);
    g_string_append(s, "\n\nУстановка займёт 10–30 минут в зависимости от интернета. Не выключай компьютер.");
    gtk_label_set_markup(GTK_LABEL(w_summary), s->str);
    g_string_free(s, TRUE);
}

static GtkWidget *page_summary(void) {
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_box_append(GTK_BOX(b), label("Что произойдёт", "title", FALSE));
    w_summary = label("", "summary", TRUE);
    GtkWidget *c = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_class(c, "card");
    gtk_box_append(GTK_BOX(c), w_summary);
    gtk_box_append(GTK_BOX(b), c);
    gtk_box_append(GTK_BOX(b), label("Нажми «Установить», только если всё верно. Вернуться и поменять можно кнопкой «Назад».", "dim", TRUE));
    return b;
}

/* ── страница 6: установка ─────────────────────────────── */

static double weights[] = {3, 2, 60, 18, 12, 5};
static int cur_step;
static int xbps_total, xbps_dl, xbps_unp, xbps_ok;
static GDataInputStream *proc_out;

static void set_progress(void) {
    double before = 0, total = 0;
    for (int i = 0; i < 6; i++) { total += weights[i]; if (i < cur_step - 1) before += weights[i]; }
    double within = 0;
    if (cur_step == 3 && xbps_total) within = fmin(1, 0.5 * xbps_dl / xbps_total + 0.3 * xbps_unp / xbps_total + 0.2 * xbps_ok / xbps_total);
    double f = cur_step ? (before + weights[cur_step - 1] * within) / total : 0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w_prog_bar), fmin(1, f));
    char *t = g_strdup_printf("%.0f%%", fmin(100, f * 100));
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w_prog_bar), t);
    g_free(t);
}

static void log_append(const char *line) {
    GtkTextBuffer *tb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w_log));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(tb, &end);
    gtk_text_buffer_insert(tb, &end, line, -1);
    gtk_text_buffer_insert(tb, &end, "\n", 1);
    if (gtk_text_buffer_get_line_count(tb) > 3000) {
        GtkTextIter a, b;
        gtk_text_buffer_get_start_iter(tb, &a);
        gtk_text_buffer_get_iter_at_line(tb, &b, 500);
        gtk_text_buffer_delete(tb, &a, &b);
    }
    gtk_text_buffer_get_end_iter(tb, &end);
    GtkTextMark *m = gtk_text_buffer_get_mark(tb, "end");
    if (!m) m = gtk_text_buffer_create_mark(tb, "end", &end, FALSE);
    else gtk_text_buffer_move_mark(tb, m, &end);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(w_log), m);
}

static void finish(gboolean ok, const char *err) {
    installing = FALSE;
    if (ok) {
        gtk_label_set_text(GTK_LABEL(w_prog_title), "Готово!");
        gtk_label_set_text(GTK_LABEL(w_prog_step), "Lavender OS установлена. Вынь флешку и перезагрузи компьютер.");
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w_prog_bar), 1);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w_prog_bar), "100%");
        gtk_widget_set_visible(w_prog_done, TRUE);
    } else {
        gtk_label_set_text(GTK_LABEL(w_prog_title), "Установка не удалась");
        char *m = g_markup_printf_escaped("<span foreground='" C_ROSE "'>%s</span>\nПолный лог: /tmp/lavender-install.log", err ? err : "Неизвестная ошибка.");
        gtk_label_set_markup(GTK_LABEL(w_prog_step), m);
        g_free(m);
        gtk_expander_set_expanded(GTK_EXPANDER(w_log_exp), TRUE);
        gtk_widget_set_visible(w_prog_err, TRUE);
    }
}

static char *last_error;

static void read_line(GObject *o, GAsyncResult *r, gpointer d) {
    (void)d;
    gsize len;
    GError *e = NULL;
    char *line = g_data_input_stream_read_line_finish_utf8(G_DATA_INPUT_STREAM(o), r, &len, &e);
    if (!line) {
        g_clear_error(&e);
        if (installing) finish(FALSE, last_error);
        return;
    }
    if (g_str_has_prefix(line, "@@STEP ")) {
        int n = 0, tot = 0;
        char *rest = NULL;
        if (sscanf(line + 7, "%d/%d", &n, &tot) == 2) {
            cur_step = n;
            rest = strchr(line + 7, ' ');
            gtk_label_set_text(GTK_LABEL(w_prog_step), rest ? rest + 1 : "");
            set_progress();
        }
        log_append(line + 7);
    } else if (g_str_has_prefix(line, "@@ERROR ")) {
        if (!last_error) last_error = g_strdup(line + 8);
        log_append(line);
    } else if (g_str_has_prefix(line, "@@INFO ")) {
        log_append(line + 7);
    } else if (!strcmp(line, "@@DONE")) {
        finish(TRUE, NULL);
    } else {
        if (cur_step == 3) {
            /* xbps: «651 packages will be installed:», затем по пакету «verifying RSA signature»,
               «unpacking», «installed successfully» */
            int n = 0;
            if (sscanf(line, "%d packages will be installed", &n) == 1) xbps_total = n;
            else if (strstr(line, ": verifying RSA signature")) xbps_dl++;
            else if (strstr(line, ": unpacking")) xbps_unp++;
            else if (strstr(line, "installed successfully")) xbps_ok++;
            if (xbps_total) {
                if (xbps_dl > xbps_total) xbps_dl = xbps_total;
                char *t = xbps_ok ? g_strdup_printf("Установка пакетов: %d из %d", MIN(xbps_ok, xbps_total), xbps_total)
                                  : g_strdup_printf("Загрузка пакетов: %d из %d", xbps_dl, xbps_total);
                gtk_label_set_text(GTK_LABEL(w_prog_step), t);
                g_free(t);
            }
            set_progress();
        }
        log_append(line);
    }
    g_free(line);
    g_data_input_stream_read_line_async(proc_out, G_PRIORITY_DEFAULT, NULL, read_line, NULL);
}

static void put_var(GString *s, const char *k, const char *v) {
    char *q = g_shell_quote(v ? v : "");
    g_string_append_printf(s, "%s=%s\n", k, q);
    g_free(q);
}

static void start_install(void) {
    installing = TRUE;
    cur_step = 0;
    xbps_total = xbps_dl = xbps_unp = xbps_ok = 0;
    g_clear_pointer(&last_error, g_free);
    gtk_widget_set_visible(w_prog_done, FALSE);
    gtk_widget_set_visible(w_prog_err, FALSE);
    gtk_label_set_text(GTK_LABEL(w_prog_title), "Устанавливаю Lavender OS");
    gtk_label_set_text(GTK_LABEL(w_prog_step), "Подготовка…");

    GString *s = g_string_new("# lavender-installer\n");
    const char *modes[] = {"free", "disk", "part"};
    put_var(s, "MODE", modes[sel_mode]);
    put_var(s, "DISK", sel_disk->path);
    put_var(s, "PART", sel_mode == M_PART ? sel_part->path : "");
    Part *esp = plan_esp();
    put_var(s, "ESP", esp ? esp->path : "");
    put_var(s, "WIPE_OK", sel_mode == M_DISK ? sel_disk->path : "");
    put_var(s, "HOST", gtk_editable_get_text(GTK_EDITABLE(w_host)));
    put_var(s, "USERN", gtk_editable_get_text(GTK_EDITABLE(w_user)));
    put_var(s, "PW1", gtk_editable_get_text(GTK_EDITABLE(w_pw1)));
    GtkStringObject *tz = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(w_tz));
    put_var(s, "TZ", gtk_string_object_get_string(tz));
    const char *ids[3] = {"media", "dev", "fun"};
    GString *o = g_string_new(NULL);
    for (int i = 0; i < 3; i++)
        if (gtk_switch_get_active(GTK_SWITCH(w_opt[i]))) g_string_append_printf(o, "%s%s", o->len ? " " : "", ids[i]);
    put_var(s, "OPT", o->str);
    g_string_free(o, TRUE);
    put_var(s, "GPU", !have_nvidia ? "none" : gtk_check_button_get_active(GTK_CHECK_BUTTON(w_gpu_rb[0])) ? "nvidia" : "nouveau");

    GSubprocessLauncher *l = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    GError *e = NULL;
    GSubprocess *p = NULL;
    if (demo) {
        log_append("[демо] файл ответов, который получил бы install.sh (пароль скрыт):");
        char **lines = g_strsplit(s->str, "\n", -1);
        for (int i = 0; lines[i]; i++) if (*lines[i] && !g_str_has_prefix(lines[i], "PW1=")) log_append(lines[i]);
        g_strfreev(lines);
        const char *fake =
            "say(){ echo \"@@STEP $1/6 $2\"; sleep ${3:-1}; }; say 1 'Разметка диска' 1; say 2 'Подготовка репозиториев' 1;"
            "echo '@@STEP 3/6 Установка пакетов (412 шт., это надолго)'; echo \"40 packages will be installed:\";"
            "for i in $(seq 1 40); do echo \"pkg$i-1.0_1: verifying RSA signature...\"; sleep 0.04; done;"
            "for i in $(seq 1 40); do echo \"pkg$i-1.0_1: unpacking ...\"; echo \"pkg$i-1.0_1: installed successfully.\"; sleep 0.03; done;"
            "say 4 'Настройка системы' 1; say 5 'Загрузчик и поиск Windows' 1; echo '@@INFO В меню загрузки добавлена Windows';"
            "say 6 'Завершение' 1; echo @@DONE";
        p = g_subprocess_launcher_spawn(l, &e, "sh", "-c", fake, NULL);
    } else {
        char *auto_path = NULL;
        int fd = g_file_open_tmp("lavender-auto-XXXXXX", &auto_path, &e);
        if (fd >= 0) {
            ssize_t wr = write(fd, s->str, s->len);
            close(fd);
            if (wr == (ssize_t)s->len) {
                char *script = g_build_filename(inst_dir, "install.sh", NULL);
                g_subprocess_launcher_setenv(l, "LAVENDER_AUTO", auto_path, TRUE);
                p = g_subprocess_launcher_spawn(l, &e, "sh", script, NULL);
                g_free(script);
            }
        }
        g_free(auto_path);
    }
    /* пароль в памяти больше не нужен */
    memset(s->str, 0, s->len);
    g_string_free(s, TRUE);
    g_object_unref(l);
    if (!p) {
        finish(FALSE, e ? e->message : "Не удалось запустить install.sh");
        g_clear_error(&e);
        return;
    }
    proc_out = g_data_input_stream_new(g_subprocess_get_stdout_pipe(p));
    g_data_input_stream_read_line_async(proc_out, G_PRIORITY_DEFAULT, NULL, read_line, NULL);
    g_object_unref(p);
}

static void on_reboot(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    if (demo) { gtk_window_destroy(GTK_WINDOW(win)); return; }
    const char *argv[] = {"reboot", NULL};
    g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
}
static void on_quit(GtkButton *b, gpointer d) { (void)b; (void)d; gtk_window_destroy(GTK_WINDOW(win)); }

static GtkWidget *page_progress(void) {
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    w_prog_title = label("Устанавливаю Lavender OS", "title", FALSE);
    w_prog_step = label("", "lead", TRUE);
    w_prog_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(w_prog_bar), TRUE);
    gtk_box_append(GTK_BOX(b), w_prog_title);
    gtk_box_append(GTK_BOX(b), w_prog_step);
    gtk_box_append(GTK_BOX(b), w_prog_bar);
    w_log = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(w_log), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(w_log), TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(w_log), FALSE);
    add_class(w_log, "log");
    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), w_log);
    gtk_widget_set_size_request(sc, -1, 260);
    w_log_exp = gtk_expander_new("Подробности");
    gtk_expander_set_child(GTK_EXPANDER(w_log_exp), sc);
    gtk_box_append(GTK_BOX(b), w_log_exp);

    w_prog_done = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *rb = gtk_button_new_with_label("Перезагрузить");
    add_class(rb, "suggested-action");
    g_signal_connect(rb, "clicked", G_CALLBACK(on_reboot), NULL);
    gtk_box_append(GTK_BOX(w_prog_done), rb);
    gtk_widget_set_visible(w_prog_done, FALSE);
    gtk_box_append(GTK_BOX(b), w_prog_done);

    w_prog_err = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *q = gtk_button_new_with_label("Закрыть установщик");
    g_signal_connect(q, "clicked", G_CALLBACK(on_quit), NULL);
    gtk_box_append(GTK_BOX(w_prog_err), q);
    gtk_box_append(GTK_BOX(w_prog_err), label("Диски могли частично измениться — смотри лог. Повторить можно из консоли: lavender-installer", "dim", TRUE));
    gtk_widget_set_visible(w_prog_err, FALSE);
    gtk_box_append(GTK_BOX(b), w_prog_err);
    return b;
}

/* ── навигация ─────────────────────────────────────────── */

static void go(int p) {
    page = p;
    if (page == 4) build_summary();
    gtk_stack_set_visible_child_name(GTK_STACK(stack), pages[page]);
    if (page == 5 && !installing) start_install();
    update_nav();
}
static void on_next(GtkButton *b, gpointer d) { (void)b; (void)d; if (page < NPAGES - 1) go(page + 1); }
static void on_back(GtkButton *b, gpointer d) { (void)b; (void)d; if (page > 0) go(page - 1); }
static gboolean sc_next(GtkWidget *w, GVariant *a, gpointer d) {
    (void)w; (void)a; (void)d;
    /* «Установить» — только явным нажатием кнопки, не горячей клавишей */
    if (page < NPAGES - 2 && gtk_widget_get_sensitive(btn_next)) on_next(NULL, NULL);
    return TRUE;
}
static gboolean sc_back(GtkWidget *w, GVariant *a, gpointer d) {
    (void)w; (void)a; (void)d;
    if (gtk_widget_get_sensitive(btn_back)) on_back(NULL, NULL);
    return TRUE;
}
static gboolean on_close(GtkWindow *w, gpointer d) { (void)w; (void)d; return installing; } /* во время установки не закрывать */

static void load_css(void) {
    const char *css =
        "window.inst { background: " C_BG "; color: " C_FG "; font-family: Inter, sans-serif; }\n"
        ".inst .hero { font-size: 30px; font-weight: 800; color: " C_LAV "; }\n"
        ".inst .title { font-size: 22px; font-weight: 800; }\n"
        ".inst .lead { font-size: 15px; color: " C_SUB "; }\n"
        ".inst .dim { color: " C_MUTED "; font-size: 13px; }\n"
        ".inst .hint { color: " C_AMBER "; font-size: 13px; }\n"
        ".inst .row-title { font-weight: 700; }\n"
        ".inst .card { background: " C_SURF "; border-radius: 16px; padding: 16px 18px; }\n"
        ".inst .ok-mark { color: " C_SAGE "; font-weight: 800; font-size: 18px; }\n"
        ".inst .bad-mark { color: " C_ROSE "; font-weight: 800; font-size: 18px; }\n"
        ".inst button { border-radius: 11px; background: " C_SEL "; color: " C_FG "; border: none; box-shadow: none; padding: 7px 16px; }\n"
        ".inst button:hover { background: alpha(" C_LAV ", 0.25); }\n"
        ".inst button:disabled { opacity: 0.45; }\n"
        ".inst button.suggested-action { background: " C_LAV "; color: #2a1f45; font-weight: 700; }\n"
        ".inst button.danger-action { background: " C_ROSE "; color: #3a1520; font-weight: 800; }\n"
        ".inst button.disk-card { background: " C_SURF "; padding: 14px 16px; border-radius: 16px; border: 2px solid transparent; }\n"
        ".inst button.disk-card:hover { background: alpha(" C_LAV ", 0.08); }\n"
        ".inst button.disk-card:checked { border-color: " C_LAV "; background: alpha(" C_LAV ", 0.10); }\n"
        ".inst button.disk-card:disabled { opacity: 0.55; }\n"
        ".inst .disk-title { font-size: 15px; }\n"
        ".inst .legend { font-size: 12px; color: " C_SUB "; }\n"
        ".inst .badge-blue, .inst .badge-rose, .inst .badge-sage, .inst .badge-muted { border-radius: 99px; padding: 2px 10px;"
        "  font-size: 12px; font-weight: 700; }\n"
        ".inst .badge-blue { background: alpha(" C_BLUE ", 0.2); color: " C_BLUE "; }\n"
        ".inst .badge-rose { background: alpha(" C_ROSE ", 0.2); color: " C_ROSE "; }\n"
        ".inst .badge-sage { background: alpha(" C_SAGE ", 0.2); color: " C_SAGE "; }\n"
        ".inst .badge-muted { background: " C_SEL "; color: " C_SUB "; }\n"
        ".inst checkbutton.method { font-weight: 700; }\n"
        ".inst .method-desc { color: " C_MUTED "; font-size: 13px; margin-left: 30px; margin-bottom: 4px; }\n"
        ".inst .danger { background: alpha(" C_ROSE ", 0.12); border: 1px solid alpha(" C_ROSE ", 0.5); border-radius: 12px;"
        "  padding: 12px 14px; color: " C_ROSE "; }\n"
        ".inst .summary { font-size: 15px; line-height: 1.5; }\n"
        ".inst .field { color: " C_SUB "; }\n"
        ".inst entry, .inst passwordentry, .inst dropdown > button { background: " C_BG "; color: " C_FG "; border-radius: 10px;"
        "  border: 1px solid " C_SEL "; box-shadow: none; min-height: 34px; }\n"
        ".inst entry:focus-within, .inst passwordentry:focus-within { border-color: " C_LAV "; }\n"
        ".inst switch { background: " C_SEL "; border-radius: 99px; } .inst switch:checked { background: " C_LAV "; }\n"
        ".inst switch slider { background: " C_FG "; border-radius: 99px; }\n"
        ".inst checkbutton check, .inst checkbutton radio { background: " C_BG "; border: 2px solid " C_MUTED "; }\n"
        ".inst checkbutton check:checked, .inst checkbutton radio:checked { background: " C_LAV "; border-color: " C_LAV "; color: " C_BG "; }\n"
        ".inst progressbar trough { background: " C_SEL "; border-radius: 99px; min-height: 12px; }\n"
        ".inst progressbar progress { background: " C_LAV "; border-radius: 99px; min-height: 12px; }\n"
        ".inst progressbar text { color: " C_SUB "; }\n"
        ".inst .log, .inst .log text { background: " C_BG "; color: " C_SUB "; font-size: 12px; }\n"
        ".inst .footer { background: " C_SURF "; padding: 12px 22px; border-top: 1px solid " C_LINE "; }\n"
        ".inst .dot { min-width: 9px; min-height: 9px; border-radius: 99px; background: " C_SEL "; }\n"
        ".inst .dot.active { background: " C_LAV "; min-width: 26px; }\n"
        ".inst *:focus-visible { outline-color: " C_LAV "; }\n"
        ".inst button:focus-visible { outline: 2px solid " C_LAV "; outline-offset: 2px; }\n"
        ".inst button.disk-card:focus-visible { outline: none; border-color: alpha(" C_LAV ", 0.6); }\n"
        ".inst selection, .inst text selection { background: alpha(" C_LAV ", 0.35); color: " C_FG "; }\n"
        ".inst popover contents, .inst popover arrow { background: " C_SURF "; color: " C_FG "; }\n";
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_string(p, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
}

static const char *opt_page, *opt_pick, *opt_mode;

static void activate(GtkApplication *app, gpointer d) {
    (void)d;
    load_css();
    win = gtk_application_window_new(app);
    add_class(win, "inst");
    gtk_window_set_title(GTK_WINDOW(win), "Установка Lavender OS");
    gtk_window_set_default_size(GTK_WINDOW(win), 1000, 760);
    g_signal_connect(win, "close-request", G_CALLBACK(on_close), NULL);
    if (!demo) gtk_window_set_decorated(GTK_WINDOW(win), FALSE);   /* в cage окно и так на весь экран */

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_widget_set_vexpand(stack, TRUE);
    GtkWidget *(*builders[])(void) = {page_welcome, page_disk, page_account, page_extras, page_summary, page_progress};
    for (int i = 0; i < NPAGES; i++) {
        GtkWidget *content = builders[i]();
        gtk_widget_set_size_request(content, -1, -1);
        GtkWidget *clamp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_halign(content, GTK_ALIGN_FILL);
        gtk_widget_set_margin_top(content, 28);
        gtk_widget_set_margin_bottom(content, 28);
        gtk_widget_set_margin_start(content, 40);
        gtk_widget_set_margin_end(content, 40);
        gtk_box_append(GTK_BOX(clamp), content);
        GtkWidget *sc = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), clamp);
        gtk_stack_add_named(GTK_STACK(stack), sc, pages[i]);
    }
    gtk_box_append(GTK_BOX(outer), stack);

    GtkWidget *foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    add_class(foot, "footer");
    btn_back = gtk_button_new_with_label("Назад");
    g_signal_connect(btn_back, "clicked", G_CALLBACK(on_back), NULL);
    GtkWidget *dbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    gtk_widget_set_margin_start(dbox, 16);
    gtk_widget_set_valign(dbox, GTK_ALIGN_CENTER);
    for (int i = 0; i < NPAGES; i++) {
        dots[i] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        add_class(dots[i], "dot");
        gtk_box_append(GTK_BOX(dbox), dots[i]);
    }
    btn_next = gtk_button_new_with_label("Далее");
    add_class(btn_next, "suggested-action");
    g_signal_connect(btn_next, "clicked", G_CALLBACK(on_next), NULL);
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(spacer, 80, -1);
    w_disk_hint = label("", "hint", FALSE);
    gtk_label_set_xalign(GTK_LABEL(w_disk_hint), 1);
    gtk_widget_set_hexpand(w_disk_hint, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(w_disk_hint), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(foot), btn_back);
    gtk_box_append(GTK_BOX(foot), dbox);
    gtk_box_append(GTK_BOX(foot), w_disk_hint);
    gtk_box_append(GTK_BOX(foot), btn_next);
    gtk_box_append(GTK_BOX(outer), foot);
    gtk_window_set_child(GTK_WINDOW(win), outer);
    /* клавиатура: Alt+→ «Далее», Alt+← «Назад» */
    GtkEventController *sc = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(sc), GTK_SHORTCUT_SCOPE_GLOBAL);
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(sc),
        gtk_shortcut_new(gtk_shortcut_trigger_parse_string("<Alt>Right"), gtk_callback_action_new(sc_next, NULL, NULL)));
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(sc),
        gtk_shortcut_new(gtk_shortcut_trigger_parse_string("<Alt>Left"), gtk_callback_action_new(sc_back, NULL, NULL)));
    gtk_widget_add_controller(win, sc);

    render_checks();
    check_online();

    /* демо: сразу открыть нужную страницу с выбранным диском */
    if (opt_pick) {
        for (guint i = 0; i < disks->len; i++) {
            Disk *dd = disks->pdata[i];
            if (!strcmp(dd->path, opt_pick)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dd->card), TRUE);
        }
        int m = !g_strcmp0(opt_mode, "free") ? M_FREE : !g_strcmp0(opt_mode, "disk") ? M_DISK : !g_strcmp0(opt_mode, "part") ? M_PART : M_NONE;
        if (m != M_NONE && sel_disk) gtk_check_button_set_active(GTK_CHECK_BUTTON(w_rb[m]), TRUE);
    }
    if (opt_page) {
        gtk_editable_set_text(GTK_EDITABLE(w_user), "alex");
        gtk_editable_set_text(GTK_EDITABLE(w_pw1), "demo");
        gtk_editable_set_text(GTK_EDITABLE(w_pw2), "demo");
        for (int i = 0; i < NPAGES; i++) if (!strcmp(pages[i], opt_page)) go(i);
    } else go(0);
    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv) {
    const char *demo_file = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--demo") && i + 1 < argc) demo_file = argv[++i];
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) opt_page = argv[++i];
        else if (!strcmp(argv[i], "--pick") && i + 1 < argc) opt_pick = argv[++i];
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) opt_mode = argv[++i];
    }
    if (g_getenv("LAVENDER_DIR")) inst_dir = g_getenv("LAVENDER_DIR");
    if (demo_file) {
        demo = TRUE;
        JsonParser *jp = json_parser_new();
        GError *e = NULL;
        if (!json_parser_load_from_file(jp, demo_file, &e)) { g_printerr("demo: %s\n", e->message); return 1; }
        demo_obj = json_object_ref(json_node_get_object(json_parser_get_root(jp)));
        g_object_unref(jp);
        uefi = !json_object_has_member(demo_obj, "uefi") || jbool(demo_obj, "uefi");
        have_nvidia = jbool(demo_obj, "nvidia");
    } else {
        if (geteuid() != 0) { g_printerr("lavender-installer: нужен root\n"); return 1; }
        uefi = g_file_test("/sys/firmware/efi", G_FILE_TEST_IS_DIR);
        GDir *dh = g_dir_open("/sys/bus/pci/devices", 0, NULL);
        const char *n;
        while (dh && (n = g_dir_read_name(dh))) {
            char *vf = g_build_filename("/sys/bus/pci/devices", n, "vendor", NULL), *cf = g_build_filename("/sys/bus/pci/devices", n, "class", NULL);
            char *v = NULL, *c = NULL;
            if (g_file_get_contents(vf, &v, NULL, NULL) && g_file_get_contents(cf, &c, NULL, NULL) &&
                g_str_has_prefix(v, "0x10de") && g_str_has_prefix(c, "0x03"))
                have_nvidia = TRUE;
            g_free(v); g_free(c); g_free(vf); g_free(cf);
        }
        if (dh) g_dir_close(dh);
    }
    load_disks();
    GtkApplication *app = gtk_application_new("lavender.installer", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int r = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    return r;
}
