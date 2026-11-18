#include "board.h"

#include "FreeRTOS.h"
#include "task.h"

#include "NanoUI/NanoUI.h"
#include "app_real/app_clock.h"
#include "app_real/app_coproc.h"
#include "app_real/app_cyber_link.h"
#include "app_real/components/ble_controls.h"
#include "app_real/components/companion_pair_prompt.h"
#include "app_real/launcher.h"
#include "app_services.h"
#include "axp2101.h"
#include "comms_link.h"
#include "companion_ble.h"
#include "display.h"
#include "event_bus.h"
#include "flash_store.h"
#include "global_store.h"
#include "health.h"
#include "hw_input.h"
#include "hw_sensors.h"
#include "i2c_bus.h"
#include "main.h"
#include "nanoui_port.h"
#include "sensor_task.h"
#include "sys_config.h"
#include "sys_power.h"
#include "topics.h"
#include "uart_task.h"
#include "usb.h"
#if CRABIT_QMI_RUN_PROBE
#include "app_real/app_qmi_run_probe.h"
#include "qmi_run.h"
#endif

extern QMI8658_Driver imu_drv;
extern Battery_Driver batt_drv;
extern PAT9125_Driver pat_drv;
extern Buzzer_Driver buzzer_drv;
extern BMP280_Driver bmp_drv;
extern AXP2101_Driver axp_drv;
extern STM32Canvas oled;
extern STM32Input keys;
extern WindowManager wm;

static void markUiDirty() {
    if (WindowManager::instance) WindowManager::instance->markDirty();
}

namespace {
// Composition-root ports for Crabit::CompanionBle. Its session state machine
// no longer names concrete App, Popup or pair-prompt types; only these
// effects live here. (Its include set can still reach NanoUI indirectly, so
// this is a dependency cut, not an include-closure guarantee.) The prompt is
// the single boot-time instance that used to live in companion_ble.cpp:
// Popup borrows its configuration tree while shown, so it must outlive every
// popup and is never rebuilt per tick.
Crabit::CompanionPairPrompt pair_prompt(Crabit::CompanionBle::confirm,
                                        Crabit::CompanionBle::cancel);

void launchBleBackground() {
    AppManager::launch(new CyberLinkApp(CyberLinkApp::Mode::BleBackground));
}
void showBleToast(const char* message) { Popup::showToast(message); }
void dismissPairPrompt() { pair_prompt.dismiss(); }
void invalidatePairPrompt(uint16_t session, uint32_t pair) {
    pair_prompt.invalidate(session, pair);
}
void tickPairPrompt(uint16_t session, uint32_t pair) { pair_prompt.tick(session, pair); }

Crabit::CompanionBle::UiHooks companionUiHooks() {
    return {&AppManager::frameAppActive, &launchBleBackground, &showBleToast,
            &dismissPairPrompt, &invalidatePairPrompt, &tickPairPrompt};
}
Crabit::CompanionBle::HardwareHooks companionHardwareHooks() {
    return {&BW16_NormalBoot, &BW16_ForceOff};
}
// Installs callbacks only: no boot, display, app launch or prompt work happens
// here. A missing port set must fail the boot instead of running BLE inert.
bool bindCompanionPorts() {
    return Crabit::CompanionBle::bind(companionUiHooks(), companionHardwareHooks());
}
}  // namespace

