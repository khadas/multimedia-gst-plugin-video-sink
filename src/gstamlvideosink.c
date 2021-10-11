#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstamlvideosink.h"
#include <stdbool.h>`

/* signals */
enum { SIGNAL_0, LAST_SIGNAL };

/* Properties */
enum {
  PROP_0,
  PROP_DISPLAY,
  PROP_FULLSCREEN,
  PROP_ALLOCATION,
  PROP_SECURE,
};

#define AML_VIDEO_FORMATS                                                      \
  "{ BGRx, BGRA, RGBx, xBGR, xRGB, RGBA, ABGR, ARGB, RGB, BGR, "               \
  "RGB16, BGR16, YUY2, YVYU, UYVY, AYUV, NV12, NV21, NV16, "                   \
  "YUV9, YVU9, Y41B, I420, YV12, Y42B, v308 }"
#define GST_CAPS_FEATURE_MEMORY_DMABUF "memory:DMABuf"

struct _GstAmlVideoSinkPrivate {
  GstVideoInfo video_info;
  gboolean video_info_changed;
  gboolean use_dmabuf;
  gboolean is_flushing;
  _GstSegment segment;
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        GST_VIDEO_CAPS_MAKE(AML_VIDEO_FORMATS) ";" GST_VIDEO_CAPS_MAKE_WITH_FEATURES(
            GST_CAPS_FEATURE_MEMORY_DMABUF, AML_VIDEO_FORMATS)));

