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
    loading = false;
    activeTabTitle = "";
    activeTabButton = nullptr;
    homeButton = nullptr;
    spinnerImage = nullptr;
}

void Statusbar::initialize() {
    spinnerImage = new Image(Path::getInstance()->pluginDirectory + "/assets/spinner.png");
    
    if (AppState::getInstance()->aircraftVariant == VariantZibo738) {
        spinnerImage->setPosition(0.875f, 1.0f);
        x = 0.79f;
    }
    else if (AppState::getInstance()->aircraftVariant == VariantFelis742) {
        spinnerImage->setPosition(0.88f, 1.092f);
        x = 0.7f;
    }
    else if (AppState::getInstance()->aircraftVariant == VariantLevelUp737) {
        spinnerImage->setPosition(0.88f, 1.025f);
        x = 0.78f;
    }
    else {
        spinnerImage->setPosition(0.54f, 0.967f);
        x = 0.9f;
    }
    
    for (const auto& icon : AppState::getInstance()->config.statusbarIcons) {
        Button *button = new Button(Path::getInstance()->pluginDirectory + "/assets/icons/" + icon.icon + ".svg");
        
        if (AppState::getInstance()->aircraftVariant == VariantZibo738) {
            button->setPosition(x, 1.0f);
        }
        else if (AppState::getInstance()->aircraftVariant == VariantFelis742) {
            button->setPosition(x, 1.092f);
        }
        else if (AppState::getInstance()->aircraftVariant == VariantLevelUp737) {
            button->setPosition(x, 1.025f);
        }
        else {
            button->setPosition(x, 0.967f);
        }
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
        spinnerImage = nullptr;
    }
    
    for (const auto& button : statusbarButtons) {
        button->destroy();
    }
    statusbarButtons.clear();
    
    if (homeButton) {
        homeButton->destroy();
    }
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
        float y = 0.967f;
        if (AppState::getInstance()->aircraftVariant == VariantZibo738) {
            y = 1.0f;
        }
        else if (AppState::getInstance()->aircraftVariant == VariantFelis742) {
            y = 1.092f;
        }
        else if (AppState::getInstance()->aircraftVariant == VariantLevelUp737) {
            y = 1.025f;
        }
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
    }
    
    if (!activeTabTitle.empty()) {
        float textWidth = Drawing::TextWidth(activeTabTitle) + 0.02f;
        activeTabButton = new Button(textWidth, 0.03f);
        activeTabButton->setClickHandler([](){
            AppState::getInstance()->showBrowser();
            return true;
        });
    }
    else {
        activeTabButton = nullptr;
    }
    
}

