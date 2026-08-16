#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <hybris/hwcomposerwindow/hwcomposer.h>
#include <stdint.h>
#include <stdio.h>

static void present(void *data, struct ANativeWindow *window,
                    struct ANativeWindowBuffer *buffer)
{
    (void)data;
    (void)window;
    (void)buffer;
}

static void dump_tls(const char *stage)
{
    void **tls;
    int i;
    __asm__("movl %%gs:0, %0" : "=r"(tls));
    printf("tls stage=%s base=%p", stage, (void *)tls);
    for (i = 0; i < 12; ++i)
        printf(" slot%d=%p", i, tls[i]);
    putchar('\n');
    fflush(stdout);
}

int main(void)
{
    const EGLint config_attrs[] = {
        EGL_BUFFER_SIZE, 32,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    const EGLint context_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    struct ANativeWindow *window;
    EGLDisplay display;
    EGLConfig config = 0;
    EGLSurface surface;
    EGLContext context;
    EGLint count = 0;
    EGLBoolean ok;
    const GLubyte *version;

    setvbuf(stdout, NULL, _IONBF, 0);
    dump_tls("start");
    window = HWCNativeWindowCreate(1080, 1920, 1, present, NULL);
    printf("window=%p\n", (void *)window);
    dump_tls("window");

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    printf("display=%p error=0x%x\n", display, eglGetError());
    ok = eglInitialize(display, NULL, NULL);
    printf("initialize=%u error=0x%x\n", ok, eglGetError());
    ok = eglChooseConfig(display, config_attrs, &config, 1, &count);
    printf("choose=%u count=%d config=%p error=0x%x\n",
           ok, count, config, eglGetError());
    surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)window, NULL);
    printf("surface=%p error=0x%x\n", surface, eglGetError());
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    printf("context=%p error=0x%x\n", context, eglGetError());
    dump_tls("before-current");
    ok = eglMakeCurrent(display, surface, surface, context);
    printf("make-current=%u error=0x%x\n", ok, eglGetError());
    dump_tls("after-current");
    version = glGetString(GL_VERSION);
    printf("gl-version-pointer=%p value=%s gl-error=0x%x\n",
           (const void *)version, version ? (const char *)version : "(null)", glGetError());
    dump_tls("after-gl");
    return version ? 0 : 3;
}
