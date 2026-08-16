#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <hardware/hwcomposer.h>
#include <stdio.h>
#include <stdint.h>

static void hwc_invalidate(const struct hwc_procs *procs) { (void)procs; }
static void hwc_vsync(const struct hwc_procs *procs, int display, int64_t ts)
{
    (void)procs;
    (void)display;
    (void)ts;
}
static void hwc_hotplug(const struct hwc_procs *procs, int display, int connected)
{
    (void)procs;
    (void)display;
    (void)connected;
}

static struct hwc_procs hwc_procs = {
    .invalidate = hwc_invalidate,
    .vsync = hwc_vsync,
    .hotplug = hwc_hotplug,
};

int main(void)
{
    EGLDisplay egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLConfig egl_config = 0;
    EGLint egl_count = 0;
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
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLContext egl_context = EGL_NO_CONTEXT;
    int egl_status = 0;
    if (egl_display == EGL_NO_DISPLAY || !eglInitialize(egl_display, 0, 0))
        egl_status = 1;
    else if (!eglBindAPI(EGL_OPENGL_ES_API) ||
             !eglChooseConfig(egl_display, config_attributes, &egl_config, 1,
                              &egl_count) || egl_count < 1)
        egl_status = 2;
    else {
        egl_surface = eglCreatePbufferSurface(
            egl_display, egl_config, surface_attributes);
        egl_context = eglCreateContext(
            egl_display, egl_config, EGL_NO_CONTEXT, context_attributes);
        if (egl_surface == EGL_NO_SURFACE || egl_context == EGL_NO_CONTEXT ||
            !eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context))
            egl_status = 3;
    }
    printf("stage=init-egl status=%d display=%p surface=%p context=%p "
           "renderer=%s error=0x%x\n", egl_status, egl_display, egl_surface,
           egl_context, egl_status ? "(unavailable)" :
           (const char *)glGetString(GL_RENDERER), eglGetError());
    fflush(0);

    void *dso = dlopen("libhardware.so", RTLD_NOW);
    printf("stage=libhardware dso=%p error=%s\n", dso, dlerror());
    fflush(0);
    int (*get_module)(const char *, const hw_module_t **) = dso ?
        (int (*)(const char *, const hw_module_t **))dlsym(dso, "hw_get_module") : 0;

    /*
     * Follow Android's hardware/libhardware/tests/hwc/cnativewindow.c order.
     * Moorefield's proprietary allocator may require the display pipeline to
     * be initialized before GRALLOC_USAGE_HW_FB allocations are accepted.
     */
    const hw_module_t *gralloc_module = 0;
    int gralloc_module_status = get_module ? get_module(
        GRALLOC_HARDWARE_MODULE_ID, &gralloc_module) : -1;
    printf("stage=get-gralloc-module status=%d module=%p\n",
           gralloc_module_status, gralloc_module);
    fflush(0);

    framebuffer_device_t *framebuffer = 0;
    int framebuffer_status = gralloc_module ? framebuffer_open(
        gralloc_module, &framebuffer) : -1;
    printf("stage=open-framebuffer status=%d device=%p\n",
           framebuffer_status, framebuffer);
    fflush(0);

    const hw_module_t *hwc_module = 0;
    int hwc_module_status = get_module ? get_module(
        HWC_HARDWARE_MODULE_ID, &hwc_module) : -1;
    printf("stage=get-hwc-module status=%d module=%p\n",
           hwc_module_status, hwc_module);
    fflush(0);

    hwc_composer_device_1_t *hwc = 0;
    int hwc_open_status = hwc_module ? hwc_open_1(hwc_module, &hwc) : -1;
    printf("stage=open-hwc status=%d device=%p version=0x%x\n",
           hwc_open_status, hwc, hwc ? hwc->common.version : 0);
    fflush(0);

    uint32_t configs[32];
    size_t num_configs = 32;
    uint32_t attributes[] = {
        HWC_DISPLAY_WIDTH,
        HWC_DISPLAY_HEIGHT,
        HWC_DISPLAY_VSYNC_PERIOD,
        HWC_DISPLAY_DPI_X,
        HWC_DISPLAY_DPI_Y,
        HWC_DISPLAY_NO_ATTRIBUTE,
    };
    int32_t values[6] = { 0 };
    int register_status = -2;
    int config_status = -1;
    int attributes_status = -1;
    int unblank_status = -1;
    if (hwc) {
        /* libhybris does not register callbacks on this standalone path. */
        config_status = hwc->getDisplayConfigs(
            hwc, HWC_DISPLAY_PRIMARY, configs, &num_configs);
        if (config_status == 0 && num_configs > 0)
            attributes_status = hwc->getDisplayAttributes(
                hwc, HWC_DISPLAY_PRIMARY, configs[0], attributes, values);
        unblank_status = hwc->blank(hwc, HWC_DISPLAY_PRIMARY, 0);
    }
    printf("stage=init-hwc register=%d configs=%d count=%u attributes=%d "
           "width=%d height=%d unblank=%d\n",
           register_status, config_status, (unsigned)num_configs,
           attributes_status, values[0], values[1], unblank_status);
    fflush(0);

    const hw_module_t *module_const = 0;
    int module_status = get_module ? get_module(GRALLOC_HARDWARE_MODULE_ID,
                                                 &module_const) : -1;
    printf("stage=get-module status=%d module=%p\n", module_status, module_const);
    fflush(0);
    hw_module_t *module = (hw_module_t *)module_const;
    hw_device_t *device = 0;
    buffer_handle_t handle = 0;
    int stride = 0;
    int open_status = module ? module->methods->open(
        module, GRALLOC_HARDWARE_GPU0, &device) : -1;
    printf("stage=open status=%d device=%p close=%p alloc=%p free=%p dump=%p\n",
           open_status, device,
           device ? device->close : 0,
           device ? ((alloc_device_t *)device)->alloc : 0,
           device ? ((alloc_device_t *)device)->free : 0,
           device ? ((alloc_device_t *)device)->dump : 0);
    fflush(0);
    printf("stage=before-allocate\n");
    fflush(0);
    int allocate_status = device ? ((alloc_device_t *)device)->alloc(
        (alloc_device_t *)device, 1080, 1920, HAL_PIXEL_FORMAT_RGBX_8888,
        GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_FB,
        &handle, &stride) : -1;

    printf("dso=%p module-status=%d module=%p open=%d device=%p allocate=%d handle=%p stride=%d\n",
           dso, module_status, module, open_status, device, allocate_status,
           handle, stride);
    if (handle && device)
        printf("free=%d\n", ((alloc_device_t *)device)->free(
            (alloc_device_t *)device, handle));
    if (device)
        printf("close=%d\n", device->close(device));
    if (hwc)
        printf("close-hwc=%d\n", hwc_close_1(hwc));
    if (framebuffer)
        printf("close-framebuffer=%d\n", framebuffer_close(framebuffer));
    return allocate_status == 0 ? 0 : 1;
}
