#include "NanoUI_Preflight.h"
#include "NanoUI.h"
#include <SPI.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include "CrabitBuildProfile.h"
#if CRABIT_BW16_ENABLE_OTA
#include <OTA.h>
#include <AmebaMDNS.h>
#endif
#include <WiFi.h>
#include <PowerSave.h>

#include "../AmebaTypes.h"
#include "../config.h"
#include "../Protocol.h"
#include "../AuditRelay.h"
#include "../NetManager.h"
#include "../AmebaStore.h"
#include "../AmebaEventBus.h"
#include "../AmebaServices.h"
#include "drivers/uart_input.h"
#include "drivers/uart_sensors.h"
#include "../app/launcher.h"
#include "../bw16_oled.h"

using namespace NanoUI;

// AmebaEventBus::subs
std::vector<Subscription> AmebaEventBus::subs;

// AmebaStore Physical Storage
float AmebaStore::imu_ax = 0, AmebaStore::imu_ay = 0, AmebaStore::imu_az = 0;
int AmebaStore::opt_dx = 0, AmebaStore::opt_dy = 0;
bool AmebaStore::opt_lifted = false;
int AmebaStore::battery_pct = 0;
uint8_t AmebaStore::raw_btn_mask = 0;
int AmebaStore::current_ui_event = 0;
QueueHandle_t AmebaStore::ui_event_queue = NULL;
QueueHandle_t AmebaServices::command_queue = NULL;
QueueHandle_t AmebaServices::notification_queue = NULL;
QueueHandle_t AmebaServices::sensor_queue = NULL;
QueueHandle_t AmebaServices::diagnostic_queue = NULL;

extern "C" void sys_reset(void); 
volatile BW16Mode current_mode = MODE_WAITING;
uint32_t boot_time = 0;
volatile bool is_display_active = false;
Bw16RawCanvas canvas; 
UartInputDriver input;
UartPAT9125Driver virtual_pat;
WindowManager wm(&canvas, &input);
QueueHandle_t json_msg_queue = NULL;
static bool mode_led_state = false;
static uint32_t mode_led_last_ms = 0;
static uint8_t mode_led_phase = 0;
static BW16Mode mode_led_last_mode = MODE_WAITING;
static uint32_t boot_diag_last_ms = 0;
static uint32_t ui_alive_last_ms = 0;
static volatile uint32_t uart_trace_bytes = 0, uart_trace_frames = 0, uart_trace_bad = 0;

void initModeLed() {
  pinMode(PA26, OUTPUT);
  digitalWrite(PA26, LOW);
  mode_led_state = false;
  mode_led_last_ms = millis();
  mode_led_phase = 0;
  mode_led_last_mode = current_mode;
}

void setModeLed(bool on) {
  mode_led_state = on;
  digitalWrite(PA26, on ? HIGH : LOW);
}

uint8_t getModeLedPulseCount(BW16Mode mode) {
  switch (mode) {
    case MODE_WAITING: return 2;
    case MODE_SLEEP:   return 3;
    case MODE_BLE:     return 4;
    case MODE_WIFI:    return 5;
    case MODE_UI:      return 0;
    case MODE_OTA:     return 0;
    default:           return 2;
  }
}

void sendBootDiag(const char* stage) {
  char msg[96];
  snprintf(
    msg,
    sizeof(msg),
    "%s t=%lu mode=%d ui=%d",
    stage,
    (unsigned long)(millis() - boot_time),
    (int)current_mode,
    is_display_active ? 1 : 0
  );
  Protocol::store_background_msg("boot", msg);
}

void serviceBootDiag() {
  uint32_t now = millis();

  if (current_mode == MODE_UI && is_display_active) {
    if (now - ui_alive_last_ms >= 1000) {
      ui_alive_last_ms = now;
      sendBootDiag("ui_alive");
    }
    return;
  }

  if (current_mode != MODE_WAITING
#if CRABIT_BW16_DIAG_ATTACH_TRACE
      && current_mode != MODE_BLE
#endif
  ) return;

  if (now - boot_diag_last_ms >= 1000) {
    boot_diag_last_ms = now;
    if (current_mode == MODE_BLE) Protocol::send_message("diag/radio", "BLE_ALIVE");
    else sendBootDiag("waiting");
    char counts[64];
    snprintf(counts, sizeof(counts), "UART_RX:%lu,%lu,%lu",
        (unsigned long)uart_trace_bytes, (unsigned long)uart_trace_frames,
        (unsigned long)uart_trace_bad);
    Protocol::send_message("diag/radio", counts);
  }
}

