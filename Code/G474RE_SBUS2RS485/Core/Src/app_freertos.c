/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* 调试器中查看；仅保存在 RAM，复位后不保留 */
volatile uint32_t g_rtosFaultCode = 0U;               // RTOS 故障代码
TaskHandle_t volatile g_rtosFaultTask = NULL;         // RTOS 故障任务句柄
/* USER CODE END Variables */
/* Definitions for diagTask */
osThreadId_t diagTaskHandle;
uint32_t diagTaskBuffer[ 256 ];
osStaticThreadDef_t diagTaskControlBlock;
const osThreadAttr_t diagTask_attributes = {
  .name = "diagTask",
  .stack_mem = &diagTaskBuffer[0],
  .stack_size = sizeof(diagTaskBuffer),
  .cb_mem = &diagTaskControlBlock,
  .cb_size = sizeof(diagTaskControlBlock),
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for sbusTask */
osThreadId_t sbusTaskHandle;
uint32_t sbusTaskBuffer[ 512 ];
osStaticThreadDef_t sbusTaskControlBlock;
const osThreadAttr_t sbusTask_attributes = {
  .name = "sbusTask",
  .stack_mem = &sbusTaskBuffer[0],
  .stack_size = sizeof(sbusTaskBuffer),
  .cb_mem = &sbusTaskControlBlock,
  .cb_size = sizeof(sbusTaskControlBlock),
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for modbusTask */
osThreadId_t modbusTaskHandle;
uint32_t modbusTaskBuffer[ 512 ];
osStaticThreadDef_t modbusTaskControlBlock;
const osThreadAttr_t modbusTask_attributes = {
  .name = "modbusTask",
  .stack_mem = &modbusTaskBuffer[0],
  .stack_size = sizeof(modbusTaskBuffer),
  .cb_mem = &modbusTaskControlBlock,
  .cb_size = sizeof(modbusTaskControlBlock),
  .priority = (osPriority_t) osPriorityAboveNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDiagTask(void *argument);
void StartSbusTask(void *argument);
void StartModbusTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
	/* Run time stack overflow checking is performed if
	configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
	called if a stack overflow is detected. */
	(void)pcTaskName;               // 告诉编译器这个参数暂时不用

	__disable_irq();                // CMSIS 提供的处理器接口，用于屏蔽普通可屏蔽中断

	g_rtosFaultTask = xTask;        // 保存 RTOS 故障任务句柄
	g_rtosFaultCode = 1U;           // 保存 RTOS 故障代码，1 对应 StackOverFlow

	for(;;)
	{
    __NOP();                      // 空指令
	}
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
  /* vApplicationMallocFailedHook() will only be called if
  configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
  function that will get called if a call to pvPortMalloc() fails.
  pvPortMalloc() is called internally by the kernel whenever a task, queue,
  timer or semaphore is created. It is also called by various parts of the
  demo application. If heap_1.c or heap_2.c are used, then the size of the
  heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
  FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
  to query the size of free heap space that remains (although it does not
  provide information on how the remaining heap might be fragmented). */
  __disable_irq();              // 屏蔽普通可屏蔽中断

  g_rtosFaultCode = 2U;         // 保存 RTOS 故障代码，2 对应 MallocFailed

  for(;;)
  {
    __NOP();                    // 空指令
  }
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  /* main() 已调用 osKernelInitialize()；创建任务前必须处于 Ready 状态。 */
  if (osKernelGetState() != osKernelReady)
  {
    g_rtosFaultCode = 3U; /* 内核初始化状态异常 */
    Error_Handler();
  }
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of diagTask */
  diagTaskHandle = osThreadNew(StartDiagTask, NULL, &diagTask_attributes);

  /* creation of sbusTask */
  sbusTaskHandle = osThreadNew(StartSbusTask, NULL, &sbusTask_attributes);

  /* creation of modbusTask */
  modbusTaskHandle = osThreadNew(StartModbusTask, NULL, &modbusTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* 调度器尚未启动；任一任务创建失败都禁止继续启动。 */
  if (diagTaskHandle == NULL)
  {
    g_rtosFaultCode = 4U;
    Error_Handler();
  }
  if (sbusTaskHandle == NULL)
  {
    g_rtosFaultCode = 5U;
    Error_Handler();
  }
  if (modbusTaskHandle == NULL)
  {
    g_rtosFaultCode = 6U;
    Error_Handler();
  }
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDiagTask */
/**
  * @brief  Function implementing the diagTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDiagTask */
void StartDiagTask(void *argument)
{
  /* USER CODE BEGIN StartDiagTask */
  /* Infinite loop */
  for(;;)
  {
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    osDelay(500);
  }
  /* USER CODE END StartDiagTask */
}

/* USER CODE BEGIN Header_StartSbusTask */
/**
* @brief Function implementing the sbusTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSbusTask */
void StartSbusTask(void *argument)
{
  /* USER CODE BEGIN StartSbusTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartSbusTask */
}

/* USER CODE BEGIN Header_StartModbusTask */
/**
* @brief Function implementing the modbusTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartModbusTask */
void StartModbusTask(void *argument)
{
  /* USER CODE BEGIN StartModbusTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartModbusTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

