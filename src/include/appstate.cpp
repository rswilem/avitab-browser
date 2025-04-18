#include "appstate.h"
#include <XPLMUtilities.h>
#include <XPLMProcessing.h>
#include <XPLMGraphics.h>
#include <fstream>
#include "INIReader.h"
#include "path.h"
#include "config.h"
#include "dataref.h"
#include "json.hpp"
#include <iostream>
#include <cmath>
#include <regex>
#include "json.hpp"
#include <curl/curl.h>

AppState* AppState::instance = nullptr;

AppState::AppState() {
    remoteVersion = "";
    shouldBrowserVisible = false;
    notification = nullptr;
    mainMenuButton = nullptr;
    aircraftVariant = VariantUnknown;
    pluginInitialized = false;
    shouldCaptureClickEvents = false;
    hasPower = false;
    browserVisible = false;
    statusbar = nullptr;
    browser = nullptr;
    activeCursor = CursorDefault;
    brightness = 1.0f;
}

AppState::~AppState() {
    instance = nullptr;
}

AppState* AppState::getInstance() {
    if (instance == nullptr) {
        instance = new AppState();
    }
    
    return instance;
}

bool AppState::initialize() {
    if (pluginInitialized) {
        return false;
    }
    
    hasPower = false;
    shouldBrowserVisible = false;
    browserVisible = false;
    if (!statusbar) {
        statusbar = new Statusbar();
    }
    
    if (!browser) {
        browser = new Browser();
    }
    
    Path::getInstance()->reloadPaths();
    
    if (Path::getInstance()->aircraftDirectory.empty()) {
        return false;
    }
    
    if (!loadConfig(false)) {
        return false;
    }
    
    determineAircraftVariant();
    
    statusbar->initialize();
    browser->initialize();
    
    if (aircraftVariant == VariantZibo738) {
        mainMenuButton = new Button(Path::getInstance()->pluginDirectory + "/assets/menu-item-zibo.png");
        mainMenuButton->setPosition(0.604f, 0.4f);
    }
    else if (aircraftVariant == VariantLevelUp737) {
        mainMenuButton = new Button(Path::getInstance()->pluginDirectory + "/assets/menu-item-levelup737.png");
        mainMenuButton->setPosition(0.604f, 0.43f);
    }
    else if (aircraftVariant == VariantFelis742) {
//        mainMenuButton = new Button(Path::getInstance()->pluginDirectory + "/assets/menu-item-felis.png");
//        mainMenuButton->setPosition(0.775, 0.615);
        mainMenuButton = new Button(Path::getInstance()->pluginDirectory + "/assets/menu-item.png");
        mainMenuButton->setPosition(0.2f, 0.568f);
    }
    else {
        mainMenuButton = new Button(Path::getInstance()->pluginDirectory + "/assets/menu-item.png");
        mainMenuButton->setPosition(0.2f, 0.568f);
    }
    
    mainMenuButton->visible = false;
    mainMenuButton->setClickHandler([](){
        AppState::getInstance()->showBrowser();
        return true;
    });
    
    Dataref::getInstance()->createDataref<bool>("avitab_browser/visible", &browserVisible);
    Dataref::getInstance()->createCommand("avitab_browser/toggle", "Show or hide the AviTab Browser in the 3D cockpit", [this](XPLMCommandPhase inPhase) {
        if (inPhase != xplm_CommandBegin) {
            return;
        }
        
        if (!browserVisible) {
            showBrowser();
        }
        else {
            hideBrowser();
        }
    });
    
    if (aircraftVariant == VariantIXEG737) {
        Dataref::getInstance()->monitorExistingDataref<int>("avitab/is_in_menu", [this](int isInMenu) {
            Dataref::getInstance()->set<int>("avitab/panel_enabled", (shouldBrowserVisible || browserVisible) && !isInMenu ? 0 : 1);
        });
    }
    
    pluginInitialized = true;
    return true;
}

