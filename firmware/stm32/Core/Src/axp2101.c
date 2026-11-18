#include "axp2101.h"
#include <stdbool.h>
#include <stdio.h>

// C boundary into the single I2C owner. The legacy handle parameter is kept
// for source compatibility; the bus instance is owned by I2cBus.
HAL_StatusTypeDef I2cBus_ReadStatus(uint8_t address, uint8_t reg, uint8_t* data, uint8_t length);
HAL_StatusTypeDef I2cBus_WriteStatus(uint8_t address, uint8_t reg, const uint8_t* data,
                                     uint8_t length);

// =========================================================
// 底层 I2C 封装
// =========================================================
HAL_StatusTypeDef AXP2101_WriteReg(uint8_t reg, uint8_t val) {
    return I2cBus_WriteStatus(AXP2101_ADDR, reg, &val, 1);
}

HAL_StatusTypeDef AXP2101_ReadReg(uint8_t reg, uint8_t *val) {
    return I2cBus_ReadStatus(AXP2101_ADDR, reg, val, 1);
}

static uint16_t ReadH6L8(uint8_t h_reg, uint8_t l_reg) {
    uint8_t h, l;
    if (AXP2101_ReadReg(h_reg, &h) != HAL_OK) return 0;
    if (AXP2101_ReadReg(l_reg, &l) != HAL_OK) return 0;
    return ((h & 0x3F) << 8) | l;
}

// =========================================================
// 核心逻辑
// =========================================================

HAL_StatusTypeDef AXP2101_Init(void) {
    uint8_t chip_id = 0;
    if (AXP2101_ReadReg(AXP2101_IC_TYPE, &chip_id) != HAL_OK || chip_id != 0x4A) {
        return HAL_ERROR;
    }

    // VBUS input current limit: 500mA.
    uint8_t input_cur_limit = 0;
    if (AXP2101_ReadReg(AXP2101_INPUT_CUR_LIMIT_CTRL, &input_cur_limit) == HAL_OK) {
        input_cur_limit = (input_cur_limit & 0xF8) | 0x01;
        AXP2101_WriteReg(AXP2101_INPUT_CUR_LIMIT_CTRL, input_cur_limit);
    }

    // 电源轨：DCDC3 是 BW16 电源域，只预配置电压，默认不输出。
    AXP2101_WriteReg(0x82, 0x12); // DCDC1 = 3.3V
    AXP2101_SetDC3Voltage(3000);
    AXP2101_SetALDOVoltage(1, 3300);
    AXP2101_SetALDOVoltage(2, 3300);
    AXP2101_SetALDOVoltage(3, 3300);

    // 开启开关 (DC1开, DC3默认关, AL123开)
    AXP2101_WriteReg(0x80, 0x01); 
    AXP2101_WriteReg(0x90, 0x07);

    // 开启通用 ADC / 温度 / VSYS / VBUS / VBAT
    // 不启用 TS pin 采样，避免把外部热敏口误当成正常片上通道。
    AXP2101_WriteReg(AXP2101_ADC_CHANNEL_CTRL, 0x3D);

    return HAL_OK;
}

static bool fuel_gauge_ready;

static bool UpdateGaugeBits(uint8_t reg, uint8_t mask, uint8_t bits) {
    uint8_t old;
    if (AXP2101_ReadReg(reg, &old) != HAL_OK) return false;
    const uint8_t next = (uint8_t)((old & ~mask) | bits);
    if (old == next) return true;
    if (AXP2101_WriteReg(reg, next) != HAL_OK) return false;
    uint8_t actual;
    return AXP2101_ReadReg(reg, &actual) == HAL_OK && (actual & mask) == bits;
}

bool AXP2101_InitFuelGauge(void) {
    // AXP2101 V1.4 §6.13.2.87: restore POR ROM selection and close the writer
    // if an STM32-only reboot inherited the old settings. No model upload.
    // Like Zephyr, enable only the gauge and battery detection feature bits.
    fuel_gauge_ready = UpdateGaugeBits(AXP2101_FUEL_GAUGE_CTRL, 0x11, 0x00) &&
                       UpdateGaugeBits(AXP2101_CHARGE_GAUGE_WDT_CTRL, 0x08, 0x08) &&
                       UpdateGaugeBits(AXP2101_BAT_DET_CTRL, 0x01, 0x01);
    return fuel_gauge_ready;
}

