/***************************************************************************
 *   Copyright (C) 2019 by Christoph Thelen                                *
 *   doc_bacardi@users.sourceforge.net                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#include "main_test.h"

#include <string.h>

#include "netx_io_areas.h"
#include "portcontrol.h"
#include "rdy_run.h"
#include "systime.h"
#include "uprintf.h"
#include "version.h"


/*-------------------------------------------------------------------------*/


typedef struct UART_HANDLE_STRUCT
{
	HOSTADEF(UART) *ptUart;
	unsigned long ulUartIndex;
	unsigned long ulCurrentBaudRate;
	unsigned long ulCurrentDeviceSpecificSpeedValue;
} UART_HANDLE_T;



struct __attribute__((__packed__)) UART_SEQ_COMMAND_WRITE_STRUCT
{
        unsigned short usDataSize;
};

typedef union UART_SEQ_COMMAND_WRITE_UNION
{
        struct UART_SEQ_COMMAND_WRITE_STRUCT s;
        unsigned char auc[2];
} UART_SEQ_COMMAND_WRITE_T;



struct __attribute__((__packed__)) UART_SEQ_COMMAND_READ_STRUCT
{
        unsigned short usDataSize;
        unsigned short usTimeoutTotalMs;
        unsigned short usTimeoutCharMs;
};

typedef union UART_SEQ_COMMAND_READ_UNION
{
        struct UART_SEQ_COMMAND_READ_STRUCT s;
        unsigned char auc[5];
} UART_SEQ_COMMAND_READ_T;



struct __attribute__((__packed__)) UART_SEQ_COMMAND_BAUDRATE_STRUCT
{
        unsigned long ulBaudRate;
};

typedef union UART_SEQ_COMMAND_BAUDRATE_UNION
{
        struct UART_SEQ_COMMAND_BAUDRATE_STRUCT s;
        unsigned char auc[4];
} UART_SEQ_COMMAND_BAUDRATE_T;



struct __attribute__((__packed__)) UART_SEQ_COMMAND_DELAY_STRUCT
{
        unsigned long ulDelayInMs;
};

typedef union UART_SEQ_COMMAND_DELAY_UNION
{
        struct UART_SEQ_COMMAND_DELAY_STRUCT s;
        unsigned char auc[4];
} UART_SEQ_COMMAND_DELAY_T;



typedef struct CMD_STATE_STRUCT
{
	unsigned long ulVerbose;
	const unsigned char *pucCmdCnt;
	const unsigned char *pucCmdEnd;
	unsigned char *pucRecCnt;
	unsigned char *pucRecEnd;
} CMD_STATE_T;



static int getDeviceSpecificBaudRate(unsigned long ulBaudRate, unsigned long *pulDeviceSpecificSpeed)
{
	unsigned long ulDeviceFrequency;
	unsigned long long ullDiv;
	unsigned long ulDiv;
	int iResult;


	/* The frequency of all UART cores is 100MHz. */
	ulDeviceFrequency = 100000000UL;

	/* Get the baud rate divider.
	 *
	 * From the regdef:
	 *   ( (Baud Rate * 16) / System Frequency ) * 2^16
	 *
	 * = BaudRate * 16 * 65536 / SystemFrequency
	 */
	ullDiv = ulBaudRate * 16ULL * 65536ULL;
	/* Round the next integer division. */
	ullDiv += ulDeviceFrequency / 2;
	ullDiv /= ulDeviceFrequency;

	/* The UART module has only 16bits for the divider. */
	if( ullDiv>0xffff )
	{
		iResult = -1;
	}
	else
	{
		iResult = 0;

		ulDiv = (unsigned long)(ullDiv);
		*pulDeviceSpecificSpeed = ulDiv;
	}

	return iResult;
}



static int command_clean(CMD_STATE_T *ptState, const UART_HANDLE_T *ptHandle)
{
	int iResult;
	unsigned long ulValue;
	HOSTADEF(UART) *ptUartArea;
	unsigned long ulCleanCnt;


	if( ptState->ulVerbose!=0U )
	{
		uprintf("CLEAN\n");
	}

	ptUartArea = ptHandle->ptUart;

	ulCleanCnt = 0;
	while(1)
	{
		/* Check for data in the FIFO. */
		ulValue  = ptUartArea->ulUartfr;
		ulValue &= HOSTMSK(uartfr_RXFE);
		if( ulValue!=0 )
		{
			/* The FIFO is empty, nothing more to discard. */
			break;
		}
		else
		{
			/* Get the received byte. */
			ptUartArea->ulUartdr;
			++ulCleanCnt;
		}
	}

	if( ptState->ulVerbose!=0U )
	{
		uprintf("Removed %d bytes from the RX FIFO.\n", ulCleanCnt);
	}

	iResult = 0;

	return iResult;
}



