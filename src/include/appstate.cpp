#include "appstate.h"
#include <XPLMUtilities.h>
#include <XPLMProcessing.h>
#include <fstream>
#include "INIReader.h"
#include "path.h"
#include "config.h"
#include "dataref.h"
#include "json.hpp"
#include <iostream>
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
    hasPower = false;
    browserVisible = false;
    statusbar = nullptr;
    browser = nullptr;
    activeCursor = CursorDefault;
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
    else {
        mainMenuButton = new Button(Path::getInstance()->pluginDirectory + "/assets/menu-item.png");
        mainMenuButton->setPosition(0.2f, 0.568f);
    }
    
    mainMenuButton->opacity = 0;
    mainMenuButton->setClickHandler([](){
        AppState::getInstance()->showBrowser();
    });
    
    pluginInitialized = true;
    return true;
}

void AppState::deinitialize() {
    if (!pluginInitialized) {
        return;
    }
    
    buttons.clear();
    notification = nullptr;
    browser->visibilityWillChange(false);
    browserVisible = false;
    browser->destroy();
    browser = nullptr;
    statusbar->destroy();
    statusbar = nullptr;
    pluginInitialized = false;
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
    if (aircraftVariant == VariantZibo738) {
        hasPower = Dataref::getInstance()->getCached<int>("laminar/B738/tab/power") == 1 && Dataref::getInstance()->getCached<int>("laminar/B738/tab/boot_active") == 0;
        canBrowserVisible = hasPower && Dataref::getInstance()->getCached<int>("laminar/B738/tab/menu_page") == 8;

        if (Dataref::getInstance()->getCached<int>("laminar/B738/tab/menu_page") == 11 && !Dataref::getInstance()->getCached<int>("avitab/panel_enabled")) {
            mainMenuButton->opacity = Dataref::getInstance()->getCached<int>("laminar/B738/tab/efb_night_mode") ? 0.4 : 0.9;
        }
        else {
            mainMenuButton->opacity = 0;
        }
        
    }
    else {
        if (AppState::getInstance()->hasAvitab) {
            hasPower = Dataref::getInstance()->getCached<int>("avitab/panel_powered");
            canBrowserVisible = hasPower && Dataref::getInstance()->getCached<int>("avitab/is_in_menu") == 0;
        } else {
            // always allow browers in standalone mode
            hasPower = true;
            canBrowserVisible = true;
        }
        
        mainMenuButton->opacity = AppState::getInstance()->hasPower && Dataref::getInstance()->getCached<int>("avitab/is_in_menu") ? Dataref::getInstance()->getCached<float>("avitab/brightness") : 0.0f;
    }
    
    if (browserVisible && !canBrowserVisible) {
        browser->visibilityWillChange(false);
        browserVisible = false;
    }
    else if (shouldBrowserVisible && canBrowserVisible) {
        browser->visibilityWillChange(true);
        browserVisible = true;
        shouldBrowserVisible = AppState::getInstance()->hasAvitab ? false : true;
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

void AppState::draw(XPLMWindowID windowId) {
    if (!pluginInitialized || !hasPower) {
        return;
    }
    
    // only draw mainMenuButton and statusBar in avitab mode
    // if we are given a windowId here, it means we are using standalone mode
    if (windowId == nullptr) {
        mainMenuButton->draw();
        statusbar->draw();
    }
    
    if (browserVisible || shouldBrowserVisible) {
        browser->draw(windowId);
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
    if (hasAvitab) {
        buttons.push_back(button);
    }
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
        if (aircraftVariant == VariantZibo738) {
            Dataref::getInstance()->executeCommand("laminar/B738/tab/home");
            Dataref::getInstance()->executeCommand("laminar/B738/tab/spec");
        }
        else {
            Dataref::getInstance()->executeCommand("AviTab/app_about");
        }
    }
    shouldBrowserVisible = true;
    
    if (!url.empty()) {
        browser->loadUrl(url);
    }
}

void AppState::showNotification(Notification *aNotification) {
    notification = aNotification;
}

void AppState::executeDelayed(DelayedTaskFunc func, float delay) {
    tasks.push_back({
        func,
        XPLMGetElapsedTime() + delay
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
    config.user_agent = reader.Get("browser", "user_agent", "");
    config.framerate = reader.GetInteger("browser", "framerate", 25);
    config.statusbarIcons.clear();
    for (int i = 1; i <= 5; ++i) {
        std::string icon = reader.Get("statusbar", "icon_" + std::to_string(i), "globe");
        std::string url = reader.Get("statusbar", "url_" + std::to_string(i), "");
        if (!url.empty()) {
            AppConfiguration::StatusBarIcon statusBarIcon = {icon, url};
            config.statusbarIcons.push_back(statusBarIcon);
        }
    }
    
    hasAvitab = loadAvitabConfig();
    
    if (!hasAvitab) {
        tabletDimensions.textureWidth = STANDALONE_BROWSER_WIDTH;
        tabletDimensions.textureHeight = STANDALONE_BROWSER_HEIGHT;
        tabletDimensions.browserWidth = STANDALONE_BROWSER_WIDTH;
        tabletDimensions.browserHeight = STANDALONE_BROWSER_HEIGHT;
    }
    
    if (isReloading) {
        debug("Config file has been reloaded.\n");
        statusbar->destroy();
        statusbar->initialize();
        
        if (browser && browserVisible) {
            std::string url = browser->currentUrl();
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
    
    nlohmann::json data = nlohmann::json::parse(fileHandle);
    fileHandle.close();
    
    if (!data.contains("panel")) {
        return false;
    }
    
    constexpr float aspectRatio = 0.6f; // Avitab 800x480
    tabletDimensions = {
        data["panel"]["left"],
        data["panel"]["bottom"],
        data["panel"]["width"],
        data["panel"]["height"],
        0,0,
        0,0
    };
    
#if AVITAB_USE_FIXED_ASPECT_RATIO
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
    
    std::string ziboFolder = Path::getInstance()->aircraftDirectory + "/plugins/zibomod";
    if (std::filesystem::exists(ziboFolder) && std::filesystem::is_directory(ziboFolder)) {
        aircraftVariant = VariantZibo738;
    }
    else {
        aircraftVariant = VariantUnknown;
    }
}