void AppState::deinitialize() {
    if (!pluginInitialized) {
        return;
    }
    
    Dataref::getInstance()->destroyAllBindings();
    
    tasks.clear();
    buttons.clear();
    notification = nullptr;
    browser->visibilityWillChange(false);
    browserVisible = false;
    browser->destroy();
    browser = nullptr;
    statusbar->destroy();
    statusbar = nullptr;
    pluginInitialized = false;
    shouldCaptureClickEvents = false;
    instance = nullptr;
}

void AppState::checkLatestVersion() {
    if (!remoteVersion.empty()) {
        // Version information was already fetched. Only check once per session.
        return;
    }
    
    std::string response;
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, VERSION_CHECK_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    CURLcode status = curl_easy_perform(curl);
    if (status != CURLE_OK) {
        debug("Version fetch failed: %s\n", curl_easy_strerror(status));
    }
    curl_easy_cleanup(curl);

    try {
        std::string tag = nlohmann::json::parse(response)[0]["tag_name"];
        if (tag.starts_with("v")) {
            tag = tag.substr(1);
        }
        
        remoteVersion = tag;
        std::string cleanedRemote = std::regex_replace(tag, std::regex("[^0-9]"), "");
        std::string cleanedLocal = std::regex_replace(VERSION, std::regex("[^0-9]"), "");
        int remoteVersionNumber = std::stoi(cleanedRemote);
        int localVersionNumber = std::stoi(cleanedLocal);
        if (remoteVersionNumber > localVersionNumber) {
            debug("There is a newer version of the plugin available. Current: %s, latest: %s\n", VERSION, tag.c_str());
            std::string description = "There is an update available for the " + std::string(FRIENDLY_NAME) + " plugin.\n\nVersion " + tag + ".\n";
            showNotification(new Notification("Update available", description));
        }
    } catch (const std::exception& e) {
        debug("Could not fetch latest version information from GitHub. Reason: %s\n", e.what());
        // Assume we're on the latest version to prevent refetching
        remoteVersion = VERSION;
    }
}

