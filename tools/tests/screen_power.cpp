#include "sys_power.h"

#include <cstdlib>
#include <iostream>
#include <limits>

GPIO_TypeDef gpioa{};
GPIO_TypeDef gpioh{};
I2C_HandleTypeDef hi2c1{};
UART_HandleTypeDef huart1{};
SPI_HandleTypeDef hspi3{};
USBD_HandleTypeDef hUsbDeviceFS{};
SystemConfig sys_cfg{};
volatile bool g_coproc_active = false;
volatile uint32_t g_qmi_int1_cnt = 0;
volatile uint32_t g_qmi_tap_cnt = 0;
volatile uint32_t g_qmi_tap_tick = 0;
volatile uint32_t g_pat_int_cnt = 0;
PAT9125_Driver pat_drv{};
QMI8658_Driver imu_drv{};
BMP280_Driver bmp_drv{};
WindowManager wm{};
uint8_t rx_pingpong[2][512]{};
volatile uint8_t rx_write_idx = 0;
WindowManager* WindowManager::instance = nullptr;

namespace screen_power_fake {
uint32_t now_ms = 0;
bool pa6_down = false;
HAL_StatusTypeDef checked_voltage_result = HAL_OK;
HAL_StatusTypeDef exit_power_result = HAL_OK;
unsigned int checked_voltage_calls = 0;
std::vector<uint16_t> checked_voltage_requests;
std::vector<std::string> events;
bool sensor_read_available = false;
StmSensorSnapshot sensor_snapshot{};

void reset() {
    now_ms = 0;
    pa6_down = false;
    checked_voltage_result = HAL_OK;
    exit_power_result = HAL_OK;
    checked_voltage_calls = 0;
    checked_voltage_requests.clear();
    events.clear();
    sys_cfg = SystemConfig{};
    g_coproc_active = false;
    g_qmi_int1_cnt = 0;
    g_qmi_tap_cnt = 0;
    g_qmi_tap_tick = 0;
    g_pat_int_cnt = 0;
    clearSensor();
    wm.stack_size = 1;
    WindowManager::instance = &wm;
}

void record(const char* event) {
    events.emplace_back(event);
}

unsigned int count(const char* event) {
    unsigned int result = 0;
    for (const auto& entry : events) {
        if (entry == event)
            ++result;
    }
    return result;
}

int indexOf(const char* event) {
    for (size_t i = 0; i < events.size(); ++i) {
        if (events[i] == event)
            return static_cast<int>(i);
    }
    return -1;
}

void clearSensor() {
    sensor_read_available = false;
    sensor_snapshot = StmSensorSnapshot{};
}

void publishImu(uint32_t timestamp, bool valid, float ax, float ay, float az) {
    sensor_read_available = true;
    sensor_snapshot = StmSensorSnapshot{};
    sensor_snapshot.imu.is_valid = valid;
    sensor_snapshot.imu.timestamp = timestamp;
    sensor_snapshot.imu.ax = ax;
    sensor_snapshot.imu.ay = ay;
    sensor_snapshot.imu.az = az;
}
}  // namespace screen_power_fake

