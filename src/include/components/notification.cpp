#include "notification.h"
#include "drawing.h"
#include <XPLMGraphics.h>
#include <XPLMUtilities.h>
#ifdef XPLM410
#include <XPLMSound.h>
#endif
#include <fstream>
#include "config.h"
#include "path.h"
#include "appstate.h"

Notification::Notification(std::string aTitle, std::string body) {
    dismissButton = nullptr;
    x = 0.0f;
    y = 0.0f;
    width = 0.0f;
    height = 0.0f;
    opacity = 1.0f;
    
    title = aTitle;
    dismissButton = new Button(0.3f, 0.05f);
    dismissButton->setClickHandler([]() {
        AppState::getInstance()->showNotification(nullptr);
        return true;
    });
    
    x = 0.5f;
    y = 0.55f;
    
    glColor3f(1.0f, 1.0f, 1.0f);
    
    width = 0.3f;
    x -= width / 2.0f;
    bodyLines = Drawing::WrapWordsToLines(xplmFont_Proportional, body, width - ((horizontalTextPadding * 2.0f) / AppState::getInstance()->tabletDimensions.height));
    height = (topPadding + titleBodyPadding + (bodyLines.size() * bodyLineHeight) + buttonPadding + dismissButton->pixelsHeight + buttonPadding) / AppState::getInstance()->tabletDimensions.height;
    opacity = 0.4f;
    
#ifdef XPLM410
    std::ifstream file(Path::getInstance()->pluginDirectory + "/assets/notify.pcm", std::ios::binary | std::ios::ate);
    if (file) {
        file.seekg(0, std::ios::beg);
        std::vector<char> buffer((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
        file.close();

        FMOD_CHANNEL *sound = XPLMPlayPCMOnBus(buffer.data(), (unsigned int)buffer.size(), FMOD_SOUND_FORMAT_PCM16, 22050, 1, 0, xplm_AudioInterior, nullptr, nullptr);
        if (sound) {
            XPLMSetAudioVolume(sound, 0.3f);
        }
    }
#endif
}

void Notification::destroy() {
    if (dismissButton) {
        dismissButton->destroy();
    }
}

void Notification::update() {
    if (opacity < 1.0f) {
        opacity += 0.2f;
    }
}

void Notification::draw() {
    XPLMSetGraphicsState(
                         0, // No fog, equivalent to glDisable(GL_FOG);
                         0, // One texture, equivalent to glEnable(GL_TEXTURE_2D);
                         0, // No lighting, equivalent to glDisable(GL_LIGHT0);
                         0, // No alpha testing, e.g glDisable(GL_ALPHA_TEST);
                         1, // Use alpha blending, e.g. glEnable(GL_BLEND);
                         0, // No depth read, e.g. glDisable(GL_DEPTH_TEST);
                         0 // No depth write, e.g. glDepthMask(GL_FALSE);
    );
    
    glColor4f(0, 0, 0, opacity * 0.6f);
    Drawing::DrawRect(0.0f, 0.0f, 1.0f, 1.0f);
    
    glColor4f(1.0f, 1.0f, 1.0f, opacity);
    Drawing::DrawRoundedRect(x, y - (height / 2.0f), x + width, y + (height / 2.0f), 16.0f);
    
    float yOffset = y + (height / 2.0f) - (topPadding  / AppState::getInstance()->tabletDimensions.height);
    Drawing::DrawText(title, x + (width / 2.0f), yOffset, 1.4f);
    
    yOffset -= titleBodyPadding / AppState::getInstance()->tabletDimensions.height;
    for (int i = 0; i < bodyLines.size(); ++i) {
        Drawing::DrawText(bodyLines[i], x + (width / 2.0f), yOffset);
        yOffset -= bodyLineHeight / AppState::getInstance()->tabletDimensions.height;
    }
    
    glColor4f(0.2f, 0.2f, 0.2f, opacity);
    Drawing::DrawLine(x, yOffset, x + width, yOffset, 1.0f);
    yOffset -= (buttonPadding / AppState::getInstance()->tabletDimensions.height) * 3.0f;
    
    dismissButton->setPosition(0.5f, yOffset);
    Drawing::DrawText("OK", x + (width / 2.0f), yOffset, 1.4f, { 0.4f, 0.4f, 1.0f });
}
