/* lavender-greeter — мягкий экран входа для greetd (GTK4, запускается в cage).
 * Анимации: обои проявляются с лёгким приближением и медленно «дышат», часы и карточка
 * всплывают по очереди, кольцо вокруг аватара светится, при ошибке карточка качается,
 * при входе всё мягко растворяется. После 40 с без действий карточка прячется, остаются часы.
 *   lavender-greeter          — рабочий режим (нужен $GREETD_SOCK)
 *   lavender-greeter --demo   — проверка в окне: пароль «demo»
 * Настройки: /etc/lavender-greeter/config.ini ([greeter] wallpaper=…) */
#include <gtk/gtk.h>
#include <gio/gunixsocketaddress.h>
#include <json-glib/json-glib.h>
#include <pwd.h>
#include <string.h>

#define CONF_DIR   "/etc/lavender-greeter"
#define STATE_FILE "/var/cache/lavender-greeter/last"
#define IDLE_SEC   40

typedef struct { char *name, *real, *home; } User;
typedef struct { char *name; char **cmd; } Session;

static gboolean demo;
static GPtrArray *users, *sessions;
static int cur_user;
static GtkWidget *shaker;
static GtkWidget *win, *root, *avatar_box, *name_lbl, *pass, *msg, *sess_dd, *clock_lbl, *date_lbl, *dots, *card, *go_btn;
static char *wallpaper;
static guint idle_id;

/* ---------- точки пароля: каждый символ — лавандовая точка ---------- */
static GtkWidget *pdots, *pcaret;   /* ряд точек поверх поля и мигающий курсор после них */
static GPtrArray *live;             /* живые точки (без тех, что уже исчезают) */
static gboolean err_clear;          /* поле очищается из-за ошибки — точки осыпаются */
static gboolean tap_flip;

static void dot_gone(gpointer d) { GtkWidget *w = d; if (gtk_widget_get_parent(w)) gtk_box_remove(GTK_BOX(pdots), w); g_object_unref(w); }

static void on_pass_changed(GtkEditable *e, gpointer u) {
    int n = g_utf8_strlen(gtk_editable_get_text(e), -1);
    gtk_widget_set_visible(pcaret, n > 0);   /* пустое поле — виден плейсхолдер «Пароль», курсор не нужен */
    if ((int)live->len == n) return;
    while ((int)live->len < n) {
        GtkWidget *d = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(d, "pd");
        gtk_widget_set_valign(d, GTK_ALIGN_CENTER);
        GtkWidget *prev = live->len ? live->pdata[live->len - 1] : NULL;
        if (prev) gtk_box_insert_child_after(GTK_BOX(pdots), d, prev);
        else gtk_box_prepend(GTK_BOX(pdots), d);
        g_ptr_array_add(live, d);
    }
    int i = 0;
    while ((int)live->len > n) {
        GtkWidget *d = g_ptr_array_steal_index(live, live->len - 1);
        if (err_clear) {   /* осыпаются по очереди, от последней к первой */
            gtk_widget_add_css_class(d, "drop");
            if (i % 3) gtk_widget_add_css_class(d, i % 3 == 1 ? "d1" : "d2");
        } else gtk_widget_add_css_class(d, "out");
        g_timeout_add_once(err_clear ? 520 : 170, dot_gone, g_object_ref(d));
        i++;
    }
    /* мягкая вспышка поля на каждое нажатие; два класса по очереди, чтобы анимация перезапускалась */
    gtk_widget_remove_css_class(pass, tap_flip ? "tap-a" : "tap-b");
    gtk_widget_add_css_class(pass, (tap_flip = !tap_flip) ? "tap-a" : "tap-b");
}

static void clear_with_drop(void) {
    err_clear = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(pass), "");
    err_clear = FALSE;
}

/* ---------- greetd IPC: 4-байтная длина (native endian) + JSON ---------- */
static gboolean ipc_send(GSocket *s, JsonNode *n, GError **e) {
    char *txt = json_to_string(n, FALSE);
    guint32 len = strlen(txt);
    gboolean ok = g_socket_send(s, (char *)&len, 4, NULL, e) == 4 && g_socket_send(s, txt, len, NULL, e) == (gssize)len;
    memset(txt, 0, len);   /* там может быть пароль */
    g_free(txt);
    return ok;
}

static gboolean recv_all(GSocket *s, char *buf, gsize n, GError **e) {
    while (n) {
        gssize r = g_socket_receive(s, buf, n, NULL, e);
        if (r <= 0) { if (!*e) g_set_error(e, G_IO_ERROR, G_IO_ERROR_CLOSED, "greetd закрыл соединение"); return FALSE; }
        buf += r; n -= r;
    }
    return TRUE;
}

static JsonObject *ipc_recv(GSocket *s, JsonParser *p, GError **e) {
    guint32 len;
    if (!recv_all(s, (char *)&len, 4, e)) return NULL;
    if (len > 1 << 20) { g_set_error(e, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "слишком длинный ответ"); return NULL; }
    char *buf = g_malloc(len + 1);
    gboolean ok = recv_all(s, buf, len, e);
    buf[len] = 0;
    if (ok) ok = json_parser_load_from_data(p, buf, len, e);
    g_free(buf);
    return ok ? json_node_get_object(json_parser_get_root(p)) : NULL;
}

