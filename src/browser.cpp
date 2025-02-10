#include "browser.h"
#include "config.h"
#include "path.h"
#include "appstate.h"
#include "dataref.h"
#include "browser_handler.h"
#include "drawing.h"
#include <iomanip>
#include <sstream>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <include/cef_app.h>
#include <include/cef_base.h>
#include <include/base/cef_callback.h>
#include <include/cef_version.h>
#include <include/cef_browser.h>
#include <include/cef_client.h>
#include <include/cef_render_handler.h>
#include <include/cef_command_line.h>
#include <include/wrapper/cef_helpers.h>
#include <include/base/cef_bind.h>
#include <include/wrapper/cef_closure_task.h>
#include <include/cef_request_context_handler.h>
#include <XPLMUtilities.h>
#include <XPLMGraphics.h>
#include <XPLMProcessing.h>
#include <XPLMDisplay.h>

#if APL
#include "unix_keycodes.h"
#include <include/wrapper/cef_library_loader.h>
#elif LIN
#include "unix_keycodes.h"
#endif

Browser::Browser() {
    textureId = 0;
    offsetStart = 0.0f;
    offsetEnd = 0.0f;
    lastGpsUpdateTime = 0.0f;
    backButton = nullptr;
    handler = nullptr;
    currentUrl = "";
}

void Browser::initialize() {
    if (textureId || handler) {
        return;
    }

    if (AppState::getInstance()->aircraftVariant == VariantZibo738) {
        offsetStart = 0.022f;
        offsetEnd = 0.977f;
        
        backButton = new Button(0.27f, 0.09f);
        backButton->setPosition(0.15f, -0.019f);
        backButton->setClickHandler([]() {
            if (!AppState::getInstance()->browserVisible) {
                return false;
            }
            
            bool didGoBack = AppState::getInstance()->browser->goBack();
            if (!didGoBack) {
                Dataref::getInstance()->executeCommand("laminar/B738/tab/home");
            }
            
            return true;
        });
    }
    else if (AppState::getInstance()->aircraftVariant == VariantLevelUp737) {
        offsetStart = 0.05f;
        offsetEnd = 1.0f;
        
        backButton = new Button(0.27f, 0.10f);
        backButton->setPosition(0.15f, -0.014f);
        backButton->setClickHandler([]() {
            if (!AppState::getInstance()->browserVisible) {
                return false;
            }
            
            bool didGoBack = AppState::getInstance()->browser->goBack();
            if (!didGoBack) {
                Dataref::getInstance()->executeCommand("laminar/B738/tab/home");
            }
            
            return true;
        });
    }
    else if (AppState::getInstance()->aircraftVariant == VariantFelis742) {
        offsetStart = -0.11f;
        offsetEnd = 1.06f;
        
        backButton = backButton = new Button(0.27f, 0.05);
        backButton->setPosition(0.5f, 1.092f);
        backButton->setClickHandler([]() {
            if (!AppState::getInstance()->browserVisible) {
                return false;
            }
         
            AppState::getInstance()->browserVisible = false;
            Dataref::getInstance()->executeCommand("AviTab/Home");
            
            // Intentionally return false so commands bubble up to the airplane.
            return false;
        });
    }
    else {
        offsetStart = 0;
        offsetEnd = 0.935f;
        
        backButton = new Button(Path::getInstance()->pluginDirectory + (AppState::getInstance()->config.hide_addressbar ? "/assets/icons/arrow-left-circle.svg" : "/assets/icons/x-circle.svg"));
        backButton->setPosition(backButton->relativeWidth / 2.0f + 0.01f, 0.967f);
        backButton->setClickHandler([]() {
            if (!AppState::getInstance()->browserVisible) {
                return false;
            }
         
            if (!AppState::getInstance()->config.hide_addressbar) {
                Dataref::getInstance()->executeCommand("AviTab/Home");
                return true;
            }
            
            bool didGoBack = AppState::getInstance()->browser->goBack();
            if (!didGoBack) {
                Dataref::getInstance()->executeCommand("AviTab/Home");
            }
            
            return true;
        });
    }
    
    XPLMGenerateTextureNumbers(&textureId, 1);
    XPLMBindTexture2d(textureId, 0);
    std::vector<unsigned char> whiteTextureData(
        AppState::getInstance()->tabletDimensions.textureWidth *
        AppState::getInstance()->tabletDimensions.textureHeight *
        AppState::getInstance()->tabletDimensions.bytesPerPixel
    );
    std::fill(whiteTextureData.begin(), whiteTextureData.end(), 0xFF);

    glTexImage2D(
                 GL_TEXTURE_2D,
                 0,                   // mipmap level
                 GL_RGBA,             // internal format for the GL to use.  (We could ask for a floating point tex or 16-bit tex if we were crazy!)
                 AppState::getInstance()->tabletDimensions.textureWidth,
                 AppState::getInstance()->tabletDimensions.textureHeight,
                 0,                   // border size
                 GL_BGRA,             // format of color we are giving to GL
                 GL_UNSIGNED_BYTE,    // encoding of our data
                 whiteTextureData.data()
                 );
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    currentUrl = AppState::getInstance()->config.homepage;
    Dataref::getInstance()->createDataref<std::string>("avitab_browser/url", &currentUrl, true, [this](void *valuePtr) {
        std::string newUrl = std::string(static_cast<const char *>(valuePtr));
        
        if (!newUrl.starts_with("http")) {
            return false;
        }
        
        loadUrl(newUrl);
        return true;
    });
}

