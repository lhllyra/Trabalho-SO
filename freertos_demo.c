/*********************************************************************************
 *																				 *
 * freertos_demo.c - Simple FreeRTOS example.									 *
 *																				 *
 * Copyright (c) 2012-2017 Texas Instruments Incorporated.  All rights reserved. *
 * Software License Agreement													 *
 * 																				 *
 * Texas Instruments (TI) is supplying this software for use solely and			 *
 * exclusively on TI's microcontroller products. The software is owned by		 *
 * TI and/or its suppliers, and is protected under applicable copyright			 *
 * laws. You may not combine this software with "viral" open-source				 *
 * software in order to form a larger program.									 *
 *																				 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND WITH ALL FAULTS.						 *
 * NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT			 *
 * NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR		 *
 * A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. TI SHALL NOT, UNDER ANY			 *
 * CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR CONSEQUENTIAL			 *
 * DAMAGES, FOR ANY REASON WHATSOEVER.											 *
 *																				 *
 * This is part of revision 2.1.4.178 of the EK-TM4C123GXL Firmware Package.	 *
 *																				 *
 *********************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/rom.h"
#include "driverlib/sysctl.h"
#include "driverlib/uart.h"
#include "driverlib/flash.h"
#include "driverlib/adc.h"
#include "utils/uartstdio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define RED 0x02
#define BLUE 0x04
#define GREEN 0x08

#define BUFFER_SIZE 10
#define TASK_STACK_DEPTH 128

#define FLSH_TASK_PRIORITY 1
#define SSRL_TASK_PRIORITY 2
#define TEMP_TASK_PRIORITY 3

#define QUEUE_LENGTH 4
#define TICKS_TO_WAIT 5

#define BASE_ADDR 0x10000

QueueHandle_t xFlashQueue;
QueueHandle_t xSerialQueue;
xSemaphoreHandle xUARTMutex;
xSemaphoreHandle xSensorBSemaphore;

/******************************************************************************
 * Configure the UART and its pins.  This must be called before UARTprintf(). *
 ******************************************************************************/
void ConfigureUART(void)
{
    // Enable the GPIO Peripheral used by the UART.
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);

    // Enable UART0
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);

    // Configure GPIO Pins for UART mode.
    ROM_GPIOPinConfigure(GPIO_PA0_U0RX);
    ROM_GPIOPinConfigure(GPIO_PA1_U0TX);
    ROM_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    // Use the internal 16MHz oscillator as the UART clock source.
    UARTClockSourceSet(UART0_BASE, UART_CLOCK_PIOSC);

    // Initialize the UART for console I/O.
    UARTStdioConfig(0, 115200, 16000000);
}

/******************************************************************************
 * Configure the onboard LED.  This must be called before changing LED state. *
 ******************************************************************************/
void ConfigureLED(void)
{
	// Configure on-board LED and turn it off
	// First, enable the peripheral
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);

	// Define output to the 3 possible colors as they are bound to 3 different pins
	GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
	// Write binary output to these 3 pins
	GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3, 0x00);

	// It just wait a little bit (looping)
	SysCtlDelay(20000000);
}

/****************************************************************************
 * Define Idle Hook Function. It will run when there is nothing left to do. *
 ****************************************************************************/
void vApplicationIdleHook(void)
{
	// While doing nothing, turn the on-board LED off
	while(1)
		GPIOPinWrite(GPIO_PORTF_BASE,GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3, 0x00);
}

/****************************************************************************************
 * Define Tick Hook Function. Tick frequency was set to 5Hz which is a period of 200ms. *
 ****************************************************************************************/
void vApplicationTickHook(void)
{
	// Delegate work to vTemperatureTask
	xSemaphoreGiveFromISR(xSensorBSemaphore, NULL);
}

/********************************************************************************************
 * Flash Task description. It should receive BUFFER_SIZE values and then store it on Flash. *
 ********************************************************************************************/
void vFlashTask(void *param)
{
	// Declare local variables
	uint32_t v;						// Queue reader variable
	uint32_t i = 0;					// Counter variable
	uint32_t buffer[BUFFER_SIZE];	// Buffer array

	// Define an base address inside Flash addresses range
	// It should be big enough to avoid erasing program data
	// All the buffer data will be written after it
	const uint32_t baseAddr = BASE_ADDR;

	while(1)
	{
		// Gets the first element of the queue structure
		// If there is no element, it should be suspended
		xQueueReceive(xFlashQueue, (void *) &v, TICKS_TO_WAIT);

		// Getting here means that task woke up. Turn on the LED to sign that
		GPIOPinWrite(GPIO_PORTF_BASE,GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3, GREEN);

		// The element read is stored
		buffer[i] = v;

		// If buffer is full, goes inside the block
		if(++i >= BUFFER_SIZE)
		{
			// Erase all the space needed to store the entire buffer
			FlashErase(baseAddr);
			FlashProgram(buffer, baseAddr, sizeof(buffer));

			// Write a message
			xSemaphoreTake(xUARTMutex, TICKS_TO_WAIT);
			UARTprintf("task FLASH gravou dados\n\n");
			xSemaphoreGive(xUARTMutex);

			// Restart counter
			i = 0;
		}
	}
}

/*********************************************************************************************
 * Serial Task description. It should receive BUFFER_SIZE values and then print it via UART. *
 *********************************************************************************************/
