#include "companion_trace.h"
#include "main.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>

extern volatile uint8_t bw16_flash_mode;
namespace Crabit { namespace CompanionTrace {
namespace {
struct Entry { uint32_t at, a, b; uint16_t session, event; };
constexpr unsigned Capacity = 48;
Entry entries[Capacity];
unsigned head = 0, tail = 0, count = 0;
uint32_t dropped = 0;
char pending[144];
uint16_t pending_size = 0;
uint32_t pending_at = 0;
const char* const names[] = {
    "START", "LEGACY_STOP", "BOOT_BEGIN", "BOOT_END", "RX_PREPARED", "START_QUEUED",
    "TX_BEGIN", "TX_END", "RX_COUNTS", "FRAME_COUNTS", "PHASE", "POWER_OFF",
    "QUEUE_COUNTS", "MSG_OTHER", "QUEUED", "READY", "CONNECTED", "AUTHENTICATED", "DISCONNECTED",
    "FAILED", "BUSY", "REJECTED", "STOPPED", "PAIR_WAIT", "PAIR_TIMEOUT",
    "BW_SETUP", "BW_HAL", "BW_SERVICES", "BW_APPMGR", "BW_UART_READY", "BW_UART_FAILED",
    "BW_WAITING", "BW_CMD_RX", "BW_START_OWNER", "BW_PREFS_BEGIN", "BW_PREFS_DONE",
    "BW_BLE_INIT_BEGIN", "BW_BLE_INIT_DONE", "BW_PERIPH_BEGIN", "BW_PERIPH_DONE", "BW_TRACE_DROPPED",
    "BW_RX_COUNTS", "BW_BAD_FRAMES", "PROBE_ONLY", "PONG", "PONG_ACCEPTED",
    "RX_EVENT", "RX_DMA", "RX_BYTES", "RX_CLASSES", "UART_ERROR", "RX_EMPTY_IDLE", "MANUAL_BLE_ONLY",
    "BW_CMD_QUEUED", "BW_ATTACH_OWNER", "BW_ATTACH_RAM_READY", "BW_ATTACH_REPLY_BEGIN",
    "BW_ATTACH_REPLY_SENT", "BW_ATTACH_REPLY_FAIL", "ATTACHED", "BW_BLE_ALIVE",
    "BW_CMD_REJECTED", "BW_ATTACH_REJECTED", "BW_ATTACH_RAM_BEGIN_DONE",
    "BW_ATTACH_REPLY_QUEUED", "BW_ATTACH_REPLY_REJECTED",
    "UART_PROBE_QUEUED", "UART_PROBE_PASS", "UART_PROBE_FAILED", "UART_PROBE_TIMEOUT", "PAIR_CANCELLED",
    "TIME_RX", "TIME_QUEUED", "TIME_HANDLED", "TIME_REJECTED", "TIME_APPLIED", "TIME_READBACK", "TIME_RESULT",
    "UI_ENTRY", "UI_STAGE", "UI_HEARTBEAT", "MODE_QUEUED", "MODE_TX_BEGIN", "MODE_TX_END",
    "BLE_STOP_REQUESTED", "BLE_OFF_COMPLETE", "BLE_RESUME_QUEUED",
    "BW_UI_INIT_BEGIN", "BW_UI_OLED_READY", "BW_UI_LAUNCHER_READY"
};
static_assert(sizeof(names) / sizeof(names[0]) == BwUiLauncherReady + 1, "trace names");
struct NameEvent { const char* name; Event event; };
}
void record(Event event, uint16_t session, uint32_t a, uint32_t b) {
    if (bw16_flash_mode) return;
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (count < Capacity) {
        entries[head] = {HAL_GetTick(), a, b, session, uint16_t(event)};
        head = (head + 1) % Capacity; ++count;
    } else ++dropped;
    __set_PRIMASK(mask);
}
void message(const char* topic, const char* value, uint16_t session) {
    // Deliberately never log arbitrary payload text or tokens.
    if (strcmp(topic, "sys/notify") == 0 && strcmp(value, "BW16 PONG OK!") == 0) {
        record(ProbePong, session); return;
    }
    const bool boot = strcmp(topic, "boot") == 0;
    const bool radio = strcmp(topic, "diag/radio") == 0;
    const bool status = strcmp(topic, "sys/comm") == 0 || strcmp(topic, "sys/ble") == 0;
    if (!boot && !radio && !status) return;
    if (status) {
        static const NameEvent pairing[] = {
            {"PAIR_WAIT:", PairWait}, {"AUTHENTICATED:", Authenticated},
            {"PAIR_TIMEOUT:", PairTimeout}, {"PAIR_CANCELLED:", PairCancelled},
            {"PAIR_REJECTED:", Rejected}
        };
        for (const auto& item : pairing) {
            const size_t length = strlen(item.name);
            if (strncmp(value, item.name, length)) continue;
            uint32_t sequence = 0;
            const char* digit = value + length;
            bool valid = *digit != '\0';
            for (; *digit && valid; ++digit) {
                if (*digit < '0' || *digit > '9' ||
                        sequence > (UINT32_MAX - uint32_t(*digit - '0')) / 10) valid = false;
                else sequence = sequence * 10 + uint32_t(*digit - '0');
            }
            if (valid && sequence) { record(item.event, session, 3, sequence); return; }
        }
    }
    if (radio && strncmp(value, "UART_RX:", 8) == 0) {
        uint32_t counts[3] = {};
        const char* p = value + 8;
        for (unsigned i = 0; i < 3; ++i) {
            if (*p < '0' || *p > '9') return;
            while (*p >= '0' && *p <= '9') {
                const uint32_t digit = uint32_t(*p++ - '0');
                if (counts[i] > (UINT32_MAX - digit) / 10) return;
                counts[i] = counts[i] * 10 + digit;
            }
            if (i < 2) { if (*p++ != ',') return; }
            else if (*p) return;
        }
        record(BwRxCounts, session, counts[0], counts[1]);
        record(BwBadFrames, session, counts[2]); return;
    }
    static const NameEvent boot_names[] = {
        {"setup_begin", SetupBegin}, {"hal_ready", HalReady}, {"services_ready", ServicesReady},
        {"appmgr_ready", AppManagerReady}, {"uart_ready", UartReady},
        {"uart_failed", UartFailed}, {"waiting", Waiting},
        {"ui_init_begin", BwUiInitBegin}, {"ui_oled_ready", BwUiOledReady},
        {"ui_launcher_ready", BwUiLauncherReady}
    };
    static const NameEvent radio_names[] = {
        {"CMD_RX", CommandReceived}, {"START_OWNER", StartOwner},
        {"PREFS_BEGIN", PrefsBegin}, {"PREFS_DONE", PrefsDone},
        {"BLE_INIT_BEGIN", BleInitBegin}, {"BLE_INIT_DONE", BleInitDone},
        {"PERIPH_BEGIN", PeripheralBegin}, {"PERIPH_DONE", PeripheralDone}, {"TRACE_DROPPED", BwTraceDropped},
        {"CMD_QUEUED", BwCommandQueued}, {"ATTACH_OWNER", BwAttachOwner},
        {"ATTACH_RAM_READY", BwAttachRamReady}, {"ATTACH_REPLY_BEGIN", BwAttachReplyBegin},
        {"ATTACH_REPLY_SENT", BwAttachReplySent}, {"ATTACH_REPLY_FAIL", BwAttachReplyFail},
        {"CMD_REJECTED", BwCommandRejected}, {"ATTACH_REJECTED", BwAttachRejected},
        {"ATTACH_RAM_BEGIN_DONE", BwAttachRamBeginDone}, {"ATTACH_REPLY_QUEUED", BwAttachReplyQueued},
        {"ATTACH_REPLY_REJECTED", BwAttachReplyRejected}, {"BLE_ALIVE", BwBleAlive}
    };
    static const NameEvent status_names[] = {
        {"QUEUED", Queued}, {"READY", Ready}, {"ADVERTISING", Ready},
        {"CONNECTED", Connected}, {"AUTHENTICATED", Authenticated},
        {"DISCONNECTED", Disconnected}, {"FAILED", Failed}, {"TIMEOUT", Failed},
        {"BUSY", Busy}, {"REJECTED", Rejected}, {"STOPPED", Stopped},
        {"PAIR_TIMEOUT", PairTimeout}, {"PAIR_CANCELLED", PairTimeout}, {"ATTACHED", Attached}
    };
    const NameEvent* table = boot ? boot_names : radio ? radio_names : status_names;
    const unsigned length = boot ? sizeof(boot_names)/sizeof(boot_names[0]) :
        radio ? sizeof(radio_names)/sizeof(radio_names[0]) : sizeof(status_names)/sizeof(status_names[0]);
    for (unsigned i = 0; i < length; ++i) {
        const size_t n = strlen(table[i].name);
        if ((boot && strncmp(value, table[i].name, n) == 0 &&
                (value[n] == ' ' || value[n] == '\0')) ||
            (!boot && strcmp(value, table[i].name) == 0)) {
            record(table[i].event, session, boot ? 1 : radio ? 2 : 3, 0); return;
        }
    }
    record(strncmp(value, "PAIR_WAIT:", 10) == 0 ? PairWait : MessageOther, session);
}
void poll() {
    // The CDC adapter copies accepted bytes to its own buffer. Never wait for
    // host, change USB mode, or interfere with the flash bridge.
    if (bw16_flash_mode) {
        const uint32_t mask = __get_PRIMASK(); __disable_irq();
        head = tail = count = 0; dropped = 0; pending_size = 0;
        __set_PRIMASK(mask); return;
    }
    if (!pending_size) {
        Entry entry{}; bool available = false; uint32_t lost;
        const uint32_t mask = __get_PRIMASK(); __disable_irq();
        lost = dropped;
        if (count) { entry = entries[tail]; tail = (tail + 1) % Capacity; --count; available = true; }
        __set_PRIMASK(mask);
        if (!available) return;
        const int n = snprintf(pending, sizeof(pending),
            "CRABIT:RADIO t=%lu sid=%u ev=%s a=%lu b=%lu lost=%lu\r\n",
            (unsigned long)entry.at, unsigned(entry.session), names[entry.event],
            (unsigned long)entry.a, (unsigned long)entry.b, (unsigned long)lost);
        if (n <= 0 || unsigned(n) >= sizeof(pending)) return;
        pending_size = uint16_t(n); pending_at = HAL_GetTick();
    }
    const uint8_t result = CDC_Transmit_FS(reinterpret_cast<uint8_t*>(pending), pending_size);
    if (result == USBD_OK) pending_size = 0;
    else if (uint32_t(HAL_GetTick() - pending_at) >= 1000) {
        const uint32_t mask = __get_PRIMASK(); __disable_irq(); ++dropped; __set_PRIMASK(mask);
        pending_size = 0;
    }
}
} }
