#ifndef __RENDER_LIB_H__
#define __RENDER_LIB_H__
#include <stdint.h>
#include <stdlib.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define RENDER_MAX_PLANES 3

/*render buffer type,fd been used by dmabuf,
 raw been used by normal buffer */
#define RENDER_BUFFER_TYPE_FD_FROM_LIB   (1 << 1)
#define RENDER_BUFFER_TYPE_FD_FROM_USER  (1 << 2)
#define RENDER_BUFFER_TYPE_RAW           (1 << 3)

typedef struct _RenderBuffer RenderBuffer;
typedef enum _RenderMessageType RenderMessageType;
typedef struct _RenderWindowSize RenderWindowSize;

struct _RenderBuffer {
    int id; //buffer id
    int type; /*value be defined with start with RENDER_BUFFER_TYPE_* */
    union {
        int fd[RENDER_MAX_PLANES];
        int8_t *ptr; //used when type is raw buffer type  
    } data;
    int dataSize; //if type is FD, size is the num of fd array in data
    int bufferSize; //buffer size allocated
    int64_t pts;
    void *ext;
};

enum _RenderMessageType {
    RENDER_MSG_RELEASE_BUFFER = 0,
};

/*render property*/
enum _RenderProp {
    PROP_UPDATE_WINDOW_SIZE  = 0x100,
    PROP_UPDATE_FRAME_SIZE,
    PROP_UPDATE_PCR_PID,
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

/**
 * a callback function that rend device call
 * this func to send msg to user,please not do 
 * time-consuming action,please copy msg context,
 * render lib will free msg buffer when msg_callback called end
 * @param msg the message of sending 
 * @return 
 */
typedef void (*render_callback)(void *userData , RenderMessageType type, void *msg);

/**
 * open a render device
 * @param name the render device name
 *   the name value list is:
 *   wayland will open wayland render
 *   videotunnel will open tunnel render
 *   
 * @return a handle of render device , return null if failed
 */
void *render_open(char *name);

/**
 * connect to render device
 * @param handle a handle of render device that was opened
 * @param callback a callback function that render send msg to user
 * @return 0 sucess,-1 fail
 */
int render_set_callback(void *handle, render_callback *callback);

/**
 * set user data to render lib
 * @param handle a handle of render device that was opened
 * @param userdata the setted userdata
 * @return 0 sucess,-1 fail
 */
int render_set_user_data(void *handle, void *userdata);

/**
 * connect to render device
 * @param handle a handle of render device that was opened
 * @return 0 sucess,-1 fail
 */
int render_connect(void *handle);

/*************************************************/

/**
 * display a video frame
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
int render_set_params(void *handle, int property, void *prop);

/**
 * get property from render device
 * @param handle a handle of render device that was opened
 * @param property a property of render device
 * @param prop property struct buffer of property
 * @return 0 sucess,-1 fail
 */
int render_get_params(void *handle, int property, void *prop);

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
 * alloc a RenderBuffer from render lib to fill video frame buffer
 * render lib will alloc buffer or nor according to type description
 * @param handle a handle of render device that was opened
 * @param type buffer type value, defined starting with RENDER_BUFFER_TYPE_*
 * @param bufferSize alloc buffer size
 * @return buffer handler or null if failed
 */
RenderBuffer *render_allocate_render_buffer(void *handle, int type, int bufferSize);

/**
 * free render buffer that allocated from render lib
 * @param handle a handle of render device that was opened
 * @return 
 */
void render_free_render_buffer(void *handle, RenderBuffer *buffer);


#ifdef  __cplusplus
}
#endif
#endif /*__RENDER_LIB_H__*/