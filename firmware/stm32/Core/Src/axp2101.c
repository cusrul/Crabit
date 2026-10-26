#include "axp2101.h"
#include <stdio.h>

extern I2C_HandleTypeDef hi2c1;

// =========================================================
// 底层 I2C 封装
// =========================================================
HAL_StatusTypeDef AXP2101_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t val) {
    return HAL_I2C_Mem_Write(hi2c, AXP2101_ADDR, reg, 1, &val, 1, 100);
}

HAL_StatusTypeDef AXP2101_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t *val) {
    return HAL_I2C_Mem_Read(hi2c, AXP2101_ADDR, reg, 1, val, 1, 100);
}

static uint16_t ReadH5L8(I2C_HandleTypeDef *hi2c, uint8_t h_reg, uint8_t l_reg) {
    uint8_t h, l;
    if (AXP2101_ReadReg(hi2c, h_reg, &h) != HAL_OK) return 0;
    if (AXP2101_ReadReg(hi2c, l_reg, &l) != HAL_OK) return 0;
    return ((h & 0x1F) << 8) | l;
}

static uint16_t ReadH6L8(I2C_HandleTypeDef *hi2c, uint8_t h_reg, uint8_t l_reg) {
    uint8_t h, l;
    if (AXP2101_ReadReg(hi2c, h_reg, &h) != HAL_OK) return 0;
    if (AXP2101_ReadReg(hi2c, l_reg, &l) != HAL_OK) return 0;
    return ((h & 0x3F) << 8) | l;
}

// =========================================================
// 核心逻辑
// =========================================================

HAL_StatusTypeDef AXP2101_Init(I2C_HandleTypeDef *hi2c) {
    uint8_t chip_id = 0;
    if (AXP2101_ReadReg(hi2c, AXP2101_IC_TYPE, &chip_id) != HAL_OK || chip_id != 0x4A) {
        return HAL_ERROR;
    }

    // 1. 复位 MCU (八步初始化开始)
    AXP2101_WriteReg(hi2c, 0x17, 0x04);
    AXP2101_WriteReg(hi2c, 0x17, 0x00);
    AXP2101_WriteReg(hi2c, 0xA2, 0x00);
    AXP2101_WriteReg(hi2c, 0xA2, 0x01);
    AXP2101_WriteReg(hi2c, 0xA2, 0x00);
    AXP2101_WriteReg(hi2c, 0xA2, 0x10);
    AXP2101_WriteReg(hi2c, 0x17, 0x04);
    AXP2101_WriteReg(hi2c, 0x17, 0x00);

    // 2. VBUS input current limit: 500mA.
    uint8_t input_cur_limit = 0;
    if (AXP2101_ReadReg(hi2c, AXP2101_INPUT_CUR_LIMIT_CTRL, &input_cur_limit) == HAL_OK) {
        input_cur_limit = (input_cur_limit & 0xF8) | 0x01;
        AXP2101_WriteReg(hi2c, AXP2101_INPUT_CUR_LIMIT_CTRL, input_cur_limit);
    }

    // 3. 电源轨：DCDC3 是 BW16 电源域，只预配置电压，默认不输出。
    AXP2101_WriteReg(hi2c, 0x82, 0x12); // DCDC1 = 3.3V
    AXP2101_SetDC3Voltage(hi2c, 3000);
    AXP2101_SetALDOVoltage(hi2c, 1, 3300);
    AXP2101_SetALDOVoltage(hi2c, 2, 3300);
    AXP2101_SetALDOVoltage(hi2c, 3, 3300);

    // 4. 开启开关 (DC1开, DC3默认关, AL123开)
    AXP2101_WriteReg(hi2c, 0x80, 0x01); 
    AXP2101_WriteReg(hi2c, 0x90, 0x07);

    // 5. 开启通用 ADC / 温度 / VSYS / VBUS / VBAT
    // 不启用 TS pin 采样，避免把外部热敏口误当成正常片上通道。
    AXP2101_WriteReg(hi2c, AXP2101_ADC_CHANNEL_CTRL, 0x3D);

    return HAL_OK;
}

uint16_t AXP2101_GetBattVoltage(I2C_HandleTypeDef *hi2c) {
    // 从寄存器 0x34/0x35 读取电池电压原始值。
    return ReadH5L8(hi2c, 0x34, 0x35);
}

float AXP2101_GetInternalTemp(I2C_HandleTypeDef *hi2c) {
    uint16_t raw = ReadH6L8(hi2c, AXP2101_ADC_DATA_RELUST8, AXP2101_ADC_DATA_RELUST9);
    if (raw == 0) return 0.0f;
    // XPowersLib 与官方常量表都使用这组寄存器和该换算式。
    return 22.0f + (7274.0f - (float)raw) / 20.0f;
}