static int command_receive(CMD_STATE_T *ptState, const UART_HANDLE_T *ptHandle)
{
	int iResult;
	const UART_SEQ_COMMAND_READ_T *ptCmd;
	unsigned long ulDataSize;
	unsigned long ulValue;
	unsigned long ulTimeoutTotalMs;
	unsigned long ulTimeoutCharMs;
	unsigned long ulTimerTotal;
	unsigned long ulTimerChar;
	int iElapsedTimerTotal;
	int iElapsedTimerChar;
	unsigned char *pucCnt;
	unsigned char *pucEnd;
	HOSTADEF(UART) *ptUartArea;


	if( (ptState->pucCmdCnt + sizeof(UART_SEQ_COMMAND_READ_T))>ptState->pucCmdEnd )
	{
		if( ptState->ulVerbose!=0U )
		{
			uprintf("Not enough data for the read command left.\n");
		}
		iResult = -1;
	}
	else
	{
		ptCmd = (const UART_SEQ_COMMAND_READ_T*)(ptState->pucCmdCnt);
		ulDataSize = ptCmd->s.usDataSize;
		if( (ptState->pucRecCnt + ulDataSize)>ptState->pucRecEnd )
		{
			if( ptState->ulVerbose!=0U )
			{
				uprintf("Not enough data for the receive data left.\n");
			}
			iResult = -1;
		}
		else
		{
			/* Get the timeout values. */
			ulTimeoutTotalMs = ptCmd->s.usTimeoutTotalMs;
			ulTimeoutCharMs = ptCmd->s.usTimeoutCharMs;

			if( ptState->ulVerbose!=0U )
			{
				uprintf("RECEIVE %d bytes, total timeout = %dms, char timeout = %dms\n", ulDataSize, ulTimeoutTotalMs, ulTimeoutCharMs);
			}

			/* Receive the data. */
			iResult = 0;
			ptUartArea = ptHandle->ptUart;
			ulTimerTotal = systime_get_ms();
			pucCnt = ptState->pucRecCnt;
			pucEnd = ptState->pucRecCnt + ulDataSize;
			iElapsedTimerTotal = 0;
			iElapsedTimerChar = 0;
			while(pucCnt<pucEnd)
			{
				/* Wait for data in the FIFO. */
				ulTimerChar = systime_get_ms();
				do
				{
					ulValue  = ptUartArea->ulUartfr;
					ulValue &= HOSTMSK(uartfr_RXFE);
					if( ulTimeoutTotalMs!=0 )
					{
						iElapsedTimerTotal = systime_elapsed(ulTimerTotal, ulTimeoutTotalMs);
					}
					if( ulTimeoutCharMs!=0 )
					{
						iElapsedTimerChar = systime_elapsed(ulTimerChar, ulTimeoutCharMs);
					}
				} while( ulValue!=0 && iElapsedTimerTotal==0 && iElapsedTimerChar==0 );

				if( iElapsedTimerTotal!=0 )
				{
					uprintf("The total timeout of %dms elapsed.\n", ulTimeoutTotalMs);
					iResult = -1;
					break;
				}
				else if( iElapsedTimerChar!=0 )
				{
					uprintf("The char timeout of %dms elapsed.\n", ulTimeoutCharMs);
					iResult = -1;
					break;
				}
				else
				{
					/* Get the received byte. */
					*(pucCnt++) = (unsigned char)(ptUartArea->ulUartdr & 0xff);
				}
			}
			if( iResult!=0 )
			{
				if( ptState->ulVerbose!=0U )
				{
					uprintf("The receive operation failed.\n");
				}
			}
			else
			{
				if( ptState->ulVerbose!=0U )
				{
					hexdump(ptState->pucRecCnt, ulDataSize);
				}
				ptState->pucCmdCnt += sizeof(UART_SEQ_COMMAND_READ_T);
				ptState->pucRecCnt += ulDataSize;
			}
		}
	}

	return iResult;
}



