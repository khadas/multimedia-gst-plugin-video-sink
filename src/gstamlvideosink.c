#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdbool.h>
#include <gst/gstdrmbufferpool.h>
#include <gst/allocators/gstdmabuf.h>
#include "gstamlvideosink.h"
#include "render_lib.h"
// #ifdef USE_AMLOGIC_MESON
// #ifdef USE_AMLOGIC_MESON_MSYNC
// #define INVALID_SESSION_ID (16)
#include "gstamlclock.h"
#include "gstamlhalasink_new.h"
// #endif
// #endif

/* signals */
enum
{
    SIGNAL_0,
    LAST_SIGNAL
};

/* Properties */
enum
{
    PROP_0,
    PROP_FULLSCREEN,
    PROP_SETMUTE,

};

#define AML_VIDEO_FORMATS                                          \
    "{ BGRx, BGRA, RGBx, xBGR, xRGB, RGBA, ABGR, ARGB, RGB, BGR, " \
    "RGB16, BGR16, YUY2, YVYU, UYVY, AYUV, NV12, NV21, NV16, "     \
    "YUV9, YVU9, Y41B, I420, YV12, Y42B, v308 }"
#define GST_CAPS_FEATURE_MEMORY_DMABUF "memory:DMABuf"
#define RENDER_DEVICE_NAME "wayland"
#define USE_DMABUF TRUE

struct _GstAmlVideoSinkPrivate
{
    gchar *render_device_name;
    void *render_device_handle;
    GstVideoInfo video_info;
    gboolean video_info_changed;
    gboolean use_dmabuf;
    gboolean is_flushing;
    gint mediasync_instanceid;
    GstSegment segment;
    /* property params */
    gboolean fullscreen;
    gboolean mute;
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        GST_VIDEO_CAPS_MAKE(AML_VIDEO_FORMATS) ";" GST_VIDEO_CAPS_MAKE_WITH_FEATURES(
            GST_CAPS_FEATURE_MEMORY_DMABUF, AML_VIDEO_FORMATS)));

GST_DEBUG_CATEGORY(gst_aml_video_sink_debug);
#define GST_CAT_DEFAULT gst_aml_video_sink_debug
#define gst_aml_video_sink_parent_class parent_class
// #define GST_AML_VIDEO_SINK_GET_PRIVATE(obj)  (G_TYPE_INSTANCE_GET_PRIVATE((obj), GST_TYPE_AML_VIDEO_SINK, GstAmlVideoSinkPrivate))
G_DEFINE_TYPE_WITH_CODE(GstAmlVideoSink, gst_aml_video_sink,
                        GST_TYPE_VIDEO_SINK, G_ADD_PRIVATE(GstAmlVideoSink));

/* public interface define */
static void gst_aml_video_sink_get_property(GObject *object, guint prop_id,
                                            GValue *value, GParamSpec *pspec);
static void gst_aml_video_sink_set_property(GObject *object, guint prop_id,
                                            const GValue *value,
                                            GParamSpec *pspec);
static void gst_aml_video_sink_finalize(GObject *object);
static GstStateChangeReturn
gst_aml_video_sink_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_aml_video_sink_propose_allocation(GstBaseSink *bsink, GstQuery *query);
static GstCaps *gst_aml_video_sink_get_caps(GstBaseSink *bsink,
                                            GstCaps *filter);
static gboolean gst_aml_video_sink_set_caps(GstBaseSink *bsink, GstCaps *caps);
static gboolean gst_aml_video_sink_show_frame(GstVideoSink *bsink, GstBuffer *buffer);
static gboolean gst_aml_video_sink_pad_event(GstPad *pad, GstObject *parent, GstEvent *event);

/* private interface define */
static void gst_aml_video_sink_reset_private(GstAmlVideoSink *sink);
static void gst_render_msg_callback(void *userData, RenderMsgType type, void *msg);
static int gst_render_val_callback(void *userData, int key, void *value);
static gboolean gst_aml_video_sink_tunnel_buf(GstAmlVideoSink *vsink, GstBuffer *gst_buf, RenderBuffer *tunnel_lib_buf_wrap);
static gboolean gst_get_mediasync_instanceid(GstAmlVideoSink *vsink);
static GstElement *gst_aml_video_sink_find_audio_sink(GstAmlVideoSink *sink);
static gboolean gst_render_set_params(GstVideoSink *vsink);

