#include "plugins_menu.h"
#include "config.h"
#include <cstdint>

PluginsMenu *PluginsMenu::instance = nullptr;

PluginsMenu::PluginsMenu() {
}

PluginsMenu::~PluginsMenu() {
    instance = nullptr;
}

PluginsMenu *PluginsMenu::getInstance() {
    if (instance == nullptr) {
        instance = new PluginsMenu();
    }

    return instance;
}

void PluginsMenu::ensureMenuExists() {
    if (mainMenuId == nullptr) {
        mainMenuItemIndex = XPLMAppendMenuItem(XPLMFindPluginsMenu(), FRIENDLY_NAME, nullptr, 1);
        mainMenuId = XPLMCreateMenu(FRIENDLY_NAME, XPLMFindPluginsMenu(), mainMenuItemIndex, handleMenuAction, this);
    }
}

int PluginsMenu::addItem(const std::string &name, const std::function<void(int)> &callback, bool checked) {
    ensureMenuExists();

    int itemId = nextItemId++;
    int itemIndex = XPLMAppendMenuItem(mainMenuId, name.c_str(), (void *) (intptr_t) itemId, 0);
    menuCallbacks[itemId] = std::make_pair(itemIndex, callback);

    if (checked) {
        XPLMCheckMenuItem(mainMenuId, itemIndex, xplm_Menu_Checked);
    }

    return itemId;
}

void PluginsMenu::addSeparator() {
    ensureMenuExists();
    XPLMAppendMenuSeparator(mainMenuId);
}

void PluginsMenu::setItemName(int itemId, const std::string &name) {
    auto it = menuCallbacks.find(itemId);
    if (it == menuCallbacks.end()) {
        return;
    }

    XPLMSetMenuItemName(mainMenuId, it->second.first, name.c_str(), 0);
}

void PluginsMenu::setItemChecked(int itemId, bool checked) {
    auto it = menuCallbacks.find(itemId);
    if (it == menuCallbacks.end()) {
        return;
    }

    XPLMCheckMenuItem(mainMenuId, it->second.first, checked ? xplm_Menu_Checked : xplm_Menu_Unchecked);
}

bool PluginsMenu::isItemChecked(int itemId) {
    auto it = menuCallbacks.find(itemId);
    if (it == menuCallbacks.end()) {
        return false;
    }

    XPLMMenuCheck currentState;
    XPLMCheckMenuItemState(mainMenuId, it->second.first, &currentState);
    return currentState == xplm_Menu_Checked;
}

void PluginsMenu::teardown() {
    if (mainMenuId != nullptr) {
        XPLMDestroyMenu(mainMenuId);
        if (mainMenuItemIndex >= 0) {
            XPLMRemoveMenuItem(XPLMFindPluginsMenu(), mainMenuItemIndex);
            mainMenuItemIndex = -1;
        }
        mainMenuId = nullptr;
    }

    menuCallbacks.clear();
    nextItemId = 0;
}

void PluginsMenu::handleMenuAction(void *mRef, void *iRef) {
    if (mRef == nullptr) {
        return;
    }

    auto *self = static_cast<PluginsMenu *>(mRef);
    int itemId = (int) (intptr_t) iRef;

    auto it = self->menuCallbacks.find(itemId);
    if (it != self->menuCallbacks.end() && it->second.second) {
        // Invoke a copy: the callback may tear the menu down and erase this entry.
        auto callback = it->second.second;
        callback(itemId);
    }
}