static JsonNode *req(const char *type) {
    JsonObject *o = json_object_new();
    json_object_set_string_member(o, "type", type);
    JsonNode *n = json_node_init_object(json_node_alloc(), o);
    json_object_unref(o);
    return n;
}

static void obj_set_str(JsonNode *n, const char *k, const char *v) { json_object_set_string_member(json_node_get_object(n), k, v); }
static const char *s_member(JsonObject *o, const char *k) { return json_object_has_member(o, k) ? json_object_get_string_member(o, k) : ""; }

/* ---------- вход (в отдельном потоке: PAM при неверном пароле думает ~2 c) ---------- */
typedef struct { char *user, *password; char **cmd; char *error; gboolean ok; } Job;

static void job_free(Job *j) {
    if (j->password) { memset(j->password, 0, strlen(j->password)); g_free(j->password); }
    g_free(j->user); g_strfreev(j->cmd); g_free(j->error); g_free(j);
}

static void login_thread(GTask *task, gpointer src, gpointer data, GCancellable *c) {
    Job *j = data;
    if (demo) {
        g_usleep(900000);
        j->ok = g_str_equal(j->password, "demo");
        if (!j->ok) j->error = g_strdup("Неверный пароль. В демо пароль «demo»");
        g_task_return_boolean(task, TRUE);
        return;
    }
    GError *e = NULL;
    const char *path = g_getenv("GREETD_SOCK");
    GSocket *s = path ? g_socket_new(G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, 0, &e) : NULL;
    GSocketAddress *addr = path ? g_unix_socket_address_new(path) : NULL;
    JsonParser *p = NULL;
    if (!s || !g_socket_connect(s, addr, NULL, &e)) {
        j->error = g_strdup_printf("Нет связи с greetd: %s", e ? e->message : "GREETD_SOCK не задан");
        goto out;
    }
    p = json_parser_new();
    JsonNode *n = req("create_session"); obj_set_str(n, "username", j->user);
    gboolean sent = ipc_send(s, n, &e); json_node_unref(n);
    while (sent) {
        JsonObject *o = ipc_recv(s, p, &e);
        if (!o) break;
        const char *t = s_member(o, "type");
        if (g_str_equal(t, "auth_message")) {
            const char *mt = s_member(o, "auth_message_type");
            n = req("post_auth_message_response");
            if (g_str_equal(mt, "secret") || g_str_equal(mt, "visible")) obj_set_str(n, "response", j->password);
            sent = ipc_send(s, n, &e); json_node_unref(n);
        } else if (g_str_equal(t, "success")) {
            if (j->ok) break;   /* start_session подтверждён */
            n = req("start_session");
            JsonArray *a = json_array_new();
            for (int i = 0; j->cmd[i]; i++) json_array_add_string_element(a, j->cmd[i]);
            json_object_set_array_member(json_node_get_object(n), "cmd", a);
            JsonArray *env = json_array_new();
            json_array_add_string_element(env, "XDG_SESSION_TYPE=wayland");
            json_array_add_string_element(env, "XDG_SESSION_DESKTOP=niri");
            json_array_add_string_element(env, "XDG_CURRENT_DESKTOP=niri");
            json_object_set_array_member(json_node_get_object(n), "env", env);
            sent = ipc_send(s, n, &e); json_node_unref(n);
            j->ok = TRUE;
        } else if (g_str_equal(t, "error")) {
            const char *et = s_member(o, "error_type");
            j->ok = FALSE;
            j->error = g_strdup(g_str_equal(et, "auth_error") ? "Неверный пароль" : s_member(o, "description"));
            n = req("cancel_session"); ipc_send(s, n, NULL); json_node_unref(n);
            ipc_recv(s, p, NULL);
            break;
        } else break;
    }
    if (e && !j->error) { j->ok = FALSE; j->error = g_strdup(e->message); }
out:
    if (p) g_object_unref(p);
    g_clear_error(&e);
    if (addr) g_object_unref(addr);
    if (s) g_object_unref(s);
    g_task_return_boolean(task, TRUE);
}

/* ---------- анимационные помощники ---------- */
/* класс снимается через ms — так одну и ту же CSS-анимацию можно запускать снова */
typedef struct { GtkWidget *w; char *cls; } ClsJob;
static void cls_off(gpointer d) { ClsJob *c = d; gtk_widget_remove_css_class(c->w, c->cls); g_object_unref(c->w); g_free(c->cls); g_free(c); }
static void pulse_class(GtkWidget *w, const char *cls, guint ms) {
    gtk_widget_remove_css_class(w, cls);
    gtk_widget_add_css_class(w, cls);
    ClsJob *c = g_new0(ClsJob, 1); c->w = g_object_ref(w); c->cls = g_strdup(cls);
    g_timeout_add_once(ms, cls_off, c);
}

static gboolean go_idle(gpointer u) { idle_id = 0; gtk_widget_add_css_class(root, "idle"); return G_SOURCE_REMOVE; }
static void poke(void) {
    gtk_widget_remove_css_class(root, "idle");
    if (idle_id) g_source_remove(idle_id);
    idle_id = g_timeout_add_seconds(IDLE_SEC, go_idle, NULL);
}