void Browser::destroy() {
    if (handler && handler->browserInstance) {
        handler->browserInstance->GetHost()->CloseBrowser(true);
        
        auto startTime = std::chrono::steady_clock::now() + std::chrono::seconds(99);
        auto gracePeriod = std::chrono::milliseconds(500);
        while (1) {
            // Get some message loop reps in so the browser can properly close.
            CefDoMessageLoopWork();
            
            if (!handler->browserInstance && startTime > std::chrono::steady_clock::now()) {
                // The browser has closed. Start grace countdown.
                startTime = std::chrono::steady_clock::now();
            }
            else if (std::chrono::steady_clock::now() - startTime > gracePeriod) {
                break;
            }
        }
        
        handler->destroy();
        handler = nullptr;
        
        // Never call CefShutdown(); since this makes all further CefInitialize(); crash.
        // #if IBM
        // debug("Cleaning up CEF instance...\n");
        // CefShutdown();
        // #endif
    }
    
    if (textureId) {
        XPLMBindTexture2d(textureId, 0);
        glDeleteTextures(1, (GLuint *)&textureId);
        textureId = 0;
    }
    
    if (backButton) {
        backButton->destroy();
        backButton = nullptr;
    }
}

void Browser::visibilityWillChange(bool becomesVisible) {
    if (becomesVisible && !handler) {
        createBrowser();
    }
    
    lastGpsUpdateTime = becomesVisible ? XPLMGetElapsedTime() : 0.0f;
}

void Browser::update() {
    if (!textureId) {
        return;
    }
    
    if (handler && AppState::getInstance()->browserVisible) {
        CefDoMessageLoopWork();
    }
    
    if (backButton) {
        backButton->visible = AppState::getInstance()->browserVisible;
    }
    
    if (lastGpsUpdateTime > __FLT_EPSILON__ && XPLMGetElapsedTime() > lastGpsUpdateTime + 1.0f) {
        updateGPSLocation();
    }
}

void Browser::draw() {
    if (!textureId) {
        return;
    }
    
    XPLMSetGraphicsState(
                         0, // No fog, equivalent to glDisable(GL_FOG);
                         1, // One texture, equivalent to glEnable(GL_TEXTURE_2D);
                         0, // No lighting, equivalent to glDisable(GL_LIGHT0);
                         0, // No alpha testing, e.g glDisable(GL_ALPHA_TEST);
                         1, // Use alpha blending, e.g. glEnable(GL_BLEND);
                         0, // No depth read, e.g. glDisable(GL_DEPTH_TEST);
                         0 // No depth write, e.g. glDepthMask(GL_FALSE);
    );
    
    XPLMBindTexture2d(textureId, 0);
    
    const auto& tabletDimensions = AppState::getInstance()->tabletDimensions;
    int x1 = tabletDimensions.x;
    int y1 = tabletDimensions.y + tabletDimensions.height * offsetStart;
    int x2 = x1 + tabletDimensions.width;
    int y2 = y1 + tabletDimensions.height * (offsetEnd - offsetStart);
    
    glBegin(GL_QUADS);
    set_brightness(AppState::getInstance()->brightness);
    
    float u = (float)tabletDimensions.browserWidth / tabletDimensions.textureWidth;
    float v = (float)tabletDimensions.browserHeight / tabletDimensions.textureHeight;
    
    glTexCoord2f(0, v);
    glVertex2f(x1,y1);
    glTexCoord2f(0, 0);
    glVertex2f(x1,y2);
    glTexCoord2f(u, 0);
    glVertex2f(x2,y2);
    glTexCoord2f(u, v);
    glVertex2f(x2,y1);
    glEnd();
    
    if (backButton) {
        backButton->draw();
    }
}

