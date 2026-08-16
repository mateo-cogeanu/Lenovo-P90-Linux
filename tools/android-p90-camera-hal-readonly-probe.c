#include <dlfcn.h>
#include <errno.h>
#include <hardware/camera.h>
#include <hardware/camera_common.h>
#include <hardware/hardware.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef int (*hw_get_module_fn)(const char *, const struct hw_module_t **);

struct probe_memory {
    camera_memory_t memory;
    size_t buffer_size;
    unsigned int buffer_count;
    int mapped;
};

static volatile unsigned int preview_frames;
static volatile unsigned int jpeg_frames;

static void release_probe_memory(camera_memory_t *memory)
{
    struct probe_memory *probe = (struct probe_memory *)memory;

    if (probe->mapped)
        munmap(probe->memory.data, probe->memory.size);
    else
        free(probe->memory.data);
    free(probe);
}

static camera_memory_t *request_probe_memory(int fd, size_t buffer_size,
                                              unsigned int buffer_count,
                                              void *user)
{
    struct probe_memory *probe;
    size_t total = buffer_size * buffer_count;

    (void)user;
    probe = calloc(1, sizeof(*probe));
    if (probe == NULL)
        return NULL;
    probe->memory.data = fd >= 0 ? mmap(NULL, total, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd, 0) : malloc(total);
    probe->mapped = fd >= 0;
    if (probe->memory.data == NULL || probe->memory.data == MAP_FAILED) {
        free(probe);
        return NULL;
    }
    probe->memory.size = total;
    probe->memory.handle = probe;
    probe->memory.release = release_probe_memory;
    probe->buffer_size = buffer_size;
    probe->buffer_count = buffer_count;
    printf("camera-memory fd=%d buffer-size=%lu count=%u total=%lu\n",
           fd, (unsigned long)buffer_size, buffer_count, (unsigned long)total);
    return &probe->memory;
}

static void camera_notify(int32_t message, int32_t ext1, int32_t ext2,
                          void *user)
{
    (void)user;
    printf("camera-notify message=0x%x ext1=%d ext2=%d\n",
           message, ext1, ext2);
}

static void camera_data(int32_t message, const camera_memory_t *memory,
                        unsigned int index, camera_frame_metadata_t *metadata,
                        void *user)
{
    const struct probe_memory *probe = (const struct probe_memory *)memory;

    (void)metadata;
    (void)user;
    if (index >= probe->buffer_count)
        return;
    if (message == CAMERA_MSG_PREVIEW_FRAME)
        ++preview_frames;
    else if (message == CAMERA_MSG_COMPRESSED_IMAGE)
        ++jpeg_frames;
    else
        return;
    if ((message == CAMERA_MSG_PREVIEW_FRAME && preview_frames == 1) ||
        (message == CAMERA_MSG_COMPRESSED_IMAGE && jpeg_frames == 1)) {
        const char *path = message == CAMERA_MSG_COMPRESSED_IMAGE ?
                           "/data/local/tmp/p90-camera-capture.jpg" :
                           "/data/local/tmp/p90-camera-preview-frame.bin";
        int fd = open(path,
                      O_WRONLY | O_CREAT | O_TRUNC, 0644);
        const char *frame = (const char *)memory->data +
                            index * probe->buffer_size;
        if (fd >= 0) {
            ssize_t written = write(fd, frame, probe->buffer_size);
            close(fd);
            printf("camera-frame message=0x%x index=%u size=%lu written=%ld path=%s\n",
                   message, index, (unsigned long)probe->buffer_size,
                   (long)written, path);
        }
    }
}

static void camera_data_timestamp(int64_t timestamp, int32_t message,
                                  const camera_memory_t *memory,
                                  unsigned int index, void *user)
{
    (void)timestamp;
    (void)message;
    (void)memory;
    (void)index;
    (void)user;
}

