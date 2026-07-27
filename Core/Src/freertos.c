/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
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
#include "usart.h"
#include "speed.h"
#include "oled.h"
#include "esp8266.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* 测速结果结构体：Task_Speed 计算完后放进队列传给 Display 和 Cloud */
typedef struct {
  float    speed_ms;
  float    speed_kmh;
  uint32_t time_us;
} SpeedResult_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
QueueHandle_t speedEventQueue;   /* ISR → Task_Speed */
QueueHandle_t display_queue;     /* Task_Speed → Task_Display */
QueueHandle_t cloud_queue;       /* Task_Speed → Task_Cloud */
SemaphoreHandle_t uart1_mutex;   /* 保护 UART1 */
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void Task_Speed(void *argument);
void Task_Display(void *argument);
void Task_Cloud(void *argument);
void Task_Heartbeat(void *argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  uart1_mutex = xSemaphoreCreateMutex();
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  speedEventQueue = xQueueCreate(5,  sizeof(uint8_t));
  display_queue   = xQueueCreate(3,  sizeof(SpeedResult_t));
  cloud_queue     = xQueueCreate(10, sizeof(SpeedResult_t));
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  static const osThreadAttr_t speedTask_attr = {
    .name = "Task_Speed", .stack_size = 256*4,
    .priority = (osPriority_t)osPriorityHigh,
  };
  osThreadNew(Task_Speed, NULL, &speedTask_attr);

  static const osThreadAttr_t displayTask_attr = {
    .name = "Task_Display", .stack_size = 512*4,
    .priority = (osPriority_t)osPriorityAboveNormal,
  };
  osThreadNew(Task_Display, NULL, &displayTask_attr);

  static const osThreadAttr_t cloudTask_attr = {
    .name = "Task_Cloud", .stack_size = 512*4,
    .priority = (osPriority_t)osPriorityNormal,
  };
  osThreadNew(Task_Cloud, NULL, &cloudTask_attr);

  static const osThreadAttr_t heartbeatTask_attr = {
    .name = "Task_Heartbeat", .stack_size = 256*4,
    .priority = (osPriority_t)osPriorityLow,
  };
  osThreadNew(Task_Heartbeat, NULL, &heartbeatTask_attr);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* 系统存活指示灯：LED 每 500ms 闪，表示调度器在跑 */
  for(;;)
  {
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    osDelay(500);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* ========== Task_Speed：测速核心任务 ========== */
void Task_Speed(void *argument)
{
  uint8_t event;
  char buf[100];
  SpeedResult_t result;

  for(;;)
  {
    if (xQueueReceive(speedEventQueue, &event, portMAX_DELAY) == pdTRUE)
    {
      Speed_Calculate();

      if (Speed_IsNewData())
      {
        result.speed_ms  = Speed_GetMS();
        result.speed_kmh = Speed_GetKMH();
        result.time_us   = g_speed_data.time_diff;

        /* 往两个队列各发一份，Display 和 Cloud 各自收 */
        xQueueSend(display_queue, &result, 0);
        xQueueSend(cloud_queue,  &result, 0);

        /* 串口也打印一份（调试用） */
        snprintf(buf, sizeof(buf),
                 "[Speed] %.2f m/s, %.2f km/h, dt=%lu us\r\n",
                 result.speed_ms, result.speed_kmh,
                 (unsigned long)result.time_us);
        xSemaphoreTake(uart1_mutex, portMAX_DELAY);
        HAL_UART_Transmit(&huart1, (uint8_t*)buf, strlen(buf), 100);
        xSemaphoreGive(uart1_mutex);

        Speed_ClearNewData();
      }
    }
  }
}

/* ========== Task_Display：OLED 显示任务 ========== */
void Task_Display(void *argument)
{
  SpeedResult_t result;

  /* 上电显示待机界面 */
  if (OLED_IsReady())
  {
    OLED_Clear();
    OLED_ShowString(0, 0, "IR Speedometer", OLED_FONT_8X16);
    OLED_ShowString(0, 40, "Waiting...", OLED_FONT_8X16);
    OLED_Refresh();
  }

  for(;;)
  {
    /* 等 Task_Speed 发来速度结果（阻塞） */
    if (xQueueReceive(display_queue, &result, portMAX_DELAY) == pdTRUE)
    {
      if (OLED_IsReady())
      {
        OLED_Clear();
        OLED_ShowString(0, 0, "Speed Result", OLED_FONT_8X16);
        OLED_ShowString(0, 20, "m/s:", OLED_FONT_8X16);
        OLED_ShowFloat(40, 20, result.speed_ms, 2, 2, OLED_FONT_8X16);
        OLED_ShowString(0, 40, "km/h:", OLED_FONT_8X16);
        OLED_ShowFloat(48, 40, result.speed_kmh, 2, 2, OLED_FONT_8X16);
        OLED_Refresh();
      }

      /* 显示 2 秒后恢复待机 */
      osDelay(2000);

      if (OLED_IsReady())
      {
        OLED_Clear();
        OLED_ShowString(0, 0, "IR Speedometer", OLED_FONT_8X16);
        OLED_ShowString(0, 40, "Waiting...", OLED_FONT_8X16);
        OLED_Refresh();
      }
    }
  }
}

/* ========== Task_Cloud：MQTT 上传任务 ========== */
void Task_Cloud(void *argument)
{
  SpeedResult_t result;
  char json[200];
  char buf[60];

  for(;;)
  {
    if (xQueueReceive(cloud_queue, &result, portMAX_DELAY) == pdTRUE)
    {
      if (g_esp8266.mqtt_connected)
      {
        snprintf(json, sizeof(json),
          "{\"id\":\"1\",\"version\":\"1.0\",\"params\":"
          "{\"speed_kmh\":{\"value\":%.2f},"
           "\"speed_ms\":{\"value\":%.2f},"
           "\"time_us\":{\"value\":%lu}}}",
          result.speed_kmh, result.speed_ms,
          (unsigned long)result.time_us);

        ESP8266_Status_t st = ESP8266_MQTTPublish(MQTT_TOPIC_PROPERTY, json);

        snprintf(buf, sizeof(buf), "[Cloud] %s\r\n",
                 (st == ESP8266_OK) ? "Published OK" : "Publish FAIL");
        xSemaphoreTake(uart1_mutex, portMAX_DELAY);
        HAL_UART_Transmit(&huart1, (uint8_t*)buf, strlen(buf), 100);
        xSemaphoreGive(uart1_mutex);
      }
    }
  }
}

/* ========== Task_Heartbeat：MQTT 心跳保活 ========== */
void Task_Heartbeat(void *argument)
{
  char ping[150];

  for(;;)
  {
    osDelay(15000);   /* 每 15 秒 */

    if (g_esp8266.mqtt_connected)
    {
      snprintf(ping, sizeof(ping),
        "{\"id\":\"1\",\"version\":\"1.0\",\"params\":"
        "{\"speed_kmh\":{\"value\":0},"
         "\"speed_ms\":{\"value\":0},"
         "\"time_us\":{\"value\":0}}}");
      ESP8266_MQTTPublish(MQTT_TOPIC_PROPERTY, ping);
    }
  }
}

/* USER CODE END Application */

