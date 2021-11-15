#ifndef __RENDER_LIB_H__
#define __RENDER_LIB_H__
#include <stdint.h>
#include <stdlib.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define RENDER_MAX_PLANES 3

typedef struct _RenderBuffer RenderBuffer;
typedef struct _RenderWindowSize RenderWindowSize;
typedef struct _RenderFrameSize RenderFrameSize;
typedef struct _RenderCallback RenderCallback;
typedef struct _RenderRawBuffer RenderRawBuffer;
typedef struct _RenderDmaBuffer RenderDmaBuffer;

/*allocate render buffer flag */
enum _BufferFlag {
    BUFFER_FLAG_NONE       = 0,
    BUFFER_FLAG_ALLOCATE_DMA_BUFFER = 1 << 1,
    BUFFER_FLAG_ALLOCATE_RAW_BUFFER = 1 << 2,
    BUFFER_FLAG_EXTER_DMA_BUFFER    = 1 << 3,
};

struct _RenderBuffer {
    int id; //buffer id
    int flag; /*render buffer flag, see  enum _BufferFlag*/
    union {
        RenderDmaBuffer *dma;
        RenderRawBuffer *raw;
    };
    int64_t pts;
    void *priv;
};

struct _RenderRawBuffer {
    void *dataPtr;
    int size;
};

struct _RenderDmaBuffer {
    int width;
    int height;
    int planeCnt;
    uint32_t handle[RENDER_MAX_PLANES];
    uint32_t stride[RENDER_MAX_PLANES];
    uint32_t offset[RENDER_MAX_PLANES];
    uint32_t size[RENDER_MAX_PLANES];
    int fd[RENDER_MAX_PLANES];
};

/*render property*/
enum _RenderProp {
    PROP_WINDOW_SIZE,
    PROP_FRAME_SIZE,
    PROP_PCR_PID,
};

/*video display window size
 if will be used by PROP_UPDATE_WINDOW_SIZE prop */
struct _RenderWindowSize {
    int x;
    int y;
    int w;
    int h;
};

/*frame size info
 it will be used by PROP_UPDATE_FRAME_SIZE prop*/
struct _RenderFrameSize {
    int frameWidth;
    int frameHeight;
};

typedef enum _RenderMsgType {
    MSG_RELEASE_BUFFER = 100,

    MSG_CONNECTED_FAIL = 200,
    MSG_DISCONNECTED_FAIL,
} RenderMsgType;

enum _RenderKey {
    KEY_GET_MEDIASYNC_INSTANCE = 300,
    KEY_VIDEO_FORMAT,
};

typedef void (*renderMsgSendCallback)(void *userData , RenderMsgType type, void *msg);
typedef int (*renderGetValueCallback)(void *userData, int key, void *value);

/**
 * a callback function that rend device call
 * this func to send msg to user,please not do 
 * time-consuming action,please copy msg context,
 * render lib will free msg buffer when msg_callback called end
 * @param msg the message of sending 
 * @return 
 */
struct _RenderCallback {
    renderMsgSendCallback doMsgSend ;
    renderGetValueCallback doGetValue;
};

