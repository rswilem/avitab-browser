#include "statusbar.h"
#include "config.h"
#include "drawing.h"
#include "path.h"
#include "appstate.h"
#include <cmath>
#include <XPLMUtilities.h>
#include <XPLMGraphics.h>
#include <XPLMProcessing.h>

Statusbar::Statusbar() {
    x = 0.0f;
    statusbarY = 0.967f;
    loading = false;
    activeTabTitle = "";
    activeTabButton = nullptr;
    homeButton = nullptr;
    spinnerImage = nullptr;
}

void Statusbar::initialize() {
    spinnerImage = new Image(Path::getInstance()->pluginDirectory + "/assets/spinner.png");
    
    float spinnerX;
    if (AppState::getInstance()->aircraftVariant == VariantZibo738) {
        spinnerX = 0.875f;
        x = 0.79f;
        statusbarY = 1.0f;
    }
    else if (AppState::getInstance()->aircraftVariant == VariantFelis742) {
        spinnerX = 0.88f;
        x = 0.7f;
        statusbarY = 1.092f;
    }
    else if (AppState::getInstance()->aircraftVariant == VariantLevelUp737) {
        spinnerX = 0.88f;
        x = 0.78f;
        statusbarY = 1.025f;
    }
    else if (AppState::getInstance()->aircraftVariant == VariantAirfoillabsC172) {
        spinnerX = 0.54f;
        x = 0.9f;
        statusbarY = 1.03f;
    }
    else {
        spinnerX = 0.54f;
        x = 0.9f;
        statusbarY = 0.967f;
    }

#if DEBUG
    // Live-tune the shared header Y (loading spinner, back button, status bar
    // icons and active tab name) via config.ini [debug] debug_value_3. Leave at
    // 0 to keep the per-aircraft position. Applied on "Reload configuration"
    // (statusbar is re-initialized).
    if (AppState::getInstance()->config.debug_value_3 != 0.0f) {
        statusbarY = AppState::getInstance()->config.debug_value_3;
    }
#endif

    // The spinner shares the status bar Y so it tracks the same header row.
    spinnerImage->setPosition(spinnerX, statusbarY);

    for (const auto& icon : AppState::getInstance()->config.statusbarIcons) {
        Button *button = new Button(Path::getInstance()->pluginDirectory + "/assets/icons/" + icon.icon + ".svg");

        button->setPosition(x, statusbarY);
        button->setClickHandler([&icon]() { AppState::getInstance()->showBrowser(icon.url); return true; });
        statusbarButtons.push_back(button);
        x -= button->relativeWidth + 0.005f;
    }
    
    if (AppState::getInstance()->aircraftVariant == VariantFelis742) {
        x = 0.3f;
    }
    
    if (AppState::getInstance()->aircraftVariant == VariantIXEG737) {
        homeButton = new Button(Path::getInstance()->pluginDirectory + "/assets/icons/home.svg");
        homeButton->setPosition(0.5f, 0.967f);
        homeButton->setClickHandler([]() {
            AppState::getInstance()->hideBrowser();
            return true;
        });
    }
}

void Statusbar::destroy() {
    if (spinnerImage) {
        spinnerImage->destroy();
        delete spinnerImage;
        spinnerImage = nullptr;
    }

    for (const auto& button : statusbarButtons) {
        button->destroy();
        delete button;
    }
    statusbarButtons.clear();

    if (homeButton) {
        homeButton->destroy();
        delete homeButton;
        homeButton = nullptr;
    }

    if (activeTabButton) {
        activeTabButton->destroy();
        delete activeTabButton;
        activeTabButton = nullptr;
    }

    activeTabTitle = "";
}

void Statusbar::update() {
}

void Statusbar::draw() {
    if (!spinnerImage) {
        return;
    }
    
    set_brightness(AppState::getInstance()->brightness * 0.2f);
    if (loading && AppState::getInstance()->browserVisible) {
        spinnerImage->draw(fmod(XPLMGetElapsedTime() * 360, 360));
    }
    
    for (const auto& button : statusbarButtons) {
        button->draw();
    }
    
    XPLMSetGraphicsState(
                         0, // No fog, equivalent to glDisable(GL_FOG);
                         0, // No texture, equivalent to glDisable(GL_TEXTURE_2D);
                         0, // No lighting, equivalent to glDisable(GL_LIGHT0);
                         0, // No alpha testing, e.g glDisable(GL_ALPHA_TEST);
                         1, // Use alpha blending, e.g. glEnable(GL_BLEND);
                         0, // No depth read, e.g. glDisable(GL_DEPTH_TEST);
                         0 // No depth write, e.g. glDepthMask(GL_FALSE);
    );
    
    if (!activeTabTitle.empty()) {
        float y = statusbarY;
        activeTabButton->setPosition(x - (activeTabButton->relativeWidth / 2.0f) - 0.005f, y);
        
        set_brightness(AppState::getInstance()->brightness * 0.2f);
        Drawing::DrawRoundedRect(x - activeTabButton->relativeWidth - 0.005f, y - 0.015f, x - 0.005f, y + 0.015f, 4.0f);
        Drawing::DrawText(activeTabTitle, x - 0.005f - activeTabButton->relativeWidth / 2.0f, y, 1.0f, {AppState::getInstance()->brightness + 0.1f, AppState::getInstance()->brightness + 0.1f, AppState::getInstance()->brightness + 0.1f});
    }
    
    if (homeButton) {
        homeButton->draw();
    }
}

void Statusbar::setActiveTab(std::string title) {
    if (title.length() > 12) {
        activeTabTitle = title.substr(0, 12) + "...";
    } else {
        activeTabTitle = title;
    }
    
    if (activeTabButton) {
        activeTabButton->destroy();
        delete activeTabButton;
        activeTabButton = nullptr;
    }

    if (!activeTabTitle.empty()) {
        float textWidth = Drawing::TextWidth(activeTabTitle) + 0.02f;
        activeTabButton = new Button(textWidth, 0.03f);
        activeTabButton->setClickHandler([](){
            AppState::getInstance()->showBrowser();
            return true;
        });
    }
}

