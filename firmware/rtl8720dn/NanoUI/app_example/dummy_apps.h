#ifndef DUMMY_APPS_H
#define DUMMY_APPS_H

#include "../src/NanoUI.h"
#include <string>
#include <vector>

class HackerApp : public Application {
public:
    const char* getAppName() override { return "HACKER"; }
    Widget* buildUI() override {
        std::vector<Widget*> content;
        content.push_back(new Text("TERMINAL v1.0", 12));
        content.push_back(new SizedBox(0, 10));
        content.push_back(new Text("> Root access granted.", 12));
        content.push_back(new Text("> Scanning network...", 12));
        content.push_back(new Spacer());
        
        std::vector<Widget*> row_items;
        row_items.push_back(new Spacer());
        row_items.push_back(new Button(new Text("[ EXIT ]", 12), [](){ AppManager::exitCurrentApp(); }));
        row_items.push_back(new Spacer());
        content.push_back(new Row(row_items));

        return new Container(new Column(content), 128, 64, 0, false, 0);
    }
};

class SettingsApp : public Application {
public:
    const char* getAppName() override { return "SETTINGS"; }
    Widget* buildUI() override {
        std::vector<Widget*> items;
        
        std::vector<Widget*> row1;
        row1.push_back(new Text("BRIGHTNESS", 12));
        row1.push_back(new Spacer());
        row1.push_back(new Text("80%", 12));
        items.push_back(new Row(row1));
        
        items.push_back(new SizedBox(0, 10));

        std::vector<Widget*> row2;
        row2.push_back(new Text("WIFI", 12));
        row2.push_back(new Spacer());
        row2.push_back(new Text("CONNECTED", 12));
        items.push_back(new Row(row2));

        items.push_back(new Spacer());
        
        std::vector<Widget*> back_row;
        back_row.push_back(new Spacer());
        back_row.push_back(new Button(new Text("[ CLOSE ]", 12), [](){ AppManager::exitCurrentApp(); }));
        back_row.push_back(new Spacer());
        items.push_back(new Row(back_row));

        return new Container(new Column(items), 120, 56, 4, true, 2);
    }
};

#endif