bool AXP2101_TryGetBattVoltage(uint16_t *out_mv) {
    if (!out_mv) return false;
    uint8_t h = 0, l = 0;
    if (AXP2101_ReadReg(AXP2101_ADC_DATA_RELUST0, &h) != HAL_OK) return false;
    if (AXP2101_ReadReg(AXP2101_ADC_DATA_RELUST1, &l) != HAL_OK) return false;
    *out_mv = (uint16_t)(((h & 0x1F) << 8) | l);
    return true;
}

uint16_t AXP2101_GetBattVoltage(void) {
    // 从寄存器 0x34/0x35 读取电池电压原始值。
    uint16_t mv = 0;
    return AXP2101_TryGetBattVoltage(&mv) ? mv : 0;
}

float AXP2101_GetInternalTemp(void) {
    uint16_t raw = ReadH6L8(AXP2101_ADC_DATA_RELUST8, AXP2101_ADC_DATA_RELUST9);
    if (raw == 0) return 0.0f;
    // XPowersLib 与官方常量表都使用这组寄存器和该换算式。
    return 22.0f + (7274.0f - (float)raw) / 20.0f;
}

uint16_t AXP2101_GetVbusVoltage(void) {
    if (!AXP2101_IsVbusIn()) return 0;
    return ReadH6L8(AXP2101_ADC_DATA_RELUST4, AXP2101_ADC_DATA_RELUST5);
}

bool AXP2101_IsVbusIn(void) {
    uint8_t status1 = 0, status2 = 0;
    if (AXP2101_ReadReg(AXP2101_STATUS1, &status1) != HAL_OK) return false;
    if (AXP2101_ReadReg(AXP2101_STATUS2, &status2) != HAL_OK) return false;
    bool vbus_good = (status1 & 0x20) != 0;
    bool vbus_in = (status2 & 0x08) == 0;
    return vbus_good && vbus_in;
}

static bool AXP2101_IsBatteryConnected(void) {
    uint8_t status1 = 0;
    if (AXP2101_ReadReg(AXP2101_STATUS1, &status1) != HAL_OK) return false;
    return (status1 & 0x08) != 0;
}

bool AXP2101_TryGetBatteryPercent(uint8_t *out_percent) {
    if (!out_percent || !fuel_gauge_ready) return false;
    if (!AXP2101_IsBatteryConnected()) return false;

    // Read the full byte. It is an absolute percentage, not a flag+7-bit
    // value, so masking would turn invalid data (e.g. 0xE3) into fake 99%.
    uint8_t raw = 0;
    if (AXP2101_ReadReg(AXP2101_BAT_PERCENT_DATA, &raw) != HAL_OK) return false;
    if (raw > 100) return false;

    *out_percent = raw;
    return true;
}

uint8_t AXP2101_GetBatteryPercent(void) {
    uint8_t percent = 0;
    return AXP2101_TryGetBatteryPercent(&percent) ? percent : 0;
}

bool AXP2101_TryGetCharging(bool *out_charging) {
    if (!out_charging) return false;
    uint8_t val = 0;
    if (AXP2101_ReadReg(AXP2101_STATUS2, &val) != HAL_OK) return false;
    *out_charging = ((val >> 5) & 0x03) == 0x01;
    return true;
}

bool AXP2101_IsCharging(void) {
    bool charging = false;
    return AXP2101_TryGetCharging(&charging) && charging;
}

HAL_StatusTypeDef AXP2101_SetDC1Voltage(uint16_t mv) {
    if (mv < 1500 || mv > 3400) return HAL_ERROR;
    uint8_t val = (uint8_t)((mv - 1500) / 100);
    return AXP2101_WriteReg(AXP2101_DC_VOL0_CTRL, val & 0x1F);
}

