#ifndef __GST_AML_VIDEO_SINK_H__
#define __GST_AML_VIDEO_SINK_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_AML_VIDEO_SINK \
	    (gst_aml_video_sink_get_type())
#define GST_AML_VIDEO_SINK(obj) \
	    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AML_VIDEO_SINK,GstAmlVideoSink))
#define GST_AML_VIDEO_SINK_CLASS(klass) \
	    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AML_VIDEO_SINK,GstAmlVideoSinkClass))
#define GST_IS_AML_VIDEO_SINK(obj) \
	    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AML_VIDEO_SINK))
#define GST_IS_AML_VIDEO_SINK_CLASS(klass) \
	    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AML_VIDEO_SINK))
#define GST_AML_VIDEO_SINK_GET_CLASS(inst) \
        (G_TYPE_INSTANCE_GET_CLASS ((inst), GST_TYPE_AML_VIDEO_SINK, GstAmlVideoSinkClass))
#define GST_AML_VIDEO_SINK_GET_PRIVATE(obj) ((GST_AML_VIDEO_SINK (obj))->priv)


typedef struct _GstAmlVideoSink GstAmlVideoSink;
typedef struct _GstAmlVideoSinkClass GstAmlVideoSinkClass;
typedef struct _GstAmlVideoSinkPrivate GstAmlVideoSinkPrivate;

struct _GstAmlVideoSink
{
  GstVideoSink parent;
  GstAmlVideoSinkPrivate *priv;
};

struct _GstAmlVideoSinkClass
{
  GstVideoSinkClass parent;
};

GType gst_aml_video_sink_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GST_AML_VIDEO_SINK_H__ */
