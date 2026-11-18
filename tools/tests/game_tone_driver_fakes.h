#pragma once
// Narrow host substitutes for the Buzzer_Driver extraction. Everything the
// driver consults (GlobalStore, SysAudio, preferences, session predicate) is the
// real production header; only the MCU timer and HAL tick are faked.

#include <stddef.h>
#include <stdint.h>

#include "NanoUI/core/sensor_manager.h"
#include "ameba_session.h"
#include "game_tone_service.h"
#include "global_store.h"
#include "product_preferences.h"

struct FakeTimInstance {
    volatile uint32_t EGR;
};
struct TIM_HandleTypeDef {
    FakeTimInstance* Instance;
};
extern TIM_HandleTypeDef htim15;
extern volatile bool g_coproc_active;

#define TIM_CHANNEL_2 2u
#define TIM_EGR_UG 1u

void fakeTimSetPrescaler(uint32_t value);
void fakeTimSetCompare(uint32_t value);
void fakeTimSetAutoReload(uint32_t value);
void fakePwmStart(void);
uint32_t HAL_GetTick(void);

#define __HAL_TIM_SET_PRESCALER(handle, value) fakeTimSetPrescaler(value)
#define __HAL_TIM_SET_COMPARE(handle, channel, value) fakeTimSetCompare(value)
#define __HAL_TIM_SET_AUTORELOAD(handle, value) fakeTimSetAutoReload(value)
#define HAL_TIM_PWM_Start(handle, channel) fakePwmStart()
