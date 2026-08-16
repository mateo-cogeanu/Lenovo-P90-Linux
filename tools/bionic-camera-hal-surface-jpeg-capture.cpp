/*
 * Lenovo P90 factory-camera capture without Android applications.
 *
 * The factory camera HAL requires a real preview window before it emits a
 * JPEG.  This client creates a hidden SurfaceFlinger surface, wraps its
 * ANativeWindow in the camera1 preview_stream_ops ABI, and writes the first
 * compressed callback to disk.  Zygote and system_server are not required.
 */

#include <camera/CameraParameters.h>
#include <gui/ISurfaceComposerClient.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/SurfaceControl.h>
#include <hardware/camera.h>
#include <hardware/hardware.h>
#include <system/camera.h>
#include <system/window.h>
#include <ui/PixelFormat.h>
#include <utils/String8.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace android;

typedef int (*hw_get_module_fn)(const char *, const hw_module_t **);

struct CaptureMemory {
    camera_memory_t memory;
    size_t item_size;
    unsigned count;
    bool mapped;
};

static volatile unsigned g_jpeg_frames;
static const char *g_output_path;

static void release_memory(camera_memory_t *memory)
{
    CaptureMemory *capture = reinterpret_cast<CaptureMemory *>(memory);
    if (capture->mapped)
        munmap(capture->memory.data, capture->memory.size);
    else
        free(capture->memory.data);
    free(capture);
}

static camera_memory_t *request_memory(int fd, size_t item_size,
                                       unsigned count, void *)
{
    CaptureMemory *capture = static_cast<CaptureMemory *>(
            calloc(1, sizeof(*capture)));
    size_t total = item_size * count;
    if (!capture)
        return 0;
    capture->memory.data = fd >= 0 ?
            mmap(0, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) :
            malloc(total);
    capture->mapped = fd >= 0;
    if (!capture->memory.data || capture->memory.data == MAP_FAILED) {
        free(capture);
        return 0;
    }
    capture->memory.size = total;
    capture->memory.handle = capture;
    capture->memory.release = release_memory;
    capture->item_size = item_size;
    capture->count = count;
    printf("memory fd=%d item=%u count=%u total=%u\n", fd,
           static_cast<unsigned>(item_size), count,
           static_cast<unsigned>(total));
    return &capture->memory;
}

static void notify_callback(int32_t message, int32_t ext1, int32_t ext2,
                            void *)
{
    printf("notify message=0x%x ext1=%d ext2=%d\n", message, ext1, ext2);
    fflush(stdout);
}

static int write_all(int fd, const void *data, size_t size)
{
    const unsigned char *cursor = static_cast<const unsigned char *>(data);
    while (size) {
        ssize_t written = write(fd, cursor, size);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        cursor += written;
        size -= static_cast<size_t>(written);
    }
    return 0;
}

