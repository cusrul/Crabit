#include "axp2101.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

I2C_HandleTypeDef hi2c1;
static uint8_t registers[256];
static unsigned writes, reads, delays;
static int readFailureRegister, readOverride;
static int readFailureAttempt, writeFailureAttempt;
static HAL_StatusTypeDef writeResult;
static bool suppressWrite;

// Ordered log of every bus operation attempted by the production driver.
enum { MAX_OPS = 64 };
static char op_kind[MAX_OPS];
static uint8_t op_reg[MAX_OPS];
static uint8_t op_value[MAX_OPS];
static unsigned op_count;

static void record_op(char kind, uint16_t reg, uint8_t value) {
    assert(op_count < MAX_OPS);
    op_kind[op_count] = kind;
    op_reg[op_count] = (uint8_t)reg;
    op_value[op_count] = value;
    ++op_count;
}

static void reset(void) {
    memset(registers, 0, sizeof(registers));
    registers[AXP2101_DC_ONOFF_DVM_CTRL] = 1;
    writes = reads = delays = 0;
    readFailureRegister = readOverride = -1;
    readFailureAttempt = writeFailureAttempt = -1;
    writeResult = HAL_OK;
    suppressWrite = false;
    op_count = 0;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef* bus, uint16_t address, uint16_t reg,
                                    uint16_t addressSize, uint8_t* data, uint16_t count,
                                    uint32_t timeout) {
    assert(bus == &hi2c1 && address == AXP2101_ADDR && addressSize == 1 && count == 1 &&
           timeout == 100);
    ++writes;
    record_op('w', reg, *data);
    if ((int)writes == writeFailureAttempt)
        return HAL_TIMEOUT;
    if (writeResult != HAL_OK)
        return writeResult;
    if (!suppressWrite)
        registers[reg] = *data;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef* bus, uint16_t address, uint16_t reg,
                                   uint16_t addressSize, uint8_t* data, uint16_t count,
                                   uint32_t timeout) {
    assert(bus == &hi2c1 && address == AXP2101_ADDR && addressSize == 1 && count == 1 &&
           timeout == 100);
    ++reads;
    record_op('r', reg, 0);
    if ((int)reg == readFailureRegister)
        return HAL_TIMEOUT;
    if ((int)reads == readFailureAttempt)
        return HAL_TIMEOUT;
    *data =
        reg == AXP2101_DC_VOL0_CTRL && readOverride >= 0 ? (uint8_t)readOverride : registers[reg];
    return HAL_OK;
}
HAL_StatusTypeDef I2cBus_WriteStatus(uint8_t address, uint8_t reg, const uint8_t* data,
                                     uint8_t length) {
    return HAL_I2C_Mem_Write(&hi2c1, address, reg, 1, (uint8_t*)data, length, 100);
}

HAL_StatusTypeDef I2cBus_ReadStatus(uint8_t address, uint8_t reg, uint8_t* data, uint8_t length) {
    return HAL_I2C_Mem_Read(&hi2c1, address, reg, 1, data, length, 100);
}

void HAL_Delay(uint32_t milliseconds) {
    // Only the checked-voltage sequence delays, and only between its write
    // and its verification reads.
    assert(writes == 1 && reads == 0 && milliseconds == 5);
    ++delays;
}

static unsigned count_ops(char kind, uint8_t reg) {
    unsigned count = 0;
    for (unsigned i = 0; i < op_count; ++i)
        if (op_kind[i] == kind && op_reg[i] == reg)
            ++count;
    return count;
}

static void assert_op(unsigned index, char kind, uint8_t reg) {
    assert(index < op_count);
    assert(op_kind[index] == kind && op_reg[index] == reg);
}

static void assert_gauge_write_is_safe(void) {
    for (unsigned i = 0; i < op_count; ++i) {
        if (op_kind[i] != 'w')
            continue;
        // The fuel gauge must never be reset and no model may be uploaded.
        assert(op_reg[i] != AXP2101_RESET_FUEL_GAUGE);
        assert(op_reg[i] != AXP2101_BAT_PARAME);
        if (op_reg[i] == AXP2101_FUEL_GAUGE_CTRL)
            assert((op_value[i] & 0x11) == 0);  // ROM selected, parameter writer closed.
    }
}

