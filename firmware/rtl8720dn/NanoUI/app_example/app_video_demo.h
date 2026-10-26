#ifndef APP_VIDEO_DEMO_H
#define APP_VIDEO_DEMO_H

#include "../src/NanoUI.h"
#include "../src/nanoui_assets.h" 

class VideoDemoApp : public Application {
public:
    const char* getAppName() override { return "VIDEO DEMO"; }

    Widget* buildUI() override {
        std::vector<Widget*> content;
        
        std::vector<Widget*> row1_items;
        row1_items.push_back(new Spacer());
        row1_items.push_back(new Text("MEDIA PLAYBACK", 12));
        row1_items.push_back(new Spacer());
        content.push_back(new Row(row1_items));
        
        content.push_back(new Spacer());

        std::vector<Widget*> row2_items;
        row2_items.push_back(new Spacer());
        row2_items.push_back(new AnimatedImage(asset_my_media, true));
        row2_items.push_back(new Spacer());
        content.push_back(new Row(row2_items));

        content.push_back(new Spacer());

        std::vector<Widget*> row3_items;
        row3_items.push_back(new Spacer());
        row3_items.push_back(new Button(new Text("[ BACK ]", 12), [](){ AppManager::exitCurrentApp(); }));
        row3_items.push_back(new Spacer());
        content.push_back(new Row(row3_items));

        return new PageTransition(
            new Container(new Column(content), 128, 64, 0, false, 0),
            TransitionType::SlideFromRight
        );
    }
};

#endif
