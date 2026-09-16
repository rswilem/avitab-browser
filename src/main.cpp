#ifndef XPLM440
    #error This is made to be compiled against the X-Plane 4.4.0 SDK for XP11 and XP12
#endif

#include "config.h"
#include "appstate.h"
#include "dataref.h"
#include "path.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <XPLMDisplay.h>
#include <XPLMPanelGraphics.h>
#include <XPLMUtilities.h>
#include <XPLMPlugin.h>
#include <XPLMMenus.h>
#include <XPLMProcessing.h>
#include <XPLMMenus.h>
#include <cmath>
#include "drawing.h"
#include "cursor.h"

#if IBM
#include <windows.h>
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    
    return TRUE;
}
#endif

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, long msg, void* params);
int draw(XPLMDrawingPhase inPhase, int inIsBefore, void * inRefcon);
float update(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon);
float pumpBrowser(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon);
int mouseClicked(XPLMWindowID inWindowID, int x, int y, XPLMMouseStatus status, void* inRefcon);
void menuAction(void* mRef, void* iRef);
void registerWindow();
void captureVrChanges();
void captureClickEvents(bool enable);

unsigned char pressedKeyCode = 0;
unsigned char pressedVirtualKeyCode = 0;
double pressedKeyTime = 0;

// SDK 4.4 entry points, resolved at runtime so one binary still loads on older sims.
struct PanelGraphicsApi {
    XPLMFontHandle (*createFont)(XPLMCharSet_t) = nullptr;
    void (*destroyFont)(XPLMFontHandle) = nullptr;
    int (*fontAddFace)(XPLMFontHandle, const char *) = nullptr;
    void (*fontDrawString)(XPLMFontHandle, uint32_t, float, float, float, const char *, XPLMJustification_t) = nullptr;
    uint32_t (*makeColor)(float, float, float, float) = nullptr;

    bool available() const {
        return createFont && destroyFont && fontAddFace && fontDrawString && makeColor;
    }
};

static PanelGraphicsApi panelGraphics;
static bool hostHasSdk440 = false;
static XPLMFontHandle aboutFont = nullptr;

static const std::array<std::pair<float, const char *>, 6> aboutLines = {{
    {16.0f, FRIENDLY_NAME},
    {32.0f, "Version " VERSION},
    {64.0f, "This software is licensed under the GNU General Public License, GPL-3.0"},
    {96.0f, "For updates to " FRIENDLY_NAME ", please see the forums at x-plane.org"},
    {112.0f, "or checkout the GitHub releases at github.com/rswilem/avitab-browser."},
    {128.0f, "Made with love by TheRamon, thank you for using this software!"},
}};

template <typename T>
static void resolveSymbol(T &target, const char *name) {
    target = reinterpret_cast<T>(XPLMFindSymbol(name));
}

static void resolvePanelGraphics() {
    int xplaneVersion = 0;
    int xplmVersion = 0;
    XPLMHostApplicationID host = 0;
    XPLMGetVersions(&xplaneVersion, &xplmVersion, &host);
    hostHasSdk440 = xplmVersion >= 440;
    if (!hostHasSdk440) {
        return;
    }

    resolveSymbol(panelGraphics.createFont, "XPLMCreateFont");
    resolveSymbol(panelGraphics.destroyFont, "XPLMDestroyFont");
    resolveSymbol(panelGraphics.fontAddFace, "XPLMFontAddFace");
    resolveSymbol(panelGraphics.fontDrawString, "XPLMFontDrawString");
    resolveSymbol(panelGraphics.makeColor, "XPLMMakeColor");
}

// Older hosts reject a struct size they do not know; the 4.4 fields are the tail.
static int windowStructSize() {
    return hostHasSdk440 ? (int) sizeof(XPLMCreateWindow_t) : (int) offsetof(XPLMCreateWindow_t, contentType);
}

static bool loadAboutFont() {
    static bool failed = false;
    if (aboutFont || failed) {
        return aboutFont != nullptr;
    }
    if (!panelGraphics.available()) {
        failed = true;
        return false;
    }

    XPLMFontHandle font = panelGraphics.createFont(xplm_CharSetUnicode);
    std::string face = Path::getInstance()->rootDirectory + "/Resources/fonts/Roboto-Regular.ttf";
    if (!font || !panelGraphics.fontAddFace(font, face.c_str())) {
        if (font) {
            panelGraphics.destroyFont(font);
        }
        failed = true;
        return false;
    }

    aboutFont = font;
    return true;
}

