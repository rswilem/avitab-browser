#ifndef DATAREF_H
#define DATAREF_H

#include <unordered_map>
#include <variant>
#include <string>
#include <functional>
#include <XPLMUtilities.h>
#include <XPLMDataAccess.h>

typedef std::function<bool(void *value)> BoundRefChangeCallback;
struct BoundRef {
    XPLMDataRef handle;
    void *valuePointer;
    BoundRefChangeCallback changeCallback;
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
    std::unordered_map<std::string, std::variant<int, float, std::string, std::vector<int>>> cachedValues;
    XPLMDataRef findRef(const char* ref);
    float lastMouseX;
    float lastMouseY;
    int lastWindowX;
    int lastWindowY;
    int lastViewHeading;
    
public:
    static Dataref* getInstance();
    
    template <typename T> void bind(const char* ref, T* value, bool writable = false, BoundRefChangeCallback changeCallback = nullptr);
    void bindExistingCommand(const char *command, CommandExecutedCallback callback);
    void createCommand(const char *command, const char *description, CommandExecutedCallback callback);
    int commandCallback(XPLMCommandRef inCommand, XPLMCommandPhase inPhase, void *inRefcon);
    void unbind(const char *ref);
    void destroyAllBindings();

    void update();
    bool getMouse(float *normalizedX, float *normalizedY, float windowX = 0, float windowY = 0);
    bool exists(const char *ref);
    template <typename T> T getCached(const char *ref);
    template <typename T> T get(const char *ref);
    template <typename T> void set(const char* ref, T value, bool setCacheOnly = false);
    
    void executeCommand(const char *command);
};

#endif