uint32_t HAL_GetTick() {
    return screen_power_fake::now_ms;
}
void HAL_NVIC_DisableIRQ(IRQn_Type) {
}
void HAL_NVIC_ClearPendingIRQ(IRQn_Type) {
}
void HAL_NVIC_SetPriority(IRQn_Type, uint32_t, uint32_t) {
}
void HAL_NVIC_EnableIRQ(IRQn_Type) {
}
void HAL_GPIO_Init(GPIO_TypeDef*, GPIO_InitTypeDef*) {
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint32_t pin) {
    if (port == GPIOA && pin == GPIO_PIN_6) {
        return screen_power_fake::pa6_down ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }
    if (port == GPIOH && pin == GPIO_PIN_3)
        return GPIO_PIN_RESET;
    return GPIO_PIN_SET;
}
void HAL_GPIO_ClearExti(uint32_t) {
}
void HAL_SuspendTick() {
}
void HAL_ResumeTick() {
}
void HAL_PWREx_DisableVddUSB() {
    screen_power_fake::record("vdd_usb_off");
}
void HAL_PWREx_EnterSTOP2Mode(uint32_t) {
    screen_power_fake::record("stop2");
}
void HAL_PWR_ClearFlag(uint32_t) {
}
void HAL_UART_DeInit(UART_HandleTypeDef*) {
    screen_power_fake::record("uart_deinit");
}
void HAL_SPI_DeInit(SPI_HandleTypeDef*) {
    screen_power_fake::record("spi_deinit");
}
void HAL_I2C_DeInit(I2C_HandleTypeDef*) {
    screen_power_fake::record("i2c_deinit");
}
uint8_t USBD_Stop(USBD_HandleTypeDef*) {
    return 0;
}
uint8_t USBD_DeInit(USBD_HandleTypeDef*) {
    return 0;
}
bool STM32_ReadSensorSnapshot(StmSensorSnapshot* snapshot) {
    if (!snapshot || !screen_power_fake::sensor_read_available)
        return false;
    *snapshot = screen_power_fake::sensor_snapshot;
    return true;
}

[[maybe_unused]] HAL_StatusTypeDef AXP2101_SetDC1VoltageChecked(I2C_HandleTypeDef*, uint16_t mv) {
    ++screen_power_fake::checked_voltage_calls;
    screen_power_fake::checked_voltage_requests.push_back(mv);
    screen_power_fake::record(mv == 3300   ? "dc1_checked_3300"
                              : mv == 1800 ? "dc1_checked_1800"
                                           : "dc1_checked_other");
    return screen_power_fake::checked_voltage_result;
}
[[maybe_unused]] HAL_StatusTypeDef AXP2101_EnterLowPower(I2C_HandleTypeDef*) {
    screen_power_fake::record("axp_low_power");
    return HAL_OK;
}
[[maybe_unused]] HAL_StatusTypeDef AXP2101_ExitLowPower(I2C_HandleTypeDef*) {
    screen_power_fake::record("axp_restore");
    return screen_power_fake::exit_power_result;
}
void OLED_SetSleep(bool sleep) {
    screen_power_fake::record(sleep ? "oled_sleep_on" : "oled_sleep_off");
}
void OLED_WR_Byte(uint8_t, uint8_t) {
    screen_power_fake::record("display_write");
}

extern "C" void OLED_Init(void) {
    screen_power_fake::record("oled_init");
}
extern "C" void SystemClock_Config(void) {
    screen_power_fake::record("clock_config");
}
extern "C" void MX_USART1_UART_Init(void) {
    screen_power_fake::record("uart_init");
}
extern "C" void MX_I2C1_Init(void) {
    screen_power_fake::record("i2c_init");
}
extern "C" void MX_SPI3_Init(void) {
    screen_power_fake::record("spi_init");
}
extern "C" void MX_USB_DEVICE_Init(void) {
    screen_power_fake::record("usb_init");
}
extern "C" void STM32_RestartUartRx(void) {
    screen_power_fake::record("uart_rx_restart");
}

void PAT9125_Driver::sleep(bool sleeping) {
    screen_power_fake::record(sleeping ? "pat_sleep" : "pat_wake");
}
void PAT9125_Driver::init() {
    screen_power_fake::record("pat_init");
}
void QMI8658_Driver::sleepForWakeOnMotion() {
    screen_power_fake::record("imu_sleep");
}
void QMI8658_Driver::init() {
    screen_power_fake::record("imu_init");
}
void BMP280_Driver::init() {
    screen_power_fake::record("bmp_init");
}
unsigned int WindowManager::getStackSize() const {
    return stack_size;
}
void WindowManager::markDirty() {
    screen_power_fake::record("window_dirty");
}
void AppManager::exitCurrentApp() {
    screen_power_fake::record("exit_app");
}
bool BadUsbArmory::hasPendingTasks() {
    return false;
}

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << __func__ << ": check failed: " #condition " (line " << __LINE__ << ")\n"; \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