// POR/stock ROM state: gauge source ROM, feature bits already enabled.
#define GAUGE_STOCK_18 0x0A  // Charging-gauge watchdog/feature bits, bit 3 set.
#define GAUGE_STOCK_68 0x01  // Battery detection enabled.

static void stock_rom_state(void) {
    registers[AXP2101_FUEL_GAUGE_CTRL] = 0x00;
    registers[AXP2101_CHARGE_GAUGE_WDT_CTRL] = GAUGE_STOCK_18;
    registers[AXP2101_BAT_DET_CTRL] = GAUGE_STOCK_68;
}

// The old firmware left the gauge on SRAM and the writer open after boot.
static void legacy_sram_state(void) {
    stock_rom_state();
    registers[AXP2101_FUEL_GAUGE_CTRL] = 0x10;
}

// A successful setup in the stock state performs no writes; it only satisfies
// the read-before-write credit the percentage read path now requires.
static void prepare_gauge(void) {
    stock_rom_state();
    assert(AXP2101_InitFuelGauge());
    writes = reads = delays = 0;
    op_count = 0;
}

// Until setup succeeds the percentage path must refuse without bus traffic and
// must leave the caller's output untouched.
static void assert_percent_refused(void) {
    registers[AXP2101_STATUS1] = 0x08;
    registers[AXP2101_BAT_PERCENT_DATA] = 55;
    const unsigned reads_before = reads, writes_before = writes;
    uint8_t percent = 0xAA;
    assert(!AXP2101_TryGetBatteryPercent(&percent));
    assert(percent == 0xAA);
    assert(reads == reads_before && writes == writes_before);
}

static void test_fuel_gauge_refuses_before_setup(void) {
    // This must run before any successful AXP2101_InitFuelGauge() call.
    reset();
    assert_percent_refused();
    assert(AXP2101_GetBatteryPercent() == 0);
    assert(reads == 0 && writes == 0);
}

static void test_board_pmu_init(void) {
    reset();
    registers[AXP2101_IC_TYPE] = 0x4A;
    registers[AXP2101_INPUT_CUR_LIMIT_CTRL] = 0xF8;
    assert(AXP2101_Init() == HAL_OK);

    // Board power, rail and ADC setup are unchanged.
    assert(registers[AXP2101_INPUT_CUR_LIMIT_CTRL] == 0xF9);  // 500 mA, upper bits kept.
    assert(registers[AXP2101_DC_VOL0_CTRL] == 0x12);          // DCDC1 3.3 V.
    assert(registers[AXP2101_DC_VOL2_CTRL] == 0x66);          // DCDC3 3.0 V.
    assert(registers[AXP2101_LDO_VOL0_CTRL] == 0x1C);         // ALDO1 3.3 V.
    assert(registers[AXP2101_LDO_VOL1_CTRL] == 0x1C);         // ALDO2 3.3 V.
    assert(registers[AXP2101_LDO_VOL2_CTRL] == 0x1C);         // ALDO3 3.3 V.
    assert(registers[AXP2101_DC_ONOFF_DVM_CTRL] == 0x01);     // DCDC1 on, DCDC3 off.
    assert(registers[AXP2101_LDO_ONOFF_CTRL0] == 0x07);       // ALDO1-3 on.
    assert(registers[AXP2101_ADC_CHANNEL_CTRL] == 0x3D);      // ADC channels, no TS.

    // Gauge selection/reset and model upload have moved out of board setup.
    assert(count_ops('w', AXP2101_RESET_FUEL_GAUGE) == 0);
    assert(count_ops('w', AXP2101_BAT_PARAME) == 0);
    assert(count_ops('w', AXP2101_FUEL_GAUGE_CTRL) == 0);
    assert_gauge_write_is_safe();

    // The chip-ID check still gates every PMU write.
    reset();
    registers[AXP2101_IC_TYPE] = 0x4B;
    assert(AXP2101_Init() == HAL_ERROR);
    assert(reads == 1 && writes == 0);
}

