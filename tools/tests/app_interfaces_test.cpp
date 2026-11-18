#include "../../firmware/shared/command_router.h"
#include "../../firmware/shared/feature_registry.h"

#include <cassert>
#include <cstring>

namespace {

struct Recorder {
    int calls = 0;
    const char* last_command = nullptr;
    const char* last_argument = nullptr;
    uint16_t last_id = 0;
    bool result = true;
};

bool onCommand(void* ctx, const char* command, const char* argument, uint16_t request_id) {
    Recorder* recorder = static_cast<Recorder*>(ctx);
    ++recorder->calls;
    recorder->last_command = command;
    recorder->last_argument = argument;
    recorder->last_id = request_id;
    return recorder->result;
}

struct FeatureState {
    int init_calls = 0;
    int tick_calls = 0;
    int event_calls = 0;
    uint32_t last_now = 0;
};

bool featureInitOk() { return true; }
bool featureInitFail() { return false; }
void featureTick(uint32_t now) { (void)now; }
void featureEvent(const char* topic, const char* value) { (void)topic; (void)value; }

}  // namespace

int main() {
    using namespace Crabit;

    // Command registration, dispatch and unregistration.
    Recorder recorder;
    assert(registerCommand("app/demo", onCommand, &recorder));
    assert(commandCount() == 1);
    assert(!registerCommand("app/demo", onCommand, &recorder));  // duplicate
    assert(!registerCommand("", onCommand, &recorder));
    assert(!registerCommand(nullptr, onCommand, &recorder));
    assert(!registerCommand("app/nohandler", nullptr, &recorder));

    assert(dispatchCommand("app/demo", "payload", 7));
    assert(recorder.calls == 1);
    assert(std::strcmp(recorder.last_command, "app/demo") == 0);
    assert(std::strcmp(recorder.last_argument, "payload") == 0);
    assert(recorder.last_id == 7);

    // A handler may decline while still owning the topic.
    recorder.result = false;
    assert(!dispatchCommand("app/demo", "payload", 8));
    assert(recorder.calls == 2);

    // Unknown commands and wrong owners do not unregister.
    assert(!dispatchCommand("app/missing", "", 0));
    assert(!unregisterCommand("app/demo", nullptr));
    assert(unregisterCommand("app/demo", &recorder));
    assert(commandCount() == 0);
    assert(!dispatchCommand("app/demo", "", 0));

    // Table capacity: one past the limit must fail.
    static char names[Crabit::detail::kMaxCommands + 1][8];
    for (size_t i = 0; i <= Crabit::detail::kMaxCommands; ++i) {
        names[i][0] = 'c';
        names[i][1] = char('0' + i / 10);
        names[i][2] = char('0' + i % 10);
        names[i][3] = '\0';
    }
    for (size_t i = 0; i < Crabit::detail::kMaxCommands; ++i)
        assert(registerCommand(names[i], onCommand, &recorder));
    assert(!registerCommand(names[Crabit::detail::kMaxCommands], onCommand, &recorder));
    for (size_t i = 0; i < Crabit::detail::kMaxCommands; ++i)
        assert(unregisterCommand(names[i], &recorder));
    assert(commandCount() == 0);

    // A long command is rejected before it can overflow.
    static char long_name[64];
    std::memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    assert(!registerCommand(long_name, onCommand, &recorder));

    // Feature table: init/tick/event fan-out and duplicate/capacity checks.
    static FeatureState states[Crabit::detail::kMaxFeatures];
    Feature features[Crabit::detail::kMaxFeatures];
    for (size_t i = 0; i < Crabit::detail::kMaxFeatures; ++i) {
        features[i] = Feature{names[i], featureInitOk, featureTick, featureEvent};
    }
    for (size_t i = 0; i < Crabit::detail::kMaxFeatures; ++i) assert(registerFeature(features[i]));
    assert(!registerFeature(features[0]));
    assert(featureCount() == Crabit::detail::kMaxFeatures);
    assert(!registerFeature(Feature{"overflow", featureInitOk, nullptr, nullptr}));
    assert(initFeatures());
    tickFeatures(1234);
    dispatchFeatureEvent("app/demo", "value");
    dispatchFeatureEvent(nullptr, "value");

    // None of the feature functions above record, so this only proves the
    // calls are safe with real pointers. A failing init is reported.
    (void)states;
    Feature failing{"failing", featureInitFail, nullptr, nullptr};
    assert(!registerFeature(failing));  // table already full
    for (size_t i = 0; i < Crabit::detail::kMaxFeatures; ++i)
        assert(unregisterFeature(features[i].id));
    assert(featureCount() == 0);

    return 0;
}
