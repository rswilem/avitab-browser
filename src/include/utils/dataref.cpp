#include "dataref.h"

#include "appstate.h"
#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>
#include <XPLMDisplay.h>
#include <XPLMUtilities.h>

using namespace std;

Dataref *Dataref::instance = nullptr;

int handleCommandCallback(XPLMCommandRef inCommand, XPLMCommandPhase inPhase, void *inRefcon) {
    return Dataref::getInstance()->_commandCallback(inCommand, inPhase, inRefcon);
}

Dataref::Dataref() {
    lastMouseX = 0.0f;
    lastMouseY = 0.0f;
    lastWindowX = 0;
    lastWindowY = 0;
    hasValidSample = false;
    lastHeadPsi = 0.0f;
    lastHeadThe = 0.0f;
    lastHeadX = 0.0f;
    lastHeadY = 0.0f;
    lastHeadZ = 0.0f;
    lastFov = 0.0f;
}

Dataref::~Dataref() {
    instance = nullptr;
}

Dataref *Dataref::getInstance() {
    if (instance == nullptr) {
        instance = new Dataref();
    }

    return instance;
}

template void Dataref::createDataref<int>(const char *ref, int *value, bool writable = false, DatarefShouldChangeCallback<int> changeCallback = nullptr);
template void Dataref::createDataref<bool>(const char *ref, bool *value, bool writable = false, DatarefShouldChangeCallback<bool> changeCallback = nullptr);
template void Dataref::createDataref<float>(const char *ref, float *value, bool writable = false, DatarefShouldChangeCallback<float> changeCallback = nullptr);
template void Dataref::createDataref<double>(const char *ref, double *value, bool writable = false, DatarefShouldChangeCallback<double> changeCallback = nullptr);
template void Dataref::createDataref<std::string>(const char *ref, std::string *value, bool writable = false, DatarefShouldChangeCallback<std::string> changeCallback = nullptr);

