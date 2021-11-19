#!/bin/bash
echo 1 > /sys/module/amvdec_ports/parameters/multiplanar
echo 1 > /sys/module/amvdec_ports/parameters/bypass_vpp
echo 0 > /sys/module/amvdec_ports/parameters/enable_drm_mode
echo 1 > /sys/kernel/debug/dri/0/vpu/blank

export XDG_RUNTIME_DIR="/run/user/0"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/lib/gstreamer-1.0
export GST_DEBUG_FILE=/data/test/gst.log
export GST_DEBUG=6
export GST_DEBUG_DUMP_DOT_DIR=/data/test
export GST_REGISTRY=/data/test/gst.reg

rm /data/core-*
rm /data/test/*.log
rm /data/test/*.dot
rm /data/test/*.reg
#gst-play-1.0 /data/test/1080P_H264.mp4
#gst-launch-1.0 filesrc location=/data/test/1080P_H264.mp4 ! qtdemux ! h264parse ! queue ! v4l2h264dec capture-io-mode=5 ! video/x-raw,format=NV12 ! amlvideosink
gst-launch-1.0 filesrc location=/data/test/1080P_H264.mp4 ! qtdemux ! h264parse ! queue ! v4l2h264dec capture-io-mode=5 ! amlvideosink