static int command_send(CMD_STATE_T *ptState, const UART_HANDLE_T *ptHandle)
{
	int iResult;
	const UART_SEQ_COMMAND_WRITE_T *ptCmd;
	unsigned long ulDataSize;
	unsigned long ulValue;
	HOSTADEF(UART) *ptUartArea;
	const unsigned char *pucCnt;
	const unsigned char *pucEnd;


	if( (ptState->pucCmdCnt + sizeof(UART_SEQ_COMMAND_WRITE_T))>ptState->pucCmdEnd )
	{
		if( ptState->ulVerbose!=0U )
		{
			uprintf("Not enough data for the write header left.\n");
		}
		iResult = -1;
	}
	else
	{
		ptCmd = (const UART_SEQ_COMMAND_WRITE_T*)(ptState->pucCmdCnt);
		ulDataSize = ptCmd->s.usDataSize;
		if( (ptState->pucCmdCnt + sizeof(UART_SEQ_COMMAND_WRITE_T) + ulDataSize)>ptState->pucCmdEnd )
		{
			if( ptState->ulVerbose!=0U )
			{
				uprintf("Not enough data for the complete write command left.\n");
			}
			iResult = -1;
		}
		else
		{
			/* Send the data. */
			pucCnt = ptState->pucCmdCnt + sizeof(UART_SEQ_COMMAND_WRITE_T);
			pucEnd = ptState->pucCmdCnt + sizeof(UART_SEQ_COMMAND_WRITE_T) + ulDataSize;

			if( ptState->ulVerbose!=0U )
			{
				uprintf("SEND %d bytes\n", ulDataSize);
				hexdump(pucCnt, ulDataSize);
			}

			ptUartArea = ptHandle->ptUart;
			while(pucCnt<pucEnd)
			{
				/* Wait until there is space in the FIFO. */
				do
				{
					ulValue  = ptUartArea->ulUartfr;
					ulValue &= HOSTMSK(uartfr_TXFF);
				} while( ulValue!=0 );

				ptUartArea->ulUartdr = *(pucCnt++);
			}

		        /* Wait until all data in the TX FIFO is send. */
			do
			{
				ulValue  = ptUartArea->ulUartfr;
				ulValue &= HOSTMSK(uartfr_BUSY);
			} while( ulValue!=0 );

			ptState->pucCmdCnt += sizeof(UART_SEQ_COMMAND_WRITE_T) + ulDataSize;

			iResult = 0;
		}
	}

	return iResult;
}



static int command_baudrate(CMD_STATE_T *ptState, const UART_HANDLE_T *ptHandle)
{
	int iResult;
	const UART_SEQ_COMMAND_BAUDRATE_T *ptCmd;
	unsigned long ulBaudRate;
	unsigned long ulCurrentDeviceSpecificSpeedValue;
	HOSTADEF(UART) *ptUartArea;


	if( (ptState->pucCmdCnt + sizeof(UART_SEQ_COMMAND_BAUDRATE_T))>ptState->pucCmdEnd )
	{
		if( ptState->ulVerbose!=0U )
		{
			uprintf("Not enough data for the baud rate command left.\n");
		}
		iResult = -1;
	}
	else
	{
		ptCmd = (const UART_SEQ_COMMAND_BAUDRATE_T*)(ptState->pucCmdCnt);
		ulBaudRate = ptCmd->s.ulBaudRate;

		if( ptState->ulVerbose!=0U )
		{
			uprintf("BaudRate %d\n", ulBaudRate);
		}

		iResult = getDeviceSpecificBaudRate(ulBaudRate, &ulCurrentDeviceSpecificSpeedValue);
		if( iResult!=0 )
		{
			uprintf("Failed to set the baud rate to %d.\n", ulBaudRate);
		}
		else
		{
			ptUartArea = ptHandle->ptUart;

			ptUartArea->ulUartlcr_l = ulCurrentDeviceSpecificSpeedValue & 0xffU;
			ptUartArea->ulUartlcr_m = ulCurrentDeviceSpecificSpeedValue >> 8;

			ptState->pucCmdCnt += sizeof(UART_SEQ_COMMAND_BAUDRATE_T);
		}
	}

	return iResult;
}



static int command_delay(CMD_STATE_T *ptState)
{
	int iResult;
	const UART_SEQ_COMMAND_DELAY_T *ptCmd;


	if( (ptState->pucCmdCnt + sizeof(UART_SEQ_COMMAND_DELAY_T))>ptState->pucCmdEnd )
	{
		if( ptState->ulVerbose!=0U )
		{
			uprintf("Not enough data for the delay command left.\n");
		}
		iResult = -1;
	}
	else
	{
		ptCmd = (const UART_SEQ_COMMAND_DELAY_T*)(ptState->pucCmdCnt);

		if( ptState->ulVerbose!=0U )
		{
			uprintf("Delay %d ms\n", ptCmd->s.ulDelayInMs);
		}

		systime_delay_ms(ptCmd->s.ulDelayInMs);
		iResult = 0;
		ptState->pucCmdCnt += sizeof(UART_SEQ_COMMAND_DELAY_T);
	}

	return iResult;
}