static void test_fuel_gauge_stock_rom_makes_no_writes(void) {
    // Migration from old firmware on a gauge that is already in ROM state:
    // all three registers are read, but none needs a write.
    reset();
    stock_rom_state();
    assert(AXP2101_InitFuelGauge());
    assert(writes == 0 && reads == 3 && delays == 0);
    assert_op(0, 'r', AXP2101_FUEL_GAUGE_CTRL);
    assert_op(1, 'r', AXP2101_CHARGE_GAUGE_WDT_CTRL);
    assert_op(2, 'r', AXP2101_BAT_DET_CTRL);

    // Unrelated bits in the A2 ROM state also require no write and stay intact.
    reset();
    stock_rom_state();
    registers[AXP2101_FUEL_GAUGE_CTRL] = 0x22;
    assert(AXP2101_InitFuelGauge());
    assert(writes == 0 && reads == 3);
    assert(registers[AXP2101_FUEL_GAUGE_CTRL] == 0x22);
}

static void test_fuel_gauge_undoes_legacy_sram_state(void) {
    reset();
    legacy_sram_state();  // A2 = 0x10, 18/68 already correct.
    assert(AXP2101_InitFuelGauge());
    assert(registers[AXP2101_FUEL_GAUGE_CTRL] == 0x00);
    assert(registers[AXP2101_CHARGE_GAUGE_WDT_CTRL] == GAUGE_STOCK_18);
    assert(registers[AXP2101_BAT_DET_CTRL] == GAUGE_STOCK_68);
    assert(writes == 1 && reads == 4);
    assert(count_ops('w', AXP2101_FUEL_GAUGE_CTRL) == 1);
    assert(op_value[1] == 0x00);
    assert_op(0, 'r', AXP2101_FUEL_GAUGE_CTRL);
    assert_op(1, 'w', AXP2101_FUEL_GAUGE_CTRL);
    assert_op(2, 'r', AXP2101_FUEL_GAUGE_CTRL);  // Readback verification.
    assert_op(3, 'r', AXP2101_CHARGE_GAUGE_WDT_CTRL);
    assert_op(4, 'r', AXP2101_BAT_DET_CTRL);
    assert_gauge_write_is_safe();
}

static void test_fuel_gauge_preserves_unrelated_bits(void) {
    // A2 0xFF -> 0xEE (clear 0x11), 18 0xF7 -> 0xFF, 68 0xFE -> 0xFF.
    reset();
    registers[AXP2101_FUEL_GAUGE_CTRL] = 0xFF;
    registers[AXP2101_CHARGE_GAUGE_WDT_CTRL] = 0xF7;
    registers[AXP2101_BAT_DET_CTRL] = 0xFE;
    assert(AXP2101_InitFuelGauge());
    assert(registers[AXP2101_FUEL_GAUGE_CTRL] == 0xEE);
    assert(registers[AXP2101_CHARGE_GAUGE_WDT_CTRL] == 0xFF);
    assert(registers[AXP2101_BAT_DET_CTRL] == 0xFF);
    assert(writes == 3 && reads == 6 && delays == 0);

    // Exact order: read/modify/write/verify A2, then 18, then 68.
    assert_op(0, 'r', AXP2101_FUEL_GAUGE_CTRL);
    assert_op(1, 'w', AXP2101_FUEL_GAUGE_CTRL);
    assert_op(2, 'r', AXP2101_FUEL_GAUGE_CTRL);
    assert_op(3, 'r', AXP2101_CHARGE_GAUGE_WDT_CTRL);
    assert_op(4, 'w', AXP2101_CHARGE_GAUGE_WDT_CTRL);
    assert_op(5, 'r', AXP2101_CHARGE_GAUGE_WDT_CTRL);
    assert_op(6, 'r', AXP2101_BAT_DET_CTRL);
    assert_op(7, 'w', AXP2101_BAT_DET_CTRL);
    assert_op(8, 'r', AXP2101_BAT_DET_CTRL);
    assert(op_value[1] == 0xEE);
    assert(op_value[4] == 0xFF);
    assert(op_value[7] == 0xFF);
    assert_gauge_write_is_safe();
}