static void set_busy(gboolean b) {
    gtk_widget_set_sensitive(pass, !b);
    gtk_widget_set_visible(dots, b);
    gtk_widget_set_visible(go_btn, !b);
}

static void save_last(const char *user) {
    if (demo) return;
    char *dir = g_path_get_dirname(STATE_FILE);
    g_mkdir_with_parents(dir, 0755);
    g_file_set_contents(STATE_FILE, user, -1, NULL);
    g_free(dir);
}

static void finish(gpointer u) {
    if (demo) {   /* в демо показываем выход и возвращаемся обратно */
        static int n;
        gtk_widget_remove_css_class(root, "leave");
        gtk_widget_remove_css_class(root, n % 2 ? "again-b" : "again-a");
        gtk_widget_add_css_class(root, n++ % 2 ? "again-a" : "again-b");
        gtk_label_set_text(GTK_LABEL(msg), "Вход выполнен. В рабочем режиме тут запустится niri");
        gtk_widget_set_sensitive(pass, TRUE);
        gtk_widget_grab_focus(pass);
    } else gtk_window_destroy(GTK_WINDOW(win));   /* greetd запускает сессию после выхода приветствия */
}

static void login_done(GObject *src, GAsyncResult *res, gpointer data) {
    Job *j = data;
    set_busy(FALSE);
    if (j->ok) {
        save_last(j->user);
        gtk_widget_set_sensitive(pass, FALSE);
        gtk_widget_add_css_class(root, "leave");
        g_timeout_add_once(720, finish, NULL);
    } else {
        gtk_label_set_text(GTK_LABEL(msg), j->error ? j->error : "Ошибка входа");
        pulse_class(msg, "show", 500);
        clear_with_drop();
        pulse_class(shaker, "shake", 520);
        pulse_class(pass, "error", 1400);
        gtk_widget_grab_focus(pass);
    }
    job_free(j);
}

static void on_login(GtkWidget *w, gpointer u) {
    const char *pw = gtk_editable_get_text(GTK_EDITABLE(pass));
    if (!*pw && !demo) return;
    poke();
    User *us = users->pdata[cur_user];
    Session *se = sessions->pdata[gtk_drop_down_get_selected(GTK_DROP_DOWN(sess_dd))];
    Job *j = g_new0(Job, 1);
    j->user = g_strdup(us->name);
    j->password = g_strdup(pw);
    j->cmd = g_strdupv(se->cmd);
    gtk_label_set_text(GTK_LABEL(msg), "");
    set_busy(TRUE);
    GTask *t = g_task_new(NULL, NULL, login_done, j);
    g_task_set_task_data(t, j, NULL);
    g_task_run_in_thread(t, login_thread);
    g_object_unref(t);
}

/* ---------- пользователи и сессии ---------- */
static gboolean real_shell(const char *sh) {
    return sh && *sh && !g_str_has_suffix(sh, "nologin") && !g_str_has_suffix(sh, "/false");
}

static void load_users(void) {
    users = g_ptr_array_new();
    struct passwd *pw;
    setpwent();
    while ((pw = getpwent())) {
        if (pw->pw_uid < 1000 || pw->pw_uid >= 60000 || !real_shell(pw->pw_shell)) continue;
        User *u = g_new0(User, 1);
        u->name = g_strdup(pw->pw_name);
        char *gecos = g_strdup(pw->pw_gecos ? pw->pw_gecos : "");
        char *comma = strchr(gecos, ',');
        if (comma) *comma = 0;
        u->real = *gecos ? gecos : (g_free(gecos), g_strdup(pw->pw_name));
        u->home = g_strdup(pw->pw_dir);
        g_ptr_array_add(users, u);
    }
    endpwent();
    if (!users->len) {
        User *u = g_new0(User, 1);
        u->name = g_strdup("demo"); u->real = g_strdup("demo"); u->home = g_strdup("/");
        g_ptr_array_add(users, u);
    }
    char *last = NULL;
    if (g_file_get_contents(STATE_FILE, &last, NULL, NULL)) {
        g_strstrip(last);
        for (guint i = 0; i < users->len; i++) if (g_str_equal(((User *)users->pdata[i])->name, last)) cur_user = i;
        g_free(last);
    }
}

static void add_session(const char *name, char **cmd) {
    Session *s = g_new0(Session, 1);
    s->name = g_strdup(name);
    s->cmd = cmd;
    g_ptr_array_add(sessions, s);
}

