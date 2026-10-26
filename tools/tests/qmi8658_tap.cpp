#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

enum HAL_StatusTypeDef { HAL_OK = 0, HAL_ERROR = 1 };

struct I2C_HandleTypeDef {};
struct GPIO_TypeDef {};
struct GPIO_InitTypeDef {
    uint32_t Pin = 0;
    uint32_t Mode = 0;
    uint32_t Pull = 0;
};

constexpr uint32_t GPIO_PIN_0 = 1u << 0;
constexpr uint32_t GPIO_PIN_1 = 1u << 1;
constexpr uint32_t GPIO_MODE_IT_RISING = 1;
constexpr uint32_t GPIO_PULLDOWN = 1;
extern GPIO_TypeDef fake_gpio_b;
#define GPIOB (&fake_gpio_b)

namespace NanoUI {
struct SensorData {
    uint32_t timestamp = 0;
    bool is_valid = false;
};

struct SensorDataIMU : SensorData {
    float ax = 0;
    float ay = 0;
    float az = 0;
    float gx = 0;
    float gy = 0;
    float gz = 0;
    uint32_t int1_count = 0;
    uint32_t int2_count = 0;
};

class Sensor {
   public:
    explicit Sensor(uint32_t) {}
    virtual ~Sensor() = default;
    virtual const char* getName() = 0;
    virtual void init() = 0;
    virtual void update() = 0;
    virtual const void* getData() = 0;
};
}  // namespace NanoUI

extern I2C_HandleTypeDef hi2c1;
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef*, uint16_t, uint16_t, uint16_t, uint8_t*,
                                    uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef*, uint16_t, uint16_t, uint16_t, uint8_t*,
                                   uint16_t, uint32_t);
void HAL_GPIO_Init(GPIO_TypeDef*, GPIO_InitTypeDef*);
void HAL_Delay(uint32_t);
uint32_t HAL_GetTick();

#include "qmi_run_profile.h"
#include "sensor_stop2.h"

namespace I2cDiag {
enum Device : uint32_t { Qmi };
inline std::atomic<bool> paused{false};
class Sample {
   public:
    Sample(Device, uint32_t) {}
};
}  // namespace I2cDiag

#include "qmi8658_tap_staged.hpp"

GPIO_TypeDef fake_gpio_b{};
I2C_HandleTypeDef hi2c1{};
volatile uint32_t g_qmi_int1_cnt = 0;
volatile uint32_t g_qmi_int2_cnt = 0;
volatile uint32_t g_qmi_tap_cnt = 0;
volatile uint32_t g_qmi_tap_tick = 0;
volatile uint32_t g_pat_int_cnt = 0;

