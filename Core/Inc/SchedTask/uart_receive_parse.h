/*
 * uart_receive_parse.h
 *
 *  Created on: Nov 27, 2022
 *      Author: ntpt
 */

#ifndef INC_SCHEDTASK_UART_RECEIVE_PARSE_H_
#define INC_SCHEDTASK_UART_RECEIVE_PARSE_H_

#define BUFFER_SIZE 10

void uart_receive_init(void);
void uart_receive_parse(void *param);

#endif /* INC_SCHEDTASK_UART_RECEIVE_PARSE_H_ */
