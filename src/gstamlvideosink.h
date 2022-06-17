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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_AML_VIDEO_SINK_H__
#define __GST_AML_VIDEO_SINK_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "aml_version.h"

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

  GstClockTime last_displayed_buf_pts;
  GstClockTime last_dec_buf_pts;

  /* eos detect */
  gint queued;
  gint dequeued;
  gint rendered;
  gint droped;
  gint avsync_mode;
  GMutex eos_lock;
  GCond eos_cond;

  GstAmlVideoSinkPrivate *priv;
};

struct _GstAmlVideoSinkClass
{
  GstVideoSinkClass parent;
};

GType gst_aml_video_sink_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GST_AML_VIDEO_SINK_H__ */
