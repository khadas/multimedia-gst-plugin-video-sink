/* GStreamer
 * Copyright (C) 2021 <song.zhao@amlogic.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstamlvideosink
 *
 * The gstamlvideosink element call render lib to render video
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdbool.h>
#include <gst/gstdrmbufferpool.h>
#include <gst/allocators/gstdmabuf.h>
#include "gstamlvideosink.h"
#include <render_lib.h>
// #ifdef USE_AMLOGIC_MESON
// #ifdef USE_AMLOGIC_MESON_MSYNC
// #define INVALID_SESSION_ID (16)
#include "gstamlclock.h"
#include "gstamlhalasink_new.h"
#include <stdio.h>
// #endif
// #endif

#ifdef GST_OBJECT_LOCK
#undef GST_OBJECT_LOCK
#define GST_OBJECT_LOCK(obj) \
{ \
  GST_DEBUG("dbg basesink ctxt lock | aml | %p | locking", obj); \
  g_mutex_lock(GST_OBJECT_GET_LOCK(obj)); \
  GST_DEBUG("dbg basesink ctxt lock | aml | %p | locked", obj); \
} 
#endif

#ifdef GST_OBJECT_UNLOCK
#undef GST_OBJECT_UNLOCK
#define GST_OBJECT_UNLOCK(obj) \
{ \
  GST_DEBUG("dbg basesink ctxt lock | aml |%p | unlocking", obj); \
  g_mutex_unlock(GST_OBJECT_GET_LOCK(obj)); \
  GST_DEBUG("dbg basesink ctxt lock | aml |%p | unlocked", obj); \
} 
#endif

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
#define GST_USE_PLAYBIN 0
#define RENDER_DEVICE_NAME "wayland"
#define USE_DMABUF TRUE

#define DRMBP_EXTRA_BUF_SZIE_FOR_DISPLAY      10
#define DRMBP_LIMIT_MAX_BUFSIZE_TO_BUFSIZE    1
#define DRMBP_UNLIMIT_MAX_BUFSIZE             0

struct _GstAmlVideoSinkPrivate
{
    GstBuffer *preroll_buffer;
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
void gst_render_msg_callback(void *userData, RenderMsgType type, void *msg);
int gst_render_val_callback(void *userData, int key, void *value);
static gboolean gst_aml_video_sink_tunnel_buf(GstAmlVideoSink *vsink, GstBuffer *gst_buf, RenderBuffer *tunnel_lib_buf_wrap);
static gboolean gst_get_mediasync_instanceid(GstAmlVideoSink *vsink);
#if GST_USE_PLAYBIN
static GstElement *gst_aml_video_sink_find_audio_sink(GstAmlVideoSink *sink);
#endif
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
    gst_base_sink_set_sync(basesink, FALSE);
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
            sink_priv->mute = is_mute;
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
    GST_DEBUG_OBJECT(sink, "trace in");

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
        if (render_connect(sink_priv->render_device_handle) == -1)
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

    GST_LOG_OBJECT(sink, "amlvideosink deal state change ok, goto basesink state change");
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
    GST_DEBUG_OBJECT(bsink, "trace in");
    //TODO only implement dma case
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(bsink);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);

    GstCaps *caps;
    GstBufferPool *pool = NULL;
    gboolean need_pool;

    gst_query_parse_allocation(query, &caps, &need_pool);
    GST_DEBUG_OBJECT(bsink, "jxsaaa need_pool:%d", need_pool);

    if (need_pool)
        //TODO 没有考虑secure场景
        pool = gst_drm_bufferpool_new(FALSE, GST_DRM_BUFFERPOOL_TYPE_VIDEO_PLANE);

    gst_query_add_allocation_pool(query, pool, sink_priv->video_info.size, DRMBP_EXTRA_BUF_SZIE_FOR_DISPLAY, DRMBP_LIMIT_MAX_BUFSIZE_TO_BUFSIZE);
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
    // gboolean use_dmabuf;
    gboolean ret = TRUE;

    GST_OBJECT_LOCK(sink);

    GST_DEBUG_OBJECT(sink, "set caps %" GST_PTR_FORMAT, caps);
    // use_dmabuf = gst_caps_features_contains(gst_caps_get_features(caps, 0), GST_CAPS_FEATURE_MEMORY_DMABUF);
    // if (use_dmabuf == FALSE)
    // {
    //     GST_ERROR_OBJECT(sink, "not support non dma buffer case");
    //     ret = FALSE;
    //     goto done;
    // }

    /* extract info from caps */
    if (!gst_video_info_from_caps(&sink_priv->video_info, caps))
    {
        GST_ERROR_OBJECT(sink, "can't get video info from caps");
        ret = FALSE;
        goto done;
    }

    sink_priv->video_info_changed = TRUE;

