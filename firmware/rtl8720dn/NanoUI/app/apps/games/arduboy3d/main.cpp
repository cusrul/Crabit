
#include "Game.h"
#include "Draw.h"
#include "FixedMath.h"
#include "Platform.h"

Arduboy2Base arduboy;
ArduboyTones sound(arduboy.audio.enabled);
Sprites sprites;

uint8_t Platform::GetInput()
{
  uint8_t result = 0;
  
  if(arduboy.pressed(A_BUTTON))
  {
    result |= INPUT_A;  
  }
  if(arduboy.pressed(B_BUTTON))
  {
    result |= INPUT_B;  
  }
  if(arduboy.pressed(UP_BUTTON))
  {
    result |= INPUT_UP;  
  }
  if(arduboy.pressed(DOWN_BUTTON))
  {
    result |= INPUT_DOWN;  
  }
  if(arduboy.pressed(LEFT_BUTTON))
  {
    result |= INPUT_LEFT;  
  }
  if(arduboy.pressed(RIGHT_BUTTON))
  {
    result |= INPUT_RIGHT;  
  }

  return result;
}

void Platform::PlaySound(const uint16_t* audioPattern)
{
  sound.tones(audioPattern);
}

void Platform::SetLED(uint8_t r, uint8_t g, uint8_t b)
{
  arduboy.digitalWriteRGB(r ? RGB_ON : RGB_OFF, g ? RGB_ON : RGB_OFF, b ? RGB_ON : RGB_OFF);
}

void Platform::PutPixel(uint8_t x, uint8_t y, uint8_t colour)
{
  arduboy.drawPixel(x, y, colour);
}

// 修改点 1：移除 PROGMEM
// 移除 AVR 特有的 PROGMEM，在 BW16 (ARM) 上，const 数组默认就存在 Flash 中。
const uint8_t topmask_[] = {
  0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80 };
const uint8_t bottommask_[] = {
  0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff };

void Platform::DrawVLine(uint8_t x, int8_t y0_, int8_t y1_, uint8_t pattern) 
{
  if (x >= WIDTH) return;
  uint8_t *screenptr = arduboy.getBuffer() + x;

  if (y1_ < y0_ || y1_ < 0 || y0_ > 63) return;

  uint8_t y0 = y0_, y1 = y1_;
  if (y0_ < 0) y0 = 0;
  if (y1_ > 63) y1 = 63;

  uint8_t *page0 = screenptr + ((y0 & 0x38) << 4);
  uint8_t *page1 = screenptr + ((y1 & 0x38) << 4);
  
  if (page0 == page1) 
  {
    // 修改点 2：替换 pgm_read_byte 为直接数组访问
    uint8_t mask = topmask_[y0 & 7] & bottommask_[y1 & 7];
    *page0 &= ~mask;
    *page0 |= pattern & mask;
  }
  else
  {
    // 替换 pgm_read_byte
    uint8_t mask = topmask_[y0 & 7];
    *page0 &= ~mask;
    *page0 |= pattern & mask;  
    page0 += 128;
    while (page0 != page1) 
    {
      *page0 = pattern;  
      page0 += 128;
    }
    // 替换 pgm_read_byte
    mask = bottommask_[y1 & 7];  
    *page0 &= ~mask;
    *page0 |= pattern & mask;
  }
}

uint8_t* Platform::GetScreenBuffer()
{
  return arduboy.getBuffer();
}

void Platform::DrawSprite(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame)
{
  sprites.drawPlusMask(x, y, bitmap, frame);
}

void Platform::DrawSprite(int16_t x, int16_t y, const uint8_t *bitmap,
  const uint8_t *mask, uint8_t frame, uint8_t mask_frame)
{
  sprites.drawExternalMask(x, y, bitmap, mask, frame, mask_frame);
}

// 修改点 3：Bitmap 绘制也需要移除 pgm_read_byte
void Platform::DrawBitmap(int16_t x, int16_t y, const uint8_t *bitmap)
{
  uint8_t w = bitmap[0];
  uint8_t h = bitmap[1];
  arduboy.drawBitmap(x, y, bitmap + 2, w, h);
}

void Platform::DrawSolidBitmap(int16_t x, int16_t y, const uint8_t *bitmap)
{
  uint8_t w = bitmap[0];
  uint8_t h = bitmap[1];
  arduboy.fillRect(x, y, w, h, BLACK);
  arduboy.drawBitmap(x, y, bitmap + 2, w, h);
}

void Platform::FillScreen(uint8_t colour)
{
  arduboy.fillScreen(colour);
}

unsigned long lastTimingSample;

bool Platform::IsAudioEnabled()
{
  return arduboy.audio.enabled();
}

void Platform::SetAudioEnabled(bool isEnabled)
{
  if(isEnabled)
    arduboy.audio.on();
  else
    arduboy.audio.off();
}

void Platform::ExpectLoadDelay()
{
  lastTimingSample = millis();
}

void setup()
{
  arduboy.boot();
  arduboy.flashlight();
  arduboy.systemButtons();
  arduboy.setFrameRate(TARGET_FRAMERATE);

  Game::Init();
  
  lastTimingSample = millis();
}

void loop()
{
  // 修改点 4：主循环的补帧逻辑
  static int32_t tickAccum = 0; 
  unsigned long timingSample = millis();
  tickAccum += (timingSample - lastTimingSample);
  lastTimingSample = timingSample;
  
#if DEV_MODE
  if(arduboy.nextFrameDEV())
#else
  if(arduboy.nextFrame())
#endif
  {
    constexpr int32_t frameDuration = 1000 / TARGET_FRAMERATE;
    
    // 限制最大积攒时间为1帧的时间，坚决不允许连续执行Tick
    if (tickAccum > frameDuration) {
        tickAccum = frameDuration; 
    }

    // 将 while 改为 if，每帧最多只执行一次物理逻辑
    if(tickAccum >= frameDuration)
    {
      Game::Tick();
      tickAccum -= frameDuration;
    }
  
    Game::Draw();
    arduboy.display(false);
  }
}