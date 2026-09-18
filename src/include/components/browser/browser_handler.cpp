// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "browser_handler.h"

#include "appstate.h"
#include "config.h"
#include "path.h"
#include "user_agent.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <include/base/cef_callback.h>
#include <include/cef_app.h>
#include <include/cef_base.h>
#include <include/cef_devtools_message_observer.h>
#include <include/cef_parser.h>
#include <include/views/cef_browser_view.h>
#include <include/views/cef_window.h>
#include <include/wrapper/cef_closure_task.h>
#include <include/wrapper/cef_helpers.h>
#include <sstream>
#include <string>
#include <XPLMDisplay.h>
#include <XPLMGraphics.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

BrowserHandler::BrowserHandler(int aTextureId, std::string *aCurrentUrl, unsigned short aWidth, unsigned short aHeight) {
    textureId = aTextureId;
    popupRect = {0, 0, 0, 0};
    popupShown = false;
    needsFullDraw = true;
    currentUrl = aCurrentUrl;
    windowWidth = aWidth;
    windowHeight = aHeight;
    cursorState = CursorDefault;
    hasInputFocus = false;
    hasTextSelection = false;
    browserInstance = nullptr;
    paintBuffer.resize((size_t) windowWidth * windowHeight * 4, 0xFF);
    paintDirty = false;
    dirtyMinX = windowWidth;
    dirtyMinY = windowHeight;
    dirtyMaxX = 0;
    dirtyMaxY = 0;
}

BrowserHandler::~BrowserHandler() {
    textureId = 0;
    browserInstance = nullptr;
    cursorState = CursorDefault;
    hasInputFocus = false;
    hasTextSelection = false;
}

void BrowserHandler::destroy() {
    textureId = 0;
    cursorState = CursorDefault;
    hasInputFocus = false;
    hasTextSelection = false;
}

// Logs the outcome of every DevTools method we send, so a refused override
// or injection shows up in the log instead of failing silently.
class DevToolsResultLogger : public CefDevToolsMessageObserver {
    IMPLEMENT_REFCOUNTING(DevToolsResultLogger);

    public:
        void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int message_id, bool success, const void *result, size_t result_size) override {
            std::string payload(static_cast<const char *>(result), result_size);
            Logger::getInstance()->info("[DevTools] id=%d %s: %s\n", message_id, success ? "ok" : "ERROR", payload.c_str());
        }
};

static void sendDevTools(CefRefPtr<CefBrowser> browser, const char *method, CefRefPtr<CefDictionaryValue> params) {
    int id = browser->GetHost()->ExecuteDevToolsMethod(0, method, params);
    Logger::getInstance()->info("[DevTools] %s -> id=%d\n", method, id);
}

void BrowserHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    browserInstance = browser;
    browserInstance->GetHost()->SetAudioMuted(AppState::getInstance()->config.audio_muted);

    // The browser is created on about:blank so the UA override is in place
    // before the first real navigation and its client hints go out.
    devToolsLogger = new DevToolsResultLogger();
    devToolsRegistration = browser->GetHost()->AddDevToolsMessageObserver(devToolsLogger);
    UserAgent::applyOverride(browser);
    installNavigatorOverrides(browser);
    if (currentUrl && !currentUrl->empty()) {
        browser->GetMainFrame()->LoadURL(*currentUrl);
    }

    notifyVisible();
}

// Windowless browsers keep painting whether or not anyone told them they are on
// screen, so a missing visibility notification goes unnoticed in the texture.
// Blink notices: a page it believes is hidden gets its throttleable timers
// aligned to whole seconds, which turns an 80ms setTimeout into up to a second.
// requestAnimationFrame keeps running off the OSR frame timer, so animation
// stays smooth and only timer-driven work is late.
//
// The resize/show/invalidate order is the one obs-browser uses, which is the
// closest production analogue to this plugin. WasResized() re-syncs the view
// rect first, and the invalidate forces a frame back after a period where
// OnPaint was allowed to stop.
void BrowserHandler::notifyVisible() {
    if (!browserInstance) {
        return;
    }

    browserInstance->GetHost()->WasResized();
    browserInstance->GetHost()->WasHidden(false);

    needsFullDraw = true;
    browserInstance->GetHost()->Invalidate(PET_VIEW);
}