GST_DEBUG_CATEGORY(gst_aml_video_sink_debug);
#define GST_CAT_DEFAULT gst_aml_video_sink_debug
#define gst_aml_video_sink_parent_class parent_class
#define GST_AML_VIDEO_SINK_GET_PRIVATE(obj)                                    \
  (G_TYPE_INSTANCE_GET_PRIVATE((obj), GST_TYPE_AML_VIDEO_SINK,                 \
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
static GstCaps *gst_aml_video_sink_get_caps(GstBaseSink *bsink,
                                            GstCaps *filter);
static gboolean gst_aml_video_sink_set_caps(GstBaseSink *bsink, GstCaps *caps);
static gboolean static gboolean
gst_aml_video_sink_show_frame(GstVideoSink *bsink, GstBuffer *buffer);
static gboolean gst_aml_video_sink_pad_event(GstAmlVideoSink *sink,
                                             GstEvent *event);

/* private interface define */
static void gst_aml_video_sink_reset_private(GstAmlVideoSink *sink);
static gboolean gst_tunnel_lib_alloc_instance(GstAmlVideoSink *sink);
static gboolean gst_tunnel_lib_destroy_instance(GstAmlVideoSink *sink);
static gboolean gst_tunnel_lib_start(GstAmlVideoSink *sink);
static gboolean gst_tunnel_lib_stop(GstAmlVideoSink *sink);
static gboolean gst_tunnel_lib_format_for_dmabuf(GstAmlVideoSink *sink,
                                                 GstVideoFormat format);
static gboolean gst_tunnel_lib_set_format(GstAmlVideoSink *sink, GstCaps *caps);
static GstFlowReturn gst_tunnel_lib_queue_frame(GstAmlVideoSink *sink,
                                                GstBuffer *buffer);
static GstFlowReturn gst_tunnel_lib_play_rate(GstAmlVideoSink *sink);
static GstFlowReturn gst_tunnel_lib_flush(GstAmlVideoSink *sink);
static void finish_render_cb(GstAmlVideoSink *sink, GstBuffer *buffer);

/* public interface definition */
static void gst_aml_video_sink_class_init(GstWaylandSinkClass *klass) {
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

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR(gst_aml_video_sink_get_caps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR(gst_aml_video_sink_set_caps);

  gstvideosink_class->show_frame =
      GST_DEBUG_FUNCPTR(gst_aml_video_sink_show_frame);

  g_object_class_install_property(
      gobject_class, PROP_DISPLAY,
      g_param_spec_string(
          "display", "Wayland Display name",
          "Wayland "
          "display name to connect to, if not supplied via the GstContext",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_FULLSCREEN,
      g_param_spec_boolean("fullscreen", "Fullscreen",
                           "Whether the surface should be made fullscreen ",
                           FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_ALLOCATION,
      g_param_spec_boolean("use-drm", "Wayland Allocation name",
                           "Wayland "
                           "Use DRM based memory for allocation",
                           FALSE, G_PARAM_WRITABLE));

  g_object_class_install_property(
      gobject_class, PROP_SECURE,
      g_param_spec_boolean("secure", "Wayland Allocation Secure",
                           "Wayland "
                           "Use Secure DRM based memory for allocation",
                           FALSE, G_PARAM_WRITABLE));
}

static void gst_aml_video_sink_init(GstAmlVideoSink *sink) {
  GstBaseSink *basesink = (GstBaseSink *)sink;
  gst_pad_set_event_function(basesink->sinkpad, gst_aml_video_sink_pad_event);
}

static void gst_aml_video_sink_get_property(GObject *object, guint prop_id,
                                            GValue *value, GParamSpec *pspec) {
  GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(object);

  switch (prop_id) {
  case PROP_DISPLAY:
    GST_OBJECT_LOCK(sink);
    g_value_set_string(value, sink->display_name);
    GST_OBJECT_UNLOCK(sink);
    break;
  case PROP_FULLSCREEN:
    GST_OBJECT_LOCK(sink);
    g_value_set_boolean(value, sink->fullscreen);
    GST_OBJECT_UNLOCK(sink);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_aml_video_sink_set_property(GObject *object, guint prop_id,
                                            const GValue *value,
                                            GParamSpec *pspec) {
  GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(object);

  switch (prop_id) {
  case PROP_DISPLAY:
    GST_OBJECT_LOCK(sink);
    sink->display_name = g_value_dup_string(value);
    GST_OBJECT_UNLOCK(sink);
    break;
  case PROP_FULLSCREEN:
    GST_OBJECT_LOCK(sink);
    gst_wayland_sink_set_fullscreen(sink, g_value_get_boolean(value));
    GST_OBJECT_UNLOCK(sink);
    break;
  case PROP_ALLOCATION:
    GST_OBJECT_LOCK(sink);
    sink->use_drm = g_value_get_boolean(value);
    GST_OBJECT_UNLOCK(sink);
    break;
  case PROP_SECURE:
    GST_OBJECT_LOCK(sink);
    sink->secure = g_value_get_boolean(value);
    GST_OBJECT_UNLOCK(sink);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_aml_video_sink_finalize(GObject *object) {
  GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(object);

  GST_DEBUG_OBJECT(sink, "Finalizing the sink..");
  gst_aml_video_sink_reset_private(sink);
  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static GstStateChangeReturn
gst_aml_video_sink_change_state(GstElement *element,
                                GstStateChange transition) {
  GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(element);
  GstAmlVideoSinkClass *class = GST_AML_VIDEO_SINK_CLASS(element);
  GstBaseSinkClass *base_class = (GstBaseSinkClass *)class;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
  case GST_STATE_CHANGE_NULL_TO_READY: {
    // TODO alloc tunnel lib instance
    gst_tunnel_lib_alloc_instance(sink);
    break;
  }
  case GST_STATE_CHANGE_READY_TO_PAUSED: {
    // TODO alloc tunnel lib start
    gst_tunnel_lib_alloc_start(sink);
    break;
  }
  default:
    break;
  }

  ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
  case GST_STATE_CHANGE_PAUSED_TO_READY: {
    // TODO destroy tunnel lib stop
    gst_tunnel_lib_alloc_stop(sink);
    break;
  }
  case GST_STATE_CHANGE_READY_TO_NULL: {
    // TODO destroy tunnel lib instance
    gst_tunnel_lib_destroy_instance(sink);
    break;
  }
  default:
    break;
  }

  return ret;
}

static GstCaps *gst_aml_video_sink_get_caps(GstBaseSink *bsink,
                                            GstCaps *filter) {
  GstAmlVideoSink *sink;
  GstCaps *caps;

  sink = GST_AML_VIDEO_SINK(bsink);

  caps = gst_pad_get_pad_template_caps(GST_VIDEO_SINK_PAD(sink));
  caps = gst_caps_make_writable(caps);
  // TODO 这里是需要从template直接取出支持的caps还是要通过tunnel lib拿到caps？

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full(filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref(caps);
    caps = intersection;
  }

  return caps;
}

static gboolean gst_aml_video_sink_set_caps(GstBaseSink *bsink, GstCaps *caps) {
  GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(bsink);
  GstAmlVideoSinkPrivate *sink_priv =
      gst_aml_video_sink_get_instance_private(sink);
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;

  GST_DEBUG_OBJECT(sink, "set caps %" GST_PTR_FORMAT, caps);

  /* extract info from caps */
  if (!gst_video_info_from_caps(&sink_priv->video_info, caps))
    goto invalid_format;

  format = GST_VIDEO_INFO_FORMAT(&sink_priv->video_info);
  sink_priv->video_info_changed = TRUE;

  /* validate the format base on the memory type. */
  sink_priv->use_dmabuf = gst_caps_features_contains(
      gst_caps_get_features(caps, 0), GST_CAPS_FEATURE_MEMORY_DMABUF);
  if (use_dmabuf) {
    // TODO 调用tunnel lib接口，确认在drmbuffer场景下，是否支持当前格式
    if (!gst_tunnel_lib_format_for_dmabuf(sink, format))
      goto unsupported_format;
  }
  return TRUE;

invalid_format : {
  GST_ERROR_OBJECT(
      sink, "Could not locate video format from caps %" GST_PTR_FORMAT, caps);
  return FALSE;
}
unsupported_format : {
  GST_ERROR_OBJECT(sink, "Format %s is not available on the display",
                   gst_video_format_to_string(format));
  return FALSE;
}
}

static GstFlowReturn gst_aml_video_sink_show_frame(GstVideoSink *vsink,
                                                   GstBuffer *buffer) {
  GstAmlVideoSink *sink = GST_AML_VIDEO_SINK(element);
  GstAmlVideoSinkPrivate *sink_priv =
      gst_aml_video_sink_get_instance_private(sink);
  GstFlowReturn ret = GST_FLOW_OK;

  // TODO should call tunnel lib flush func
  if (sink_priv->is_flushing) {
    gst_tunnel_lib_flush(sink);
    finish_render_cb(sink, buffer);
    return ret;
  }

  if (sink_priv->video_info_changed) {
    // TODO 是否需要将caps设置给tunnel lib？ 由于caps event是series
    // event，所以在收到新的buffer前将新的caps配置下去即可
    gst_tunnel_lib_set_format(sink, caps);
    sink_priv->video_info_changed = FALSE;
  }
  ret = gst_tunnel_lib_queue_frame(sink, buffer);
  return ret;
}

static gboolean gst_aml_video_sink_pad_event(GstAmlVideoSink *sink,
                                             GstEvent *event) {
  gboolean result = TRUE;
  GstBaseSink *bsink = GST_BASE_SINK_CAST(sink);
  GstAmlVideoSinkPrivate *sink_priv =
      gst_aml_video_sink_get_instance_private(sink);

  switch (GST_EVENT_TYPE(event)) {
  case GST_EVENT_FLUSH_START: {
    GST_INFO_OBJECT(sink, "flush start");
    GST_OBJECT_LOCK(sink);
    sink_priv->is_flushing = TRUE;
    GST_OBJECT_UNLOCK(sink);
    break;
  }
  case GST_EVENT_FLUSH_STOP: {
    GST_INFO_OBJECT(sink, "flush stop");

    GST_OBJECT_LOCK(sink);
    sink_priv->is_flushing = FALSE;
    GST_OBJECT_UNLOCK(sink);
    break;
  }
  case GST_EVENT_SEGMENT: {
    gst_event_copy_segment(event, &sink_priv->segment);
    GST_INFO_OBJECT(sink, "configured segment %" GST_SEGMENT_FORMAT, &sink_priv->segment);
    //TODO set play rate to tunnel lib
    gst_tunnel_lib_play_rate(sink);
    break;
  }
  default: {
    GST_DEBUG_OBJECT(sink, "pass to basesink");
    return GST_BASE_SINK_CLASS(parent_class)->event(bsink, event);
  }
  }
  gst_event_unref(event);
  return result;
}

/* private interface definition */
static void gst_aml_video_sink_reset_private(GstAmlVideoSink *sink) {
  GstAmlVideoSinkPrivate *sink_priv =
      gst_aml_video_sink_get_instance_private(sink);
  memset(sink_priv, 0, sizeof(GstAmlVideoSinkPrivate));
}

static gboolean gst_tunnel_lib_alloc_instance(GstAmlVideoSink *sink) {
  GstFlowReturn ret = GST_FLOW_OK;
  gst_aml_video_sink_reset_private(sink);
  // TODO call tunnel lib func

  return ret;
}

static gboolean gst_tunnel_lib_destroy_instance(GstAmlVideoSink *sink) {
  GstFlowReturn ret = GST_FLOW_OK;
  gst_aml_video_sink_reset_private(sink);
  // TODO call tunnel lib func

  return ret;
}

static void finish_render_cb(GstAmlVideoSink *sink, GstBuffer *buffer);
{
  if (buffer)
    gst_buffer_unref(buffer);
}

/* plugin init */
static gboolean plugin_init(GstPlugin *plugin) {
  GST_DEBUG_CATEGORY_INIT(gst_aml_video_sink_debug, "amlvideosink", 0,
                          " aml video sink");

  return gst_element_register(plugin, "amlvideosink", 1,
                              GST_TYPE_AML_VIDEO_SINK);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, waylandsink,
                  "aml Video Sink", plugin_init, VERSION, "LGPL",
                  GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
