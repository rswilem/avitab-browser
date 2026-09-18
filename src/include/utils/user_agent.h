#ifndef USER_AGENT_H
#define USER_AGENT_H

#include <include/cef_browser.h>
#include <string>

// Cloudflare fails any UA that claims Chrome on this (older) Chromium engine but
// accepts an unknown product token; Google sign-in is the reverse. So the
// browser runs with an honest product token and only claims Chrome on Google's
// own domains. All switching goes through the DevTools UA override, which
// covers headers, navigator.* and client hints in every frame and worker.
namespace UserAgent {
    enum class Profile { Engine, Chrome };

    // Product token used in place of "Chrome/x" on X-Plane 11 (CefSettings).
    std::string productToken();

    // Which profile a main-frame navigation to this URL should use.
    Profile profileFor(const std::string &url);

    // Applies the profile to the whole browser. A configured user_agent wins
    // over both profiles. Only valid once the CEF library is loaded.
    void apply(CefRefPtr<CefBrowser> browser, Profile profile);
}

#endif
