#include <stdint.h>


#ifndef __INTERFACE_H__
#define __INTERFACE_H__


typedef enum UART_CMD_ENUM
{
	UART_CMD_Open = 0,
	UART_CMD_RunSequence = 1,
	UART_CMD_Close = 2
} UART_CMD_T;



typedef enum UART_SEQ_COMMAND_ENUM
{
	UART_SEQ_COMMAND_Clean = 0,
	UART_SEQ_COMMAND_Send = 1,
	UART_SEQ_COMMAND_Receive = 2,
	UART_SEQ_COMMAND_BaudRate = 3,
	UART_SEQ_COMMAND_Delay = 4
} UART_SEQ_COMMAND_T;



typedef struct UART_PARAMETER_OPEN_STRUCT
{
	uint32_t ptHandle;
	uint32_t ulUartCore;
	uint32_t ulBaudRate;
	uint8_t  aucMMIO[4];
	uint16_t ausPortcontrol[4];
} UART_PARAMETER_OPEN_T;



typedef struct UART_PARAMETER_RUN_SEQUENCE_STRUCT
{
	uint32_t ptHandle;
	const uint8_t *pucCommand;
	uint32_t sizCommand;
	uint8_t *pucReceivedData;
	uint32_t sizReceivedDataMax;
	uint32_t sizReceivedData;
} UART_PARAMETER_RUN_SEQUENCE_T;



typedef struct UART_PARAMETER_CLOSE_STRUCT
{
	uint32_t ptHandle;
} UART_PARAMETER_CLOSE_T;



typedef struct UART_PARAMETER_STRUCT
{
	uint32_t ulVerbose;
	uint32_t ulCommand;
	union {
		UART_PARAMETER_OPEN_T tOpen;
		UART_PARAMETER_RUN_SEQUENCE_T tRunSequence;
		UART_PARAMETER_CLOSE_T tClose;
	} uParameter;
} UART_PARAMETER_T;



typedef enum TEST_RESULT_ENUM
{
	TEST_RESULT_OK = 0,
	TEST_RESULT_ERROR = 1
} TEST_RESULT_T;


#endif  /* __INTERFACE_H__ */
