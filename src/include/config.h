#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#elif __linux__
    #include <GL/gl.h>
#elif __GNUC__
    #define GL_SILENCE_DEPRECATION 1
    #include <OpenGL/gl.h>
#else
    #include <GL/gl.h>
#endif

#define debug(format, ...) { char buffer[1024]; snprintf(buffer, sizeof(buffer), "[avitab-browser] " format, ##__VA_ARGS__); XPLMDebugString(buffer); }

#define PRODUCT_NAME "avitab-browser"
#define FRIENDLY_NAME "AviTab Browser"
#define VERSION 3
#define VERSION_CHECK_URL "https://api.github.com/repos/rswilem/avitab-browser/releases?per_page=1&page=1"
#define ALL_PLUGINS_DIRECTORY "/Resources/plugins/"
#define PLUGIN_DIRECTORY ALL_PLUGINS_DIRECTORY PRODUCT_NAME
#define BUNDLE_ID "com.ramonster." PRODUCT_NAME

// See https://forums.x-plane.org/index.php?/forums/topic/261574-tutorial-integrating-avitab/#findComment-2319386
#define AVITAB_USE_FIXED_ASPECT_RATIO 1

#define SCALE_IMAGES 1

#define REFRESH_INTERVAL_SECONDS_FAST 0.1
#define REFRESH_INTERVAL_SECONDS_SLOW 2.0