void vSerialTask(void *param)
{
	// Declare local variables
	uint32_t v;						// Queue reader variable
	uint32_t i = 0;					// Counter variable
	uint32_t buffer[BUFFER_SIZE];	// Buffer array

	while(1)
	{
		// Gets the first element of the queue structure
		// If there is no element, it should be suspended
		xQueueReceive(xSerialQueue, (void *) &v, TICKS_TO_WAIT);

		// Getting here means that task woke up. Turn on the LED to sign that
		GPIOPinWrite(GPIO_PORTF_BASE,GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3, BLUE);

		// The element read is stored
		buffer[i] = v;

		if(++i >= BUFFER_SIZE)
		{
			// Write a message
			xSemaphoreTake(xUARTMutex, TICKS_TO_WAIT);
			UARTprintf("\ntask SERIAL enviou os seguintes valores\n");

			for(i = 0; i < BUFFER_SIZE; i++)
			{
				UARTprintf("[%02d] %dºC\n", i + 1, buffer[i]);
			}

			UARTprintf("\n");
			xSemaphoreGive(xUARTMutex);

			// Restart counter
			i = 0;
		}
	}
}

/***************************************************************************************************************
 * Temperature Task description. It should get temperature samples after each tick and send it to other tasks. *
 ***************************************************************************************************************/
void vTemperatureTask(void *param)
{
	// Declare local variables
	uint32_t i = 0;						// Counter variable
	uint32_t ui32ADC0Value[4];			// ADC12 read array
	volatile uint32_t ui32TempAvg;		// Temp Average
	volatile uint32_t ui32TempValueC;	// Temp in Celsius scale

	// Enable ADC0 peripheral
	SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
	ADCHardwareOversampleConfigure(ADC0_BASE, 64);

	// Set processor to trigger the sequencer 1
	ADCSequenceConfigure(ADC0_BASE, 1, ADC_TRIGGER_PROCESSOR, 0);
	// Set steps on sequencer 1 to sample the ADC_CTL_TS (Temperature) sensor
	ADCSequenceStepConfigure(ADC0_BASE, 1, 0, ADC_CTL_TS);
	ADCSequenceStepConfigure(ADC0_BASE, 1, 1, ADC_CTL_TS);
	ADCSequenceStepConfigure(ADC0_BASE, 1, 2, ADC_CTL_TS);
	// Set the last step to configure the interrupt flag and be the end of sampling process
	ADCSequenceStepConfigure(ADC0_BASE, 1, 3, ADC_CTL_TS | ADC_CTL_IE | ADC_CTL_END);

	// Enable sequencer 1
	ADCSequenceEnable(ADC0_BASE, 1);

	while(1)
	{
		xSemaphoreTake(xSensorBSemaphore, TICKS_TO_WAIT);
		xSemaphoreTake(xUARTMutex, TICKS_TO_WAIT);
		UARTprintf("[%02d] Task TEMPERATURA acordou\n", ++i);
		xSemaphoreGive(xUARTMutex);

		if(i >= BUFFER_SIZE)
			i = 0;

		GPIOPinWrite(GPIO_PORTF_BASE,GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3, RED);

		// Clear the ADC0 sequencer 1 interrupt flag
		ADCIntClear(ADC0_BASE, 1);

		// Ready to trigger A-D conversion
		ADCProcessorTrigger(ADC0_BASE, 1);

		// Wait for the end of conversion using polling mode
		while(!ADCIntStatus(ADC0_BASE, 1, false));

		// Read data converted
		ADCSequenceDataGet(ADC0_BASE, 1, ui32ADC0Value);

		// Fixed-point arithmetic. Adding 2 at the sum is for rounding
		// Conversion to Celsius scale follows the equation shown at the TM4C123GH6PM datasheet
		ui32TempAvg = (ui32ADC0Value[0] + ui32ADC0Value[1] + ui32ADC0Value[2] + ui32ADC0Value[3] + 2)/4;
		ui32TempValueC = (1475 - ((2475 * ui32TempAvg)) / 4096)/10;

		// Add to the queue the pointer to the value obtained above
		// It will add to the queue the value stored at the address indicated
		xQueueSend(xFlashQueue, (void *) &ui32TempValueC, TICKS_TO_WAIT);
		xQueueSend(xSerialQueue, (void *) &ui32TempValueC, TICKS_TO_WAIT);
	}
}

/***********************************************************
 * Initialize FreeRTOS and start the initial set of tasks. *
 ***********************************************************/
int main(void)
{
    // Set the clocking to run at 50 MHz from the PLL.
    ROM_SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_XTAL_16MHZ |
                       SYSCTL_OSC_MAIN);

    // Initialize the onboard LED
    ConfigureLED();

    // Initialize the UART and configure it
    ConfigureUART();
    UARTprintf("\n\nWelcome to the EK-TM4C123GXL FreeRTOS Demo!\n");

    // Create mutexes, semaphores and queues
    xUARTMutex = xSemaphoreCreateMutex();
    xSensorBSemaphore = xSemaphoreCreateBinary();
    xFlashQueue = xQueueCreate(QUEUE_LENGTH, sizeof(uint32_t));
    xSerialQueue = xQueueCreate(QUEUE_LENGTH, sizeof(uint32_t));

    // Create tasks
    xTaskCreate(vFlashTask, "FLSH_TASK", TASK_STACK_DEPTH, NULL,
    		(tskIDLE_PRIORITY + FLSH_TASK_PRIORITY), NULL);
    xTaskCreate(vSerialTask, "SSRL_TASK", TASK_STACK_DEPTH, NULL,
    		(tskIDLE_PRIORITY + SSRL_TASK_PRIORITY), NULL);
    xTaskCreate(vTemperatureTask, "TEMP_TASK", TASK_STACK_DEPTH, NULL,
    		(tskIDLE_PRIORITY + TEMP_TASK_PRIORITY), NULL);

    // Start the scheduler.  This should not return.
    // If it fails or something else, though, it will return.
    // So keep a loop running doing nothing
    vTaskStartScheduler();
    while(1);
}
