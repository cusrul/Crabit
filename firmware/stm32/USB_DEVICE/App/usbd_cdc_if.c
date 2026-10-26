/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @version        : v2.0_Cube
  * @brief          : Usb device for Virtual Com Port.
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
#include "usbd_cdc_if.h"

/* USER CODE BEGIN INCLUDE */

#include "usart.h"  // ?? ????????? huart1
#include "usb_composite_config.h"
#include "usb_diag.h"
#include <string.h>

/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief Usb device library.
  * @{
  */

/** @addtogroup USBD_CDC_IF
  * @{
  */

/** @defgroup USBD_CDC_IF_Private_TypesDefinitions USBD_CDC_IF_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Defines USBD_CDC_IF_Private_Defines
  * @brief Private defines.
  * @{
  */

/* USER CODE BEGIN PRIVATE_DEFINES */
/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Macros USBD_CDC_IF_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Variables USBD_CDC_IF_Private_Variables
  * @brief Private variables.
  * @{
  */
/* Create buffer for reception and transmission           */
/* It's up to user to redefine and/or remove those define */
/** Received data over USB are stored in this buffer      */
uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];

/** Data to send over USB CDC are stored in this buffer   */
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

/* USER CODE BEGIN PRIVATE_VARIABLES */

extern UART_HandleTypeDef huart1;
extern volatile uint8_t bw16_flash_mode;
extern uint8_t uart_rx_ring_buf[]; // ????? UART ?????

// ?? ?????????????? USB ???
volatile uint8_t usb_rx_ready = 0;
volatile uint32_t usb_rx_len = 0;

/* USER CODE END PRIVATE_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Exported_Variables USBD_CDC_IF_Exported_Variables
  * @brief Public variables.
  * @{
  */

extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_FunctionPrototypes USBD_CDC_IF_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */

/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
  * @}
  */

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS =
{
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS,
  CDC_TransmitCplt_FS
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Initializes the CDC media low layer over the FS USB IP
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Init_FS(void)
{
  /* USER CODE BEGIN 3 */
  /* Set Application Buffers */
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0, CRABIT_USB_CDC_CLASS);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  CrabitUsb_DiagReset();
  return (USBD_OK);
  /* USER CODE END 3 */
}

/**
  * @brief  DeInitializes the CDC media low layer
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_DeInit_FS(void)
{
  /* USER CODE BEGIN 4 */
  CrabitUsb_DiagReset();
  return (USBD_OK);
  /* USER CODE END 4 */
}

/**
  * @brief  Manage the CDC class requests
  * @param  cmd: Command code
  * @param  pbuf: Buffer containing command data (request parameters)
  * @param  length: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  /* USER CODE BEGIN 5 */
  switch(cmd)
  {
    case CDC_SEND_ENCAPSULATED_COMMAND:

    break;

    case CDC_GET_ENCAPSULATED_RESPONSE:

    break;

    case CDC_SET_COMM_FEATURE:

    break;

    case CDC_GET_COMM_FEATURE:

    break;

    case CDC_CLEAR_COMM_FEATURE:

    break;

  /*******************************************************************************/
  /* Line Coding Structure                                                       */
  /*-----------------------------------------------------------------------------*/
  /* Offset | Field       | Size | Value  | Description                          */
  /* 0      | dwDTERate   |   4  | Number |Data terminal rate, in bits per second*/
  /* 4      | bCharFormat |   1  | Number | Stop bits                            */
  /*                                        0 - 1 Stop bit                       */
  /*                                        1 - 1.5 Stop bits                    */
  /*                                        2 - 2 Stop bits                      */
  /* 5      | bParityType |  1   | Number | Parity                               */
  /*                                        0 - None                             */
  /*                                        1 - Odd                              */
  /*                                        2 - Even                             */
  /*                                        3 - Mark                             */
  /*                                        4 - Space                            */
  /* 6      | bDataBits  |   1   | Number Data bits (5, 6, 7, 8 or 16).          */
  /*******************************************************************************/
    case CDC_SET_LINE_CODING:
    {
        /* USER CODE BEGIN SET_LINE_CODING */
        if (pbuf == NULL || length < 7U) return (USBD_FAIL);
        // 1. ???????????
        uint32_t baudrate = (uint32_t)(pbuf[0] | (pbuf[1] << 8) | (pbuf[2] << 16) | (pbuf[3] << 24));
        
        /* Host serial-open probes must not reconfigure the normal UI UART. */
        if (bw16_flash_mode && baudrate != 0 && huart1.Init.BaudRate != baudrate) {
            HAL_UART_DeInit(&huart1);
            huart1.Init.BaudRate = baudrate;
            HAL_UART_Init(&huart1);
            
            // 2. ?????????????????? DMA???? Init ??? NORMAL ??
            if (bw16_flash_mode) {
                extern volatile uint16_t ring_last_head;
                extern DMA_HandleTypeDef hdma_usart1_rx;
                
                // ?? ?????????????????
                hdma_usart1_rx.Init.Mode = DMA_CIRCULAR; 
                HAL_DMA_Init(&hdma_usart1_rx);

                ring_last_head = 0; 
                HAL_UART_Receive_DMA(&huart1, uart_rx_ring_buf, 2048); 
            }
        }
        /* USER CODE END SET_LINE_CODING */
    }

    break;

    case CDC_GET_LINE_CODING:
      if (pbuf == NULL || length < 7U) return (USBD_FAIL);
      pbuf[0] = (uint8_t)(huart1.Init.BaudRate & 0xFFU);
      pbuf[1] = (uint8_t)((huart1.Init.BaudRate >> 8) & 0xFFU);
      pbuf[2] = (uint8_t)((huart1.Init.BaudRate >> 16) & 0xFFU);
      pbuf[3] = (uint8_t)((huart1.Init.BaudRate >> 24) & 0xFFU);
      pbuf[4] = 0U; /* one stop bit */
      pbuf[5] = 0U; /* no parity */
      pbuf[6] = 8U; /* eight data bits */

    break;

    case CDC_SET_CONTROL_LINE_STATE:

    break;

    case CDC_SEND_BREAK:

    break;

  default:
    break;
  }

  return (USBD_OK);
  /* USER CODE END 5 */
}