int main(int argc, char **argv)
{
    void *libhardware;
    hw_get_module_fn get_module;
    const hw_module_t *module = NULL;
    camera_module_t *camera_module;
    int count;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    libhardware = dlopen("/system/lib/libhardware.so", RTLD_NOW | RTLD_LOCAL);
    if (libhardware == NULL) {
        printf("libhardware-load=failed error=%s\n", dlerror());
        return 2;
    }
    get_module = (hw_get_module_fn)dlsym(libhardware, "hw_get_module");
    if (get_module == NULL) {
        printf("hw-get-module-symbol=failed error=%s\n", dlerror());
        return 3;
    }
    i = get_module(CAMERA_HARDWARE_MODULE_ID, &module);
    printf("hw-get-module=%d errno=%d (%s) module=%p\n",
           i, errno, strerror(errno), module);
    if (i != 0 || module == NULL)
        return 4;

    printf("module-id=%s name=%s author=%s module-api=0x%04x hal-api=0x%04x\n",
           module->id ? module->id : "(null)",
           module->name ? module->name : "(null)",
           module->author ? module->author : "(null)",
           module->module_api_version,
           module->hal_api_version);

    camera_module = (camera_module_t *)module;
    count = camera_module->get_number_of_cameras();
    printf("camera-count=%d\n", count);
    for (i = 0; i < count; ++i) {
        struct camera_info info;
        int result;

        memset(&info, 0, sizeof(info));
        result = camera_module->get_camera_info(i, &info);
        printf("camera=%d result=%d facing=%d orientation=%d device-version=0x%04x\n",
               i, result, info.facing, info.orientation,
               info.device_version);
    }
    if (argc == 2 && (strcmp(argv[1], "--open") == 0 ||
                      strcmp(argv[1], "--capture-preview") == 0 ||
                      strcmp(argv[1], "--capture-jpeg") == 0)) {
        hw_device_t *device = NULL;
        camera_device_t *camera_device;
        int result;

        alarm(15);
        result = module->methods->open(module, "0", &device);
        alarm(0);
        printf("camera-open=0 result=%d errno=%d (%s) device=%p\n",
               result, errno, strerror(errno), device);
        if (result == 0 && device != NULL) {
            printf("camera-device-version=0x%04x close=%p\n",
                   device->version, device->close);
            camera_device = (camera_device_t *)device;
            if (strcmp(argv[1], "--capture-preview") == 0 ||
                strcmp(argv[1], "--capture-jpeg") == 0) {
                char *parameters;
                int seconds;

                parameters = camera_device->ops->get_parameters(camera_device);
                printf("camera-parameters=%s\n", parameters ? parameters : "(null)");
                if (parameters != NULL)
                    camera_device->ops->put_parameters(camera_device, parameters);
                camera_device->ops->set_callbacks(
                    camera_device, camera_notify, camera_data,
                    camera_data_timestamp, request_probe_memory, NULL);
                camera_device->ops->enable_msg_type(
                    camera_device, CAMERA_MSG_PREVIEW_FRAME |
                                   CAMERA_MSG_COMPRESSED_IMAGE |
                                   CAMERA_MSG_SHUTTER | CAMERA_MSG_ERROR);
                result = camera_device->ops->start_preview(camera_device);
                printf("preview-start result=%d\n", result);
                if (strcmp(argv[1], "--capture-jpeg") == 0) {
                    sleep(2);
                    result = camera_device->ops->take_picture(camera_device);
                    printf("take-picture result=%d\n", result);
                    for (seconds = 0; seconds < 15 && jpeg_frames == 0; ++seconds)
                        sleep(1);
                } else {
                    for (seconds = 0; seconds < 10 && preview_frames == 0; ++seconds)
                        sleep(1);
                }
                printf("preview-frames=%u preview-enabled=%d\n",
                       preview_frames,
                       camera_device->ops->preview_enabled(camera_device));
                printf("jpeg-frames=%u\n", jpeg_frames);
                camera_device->ops->stop_preview(camera_device);
                camera_device->ops->disable_msg_type(
                    camera_device, CAMERA_MSG_PREVIEW_FRAME |
                                   CAMERA_MSG_COMPRESSED_IMAGE |
                                   CAMERA_MSG_SHUTTER | CAMERA_MSG_ERROR);
            }
            result = device->close(device);
            printf("camera-close=0 result=%d errno=%d (%s)\n",
                   result, errno, strerror(errno));
        }
    }
    dlclose(libhardware);
    return 0;
}
