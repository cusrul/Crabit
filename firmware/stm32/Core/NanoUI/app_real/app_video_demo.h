#ifndef APP_VIDEO_DEMO_H
#define APP_VIDEO_DEMO_H

#include "NanoUI/NanoUI.h"
#include "NanoUI/nanoui_assets.h" 

class VideoDemoApp : public Application {
public:
    const char* getAppName() override { return "VIDEO DEMO"; }

    Widget* buildUI() override {
        return new PageTransition(
            new Container(
                new Column(NanoUI::EmbeddedVector<Widget*>{
                    new Row(NanoUI::EmbeddedVector<Widget*>{
                        new Spacer(),
                        new Text("MEDIA PLAYBACK", 8),
                        new Spacer()
                    }),
                    
                    new Spacer(), 

                    // 播放导出的 my_media 资产
                    new Row(NanoUI::EmbeddedVector<Widget*>{
                        new Spacer(),
                        new AnimatedImage(asset_my_media, true),
                        new Spacer()
                    }),

                    new Spacer(), 

                    new Row(NanoUI::EmbeddedVector<Widget*>{
                        new Spacer(),
                        new Button(new Text("[ BACK ]", 8), [](){ AppManager::exitCurrentApp(); }),
                        new Spacer()
                    })
                }),
                128, 64, 0, false, 0
            ),
            TransitionType::SlideFromRight
        );
    }
};

#endif