void Browser::mouseMove(float normalizedX, float normalizedY) {
    if (!textureId || !handler || !handler->browserInstance) {
        return;
    }
    
    if (normalizedX < 0 || normalizedX > 1 || normalizedY < offsetStart || normalizedY > offsetEnd) {
        return;
    }
    
    CefMouseEvent mouseEvent = getMouseEvent(normalizedX, normalizedY);
    handler->browserInstance->GetHost()->SendMouseMoveEvent(mouseEvent, false);
}

 bool Browser::click(XPLMMouseStatus status, float normalizedX, float normalizedY) {
     if (!textureId || !handler || !handler->browserInstance) {
         return false;
     }
     
     if (normalizedX < 0 || normalizedX > 1 || normalizedY < offsetStart || normalizedY > offsetEnd) {
         return false;
     }
    
     CefMouseEvent mouseEvent = getMouseEvent(normalizedX, normalizedY);
     if (mouseEvent.y < 0) {
         return false;
     }
    
     if (status == xplm_MouseDown) {
         handler->browserInstance->GetHost()->SendMouseClickEvent(mouseEvent, MBT_LEFT, false, 1);
     }
     else if (status == xplm_MouseDrag) {
         // Yes, we already send this event in mouseMove(). Adding the line below makes it more responsive.
         handler->browserInstance->GetHost()->SendMouseMoveEvent(mouseEvent, false);
     }
     else {
         handler->browserInstance->GetHost()->SendMouseClickEvent(mouseEvent, MBT_LEFT, true, 1);
     }
    
     return true;
 }

void Browser::scroll(float normalizedX, float normalizedY, int clicks, bool horizontal = false) {
    if (!textureId || !handler || !handler->browserInstance) {
        return;
    }
    
    if (normalizedX < 0 || normalizedX > 1 || normalizedY < offsetStart || normalizedY > offsetEnd) {
        return;
    }
    
    CefMouseEvent mouseEvent = getMouseEvent(normalizedX, normalizedY);
    mouseEvent.modifiers = EVENTFLAG_NONE;
    handler->browserInstance->GetHost()->SendMouseWheelEvent(mouseEvent, horizontal ? clicks : 0, horizontal ? 0 : clicks);
}

void Browser::loadUrl(std::string url) {
    if (!textureId || !handler) {
        currentUrl = url;
        return;
    }
    
    currentUrl = url;
    if (handler->browserInstance) {
        handler->browserInstance->GetMainFrame()->LoadURL(url);
    }
}

bool Browser::hasInputFocus() {
    if (!textureId || !handler) {
        return false;
    }
    
    return handler->hasInputFocus;
}

void Browser::setFocus(bool focus) {
    if (!textureId || !handler || !handler->browserInstance) {
        return;
    }
    
    handler->browserInstance->GetHost()->SetFocus(focus);
    if (!focus && handler->hasInputFocus) {
        std::string script = "document.activeElement?.blur();";
        handler->browserInstance->GetMainFrame()->ExecuteJavaScript(script, "about:blank", 0);
    }
}

void Browser::key(unsigned char key, unsigned char virtualKey, XPLMKeyFlags flags) {
    if (!textureId || !handler) {
        return;
    }
    
    CefKeyEvent keyEvent;
    keyEvent.type = (flags == 0 || (flags & xplm_DownFlag) == xplm_DownFlag) ? KEYEVENT_KEYDOWN : KEYEVENT_KEYUP;
    
#if IBM
    wchar_t utf16Character;
    MultiByteToWideChar(CP_UTF8, 0, (char*)&key, 1, &utf16Character, 1);
    keyEvent.windows_key_code = virtualKey;
    keyEvent.native_key_code = MapVirtualKey(virtualKey, MAPVK_VK_TO_VSC);
    keyEvent.character = utf16Character;
    keyEvent.unmodified_character = keyEvent.character;
#else
    auto it = virtualKeycodeToUnixKeycode.find(virtualKey);
    if (it != virtualKeycodeToUnixKeycode.end()) {
        int keyCode = it->second;
        keyEvent.native_key_code = keyCode;
    }
    else {
        debug("Unknown key: 0x%02X VK: 0x%02X\n", key, virtualKey);
        keyEvent.native_key_code = key;
    }
    keyEvent.windows_key_code = virtualKey;
    keyEvent.character = key;
    keyEvent.unmodified_character = keyEvent.character;
#endif

    keyEvent.is_system_key = false;
    keyEvent.modifiers = 0;
    if ((flags & xplm_ShiftFlag) == xplm_ShiftFlag) {
        keyEvent.modifiers |= EVENTFLAG_SHIFT_DOWN;
    }
    
    if ((flags & xplm_OptionAltFlag) == xplm_OptionAltFlag) {
        keyEvent.modifiers |= EVENTFLAG_ALT_DOWN;
    }
    
    if ((flags & xplm_ControlFlag) == xplm_ControlFlag) {
        keyEvent.modifiers |= EVENTFLAG_CONTROL_DOWN;
        //keyEvent.modifiers |= EVENTFLAG_COMMAND_DOWN;
    }
    
    handler->browserInstance->GetHost()->SendKeyEvent(keyEvent);
    
    if (keyEvent.type == KEYEVENT_KEYDOWN && isprint(key)) {
        CefKeyEvent textEvent;
        textEvent.type = KEYEVENT_CHAR;
        textEvent.character = keyEvent.character;
        textEvent.unmodified_character = keyEvent.unmodified_character;
        textEvent.native_key_code = keyEvent.native_key_code;
        textEvent.windows_key_code = keyEvent.character;
        
        handler->browserInstance->GetHost()->SendKeyEvent(textEvent);
    }
}