bool BrowserHandler::DoClose(CefRefPtr<CefBrowser> browser) {
    textureId = 0;

    if (AppState::getInstance()->statusbar) {
        AppState::getInstance()->statusbar->setActiveTab("");
    }

    AppState::getInstance()->hideBrowser();

    return false;
}

void BrowserHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    textureId = 0;
    browserInstance = nullptr;

    if (AppState::getInstance()->statusbar) {
        AppState::getInstance()->statusbar->setActiveTab("");
    }
}

bool BrowserHandler::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &target_url, const CefString &target_frame_name, CefLifeSpanHandler::WindowOpenDisposition target_disposition, bool user_gesture, const CefPopupFeatures &popupFeatures, CefWindowInfo &windowInfo, CefRefPtr<CefClient> &client, CefBrowserSettings &settings, CefRefPtr<CefDictionaryValue> &extra_info, bool *no_javascript_access) {
    if (user_gesture && !target_url.empty()) {
        browser->GetMainFrame()->LoadURL(target_url);
    }

    return true;
}

void BrowserHandler::OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) {
    popupShown = show;

    if (popupShown) {
        browser->GetHost()->Invalidate(PET_POPUP);
    } else {
        needsFullDraw = true;
        // The framebuffer still shows the popup; force a view repaint so it
        // clears immediately instead of lingering until the page next paints.
        browser->GetHost()->Invalidate(PET_VIEW);
    }
}

void BrowserHandler::OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect &rect) {
    popupRect = {rect.x, rect.y, rect.width, rect.height};
}

void BrowserHandler::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) {
    rect = CefRect(0, 0, windowWidth, windowHeight);
}

// Without these CEF reports a 0-bit colour depth and a screen identical to the
// viewport, both of which bot checks read as headless. Report the sim's screen
// and a window slightly taller than the view, like a browser with a toolbar.
bool BrowserHandler::GetRootScreenRect(CefRefPtr<CefBrowser> browser, CefRect &rect) {
    const int toolbarHeight = 87;
    rect = CefRect(0, 0, windowWidth, windowHeight + toolbarHeight);
    return true;
}

bool BrowserHandler::GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo &screen_info) {
    int screenWidth = 0, screenHeight = 0;
    XPLMGetScreenSize(&screenWidth, &screenHeight);
    screenWidth = std::max<int>(screenWidth, windowWidth);
    screenHeight = std::max<int>(screenHeight, windowHeight);

    screen_info.device_scale_factor = 1.0f;
    screen_info.depth = 24;
    screen_info.depth_per_component = 8;
    screen_info.is_monochrome = false;
    screen_info.rect = CefRect(0, 0, screenWidth, screenHeight);
    screen_info.available_rect = CefRect(0, 0, screenWidth, screenHeight);
    return true;
}

void BrowserHandler::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString &title) {
    AppState::getInstance()->statusbar->setActiveTab(title.ToString());
}

// OnPaint runs outside X-Plane's draw callbacks (from the CEF message loop
// pump), where the GL bridge context is not ours to use. Under the XP12 Metal
// renderer, issuing GL here entangles X-Plane's GL-on-Metal command stream and
// can deadlock the render thread in waitUntilScheduled. So: only copy the
// pixels into a CPU buffer here; uploadPendingPaint() does the GL upload from
// the draw callback.
void BrowserHandler::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const void *buffer, int width, int height) {
    if (!textureId) {
        return;
    }

    std::lock_guard<std::mutex> lock(paintMutex);
    const unsigned char *pixels = static_cast<const unsigned char *>(buffer);

    if (type == PET_POPUP) {
        if (!popupShown) {
            return;
        }

        for (const auto &rect : dirtyRects) {
            copyPaintRect(pixels, width, height, rect.x, rect.y, popupRect.x + rect.x, popupRect.y + rect.y, rect.width, rect.height);
        }
    } else if (needsFullDraw) {
        copyPaintRect(pixels, width, height, 0, 0, 0, 0, width, height);
        needsFullDraw = false;
    } else if (!popupShown) {
        for (const auto &rect : dirtyRects) {
            copyPaintRect(pixels, width, height, rect.x, rect.y, rect.x, rect.y, rect.width, rect.height);
        }
    }
    // View updates while a popup is shown are dropped, same as before: the view
    // buffer does not contain the popup, so copying it would erase the popup.
}

