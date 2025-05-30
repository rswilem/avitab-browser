#ifndef XPLM301
    #error This is made to be compiled against the XPLM301 for XP11 and XPLM410 SDK for XP12
#endif

#include "config.h"
#include "appstate.h"
#include "dataref.h"
#include "path.h"
#include <algorithm>
#include <XPLMDisplay.h>
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
int mouseClicked(XPLMWindowID inWindowID, int x, int y, XPLMMouseStatus status, void* inRefcon);
void menuAction(void* mRef, void* iRef);
void registerWindow();
void captureVrChanges();
void captureClickEvents(bool enable);

unsigned char pressedKeyCode = 0;
unsigned char pressedVirtualKeyCode = 0;
double pressedKeyTime = 0;

PLUGIN_API int XPluginStart(char * name, char * sig, char * desc)
{
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
    XPLMRegisterDrawCallback(draw, xplm_Phase_Gauges, 0, nullptr);
    
    XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, nullptr);
    
    captureVrChanges();
    initializeCursor();
    
    debug("Plugin started (version %s)\n", VERSION);
    
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
    XPLMUnregisterFlightLoopCallback(update, nullptr);
    XPLMDestroyWindow(AppState::getInstance()->mainWindow);
    AppState::getInstance()->mainWindow = nullptr;
    
    destroyCursor();
    captureClickEvents(false);
    
    AppState::getInstance()->deinitialize();
    debug("Plugin stopped\n");
}

PLUGIN_API int XPluginEnable(void) {
    Path::getInstance()->reloadPaths();
    
    if (AppState::getInstance()->mainWindow) {
        XPLMBringWindowToFront(AppState::getInstance()->mainWindow);
    }
    
    return 1;
}

PLUGIN_API void XPluginDisable(void) {
    debug("Disabling plugin...\n");
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
        XPLMCreateWindow_t params;
        float screenWidth = fabs(winLeft - winRight);
        float screenHeight = fabs(winTop - winBot);
        float width = 450.0f;
        float height = 180.0f;

        // Calculate centered position for the window
        params.structSize = sizeof(params);
        params.left = (int)(winLeft + (screenWidth - width) / 2);
        params.right = params.left + width;
        params.top = (int)(winTop - (screenHeight - height) / 2);
        params.bottom = params.top - height;
        params.visible = 1;
        params.refcon = nullptr;
        params.drawWindowFunc = [](XPLMWindowID inWindowID, void *drawingRef){
            XPLMSetGraphicsState(
                                 0, // No fog, equivalent to glDisable(GL_FOG);
                                 0, // One texture, equivalent to glEnable(GL_TEXTURE_2D);
                                 0, // No lighting, equivalent to glDisable(GL_LIGHT0);
                                 0, // No alpha testing, e.g glDisable(GL_ALPHA_TEST);
                                 1, // Use alpha blending, e.g. glEnable(GL_BLEND);
                                 0, // No depth read, e.g. glDisable(GL_DEPTH_TEST);
                                 0 // No depth write, e.g. glDepthMask(GL_FALSE);
            );
            glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
            
            int left, top, right, bottom;
            XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
            float color[] = {1.0f, 1.0f, 1.0f};
            
            float x = left + 16.0f;
            float y = top - 16.0f;
            XPLMDrawString(color, x, y, FRIENDLY_NAME, nullptr, xplmFont_Proportional);
            y -= 16.0f;
            XPLMDrawString(color, x, y, "Version " VERSION, nullptr, xplmFont_Proportional);
            y -= 32.0f;
            XPLMDrawString(color, x, y, "This software is licensed under the GNU General Public License, GPL-3.0", nullptr, xplmFont_Proportional);
            y -= 32.0f;
            XPLMDrawString(color, x, y, "For updates to " FRIENDLY_NAME ", please see the forums at x-plane.org", nullptr, xplmFont_Proportional);
            y -= 16.0f;
            XPLMDrawString(color, x, y, "or checkout the GitHub releases at github.com/rswilem/avitab-browser.", nullptr, xplmFont_Proportional);
            y -= 16.0f;
            XPLMDrawString(color, x, y, "Made with love by TheRamon, thank you for using this software!", nullptr, xplmFont_Proportional);
        };
        
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

    float mouseX, mouseY;
    if (Dataref::getInstance()->getMouse(&mouseX, &mouseY)) {
        AppState::getInstance()->browser->mouseMove(mouseX, mouseY);
    }
    
    return REFRESH_INTERVAL_SECONDS_FAST;
}

int draw(XPLMDrawingPhase inPhase, int inIsBefore, void * inRefcon) {
    AppState::getInstance()->draw();
    return 1;
}

void registerWindow() {
    if (AppState::getInstance()->mainWindow) {
        XPLMDestroyWindow(AppState::getInstance()->mainWindow);
        AppState::getInstance()->mainWindow = 0;
    }
    
    int winLeft, winTop, winRight, winBot;
    XPLMGetScreenBoundsGlobal(&winLeft, &winTop, &winRight, &winBot);
    XPLMCreateWindow_t params;
    params.structSize = sizeof(params);
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
    params.layer = Dataref::getInstance()->get<bool>("sim/graphics/VR/enabled") ? xplm_WindowLayerFloatingWindows : xplm_WindowLayerFlightOverlay;
    params.decorateAsFloatingWindow = xplm_WindowDecorationNone;
    
    AppState::getInstance()->mainWindow = XPLMCreateWindowEx(&params);
    XPLMSetWindowPositioningMode(AppState::getInstance()->mainWindow, xplm_WindowFullScreenOnMonitor, -1);
    
    XPLMBringWindowToFront(AppState::getInstance()->mainWindow);
}

void captureVrChanges() {
    std::function setVrWindowPositioningMode = [](){
        XPLMBringWindowToFront(AppState::getInstance()->mainWindow);
        
        if (Dataref::getInstance()->get<bool>("sim/graphics/VR/using_3d_mouse")) {
            XPLMSetWindowPositioningMode(AppState::getInstance()->mainWindow, xplm_WindowVR, -1);
        }
        else {
            XPLMSetWindowPositioningMode(AppState::getInstance()->mainWindow, xplm_WindowFullScreenOnMonitor, -1);
        }
    };
    
    Dataref::getInstance()->monitorExistingDataref<bool>("sim/graphics/VR/enabled", [setVrWindowPositioningMode](bool isVrEnabled) {
        registerWindow();
        
        if (isVrEnabled) {
            setVrWindowPositioningMode();
            
            debug("VR is now enabled.\n");
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
            debug("VR is disabled.\n");
            Dataref::getInstance()->unbind("sim/VR/reserved/select");
            setVrWindowPositioningMode();
        }
    });
    
    Dataref::getInstance()->monitorExistingDataref<bool>("sim/graphics/VR/using_3d_mouse", [setVrWindowPositioningMode](bool isVrUsingMouse) {
        setVrWindowPositioningMode();
    });
}

void captureClickEvents(bool enable) {
    if (!AppState::getInstance()->shouldCaptureClickEvents) {
        return;
    }
    
    if (enable) {
        debug("Start capturing AviTab click events.\n");
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
        debug("Stopped capturing AviTab click events.\n");
        Dataref::getInstance()->unbind("AviTab/click_left");
    }
}

