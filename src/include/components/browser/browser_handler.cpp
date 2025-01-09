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
#include "host_func_handler.h"
BrowserHandler::BrowserHandler(int aTextureId, unsigned short aWidth, unsigned short aHeight) {
    textureId = aTextureId;
    windowWidth = aWidth;
    windowHeight = aHeight;
    cursorState = CursorDefault;
    hasInputFocus = false;
    browserInstance = nullptr;
    
    CefMessageRouterConfig config;
    message_router_ = CefMessageRouterBrowserSide::Create(config);
    message_router_->AddHandler(new HostFunctionHandler(), false);
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
    if (message_router_) {
        message_router_->RemoveHandler(nullptr);
        message_router_ = nullptr;
    }
    
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
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            rect.x, rect.y,
            rect.width, rect.height,
            GL_BGRA,
            GL_UNSIGNED_BYTE,
            rectBuffer
        );
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
}

void BrowserHandler::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString& errorText, const CefString& failedUrl) {
    CEF_REQUIRE_UI_THREAD();
    if (errorCode == ERR_ABORTED) {
        return;
    }

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
        AppState::getInstance()->statusbar->setNotice("");
        AppState::getInstance()->showNotification(new Notification("Download finished", "The download has been completed."));
    }
    else {
        int percentComplete = fmax(0, static_cast<int>(download_item->GetPercentComplete()));
        AppState::getInstance()->statusbar->setNotice("Downloading... " + std::to_string(percentComplete) + "%");
    }
}

bool BrowserHandler::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                              CefRefPtr<CefFrame> frame,
                                              CefProcessId source_process,
                                              CefRefPtr<CefProcessMessage> message) {
    // Let the router handle the incoming messages first
    if (message_router_ && message_router_->OnProcessMessageReceived(browser, frame, source_process, message)) {
        return true;
    }

    // Otherwise handle your own IPC or fallback
    return false;
}
