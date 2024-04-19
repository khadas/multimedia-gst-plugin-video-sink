/* GStreamer
 * Copyright (C) 2022 <xuesong.jiang@amlogic.com>
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
#include <unistd.h>
// #endif
// #endif

#ifdef GST_OBJECT_LOCK
#undef GST_OBJECT_LOCK
#define GST_OBJECT_LOCK(obj)                                           \
    {                                                                  \
        GST_TRACE("dbg basesink ctxt lock | aml | locking | %p", obj); \
        g_mutex_lock(GST_OBJECT_GET_LOCK(obj));                        \
        GST_TRACE("dbg basesink ctxt lock | aml | locked  | %p", obj); \
    }
#endif

#ifdef GST_OBJECT_UNLOCK
#undef GST_OBJECT_UNLOCK
#define GST_OBJECT_UNLOCK(obj)                                           \
    {                                                                    \
        GST_TRACE("dbg basesink ctxt lock | aml | unlocking | %p", obj); \
        g_mutex_unlock(GST_OBJECT_GET_LOCK(obj));                        \
        GST_TRACE("dbg basesink ctxt lock | aml | unlocked  | %p", obj); \
    }
#endif

#ifdef GST_BASE_SINK_PREROLL_LOCK
#undef GST_BASE_SINK_PREROLL_LOCK
#define GST_BASE_SINK_PREROLL_LOCK(obj)                                   \
    {                                                                     \
        GST_TRACE("dbg basesink preroll lock | aml | locking | %p", obj); \
        g_mutex_lock(GST_BASE_SINK_GET_PREROLL_LOCK(obj));                \
        GST_TRACE("dbg basesink preroll lock | aml | locked  | %p", obj); \
    }
#endif

#ifdef GST_BASE_SINK_PREROLL_UNLOCK
#undef GST_BASE_SINK_PREROLL_UNLOCK
#define GST_BASE_SINK_PREROLL_UNLOCK(obj)                                   \
    {                                                                       \
        GST_TRACE("dbg basesink preroll lock | aml | unlocking | %p", obj); \
        g_mutex_unlock(GST_BASE_SINK_GET_PREROLL_LOCK(obj));                \
        GST_TRACE("dbg basesink preroll lock | aml | unlocked  | %p", obj); \
    }
#endif

#define GST_IMPORT_LGE_PROP 0

/* signals */
enum
{
    SIGNAL_FIRSTFRAME,
    SIGNAL_UNDERFLOW,
    SIGNAL_DECODEDBUFFER,
    LAST_SIGNAL
};

/* Properties */
enum
{
    PROP_0,
    PROP_FULLSCREEN,
    PROP_SETMUTE,
    PROP_DEFAULT_SYNC,
    PROP_AVSYNC_MODE,
    PROP_VIDEO_FRAME_DROP_NUM,
    PROP_WINDOW_SET,
    PROP_RES_USAGE,
    PROP_DISPLAY_OUTPUT,
    PROP_SHOW_FIRST_FRAME_ASAP,
    PROP_KEEP_LAST_FRAME_ON_FLUSH,
    PROP_ENABLE_USER_RENDERING,
#if GST_IMPORT_LGE_PROP
    PROP_LGE_RESOURCE_INFO,
    PROP_LGE_CURRENT_PTS,
    PROP_LGE_INTERLEAVING_TYPE,
    PROP_LGE_APP_TYPE,
    PROP_LGE_SYNC,
    PROP_LGE_DISH_TRICK,
    PROP_LGE_DISH_TRICK_IGNORE_RATE,
#endif
};

// #define AML_VIDEO_FORMATS                                          \
//     "{ BGRx, BGRA, RGBx, xBGR, xRGB, RGBA, ABGR, ARGB, RGB, BGR, " \
//     "RGB16, BGR16, YUY2, YVYU, UYVY, AYUV, NV12, NV21, NV16, "     \
//     "YUV9, YVU9, Y41B, I420, YV12, Y42B, v308 }"
#define AML_VIDEO_FORMATS "{ NV21 }"

#define GST_CAPS_FEATURE_MEMORY_DMABUF "memory:DMABuf"
#define GST_USE_PLAYBIN 1
#define GST_DEFAULT_AVSYNC_MODE 1 // 0:v master, 1:a master
#define GST_DUMP_STAT_FILENAME "amlvideosink_buf_stat"

#define USE_DMABUF TRUE

#define DRMBP_EXTRA_BUF_SZIE_FOR_DISPLAY 1
#define DRMBP_LIMIT_MAX_BUFSIZE_TO_BUFSIZE 1
#define DRMBP_UNLIMIT_MAX_BUFSIZE 0
#define GST_AML_WAIT_TIME 5000
#define FORMAT_NV21 0x3231564e // this value is used to be same as cobalt

typedef struct _GstAmlVideoSinkWindowSet
{
    gboolean window_change;
    gint x;
    gint y;
    gint w;
    gint h;
} GstAmlVideoSinkWindowSet;

#if GST_IMPORT_LGE_PROP
typedef struct _GstAmlResourceInfo
{
    gchar *coretype;
    gint videoport;
    gint audioport;
    gint maxwidth;
    gint maxheight;
    gint mixerport;
} GstAmlResourceInfo;

typedef struct _GstAmlVideoSinkLgeCtxt
{
    GstAmlResourceInfo res_info;
    guint interleaving_type;
    gchar *app_type;
    gboolean sync;
    gboolean dish_trick;
    gboolean dish_trick_ignore_rate;
} GstAmlVideoSinkLgeCtxt;
#endif

struct _GstAmlVideoSinkPrivate
{
    GstAmlVideoSinkWindowSet window_set;
    GstBuffer *preroll_buffer;
    void *render_device_handle;
    GstVideoInfo video_info;
    gboolean video_info_changed;
    gboolean use_dmabuf;
    gboolean is_flushing;
    gboolean got_eos;
    gint mediasync_instanceid;
    GstSegment segment;

    /* property params */
    gboolean fullscreen;
    gboolean mute;
    gboolean show_first_frame_asap;
    gboolean emitUnderflowSignal;

#if GST_IMPORT_LGE_PROP
    GstAmlVideoSinkLgeCtxt lge_ctxt;
#endif
};

typedef struct bufferInfo
{
   GstAmlVideoSink *sink;
   GstBuffer *buf;
} bufferInfo;

static guint g_signals[LAST_SIGNAL]= {0};

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
static GstStateChangeReturn gst_aml_video_sink_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_aml_video_sink_query(GstElement *element, GstQuery *query);
static gboolean gst_aml_video_sink_propose_allocation(GstBaseSink *bsink, GstQuery *query);
static GstCaps *gst_aml_video_sink_get_caps(GstBaseSink *bsink,
                                            GstCaps *filter);
static gboolean gst_aml_video_sink_set_caps(GstBaseSink *bsink, GstCaps *caps);
static gboolean gst_aml_video_sink_show_frame(GstVideoSink *bsink, GstBuffer *buffer);
static gboolean gst_aml_video_sink_pad_event (GstBaseSink *basesink, GstEvent *event);
static gboolean gst_aml_video_sink_send_event(GstElement *element, GstEvent *event);