void Board::init() {
    const bool usb_log_ready = Usb::initLog();
    const bool i2c_bus_ready = I2cBus::init();
    if (!usb_log_ready || !i2c_bus_ready)
        Fault::reset(Fault::Reason::Boot);

    NanoUI_InitAllocator();

    if (!FlashStore::init()) {
        USB_LOG("[FLASH] store mutex allocation failed\r\n");
        Fault::reset(Fault::Reason::Boot);
    }
    Health::init();

    // Composition root, before any input/service/UI loop can run: the physical
    // BLE shortcut and the companion ports are installed first. Binding only
    // stores callbacks; a rejected port set must not boot BLE silently.
    keys.setBleShortcutHandler(Crabit::toggleBleFromUi);
    if (!bindCompanionPorts())
        Fault::reset(Fault::Reason::Boot);

    ConfigManager::load();
    GlobalStore.sys_volume = CrabitPreferences::current.volume;

    if (AXP2101_Init() != HAL_OK || AXP2101_SetDC1VoltageChecked(3300) != HAL_OK) {
        USB_LOG("[PWR] boot DC1 configuration check failed; UI startup aborted\r\n");
        Fault::reset(Fault::Reason::I2c);
    }
    HAL_Time::init([]() -> uint32_t { return HAL_GetTick(); },
                   [](uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); });

    // Boot default: keep BW16 powered off until a deliberate wake-up path is chosen.
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(100));

    keys.init();

    // Registration owns sensor initialization; keep the hardware startup order.
    auto& sensors = NanoUI::SensorManager::getInstance();
    sensors.registerSensor(&imu_drv);
    USB_LOG("[QMI] tap config=%s; screen-only gesture wake\r\n",
            imu_drv.tapReady() ? "OK" : "FAILED");
    sensors.registerSensor(&batt_drv);
    sensors.registerSensor(&pat_drv);
    sensors.registerSensor(&buzzer_drv);
    sensors.registerSensor(&bmp_drv);
    if (!AXP2101_InitFuelGauge()) {
        USB_LOG("[PWR] fuel gauge setup failed; percentage unavailable\r\n");
    }
    sensors.registerSensor(&axp_drv);

    AppManager::init(&oled, &keys);
    Navigator::init(&wm);
    if (!EventBus::init()) {
        USB_LOG("[RTOS] event bus allocation failed\r\n");
        Fault::reset(Fault::Reason::Task);
    }
    const Fault::Reason boot_reason = Fault::lastBoot();
    if (boot_reason != Fault::Reason::None) {
        char reason[8];
        snprintf(reason, sizeof(reason), "%u", static_cast<unsigned>(boot_reason));
        CrabitLink::sendBestEffort(Topics::DiagBoot, reason);
        USB_LOG("[BOOT] reset reason=%s\r\n", reason);
    }
    if (!Crabit::initFeatures())
        USB_LOG("[RTOS] a feature failed to initialise\r\n");
    AppServices::init(&wm);
    EventBus::setSessionStatusHandler(Crabit::CompanionBle::receive);
    EventBus::setDirtyHandler(markUiDirty);
    EventBus::subscribe(Topics::SysNotify, Crabit::CompanionBle::observeProbePong);
    SysPower::init();

    // Crabit_Start has initialized and blanked the panel. Apply UI settings only.
    Display::setSleep(false);
    Display::writeByte(0x81, 0);
    Display::writeByte(static_cast<uint8_t>(sys_cfg.brightness * 255.0f), 0);
    Display::setInvert(sys_cfg.invert_disp);

    if (!Uart::init()) {
        USB_LOG("[RTOS] communication resource allocation failed\r\n");
        Fault::reset(Fault::Reason::Uart);
    }
    Uart::startRx();

    vTaskDelay(pdMS_TO_TICKS(500));
    if (sys_cfg.enable_ble && Crabit::CompanionBle::legacyAutostartAllowed()) {
        USB_LOG("🚀 [SYS] Wake up BW16 -> BLE Mode\r\n");
        BW16_NormalBoot();
        vTaskDelay(pdMS_TO_TICKS(650));
        CrabitLink::send(Topics::SysMode, "BLE");
        Crabit::CompanionBle::registerLegacyAutostart();
    } else {
        CrabitLink::send(Topics::SysMode, "EXIT");
    }

    // Normal product root; STOP2 metrics remain available in System Info.
#if CRABIT_QMI_RUN_PROBE
    AppManager::launch(new QmiRunProbeApp());
#else
    AppManager::launch(new ClockApp());
#endif
}

void Board::startTasks() {
    TaskHandle_t modem_task = NULL;
    TaskHandle_t tx_task = NULL;
    TaskHandle_t sensor_task = NULL;
    BaseType_t tasks_ok = xTaskCreate(Task_Modem, "Modem", 512, NULL, 3, &modem_task);
    tasks_ok &= xTaskCreate(Task_TX_Worker, "TX_Worker", 512, NULL, 3, &tx_task);
    tasks_ok &= xTaskCreate(Task_SensorHub, "Sensors", 1024, NULL, 3, &sensor_task);
    if (tasks_ok != pdPASS) {
        USB_LOG("[RTOS] background task creation failed\r\n");
        if (modem_task) vTaskDelete(modem_task);
        if (tx_task) vTaskDelete(tx_task);
        if (sensor_task) vTaskDelete(sensor_task);
        Fault::reset(Fault::Reason::Task);
    }
    Health::start();
}
