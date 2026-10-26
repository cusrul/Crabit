#include "RadioSvc.h"

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>
#include <BLEDevice.h>
#include <Preferences.h>

extern "C" {
#include <gap.h>
#include <gap_callback_le.h>
#include <gap_le.h>
}

#include "AmebaServices.h"
#include "NetManager.h"
#include "Protocol.h"
#include "config.h"
#include "ble_auth.local.h"

// Fixed UART stages are sent by the Arduino loop before SDK entry.
#ifndef CRABIT_BW16_DIAG_CHECKPOINTS
#define CRABIT_BW16_DIAG_CHECKPOINTS 1
#endif

// Temporary cold-START admission test. UI requestStart()/PAIR DIAG is unchanged.
// Default is explicit for Arduino IDE; override 0 restores companion startup.
#ifndef CRABIT_BW16_DIAG_START_ADMISSION_ONLY
#define CRABIT_BW16_DIAG_START_ADMISSION_ONLY 1
#endif

namespace RadioSvc { void flushUartEvents(); } // Module-local writer helper.

namespace {

static const uint8_t RADIO_COMMAND_QUEUE_LENGTH = 4;
static const uint8_t RADIO_EVENT_QUEUE_LENGTH = 6;
static const uint8_t RADIO_NOTIFY_QUEUE_LENGTH = 4;
static const uint8_t RADIO_UART_QUEUE_LENGTH = 8;
static const uint16_t RADIO_BLE_WRITE_MAX = 20;
static const uint16_t RADIO_BLE_NOTIFY_MAX = 230;
static const uint16_t RADIO_NOTIFY_MAX = 320;
static const uint16_t RADIO_START_TIMEOUT_MS = 5000;
static const uint16_t RADIO_STOP_TIMEOUT_MS = 1000;
static const uint32_t RADIO_AUTH_TIMEOUT_MS = 15000;
static const uint16_t RADIO_RX_BUFFER_MAX = 512;
static const uint8_t RADIO_JSON_OBJECT_OPEN = '{';
static const size_t PAIR_TOKEN_HEX_LENGTH = 32;
static const size_t PAIR_COMMAND_LENGTH = 5 + PAIR_TOKEN_HEX_LENGTH;
static const char BLE_AUTH_OK[] = "{\"t\":\"auth\",\"v\":\"1\"}";
static_assert(sizeof(BLE_AUTH_OK) - 1 <= 20, "BLE auth confirmation must fit one default MTU notification");

enum CommandKind : uint8_t {
    COMMAND_START = 1,
    COMMAND_STOP = 2,
    COMMAND_PAIR_DIAG_BEGIN = 3,
    COMMAND_PAIR_DIAG_CONFIRM_RAM = 4,
    COMMAND_PAIR_DIAG_CANCEL = 5,
    COMMAND_PAIR_DIAG_END = 6,
    COMMAND_COMPANION_START = 7,
    COMMAND_COMPANION_STOP = 8,
    COMMAND_COMPANION_CONFIRM = 9,
    COMMAND_COMPANION_CANCEL = 10,
    COMMAND_COMPANION_ATTACH = 11
};

enum UartEventKind : uint8_t {
    UART_STATUS = 1,
    UART_FORWARD = 2,
    UART_COMPANION_STATUS = 3
};

enum RadioTraceStage : uint8_t {
    TRACE_CMD_RX, TRACE_START_OWNER, TRACE_PREFS_BEGIN, TRACE_PREFS_DONE,
    TRACE_BLE_INIT_BEGIN, TRACE_BLE_INIT_DONE, TRACE_PERIPH_BEGIN, TRACE_PERIPH_DONE,
    TRACE_CMD_QUEUED, TRACE_CMD_REJECTED
};
struct RadioTraceEvent { uint16_t request_id; RadioTraceStage stage; };
QueueHandle_t g_trace_queue = NULL;
volatile bool g_trace_overflow = false;

const char* radioTraceText(RadioTraceStage stage) {
    switch (stage) {
        case TRACE_CMD_RX: return "CMD_RX";
        case TRACE_START_OWNER: return "START_OWNER";
        case TRACE_PREFS_BEGIN: return "PREFS_BEGIN";
        case TRACE_PREFS_DONE: return "PREFS_DONE";
        case TRACE_BLE_INIT_BEGIN: return "BLE_INIT_BEGIN";
        case TRACE_BLE_INIT_DONE: return "BLE_INIT_DONE";
        case TRACE_PERIPH_BEGIN: return "PERIPH_BEGIN";
        case TRACE_PERIPH_DONE: return "PERIPH_DONE";
        case TRACE_CMD_QUEUED: return "CMD_QUEUED";
        case TRACE_CMD_REJECTED: return "CMD_REJECTED";
    }
    return "CMD_RX";
}

struct RadioCommand {
    CommandKind kind;
    uint16_t request_id;
    uint32_t generation;
    uint32_t pair_seq;
};

struct BleWriteEvent {
    uint8_t conn_id;
    uint16_t length;
    uint32_t link_epoch;
    uint32_t overflow_epoch;
    uint8_t data[RADIO_BLE_WRITE_MAX];
};

struct NotifyEvent {
    uint8_t conn_id;
    uint16_t length;
    uint32_t link_epoch;
    char payload[RADIO_NOTIFY_MAX];
};

struct UartEvent {
    UartEventKind kind;
    uint16_t request_id;
    uint16_t length;
    char payload[RADIO_NOTIFY_MAX];
};

QueueHandle_t g_command_queue = NULL;
QueueHandle_t g_event_queue = NULL;
QueueHandle_t g_notify_queue = NULL;
QueueHandle_t g_uart_queue = NULL;
TaskHandle_t g_loop_owner = NULL;

volatile RadioSvc::State g_state = RadioSvc::IDLE;
volatile RadioSvc::Result g_last_result = RadioSvc::STOPPED_RESULT;
volatile uint32_t g_generation = 0;
volatile uint32_t g_state_since_ms = 0;
volatile uint32_t g_start_deadline_ms = 0;
volatile uint32_t g_stop_deadline_ms = 0;
volatile uint16_t g_request_id = 0;
volatile uint8_t g_conn_id = 0;
volatile uint32_t g_ble_link_epoch = 0;
uint32_t g_active_link_epoch = 0;
volatile uint32_t g_tx_notify_epoch = 0;
volatile uint32_t g_auth_deadline_ms = 0;
volatile bool g_auth_pending = false;
volatile bool g_auth_disconnect_pending = false;
volatile bool g_auth_disconnect_requested = false;
volatile uint32_t g_auth_disconnect_retry_ms = 0;
volatile bool g_tx_notify_enabled = false;
volatile uint8_t g_tx_notify_conn_id = 0xff;
volatile uint32_t g_rx_queue_overflow_count = 0;
uint32_t g_rx_queue_overflow_seen = 0;
bool g_rx_discard_until_command = false;
volatile bool g_initialized = false;
volatile bool g_owner_ready = false;

BLEService g_uart_service("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
BLECharacteristic g_tx_char("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");
BLECharacteristic g_rx_char("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");

bool g_ble_stack_ready = false;
bool g_peripheral_started = false;
// A one-way policy for this boot, selected before ordinary MODE_BLE startup.
// Ending a UI diagnostic must never re-enable Preferences writes in this mode.
bool g_mode_ram_pairing = false;
uint16_t g_mode_pair_session = 0;
Preferences g_pairing_prefs;
String g_bound_pair_token;
String g_pending_pair_token;
uint32_t g_pair_deadline_ms = 0;
uint8_t g_pair_conn_id = 0;
char g_rx_buffer[RADIO_RX_BUFFER_MAX + 1] = {};
size_t g_rx_length = 0;
volatile RadioSvc::PairDiagnosticState g_pair_diagnostic_state = RadioSvc::PAIR_DIAG_OFF;
volatile uint16_t g_pair_rx_length = 0;
volatile bool g_pair_diagnostic_active = false;
volatile bool g_companion_active = false;
volatile bool g_companion_mode_owned = false;
uint16_t g_companion_session = 0;
uint32_t g_pair_sequence = 0;
uint32_t g_pending_pair_sequence = 0;

void bleWriteCallback(BLECharacteristic* characteristic, uint8_t conn_id);
void bleCccdCallback(BLECharacteristic* characteristic, uint8_t conn_id, uint16_t ccc_bits);

T_APP_RESULT radioGapCallback(uint8_t cb_type, void*) {
    if (cb_type == GAP_MSG_LE_CREATE_CONN_IND) {
        uint32_t next_epoch = g_ble_link_epoch + 1;
        if (next_epoch == 0) ++next_epoch;
        g_ble_link_epoch = next_epoch;
        g_tx_notify_enabled = false;
        g_tx_notify_conn_id = 0xff;
        return APP_RESULT_ACCEPT;
    }
#if defined(GAP_MSG_LE_CONN_UPDATE_IND)
    if (cb_type == GAP_MSG_LE_CONN_UPDATE_IND) return APP_RESULT_ACCEPT;
#endif
    return APP_RESULT_SUCCESS;
}

bool enableConnectionIndicationCallback() {
    // AmebaD defaults GAP_PARAM_HANDLE_CREATE_CONN_IND to disabled. Registering
    // the callback alone does not make the SDK deliver connection indications.
    le_register_app_cb(radioGapCallback);
    uint8_t enabled = 1;
    return le_set_gap_param(GAP_PARAM_HANDLE_CREATE_CONN_IND, sizeof(enabled), &enabled) == GAP_CAUSE_SUCCESS;
}

bool isConfiguredAuthCodeValid() {
    if (strlen(CRABIT_BLE_AUTH_CODE) != 6) return false;
    for (size_t index = 0; index < 6; ++index) {
        if (CRABIT_BLE_AUTH_CODE[index] < '0' || CRABIT_BLE_AUTH_CODE[index] > '9') return false;
    }
    return true;
}

bool matchesConfiguredAuthCode(const char* command) {
    static const char prefix[] = "AUTH:";
    if (!command || strlen(command) != sizeof(prefix) - 1 + 6 ||
        memcmp(command, prefix, sizeof(prefix) - 1) != 0) return false;

    uint8_t difference = 0;
    for (size_t index = 0; index < 6; ++index) {
        difference |= (uint8_t)(command[sizeof(prefix) - 1 + index] ^ CRABIT_BLE_AUTH_CODE[index]);
    }
    return difference == 0;
}

bool requestBleDisconnect(uint8_t conn_id) {
    BLEConnect* connection = BLE.configConnection();
    return connection && connection->disconnect(conn_id);
}

void rejectBleConnection(uint8_t conn_id) {
    ble_is_authenticated = false;
    g_auth_pending = false;
    g_auth_deadline_ms = 0;
    g_auth_disconnect_pending = true;
    g_auth_disconnect_requested = false;
    g_auth_disconnect_retry_ms = 0;
    if (requestBleDisconnect(conn_id)) {
        g_auth_disconnect_requested = true;
        g_auth_disconnect_retry_ms = millis();
    }
}

bool enqueueUartEvent(UartEventKind kind, const char* payload, uint16_t request_id) {
    if (!g_uart_queue || !payload) return false;
    UartEvent event{};
    event.kind = kind;
    event.request_id = request_id;
    event.length = (uint16_t)strlen(payload);
    if (event.length == 0 || event.length >= sizeof(event.payload)) {
        g_last_result = RadioSvc::REJECTED;
        return false;
    }
    memcpy(event.payload, payload, event.length + 1);
    if (xQueueSend(g_uart_queue, &event, 0) != pdTRUE) {
        g_last_result = RadioSvc::REJECTED;
        return false;
    }
    return true;
}

void publishProtocolStatus(const char* status, uint16_t request_id) {
    enqueueUartEvent(UART_STATUS, status, request_id);
}

void publishCompanionStatus(const char* status, uint16_t session_id) {
    if (!g_uart_queue || !status) return;
    UartEvent event{}; event.kind = UART_COMPANION_STATUS; event.request_id = session_id;
    event.length = (uint16_t)strlen(status);
    if (!event.length || event.length >= sizeof(event.payload)) return;
    memcpy(event.payload, status, event.length + 1);
    if (xQueueSend(g_uart_queue, &event, 0) != pdTRUE) g_last_result = RadioSvc::REJECTED;
}

bool enqueueRadioTrace(RadioTraceStage stage, uint16_t request_id) {
    if (!g_trace_queue) return false;
    const RadioTraceEvent event{request_id, stage};
    if (xQueueSend(g_trace_queue, &event, 0) == pdTRUE) return true;
    g_trace_overflow = true;
    return false;
}

void setState(RadioSvc::State state) {
    g_state = state;
    g_state_since_ms = millis();
}

bool generationIsCurrent(uint32_t generation) {
    return generation == g_generation;
}

bool radioCheckpoint(RadioTraceStage stage, uint16_t request_id, uint32_t generation) {
    // Unlike the previous worker fence, the SDK caller IS the UART writer.
    // No notification, helper task or wait for another task is involved.
    if (!g_loop_owner || xTaskGetCurrentTaskHandle() != g_loop_owner ||
            !generationIsCurrent(generation) || g_state != RadioSvc::STARTING ||
            (int32_t)(millis() - g_start_deadline_ms) >= 0) return false;
    if (!CRABIT_BW16_DIAG_CHECKPOINTS || !g_companion_active) {
        enqueueRadioTrace(stage, request_id);
        return true;
    }
    if (!Protocol::send_message_checked("diag/radio", radioTraceText(stage), request_id)) return false;
    // LOGUART flush is a no-op. This is driver acceptance, not a remote ACK.
    return generationIsCurrent(generation) && g_state == RadioSvc::STARTING &&
        (int32_t)(millis() - g_start_deadline_ms) < 0;
}

void serviceRadioTrace(const RadioTraceEvent& trace) {
    Protocol::send_message("diag/radio", radioTraceText(trace.stage), trace.request_id);
}

void clearConnectionState(bool reset_write_queue = true) {
    const bool pending_pair = g_pending_pair_token.length() > 0;
    ble_is_authenticated = false;
    ble_was_connected = false;
    NetManager::active_conn_id = 0;
    g_conn_id = 0;
    g_active_link_epoch = g_ble_link_epoch;
    g_auth_deadline_ms = 0;
    g_auth_pending = false;
    g_auth_disconnect_pending = false;
    g_auth_disconnect_requested = false;
    g_auth_disconnect_retry_ms = 0;
    g_tx_notify_enabled = false;
    g_tx_notify_conn_id = 0xff;
    g_tx_notify_epoch = 0;
    g_rx_queue_overflow_seen = g_rx_queue_overflow_count;
    g_rx_discard_until_command = false;
    if (reset_write_queue && g_event_queue) xQueueReset(g_event_queue);
    g_pending_pair_token = "";
    g_pair_deadline_ms = 0;
    g_pair_conn_id = 0;
    g_pending_pair_sequence = 0;
    g_rx_length = 0;
    g_rx_buffer[0] = '\0';
    if (g_pair_diagnostic_active && pending_pair) {
        g_pair_diagnostic_state = RadioSvc::PAIR_DIAG_CANCELLED;
    }
}

void clearPendingPair() {
    g_pending_pair_token = "";
    g_pair_deadline_ms = 0;
    g_pair_conn_id = 0;
    g_pending_pair_sequence = 0;
}

void publishModePairStatus(const char* status, uint32_t sequence = 0) {
    if (!g_mode_ram_pairing || !g_mode_pair_session || current_mode != MODE_BLE) return;
    char value[56];
    if (sequence) snprintf(value, sizeof(value), "%s:%lu", status, (unsigned long)sequence);
    else snprintf(value, sizeof(value), "%s", status);
    publishCompanionStatus(value, g_mode_pair_session);
}

void publishBleStatus(const char* status, uint16_t request_id);

bool beginBleSessionForEpoch(uint8_t conn_id, uint32_t link_epoch) {
    if (link_epoch != g_ble_link_epoch) return false;
    if (g_state == RadioSvc::CONNECTED && link_epoch == g_active_link_epoch &&
            conn_id == g_conn_id) return false;

    const bool current_link_notifies = g_tx_notify_enabled &&
        g_tx_notify_conn_id == conn_id && g_tx_notify_epoch == link_epoch;
    // Retain queued writes; old-link events are dropped by their epoch and
    // already-arrived writes for this link remain ordered for the owner.
    clearConnectionState(false);
    g_active_link_epoch = link_epoch;
    g_conn_id = conn_id;
    g_tx_notify_enabled = current_link_notifies;
    g_tx_notify_conn_id = current_link_notifies ? conn_id : 0xff;
    g_tx_notify_epoch = current_link_notifies ? link_epoch : 0;
    NetManager::active_conn_id = conn_id;
    ble_was_connected = true;
    g_auth_pending = g_mode_ram_pairing;
    g_auth_deadline_ms = g_auth_pending ? millis() + RADIO_AUTH_TIMEOUT_MS : 0;
    setState(RadioSvc::CONNECTED);
    publishBleStatus("READY", g_request_id);
    if (g_companion_active) publishCompanionStatus("CONNECTED", g_companion_session);
    publishModePairStatus("CONNECTED");
    return true;
}

size_t notificationChunkBytes(uint8_t conn_id) {
    uint16_t mtu = 23;
    if (le_get_conn_param(GAP_PARAM_CONN_MTU_SIZE, &mtu, conn_id) != GAP_CAUSE_SUCCESS || mtu < 23) {
        return 20;
    }
    const size_t available = mtu - 3;
    return available < RADIO_BLE_NOTIFY_MAX ? available : RADIO_BLE_NOTIFY_MAX;
}

bool ownerNotify(const char* payload, uint8_t conn_id) {
    if (!payload || g_state != RadioSvc::CONNECTED ||
            g_active_link_epoch != g_ble_link_epoch || conn_id != g_conn_id) return false;
    if (!g_tx_notify_enabled || g_tx_notify_conn_id != conn_id ||
            g_tx_notify_epoch != g_active_link_epoch) return false;
    const size_t length = strlen(payload);
    if (length == 0 || length > RADIO_NOTIFY_MAX) return false;

    size_t offset = 0;
    const size_t chunk_bytes = notificationChunkBytes(conn_id);
    while (offset < length) {
        if (g_state != RadioSvc::CONNECTED || g_active_link_epoch != g_ble_link_epoch ||
                conn_id != g_conn_id || !g_tx_notify_enabled ||
                g_tx_notify_conn_id != conn_id || g_tx_notify_epoch != g_active_link_epoch) return false;
        size_t chunk_length = length - offset;
        if (chunk_length > chunk_bytes) chunk_length = chunk_bytes;
        // SDK setData copies into the characteristic buffer; avoid adding a
        // 230-byte temporary to the radio task stack. Input is not modified.
        if (!g_tx_char.setData(reinterpret_cast<uint8_t*>(const_cast<char*>(payload + offset)), chunk_length)) return false;
        g_tx_char.notify(conn_id);
        offset += chunk_length;
        if (offset < length) vTaskDelay(pdMS_TO_TICKS(30));
    }
    return true;
}

void publishBleStatus(const char* status, uint16_t request_id) {
    char json[96];
    snprintf(json, sizeof(json), "{\"t\":\"sys/ble\",\"v\":\"%s\"}", status);
    if (g_state == RadioSvc::CONNECTED) ownerNotify(json, g_conn_id);
    publishProtocolStatus(status, request_id);
}

void failGeneration(uint32_t generation, const char* status) {
    if (!generationIsCurrent(generation)) return;
    clearConnectionState();
    g_last_result = RadioSvc::FAILED_RESULT;
    setState(RadioSvc::FAILED);
    publishBleStatus(status, g_request_id);
}

bool configureBleOnce(uint32_t generation) {
    if (g_ble_stack_ready) return true;

    if (!g_mode_ram_pairing) {
        if (!radioCheckpoint(TRACE_PREFS_BEGIN, g_request_id, generation)) return false;
        g_pairing_prefs.begin("crabit", false);
        g_bound_pair_token = g_pairing_prefs.getString("pair_token", "");
        if (!radioCheckpoint(TRACE_PREFS_DONE, g_request_id, generation)) return false;
    }

    // Match BLEUartService's one-shot setup order: configure characteristics
    // and service, init the SDK, then attach advertisement/server/service.
    g_tx_char.setReadProperty(true);
    g_tx_char.setReadPermissions(GATT_PERM_READ);
    g_tx_char.setNotifyProperty(true);
    g_tx_char.setBufferLen(RADIO_BLE_NOTIFY_MAX);
    g_tx_char.setCCCDCallback(bleCccdCallback);

    g_rx_char.setWriteProperty(true);
    g_rx_char.setWritePermissions(GATT_PERM_WRITE);
    g_rx_char.setBufferLen(20);
    g_rx_char.setWriteCallback(bleWriteCallback);

    g_uart_service.addCharacteristic(g_tx_char);
    g_uart_service.addCharacteristic(g_rx_char);
    BLEAdvertData advert_data;
    advert_data.addFlags();
    advert_data.addCompleteName("Crabit-Watch");
    if (!radioCheckpoint(TRACE_BLE_INIT_BEGIN, g_request_id, generation)) return false;
    BLE.init();
    if (!radioCheckpoint(TRACE_BLE_INIT_DONE, g_request_id, generation)) return false;
    BLE.configAdvert()->setAdvData(advert_data);
    BLE.configServer(1);
    BLE.addService(g_uart_service);
    BLE.setDeviceName("Crabit-Watch");
    g_ble_stack_ready = true;
    return true;
}

bool bleCommandIsComplete() {
    if (g_rx_length == 0) return false;
    if (g_rx_buffer[0] == '{') return g_rx_buffer[g_rx_length - 1] == '}';
    if (strncmp(g_rx_buffer, "PAIR:", 5) == 0) {
        return g_rx_length >= PAIR_COMMAND_LENGTH;
    }
    if (strncmp(g_rx_buffer, "AUTH:", 5) == 0) return true;
    if (strncmp(g_rx_buffer, "TEST:", 5) == 0) return true;
    return false;
}

bool isPairTokenValid(const String& token) {
    if (token.length() != PAIR_TOKEN_HEX_LENGTH) return false;
    for (size_t index = 0; index < PAIR_TOKEN_HEX_LENGTH; ++index) {
        const char value = token[index];
        const bool is_digit = value >= '0' && value <= '9';
        const bool is_lower_hex = value >= 'a' && value <= 'f';
        const bool is_upper_hex = value >= 'A' && value <= 'F';
        if (!is_digit && !is_lower_hex && !is_upper_hex) return false;
    }
    return true;
}

String diagnosticRequestId(const String& json) {
    int marker = json.indexOf("\"i\"");
    if (marker < 0) return "0";
    int colon = json.indexOf(':', marker);
    if (colon < 0) return "0";
    int start = colon + 1;
    while (start < json.length() && (json[start] == ' ' || json[start] == '\t')) start++;
    int end = start;
    while (end < json.length() && json[end] >= '0' && json[end] <= '9') end++;
    if (end == start) return "0";
    return json.substring(start, end);
}

void handleBleCommand(const char* command, uint8_t conn_id) {
    String full_command(command ? command : "");
    if (full_command.startsWith("AUTH:")) {
        if (!g_mode_ram_pairing || current_mode != MODE_BLE ||
            !g_auth_pending || g_auth_disconnect_pending ||
            RadioSvc::pairDeadlineExpired(millis(), g_auth_deadline_ms) ||
            !g_tx_notify_enabled || g_tx_notify_conn_id != conn_id ||
            !matchesConfiguredAuthCode(command)) {
            rejectBleConnection(conn_id);
            return;
        }
        g_auth_pending = false;
        g_auth_deadline_ms = 0;
        ble_is_authenticated = true;
        if (!ownerNotify(BLE_AUTH_OK, conn_id)) {
            rejectBleConnection(conn_id);
        }
        return;
    }

    if (full_command.startsWith("PAIR:")) {
        if (g_mode_ram_pairing) {
            rejectBleConnection(conn_id);
            return;
        }
        String token = full_command.substring(5);
        if (!isPairTokenValid(token)) return;
        if (g_bound_pair_token.length() > 0 && token == g_bound_pair_token) {
            if (g_mode_pair_session) clearPendingPair();
            ble_is_authenticated = true;
            ownerNotify("{\"t\":\"sys/ble\",\"v\":\"PAIR_CONFIRMED\"}", conn_id);
            if (g_companion_active) publishCompanionStatus("AUTHENTICATED", g_companion_session);
            if (g_mode_pair_session) {
                if (++g_pair_sequence == 0) ++g_pair_sequence;
                publishModePairStatus("AUTHENTICATED", g_pair_sequence);
            }
            return;
        }

        g_pending_pair_token = token;
        g_pair_conn_id = conn_id;
        g_pair_deadline_ms = millis() + 30000;
        ble_is_authenticated = false;
        if (g_pair_diagnostic_active) {
            g_pair_diagnostic_state = RadioSvc::PAIR_DIAG_WAITING;
            if (++g_pair_sequence == 0) ++g_pair_sequence;
            g_pending_pair_sequence = g_pair_sequence;
            if (g_companion_active) { char status[40]; snprintf(status, sizeof(status), "PAIR_WAIT:%lu", (unsigned long)g_pending_pair_sequence); publishCompanionStatus(status, g_companion_session); }
            publishModePairStatus("PAIR_WAIT", g_pending_pair_sequence);
            g_pair_rx_length = PAIR_COMMAND_LENGTH;
        }
        ownerNotify("{\"t\":\"sys/ble\",\"v\":\"PAIR_CONFIRM_ON_WATCH\"}", conn_id);
        return;
    }

    if (g_mode_ram_pairing && !ble_is_authenticated) {
        rejectBleConnection(conn_id);
        return;
    }
    if (full_command.startsWith("TEST:")) {
        String response = "{\"t\":\"sys/notify\",\"v\":\"" + full_command + "\"}";
        ownerNotify(response.c_str(), conn_id);
        return;
    }
    if (!ble_is_authenticated) return;
    if (full_command.indexOf("\"t\":\"diag/wifi\"") >= 0) {
        String request_id = diagnosticRequestId(full_command);
        AmebaServices::enqueueWifiDiagnostic((uint16_t)request_id.toInt(), conn_id);
        return;
    }
    if (!enqueueUartEvent(UART_FORWARD, full_command.c_str(), 0)) {
        ownerNotify("{\"t\":\"sys/ble\",\"v\":\"REJECTED\"}", conn_id);
    }
}

bool prepareBleWriteEvent(const BleWriteEvent& event) {
    if (event.link_epoch != g_ble_link_epoch || !BLE.connected(event.conn_id)) return false;

    const uint32_t overflow_epoch = g_rx_queue_overflow_count;
    if (g_rx_queue_overflow_seen != overflow_epoch) {
        g_rx_queue_overflow_seen = overflow_epoch;
        g_rx_length = 0;
        g_rx_buffer[0] = '\0';
        g_rx_discard_until_command = true;
    }
    if (event.overflow_epoch != overflow_epoch) {
        g_rx_discard_until_command = true;
        return false;
    }

    const bool new_command = event.length > 0 &&
        (event.data[0] == RADIO_JSON_OBJECT_OPEN ||
         (event.length >= 5 && (memcmp(event.data, "PAIR:", 5) == 0 ||
                                memcmp(event.data, "AUTH:", 5) == 0 ||
                                memcmp(event.data, "TEST:", 5) == 0)));
    if (new_command) {
        g_rx_length = 0;
        g_rx_discard_until_command = false;
        if (g_pair_diagnostic_active) g_pair_rx_length = 0;
    }
    return !g_rx_discard_until_command;
}

void activateBleSessionForEvent(const BleWriteEvent& event) {
    if (event.link_epoch != g_ble_link_epoch) return;
    beginBleSessionForEpoch(event.conn_id, event.link_epoch);
}

void handleBleWriteEvent(const BleWriteEvent& event) {
    if (event.link_epoch != g_ble_link_epoch || !BLE.connected(event.conn_id)) return;
    activateBleSessionForEvent(event);

    // Queued fragments from before an overflow may include a stale opening '{'.
    // Drop that entire pre-loss batch; only writes enqueued after the loss can
    // restart command assembly.
    if (!prepareBleWriteEvent(event)) return;

    g_conn_id = event.conn_id;
    NetManager::active_conn_id = event.conn_id;
    if (g_state == RadioSvc::ADVERTISING) {
        setState(RadioSvc::CONNECTED);
        ble_was_connected = true;
        g_auth_pending = g_mode_ram_pairing;
        g_auth_deadline_ms = g_auth_pending ? millis() + RADIO_AUTH_TIMEOUT_MS : 0;
        publishBleStatus("READY", g_request_id);
        if (g_companion_active) publishCompanionStatus("CONNECTED", g_companion_session);
        publishModePairStatus("CONNECTED");
    } else if (g_state != RadioSvc::CONNECTED) {
        return;
    }

    if (g_rx_discard_until_command) return;
    if (g_rx_length + event.length > RADIO_RX_BUFFER_MAX) {
        g_rx_length = 0;
        g_rx_discard_until_command = true;
        return;
    }
    memcpy(g_rx_buffer + g_rx_length, event.data, event.length);
    g_rx_length += event.length;
    g_rx_buffer[g_rx_length] = '\0';
    if (g_pair_diagnostic_active && strncmp(g_rx_buffer, "PAIR:", 5) == 0) {
        g_pair_rx_length = (uint16_t)g_rx_length;
    }

    if (!bleCommandIsComplete()) return;
    char command[RADIO_RX_BUFFER_MAX + 1];
    memcpy(command, g_rx_buffer, g_rx_length + 1);
    g_rx_length = 0;
    handleBleCommand(command, event.conn_id);
}

void processNotifyEvent(const NotifyEvent& event) {
    if (event.link_epoch != g_ble_link_epoch || event.length == 0 ||
            event.length > RADIO_NOTIFY_MAX) return;
    ownerNotify(event.payload, event.conn_id);
}

void processPairing() {
    if (g_pending_pair_token.length() == 0) return;
    if (RadioSvc::pairDeadlineExpired(millis(), g_pair_deadline_ms)) {
        const uint8_t pair_conn_id = g_pair_conn_id;
        const uint32_t pair_sequence = g_pending_pair_sequence;
        clearPendingPair();
        g_pending_pair_sequence = 0;
        if (g_pair_diagnostic_active) {
            g_pair_diagnostic_state = RadioSvc::PAIR_DIAG_TIMEOUT;
            if (g_companion_active) publishCompanionStatus("PAIR_TIMEOUT", g_companion_session);
            publishModePairStatus("PAIR_TIMEOUT", pair_sequence);
        }
        ownerNotify("{\"t\":\"sys/ble\",\"v\":\"PAIR_TIMEOUT\"}", pair_conn_id);
        return;
    }
    if (g_mode_ram_pairing || g_pair_diagnostic_active) return;
    if (digitalRead(BTN_OK_PIN) != LOW) return;

    ble_is_authenticated = true;
    g_bound_pair_token = g_pending_pair_token;
    g_pairing_prefs.putString("pair_token", g_bound_pair_token);
    g_pending_pair_token = "";
    ownerNotify("{\"t\":\"sys/ble\",\"v\":\"PAIR_CONFIRMED\"}", g_pair_conn_id);
}

void processPairDiagnosticBegin() {
    clearPendingPair();
    g_pending_pair_sequence = 0;
    ble_is_authenticated = false;
    g_pair_rx_length = 0;
    g_pair_diagnostic_active = true;
    g_pair_diagnostic_state = RadioSvc::PAIR_DIAG_IDLE;
}

void processPairDiagnosticConfirmRam() {
    if (!g_pair_diagnostic_active || g_pending_pair_token.length() == 0 ||
        g_state != RadioSvc::CONNECTED || g_pair_conn_id != g_conn_id) {
        return;
    }

    ble_is_authenticated = true;
    g_bound_pair_token = g_pending_pair_token;
    clearPendingPair();
    g_pair_diagnostic_state = RadioSvc::PAIR_DIAG_RAM_CONFIRMED;
    g_pending_pair_sequence = 0;
    ownerNotify("{\"t\":\"sys/ble\",\"v\":\"PAIR_CONFIRMED\"}", g_conn_id);
}

void processPairDiagnosticCancel() {
    if (!g_pair_diagnostic_active) return;
    const bool had_pending_pair = g_pending_pair_token.length() > 0;
    clearPendingPair();
    ble_is_authenticated = false;
    g_pair_diagnostic_state = RadioSvc::PAIR_DIAG_CANCELLED;
    if (had_pending_pair && g_state == RadioSvc::CONNECTED) {
        ownerNotify("{\"t\":\"sys/ble\",\"v\":\"PAIR_CANCELLED\"}", g_conn_id);
    }
}

void processPairDiagnosticEnd() {
    if (g_mode_ram_pairing) return;
    processPairDiagnosticCancel();
    g_pair_diagnostic_active = false;
    g_pair_diagnostic_state = RadioSvc::PAIR_DIAG_OFF;
    g_pair_rx_length = 0;
}

void processConnection() {
    if (BLE.connected() && (g_ble_link_epoch != g_active_link_epoch ||
            g_state == RadioSvc::ADVERTISING)) {
        // Reconcile before draining RX/TX queues so a silent reconnect cannot
        // retain the previous link's authorization or CCCD state.
        beginBleSessionForEpoch(0, g_ble_link_epoch);
    }

    if (g_state != RadioSvc::CONNECTED) return;
    if (BLE.connected(g_conn_id)) return;

    clearConnectionState();
    if (g_peripheral_started) BLE.configAdvert()->startAdv();
    setState(RadioSvc::ADVERTISING);
    publishProtocolStatus("DISCONNECTED", g_request_id);
    if (g_companion_active) publishCompanionStatus("DISCONNECTED", g_companion_session);
    publishModePairStatus("DISCONNECTED");
}

void processAuthentication() {
    if (g_state != RadioSvc::CONNECTED || !g_mode_ram_pairing) return;
    if (g_auth_disconnect_pending) {
        if (g_auth_disconnect_requested &&
            (uint32_t)(millis() - g_auth_disconnect_retry_ms) < 250) return;
        g_auth_disconnect_requested = requestBleDisconnect(g_conn_id);
        g_auth_disconnect_retry_ms = millis();
        return;
    }
    if (g_auth_pending && RadioSvc::pairDeadlineExpired(millis(), g_auth_deadline_ms)) {
        rejectBleConnection(g_conn_id);
    }
}

void processStart(const RadioCommand& command) {
    if (!generationIsCurrent(command.generation)) return;
    if (current_mode == MODE_BLE && !isConfiguredAuthCodeValid()) {
        failGeneration(command.generation, "AUTH_CONFIG_INVALID");
        return;
    }
    if (current_mode == MODE_BLE && !g_mode_ram_pairing) {
        g_mode_ram_pairing = true;
        processPairDiagnosticBegin();
        g_bound_pair_token = ""; // Forget legacy token only in RAM, before configureBleOnce.
    }
    if (current_mode == MODE_BLE) g_mode_pair_session = command.request_id;
    // A UART command can arrive after service()'s first flush. Flush again
    // AFTER dequeue/START_OWNER, before configureBleOnce can enter the SDK.
    RadioSvc::flushUartEvents();
    if (!configureBleOnce(command.generation)) {
        failGeneration(command.generation, "FAILED");
        return;
    }
    if (!generationIsCurrent(command.generation)) return;

    if (!radioCheckpoint(TRACE_PERIPH_BEGIN, command.request_id, command.generation)) {
        failGeneration(command.generation, "FAILED"); return;
    }
    const bool peripheral_was_started = g_peripheral_started;
    if (!g_peripheral_started) {
        BLE.beginPeripheral();
        g_peripheral_started = true;
    }
    // AmebaD defaults connection-indication delivery off. Register the callback
    // and enable that opt-in before explicitly restarting an existing stack.
    if (!enableConnectionIndicationCallback()) {
        BLE.configAdvert()->stopAdv();
        failGeneration(command.generation, "FAILED");
        return;
    }
    if (peripheral_was_started) {
        BLE.configAdvert()->startAdv();
    }
    // beginPeripheral only waits for STACK_READY. The SDK's subsequent
    // le_adv_start is asynchronous and its result is not returned by Arduino.
    // Observe actual GAP advertising (or an already established connection)
    // within the original budget before publishing READY.
    bool radio_ready = false;
    while (generationIsCurrent(command.generation) &&
            (int32_t)(millis() - g_start_deadline_ms) < 0) {
        T_GAP_DEV_STATE device_state{};
        if (le_get_gap_param(GAP_PARAM_DEV_STATE, &device_state) == GAP_CAUSE_SUCCESS &&
                (device_state.gap_adv_state == GAP_ADV_STATE_ADVERTISING || BLE.connected())) {
            radio_ready = true;
            break;
        }
        delay(10);
    }
    if (!radio_ready) {
        BLE.configAdvert()->stopAdv();
        failGeneration(command.generation, "FAILED"); return;
    }
    if (!radioCheckpoint(TRACE_PERIPH_DONE, command.request_id, command.generation)) {
        // Peripheral may already advertise. Match the existing stale-generation
        // cleanup instead of leaving radio output active behind FAILED.
        BLE.configAdvert()->stopAdv();
        failGeneration(command.generation, "FAILED"); return;
    }
    if (!generationIsCurrent(command.generation)) {
        BLE.configAdvert()->stopAdv();
        return;
    }

    g_last_result = RadioSvc::ACQUIRED;
    setState(RadioSvc::ADVERTISING);
    publishProtocolStatus("ADVERTISING", command.request_id);
}

void processStop(const RadioCommand& command);

void attachDiagnostic(const char* stage, uint16_t request_id) {
#if CRABIT_BW16_DIAG_ATTACH_TRACE
    // Called only from the existing Arduino writer, never the UART worker.
    Protocol::send_message("diag/radio", stage, request_id);
#else
    (void)stage; (void)request_id;
#endif
}

void processCompanion(const RadioCommand& command) {
    if (command.kind == COMMAND_COMPANION_ATTACH) {
        attachDiagnostic("ATTACH_OWNER", command.request_id);
        // Attach RAM pairing only AFTER the proven ordinary BLE mode boot.
        // No SDK init, mode transition, Preferences write or radio restart.
        if (!command.request_id || command.pair_seq || command.generation != g_generation ||
                current_mode != MODE_BLE || !g_ble_stack_ready ||
                (g_state != RadioSvc::ADVERTISING && g_state != RadioSvc::CONNECTED)) {
            attachDiagnostic("ATTACH_REJECTED", command.request_id);
            publishCompanionStatus("REJECTED", command.request_id); return;
        }
        if (g_companion_active) {
            publishCompanionStatus(g_companion_session == command.request_id ? "ATTACHED" : "BUSY",
                                   command.request_id);
            return; // Idempotent retry must not erase a pending/confirmed pair.
        }
#if CRABIT_BW16_DIAG_ATTACH_ADMISSION_ONLY
        // Diagnostic refusal, not ATTACHED: leave all RAM pairing/session state
        // untouched while exercising the same command queue and UART reply.
        attachDiagnostic("ATTACH_REJECTED", command.request_id);
        publishCompanionStatus("REJECTED", command.request_id);
        return;
#else
        processPairDiagnosticBegin();
        attachDiagnostic("ATTACH_RAM_BEGIN_DONE", command.request_id);
        g_bound_pair_token = ""; // Forget loaded token in RAM, never erase Flash.
        attachDiagnostic("ATTACH_RAM_READY", command.request_id);
        g_companion_session = command.request_id;
        g_companion_active = true;
        const bool reply_queued = enqueueUartEvent(UART_COMPANION_STATUS, "ATTACHED", command.request_id);
        attachDiagnostic(reply_queued ? "ATTACH_REPLY_QUEUED" : "ATTACH_REPLY_REJECTED", command.request_id);
        return;
#endif
    }
    if (command.kind == COMMAND_COMPANION_START) {
        enqueueRadioTrace(TRACE_START_OWNER, command.request_id);
        if (command.generation != g_generation + 1) { publishCompanionStatus("REJECTED", command.request_id); return; }
        if (current_mode != MODE_WAITING) { publishCompanionStatus("REJECTED", command.request_id); return; }
        if (g_companion_active || g_state != RadioSvc::IDLE) { publishCompanionStatus("BUSY", command.request_id); return; }
#if CRABIT_BW16_DIAG_START_ADMISSION_ONLY
        // Prove receive -> admission -> main-loop -> response without entering
        // Preferences/BLE or selecting MODE_BLE. Never fabricate READY.
        g_last_result = RadioSvc::REJECTED;
        publishCompanionStatus("REJECTED", command.request_id);
        RadioSvc::flushUartEvents();
        return;
#endif
        g_generation = command.generation;
        g_request_id = command.request_id;
        g_start_deadline_ms = millis() + RADIO_START_TIMEOUT_MS;
        g_last_result = RadioSvc::QUEUED;
        setState(RadioSvc::STARTING);
        g_companion_active = true; g_companion_session = command.request_id;
        g_companion_mode_owned = true;
        // Boot selection is one-way. STOP releases radio activity, not the
        // selected boot mode; STM32 powers down before selecting another mode.
        current_mode = MODE_BLE;
        g_pair_diagnostic_active = true;
        processStart(RadioCommand{COMMAND_START, command.request_id, g_generation, 0});
        if (g_state == RadioSvc::ADVERTISING || g_state == RadioSvc::CONNECTED) publishCompanionStatus("READY", command.request_id);
        else if (g_state == RadioSvc::FAILED) {
            publishCompanionStatus("FAILED", command.request_id);
            g_companion_active = false; g_companion_session = 0; g_pair_diagnostic_active = false;
        }
        return;
    }
    if (!g_companion_active || command.request_id != g_companion_session) { publishCompanionStatus("REJECTED", command.request_id); return; }
    if (command.kind == COMMAND_COMPANION_STOP) {
        processStop(RadioCommand{COMMAND_STOP, command.request_id, ++g_generation, 0});
        publishCompanionStatus("STOPPED", command.request_id);
        g_companion_active = false; g_companion_session = 0; g_pending_pair_sequence = 0;
        g_pair_diagnostic_active = false; g_pair_diagnostic_state = RadioSvc::PAIR_DIAG_OFF;
    } else if (command.kind == COMMAND_COMPANION_CONFIRM || command.kind == COMMAND_COMPANION_CANCEL) {
        if (command.pair_seq == 0 || command.pair_seq != g_pending_pair_sequence ||
            g_state != RadioSvc::CONNECTED || g_pending_pair_token.length() == 0) {
            publishCompanionStatus("PAIR_REJECTED", command.request_id);
            return;
        }
        if (RadioSvc::pairDeadlineExpired(millis(), g_pair_deadline_ms)) {
            const uint8_t pair_conn_id = g_pair_conn_id;
            clearPendingPair();
            g_pair_rx_length = 0;
            ble_is_authenticated = false;
            g_pair_diagnostic_state = RadioSvc::PAIR_DIAG_TIMEOUT;
            ownerNotify("{\"t\":\"sys/ble\",\"v\":\"PAIR_TIMEOUT\"}", pair_conn_id);
            publishCompanionStatus("PAIR_TIMEOUT", g_companion_session);
            return;
        }
        if (command.kind == COMMAND_COMPANION_CONFIRM) processPairDiagnosticConfirmRam();
        else processPairDiagnosticCancel();
        g_pending_pair_sequence = 0;
        publishCompanionStatus(command.kind == COMMAND_COMPANION_CONFIRM ? "AUTHENTICATED" : "PAIR_CANCELLED", command.request_id);
    } else publishCompanionStatus("REJECTED", command.request_id);
}

void processStop(const RadioCommand& command) {
    if (!generationIsCurrent(command.generation)) return;
    if (g_ble_stack_ready && g_peripheral_started) BLE.configAdvert()->stopAdv();
    if (g_state == RadioSvc::CONNECTED) publishBleStatus("STOPPED", command.request_id);
    clearConnectionState();
    g_last_result = RadioSvc::STOPPED_RESULT;
    setState(RadioSvc::IDLE);
    publishProtocolStatus("STOPPED", command.request_id);
}

void bleWriteCallback(BLECharacteristic* characteristic, uint8_t conn_id) {
    if (!characteristic || !g_event_queue) return;
    const uint16_t length = characteristic->getDataLen();
    if (length == 0 || length > RADIO_BLE_WRITE_MAX) {
        ++g_rx_queue_overflow_count;
        return;
    }

    BleWriteEvent event{};
    event.conn_id = conn_id;
    event.link_epoch = g_ble_link_epoch;
    event.overflow_epoch = g_rx_queue_overflow_count;
    event.length = characteristic->getData(event.data, length);
    if (event.length == 0 || event.length > RADIO_BLE_WRITE_MAX ||
        xQueueSend(g_event_queue, &event, 0) != pdTRUE) {
        ++g_rx_queue_overflow_count;
    }
}

void bleCccdCallback(BLECharacteristic*, uint8_t conn_id, uint16_t ccc_bits) {
    g_tx_notify_conn_id = conn_id;
    g_tx_notify_enabled = (ccc_bits & GATT_CLIENT_CHAR_CONFIG_NOTIFY) != 0;
    g_tx_notify_epoch = g_ble_link_epoch;
}

void serviceOwner() {
        // Link changes revoke the previous session before commands are allowed
        // to confirm pairing or queued traffic can be sent to a reused conn_id.
        if (g_ble_stack_ready) processConnection();

        RadioCommand command{};
        if (g_command_queue && xQueueReceive(g_command_queue, &command, 0) == pdTRUE) {
            if (command.kind == COMMAND_START) processStart(command);
            else if (command.kind == COMMAND_STOP) processStop(command);
            else if (command.kind == COMMAND_PAIR_DIAG_BEGIN) processPairDiagnosticBegin();
            else if (command.kind == COMMAND_PAIR_DIAG_CONFIRM_RAM) processPairDiagnosticConfirmRam();
            else if (command.kind == COMMAND_PAIR_DIAG_CANCEL) processPairDiagnosticCancel();
            else if (command.kind == COMMAND_PAIR_DIAG_END) processPairDiagnosticEnd();
            else if (command.kind >= COMMAND_COMPANION_START) processCompanion(command);
        }

        BleWriteEvent write_event{};
        while (g_event_queue && xQueueReceive(g_event_queue, &write_event, 0) == pdTRUE) {
            handleBleWriteEvent(write_event);
        }

        NotifyEvent notify_event{};
        while (g_notify_queue && xQueueReceive(g_notify_queue, &notify_event, 0) == pdTRUE) {
            processNotifyEvent(notify_event);
        }

        if (g_ble_stack_ready) {
            processAuthentication();
            processPairing();
        }
}

}  // namespace

namespace RadioSvc {

void init() {
    if (g_initialized) return;
    g_command_queue = xQueueCreate(RADIO_COMMAND_QUEUE_LENGTH, sizeof(RadioCommand));
    g_event_queue = xQueueCreate(RADIO_EVENT_QUEUE_LENGTH, sizeof(BleWriteEvent));
    g_notify_queue = xQueueCreate(RADIO_NOTIFY_QUEUE_LENGTH, sizeof(NotifyEvent));
    g_uart_queue = xQueueCreate(RADIO_UART_QUEUE_LENGTH, sizeof(UartEvent));
    if (!g_command_queue || !g_event_queue || !g_notify_queue || !g_uart_queue) {
        g_last_result = REJECTED;
        return;
    }
    // init() runs in Arduino setup(); loop() is the same SDK main_task.
    // Callbacks and UART workers only enqueue. SDK work stays in this context.
    g_loop_owner = xTaskGetCurrentTaskHandle();
    g_owner_ready = g_loop_owner != NULL;
    if (!g_owner_ready) { g_last_result = REJECTED; return; }
    g_initialized = true;
}

void initTracing() {
    // Called only after the UART worker and every other startup resource exist.
    // Failure leaves diagnostics disabled without gating radio startup.
    if (!g_trace_queue) g_trace_queue = xQueueCreate(16, sizeof(RadioTraceEvent));
}

Result requestStart(uint16_t request_id) {
    if (!g_owner_ready || !g_command_queue) return REJECTED;
    if (g_state == STARTING || g_state == ADVERTISING ||
        g_state == CONNECTED || g_state == STOPPING) return BUSY;

    RadioCommand command{COMMAND_START, request_id, ++g_generation};
    g_request_id = request_id;
    g_start_deadline_ms = millis() + RADIO_START_TIMEOUT_MS;
    g_last_result = QUEUED;
    setState(STARTING);
    if (xQueueSend(g_command_queue, &command, 0) != pdTRUE) {
        g_last_result = REJECTED;
        setState(IDLE);
        return REJECTED;
    }
    publishProtocolStatus("QUEUED", request_id);
    return QUEUED;
}

Result requestStop(uint16_t request_id) {
    if (!g_owner_ready || !g_command_queue) return REJECTED;
    if (g_state == IDLE || g_state == FAILED || g_state == TIMEOUT) {
        clearConnectionState();
        return STOPPED_RESULT;
    }

    RadioCommand command{COMMAND_STOP, request_id, ++g_generation};
    g_request_id = request_id;
    g_stop_deadline_ms = millis() + RADIO_STOP_TIMEOUT_MS;
    g_last_result = QUEUED;
    setState(STOPPING);
    if (xQueueSend(g_command_queue, &command, 0) != pdTRUE) {
        g_last_result = REJECTED;
        setState(IDLE);
        clearConnectionState();
        return REJECTED;
    }
    publishProtocolStatus("QUEUED", request_id);
    return QUEUED;
}

Result enqueuePairDiagnosticCommand(CommandKind kind) {
    if (!g_owner_ready || !g_command_queue) return REJECTED;
    RadioCommand command{kind, 0, g_generation};
    if (xQueueSend(g_command_queue, &command, 0) != pdTRUE) return REJECTED;
    return QUEUED;
}

Result beginPairDiagnostic() {
    return enqueuePairDiagnosticCommand(COMMAND_PAIR_DIAG_BEGIN);
}

Result confirmPairInRam() {
    return enqueuePairDiagnosticCommand(COMMAND_PAIR_DIAG_CONFIRM_RAM);
}

Result cancelPairDiagnostic() {
    return enqueuePairDiagnosticCommand(COMMAND_PAIR_DIAG_CANCEL);
}

Result endPairDiagnostic() {
    return enqueuePairDiagnosticCommand(COMMAND_PAIR_DIAG_END);
}

Result confirmModePair(uint16_t session_id, const char* action, uint32_t pair_seq) {
    if (!g_loop_owner || xTaskGetCurrentTaskHandle() != g_loop_owner) return REJECTED;
    const bool allow = action && strcmp(action, "CONFIRM") == 0;
    const bool deny = action && strcmp(action, "CANCEL") == 0;
    if (!g_mode_ram_pairing || current_mode != MODE_BLE || !session_id ||
            session_id != g_mode_pair_session || !pair_seq ||
            pair_seq != g_pending_pair_sequence || (!allow && !deny) ||
            !g_pair_diagnostic_active || g_state != CONNECTED ||
            g_pending_pair_token.length() == 0 || g_pair_conn_id != g_conn_id ||
            !BLE.connected(g_conn_id)) {
        if (session_id == g_mode_pair_session && pair_seq)
            publishModePairStatus("PAIR_REJECTED", pair_seq);
        return REJECTED;
    }
    if (pairDeadlineExpired(millis(), g_pair_deadline_ms)) {
        clearPendingPair();
        ble_is_authenticated = false;
        g_pair_diagnostic_state = PAIR_DIAG_TIMEOUT;
        publishModePairStatus("PAIR_TIMEOUT", pair_seq);
        ownerNotify("{\"t\":\"sys/ble\",\"v\":\"PAIR_TIMEOUT\"}", g_conn_id);
        return TIMEOUT_RESULT;
    }
    if (allow) processPairDiagnosticConfirmRam();
    else processPairDiagnosticCancel();
    publishModePairStatus(allow ? "AUTHENTICATED" : "PAIR_CANCELLED", pair_seq);
    return allow ? ACQUIRED : STOPPED_RESULT;
}

Result enqueueNotify(const char* payload, uint8_t conn_id) {
    if (!g_owner_ready || !g_notify_queue || !payload) return REJECTED;
    const size_t length = strlen(payload);
    if (length == 0 || length > RADIO_NOTIFY_MAX) return REJECTED;

    NotifyEvent event{};
    event.conn_id = conn_id;
    event.length = (uint16_t)length;
    event.link_epoch = g_ble_link_epoch;
    memcpy(event.payload, payload, length + 1);
    if (xQueueSend(g_notify_queue, &event, 0) != pdTRUE) return REJECTED;
    return QUEUED;
}

Result enqueueCompanion(uint16_t session_id, const char* action, uint32_t pair_seq) {
    if (!session_id) return REJECTED;
    if (!action || !g_owner_ready || !g_command_queue) { publishCompanionStatus("REJECTED", session_id); return REJECTED; }
    CommandKind kind;
    if (!strcmp(action, "START")) kind = COMMAND_COMPANION_START;
    else if (!strcmp(action, "ATTACH")) kind = COMMAND_COMPANION_ATTACH;
    else if (!strcmp(action, "STOP")) kind = COMMAND_COMPANION_STOP;
    else if (!strcmp(action, "CONFIRM")) kind = COMMAND_COMPANION_CONFIRM;
    else if (!strcmp(action, "CANCEL")) kind = COMMAND_COMPANION_CANCEL;
    else { publishCompanionStatus("REJECTED", session_id); return REJECTED; }
    const bool pairing_action = kind == COMMAND_COMPANION_CONFIRM || kind == COMMAND_COMPANION_CANCEL;
    if ((pairing_action && pair_seq == 0) || (!pairing_action && pair_seq != 0)) {
        publishCompanionStatus(pairing_action ? "PAIR_REJECTED" : "REJECTED", session_id); return REJECTED;
    }
    uint32_t generation = g_generation;
    if (kind == COMMAND_COMPANION_START) ++generation;
    RadioCommand command{kind, session_id, generation, pair_seq};
    if (xQueueSend(g_command_queue, &command, 0) != pdTRUE) {
#if CRABIT_BW16_DIAG_ATTACH_TRACE
        if (kind == COMMAND_COMPANION_ATTACH) enqueueRadioTrace(TRACE_CMD_REJECTED, session_id);
#endif
        publishCompanionStatus("REJECTED", session_id); return REJECTED;
    }
#if CRABIT_BW16_DIAG_ATTACH_TRACE
    if (kind == COMMAND_COMPANION_ATTACH) enqueueRadioTrace(TRACE_CMD_QUEUED, session_id);
#endif
    publishCompanionStatus("QUEUED", session_id);
    return QUEUED;
}

void traceCompanionReceived(uint16_t request_id) {
    enqueueRadioTrace(TRACE_CMD_RX, request_id);
}

bool companionActive() { return g_companion_active; }
bool companionControlsMode() { return g_companion_mode_owned; }


Snapshot snapshot() {
    Snapshot result{};
    result.state = g_state;
    result.last_result = g_last_result;
    result.generation = g_generation;
    result.state_since_ms = g_state_since_ms;
    result.request_id = g_request_id;
    result.conn_id = g_conn_id;
    result.authenticated = ble_is_authenticated;
    result.initialized = g_initialized;
    result.pair_diagnostic_state = g_pair_diagnostic_state;
    result.pair_rx_length = g_pair_rx_length;
    result.pair_diagnostic_active = g_pair_diagnostic_active;
    result.ram_pairing_only = g_mode_ram_pairing;
    return result;
}

void flushUartEvents() {
    if (!g_uart_queue) return;
    UartEvent event{};
    while (xQueueReceive(g_uart_queue, &event, 0) == pdTRUE) {
        event.payload[event.length < sizeof(event.payload) ? event.length : sizeof(event.payload) - 1] = '\0';
        if (event.kind == UART_STATUS) {
            Protocol::send_message("sys/ble", event.payload, event.request_id);
        } else if (event.kind == UART_COMPANION_STATUS) {
            const bool attach_reply = strcmp(event.payload, "ATTACHED") == 0;
            if (attach_reply) attachDiagnostic("ATTACH_REPLY_BEGIN", event.request_id);
            const bool sent = Protocol::send_message_checked("sys/comm", event.payload, event.request_id);
            if (attach_reply) attachDiagnostic(sent ? "ATTACH_REPLY_SENT" : "ATTACH_REPLY_FAIL", event.request_id);
        } else if (event.kind == UART_FORWARD) {
            Protocol::send_to_stm32(event.payload);
        }
    }
    RadioTraceEvent trace{};
    for (unsigned i = 0; i < 4 && g_trace_queue &&
            xQueueReceive(g_trace_queue, &trace, 0) == pdTRUE; ++i) {
        serviceRadioTrace(trace);
    }
    taskENTER_CRITICAL();
    const bool overflow = g_trace_overflow;
    g_trace_overflow = false;
    taskEXIT_CRITICAL();
    if (overflow) Protocol::send_message("diag/radio", "TRACE_DROPPED", 0);
}

void service() {
    if (!g_owner_ready || xTaskGetCurrentTaskHandle() != g_loop_owner) return;
    // Send QUEUED/ingress markers before any potentially blocking SDK call.
    flushUartEvents();
    serviceOwner();
    flushUartEvents();
}

void tick() {
    const uint32_t now = millis();
    if (g_state == STARTING && (int32_t)(now - g_start_deadline_ms) >= 0) {
        ++g_generation;
        g_last_result = TIMEOUT_RESULT;
        clearConnectionState();
        setState(TIMEOUT);
        publishBleStatus("TIMEOUT", g_request_id);
        if (g_companion_active) publishCompanionStatus("FAILED", g_companion_session);
    } else if (g_state == STOPPING && (int32_t)(now - g_stop_deadline_ms) >= 0) {
        ++g_generation;
        clearConnectionState();
        g_last_result = STOPPED_RESULT;
        setState(IDLE);
        publishProtocolStatus("STOPPED", g_request_id);
    }
}

}  // namespace RadioSvc
