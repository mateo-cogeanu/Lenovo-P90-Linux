#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <hybris/surface_flinger/surface_flinger_compatibility_layer.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    SfClient *client;
    SfSurface *surface;
    EGLDisplay display;
    EGLSurface egl_surface;
    SfSurfaceCreationParameters params = {
        0, 0, 1080, 1920,
        -1,
        INT_MAX,
        1.0f,
        1,
        "P90 native PowerVR presentation probe"
    };

    printf("stage=create-client\n");
    fflush(0);
    client = sf_client_create();
    printf("client=%p\n", client);
    fflush(0);
    if (!client)
        return 10;

    printf("stage=create-surface\n");
    fflush(0);
    surface = sf_surface_create(client, &params);
    printf("surface=%p\n", surface);
    fflush(0);
    if (!surface)
        return 11;

    printf("stage=make-current\n");
    fflush(0);
    sf_surface_make_current(surface);
    display = sf_client_get_egl_display(client);
    egl_surface = sf_surface_get_egl_surface(surface);
    printf("display=%p egl-surface=%p renderer=%s egl-error=0x%x\n",
           display, egl_surface, glGetString(GL_RENDERER), eglGetError());
    fflush(0);

    for (int frame = 0; frame < 240; ++frame) {
        glViewport(0, 0, 1080, 1920);
        glClearColor(0.02f, 0.12f, 0.85f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!eglSwapBuffers(display, egl_surface)) {
            printf("swap-failed frame=%d egl-error=0x%x gl-error=0x%x\n",
                   frame, eglGetError(), glGetError());
            fflush(0);
            return 12;
        }
        usleep(16000);
    }
    printf("complete=1 gl-error=0x%x\n", glGetError());
    fflush(0);
    return 0;
}