typedef struct UART_INSTANCE_STRUCT
{
	HOSTADEF(UART) * const ptArea;
#if ASIC_TYP==ASIC_TYP_NETX10 || ASIC_TYP==ASIC_TYP_NETX50 || ASIC_TYP==ASIC_TYP_NETX56 || ASIC_TYP==ASIC_TYP_NETX6 || ASIC_TYP==ASIC_TYP_NETX4000 || ASIC_TYP==ASIC_TYP_NETX4000_RELAXED
	HOSTMMIODEF atMMIO[4];
#endif
} UART_INSTANCE_T;



static const UART_INSTANCE_T atUartInstances[] =
{
#if ASIC_TYP==ASIC_TYP_NETX10
	{
		.ptArea = (NX10_UART_AREA_T * const)Addr_NX10_uart0,
		.atMMIO =
		{
			NX10_MMIO_CFG_uart0_rxd,
			NX10_MMIO_CFG_uart0_txd,
			NX10_MMIO_CFG_uart0_rtsn,
			NX10_MMIO_CFG_uart0_ctsn
		}
	},

	{
		.ptArea = (NX10_UART_AREA_T * const)Addr_NX10_uart1,
		.atMMIO =
		{
			NX10_MMIO_CFG_uart1_rxd,
			NX10_MMIO_CFG_uart1_txd,
			NX10_MMIO_CFG_uart1_rtsn,
			NX10_MMIO_CFG_uart1_ctsn
		}
	}

#elif ASIC_TYP==ASIC_TYP_NETX50
	{
		.ptArea = (NX50_UART_AREA_T * const)Addr_NX50_uart0,
		.atMMIO =
		{
			NX50_MMIO_CFG_uart0_rxd,
			NX50_MMIO_CFG_uart0_txd,
			NX50_MMIO_CFG_uart0_rts,
			NX50_MMIO_CFG_uart0_cts
		}
	},

	{
		.ptArea = (NX50_UART_AREA_T * const)Addr_NX50_uart1,
		.atMMIO =
		{
			NX50_MMIO_CFG_uart1_rxd,
			NX50_MMIO_CFG_uart1_txd,
			NX50_MMIO_CFG_uart1_rts,
			NX50_MMIO_CFG_uart1_cts
		}
	},

	{
		.ptArea = (NX50_UART_AREA_T * const)Addr_NX50_uart2,
		.atMMIO =
		{
			NX50_MMIO_CFG_uart2_rxd,
			NX50_MMIO_CFG_uart2_txd,
			NX50_MMIO_CFG_uart2_rts,
			NX50_MMIO_CFG_uart2_cts
		}
	}

#elif ASIC_TYP==ASIC_TYP_NETX56
	{
		.ptArea = (NX56_UART_AREA_T * const)Addr_NX56_uart0,
		.atMMIO =
		{
			NX56_MMIO_CFG_uart0_rxd,
			NX56_MMIO_CFG_uart0_txd,
			NX56_MMIO_CFG_uart0_rtsn,
			NX56_MMIO_CFG_uart0_ctsn
		}
	},

	{
		.ptArea = (NX56_UART_AREA_T * const)Addr_NX56_uart1,
		.atMMIO =
		{
			NX56_MMIO_CFG_uart1_rxd,
			NX56_MMIO_CFG_uart1_txd,
			NX56_MMIO_CFG_uart1_rtsn,
			NX56_MMIO_CFG_uart1_ctsn
		}
	},

	{
		.ptArea = (NX56_UART_AREA_T * const)Addr_NX56_uart2,
		.atMMIO =
		{
			NX56_MMIO_CFG_uart2_rxd,
			NX56_MMIO_CFG_uart2_txd,
			NX56_MMIO_CFG_uart2_rtsn,
			NX56_MMIO_CFG_uart2_ctsn
		}
	}

#elif ASIC_TYP==ASIC_TYP_NETX6
	{
		.ptArea = (NX6_UART_AREA_T * const)Addr_NX6_uart0,
		.atMMIO =
		{
			NX6_MMIO_CFG_uart0_rxd,
			NX6_MMIO_CFG_uart0_txd,
			NX6_MMIO_CFG_uart0_rtsn,
			NX6_MMIO_CFG_uart0_ctsn
		}
	},

	{
		.ptArea = (NX6_UART_AREA_T * const)Addr_NX6_uart1,
		.atMMIO =
		{
			NX6_MMIO_CFG_uart1_rxd,
			NX6_MMIO_CFG_uart1_txd,
			NX6_MMIO_CFG_uart1_rtsn,
			NX6_MMIO_CFG_uart1_ctsn
		}
	},

	{
		.ptArea = (NX6_UART_AREA_T * const)Addr_NX6_uart2,
		.atMMIO =
		{
			NX6_MMIO_CFG_uart2_rxd,
			NX6_MMIO_CFG_uart2_txd,
			NX6_MMIO_CFG_uart2_rtsn,
			NX6_MMIO_CFG_uart2_ctsn
		}
	}

#elif ASIC_TYP==ASIC_TYP_NETX500
	{
		.ptArea = (NX500_UART_AREA_T * const)Addr_NX500_uart0
	},

	{
		.ptArea = (NX500_UART_AREA_T * const)Addr_NX500_uart1
	},

	{
		.ptArea = (NX500_UART_AREA_T * const)Addr_NX500_uart2
	}

#elif ASIC_TYP==ASIC_TYP_NETX4000_RELAXED || ASIC_TYP==ASIC_TYP_NETX4000
	{
		.ptArea = (HOSTADEF(UART) * const)HOSTADDR(uart0),
		.atMMIO =
		{
			HOSTMMIO(UART0_RXD),
			HOSTMMIO(UART0_TXD),
			HOSTMMIO(UART0_RTSN),
			HOSTMMIO(UART0_CTSN)
		}
	},

	{
		.ptArea = (HOSTADEF(UART) * const)HOSTADDR(uart1),
		.atMMIO =
		{
			HOSTMMIO(UART1_RXD),
			HOSTMMIO(UART1_TXD),
			HOSTMMIO(UART1_RTSN),
			HOSTMMIO(UART1_CTSN)
		}
	},

	{
		.ptArea = (HOSTADEF(UART) * const)HOSTADDR(uart2),
		.atMMIO =
		{
			HOSTMMIO(UART2_RXD),
			HOSTMMIO(UART2_TXD),
			HOSTMMIO(UART2_RTSN),
			HOSTMMIO(UART2_CTSN)
		}
	}

#elif ASIC_TYP==ASIC_TYP_NETX90_MPW
	{
		.ptArea = (NX90MPW_UART_AREA_T * const)Addr_NX90MPW_uart_com
	}

#elif ASIC_TYP==ASIC_TYP_NETX90
	{
		.ptArea = (NX90_UART_AREA_T * const)Addr_NX90_uart
	}

#elif ASIC_TYP==ASIC_TYP_NETX90_MPW_APP || ASIC_TYP==ASIC_TYP_NETX90_APP
	{
		.ptArea = (NX90_UART_AREA_T * const)Addr_NX90_uart_app
	},

	{
		.ptArea = (NX90_UART_AREA_T * const)Addr_NX90_uart_xpic_app
	}

#else
#       error "Unsupported ASIC_TYPE!"

#endif
};



