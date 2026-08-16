/*
 * Lenovo P90 isolated CameraService capture client.
 *
 * This is intentionally an Android/Bionic client: it talks to the factory
 * CameraService over Binder and supplies a private BufferQueue for preview.
 * It neither starts Android applications nor requires Zygote/system_server.
 * The JPEG callback is copied verbatim to the requested output path.
 */

#include <binder/IMemory.h>
#include <binder/ProcessState.h>
#include <camera/Camera.h>
#include <camera/CameraParameters.h>
#include <gui/BufferQueue.h>
#include <gui/GraphicBufferAlloc.h>
#include <gui/IConsumerListener.h>
#include <system/camera.h>
#include <ui/Fence.h>
#include <utils/String16.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace android;

static volatile int g_jpeg_complete;
static volatile int g_camera_error;
static const char *g_output_path;

static int write_all(int fd, const void *buffer, size_t size)
{
    const unsigned char *cursor = static_cast<const unsigned char *>(buffer);

    while (size != 0) {
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

class PreviewDrain : public BnConsumerListener {
public:
    explicit PreviewDrain(const sp<BufferQueue>& queue) : mQueue(queue) {}

    virtual void onFrameAvailable()
    {
        IGraphicBufferConsumer::BufferItem item;

        while (mQueue->acquireBuffer(&item, 0) == NO_ERROR) {
            mQueue->releaseBuffer(item.mBuf, item.mFrameNumber,
                                  EGL_NO_DISPLAY, EGL_NO_SYNC_KHR,
                                  Fence::NO_FENCE);
        }
    }

    virtual void onBuffersReleased()
    {
        uint32_t released = 0;
        mQueue->getReleasedBuffers(&released);
    }

private:
    sp<BufferQueue> mQueue;
};

class CaptureListener : public CameraListener {
public:
    virtual void notify(int32_t type, int32_t ext1, int32_t ext2)
    {
        printf("notify type=0x%x ext1=%d ext2=%d\n", type, ext1, ext2);
        fflush(stdout);
        if (type == CAMERA_MSG_ERROR)
            g_camera_error = ext1 ? ext1 : 1;
    }

    virtual void postData(int32_t type, const sp<IMemory>& data,
                          camera_frame_metadata_t *)
    {
        if (type != CAMERA_MSG_COMPRESSED_IMAGE)
            return;

        if (data == 0 || data->pointer() == 0 || data->size() == 0) {
            fprintf(stderr, "jpeg callback contained no data\n");
            g_camera_error = EIO;
            return;
        }

        int fd = open(g_output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            fprintf(stderr, "open %s failed: %s\n", g_output_path,
                    strerror(errno));
            g_camera_error = errno;
            return;
        }
        if (write_all(fd, data->pointer(), data->size()) != 0) {
            fprintf(stderr, "write %s failed: %s\n", g_output_path,
                    strerror(errno));
            g_camera_error = errno;
        } else {
            printf("jpeg-bytes=%u path=%s\n",
                   static_cast<unsigned>(data->size()), g_output_path);
            g_jpeg_complete = 1;
        }
        close(fd);
        fflush(stdout);
    }

    virtual void postDataTimestamp(nsecs_t, int32_t, const sp<IMemory>&) {}
};

int main(int argc, char **argv)
{
    const int camera_id = argc > 2 ? atoi(argv[2]) : 0;
    g_output_path = argc > 1 ? argv[1] : "/data/local/tmp/p90-camera.jpg";

    ProcessState::self()->startThreadPool();

    int count = Camera::getNumberOfCameras();
    printf("camera-count=%d selected=%d\n", count, camera_id);
    fflush(stdout);
    if (camera_id < 0 || camera_id >= count)
        return 10;

    sp<Camera> camera = Camera::connect(camera_id,
            String16("p90-native-camera"), Camera::USE_CALLING_UID);
    if (camera == 0) {
        fprintf(stderr, "CameraService connect failed\n");
        return 11;
    }

    sp<CaptureListener> capture_listener = new CaptureListener();
    camera->setListener(capture_listener);

    sp<IGraphicBufferAlloc> allocator = new GraphicBufferAlloc();
    sp<BufferQueue> queue = new BufferQueue(allocator);
    sp<PreviewDrain> preview_listener = new PreviewDrain(queue);
    status_t result = queue->setDefaultBufferSize(640, 480);
    if (result == NO_ERROR)
        result = queue->setDefaultMaxBufferCount(4);
    if (result == NO_ERROR)
        result = queue->consumerConnect(preview_listener, false);
    printf("preview-queue result=%d\n", result);
    if (result != NO_ERROR)
        return 12;

    sp<IGraphicBufferProducer> producer = queue;
    result = camera->setPreviewTarget(producer);
    printf("preview-target result=%d\n", result);
    if (result != NO_ERROR)
        return 13;

    CameraParameters parameters(camera->getParameters());
    parameters.setPreviewSize(640, 480);
    parameters.setPictureSize(640, 480);
    parameters.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    result = camera->setParameters(parameters.flatten());
    printf("parameters result=%d\n", result);
    if (result != NO_ERROR)
        return 14;

    result = camera->startPreview();
    printf("preview-start result=%d\n", result);
    if (result != NO_ERROR)
        return 15;

    sleep(2);
    result = camera->takePicture(CAMERA_MSG_SHUTTER |
                                 CAMERA_MSG_COMPRESSED_IMAGE);
    printf("take-picture result=%d\n", result);
    fflush(stdout);
    if (result != NO_ERROR)
        return 16;

    for (int tenth = 0; tenth < 200 && !g_jpeg_complete && !g_camera_error;
         ++tenth)
        usleep(100000);

    camera->stopPreview();
    camera->disconnect();
    queue->consumerDisconnect();

    if (g_jpeg_complete)
        return 0;
    fprintf(stderr, "capture failed or timed out error=%d\n", g_camera_error);
    return g_camera_error ? 17 : 18;
}
