// I2C bus, recovery and fault diagnostics. No sensor service code here.
#include "i2c_bus.h"

#include "axp2101.h"
#include "i2c.h"
#include "i2c_recovery_policy.h"
#include "main.h"
#include "wake_log.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <atomic>

// I2C bus, recovery and fault diagnostics.
static SemaphoreHandle_t bus_mutex = NULL;

bool I2cBus::init() {
    bus_mutex = xSemaphoreCreateMutex();
    return bus_mutex != NULL;
}

bool I2cBus::suspend() {
    return HAL_I2C_DeInit(&hi2c1) == HAL_OK;
}

bool I2cBus::resume() {
    MX_I2C1_Init();
    return HAL_I2C_GetState(&hi2c1) == HAL_I2C_STATE_READY;
}

bool I2cBus::ready() {
    return HAL_I2C_GetState(&hi2c1) == HAL_I2C_STATE_READY;
}

uint32_t I2cBus::errorCode() {
    return hi2c1.ErrorCode;
}

extern "C" uint32_t CrabitI2c_Isr(void) {
    return hi2c1.Instance->ISR;
}

extern "C" uint32_t CrabitI2c_Error(void) {
    return hi2c1.ErrorCode;
}

extern "C" uint32_t CrabitI2c_LineLevels(void) {
    return (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_SET ? 2u : 0u) |
           (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_10) == GPIO_PIN_SET ? 1u : 0u);
}

extern "C" bool CrabitI2c_Ready(void) {
    return I2cBus::ready();
}

bool I2cBus::read(uint8_t address, uint8_t reg, uint8_t* data, size_t length,
                  uint32_t timeout_ms) {
    return HAL_I2C_Mem_Read(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT, data,
                            static_cast<uint16_t>(length), timeout_ms) == HAL_OK;
}

bool I2cBus::write(uint8_t address, uint8_t reg, const uint8_t* data, size_t length,
                   uint32_t timeout_ms) {
    return HAL_I2C_Mem_Write(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT,
                             const_cast<uint8_t*>(data), static_cast<uint16_t>(length),
                             timeout_ms) == HAL_OK;
}

extern "C" bool I2cBus_Read(uint8_t address, uint8_t reg, uint8_t* data, uint8_t length) {
    return I2cBus::read(address, reg, data, length);
}

extern "C" bool I2cBus_Write(uint8_t address, uint8_t reg, const uint8_t* data, uint8_t length) {
    return I2cBus::write(address, reg, data, length);
}

extern "C" HAL_StatusTypeDef I2cBus_ReadStatus(uint8_t address, uint8_t reg, uint8_t* data,
                                               uint8_t length) {
    return HAL_I2C_Mem_Read(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT, data, length, 100);
}

extern "C" HAL_StatusTypeDef I2cBus_WriteStatus(uint8_t address, uint8_t reg,
                                                const uint8_t* data, uint8_t length) {
    return HAL_I2C_Mem_Write(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT,
                             const_cast<uint8_t*>(data), length, 100);
}

bool I2cBus::lock(uint32_t timeout_ms) {
    if (bus_mutex == NULL) return true;
    return xSemaphoreTake(bus_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void I2cBus::unlock() {
    if (bus_mutex != NULL) xSemaphoreGive(bus_mutex);
}

static I2cRecovery::Budget sensor_recovery(pdMS_TO_TICKS(2000));

struct SensorBusPins {
    bool scl() const { return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_SET; }
    bool sda() const { return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_10) == GPIO_PIN_SET; }
    void pause() const { for (unsigned i = 0; i < 800; ++i) __NOP(); }
    bool waitScl() const {
        for (unsigned i = 0; i < 16; ++i) { if (scl()) return true; pause(); }
        return scl();
    }
    void sclLow() { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET); }
    void sclRelease() { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET); }
    void sdaLow() { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET); }
    void sdaRelease() { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET); }
    void releaseOutputs() {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9 | GPIO_PIN_10, GPIO_PIN_SET);
    }
    void gpioOpenDrain() {
        releaseOutputs(); // Set release latches BEFORE changing modes; never push-pull high.
        GPIO_InitTypeDef pins = {};
        pins.Pin = GPIO_PIN_9 | GPIO_PIN_10;
        pins.Mode = GPIO_MODE_OUTPUT_OD; pins.Pull = GPIO_PULLUP;
        pins.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOA, &pins);
    }
};


// Called only by SensorHub while it owns i2c_bus_mutex. No PMIC or sensor writes.
bool I2cBus::recover() {
    sensor_recovery.beginAttempt(xTaskGetTickCount());
    I2cFault::attempts.store(sensor_recovery.attempts(), std::memory_order_relaxed);
    WakeMetrics::recoveries.fetch_add(1, std::memory_order_relaxed);
    if (HAL_I2C_DeInit(&hi2c1) != HAL_OK) {
        I2cFault::result.store(uint32_t(I2cRecovery::Result::InitFailed), std::memory_order_relaxed);
        return false;
    }
    GPIO_InitTypeDef pins = {};
    pins.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    pins.Mode = GPIO_MODE_INPUT; pins.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &pins);
    SensorBusPins bus;
    bus.pause();
    I2cRecovery::Result result = I2cRecovery::Result::Idle;
    if (!bus.scl()) result = I2cRecovery::Result::SclLow;
    else if (!bus.sda()) {
        result = sensor_recovery.consumeClear() ? I2cRecovery::releaseBus(bus) : I2cRecovery::Result::SdaLow;
    }
    // Always restore the original PA9/PA10 AF4 configuration, including on failure.
    MX_I2C1_Init();
    if (result == I2cRecovery::Result::Idle || result == I2cRecovery::Result::Released) {
        uint8_t id = 0;
        const uint32_t failed_addr = I2cFault::address.load(std::memory_order_relaxed) << 1;
        // Read-only probe; ACK/readback is not a claim that all sensor configuration survived.
        if (HAL_I2C_Mem_Read(&hi2c1, AXP2101_ADDR, AXP2101_IC_TYPE, 1, &id, 1, 5) != HAL_OK ||
            id != 0x4A || (failed_addr &&
            HAL_I2C_IsDeviceReady(&hi2c1, failed_addr, 1, 5) != HAL_OK) ||
            !bus.scl() || !bus.sda()) result = I2cRecovery::Result::ProbeFailed;
    }
    I2cFault::result.store(uint32_t(result), std::memory_order_relaxed);
    const bool passed = result == I2cRecovery::Result::Idle || result == I2cRecovery::Result::Released;
    if (passed) {
        sensor_recovery.probePassed(xTaskGetTickCount());
        I2cFault::paused.store(false, std::memory_order_relaxed);
    }
    return passed;
}


bool I2cBus::recoverySyncWake(uint32_t wake) { return sensor_recovery.syncWake(wake); }
bool I2cBus::recoveryDue(uint32_t now) { return sensor_recovery.due(now); }
void I2cBus::recoveryFault(uint32_t now) { sensor_recovery.fault(now); }
void I2cBus::recoveryHealthy(uint32_t now) { sensor_recovery.healthy(now); }
unsigned I2cBus::recoveryAttempts() { return sensor_recovery.attempts(); }