static void data_callback(int32_t message, const camera_memory_t *memory,
                          unsigned index, camera_frame_metadata_t *, void *)
{
    const CaptureMemory *capture =
            reinterpret_cast<const CaptureMemory *>(memory);
    if (message != CAMERA_MSG_COMPRESSED_IMAGE || index >= capture->count)
        return;
    const char *data = static_cast<const char *>(memory->data) +
                       index * capture->item_size;
    int fd = open(g_output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
        fchmod(fd, 0644);
    int result = fd >= 0 ? write_all(fd, data, capture->item_size) : -1;
    if (fd >= 0)
        close(fd);
    printf("jpeg index=%u bytes=%u result=%d path=%s\n", index,
           static_cast<unsigned>(capture->item_size), result, g_output_path);
    fflush(stdout);
    if (result == 0)
        ++g_jpeg_frames;
}

static void timestamp_callback(int64_t, int32_t, const camera_memory_t *,
                               unsigned, void *) {}

struct PreviewWindow {
    preview_stream_ops ops;
    ANativeWindow *window;
};

static ANativeWindow *native_window(preview_stream_ops *ops)
{
    return reinterpret_cast<PreviewWindow *>(ops)->window;
}

static int dequeue_buffer(preview_stream_ops *ops, buffer_handle_t **buffer,
                          int *stride)
{
    ANativeWindowBuffer *native_buffer = 0;
    int result = native_window_dequeue_buffer_and_wait(native_window(ops),
                                                        &native_buffer);
    if (!result) {
        *buffer = &native_buffer->handle;
        *stride = native_buffer->stride;
    }
    return result;
}

static ANativeWindowBuffer *buffer_from_handle_pointer(buffer_handle_t *handle)
{
    return reinterpret_cast<ANativeWindowBuffer *>(
            reinterpret_cast<char *>(handle) -
            offsetof(ANativeWindowBuffer, handle));
}

static int enqueue_buffer(preview_stream_ops *ops, buffer_handle_t *buffer)
{
    ANativeWindow *window = native_window(ops);
    return window->queueBuffer(window, buffer_from_handle_pointer(buffer), -1);
}

static int cancel_buffer(preview_stream_ops *ops, buffer_handle_t *buffer)
{
    ANativeWindow *window = native_window(ops);
    return window->cancelBuffer(window, buffer_from_handle_pointer(buffer), -1);
}

static int set_buffer_count(preview_stream_ops *ops, int count)
{
    return native_window_set_buffer_count(native_window(ops), count);
}

static int set_geometry(preview_stream_ops *ops, int width, int height,
                        int format)
{
    return native_window_set_buffers_geometry(native_window(ops), width,
                                               height, format);
}

static int set_crop(preview_stream_ops *ops, int left, int top, int right,
                    int bottom)
{
    android_native_rect_t crop = { left, top, right, bottom };
    return native_window_set_crop(native_window(ops), &crop);
}

static int set_usage(preview_stream_ops *ops, int usage)
{
    return native_window_set_usage(native_window(ops), usage);
}

static int set_swap_interval(preview_stream_ops *ops, int interval)
{
    ANativeWindow *window = native_window(ops);
    return window->setSwapInterval(window, interval);
}

static int get_min_undequeued(const preview_stream_ops *ops, int *count)
{
    ANativeWindow *window = reinterpret_cast<const PreviewWindow *>(ops)->window;
    return window->query(window, NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS, count);
}

static int lock_buffer(preview_stream_ops *, buffer_handle_t *) { return 0; }

static int set_timestamp(preview_stream_ops *ops, int64_t timestamp)
{
    return native_window_set_buffers_timestamp(native_window(ops), timestamp);
}

static void initialize_preview_window(PreviewWindow *preview,
                                      ANativeWindow *window)
{
    memset(preview, 0, sizeof(*preview));
    preview->window = window;
    preview->ops.dequeue_buffer = dequeue_buffer;
    preview->ops.enqueue_buffer = enqueue_buffer;
    preview->ops.cancel_buffer = cancel_buffer;
    preview->ops.set_buffer_count = set_buffer_count;
    preview->ops.set_buffers_geometry = set_geometry;
    preview->ops.set_crop = set_crop;
    preview->ops.set_usage = set_usage;
    preview->ops.set_swap_interval = set_swap_interval;
    preview->ops.get_min_undequeued_buffer_count = get_min_undequeued;
    preview->ops.lock_buffer = lock_buffer;
    preview->ops.set_timestamp = set_timestamp;
}

int main(int argc, char **argv)
{
    g_output_path = argc > 1 ? argv[1] :
            "/data/local/tmp/p90-camera-hal-surface.jpg";
    setvbuf(stdout, 0, _IONBF, 0);
    alarm(45);

    void *libhardware = dlopen("/system/lib/libhardware.so",
                               RTLD_NOW | RTLD_LOCAL);
    hw_get_module_fn get_module = libhardware ?
            reinterpret_cast<hw_get_module_fn>(
                    dlsym(libhardware, "hw_get_module")) : 0;
    const hw_module_t *module = 0;
    int result = get_module ? get_module(CAMERA_HARDWARE_MODULE_ID, &module) :
                              -ENODEV;
    printf("camera-module result=%d module=%p\n", result, module);
    if (result || !module)
        return 10;

    sp<SurfaceComposerClient> composer = new SurfaceComposerClient();
    printf("surface-client result=%d\n", composer->initCheck());
    if (composer->initCheck() != NO_ERROR)
        return 11;
    sp<SurfaceControl> control = composer->createSurface(
            String8("P90 native camera preview"), 640, 480,
            PIXEL_FORMAT_RGBX_8888, ISurfaceComposerClient::eHidden);
    if (control == 0 || !control->isValid()) {
        printf("surface-control invalid\n");
        return 12;
    }
    sp<Surface> surface = control->getSurface();
    result = native_window_api_connect(surface.get(), NATIVE_WINDOW_API_CAMERA);
    printf("surface-connect result=%d\n", result);
    if (result != NO_ERROR)
        return 13;

    hw_device_t *common = 0;
    result = module->methods->open(module, "0", &common);
    printf("camera-open result=%d device=%p\n", result, common);
    if (result || !common)
        return 14;
    camera_device_t *camera = reinterpret_cast<camera_device_t *>(common);

    PreviewWindow preview;
    initialize_preview_window(&preview, surface.get());
    result = camera->ops->set_preview_window(camera, &preview.ops);
    printf("preview-window result=%d\n", result);
    if (result)
        return 15;

    camera->ops->set_callbacks(camera, notify_callback, data_callback,
                               timestamp_callback, request_memory, 0);
    camera->ops->enable_msg_type(camera, CAMERA_MSG_SHUTTER |
            CAMERA_MSG_COMPRESSED_IMAGE | CAMERA_MSG_ERROR);

    char *raw = camera->ops->get_parameters(camera);
    CameraParameters parameters(String8(raw ? raw : ""));
    if (raw)
        camera->ops->put_parameters(camera, raw);
    parameters.setPreviewSize(640, 480);
    parameters.setPictureSize(640, 480);
    parameters.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    result = camera->ops->set_parameters(camera,
                                         parameters.flatten().string());
    printf("parameters result=%d\n", result);
    if (result)
        return 16;

    result = camera->ops->start_preview(camera);
    printf("preview-start result=%d enabled=%d\n", result,
           camera->ops->preview_enabled(camera));
    if (result)
        return 17;
    sleep(3);
    result = camera->ops->take_picture(camera);
    printf("take-picture result=%d\n", result);
    if (!result) {
        for (int tenth = 0; tenth < 200 && !g_jpeg_frames; ++tenth)
            usleep(100000);
    }

    camera->ops->stop_preview(camera);
    camera->ops->set_preview_window(camera, 0);
    common->close(common);
    native_window_api_disconnect(surface.get(), NATIVE_WINDOW_API_CAMERA);
    control.clear();
    composer->dispose();
    alarm(0);
    printf("complete jpeg-frames=%u\n", g_jpeg_frames);
    return g_jpeg_frames ? 0 : 18;
}