static void begin(uint32_t start, uint32_t screen_off_ms, uint32_t deep_sleep_ms,
                  uint32_t initial_tap_count = 0, uint32_t initial_tap_tick = 0) {
    screen_power_fake::reset();
    screen_power_fake::now_ms = start;
    sys_cfg.screen_off_ms = screen_off_ms;
    sys_cfg.deep_sleep_ms = deep_sleep_ms;
    g_qmi_tap_cnt = initial_tap_count;
    g_qmi_tap_tick = initial_tap_tick;
    SysPower::init();
}

static void publish_in_zone(uint32_t timestamp) {
    screen_power_fake::publishImu(timestamp, true, 0.1f, 1.5f, 0.5f);
}

static void publish_outside_zone(uint32_t timestamp) {
    screen_power_fake::publishImu(timestamp, true, 0.1f, 0.5f, 0.5f);
}

static bool enter_screen_off(uint32_t start, uint32_t screen_off_ms = 100) {
    begin(start, screen_off_ms, 1);
    screen_power_fake::now_ms = static_cast<uint32_t>(start + screen_off_ms);
    SysPower::tick();
    return !SysPower::isActive();
}

static bool test_screen_off_grace_and_no_rail_drop() {
    begin(1000, 100, 1);
    CHECK(!SysPower::canPollGesture());
    screen_power_fake::now_ms = 1099;
    SysPower::tick();
    CHECK(SysPower::isActive());
    CHECK(screen_power_fake::count("oled_sleep_on") == 0);

    screen_power_fake::now_ms = 1100;
    SysPower::tick();
    CHECK(!SysPower::isActive());
    CHECK(screen_power_fake::count("oled_sleep_on") == 1);
    CHECK(SysPower::canPollGesture());
    sys_cfg.enable_lift_wake = false;
    sys_cfg.enable_tap_wake = false;
    CHECK(!SysPower::canPollGesture());
    sys_cfg.enable_lift_wake = true;
    CHECK(SysPower::canPollGesture());

    screen_power_fake::now_ms = 100000;
    SysPower::tick();
    CHECK(!SysPower::isActive());
    CHECK(screen_power_fake::checked_voltage_calls == 0);
    CHECK(screen_power_fake::count("dc1_checked_1800") == 0);
    CHECK(screen_power_fake::count("stop2") == 0);
    CHECK(screen_power_fake::count("axp_low_power") == 0);
    CHECK(screen_power_fake::count("i2c_deinit") == 0);
    return true;
}

static bool test_pa6_wake_preserves_supply_and_display_state() {
    begin(2000, 100, 1);
    screen_power_fake::now_ms = 2100;
    SysPower::tick();
    CHECK(!SysPower::isActive());
    ++g_qmi_int1_cnt;
    screen_power_fake::now_ms = 2101;
    SysPower::tick();
    CHECK(!SysPower::isActive());
    screen_power_fake::pa6_down = true;
    screen_power_fake::now_ms = 2102;
    SysPower::tick();
    CHECK(SysPower::isActive());
    CHECK(screen_power_fake::checked_voltage_calls == 0);
    CHECK(screen_power_fake::count("oled_init") == 0);
    CHECK(screen_power_fake::count("oled_sleep_off") == 1);
    CHECK(screen_power_fake::count("display_write") == 3);
    CHECK(screen_power_fake::count("input_reset") == 1);
    return true;
}

