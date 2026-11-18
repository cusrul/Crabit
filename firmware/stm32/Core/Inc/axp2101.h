#ifndef AXP2101_H
#define AXP2101_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// AXP2101 I2C Slave Address
#define AXP2101_ADDR        (0x34 << 1)

// =========================================================
// AXP2101 Register Map (Based on XPowersLib)
// =========================================================
#define AXP2101_STATUS1                          (0x00)
#define AXP2101_STATUS2                          (0x01)
#define AXP2101_IC_TYPE                          (0x03)
#define AXP2101_COMMON_CONFIG                    (0x10)
#define AXP2101_BATFET_CTRL                      (0x12)
#define AXP2101_DIE_TEMP_CTRL                    (0x13)
#define AXP2101_MIN_SYS_VOL_CTRL                 (0x14)
#define AXP2101_INPUT_VOL_LIMIT_CTRL             (0x15)
#define AXP2101_INPUT_CUR_LIMIT_CTRL             (0x16)
#define AXP2101_RESET_FUEL_GAUGE                 (0x17)
#define AXP2101_CHARGE_GAUGE_WDT_CTRL            (0x18)
#define AXP2101_WDT_CTRL                         (0x19)
#define AXP2101_LOW_BAT_WARN_SET                 (0x1A)
#define AXP2101_PWRON_STATUS                     (0x20)
#define AXP2101_PWROFF_STATUS                    (0x21)
#define AXP2101_PWROFF_EN                        (0x22)
#define AXP2101_DC_OVP_UVP_CTRL                  (0x23)
#define AXP2101_VOFF_SET                         (0x24)
#define AXP2101_PWROK_SEQU_CTRL                  (0x25)
#define AXP2101_SLEEP_WAKEUP_CTRL                (0x26)
#define AXP2101_IRQ_OFF_ON_LEVEL_CTRL            (0x27)

#define AXP2101_ADC_CHANNEL_CTRL                 (0x30)
#define AXP2101_ADC_DATA_RELUST0                 (0x34) // VBAT H
#define AXP2101_ADC_DATA_RELUST1                 (0x35) // VBAT L
#define AXP2101_ADC_DATA_RELUST2                 (0x36) // CURR H
#define AXP2101_ADC_DATA_RELUST3                 (0x37) // CURR L
#define AXP2101_ADC_DATA_RELUST4                 (0x38) // VBUS H
#define AXP2101_ADC_DATA_RELUST5                 (0x39) // VBUS L
#define AXP2101_ADC_DATA_RELUST6                 (0x3A) // VSYS H
#define AXP2101_ADC_DATA_RELUST7                 (0x3B) // VSYS L
#define AXP2101_ADC_DATA_RELUST8                 (0x3C) // TEMP H
#define AXP2101_ADC_DATA_RELUST9                 (0x3D) // TEMP L

#define AXP2101_BAT_DET_CTRL                     (0x68)
#define AXP2101_DC_ONOFF_DVM_CTRL                (0x80)
#define AXP2101_DC_VOL0_CTRL                     (0x82) // DCDC1
#define AXP2101_DC_VOL1_CTRL                     (0x83) // DCDC2
#define AXP2101_DC_VOL2_CTRL                     (0x84) // DCDC3
#define AXP2101_DC_VOL3_CTRL                     (0x85) // DCDC4
#define AXP2101_DC_VOL4_CTRL                     (0x86) // DCDC5

#define AXP2101_LDO_ONOFF_CTRL0                  (0x90)
#define AXP2101_LDO_ONOFF_CTRL1                  (0x91)
#define AXP2101_LDO_VOL0_CTRL                    (0x92) // ALDO1
#define AXP2101_LDO_VOL1_CTRL                    (0x93) // ALDO2
#define AXP2101_LDO_VOL2_CTRL                    (0x94) // ALDO3
#define AXP2101_LDO_VOL3_CTRL                    (0x95) // ALDO4
#define AXP2101_LDO_VOL4_CTRL                    (0x96) // BLDO1
#define AXP2101_LDO_VOL5_CTRL                    (0x97) // BLDO2

#define AXP2101_BAT_PARAME                       (0xA1)
#define AXP2101_FUEL_GAUGE_CTRL                  (0xA2)
#define AXP2101_BAT_PERCENT_DATA                 (0xA4)