uint16_t AXP2101_GetVbusVoltage(I2C_HandleTypeDef *hi2c) {
    if (!AXP2101_IsVbusIn(hi2c)) return 0;
    return ReadH6L8(hi2c, AXP2101_ADC_DATA_RELUST4, AXP2101_ADC_DATA_RELUST5);
}

bool AXP2101_IsVbusIn(I2C_HandleTypeDef *hi2c) {
    uint8_t status1 = 0, status2 = 0;
    if (AXP2101_ReadReg(hi2c, AXP2101_STATUS1, &status1) != HAL_OK) return false;
    if (AXP2101_ReadReg(hi2c, AXP2101_STATUS2, &status2) != HAL_OK) return false;
    bool vbus_good = (status1 & 0x20) != 0;
    bool vbus_in = (status2 & 0x08) == 0;
    return vbus_good && vbus_in;
}

static bool AXP2101_IsBatteryConnected(I2C_HandleTypeDef *hi2c) {
    uint8_t status1 = 0;
    if (AXP2101_ReadReg(hi2c, AXP2101_STATUS1, &status1) != HAL_OK) return false;
    return (status1 & 0x08) != 0;
}

uint8_t AXP2101_GetBatteryPercent(I2C_HandleTypeDef *hi2c) {
    uint8_t val = 0;
    if (!AXP2101_IsBatteryConnected(hi2c)) return 0;
    if (AXP2101_ReadReg(hi2c, AXP2101_BAT_PERCENT_DATA, &val) == HAL_OK) return val & 0x7F;
    return 0;
}

bool AXP2101_IsCharging(I2C_HandleTypeDef *hi2c) {
    uint8_t val = 0;
    if (AXP2101_ReadReg(hi2c, 0x01, &val) == HAL_OK) return ((val >> 5) & 0x03) == 0x01;
    return false;
}

HAL_StatusTypeDef AXP2101_SetDC1Voltage(I2C_HandleTypeDef *hi2c, uint16_t mv) {
    if (mv < 1500 || mv > 3400) return HAL_ERROR;
    uint8_t val = (uint8_t)((mv - 1500) / 100);
    return AXP2101_WriteReg(hi2c, AXP2101_DC_VOL0_CTRL, val & 0x1F);
}

