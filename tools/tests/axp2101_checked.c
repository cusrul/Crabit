#include "axp2101.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

I2C_HandleTypeDef hi2c1;
static uint8_t registers[256];
static unsigned writes, reads, delays;
static int readFailureRegister, readOverride;
static HAL_StatusTypeDef writeResult;
static bool suppressWrite;

static void reset(void) {
    memset(registers, 0, sizeof(registers));
    registers[AXP2101_DC_ONOFF_DVM_CTRL] = 1;
    writes = reads = delays = 0;
    readFailureRegister = readOverride = -1;
    writeResult = HAL_OK;
    suppressWrite = false;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef* bus, uint16_t address, uint16_t reg,
                                    uint16_t addressSize, uint8_t* data, uint16_t count,
                                    uint32_t timeout) {
    assert(bus == &hi2c1 && address == AXP2101_ADDR && addressSize == 1 && count == 1 &&
           timeout == 100);
    ++writes;
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
    if ((int)reg == readFailureRegister)
        return HAL_TIMEOUT;
    *data =
        reg == AXP2101_DC_VOL0_CTRL && readOverride >= 0 ? (uint8_t)readOverride : registers[reg];
    return HAL_OK;
}
void HAL_Delay(uint32_t milliseconds) {
    assert(writes == 1 && reads == 0 && milliseconds == 5);
    ++delays;
}
int main(void) {
    reset();
    assert(AXP2101_SetDC1VoltageChecked(&hi2c1, 3300) == HAL_OK);
    assert(registers[AXP2101_DC_VOL0_CTRL] == 0x12 && writes == 1 && reads == 2 && delays == 1);
    assert(registers[AXP2101_DC_ONOFF_DVM_CTRL] == 1);
    reset();
    assert(AXP2101_SetDC1VoltageChecked(&hi2c1, 1800) == HAL_OK);
    assert(registers[AXP2101_DC_VOL0_CTRL] == 3);
    reset();
    assert(AXP2101_SetDC1VoltageChecked(&hi2c1, 1499) == HAL_ERROR);
    assert(AXP2101_SetDC1VoltageChecked(&hi2c1, 3401) == HAL_ERROR);
    assert(AXP2101_SetDC1VoltageChecked(&hi2c1, 3301) == HAL_ERROR);
    assert(writes == 0 && reads == 0 && delays == 0);
    reset();
    writeResult = HAL_TIMEOUT;
    assert(AXP2101_SetDC1VoltageChecked(&hi2c1, 3300) == HAL_TIMEOUT);
    assert(writes == 1 && reads == 0 && delays == 0);
    reset();
    readFailureRegister = AXP2101_DC_VOL0_CTRL;
    assert(AXP2101_SetDC1VoltageChecked(&hi2c1, 3300) == HAL_TIMEOUT);
    assert(reads == 1 && delays == 1);
    reset();
    suppressWrite = true;
    registers[AXP2101_DC_VOL0_CTRL] = 3;
    assert(AXP2101_SetDC1VoltageChecked(&hi2c1, 3300) == HAL_ERROR);
    assert(reads == 1);
    reset();
    registers[AXP2101_DC_ONOFF_DVM_CTRL] = 0x04;
    assert(AXP2101_SetDC1VoltageChecked(&hi2c1, 3300) == HAL_ERROR);
    reset();
    readFailureRegister = AXP2101_DC_ONOFF_DVM_CTRL;
    assert(AXP2101_SetDC1VoltageChecked(&hi2c1, 3300) == HAL_TIMEOUT);
    assert(reads == 2);
    reset();
    readOverride = 0xf2;
    assert(AXP2101_SetDC1VoltageChecked(&hi2c1, 3300) == HAL_OK);
    puts("AXP2101 checked voltage production tests PASS");
    return 0;
}