bool Browser::goBack() {
    if (!textureId || !handler) {
        return false;
    }
    
    if (!handler->browserInstance->CanGoBack()) {
        return false;
    }
    
    handler->browserInstance->GoBack();
    return true;
}

CursorType Browser::cursor() {
    if (!handler) {
        return CursorDefault;
    }
    
    return handler->cursorState;
}

bool Browser::createBrowser() {
    if (handler && handler->browserInstance) {
        return false;
    }
    
#if APL
    #if XPLM410
        CefScopedLibraryLoader library_loader;
        if (!library_loader.LoadInMain()) {
            debug("Could not load CEF library dylib (CefScopedLibraryLoader)!\n");
            return false;
        }
    #else
        cef_load_library((Path::getInstance()->pluginDirectory + "/mac_x64/Chromium Embedded Framework.framework/Chromium Embedded Framework").c_str());
    #endif
#endif
    
    std::string cachePath = Path::getInstance()->pluginDirectory + "/cache";
    if (!std::filesystem::exists(cachePath)) {
        std::filesystem::create_directories(cachePath);
    }
    
    CefRequestContextSettings context_settings;
    CefString(&context_settings.cache_path) = cachePath;
    
    std::string language = "";
    switch (XPLMLanguageCode()) {
        case xplm_Language_English:
            language = "en-US,en";
            break;
            
        case xplm_Language_French:
            language = "fr-FR,fr";
            break;
            
        case xplm_Language_German:
            language = "de-DE,de";
            break;
            
        case xplm_Language_Italian:
            language = "it-IT,it";
            break;
            
        case xplm_Language_Spanish:
            language = "es-ES,es";
            break;
            
        case xplm_Language_Korean:
            language = "ko-KR,ko";
            break;
            
        case xplm_Language_Russian:
            language = "ru-RU,ru";
            break;
            
        case xplm_Language_Greek:
            language = "el-GR,el";
            break;
            
        case xplm_Language_Japanese:
            language = "ja-JP,ja";
            break;
            
        case xplm_Language_Chinese:
            language = "zh-CN,zh";
            break;

#ifdef XPLM410
        case xplm_Language_Ukrainian:
            language = "uk-UA,uk";
            break;
#endif
            
        case xplm_Language_Unknown:
        default:
            break;
    }
    
    if (!AppState::getInstance()->config.forced_language.empty()) {
        language = AppState::getInstance()->config.forced_language;
    }
    
    if (!language.empty()) {
        CefString(&context_settings.accept_language_list) = language;
    }
    
    context_settings.persist_user_preferences = true;
    context_settings.persist_session_cookies = true;
    CefRefPtr<CefRequestContext> request_context = CefRequestContext::CreateContext(context_settings, nullptr);
    
    CefBrowserSettings browser_settings;
    browser_settings.windowless_frame_rate = AppState::getInstance()->config.framerate;
    browser_settings.background_color = CefColorSetARGB(0xFF, 0xFF, 0xFF, 0xFF);

#ifndef XPLM410
    // CEF is not automatically loaded when starting X-Plane 11. Initialize CEF.
    CefRefPtr<CefApp> app;
    CefSettings settings;
    settings.windowless_rendering_enabled = true;
    CefString(&settings.cache_path) = cachePath;
    
#if IBM
    CefMainArgs main_args(GetModuleHandle(nullptr));
    CefString(&settings.resources_dir_path) = Path::getInstance()->pluginDirectory + "/win_x64/res";
    CefString(&settings.locales_dir_path) = Path::getInstance()->pluginDirectory + "/win_x64/res/locales";
    CefString(&settings.browser_subprocess_path) = Path::getInstance()->pluginDirectory + "/win_x64/avitab_cef_helper.exe";
#elif APL
    settings.no_sandbox = true;
    CefMainArgs main_args;
    CefString(&settings.locales_dir_path) = Path::getInstance()->pluginDirectory + "/mac_x64/Chromium Embedded Framework.framework/Resources";
    CefString(&settings.resources_dir_path) = Path::getInstance()->pluginDirectory + "/mac_x64/Chromium Embedded Framework.framework/Resources";
    CefString(&settings.main_bundle_path) = Path::getInstance()->pluginDirectory + "/mac_x64/cefclient Helper.app";
    CefString(&settings.framework_dir_path) = Path::getInstance()->pluginDirectory + "/mac_x64/Chromium Embedded Framework.framework";
    CefString(&settings.browser_subprocess_path) = Path::getInstance()->pluginDirectory + "/mac_x64/cefclient Helper.app/Contents/MacOS/cefclient Helper";
#elif LIN
    CefMainArgs main_args;
#endif
    
    debug("Initializing a new CEF instance for X-Plane 11...\n");
    if (!CefInitialize(main_args, settings, app, nullptr)) {
        debug("Could not initialize CEF instance.\n");
        return false;
    }
    debug("CEF instance for X-Plane 11 has been set up successfully.\n");
#endif
    
    handler = CefRefPtr<BrowserHandler>(new BrowserHandler(textureId, &currentUrl, AppState::getInstance()->tabletDimensions.browserWidth, AppState::getInstance()->tabletDimensions.browserHeight));
    
    CefWindowInfo window_info;
#if LIN
    window_info.SetAsWindowless(0);
#else
    window_info.SetAsWindowless(nullptr);
#endif
    //window_info.shared_texture_enabled
    window_info.windowless_rendering_enabled = true;
    
    bool browserCreated = CefBrowserHost::CreateBrowser(window_info, handler, currentUrl, browser_settings, nullptr, request_context);
    if (!browserCreated) {
        AppState::getInstance()->showNotification(new Notification("Error creating browser", "An error occured while starting the browser.\nPlease verify if there are any updates for the " FRIENDLY_NAME " plugin and try again."));
    }
    
    return true;
}

