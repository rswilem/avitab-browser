#if defined(_WIN32) || defined(_WIN64)
#include <GL/gl.h>
#include <windows.h>
#define GL_BGRA GL_BGRA_EXT
#define GL_CLAMP_TO_EDGE 0x812F
#elif __linux__
#include <GL/gl.h>
#elif __GNUC__
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl.h>
#endif

#define set_brightness(value) glColor4f(value, value, value, 1.0f)

#define PRODUCT_NAME "avitab-browser"
#define FRIENDLY_NAME "AviTab Browser"
#define VERSION "1.1.2"
#define ALL_PLUGINS_DIRECTORY "/Resources/plugins/"
#define PLUGIN_DIRECTORY (ALL_PLUGINS_DIRECTORY PRODUCT_NAME)
#define BUNDLE_ID "com.ramonster." PRODUCT_NAME

// See https://forums.x-plane.org/index.php?/forums/topic/261574-tutorial-integrating-avitab/#findComment-2319386
#define AVITAB_USE_FIXED_ASPECT_RATIO 1

#define SCALE_IMAGES 1

#define REFRESH_INTERVAL_SECONDS_FAST 0.1
#define REFRESH_INTERVAL_SECONDS_SLOW 2.0

// Mouse-anchor invalidation thresholds (see Dataref::getMouse). A cached tablet
// click sample is treated as stale once the pilot's head view moves by more than
// these amounts. Rotation is wide to tolerate head-shake / bounce camera plugins;
// translation is tight because any real zoom-to-tablet moves the head well over
// 10cm. Raise VIEW_FOV_DEADBAND_DEG (or set it very high to disable) if a camera
// plugin animates field of view for g-force/speed effects.
#define VIEW_ROTATION_DEADBAND_DEG 12.0f
#define VIEW_TRANSLATION_DEADBAND_M 0.08f
#define VIEW_FOV_DEADBAND_DEG 5.0f

#include "logger.hpp"
