#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <vector>
#include <string>
#include "button.h"

class Notification {
private:
    std::string title;
    std::vector<std::string> bodyLines;
    Button *dismissButton;
    float x;
    float y;
    float width;
    float height;
    float animateIn;
    
    static constexpr float horizontalTextPadding = 24.0f;
    static constexpr float topPadding = 32.0f;
    static constexpr float titleBodyPadding = 24.0f;
    static constexpr float bodyLineHeight = 12.0f;
    static constexpr float buttonPadding = 8.0f;
    
public:
    Notification(std::string title, std::string body);
    void destroy();
    
    void update();
    void draw();
};

#endif