template<typename T>
void Dataref::createDataref(const char *ref, T *value, bool writable, DatarefShouldChangeCallback<T> changeCallback) {
    unbind(ref);

    XPLMDataRef handle = nullptr;
    boundRefs[ref] = {
        handle,
        value,
        [changeCallback](DataRefValueType newValue) -> bool {
            if constexpr (std::is_same_v<T, std::string>) {
                if (std::holds_alternative<std::string>(newValue)) {
                    return changeCallback(std::get<std::string>(newValue));
                }
            } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, bool>) {
                if (std::holds_alternative<int>(newValue)) {
                    return changeCallback(std::get<int>(newValue));
                }
            } else if constexpr (std::is_same_v<T, float>) {
                if (std::holds_alternative<float>(newValue)) {
                    return changeCallback(std::get<float>(newValue));
                }
            } else if constexpr (std::is_same_v<T, double>) {
                if (std::holds_alternative<double>(newValue)) {
                    return changeCallback(std::get<double>(newValue));
                }
            }
            return false;
        }};

    if constexpr ((std::is_same<T, int>::value) || (std::is_same<T, bool>::value)) {
        handle = XPLMRegisterDataAccessor(ref, xplmType_Int, writable ? 1 : 0, [](void *inRefcon) -> int {
            return *static_cast<T *>(inRefcon);
        },
            [](void *inRefcon, int inValue) {
                BoundRef *info = static_cast<BoundRef *>(inRefcon);
                T *valuePtr = static_cast<T *>(info->valuePointer);

                if (info->changeCallback) {
                    if (info->changeCallback(inValue)) {
                        *valuePtr = inValue;
                    }
                } else {
                    *valuePtr = inValue;
                }
            },
            nullptr, nullptr, // Float
            nullptr, nullptr, // Double
            nullptr, nullptr, // Int array
            nullptr, nullptr, // Float array
            nullptr, nullptr, // Binary
            value,            // Read refcon
            &boundRefs[ref]); // Write refcon
    } else if constexpr (std::is_same<T, float>::value) {
        handle = XPLMRegisterDataAccessor(ref, xplmType_Float, writable ? 1 : 0, nullptr, nullptr, // Int
            [](void *inRefcon) -> T {
                return *static_cast<T *>(inRefcon);
            },
            [](void *inRefcon, T inValue) {
                BoundRef *info = static_cast<BoundRef *>(inRefcon);
                T *valuePtr = static_cast<T *>(info->valuePointer);

                if (info->changeCallback) {
                    if (info->changeCallback(inValue)) {
                        *valuePtr = inValue;
                    }
                } else {
                    *valuePtr = inValue;
                }
            },
            nullptr, nullptr, // Double
            nullptr, nullptr, // Int array
            nullptr, nullptr, // Float array
            nullptr, nullptr, // Binary
            value,            // Read refcon
            &boundRefs[ref]); // Write refcon
    } else if constexpr (std::is_same<T, double>::value) {
        handle = XPLMRegisterDataAccessor(ref, xplmType_Double, writable ? 1 : 0, nullptr, nullptr, // Int
            nullptr, nullptr,                                                                       // Float
            [](void *inRefcon) -> T {
                return *static_cast<T *>(inRefcon);
            },
            [](void *inRefcon, T inValue) {
                BoundRef *info = static_cast<BoundRef *>(inRefcon);
                T *valuePtr = static_cast<T *>(info->valuePointer);

                if (info->changeCallback) {
                    if (info->changeCallback(inValue)) {
                        *valuePtr = inValue;
                    }
                } else {
                    *valuePtr = inValue;
                }
            },
            nullptr, nullptr, // Int array
            nullptr, nullptr, // Float array
            nullptr, nullptr, // Binary
            value,            // Read refcon
            &boundRefs[ref]); // Write refcon
    } else if constexpr (std::is_same<T, std::string>::value) {
        handle = XPLMRegisterDataAccessor(ref, xplmType_Data, writable ? 1 : 0, nullptr, nullptr, // Int
            nullptr, nullptr,                                                                     // Float
            nullptr, nullptr,                                                                     // Double
            nullptr, nullptr,                                                                     // Int array
            nullptr, nullptr,                                                                     // Float array
            [](void *inRefcon, void *outValue, int inOffset, int inMaxLength) -> int {
                T *value = static_cast<T *>(inRefcon);
                int length = static_cast<int>(value->length());

                // XPLM convention: a null buffer queries the total size. Tools
                // like DataRefTool (and our own get<std::string>) rely on this.
                if (outValue == nullptr) {
                    return length;
                }

                if (inOffset >= length || inMaxLength <= 0) {
                    return 0;
                }

                int copyLength = std::min(inMaxLength, length - inOffset);
                memcpy(outValue, value->c_str() + inOffset, (size_t) copyLength);
                return copyLength;
            },
            [](void *inRefcon, void *inValue, int inOffset, int inMaxLength) {
                BoundRef *info = static_cast<BoundRef *>(inRefcon);
                T *valuePtr = static_cast<T *>(info->valuePointer);

                if (info->changeCallback) {
                    std::string newValue = std::string(static_cast<const char *>(inValue));
                    if (info->changeCallback(newValue)) {
                        *valuePtr = (const char *) inValue;
                    }
                } else {
                    *valuePtr = (const char *) inValue;
                }
            },
            value,            // Read refcon
            &boundRefs[ref]); // Write refcon
    }

    boundRefs[ref].handle = handle;
}

template void Dataref::monitorExistingDataref<int>(const char *ref, DatarefMonitorChangedCallback<int> changeCallback);
template void Dataref::monitorExistingDataref<bool>(const char *ref, DatarefMonitorChangedCallback<bool> changeCallback);
template void Dataref::monitorExistingDataref<float>(const char *ref, DatarefMonitorChangedCallback<float> changeCallback);
template void Dataref::monitorExistingDataref<double>(const char *ref, DatarefMonitorChangedCallback<double> changeCallback);
template void Dataref::monitorExistingDataref<std::string>(const char *ref, DatarefMonitorChangedCallback<std::string> changeCallback);