static bool test_screen_only_wake_never_touches_power_restore() {
    begin(3000, 100, 100);
    screen_power_fake::now_ms = 3100;
    SysPower::tick();
    CHECK(!SysPower::isActive());
    screen_power_fake::checked_voltage_result = HAL_ERROR;
    screen_power_fake::exit_power_result = HAL_ERROR;
    screen_power_fake::pa6_down = true;
    screen_power_fake::now_ms = 3101;
    SysPower::tick();
    CHECK(SysPower::isActive());
    CHECK(screen_power_fake::checked_voltage_calls == 0);
    CHECK(screen_power_fake::count("axp_restore") == 0);
    CHECK(screen_power_fake::count("oled_init") == 0);
    CHECK(screen_power_fake::count("oled_sleep_off") == 1);
    return true;
}

static bool test_tick_wrap_preserves_timeout_and_screen_only_policy() {
    constexpr uint32_t start = std::numeric_limits<uint32_t>::max() - 49u;
    begin(start, 100, 1);

    screen_power_fake::now_ms = static_cast<uint32_t>(start + 99u);
    SysPower::tick();
    CHECK(SysPower::isActive());
    CHECK(screen_power_fake::count("oled_sleep_on") == 0);

    screen_power_fake::now_ms = static_cast<uint32_t>(start + 100u);
    SysPower::tick();
    CHECK(!SysPower::isActive());
    CHECK(screen_power_fake::count("oled_sleep_on") == 1);

    screen_power_fake::now_ms = static_cast<uint32_t>(start + 100000u);
    SysPower::tick();
    CHECK(screen_power_fake::count("stop2") == 0);
    CHECK(screen_power_fake::count("dc1_checked_1800") == 0);
    return true;
}

static bool test_zero_screen_off_threshold_keeps_system_quiet() {
    begin(500, 0, 1);
    SysPower::tick();
    CHECK(SysPower::isActive());
    CHECK(screen_power_fake::count("oled_sleep_on") == 0);

    screen_power_fake::now_ms = 60500;
    SysPower::tick();
    screen_power_fake::now_ms = 60501;
    SysPower::tick();
    CHECK(SysPower::isActive());
    CHECK(screen_power_fake::count("oled_sleep_on") == 0);
    CHECK(screen_power_fake::checked_voltage_calls == 0);
    CHECK(screen_power_fake::count("stop2") == 0);
    CHECK(screen_power_fake::count("axp_low_power") == 0);
    return true;
}

static bool test_stationary_viewing_pose_never_arms_lift_wake() {
    CHECK(enter_screen_off(4000));

    for (uint32_t sample_tick : {4600u, 4675u, 4750u}) {
        screen_power_fake::now_ms = sample_tick;
        publish_in_zone(sample_tick);
        SysPower::tick();
        CHECK(!SysPower::isActive());
    }
    CHECK(screen_power_fake::count("oled_sleep_on") == 1);
    CHECK(screen_power_fake::checked_voltage_calls == 0);
    return true;
}

static bool test_lift_wake_requires_exit_and_fresh_continuous_return() {
    CHECK(enter_screen_off(5000));

    screen_power_fake::now_ms = 5600;
    publish_outside_zone(5400);  // IMU age 200 ms is still fresh.
    SysPower::tick();
    CHECK(!SysPower::isActive());

    screen_power_fake::now_ms = 5601;
    publish_in_zone(5601);
    SysPower::tick();
    screen_power_fake::now_ms = 5676;
    publish_in_zone(5601);  // Re-reading one sample cannot accrue hold time.
    SysPower::tick();
    CHECK(!SysPower::isActive());

    screen_power_fake::now_ms = 5701;
    publish_in_zone(5701);  // Exactly 100 ms between distinct samples is valid.
    SysPower::tick();
    CHECK(!SysPower::isActive());
    screen_power_fake::now_ms = 5751;
    publish_in_zone(5751);
    SysPower::tick();
    CHECK(SysPower::isActive());
    CHECK(screen_power_fake::count("dc1_checked_3300") == 0);
    return true;
}