void BrowserHandler::copyPaintRect(const unsigned char *source, int sourceWidth, int sourceHeight, int sourceX, int sourceY, int destX, int destY, int rectWidth, int rectHeight) {
    constexpr int bytesPerPixel = 4;

    // Clamp against both the source buffer and the destination framebuffer.
    if (destX < 0) {
        sourceX -= destX;
        rectWidth += destX;
        destX = 0;
    }
    if (destY < 0) {
        sourceY -= destY;
        rectHeight += destY;
        destY = 0;
    }
    rectWidth = std::min({rectWidth, sourceWidth - sourceX, windowWidth - destX});
    rectHeight = std::min({rectHeight, sourceHeight - sourceY, windowHeight - destY});
    if (rectWidth <= 0 || rectHeight <= 0 || sourceX < 0 || sourceY < 0) {
        return;
    }

    for (int row = 0; row < rectHeight; ++row) {
        const unsigned char *sourceRow = source + ((size_t) (sourceY + row) * sourceWidth + sourceX) * bytesPerPixel;
        unsigned char *destRow = paintBuffer.data() + ((size_t) (destY + row) * windowWidth + destX) * bytesPerPixel;
        memcpy(destRow, sourceRow, (size_t) rectWidth * bytesPerPixel);
    }

    dirtyMinX = std::min(dirtyMinX, destX);
    dirtyMinY = std::min(dirtyMinY, destY);
    dirtyMaxX = std::max(dirtyMaxX, destX + rectWidth);
    dirtyMaxY = std::max(dirtyMaxY, destY + rectHeight);
    paintDirty = true;
}

// Must be called from an X-Plane draw callback: this is the only context where
// plugin GL is valid under the Metal renderer.
void BrowserHandler::uploadPendingPaint() {
    if (!textureId) {
        return;
    }

    std::lock_guard<std::mutex> lock(paintMutex);
    if (!paintDirty) {
        return;
    }

    int rectWidth = dirtyMaxX - dirtyMinX;
    int rectHeight = dirtyMaxY - dirtyMinY;
    if (rectWidth > 0 && rectHeight > 0) {
        XPLMBindTexture2d(textureId, 0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, windowWidth);
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            dirtyMinX, dirtyMinY,
            rectWidth, rectHeight,
            GL_BGRA,
            GL_UNSIGNED_BYTE,
            paintBuffer.data() + ((size_t) dirtyMinY * windowWidth + dirtyMinX) * 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    paintDirty = false;
    dirtyMinX = windowWidth;
    dirtyMinY = windowHeight;
    dirtyMaxX = 0;
    dirtyMaxY = 0;
}

bool BrowserHandler::OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor, cef_cursor_type_t type, const CefCursorInfo &custom_cursor_info) {
    switch (type) {
        case CT_HAND:
            cursorState = CursorHand;
            break;

        case CT_IBEAM:
        case CT_VERTICALTEXT:
            cursorState = CursorText;
            break;

        default:
            cursorState = CursorDefault;
    }

    return false;
}

void BrowserHandler::OnVirtualKeyboardRequested(CefRefPtr<CefBrowser> browser, TextInputMode input_mode) {
    hasInputFocus = input_mode != CEF_TEXT_INPUT_MODE_NONE;
}

void BrowserHandler::OnTextSelectionChanged(CefRefPtr<CefBrowser> browser, const CefString& selected_text, const CefRange& selected_range) {
    hasTextSelection = !selected_text.empty();
}

void BrowserHandler::OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading, bool canGoBack, bool canGoForward) {
    AppState::getInstance()->statusbar->loading = isLoading;

    // Skip the about:blank the browser starts on, so the homepage stays current.
    std::string url = browser->GetMainFrame()->GetURL().ToString();
    if (!isLoading && url != "about:blank") {
        injectAddressBar(browser);
        *currentUrl = url;
    }
}