PLUGIN_API int XPluginStart(char * name, char * sig, char * desc)
{
    // Capture the main thread and register the log flush loop before anything
    // else logs, so every XPLMDebugString call is marshalled onto this thread.
    Logger::getInstance()->initialize();
    resolvePanelGraphics();

    strcpy(name, FRIENDLY_NAME);
    strcpy(sig, BUNDLE_ID);
    strcpy(desc, "Browser extension for the Avitab");
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
    XPLMEnableFeature("XPLM_USE_NATIVE_WIDGET_WINDOWS", 1);
    
    int item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), FRIENDLY_NAME, nullptr, 1);
    XPLMMenuID id = XPLMCreateMenu(FRIENDLY_NAME, XPLMFindPluginsMenu(), item, menuAction, nullptr);
    XPLMAppendMenuItem(id, "Reload configuration", (void *)"ActionReloadConfig", 0);
    XPLMAppendMenuItem(id, "About", (void *)"ActionAbout", 0);

    XPLMRegisterFlightLoopCallback(update, REFRESH_INTERVAL_SECONDS_SLOW, nullptr);
    XPLMRegisterFlightLoopCallback(pumpBrowser, REFRESH_INTERVAL_SECONDS_FAST, nullptr);
    XPLMRegisterDrawCallback(draw, xplm_Phase_Gauges, 0, nullptr);
    
    XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, nullptr);

    initializeCursor();
    
    Logger::getInstance()->info("Plugin started (version %s)\n", VERSION);
    
    #if DEBUG
    Dataref::getInstance()->createCommand("avitab_browser/debug/window_to_foreground", "Bring window to front", [](XPLMCommandPhase inPhase) {
        if (inPhase != xplm_CommandBegin) {
            return;
        }
        
        XPLMBringWindowToFront(AppState::getInstance()->mainWindow);
    });
    
    Dataref::getInstance()->createCommand("avitab_browser/debug/vr_click_proxy", "sim/VR/reserved/select", [](XPLMCommandPhase inPhase) {
        Dataref::getInstance()->executeCommand("sim/VR/reserved/select", inPhase);
    });
    #endif
    
    return 1;
}

PLUGIN_API void XPluginStop(void) {
    XPLMUnregisterDrawCallback(draw, xplm_Phase_Gauges, 0, nullptr);
    XPLMUnregisterFlightLoopCallback(pumpBrowser, nullptr);
    XPLMUnregisterFlightLoopCallback(update, nullptr);
    if (AppState::getInstance()->mainWindow) {
        XPLMDestroyWindow(AppState::getInstance()->mainWindow);
        AppState::getInstance()->mainWindow = nullptr;
    }
    if (aboutFont) {
        panelGraphics.destroyFont(aboutFont);
        aboutFont = nullptr;
    }
    
    destroyCursor();
    captureClickEvents(false);
    
    AppState::getInstance()->deinitialize();
    Logger::getInstance()->info("Plugin stopped\n");

    // Unregister the flush loop and drain any remaining queued messages.
    Logger::getInstance()->destroy();
}

PLUGIN_API int XPluginEnable(void) {
    Path::getInstance()->reloadPaths();
    
    if (AppState::getInstance()->mainWindow) {
        XPLMBringWindowToFront(AppState::getInstance()->mainWindow);
    }
    
    return 1;
}

PLUGIN_API void XPluginDisable(void) {
    Logger::getInstance()->info("Disabling plugin...\n");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, long msg, void* params) {
    switch (msg) {
        case XPLM_MSG_PLANE_LOADED:
            if ((intptr_t)params != 0) {
                // It was not the user's plane. Ignore.
                return;
            }

            if (AppState::getInstance()->initialize()) {
                registerWindow();
                captureClickEvents(true);

                // Re-register the VR monitor on every aircraft load:
                // deinitialize() destroys all dataref bindings on unload, which
                // would otherwise leave VR switching (and VR clicks) dead after
                // the first aircraft change.
                captureVrChanges();
            }
            break;
            
        case XPLM_MSG_PLANE_CRASHED:
            break;
            
        case XPLM_MSG_PLANE_UNLOADED:
            if ((intptr_t)params != 0) {
                // It was not the user's plane. Ignore.
                return;
            }
            
            Dataref::getInstance()->executeCommand("AviTab/Home");
            AppState::getInstance()->deinitialize();
            break;
            
        default:
            break;
    }
}