#if ASIC_TYP==ASIC_TYP_NETX10 || ASIC_TYP==ASIC_TYP_NETX50 || ASIC_TYP==ASIC_TYP_NETX56 || ASIC_TYP==ASIC_TYP_NETX6 || ASIC_TYP==ASIC_TYP_NETX4000 || ASIC_TYP==ASIC_TYP_NETX4000_RELAXED
static const char *apcPinOrder[4] =
{
	"RX",
	"TX",
	"RTS",
	"CTS"
};
#endif



static TEST_RESULT_T processCommandOpen(unsigned long ulVerbose, UART_PARAMETER_OPEN_T *ptParameter)
{
	TEST_RESULT_T tResult;
	int iResult;
	unsigned long ulCore;
	UART_HANDLE_T *ptHandle;
	const UART_INSTANCE_T *ptUartInstance;
	HOSTADEF(UART) *ptUartArea;
	unsigned long ulValue;
	unsigned long ulBaudRate;
	unsigned long ulCurrentDeviceSpecificSpeedValue;
#if ASIC_TYP==ASIC_TYP_NETX4000 || ASIC_TYP==ASIC_TYP_NETX4000_RELAXED
	unsigned long ulPortControl;
#endif
#if ASIC_TYP==ASIC_TYP_NETX10 || ASIC_TYP==ASIC_TYP_NETX50 || ASIC_TYP==ASIC_TYP_NETX56 || ASIC_TYP==ASIC_TYP_NETX6 || ASIC_TYP==ASIC_TYP_NETX4000 || ASIC_TYP==ASIC_TYP_NETX4000_RELAXED
	unsigned int uiCnt;
	const char *pcName;
	HOSTDEF(ptAsicCtrlArea);
	HOSTDEF(ptMmioCtrlArea);
#elif ASIC_TYP==ASIC_TYP_NETX500
	HOSTDEF(ptGpioArea);
	unsigned int uiIdx;
#endif

	/* Check the parameter. */
	tResult = TEST_RESULT_ERROR;
	ulCore = ptParameter->ulUartCore;

	/* Is the core number valid? */
	if( ulCore>=(sizeof(atUartInstances)/sizeof(atUartInstances[0])) )
	{
		uprintf("The UART core number %d is invalid on the host %s.\n", ulCore, HOSTNAME);
	}
	else
	{
		ptUartInstance = atUartInstances + ulCore;

		/* Get the UART area. */
		ptUartArea = ptUartInstance->ptArea;

		/* Disable the UART. */
		ptUartArea->ulUartcr = 0;

		/* Use baud rate mode 2. */
		ptUartArea->ulUartcr_2 = HOSTMSK(uartcr_2_Baud_Rate_Mode);

		/* Set the baud rate. */
		ulBaudRate = ptParameter->ulBaudRate;
		iResult = getDeviceSpecificBaudRate(ulBaudRate, &ulCurrentDeviceSpecificSpeedValue);
		if( iResult!=0 )
		{
			uprintf("Failed to set the baud rate to %d.\n", ulBaudRate);
		}
		else
		{
			ptUartArea->ulUartlcr_l = ulCurrentDeviceSpecificSpeedValue & 0xffU;
			ptUartArea->ulUartlcr_m = ulCurrentDeviceSpecificSpeedValue >> 8;

			/* Set the UART to 8N1, FIFO enabled. */
			ulValue  = HOSTMSK(uartlcr_h_WLEN);
			ulValue |= HOSTMSK(uartlcr_h_FEN);
			ptUartArea->ulUartlcr_h = ulValue;

			/* Disable all drivers. */
			ptUartArea->ulUartdrvout = 0;

			/* Disable RTS/CTS mode. */
			ptUartArea->ulUartrts = 0;

			/* Enable the UART. */
			ptUartArea->ulUartcr = HOSTMSK(uartcr_uartEN);

			if( ulVerbose!=0 )
			{
				/* Setup the UART. */
				uprintf("Setup UART %d with %d baud (native 0x%04x).\n", ulCore, ulBaudRate, ulCurrentDeviceSpecificSpeedValue);
			}
#if ASIC_TYP==ASIC_TYP_NETX10 || ASIC_TYP==ASIC_TYP_NETX50 || ASIC_TYP==ASIC_TYP_NETX56 || ASIC_TYP==ASIC_TYP_NETX6 || ASIC_TYP==ASIC_TYP_NETX4000 || ASIC_TYP==ASIC_TYP_NETX4000_RELAXED
			for(uiCnt=0; uiCnt<4; uiCnt++)
			{
				ulValue = ptParameter->aucMMIO[uiCnt];
#       if ASIC_TYP==ASIC_TYP_NETX4000 || ASIC_TYP==ASIC_TYP_NETX4000_RELAXED
				ulPortControl = ptParameter->ausPortcontrol[uiCnt];
#       endif

				if( ulVerbose!=0 )
				{
					pcName = apcPinOrder[uiCnt];

					if( ulValue!=0xff )
					{
#       if ASIC_TYP==ASIC_TYP_NETX4000 || ASIC_TYP==ASIC_TYP_NETX4000_RELAXED
						if( ulPortControl!=PORTCONTROL_SKIP )
						{
							uprintf("  %s = MMIO%d with port control 0x%04x\n", pcName, ulValue, ulPortControl);
						}
						else
						{
							uprintf("  %s = MMIO%d without port control\n", pcName, ulValue);
						}
#       else
						uprintf("  %s = MMIO%d\n", pcName, ulValue);
#       endif
					}
					else
					{
						uprintf("  %s without MMIO\n", pcName);
					}
				}

				if( ulValue!=0xff )
				{
					ptAsicCtrlArea->ulAsic_ctrl_access_key = ptAsicCtrlArea->ulAsic_ctrl_access_key;    /* @suppress("Assignment to itself") */
					ptMmioCtrlArea->aulMmio_cfg[ulValue] = ptUartInstance->atMMIO[uiCnt];
				}
			}
#       if ASIC_TYP==ASIC_TYP_NETX4000 || ASIC_TYP==ASIC_TYP_NETX4000_RELAXED
			portcontrol_apply_mmio(ptParameter->aucMMIO, ptParameter->ausPortcontrol, 4);
#       endif
#elif ASIC_TYP==ASIC_TYP_NETX500
			uiIdx = uiUartUnit << 2;
			ptGpioArea->aulGpio_cfg[uiIdx+0] = 2;
			ptGpioArea->aulGpio_cfg[uiIdx+1] = 2;
			ptGpioArea->aulGpio_cfg[uiIdx+2] = 2;
			ptGpioArea->aulGpio_cfg[uiIdx+3] = 2;
#endif

			/* Enable the drivers. */
			ulValue = HOSTMSK(uartdrvout_DRVTX);
			ptUartArea->ulUartdrvout = ulValue;

			/* Fill the handle. */
			ptHandle = (UART_HANDLE_T*)ptParameter->ptHandle;
			ptHandle->ptUart = ptUartArea;
			ptHandle->ulUartIndex = ulCore;
			ptHandle->ulCurrentBaudRate = ulBaudRate;
			ptHandle->ulCurrentDeviceSpecificSpeedValue = ulCurrentDeviceSpecificSpeedValue;

			tResult = TEST_RESULT_OK;
		}
	}

	return tResult;
}



