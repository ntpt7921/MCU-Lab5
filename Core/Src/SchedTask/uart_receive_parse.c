/*
 * uart_receive_parse.c
 *
 *  Created on: Nov 27, 2022
 *      Author: ntpt
 */

#include "SchedTask/uart_receive_parse.h"
#include "SchedTask/uart_send_response.h"
#include "Custom/circular_buffer.h"
#include "Custom/scheduler.h"
#include "stm32f103xb.h"
#include "stm32f1xx_hal_uart.h"
#include "usart.h"

#define START_CMD ((const uint8_t*) "!RST#")
#define START_CMD_LEN (5)
#define END_CMD ((const uint8_t*) "!OK#")
#define END_CMD_LEN (4)


static uint8_t cirbuff[BUFFER_SIZE];
static size_t buff_head;
static size_t buff_count;
static uint8_t read_char;
static size_t start_cmd_curr_pos;
static size_t end_cmd_curr_pos;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        HAL_UART_Receive_IT(huart, &read_char, 1);
        // if buffer is full, the data will be discarded
        Custom_CirBuff_Insert(&cirbuff, BUFFER_SIZE, sizeof(uint8_t), &buff_head, &buff_count, &read_char);
        // print back the character read
        HAL_UART_Transmit(huart, &read_char, 1, 10);
    }
}

static uint8_t parse_command(const uint8_t *cmd, size_t cmd_len, size_t *curr_pos)
{
    if (buff_count > 0)
    {
        if (cirbuff[buff_head] == cmd[*curr_pos])
        {
            (*curr_pos)++;
        }
        else
        {
            *curr_pos = 0;
        }
    }

    if (*curr_pos >= cmd_len)
    {
        *curr_pos = 0;
        return 1;
    }

    return 0;
}

void uart_receive_init(void)
{
    buff_head = 0;
    buff_count = 0;
    start_cmd_curr_pos = 0;
    end_cmd_curr_pos = 0;
    HAL_UART_Receive_IT(&huart2, &read_char, 1);
}

void uart_receive_parse(void *param)
{
    // parse the newest character read (if exist)
    if (parse_command(START_CMD, START_CMD_LEN, &start_cmd_curr_pos))
    {
        Custom_Scheduler_Add(uart_send_response, NULL, 0,
                CUSTOM_SCHEDULER_MS_TO_TICK(3000), 0, TASK_SEND_ID);
    }
    else if (parse_command(END_CMD, END_CMD_LEN, &end_cmd_curr_pos))
    {
        Custom_Scheduler_Delete(TASK_SEND_ID);
    }

    // then clear that character
    if (buff_count > 0)
    {
        Custom_CirBuff_Delete(BUFFER_SIZE, &buff_head, &buff_count);
    }
}