void serviceModeLed() {
  if (mode_led_last_mode != current_mode) {
    mode_led_last_mode = current_mode;
    mode_led_phase = 0;
    mode_led_last_ms = millis();
    setModeLed(false);
  }

  uint32_t now = millis();
  uint8_t pulses = getModeLedPulseCount(current_mode);

  if (current_mode == MODE_UI) {
    if (!mode_led_state || now - mode_led_last_ms >= 400) {
      mode_led_last_ms = now;
      setModeLed(true);
    }
    return;
  }

  if (current_mode == MODE_OTA) {
    if (now - mode_led_last_ms >= 100) {
      mode_led_last_ms = now;
      setModeLed(!mode_led_state);
    }
    return;
  }

  const uint16_t on_ms = 120;
  const uint16_t gap_ms = 200;
  const uint32_t cycle_pause_ms = 1600;

  if (mode_led_phase < pulses * 2) {
    if (!mode_led_state) {
      if (now - mode_led_last_ms >= gap_ms) {
        mode_led_last_ms = now;
        setModeLed(true);
      }
    } else {
      if (now - mode_led_last_ms >= on_ms) {
        mode_led_last_ms = now;
        setModeLed(false);
        mode_led_phase++;
      }
    }
  } else {
    if (now - mode_led_last_ms >= cycle_pause_ms) {
      mode_led_last_ms = now;
      mode_led_phase = 0;
    }
  }
}

void releaseOledBus() {
  SPI.end();
  pinMode(PA12, INPUT); pinMode(PA14, INPUT); pinMode(PB1, INPUT); pinMode(PA15, INPUT); pinMode(PB2, INPUT_PULLUP);
}

#if CRABIT_BW16_ENABLE_OTA
void run_ota_mode_tick() {
    static bool ota_started = false;
    static OTA ota;
    if (ota_started) return;
    ota_started = true;
    sendBootDiag("ota_begin");
    is_display_active = false;
    releaseOledBus();

    // OTA never owns credentials. The caller must establish Wi-Fi through
    // NetManager first; otherwise leave OTA mode rather than blocking forever
    // on placeholder credentials.
    if (WiFi.status() != WL_CONNECTED) {
        Protocol::send_message("sys/notify", "OTA_WIFI_REQUIRED");
        current_mode = MODE_SLEEP;
        return;
    }

    MDNS.begin();
    ota.beginOTA(8082);
    Protocol::send_message("sys/notify", "OTA_READY");
}
#endif

void start_ui_mode() {
    if (!is_display_active) {
        sendBootDiag("ui_init_begin");
        canvas.initHardware(); is_display_active = true;
        sendBootDiag("ui_oled_ready");
        AppManager::launch(new LauncherApp());
        sendBootDiag("ui_launcher_ready");
        ui_alive_last_ms = millis();
    }
}