static void test_fuel_gauge_fail_fast(void) {
    // A2 read failure: no write, later registers untouched.
    reset();
    legacy_sram_state();
    readFailureRegister = AXP2101_FUEL_GAUGE_CTRL;
    assert(!AXP2101_InitFuelGauge());
    assert(writes == 0 && reads == 1);
    assert(count_ops('r', AXP2101_CHARGE_GAUGE_WDT_CTRL) == 0);
    assert(count_ops('r', AXP2101_BAT_DET_CTRL) == 0);
    assert_percent_refused();

    // A2 write failure: fail before readback; 18/68 are not inspected.
    reset();
    legacy_sram_state();
    writeFailureAttempt = 1;
    assert(!AXP2101_InitFuelGauge());
    assert(writes == 1 && reads == 1);
    assert(registers[AXP2101_FUEL_GAUGE_CTRL] == 0x10);
    assert(count_ops('r', AXP2101_CHARGE_GAUGE_WDT_CTRL) == 0);

    // A2 write silently ignored: readback verification rejects it.
    reset();
    legacy_sram_state();
    suppressWrite = true;
    assert(!AXP2101_InitFuelGauge());
    assert(writes == 1 && reads == 2);
    assert(registers[AXP2101_FUEL_GAUGE_CTRL] == 0x10);
    assert(count_ops('r', AXP2101_CHARGE_GAUGE_WDT_CTRL) == 0);

    // A2 readback failure also fails the setup.
    reset();
    legacy_sram_state();
    readFailureAttempt = 2;
    assert(!AXP2101_InitFuelGauge());
    assert(writes == 1 && reads == 2);
    assert(count_ops('r', AXP2101_CHARGE_GAUGE_WDT_CTRL) == 0);

    // 18 read failure after A2 migration: A2 stays migrated, 68 is not read.
    reset();
    legacy_sram_state();
    readFailureRegister = AXP2101_CHARGE_GAUGE_WDT_CTRL;
    assert(!AXP2101_InitFuelGauge());
    assert(registers[AXP2101_FUEL_GAUGE_CTRL] == 0x00);
    assert(writes == 1 && count_ops('w', AXP2101_CHARGE_GAUGE_WDT_CTRL) == 0);
    assert(count_ops('r', AXP2101_BAT_DET_CTRL) == 0);

    // 18 write silently ignored: readback verification rejects it; 68 is not read.
    reset();
    stock_rom_state();
    registers[AXP2101_CHARGE_GAUGE_WDT_CTRL] = 0xF7;
    suppressWrite = true;
    assert(!AXP2101_InitFuelGauge());
    assert(writes == 1 && reads == 3);
    assert(registers[AXP2101_CHARGE_GAUGE_WDT_CTRL] == 0xF7);
    assert(count_ops('r', AXP2101_BAT_DET_CTRL) == 0);

    // 68 write failure: fail before readback, A2/18 already correct.
    reset();
    stock_rom_state();
    registers[AXP2101_BAT_DET_CTRL] = 0xFE;
    writeFailureAttempt = 1;
    assert(!AXP2101_InitFuelGauge());
    assert(writes == 1 && reads == 3);
    assert(registers[AXP2101_BAT_DET_CTRL] == 0xFE);

    // The last failed attempt must not leave the percentage path enabled.
    assert_percent_refused();
}

static void test_fuel_gauge_state_tracks_setup(void) {
    reset();
    stock_rom_state();
    assert(AXP2101_InitFuelGauge());
    registers[AXP2101_STATUS1] = 0x08;
    registers[AXP2101_BAT_PERCENT_DATA] = 42;
    uint8_t percent = 0;
    assert(AXP2101_TryGetBatteryPercent(&percent) && percent == 42);

    // A later failed attempt closes the gate again before it is re-opened.
    readFailureRegister = AXP2101_FUEL_GAUGE_CTRL;
    assert(!AXP2101_InitFuelGauge());
    assert_percent_refused();

    // A clean retry reopens it; no model write is involved.
    readFailureRegister = -1;
    assert(AXP2101_InitFuelGauge());
    percent = 0;
    assert(AXP2101_TryGetBatteryPercent(&percent) && percent == 55);
}

