#ifndef BROWSER_H
#define BROWSER_H

#include "browser_handler.h"
#include "button.h"

#include <include/cef_app.h>
#include <XPLMDefs.h>
#include <XPLMDisplay.h>

class Browser {
    private:
        int textureId;
        float offsetStart;
        float offsetEnd;
        float lastGpsUpdateTime;
        Button *backButton;
        CefRefPtr<BrowserHandler> handler;
        bool leftMouseButtonDown;
        bool createBrowser();
        void updateGPSLocation();
        CefMouseEvent getMouseEvent(float normalizedX, float normalizedY);

    public:
        Browser();

        std::string currentUrl;

        void initialize();
        void destroy();
        void resetHandler();
        void visibilityWillChange(bool becomesVisible);
        void update();
        void draw();
        void loadUrl(std::string url);
        bool hasInputFocus();
        void setFocus(bool focus);
        void mouseMove(float normalizedX, float normalizedY);
        bool click(XPLMMouseStatus status, float normalizedX, float normalizedY);
        void scroll(float normalizedX, float normalizedY, int clicks, bool horizontal);
        void key(unsigned char key, unsigned char virtualKey, XPLMKeyFlags flags = 0);
        bool goBack();
        CursorType cursor();
};

#endif