void AppState::update() {
    if (!browser) {
        return;
    }
    
    bool canBrowserVisible = false;
    if (aircraftVariant == VariantZibo738 || aircraftVariant == VariantLevelUp737) {
        hasPower = Dataref::getInstance()->getCached<int>("laminar/B738/tab/power") == 1 && Dataref::getInstance()->getCached<int>("laminar/B738/tab/boot_active") == 0;
        canBrowserVisible = hasPower && Dataref::getInstance()->getCached<int>("laminar/B738/tab/menu_page") == 8;

        brightness = aircraftVariant == VariantZibo738 && Dataref::getInstance()->getCached<int>("laminar/B738/tab/efb_night_mode") ? 0.5f : 1.0f;
        mainMenuButton->visible = (Dataref::getInstance()->getCached<int>("laminar/B738/tab/menu_page") == 11 && !Dataref::getInstance()->getCached<int>("avitab/panel_enabled"));
    }
    else if (aircraftVariant == VariantFelis742) {
        hasPower = Dataref::getInstance()->getCached<int>("avitab/panel_powered") && Dataref::getInstance()->getCached<int>("avitab/panel_enabled");
        canBrowserVisible = hasPower && Dataref::getInstance()->getCached<int>("avitab/is_in_menu") == 0;
        
        brightness = fmin(1.0f, fmax(0.0f, Dataref::getInstance()->getCached<float>("avitab/brightness")));
        mainMenuButton->visible = hasPower && Dataref::getInstance()->getCached<int>("avitab/panel_enabled") && Dataref::getInstance()->getCached<int>("avitab/is_in_menu");
    }
    else if (aircraftVariant == VariantJustFlight) {
        bool hadPower = hasPower;
        hasPower = Dataref::getInstance()->getCached<int>("avitab/panel_powered") && Dataref::getInstance()->getCached<int>("avitab/panel_enabled") && Dataref::getInstance()->getCached<int>("thranda/avitab/aviTabSwap");
        canBrowserVisible = hasPower && Dataref::getInstance()->getCached<int>("avitab/is_in_menu") == 0;
        
        if (!hadPower && hasPower) {
            // The Duchess creates a window to capture clicks whenever AviTab is opened. We want to be on top of that window.
            XPLMBringWindowToFront(mainWindow);
        }
        
        brightness = fmin(1.0f, fmax(0.0f, Dataref::getInstance()->getCached<float>("avitab/brightness")));
        mainMenuButton->visible = AppState::getInstance()->hasPower && Dataref::getInstance()->getCached<int>("avitab/is_in_menu");
    }
    else if (aircraftVariant == VariantIXEG737) {
        hasPower = Dataref::getInstance()->getCached<int>("avitab/panel_powered") && Dataref::getInstance()->getCached<int>("ixeg/733/misc/show_avitab");
        canBrowserVisible = hasPower && Dataref::getInstance()->getCached<int>("avitab/is_in_menu") == 0;
        
        brightness = fmin(1.0f, fmax(0.0f, Dataref::getInstance()->getCached<float>("avitab/brightness")));
        mainMenuButton->visible = AppState::getInstance()->hasPower && Dataref::getInstance()->getCached<int>("avitab/is_in_menu");
    }
    else {
        hasPower = Dataref::getInstance()->getCached<int>("avitab/panel_powered") && Dataref::getInstance()->getCached<int>("avitab/panel_enabled");
        canBrowserVisible = hasPower && Dataref::getInstance()->getCached<int>("avitab/is_in_menu") == 0;
        
        brightness = fmin(1.0f, fmax(0.0f, Dataref::getInstance()->getCached<float>("avitab/brightness")));
        mainMenuButton->visible = AppState::getInstance()->hasPower && Dataref::getInstance()->getCached<int>("avitab/is_in_menu");
    }
    
    if (browserVisible && !canBrowserVisible) {
        browser->visibilityWillChange(false);
        browserVisible = false;
        
        if (!hasPower && Dataref::getInstance()->getCached<int>("avitab/is_in_menu") == 0) {
            Dataref::getInstance()->executeCommand("AviTab/Home");
        }
    }
    else if (shouldBrowserVisible && canBrowserVisible) {
        browser->visibilityWillChange(true);
        browserVisible = true;
        shouldBrowserVisible = false;
        checkLatestVersion();
    }

    if (notification) {
        notification->update();
    }
    
    tasks.erase(
        std::remove_if(tasks.begin(), tasks.end(), [&](const DelayedTask& task) {
            if (XPLMGetElapsedTime() > task.executeAfterElapsedSeconds) {
                task.func();
                return true;
            }
            
            return false;
        }),
        tasks.end()
    );
}

void AppState::draw() {
    if (!pluginInitialized || !hasPower) {
        return;
    }
    
    if (aircraftVariant == VariantIXEG737 && browserVisible) {
        XPLMSetGraphicsState(
                             0, // No fog, equivalent to glDisable(GL_FOG);
                             0, // One texture, equivalent to glEnable(GL_TEXTURE_2D);
                             0, // No lighting, equivalent to glDisable(GL_LIGHT0);
                             0, // No alpha testing, e.g glDisable(GL_ALPHA_TEST);
                             1, // Use alpha blending, e.g. glEnable(GL_BLEND);
                             0, // No depth read, e.g. glDisable(GL_DEPTH_TEST);
                             0 // No depth write, e.g. glDepthMask(GL_FALSE);
        );

        int x1 = tabletDimensions.x;
        int y1 = tabletDimensions.y;
        int x2 = x1 + tabletDimensions.width;
        int y2 = y1 + tabletDimensions.height;
        
        glColor3f(0.0f, 0.0f, 0.0f);
        glBegin(GL_QUADS);
        glVertex2f(x1, y1);
        glVertex2f(x1, y2);
        glVertex2f(x2, y2);
        glVertex2f(x2, y1);
        glEnd();
    }
    
    set_brightness(brightness);
    mainMenuButton->draw();
    statusbar->draw();
    
    if (browserVisible || shouldBrowserVisible) {
        browser->draw();
    }
    
    if (notification) {
        notification->draw();
    }
}

