/*
    IRUSB
    Copyright (C) 2020 Malte Marwedel

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


Version history:

2020-12-07: Version 1.0
*/


#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "irusb.h"

#include "usbd_core.h"
#include "usb.h"
#include "main.h"
#include "irsnd.h"
#include "irmp.h"


/* Control codes:
bmRequestType = USB_REQ_VENDOR & USB_REQ_DEVICE
bRequest = 0 -> control red LED
 wValue = 0: LED off
 wValue = 1: LED blinking
 wValue = 2: LED on
bRequest = 1 -> control IR LED
 Send ir cmd. 8 Data bytes = IRMP_DATA encoding in PACKED form
bRequest = 2 -> Add delay to command queue
 Delay command - first two databytes contain the delay in ms
bRequest = 3 -> Get last data from IRMP. 8 Data bytes = IRMP_DATA encoding in
 PACKED form
bRequest = 4 -> Do a system reset
*/

typedef struct {
	uint8_t type; //0 = red LED control, 1 = IR command, 2 = delay
	union {
		IRMP_DATA irmpData;
		uint16_t delay;
		uint8_t ledState;
	} data;
} commandEntry_t;

//-------------------- Global variables ----------------------------------------

usbd_device g_usbDev;

//must be 4byte aligned
uint32_t g_usbBuffer[20];

/* The PID is reserved for this project. Please use another PID,
should the source be used for a project with another purpose/incompatible
USB control commands */
uint8_t g_deviceDescriptor[] = {
	0x12,       //length of this struct
	0x01,       //always 1
	0x10,0x01,  //usb version
	0xFF,       //device class
	0xFF,       //subclass
	0xFF,       //device protocol
	0x20,       //maximum packet size
	0x09,0x12,  //vid
	0x00,0x77,  //pid
	0x00,0x01,  //revision
	0x1,        //manufacturer index
	0x2,        //product name index
	0x0,        //serial number index
	0x01        //number of configurations
};

uint8_t g_DeviceConfiguration[] = {
	9, //length of this entry
	0x2, //device configuration
	18, 0, //total length of this struct
	0x1, //number of interfaces
	0x1, //this config
	0x0, //descriptor of this config index, not used
	0x80, //bus powered
	60,    //120mA
	//interface descriptor follows
	9, //length of this descriptor
	0x4, //interface descriptor
	0x0, //interface number
	0x0, //alternate settings
	0x0, //number of endpoints without ep 0
	0xFF, //class code -> vendor specific
	0x0, //subclass code
	0x0, //protocol code
	0x0 //string index for interface descriptor
};

static struct usb_string_descriptor g_lang_desc     = USB_ARRAY_DESC(USB_LANGID_ENG_US);
static struct usb_string_descriptor g_manuf_desc_en = USB_STRING_DESC("marwedels.de");
static struct usb_string_descriptor g_prod_desc_en  = USB_STRING_DESC("IRUSB");

#define COMMANDQUEUELEN 16

#define CMD_LED 0
#define CMD_IR 1
#define CMD_DELAY 2

commandEntry_t g_commandQueue[COMMANDQUEUELEN];
/*logic: if g_commandQueueReadIdx != g_commandQueueWriteIdx -> something to read from
  if (g_commandQueueReadIdx != (g_commandQueueWriteIdx + 1)) -> we have space to write to
  since the read and write index pointer read-access is atomic, no interrupt lock is needed.
  For write no lock is needed, as only the one interrupt modifies the WP, and only the
  main loop modifies the RP.
*/
volatile uint8_t g_commandQueueReadIdx;
volatile uint8_t g_commandQueueWriteIdx;

volatile uint8_t g_ResetRequest;

volatile bool g_irRxValid;
IRMP_DATA g_irRxLast;

#define UARTBUFFERLEN 256

char g_uartBuffer[UARTBUFFERLEN];
volatile uint8_t g_uartBufferReadIdx;
volatile uint8_t g_uartBufferWriteIdx;



//--------- Code for debug prints ----------------------------------------------

char printReadChar(void) {
	char out = 0;
	if (g_uartBufferReadIdx != g_uartBufferWriteIdx) {
		uint8_t ri = g_uartBufferReadIdx;
		out = g_uartBuffer[ri];
		__sync_synchronize(); //the pointer increment may only be visible after the copy
		ri = (ri + 1) % UARTBUFFERLEN;
		g_uartBufferReadIdx = ri;
	}
	return out;
}