static bool test_lift_hold_resets_after_inter_sample_gap_over_100ms() {
    CHECK(enter_screen_off(6000));
    screen_power_fake::now_ms = 6600;
    publish_outside_zone(6600);
    SysPower::tick();

    screen_power_fake::now_ms = 6601;
    publish_in_zone(6601);
    SysPower::tick();
    screen_power_fake::now_ms = 6702;
    publish_in_zone(6702);  // 101 ms gap restarts the hold.
    SysPower::tick();
    screen_power_fake::now_ms = 6777;
    publish_in_zone(6777);
    SysPower::tick();
    CHECK(!SysPower::isActive());

    screen_power_fake::now_ms = 6852;
    publish_in_zone(6852);
    SysPower::tick();
    CHECK(SysPower::isActive());
    return true;
}

static bool test_stale_outside_sample_does_not_arm_lift() {
    CHECK(enter_screen_off(7000));
    screen_power_fake::now_ms = 7601;
    publish_outside_zone(7400);  // 201 ms old.
    SysPower::tick();
    for (uint32_t sample_tick : {7602u, 7677u, 7752u}) {
        screen_power_fake::now_ms = sample_tick;
        publish_in_zone(sample_tick);
        SysPower::tick();
        CHECK(!SysPower::isActive());
    }

    screen_power_fake::now_ms = 7753;
    publish_outside_zone(7753);
    SysPower::tick();
    screen_power_fake::now_ms = 7754;
    publish_in_zone(7754);
    SysPower::tick();
    screen_power_fake::now_ms = 7829;
    publish_in_zone(7829);
    SysPower::tick();
    screen_power_fake::now_ms = 7904;
    publish_in_zone(7904);
    SysPower::tick();
    CHECK(SysPower::isActive());
    return true;
}

static bool test_invalid_sample_breaks_lift_hold() {
    CHECK(enter_screen_off(8000));
    screen_power_fake::now_ms = 8600;
    publish_outside_zone(8600);
    SysPower::tick();
    screen_power_fake::now_ms = 8601;
    publish_in_zone(8601);
    SysPower::tick();
    screen_power_fake::now_ms = 8676;
    publish_in_zone(8676);
    SysPower::tick();

    screen_power_fake::now_ms = 8701;
    screen_power_fake::publishImu(8701, false, 0.1f, 1.5f, 0.5f);
    SysPower::tick();
    screen_power_fake::now_ms = 8751;
    publish_in_zone(8751);
    SysPower::tick();
    screen_power_fake::now_ms = 8826;
    publish_in_zone(8826);
    SysPower::tick();
    CHECK(!SysPower::isActive());
    screen_power_fake::now_ms = 8901;
    publish_in_zone(8901);
    SysPower::tick();
    CHECK(SysPower::isActive());
    return true;
}

static bool test_nan_sample_breaks_lift_hold() {
    CHECK(enter_screen_off(9000));
    screen_power_fake::now_ms = 9600;
    publish_outside_zone(9600);
    SysPower::tick();
    screen_power_fake::now_ms = 9601;
    publish_in_zone(9601);
    SysPower::tick();
    screen_power_fake::now_ms = 9676;
    publish_in_zone(9676);
    SysPower::tick();

    const float nan = std::numeric_limits<float>::quiet_NaN();
    screen_power_fake::now_ms = 9701;
    screen_power_fake::publishImu(9701, true, nan, 1.5f, 0.5f);
    SysPower::tick();
    screen_power_fake::now_ms = 9751;
    publish_in_zone(9751);
    SysPower::tick();
    screen_power_fake::now_ms = 9826;
    publish_in_zone(9826);
    SysPower::tick();
    CHECK(!SysPower::isActive());
    screen_power_fake::now_ms = 9901;
    publish_in_zone(9901);
    SysPower::tick();
    CHECK(SysPower::isActive());
    return true;
}