void menuAction(void* mRef, void* iRef) {
    if (!strcmp((char *)iRef, "ActionAbout")) {
        int winLeft, winTop, winRight, winBot;
        XPLMGetScreenBoundsGlobal(&winLeft, &winTop, &winRight, &winBot);
        XPLMCreateWindow_t params = {};
        float screenWidth = fabs(winLeft - winRight);
        float screenHeight = fabs(winTop - winBot);
        float width = 450.0f;
        float height = 180.0f;

        params.structSize = windowStructSize();
        params.left = (int)(winLeft + (screenWidth - width) / 2);
        params.right = params.left + width;
        params.top = (int)(winTop - (screenHeight - height) / 2);
        params.bottom = params.top - height;
        params.visible = 1;
        params.refcon = nullptr;
        if (loadAboutFont()) {
            params.contentType = xplm_WindowContentTypePanelGraphics;
            params.drawWindowFunc = [](XPLMWindowID inWindowID, void *) {
                int left, top, right, bottom;
                XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
                uint32_t white = panelGraphics.makeColor(1.0f, 1.0f, 1.0f, 1.0f);
                for (const auto &[offset, text] : aboutLines) {
                    panelGraphics.fontDrawString(aboutFont, white, 13.0f, left + 16.0f, top - offset, text, xplm_JustLeft);
                }
            };
        } else {
            params.drawWindowFunc = [](XPLMWindowID inWindowID, void *) {
                XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
                int left, top, right, bottom;
                XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
                float color[] = {1.0f, 1.0f, 1.0f};
                for (const auto &[offset, text] : aboutLines) {
                    XPLMDrawString(color, left + 16.0f, top - offset, text, nullptr, xplmFont_Proportional);
                }
            };
        }

        params.handleMouseClickFunc = nullptr;
        params.handleRightClickFunc = nullptr;
        params.handleMouseWheelFunc = nullptr;
        params.handleKeyFunc = nullptr;
        params.handleCursorFunc = nullptr;
        params.layer = xplm_WindowLayerFloatingWindows;
        params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
        XPLMWindowID aboutWindow = XPLMCreateWindowEx(&params);
        XPLMSetWindowTitle(aboutWindow, FRIENDLY_NAME);
        XPLMSetWindowPositioningMode(aboutWindow, Dataref::getInstance()->get<bool>("sim/graphics/VR/enabled") ? xplm_WindowVR : xplm_WindowPositionFree, -1);
        XPLMBringWindowToFront(aboutWindow);
    }
    else if (!strcmp((char *)iRef, "ActionReloadConfig")) {
        AppState::getInstance()->loadConfig();
        
        if (AppState::getInstance()->mainWindow) {
            XPLMBringWindowToFront(AppState::getInstance()->mainWindow);
        }
    }
}

void keyPressed(XPLMWindowID inWindowID, char key, XPLMKeyFlags flags, char virtualKey, void* inRefcon, int losingFocus) {
    if ((flags & xplm_DownFlag) == xplm_DownFlag) {
        pressedKeyCode = key;
        pressedVirtualKeyCode = virtualKey;
        pressedKeyTime = XPLMGetElapsedTime();
        AppState::getInstance()->browser->key(key, virtualKey, flags);
    }
    
    if ((flags & xplm_UpFlag) == xplm_UpFlag) {
        pressedKeyCode = 0;
        pressedVirtualKeyCode = 0;
        pressedKeyTime = 0;
        AppState::getInstance()->browser->key(key, virtualKey, flags);
    }
    
    if (losingFocus) {
        AppState::getInstance()->browser->setFocus(false);
    }
}

int mouseClicked(XPLMWindowID inWindowID, int x, int y, XPLMMouseStatus status, void* inRefcon) {
    if (!AppState::getInstance()->hasPower) {
        return 0;
    }
    
    float mouseX, mouseY;
    if (!Dataref::getInstance()->getMouse(&mouseX, &mouseY, x, y)) {
        if (AppState::getInstance()->browserVisible && AppState::getInstance()->browser->hasInputFocus()) {
            AppState::getInstance()->browser->setFocus(false);
        }
        return 0;
    }
    
    if (status == xplm_MouseDown) {
        bool didConsume = AppState::getInstance()->updateButtons(mouseX, mouseY, kButtonClick);
        if (didConsume) {
            return 1;
        }
    }
    
    if (!AppState::getInstance()->browserVisible) {
        return 0;
    }
    
    if (AppState::getInstance()->browser->click(status, mouseX, mouseY)) {
        return 1;
    }
    
    AppState::getInstance()->browser->setFocus(false);
    return 0;
}