void BrowserHandler::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString &errorText, const CefString &failedUrl) {
    CEF_REQUIRE_UI_THREAD();
    if (errorCode == ERR_ABORTED) {
        return;
    }

#if DEBUG
    if (failedUrl.ToString() == "http://__debug__/") {
        const std::string htmlString = R"(
        <html>
        <head>
            <meta charset="UTF-8" />
            <meta name="viewport" content="width=device-width, initial-scale=1.0" />
            <script src="https://unpkg.com/@tailwindcss/browser@4"></script>
            <title>)" + std::string(FRIENDLY_NAME) +
                                       R"(</title>
            <script>
                function refreshUserAgent() {
                    document.getElementById('user-agent').textContent = navigator.userAgent;
                }
                
                function didLoad() {
                    refreshUserAgent();
                
                    navigator.geolocation.watchPosition(({coords, wind, extra}) => {
                        document.getElementById('location').textContent = `${coords.latitude}, ${coords.longitude} at ${coords.altitude}m msl, ${extra.altitudeAgl}m agl - ${coords.speed}m/s`;
                        document.getElementById('wind').textContent = `${wind.direction}deg / ${wind.speedKts}kts - Airspeed: ${extra.airspeedKts}kts`;
                    });
                }
                
                if (document.readyState === "complete") {
                    didLoad();
                }
                else {
                    window.addEventListener("load", didLoad);
                }
            </script>
        </head>
        <body class="flex flex-col items-center justify-start w-full">
            <div class="flex flex-col items-center gap-4 max-w-3xl">
                <h1 class="text-3xl font-bold underline">)" +
                                       std::string(FRIENDLY_NAME) + R"(</h1>
                <div>
                    <input id="alert-text" placeholder="Type here" class="border border-gray-300 rounded-lg shadow-sm focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-blue-500" />
                    <button onclick="javascript:alert(document.getElementById('alert-text').value || 'Test');">Show alert</button>
                    <span class="mx-2 text-gray-500">|</span>
                    <button onclick="javascript:confirm(document.getElementById('alert-text').value || 'Confirm?');">Show confirm</button>
                </div>
        
                <div>
                    <select class="w-48 p-2 border border-gray-300 rounded-lg shadow-sm focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-blue-500">
                        <option>Option 1</option>
                        <option>Option 2</option>
                        <option>Option 3</option>
                        <option>Option 4</option>
                        <option>Option 5</option>
                        <option>Option 6</option>
                        <option>Option 7</option>
                        <option>Option 8</option>
                        <option>Option 9</option>
                        <option>Option 10</option>
                        <option>Option 11</option>
                        <option>Option 12</option>
                        <option>Option 13</option>
                        <option>Option 14</option>
                        <option>Option 15</option>
                        <option>Option 16</option>
                        <option>Option 17</option>
                        <option>Option 18</option>
                        <option>Option 19</option>
                        <option>Option 20</option>
                    </select>
                </div>
        
                <div class="flex flex-col w-full gap-4">
                    <div class="flex flex-col gap-2 text-xs" onclick="refreshUserAgent();">
                        <span>User-Agent (JS)</span>
                        <span id="user-agent">not_loaded</span>
                    </div>
            
                    <div class="flex flex-col gap-2 text-xs">
                        <span>Location</span>
                        <span id="location">not_loaded</span>
                    </div>
            
                    <div class="flex flex-col gap-2 text-xs">
                        <span>Wind</span>
                        <span id="wind">not_loaded</span>
                    </div>
                </div>
            </div>
        </body>
        </html>
        )";
        browser->GetMainFrame()->LoadURL("data:text/html;charset=utf-8," + htmlString);
        return;
    }
#endif

    Logger::getInstance()->warn("Error loading %s: %s\n", failedUrl.ToString().c_str(), errorText.ToString().c_str());
}

bool BrowserHandler::OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString &origin_url, JSDialogType dialog_type, const CefString &message_text, const CefString &default_prompt_text, CefRefPtr<CefJSDialogCallback> callback, bool &suppress_message) {
    suppress_message = true;

    AppState::getInstance()->showNotification(new Notification("Alert", message_text.ToString()));
    return false;
}

bool BrowserHandler::OnFileDialog(CefRefPtr<CefBrowser> browser, FileDialogMode mode, const CefString &title, const CefString &default_file_path, const std::vector<CefString> &accept_filters, CefRefPtr<CefFileDialogCallback> callback) {
    // Logger::getInstance()->info("file dialog: %i :: %s\n", mode, title.ToString().c_str());
    return false;
}

bool BrowserHandler::OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser, uint64_t prompt_id, const CefString &requesting_origin, uint32_t requested_permissions, CefRefPtr<CefPermissionPromptCallback> callback) {
    if (requested_permissions & CEF_PERMISSION_TYPE_GEOLOCATION) {
        //        callback->Continue(CEF_PERMISSION_RESULT_DENY);
        //        return true;
        return false;
    }

    Logger::getInstance()->info("Denied browser permissions request from %s. Requested flags=%i\n", requesting_origin.ToString().c_str(), requested_permissions);
    callback->Continue(CEF_PERMISSION_RESULT_DENY);

    return true;
}