/* public interface definition */
static void gst_aml_video_sink_class_init(GstAmlVideoSinkClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;
    GstBaseSinkClass *gstbasesink_class;
    GstVideoSinkClass *gstvideosink_class;

    gobject_class = (GObjectClass *)klass;
    gstelement_class = (GstElementClass *)klass;
    gstbasesink_class = (GstBaseSinkClass *)klass;
    gstvideosink_class = (GstVideoSinkClass *)klass;

    gobject_class->set_property = gst_aml_video_sink_set_property;
    gobject_class->get_property = gst_aml_video_sink_get_property;
    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_aml_video_sink_finalize);

    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);

    gst_element_class_set_static_metadata(
        gstelement_class, "aml video sink", "Sink/Video",
        "Output to video tunnel lib",
        "Xuesong.Jiang@amlogic.com<Xuesong.Jiang@amlogic.com>");

    gstelement_class->change_state =
        GST_DEBUG_FUNCPTR(gst_aml_video_sink_change_state);

    gstbasesink_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_aml_video_sink_propose_allocation);
    gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR(gst_aml_video_sink_get_caps);
    gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR(gst_aml_video_sink_set_caps);

    gstvideosink_class->show_frame = GST_DEBUG_FUNCPTR(gst_aml_video_sink_show_frame);

    g_object_class_install_property(
        gobject_class, PROP_FULLSCREEN,
        g_param_spec_boolean("fullscreen", "Fullscreen",
                             "Whether the surface should be made fullscreen ",
                             FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_SETMUTE,
        g_param_spec_boolean("set mute", "set mute params",
                             "Whether set screen mute ",
                             FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void gst_aml_video_sink_init(GstAmlVideoSink *sink)
{
    GstBaseSink *basesink = (GstBaseSink *)sink;
    gst_pad_set_event_function(basesink->sinkpad, gst_aml_video_sink_pad_event);
    GST_AML_VIDEO_SINK_GET_PRIVATE(sink) = malloc (sizeof(GstAmlVideoSinkPrivate));
    gst_aml_video_sink_reset_private(sink);
}

static void gst_aml_video_sink_get_property(GObject *object, guint prop_id,
                                            GValue *value, GParamSpec *pspec)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(object);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);

    switch (prop_id)
    {
    case PROP_FULLSCREEN:
        GST_OBJECT_LOCK(sink);
        g_value_set_boolean(value, sink_priv->fullscreen);
        GST_OBJECT_UNLOCK(sink);
        break;
    case PROP_SETMUTE:
        GST_OBJECT_LOCK(sink);
        g_value_set_boolean(value, sink_priv->mute);
        GST_OBJECT_UNLOCK(sink);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_aml_video_sink_set_property(GObject *object, guint prop_id,
                                            const GValue *value,
                                            GParamSpec *pspec)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(object);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);

    switch (prop_id)
    {
    case PROP_FULLSCREEN:
        GST_OBJECT_LOCK(sink);
        gboolean is_fullscreen = g_value_get_boolean(value);
        if (sink_priv->fullscreen != is_fullscreen)
        {
            sink_priv->fullscreen = is_fullscreen;
            //TODO set full screen to tunnel lib
        }
        GST_OBJECT_UNLOCK(sink);
        break;
    case PROP_SETMUTE:
        GST_OBJECT_LOCK(sink);
        gboolean is_mute = g_value_get_boolean(value);
        if (sink_priv->mute != is_mute)
        {
            sink_priv->fullscreen = is_mute;
            //TODO set full screen to tunnel lib
        }
        GST_OBJECT_UNLOCK(sink);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_aml_video_sink_finalize(GObject *object)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(object);

    GST_DEBUG_OBJECT(sink, "Finalizing aml video sink..");
    gst_aml_video_sink_reset_private(sink);
    if(GST_AML_VIDEO_SINK_GET_PRIVATE(sink))
        free(GST_AML_VIDEO_SINK_GET_PRIVATE(sink));
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static GstStateChangeReturn
gst_aml_video_sink_change_state(GstElement *element,
                                GstStateChange transition)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(element);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

    GST_OBJECT_LOCK(sink);
    switch (transition)
    {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
        sink_priv->render_device_handle = render_open(sink_priv->render_device_name);
        if (sink_priv->render_device_handle == NULL)
        {
            GST_ERROR_OBJECT(sink, "render lib: open device fail");
            goto error;
        }
        RenderCallback cb = {gst_render_msg_callback, gst_render_val_callback};
        render_set_callback(sink_priv->render_device_handle, &cb);
        render_set_user_data(sink_priv->render_device_handle, sink);

        break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
        if (render_connect(sink_priv->render_device_handle) == 0)
        {
            GST_ERROR_OBJECT(sink, "render lib connect device fail");
            goto error;
        }
        break;
    }
    default:
        break;
    }
    GST_OBJECT_UNLOCK(sink);

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

    GST_OBJECT_LOCK(sink);
    if (ret == GST_STATE_CHANGE_FAILURE)
        goto error;

    switch (transition)
    {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
        render_disconnect(sink_priv->render_device_handle);
        break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
        if (sink_priv->render_device_handle)
        {
            render_close(sink_priv->render_device_handle);
        }
        gst_aml_video_sink_reset_private(sink);

        break;
    }
    default:
        break;
    }
    GST_OBJECT_UNLOCK(sink);
    return ret;

error:
    GST_OBJECT_UNLOCK(sink);
    ret = GST_STATE_CHANGE_FAILURE;
    return ret;
}

