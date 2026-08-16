#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stddef.h>
#include <hybris/surface_flinger/surface_flinger_compatibility_layer.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server.h>

#include "xdg-shell-server-protocol.h"

#define SCREEN_W 1080
#define SCREEN_H 1920
#define OUTPUT_W 540
#define OUTPUT_H 960
#define GL_BGRA_EXT 0x80E1

struct Surface {
    wl_resource *resource;
    wl_resource *pending_buffer;
    wl_resource *xdg_surface;
    wl_resource *toplevel;
    wl_list frame_callbacks;
    int scale;
};

struct FrameCallback {
    wl_resource *resource;
    wl_list link;
};

struct TouchPoint {
    int tracking_id;
    int sent_id;
    int x, y;
    int old_x, old_y;
    bool active, was_active;
};

struct Server {
    wl_display *display;
    wl_event_loop *loop;
    wl_resource *touch;
    wl_resource *keyboard;
    Surface *active;
    SfClient *sf_client;
    SfSurface *sf_surface;
    EGLDisplay egl_display;
    EGLSurface egl_surface;
    GLuint program, texture, vbo;
    GLint sampler;
    int input_fd;
    bool touch_enabled;
    int abs_x_min, abs_x_max, abs_y_min, abs_y_max;
    int slot, next_touch_id;
    TouchPoint points[16];
};

static Server g;

static uint32_t monotonic_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

static uintptr_t module_base(const char *name)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[512];
    uintptr_t result = 0;
    while (maps && fgets(line, sizeof(line), maps)) {
        unsigned long start = 0, offset = 1;
        if (strstr(line, name) &&
            sscanf(line, "%lx-%*lx %*4s %lx", &start, &offset) == 2 &&
            offset == 0) {
            result = (uintptr_t)start;
            break;
        }
    }
    if (maps) fclose(maps);
    return result;
}

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, 0);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), 0, log);
        fprintf(stderr, "shader failure: %s\n", log);
        return 0;
    }
    return shader;
}

