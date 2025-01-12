#ifndef DATAREF_H
#define DATAREF_H

#include <unordered_map>
#include <variant>
#include <string>
#include <XPLMDataAccess.h>
#include <XPLMDisplay.h>

class Dataref {
private:
    Dataref();
    ~Dataref();
    static Dataref* instance;
    std::unordered_map<std::string, XPLMDataRef> refs;
    std::unordered_map<std::string, std::variant<int, float, std::string>> cachedValues;
    XPLMDataRef findRef(const char* ref);
    float lastMouseX;
    float lastMouseY;
    int lastWindowX;
    int lastWindowY;
    int lastViewHeading;

public:
    static Dataref* getInstance();

    void update();
    bool getMouse(float *normalizedX, float *normalizedY, float windowX = 0, float windowY = 0, XPLMWindowID windowId = 0);
    bool exists(const char *ref);
    template <typename T> T getCached(const char *ref);
    template <typename T> T get(const char *ref);
    template <typename T> void set(const char* ref, T value, bool setCacheOnly = false);
    
    void executeCommand(const char *command);
    void createCommand();
    void shareData();
};

#endif