static void load_sessions(void) {
    sessions = g_ptr_array_new();
    /* niri — со своей шиной D-Bus, как запускалось раньше */
    if (g_file_test("/usr/bin/niri", G_FILE_TEST_IS_EXECUTABLE)) {
        char *c[] = { "dbus-run-session", "niri", NULL };
        add_session("niri", g_strdupv(c));
    }
    GDir *d = g_dir_open("/usr/share/wayland-sessions", 0, NULL);
    const char *f;
    while (d && (f = g_dir_read_name(d))) {
        if (!g_str_has_suffix(f, ".desktop") || g_str_has_prefix(f, "niri")) continue;
        char *path = g_build_filename("/usr/share/wayland-sessions", f, NULL);
        GKeyFile *k = g_key_file_new();
        if (g_key_file_load_from_file(k, path, G_KEY_FILE_NONE, NULL)) {
            char *name = g_key_file_get_locale_string(k, "Desktop Entry", "Name", NULL, NULL);
            char *exec = g_key_file_get_string(k, "Desktop Entry", "Exec", NULL);
            char **argv = NULL;
            if (name && exec && g_shell_parse_argv(exec, NULL, &argv, NULL)) {
                GStrvBuilder *b = g_strv_builder_new();
                g_strv_builder_add(b, "dbus-run-session");
                g_strv_builder_addv(b, (const char **)argv);
                add_session(name, g_strv_builder_end(b));
                g_strv_builder_unref(b);
                g_strfreev(argv);
            }
            g_free(name); g_free(exec);
        }
        g_key_file_free(k);
        g_free(path);
    }
    if (d) g_dir_close(d);
    if (!sessions->len) { char *c[] = { "/bin/sh", "-l", NULL }; add_session("shell", g_strdupv(c)); }
}

/* ---------- интерфейс ---------- */
static GtkWidget *make_avatar(User *u, int size) {
    char *face = g_build_filename(u->home, ".face", NULL);
    char *sys = g_build_filename(CONF_DIR, "avatars", u->name, NULL);
    const char *img = g_file_test(sys, G_FILE_TEST_EXISTS) ? sys : g_file_test(face, G_FILE_TEST_EXISTS) ? face : NULL;
    GtkWidget *w;
    if (img) {
        w = gtk_picture_new_for_filename(img);
        gtk_picture_set_content_fit(GTK_PICTURE(w), GTK_CONTENT_FIT_COVER);
        gtk_widget_set_overflow(w, GTK_OVERFLOW_HIDDEN);
    } else {
        char letter[8] = { 0 };
        g_unichar_to_utf8(g_unichar_toupper(g_utf8_get_char(u->real)), letter);
        w = gtk_label_new(letter);
    }
    gtk_widget_add_css_class(w, "avatar");
    gtk_widget_set_size_request(w, size, size);
    gtk_widget_set_halign(w, GTK_ALIGN_CENTER);
    g_free(face); g_free(sys);
    return w;
}

static void show_user(void);
static void on_pick_user(GtkButton *b, gpointer idx) { cur_user = GPOINTER_TO_INT(idx); show_user(); }

static void show_user(void) {
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(avatar_box))) gtk_box_remove(GTK_BOX(avatar_box), c);
    for (guint i = 0; i < users->len; i++) {
        User *u = users->pdata[i];
        gboolean on = (int)i == cur_user;
        GtkWidget *b = gtk_button_new();
        gtk_widget_add_css_class(b, "user");
        if (on) gtk_widget_add_css_class(b, "on");
        gtk_button_set_child(GTK_BUTTON(b), make_avatar(u, on ? 92 : 54));
        gtk_widget_set_tooltip_text(b, u->real);
        gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
        gtk_widget_set_focus_on_click(b, FALSE);
        g_signal_connect(b, "clicked", G_CALLBACK(on_pick_user), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(avatar_box), b);
    }
    gtk_label_set_text(GTK_LABEL(name_lbl), ((User *)users->pdata[cur_user])->real);
    gtk_label_set_text(GTK_LABEL(msg), "");
    gtk_editable_set_text(GTK_EDITABLE(pass), "");
    pulse_class(name_lbl, "swap", 400);
    gtk_widget_grab_focus(pass);
}

static gboolean tick(gpointer u) {
    GDateTime *now = g_date_time_new_now_local();
    char *t = g_date_time_format(now, "%H:%M");
    char *d = g_date_time_format(now, "%A, %e %B");
    gtk_label_set_text(GTK_LABEL(clock_lbl), t);
    gtk_label_set_text(GTK_LABEL(date_lbl), g_strstrip(d));
    g_free(t); g_free(d);
    g_date_time_unref(now);
    return G_SOURCE_CONTINUE;
}

static void power(GtkButton *b, gpointer what) {
    if (demo) { char *s = g_strdup_printf("Демо: тут выполнилось бы loginctl %s", (char *)what); gtk_label_set_text(GTK_LABEL(msg), s); g_free(s); return; }
    char *argv[] = { "loginctl", what, NULL };
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
}

static GtkWidget *power_btn(const char *icon, const char *tip, const char *what) {
    GtkWidget *b = gtk_button_new_from_icon_name(icon);
    gtk_widget_add_css_class(b, "power");
    gtk_widget_set_tooltip_text(b, tip);
    gtk_widget_set_focus_on_click(b, FALSE);
    g_signal_connect(b, "clicked", G_CALLBACK(power), (gpointer)what);
    return b;
}