template<typename T>
void Dataref::monitorExistingDataref(const char *ref, DatarefMonitorChangedCallback<T> changeCallback) {
    if constexpr (std::is_same<T, std::string>::value) {
        set<T>(ref, "", true);
    } else {
        set<T>(ref, 0, true);
    }

    boundRefs[ref] = {
        0,
        nullptr,
        [changeCallback](DataRefValueType newValue) -> bool {
            if constexpr (std::is_same_v<T, std::string>) {
                if (std::holds_alternative<std::string>(newValue)) {
                    changeCallback(std::get<std::string>(newValue));
                }
            } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, bool>) {
                if (std::holds_alternative<int>(newValue)) {
                    changeCallback(std::get<int>(newValue));
                }
            } else if constexpr (std::is_same_v<T, float>) {
                if (std::holds_alternative<float>(newValue)) {
                    changeCallback(std::get<float>(newValue));
                }
            } else if constexpr (std::is_same_v<T, double>) {
                if (std::holds_alternative<double>(newValue)) {
                    changeCallback(std::get<double>(newValue));
                }
            }

            return false;
        }};
}

void Dataref::destroyAllBindings() {
    for (auto &[key, ref] : boundRefs) {
        // Monitors registered via monitorExistingDataref have no accessor handle.
        if (ref.handle) {
            XPLMUnregisterDataAccessor(ref.handle);
        }
    }
    boundRefs.clear();

    for (auto &[key, ref] : boundCommands) {
        XPLMUnregisterCommandHandler(ref.handle, handleCommandCallback, 1, nullptr);
    }
    boundCommands.clear();

    // Handles to aircraft-provided datarefs die with the aircraft plugin, and
    // cached values from the previous aircraft must not survive into the next
    // one. Without this, update() also keeps polling the old aircraft's refs
    // forever and both maps grow on every aircraft switch.
    refs.clear();
    cachedValues.clear();
}

void Dataref::unbind(const char *ref) {
    auto it = boundRefs.find(ref);
    if (it != boundRefs.end()) {
        if (it->second.handle) {
            XPLMUnregisterDataAccessor(it->second.handle);
        }
        boundRefs.erase(it);
    }

    auto it2 = boundCommands.find(ref);
    if (it2 != boundCommands.end()) {
        XPLMUnregisterCommandHandler(it2->second.handle, handleCommandCallback, 1, nullptr);
        boundCommands.erase(it2);
    }
}

void Dataref::update() {
    // Collect changes first, then fire callbacks. A change callback can call
    // set() on a new dataref, which inserts into cachedValues and would rehash
    // the map mid-iteration, invalidating the iterator. Updating existing values
    // in-place (below) never rehashes, so the first pass is safe.
    std::vector<std::pair<std::string, DataRefValueType>> changes;
    for (auto &[key, data] : cachedValues) {
        std::visit([&](auto &&value) {
            using T = std::decay_t<decltype(value)>;
            T newValue = get<T>(key.c_str());
            if (value != newValue) {
                changes.emplace_back(key, newValue);
                value = newValue;
            }
        },
            data);
    }

    for (auto &[key, newValue] : changes) {
        auto it = boundRefs.find(key);
        if (it != boundRefs.end() && it->second.changeCallback) {
            it->second.changeCallback(newValue);
        }
    }
}

bool Dataref::cameraMoved(float headPsi, float headThe, float headX, float headY, float headZ, float fov, int viewType) {
    // Anything other than the 3D cockpit (1026) can't anchor the tablet.
    if (viewType != 1026) {
        return true;
    }

    // Wrap-safe angular deltas so the 0/360 boundary doesn't false-trigger.
    float dPsi = fabsf(fmodf(headPsi - lastHeadPsi + 540.0f, 360.0f) - 180.0f);
    float dThe = fabsf(fmodf(headThe - lastHeadThe + 540.0f, 360.0f) - 180.0f);
    if (dPsi > VIEW_ROTATION_DEADBAND_DEG || dThe > VIEW_ROTATION_DEADBAND_DEG) {
        return true;
    }

    if (fabsf(headX - lastHeadX) > VIEW_TRANSLATION_DEADBAND_M ||
        fabsf(headY - lastHeadY) > VIEW_TRANSLATION_DEADBAND_M ||
        fabsf(headZ - lastHeadZ) > VIEW_TRANSLATION_DEADBAND_M) {
        return true;
    }

    if (fabsf(fov - lastFov) > VIEW_FOV_DEADBAND_DEG) {
        return true;
    }

    return false;
}

