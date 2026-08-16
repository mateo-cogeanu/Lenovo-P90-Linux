#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <pthread.h>
#include <stdio.h>

int main(void)
{
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLConfig config = 0;
    EGLint count = 0;
    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    const EGLint surface_attributes[] = {
        EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE
    };
    const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
    };
    EGLSurface surface;
    EGLContext context;

    printf("egl-display=%p\n", display);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, 0, 0))
        return 10;
    if (!eglBindAPI(EGL_OPENGL_ES_API))
        return 11;
    if (!eglChooseConfig(display, config_attributes, &config, 1, &count) || count < 1)
        return 12;
    surface = eglCreatePbufferSurface(display, config, surface_attributes);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    printf("egl-surface=%p egl-context=%p error=0x%x\n", surface, context, eglGetError());
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT)
        return 13;
    printf("egl-make-current=%u error-before=0x%x\n",
           eglMakeCurrent(display, surface, surface, context), eglGetError());
    printf("egl-current-context=%p display=%p surface=%p error=0x%x\n",
           eglGetCurrentContext(), eglGetCurrentDisplay(),
           eglGetCurrentSurface(EGL_DRAW), eglGetError());
    printf("bionic-context-words");
    for (int word = 0; word < 32; ++word)
        printf(" w%d=0x%x", word, ((unsigned int *)context)[word]);
    printf("\n");
    {
        void **tls;
        __asm__("movl %%gs:0, %0" : "=r"(tls));
        printf("bionic-tls base=%p", tls);
        for (int slot = 0; slot < 16; ++slot)
            printf(" slot%d=%p", slot, tls[slot]);
        printf("\n");
    }
    for (unsigned int key = 0; key < 128; ++key) {
        unsigned int *specific = (unsigned int *)pthread_getspecific(key);
        if (!specific)
            continue;
        printf("bionic-specific key=%u ptr=%p", key, specific);
        if (specific) {
            for (int word = 0; word < 20; ++word)
                printf(" w%d=0x%x", word, specific[word]);
            if (specific[0] == 0x21 && specific[2])
                printf(" emutls-w2-value=%p", *(void **)specific[2]);
        }
        printf("\n");
    }
    printf("gl-version=%s\n", glGetString(GL_VERSION));
    printf("gl-vendor=%s\n", glGetString(GL_VENDOR));
    printf("gl-renderer=%s\n", glGetString(GL_RENDERER));
    printf("gl-error=0x%x\n", glGetError());
    return 0;
}
