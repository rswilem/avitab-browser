// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "browser_handler.h"
#include <include/cef_base.h>
#include <include/base/cef_callback.h>
#include <include/cef_app.h>
#include <include/cef_parser.h>
#include <include/views/cef_browser_view.h>
#include <include/views/cef_window.h>
#include <include/wrapper/cef_closure_task.h>
#include <include/wrapper/cef_helpers.h>
#include <sstream>
#include <string>
#include <cmath>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>
#include <XPLMGraphics.h>
#include "config.h"
#include "appstate.h"
#include "path.h"

BrowserHandler::BrowserHandler(int aTextureId, std::string* aCurrentUrl, unsigned short aWidth, unsigned short aHeight) {
    textureId = aTextureId;
    popupRect = {0,0,0,0};
    popupShown = false;
    currentUrl = aCurrentUrl;
    windowWidth = aWidth;
    windowHeight = aHeight;
    cursorState = CursorDefault;
    hasInputFocus = false;
    browserInstance = nullptr;
}

BrowserHandler::~BrowserHandler() {
    textureId = 0;
    browserInstance = nullptr;
    cursorState = CursorDefault;
    hasInputFocus = false;
}

void BrowserHandler::destroy() {
    textureId = 0;
    cursorState = CursorDefault;
    hasInputFocus = false;
}

void BrowserHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    browserInstance = browser;
    browserInstance->GetHost()->SetAudioMuted(AppState::getInstance()->config.audio_muted);
}

bool BrowserHandler::DoClose(CefRefPtr<CefBrowser> browser) {
    textureId = 0;
    
    if (AppState::getInstance()->statusbar) {
        AppState::getInstance()->statusbar->setActiveTab("");
    }
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
    }
    else {
        needsFullDraw = true;
    }
}

void BrowserHandler::OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect &rect) {
    popupRect = {rect.x, rect.y, rect.width, rect.height};
}

void BrowserHandler::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) {
    rect = CefRect(0, 0, windowWidth, windowHeight);
}

void BrowserHandler::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString &title) {
    AppState::getInstance()->statusbar->setActiveTab(title.ToString());
}

void BrowserHandler::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const void *buffer, int width, int height) {
    if (!textureId) {
        return;
    }
    
    XPLMBindTexture2d(textureId, 0);
    constexpr uint32_t bytes_per_pixel = 4;
    
    for (const auto& rect : dirtyRects) {
        const uint8_t* rectBuffer = static_cast<const uint8_t*>(buffer) + rect.y * width * bytes_per_pixel + rect.x * bytes_per_pixel;
        
        glPixelStorei(GL_UNPACK_ROW_LENGTH, width);
        
        if  (needsFullDraw) {
            glTexSubImage2D(
                            GL_TEXTURE_2D,
                            0,
                            0, 0,
                            width, height,
                            GL_BGRA,
                            GL_UNSIGNED_BYTE,
                            buffer
                            );
            needsFullDraw = false;
        }
        else if (popupShown) {
            if (type == PET_POPUP) {
                glTexSubImage2D(
                    GL_TEXTURE_2D,
                    0,
                    popupRect.x + rect.x, popupRect.y + rect.y,
                    rect.width, rect.height,
                    GL_BGRA,
                    GL_UNSIGNED_BYTE,
                    rectBuffer
                );
            }
        }
        else {
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                rect.x, rect.y,
                rect.width, rect.height,
                GL_BGRA,
                GL_UNSIGNED_BYTE,
                rectBuffer
            );
        }
        
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
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

void BrowserHandler::OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading, bool canGoBack, bool canGoForward) {
    AppState::getInstance()->statusbar->loading = isLoading;
    
    if (!isLoading) {
        injectAddressBar(browser);
        *currentUrl = browser->GetMainFrame()->GetURL().ToString();
    }
}

void BrowserHandler::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString& errorText, const CefString& failedUrl) {
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
            <title>)" + std::string(FRIENDLY_NAME) + R"(</title>
            <script>
                window.onload = () => {
                    document.getElementById('user-agent').textContent = navigator.userAgent;
                
                    navigator.geolocation.watchPosition(({coords}) => {
                        document.getElementById('location').textContent = `${coords.latitude}, ${coords.longitude} at ${coords.altitude}m ${coords.speed}m/s`;
                    });
                };
            </script>
        </head>
        <body class="flex flex-col items-center justify-start w-full">
            <div class="flex flex-col items-center gap-4 max-w-3xl">
                <h1 class="text-3xl font-bold underline">)" + std::string(FRIENDLY_NAME) + R"(</h1>
                <div>
                    <input id="alert-text" placeholder="Type here" />
                    <button onclick="javascript:alert(document.getElementById('alert-text').value || 'Test');">Show alert</button>
                </div>
        
                <div class="flex flex-col gap-2 text-xs">
                    <span>User-Agent (JS)</span>
                    <span id="user-agent" />
                </div>
        
                <div class="flex flex-col gap-2 text-xs">
                    <span>Location</span>
                    <span id="location" />
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
            </div>
        </body>
        </html>
        )";
        browser->GetMainFrame()->LoadURL("data:text/html;charset=utf-8," + htmlString);
        return;
    }