bool Dataref::getMouse(float *normalizedX, float *normalizedY, float windowX, float windowY) {
    float mouseX = get<float>("sim/graphics/view/click_3d_x_pixels");
    float mouseY = get<float>("sim/graphics/view/click_3d_y_pixels");

    bool extrapolated = false;

    if (windowX > 0) {
        float headPsi = get<float>("sim/graphics/view/pilots_head_psi");
        float headThe = get<float>("sim/graphics/view/pilots_head_the");
        float headX = get<float>("sim/graphics/view/pilots_head_x");
        float headY = get<float>("sim/graphics/view/pilots_head_y");
        float headZ = get<float>("sim/graphics/view/pilots_head_z");
        float fov = get<float>("sim/graphics/view/field_of_view_deg");
        int viewType = get<int>("sim/graphics/view/view_type");
        bool moved = cameraMoved(headPsi, headThe, headX, headY, headZ, fov, viewType);

        if (mouseX < 0 || mouseY < 0) {
            // The 3D click position is unavailable (some tablets report this too
            // early). Keep the interaction alive by extrapolating from the last
            // valid sample, but only while the camera hasn't moved.
            if (!hasValidSample || moved) {
                hasValidSample = false;
                return false;
            }
            mouseX = lastMouseX + (windowX - lastWindowX) / 1.5;
            mouseY = lastMouseY + (windowY - lastWindowY) / 1.5;
            extrapolated = true;
        } else if (moved && mouseX == lastMouseX && mouseY == lastMouseY) {
            // Coordinates identical to the stored ones after a view change are a
            // stale read, not a real position. Not gated on hasValidSample: the
            // stored pose only updates below, so this stays latched until X-Plane
            // reports different coordinates.
            hasValidSample = false;
            return false;
        } else {
            lastMouseX = mouseX;
            lastMouseY = mouseY;
            lastWindowX = windowX;
            lastWindowY = windowY;
            lastHeadPsi = headPsi;
            lastHeadThe = headThe;
            lastHeadX = headX;
            lastHeadY = headY;
            lastHeadZ = headZ;
            lastFov = fov;
            hasValidSample = true;
        }
    }

    if (mouseX == -1 || mouseY == -1) {
        return false;
    }

    *normalizedX = (mouseX - AppState::getInstance()->tabletDimensions.x) / AppState::getInstance()->tabletDimensions.width;
    *normalizedY = (mouseY - AppState::getInstance()->tabletDimensions.y) / AppState::getInstance()->tabletDimensions.height;

    // The margin around the tablet exists because the Felis status bar and back
    // button are drawn just above and below the panel rect, so clicks there
    // still belong to us. Horizontally there is nothing of ours outside the
    // rect, and on the Felis the CDU sits right next to the tablet on the same
    // panel texture, where a margin swallows its left-hand keys before the
    // aircraft sees the click. So no horizontal slack on that variant.
    AircraftVariant variant = AppState::getInstance()->aircraftVariant;
    float paddingX = (variant == VariantFelis742 || variant == VariantFelis742Cargo) ? 0.0f : 0.1f;
    constexpr float paddingY = 0.1f;

    bool inBounds = !(*normalizedX < -paddingX || *normalizedX > 1.0f + paddingX || *normalizedY < -paddingY || *normalizedY > 1.0f + paddingY);

    // Once an extrapolated position leaves the tablet, drop the anchor so
    // extrapolation can't resume until a real click_3d sample arrives.
    if (extrapolated && !inBounds) {
        hasValidSample = false;
    }

    return inBounds;
}

XPLMDataRef Dataref::findRef(const char *ref) {
    if (refs.find(ref) != refs.end()) {
        return refs[ref];
    }

    XPLMDataRef handle = XPLMFindDataRef(ref);
    if (!handle) {
        return nullptr;
    }

    refs[ref] = handle;
    return refs[ref];
}

bool Dataref::exists(const char *ref) {
    return XPLMFindDataRef(ref) != nullptr;
}

template int Dataref::getCached<int>(const char *ref);
template std::vector<int> Dataref::getCached<std::vector<int>>(const char *ref);
template float Dataref::getCached<float>(const char *ref);
template std::string Dataref::getCached<std::string>(const char *ref);