static gboolean on_key(GtkEventControllerKey *c, guint kv, guint kc, GdkModifierType st, gpointer u) {
    gboolean was_idle = gtk_widget_has_css_class(root, "idle");
    poke();
    if (was_idle && (kv == GDK_KEY_Escape || kv == GDK_KEY_space)) return TRUE;   /* просто разбудить */
    if (users->len > 1 && (kv == GDK_KEY_Up || kv == GDK_KEY_Down)) {   /* ↑↓ — другой пользователь */
        cur_user = (cur_user + (kv == GDK_KEY_Down ? 1 : (int)users->len - 1)) % users->len;
        show_user();
        return TRUE;
    }
    return FALSE;
}
static void on_motion(GtkEventControllerMotion *c, double x, double y, gpointer u) {
    static double lx = -1, ly = -1;
    if (lx >= 0 && (ABS(x - lx) > 3 || ABS(y - ly) > 3)) poke();
    lx = x; ly = y;
}

/* Лаванда: #18161d фон · #211e28 поверхности · #e6e1ef текст · #bba7e3 акцент · #a7cdb6 шалфей · #e3a7b5 ошибка */
static const char *CSS =
    "window { background: #18161d; color: #e6e1ef; font-family: 'Inter', 'Noto Sans', sans-serif; }"
    /* обои: проявляются из темноты с приближением, потом медленно дышат */
    ".wall { animation: wall-in 1800ms cubic-bezier(.2,.8,.2,1) backwards; }"
    ".drift { animation: drift 38s ease-in-out infinite alternate; }"
    "@keyframes wall-in { from { opacity: 0; transform: scale(1.08); } }"
    "@keyframes drift { from { transform: scale(1.0); } to { transform: scale(1.045) translate(-14px, 8px); } }"
    ".shade { background: linear-gradient(to bottom, alpha(#18161d, 0.15), alpha(#18161d, 0.55)); }"
    /* появление по очереди */
    ".clock { font-size: 104px; font-weight: 200; letter-spacing: 1px; color: #f1edf7;"
    "         animation: rise-down 1100ms cubic-bezier(.2,.8,.2,1) 250ms backwards;"
    "         transition: transform 900ms cubic-bezier(.2,.8,.2,1), opacity 600ms; }"
    ".date { font-size: 17px; color: #b9b2c6; margin-bottom: 30px;"
    "        animation: rise-down 1100ms cubic-bezier(.2,.8,.2,1) 400ms backwards; transition: transform 900ms cubic-bezier(.2,.8,.2,1), opacity 600ms; }"
    ".card { background: alpha(#211e28, 0.78); border-radius: 26px; padding: 30px 36px 24px;"
    "        border: 1px solid alpha(#bba7e3, 0.14); box-shadow: 0 24px 60px alpha(black, 0.45);"
    "        animation: rise-up 1000ms cubic-bezier(.2,.8,.2,1) 600ms backwards;"
    "        transition: opacity 700ms ease, transform 800ms cubic-bezier(.2,.8,.2,1); }"
    ".bottom { animation: fade 1200ms ease 1000ms backwards; transition: opacity 700ms; }"
    "@keyframes rise-down { from { opacity: 0; transform: translateY(-26px); } }"
    "@keyframes rise-up { from { opacity: 0; transform: translateY(34px) scale(0.96); } }"
    "@keyframes fade { from { opacity: 0; } }"
    /* повтор появления в демо после «входа» */
    ".again-a .clock, .again-a .date { animation: ra-down 900ms cubic-bezier(.2,.8,.2,1) backwards; }"
    ".again-a .card { animation: ra-up 900ms cubic-bezier(.2,.8,.2,1) 150ms backwards; }"
    ".again-a .wall, .again-a .bottom { animation: ra-fade 800ms ease backwards; }"
    ".again-b .clock, .again-b .date { animation: rb-down 900ms cubic-bezier(.2,.8,.2,1) backwards; }"
    ".again-b .card { animation: rb-up 900ms cubic-bezier(.2,.8,.2,1) 150ms backwards; }"
    ".again-b .wall, .again-b .bottom { animation: rb-fade 800ms ease backwards; }"
    "@keyframes ra-down { from { opacity: 0; transform: translateY(-26px); } } @keyframes rb-down { from { opacity: 0; transform: translateY(-26px); } }"
    "@keyframes ra-up { from { opacity: 0; transform: translateY(34px) scale(0.96); } } @keyframes rb-up { from { opacity: 0; transform: translateY(34px) scale(0.96); } }"
    "@keyframes ra-fade { from { opacity: 0; } } @keyframes rb-fade { from { opacity: 0; } }"
    /* ошибка: карточка качается, поле вспыхивает розовым, текст проявляется */
    ".shaker.shake { animation: shake 480ms cubic-bezier(.36,.07,.19,.97); }"
    "@keyframes shake { 0%,100% { transform: none; } 15% { transform: translateX(-12px); } 35% { transform: translateX(10px); }"
    "  55% { transform: translateX(-6px); } 75% { transform: translateX(3px); } }"
    ".msg { color: #e3a7b5; min-height: 20px; margin-top: 12px; font-size: 13px; }"
    ".msg.show { animation: msg-in 420ms ease-out; }"
    "@keyframes msg-in { from { opacity: 0; transform: translateY(-4px); } }"
    /* аватар с дышащим кольцом */
    "button.user { background: none; border: none; box-shadow: none; padding: 5px; border-radius: 999px; opacity: 0.5;"
    "              transition: opacity 300ms, transform 300ms cubic-bezier(.2,.8,.2,1); }"
    "button.user:hover { opacity: 0.85; transform: scale(1.05); }"
    "button.user.on { opacity: 1; }"
    ".avatar { border-radius: 999px; background: alpha(#bba7e3, 0.16); color: #cdbdf0; font-size: 32px; font-weight: 500; }"
    "button.user.on .avatar { font-size: 42px; animation: breathe 4.5s ease-in-out infinite; }"
    "@keyframes breathe {"
    "  0%,100% { box-shadow: 0 0 0 2px alpha(#bba7e3, 0.55), 0 0 18px 0 alpha(#bba7e3, 0.18); }"
    "  50%     { box-shadow: 0 0 0 2px alpha(#bba7e3, 0.85), 0 0 38px 4px alpha(#bba7e3, 0.38); } }"
    ".name { font-size: 21px; font-weight: 600; margin: 12px 0 20px; }"
    ".name.swap { animation: fade 400ms ease; }"
    /* поле пароля */
    "entry.password { min-height: 44px; min-width: 290px; border-radius: 14px; padding: 0 14px;"
    "        background: alpha(#18161d, 0.85); color: #e6e1ef; border: 1px solid alpha(#9a93a8, 0.28); box-shadow: none; outline: none;"
    "        transition: border-color 250ms, box-shadow 350ms, background 250ms; }"
    "entry.password:focus-within { border-color: alpha(#bba7e3, 0.8); box-shadow: 0 0 0 4px alpha(#bba7e3, 0.14); }"
    "entry.password.error { border-color: #e3a7b5; box-shadow: 0 0 0 4px alpha(#e3a7b5, 0.18); }"
    "entry.password text placeholder { color: alpha(#b9b2c6, 0.6); }"
    /* настоящий текст и курсор GTK не видны — их заменяют точки */
    "entry.password text { color: transparent; caret-color: transparent; }"
    "entry.password text selection { background: transparent; color: transparent; }"
    "entry.password.tap-a { animation: tap-a 320ms ease-out; } entry.password.tap-b { animation: tap-b 320ms ease-out; }"
    "@keyframes tap-a { from { box-shadow: 0 0 0 7px alpha(#bba7e3, 0.26); border-color: #cdbdf0; } }"
    "@keyframes tap-b { from { box-shadow: 0 0 0 7px alpha(#bba7e3, 0.26); border-color: #cdbdf0; } }"
    ".pd { min-width: 9px; min-height: 9px; border-radius: 99px; background: #cdbdf0;"
    "      animation: pd-in 300ms ease-out backwards; }"
    "@keyframes pd-in { 0% { opacity: 0; transform: scale(0.2) translateY(6px); } 55% { opacity: 1; transform: scale(1.35); } 100% { transform: scale(1); } }"
    ".pd.out { animation: pd-out 170ms ease-in forwards; }"
    "@keyframes pd-out { to { opacity: 0; transform: scale(0.2); } }"
    ".pd.drop { animation: pd-drop 460ms cubic-bezier(.5,0,.9,.5) forwards; background: #e3a7b5; }"
    ".pd.drop.d1 { animation-delay: 40ms; } .pd.drop.d2 { animation-delay: 80ms; }"
    "@keyframes pd-drop { 30% { opacity: 1; transform: translateY(-3px); } to { opacity: 0; transform: translateY(22px) scale(0.6); } }"
    ".pcaret { min-width: 2px; min-height: 18px; border-radius: 2px; background: #bba7e3; opacity: 0;"
    "          transition: opacity 200ms; }"
    ".pcaret { animation: caret 1.1s ease-in-out infinite; }"
    "@keyframes caret { 0%, 100% { opacity: 0.95; } 50% { opacity: 0.1; } }"
    "button.go { min-width: 44px; min-height: 44px; border-radius: 14px; background: #bba7e3; color: #2a1f45; border: none; box-shadow: none;"
    "            transition: background 200ms, transform 250ms cubic-bezier(.2,.8,.2,1); }"
    "button.go:hover { background: #cdbdf0; transform: translateX(2px); }"
    /* «думаю»: три точки по очереди */
    ".dots { min-width: 44px; }"
    ".dot { min-width: 7px; min-height: 7px; border-radius: 99px; background: #bba7e3; animation: dot 1s ease-in-out infinite; }"
    ".dot.d2 { animation-delay: 160ms; } .dot.d3 { animation-delay: 320ms; }"
    "@keyframes dot { 0%,100% { opacity: 0.25; transform: translateY(0); } 40% { opacity: 1; transform: translateY(-4px); } }"
    "dropdown > button { background: transparent; border: none; box-shadow: none; color: #b9b2c6; font-size: 13px; border-radius: 10px; }"
    "dropdown > button:hover { background: alpha(#bba7e3, 0.1); }"
    "button.power { background: alpha(#211e28, 0.7); color: #b9b2c6; border: none; box-shadow: none; min-width: 44px; min-height: 44px; border-radius: 999px;"
    "               transition: background 250ms, color 250ms, transform 250ms cubic-bezier(.2,.8,.2,1); }"
    "button.power:hover { background: alpha(#bba7e3, 0.22); color: #f1edf7; transform: translateY(-2px); }"
    ".hint { color: alpha(#b9b2c6, 0.5); font-size: 12px; }"
    /* простой: карточка уплывает, часы опускаются в центр */
    ".idle .card { opacity: 0; transform: translateY(40px) scale(0.97); }"
    ".idle .clock { transform: translateY(120px) scale(1.3); }"
    ".idle .date { transform: translateY(150px); }"
    ".idle .bottom { opacity: 0.25; }"
    /* вход: всё растворяется, обои гаснут */
    ".leave .card { animation: out-down 600ms cubic-bezier(.4,0,.2,1) forwards; }"
    ".leave .clock, .leave .date { animation: out-up 600ms cubic-bezier(.4,0,.2,1) forwards; }"
    ".leave .bottom { animation: out 400ms ease forwards; }"
    ".leave .wall { animation: wall-out 700ms ease forwards; }"
    "@keyframes out-down { to { opacity: 0; transform: translateY(18px) scale(0.95); } }"
    "@keyframes out-up { to { opacity: 0; transform: translateY(-18px); } }"
    "@keyframes out { to { opacity: 0; } }"
    "@keyframes wall-out { to { opacity: 0; transform: scale(1.04); } }";