static int processCommandSequence(unsigned long ulVerbose, UART_PARAMETER_RUN_SEQUENCE_T *ptParameter)
{
	int iResult;
	CMD_STATE_T tState;
	unsigned char ucData;
	UART_SEQ_COMMAND_T tCmd;
	unsigned int uiDataSize;
	UART_HANDLE_T *ptHandle;


	/* An empty command is OK. */
	iResult = 0;

	/* Get the verbose flag. */
	tState.ulVerbose = ulVerbose;

	/* Get the handle. */
	ptHandle = (UART_HANDLE_T*)(ptParameter->ptHandle);

	/* Loop over all commands. */
	tState.pucCmdCnt = ptParameter->pucCommand;
	tState.pucCmdEnd = tState.pucCmdCnt + ptParameter->sizCommand;
	tState.pucRecCnt = ptParameter->pucReceivedData;
	tState.pucRecEnd = tState.pucRecCnt + ptParameter->sizReceivedDataMax;
	if( tState.ulVerbose!=0U )
	{
		uprintf("Running command [0x%08x, 0x%08x[ with a receive buffer of %d bytes [0x%08x, 0x%08x[.\n",
		        (unsigned long)tState.pucCmdCnt,
		        (unsigned long)tState.pucCmdEnd,
		        ptParameter->sizReceivedDataMax,
		        (unsigned long)tState.pucRecCnt,
		        (unsigned long)tState.pucRecEnd
		);
	}

	while( tState.pucCmdCnt<tState.pucCmdEnd )
	{
		/* Get the next command. */
		iResult = -1;
		ucData = *(tState.pucCmdCnt++);
		tCmd = (UART_SEQ_COMMAND_T)ucData;
		switch( tCmd )
		{
		case UART_SEQ_COMMAND_Clean:
		case UART_SEQ_COMMAND_Send:
		case UART_SEQ_COMMAND_Receive:
		case UART_SEQ_COMMAND_BaudRate:
		case UART_SEQ_COMMAND_Delay:
			iResult = 0;
			break;
		}
		if( iResult!=0 )
		{
			uprintf("Invalid command: 0x%02x\n", ucData);
			break;
		}
		else
		{
			switch( tCmd )
			{
			case UART_SEQ_COMMAND_Clean:
				iResult = command_clean(&tState, ptHandle);
				break;

			case UART_SEQ_COMMAND_Send:
				iResult = command_send(&tState, ptHandle);
				break;

			case UART_SEQ_COMMAND_Receive:
				iResult = command_receive(&tState, ptHandle);
				break;

			case UART_SEQ_COMMAND_BaudRate:
				iResult = command_baudrate(&tState, ptHandle);
				break;

			case UART_SEQ_COMMAND_Delay:
				iResult = command_delay(&tState);
				break;
			}
			if( iResult!=0 )
			{
				if( tState.ulVerbose!=0U )
				{
					uprintf("The command failed. Stopping execution of the sequence.\n");
				}
				break;
			}
		}
	}

	if( iResult==0 )
	{
		/* Set the size of the result data. */
		uiDataSize = (unsigned int)(tState.pucRecCnt-ptParameter->pucReceivedData);
		if( uiDataSize<=ptParameter->sizReceivedDataMax )
		{
			ptParameter->sizReceivedData = uiDataSize;
		}
		else
		{
			iResult = -1;
		}
	}

	return iResult;
}



