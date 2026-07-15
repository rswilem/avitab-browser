#include "browser.h"

#include "appstate.h"
#include "browser_handler.h"
#include "config.h"
#include "dataref.h"
#include "drawing.h"
#include "path.h"

#include <chrono>
#include <cmath>
#include <thread>
#include <filesystem>
#include <fstream>
#include <include/base/cef_bind.h>
#include <include/base/cef_callback.h>
#include <include/cef_app.h>
#include <include/cef_base.h>
#include <include/cef_browser.h>
#include <include/cef_client.h>
#include <include/cef_command_line.h>
#include <include/cef_render_handler.h>
#include <include/cef_request_context_handler.h>
#include <include/cef_version.h>
#include <include/wrapper/cef_closure_task.h>
#include <include/wrapper/cef_helpers.h>
#include <iomanip>
#include <sstream>
#include <XPLMDisplay.h>
#include <XPLMGraphics.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#if APL
#include "unix_keycodes.h"

#include <include/wrapper/cef_library_loader.h>
#elif LIN
#include "unix_keycodes.h"
#elif IBM
#include <windows.h>
#endif

Browser::Browser() {
    textureId = 0;
    textureInitialized = false;
    offsetStart = 0.0f;
    offsetEnd = 0.0f;
    lastGpsUpdateTime = 0.0f;
    backButton = nullptr;
    handler = nullptr;
    currentUrl = "";
    leftMouseButtonDown = false;
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
    } else if (AppState::getInstance()->aircraftVariant == VariantLevelUp737) {
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
    } else if (AppState::getInstance()->aircraftVariant == VariantFelis742) {
        offsetStart = -0.11f;
        offsetEnd = 1.06f;

        backButton = new Button(0.27f, 0.05f);
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
    } else if (AppState::getInstance()->aircraftVariant == VariantAirfoillabsC172) {
        offsetStart = 0;
        offsetEnd = 1.0f;

        backButton = new Button(Path::getInstance()->pluginDirectory + (AppState::getInstance()->config.hide_addressbar ? "/assets/icons/arrow-left-circle.svg" : "/assets/icons/x-circle.svg"));
        backButton->setPosition(backButton->relativeWidth / 2.0f + 0.01f, 1.03f);
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
    } else {
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

    // Only reserve the texture number here; the GL allocation happens in
    // initializeTexture() on the first draw() call, because plugin GL is only
    // valid inside a draw callback under the XP12 Metal renderer.
    XPLMGenerateTextureNumbers(&textureId, 1);
    textureInitialized = false;

    currentUrl = AppState::getInstance()->config.homepage;

    Dataref::getInstance()->createDataref<std::string>("avitab_browser/url", &currentUrl, true, [this](std::string newUrl) {
        if (!newUrl.starts_with("http") && !newUrl.starts_with("chrome://") && !newUrl.starts_with("data:")) {
            return false;
        }

        loadUrl(newUrl);
        return true;
    });

    Dataref::getInstance()->createCommand("avitab_browser/refresh", "Refresh the current web page", [this](XPLMCommandPhase inPhase) {
        if (inPhase != xplm_CommandBegin) {
            return;
        }

        if (handler && handler->browserInstance) {
            handler->browserInstance->Reload();
        }
    });
}

