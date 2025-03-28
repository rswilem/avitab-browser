#ifndef APPSTATE_H
#define APPSTATE_H

#include <string>
#include <vector>
#include <XPLMDisplay.h>
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
    bool hide_addressbar;
    unsigned char framerate;
    struct StatusBarIcon {
        std::string icon;
        std::string url;
    };
    std::vector<StatusBarIcon> statusbarIcons;
#if DEBUG
    float debug_value_1;
    float debug_value_2;
    float debug_value_3;
#endif
};

enum AircraftVariant: unsigned char {
    VariantUnknown = 0,
    VariantZibo738,
    VariantFelis742,
    VariantLevelUp737,
    VariantJustFlight,
    VariantIXEG737,
};

typedef std::function<void()> CallbackFunc;

struct DelayedTask {
    CallbackFunc func;
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
    XPLMWindowID mainWindow;
    float brightness;
    AvitabDimensions tabletDimensions;
    AppConfiguration config;
    AircraftVariant aircraftVariant = VariantUnknown;
    bool pluginInitialized = false;
    bool shouldCaptureClickEvents = false;
    bool hasPower = false;
    bool browserVisible = false;
    Statusbar *statusbar;
    Browser *browser;
    CursorType activeCursor;
    
    static AppState* getInstance();
    bool initialize();
    void deinitialize();
    void checkLatestVersion();
    
    void update();
    void draw();
    
    bool updateButtons(float normalizedX, float normalizedY, ButtonState state);
    void registerButton(Button *button);
    void unregisterButton(Button *button);
    
    void showBrowser(std::string url = "");
    void hideBrowser();
    void showNotification(Notification *notification);
    void executeDelayed(CallbackFunc func, float delaySeconds);
    bool loadConfig(bool isReloading = true);
};

#endif