static TEST_RESULT_T processCommandClose(unsigned long ulVerbose, UART_PARAMETER_CLOSE_T *ptParameter)
{
	unsigned long ulValue;
	UART_HANDLE_T *ptHandle;
	HOSTADEF(UART) *ptUartArea;


	/* Get the handle. */
	ptHandle = (UART_HANDLE_T*)(ptParameter->ptHandle);
	ptUartArea = ptHandle->ptUart;

	if( ulVerbose!=0 )
	{
		uprintf("Closing UART%d.\n", ptHandle->ulUartIndex);
	}

        /* Wait until all data in the TX FIFO is sent. */
	if( ulVerbose!=0 )
	{
		uprintf("Waiting until all data in the TX FIFO is sent...\n");
	}
	do
	{
		ulValue  = ptUartArea->ulUartfr;
		ulValue &= HOSTMSK(uartfr_BUSY);
	} while( ulValue!=0 );
	if( ulVerbose!=0 )
	{
		uprintf("Done.\n");
	}

#if ASIC_TYP==ASIC_TYP_NETX500
	uiIdx = uiUartUnit << 2;
	ptGpioArea->aulGpio_cfg[uiIdx+0] = 2;
	ptGpioArea->aulGpio_cfg[uiIdx+1] = 2;
	ptGpioArea->aulGpio_cfg[uiIdx+2] = 2;
	ptGpioArea->aulGpio_cfg[uiIdx+3] = 2;
#endif

	ptUartArea->ulUartcr = 0;
	ptUartArea->ulUartlcr_m = 0;
	ptUartArea->ulUartlcr_l = 0;
	ptUartArea->ulUartlcr_h = 0;
	ptUartArea->ulUartrts = 0;
	ptUartArea->ulUartdrvout = 0;

	return TEST_RESULT_OK;
}



