// SSD1306 panel driver. The only product file that touches hspi3 or the
// panel control pins.
#include "display.h"

#include "cmsis_os.h"
#include "main.h"
#include "spi.h"

// OLED 控制引脚：DC=PA15，RST=PB4。
#define OLED_DC_CMD()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET)
#define OLED_DC_DATA() HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET)
#define OLED_RST_CLR() HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET)
#define OLED_RST_SET() HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET)

void Display::writeByte(uint8_t data, uint8_t cmd) {
    if (cmd) OLED_DC_DATA();
    else OLED_DC_CMD();
    HAL_SPI_Transmit(&hspi3, &data, 1, 10);
}

void Display::init() {
    OLED_RST_SET();
    osDelay(50);
    OLED_RST_CLR();
    osDelay(100);
    OLED_RST_SET();
    osDelay(100);
    OLED_RST_SET();

    writeByte(0xAE, 0);  // 关闭显示
    writeByte(0xD5, 0);  // 设置时钟分频
    writeByte(0x80, 0);
    writeByte(0xA8, 0);  // 设置复用率
    writeByte(0x3F, 0);
    writeByte(0xD3, 0);  // 设置显示偏移
    writeByte(0x00, 0);

    writeByte(0x40, 0);  // 起始行 0

    writeByte(0x8D, 0);  // 电荷泵设置
    writeByte(0x14, 0);  // 使能电荷泵
    writeByte(0x20, 0);  // 设置内存寻址模式
    writeByte(0x02, 0);  // 页寻址模式
    writeByte(0xA1, 0);  // 段重映射 (左右镜像)
    writeByte(0xC8, 0);  // COM 扫描方向 (上下翻转)
    writeByte(0xDA, 0);  // COM 引脚配置
    writeByte(0x12, 0);
    writeByte(0x81, 0);  // 对比度设置
    writeByte(0xCF, 0);
    writeByte(0xD9, 0);  // 预充电周期
    writeByte(0xF1, 0);
    writeByte(0xDB, 0);  // VCOMH 电压
    writeByte(0x40, 0);
    writeByte(0xA4, 0);  // 恢复 RAM 内容显示
    writeByte(0xA6, 0);  // 正常显示 (非反色)
    writeByte(0xAF, 0);  // 开启显示
}

void Display::fill(uint8_t data) {
    for (uint8_t page = 0; page < 8; ++page) {
        writeByte(0xB0 + page, 0);
        writeByte(0x00, 0);
        writeByte(0x10, 0);
        for (uint8_t column = 0; column < 128; ++column)
            writeByte(data, 1);
    }
}

void Display::setSleep(bool sleep) {
    if (sleep) {
        writeByte(0xAE, 0);
        writeByte(0x8D, 0);
        writeByte(0x10, 0);
    } else {
        writeByte(0x8D, 0);
        writeByte(0x14, 0);
        writeByte(0xAF, 0);
    }
}

void Display::setBrightness(float brightness) {
    if (!(brightness >= 0.0f && brightness <= 1.0f))
        return;
    writeByte(0x81, 0);
    writeByte(static_cast<uint8_t>(brightness * 255.0f), 0);
    writeByte(0xD9, 0);
    writeByte(brightness < 0.15f ? 0x11 : 0x22, 0);
    writeByte(0xDB, 0);
    writeByte(brightness < 0.15f ? 0x00 : 0x20, 0);
}

void Display::setInvert(bool invert) {
    writeByte(invert ? 0xA7 : 0xA6, 0);
}

void Display::present(const uint8_t* buffer) {
    for (int page = 0; page < 8; ++page) {
        writeByte(0xB0 + page, 0);
        writeByte(0x00, 0);
        writeByte(0x10, 0);
        OLED_DC_DATA();
        if (HAL_SPI_Transmit_DMA(&hspi3, const_cast<uint8_t*>(&buffer[page * 128]), 128) != HAL_OK) {
            HAL_SPI_Abort(&hspi3);
            return;
        }
        const uint32_t start = HAL_GetTick();
        while (HAL_SPI_GetState(&hspi3) != HAL_SPI_STATE_READY) {
            if (HAL_GetTick() - start >= 100) {
                HAL_SPI_Abort(&hspi3);
                return;
            }
        }
    }
}

bool Display::releaseBus() {
    const bool ok = HAL_SPI_DeInit(&hspi3) == HAL_OK;
    GPIO_InitTypeDef pin{};
    pin.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
    pin.Mode = GPIO_MODE_ANALOG;
    pin.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &pin);
    pin.Pin = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOA, &pin);
    return ok;
}

void Display::beginClaim() {
    GPIO_InitTypeDef pin{};
    pin.Pin = GPIO_PIN_3 | GPIO_PIN_5;
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Pull = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    pin.Alternate = GPIO_AF6_SPI3;
    HAL_GPIO_Init(GPIOB, &pin);

    pin.Pin = GPIO_PIN_4;
    pin.Mode = GPIO_MODE_OUTPUT_PP;
    pin.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &pin);

    pin.Pin = GPIO_PIN_15;
    pin.Mode = GPIO_MODE_OUTPUT_PP;
    pin.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &pin);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);

    HAL_SPI_DeInit(&hspi3);
}

bool Display::finishClaim() {
    if (HAL_SPI_Init(&hspi3) != HAL_OK)
        return false;
    init();
    setSleep(false);
    return HAL_SPI_GetState(&hspi3) == HAL_SPI_STATE_READY;
}

bool Display::suspend() {
    return HAL_SPI_DeInit(&hspi3) == HAL_OK;
}

bool Display::resume() {
    MX_SPI3_Init();
    return HAL_SPI_GetState(&hspi3) == HAL_SPI_STATE_READY;
}

bool Display::ready() {
    return HAL_SPI_GetState(&hspi3) == HAL_SPI_STATE_READY;
}

// C compatibility for the CubeMX startup task and legacy call sites.
void OLED_WR_Byte(uint8_t dat, uint8_t cmd) {
    Display::writeByte(dat, cmd);
}

void OLED_Init(void) {
    Display::init();
}

void OLED_Fill(uint8_t data) {
    Display::fill(data);
}

void OLED_SetSleep(bool sleep) {
    Display::setSleep(sleep);
}