int mouseWheel(XPLMWindowID inWindowID, int x, int y, int wheel, int clicks, void* inRefcon) {
    if (!AppState::getInstance()->browserVisible) {
        return 0;
    }
    
    float mouseX, mouseY;
    if (!Dataref::getInstance()->getMouse(&mouseX, &mouseY, x, y)) {
        return 0;
    }
    
    bool horizontal = wheel == 1;
    AppState::getInstance()->browser->scroll(mouseX, mouseY, clicks * AppState::getInstance()->config.scroll_speed, horizontal);
    return 1;
}

int mouseCursor(XPLMWindowID inWindowID, int x, int y, void* inRefcon) {
    bool isVREnabled = Dataref::getInstance()->getCached<int>("sim/graphics/VR/enabled");
    if (isVREnabled) {
        return xplm_CursorDefault;
    }
    
    if (!AppState::getInstance()->hasPower) {
        AppState::getInstance()->activeCursor = CursorDefault;
        return xplm_CursorDefault;
    }
    
    float mouseX, mouseY;
    if (!Dataref::getInstance()->getMouse(&mouseX, &mouseY, x, y)) {
        AppState::getInstance()->activeCursor = CursorDefault;
        return xplm_CursorDefault;
    }
    
    CursorType wantedCursor = CursorDefault;
    if (AppState::getInstance()->updateButtons(mouseX, mouseY, kButtonHover)) {
        wantedCursor = CursorHand;
    }
    else if (AppState::getInstance()->browserVisible && AppState::getInstance()->browser->cursor() != CursorDefault) {
        wantedCursor = AppState::getInstance()->browser->cursor();
    }
    
    if (wantedCursor == CursorDefault) {
        AppState::getInstance()->activeCursor = CursorDefault;
        return xplm_CursorDefault;
    }
    
    if (wantedCursor != AppState::getInstance()->activeCursor) {
        AppState::getInstance()->activeCursor = wantedCursor;
        setCursor(wantedCursor);
    }
    
    return xplm_CursorCustom;
}

float update(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon) {
    if (!AppState::getInstance()->pluginInitialized) {
        return REFRESH_INTERVAL_SECONDS_SLOW;
    }

    Dataref::getInstance()->update();
    AppState::getInstance()->update();
    AppState::getInstance()->statusbar->update();

    AppState::getInstance()->browser->update();
    if (!AppState::getInstance()->browserVisible) {
        return REFRESH_INTERVAL_SECONDS_FAST;
    }
    
#ifndef DEBUG
    if (pressedKeyTime > 0 && XPLMGetElapsedTime() > pressedKeyTime + 0.3f) {
        AppState::getInstance()->browser->key(pressedKeyCode, pressedVirtualKeyCode);
    }
#endif
    
    if (AppState::getInstance()->mainWindow) {
        if (AppState::getInstance()->browser->hasInputFocus() != XPLMHasKeyboardFocus(AppState::getInstance()->mainWindow)) {
            if (AppState::getInstance()->browser->hasInputFocus()) {
                AppState::getInstance()->browser->setFocus(true);
                XPLMBringWindowToFront(AppState::getInstance()->mainWindow);
                XPLMTakeKeyboardFocus(AppState::getInstance()->mainWindow);
            }
            else {
                AppState::getInstance()->browser->setFocus(false);
                XPLMTakeKeyboardFocus(0);
            }
        }
    }

    return REFRESH_INTERVAL_SECONDS_FAST;
}