namespace fake {
enum class Ctrl9Mode { Complete, NeverAssert, NeverClear };

struct Write {
    uint8_t reg;
    std::vector<uint8_t> bytes;
    bool ok;
};

struct Read {
    uint8_t reg;
    uint16_t size;
    bool ok;
    uint8_t first = 0;
};

std::array<uint8_t, 256> registers{};
std::vector<Write> writes;
std::vector<Read> reads;
bool device_present = true;
uint8_t status1 = 0;
uint8_t tap_status = 0;
uint32_t now = 0;
uint32_t delay_total = 0;
uint32_t delay_1ms = 0;
uint32_t gpio_init_calls = 0;
int fail_write_reg = -1;
int fail_write_count = 0;
int fail_read_reg = -1;
int fail_read_count = 0;
int override_read_reg = -1;
uint8_t override_read_value = 0;
Ctrl9Mode ctrl9_mode = Ctrl9Mode::Complete;
bool ctrl9_started = false;
bool ctrl9_clear_phase = false;

void reset() {
    registers.fill(0);
    writes.clear();
    reads.clear();
    device_present = true;
    status1 = 0;
    tap_status = 0;
    now = 0;
    delay_total = 0;
    delay_1ms = 0;
    gpio_init_calls = 0;
    fail_write_reg = -1;
    fail_write_count = 0;
    fail_read_reg = -1;
    fail_read_count = 0;
    override_read_reg = -1;
    override_read_value = 0;
    ctrl9_mode = Ctrl9Mode::Complete;
    ctrl9_started = false;
    ctrl9_clear_phase = false;
    I2cDiag::paused.store(false, std::memory_order_relaxed);
    SensorStop2::restoreFault.store(false, std::memory_order_relaxed);
    g_qmi_int1_cnt = 0;
    g_qmi_int2_cnt = 0;
    g_qmi_tap_cnt = 0;
    g_qmi_tap_tick = 0;
    g_pat_int_cnt = 0;
}

void failWriteOnce(uint8_t reg) {
    fail_write_reg = reg;
    fail_write_count = 1;
}

void failReadOnce(uint8_t reg) {
    fail_read_reg = reg;
    fail_read_count = 1;
}

void overrideReadOnce(uint8_t reg, uint8_t value) {
    override_read_reg = reg;
    override_read_value = value;
}

const Write* writeAt(uint8_t reg, size_t occurrence) {
    for (const auto& write : writes) {
        if (write.ok && write.reg == reg) {
            if (occurrence == 0)
                return &write;
            --occurrence;
        }
    }
    return nullptr;
}

std::vector<uint8_t> successfulWriteBytes(uint8_t reg) {
    std::vector<uint8_t> values;
    for (const auto& write : writes) {
        if (write.ok && write.reg == reg && write.bytes.size() == 1)
            values.push_back(write.bytes[0]);
    }
    return values;
}

uint32_t readCount(uint8_t reg) {
    uint32_t result = 0;
    for (const auto& read : reads) {
        if (read.reg == reg)
            ++result;
    }
    return result;
}

uint32_t failedWriteCount(uint8_t reg) {
    uint32_t result = 0;
    for (const auto& write : writes) {
        if (!write.ok && write.reg == reg)
            ++result;
    }
    return result;
}

std::vector<uint8_t> successfulReadFirstBytes(uint8_t reg) {
    std::vector<uint8_t> values;
    for (const auto& read : reads) {
        if (read.ok && read.reg == reg)
            values.push_back(read.first);
    }
    return values;
}

void setAccelerationRaw(int16_t x, int16_t y, int16_t z) {
    const int16_t samples[] = {x, y, z};
    for (size_t axis = 0; axis < 3; ++axis) {
        const uint16_t bits = static_cast<uint16_t>(samples[axis]);
        registers[0x35 + axis * 2] = static_cast<uint8_t>(bits & 0xFF);
        registers[0x36 + axis * 2] = static_cast<uint8_t>(bits >> 8);
    }
}

bool hasWrite(uint8_t reg, uint8_t value) {
    for (const auto& write : writes) {
        if (write.ok && write.reg == reg && write.bytes.size() == 1 && write.bytes[0] == value)
            return true;
    }
    return false;
}
}  // namespace fake

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef*, uint16_t address, uint16_t reg, uint16_t,
                                    uint8_t* data, uint16_t size, uint32_t) {
    fake::Write write{static_cast<uint8_t>(reg), {}, false};
    if (data != nullptr)
        write.bytes.assign(data, data + size);

    if (!fake::device_present || address != 0xD4 || data == nullptr || size == 0) {
        fake::writes.push_back(std::move(write));
        return HAL_ERROR;
    }

    if (fake::fail_write_count > 0 && fake::fail_write_reg == reg) {
        --fake::fail_write_count;
        fake::writes.push_back(std::move(write));
        return HAL_ERROR;
    }

    write.ok = true;
    fake::writes.emplace_back(write);
    for (uint16_t i = 0; i < size; ++i)
        fake::registers[static_cast<uint8_t>(reg + i)] = data[i];

    if (reg == 0x0A) {
        if (data[0] != 0) {
            fake::ctrl9_started = true;
            fake::ctrl9_clear_phase = false;
        } else if (fake::ctrl9_started) {
            fake::ctrl9_clear_phase = true;
        }
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef*, uint16_t address, uint16_t reg, uint16_t,
                                   uint8_t* data, uint16_t size, uint32_t) {
    if (data == nullptr || size == 0)
        return HAL_ERROR;
    if (!fake::device_present || address != 0xD4) {
        fake::reads.push_back({static_cast<uint8_t>(reg), size, false, 0});
        return HAL_ERROR;
    }
    if (fake::fail_read_count > 0 && fake::fail_read_reg == reg) {
        --fake::fail_read_count;
        fake::reads.push_back({static_cast<uint8_t>(reg), size, false, 0});
        return HAL_ERROR;
    }

    for (uint16_t i = 0; i < size; ++i) {
        const uint8_t current = static_cast<uint8_t>(reg + i);
        uint8_t value = fake::registers[current];
        if (current == 0x00)
            value = 0x05;
        if (current == 0x2D && fake::ctrl9_started) {
            switch (fake::ctrl9_mode) {
                case fake::Ctrl9Mode::Complete:
                    value = fake::ctrl9_clear_phase ? 0 : 0x80;
                    break;
                case fake::Ctrl9Mode::NeverAssert:
                    value = 0;
                    break;
                case fake::Ctrl9Mode::NeverClear:
                    value = 0x80;
                    break;
            }
        }
        if (current == 0x2F)
            value = fake::status1;
        if (current == 0x59)
            value = fake::tap_status;
        if (fake::override_read_reg == current) {
            value = fake::override_read_value;
            fake::override_read_reg = -1;
        }
        data[i] = value;
    }
    fake::reads.push_back({static_cast<uint8_t>(reg), size, true, data[0]});
    return HAL_OK;
}

void HAL_GPIO_Init(GPIO_TypeDef*, GPIO_InitTypeDef*) {
    ++fake::gpio_init_calls;
}

void HAL_Delay(uint32_t ms) {
    fake::delay_total += ms;
    if (ms == 1)
        ++fake::delay_1ms;
    fake::now += ms;
}

uint32_t HAL_GetTick() {
    return fake::now;
}

#define CHECK(condition)                                                                      \
    do {                                                                                      \
        if (!(condition)) {                                                                   \
            std::cerr << __func__ << ": check failed at line " << __LINE__ << ": " #condition \
                      << '\n';                                                                \
            return false;                                                                     \
        }                                                                                     \
    } while (false)

bool initRegisterSequenceAndBytes() {
    fake::reset();
    CHECK(QmiRunProfile::SampleHz == 250);
    CHECK(QmiRunProfile::Ctrl2 == 0x25);
    const std::array<uint8_t, 8> expected_timing = {9, 4, 23, 0, 112, 0, 0, 1};
    const std::array<uint8_t, 8> expected_threshold = {29, 88, 0x20, 3, 0x90, 1, 0, 2};
    CHECK(QmiRunProfile::TapTiming == expected_timing);
    CHECK(QmiRunProfile::TapThreshold == expected_threshold);
    QMI8658_Driver driver;
    driver.init();
    CHECK(driver.tapReady());

    const std::vector<uint8_t> expected_order = {
        0x60, 0x02, 0x08, 0x09, 0x03, 0x0B, 0x0A, 0x0A, 0x0B, 0x0A, 0x0A, 0x09, 0x03, 0x08,
    };
    std::vector<uint8_t> actual_order;
    for (const auto& write : fake::writes) {
        if (write.ok)
            actual_order.push_back(write.reg);
        CHECK(write.reg != 0x0E);
    }
    CHECK(actual_order == expected_order);
    CHECK(fake::successfulWriteBytes(0x0A) == std::vector<uint8_t>({0x0C, 0, 0x0C, 0}));
    std::vector<std::pair<uint8_t, uint8_t>> single_byte_writes;
    for (const auto& write : fake::writes) {
        if (write.ok && write.bytes.size() == 1)
            single_byte_writes.emplace_back(write.reg, write.bytes[0]);
    }
    const std::vector<std::pair<uint8_t, uint8_t>> expected_single_byte_writes = {
        {0x60, 0xB0},
        {0x02, 0x5E},
        {0x08, 0},
        {0x09, 0xC0},
        {0x03, QmiRunProfile::Ctrl2},
        {0x0A, 0x0C},
        {0x0A, 0},
        {0x0A, 0x0C},
        {0x0A, 0},
        {0x09, 0xC1},
        {0x03, QmiRunProfile::Ctrl2},
        {0x08, 0x21},
    };
    CHECK(single_byte_writes == expected_single_byte_writes);

    const fake::Write* timing = fake::writeAt(0x0B, 0);
    const fake::Write* threshold = fake::writeAt(0x0B, 1);
    CHECK(timing != nullptr &&
          timing->bytes == std::vector<uint8_t>(QmiRunProfile::TapTiming.begin(),
                                                QmiRunProfile::TapTiming.end()));
    CHECK(threshold != nullptr &&
          threshold->bytes == std::vector<uint8_t>(QmiRunProfile::TapThreshold.begin(),
                                                   QmiRunProfile::TapThreshold.end()));
    CHECK(fake::readCount(0x2D) == 4);
    CHECK(fake::successfulReadFirstBytes(0x2D) == std::vector<uint8_t>({0x80, 0, 0x80, 0}));
    CHECK(fake::successfulReadFirstBytes(0x09) == std::vector<uint8_t>({0xC1}));
    CHECK(fake::hasWrite(0x09, 0xC0));
    CHECK(fake::hasWrite(0x09, 0xC1));
    CHECK(fake::hasWrite(0x08, 0x21));
    CHECK(fake::successfulWriteBytes(0x03) ==
          std::vector<uint8_t>({QmiRunProfile::Ctrl2, QmiRunProfile::Ctrl2}));
    CHECK(fake::registers[0x03] == QmiRunProfile::Ctrl2 && fake::registers[0x08] == 0x21);
    return true;
}

bool validSingleDoubleAndStatusFiltering() {
    fake::reset();
    QMI8658_Driver driver;
    driver.init();
    CHECK(driver.tapReady());

    g_qmi_int1_cnt = 9;
    g_qmi_int2_cnt = 4;
    fake::status1 = 0x02;
    fake::tap_status = 0x11;  // One tap, X axis.
    fake::now = 1234;
    driver.update();
    CHECK(g_qmi_tap_cnt == 1 && g_qmi_tap_tick == 1234);

    fake::tap_status = 0x21;  // Another valid status, but latch is still high.
    fake::now = 1300;
    driver.update();
    CHECK(g_qmi_tap_cnt == 1 && g_qmi_tap_tick == 1234);

    fake::status1 = 0;
    driver.update();  // Only a successful low status rearms the latch.
    CHECK(g_qmi_tap_cnt == 1);
    const uint32_t tap_status_reads = fake::readCount(0x59);

    fake::status1 = 0x02;
    fake::tap_status = 0x22;  // Two taps, X axis.
    fake::now = 1400;
    driver.update();
    CHECK(g_qmi_tap_cnt == 2 && g_qmi_tap_tick == 1400);
    CHECK(fake::readCount(0x59) == tap_status_reads + 1);

    fake::status1 = 0x01;  // Motion/raw interrupt is not Tap status bit 1.
    ++g_qmi_int1_cnt;
    ++g_qmi_int2_cnt;
    const uint32_t reads_before_non_tap_status = fake::readCount(0x59);
    driver.update();
    CHECK(g_qmi_tap_cnt == 2 && g_qmi_tap_tick == 1400);
    CHECK(fake::readCount(0x59) == reads_before_non_tap_status);
    const auto* data = static_cast<const NanoUI::SensorDataIMU*>(driver.getData());
    CHECK(data->int1_count == 10 && data->int2_count == 5);
    return true;
}

bool invalidNumberAndAxisAreIgnored() {
    fake::reset();
    QMI8658_Driver driver;
    driver.init();
    CHECK(driver.tapReady());

    const uint8_t invalid_statuses[] = {0x10, 0x13, 0x01, 0x02};
    for (const uint8_t status : invalid_statuses) {
        fake::status1 = 0x02;
        fake::tap_status = status;
        driver.update();
        CHECK(g_qmi_tap_cnt == 0);
        fake::status1 = 0;
        driver.update();
    }
    CHECK(fake::readCount(0x59) == 4);
    return true;
}

bool i2cErrorsDoNotCreateTapEvents() {
    fake::reset();
    QMI8658_Driver driver;
    driver.init();
    CHECK(driver.tapReady());
    fake::status1 = 0x02;
    fake::tap_status = 0x11;

    fake::failReadOnce(0x2F);
    driver.update();
    CHECK(g_qmi_tap_cnt == 0);
    CHECK(fake::readCount(0x59) == 0);

    fake::failReadOnce(0x59);
    driver.update();
    CHECK(g_qmi_tap_cnt == 0);

    fake::now = 77;
    driver.update();
    CHECK(g_qmi_tap_cnt == 1 && g_qmi_tap_tick == 77);

    fake::tap_status = 0x22;
    fake::failReadOnce(0x2F);
    driver.update();  // An I2C error is not a successful low/rearm observation.
    CHECK(g_qmi_tap_cnt == 1 && g_qmi_tap_tick == 77);

    fake::status1 = 0;
    driver.update();
    fake::failReadOnce(0x35);
    driver.update();
    CHECK(g_qmi_tap_cnt == 1 && g_qmi_tap_tick == 77);
    const auto* data = static_cast<const NanoUI::SensorDataIMU*>(driver.getData());
    CHECK(!data->is_valid);
    return true;
}

bool ctrl9AckNeverAssertsWithinBound() {
    fake::reset();
    fake::ctrl9_mode = fake::Ctrl9Mode::NeverAssert;
    QMI8658_Driver driver;
    driver.init();
    CHECK(!driver.tapReady());
    CHECK(fake::successfulWriteBytes(0x0A) == std::vector<uint8_t>({0x0C, 0}));
    CHECK(fake::readCount(0x2D) == 50);
    CHECK(fake::delay_1ms == 50);
    CHECK(fake::hasWrite(0x03, QmiRunProfile::Ctrl2) && fake::hasWrite(0x08, 0x21));
    return true;
}

bool ctrl9AckNeverClearsWithinBound() {
    fake::reset();
    fake::ctrl9_mode = fake::Ctrl9Mode::NeverClear;
    QMI8658_Driver driver;
    driver.init();
    CHECK(!driver.tapReady());
    CHECK(fake::successfulWriteBytes(0x0A) == std::vector<uint8_t>({0x0C, 0}));
    CHECK(fake::readCount(0x2D) == 51);  // One asserted read, then 50 clear polls.
    CHECK(fake::delay_1ms == 50);
    return true;
}

bool failedTapWriteKeepsAccelerationUsable() {
    fake::reset();
    fake::setAccelerationRaw(4096, -2048, 1024);
    fake::failWriteOnce(0x0B);
    QMI8658_Driver driver;
    driver.init();
    CHECK(!driver.tapReady());
    CHECK(fake::failedWriteCount(0x0B) == 1);
    CHECK(fake::hasWrite(0x03, QmiRunProfile::Ctrl2) && fake::hasWrite(0x08, 0x21));

    fake::status1 = 0x02;
    fake::tap_status = 0x11;
    driver.update();
    const auto* data = static_cast<const NanoUI::SensorDataIMU*>(driver.getData());
    CHECK(data->is_valid);
    CHECK(data->ax == 1.0f && data->ay == -0.5f && data->az == 0.25f);
    CHECK(g_qmi_tap_cnt == 0 && fake::readCount(0x59) == 0);
    return true;
}

bool failedReadbackKeepsAccelerationUsable() {
    fake::reset();
    fake::setAccelerationRaw(4096, -2048, 1024);
    fake::overrideReadOnce(0x09, 0x00);
    QMI8658_Driver driver;
    driver.init();
    CHECK(!driver.tapReady());
    CHECK(fake::successfulReadFirstBytes(0x09) == std::vector<uint8_t>({0x00}));
    CHECK(fake::hasWrite(0x09, 0xC1));
    CHECK(fake::hasWrite(0x09, 0xC0));
    CHECK(fake::hasWrite(0x03, QmiRunProfile::Ctrl2) && fake::hasWrite(0x08, 0x21));

    driver.update();
    const auto* data = static_cast<const NanoUI::SensorDataIMU*>(driver.getData());
    CHECK(data->is_valid);
    CHECK(data->ax == 1.0f && data->ay == -0.5f && data->az == 0.25f);
    CHECK(g_qmi_tap_cnt == 0);
    return true;
}

bool readbackI2cErrorKeepsAccelerationUsable() {
    fake::reset();
    fake::setAccelerationRaw(4096, -2048, 1024);
    fake::failReadOnce(0x09);
    QMI8658_Driver driver;
    driver.init();
    CHECK(!driver.tapReady());
    CHECK(fake::readCount(0x09) == 1);
    CHECK(fake::hasWrite(0x03, QmiRunProfile::Ctrl2) && fake::hasWrite(0x08, 0x21));

    driver.update();
    const auto* data = static_cast<const NanoUI::SensorDataIMU*>(driver.getData());
    CHECK(data->is_valid);
    CHECK(data->ax == 1.0f && data->ay == -0.5f && data->az == 0.25f);
    CHECK(g_qmi_tap_cnt == 0);
    return true;
}

bool missingDeviceAndReinitResetState() {
    fake::reset();
    QMI8658_Driver driver;
    driver.init();
    CHECK(driver.tapReady());
    fake::status1 = 0x02;
    fake::tap_status = 0x11;
    driver.update();
    CHECK(g_qmi_tap_cnt == 1);

    fake::device_present = false;
    driver.init();
    CHECK(!driver.tapReady());
    auto* data = static_cast<const NanoUI::SensorDataIMU*>(driver.getData());
    CHECK(!data->is_valid);
    const size_t writes_without_device = fake::writes.size();
    const size_t reads_without_device = fake::reads.size();
    driver.update();
    CHECK(fake::writes.size() == writes_without_device);
    CHECK(fake::reads.size() == reads_without_device);

    fake::device_present = true;
    driver.init();
    CHECK(driver.tapReady());
    data = static_cast<const NanoUI::SensorDataIMU*>(driver.getData());
    CHECK(!data->is_valid);
    fake::status1 = 0x02;
    fake::tap_status = 0x12;
    driver.update();
    CHECK(g_qmi_tap_cnt == 2);  // Reinit cleared the previous latched-report state.
    return true;
}

void runScenario(const char* name, bool (*scenario)(), int& failures) {
    if (scenario()) {
        std::cout << "PASS " << name << '\n';
    } else {
        ++failures;
        std::cerr << "FAIL " << name << '\n';
    }
}

int main() {
    int failures = 0;
    runScenario("init register sequence and bytes", initRegisterSequenceAndBytes, failures);
    runScenario("valid single/double taps and status filtering",
                validSingleDoubleAndStatusFiltering, failures);
    runScenario("invalid tap numbers and axes", invalidNumberAndAxisAreIgnored, failures);
    runScenario("I2C errors do not create events", i2cErrorsDoNotCreateTapEvents, failures);
    runScenario("CTRL9 acknowledgement timeout is bounded", ctrl9AckNeverAssertsWithinBound,
                failures);
    runScenario("CTRL9 clear timeout is bounded", ctrl9AckNeverClearsWithinBound, failures);
    runScenario("failed tap write preserves acceleration", failedTapWriteKeepsAccelerationUsable,
                failures);
    runScenario("failed readback preserves acceleration", failedReadbackKeepsAccelerationUsable,
                failures);
    runScenario("readback I2C error preserves acceleration",
                readbackI2cErrorKeepsAccelerationUsable, failures);
    runScenario("missing device and reinit reset state", missingDeviceAndReinitResetState,
                failures);

    if (failures != 0)
        return 1;
    std::cout << "QMI8658 production tap tests PASS (10 scenarios)\n";
    return 0;
}
