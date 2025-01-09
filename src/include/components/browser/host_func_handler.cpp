#include "host_func_handler.h"
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


bool HostFunctionHandler::OnQuery(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame> frame,
                             int64_t query_id,
                             const CefString& request,
                             bool persistent,
                             CefRefPtr<Callback> callback) {
    // The `request` string is whatever you pass from JS
    if (request == "myHostFunction") {
        // Do something in C++
        // e.g., read a file, compute a value, call some API, etc.

        // Return data back to JS
        callback->Success("Hello from C++ host!");
        
        // We handled this query
        return true;
    }
    
    // If we do not recognize this request string,
    // return false so other handlers (if any) can process it
    return false;
}