void Browser::updateGPSLocation() {
    if (!handler || !handler->browserInstance) {
        return;
    }
    
    float latitude = Dataref::getInstance()->get<float>("sim/flightmodel/position/latitude");
    float longitude = Dataref::getInstance()->get<float>("sim/flightmodel/position/longitude");
    float speedMetersSecond = Dataref::getInstance()->get<float>("sim/flightmodel/position/groundspeed");
    float altitudeMeters = Dataref::getInstance()->get<float>("sim/flightmodel/position/y_agl");
    float magneticHeading = Dataref::getInstance()->get<float>("sim/flightmodel/position/mag_psi");
    
    std::stringstream stream;
    stream << "window.avitab_location = { ";
    stream << "coords: { ";
    stream << "latitude: " << std::fixed << std::setprecision(6) << latitude << ", ";
    stream << "longitude: " << std::fixed << std::setprecision(6) << longitude << ", ";
    stream << "accuracy: 10, ";
    stream << "altitude: " << std::fixed << std::setprecision(0) << altitudeMeters << ", ";
    stream << "altitudeAccuracy: 10, ";
    stream << "heading: " << std::fixed << std::setprecision(0) << magneticHeading << ", ";
    stream << "speed: " << std::fixed << std::setprecision(0) << speedMetersSecond << ", ";
    stream <<  "}, timestamp: Date.now() }; for (let key in window.avitab_watchers) { window.avitab_watchers[key](window.avitab_location); }";
    
    handler->browserInstance->GetMainFrame()->ExecuteJavaScript(stream.str(), "about:blank", 0);
    lastGpsUpdateTime = XPLMGetElapsedTime();
}

CefMouseEvent Browser::getMouseEvent(float normalizedX, float normalizedY) {
    const auto& tabletDimensions = AppState::getInstance()->tabletDimensions;

    CefMouseEvent mouseEvent;
    mouseEvent.x = tabletDimensions.browserWidth * normalizedX;
    mouseEvent.y = tabletDimensions.browserHeight * (1.0f - ((normalizedY - offsetStart) / (offsetEnd - offsetStart)));
    return mouseEvent;
}