bool BrowserHandler::OnRequestMediaAccessPermission(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &requesting_origin, uint32_t requested_permissions, CefRefPtr<CefMediaAccessCallback> callback) {
    callback->Continue(CEF_MEDIA_PERMISSION_NONE);
    return true;
}

void BrowserHandler::OnDocumentAvailableInMainFrame(CefRefPtr<CefBrowser> browser) {
    if (!browser->GetMainFrame()) {
        return;
    }

    overrideGeolocationAndNavigator(browser);
}

void BrowserHandler::OnBeforeDownload(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item, const CefString &suggested_name, CefRefPtr<CefBeforeDownloadCallback> callback) {
    if (suggested_name == "b738x.xml" || suggested_name.ToString().ends_with(".fms")) {
        std::string filename = Path::getInstance()->rootDirectory + "/output/FMS plans/" + suggested_name.ToString();
        callback->Continue(filename, false);
        return;
    }

    // Cancel all other downloads (by default).
    AppState::getInstance()->showNotification(new Notification("Download failed", "Could not download the requested file."));
}

void BrowserHandler::OnDownloadUpdated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item, CefRefPtr<CefDownloadItemCallback> callback) {
    if (download_item->IsComplete()) {
        AppState::getInstance()->showNotification(new Notification("Download finished", "The download has been completed."));
    }
}

#if DEBUG
bool BrowserHandler::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool user_gesture, bool is_redirect) {
    if (frame->IsMain()) {
        std::string url = request->GetURL();
        Logger::getInstance()->debug("URL: %s\n", url.c_str());
    }

    return false;
}
#endif

void BrowserHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {
    if (!frame->IsMain()) {
        return;
    }

    // Diagnostic: report from inside the page whether the early injection ran.
    CefRefPtr<CefDictionaryValue> probe = CefDictionaryValue::Create();
    probe->SetString("expression", R"(JSON.stringify({
        href: location.href, installed: window.avitab_overrides_installed,
        outer: [outerWidth, outerHeight], inner: [innerWidth, innerHeight],
        screen: [screen.width, screen.height, screen.availWidth, screen.availHeight, screen.colorDepth], dpr: devicePixelRatio,
        visibility: document.visibilityState, focus: document.hasFocus(),
        uaData: navigator.userAgentData ? navigator.userAgentData.brands : null, platform: navigator.platform,
        cores: navigator.hardwareConcurrency, mem: navigator.deviceMemory, langs: navigator.languages,
        chrome: typeof window.chrome, webdriver: navigator.webdriver, plugins: navigator.plugins.length,
        cookies: navigator.cookieEnabled, storage: (function(){ try { localStorage.setItem('_a','1'); return true; } catch(e) { return e.name; } })(),
        notif: window.Notification ? Notification.permission : 'n/a',
        turnstile: Array.from(document.querySelectorAll('iframe')).map(f => f.src.slice(0, 60))
    }))");
    probe->SetBool("returnByValue", true);
    sendDevTools(browser, "Runtime.evaluate", probe);

    overrideGeolocationAndNavigator(browser);
    injectAddressBar(browser);
}

// Only geolocation is touched: the permissions query wraps the native one
// instead of replacing it, since bot checks flag a non-native permissions.query
// and a synthetic load event.
static const std::string navigatorOverrideScript = R"(
        (function() {
            if (window.avitab_overrides_installed) { return; }
            window.avitab_overrides_installed = true;
            window.avitab_watchers = (window.avitab_watchers || {});
            Object.defineProperty(navigator, 'onLine', { get: function() { return true; }, configurable: true });
            if (!window.Notification) {
                function Notification() { throw new TypeError('Illegal constructor'); }
                Notification.permission = 'default';
                Notification.maxActions = 2;
                Notification.requestPermission = function(cb) { if (cb) { cb('denied'); } return Promise.resolve('denied'); };
                window.Notification = Notification;
            }
            var nativeQuery = navigator.permissions.query.bind(navigator.permissions);
            navigator.permissions.query = function(options) {
                if (options && options.name === 'geolocation') { return Promise.resolve({ state: 'granted', onchange: null }); }
                if (options && options.name === 'notifications' && window.Notification) {
                    var state = Notification.permission === 'default' ? 'prompt' : Notification.permission;
                    return Promise.resolve({ state: state, onchange: null });
                }
                return nativeQuery(options);
            };
            navigator.geolocation.watchPosition = function(success, error, options) {
                var id = Math.round(Date.now() / 1000);
                window.avitab_watchers[id] = success;
                if (window.avitab_location) { success(window.avitab_location); }
                return id;
            };
            navigator.geolocation.clearWatch = function(id) { delete window.avitab_watchers[id]; };
            navigator.geolocation.getCurrentPosition = function(success, error, options) {
                if (window.avitab_location) { success(window.avitab_location); }
                else { navigator.geolocation.watchPosition(success, error, options); }
            };
        })();
    )";