/**
  * @brief  Data received over USB OUT endpoint are sent over CDC interface
  *         through this function.
  *
  *         @note
  *         This function will issue a NAK packet on any OUT packet received on
  *         USB endpoint until exiting this function. If you exit this function
  *         before transfer is complete on CDC interface (ie. using DMA controller)
  *         it will result in receiving more data while previous ones are still
  *         not sent.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  /* USER CODE BEGIN 6 */
  if(bw16_flash_mode) {
      // 1. ??????????????? ready ???? UART ??
      usb_rx_len = *Len;
      usb_rx_ready = 1;
      
      // ?? ??????????? USBD_CDC_ReceivePacket()?
      // ?????????????? NAK??????????????????
      return (USBD_OK); 
  }

  CrabitUsb_DiagReceive(Buf, *Len);
  
  // ?? UI ????????????????????
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
  /* USER CODE END 6 */
}

/**
  * @brief  CDC_Transmit_FS
  *         Data to send over USB IN endpoint are sent over CDC interface
  *         through this function.
  *         @note
  *
  *
  * @param  Buf: Buffer of data to be sent
  * @param  Len: Number of data to be sent (in bytes)
  * @retval USBD_OK if all operations are OK else USBD_FAIL or USBD_BUSY
  */
uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
  uint8_t result = USBD_OK;
  uint32_t primask;
  uint8_t savedClassId;
  /* USER CODE BEGIN 7 */
  if (Buf == NULL || Len == 0 || Len > APP_TX_DATA_SIZE){
    return USBD_FAIL;
  }
  primask = __get_PRIMASK();
  __disable_irq();
  savedClassId = hUsbDeviceFS.classId;
  hUsbDeviceFS.classId = CRABIT_USB_CDC_CLASS;
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassDataCmsit[CRABIT_USB_CDC_CLASS];
  if (hcdc == NULL || hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
    hUsbDeviceFS.classId = savedClassId;
    __set_PRIMASK(primask);
    return USBD_FAIL;
  }
  if (hcdc->TxState != 0){
    hUsbDeviceFS.classId = savedClassId;
    __set_PRIMASK(primask);
    return USBD_BUSY;
  }
  memcpy(UserTxBufferFS, Buf, Len);
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, Len, CRABIT_USB_CDC_CLASS);
  result = USBD_CDC_TransmitPacket(&hUsbDeviceFS, CRABIT_USB_CDC_CLASS);
  hUsbDeviceFS.classId = savedClassId;
  __set_PRIMASK(primask);
  /* USER CODE END 7 */
  return result;
}

/**
  * @brief  CDC_TransmitCplt_FS
  *         Data transmitted callback
  *
  *         @note
  *         This function is IN transfer complete callback used to inform user that
  *         the submitted Data is successfully sent over USB.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 13 */
  UNUSED(Buf);
  UNUSED(Len);
  UNUSED(epnum);
  /* USER CODE END 13 */
  return result;
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */
// UART 转发完成后，允许 USB 接收下一包数据。
void CDC_Resume_Receive(void) {
    uint32_t primask = __get_PRIMASK();
    uint8_t savedClassId;
    __disable_irq();
    savedClassId = hUsbDeviceFS.classId;
    hUsbDeviceFS.classId = CRABIT_USB_CDC_CLASS;
    if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED &&
        hUsbDeviceFS.pClassDataCmsit[CRABIT_USB_CDC_CLASS] != NULL &&
        USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS) == USBD_OK) {
        USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    }
    hUsbDeviceFS.classId = savedClassId;
    __set_PRIMASK(primask);
}
/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @}
  */

/**
  * @}
  */