HAL_StatusTypeDef AXP2101_SetDC1VoltageChecked(I2C_HandleTypeDef *hi2c, uint16_t mv) {
    if (mv < 1500 || mv > 3400 || (mv - 1500) % 100 != 0) return HAL_ERROR;
    HAL_StatusTypeDef status = AXP2101_SetDC1Voltage(hi2c, mv);
    if (status != HAL_OK) return status;
    HAL_Delay(5);
    uint8_t selection = 0, enabled = 0;
    status = AXP2101_ReadReg(hi2c, AXP2101_DC_VOL0_CTRL, &selection);
    if (status != HAL_OK) return status;
    if ((selection & 0x1F) != (mv - 1500) / 100) return HAL_ERROR;
    status = AXP2101_ReadReg(hi2c, AXP2101_DC_ONOFF_DVM_CTRL, &enabled);
    if (status != HAL_OK) return status;
    return (enabled & 0x01) != 0 ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef AXP2101_SetDC3Voltage(I2C_HandleTypeDef *hi2c, uint16_t mv) {
    uint8_t val = 0, reg_val = 0;
    if (mv >= 1600 && mv <= 3400) val = 88 + (mv - 1600) / 100;
    else if (mv >= 1220 && mv <= 1540) val = 71 + (mv - 1220) / 20;
    else if (mv >= 500 && mv < 1220) val = (mv - 500) / 10;
    else return HAL_ERROR;
    if (AXP2101_ReadReg(hi2c, 0x84, &reg_val) == HAL_OK) {
        reg_val &= 0x80; reg_val |= (val & 0x7F);
        return AXP2101_WriteReg(hi2c, 0x84, reg_val);
    }
    return HAL_ERROR;
}

HAL_StatusTypeDef AXP2101_SetDC3Enable(I2C_HandleTypeDef *hi2c, bool enable) {
    uint8_t val = 0;
    if (AXP2101_ReadReg(hi2c, AXP2101_DC_ONOFF_DVM_CTRL, &val) != HAL_OK) return HAL_ERROR;
    if (enable) val |= 0x04;
    else val &= (uint8_t)~0x04;
    return AXP2101_WriteReg(hi2c, AXP2101_DC_ONOFF_DVM_CTRL, val);
}

uint16_t AXP2101_GetDC3Voltage(I2C_HandleTypeDef *hi2c) {
    uint8_t val = 0;
    if (AXP2101_ReadReg(hi2c, AXP2101_DC_VOL2_CTRL, &val) != HAL_OK) return 0;
    val &= 0x7F;
    if (val >= 88) return (uint16_t)(1600 + (val - 88) * 100);
    if (val >= 71) return (uint16_t)(1220 + (val - 71) * 20);
    return (uint16_t)(500 + val * 10);
}

void AXP2101_SetDCDC3_Test(uint8_t val) {
    uint8_t reg_val = 0;
    if (AXP2101_ReadReg(&hi2c1, 0x84, &reg_val) == HAL_OK) {
        reg_val &= 0x80; reg_val |= (val & 0x7F);
        AXP2101_WriteReg(&hi2c1, 0x84, reg_val);
    }
}

HAL_StatusTypeDef AXP2101_SetALDOVoltage(I2C_HandleTypeDef *hi2c, uint8_t channel, uint16_t mv) {
    if (channel < 1 || channel > 4 || mv < 500 || mv > 3500) return HAL_ERROR;
    uint8_t val = (mv - 500) / 100, reg = 0x92 + (channel - 1), reg_val = 0;
    if (AXP2101_ReadReg(hi2c, reg, &reg_val) == HAL_OK) {
        reg_val &= 0xE0; reg_val |= (val & 0x1F);
        return AXP2101_WriteReg(hi2c, reg, reg_val);
    }
    return HAL_ERROR;
}

HAL_StatusTypeDef AXP2101_EnterLowPower(I2C_HandleTypeDef *hi2c) {
    HAL_StatusTypeDef st = HAL_OK;

    // Keep DCDC1 alive for the STM32, but turn off BW16 and unused DC rails.
    if (AXP2101_WriteReg(hi2c, AXP2101_DC_ONOFF_DVM_CTRL, 0x01) != HAL_OK) st = HAL_ERROR;

    // PA6 is now the only STOP2 wake source, so the sensor rails can be off.
    if (AXP2101_WriteReg(hi2c, AXP2101_ADC_CHANNEL_CTRL, 0x00) != HAL_OK) st = HAL_ERROR;
    if (AXP2101_WriteReg(hi2c, AXP2101_LDO_ONOFF_CTRL0, 0x00) != HAL_OK) st = HAL_ERROR;

    return st;
}

HAL_StatusTypeDef AXP2101_ExitLowPower(I2C_HandleTypeDef *hi2c) {
    AXP2101_SetDC1Voltage(hi2c, 3300);
    HAL_Delay(5);
    return AXP2101_Init(hi2c);
}



// 官方 128 字节电量计特征参数。
// 这批参数已经保留在工程里，但具体写入协议不能简单按 A1 顺序灌 128 字节，
// 否则反而容易把电量计状态机写乱，因此这里先保留、暂不盲写。
static const uint8_t axp2101_battery_params[128] __attribute__((unused)) = {
    0x01, 0xf5, 0x00, 0x30, 0x1b, 0xe2, 0x28, 0x0f, 0x0c, 0x1e, 0x32, 0x02, 0x14, 0x05, 0x0a, 0xfd,
    0xd0, 0xfb, 0xc8, 0x0d, 0xa7, 0x10, 0x54, 0xfb, 0x46, 0x01, 0xea, 0x15, 0xcb, 0x06, 0x54, 0x06,
    0x07, 0x0a, 0xef, 0x0f, 0xa9, 0x0f, 0x50, 0x0a, 0x01, 0x0e, 0x7e, 0x0e, 0x65, 0x04, 0x4a, 0x04,
    0x33, 0x09, 0x1f, 0x0e, 0x05, 0x0d, 0xf8, 0x08, 0xde, 0x0d, 0xb7, 0x0d, 0x9d, 0x03, 0x76, 0x03,
    0x60, 0x08, 0x48, 0x0d, 0x0f, 0x0c, 0xb6, 0x07, 0x54, 0x5b, 0x2d, 0x18, 0x12, 0x02, 0x11, 0x03,
    0xc5, 0x98, 0x7e, 0x66, 0x4e, 0x44, 0x38, 0x1a, 0x12, 0x0a, 0xf6, 0x00, 0x00, 0xf6, 0x00, 0xf6,
    0x00, 0xfb, 0x00, 0x00, 0xfb, 0x00, 0x00, 0xfb, 0x00, 0x00, 0xf6, 0x00, 0x00, 0xf6, 0x00, 0xf6,
    0x00, 0xfb, 0x00, 0x00, 0xfb, 0x00, 0x00, 0xfb, 0x00, 0x00, 0xf6, 0x00, 0x00, 0xf6, 0x00, 0xf6
};