TEST_RESULT_T test(UART_PARAMETER_T *ptTestParams)
{
	TEST_RESULT_T tResult;
	int iResult;
	unsigned long ulVerbose;
	UART_CMD_T tCmd;

	systime_init();

	/* Set the verbose mode. */
	ulVerbose = ptTestParams->ulVerbose;
	if( ulVerbose!=0 )
	{
		uprintf("\f. *** UART test by doc_bacardi@users.sourceforge.net ***\n");
		uprintf("V" VERSION_ALL "\n\n");

		/* Get the test parameter. */
		uprintf(". Parameters: 0x%08x\n", (unsigned long)ptTestParams);
		uprintf(".    Verbose: 0x%08x\n", ptTestParams->ulVerbose);
	}

	tCmd = (UART_CMD_T)(ptTestParams->ulCommand);
	tResult = TEST_RESULT_ERROR;
	switch(tCmd)
	{
	case UART_CMD_Open:
	case UART_CMD_RunSequence:
	case UART_CMD_Close:
		tResult = TEST_RESULT_OK;
		break;
	}
	if( tResult!=TEST_RESULT_OK )
	{
		uprintf("Invalid command: 0x%08x\n", tCmd);
	}
	else
	{
		switch(tCmd)
		{
		case UART_CMD_Open:
			tResult = processCommandOpen(ulVerbose, &(ptTestParams->uParameter.tOpen));
			break;

		case UART_CMD_RunSequence:
			iResult = processCommandSequence(ulVerbose, &(ptTestParams->uParameter.tRunSequence));
			if( iResult!=0 )
			{
				tResult = TEST_RESULT_ERROR;
			}
			break;

		case UART_CMD_Close:
			tResult = processCommandClose(ulVerbose, &(ptTestParams->uParameter.tClose));
			break;
		}
	}

	if( tResult==TEST_RESULT_OK )
	{
		rdy_run_setLEDs(RDYRUN_GREEN);
	}
	else
	{
		rdy_run_setLEDs(RDYRUN_YELLOW);
	}

	return tResult;
}

/*-----------------------------------*/