static void test_checked_voltage(void) {
    reset();
    assert(AXP2101_SetDC1VoltageChecked(3300) == HAL_OK);
    assert(registers[AXP2101_DC_VOL0_CTRL] == 0x12 && writes == 1 && reads == 2 && delays == 1);
    assert(registers[AXP2101_DC_ONOFF_DVM_CTRL] == 1);
    reset();
    assert(AXP2101_SetDC1VoltageChecked(1800) == HAL_OK);
    assert(registers[AXP2101_DC_VOL0_CTRL] == 3);
    reset();
    assert(AXP2101_SetDC1VoltageChecked(1499) == HAL_ERROR);
    assert(AXP2101_SetDC1VoltageChecked(3401) == HAL_ERROR);
    assert(AXP2101_SetDC1VoltageChecked(3301) == HAL_ERROR);
    assert(writes == 0 && reads == 0 && delays == 0);
    reset();
    writeResult = HAL_TIMEOUT;
    assert(AXP2101_SetDC1VoltageChecked(3300) == HAL_TIMEOUT);
    assert(writes == 1 && reads == 0 && delays == 0);
    reset();
    readFailureRegister = AXP2101_DC_VOL0_CTRL;
    assert(AXP2101_SetDC1VoltageChecked(3300) == HAL_TIMEOUT);
    assert(reads == 1 && delays == 1);
    reset();
    suppressWrite = true;
    registers[AXP2101_DC_VOL0_CTRL] = 3;
    assert(AXP2101_SetDC1VoltageChecked(3300) == HAL_ERROR);
    assert(reads == 1);
    reset();
    registers[AXP2101_DC_ONOFF_DVM_CTRL] = 0x04;
    assert(AXP2101_SetDC1VoltageChecked(3300) == HAL_ERROR);
    reset();
    readFailureRegister = AXP2101_DC_ONOFF_DVM_CTRL;
    assert(AXP2101_SetDC1VoltageChecked(3300) == HAL_TIMEOUT);
    assert(reads == 2);
    reset();
    readOverride = 0xf2;
    assert(AXP2101_SetDC1VoltageChecked(3300) == HAL_OK);
}

static void assert_percent(uint8_t raw, bool expected_valid, uint8_t expected_value) {
    reset();
    prepare_gauge();
    registers[AXP2101_STATUS1] = 0x08;  // Battery connected.
    registers[AXP2101_BAT_PERCENT_DATA] = raw;
    uint8_t percent = 0xAA;
    const bool valid = AXP2101_TryGetBatteryPercent(&percent);
    assert(valid == expected_valid);
    if (expected_valid)
        assert(percent == expected_value);
    else
        assert(percent == 0xAA);  // Failure must never overwrite the output.
    assert(writes == 0);
}

static void test_battery_percent(void) {
    assert_percent(0, true, 0);
    assert_percent(1, true, 1);
    assert_percent(55, true, 55);
    assert_percent(99, true, 99);
    assert_percent(100, true, 100);
    // Above 100 is not a percentage. It must not be masked into 0..100.
    assert_percent(101, false, 0);
    assert_percent(0xE3, false, 0);  // Masked as 7-bit this used to read 99.
    assert_percent(0xFF, false, 0);

    reset();
    prepare_gauge();
    registers[AXP2101_STATUS1] = 0x08;
    registers[AXP2101_BAT_PERCENT_DATA] = 55;
    assert(AXP2101_GetBatteryPercent() == 55);
    assert(writes == 0);

    // A connected battery with gauge 0 is a real empty reading, not unknown.
    reset();
    prepare_gauge();
    registers[AXP2101_STATUS1] = 0x08;
    assert(AXP2101_GetBatteryPercent() == 0);

    // Disconnected: the gauge register is not read and the result is unknown.
    reset();
    prepare_gauge();
    registers[AXP2101_BAT_PERCENT_DATA] = 55;
    uint8_t percent = 0xAA;
    assert(!AXP2101_TryGetBatteryPercent(&percent));
    assert(percent == 0xAA && reads == 1 && writes == 0);
    assert(AXP2101_GetBatteryPercent() == 0);

    // Presence-read failure is unknown, not 0%.
    reset();
    prepare_gauge();
    registers[AXP2101_BAT_PERCENT_DATA] = 55;
    readFailureRegister = AXP2101_STATUS1;
    assert(!AXP2101_TryGetBatteryPercent(&percent));

    // Gauge-read failure is unknown, not 0%.
    reset();
    prepare_gauge();
    registers[AXP2101_STATUS1] = 0x08;
    readFailureRegister = AXP2101_BAT_PERCENT_DATA;
    percent = 0xAA;
    assert(!AXP2101_TryGetBatteryPercent(&percent));
    assert(percent == 0xAA && reads == 2 && writes == 0);

    // A missing output pointer is rejected before touching the bus.
    reset();
    prepare_gauge();
    registers[AXP2101_STATUS1] = 0x08;
    assert(!AXP2101_TryGetBatteryPercent(NULL));
    assert(reads == 0 && writes == 0);
}