void USART1_IRQHandler(void) {
	if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE) == SET) {
		char c = printReadChar();
		if (c) {
			huart1.Instance->TDR = c;
		} else {
			__HAL_UART_DISABLE_IT(&huart1, UART_IT_TXE);
		}
	}
	//just clear all flags
	__HAL_UART_CLEAR_FLAG(&huart1, UART_CLEAR_PEF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_OREF | UART_CLEAR_IDLEF | UART_CLEAR_TCF | UART_CLEAR_LBDF | UART_CLEAR_CTSF | UART_CLEAR_CMF | UART_CLEAR_WUF | UART_CLEAR_RTOF);
}

void printWriteChar(char out) {
	uint8_t writeThis = g_uartBufferWriteIdx;
	uint8_t writeNext = (writeThis + 1) % UARTBUFFERLEN;
	if (writeNext != g_uartBufferReadIdx) {
		g_uartBuffer[writeThis] = out;
		g_uartBufferWriteIdx = writeNext;
	}
	__disable_irq();
	if (__HAL_UART_GET_IT_SOURCE(&huart1, UART_IT_TXE) == RESET) {
		__HAL_UART_ENABLE_IT(&huart1, UART_IT_TXE);
	}
	__enable_irq();
}

void writeString(const char * str) {
	while (*str) {
		printWriteChar(*str);
		str++;
	}
	//HAL_UART_Transmit(&huart1, (unsigned char *)str, strlen(str), 1000);
}

void dbgPrintf(const char * format, ...)
{
	static char buffer[128];

	va_list args;
	va_start(args, format);
	//removing the vsnprintf call would save more than 3KiB program code.
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	writeString(buffer);
}

//simpler than HAL_UART_Receive and without an annoying timeout
bool dbgUartGet(UART_HandleTypeDef *huart, uint8_t *pData) {

	if (__HAL_UART_GET_FLAG(huart, UART_FLAG_RXNE)) {
		*pData = (uint8_t)(huart->Instance->RDR & 0xFF);
		return true;
	}
	return false;
}

void _Error_Handler(const char *file, uint32_t line) {
	dbgPrintf("Error in %s:%u\r\n", file, (unsigned int)line);
}


//----- IR and usb glue logic --------------------------------------------------

bool cmdPut(const commandEntry_t * pCommand) {
	uint8_t writeThis = g_commandQueueWriteIdx;
	uint8_t writeNext = (writeThis + 1) % COMMANDQUEUELEN;
	if (writeNext != g_commandQueueReadIdx) {
		memcpy(&(g_commandQueue[writeThis]), pCommand, sizeof(commandEntry_t));
		/*No need for a barrier, since the main loop cant access the queue before
		  the interrupt is over anyway */
		g_commandQueueWriteIdx = writeNext;
		return true;
	}
	return false;
}

bool cmdPutLed(uint8_t state) {
	commandEntry_t command;
	command.type = CMD_LED;
	command.data.ledState = state;
	return cmdPut(&command);
}

bool cmdPutIr(const uint8_t * buffer) {
	commandEntry_t command;
	command.type = CMD_IR;
	command.data.irmpData.protocol = buffer[0];
	command.data.irmpData.address = buffer[1] | (buffer[2] << 8);
	command.data.irmpData.command = buffer[3] | (buffer[4] << 8) | (buffer[5] << 16) | (buffer[6] << 24);
	command.data.irmpData.flags = buffer[7];
	return cmdPut(&command);
}

bool cmdPutDelay(const uint8_t * buffer) {
	commandEntry_t command;
	command.type = CMD_DELAY;
	command.data.delay = buffer[0] | (buffer[1] << 8);
	return cmdPut(&command);
}

bool cmdGet(commandEntry_t * pCommand) {
	if (g_commandQueueReadIdx != g_commandQueueWriteIdx) {
		uint8_t ri = g_commandQueueReadIdx;
		memcpy(pCommand, &(g_commandQueue[ri]), sizeof(commandEntry_t));
		__sync_synchronize(); //the pointer increment may only be visible after the copy
		ri = (ri + 1) % COMMANDQUEUELEN;
		g_commandQueueReadIdx = ri;
		return true;
	}
	return false;
}