static bool test_stale_in_zone_sample_breaks_lift_hold() {
    CHECK(enter_screen_off(10000));
    screen_power_fake::now_ms = 10600;
    publish_outside_zone(10600);
    SysPower::tick();
    screen_power_fake::now_ms = 10601;
    publish_in_zone(10601);
    SysPower::tick();
    screen_power_fake::now_ms = 10676;
    publish_in_zone(10676);
    SysPower::tick();
    screen_power_fake::now_ms = 10677;
    publish_in_zone(10476);  // Stale snapshot interrupts the hold.
    SysPower::tick();

    screen_power_fake::now_ms = 10751;
    publish_in_zone(10751);
    SysPower::tick();
    screen_power_fake::now_ms = 10826;
    publish_in_zone(10826);
    SysPower::tick();
    CHECK(!SysPower::isActive());
    screen_power_fake::now_ms = 10901;
    publish_in_zone(10901);
    SysPower::tick();
    CHECK(SysPower::isActive());
    return true;
}

static bool test_lift_wake_switch_disables_screen_off_lift() {
    CHECK(enter_screen_off(11000));
    sys_cfg.enable_lift_wake = false;
    screen_power_fake::now_ms = 11600;
    publish_outside_zone(11600);
    SysPower::tick();
    for (uint32_t sample_tick : {11601u, 11676u, 11751u}) {
        screen_power_fake::now_ms = sample_tick;
        publish_in_zone(sample_tick);
        SysPower::tick();
    }
    CHECK(!SysPower::isActive());
    CHECK(screen_power_fake::checked_voltage_calls == 0);
    return true;
}

static bool test_raw_qmi_interrupt_count_never_wakes_screen_off() {
    CHECK(enter_screen_off(12000));
    for (uint32_t sample_tick : {12600u, 12700u, 12800u}) {
        ++g_qmi_int1_cnt;
        screen_power_fake::now_ms = sample_tick;
        SysPower::tick();
        CHECK(!SysPower::isActive());
    }
    CHECK(screen_power_fake::checked_voltage_calls == 0);
    return true;
}

static bool test_fresh_typed_tap_at_guard_and_age_limits_wakes() {
    CHECK(enter_screen_off(13000));
    const uint32_t off_tick = 13100;

    screen_power_fake::now_ms = off_tick + 701;
    ++g_qmi_tap_cnt;
    g_qmi_tap_tick = off_tick + 500;  // Guard boundary, but now 201 ms old.
    SysPower::tick();
    CHECK(!SysPower::isActive());

    screen_power_fake::now_ms = off_tick + 702;
    ++g_qmi_tap_cnt;
    g_qmi_tap_tick = off_tick + 502;  // Post-guard event and exactly 200 ms old.
    SysPower::tick();
    CHECK(SysPower::isActive());
    CHECK(screen_power_fake::checked_voltage_requests.empty());
    return true;
}

static bool test_old_and_pre_guard_taps_are_consumed_without_wake() {
    constexpr uint32_t start = 14000;
    begin(start, 100, 1, 9, start);
    screen_power_fake::now_ms = start + 100;
    SysPower::tick();
    CHECK(!SysPower::isActive());

    screen_power_fake::now_ms = start + 599;  // One ms before the guard ends.
    ++g_qmi_tap_cnt;
    g_qmi_tap_tick = screen_power_fake::now_ms;
    SysPower::tick();
    CHECK(!SysPower::isActive());
    screen_power_fake::now_ms = start + 600;
    SysPower::tick();
    CHECK(!SysPower::isActive());
    CHECK(screen_power_fake::checked_voltage_calls == 0);
    return true;
}