bool AppState::updateButtons(float normalizedX, float normalizedY, ButtonState state) {
    bool didAct = false;
    for (const auto& button : buttons) {
        didAct = didAct || button->handleState(normalizedX, normalizedY, state);
    }
    
    return didAct;
}

void AppState::registerButton(Button *button) {
    buttons.push_back(button);
}

void AppState::unregisterButton(Button *button) {
    auto it = std::find(buttons.begin(), buttons.end(), button);
    if (it != buttons.end()) {
        buttons.erase(it);
    }
}

void AppState::showBrowser(std::string url) {
    if (!hasPower) {
        return;
    }
    
    if (!browserVisible) {
        if (aircraftVariant == VariantZibo738 || aircraftVariant == VariantLevelUp737) {
            Dataref::getInstance()->executeCommand("laminar/B738/tab/home");
            Dataref::getInstance()->executeCommand("laminar/B738/tab/spec");
        }
        else {
            Dataref::getInstance()->executeCommand("AviTab/app_about");
        }
        shouldBrowserVisible = true;
    }
    
    if (!url.empty()) {
        browser->loadUrl(url);
    }
}

void AppState::hideBrowser() {
    if (!hasPower || !browserVisible) {
        return;
    }
    
    if (aircraftVariant == VariantZibo738 || aircraftVariant == VariantLevelUp737) {
        Dataref::getInstance()->executeCommand("laminar/B738/tab/home");
    }
    else {
        Dataref::getInstance()->executeCommand("AviTab/Home");
    }
}

void AppState::showNotification(Notification *aNotification) {
    if (notification && !aNotification) {
        notification->destroy();
    }
    
    notification = aNotification;
}

void AppState::executeDelayed(CallbackFunc func, float delaySeconds) {
    tasks.push_back({
        func,
        XPLMGetElapsedTime() + delaySeconds
    });
}

bool AppState::loadConfig(bool isReloading) {
    if (Path::getInstance()->pluginDirectory.empty()) {
        return false;
    }
    
    std::string filename = Path::getInstance()->pluginDirectory + "/config.ini";
    if (isReloading) {
        debug("Reloading configuration at %s...\n", filename.c_str());
    }
    
    if (!fileExists(filename)) {
        const char *defaultConfig = R"(# AviTab browser configuration file.
# If you're having trouble with this file or missing parameters, delete it and restart X-Plane.
# This file will then be recreated with default settings.
[browser]
homepage=https://www.google.com
audio_muted=false
# minimum_width: Ensures the browser width does not go below this value.
# This is useful for planes with small AviTab resolutions.
# The height is adjusted proportionally to maintain the aspect ratio.
# Note: Setting a minimum width scales the browser, which may reduce quality. 
minimum_width=
# scroll_speed: The speed/steps in which the browser scrolls.
# The default value is 5. Increase to scroll faster.
scroll_speed=
# forced_language: The language code for the application.
# Valid values: en-US, en-GB, nl-NL, fr-FR, etc.
# Leave empty for default language.
forced_language=
# user_agent: The User-Agent header for the browser
# Leave empty for the default Chrome UA.
user_agent=
# hide_addressbar: Whether the address bar should be hidden or not. Default is false.
hide_addressbar=
# framerate: The number of frames per second to render the browser. Saves CPU if set to a lower value.
# The browser will still sleep / idle when able or not visible.
# Leave empty for default framerate.
framerate=

# Statusbar: Define up to 5 bookmarks for easy access.
# Use icon_<index> and url_<index> for each icon.
# Values of icon_<index> can be found at https://feathericons.com/
# Leave url_<index> empty to hide the icon.
[statusbar]
icon_1=search
url_1=https://www.google.com
icon_2=mail
url_2=https://dispatch.simbrief.com/briefing/latest
icon_3=map
url_3=https://vatsim-radar.com
icon_4=
url_4=
icon_5=
url_5=
)";
        
        std::ofstream fileOutputHandle(filename);
        if (fileOutputHandle.is_open()) {
            fileOutputHandle << defaultConfig;
            fileOutputHandle.close();
            debug("Default config file written to %s\n", filename.c_str());
        } else {
            debug("Failed to write default config file at %s\n", filename.c_str());
        }
    }
    
    INIReader reader(filename);
    if (reader.ParseError() != 0) {
        debug("Could not read config file at path %s, file is malformed.\n", filename.c_str());
        return false;
    }
    
    config.homepage = reader.Get("browser", "homepage", "https://www.google.com");
    config.audio_muted = reader.GetBoolean("browser", "audio_muted", false);
    config.minimum_width = reader.GetInteger("browser", "minimum_width", 0);
    config.scroll_speed = reader.GetInteger("browser", "scroll_speed", 5);
    config.forced_language = reader.Get("browser", "forced_language", "");
    config.user_agent = reader.GetString("browser", "user_agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/117.2.5.0 Safari/537.36");
    config.hide_addressbar = reader.GetBoolean("browser", "hide_addressbar", false);
    config.framerate = reader.GetInteger("browser", "framerate", 25);
    
    config.statusbarIcons.clear();
    
