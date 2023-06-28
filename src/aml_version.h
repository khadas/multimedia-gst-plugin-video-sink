#ifndef __AML_VERSION_H__
#define __AML_VERSION_H__

#ifdef  __cplusplus
extern "C" {
#endif

const char libVersion[]=
"MM-module-name:gst-plugin-video-sink,version:1.2.13-gbcd880e";

const char libFeatures[]=
"MM-module-feature: set video full screen \n" \
"MM-module-feature: Mute video frame \n" \
"MM-module-feature: set av sync mode \n" \
"MM-module-feature: set window size rectangle \n" \
"MM-module-feature: set video as pip \n" \
"MM-module-feature: support render lib for a/v sync and display\n"
"MM-module-feature: set the fence to control the speed of sending buffer\n";

#ifdef  __cplusplus
}
#endif
#endif /*__AML_VERSION_H__*/