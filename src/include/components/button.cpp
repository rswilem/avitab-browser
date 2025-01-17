#include "button.h"
#include "appstate.h"
#include "config.h"
#include <XPLMGraphics.h>
#include <XPLMUtilities.h>
#include <XPLMDisplay.h>

Button::Button(float aWidth, float aHeight) : Image("") {
    callback = nullptr;
    relativeWidth = aWidth;
    relativeHeight = aHeight;
    pixelsWidth = AppState::getInstance()->tabletDimensions.width * aWidth;
    pixelsHeight = AppState::getInstance()->tabletDimensions.height * aHeight;
    AppState::getInstance()->registerButton(this);
}

Button::Button(std::string filename) : Image(filename) {
    AppState::getInstance()->registerButton(this);
}

void Button::destroy() {
    AppState::getInstance()->unregisterButton(this);
    Image::destroy();
}

bool Button::handleState(float normalizedX, float normalizedY, ButtonState state) {
    if (opacity < __FLT_EPSILON__) {
        return false;
    }
    
    float mouseX = AppState::getInstance()->tabletDimensions.width * normalizedX;
    float halfWidth = pixelsWidth / 2.0f;
    
    if (mouseX >= (x - halfWidth) && mouseX <= (x + halfWidth)) {
        float mouseY = AppState::getInstance()->tabletDimensions.height * normalizedY;
        float halfHeight = pixelsHeight / 2.0f;
        if (mouseY >= (y - halfHeight) && mouseY <= (y + halfHeight)) {
            if (state == kButtonClick) {
                if (callback) {
                    return callback();
                }
            }
            
            return true;
        }
    }
    
    return false;
}

void Button::setClickHandler(ButtonClickHandlerFunc cb) {
    callback = cb;
}

#if DEBUG
void Button::draw() {
    if (!debugEnabled) {
        Image::draw();
        return;
    }
    
    XPLMSetGraphicsState(
                         0, // No fog, equivalent to glDisable(GL_FOG);
                         0, // One texture, equivalent to glEnable(GL_TEXTURE_2D);
                         0, // No lighting, equivalent to glDisable(GL_LIGHT0);
                         0, // No alpha testing, e.g glDisable(GL_ALPHA_TEST);
                         1, // Use alpha blending, e.g. glEnable(GL_BLEND);
                         0, // No depth read, e.g. glDisable(GL_DEPTH_TEST);
                         0 // No depth write, e.g. glDepthMask(GL_FALSE);
    );
    
    unsigned short x1 = AppState::getInstance()->tabletDimensions.x + x;
    unsigned short y1 = AppState::getInstance()->tabletDimensions.y + y;
    
    x1 -= pixelsWidth / 2.0f;
    y1 -= pixelsHeight / 2.0f;
    
    glBegin(GL_QUADS);
    glColor4f(1, 0, 0, 0.5f);
    
    glTexCoord2f(0, 1);
    glVertex2f(x1, y1);
    
    glTexCoord2f(0, 0);
    glVertex2f(x1, y1 + pixelsHeight);
    
    glTexCoord2f(1, 0);
    glVertex2f(x1 + pixelsWidth, y1 + pixelsHeight);
    
    glTexCoord2f(1, 1);
    glVertex2f(x1 + pixelsWidth, y1);
    
    glEnd();
}
#endif