HAL_StatusTypeDef AXP2101_SetDC1VoltageChecked(uint16_t mv) {
    if (mv < 1500 || mv > 3400 || (mv - 1500) % 100 != 0) return HAL_ERROR;
    HAL_StatusTypeDef status = AXP2101_SetDC1Voltage(mv);
    if (status != HAL_OK) return status;
    HAL_Delay(5);
    uint8_t selection = 0, enabled = 0;
    status = AXP2101_ReadReg(AXP2101_DC_VOL0_CTRL, &selection);
    if (status != HAL_OK) return status;
    if ((selection & 0x1F) != (mv - 1500) / 100) return HAL_ERROR;
    status = AXP2101_ReadReg(AXP2101_DC_ONOFF_DVM_CTRL, &enabled);
    if (status != HAL_OK) return status;
    return (enabled & 0x01) != 0 ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef AXP2101_SetDC3Voltage(uint16_t mv) {
    uint8_t val = 0, reg_val = 0;
    if (mv >= 1600 && mv <= 3400) val = 88 + (mv - 1600) / 100;
    else if (mv >= 1220 && mv <= 1540) val = 71 + (mv - 1220) / 20;
    else if (mv >= 500 && mv < 1220) val = (mv - 500) / 10;
    else return HAL_ERROR;
    if (AXP2101_ReadReg(0x84, &reg_val) == HAL_OK) {
        reg_val &= 0x80; reg_val |= (val & 0x7F);
        return AXP2101_WriteReg(0x84, reg_val);
    }
    return HAL_ERROR;
}

HAL_StatusTypeDef AXP2101_SetDC3Enable(bool enable) {
    uint8_t val = 0;
    if (AXP2101_ReadReg(AXP2101_DC_ONOFF_DVM_CTRL, &val) != HAL_OK) return HAL_ERROR;
    if (enable) val |= 0x04;
    else val &= (uint8_t)~0x04;
    return AXP2101_WriteReg(AXP2101_DC_ONOFF_DVM_CTRL, val);
}

uint16_t AXP2101_GetDC3Voltage(void) {
    uint8_t val = 0;
    if (AXP2101_ReadReg(AXP2101_DC_VOL2_CTRL, &val) != HAL_OK) return 0;
    val &= 0x7F;
    if (val >= 88) return (uint16_t)(1600 + (val - 88) * 100);
    if (val >= 71) return (uint16_t)(1220 + (val - 71) * 20);
    return (uint16_t)(500 + val * 10);
}

void AXP2101_SetDCDC3_Test(uint8_t val) {
    uint8_t reg_val = 0;
    if (AXP2101_ReadReg(0x84, &reg_val) == HAL_OK) {
        reg_val &= 0x80; reg_val |= (val & 0x7F);
        AXP2101_WriteReg(0x84, reg_val);
    }
}

HAL_StatusTypeDef AXP2101_SetALDOVoltage(uint8_t channel, uint16_t mv) {
    if (channel < 1 || channel > 4 || mv < 500 || mv > 3500) return HAL_ERROR;
    uint8_t val = (mv - 500) / 100, reg = 0x92 + (channel - 1), reg_val = 0;
    if (AXP2101_ReadReg(reg, &reg_val) == HAL_OK) {
        reg_val &= 0xE0; reg_val |= (val & 0x1F);
        return AXP2101_WriteReg(reg, reg_val);
    }
    return HAL_ERROR;
}

HAL_StatusTypeDef AXP2101_EnterLowPower(void) {
    HAL_StatusTypeDef st = HAL_OK;

    // Keep DCDC1 alive for the STM32, but turn off BW16 and unused DC rails.
    if (AXP2101_WriteReg(AXP2101_DC_ONOFF_DVM_CTRL, 0x01) != HAL_OK) st = HAL_ERROR;

    // PA6 is now the only STOP2 wake source, so the sensor rails can be off.
    if (AXP2101_WriteReg(AXP2101_ADC_CHANNEL_CTRL, 0x00) != HAL_OK) st = HAL_ERROR;
    if (AXP2101_WriteReg(AXP2101_LDO_ONOFF_CTRL0, 0x00) != HAL_OK) st = HAL_ERROR;

    return st;
}

HAL_StatusTypeDef AXP2101_ExitLowPower(void) {
    AXP2101_SetDC1Voltage(3300);
    HAL_Delay(5);
    return AXP2101_Init();
}
