#ifndef BROWSER_HANDLER_H
#define BROWSER_HANDLER_H

#include <include/cef_client.h>
#include <include/cef_version.h>
#include "cursor.h"

class BrowserHandler : public CefClient, public CefPermissionHandler, public CefRenderHandler, public CefDisplayHandler, public CefLifeSpanHandler, public CefLoadHandler, public CefDialogHandler, public CefJSDialogHandler, public CefFocusHandler, public CefRequestHandler, public CefDownloadHandler, public CefResourceRequestHandler {
private:
    IMPLEMENT_REFCOUNTING(BrowserHandler);
    int textureId;
    unsigned short windowWidth;
    unsigned short windowHeight;
    
public:
    BrowserHandler(int textureId, unsigned short width, unsigned short height);
    ~BrowserHandler();
    
    bool hasInputFocus;
    CursorType cursorState;
    CefRefPtr<CefBrowser> browserInstance;
    
    void destroy();
    
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefDialogHandler> GetDialogHandler() override { return this; }
    CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
    CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool is_navigation, bool is_download, const CefString &request_initiator, bool &disable_default_handling) override { return this; }
    
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool DoClose(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &target_url, const CefString &target_frame_name, CefLifeSpanHandler::WindowOpenDisposition target_disposition, bool user_gesture, const CefPopupFeatures &popupFeatures, CefWindowInfo &windowInfo, CefRefPtr<CefClient> &client, CefBrowserSettings &settings, CefRefPtr<CefDictionaryValue> &extra_info, bool *no_javascript_access) override;
    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) override;
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString &title) override;
    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const void *buffer, int width, int height) override;
    void OnVirtualKeyboardRequested(CefRefPtr<CefBrowser> browser, TextInputMode input_mode) override;
    bool OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor, cef_cursor_type_t type, const CefCursorInfo &custom_cursor_info) override;
    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading, bool canGoBack, bool canGoForward) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString& errorText, const CefString& failedUrl) override;
    bool OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString &origin_url, JSDialogType dialog_type, const CefString &message_text, const CefString &default_prompt_text, CefRefPtr<CefJSDialogCallback> callback, bool &suppress_message) override;
    bool OnFileDialog(CefRefPtr<CefBrowser> browser, FileDialogMode mode, const CefString &title, const CefString &default_file_path, const std::vector<CefString> &accept_filters, CefRefPtr<CefFileDialogCallback> callback) override;
    bool OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser, uint64_t prompt_id, const CefString &requesting_origin, uint32_t requested_permissions, CefRefPtr<CefPermissionPromptCallback> callback) override;
    bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &requesting_origin, uint32_t requested_permissions, CefRefPtr<CefMediaAccessCallback> callback) override;
    void OnDocumentAvailableInMainFrame(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeDownload(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item, const CefString &suggested_name, CefRefPtr<CefBeforeDownloadCallback> callback) override;
    void OnDownloadUpdated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item, CefRefPtr<CefDownloadItemCallback> callback) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) override;
    cef_return_value_t OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, CefRefPtr<CefCallback> callback) override;
};

#endif