/* только для --demo: набирает текст из LAVENDER_GREETER_DEMO_TYPE по букве, чтобы увидеть анимацию */
static gboolean demo_type(gpointer u) {
    static int i;
    const char *t = g_getenv("LAVENDER_GREETER_DEMO_TYPE");
    if (!t) return G_SOURCE_REMOVE;
    if (i >= (int)g_utf8_strlen(t, -1)) {   /* дописали — отправляем, как Enter */
        if (g_getenv("LAVENDER_GREETER_DEMO_SUBMIT")) g_signal_emit_by_name(pass, "activate");
        return G_SOURCE_REMOVE;
    }
    char *pre = g_utf8_substring(t, 0, ++i);
    gtk_editable_set_text(GTK_EDITABLE(pass), pre);
    gtk_editable_set_position(GTK_EDITABLE(pass), -1);
    g_free(pre);
    return G_SOURCE_CONTINUE;
}

static void activate(GtkApplication *app, gpointer u) {
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, CSS);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);

    win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "lavender-greeter");
    gtk_window_set_default_size(GTK_WINDOW(win), 1280, 800);
    root = gtk_overlay_new();
    gtk_widget_set_overflow(root, GTK_OVERFLOW_HIDDEN);

    /* обои: .wall (появление/уход) → .drift (медленное дыхание) → картинка */
    GtkWidget *wall = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(wall, "wall");
    if (wallpaper && g_file_test(wallpaper, G_FILE_TEST_EXISTS)) {
        GtkWidget *drift = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(drift, "drift");
        GtkWidget *bg = gtk_picture_new_for_filename(wallpaper);
        gtk_picture_set_content_fit(GTK_PICTURE(bg), GTK_CONTENT_FIT_COVER);
        gtk_widget_set_vexpand(bg, TRUE);
        gtk_box_append(GTK_BOX(drift), bg);
        gtk_widget_set_vexpand(drift, TRUE);
        gtk_box_append(GTK_BOX(wall), drift);
    }
    gtk_overlay_set_child(GTK_OVERLAY(root), wall);
    GtkWidget *shade = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(shade, "shade");
    gtk_widget_set_can_target(shade, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(root), shade);

    GtkWidget *center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(center, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center, GTK_ALIGN_CENTER);
    clock_lbl = gtk_label_new("");
    gtk_widget_add_css_class(clock_lbl, "clock");
    date_lbl = gtk_label_new("");
    gtk_widget_add_css_class(date_lbl, "date");
    gtk_box_append(GTK_BOX(center), clock_lbl);
    gtk_box_append(GTK_BOX(center), date_lbl);

    card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    avatar_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(avatar_box, GTK_ALIGN_CENTER);
    name_lbl = gtk_label_new("");
    gtk_widget_add_css_class(name_lbl, "name");

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    pass = gtk_password_entry_new();
    g_object_set(pass, "placeholder-text", "Пароль", "activates-default", FALSE, NULL);
    gtk_widget_set_hexpand(pass, TRUE);
    g_signal_connect(pass, "activate", G_CALLBACK(on_login), NULL);
    g_signal_connect(pass, "changed", G_CALLBACK(on_pass_changed), NULL);
    /* GTK рисует свои точки на месте символов — делаем их пробелами, видны только наши */
    GtkEditable *dl = gtk_editable_get_delegate(GTK_EDITABLE(pass));
    if (GTK_IS_TEXT(dl)) gtk_text_set_invisible_char(GTK_TEXT(dl), ' ');
    live = g_ptr_array_new();
    GtkWidget *pov = gtk_overlay_new();
    gtk_widget_set_hexpand(pov, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(pov), pass);
    pdots = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(pdots, "pdots");
    gtk_widget_set_halign(pdots, GTK_ALIGN_START);
    gtk_widget_set_valign(pdots, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(pdots, 16);
    gtk_widget_set_margin_end(pdots, 16);
    gtk_widget_set_overflow(pdots, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_can_target(pdots, FALSE);
    pcaret = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(pcaret, "pcaret");
    gtk_widget_set_valign(pcaret, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(pdots), pcaret);
    gtk_widget_set_visible(pcaret, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(pov), pdots);
    go_btn = gtk_button_new_from_icon_name("go-next-symbolic");
    gtk_widget_add_css_class(go_btn, "go");
    gtk_widget_set_tooltip_text(go_btn, "Войти");
    g_signal_connect(go_btn, "clicked", G_CALLBACK(on_login), NULL);
    dots = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_add_css_class(dots, "dots");
    gtk_widget_set_halign(dots, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(dots, GTK_ALIGN_CENTER);
    for (int i = 1; i <= 3; i++) {
        GtkWidget *d = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(d, "dot");
        if (i > 1) gtk_widget_add_css_class(d, i == 2 ? "d2" : "d3");
        gtk_widget_set_valign(d, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(dots), d);
    }
    gtk_widget_set_visible(dots, FALSE);
    gtk_box_append(GTK_BOX(row), pov);
    gtk_box_append(GTK_BOX(row), dots);
    gtk_box_append(GTK_BOX(row), go_btn);

    msg = gtk_label_new("");
    gtk_widget_add_css_class(msg, "msg");
    gtk_label_set_wrap(GTK_LABEL(msg), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(msg), 36);

    const char **names = g_new0(const char *, sessions->len + 1);
    for (guint i = 0; i < sessions->len; i++) names[i] = ((Session *)sessions->pdata[i])->name;
    sess_dd = gtk_drop_down_new_from_strings(names);
    g_free(names);
    gtk_widget_set_halign(sess_dd, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(sess_dd, 4);
    gtk_widget_set_visible(sess_dd, sessions->len > 1);
    gtk_widget_set_tooltip_text(sess_dd, "Сессия");

    gtk_box_append(GTK_BOX(card), avatar_box);
    gtk_box_append(GTK_BOX(card), name_lbl);
    gtk_box_append(GTK_BOX(card), row);
    gtk_box_append(GTK_BOX(card), msg);
    gtk_box_append(GTK_BOX(card), sess_dd);
    /* тряска — на обёртке: если трясти саму карточку, после снятия класса GTK перезапускает её анимацию появления */
    shaker = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(shaker, "shaker");
    gtk_widget_set_halign(shaker, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(shaker), card);
    gtk_box_append(GTK_BOX(center), shaker);
    gtk_overlay_add_overlay(GTK_OVERLAY(root), center);

    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(bottom, "bottom");
    gtk_widget_set_halign(bottom, GTK_ALIGN_FILL);
    gtk_widget_set_valign(bottom, GTK_ALIGN_END);
    gtk_widget_set_margin_start(bottom, 28);
    gtk_widget_set_margin_end(bottom, 28);
    gtk_widget_set_margin_bottom(bottom, 24);
    GtkWidget *hint = gtk_label_new(users->len > 1 ? "↑↓ пользователь · Ctrl+Shift раскладка · Ctrl+Alt+F2 консоль"
                                                   : "Ctrl+Shift раскладка · Ctrl+Alt+F2 консоль");
    gtk_widget_add_css_class(hint, "hint");
    gtk_widget_set_hexpand(hint, TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_box_append(GTK_BOX(bottom), hint);
    gtk_box_append(GTK_BOX(bottom), power_btn("system-reboot-symbolic", "Перезагрузка", "reboot"));
    gtk_box_append(GTK_BOX(bottom), power_btn("system-shutdown-symbolic", "Выключение", "poweroff"));
    gtk_overlay_add_overlay(GTK_OVERLAY(root), bottom);

    gtk_window_set_child(GTK_WINDOW(win), root);
    GtkEventController *kc = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_CAPTURE);
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(win, kc);
    GtkEventController *mc = gtk_event_controller_motion_new();
    g_signal_connect(mc, "motion", G_CALLBACK(on_motion), NULL);
    gtk_widget_add_controller(win, mc);

    tick(NULL);
    g_timeout_add_seconds(1, tick, NULL);
    show_user();
    poke();
    if (!demo) gtk_window_fullscreen(GTK_WINDOW(win));
    gtk_window_present(GTK_WINDOW(win));
    gtk_widget_grab_focus(pass);
    if (demo && g_getenv("LAVENDER_GREETER_DEMO_TYPE")) g_timeout_add(260, demo_type, NULL);
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) if (g_str_equal(argv[i], "--demo")) demo = TRUE;
    GKeyFile *k = g_key_file_new();
    g_key_file_load_from_file(k, CONF_DIR "/config.ini", G_KEY_FILE_NONE, NULL);
    wallpaper = g_key_file_get_string(k, "greeter", "wallpaper", NULL);
    if (!wallpaper) wallpaper = g_strdup(CONF_DIR "/wallpaper.png");
    g_key_file_free(k);
    for (int i = 1; i < argc - 1; i++) if (g_str_equal(argv[i], "--wallpaper")) { g_free(wallpaper); wallpaper = g_strdup(argv[i + 1]); }
    load_users();
    load_sessions();
    GtkApplication *app = gtk_application_new(NULL, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int rc = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    return rc;
}