#if DEBUG
    config.debug_value_1 = reader.GetReal("debug", "debug_value_1", 0.0f);
    config.debug_value_2 = reader.GetReal("debug", "debug_value_2", 0.0f);
    config.debug_value_3 = reader.GetReal("debug", "debug_value_3", 0.0f);
    config.statusbarIcons.push_back({"terminal", "__DEBUG__"});
#endif
    
    for (int i = 1; i <= 5; ++i) {
        std::string icon = reader.Get("statusbar", "icon_" + std::to_string(i), "globe");
        std::string url = reader.Get("statusbar", "url_" + std::to_string(i), "");
        if (!url.empty()) {
            AppConfiguration::StatusBarIcon statusBarIcon = {icon, url};
            config.statusbarIcons.push_back(statusBarIcon);
        }
    }
    
    if (!loadAvitabConfig()) {
        debug("Could not find AviTab.json config file in aircraft directory, or the JSON file is malformed. Not loading the plugin for this aircraft.\n");
        return false;
    }
    
    if (isReloading) {
        debug("Config file has been reloaded.\n");
        statusbar->destroy();
        statusbar->initialize();
        
        if (browser && browserVisible) {
            std::string url = std::string(browser->currentUrl.c_str());
            if (!url.empty()) {
                browser->visibilityWillChange(false);
                browserVisible = false;
                browser->destroy();
                browser->initialize();
                browser->loadUrl(url);
                browser->visibilityWillChange(true);
                browserVisible = true;
            }
        }
    }
    
    return true;
}