template<typename T>
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
        } else if constexpr (std::is_same<T, std::vector<int>>::value) {
            return {};
        } else {
            return 0;
        }
    }

    return std::get<T>(it->second);
}

template float Dataref::get<float>(const char *ref);
template double Dataref::get<double>(const char *ref);
template int Dataref::get<int>(const char *ref);
template bool Dataref::get<bool>(const char *ref);
template std::vector<int> Dataref::get<std::vector<int>>(const char *ref);
template std::string Dataref::get<std::string>(const char *ref);

template<typename T>
T Dataref::get(const char *ref) {
    XPLMDataRef handle = findRef(ref);
    if (!handle) {
        if constexpr (std::is_same<T, std::string>::value) {
            return "";
        } else if constexpr (std::is_same<T, std::vector<int>>::value) {
            return {};
        } else {
            return 0;
        }
    }

    if constexpr (std::is_same<T, int>::value) {
        return XPLMGetDatai(handle);
    } else if constexpr (std::is_same<T, bool>::value) {
        return XPLMGetDatai(handle) > 0;
    } else if constexpr (std::is_same<T, float>::value) {
        return XPLMGetDataf(handle);
    } else if constexpr (std::is_same<T, double>::value) {
        return XPLMGetDatad(handle);
    } else if constexpr (std::is_same<T, std::vector<int>>::value) {
        int size = XPLMGetDatavi(handle, nullptr, 0, 0);
        std::vector<int> outValues(size);
        XPLMGetDatavi(handle, outValues.data(), 0, size);
        return outValues;
    } else if constexpr (std::is_same<T, std::string>::value) {
        int size = XPLMGetDatab(handle, nullptr, 0, 0);
        if (size <= 0) {
            return "";
        }

        std::vector<char> buffer(size);
        int length = XPLMGetDatab(handle, buffer.data(), 0, size);
        if (length <= 0) {
            return "";
        }

        // The data is not guaranteed to be null-terminated; conversely, a
        // provider that does terminate should not leave a trailing '\0'.
        length = std::min(length, size);
        return std::string(buffer.data(), strnlen(buffer.data(), length));
    }

    if constexpr (std::is_same<T, std::string>::value) {
        return "";
    } else if constexpr (std::is_same<T, std::vector<int>>::value) {
        return {};
    } else {
        return 0;
    }
}

template void Dataref::set<float>(const char *ref, float value, bool setCacheOnly);
template void Dataref::set<int>(const char *ref, int value, bool setCacheOnly);
template void Dataref::set<std::string>(const char *ref, std::string value, bool setCacheOnly);

template<typename T>
void Dataref::set(const char *ref, T value, bool setCacheOnly) {
    XPLMDataRef handle = findRef(ref);
    if (!handle) {
        return;
    }

    cachedValues[ref] = value;

    if (setCacheOnly) {
        return;
    }

    if constexpr (std::is_same<T, int>::value) {
        XPLMSetDatai(handle, value);
    } else if constexpr (std::is_same<T, float>::value) {
        XPLMSetDataf(handle, value);
    } else if constexpr (std::is_same<T, std::string>::value) {
        XPLMSetDatab(handle, (char *) value.c_str(), 0, (unsigned int) value.length());
    }
}

void Dataref::executeCommand(const char *command, XPLMCommandPhase phase) {
    XPLMCommandRef ref = XPLMFindCommand(command);
    if (!ref) {
        return;
    }

    if (phase == -1) {
        XPLMCommandOnce(ref);
    } else if (phase == xplm_CommandBegin) {
        XPLMCommandBegin(ref);
    } else if (phase == xplm_CommandEnd) {
        XPLMCommandEnd(ref);
    }
}

void Dataref::bindExistingCommand(const char *command, CommandExecutedCallback callback) {
    XPLMCommandRef handle = XPLMFindCommand(command);
    if (!handle) {
        return;
    }

    boundCommands[command] = {
        handle,
        callback};

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

int Dataref::_commandCallback(XPLMCommandRef inCommand, XPLMCommandPhase inPhase, void *inRefcon) {
    for (const auto &entry : boundCommands) {
        XPLMCommandRef handle = entry.second.handle;
        if (inCommand == handle) {
            entry.second.callback(inPhase);
            break;
        }
    }

    return 1;
}