// Drives the CEF message loop once per sim frame while the browser is visible,
// so paint and input latency track the sim framerate instead of the fixed 0.1s
// update interval. On a fast machine the browser reaches its configured
// windowless_frame_rate; on a slow machine (10fps or less) this fires no more
// often than the old timer did, so it adds no load where none can be spared.
// While the browser is hidden or the plugin is inactive nothing is pumped and
// the callback just idles at the fast polling interval.
float pumpBrowser(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon) {
    if (!AppState::getInstance()->pluginInitialized || !AppState::getInstance()->browserVisible) {
        return REFRESH_INTERVAL_SECONDS_FAST;
    }

    AppState::getInstance()->browser->pump();

    // Mouse moves ride the same per-frame cadence; at 10Hz hover and drag felt
    // steppy.
    float mouseX, mouseY;
    if (Dataref::getInstance()->getMouse(&mouseX, &mouseY)) {
        AppState::getInstance()->browser->mouseMove(mouseX, mouseY);
    }

    return -1.0f;
}

int draw(XPLMDrawingPhase inPhase, int inIsBefore, void * inRefcon) {
    Drawing::DeleteQueuedTextures();
    AppState::getInstance()->draw();
    return 1;
}

void registerWindow() {
    if (AppState::getInstance()->mainWindow) {
        XPLMDestroyWindow(AppState::getInstance()->mainWindow);
        AppState::getInstance()->mainWindow = 0;
    }

    if (Dataref::getInstance()->get<bool>("sim/graphics/VR/enabled")) {
        return;
    }

    int winLeft, winTop, winRight, winBot;
    XPLMGetScreenBoundsGlobal(&winLeft, &winTop, &winRight, &winBot);
    XPLMCreateWindow_t params = {};
    params.structSize = windowStructSize();
    params.left = winLeft;
    params.right = winRight;
    params.top = winTop;
    params.bottom = winBot;
    params.visible = 1;
    params.refcon = nullptr;
    params.drawWindowFunc = [](XPLMWindowID, void *){};
    params.handleMouseClickFunc = mouseClicked;
    params.handleRightClickFunc = nullptr;
    params.handleMouseWheelFunc = mouseWheel;
    params.handleKeyFunc = keyPressed;
    params.handleCursorFunc = mouseCursor;
    params.layer = xplm_WindowLayerFlightOverlay;
    params.decorateAsFloatingWindow = xplm_WindowDecorationNone;

    AppState::getInstance()->mainWindow = XPLMCreateWindowEx(&params);
    XPLMSetWindowPositioningMode(AppState::getInstance()->mainWindow, xplm_WindowFullScreenOnMonitor, -1);

    XPLMBringWindowToFront(AppState::getInstance()->mainWindow);
}

void captureVrChanges() {
    Dataref::getInstance()->monitorExistingDataref<bool>("sim/graphics/VR/enabled", [](bool isVrEnabled) {
        registerWindow();

        if (isVrEnabled) {
            Logger::getInstance()->info("VR is now enabled.\n");
            Dataref::getInstance()->bindExistingCommand("sim/VR/reserved/select", [](XPLMCommandPhase inPhase) {
                if (inPhase == xplm_CommandBegin) {
                    mouseClicked(0, -1, -1, xplm_MouseDown, nullptr);
                }
                else if (inPhase == xplm_CommandContinue) {
                    mouseClicked(0, -1, -1, xplm_MouseDrag, nullptr);
                }
                else if (inPhase == xplm_CommandEnd) {
                    mouseClicked(0, -1, -1, xplm_MouseUp, nullptr);
                }

                return 1;
            });
        }
        else {
            Logger::getInstance()->info("VR is disabled.\n");
            Dataref::getInstance()->unbind("sim/VR/reserved/select");
            captureClickEvents(true);
        }
    });
}

void captureClickEvents(bool enable) {
    if (!AppState::getInstance()->shouldCaptureClickEvents) {
        return;
    }
    
    if (enable) {
        Logger::getInstance()->info("Start capturing AviTab click events.\n");
        Dataref::getInstance()->bindExistingCommand("AviTab/click_left", [](XPLMCommandPhase inPhase) {
            if (inPhase == xplm_CommandBegin) {
                mouseClicked(0, -1, -1, xplm_MouseDown, nullptr);
            }
            else if (inPhase == xplm_CommandContinue) {
                mouseClicked(0, -1, -1, xplm_MouseDrag, nullptr);
            }
            else if (inPhase == xplm_CommandEnd) {
                mouseClicked(0, -1, -1, xplm_MouseUp, nullptr);
            }
            
            return 1;
        });
    }
    else {
        Logger::getInstance()->info("Stopped capturing AviTab click events.\n");
        Dataref::getInstance()->unbind("AviTab/click_left");
    }
}