#endif

    debug("Error loading %s: %s\n", failedUrl.ToString().c_str(), errorText.ToString().c_str());
}

bool BrowserHandler::OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString &origin_url, JSDialogType dialog_type, const CefString &message_text, const CefString &default_prompt_text, CefRefPtr<CefJSDialogCallback> callback, bool &suppress_message) {
    suppress_message = true;
    
    AppState::getInstance()->showNotification(new Notification("Alert", message_text.ToString()));
    return false;
}

bool BrowserHandler::OnFileDialog(CefRefPtr<CefBrowser> browser, FileDialogMode mode, const CefString &title, const CefString &default_file_path, const std::vector<CefString> &accept_filters, CefRefPtr<CefFileDialogCallback> callback) {
    //debug("file dialog: %i :: %s", mode, title.ToString().c_str());
    return false;
}

bool BrowserHandler::OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser, uint64_t prompt_id, const CefString &requesting_origin, uint32_t requested_permissions, CefRefPtr<CefPermissionPromptCallback> callback) {
    if (requested_permissions & CEF_PERMISSION_TYPE_GEOLOCATION) {
//        callback->Continue(CEF_PERMISSION_RESULT_DENY);
//        return true;
        return false;
    }
    
    debug("Denied browser permissions request from %s. Requested flags=%i\n", requesting_origin.ToString().c_str(), requested_permissions);
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
    
    const char *js = R"(
        window.avitab_watchers = {};
        navigator.permissions.query = (options) => {
          return Promise.resolve({
            state: "granted",
          });
        };
    
        navigator.geolocation.watchPosition = (success, error, options) => {
          window.avitab_watchers = (window.avitab_watchers || {});
          const id = Math.round(Date.now() / 1000);
          window.avitab_watchers[id] = success;
          return id;
        };
    
        navigator.geolocation.clearWatch = (id) => {
          if (!window.avitab_watchers) { return; }
          delete window.avitab_watchers[id];
        };
    
        navigator.geolocation.getCurrentPosition = (success, error, options) => {
          const wid = navigator.geolocation.watchPosition(()=>{
            success(window.avitab_location || null);
            navigator.geolocation.clearWatch(wid);
          }, error, options);
        };
    )";
    
    browser->GetMainFrame()->ExecuteJavaScript(js, "about:blank", 0);
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

cef_return_value_t BrowserHandler::OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, CefRefPtr<CefCallback> callback) {
    CefRequest::HeaderMap headers;
    request->GetHeaderMap(headers);
    auto it = headers.find("User-Agent");
    if (it == headers.end()) {
        return RV_CONTINUE;
    }

    // Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) X-Plane Safari/537.36
    std::string userAgent = it->second.ToString();
    
    if (AppState::getInstance()->config.user_agent.empty()) {
        size_t start = userAgent.find("X-Plane");
        if (start != std::string::npos) {
            size_t end = userAgent.find(" ", start);
            if (end == std::string::npos) {
                end = userAgent.length();
            }
            
            userAgent.replace(start, end - start, "Chrome/117.2.5.0");
        }
    }
    else {
        userAgent = AppState::getInstance()->config.user_agent;
    }
    
    headers.erase("User-Agent");
    headers.insert(std::make_pair("User-Agent", userAgent));
    request->SetHeaderMap(headers);
    
    return RV_CONTINUE;
}

void BrowserHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {
    if (!frame->IsMain()) {
        return;
    }
    
    injectAddressBar(browser);
}

void BrowserHandler::injectAddressBar(CefRefPtr<CefBrowser> browser) {
    if (AppState::getInstance()->config.hide_addressbar) {
        return;
    }
    
    const std::string jsCode = R"(
        (function() {
            if (document.getElementById('cefToolbar')) {
                document.getElementById('cefBack').style.filter = ')" + std::string(browser->CanGoBack() ? "" : "brightness(1.5) saturate(0)") + R"(';
                document.getElementById('cefForward').style.filter = ')" + std::string(browser->CanGoForward() ? "" : "brightness(1.5) saturate(0)") + R"(';
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

            // Enter key to navigate
            addressBar.addEventListener('keydown', function(e) {
                if (e.key === 'Enter') {
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
            clearInputButton.innerHTML = '&#10005;';
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
            document.body.style.marginTop = '40px';
        })();
    )";

    browser->GetMainFrame()->ExecuteJavaScript(jsCode, "about:blank", 0);
}