static gboolean gst_aml_video_sink_propose_allocation(GstBaseSink *bsink, GstQuery *query)
{
    //TODO only implement dma case
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(bsink);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);

    GstCaps *caps;
    GstBufferPool *pool = NULL;
    gboolean need_pool;

    gst_query_parse_allocation(query, &caps, &need_pool);

    if (need_pool)
        //TODO 没有考虑secure场景
        pool = gst_drm_bufferpool_new(FALSE, GST_DRM_BUFFERPOOL_TYPE_VIDEO_PLANE);

    gst_query_add_allocation_pool(query, pool, sink_priv->video_info.size, 2, 0);
    if (pool)
        g_object_unref(pool);

    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);

    return TRUE;
}

static GstCaps *gst_aml_video_sink_get_caps(GstBaseSink *bsink,
                                            GstCaps *filter)
{
    GstAmlVideoSink *sink;
    GstCaps *caps;

    sink = GST_AML_VIDEO_SINK(bsink);

    caps = gst_pad_get_pad_template_caps(GST_VIDEO_SINK_PAD(sink));
    caps = gst_caps_make_writable(caps);
    // TODO 这里是需要从template直接取出支持的caps还是要通过tunnel lib拿到caps？

    if (filter)
    {
        GstCaps *intersection;

        intersection =
            gst_caps_intersect_full(filter, caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(caps);
        caps = intersection;
    }

    return caps;
}

static gboolean gst_aml_video_sink_set_caps(GstBaseSink *bsink, GstCaps *caps)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(bsink);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
    gboolean use_dmabuf;

    GST_OBJECT_LOCK(sink);

    GST_DEBUG_OBJECT(sink, "set caps %" GST_PTR_FORMAT, caps);
    use_dmabuf = gst_caps_features_contains(gst_caps_get_features(caps, 0), GST_CAPS_FEATURE_MEMORY_DMABUF);
    if (use_dmabuf == FALSE)
    {
        GST_DEBUG_OBJECT(sink, "not support non dma buffer case");
        return FALSE;
    }

    /* extract info from caps */
    if (!gst_video_info_from_caps(&sink_priv->video_info, caps))
    {
        GST_ERROR_OBJECT(sink, "can't get video info from caps");
        return FALSE;
    }

    sink_priv->video_info_changed = TRUE;

    GST_OBJECT_UNLOCK(sink);
    return TRUE;
}