void copyIrRxData(uint8_t * buffer) {
	if (g_irRxValid) {
		buffer[0] = g_irRxLast.protocol;
		uint16_t address = g_irRxLast.address;
		uint32_t command = g_irRxLast.command;
		buffer[1] = address & 0xFF;
		buffer[2] = address >> 8;
		buffer[3] = command & 0xFF;
		buffer[4] = (command >> 8) & 0xFF;
		buffer[5] = (command >> 16) & 0xFF;
		buffer[6] = (command >> 24) & 0xFF;
		buffer[7] = g_irRxLast.flags;
		//in some rare cases, this throws away data just in the stage of being copied
		g_irRxValid = false;
	} else
	{
		memset(buffer, 0, 8);
	}
}

//--------- IR handling --------------------------------------------------------

void  timer3IntCallback(void) {
	if (!irsnd_ISR())
	{
		irmp_ISR();
	}
}

//---------- USB handling ------------------------------------------------------

void USB_IRQHandler(void)
{
	usbd_poll(&g_usbDev);
}

static usbd_respond usbGetDesc(usbd_ctlreq *req, void **address, uint16_t *length) {
	const uint8_t dtype = req->wValue >> 8;
	const uint8_t dnumber = req->wValue & 0xFF;
	void* desc = NULL;
	uint16_t len = 0;
	usbd_respond result = usbd_fail;
	switch (dtype) {
		case USB_DTYPE_DEVICE:
			desc = g_deviceDescriptor;
			len = sizeof(g_deviceDescriptor);
			result = usbd_ack;
			break;
		case USB_DTYPE_CONFIGURATION:
			desc = g_DeviceConfiguration;
			len = sizeof(g_DeviceConfiguration);
			result = usbd_ack;
			break;
		case USB_DTYPE_STRING:
			if (dnumber < 3) {
				struct usb_string_descriptor * pStringDescr = NULL;
				if (dnumber == 0) {
					pStringDescr = &g_lang_desc;
				}
				if (dnumber == 1) {
					pStringDescr = &g_manuf_desc_en;
				}
				if (dnumber == 2) {
					pStringDescr = &g_prod_desc_en;
				}
				desc = pStringDescr;
				len = pStringDescr->bLength;
				result = usbd_ack;
			}
			break;
	}
	*address = desc;
	*length = len;
	return result;
}

static usbd_respond usbControl(usbd_device *dev, usbd_ctlreq *req, usbd_rqc_callback *callback) {
	//Printing can be done here as long it is buffered. Otherwise it might be too slow
	//dbgPrintf("type %x request %x wvalue %x windex %x wLength %u\r\n", req->bmRequestType, req->bRequest, req->wValue, req->wIndex, req->wLength);
	if ((req->bmRequestType & (USB_REQ_TYPE | USB_REQ_RECIPIENT)) == (USB_REQ_VENDOR| USB_REQ_DEVICE)) {
		switch(req->bRequest)
		{
			case CMD_LED: //0
				if (req->wValue < 3) {
					if (cmdPutLed(req->wValue)) {
						return usbd_ack;
					}
				}
				break;
			case CMD_IR: //1
				if (req->wLength >= 8) {
					if (cmdPutIr(req->data)) {
						return usbd_ack;
					}
				}
				break;
			case CMD_DELAY: //2
				if (req->wLength >= 2) {
					if (cmdPutDelay(req->data)) {
						return usbd_ack;
					}
				}
				break;
			case 3:
				if (req->wLength >= 8) {
					copyIrRxData(req->data);
					return usbd_ack;
				}
				break;
			case 4:
				g_ResetRequest = true;
				return usbd_ack;
		}
	}
	return usbd_fail;
}

static usbd_respond usbSetConf(usbd_device *dev, uint8_t cfg) {
	usbd_respond result = usbd_fail;
	switch (cfg) {
		case 0:
			//deconfig
			break;
		case 1:
			//set config
			result = usbd_ack;
			break;
	}
	return result;
}

void startUsb(void)
{
	__HAL_RCC_USB_CLK_ENABLE();
	usbd_init(&g_usbDev, &usbd_hw, 0x20, g_usbBuffer, sizeof(g_usbBuffer));
	usbd_reg_config(&g_usbDev, usbSetConf);
	usbd_reg_control(&g_usbDev, usbControl);
	usbd_reg_descr(&g_usbDev, usbGetDesc);

	usbd_enable(&g_usbDev, true);
	usbd_connect(&g_usbDev, true);

	NVIC_EnableIRQ(USB_IRQn);
}