// Registered once through DevTools so it runs before any page script, in
// every frame. The ExecuteJavaScript fallback below covers the case where the
// DevTools call is refused; the script itself is idempotent.
void BrowserHandler::installNavigatorOverrides(CefRefPtr<CefBrowser> browser) {
    sendDevTools(browser, "Page.enable", nullptr);
    CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
    params->SetString("source", navigatorOverrideScript);
    sendDevTools(browser, "Page.addScriptToEvaluateOnNewDocument", params);
}

void BrowserHandler::overrideGeolocationAndNavigator(CefRefPtr<CefBrowser> browser) {
    browser->GetMainFrame()->ExecuteJavaScript(navigatorOverrideScript, browser->GetMainFrame()->GetURL(), 0);
}

void BrowserHandler::injectAddressBar(CefRefPtr<CefBrowser> browser) {
    if (AppState::getInstance()->config.hide_addressbar) {
        return;
    }

    const std::string jsCode = R"(
        (function() {
            if (document.getElementById('cefToolbar')) {
                document.getElementById('cefBack').style.filter = ')" +
                               std::string(browser->CanGoBack() ? "" : "brightness(1.5) saturate(0)") + R"(';
                document.getElementById('cefForward').style.filter = ')" +
                               std::string(browser->CanGoForward() ? "" : "brightness(1.5) saturate(0)") + R"(';
                return;
            }
            
            const isDarkMode = window.matchMedia('(prefers-color-scheme: dark)').matches;
            const chevron = 'data:image/svg+xml;charset=UTF-8,' + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 9.971 13.623"><path fill-opacity=".85" fill="#007AFF" d="M0 6.807c0 .214.088.41.244.576l6.035 5.996a.773.773 0 0 0 .576.234.78.78 0 0 0 .801-.8.83.83 0 0 0-.234-.577l-5.45-5.43 5.45-5.43a.847.847 0 0 0 .234-.575.78.78 0 0 0-.8-.801.773.773 0 0 0-.577.234L.244 6.23A.831.831 0 0 0 0 6.807Z"/></svg>');
            const refresh = 'data:image/svg+xml;charset=UTF-8,' + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 13.994 18.271"><path fill-opacity=".85" fill="#999999" d="M0 9.717a6.816 6.816 0 1 0 13.633 0c0-.44-.303-.742-.733-.742-.41 0-.673.302-.673.732 0 2.988-2.422 5.4-5.41 5.4a5.401 5.401 0 0 1-5.41-5.4 5.41 5.41 0 0 1 5.41-5.41c.507 0 .966.02 1.376.107l-2.07 2.032a.663.663 0 0 0-.195.488c0 .4.302.703.683.703.225 0 .381-.078.508-.195l3.086-3.086a.685.685 0 0 0 .205-.508.719.719 0 0 0-.205-.498L7.12.215A.647.647 0 0 0 6.611 0c-.38 0-.683.322-.683.723 0 .185.068.361.185.498L7.91 2.998A5.769 5.769 0 0 0 6.816 2.9 6.81 6.81 0 0 0 0 9.717Z"/></svg>');
            
            // Create container
            const toolbar = document.createElement('div');
            toolbar.id = 'cefToolbar';
            toolbar.style.position = 'fixed';
            toolbar.style.top = '0';
            toolbar.style.left = '0';
            toolbar.style.width = '100%';
            toolbar.style.zIndex = '9999';
            toolbar.style.padding = '4px';
            toolbar.style.backgroundColor = isDarkMode ? '#1A1A1A' : '#EEE';
            toolbar.style.boxSizing = 'border-box';
            toolbar.style.display = 'flex';
            toolbar.style.alignItems = 'center';
            toolbar.style.gap = '8px';

            // Back button
            const backBtn = document.createElement('button');
            backBtn.id = "cefBack";
            backBtn.style.background = `url(${chevron}) no-repeat center / contain`
            backBtn.style.cursor = 'pointer';
            backBtn.style.width = '14px';
            backBtn.style.height = '14px';
            backBtn.style.outline = 'none';
            backBtn.style.border = 'none';
            backBtn.onclick = function() {
                window.history.back();
            };

            // Forward button
            const fwdBtn = document.createElement('button');
            fwdBtn.id = "cefForward";
            fwdBtn.style.cssText = backBtn.style.cssText;
            fwdBtn.style.transform = 'scaleX(-1)';
            fwdBtn.onclick = function() {
                window.history.forward();
            };

            // Address bar
            const addressBar = document.createElement('input');
            addressBar.type = 'text';
            addressBar.id = 'cefAddressBar';
            addressBar.value = window.location.href;
            addressBar.style.flex = '1';
            addressBar.style.fontSize = '12px';
            addressBar.style.border = 'none';
            addressBar.style.outline = 'none';
            addressBar.style.height = '20px';
            addressBar.style.color = isDarkMode ? '#D2D2D2' : '#1A1A1A';
            addressBar.style.backgroundColor = isDarkMode ? '#000000' : '#D2D2D2';
            addressBar.style.padding = '2px 8px';
            addressBar.style.borderRadius = '12px';
    
            const observer = new MutationObserver(() => {
                addressBar.value = window.location.href;
            });
            observer.observe(document, { subtree: true, childList: true });

            window.addEventListener("popstate", () => {
                addressBar.value = window.location.href;
            });

            addressBar.addEventListener('keydown', function(e) {
                if (e.key === 'Enter') {
                    if (!addressBar.value.startsWith('http')) {
                        addressBar.value = "https://" + addressBar.value;
                    }
    
                    window.location.href = addressBar.value;
                }
            });

            addressBar.addEventListener('blur', function(e) {
                if (addressBar.value === '') {
                    addressBar.value = window.location.href;
                }
            });

            // Clear input button
            const clearInputButton = document.createElement('button');
            clearInputButton.textContent = String.fromCharCode(10005);
            clearInputButton.style.cursor = 'pointer';
            clearInputButton.style.color = isDarkMode ? '#D2D2D2' : '#1A1A1A';
            clearInputButton.style.backgroundColor = 'transparent';
            clearInputButton.style.fontSize = '12px';
            clearInputButton.style.width = '14px';
            clearInputButton.style.height = '14px';
            clearInputButton.style.outline = 'none';
            clearInputButton.style.border = 'none';
            clearInputButton.style.position = 'absolute';
            clearInputButton.style.right = '32px';
            clearInputButton.style.zIndex = '9';
            clearInputButton.onclick = function() {
                addressBar.value = '';
                addressBar.focus();
            };

            // Refresh button
            const refreshButton = document.createElement('button');
            refreshButton.style.background = `url(${refresh}) no-repeat center / contain`
            refreshButton.style.cursor = 'pointer';
            refreshButton.style.width = '14px';
            refreshButton.style.height = '14px';
            refreshButton.style.outline = 'none';
            refreshButton.style.border = 'none';
            refreshButton.onclick = function() {
                window.location.reload();
            };

            // Add elements to the toolbar
            toolbar.appendChild(backBtn);
            toolbar.appendChild(fwdBtn);
            toolbar.appendChild(addressBar);
            toolbar.appendChild(clearInputButton);
            toolbar.appendChild(refreshButton);

            // Insert toolbar at the very top of <body>
            document.body.insertBefore(toolbar, document.body.firstChild);

            // Add some spacing so the page content isn't hidden behind the toolbar
            const elements = [document.body, document.querySelector('ytd-masthead')];
            for (const elem of elements) {
                if (!elem) {
                    continue;
                }
                elem.style.marginTop = '40px';
            }
        })();
    )";

    browser->GetMainFrame()->ExecuteJavaScript(jsCode, browser->GetMainFrame()->GetURL(), 0);
}