// =========================================================
// Functions
// =========================================================

/**
 * @brief Configure board power rails, input limit and ADC channels.
 * @return HAL_StatusTypeDef
 */
HAL_StatusTypeDef AXP2101_Init(void);

// Boot-only optional gauge setup. Failure does not affect VBAT or charging reads.
bool AXP2101_InitFuelGauge(void);

/**
 * @brief Read battery voltage in mV
 * @param out_mv Receives the raw ADC value. Untouched when the read fails.
 * @return true only when both ADC bytes were read successfully.
 */
bool AXP2101_TryGetBattVoltage(uint16_t *out_mv);

/**
 * @brief Read battery voltage in mV
 * @return Raw ADC value, or 0 when the read failed. 0 is not a measurement;
 *         use AXP2101_TryGetBattVoltage to distinguish failure from data.
 */
uint16_t AXP2101_GetBattVoltage(void);

/**
 * @brief Read the raw fuel-gauge percentage byte without masking.
 *
 * Success requires gauge setup, a connected battery, a successful 0xA4 read, and a raw
 * value in 0..100. Any other condition means the percentage is unknown:
 * register reads never invent a level, and datasheet-invalid bytes such as
 * 0xE3 (>=101) are rejected rather than masked into a plausible 0..100 value.
 *
 * @param out_percent Receives 0..100. Untouched on failure.
 * @return true when out_percent holds a valid gauge reading.
 */
bool AXP2101_TryGetBatteryPercent(uint8_t *out_percent);

/**
 * @brief Legacy percentage accessor (0-100)
 * @return A valid gauge reading, or 0 when the percentage is unknown.
 *         Use AXP2101_TryGetBatteryPercent where unknown matters.
 */
uint8_t AXP2101_GetBatteryPercent(void);

/**
 * @brief Read internal temperature in Celsius
 */
float AXP2101_GetInternalTemp(void);

/**
 * @brief Read VBUS voltage in mV
 */
uint16_t AXP2101_GetVbusVoltage(void);

/**
 * @brief Check if VBUS is present
 */
bool AXP2101_IsVbusIn(void);

/**
 * @brief Check if charging is active
 * @param out_charging Receives the charge state. Untouched when the read fails.
 * @return true when STATUS2 was read successfully.
 */
bool AXP2101_TryGetCharging(bool *out_charging);

/**
 * @brief Legacy charge-state accessor
 * @return true only when charging was read successfully and is active.
 *         false also means "not charging or unknown".
 */
bool AXP2101_IsCharging(void);

/**
 * @brief Set DCDC1 voltage
 */
HAL_StatusTypeDef AXP2101_SetDC1Voltage(uint16_t mv);
// Write, wait, then verify voltage selection and output enable. This verifies
// PMIC configuration only; it is not an analog measurement of STM32 VDD.
HAL_StatusTypeDef AXP2101_SetDC1VoltageChecked(uint16_t mv);

/**
 * @brief Set DCDC3 voltage (Special handling for ranges)
 */
HAL_StatusTypeDef AXP2101_SetDC3Voltage(uint16_t mv);

/**
 * @brief Enable or disable DCDC3 output
 */
HAL_StatusTypeDef AXP2101_SetDC3Enable(bool enable);

/**
 * @brief Get current DCDC3 voltage setting in mV
 */
uint16_t AXP2101_GetDC3Voltage(void);

// 兼容性接口：直接写入寄存器值以进行步进测试
void AXP2101_SetDCDC3_Test(uint8_t val);

/**
 * @brief Set ALDO voltage
 * @param channel 1-4 for ALDO1-4
 */
HAL_StatusTypeDef AXP2101_SetALDOVoltage(uint8_t channel, uint16_t mv);

/**
 * @brief Reduce AXP2101-powered domains before STM32 STOP2.
 */
HAL_StatusTypeDef AXP2101_EnterLowPower(void);

/**
 * @brief Restore AXP2101 rails after STM32 wakes from STOP2.
 */
HAL_StatusTypeDef AXP2101_ExitLowPower(void);

// Utility
HAL_StatusTypeDef AXP2101_WriteReg(uint8_t reg, uint8_t val);
HAL_StatusTypeDef AXP2101_ReadReg(uint8_t reg, uint8_t *val);

#ifdef __cplusplus
}
#endif

#endif // AXP2101_H
