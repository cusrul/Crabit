#ifndef CRABIT_COMPANION_TRACE_H
#define CRABIT_COMPANION_TRACE_H
#include <stdint.h>

// Diagnostic observations only: no power control, UART commands or pairing data.
namespace Crabit { namespace CompanionTrace {
enum Event : uint16_t {
    Start, LegacyStop, BootBegin, BootEnd, RxPrepared, StartQueued,
    TxBegin, TxEnd, RxCounts, FrameCounts, Phase, PowerOff,
    QueueCounts, MessageOther, Queued, Ready, Connected, Authenticated, Disconnected,
    Failed, Busy, Rejected, Stopped, PairWait, PairTimeout,
    SetupBegin, HalReady, ServicesReady, AppManagerReady, UartReady, UartFailed,
    Waiting, CommandReceived, StartOwner, PrefsBegin, PrefsDone,
    BleInitBegin, BleInitDone, PeripheralBegin, PeripheralDone, BwTraceDropped,
    BwRxCounts, BwBadFrames, ProbeOnly, ProbePong, ProbePongAccepted,
    RxEvent, RxDma, RxBytes, RxClasses, UartError, RxEmptyIdle, ManualBleOnly,
    BwCommandQueued, BwAttachOwner, BwAttachRamReady, BwAttachReplyBegin,
    BwAttachReplySent, BwAttachReplyFail, Attached, BwBleAlive,
    BwCommandRejected, BwAttachRejected, BwAttachRamBeginDone,
    BwAttachReplyQueued, BwAttachReplyRejected,
    UartProbeQueued, UartProbePass, UartProbeFailed, UartProbeTimeout, PairCancelled,
    TimeRx, TimeQueued, TimeHandled, TimeRejected, TimeApplied, TimeReadback, TimeResult,
    UiEntry, UiStage, UiHeartbeat, ModeQueued, ModeTxBegin, ModeTxEnd,
    BleStopRequested, BleOffComplete, BleResumeQueued,
    BwUiInitBegin, BwUiOledReady, BwUiLauncherReady
};
void record(Event event, uint16_t session = 0, uint32_t a = 0, uint32_t b = 0);
void message(const char* topic, const char* value, uint16_t session);
void poll(); // UI task, outside the I2C lock; one non-blocking USB attempt.
} }
#endif
