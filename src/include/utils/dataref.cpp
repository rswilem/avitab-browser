#include "dataref.h"
#include "config.h"
#include "appstate.h"
#include <XPLMUtilities.h>
#include <XPLMDisplay.h>

using namespace std;

Dataref *Dataref::instance = nullptr;

Dataref::Dataref() {
    lastMouseX = 0.0f;
    lastMouseY = 0.0f;
    lastWindowX = 0;
    lastWindowY = 0;
    lastViewHeading = 0;
}

Dataref::~Dataref() {
    instance = nullptr;
}

Dataref* Dataref::getInstance() {
    if (instance == nullptr) {
        instance = new Dataref();
    }
    
    return instance;
}

void Dataref::update() {
    for (auto& [key, data] : cachedValues) {
        std::visit([&](auto&& value) {
            using T = std::decay_t<decltype(value)>;
            cachedValues[key] = get<T>(key.c_str());
        }, data);
    }
}

bool Dataref::getMouse(float *normalizedX, float *normalizedY, float windowX, float windowY, XPLMWindowID inWindowId) {
    if (inWindowId != AppState::getInstance()->standaloneWindow) {
        float mouseX = get<float>("sim/graphics/view/click_3d_x_pixels");
        float mouseY = get<float>("sim/graphics/view/click_3d_y_pixels");
        int viewHeading = (int)get<float>("sim/graphics/view/view_heading");
        
        if (windowX > 0) {
            if (mouseX < 0 || mouseY < 0) {
                if (abs(viewHeading - lastViewHeading) > 5) {
                    return false;
                }
                mouseX = lastMouseX + (windowX - lastWindowX) / 1.5;
                mouseY = lastMouseY + (windowY - lastWindowY) / 1.5;
            }
            else {
                lastMouseX = mouseX;
                lastMouseY = mouseY;
                lastWindowX = windowX;
                lastWindowY = windowY;
                lastViewHeading = viewHeading;
            }
        }
        
        if (mouseX == -1 || mouseY == -1) {
            return false;
        }
        
        *normalizedX = (mouseX - AppState::getInstance()->tabletDimensions.x) / AppState::getInstance()->tabletDimensions.width;
        *normalizedY = (mouseY - AppState::getInstance()->tabletDimensions.y) / AppState::getInstance()->tabletDimensions.height;
    } else {
        int left, top, right, bottom;
        XPLMGetWindowGeometry(inWindowId, &left, &top, &right, &bottom);
        *normalizedX = (windowX - left) / (right - left);
        *normalizedY = (windowY - bottom) / (top - bottom);
    }
    return !(*normalizedX < -0.1f || *normalizedX > 1.1f || *normalizedY < -0.1f || *normalizedY > 1.1f);
}

XPLMDataRef Dataref::findRef(const char* ref) {
    if (refs.find(ref) != refs.end()) {
        return refs[ref];
    }
    
    XPLMDataRef handle = XPLMFindDataRef(ref);
    if (!handle) {
        debug("Dataref not found: '%s'\n", ref);
        return nullptr;
    }
    
    refs[ref] = handle;
    return refs[ref];
}

bool Dataref::exists(const char* ref) {
    return XPLMFindDataRef(ref) != nullptr;
}

template int Dataref::getCached<int>(const char* ref);
template float Dataref::getCached<float>(const char* ref);
template std::string Dataref::getCached<std::string>(const char* ref);
template <typename T>
T Dataref::getCached(const char *ref) {
    auto it = cachedValues.find(ref);
    if (it == cachedValues.end()) {
        auto val = get<T>(ref);
        cachedValues[ref] = val;
        return val;
    }
    
    if (!std::holds_alternative<T>(it->second)) {
        if constexpr (std::is_same<T, std::string>::value) {
            return "";
        }
        else {
            return 0;
        }
    }
    
    return std::get<T>(it->second);
}

template float Dataref::get<float>(const char* ref);
template int Dataref::get<int>(const char* ref);
template std::string Dataref::get<std::string>(const char* ref);
template <typename T>
T Dataref::get(const char *ref) {
    XPLMDataRef handle = findRef(ref);
    if (!handle) {
        if constexpr (std::is_same<T, std::string>::value) {
            return "";
        }
        else {
            return 0;
        }
    }
    
    if constexpr (std::is_same<T, int>::value) {
        return XPLMGetDatai(handle);
    }
    else if constexpr (std::is_same<T, float>::value) {
        return XPLMGetDataf(handle);
    }
    /*else if constexpr (std::is_same<T, std::string>::value) {
        int size = XPLMGetDatab(handle, nullptr, 0, 0);
        char str[size];
        XPLMGetDatab(handle, &str, 0, size);
        return std::string(str);
    }*/
    
    if constexpr (std::is_same<T, std::string>::value) {
        return "";
    }
    else {
        return 0;
    }
}

template void Dataref::set<float>(const char* ref, float value, bool setCacheOnly);
template void Dataref::set<int>(const char* ref, int value, bool setCacheOnly);
template <typename T>
void Dataref::set(const char* ref, T value, bool setCacheOnly) {
    XPLMDataRef handle = findRef(ref);
    if (!handle) {
        return;
    }
    
    cachedValues[ref] = value;
    
    if constexpr (std::is_same<T, int>::value) {
        if (!setCacheOnly) {
            XPLMSetDatai(handle, value);
        }
    }
    else if constexpr (std::is_same<T, float>::value) {
        if (!setCacheOnly) {
            XPLMSetDataf(handle, value);
        }
    }
    // TODO: Set binary data
//    else if constexpr (std::is_same<T, std::string>::value) {
//        int size = XPLMGetDatab(handle, nullptr, 0, 0);
//        char str[size];
//        XPLMGetDatab(handle, &str, 0, size);
//        return std::string(str);
//    }
}

void Dataref::executeCommand(const char *command) {
    XPLMCommandRef ref = XPLMFindCommand(command);
    if (!ref) {
        return;
    }
    
    XPLMCommandOnce(ref);
}

void Dataref::createCommand() {
    //XPLMCreateCommand
    //https://developer.x-plane.com/sdk/XPLMCreateCommand/
}

//void Dataref::shareCallback(void* inRefcon) {
//    
//}

void Dataref::shareData() {
    // https://developer.x-plane.com/sdk/XPLMDataAccess/#XPLMShareData
    //XPLMShareData("avitab-browser/website/url", xplmType_Data, shareCallback, nullptr);
}