static GstFlowReturn gst_aml_video_sink_show_frame(GstVideoSink *vsink, GstBuffer *buffer)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(vsink);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
    GstFlowReturn ret = GST_FLOW_OK;
    RenderBuffer *tunnel_lib_buf_wrap = NULL;

    GST_OBJECT_LOCK(vsink);

    if (!sink_priv->render_device_handle)
    {
        GST_ERROR_OBJECT(sink, "flow error, render_device_handle == NULL");
        goto error;
    }

    // TODO should call tunnel lib flush func
    if (sink_priv->is_flushing)
    {
        gst_buffer_unref(buffer);
        if (!render_flush(sink_priv->render_device_handle))
        {
            GST_ERROR_OBJECT(sink, "render lib: flush error");
            goto error;
        }
        GST_DEBUG_OBJECT(sink, "in flushing flow, release the buffer directly");
        goto done;
    }

    if (sink_priv->video_info_changed)
    {
        if (gst_render_set_params(vsink) == FALSE)
        {
            GST_ERROR_OBJECT(sink, "render lib: set params fail");
            goto error;
        }
        sink_priv->video_info_changed = FALSE;
    }

    tunnel_lib_buf_wrap = render_allocate_render_buffer_wrap(sink_priv->render_device_handle, BUFFER_FLAG_EXTER_DMA_BUFFER, 0);
    if (!tunnel_lib_buf_wrap)
    {
        GST_ERROR_OBJECT(sink, "render lib: alloc buffer wrap fail");
        goto error;
    }
    if (!gst_aml_video_sink_tunnel_buf(sink, buffer, tunnel_lib_buf_wrap))
    {
        GST_ERROR_OBJECT(sink, "construc render buffer fail");
        goto error;
    }

    if (!render_display_frame(sink_priv->render_device_handle, tunnel_lib_buf_wrap))
    {
        GST_ERROR_OBJECT(sink, "render lib: display frame fail");
        goto error;
    }
    GST_DEBUG_OBJECT(sink, "GstBuffer:%p queued", buffer);

done:
    GST_OBJECT_UNLOCK(vsink);
    return ret;
error:
    GST_OBJECT_UNLOCK(vsink);
    ret = GST_FLOW_CUSTOM_ERROR_2;
    return ret;
}