done:
    GST_OBJECT_UNLOCK(sink);
    return ret;
}

static GstFlowReturn gst_aml_video_sink_show_frame(GstVideoSink *vsink, GstBuffer *buffer)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(vsink);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
    GstFlowReturn ret = GST_FLOW_OK;
    RenderBuffer *tunnel_lib_buf_wrap = NULL;

    GST_OBJECT_LOCK(vsink);
    GST_LOG_OBJECT(sink, "revice buffer:%p, from pool:%p, need_preroll:%d", buffer, buffer->pool, ((GstBaseSink *)sink)->need_preroll);

    if (!sink_priv->render_device_handle)
    {
        GST_ERROR_OBJECT(sink, "flow error, render_device_handle == NULL");
        goto error;
    }

    if (sink_priv->preroll_buffer && sink_priv->preroll_buffer == buffer)
    {
        GST_LOG_OBJECT(sink, "get preroll buffer:%p 2nd time, goto done", buffer);
        sink_priv->preroll_buffer = NULL;
        goto done;
    }
    if (G_UNLIKELY(((GstBaseSink *)sink)->need_preroll))
    {
        GST_LOG_OBJECT(sink, "get preroll buffer 1st time, buf:%p", buffer);
        sink_priv->preroll_buffer = buffer;
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

    if (render_display_frame(sink_priv->render_device_handle, tunnel_lib_buf_wrap) == -1)
    {
        GST_ERROR_OBJECT(sink, "render lib: display frame fail");
        goto error;
    }

done:
    GST_OBJECT_UNLOCK(vsink);
    GST_DEBUG_OBJECT(sink, "GstBuffer:%p queued ok", buffer);
    return ret;
error:
    GST_OBJECT_UNLOCK(vsink);
    GST_DEBUG_OBJECT(sink, "GstBuffer:%p queued error", buffer);
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

void gst_render_msg_callback(void *userData, RenderMsgType type, void *msg)
{
    GstAmlVideoSink *sink = (GstAmlVideoSink *)userData;
    switch (type)
    {
    case MSG_RELEASE_BUFFER:
    {
        GST_LOG_OBJECT(sink, "get message: MSG_RELEASE_BUFFER from tunnel lib");
        GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
        RenderBuffer *tunnel_lib_buf_wrap = (RenderBuffer *)msg;
        RenderDmaBuffer *dmabuf = &tunnel_lib_buf_wrap->dma;
        GstBuffer *buffer = (GstBuffer *)tunnel_lib_buf_wrap->priv;

        GST_DEBUG_OBJECT(sink, "dbg: buf out:%p, planeCnt:%d, plane[0].fd:%d, plane[1].fd:%d", 
                        tunnel_lib_buf_wrap->priv, 
                        dmabuf->planeCnt, 
                        dmabuf->fd[0], 
                        dmabuf->fd[1]);

        if (buffer)
        {
            GST_LOG_OBJECT(sink, "get message: MSG_RELEASE_BUFFER from tunnel lib, buffer:%p, from pool:%p", buffer, buffer->pool);
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
    int *val = (int *)value;
    gint ret = 0;
    switch (key)
    {
    case KEY_MEDIASYNC_INSTANCE_ID:
    {
        // break;
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
        if(sink_priv->video_info.finfo != NULL)
        {
            *val = sink_priv->video_info.finfo->format;
            GST_DEBUG_OBJECT(vsink, "get video format:%d", *val);
        }
        else
        {
            GST_ERROR_OBJECT(vsink, "get video format error");
            *val = GST_VIDEO_FORMAT_UNKNOWN;
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
    RenderDmaBuffer *dmabuf = &tunnel_lib_buf_wrap->dma;
    GstMemory *dma_mem = NULL;
    GstVideoMeta *vmeta = NULL;
    guint n_mem = 0;
    gboolean ret = TRUE;

    if (gst_buf == NULL || tunnel_lib_buf_wrap == NULL || dmabuf == NULL)
    {
        GST_ERROR_OBJECT(vsink, "input params error");
        ret = FALSE;
        goto error;
    }
    gst_buffer_ref(gst_buf);
    n_mem = gst_buffer_n_memory(gst_buf);
    vmeta = gst_buffer_get_video_meta (gst_buf);
    if(vmeta == NULL)
    {
        GST_ERROR_OBJECT(vsink, "not found video meta info");
        ret = FALSE;
        goto error;
    }
    if (n_mem > RENDER_MAX_PLANES || vmeta->n_planes > RENDER_MAX_PLANES || n_mem != vmeta->n_planes)
    {
        GST_ERROR_OBJECT(vsink, "too many memorys in gst buffer");
        goto error;
    }
    GST_DEBUG_OBJECT(vsink, "dbg3-0, dmabuf:%p", dmabuf);
    
    dmabuf->planeCnt = n_mem;
    dmabuf->width = vmeta->width;
    dmabuf->height = vmeta->height;

    GST_DEBUG_OBJECT(vsink, "dbgjxs, vmeta->width:%d, dmabuf->width:%d", vmeta->width, dmabuf->width);

    for (guint i = 0; i < n_mem; i++)
    {
        gint dmafd;
        gsize size, offset, maxsize;
        dma_mem = gst_buffer_peek_memory(gst_buf, i);
        guint mem_idx = 0;
        guint length = 0;
        gsize skip = 0;

        if (!gst_is_dmabuf_memory(dma_mem))
        {
            GST_ERROR_OBJECT(vsink, "not support non-dma buf");
            ret = FALSE;
            goto error;
        }
        size = gst_memory_get_sizes(dma_mem, &offset, &maxsize);
        GST_LOG_OBJECT(vsink, "get memory size:%d, offeset:%d, maxsize:%d", size, offset, maxsize);

        dmafd = gst_dmabuf_memory_get_fd(dma_mem);
        dmabuf->handle[i] = 0;
        dmabuf->fd[i] = dmafd;
        dmabuf->size[i] = dma_mem->size;
        dmabuf->stride[i] = vmeta->stride[i];
        if (gst_buffer_find_memory (gst_buf, vmeta->offset[i], 1, &mem_idx, &length, &skip) && mem_idx == i)
        {
            dmabuf->offset[i] = dma_mem->offset + skip;
            GST_DEBUG_OBJECT(vsink, "get skip from buffer:%d, offset[%d]:%d", skip, i, dmabuf->offset[i]);
        }
        else
        {
            GST_ERROR_OBJECT(vsink, "get skip from buffer error");
            ret = FALSE;
            goto error;
        }


        GST_DEBUG_OBJECT(vsink, "dma buffer layer:%d, handle:%d, fd:%d, size:%d, offset:%d, stride:%d", 
                         i, dmabuf->handle[i], dmabuf->fd[i], dmabuf->size[i], dmabuf->offset[i], dmabuf->stride[i]);
    }
    tunnel_lib_buf_wrap->flag = BUFFER_FLAG_EXTER_DMA_BUFFER;
    tunnel_lib_buf_wrap->pts = GST_BUFFER_PTS(gst_buf);
    tunnel_lib_buf_wrap->priv = (void *)gst_buf;
    GST_DEBUG_OBJECT(vsink, "set tunnel lib buf priv:%p from pool:%p", tunnel_lib_buf_wrap->priv, gst_buf->pool);
    GST_DEBUG_OBJECT(vsink, "dbg: buf in:%p, planeCnt:%d, plane[0].fd:%d, plane[1].fd:%d", 
                            tunnel_lib_buf_wrap->priv, 
                            dmabuf->planeCnt, 
                            dmabuf->fd[0], 
                            dmabuf->fd[1]);
    
    return ret;

error:
    return ret;
}

static gboolean gst_get_mediasync_instanceid(GstAmlVideoSink *vsink)
{
    GST_DEBUG_OBJECT(vsink, "trace in");
#if GST_USE_PLAYBIN
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
#else
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(vsink);
    gboolean ret = TRUE;
    FILE * fp;
    fp = fopen("/data/MediaSyncId", "r");
    if (fp == NULL) {
        GST_ERROR_OBJECT(vsink, "could not open file:/data/MediaSyncId failed");
        ret = FALSE;
    } else {
        size_t read_size = 0;
        read_size = fread(&sink_priv->mediasync_instanceid, sizeof(int), 1, fp);
        if (read_size != sizeof(int))
        {
            GST_DEBUG_OBJECT(vsink, "get mediasync instance id read error");
        }
        fclose(fp);
        GST_DEBUG_OBJECT(vsink, "get mediasync instance id:0x%x", sink_priv->mediasync_instanceid);
    }
#endif
    return ret;
}

#if GST_USE_PLAYBIN
static GstElement *gst_aml_video_sink_find_audio_sink(GstAmlVideoSink *sink)
{
    GST_DEBUG_OBJECT(sink, "trace in");
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
        GST_DEBUG_OBJECT(sink, "dbg-0, element:%p, parent:%p", element, gst_element_get_parent(element));
        element = GST_ELEMENT_CAST(gst_element_get_parent(element));
        if (element)
        {
            elementPrev = pipeline;
            pipeline = element;
        }
        GST_DEBUG_OBJECT(sink, "dbg-1");
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
    GST_DEBUG_OBJECT(sink, "trace out");
    return audioSink;
}
#endif

static gboolean gst_render_set_params(GstVideoSink *vsink)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(vsink);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
    GstVideoInfo *video_info = &(sink_priv->video_info);

    RenderWindowSize window_size = {0, 0, video_info->width, video_info->height};
    RenderFrameSize frame_size = {video_info->width, video_info->height};
    GstVideoFormat format = video_info->finfo? video_info->finfo->format : GST_VIDEO_FORMAT_UNKNOWN;
    if (render_set(sink_priv->render_device_handle, KEY_WINDOW_SIZE, &window_size) == -1)
    {
        GST_ERROR_OBJECT(vsink, "tunnel lib: set window size error");
        return FALSE;
    }
    if (render_set(sink_priv->render_device_handle, KEY_FRAME_SIZE, &frame_size) == -1)
    {
        GST_ERROR_OBJECT(vsink, "tunnel lib: set frame size error");
        return FALSE;
    }
    if (render_set(sink_priv->render_device_handle, KEY_VIDEO_FORMAT, &format) == -1)
    {
        GST_ERROR_OBJECT(vsink, "tunnel lib: set video format error");
        return FALSE;
    }

    return TRUE;
}

/* plugin init */
static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(gst_aml_video_sink_debug, "amlvideosink", 0,
                            " aml video sink");

    gint rank = 1;
    const char *rank_env = getenv ("GST_AML_VIDEO_SINK_RANK");
    if (rank_env) {
      rank = atoi(rank_env);
   }

    return gst_element_register(plugin, "amlvideosink", rank,
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
