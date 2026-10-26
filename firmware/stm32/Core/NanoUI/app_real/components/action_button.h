#ifndef APP_REAL_COMPONENTS_ACTION_BUTTON_H
#define APP_REAL_COMPONENTS_ACTION_BUTTON_H

#include <NanoUI.h>

class ActionButton : public Button {
   public:
    ActionButton(const char* text, NanoUI::Callback<void()> callback)
        : Button(new Container(new Text(text, 8), 46, 16, 0, true, 2), callback) {}
};

#endif