static void test_battery_voltage(void) {
    reset();
    registers[AXP2101_ADC_DATA_RELUST0] = 0x0D;
    registers[AXP2101_ADC_DATA_RELUST1] = 0x86;
    uint16_t millivolts = 0;
    assert(AXP2101_TryGetBattVoltage(&millivolts));
    assert(millivolts == 0x0D86);
    assert(AXP2101_GetBattVoltage() == 0x0D86);
    assert(writes == 0);

    // Reserved high-byte bits are ignored, as before.
    reset();
    registers[AXP2101_ADC_DATA_RELUST0] = 0xED;
    registers[AXP2101_ADC_DATA_RELUST1] = 0x86;
    millivolts = 0;
    assert(AXP2101_TryGetBattVoltage(&millivolts));
    assert(millivolts == 0x0D86);

    // Failures leave the caller's value untouched and never fabricate 0 mV.
    reset();
    readFailureRegister = AXP2101_ADC_DATA_RELUST0;
    millivolts = 1234;
    assert(!AXP2101_TryGetBattVoltage(&millivolts));
    assert(millivolts == 1234 && reads == 1);
    reset();
    readFailureRegister = AXP2101_ADC_DATA_RELUST1;
    millivolts = 1234;
    assert(!AXP2101_TryGetBattVoltage(&millivolts));
    assert(millivolts == 1234 && reads == 2);
    assert(AXP2101_GetBattVoltage() == 0);
    reset();
    assert(!AXP2101_TryGetBattVoltage(NULL));
    assert(reads == 0 && writes == 0);
}

static void test_charging(void) {
    reset();
    registers[AXP2101_STATUS2] = 0xA0;  // Charge state 01: charging.
    bool charging = false;
    assert(AXP2101_TryGetCharging(&charging) && charging);
    assert(AXP2101_IsCharging());
    assert(writes == 0);

    reset();
    assert(AXP2101_TryGetCharging(&charging) && !charging);  // Charge state 00.
    assert(!AXP2101_IsCharging());

    reset();
    registers[AXP2101_STATUS2] = 0x40;  // Charge state 10 is not "charging".
    assert(AXP2101_TryGetCharging(&charging) && !charging);

    // A read failure is unknown and does not overwrite the output.
    reset();
    readFailureRegister = AXP2101_STATUS2;
    charging = true;
    assert(!AXP2101_TryGetCharging(&charging));
    assert(charging);
    assert(!AXP2101_IsCharging());

    reset();
    assert(!AXP2101_TryGetCharging(NULL));
    assert(reads == 0 && writes == 0);
}

int main(void) {
    test_fuel_gauge_refuses_before_setup();
    test_board_pmu_init();
    test_fuel_gauge_stock_rom_makes_no_writes();
    test_fuel_gauge_undoes_legacy_sram_state();
    test_fuel_gauge_preserves_unrelated_bits();
    test_fuel_gauge_fail_fast();
    test_fuel_gauge_state_tracks_setup();
    test_checked_voltage();
    test_battery_percent();
    test_battery_voltage();
    test_charging();
    puts("AXP2101 board init, fuel-gauge setup and read-semantics production tests PASS");
    return 0;
}
