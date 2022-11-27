/*
 * uart_send_response.c
 *
 *  Created on: Nov 27, 2022
 *      Author: ntpt
 */

#include "SchedTask/uart_send_response.h"
#include "adc.h"
#include "stm32f1xx_hal_adc.h"
#include "stm32f1xx_hal_uart.h"
#include "usart.h"
#include <inttypes.h>
#include <stdio.h>

void uart_send_response(void *param)
{
    // read the current ADC value
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
    uint32_t adc_value = HAL_ADC_GetValue(&hadc1);

    // convert the value to a string
    uint8_t buff[15]; // big enough size for uint32_t
    size_t len = sprintf((char*) &buff, "%"PRIu32 "\r\n", adc_value);

    // print adc value to serial
    HAL_UART_Transmit(&huart2, (uint8_t*) &buff, len, 50);
}