/* private interface define */
static gboolean gst_aml_video_sink_check_buf(GstAmlVideoSink *sink, GstBuffer *buffer);
static void gst_aml_video_sink_reset_private(GstAmlVideoSink *sink);
static void gst_render_msg_callback(void *userData, RenderMsgType type, void *msg);
static int gst_render_val_callback(void *userData, int key, void *value);
static gboolean gst_aml_video_sink_tunnel_buf(GstAmlVideoSink *vsink, GstBuffer *gst_buf, RenderBuffer *tunnel_lib_buf_wrap);
static gboolean gst_get_mediasync_instanceid(GstAmlVideoSink *vsink);
#if GST_USE_PLAYBIN
static GstElement *gst_aml_video_sink_find_audio_sink(GstAmlVideoSink *sink);
#endif
static gboolean gst_render_set_params(GstVideoSink *vsink);
static void gst_emit_eos_signal(GstAmlVideoSink *vsink);
static void gst_wait_eos_signal(GstAmlVideoSink *vsink);
static void gst_aml_video_sink_dump_stat(GstAmlVideoSink *sink, const gchar *file_name);
static gpointer eos_detection_thread(gpointer data);

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

    gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_aml_video_sink_change_state);
    gstelement_class->query = GST_DEBUG_FUNCPTR(gst_aml_video_sink_query);
    gstelement_class->send_event = GST_DEBUG_FUNCPTR(gst_aml_video_sink_send_event);

    gstbasesink_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_aml_video_sink_propose_allocation);
    gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR(gst_aml_video_sink_get_caps);
    gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR(gst_aml_video_sink_set_caps);
    gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_aml_video_sink_pad_event);

    gstvideosink_class->show_frame = GST_DEBUG_FUNCPTR(gst_aml_video_sink_show_frame);

    g_object_class_install_property(
        gobject_class, PROP_FULLSCREEN,
        g_param_spec_boolean("fullscreen", "Fullscreen",
                             "Whether the surface should be made fullscreen ",
                             FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_DEFAULT_SYNC,
        g_param_spec_boolean("set-sync", "use basesink avsync",
                             "Whether use basesink sync flow. Configure when make element ",
                             FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_SETMUTE,
        g_param_spec_boolean("set mute", "set mute params",
                             "Whether set screen mute ",
                             FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (
        gobject_class, PROP_KEEP_LAST_FRAME_ON_FLUSH,
        g_param_spec_boolean ("keep-last-frame-on-flush",
                              "set keep last frame on flush or not,default is keep last frame",
                              "0: clean; 1: keep", TRUE, G_PARAM_READWRITE));

    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_AVSYNC_MODE,
        g_param_spec_int("avsync-mode", "avsync mode",
                         "Vmaster(0) Amaster(1) PCRmaster(2) IPTV(3) FreeRun(4)",
                         G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE));

    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_VIDEO_FRAME_DROP_NUM,
        g_param_spec_int("frames-dropped", "frames-dropped",
                         "number of dropped frames",
                         0, G_MAXINT32, 0, G_PARAM_READABLE));

    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_WINDOW_SET,
        g_param_spec_string("rectangle", "rectangle",
                            "Window Set Format: x,y,width,height",
                            NULL, G_PARAM_WRITABLE));

    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_RES_USAGE,
        g_param_spec_int("res-usage", "res-usage",
                         "Flags to indicate intended usage",
                         G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE));

    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_DISPLAY_OUTPUT,
        g_param_spec_int("display-output", "display output index",
                         "display output index, 0 is primary output and default value; 1 is extend display output",
                         G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));

    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_SHOW_FIRST_FRAME_ASAP,
        g_param_spec_boolean("show-first-frame-asap", "show first video frame asap",
                             "Whether showing first video frame asap, default is disable",
                             FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (
        G_OBJECT_CLASS(klass), PROP_ENABLE_USER_RENDERING,
        g_param_spec_boolean ("enable-user-rendering",
                             "enable signal decoded buffer to user to rendering",
                             "0: disable; 1: enable",
                             FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_signals[SIGNAL_FIRSTFRAME]= g_signal_new( "first-video-frame-callback",
                                               G_TYPE_FROM_CLASS(GST_ELEMENT_CLASS(klass)),
                                               (GSignalFlags) (G_SIGNAL_RUN_LAST),
                                               0,    /* class offset */
                                               NULL, /* accumulator */
                                               NULL, /* accu data */
                                               g_cclosure_marshal_VOID__UINT_POINTER,
                                               G_TYPE_NONE,
                                               2,
                                               G_TYPE_UINT,
                                               G_TYPE_POINTER );

    g_signals[SIGNAL_UNDERFLOW]= g_signal_new( "buffer-underflow-callback",
                                              G_TYPE_FROM_CLASS(GST_ELEMENT_CLASS(klass)),
                                              (GSignalFlags) (G_SIGNAL_RUN_LAST),
                                              0,    // class offset
                                              NULL, // accumulator
                                              NULL, // accu data
                                              g_cclosure_marshal_VOID__UINT_POINTER,
                                              G_TYPE_NONE,
                                              2,
                                              G_TYPE_UINT,
                                              G_TYPE_POINTER );
    g_signals[SIGNAL_DECODEDBUFFER]= g_signal_new ("decoded-buffer-callback",
                                              G_TYPE_FROM_CLASS(GST_ELEMENT_CLASS(klass)),
                                              (GSignalFlags) (G_SIGNAL_RUN_FIRST),
                                              0,    /* class offset */
                                              NULL, /* accumulator */
                                              NULL, /* accu data */
                                              NULL,
                                              G_TYPE_NONE,
                                              1,
                                              GST_TYPE_BUFFER);
#if GST_IMPORT_LGE_PROP
    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_LGE_RESOURCE_INFO,
        g_param_spec_object("resource-info", "resource-info",
                            "After acquisition of H/W resources is completed, allocated resource information must be delivered to the decoder and the sink",
                            GST_TYPE_STRUCTURE,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_LGE_CURRENT_PTS,
        g_param_spec_uint64("current-pts", "current pts",
                            "get rendering timing video position",
                            0, G_MAXUINT64,
                            0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_LGE_INTERLEAVING_TYPE,
        g_param_spec_uint("interleaving-type", "interleaving type",
                          "set 3D type",
                          0, G_MAXUINT,
                          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_LGE_APP_TYPE,
        g_param_spec_string("app-type", "app-type",
                            "set application type.",
                            "default_app",
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_LGE_SYNC,
        g_param_spec_boolean("sync", "sync",
                             "M16, H15, K2L",
                             FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_LGE_DISH_TRICK,
        g_param_spec_boolean("dish-trick", "dish trick",
                             "H15, M16",
                             FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        G_OBJECT_CLASS(klass), PROP_LGE_DISH_TRICK_IGNORE_RATE,
        g_param_spec_boolean("dish-trick-ignore-rate", "dish trick ignore rate",
                             "H15, M16",
                             FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif
}

static void gst_aml_video_sink_init(GstAmlVideoSink *sink)
{
    GstBaseSink *basesink = (GstBaseSink *)sink;

    sink->last_displayed_buf_pts = 0;
    sink->last_dec_buf_pts = GST_CLOCK_TIME_NONE;
    /* init eos detect */
    sink->queued = 0;
    sink->dequeued = 0;
    sink->rendered = 0;
    sink->droped = 0;
    sink->avsync_mode = GST_DEFAULT_AVSYNC_MODE;
    sink->default_sync = FALSE;
    sink->pip_mode = 0;
    sink->display_output_index = 0;
    sink->secure_mode = FALSE;
    sink->eos_detect_thread_handle = NULL;
    sink->quit_eos_detect_thread = FALSE;
    sink->frame_rate_num = 0;
    sink->frame_rate_denom = 0;
    sink->frame_rate_changed = FALSE;
    sink->frame_rate = 0.0;
    sink->pixel_aspect_ratio_changed = FALSE;
    sink->pixel_aspect_ratio = 1.0;
    sink->keep_last_frame_on_flush = TRUE;
    //sink->enable_decoded_buffer_signal = FALSE;
    g_mutex_init(&sink->eos_lock);
    g_cond_init(&sink->eos_cond);

    GST_AML_VIDEO_SINK_GET_PRIVATE(sink) = malloc(sizeof(GstAmlVideoSinkPrivate));
    gst_aml_video_sink_reset_private(sink);
    gst_base_sink_set_sync(basesink, FALSE);

    gst_base_sink_set_qos_enabled(basesink, FALSE);
    gst_base_sink_set_last_sample_enabled(basesink, FALSE);
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
    case PROP_DEFAULT_SYNC:
        GST_OBJECT_LOCK(sink);
        g_value_set_boolean(value, sink->default_sync);
        GST_OBJECT_UNLOCK(sink);
        break;
    case PROP_AVSYNC_MODE:
        GST_OBJECT_LOCK(sink);
        g_value_set_boolean(value, sink->avsync_mode);
        GST_OBJECT_UNLOCK(sink);
        break;
    case PROP_VIDEO_FRAME_DROP_NUM:
        GST_OBJECT_LOCK(sink);
        GST_DEBUG_OBJECT(sink, "app get frame drop num | queued:%d, dequeued:%d, droped:%d, rendered:%d",
                         sink->queued, sink->dequeued, sink->droped, sink->rendered);
        g_value_set_int(value, sink->droped);
        GST_OBJECT_UNLOCK(sink);
        break;
    case PROP_DISPLAY_OUTPUT:
    {
        GST_OBJECT_LOCK(sink);
        g_value_set_int(value, sink->display_output_index);
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case PROP_KEEP_LAST_FRAME_ON_FLUSH:
    {
        GST_OBJECT_LOCK(sink);
        g_value_set_boolean(value, sink->keep_last_frame_on_flush);
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case PROP_ENABLE_USER_RENDERING:
    {
        GST_OBJECT_LOCK(sink);
        g_value_set_boolean(value, sink->enable_decoded_buffer_signal);
        GST_OBJECT_UNLOCK(sink);
        break;
    }

#if GST_IMPORT_LGE_PROP
    case PROP_LGE_CURRENT_PTS:
    {
        GST_OBJECT_LOCK(sink);
        g_value_set_uint64(value, sink->last_displayed_buf_pts);
        GST_OBJECT_UNLOCK(sink);
        break;
    }
#endif
    default:
        // G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
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
            // TODO set full screen to tunnel lib
        }
        GST_OBJECT_UNLOCK(sink);
        break;
    case PROP_SETMUTE:
        GST_OBJECT_LOCK(sink);
        gboolean is_mute = g_value_get_boolean(value);
        if (sink_priv->mute != is_mute)
        {
            sink_priv->mute = is_mute;
            // TODO set full screen to tunnel lib
        }
        GST_OBJECT_UNLOCK(sink);
        break;
    case PROP_DEFAULT_SYNC:
    {
        GST_OBJECT_LOCK(sink);
        sink->default_sync = g_value_get_boolean(value);
        GST_OBJECT_UNLOCK(sink);
        gst_base_sink_set_sync(GST_BASE_SINK(sink), sink->default_sync);
        GST_DEBUG_OBJECT(sink, "use basessink avsync flow %d", sink->default_sync);
        break;
    }
    case PROP_AVSYNC_MODE:
        GST_OBJECT_LOCK(sink);
        gint mode = g_value_get_int(value);
        if (mode >= 0)
        {
            sink->avsync_mode = mode;
            GST_DEBUG_OBJECT(sink, "AV sync mode %d", mode);
        }
        GST_OBJECT_UNLOCK(sink);
        break;
    case PROP_WINDOW_SET:
    {
        const gchar *str = g_value_get_string(value);
        gchar **parts = g_strsplit(str, ",", 4);

        if (!parts[0] || !parts[1] || !parts[2] || !parts[3])
        {
            GST_ERROR("Bad window properties string");
        }
        else
        {
            int nx, ny, nw, nh;
            nx = atoi(parts[0]);
            ny = atoi(parts[1]);
            nw = atoi(parts[2]);
            nh = atoi(parts[3]);

            if ((nx != sink_priv->window_set.x) ||
                (ny != sink_priv->window_set.y) ||
                (nw != sink_priv->window_set.w) ||
                (nh != sink_priv->window_set.h))
            {
                GST_OBJECT_LOCK(sink);
                sink_priv->window_set.window_change = true;
                sink_priv->window_set.x = nx;
                sink_priv->window_set.y = ny;
                sink_priv->window_set.w = nw;
                sink_priv->window_set.h = nh;

                GST_DEBUG("set window rect (%d,%d,%d,%d)\n",
                          sink_priv->window_set.x,
                          sink_priv->window_set.y,
                          sink_priv->window_set.w,
                          sink_priv->window_set.h);
                GST_OBJECT_UNLOCK(sink);
            }
        }

        g_strfreev(parts);
        break;
    }
    case PROP_RES_USAGE:
    {
        GST_OBJECT_LOCK(sink);
        sink->pip_mode = 1;
        GST_DEBUG_OBJECT(sink, "play video in sub layer(pip)");
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case PROP_DISPLAY_OUTPUT:
    {
        GST_OBJECT_LOCK(sink);
        gint index = g_value_get_int(value);
        if (index == 0 || index == 1)
        {
            sink->display_output_index = index;
            if (sink_priv->render_device_handle)
            {
                if (render_set_value(sink_priv->render_device_handle, KEY_SELECT_DISPLAY_OUTPUT, &sink->display_output_index) == -1)
                    GST_ERROR_OBJECT(sink, "render lib update output index error");
            }
        }
        GST_DEBUG_OBJECT(sink, "update display output index to:%d", sink->display_output_index);
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case PROP_SHOW_FIRST_FRAME_ASAP:
    {
        sink_priv->show_first_frame_asap = g_value_get_boolean(value);
        GST_DEBUG_OBJECT(sink, "set show first frame asap %d",sink_priv->show_first_frame_asap);
        break;
    }
    case PROP_KEEP_LAST_FRAME_ON_FLUSH:
    {
        GST_OBJECT_LOCK(sink);
        sink->keep_last_frame_on_flush = g_value_get_boolean(value);
        GST_DEBUG_OBJECT(sink, "keep last frame on flush %d", sink->keep_last_frame_on_flush);
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case PROP_ENABLE_USER_RENDERING:
    {
        sink->enable_decoded_buffer_signal = g_value_get_boolean(value);
        GST_DEBUG("set enable decoded buffer signal %d", sink->enable_decoded_buffer_signal);
    }
#if GST_IMPORT_LGE_PROP
    case PROP_LGE_RESOURCE_INFO:
    {
        GST_OBJECT_LOCK(sink);
        GstStructure *r_info = g_value_get_object(value);
        if (r_info)
        {
            if (gst_structure_has_field(r_info, "coretype"))
            {
                if (sink_priv->lge_ctxt.res_info.coretype)
                {
                    g_free(sink_priv->lge_ctxt.res_info.coretype);
                    sink_priv->lge_ctxt.res_info.coretype = NULL;
                }
                sink_priv->lge_ctxt.res_info.coretype = g_strdup(gst_structure_get_string(r_info, "coretype"));
            }
            if (gst_structure_has_field(r_info, "videoport"))
                gst_structure_get_int(r_info, "videoport", &(sink_priv->lge_ctxt.res_info.videoport));
            if (gst_structure_has_field(r_info, "audioport"))
                gst_structure_get_int(r_info, "audioport", &(sink_priv->lge_ctxt.res_info.audioport));
            if (gst_structure_has_field(r_info, "maxwidth"))
                gst_structure_get_int(r_info, "maxwidth", &(sink_priv->lge_ctxt.res_info.maxwidth));
            if (gst_structure_has_field(r_info, "maxheight"))
                gst_structure_get_int(r_info, "maxheight", &(sink_priv->lge_ctxt.res_info.maxheight));
            if (gst_structure_has_field(r_info, "mixerport"))
                gst_structure_get_int(r_info, "mixerport", &(sink_priv->lge_ctxt.res_info.mixerport));
        }
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case PROP_LGE_INTERLEAVING_TYPE:
    {
        GST_OBJECT_LOCK(sink);
        guint interlv_type = g_value_get_uint(value);
        sink_priv->lge_ctxt.interleaving_type = interlv_type;
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case PROP_LGE_APP_TYPE:
    {
        GST_OBJECT_LOCK(sink);
        GST_DEBUG_OBJECT(sink, "LGE up layer set app type");
        if (sink_priv->lge_ctxt.app_type)
            g_free(sink_priv->lge_ctxt.app_type);
        sink_priv->lge_ctxt.app_type = g_strdup(g_value_get_string(value));
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case PROP_LGE_SYNC:
    {
        GST_OBJECT_LOCK(sink);
        sink_priv->lge_ctxt.sync = g_value_get_boolean(value);
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case PROP_LGE_DISH_TRICK:
    {
        GST_OBJECT_LOCK(sink);
        sink_priv->lge_ctxt.dish_trick = g_value_get_boolean(value);
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case PROP_LGE_DISH_TRICK_IGNORE_RATE:
    {
        GST_OBJECT_LOCK(sink);
        sink_priv->lge_ctxt.dish_trick_ignore_rate = g_value_get_boolean(value);
        GST_OBJECT_UNLOCK(sink);
        break;
    }
#endif
    default:
        // G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_aml_video_sink_finalize(GObject *object)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(object);

    GST_DEBUG_OBJECT(sink, "Finalizing aml video sink..");

    g_mutex_clear(&sink->eos_lock);
    g_cond_clear(&sink->eos_cond);

    gst_aml_video_sink_reset_private(sink);
    if (GST_AML_VIDEO_SINK_GET_PRIVATE(sink))
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
    GstState state, next;

    state = (GstState)GST_STATE_TRANSITION_CURRENT(transition);
    next = GST_STATE_TRANSITION_NEXT(transition);
    GST_DEBUG_OBJECT(sink,
                   "amlvideosink handler tries setting state from %s to %s (%04x)",
                   gst_element_state_get_name(state),
                   gst_element_state_get_name(next), transition);

    GST_OBJECT_LOCK(sink);
    switch (transition)
    {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
        //set env to enable essos
        setenv("ENABLE_WST_ESSOS","1",1);
        if (!sink_priv->show_first_frame_asap) {
            setenv("vendor_mediasync_show_firstframe_nosync", "0", 0);
        }
        sink_priv->render_device_handle = render_open();
        if (sink_priv->render_device_handle == NULL)
        {
            GST_ERROR_OBJECT(sink, "render lib: open device fail");
            goto error;
        }
        RenderCallback cb = {gst_render_msg_callback, gst_render_val_callback};
        render_set_callback(sink_priv->render_device_handle, sink,  &cb);

        GST_DEBUG_OBJECT(sink, "tunnel lib: set pip mode %d",sink->pip_mode);
        int pip = 1;
        if (sink->pip_mode && render_set_value(sink_priv->render_device_handle, KEY_VIDEO_PIP, &pip) == -1)
        {
            GST_ERROR_OBJECT(sink, "tunnel lib: set pip error");
            goto error;
        }
        //check if showing first frame no sync
        if (sink_priv->show_first_frame_asap) {
            int show_frame_asap = 1;
            render_set_value(sink_priv->render_device_handle, KEY_SHOW_FRIST_FRAME_NOSYNC, &show_frame_asap);
        }

        GST_DEBUG_OBJECT(sink, "set qos fail");
        gst_base_sink_set_qos_enabled((GstBaseSink *)sink, FALSE);

        break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {

        if (render_set_value(sink_priv->render_device_handle, KEY_SELECT_DISPLAY_OUTPUT, &sink->display_output_index) == -1)
        {
            GST_ERROR_OBJECT(sink, "render lib first set output index error");
            goto error;
        }

        if (render_connect(sink_priv->render_device_handle) == -1)
        {
            GST_ERROR_OBJECT(sink, "render lib connect device fail");
            goto error;
        }

        if (render_set_value(sink_priv->render_device_handle, KEY_IMMEDIATELY_OUTPUT, &sink->default_sync) == -1)
        {
            GST_ERROR_OBJECT(sink, "render lib set immediately output error");
            goto error;
        }

        if (render_pause(sink_priv->render_device_handle) == -1)
        {
            GST_ERROR_OBJECT(sink, "render lib pause device fail when first into paused state");
            goto error;
        }
        break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
        sink->video_playing = TRUE;
        if (render_resume(sink_priv->render_device_handle) == -1)
        {
            GST_ERROR_OBJECT(sink, "render lib resume device fail");
            goto error;
        }
        break;
    }
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    {
        sink->video_playing = FALSE;
        if (gst_base_sink_is_async_enabled(GST_BASE_SINK(sink)))
        {
            GST_OBJECT_UNLOCK(sink);
            GstBaseSink *basesink;
            basesink = GST_BASE_SINK(sink);
            //GST_BASE_SINK_PREROLL_LOCK(basesink);
            basesink->have_preroll = 1;
            //GST_BASE_SINK_PREROLL_UNLOCK(basesink);
            GST_OBJECT_LOCK(sink);
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
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    {
        // GstBaseSink *basesink;
        // basesink = GST_BASE_SINK(sink);

        if (render_pause(sink_priv->render_device_handle) == -1)
        {
            GST_ERROR_OBJECT(sink, "render lib pause device fail");
            goto error;
        }

        // GST_BASE_SINK_PREROLL_LOCK(basesink);
        // basesink->have_preroll = 1;
        // GST_BASE_SINK_PREROLL_UNLOCK(basesink);
        break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
        if ( sink->eos_detect_thread_handle )
        {
            sink->quit_eos_detect_thread = TRUE;
            g_thread_join(sink->eos_detect_thread_handle);
            sink->eos_detect_thread_handle = NULL;
        }
        GST_DEBUG_OBJECT(sink, "before disconnect rlib");
        render_disconnect(sink_priv->render_device_handle);
        GST_DEBUG_OBJECT(sink, "after disconnect rlib");
        GST_DEBUG_OBJECT(sink, "buf stat | queued:%d, dequeued:%d, droped:%d, rendered:%d",
                         sink->queued, sink->dequeued, sink->droped, sink->rendered);
        break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
        if (sink_priv->render_device_handle)
        {
            render_close(sink_priv->render_device_handle);
        }
        gst_aml_video_sink_reset_private(sink);
        //set env to invalid essos
        setenv("ENABLE_WST_ESSOS","0",1);

        break;
    }
    default:
        break;
    }

    //gst_aml_video_sink_dump_stat(sink, GST_DUMP_STAT_FILENAME);

    GST_OBJECT_UNLOCK(sink);
    GST_DEBUG_OBJECT(sink, "done");
    return ret;

error:
    GST_OBJECT_UNLOCK(sink);
    ret = GST_STATE_CHANGE_FAILURE;
    return ret;
}

static gboolean gst_aml_video_sink_query(GstElement *element, GstQuery *query)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(element);

    switch (GST_QUERY_TYPE(query))
    {
    case GST_QUERY_POSITION:
    {
        GstFormat format;
        gst_query_parse_position(query, &format, NULL);
        if (GST_FORMAT_BYTES == format)
        {
            return GST_ELEMENT_CLASS(parent_class)->query(element, query);
        }
        else
        {
            GST_OBJECT_LOCK(sink);
            gint64 position = sink->last_displayed_buf_pts;
            // gint64 position = sink->last_dec_buf_pts;
            GST_OBJECT_UNLOCK(sink);
            GST_DEBUG_OBJECT(sink, "got position: %" GST_TIME_FORMAT, GST_TIME_ARGS(position));
            gst_query_set_position(query, GST_FORMAT_TIME, position);
            return TRUE;
        }
        break;
    }
    default:
        return GST_ELEMENT_CLASS(parent_class)->query(element, query);
    }
}

static gboolean gst_aml_video_sink_propose_allocation(GstBaseSink *bsink, GstQuery *query)
{
    GST_DEBUG_OBJECT(bsink, "trace in");
    // TODO only implement dma case
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(bsink);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);

    GstCaps *caps;
    GstBufferPool *pool = NULL;
    gboolean need_pool;

    gst_query_parse_allocation(query, &caps, &need_pool);
    GST_DEBUG_OBJECT(bsink, "need_pool: %d, secure_mode: %d", need_pool, sink->secure_mode);

    if (need_pool)
        pool = gst_drm_bufferpool_new(sink->secure_mode, GST_DRM_BUFFERPOOL_TYPE_VIDEO_PLANE);

    // Do not store the last received sample if it is secure_mode
    if (TRUE == sink->secure_mode)
        gst_base_sink_set_last_sample_enabled(bsink, FALSE);

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

    if (filter)
    {
        GstCaps *intersection;

        intersection =
            gst_caps_intersect_full(filter, caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(caps);
        caps = intersection;
    }
    GST_DEBUG_OBJECT(sink, "filter caps: %" GST_PTR_FORMAT, filter);
    GST_DEBUG_OBJECT(sink, "final  caps: %" GST_PTR_FORMAT, caps);

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

    // #if GST_IMPORT_LGE_PROP
    //     GstMessage *message;
    //     GstStructure *s;
    //     s = gst_structure_new("media-info",
    //                           "type", "G_TYPE_INT", 1,
    //                           "mime-type", G_TYPE_STRING, sink_priv->video_info.finfo->name,
    //                           "width", G_TYPE_INT, sink_priv->video_info.width,
    //                           "height", "G_TYPE_INT", sink_priv->video_info.height,
    //                           NULL);
    //     message = gst_message_new_custom(GST_MESSAGE_APPLICATION, GST_OBJECT(sink), s);
    //     gst_element_post_message(GST_ELEMENT_CAST(sink), message);
    // #endif

    sink_priv->video_info_changed = TRUE;

done:
    GST_OBJECT_UNLOCK(sink);
    return ret;
}


static void gst_show_vr_cb(gpointer data)
{
    bufferInfo *binfo = (bufferInfo*)data;
    GstAmlVideoSink *sink = binfo->sink;
    GST_DEBUG("unref vr360 buffer %p ",binfo->buf);
    sink->rendered++;
    if (binfo->buf)
        gst_buffer_unref(binfo->buf);
    free(binfo);
}

static gboolean gst_aml_video_sink_show_vr_frame(GstAmlVideoSink *sink, GstBuffer *buffer)
{
    bufferInfo *binfo;
    GstStructure *s;
    GstBuffer *buf;
    gint fd0 = -1, fd1 = -1, fd2 = -1;
    gint stride0, stride1, stride2;
    GstVideoMeta *vmeta = NULL;
    GST_DEBUG("pts: %lld   %p", GST_BUFFER_PTS (buffer),buffer);

    binfo= (bufferInfo*)malloc( sizeof(bufferInfo) );
    if (binfo)
    {
        int offset1= 0, offset2 = 0;
        guint n_mem = 0;
        GstMemory *dma_mem0 = NULL;
        GstMemory *dma_mem1 = NULL;
        GstMemory *dma_mem2 = NULL;
        gst_buffer_ref(buffer);
        binfo->sink= sink;
        binfo->buf = buffer;
        n_mem = gst_buffer_n_memory(buffer);
        vmeta = gst_buffer_get_video_meta(buffer);
        if (vmeta == NULL)
        {
            GST_ERROR_OBJECT(sink, "not found video meta info");
            gst_buffer_unref(binfo->buf);
            free(binfo);
            return FALSE;
        }
        GST_DEBUG("height:%d,width:%d,n_mem:%d",vmeta->height,vmeta->width,n_mem);

        if (n_mem > 1)
        {
            dma_mem0 = gst_buffer_peek_memory(buffer, 0);
            dma_mem1 = gst_buffer_peek_memory(buffer, 1);
            fd0 = gst_dmabuf_memory_get_fd(dma_mem0);
            fd1 = gst_dmabuf_memory_get_fd(dma_mem1);
            GST_DEBUG("fd0:%d,fd1:%d,fd2:%d",fd0,fd1,fd2);
            stride0 = vmeta->stride[0];
            stride1 = vmeta->stride[1];
            stride2 = 0;
            if ( fd1 < 0 )
            {
                stride1= stride0;
                offset1= stride0*vmeta->height;
            }
            if ( fd2 < 0 )
            {
                offset2= offset1+(vmeta->width*vmeta->height)/2;
                stride2= stride0;
            }
            GST_DEBUG("stride0:%d,stride1:%d,stride2:%d",stride0,stride1,stride2);
            GST_DEBUG("offset0:%d,offset1:%d,offset2:%d",offset2,offset2,offset2);

        }
        else
        {
            GST_DEBUG("single plane");
            dma_mem0 = gst_buffer_peek_memory(buffer, 0);
            fd0 = gst_dmabuf_memory_get_fd(dma_mem0);
            stride0 = vmeta->stride[0];
            offset1= stride0*vmeta->height;
        }
    }
    s = gst_structure_new ("drmbuffer_info",
          "fd0", G_TYPE_INT, fd0,
          "fd1", G_TYPE_INT, fd1,
          "fd2", G_TYPE_INT, fd2,
          "stride0", G_TYPE_INT, stride0,
          "stride1", G_TYPE_INT, stride1,
          "stride2", G_TYPE_INT, stride2,
          "width", G_TYPE_INT, vmeta->width,
          "height", G_TYPE_INT, vmeta->height,
          "format", G_TYPE_INT, FORMAT_NV21,
          NULL);
    GST_DEBUG("structure: %" GST_PTR_FORMAT,s);

    buf = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
        (gpointer) binfo, sizeof(bufferInfo), 0, sizeof(bufferInfo),
        (gpointer) binfo, gst_show_vr_cb);
    if (!buf)
    {
        GST_ERROR_OBJECT(sink, "new buffer fail!");
        gst_buffer_unref(buf);
        gst_buffer_unref(binfo->buf);
        free(binfo);
        return FALSE;;
    }
    gst_buffer_add_protection_meta(buf, s);
    GST_BUFFER_PTS (buf) = GST_BUFFER_PTS(buffer);
    GST_DEBUG("pts: %lld, %p", GST_BUFFER_PTS (buf),buf);
    g_signal_emit (G_OBJECT(sink),g_signals[SIGNAL_DECODEDBUFFER],0,buf);
    gst_buffer_unref(buf);
    return TRUE;

}


static GstFlowReturn gst_aml_video_sink_show_frame(GstVideoSink *vsink, GstBuffer *buffer)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(vsink);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
    GstFlowReturn ret = GST_FLOW_OK;
    RenderBuffer *tunnel_lib_buf_wrap = NULL;

    GST_LOG_OBJECT(sink, "revice buffer:%p (start: %" GST_TIME_FORMAT ", end: %" GST_TIME_FORMAT "), from pool:%p, need_preroll:%d",
                   buffer, GST_TIME_ARGS(GST_BUFFER_PTS(buffer)), GST_TIME_ARGS(GST_BUFFER_DURATION(buffer)),
                   buffer->pool, ((GstBaseSink *)sink)->need_preroll);

    GST_OBJECT_LOCK(vsink);
    sink->last_dec_buf_pts = GST_BUFFER_PTS(buffer);
    GST_DEBUG_OBJECT(sink, "set last_dec_buf_pts %" GST_TIME_FORMAT, GST_TIME_ARGS(sink->last_dec_buf_pts));

    if (!sink_priv->render_device_handle)
    {
        GST_ERROR_OBJECT(sink, "flow error, render_device_handle == NULL");
        goto error;
    }

    if (sink_priv->preroll_buffer && sink_priv->preroll_buffer == buffer)
    {
        GST_LOG_OBJECT(sink, "get preroll buffer:%p 2nd time, goto ret", buffer);
        sink_priv->preroll_buffer = NULL;
        goto ret;
    }
    if (G_UNLIKELY(((GstBaseSink *)sink)->need_preroll))
    {
        GST_LOG_OBJECT(sink, "get preroll buffer 1st time, buf:%p", buffer);
        sink_priv->preroll_buffer = buffer;
    }
    GST_INFO_OBJECT(sink, "sink->enable_decoded_buffer_signal:%d",
        sink->enable_decoded_buffer_signal);

    if (sink->enable_decoded_buffer_signal)
    {
        if (!gst_aml_video_sink_check_buf(sink, buffer))
        {
            GST_ERROR_OBJECT(sink, "buf out of segment return");
            goto ret;
        }

        if (!gst_aml_video_sink_show_vr_frame(sink,buffer))
        {
            GST_ERROR_OBJECT(sink, "can't play vr360 file!");
            goto ret;
        }
        sink->queued++;
        sink->quit_eos_detect_thread = FALSE;
        if (sink->eos_detect_thread_handle == NULL )
        {
            GST_DEBUG_OBJECT(sink, "start eos detect thread");
            sink->eos_detect_thread_handle = g_thread_new("video_sink_eos", eos_detection_thread, sink);
        }
        goto ret;
    }

    if (!gst_aml_video_sink_check_buf(sink, buffer))
    {
        GST_ERROR_OBJECT(sink, "buf out of segment return");
        goto ret;
    }

    if (sink_priv->window_set.window_change)
    {
        RenderRect window_size = {sink_priv->window_set.x, sink_priv->window_set.y, sink_priv->window_set.w, sink_priv->window_set.h};
        if (render_set_value(sink_priv->render_device_handle, KEY_WINDOW_SIZE, &window_size) == -1)
        {
            GST_ERROR_OBJECT(vsink, "tunnel lib: set window size error");
            return FALSE;
        }
        sink_priv->window_set.window_change = FALSE;

        GST_DEBUG_OBJECT(sink, "tunnel lib: set window size to %d,%d,%d,%d",
                         sink_priv->window_set.x,
                         sink_priv->window_set.y,
                         sink_priv->window_set.w,
                         sink_priv->window_set.h);
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

    if (sink->frame_rate_changed) {
        sink->frame_rate_changed = FALSE;
        RenderFraction frame_rate_fraction;
        frame_rate_fraction.num = sink->frame_rate_num;
        frame_rate_fraction.denom = sink->frame_rate_denom;
        render_set_value(sink_priv->render_device_handle, KEY_VIDEO_FRAME_RATE, &frame_rate_fraction);
    }

    if (sink->pixel_aspect_ratio_changed)
    {
        sink->pixel_aspect_ratio_changed = FALSE;
        render_set_value(sink_priv->render_device_handle, KEY_PIXEL_ASPECT_RATIO, &sink->pixel_aspect_ratio);
    }



    tunnel_lib_buf_wrap = render_allocate_render_buffer_wrap(sink_priv->render_device_handle, BUFFER_FLAG_DMA_BUFFER);
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
    GST_OBJECT_UNLOCK(vsink);
    if (render_display_frame(sink_priv->render_device_handle, tunnel_lib_buf_wrap) == -1)
    {
        GST_ERROR_OBJECT(sink, "render lib: display frame fail");
        return GST_FLOW_CUSTOM_ERROR_2;
    }

    GST_OBJECT_LOCK(vsink);
    sink->queued++;
    if (sink_priv->is_flushing)
    {
        if (render_flush(sink_priv->render_device_handle) == 0)
        {
            GST_DEBUG_OBJECT(sink, "in flushing flow, release the buffer directly");
            goto flushing;
        }
        else
        {
            GST_ERROR_OBJECT(sink, "render lib: flush error");
            goto error;
        }
    }

    //gst_aml_video_sink_dump_stat(sink, GST_DUMP_STAT_FILENAME);
    GST_DEBUG_OBJECT(sink, "GstBuffer:%p, pts: %" GST_TIME_FORMAT " queued ok, queued:%d", buffer, GST_TIME_ARGS(GST_BUFFER_PTS(buffer)), sink->queued);
    sink->quit_eos_detect_thread = FALSE;
    if (sink->eos_detect_thread_handle == NULL )
    {
        GST_DEBUG_OBJECT(sink, "start eos detect thread");
        sink->eos_detect_thread_handle = g_thread_new("video_sink_eos", eos_detection_thread, sink);
    }
    goto ret;

error:
    GST_DEBUG_OBJECT(sink, "GstBuffer:%p queued error", buffer);
    ret = GST_FLOW_CUSTOM_ERROR_2;
    goto ret;
flushing:
    GST_DEBUG_OBJECT(sink, "flushing when buf:%p", buffer);
    ret = GST_FLOW_FLUSHING;
    goto ret;
ret:
    GST_OBJECT_UNLOCK(vsink);
    return ret;
}

static gboolean gst_aml_video_sink_pad_event (GstBaseSink *basesink, GstEvent *event)
{
    gboolean result = TRUE;
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(basesink);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);

    GST_DEBUG_OBJECT(sink, "received event %p %" GST_PTR_FORMAT, event, event);

    switch (GST_EVENT_TYPE(event))
    {
    case GST_EVENT_FLUSH_START:
    {
        GST_INFO_OBJECT(sink, "flush start");
        GST_OBJECT_LOCK(sink);
        sink_priv->is_flushing = TRUE;
        render_set_value(sink_priv->render_device_handle, KEY_KEEP_LAST_FRAME_ON_FLUSH, &sink->keep_last_frame_on_flush);

        if (render_flush(sink_priv->render_device_handle) == 0)
        {
            GST_INFO_OBJECT(sink, "recv flush start and set render lib flushing succ");
        }
        GST_INFO_OBJECT(sink, "flush start done");
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
        GST_INFO_OBJECT(sink, "flush stop");
        GST_DEBUG_OBJECT(sink, "flushing need waitting display render all buf, queued:%d, rendered:%d, droped:%d",
                         sink->queued,
                         sink->rendered,
                         sink->droped);

        GST_OBJECT_LOCK(sink);
        GST_INFO_OBJECT(sink, "flush all count num to zero");
        sink->queued = 0;
        sink->dequeued = 0;
        sink->rendered = 0;
        sink->droped = 0;
        sink_priv->got_eos = FALSE;
        sink_priv->is_flushing = FALSE;
        GST_INFO_OBJECT(sink, "flush stop done");
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case GST_EVENT_SEGMENT:
    {
        GST_OBJECT_LOCK(sink);
        gst_event_copy_segment(event, &sink_priv->segment);
        GST_INFO_OBJECT(sink, "configured segment %" GST_SEGMENT_FORMAT, &sink_priv->segment);
        sink->last_displayed_buf_pts = sink_priv->segment.position;
        GST_INFO_OBJECT(sink, "update cur pos to %" GST_TIME_FORMAT, GST_TIME_ARGS(sink->last_displayed_buf_pts));
        GST_OBJECT_UNLOCK(sink);
        break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_STICKY:
    {
        if (gst_event_has_name(event, "IS_SVP"))
        {
            GST_OBJECT_LOCK(sink);
            GST_DEBUG_OBJECT(sink, "Got SVP Event");
            sink->secure_mode = TRUE;
            GST_OBJECT_UNLOCK(sink);
        }

        gst_event_unref(event);
        return result;
    }
    case GST_EVENT_EOS:
    {
        GST_OBJECT_LOCK(sink);
        sink_priv->got_eos = TRUE;
        GST_OBJECT_UNLOCK(sink);

        //some frames aren't displayed,so don't pass this event to basesink
        if (sink->queued > sink->rendered + sink->droped)
        {
            GST_DEBUG_OBJECT(sink, "display render all buf, queued:%d, rendered:%d, droped:%d",
                             sink->queued,
                             sink->rendered,
                             sink->droped);
            gst_event_unref(event);
            return result;
        }
        break;
    }
    case GST_EVENT_CAPS:
    {
        GstCaps *caps;
        GstStructure *structure;
        gst_event_parse_caps(event, &caps);

        structure= gst_caps_get_structure(caps, 0);
        if (structure)
        {
            gint num, denom;
            if (gst_structure_get_fraction( structure, "framerate", &num, &denom ))
            {
                GST_DEBUG_OBJECT(sink, "framerate num:%d,denom:%d",num,denom);
                if ( denom == 0 ) denom= 1;
                sink->frame_rate= (double)num/(double)denom;
                if ( sink->frame_rate <= 0.0 )
                {
                    GST_DEBUG_OBJECT(sink, "caps have framerate of 0 - assume 60");
                    sink->frame_rate= 60.0;
                }
                if (sink->frame_rate_num != num || sink->frame_rate_denom != denom) {
                    sink->frame_rate_num = num;
                    sink->frame_rate_denom = denom;
                    sink->frame_rate_changed = TRUE;
                }

                if ( gst_structure_get_fraction( structure, "pixel-aspect-ratio", &num, &denom ) )
                {
                    if ( (num <= 0) || (denom <= 0))
                    {
                        num = denom = 1;
                    }
                    sink->pixel_aspect_ratio = (double)num/(double)denom;
                    sink->pixel_aspect_ratio_changed = TRUE;
                }
            }
        }
    } break;
    default:
    {
        break;
    }
    }

    GST_DEBUG_OBJECT(sink, "pass to basesink");
    result = GST_BASE_SINK_CLASS(parent_class)->event((GstBaseSink *)sink, event);
    GST_DEBUG_OBJECT(sink, "done");
    return result;
}

static gboolean gst_aml_video_sink_send_event(GstElement *element, GstEvent *event)
{
    GstPad *pad = NULL;
    GstBaseSink *basesink = GST_BASE_SINK(element);
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(element);
    GstAmlVideoSinkClass *sink_class = GST_AML_VIDEO_SINK_GET_CLASS(sink);
    GstVideoSinkClass *sink_p_class = parent_class;
    GstBaseSinkClass *sink_pp_class = g_type_class_peek_parent(sink_p_class);
    gboolean result = TRUE;
    GstPadMode mode = GST_PAD_MODE_NONE;

    GST_DEBUG_OBJECT(sink, "amlvideosink_class:%p, videosink_class:%p, basesink_class:%p", sink_class, sink_p_class, sink_pp_class);
    GST_DEBUG_OBJECT(sink, "handling event %p %" GST_PTR_FORMAT, event, event);

    switch (GST_EVENT_TYPE(event))
    {
    case GST_EVENT_SEEK:
    {
        GST_OBJECT_LOCK(element);
        /* get the pad and the scheduling mode */
        pad = gst_object_ref(basesink->sinkpad);
        mode = basesink->pad_mode;
        GST_OBJECT_UNLOCK(element);

        if (mode == GST_PAD_MODE_PUSH)
        {
            GST_BASE_SINK_PREROLL_LOCK(basesink);
            if (GST_BASE_SINK(sink)->need_preroll && GST_BASE_SINK(sink)->have_preroll)
            {
                GST_DEBUG_OBJECT(sink, "reset preroll when recived seek event");
                GST_BASE_SINK(sink)->need_preroll = FALSE;
                GST_BASE_SINK(sink)->have_preroll = FALSE;
                GST_BASE_SINK_PREROLL_SIGNAL(basesink);
            }
            GST_BASE_SINK_PREROLL_UNLOCK(basesink);
        }

        gst_object_unref(pad);
        break;
    }
    default:
        break;
    }

    if (GST_ELEMENT_CLASS(sink_pp_class)->send_event)
    {
        GST_DEBUG_OBJECT(sink, "use basesink class send event func handle event");
        result = GST_ELEMENT_CLASS(sink_pp_class)->send_event(element, event);
    }
    else
    {
        GST_ERROR_OBJECT(sink, "can't find basesink send event func");
        result = FALSE;
    }

    GST_DEBUG_OBJECT(sink, "handled event: %d", result);

    return result;
}

/* private interface definition */
static gboolean gst_aml_video_sink_check_buf(GstAmlVideoSink *sink, GstBuffer *buf)
{
    gboolean ret = TRUE;
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
    guint64 start, stop;
    guint64 cstart, cstop;
    GstSegment *segment;
    GstClockTime duration;

    /* Check for clipping */
    start = GST_BUFFER_PTS(buf);
    duration = GST_BUFFER_DURATION(buf);
    stop = GST_CLOCK_TIME_NONE;

    if (GST_CLOCK_TIME_IS_VALID (start) && GST_CLOCK_TIME_IS_VALID (duration))
    {
        stop = start + duration;
    }
    else if (GST_CLOCK_TIME_IS_VALID (start) && !GST_CLOCK_TIME_IS_VALID (duration))
    {
        if (sink_priv->video_info.fps_n != 0)
        {
            //TODO calc with framerate
            stop = start + 40 * GST_MSECOND;
        }
        else
        {
            /* If we don't clip away buffers that far before the segment we
            * can cause the pipeline to lockup. This can happen if audio is
            * properly clipped, and thus the audio sink does not preroll yet
            * but the video sink prerolls because we already outputted a
            * buffer here... and then queues run full.
            *
            * In the worst case we will clip one buffer too many here now if no
            * framerate is given, no buffer duration is given and the actual
            * framerate is lower than 25fps */
            stop = start + 40 * GST_MSECOND;
        }
    }

    segment = &sink_priv->segment;
    GST_LOG_OBJECT(sink, "check buffer start: %" GST_TIME_FORMAT " stop %" GST_TIME_FORMAT "fps_n:%d, fps_d:%d",
                         GST_TIME_ARGS(start), GST_TIME_ARGS(stop), sink_priv->video_info.fps_n, sink_priv->video_info.fps_d);
    if (gst_segment_clip(segment, GST_FORMAT_TIME, start, stop, &cstart, &cstop))
    {
        GST_BUFFER_PTS(buf) = cstart;

        if (stop != GST_CLOCK_TIME_NONE && GST_CLOCK_TIME_IS_VALID(duration))
            GST_BUFFER_DURATION(buf) = cstop - cstart;

        GST_LOG_OBJECT(sink,
                       "accepting buffer inside segment: %" GST_TIME_FORMAT " %" GST_TIME_FORMAT " seg %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
                       " time %" GST_TIME_FORMAT,
                       GST_TIME_ARGS(cstart),
                       GST_TIME_ARGS(cstop),
                       GST_TIME_ARGS(segment->start), GST_TIME_ARGS(segment->stop),
                       GST_TIME_ARGS(segment->time));
    }
    else
    {
        GST_LOG_OBJECT(sink,
                       "dropping buffer outside segment: %" GST_TIME_FORMAT
                       " %" GST_TIME_FORMAT
                       " seg %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
                       " time %" GST_TIME_FORMAT,
                       GST_TIME_ARGS(start), GST_TIME_ARGS(stop),
                       GST_TIME_ARGS(segment->start),
                       GST_TIME_ARGS(segment->stop), GST_TIME_ARGS(segment->time));

        ret = FALSE;
    }

    return ret;
}

static void gst_aml_video_sink_reset_private(GstAmlVideoSink *sink)
{
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
#if GST_IMPORT_LGE_PROP
    if (sink_priv->lge_ctxt.app_type)
        g_free(sink_priv->lge_ctxt.app_type);
    if (sink_priv->lge_ctxt.res_info.coretype)
        g_free(sink_priv->lge_ctxt.res_info.coretype);
#endif
    memset(sink_priv, 0, sizeof(GstAmlVideoSinkPrivate));
    sink_priv->use_dmabuf = USE_DMABUF;
    sink_priv->mediasync_instanceid = -1;
    sink_priv->show_first_frame_asap = FALSE;
    sink_priv->emitUnderflowSignal = FALSE;
}

static void gst_render_msg_callback(void *userData, RenderMsgType type, void *msg)
{
    GstAmlVideoSink *sink = (GstAmlVideoSink *)userData;
    switch (type)
    {
    case MSG_DROPED_BUFFER:
    case MSG_DISPLAYED_BUFFER:
    {
        RenderBuffer *tunnel_lib_buf_wrap = (RenderBuffer *)msg;
        RenderDmaBuffer *dmabuf = &tunnel_lib_buf_wrap->dma;
        GstBuffer *buffer = (GstBuffer *)tunnel_lib_buf_wrap->priv;
        if (buffer)
        {
            sink->last_displayed_buf_pts = GST_BUFFER_PTS(buffer);
            if (type == MSG_DROPED_BUFFER)
            {
                GST_LOG_OBJECT(sink, "get message: MSG_DROPED_BUFFER from tunnel lib");
                sink->droped++;
            }
            else if (type == MSG_DISPLAYED_BUFFER)
            {
                GST_LOG_OBJECT(sink, "get message: MSG_DISPLAYED_BUFFER from tunnel lib");
                sink->rendered++;
            }

            GST_DEBUG_OBJECT(sink, "buf:%p planeCnt:%d, plane[0].fd:%d, plane[1].fd:%d pts:%lld, buf stat | queued:%d, dequeued:%d, droped:%d, rendered:%d",
                             buffer,
                             dmabuf->planeCnt, dmabuf->fd[0], dmabuf->fd[1],
                             buffer ? GST_BUFFER_PTS(buffer) : -1, sink->queued, sink->dequeued, sink->droped, sink->rendered);
            //gst_aml_video_sink_dump_stat(sink, GST_DUMP_STAT_FILENAME);
        }
        else
        {
            GST_ERROR_OBJECT(sink, "tunnel lib: return void GstBuffer when MSG_DISPLAYED_BUFFER or MSG_DROPED_BUFFER");
        }
        break;
    }
    case MSG_RELEASE_BUFFER:
    {
        GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
        RenderBuffer *tunnel_lib_buf_wrap = (RenderBuffer *)msg;
        GstBuffer *buffer = (GstBuffer *)tunnel_lib_buf_wrap->priv;
        GST_LOG_OBJECT(sink, "get message: MSG_RELEASE_BUFFER from tunnel lib,%p, pts:%lld ns",buffer, tunnel_lib_buf_wrap->pts);

        if (buffer)
        {
            GST_DEBUG_OBJECT(sink, "get message: MSG_RELEASE_BUFFER from tunnel lib, buffer:%p, from pool:%p", buffer, buffer->pool);
            gst_buffer_unref(buffer);
            sink->dequeued++;
            //gst_aml_video_sink_dump_stat(sink, GST_DUMP_STAT_FILENAME);
        }
        else
        {
            GST_ERROR_OBJECT(sink, "tunnel lib: return void GstBuffer when MSG_RELEASE_BUFFER");
        }
        render_free_render_buffer_wrap(sink_priv->render_device_handle, tunnel_lib_buf_wrap);
        break;
    }
    case MSG_FIRST_FRAME: {
        GST_LOG_OBJECT(sink, "signal first frame");
        g_signal_emit (G_OBJECT (sink), g_signals[SIGNAL_FIRSTFRAME], 0, 2, NULL);
    } break;
    case MSG_UNDER_FLOW: {
        GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
        if (sink->video_playing && !sink_priv->got_eos) {
            GST_LOG_OBJECT(sink, "signal under flow");
            sink_priv->emitUnderflowSignal = TRUE;
        }
    } break;
    default:
    {
        GST_ERROR_OBJECT(sink, "tunnel lib: error message type");
    }
    }
    return;
}

static int gst_render_val_callback(void *userData, int key, void *value)
{
    GstAmlVideoSink *vsink = (GstAmlVideoSink *)userData;
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(vsink);
    int *val = (int *)value;
    gint ret = 0;
    switch (key)
    {
    case KEY_MEDIASYNC_INSTANCE_ID:
    {
        if (gst_get_mediasync_instanceid(vsink))
        {
            int hasAudio = 1;
            *val = sink_priv->mediasync_instanceid;
            GST_DEBUG_OBJECT(vsink, "get mediasync instance id:%d", *val);
            render_set_value(sink_priv->render_device_handle, KEY_MEDIASYNC_SYNC_MODE, (void *)&vsink->avsync_mode);
            render_set_value(sink_priv->render_device_handle, KEY_MEDIASYNC_HAS_AUDIO, (void *)&hasAudio);
        }
        else
        {
            int hasAudio = 0;
            vsink->avsync_mode = 0;// 0:v master, 1:a master
            render_set_value(sink_priv->render_device_handle, KEY_MEDIASYNC_SYNC_MODE, (void *)&vsink->avsync_mode);
            render_set_value(sink_priv->render_device_handle, KEY_MEDIASYNC_HAS_AUDIO, (void *)&hasAudio);
            GST_ERROR_OBJECT(vsink, "can't get mediasync instance id, use vmaster");
            ret = -1;
        }
        break;
    }
    case KEY_VIDEO_FORMAT:
    {
        if (sink_priv->video_info.finfo != NULL)
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
    vmeta = gst_buffer_get_video_meta(gst_buf);
    if (vmeta == NULL)
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
    //GST_DEBUG_OBJECT(vsink, "dbg3-0, dmabuf:%p", dmabuf);

    dmabuf->planeCnt = n_mem;
    dmabuf->width = vmeta->width;
    dmabuf->height = vmeta->height;

    //GST_DEBUG_OBJECT(vsink, "dbgjxs, vmeta->width:%d, dmabuf->width:%d", vmeta->width, dmabuf->width);

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
        if (gst_buffer_find_memory(gst_buf, vmeta->offset[i], 1, &mem_idx, &length, &skip) && mem_idx == i)
        {
            dmabuf->offset[i] = dma_mem->offset + skip;
            //GST_DEBUG_OBJECT(vsink, "get skip from buffer:%d, offset[%d]:%d", skip, i, dmabuf->offset[i]);
        }
        else
        {
            GST_ERROR_OBJECT(vsink, "get skip from buffer error");
            ret = FALSE;
            goto error;
        }

        GST_LOG_OBJECT(vsink, "dma buffer layer:%d, handle:%d, fd:%d, size:%d, offset:%d, stride:%d",
                         i, dmabuf->handle[i], dmabuf->fd[i], dmabuf->size[i], dmabuf->offset[i], dmabuf->stride[i]);
    }
    tunnel_lib_buf_wrap->flag = BUFFER_FLAG_DMA_BUFFER;
    tunnel_lib_buf_wrap->pts = GST_BUFFER_PTS(gst_buf);
    tunnel_lib_buf_wrap->priv = (void *)gst_buf;
    GST_LOG_OBJECT(vsink, "set tunnel lib buf priv:%p from pool:%p, pts:%lld", tunnel_lib_buf_wrap->priv, gst_buf->pool, tunnel_lib_buf_wrap->pts);
    // GST_LOG_OBJECT(vsink, "dbg: buf in:%p, planeCnt:%d, plane[0].fd:%d, plane[1].fd:%d",
    //                  tunnel_lib_buf_wrap->priv,
    //                  dmabuf->planeCnt,
    //                  dmabuf->fd[0],
    //                  dmabuf->fd[1]);

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
    if (!asink)
    {
        GST_DEBUG_OBJECT(vsink, "pipeline don't have audio sink element");
        return FALSE;
    }
    gboolean ret = TRUE;
    GParamSpec* spec = NULL;
    spec = g_object_class_find_property(G_OBJECT_GET_CLASS(G_OBJECT(asink)), "avsync-session");
    GST_DEBUG_OBJECT(vsink, "check amlhalasink has avsync-session property %p", spec);
    if (spec)
        g_object_get (G_OBJECT(asink), "avsync-session", &sink_priv->mediasync_instanceid, NULL);

    GST_DEBUG_OBJECT(vsink, "get mediasync instance id:%d, from amlhalasink:%p", sink_priv->mediasync_instanceid, asink);

    if (sink_priv->mediasync_instanceid == -1)
    {
        GST_ERROR_OBJECT(vsink, "audio sink: don't have valid mediasync instance id");
        ret = FALSE;
    }
    gst_object_unref(asink);
#else
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(vsink);
    gboolean ret = TRUE;
    FILE *fp;
    fp = fopen("/data/MediaSyncId", "r");
    if (fp == NULL)
    {
        GST_ERROR_OBJECT(vsink, "could not open file:/data/MediaSyncId failed");
        ret = FALSE;
    }
    else
    {
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

static void gst_emit_eos_signal(GstAmlVideoSink *vsink)
{
    GST_DEBUG_OBJECT(vsink, "emit eos signal");
    g_mutex_lock(&vsink->eos_lock);
    g_cond_signal(&vsink->eos_cond);
    g_mutex_unlock(&vsink->eos_lock);
}

static void gst_wait_eos_signal(GstAmlVideoSink *vsink)
{
    GST_DEBUG_OBJECT(vsink, "waitting eos signal");
    g_mutex_lock(&vsink->eos_lock);
    g_cond_wait(&vsink->eos_cond, &vsink->eos_lock);
    g_mutex_unlock(&vsink->eos_lock);
    GST_DEBUG_OBJECT(vsink, "waitted eos signal");
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
        // TODO use this func will ref element,unref when done
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
    GST_DEBUG_OBJECT(sink, "trace out get audioSink:%p", audioSink);
    return audioSink;
}
#endif

static gboolean gst_render_set_params(GstVideoSink *vsink)
{
    GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(vsink);
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
    GstVideoInfo *video_info = &(sink_priv->video_info);
    int tunnelmode = 0; // 1 for tunnel mode; 0 for non-tunnel mode

    // RenderWindowSize window_size = {0, 0, video_info->width, video_info->height};
    RenderFrameSize frame_size = {video_info->width, video_info->height};
    GstVideoFormat format = video_info->finfo ? video_info->finfo->format : GST_VIDEO_FORMAT_UNKNOWN;

    if (render_set_value(sink_priv->render_device_handle, KEY_MEDIASYNC_TUNNEL_MODE, (void *)&tunnelmode) == -1)
    {
        GST_ERROR_OBJECT(vsink, "tunnel lib: set tunnelmode error");
        return FALSE;
    }
    if (render_set_value(sink_priv->render_device_handle, KEY_FRAME_SIZE, &frame_size) == -1)
    {
        GST_ERROR_OBJECT(vsink, "tunnel lib: set frame size error");
        return FALSE;
    }
    if (render_set_value(sink_priv->render_device_handle, KEY_VIDEO_FORMAT, &format) == -1)
    {
        GST_ERROR_OBJECT(vsink, "tunnel lib: set video format error");
        return FALSE;
    }

    return TRUE;
}

static gpointer eos_detection_thread(gpointer data)
{
    GstAmlVideoSink *sink = (GstAmlVideoSink *)data;
    GstAmlVideoSinkPrivate *sink_priv = GST_AML_VIDEO_SINK_GET_PRIVATE(sink);
    int eosCountDown;
    double frameRate = (sink->frame_rate > 0.0 ? sink->frame_rate : 30.0);
    GST_DEBUG("eos_detection_thread: enter");

    eosCountDown = 2;
    while (!sink->quit_eos_detect_thread )
    {
        usleep(1000000/frameRate);
        if (sink->video_playing && sink_priv->emitUnderflowSignal) {
            sink_priv->emitUnderflowSignal = FALSE;
            g_signal_emit (G_OBJECT (sink), g_signals[SIGNAL_UNDERFLOW], 0, 0, NULL);
        }
        if (sink->video_playing && sink_priv->got_eos)
        {
            if (sink->queued == sink->rendered + sink->droped)
            {
                --eosCountDown;
                if ( eosCountDown == 0 )
                {
                    GST_DEBUG("EOS detected");
                    GstBaseSink *bs;
                    bs= GST_BASE_SINK(sink);
                    GST_BASE_SINK_PREROLL_LOCK(bs);
                    GST_DEBUG("EOS: need_preroll %d have_preroll %d", bs->need_preroll, bs->have_preroll);
                    if ( bs->need_preroll )
                    {
                        GstState cur, nxt, pend;
                        /* complete preroll and commit state */
                        GST_DEBUG("EOS signal preroll\n");
                        bs->need_preroll= FALSE;
                        bs->have_preroll= TRUE;
                        GST_OBJECT_LOCK(bs);
                        cur= GST_STATE(bs);
                        nxt= GST_STATE_NEXT(bs);
                        GST_STATE(bs)= pend= GST_STATE_PENDING(bs);
                        GST_STATE_NEXT(bs)= GST_STATE_PENDING(bs)= GST_STATE_VOID_PENDING;
                        GST_STATE_RETURN(bs)= GST_STATE_CHANGE_SUCCESS;
                        GST_OBJECT_UNLOCK(bs);
                        GST_DEBUG("EOS posting state change: curr(%s) next(%s) pending(%s)",
                                gst_element_state_get_name(cur),
                                gst_element_state_get_name(nxt),
                                gst_element_state_get_name(pend));
                        gst_element_post_message(GST_ELEMENT_CAST(bs), gst_message_new_state_changed(GST_OBJECT_CAST(bs), cur, nxt, pend));
                        GST_DEBUG("EOS posting async done");
                        gst_element_post_message(GST_ELEMENT_CAST(bs), gst_message_new_async_done(GST_OBJECT_CAST(bs), GST_CLOCK_TIME_NONE));
                        GST_STATE_BROADCAST(bs)
                    }
                    GST_BASE_SINK_PREROLL_UNLOCK(bs);
                    GST_DEBUG("EOS: calling eos detected: need_preroll %d have_preroll %d", bs->need_preroll, bs->have_preroll);
                    gst_element_post_message (GST_ELEMENT_CAST(sink), gst_message_new_eos(GST_OBJECT_CAST(sink)));
                    GST_DEBUG("EOS: done calling eos detected,posted eos msg, need_preroll %d have_preroll %d", bs->need_preroll, bs->have_preroll);
                    break;
               }
            }
            else
            {
                eosCountDown = 2;
            }
        }
    }

    if (!sink->quit_eos_detect_thread)
    {
        GThread *thread= sink->eos_detect_thread_handle;
        g_thread_unref( sink->eos_detect_thread_handle );
        sink->eos_detect_thread_handle= NULL;
    }
    GST_DEBUG("eos_detection_thread: exit");
    return NULL;
}

static void gst_aml_video_sink_dump_stat(GstAmlVideoSink *sink, const gchar *file_name)
{
    const gchar *dump_dir = NULL;
    gchar *full_file_name = NULL;
    FILE *out = NULL;

    dump_dir = g_getenv("GST_DEBUG_DUMP_AMLVIDEOSINK_STAT_DIR");
    if (G_LIKELY(dump_dir == NULL))
        return;

    if (!file_name)
    {
        file_name = "unnamed";
    }

    full_file_name = g_strdup_printf("%s" G_DIR_SEPARATOR_S "%s.stat", dump_dir, file_name);

    if ((out = fopen(full_file_name, "w")))
    {
        gchar *stat_info;
        stat_info = g_strdup_printf("Stat:%d | Q:%d,  Dq:%d,  Render:%d,  Drop:%d\n", GST_STATE(sink), sink->queued, sink->dequeued, sink->rendered, sink->droped);
        fputs(stat_info, out);
        g_free(stat_info);
        fclose(out);
        GST_INFO("wrote amlvideosink stat to : '%s' succ", full_file_name);
    }
    else
    {
        GST_WARNING("Failed to open file '%s' for writing: %s", full_file_name, g_strerror(errno));
    }
    g_free(full_file_name);
}

/* plugin init */
static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(gst_aml_video_sink_debug, "amlvideosink", 0,
                            " aml video sink");

    gint rank = 1;
    const char *rank_env = getenv("GST_AML_VIDEO_SINK_RANK");
    if (rank_env)
    {
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
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  amlvideosink,
                  "Amlogic plugin for video decoding/rendering",
                  plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
