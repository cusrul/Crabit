#ifndef USB_COMPOSITE_HOST_USART_H
#define USB_COMPOSITE_HOST_USART_H
#include "main.h"
typedef struct { struct { uint32_t BaudRate; } Init; } UART_HandleTypeDef;
typedef struct { struct { uint32_t Mode; } Init; } DMA_HandleTypeDef;
#define DMA_CIRCULAR 1U
int HAL_UART_DeInit(UART_HandleTypeDef *uart);
int HAL_UART_Init(UART_HandleTypeDef *uart);
int HAL_DMA_Init(DMA_HandleTypeDef *dma);
int HAL_UART_Receive_DMA(UART_HandleTypeDef *uart, uint8_t *data, uint16_t size);
#endif
