#pragma once

#include <NanoUI.h>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace Crabit {

// UI-thread owner of one reusable configuration tree. PopupRoot borrows that
// tree and owns its RenderObjects. Keep this component alive until those render
// owners are destroyed. Popup::hide()/reset() only release the framework's
// borrowed reference; this component always owns the configuration.
class CompanionPairPrompt {
   public:
    using Command = bool (*)(uint16_t, uint32_t);

    CompanionPairPrompt(Command allow, Command deny) : allow_(allow), deny_(deny) {}
    CompanionPairPrompt(const CompanionPairPrompt&) = delete;
    CompanionPairPrompt& operator=(const CompanionPairPrompt&) = delete;
    CompanionPairPrompt(CompanionPairPrompt&&) = delete;
    CompanionPairPrompt& operator=(CompanionPairPrompt&&) = delete;

    ~CompanionPairPrompt() { dismiss(); }

    void dismiss() {
        if (ownsVisiblePopup()) {
            Popup::hide();
            hideGap_ = true;
        }
    }

    void invalidate(uint16_t currentSession, uint32_t currentPair) {
        if (!currentSession || !currentPair || currentSession != shownSession_ ||
            currentPair != shownPair_)
            dismiss();
    }

    // Call once per UI loop, with a render/layout opportunity between ticks.
    // A hide consumes a tick before changed label/command data can be published.
    void tick(uint16_t session, uint32_t pair) {
        if (NanoUI::detail::shutdownInProgress())
            return;
        invalidate(session, pair);
        if (hideGap_) {
            hideGap_ = false;
            return;
        }
        if (!session || !pair || Popup::isVisible ||
            (session == shownSession_ && pair == shownPair_))
            return;

        shownSession_ = session;
        shownPair_ = pair;
        char label[32];
        std::snprintf(label, sizeof(label), "PAIR PHONE? #%lu", static_cast<unsigned long>(pair));
        label_ = label;
        if (!content_) {
            content_.reset(new Container(
                new Column({new SlidingText(&label_, 8), new SizedBox(0, 4),
                            new Row({new Button(new Container(new Text("ALLOW", 8), 48, 16),
                                                [this]() { command(allow_); }),
                                     new SizedBox(4, 0),
                                     new Button(new Container(new Text("DENY", 8), 48, 16),
                                                [this]() { command(deny_); })})}),
                112, 42));
        }
        Popup::show(content_.get(), 116, 48);
    }

   private:
    Command allow_, deny_;
    uint16_t shownSession_ = 0;
    uint32_t shownPair_ = 0;
    bool hideGap_ = false;
    NanoUI::NanoUIText label_ = "PAIR PHONE?";
    std::unique_ptr<Widget> content_;

    bool ownsVisiblePopup() const {
        return content_ && Popup::isVisible && Popup::pendingContent == content_.get();
    }

    void command(Command callback) {
        // Service callback is authoritative for current session, authentication
        // and sequence validation. Failed sends keep the same prompt available.
        if (callback && callback(shownSession_, shownPair_))
            dismiss();
    }
};

}  // namespace Crabit
