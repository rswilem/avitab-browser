#include <include/wrapper/cef_message_router.h>

class HostFunctionHandler : public CefMessageRouterBrowserSide::Handler {
public:
    HostFunctionHandler() {}

    // Called when JS calls window.cefQuery({request: 'something', ...})
    virtual bool OnQuery(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         int64_t query_id,
                         const CefString& request,
                         bool persistent,
                         CefRefPtr<Callback> callback) override ;
   
};
