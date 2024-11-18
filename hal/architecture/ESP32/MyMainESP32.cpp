/*
* The MySensors Arduino library handles the wireless radio link and protocol
* between your home built sensors/actuators and HA controller of choice.
* The sensors forms a self healing radio network with optional repeaters. Each
* repeater and gateway builds a routing tables in EEPROM which keeps track of the
* network topology allowing messages to be routed to nodes.
*
* Created by Henrik Ekblad <henrik.ekblad@mysensors.org>
* Copyright (C) 2013-2022 Sensnology AB
* Full contributor list: https://github.com/mysensors/MySensors/graphs/contributors
*
* Documentation: http://www.mysensors.org
* Support Forum: http://forum.mysensors.org
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* version 2 as published by the Free Software Foundation.
*
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "Arduino.h"

TaskHandle_t loopTaskHandle = NULL;
#ifdef MY_SEPARATE_PROCESS_TASK
 TaskHandle_t processTaskHandle = NULL;
 volatile bool beginDone = false;
#endif

#if CONFIG_AUTOSTART_ARDUINO

/*
#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif
*/

bool loopTaskWDTEnabled;

void loopTask(void *pvParameters)
{
    const TickType_t xPeriod = 1 / portTICK_PERIOD_MS;
    TickType_t xLastWakeTime = xTaskGetTickCount();
#ifdef MY_SEPARATE_PROCESS_TASK
    while (!beginDone) { vTaskDelayUntil( &xLastWakeTime, xPeriod ); }
#else
	_begin();			// Startup MySensors library
#endif
	for(;;) {
		if(loopTaskWDTEnabled) {
			esp_task_wdt_reset();
		}
#ifdef MY_SEPARATE_PROCESS_TASK
        vTaskDelayUntil( &xLastWakeTime, xPeriod );
#else
		_process();		// Process incoming data
#endif
		loop();
	}
}

#ifdef MY_SEPARATE_PROCESS_TASK
void processTask(void *pvParameters)
{
    const TickType_t xPeriod = 1 / portTICK_PERIOD_MS;
    TickType_t xLastWakeTime = xTaskGetTickCount();

	_begin();			// Startup MySensors library
    beginDone = true;
	for(;;) {
		_process();		// Process incoming data
        vTaskDelayUntil( &xLastWakeTime, xPeriod );
	}
}
#endif

extern "C" void app_main()
{
	loopTaskWDTEnabled = false;
	initArduino();
#ifdef MY_SEPARATE_PROCESS_TASK
    xTaskCreatePinnedToCore(processTask, "processTask", 8192, NULL, 1, &processTaskHandle, ARDUINO_RUNNING_CORE);
    //xTaskCreate(processTask, "processTask", 8192, NULL, 1, &processTaskHandle);
#endif
	xTaskCreatePinnedToCore(loopTask, "loopTask", 8192, NULL, 1, &loopTaskHandle, ARDUINO_RUNNING_CORE);
}

#endif
