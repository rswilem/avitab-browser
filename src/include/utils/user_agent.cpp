#include "user_agent.h"

#include "appstate.h"
#include "config.h"

#include <include/cef_parser.h>
#include <include/cef_values.h>
#include <include/cef_version.h>

namespace {
    // cef_version_info resolves against the libcef that is loaded at runtime,
    // which on X-Plane 12 is the sim's own, not the one we compiled against.
    int engineMajor() {
        int major = cef_version_info(4);
        return major > 0 ? major : CHROME_VERSION_MAJOR;
    }

    std::string majorString() {
        return std::to_string(engineMajor());
    }

    std::string fullVersion() {
        if (cef_version_info(4) <= 0) {
            return majorString() + ".0.0.0";
        }
        return majorString() + "." + std::to_string(cef_version_info(5)) + "." + std::to_string(cef_version_info(6)) + "." + std::to_string(cef_version_info(7));
    }

#if APL
    const char *osToken = "Macintosh; Intel Mac OS X 10_15_7";
    const char *navigatorPlatform = "MacIntel";
    const char *hintPlatform = "macOS";
    const char *hintPlatformVersion = "10.15.7";
#elif IBM
    const char *osToken = "Windows NT 10.0; Win64; x64";
    const char *navigatorPlatform = "Win32";
    const char *hintPlatform = "Windows";
    const char *hintPlatformVersion = "10.0.0";
#else
    const char *osToken = "X11; Linux x86_64";
    const char *navigatorPlatform = "Linux x86_64";
    const char *hintPlatform = "Linux";
    const char *hintPlatformVersion = "6.5.0";
#endif

    std::string chromeString() {
        return "Mozilla/5.0 (" + std::string(osToken) + ") AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" + majorString() + ".0.0.0 Safari/537.36";
    }

    CefRefPtr<CefDictionaryValue> brand(const std::string &name, const std::string &version) {
        CefRefPtr<CefDictionaryValue> entry = CefDictionaryValue::Create();
        entry->SetString("brand", name);
        entry->SetString("version", version);
        return entry;
    }

    // Only the brands the engine itself reports; claiming Google Chrome invites
    // Chrome-specific checks (codecs, Widevine) that a Chromium build fails.
    CefRefPtr<CefListValue> brandList(const std::string &version, const std::string &greaseVersion) {
        CefRefPtr<CefListValue> list = CefListValue::Create();
        list->SetDictionary(0, brand("Chromium", version));
        list->SetDictionary(1, brand("Not;A=Brand", greaseVersion));
        return list;
    }

    CefRefPtr<CefDictionaryValue> metadata() {
        CefRefPtr<CefDictionaryValue> value = CefDictionaryValue::Create();
        value->SetList("brands", brandList(majorString(), "8"));
        value->SetList("fullVersionList", brandList(fullVersion(), "8.0.0.0"));
        value->SetString("fullVersion", fullVersion());
        value->SetString("platform", hintPlatform);
        value->SetString("platformVersion", hintPlatformVersion);
        value->SetString("architecture", "x86");
        value->SetString("bitness", "64");
        value->SetString("model", "");
        value->SetBool("mobile", false);
        return value;
    }

    bool hostMatches(const std::string &host, const std::string &domain) {
        if (host == domain) {
            return true;
        }
        return host.size() > domain.size() && host.compare(host.size() - domain.size() - 1, domain.size() + 1, "." + domain) == 0;
    }
}

std::string UserAgent::productToken() {
    return "AviTabBrowser/" VERSION;
}

UserAgent::Profile UserAgent::profileFor(const std::string &url) {
    CefURLParts parts;
    if (!CefParseURL(url, parts)) {
        return Profile::Engine;
    }

    std::string host = CefString(&parts.host).ToString();
    for (const char *domain : {"google.com", "youtube.com", "gstatic.com", "googleusercontent.com"}) {
        if (hostMatches(host, domain)) {
            return Profile::Chrome;
        }
    }
    return Profile::Engine;
}

void UserAgent::apply(CefRefPtr<CefBrowser> browser, Profile profile) {
    if (!browser || !browser->GetHost()) {
        return;
    }

    const std::string &configured = AppState::getInstance()->config.user_agent;
    CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();

    if (!configured.empty()) {
        params->SetString("userAgent", configured);
    } else if (profile == Profile::Chrome) {
        params->SetString("userAgent", chromeString());
    } else {
        // An empty override restores the engine's own UA and client hints.
        params->SetString("userAgent", "");
    }

    if (!params->GetString("userAgent").empty()) {
        params->SetString("platform", navigatorPlatform);
        params->SetDictionary("userAgentMetadata", metadata());
    }

    if (browser->GetHost()->ExecuteDevToolsMethod(0, "Network.setUserAgentOverride", params) == 0) {
        Logger::getInstance()->warn("[DevTools] UA override (%s) was not sent\n", profile == Profile::Chrome ? "chrome" : "engine");
    }
}
