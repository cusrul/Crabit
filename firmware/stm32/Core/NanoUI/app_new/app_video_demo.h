#ifndef APP_VIDEO_DEMO_H
#define APP_VIDEO_DEMO_H

#include <NanoUI/NanoUI.h>
#include <NanoUI/nanoui_assets.h> // 引入新导出的资产

class VideoDemoApp : public Application {
public:
    const char* getAppName() override { return "VIDEO DEMO"; }

    Widget* buildUI() override {
        return new PageTransition(
            new Container(
                new Column({
                    new Row({
                        new Spacer(),
                        new Text("MEDIA PLAYBACK", 12),
                        new Spacer()
                    }),
                    
                    new Spacer(), // 动态分配空间

                    // 播放导出的 my_media 资产
                    new Row({
                        new Spacer(),
                        new AnimatedImage(asset_my_media, true),
                        new Spacer()
                    }),

                    new Spacer(), // 动态分配空间

                    new Row({
                        new Spacer(),
                        new Button(new Text("[ BACK ]", 12), [](){ AppManager::exitCurrentApp(); }),
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