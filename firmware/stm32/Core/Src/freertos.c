/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */





#include "spi.h"
extern void NanoUI_MainLoop(void);

// OLED 控制引脚：DC=PA15，RST=PB4。
#define OLED_DC_CMD()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET)
#define OLED_DC_DATA() HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET)
#define OLED_RST_CLR() HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET)
#define OLED_RST_SET() HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET)
void OLED_Init(void);
void OLED_Fill(uint8_t data);
// 硬件上 CS 未连接，因此不提供片选宏。
// #define OLED_CS_CLR() HAL_GPIO_WritePin(GPIOB, GPIO_PIN_x, GPIO_PIN_RESET)
// #define OLED_CS_SET() HAL_GPIO_WritePin(GPIOB, GPIO_PIN_x, GPIO_PIN_SET)

void OLED_WR_Byte(uint8_t dat, uint8_t cmd) {
    if(cmd) OLED_DC_DATA();
    else    OLED_DC_CMD();
    
    // OLED_CS_CLR();
    HAL_SPI_Transmit(&hspi3, &dat, 1, 10);
    // OLED_CS_SET();
}








/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 1024 * 6,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  (void)argument;
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN StartDefaultTask */
  // 设置 PA2 电平并等待启动稳定，再初始化 OLED。
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2,GPIO_PIN_SET);
  vTaskDelay(pdMS_TO_TICKS(500)); // 启动等待，单位为毫秒。
  OLED_Init();
  
  // 将屏幕内容清为黑色。
  OLED_Fill(0x00); 
  
  // NanoUI_MainLoop() 接管该任务的 UI 循环。
  NanoUI_MainLoop();

  /* NanoUI 主循环意外返回时的后备循环。 */
  for(;;)
  {
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_3); // 后备循环翻转 PA3。
    osDelay(500);
  }



  // =========================================================
// 以下为已停用的实验代码：STM32 交出 SPI/OLED 控制权后的引脚处理。
// =========================================================

// 1. 先给 BW16 上电并等待稳定
// HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
// vTaskDelay(pdMS_TO_TICKS(100));

// 2. 把 STM32 侧的 OLED 相关引脚交给 BW16（旧方案）
// GPIO_InitTypeDef GPIO_InitStruct = {0};
// GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
// GPIO_InitStruct.Pull = GPIO_NOPULL;

// // 涉及 PB3(SCK), PB4(RST), PB5(MOSI)
// GPIO_InitStruct.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
// HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

// // 以及 PA15(DC)
// GPIO_InitStruct.Pin = GPIO_PIN_15;
// HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

// // 3. 释放 SPI 外设
// HAL_SPI_DeInit(&hspi3);

// // 4. STM32 侧空转心跳
// while(1) {
//     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_SET); 
//     vTaskDelay(pdMS_TO_TICKS(100));
//     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET); 
//     vTaskDelay(pdMS_TO_TICKS(900));
// }


  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */




void OLED_Init(void) {
    OLED_RST_SET();
    osDelay(50);    // 上电稳定
    OLED_RST_CLR();
    osDelay(100);   // 保持复位低电平 100ms
    OLED_RST_SET();
    osDelay(100);   // 等待复位释放后的稳压
    OLED_RST_SET();
    
    OLED_WR_Byte(0xAE,0); // 关闭显示
    OLED_WR_Byte(0xD5,0); // 设置时钟分频
    OLED_WR_Byte(0x80,0);
    OLED_WR_Byte(0xA8,0); // 设置复用率
    OLED_WR_Byte(0x3F,0);
    OLED_WR_Byte(0xD3,0); // 设置显示偏移
    OLED_WR_Byte(0x00,0);
    
    // 起始行地址 (0x40)
    OLED_WR_Byte(0x40, 0); // 起始行 0

    
    OLED_WR_Byte(0x8D,0); // 电荷泵设置
    OLED_WR_Byte(0x14,0); // 使能电荷泵
    OLED_WR_Byte(0x20,0); // 设置内存寻址模式
    OLED_WR_Byte(0x02,0); // 页寻址模式
    OLED_WR_Byte(0xA1,0); // 段重映射 (左右镜像)
    OLED_WR_Byte(0xC8,0); // COM 扫描方向 (上下翻转)
    OLED_WR_Byte(0xDA,0); // COM 引脚配置
    OLED_WR_Byte(0x12,0);
    OLED_WR_Byte(0x81,0); // 对比度设置
    OLED_WR_Byte(0xCF,0);
    OLED_WR_Byte(0xD9,0); // 预充电周期
    OLED_WR_Byte(0xF1,0);
    OLED_WR_Byte(0xDB,0); // VCOMH 电压
    OLED_WR_Byte(0x40,0);
    OLED_WR_Byte(0xA4,0); // 恢复 RAM 内容显示
    OLED_WR_Byte(0xA6,0); // 正常显示 (非反色)
    OLED_WR_Byte(0xAF,0); // 开启显示
}

// 以给定字节填满整屏。
void OLED_Fill(uint8_t data) {
    uint8_t i, n;
    for(i=0; i<8; i++) {
        OLED_WR_Byte(0xB0+i, 0); // 页起始地址
        OLED_WR_Byte(0x00, 0);   // 列低 4 位 = 0
        OLED_WR_Byte(0x10, 0);   // 列高 4 位 = 0
        for(n=0; n<128; n++) OLED_WR_Byte(data, 1);
    }
}


/* USER CODE END Application */