bool AppState::loadAvitabConfig() {
    if (Path::getInstance()->aircraftDirectory.empty()) {
        return false;
    }
    
    std::string filename = Path::getInstance()->aircraftDirectory + "/AviTab.json";
    std::ifstream fileHandle(filename);
    if (!fileHandle.good()) {
        filename = Path::getInstance()->aircraftDirectory + "/avitab.json";
        fileHandle = std::ifstream(filename);
        if (!fileHandle.good()) {
            return false;
        }
    }
    
    nlohmann::json data = nullptr;
    try {
        data = nlohmann::json::parse(fileHandle);
    } catch (const nlohmann::json::parse_error& e) {
        debug("There was an error parsing the AviTab.json file:\n%s\n", e.what());
        
        // Be graceful and try to find the first '{' and last '}', then parse again.
        std::stringstream buffer;
        fileHandle.clear();
        fileHandle.seekg(0);
        buffer << fileHandle.rdbuf();

        std::string content = buffer.str();
        auto start = content.find('{');
        auto end = content.rfind('}');
        if (start != std::string::npos && end != std::string::npos && end > start) {
            debug("Retry parsing the AviTab.json file...\n");
            try {
                data = nlohmann::json::parse(content.substr(start, end - start + 1));
                debug("Parsed AviTab.json gracefully and succeeded.\n");
            } catch (const nlohmann::json::parse_error& nested_e) {
                debug("Retried parsing and failed again:\n%s\n", e.what());
            }
        }
    }
    
    fileHandle.close();
    
    if (data == nullptr || !data.contains("panel")) {
        return false;
    }
    
    if (data["panel"].contains("disable_capture_window") && data["panel"]["disable_capture_window"]) {
        shouldCaptureClickEvents = true;
    }
    
    tabletDimensions = {
        data["panel"]["left"],
        data["panel"]["bottom"],
        data["panel"]["width"],
        data["panel"]["height"],
        0,0,
        0,0
    };
    
#if AVITAB_USE_FIXED_ASPECT_RATIO
    constexpr float aspectRatio = 0.6f; // Avitab 800x480
    float aspectHeight = (float)tabletDimensions.width * aspectRatio;
    tabletDimensions.y += (tabletDimensions.height - aspectHeight) / 2.0f;
    tabletDimensions.height = aspectHeight;
#endif
    
    float multiplier = tabletDimensions.width < config.minimum_width ? (float)config.minimum_width / tabletDimensions.width : 1;
    tabletDimensions.textureWidth = pow(2, ceil(log2(tabletDimensions.width * multiplier)));
    tabletDimensions.textureHeight = pow(2, ceil(log2(tabletDimensions.height * multiplier)));
    tabletDimensions.browserWidth = ceil(tabletDimensions.width * multiplier);
    tabletDimensions.browserHeight = ceil(tabletDimensions.height * multiplier);
    
    debug("Found AviTab.json config (%ipx x %ipx)\n", tabletDimensions.width, tabletDimensions.height);
    if (tabletDimensions.browserWidth > tabletDimensions.width) {
        debug("AviTab.json resolution was smaller than %ipx, using upscaled browser. (%ipx x %ipx)\n", config.minimum_width, tabletDimensions.browserWidth, tabletDimensions.browserHeight);
    }
    
    return true;
}

bool AppState::fileExists(std::string filename) {
    std::ifstream fileExistsHandle(filename);
    if (!fileExistsHandle.good()) {
        return false;
    }
    
    fileExistsHandle.close();
    return true;
}

void AppState::determineAircraftVariant() {
    if (Path::getInstance()->aircraftDirectory.empty()) {
        return;
    }
    
    std::string levelupTextFile = Path::getInstance()->aircraftDirectory + "/LU & Zibo Version.txt";
    if (std::filesystem::exists(levelupTextFile)) {
        aircraftVariant = VariantLevelUp737;
        return;
    }
    
    std::string ziboFolder = Path::getInstance()->aircraftDirectory + "/plugins/zibomod";
    if (std::filesystem::exists(ziboFolder) && std::filesystem::is_directory(ziboFolder)) {
        aircraftVariant = VariantZibo738;
        return;
    }
    
    if (Path::getInstance()->aircraftFilename.starts_with("B742_PW_Felis")) {
        aircraftVariant = VariantFelis742;
        return;
    }
    
    std::string justFlightFolder = Path::getInstance()->aircraftDirectory + "/plugins/SASL3_Thranda12";
    if (std::filesystem::exists(justFlightFolder) && std::filesystem::is_directory(justFlightFolder)) {
        aircraftVariant = VariantJustFlight;
        return;
    }
    
    std::string ixegTextFile = Path::getInstance()->aircraftDirectory + "/IXEG_user_prefs.txt";
    if (std::filesystem::exists(ixegTextFile)) {
        aircraftVariant = VariantIXEG737;
        return;
    }
    
    aircraftVariant = VariantUnknown;
}
