#ifndef APPSTATE_H
#define APPSTATE_H

#include <string>
#include <vector>
#include "button.h"
#include "browser.h"
#include "statusbar.h"
#include "notification.h"

struct AvitabDimensions {
    short x;
    short y;
    unsigned short width;
    unsigned short height;
    unsigned short textureWidth;
    unsigned short textureHeight;
    unsigned short browserWidth;
    unsigned short browserHeight;
    static constexpr unsigned char bytesPerPixel = 4;
};

struct AppConfiguration {
    std::string homepage;
    bool audio_muted;
    unsigned short minimum_width;
    unsigned char scroll_speed;
    std::string forced_language;
    std::string user_agent;
    unsigned char framerate;
    struct StatusBarIcon {
        std::string icon;
        std::string url;
    };
    std::vector<StatusBarIcon> statusbarIcons;
};

enum AircraftVariant: unsigned char {
    VariantUnknown = 0,
    VariantZibo738
};

typedef std::function<void()> DelayedTaskFunc;

struct DelayedTask {
    DelayedTaskFunc func;
    float executeAfterElapsedSeconds;
};

class AppState {
private:
    AppState();
    ~AppState();
    static AppState* instance;
    std::string remoteVersion;
    bool shouldBrowserVisible;
    std::vector<DelayedTask> tasks;
    std::vector<Button *> buttons;
    Notification *notification;
    Button *mainMenuButton;
    bool loadAvitabConfig();
    bool fileExists(std::string filename);
    void determineAircraftVariant();

public:
    AvitabDimensions tabletDimensions;
    AppConfiguration config;
    AircraftVariant aircraftVariant = VariantUnknown;
    bool pluginInitialized = false;
    bool hasPower = false;
    bool browserVisible = false;
    bool hasAvitab = false;
    XPLMWindowID standaloneWindow;
    Statusbar *statusbar;
    Browser *browser;
    CursorType activeCursor;
    
    static AppState* getInstance();
    bool initialize();
    void deinitialize();
    void checkLatestVersion();
    
    void update();
    void draw(XPLMWindowID windowId);
    
    bool updateButtons(float normalizedX, float normalizedY, ButtonState state);
    void registerButton(Button *button);
    void unregisterButton(Button *button);
    
    void showBrowser(std::string url = "");
    void showNotification(Notification *notification);
    void executeDelayed(DelayedTaskFunc func, float delay);
    bool loadConfig(bool isReloading = true);
};

#endif
