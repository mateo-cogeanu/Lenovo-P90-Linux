#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stddef.h>
#include <hybris/surface_flinger/surface_flinger_compatibility_layer.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static uintptr_t module_base(const char *name)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[512];
    uintptr_t result = 0;
    while (maps && fgets(line, sizeof(line), maps)) {
        unsigned long start = 0;
        unsigned long offset = 1;
        if (strstr(line, name) &&
            sscanf(line, "%lx-%*lx %*4s %lx", &start, &offset) == 2 &&
            offset == 0) {
            result = (uintptr_t)start;
            break;
        }
    }
    if (maps)
        fclose(maps);
    return result;
}

int main(void)
{
    SfSurfaceCreationParameters params = {
        0, 0, 1080, 1920, -1, INT_MAX, 1.0f, 1,
        "P90 libhybris PowerVR presentation probe"
    };
    printf("stage=create-client\n");
    fflush(0);
    SfClient *client = sf_client_create();
    printf("client=%p\n", client);
    fflush(0);
    if (!client)
        return 10;

    printf("stage=create-surface\n");
    fflush(0);
    SfSurface *surface = sf_surface_create(client, &params);
    printf("surface=%p\n", surface);
    fflush(0);
    if (!surface)
        return 11;

    sf_surface_make_current(surface);
    EGLDisplay display = sf_client_get_egl_display(client);
    EGLSurface egl_surface = sf_surface_get_egl_surface(surface);
    EGLContext context = sf_client_get_egl_context(client);
    uintptr_t gles_base = module_base("libGLESv2_POWERVR_ROGUE.so");
    void *vendor_context = context ? ((void **)context)[4] : 0;
    void *gles_context = vendor_context ? ((void **)vendor_context)[8] : 0;
    int bridge = 0;
    if (gles_base && gles_context)
        bridge = ((int (*)(void *))(gles_base + 0x3eb30))(gles_context);
    printf("stage=current display=%p surface=%p context=%p gles-base=%p "
           "vendor-context=%p gles-context=%p bridge=%d renderer=%s "
           "egl-error=0x%x\n",
           display, egl_surface, context, (void *)gles_base, vendor_context,
           gles_context, bridge, glGetString(GL_RENDERER), eglGetError());
    fflush(0);
    if (!bridge)
        return 12;

    for (int frame = 0; frame < 240; ++frame) {
        glViewport(0, 0, 1080, 1920);
        glClearColor(0.02f, 0.70f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!eglSwapBuffers(display, egl_surface)) {
            printf("swap-failed frame=%d egl-error=0x%x gl-error=0x%x\n",
                   frame, eglGetError(), glGetError());
            fflush(0);
            return 13;
        }
        usleep(16000);
    }
    printf("complete=1 gl-error=0x%x\n", glGetError());
    fflush(0);
    return 0;
}