void Browser::destroy() {
    if (handler && handler->browserInstance) {
        handler->browserInstance->GetHost()->CloseBrowser(true);

        // Pump the CEF message loop so the browser can close cleanly, then give
        // it a short grace period. A hard deadline guarantees we return even if
        // the browser never signals closure (e.g. a hung renderer), instead of
        // spinning the main thread and freezing the sim.
        constexpr auto maxWait = std::chrono::seconds(3);
        constexpr auto gracePeriod = std::chrono::milliseconds(500);
        auto deadline = std::chrono::steady_clock::now() + maxWait;
        auto graceEnd = std::chrono::steady_clock::time_point::max();
        while (std::chrono::steady_clock::now() < deadline) {
            // Get some message loop reps in so the browser can properly close.
            CefDoMessageLoopWork();

            if (!handler->browserInstance) {
                if (graceEnd == std::chrono::steady_clock::time_point::max()) {
                    // The browser has closed. Start the grace countdown.
                    graceEnd = std::chrono::steady_clock::now() + gracePeriod;
                } else if (std::chrono::steady_clock::now() >= graceEnd) {
                    break;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Never call CefShutdown(); since this makes all further CefInitialize(); crash.
        // #if IBM
        // Logger::getInstance()->info("Cleaning up CEF instance...\n");
        // CefShutdown();
        // #endif
    }

    // Always release the handler, even when the browser instance was already
    // gone (e.g. a page called window.close). Leaving it set makes the next
    // initialize() bail out early while the texture below is discarded, which
    // ends in a permanently blank browser until the aircraft is reloaded.
    if (handler) {
        handler->destroy();
        handler = nullptr;
    }

    if (textureId) {
        // Deleting the texture is a GL call too; defer it to the draw callback.
        Drawing::QueueTextureDeletion(textureId);
        textureId = 0;
        textureInitialized = false;
    }

    if (backButton) {
        backButton->destroy();
        delete backButton;
        backButton = nullptr;
    }
}

void Browser::initializeTexture() {
    if (!textureId || textureInitialized) {
        return;
    }

    XPLMBindTexture2d(textureId, 0);
    std::vector<unsigned char> whiteTextureData(
        AppState::getInstance()->tabletDimensions.textureWidth *
        AppState::getInstance()->tabletDimensions.textureHeight *
        AppState::getInstance()->tabletDimensions.bytesPerPixel);
    std::fill(whiteTextureData.begin(), whiteTextureData.end(), 0xFF);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,       // mipmap level
        GL_RGBA, // internal format for the GL to use.  (We could ask for a floating point tex or 16-bit tex if we were crazy!)
        AppState::getInstance()->tabletDimensions.textureWidth,
        AppState::getInstance()->tabletDimensions.textureHeight,
        0,                // border size
        GL_BGRA,          // format of color we are giving to GL
        GL_UNSIGNED_BYTE, // encoding of our data
        whiteTextureData.data());

    // The texture is virtually never sampled 1:1 (3D cockpit projection, VR,
    // minimum_width upscaling), so linear filtering renders visibly better.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    textureInitialized = true;
}

void Browser::visibilityWillChange(bool becomesVisible) {
    if (becomesVisible) {
        // If handler exists but browserInstance is null, the browser was closed by a page
        // Clear the handler so we can create a fresh one
        if (handler && !handler->browserInstance) {
            handler = nullptr;
        }

        if (!handler) {
            createBrowser();
        }
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

// Runs inside the xplm_Phase_Gauges draw callback: the only place where this
// plugin may touch OpenGL. All texture allocation and pixel uploads happen here.
void Browser::draw() {
    if (!textureId) {
        return;
    }

    if (!textureInitialized) {
        initializeTexture();
    }

    if (handler) {
        handler->uploadPendingPaint();
    }

    XPLMSetGraphicsState(
        0, // No fog, equivalent to glDisable(GL_FOG);
        1, // One texture, equivalent to glEnable(GL_TEXTURE_2D);
        0, // No lighting, equivalent to glDisable(GL_LIGHT0);
        0, // No alpha testing, e.g glDisable(GL_ALPHA_TEST);
        1, // Use alpha blending, e.g. glEnable(GL_BLEND);
        0, // No depth read, e.g. glDisable(GL_DEPTH_TEST);
        0  // No depth write, e.g. glDepthMask(GL_FALSE);
    );

    XPLMBindTexture2d(textureId, 0);

#if DEBUG
    // Live-tune the image offsets via config.ini [debug] section. Edit
    // debug_value_1 (offsetStart) / debug_value_2 (offsetEnd), then use the
    // "Reload configuration" menu item to see the change immediately. Leave
    // debug_value_2 at 0 to keep the per-aircraft branch values.
    if (AppState::getInstance()->config.debug_value_2 != 0.0f) {
        offsetStart = AppState::getInstance()->config.debug_value_1;
        offsetEnd = AppState::getInstance()->config.debug_value_2;
    }
#endif

    const auto &tabletDimensions = AppState::getInstance()->tabletDimensions;
    int x1 = tabletDimensions.x;
    int y1 = tabletDimensions.y + tabletDimensions.height * offsetStart;
    int x2 = x1 + tabletDimensions.width;
    int y2 = y1 + tabletDimensions.height * (offsetEnd - offsetStart);

    glBegin(GL_QUADS);
    set_brightness(AppState::getInstance()->brightness);

    float u = (float) tabletDimensions.browserWidth / tabletDimensions.textureWidth;
    float v = (float) tabletDimensions.browserHeight / tabletDimensions.textureHeight;

    glTexCoord2f(0, v);
    glVertex2f(x1, y1);
    glTexCoord2f(0, 0);
    glVertex2f(x1, y2);
    glTexCoord2f(u, 0);
    glVertex2f(x2, y2);
    glTexCoord2f(u, v);
    glVertex2f(x2, y1);
    glEnd();

    if (backButton) {
#if DEBUG
        // Live-tune the back button Y via the shared header Y (config.ini
        // [debug] debug_value_3, which also moves the spinner and status bar
        // icons). Leave at 0 to keep the per-aircraft position. X stays at the
        // branch default (button half-width + 0.01).
        if (AppState::getInstance()->config.debug_value_3 != 0.0f) {
            backButton->setPosition(backButton->relativeWidth / 2.0f + 0.01f, AppState::getInstance()->config.debug_value_3);
        }
#endif
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
    if (leftMouseButtonDown) {
        mouseEvent.modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    }
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
        leftMouseButtonDown = true;
        handler->browserInstance->GetHost()->SendMouseClickEvent(mouseEvent, MBT_LEFT, false, 1);
    } else if (status == xplm_MouseDrag) {
        // Yes, we already send this event in mouseMove(). Adding the line below makes it more responsive.
        mouseEvent.modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
        handler->browserInstance->GetHost()->SendMouseMoveEvent(mouseEvent, false);
    } else {
        leftMouseButtonDown = false;
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
        handler->browserInstance->GetMainFrame()->ExecuteJavaScript(script, handler->browserInstance->GetMainFrame()->GetURL(), 0);
    }
}

void Browser::key(unsigned char key, unsigned char virtualKey, XPLMKeyFlags flags) {
    if (!textureId || !handler || !handler->browserInstance) {
        return;
    }

    CefKeyEvent keyEvent;
    keyEvent.type = (flags == 0 || (flags & xplm_DownFlag) == xplm_DownFlag) ? KEYEVENT_KEYDOWN : KEYEVENT_KEYUP;

#if IBM
    wchar_t utf16Character;
    MultiByteToWideChar(CP_UTF8, 0, (char *) &key, 1, &utf16Character, 1);
    keyEvent.windows_key_code = virtualKey;
    keyEvent.native_key_code = MapVirtualKey(virtualKey, MAPVK_VK_TO_VSC);
    keyEvent.character = utf16Character;
    keyEvent.unmodified_character = keyEvent.character;
#else
    auto it = virtualKeycodeToUnixKeycode.find(virtualKey);
    if (it != virtualKeycodeToUnixKeycode.end()) {
        int keyCode = it->second;
        keyEvent.native_key_code = keyCode;
    } else {
        Logger::getInstance()->warn("Unknown key: 0x%02X VK: 0x%02X\n", key, virtualKey);
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

        if (key == 'a') {
            if (keyEvent.type == KEYEVENT_KEYDOWN) {
                handler->browserInstance->GetMainFrame()->SelectAll();
            }
            return;
        } else if (key == 'c') {
            if (keyEvent.type == KEYEVENT_KEYDOWN) {
                handler->browserInstance->GetMainFrame()->Copy();
            }
            return;
        } else if (key == 'v') {
            if (keyEvent.type == KEYEVENT_KEYDOWN) {
                handler->browserInstance->GetMainFrame()->Paste();
            }
            return;
        }
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
    if (!textureId || !handler || !handler->browserInstance) {
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
#if XPLANE_VERSION == 12
    // CefScopedLibraryLoader unloads the framework from its destructor, and
    // createBrowser() runs again whenever the handler was reset. Keep the
    // loader alive for the process lifetime and load exactly once.
    static CefScopedLibraryLoader library_loader;
    static bool cefLibraryLoaded = library_loader.LoadInMain();
    if (!cefLibraryLoaded) {
        Logger::getInstance()->critical("Could not load CEF library dylib (CefScopedLibraryLoader)!\n");
        return false;
    }
#else
    static bool cefLibraryLoaded = cef_load_library((Path::getInstance()->pluginDirectory + "/mac_x64/Chromium Embedded Framework.framework/Chromium Embedded Framework").c_str());
    if (!cefLibraryLoaded) {
        Logger::getInstance()->critical("Could not load CEF library dylib!\n");
        return false;
    }
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

#if XPLANE_VERSION == 12
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

    if (!initializeCef(cachePath)) {
        return false;
    }

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

// CefInitialize() may only be called once per process; a second call fails and
// would leave the browser permanently unavailable. createBrowser() runs again
// whenever the handler was reset (e.g. a page called window.close), so the
// one-time process setup lives behind a static guard here. Only X-Plane 11
// needs this: X-Plane 12 initializes CEF itself.
bool Browser::initializeCef(const std::string &cachePath) {
#if XPLANE_VERSION == 11
    static bool cefInitialized = false;
    if (cefInitialized) {
        return true;
    }

    // CEF is not automatically loaded when starting X-Plane 11. Initialize CEF.
    CefRefPtr<CefApp> app;
    CefSettings settings;
    settings.windowless_rendering_enabled = true;
    CefString(&settings.cache_path) = cachePath;

#if IBM
    CefMainArgs main_args(GetModuleHandle(nullptr));

    std::string resourcesDir = Path::getInstance()->pluginDirectory + "/win_x64/res";
    std::string localesDir = Path::getInstance()->pluginDirectory + "/win_x64/res/locales";
    std::string helperPath = Path::getInstance()->pluginDirectory + "/win_x64/avitab_cef_helper.exe";

    Logger::getInstance()->info("[Windows CEF Init] Resources directory: %s\n", resourcesDir.c_str());
    Logger::getInstance()->info("[Windows CEF Init] Locales directory: %s\n", localesDir.c_str());
    Logger::getInstance()->info("[Windows CEF Init] Helper exe path: %s\n", helperPath.c_str());

    // Check if required directories and files exist
    if (!std::filesystem::exists(resourcesDir)) {
        Logger::getInstance()->critical("[Windows CEF Init ERROR] Resources directory does not exist: %s\n", resourcesDir.c_str());
    } else {
        Logger::getInstance()->info("[Windows CEF Init] Resources directory exists\n");
    }

    if (!std::filesystem::exists(localesDir)) {
        Logger::getInstance()->critical("[Windows CEF Init ERROR] Locales directory does not exist: %s\n", localesDir.c_str());
    } else {
        Logger::getInstance()->info("[Windows CEF Init] Locales directory exists\n");
    }

    if (!std::filesystem::exists(helperPath)) {
        Logger::getInstance()->critical("[Windows CEF Init ERROR] Helper exe does not exist: %s\n", helperPath.c_str());
    } else {
        Logger::getInstance()->info("[Windows CEF Init] Helper exe exists\n");
        // Check if we can read the file
        std::ifstream helperCheck(helperPath);
        if (!helperCheck.is_open()) {
            Logger::getInstance()->critical("[Windows CEF Init ERROR] Cannot open/read helper exe (may be a permissions issue)\n");
        } else {
            Logger::getInstance()->info("[Windows CEF Init] Helper exe is readable\n");
            helperCheck.close();
        }
    }

    // Check for all critical CEF files and data from dist_extra_11
    std::string winX64Dir = Path::getInstance()->pluginDirectory + "/win_x64";

    // Critical executables and libraries
    std::vector<std::string> criticalFiles = {
        "avitab_cef_helper.exe",
        "libcef.dll",
        "chrome_elf.dll",
        "d3dcompiler_47.dll",
        "libEGL.dll",
        "libGLESv2.dll",
        "vk_swiftshader.dll",
        "vulkan-1.dll"};

    // Required data files
    std::vector<std::string> dataFiles = {
        "icudtl.dat",
        "snapshot_blob.bin",
        "v8_context_snapshot.bin",
        "vk_swiftshader_icd.json"};

    Logger::getInstance()->info("[Windows CEF Init] Checking critical files from dist_extra_11...\n");
    bool allCriticalFilesExist = true;

    for (const auto &file : criticalFiles) {
        std::string filePath = winX64Dir + "/" + file;
        if (!std::filesystem::exists(filePath)) {
            Logger::getInstance()->critical("[Windows CEF Init ERROR] Critical file missing: %s\n", filePath.c_str());
            allCriticalFilesExist = false;
        } else {
            Logger::getInstance()->info("[Windows CEF Init] Found: %s\n", file.c_str());
        }
    }

    for (const auto &file : dataFiles) {
        std::string filePath = winX64Dir + "/" + file;
        if (!std::filesystem::exists(filePath)) {
            Logger::getInstance()->critical("[Windows CEF Init ERROR] Required data file missing: %s\n", filePath.c_str());
            allCriticalFilesExist = false;
        } else {
            Logger::getInstance()->info("[Windows CEF Init] Found: %s\n", file.c_str());
        }
    }

    if (!allCriticalFilesExist) {
        Logger::getInstance()->warn("[Windows CEF Init WARNING] Some files from dist_extra_11 are missing. Plugin may not work correctly.\n");
        Logger::getInstance()->warn("[Windows CEF Init WARNING] Ensure all files from lib/win_x64/dist_extra_11/ are copied to <plugin>/win_x64/\n");
    }

    CefString(&settings.resources_dir_path) = resourcesDir;
    CefString(&settings.locales_dir_path) = localesDir;
    CefString(&settings.browser_subprocess_path) = helperPath;
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

    Logger::getInstance()->info("Initializing a new CEF instance for X-Plane 11...\n");

#if IBM
    // Clear any previous Windows errors before initialization
    SetLastError(0);
#endif

    if (!CefInitialize(main_args, settings, app, nullptr)) {
        Logger::getInstance()->critical("[CEF Init ERROR] Could not initialize CEF instance.\n");

#if IBM
        DWORD lastError = GetLastError();
        if (lastError != 0) {
            char errorBuffer[1024] = {0};
            FormatMessageA(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                lastError,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                errorBuffer,
                sizeof(errorBuffer) - 1,
                nullptr);
            Logger::getInstance()->critical("[Windows Error 127 Details] Error Code: %lu (0x%lX)\n", lastError, lastError);
            Logger::getInstance()->critical("[Windows Error 127 Details] Error Message: %s\n", errorBuffer);
        } else {
            Logger::getInstance()->critical("[Windows Error 127 Details] GetLastError() returned 0 - this suggests CEF library loading failed\n");
        }

        // Additional diagnostics
        Logger::getInstance()->info("[Windows Error 127 Diagnostics] Checking critical CEF components...\n");
        HMODULE libcef = LoadLibraryA((Path::getInstance()->pluginDirectory + "/win_x64/libcef.dll").c_str());
        if (libcef) {
            Logger::getInstance()->info("[Windows Error 127 Diagnostics] libcef.dll loaded successfully\n");
            FreeLibrary(libcef);
        } else {
            DWORD libcefError = GetLastError();
            char libcefErrorBuffer[1024] = {0};
            FormatMessageA(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                libcefError,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                libcefErrorBuffer,
                sizeof(libcefErrorBuffer) - 1,
                nullptr);
            Logger::getInstance()->critical("[Windows Error 127 Diagnostics] libcef.dll failed to load: %s (Error: %lu)\n", libcefErrorBuffer, libcefError);
        }
#endif

        return false;
    }
    Logger::getInstance()->info("CEF instance for X-Plane 11 has been set up successfully.\n");
    cefInitialized = true;
#endif

    return true;
}

void Browser::updateGPSLocation() {
    if (!handler || !handler->browserInstance) {
        return;
    }

    // Latitude, longitude and elevation are double datarefs; reading them as
    // float quantizes the position by roughly a meter while we format six
    // decimals below.
    double latitude = Dataref::getInstance()->get<double>("sim/flightmodel/position/latitude");
    double longitude = Dataref::getInstance()->get<double>("sim/flightmodel/position/longitude");
    float speedMetersSecond = Dataref::getInstance()->get<float>("sim/flightmodel/position/groundspeed");
    double altitudeMetersAboveSeaLevel = Dataref::getInstance()->get<double>("sim/flightmodel/position/elevation");
    float magneticHeading = Dataref::getInstance()->get<float>("sim/flightmodel/position/mag_psi");

    float windDirection = Dataref::getInstance()->get<float>("sim/weather/wind_direction_degt");
    float windSpeed = Dataref::getInstance()->get<float>("sim/weather/wind_speed_kt");

    float altitudeMetersAboveGroundLevel = Dataref::getInstance()->get<float>("sim/flightmodel/position/y_agl");
    float airspeedKts = Dataref::getInstance()->get<float>("sim/flightmodel/position/indicated_airspeed");

    std::stringstream stream;
    stream << "window.avitab_location = { ";
    stream << "coords: { ";
    stream << "latitude: " << std::fixed << std::setprecision(6) << latitude << ", ";
    stream << "longitude: " << std::fixed << std::setprecision(6) << longitude << ", ";
    stream << "accuracy: 10, ";
    stream << "altitude: " << std::fixed << std::setprecision(0) << altitudeMetersAboveSeaLevel << ", ";
    stream << "altitudeAccuracy: 10, ";
    stream << "heading: " << std::fixed << std::setprecision(0) << magneticHeading << ", ";
    stream << "speed: " << std::fixed << std::setprecision(0) << speedMetersSecond << ", ";
    stream << "}, ";
    stream << "wind: { ";
    stream << "direction: " << std::fixed << std::setprecision(0) << windDirection << ", ";
    stream << "speedKts: " << std::fixed << std::setprecision(0) << windSpeed << ", ";
    stream << "}, ";
    stream << "extra: { ";
    stream << "altitudeAgl: " << std::fixed << std::setprecision(0) << altitudeMetersAboveGroundLevel << ", ";
    stream << "airspeedKts: " << std::fixed << std::setprecision(0) << airspeedKts << ", ";
    stream << "}, timestamp: Date.now() }; for (let key in window.avitab_watchers) { window.avitab_watchers[key](window.avitab_location); }";

    handler->browserInstance->GetMainFrame()->ExecuteJavaScript(stream.str(), handler->browserInstance->GetMainFrame()->GetURL(), 0);
    lastGpsUpdateTime = XPLMGetElapsedTime();
}

CefMouseEvent Browser::getMouseEvent(float normalizedX, float normalizedY) {
    const auto &tabletDimensions = AppState::getInstance()->tabletDimensions;

    CefMouseEvent mouseEvent;
    mouseEvent.x = tabletDimensions.browserWidth * normalizedX;
    mouseEvent.y = tabletDimensions.browserHeight * (1.0f - ((normalizedY - offsetStart) / (offsetEnd - offsetStart)));
    return mouseEvent;
}
