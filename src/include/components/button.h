#ifndef BUTTON_H
#define BUTTON_H

#include <string>
#include <functional>
#include "image.h"

enum ButtonState {
    kButtonHover = 1,
    kButtonClick = 2
};

typedef std::function<bool()> ButtonClickHandlerFunc;

class Button : public Image {
private:
    ButtonClickHandlerFunc callback;
    
public:
#if DEBUG
    bool debugEnabled = false;
    void draw() override;
#endif
    Button(float relativeWidth, float relativeHeight);
    Button(std::string filename);
    void destroy();
    bool handleState(float normalizedX, float normalizedY, ButtonState state);
    void setClickHandler(ButtonClickHandlerFunc callback);
};

#endif
