#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND    "TuneBrew"
#define DISTRHO_PLUGIN_NAME     "LevelBrew Vocal Rider"
#define DISTRHO_PLUGIN_URI      "urn:tunebrew:levelbrew"
#define DISTRHO_PLUGIN_CLAP_ID  "com.tunebrew.levelbrew"

#define DISTRHO_PLUGIN_BRAND_ID  TnBr
#define DISTRHO_PLUGIN_UNIQUE_ID LvBr

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_CAIRO         1
#define DISTRHO_UI_USER_RESIZABLE    1
#define DISTRHO_UI_DEFAULT_WIDTH     912
#define DISTRHO_UI_DEFAULT_HEIGHT    540
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_TIMEPOS  1
#define DISTRHO_PLUGIN_WANT_STATE    1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
// the editor draws straight from the plugin's scan (same process)
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:DynamicsPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Dynamics|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "compressor", "stereo"

#endif