static bool init_power_vr()
{
    SfSurfaceCreationParameters params = {
        0, 0, SCREEN_W, SCREEN_H, -1, INT_MAX, 1.0f, 1,
        "P90 Wayland PowerVR compositor"
    };
    g.sf_client = sf_client_create();
    if (!g.sf_client) return false;
    g.sf_surface = sf_surface_create(g.sf_client, &params);
    if (!g.sf_surface) return false;
    sf_surface_make_current(g.sf_surface);
    g.egl_display = sf_client_get_egl_display(g.sf_client);
    g.egl_surface = sf_surface_get_egl_surface(g.sf_surface);
    EGLContext context = sf_client_get_egl_context(g.sf_client);
    uintptr_t base = module_base("libGLESv2_POWERVR_ROGUE.so");
    void *vendor_context = context ? ((void **)context)[4] : 0;
    void *gles_context = vendor_context ? ((void **)vendor_context)[8] : 0;
    if (!base || !gles_context ||
        !((int (*)(void *))(base + 0x3eb30))(gles_context)) return false;

    const char *vs =
        "attribute vec2 p; attribute vec2 t; varying vec2 uv;"
        "void main(){gl_Position=vec4(p,0.0,1.0);uv=t;}";
    const char *fs =
        "precision mediump float; varying vec2 uv; uniform sampler2D tex;"
        "void main(){gl_FragColor=texture2D(tex,uv);}";
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return false;
    g.program = glCreateProgram();
    glAttachShader(g.program, v);
    glAttachShader(g.program, f);
    glBindAttribLocation(g.program, 0, "p");
    glBindAttribLocation(g.program, 1, "t");
    glLinkProgram(g.program);
    GLint linked = 0;
    glGetProgramiv(g.program, GL_LINK_STATUS, &linked);
    if (!linked) return false;
    g.sampler = glGetUniformLocation(g.program, "tex");
    const GLfloat vertices[] = {
        -1, -1, 0, 1,  1, -1, 1, 1,
        -1,  1, 0, 0,  1,  1, 1, 0
    };
    glGenBuffers(1, &g.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glGenTextures(1, &g.texture);
    glBindTexture(GL_TEXTURE_2D, g.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glClearColor(0.03f, 0.03f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(g.egl_display, g.egl_surface);
    fprintf(stderr, "PowerVR ready: %s\n", glGetString(GL_RENDERER));
    return true;
}

static void render_surface(Surface *s)
{
    static bool first_frame = true;
    static unsigned long frame_count = 0;
    wl_resource *buffer_resource = s->pending_buffer;
    s->pending_buffer = 0;
    if (!buffer_resource) return;
    wl_shm_buffer *buffer = wl_shm_buffer_get(buffer_resource);
    if (!buffer) {
        fprintf(stderr, "non-shm buffer ignored\n");
        wl_buffer_send_release(buffer_resource);
        return;
    }
    int width = wl_shm_buffer_get_width(buffer);
    int height = wl_shm_buffer_get_height(buffer);
    int stride = wl_shm_buffer_get_stride(buffer);
    uint32_t format = wl_shm_buffer_get_format(buffer);
    wl_shm_buffer_begin_access(buffer);
    void *data = wl_shm_buffer_get_data(buffer);
    uint32_t sample_hash = 2166136261U;
    unsigned char *bytes = (unsigned char *)data;
    size_t byte_count = (size_t)stride * height;
    for (size_t offset = 0; offset < byte_count; offset += 4093) {
        sample_hash ^= bytes[offset];
        sample_hash *= 16777619U;
    }
    uint32_t top_left = *(uint32_t *)data;
    uint32_t center = *(uint32_t *)(bytes + (height / 2) * stride +
                                    (width / 2) * 4);
    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glUseProgram(g.program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (stride == width * 4 &&
        (format == WL_SHM_FORMAT_ARGB8888 || format == WL_SHM_FORMAT_XRGB8888)) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, width, height, 0,
                     GL_BGRA_EXT, GL_UNSIGNED_BYTE, data);
    } else {
        fprintf(stderr, "unsupported shm %dx%d stride=%d format=0x%x\n",
                width, height, stride, format);
    }
    wl_shm_buffer_end_access(buffer);
    GLenum upload_error = glGetError();
    glUniform1i(g.sampler, 0);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          (void *)(2 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    GLenum draw_error = glGetError();
    if (!eglSwapBuffers(g.egl_display, g.egl_surface))
        fprintf(stderr, "eglSwapBuffers failed: 0x%x\n", eglGetError());
    ++frame_count;
    if (first_frame || frame_count <= 10 || frame_count % 120 == 0) {
        fprintf(stderr,
                "shm frame=%lu %dx%d stride=%d hash=%08x tl=%08x "
                "center=%08x upload-gl=0x%x draw-gl=0x%x\n",
                frame_count, width, height, stride, sample_hash, top_left,
                center, upload_error, draw_error);
    }
    if (first_frame) {
        fprintf(stderr, "first shm frame presented: %dx%d stride=%d\n",
                width, height, stride);
        first_frame = false;
    }
    wl_buffer_send_release(buffer_resource);

    FrameCallback *cb, *tmp;
    wl_list_for_each_safe(cb, tmp, &s->frame_callbacks, link) {
        wl_callback_send_done(cb->resource, monotonic_ms());
        wl_resource_destroy(cb->resource);
    }
}

static void resource_destroy(wl_client *, wl_resource *resource)
{ wl_resource_destroy(resource); }

static void surface_resource_destroy(wl_resource *resource)
{
    Surface *s = (Surface *)wl_resource_get_user_data(resource);
    if (g.active == s) g.active = 0;
    FrameCallback *cb, *tmp;
    wl_list_for_each_safe(cb, tmp, &s->frame_callbacks, link)
        wl_resource_destroy(cb->resource);
    delete s;
}

static void frame_destroy(wl_resource *resource)
{
    FrameCallback *cb = (FrameCallback *)wl_resource_get_user_data(resource);
    wl_list_remove(&cb->link);
    delete cb;
}

static void surface_attach(wl_client *, wl_resource *resource,
                           wl_resource *buffer, int32_t, int32_t)
{
    Surface *s = (Surface *)wl_resource_get_user_data(resource);
    s->pending_buffer = buffer;
}
static void surface_damage(wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {}
static void surface_frame(wl_client *client, wl_resource *resource, uint32_t id)
{
    Surface *s = (Surface *)wl_resource_get_user_data(resource);
    FrameCallback *cb = new FrameCallback();
    cb->resource = wl_resource_create(client, &wl_callback_interface, 1, id);
    wl_resource_set_implementation(cb->resource, 0, cb, frame_destroy);
    wl_list_insert(s->frame_callbacks.prev, &cb->link);
}
static void surface_region(wl_client *, wl_resource *, wl_resource *) {}
static void surface_commit(wl_client *, wl_resource *resource)
{
    Surface *s = (Surface *)wl_resource_get_user_data(resource);
    if (s == g.active) render_surface(s);
    else if (s->pending_buffer) {
        wl_buffer_send_release(s->pending_buffer);
        s->pending_buffer = 0;
    }
}
static void surface_transform(wl_client *, wl_resource *, int32_t) {}
static void surface_scale(wl_client *, wl_resource *resource, int32_t scale)
{ ((Surface *)wl_resource_get_user_data(resource))->scale = scale; }
static void surface_offset(wl_client *, wl_resource *, int32_t, int32_t) {}

static const struct wl_surface_interface surface_impl = {
    resource_destroy, surface_attach, surface_damage, surface_frame,
    surface_region, surface_region, surface_commit, surface_transform,
    surface_scale, surface_damage, surface_offset
};

static void compositor_create_surface(wl_client *client, wl_resource *, uint32_t id)
{
    Surface *s = new Surface();
    memset(s, 0, sizeof(*s));
    s->scale = 1;
    wl_list_init(&s->frame_callbacks);
    s->resource = wl_resource_create(client, &wl_surface_interface, 6, id);
    wl_resource_set_implementation(s->resource, &surface_impl, s,
                                   surface_resource_destroy);
}
static void region_op(wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {}
static const struct wl_region_interface region_impl = { resource_destroy, region_op, region_op };
static void compositor_create_region(wl_client *client, wl_resource *, uint32_t id)
{
    wl_resource *r = wl_resource_create(client, &wl_region_interface, 1, id);
    wl_resource_set_implementation(r, &region_impl, 0, 0);
}
static const struct wl_compositor_interface compositor_impl = {
    compositor_create_surface, compositor_create_region
};
static void bind_compositor(wl_client *client, void *, uint32_t version, uint32_t id)
{
    wl_resource *r = wl_resource_create(client, &wl_compositor_interface,
                                        version < 6 ? version : 6, id);
    wl_resource_set_implementation(r, &compositor_impl, 0, 0);
}

static void output_release(wl_client *, wl_resource *r) { wl_resource_destroy(r); }
static const struct wl_output_interface output_impl = { output_release };
static void bind_output(wl_client *client, void *, uint32_t version, uint32_t id)
{
    uint32_t v = version < 3 ? version : 3;
    wl_resource *r = wl_resource_create(client, &wl_output_interface, v, id);
    wl_resource_set_implementation(r, &output_impl, 0, 0);
    wl_output_send_geometry(r, 0, 0, 68, 121, WL_OUTPUT_SUBPIXEL_UNKNOWN,
                            "Lenovo", "P90", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(r, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                        OUTPUT_W, OUTPUT_H, 60000);
    if (v >= 2) { wl_output_send_scale(r, 1); wl_output_send_done(r); }
}

static void touch_release(wl_client *, wl_resource *r) { wl_resource_destroy(r); }
static void touch_gone(wl_resource *r) { if (g.touch == r) g.touch = 0; }
static const struct wl_touch_interface touch_impl = { touch_release };
static void keyboard_release(wl_client *, wl_resource *r) { wl_resource_destroy(r); }
static void keyboard_gone(wl_resource *r) { if (g.keyboard == r) g.keyboard = 0; }
static const struct wl_keyboard_interface keyboard_impl = { keyboard_release };
static const char keymap_text[] =
    "xkb_keymap {\n"
    " xkb_keycodes \"evdev\" { include \"evdev+aliases(qwerty)\" };\n"
    " xkb_types \"complete\" { include \"complete\" };\n"
    " xkb_compatibility \"complete\" { include \"complete\" };\n"
    " xkb_symbols \"pc+us\" { include \"pc+us\" };\n"
    "};\n";
static void seat_get_pointer(wl_client *c, wl_resource *r, uint32_t)
{ wl_resource_post_error(r, WL_SEAT_ERROR_MISSING_CAPABILITY, "no pointer"); }
static void send_keyboard_enter()
{
    if (!g.keyboard || !g.active) return;
    wl_array keys;
    wl_array_init(&keys);
    wl_keyboard_send_enter(g.keyboard, wl_display_next_serial(g.display),
                           g.active->resource, &keys);
    wl_array_release(&keys);
}
static void seat_get_keyboard(wl_client *client, wl_resource *seat, uint32_t id)
{
    uint32_t v = wl_resource_get_version(seat);
    if (v > 7) v = 7;
    g.keyboard = wl_resource_create(client, &wl_keyboard_interface, v, id);
    wl_resource_set_implementation(g.keyboard, &keyboard_impl, 0, keyboard_gone);
    char path[] = "/data/local/tmp/p90-keymap-XXXXXX";
    int fd = mkstemp(path);
    if (fd >= 0) {
        unlink(path);
        write(fd, keymap_text, sizeof(keymap_text));
        lseek(fd, 0, SEEK_SET);
        wl_keyboard_send_keymap(g.keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                fd, sizeof(keymap_text));
        close(fd);
    }
    if (v >= 4) wl_keyboard_send_repeat_info(g.keyboard, 25, 600);
    send_keyboard_enter();
}
static void seat_get_touch(wl_client *client, wl_resource *seat, uint32_t id)
{
    if (!g.touch_enabled) {
        wl_resource_post_error(seat, WL_SEAT_ERROR_MISSING_CAPABILITY,
                               "touch is handled directly by nested compositor");
        return;
    }
    uint32_t v = wl_resource_get_version(seat);
    if (v > 7) v = 7;
    g.touch = wl_resource_create(client, &wl_touch_interface, v, id);
    wl_resource_set_implementation(g.touch, &touch_impl, 0, touch_gone);
}
static const struct wl_seat_interface seat_impl = {
    seat_get_pointer, seat_get_keyboard, seat_get_touch, resource_destroy
};
static void bind_seat(wl_client *client, void *, uint32_t version, uint32_t id)
{
    uint32_t v = version < 7 ? version : 7;
    wl_resource *r = wl_resource_create(client, &wl_seat_interface, v, id);
    wl_resource_set_implementation(r, &seat_impl, 0, 0);
    uint32_t capabilities = WL_SEAT_CAPABILITY_KEYBOARD;
    if (g.touch_enabled) capabilities |= WL_SEAT_CAPABILITY_TOUCH;
    wl_seat_send_capabilities(r, capabilities);
    if (v >= 2) wl_seat_send_name(r, "p90-touchscreen");
}

static void send_configure(Surface *s)
{
    if (!s->xdg_surface || !s->toplevel) return;
    wl_array states;
    wl_array_init(&states);
    uint32_t *state = (uint32_t *)wl_array_add(&states, sizeof(uint32_t));
    if (state) *state = XDG_TOPLEVEL_STATE_FULLSCREEN;
    xdg_toplevel_send_configure(s->toplevel, OUTPUT_W, OUTPUT_H, &states);
    wl_array_release(&states);
    xdg_surface_send_configure(s->xdg_surface, wl_display_next_serial(g.display));
}

static void toplevel_parent(wl_client *, wl_resource *, wl_resource *) {}
static void toplevel_string(wl_client *, wl_resource *, const char *) {}
static void toplevel_menu(wl_client *, wl_resource *, wl_resource *, uint32_t, int32_t, int32_t) {}
static void toplevel_move(wl_client *, wl_resource *, wl_resource *, uint32_t) {}
static void toplevel_resize(wl_client *, wl_resource *, wl_resource *, uint32_t, uint32_t) {}
static void toplevel_size(wl_client *, wl_resource *, int32_t, int32_t) {}
static void toplevel_state(wl_client *, wl_resource *r)
{
    Surface *s = (Surface *)wl_resource_get_user_data(r);
    send_configure(s);
}
static void toplevel_fullscreen(wl_client *, wl_resource *r, wl_resource *)
{ toplevel_state(0, r); }
static const struct xdg_toplevel_interface toplevel_impl = {
    resource_destroy, toplevel_parent, toplevel_string, toplevel_string,
    toplevel_menu, toplevel_move, toplevel_resize, toplevel_size,
    toplevel_size, toplevel_state, toplevel_state, toplevel_fullscreen,
    toplevel_state, toplevel_state
};

static void xdg_surface_get_toplevel(wl_client *client, wl_resource *resource,
                                     uint32_t id)
{
    Surface *s = (Surface *)wl_resource_get_user_data(resource);
    s->toplevel = wl_resource_create(client, &xdg_toplevel_interface, 1, id);
    wl_resource_set_implementation(s->toplevel, &toplevel_impl, s, 0);
    g.active = s;
    send_configure(s);
    send_keyboard_enter();
}
static void xdg_surface_popup(wl_client *, wl_resource *r, uint32_t,
                              wl_resource *, wl_resource *)
{ wl_resource_post_error(r, XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE, "popups unsupported"); }
static void xdg_surface_geometry(wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {}
static void xdg_surface_ack(wl_client *, wl_resource *, uint32_t) {}
static const struct xdg_surface_interface xdg_surface_impl = {
    resource_destroy, xdg_surface_get_toplevel, xdg_surface_popup,
    xdg_surface_geometry, xdg_surface_ack
};
static void xdg_destroy(wl_client *, wl_resource *r) { wl_resource_destroy(r); }
static void xdg_positioner(wl_client *, wl_resource *r, uint32_t)
{ wl_resource_post_error(r, XDG_WM_BASE_ERROR_INVALID_POSITIONER, "positioners unsupported"); }
static void xdg_get_surface(wl_client *client, wl_resource *, uint32_t id,
                            wl_resource *surface_resource)
{
    Surface *s = (Surface *)wl_resource_get_user_data(surface_resource);
    s->xdg_surface = wl_resource_create(client, &xdg_surface_interface, 1, id);
    wl_resource_set_implementation(s->xdg_surface, &xdg_surface_impl, s, 0);
}
static void xdg_pong(wl_client *, wl_resource *, uint32_t) {}
static const struct xdg_wm_base_interface xdg_impl = {
    xdg_destroy, xdg_positioner, xdg_get_surface, xdg_pong
};
static void bind_xdg(wl_client *client, void *, uint32_t version, uint32_t id)
{
    uint32_t v = version < 3 ? version : 3;
    wl_resource *r = wl_resource_create(client, &xdg_wm_base_interface, v, id);
    wl_resource_set_implementation(r, &xdg_impl, 0, 0);
}

static wl_fixed_t scale_axis(int value, int low, int high, int screen)
{
    if (high <= low) return wl_fixed_from_int(0);
    if (value < low) value = low;
    if (value > high) value = high;
    return wl_fixed_from_double((double)(value - low) * (screen - 1) / (high - low));
}

static void flush_touch_frame()
{
    static unsigned long output_frames;
    if (!g.touch || !g.active) return;
    uint32_t time = monotonic_ms();
    bool sent = false;
    for (unsigned i = 0; i < 16; ++i) {
        TouchPoint &p = g.points[i];
        if (p.active && !p.was_active) {
            p.sent_id = g.next_touch_id++;
            wl_touch_send_down(g.touch, wl_display_next_serial(g.display), time,
                               g.active->resource, p.sent_id,
                               scale_axis(p.x, g.abs_x_min, g.abs_x_max, OUTPUT_W),
                               scale_axis(p.y, g.abs_y_min, g.abs_y_max, OUTPUT_H));
            sent = true;
        } else if (!p.active && p.was_active) {
            wl_touch_send_up(g.touch, wl_display_next_serial(g.display), time,
                             p.sent_id);
            sent = true;
        } else if (p.active && (p.x != p.old_x || p.y != p.old_y)) {
            wl_touch_send_motion(g.touch, time, p.sent_id,
                                 scale_axis(p.x, g.abs_x_min, g.abs_x_max, OUTPUT_W),
                                 scale_axis(p.y, g.abs_y_min, g.abs_y_max, OUTPUT_H));
            sent = true;
        }
        p.was_active = p.active;
        p.old_x = p.x;
        p.old_y = p.y;
    }
    if (sent) {
        wl_touch_send_frame(g.touch);
        ++output_frames;
        if (output_frames <= 30)
            fprintf(stderr, "wl_touch frame=%lu serial-next=%u\n",
                    output_frames, wl_display_get_serial(g.display));
    }
}

static int input_ready(int fd, uint32_t, void *)
{
    static unsigned long raw_events;
    input_event events[64];
    ssize_t got;
    while ((got = read(fd, events, sizeof(events))) > 0) {
        for (size_t i = 0; i < (size_t)got / sizeof(events[0]); ++i) {
            input_event &e = events[i];
            if ((e.type == EV_ABS || e.type == EV_SYN || e.type == EV_KEY) &&
                raw_events < 120) {
                fprintf(stderr, "touch raw=%lu type=%u code=%u value=%d\n",
                        ++raw_events, e.type, e.code, e.value);
            }
            if (e.type == EV_ABS) {
                if (e.code == ABS_MT_SLOT) g.slot = e.value;
                else if (g.slot >= 0 && g.slot < 16) {
                    TouchPoint &p = g.points[g.slot];
                    if (e.code == ABS_MT_TRACKING_ID) {
                        p.tracking_id = e.value;
                        p.active = e.value >= 0;
                    } else if (e.code == ABS_MT_POSITION_X) p.x = e.value;
                    else if (e.code == ABS_MT_POSITION_Y) p.y = e.value;
                }
            } else if (e.type == EV_SYN && e.code == SYN_REPORT) {
                flush_touch_frame();
            }
        }
    }
    return 0;
}

static bool init_touch(const char *path)
{
    g.input_fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (g.input_fd < 0) { perror("open touchscreen"); return false; }
    input_absinfo x = {}, y = {};
    if (ioctl(g.input_fd, EVIOCGABS(ABS_MT_POSITION_X), &x) ||
        ioctl(g.input_fd, EVIOCGABS(ABS_MT_POSITION_Y), &y)) {
        perror("EVIOCGABS"); return false;
    }
    g.abs_x_min = x.minimum; g.abs_x_max = x.maximum;
    g.abs_y_min = y.minimum; g.abs_y_max = y.maximum;
    if (ioctl(g.input_fd, EVIOCGRAB, 1))
        perror("EVIOCGRAB (continuing)");
    else
        fprintf(stderr, "touch grab=exclusive\n");
    wl_event_loop_add_fd(g.loop, g.input_fd, WL_EVENT_READABLE, input_ready, 0);
    fprintf(stderr, "touch ready: %s x=%d..%d y=%d..%d\n", path,
            g.abs_x_min, g.abs_x_max, g.abs_y_min, g.abs_y_max);
    return true;
}

int main(int argc, char **argv)
{
    const char *socket = argc > 1 ? argv[1] : "wayland-0";
    const char *touch = argc > 2 ? argv[2] : "/dev/input/event2";
    memset(&g, 0, sizeof(g));
    g.input_fd = -1;
    for (unsigned i = 0; i < 16; ++i) g.points[i].tracking_id = -1;
    if (!init_power_vr()) { fprintf(stderr, "PowerVR init failed\n"); return 10; }
    g.display = wl_display_create();
    if (!g.display) return 11;
    g.loop = wl_display_get_event_loop(g.display);
    if (wl_display_init_shm(g.display)) return 12;
    wl_global_create(g.display, &wl_compositor_interface, 6, 0, bind_compositor);
    wl_global_create(g.display, &wl_output_interface, 3, 0, bind_output);
    wl_global_create(g.display, &wl_seat_interface, 7, 0, bind_seat);
    wl_global_create(g.display, &xdg_wm_base_interface, 3, 0, bind_xdg);
    g.touch_enabled = strcmp(touch, "none") != 0;
    if (g.touch_enabled && !init_touch(touch)) return 13;
    if (wl_display_add_socket(g.display, socket)) {
        perror("wl_display_add_socket"); return 14;
    }
    fprintf(stderr, "Wayland ready: %s/%s\n", getenv("XDG_RUNTIME_DIR"), socket);
    wl_display_run(g.display);
    return 0;
}