//--------- main loop and init -------------------------------------------------


void initIrusb(void) {
	NVIC_EnableIRQ(USART1_IRQn);
	dbgPrintf("IRUSB 1.0 (c) 2020 by Malte Marwedel\r\nStarting...\r\n");
	startUsb();
	irsnd_init();
	irmp_init();
	HAL_TIM_Base_Start_IT(&htim3);
	dbgPrintf("started\r\n");
}

/*The timestamp may only increase by one for each call, otherwise a delay might
  never end. The timestamp should be in ms */
void IrUsb1msPassed(uint32_t timestamp) {
	static uint8_t ledMode = 1;
	static uint32_t delayEndpoint;
	static bool delayInProgress = false;
	static uint32_t cycleCnt = 0;

	commandEntry_t command;
	if (delayInProgress == false)
	{
		if (!irsnd_is_busy())
		{
			bool newCmd = cmdGet(&command);
			if (newCmd)
			{
				if (command.type == CMD_LED)
				{
					ledMode = command.data.ledState;
					dbgPrintf("New led mode %u\r\n", (unsigned int)ledMode);
				}
				if (command.type == CMD_IR)
				{
					IRMP_DATA * pId = &(command.data.irmpData);
					dbgPrintf("Send IR proto: %u, addr: 0x%x, comm: 0x%x, flags: 0x%x\r\n",
					   pId->protocol, pId->address, (unsigned int)pId->command, pId->flags);
					irsnd_send_data(pId, false);
				}
				if (command.type == CMD_DELAY)
				{
					dbgPrintf("Delay %ums\r\n", command.data.delay);
					delayEndpoint = command.data.delay + timestamp;
					delayInProgress = true;
				}
			}
		}
	} else if (timestamp == delayEndpoint) {
		delayInProgress = false;
	}
	IRMP_DATA irmp_data;
	if (irmp_get_data(&irmp_data))
	{
		dbgPrintf("Rec IR proto: %u, addr: 0x%x, comm: 0x%x, flags: 0x%x\r\n",
		  irmp_data.protocol, irmp_data.address, (unsigned int)irmp_data.command, irmp_data.flags);
		g_irRxValid = false;
		__sync_synchronize();
		memcpy(&g_irRxLast, &irmp_data, sizeof(IRMP_DATA));
		__sync_synchronize();
		g_irRxValid = true;
	}
	GPIO_PinState pinState = GPIO_PIN_RESET;
	if (ledMode == 1) {
		if ((timestamp % 1000) >= 500)
		{
			pinState = GPIO_PIN_SET;
		}
	} else if (ledMode == 2) {
		 pinState = GPIO_PIN_SET;
	}
	HAL_GPIO_WritePin(LedRed_GPIO_Port, LedRed_Pin, pinState);
	if ((timestamp % 1000) == 0)
	{
		cycleCnt++;
		dbgPrintf("Cylce %u\r\n", (unsigned int)cycleCnt);
	}
	uint8_t debugData = 0;
	if (dbgUartGet(&huart1, &debugData)) {
		dbgPrintf("Input %u\r\n", debugData);
	}
	if ((debugData == 'R') || (g_ResetRequest == true)) {
		dbgPrintf("Resetting...\r\n");
		HAL_Delay(50); //let the uart print its message and the USB send the act
#if 1
		NVIC_SystemReset();
#else
		//test if the independend watchdog does its work
		uint32_t x = 0;
		while(1)
		{
			x++;
			HAL_Delay(100);
			dbgPrintf("%u\r\n", x);
		}
#endif
	}
	/* We have to call the refresh within 1s, if the clock source has its
	   nominal frequency of 40KHz. However this may vary from 20 to 50kHz!*/
	HAL_IWDG_Refresh(&hiwdg);
}

void loopIrusb(void) {
	static uint32_t counter = 0;
	uint32_t timestamp = HAL_GetTick();
	if (counter != timestamp) {
		counter++;
		IrUsb1msPassed(counter);
	} else {
		/* Total USB power consumtion:
		   With TSOP31236 connected:
		     During waiting for DFU firmare upload: 18.8mA
		     Not having a wfi here, red LED off: 10mA
		     Not having a wfi here, red LED on:  11.6mA
		     With wfi, red LED off: 9.3mA
		     With wfi, red LED on: 10.7mA
	     Removing the TSOP31236 saves additional 0.3mA
		*/
		__WFI();
	}
}