typedef enum {
    VIDEO_FORMAT_UNKNOWN,
    VIDEO_FORMAT_ENCODED,
    VIDEO_FORMAT_I420,
    VIDEO_FORMAT_YV12,
    VIDEO_FORMAT_YUY2,
    VIDEO_FORMAT_UYVY,
    VIDEO_FORMAT_AYUV,
    VIDEO_FORMAT_RGBx,
    VIDEO_FORMAT_BGRx,
    VIDEO_FORMAT_xRGB,
    VIDEO_FORMAT_xBGR,
    VIDEO_FORMAT_RGBA,
    VIDEO_FORMAT_BGRA,
    VIDEO_FORMAT_ARGB,
    VIDEO_FORMAT_ABGR,
    VIDEO_FORMAT_RGB,
    VIDEO_FORMAT_BGR,
    VIDEO_FORMAT_Y41B,
    VIDEO_FORMAT_Y42B,
    VIDEO_FORMAT_YVYU,
    VIDEO_FORMAT_Y444,
    VIDEO_FORMAT_v210,
    VIDEO_FORMAT_v216,
    VIDEO_FORMAT_NV12,
    VIDEO_FORMAT_NV21,
    VIDEO_FORMAT_GRAY8,
    VIDEO_FORMAT_GRAY16_BE,
    VIDEO_FORMAT_GRAY16_LE,
    VIDEO_FORMAT_v308,
    VIDEO_FORMAT_RGB16,
    VIDEO_FORMAT_BGR16,
    VIDEO_FORMAT_RGB15,
    VIDEO_FORMAT_BGR15,
    VIDEO_FORMAT_UYVP,
    VIDEO_FORMAT_A420,
    VIDEO_FORMAT_RGB8P,
    VIDEO_FORMAT_YUV9,
    VIDEO_FORMAT_YVU9,
    VIDEO_FORMAT_IYU1,
    VIDEO_FORMAT_ARGB64,
    VIDEO_FORMAT_AYUV64,
    VIDEO_FORMAT_r210,
    VIDEO_FORMAT_I420_10BE,
    VIDEO_FORMAT_I420_10LE,
    VIDEO_FORMAT_I422_10BE,
    VIDEO_FORMAT_I422_10LE,
    VIDEO_FORMAT_Y444_10BE,
    VIDEO_FORMAT_Y444_10LE,
    VIDEO_FORMAT_GBR,
    VIDEO_FORMAT_GBR_10BE,
    VIDEO_FORMAT_GBR_10LE,
    VIDEO_FORMAT_NV16,
    VIDEO_FORMAT_NV24,
    VIDEO_FORMAT_NV12_64Z32,
    VIDEO_FORMAT_A420_10BE,
    VIDEO_FORMAT_A420_10LE,
    VIDEO_FORMAT_A422_10BE,
    VIDEO_FORMAT_A422_10LE,
    VIDEO_FORMAT_A444_10BE,
    VIDEO_FORMAT_A444_10LE,
    VIDEO_FORMAT_NV61,
    VIDEO_FORMAT_P010_10BE,
    VIDEO_FORMAT_P010_10LE,
    VIDEO_FORMAT_IYU2,
    VIDEO_FORMAT_VYUY,
    VIDEO_FORMAT_GBRA,
    VIDEO_FORMAT_GBRA_10BE,
    VIDEO_FORMAT_GBRA_10LE,
    VIDEO_FORMAT_GBR_12BE,
    VIDEO_FORMAT_GBR_12LE,
    VIDEO_FORMAT_GBRA_12BE,
    VIDEO_FORMAT_GBRA_12LE,
    VIDEO_FORMAT_I420_12BE,
    VIDEO_FORMAT_I420_12LE,
    VIDEO_FORMAT_I422_12BE,
    VIDEO_FORMAT_I422_12LE,
    VIDEO_FORMAT_Y444_12BE,
    VIDEO_FORMAT_Y444_12LE,
    VIDEO_FORMAT_GRAY10_LE32,
    VIDEO_FORMAT_NV12_10LE32,
    VIDEO_FORMAT_NV16_10LE32,
    VIDEO_FORMAT_NV12_10LE40,
    VIDEO_FORMAT_Y410,
    VIDEO_FORMAT_VUYA,
    VIDEO_FORMAT_BGR10A2_LE,
    VIDEO_FORMAT_RGB10A2_LE,
    VIDEO_FORMAT_Y444_16BE,
    VIDEO_FORMAT_Y444_16LE,
    VIDEO_FORMAT_P016_BE,
    VIDEO_FORMAT_P016_LE,
    VIDEO_FORMAT_P012_BE,
    VIDEO_FORMAT_P012_LE,
    VIDEO_FORMAT_Y212_BE,
    VIDEO_FORMAT_Y212_LE,
    VIDEO_FORMAT_Y412_BE,
    VIDEO_FORMAT_Y412_LE,
    /**
     * GST_VIDEO_FORMAT_NV12_4L4:
     *
     * NV12 with 4x4 tiles in linear order.
     *
     * Since: 1.18
     */
    VIDEO_FORMAT_NV12_4L4,
    /**
     * GST_VIDEO_FORMAT_NV12_32L32:
     *
     * NV12 with 32x32 tiles in linear order.
     *
     * Since: 1.18
     */
    VIDEO_FORMAT_NV12_32L32,

} RenderVideoFormat;

