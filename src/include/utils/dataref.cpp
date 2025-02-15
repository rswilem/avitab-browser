#include "dataref.h"
#include "config.h"
#include "appstate.h"
#include <XPLMUtilities.h>
#include <XPLMDisplay.h>

using namespace std;

Dataref *Dataref::instance = nullptr;

int handleCommandCallback(XPLMCommandRef inCommand, XPLMCommandPhase inPhase, void *inRefcon) {
    return Dataref::getInstance()->commandCallback(inCommand, inPhase, inRefcon);
}

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

template void Dataref::createDataref<int>(const char* ref, int* value, bool writable = false, BoundRefChangeCallback changeCallback = nullptr);
template void Dataref::createDataref<bool>(const char* ref, bool* value, bool writable = false, BoundRefChangeCallback changeCallback = nullptr);
template void Dataref::createDataref<float>(const char* ref, float* value, bool writable = false, BoundRefChangeCallback changeCallback = nullptr);
template void Dataref::createDataref<std::string>(const char* ref, std::string* value, bool writable = false, BoundRefChangeCallback changeCallback = nullptr);
template <typename T>
void Dataref::createDataref(const char* ref, T *value, bool writable, BoundRefChangeCallback changeCallback) {
    unbind(ref);
    
    XPLMDataRef handle = nullptr;
    boundRefs[ref] = {
        handle,
        value,
        changeCallback
    };
    
    if constexpr ((std::is_same<T, int>::value) || (std::is_same<T, bool>::value)) {
        handle = XPLMRegisterDataAccessor(ref,
                                          xplmType_Int,
                                          writable ? 1 : 0,
                                          [](void* inRefcon) -> int {
                                            return *static_cast<T*>(inRefcon);
                                          },
                                          [](void* inRefcon, int inValue) {
                                            BoundRef* info = static_cast<BoundRef*>(inRefcon);
                                            T* valuePtr = static_cast<T*>(info->valuePointer);
            
                                            if (info->changeCallback) {
                                                if (info->changeCallback(&inValue)) {
                                                    *valuePtr = inValue;
                                                }
                                            }
                                            else {
                                                *valuePtr = inValue;
                                            }
                                          },
                                          nullptr, nullptr, // Float
                                          nullptr, nullptr, // Double
                                          nullptr, nullptr, // Int array
                                          nullptr, nullptr, // Float array
                                          nullptr, nullptr, // Binary
                                          value, // Read refcon
                                          &boundRefs[ref]);  // Write refcon
    }
    else if constexpr (std::is_same<T, float>::value) {
        
    }
    else if constexpr (std::is_same<T, std::string>::value) {
        handle = XPLMRegisterDataAccessor(ref,
                                          xplmType_Data,
                                          writable ? 1 : 0,
                                          nullptr, nullptr, // Int
                                          nullptr, nullptr, // Float
                                          nullptr, nullptr, // Double
                                          nullptr, nullptr, // Int array
                                          nullptr, nullptr, // Float array
                                          [](void* inRefcon, void* outValue, int inOffset, int inMaxLength) -> int {
                                            T value = *static_cast<T*>(inRefcon);
                                            strncpy(static_cast<char*>(outValue), value.c_str(), inMaxLength);
                                            return static_cast<int>(value.length());
                                          },
                                          [](void* inRefcon, void* inValue, int inOffset, int inMaxLength) {
                                            BoundRef* info = static_cast<BoundRef*>(inRefcon);
                                            T* valuePtr = static_cast<T*>(info->valuePointer);
                                            
                                            if (info->changeCallback) {
                                                if (info->changeCallback(inValue)) {
                                                    *valuePtr = (const char *)inValue;
                                                }
                                            }
                                            else {
                                                *valuePtr = (const char *)inValue;
                                            }
                                          },
                                          value, // Read refcon
                                          &boundRefs[ref]);  // Write refcon
    }
    
    boundRefs[ref].handle = handle;
}

void Dataref::destroyAllBindings(){
    for (auto& [key, ref] : boundRefs) {
        XPLMUnregisterDataAccessor(ref.handle);
    }
    boundRefs.clear();
    
    for (auto& [key, ref] : boundCommands) {
        XPLMUnregisterCommandHandler(ref.handle, handleCommandCallback, 1, nullptr);
    }
    boundCommands.clear();
}

void Dataref::unbind(const char *ref) {
    auto it = boundRefs.find(ref);
    if (it != boundRefs.end()) {
        XPLMUnregisterDataAccessor(it->second.handle);
        boundRefs.erase(it);
    }
    
    auto it2 = boundCommands.find(ref);
    if (it2 != boundCommands.end()) {
        XPLMUnregisterCommandHandler(it2->second.handle, handleCommandCallback, 1, nullptr);
        boundCommands.erase(it2);
    }
}

void Dataref::update() {
    for (auto& [key, data] : cachedValues) {
        std::visit([&](auto&& value) {
            using T = std::decay_t<decltype(value)>;
            cachedValues[key] = get<T>(key.c_str());
        }, data);
    }
}

bool Dataref::getMouse(float *normalizedX, float *normalizedY, float windowX, float windowY) {
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
template std::vector<int> Dataref::getCached<std::vector<int>>(const char* ref);
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
        else if constexpr (std::is_same<T, std::vector<int>>::value) {
            return {};
        }
        else {
            return 0;
        }
    }
    
    return std::get<T>(it->second);
}

template float Dataref::get<float>(const char* ref);
template int Dataref::get<int>(const char* ref);
template std::vector<int> Dataref::get<std::vector<int>>(const char* ref);
template std::string Dataref::get<std::string>(const char* ref);
template <typename T>
T Dataref::get(const char *ref) {
    XPLMDataRef handle = findRef(ref);
    if (!handle) {
        if constexpr (std::is_same<T, std::string>::value) {
            return "";
        }
        else if constexpr (std::is_same<T, std::vector<int>>::value) {
            return {};
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
    else if constexpr (std::is_same<T, std::vector<int>>::value) {
        int size = XPLMGetDatavi(handle, nullptr, 0, 0);
        std::vector<int> outValues(size);
        XPLMGetDatavi(handle, outValues.data(), 0, size);
        return outValues;
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
    else if constexpr (std::is_same<T, std::vector<int>>::value) {
        return {};
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

void Dataref::bindExistingCommand(const char *command, CommandExecutedCallback callback) {
    XPLMCommandRef handle = XPLMFindCommand(command);
    if (!handle) {
        return;
    }
    
    boundCommands[command] = {
        handle,
        callback
    };
    
    XPLMRegisterCommandHandler(handle, handleCommandCallback, 1, nullptr);
}

void Dataref::createCommand(const char *command, const char *description, CommandExecutedCallback callback) {
    XPLMCommandRef handle = XPLMCreateCommand(command, description);
    if (!handle) {
        return;
    }
    
    auto it = boundCommands.find(command);
    if (it != boundCommands.end()) {
        XPLMUnregisterCommandHandler(handle, handleCommandCallback, 1, nullptr);
    }
    
    bindExistingCommand(command, callback);
}

int Dataref::commandCallback(XPLMCommandRef inCommand, XPLMCommandPhase inPhase, void *inRefcon) {
    for (const auto& entry : boundCommands) {
        XPLMCommandRef handle = entry.second.handle;
        if (inCommand == handle) {
            entry.second.callback(inPhase);
            break;
        }
    }
    
    return 1;
}
