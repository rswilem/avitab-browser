#ifndef PLUGINS_MENU_H
#define PLUGINS_MENU_H

#include <functional>
#include <map>
#include <string>
#include <XPLMMenus.h>

// Owns the plugin's entry in X-Plane's Plugins menu. Items are addressed by
// the id returned from addItem, so callbacks can update their own label or
// checkmark.
class PluginsMenu {
    private:
        PluginsMenu();
        ~PluginsMenu();
        static PluginsMenu *instance;

        XPLMMenuID mainMenuId = nullptr;
        int mainMenuItemIndex = -1;
        int nextItemId = 0;
        std::map<int, std::pair<int, std::function<void(int)>>> menuCallbacks; // itemId -> (itemIndex, callback)

        void ensureMenuExists();
        static void handleMenuAction(void *mRef, void *iRef);

    public:
        static PluginsMenu *getInstance();
        int addItem(const std::string &name, const std::function<void(int)> &callback, bool checked = false);
        void addSeparator();
        void setItemName(int itemId, const std::string &name);
        void setItemChecked(int itemId, bool checked);
        bool isItemChecked(int itemId);
        void teardown();
};

#endif