/**
 * open a render lib,render lib will open a compositer with the special
 * render name
 * @param name the render device name
 *   the name value list is:
 *   wayland will open wayland render
 *   videotunnel will open tunnel render
 *   
 * @return a handle of render lib , return null if failed
 */
void *render_open(char *name);

/**
 * registe callback to render lib, render device will call
 * these callbacks to send msg to user or get some value from user
 * @param handle a handle of render device that was opened
 * @param callback  callback function struct that render will use
 * @return 0 sucess,-1 fail
 */
void render_set_callback(void *handle, RenderCallback *callback);

/**
 * set user data to render lib
 * @param handle a handle of render lib that was opened
 * @param userdata the set userdata
 * @return 0 sucess,-1 fail
 */
void render_set_user_data(void *handle, void *userdata);

/**
 * connect to render device
 * @param handle a handle of render device that was opened
 * @return 0 sucess,-1 fail
 */
int render_connect(void *handle);

/*************************************************/

/**
 * display a video frame, the buffer will be obtained by render lib
 * until render lib release it, so please allcating buffer from memory heap 
 * @param handle a handle of render device that was opened
 * @param buffer a video buffer will be displayed
 * @return 0 sucess,-1 fail
 */
int render_display_frame(void *handle, RenderBuffer *buffer);

/**
 * set property to render device,user must alloc a prop struct buffer of
 * the property 
 * @param handle a handle of render device that was opened
 * @param property a property of render device
 * @param prop property struct buffer of property
 * @return 0 sucess,-1 fail
 */
int render_set_props(void *handle, int property, void *prop);

/**
 * get property from render device
 * @param handle a handle of render device that was opened
 * @param property a property of render device
 * @param prop property struct buffer of property
 * @return 0 sucess,-1 fail
 */
int render_get_props(void *handle, int property, void *prop);

/**
 * flush render lib buffer
 * @param handle a handle of render device that was opened
 * @return 0 sucess,-1 fail
 */
int render_flush(void *handle);

/**
 * pause display video frame
 * @param handle a handle of render device that was opened
 * @return 0 sucess,-1 fail
 */
int render_pause(void *handle);

/**
 * resume display video frame
 * @param handle a handle of render device that was opened
 * @return 0 sucess,-1 fail
 */
int render_resume(void *handle);

/**
 * disconnect to render device
 * @param handle a handle of render device that was opened
 * @return 0 sucess,-1 fail
 */
int render_disconnect(void *handle);

/**
 * close render device
 * @param handle a handle of render device that was opened
 * @return 0 sucess,-1 fail
 */
int render_close(void *handle);


/**********************tools func for render devices***************************/
/**
 * only alloc a RenderBuffer wrapper from render lib,
 * @param handle a handle of render device that was opened
 * @param flag buffer flag value, defined allocate buffer action, the value see _BufferFlag defined
 * @param rawBufferSize allocated raw buffer size, if only allocate render buffer wrap, the bufferSize can 0
 * @return buffer handler or null if failed
 */
RenderBuffer *render_allocate_render_buffer_wrap(void *handle, int flag, int rawBufferSize);

/**
 * free render buffer that allocated from render lib
 * @param handle a handle of render device that was opened
 * @return 
 */
void render_free_render_buffer_wrap(void *handle, RenderBuffer *buffer);

/**
 * accquire dma buffer from render lib
 * @param handle a handle of render device that was opened
 * @param planecnt the dma buffer plane count
 * @param width video width
 * @param height video height
 * @return RenderDmaBuffer point or NULL if fail
 * 
*/
RenderDmaBuffer *render_accquire_dma_buffer(void *handle, int planecnt, int width, int height);

/**
 * release dma buffer that allocated from render lib
*/
void render_release_dma_buffer(void *handle, RenderDmaBuffer *buffer);


#ifdef  __cplusplus
}
#endif
#endif /*__RENDER_LIB_H__*/