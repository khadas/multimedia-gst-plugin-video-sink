#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstamlvideosink.h"
#include "render_lib.h"
#include <stdbool.h>`
#include <gst/gstdrmbufferpool.h>
#include <gst/allocators/gstdmabuf.h>

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
    _GstSegment segment;
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
#define GST_AML_VIDEO_SINK_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), GST_TYPE_AML_VIDEO_SINK, \
                                 GstAmlVideoSinkPrivate))
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
static gboolean static gboolean
gst_aml_video_sink_show_frame(GstVideoSink *bsink, GstBuffer *buffer);
static gboolean gst_aml_video_sink_pad_event(GstAmlVideoSink *sink,
                                             GstEvent *event);

/* private interface define */
static void gst_aml_video_sink_reset_private(GstAmlVideoSink *sink);
static GstFlowReturn gst_tunnel_lib_play_rate(GstAmlVideoSink *sink);
static GstFlowReturn gst_tunnel_lib_flush(GstAmlVideoSink *sink);
static void render_callback(void *userData, RenderMessageType type, void *msg);

/* public interface definition */
static void gst_aml_video_sink_class_init(GstWaylandSinkClass *klass)
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
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);

    gst_pad_set_event_function(basesink->sinkpad, gst_aml_video_sink_pad_event);
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
        g_value_set_boolean(value, sink->mute);
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
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static GstStateChangeReturn
gst_aml_video_sink_change_state(GstElement *element,
                                GstStateChange transition)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(element);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
    GstAmlVideoSinkClass *class = GST_AML_VIDEO_SINK_CLASS(element);
    GstBaseSinkClass *base_class = (GstBaseSinkClass *)class;
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
        if (render_set_callback(sink_priv->render_device_handle, render_callback) == 0)
        {
            GST_ERROR_OBJECT(sink, "render lib: set callback fail");
            goto error;
        }
        if (render_set_user_data(sink_priv->render_device_handle, sink))
        {
            GST_ERROR_OBJECT(sink, "render lib: set usr data fail");
            goto error;
        }

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
    GstCaps *caps;
    GstBufferPool *pool = NULL;
    gboolean need_pool;
    GstAllocator *alloc;

    gst_query_parse_allocation(query, &caps, &need_pool);

    if (need_pool)
        pool = gst_drm_bufferpool_new(sink->secure, GST_DRM_BUFFERPOOL_TYPE_VIDEO_PLANE);

    gst_query_add_allocation_pool(query, pool, sink->video_info.size, 2, 0);
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
    goto invalid_format;

    sink_priv->video_info_changed = TRUE;

    GST_OBJECT_UNLOCK(sink);
    return TRUE;
}

static GstFlowReturn gst_aml_video_sink_show_frame(GstVideoSink *vsink, GstBuffer *buffer)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(element);
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
        //TODO set para for tunnel lib, need convert GstVideoInfo to tunnel lib struct
        if (!render_set_params(sink_priv->render_device_handle, 0, NULL))
        {
            GST_ERROR_OBJECT(sink, "render lib: set params fail");
            goto error;
        }
        sink_priv->video_info_changed = FALSE;
    }

    tunnel_lib_buf_wrap = render_allocate_render_buffer(sink_priv->render_device_handle, RENDER_BUFFER_TYPE_FD_FROM_USER, 0);
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
    GST_DEBUG_OBJECT(sink, "GstBuffer:0x%x queued", buffer);

done:
    GST_OBJECT_UNLOCK(vsink);
    return ret;
error:
    GST_OBJECT_UNLOCK(vsink);
    ret = GST_FLOW_CUSTOM_ERROR_2;
    return ret;
}

static gboolean gst_aml_video_sink_pad_event(GstAmlVideoSink *sink,
                                             GstEvent *event)
{
    gboolean result = TRUE;
    GstBaseSink *bsink = GST_BASE_SINK_CAST(sink);
    GstAmlVideoSinkPrivate *sink_priv =
        GST_AML_VIDEO_SINK_GET_PRIVATE(sink);

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
        return GST_BASE_SINK_CLASS(parent_class)->event(bsink, event);
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
}

static void render_callback(void *userData, RenderMessageType type, void *msg)
{
    GstAmlVideoSink *sink = (GstAmlVideoSink *)userData;
    switch (type)
    {
    case RENDER_MSG_RELEASE_BUFFER:
    {
        GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
        //TODO 从msg中拿到renderbuffer
        RenderBuffer *tunnel_lib_buf_wrap;
        GstBuffer *buffer = (GstBuffer *)tunnel_lib_buf_wrap->ext;

        if (buffer)
        {
            GST_DEBUG_OBJECT(sink, "GstBuffer:0x%x rendered", buffer);
            gst_buffer_unref(buffer);
        }
        else
        {
            GST_ERROR_OBJECT(sink, "tunnel lib: return void GstBuffer");
        }
        render_free_render_buffer(sink_priv->render_device_handle, tunnel_lib_buf_wrap);
        break;
    }
    default:
    {
        GST_ERROR_OBJECT(sink, "tunnel lib: error message type");
    }
    }
    return;
}

static gboolean gst_aml_video_sink_tunnel_buf(GstVideoSink *vsink, GstBuffer *gst_buf, RenderBuffer *tunnel_lib_buf_wrap)
{
    // only support dma buf
    GstMemory *dma_mem[GST_VIDEO_MAX_PLANES] = {0};
    guint n_mem = 0;

    if (gst_buf == NULL || tunnel_lib_buf_wrap tunnel_lib_buf_wrap == NULL)
    {
        GST_ERROR_OBJECT(vsink, "input params error");
        goto error;
    }
    if (n_mem > GST_VIDEO_MAX_PLANES)
    {
        GST_ERROR_OBJECT(vsink, "too many memorys in gst buffer");
        goto error;
    }
    n_mem = gst_buffer_n_memory(gst_buf);
    for (i = 0; i < n_mem; i++)
    {
        gint dmafd;
        gsize size, offset, maxsize;
        dma_mem[i] = gst_buffer_peek_memory(src, i);
        if (!gst_is_dmabuf_memory(dma_mem[i]))
        {
            GST_ERROR_OBJECT(vsink, "not support non-dma buf");
            goto error;
        }
        size = gst_memory_get_sizes(dma_mem[i], &offset, &maxsize);
        dmafd = gst_dmabuf_memory_get_fd(dma_mem[i]);
        tunnel_lib_buf_wrap->data.fd[i] = dmafd;
        tunnel_lib_buf_wrap->data.dataSize++ GST_DEBUG_OBJECT(vsink, "dma buffer layer:%d, fd:%d", i, tunnel_lib_buf_wrap->data.fd[i]);
    }
    tunnel_lib_buf_wrap->type = RENDER_BUFFER_TYPE_FD_FROM_USER;
    tunnel_lib_buf_wrap->pts = GST_BUFFER_PTS(gst_buf);
    tunnel_lib_buf_wrap->ext = (void *)gst_buf;

error:
    return FALSE;
}

/* plugin init */
static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(gst_aml_video_sink_debug, "amlvideosink", 0,
                            " aml video sink");

    return gst_element_register(plugin, "amlvideosink", 1,
                                GST_TYPE_AML_VIDEO_SINK);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, waylandsink,
                  "aml Video Sink", plugin_init, VERSION, "LGPL",
                  GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
