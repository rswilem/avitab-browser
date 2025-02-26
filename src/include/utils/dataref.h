#ifndef DATAREF_H
#define DATAREF_H

#include <unordered_map>
#include <variant>
#include <string>
#include <functional>
#include <XPLMUtilities.h>
#include <XPLMDataAccess.h>

using DataRefValueType = std::variant<int, float, double, std::string, std::vector<int>>;
template <typename T> using DatarefShouldChangeCallback = std::function<bool(T)>;
template <typename T> using DatarefMonitorChangedCallback = std::function<void(T)>;

struct BoundRef {
    XPLMDataRef handle;
    void *valuePointer;
    DatarefShouldChangeCallback<DataRefValueType> changeCallback;
};

typedef std::function<void(XPLMCommandPhase inPhase)> CommandExecutedCallback;
struct BoundCommand {
    XPLMCommandRef handle;
    CommandExecutedCallback callback;
};

class Dataref {
private:
    Dataref();
    ~Dataref();
    static Dataref* instance;
    std::unordered_map<std::string, BoundRef> boundRefs;
    std::unordered_map<std::string, BoundCommand> boundCommands;
    std::unordered_map<std::string, XPLMDataRef> refs;
    std::unordered_map<std::string, DataRefValueType> cachedValues;
    XPLMDataRef findRef(const char* ref);
    float lastMouseX;
    float lastMouseY;
    int lastWindowX;
    int lastWindowY;
    int lastViewHeading;
    
public:
    static Dataref* getInstance();
    
    template <typename T> void monitorExistingDataref(const char *ref, DatarefMonitorChangedCallback<T> callback);
    template <typename T> void createDataref(const char* ref, T* value, bool writable = false, DatarefShouldChangeCallback<T> changeCallback = nullptr);
    void bindExistingCommand(const char *command, CommandExecutedCallback callback);
    void createCommand(const char *command, const char *description, CommandExecutedCallback callback);
    void unbind(const char *ref);
    void destroyAllBindings();
    int _commandCallback(XPLMCommandRef inCommand, XPLMCommandPhase inPhase, void *inRefcon);
    

    void update();
    bool getMouse(float *normalizedX, float *normalizedY, float windowX = 0, float windowY = 0);
    bool exists(const char *ref);
    template <typename T> T getCached(const char *ref);
    template <typename T> T get(const char *ref);
    template <typename T> void set(const char* ref, T value, bool setCacheOnly = false);
    
    void executeCommand(const char *command);
};

#endif