void Task_UART_Worker(void *pvParameters) {
    (void)pvParameters; uint8_t rx_buf[512]; int rx_ptr = 0;
    while (1) {
        while (Serial.available()) {
            uint8_t c = Serial.read();
            ++uart_trace_bytes;
            if (rx_ptr < (int)sizeof(rx_buf)) rx_buf[rx_ptr++] = c;
            if (rx_ptr >= 2) {
                uint16_t payload_len = (uint16_t)rx_buf[0] | ((uint16_t)rx_buf[1] << 8);
                int expected = (int)payload_len + 4;
                if (payload_len < 5 || expected > (int)sizeof(rx_buf)) { ++uart_trace_bad; rx_ptr = 0; continue; }
                if (rx_ptr == expected) {
                    uint8_t payload[CrabitWire::PAYLOAD_SIZE]; size_t actual = 0;
                    CrabitWire::Message message;
                    if (CrabitWire::decode_frame(rx_buf, rx_ptr, payload, sizeof(payload), &actual) &&
                        CrabitWire::decode_message(payload, actual, &message)) {
                        ++uart_trace_frames;
                        if (strcmp(message.topic, "audit/put") == 0) {
                            AuditRelay::receive(message);
                            rx_ptr = 0;
                            continue;
                        }
                        if (strcmp(message.topic, "ack") == 0 ||
                            strncmp(message.topic, "diag/", 5) == 0 ||
                            strcmp(message.topic, "sys/settings") == 0 ||
                            strcmp(message.topic, "sys/settings/set") == 0 ||
                            strcmp(message.topic, "sys/prefs") == 0 ||
                            strcmp(message.topic, "sys/prefs/set") == 0 ||
                            strcmp(message.topic, "audit/read") == 0 ||
                            strcmp(message.topic, "audit/ack") == 0 ||
                            strcmp(message.topic, "app/usb") == 0 ||
                            strcmp(message.topic, "app/net") == 0 ||
                            strcmp(message.topic, "app/weather") == 0 ||
                            strcmp(message.topic, "app/badusb") == 0) {
                            AmebaServices::enqueueNotification(message);
                        }
                        if (strcmp(message.topic, "sys/mode") == 0 && message.request_id &&
                            (strcmp(message.value, "BLE") == 0 || strcmp(message.value, "WIFI") == 0)) {
                            AmebaServices::enqueueMode(message);
                        } else if (strcmp(message.topic, "sys/bw/pair") == 0) {
                            AmebaServices::enqueueModePair(message);
                        } else if (strcmp(message.topic, "sys/comm") == 0) {
                            RadioSvc::traceCompanionReceived(message.request_id);
                            AmebaServices::enqueueCompanion(message);
                        } else if (strcmp(message.topic, "sys/bw/ping") == 0 && message.request_id &&
                                   strcmp(message.value, "BLE_UART") == 0) {
                            AmebaServices::enqueueUartProbe(message);
                        }
#if CRABIT_BW16_ENABLE_NETWORK_SYNC
                        else if (strncmp(message.topic, "net/", 4) == 0) {
                            NetworkSync::enqueue(message);
                        }
#endif
                        else AmebaEventBus::dispatch(message.topic, message.value);
                    } else {
                        ++uart_trace_bad;
                    }
                    rx_ptr = 0;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void main_setup() {
    Serial.begin(460800); boot_time = millis();
    initModeLed();
    boot_diag_last_ms = boot_time;
    sendBootDiag("setup_begin");
    HAL_Time::init([]() -> uint32_t { return millis(); }, [](uint32_t ms) { delay(ms); });
    AmebaStore::initRuntimeQueues();
    sendBootDiag("hal_ready");
    AmebaServices::init(); input.init(); virtual_pat.init();
    sendBootDiag("services_ready");
    SensorManager::getInstance().registerSensor(&virtual_pat);
    AppManager::init(&canvas, &input);
    sendBootDiag("appmgr_ready");
    if (xTaskCreate(Task_UART_Worker, "UART_TSK", 1024, NULL, 3, NULL) == pdPASS) {
        RadioSvc::initTracing();
        sendBootDiag("uart_ready");
        // Informational only: STM32's existing delayed START remains valid.
        // No wireless SDK or OLED initialization has happened at this point.
        Protocol::send_message("diag/radio", "BOOT_READY");
    } else {
        sendBootDiag("uart_failed");
    }
}

void main_loop() {
    AmebaServices::service();
    serviceBootDiag();
    serviceModeLed();
    NetManager::tickWiFi();
    NetManager::tickPairing();
    if (current_mode == MODE_WAITING) {
        // Await an explicit STM32 mode selection, regardless of boot duration.
        // There is no autonomous wireless/UI start or fallback mode switch.
        delay(10);
    }
    if (current_mode == MODE_UI) {
        AuditRelay::tick();
        if (is_display_active) {
            SensorManager::getInstance().tick(millis());
            AppManager::tick();
            if (!AppManager::frameAppActive()) {
                wm.update();
                wm.drawFrame();
            } else {
                delay(16);
            }
        } else delay(10);
    }
    else if (current_mode == MODE_OTA) {
#if CRABIT_BW16_ENABLE_OTA
        run_ota_mode_tick();
#else
        current_mode = MODE_SLEEP;
#endif
    }
    else delay(10);
}