static bool test_tap_switch_and_on_screen_tap_activity() {
    begin(15000, 100, 1);
    screen_power_fake::now_ms = 15099;
    ++g_qmi_tap_cnt;
    g_qmi_tap_tick = screen_power_fake::now_ms;
    SysPower::tick();
    CHECK(SysPower::isActive());
    screen_power_fake::now_ms = 15198;
    SysPower::tick();
    CHECK(SysPower::isActive());
    screen_power_fake::now_ms = 15199;
    SysPower::tick();
    CHECK(!SysPower::isActive());

    CHECK(enter_screen_off(16000));
    sys_cfg.enable_tap_wake = false;
    screen_power_fake::now_ms = 16600;
    ++g_qmi_tap_cnt;
    g_qmi_tap_tick = screen_power_fake::now_ms;
    SysPower::tick();
    CHECK(!SysPower::isActive());
    sys_cfg.enable_tap_wake = true;
    screen_power_fake::now_ms = 16601;
    SysPower::tick();  // Disabled event was consumed and cannot replay.
    CHECK(!SysPower::isActive());
    ++g_qmi_tap_cnt;
    g_qmi_tap_tick = screen_power_fake::now_ms;
    SysPower::tick();
    CHECK(SysPower::isActive());
    return true;
}

static bool test_tap_counter_and_event_tick_wrap_are_safe() {
    constexpr uint32_t start = std::numeric_limits<uint32_t>::max() - 599u;
    constexpr uint32_t off_tick = std::numeric_limits<uint32_t>::max() - 499u;
    begin(start, 100, 1, std::numeric_limits<uint32_t>::max(), start);
    screen_power_fake::now_ms = off_tick;
    SysPower::tick();
    CHECK(!SysPower::isActive());

    g_qmi_tap_cnt = 0;                // One typed event after uint32 counter wrap.
    g_qmi_tap_tick = 0;               // Exactly 500 ms after screen-off, across tick wrap.
    screen_power_fake::now_ms = 200;  // Event age is exactly 200 ms.
    SysPower::tick();
    CHECK(SysPower::isActive());
    CHECK(screen_power_fake::checked_voltage_requests.empty());
    return true;
}

int main() {
    if (!test_screen_off_grace_and_no_rail_drop())
        return EXIT_FAILURE;
    if (!test_pa6_wake_preserves_supply_and_display_state())
        return EXIT_FAILURE;
    if (!test_screen_only_wake_never_touches_power_restore())
        return EXIT_FAILURE;
    if (!test_tick_wrap_preserves_timeout_and_screen_only_policy())
        return EXIT_FAILURE;
    if (!test_zero_screen_off_threshold_keeps_system_quiet())
        return EXIT_FAILURE;
    if (!test_stationary_viewing_pose_never_arms_lift_wake())
        return EXIT_FAILURE;
    if (!test_lift_wake_requires_exit_and_fresh_continuous_return())
        return EXIT_FAILURE;
    if (!test_lift_hold_resets_after_inter_sample_gap_over_100ms())
        return EXIT_FAILURE;
    if (!test_stale_outside_sample_does_not_arm_lift())
        return EXIT_FAILURE;
    if (!test_invalid_sample_breaks_lift_hold())
        return EXIT_FAILURE;
    if (!test_nan_sample_breaks_lift_hold())
        return EXIT_FAILURE;
    if (!test_stale_in_zone_sample_breaks_lift_hold())
        return EXIT_FAILURE;
    if (!test_lift_wake_switch_disables_screen_off_lift())
        return EXIT_FAILURE;
    if (!test_raw_qmi_interrupt_count_never_wakes_screen_off())
        return EXIT_FAILURE;
    if (!test_fresh_typed_tap_at_guard_and_age_limits_wakes())
        return EXIT_FAILURE;
    if (!test_old_and_pre_guard_taps_are_consumed_without_wake())
        return EXIT_FAILURE;
    if (!test_tap_switch_and_on_screen_tap_activity())
        return EXIT_FAILURE;
    if (!test_tap_counter_and_event_tick_wrap_are_safe())
        return EXIT_FAILURE;
    std::cout << "SysPower production-header host scenarios: 18 passed\n";
    return EXIT_SUCCESS;
}