static gboolean gst_aml_video_sink_pad_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    gboolean result = TRUE;
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(parent);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);

    switch (GST_EVENT_TYPE(event))
    {
    case GST_EVENT_FLUSH_START:
    {
        GST_INFO_OBJECT(sink, "flush start");
        GST_OBJECT_LOCK(sink);
        sink_priv->is_flushing = TRUE;
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
        GST_INFO_OBJECT(sink, "flush stop");

        GST_OBJECT_LOCK(sink);
        sink_priv->is_flushing = FALSE;
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case GST_EVENT_SEGMENT:
    {
        GST_OBJECT_LOCK(sink);
        gst_event_copy_segment(event, &sink_priv->segment);
        GST_INFO_OBJECT(sink, "configured segment %" GST_SEGMENT_FORMAT, &sink_priv->segment);
        //TODO set play rate to tunnel lib, 切换rate这部分是不是只需要audio那边set即可
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    default:
    {
        GST_DEBUG_OBJECT(sink, "pass to basesink");
        return GST_BASE_SINK_CLASS(parent_class)->event((GstBaseSink *)sink, event);
    }
    }
    gst_event_unref(event);
    return result;
}

/* private interface definition */
static void gst_aml_video_sink_reset_private(GstAmlVideoSink *sink)
{
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
    memset(sink_priv, 0, sizeof(GstAmlVideoSinkPrivate));
    sink_priv->render_device_name = RENDER_DEVICE_NAME;
    sink_priv->use_dmabuf = USE_DMABUF;
    sink_priv->mediasync_instanceid = -1;
}

static void gst_render_msg_callback(void *userData, RenderMsgType type, void *msg)
{
    GstAmlVideoSink *sink = (GstAmlVideoSink *)userData;
    switch (type)
    {
    case MSG_RELEASE_BUFFER:
    {
        GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
        RenderBuffer *tunnel_lib_buf_wrap = (RenderBuffer *)msg;
        GstBuffer *buffer = (GstBuffer *)tunnel_lib_buf_wrap->priv;

        if (buffer)
        {
            GST_DEBUG_OBJECT(sink, "GstBuffer:%p rendered", buffer);
            gst_buffer_unref(buffer);
        }
        else
        {
            GST_ERROR_OBJECT(sink, "tunnel lib: return void GstBuffer");
        }
        render_free_render_buffer_wrap(sink_priv->render_device_handle, tunnel_lib_buf_wrap);
        break;
    }
    case MSG_CONNECTED_FAIL:
    {
        GST_ERROR_OBJECT(sink, "tunnel lib: should not send message:%d", type);
        break;
    }
    case MSG_DISCONNECTED_FAIL:
    {
        GST_ERROR_OBJECT(sink, "tunnel lib: should not send message:%d", type);
        break;
    }
    default:
    {
        GST_ERROR_OBJECT(sink, "tunnel lib: error message type");
    }
    }
    return;
}

int gst_render_val_callback(void *userData, int key, void *value)
{
    GstAmlVideoSink *vsink = (GstAmlVideoSink *)userData;
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(vsink);
    gint *val = (gint *)value;
    gint ret = 0;
    switch (key)
    {
    case KEY_GET_MEDIASYNC_INSTANCE:
    {
        if (gst_get_mediasync_instanceid(vsink))
        {
            *val = sink_priv->mediasync_instanceid;
            GST_DEBUG_OBJECT(vsink, "get mediasync instance id:%d", *val);
        }
        else
        {
            GST_ERROR_OBJECT(vsink, "can't get mediasync instance id");
            ret = -1;
        }
        break;
    }
    case KEY_VIDEO_FORMAT:
    {
        *val = sink_priv->video_info.finfo->format;
        GST_DEBUG_OBJECT(vsink, "get video format:%d", *val);
        if (*val == GST_VIDEO_FORMAT_UNKNOWN)
        {
            GST_ERROR_OBJECT(vsink, "get video format error");
            ret = -1;
        }
        break;
    }
    default:
    {
        GST_ERROR_OBJECT(vsink, "tunnel lib: error key type");
        ret = -1;
    }
    }
    return ret;
}

static gboolean gst_aml_video_sink_tunnel_buf(GstAmlVideoSink *vsink, GstBuffer *gst_buf, RenderBuffer *tunnel_lib_buf_wrap)
{
    // only support dma buf
    RenderDmaBuffer *dmabuf = tunnel_lib_buf_wrap->dma;
    GstMemory *dma_mem = NULL;
    GstVideoMeta *vmeta = NULL;
    guint n_mem = 0;

    if (gst_buf == NULL || tunnel_lib_buf_wrap == NULL)
    {
        GST_ERROR_OBJECT(vsink, "input params error");
        goto error;
    }
    n_mem = gst_buffer_n_memory(gst_buf);
    vmeta = gst_buffer_get_video_meta (gst_buf);
    if(vmeta == NULL)
    {
        GST_ERROR_OBJECT(vsink, "not found video meta info");
        goto error;
    }
    if (n_mem > RENDER_MAX_PLANES || vmeta->n_planes > RENDER_MAX_PLANES || n_mem != vmeta->n_planes)
    {
        GST_ERROR_OBJECT(vsink, "too many memorys in gst buffer");
        goto error;
    }
    
    dmabuf->planeCnt = n_mem;
    dmabuf->width = vmeta->width;
    dmabuf->height = vmeta->height;
    for (guint i = 0; i < n_mem; i++)
    {
        gint dmafd;
        gsize size, offset, maxsize;
        dma_mem = gst_buffer_peek_memory(gst_buf, i);
        if (!gst_is_dmabuf_memory(dma_mem))
        {
            GST_ERROR_OBJECT(vsink, "not support non-dma buf");
            goto error;
        }
        size = gst_memory_get_sizes(dma_mem, &offset, &maxsize);
        dmafd = gst_dmabuf_memory_get_fd(dma_mem);
        dmabuf->handle[i] = 0;
        dmabuf->fd[i] = dmafd;
        dmabuf->size[i] = dma_mem->size;
        dmabuf->offset[i] = vmeta->offset[i];
        dmabuf->stride[i] = vmeta->stride[i];
        GST_DEBUG_OBJECT(vsink, "dma buffer layer:%d, handle:%d, fd:%d, size:%d, offset:%d, stride:%d", 
                         i, dmabuf->handle[i], dmabuf->fd[i], dmabuf->size[i], dmabuf->offset[i], dmabuf->stride[i]);
    }
    tunnel_lib_buf_wrap->flag = BUFFER_FLAG_EXTER_DMA_BUFFER;
    tunnel_lib_buf_wrap->pts = GST_BUFFER_PTS(gst_buf);
    tunnel_lib_buf_wrap->priv = (void *)gst_buf;

error:
    return FALSE;
}

static gboolean gst_get_mediasync_instanceid(GstAmlVideoSink *vsink)
{
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(vsink);
    GstElement *asink = gst_aml_video_sink_find_audio_sink(vsink);
    GstClock *amlclock = gst_aml_hal_asink_get_clock((GstElement *)asink);
    gboolean ret = TRUE;
    if (amlclock)
    {
        sink_priv->mediasync_instanceid = gst_aml_clock_get_session_id(amlclock);
        GST_DEBUG_OBJECT(vsink, "get mediasync instance id:%d, from aml audio clock:%p. in aml audio sink:%p", sink_priv->mediasync_instanceid, amlclock, vsink);
        if (sink_priv->mediasync_instanceid == -1)
        {
            GST_ERROR_OBJECT(vsink, "audio sink: don't have valid mediasync instance id");
            ret = FALSE;
        }
    }
    else
    {
        GST_WARNING_OBJECT(vsink, "no clock: vmaster mode");
        ret = FALSE;
    }
    gst_object_unref(asink);
    return ret;
}

static GstElement *gst_aml_video_sink_find_audio_sink(GstAmlVideoSink *sink)
{
    GstElement *audioSink = 0;
    GstElement *pipeline = 0;
    GstElement *element, *elementPrev = 0;

    element = GST_ELEMENT_CAST(sink);
    do
    {
        if (elementPrev)
        {
            gst_object_unref(elementPrev);
        }
        element = GST_ELEMENT_CAST(gst_element_get_parent(element));
        if (element)
        {
            elementPrev = pipeline;
            pipeline = element;
        }
    } while (element != 0);

    if (pipeline)
    {
        GstIterator *iterElement = gst_bin_iterate_recurse(GST_BIN(pipeline));
        if (iterElement)
        {
            GValue itemElement = G_VALUE_INIT;
            while (gst_iterator_next(iterElement, &itemElement) == GST_ITERATOR_OK)
            {
                element = (GstElement *)g_value_get_object(&itemElement);
                if (element && !GST_IS_BIN(element))
                {
                    int numSrcPads = 0;

                    GstIterator *iterPad = gst_element_iterate_src_pads(element);
                    if (iterPad)
                    {
                        GValue itemPad = G_VALUE_INIT;
                        while (gst_iterator_next(iterPad, &itemPad) == GST_ITERATOR_OK)
                        {
                            GstPad *pad = (GstPad *)g_value_get_object(&itemPad);
                            if (pad)
                            {
                                ++numSrcPads;
                            }
                            g_value_reset(&itemPad);
                        }
                        gst_iterator_free(iterPad);
                    }

                    if (numSrcPads == 0)
                    {
                        GstElementClass *ec = GST_ELEMENT_GET_CLASS(element);
                        if (ec)
                        {
                            const gchar *meta = gst_element_class_get_metadata(ec, GST_ELEMENT_METADATA_KLASS);
                            if (meta && strstr(meta, "Sink") && strstr(meta, "Audio"))
                            {
                                audioSink = (GstElement *)gst_object_ref(element);
                                gchar *name = gst_element_get_name(element);
                                if (name)
                                {
                                    GST_DEBUG("detected audio sink: name (%s)", name);
                                    g_free(name);
                                }
                                g_value_reset(&itemElement);
                                break;
                            }
                        }
                    }
                }
                g_value_reset(&itemElement);
            }
            gst_iterator_free(iterElement);
        }

        gst_object_unref(pipeline);
    }
    return audioSink;
}

static gboolean gst_render_set_params(GstVideoSink *vsink)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(vsink);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
    GstVideoInfo *video_info = &(sink_priv->video_info);

    RenderWindowSize window_size = {0, 0, video_info->width, video_info->height};
    RenderFrameSize frame_size = {video_info->width, video_info->height};
    if (render_set_props(sink_priv->render_device_handle, PROP_WINDOW_SIZE, &window_size) == -1)
    {
        GST_ERROR_OBJECT(vsink, "tunnel lib: set window size error");
        return FALSE;
    }
    if (render_set_props(sink_priv->render_device_handle, PROP_FRAME_SIZE, &frame_size))
    {
        GST_ERROR_OBJECT(vsink, "tunnel lib: set frame size error");
        return FALSE;
    }
    return TRUE;
}

/* plugin init */
static gboolean plugin_init(GstPlugin *plugin)
{
    // GST_DEBUG_CATEGORY_INIT(gst_aml_video_sink_debug, "amlvideosink", 0,
    //                         " aml video sink");
    // GST_DEBUG("trace in amlvideosink 111");

    return gst_element_register(plugin, "amlvideosink", 300,
                                GST_TYPE_AML_VIDEO_SINK);
}

#ifndef VERSION
#define VERSION "0.1.0"
#endif
#ifndef PACKAGE
#define PACKAGE "aml_package"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "aml_media"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://amlogic.com"
#endif
// GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, amlvideosink,
//                   "aml Video Sink", plugin_init, VERSION, "LGPL",
//                   "gst-plugin-video-sink", "")
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    amlvideosink,
    "Amlogic plugin for video decoding/rendering",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
