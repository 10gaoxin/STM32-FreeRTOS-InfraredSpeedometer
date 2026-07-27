/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "cmsis_os.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "speed.h"
#include "oled.h"
#include "esp8266.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define USE_ESP8266       /* 已加传感器串联电阻，BOR问题解决后开�? */
#define SPEED_LIMIT_MS    0.6f   /* 超�?�阈值：0.6 m/s */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
char uart_buf[100];
uint8_t esp8266_enabled = 0;
uint8_t uart2_rx_byte;
uint8_t mqtt_fail_count = 0;
static char reset_cause_str[32];  /* 启动时记录的复位原因 */
static uint32_t last_mqtt_activity = 0;    /* 上次MQTT发�?�时间，用于keepalive */
static uint32_t last_reconnect_time = 0;   /* 上次主动重连发起时间，用于冷却限�? */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* ====== 复位原因诊断（必须在HAL_Init前读取，否则标志被清除） ====== */
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST))
    strcpy(reset_cause_str, "POR/BOR Reset");      /* 上电或欠压复�? */
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST))
    strcpy(reset_cause_str, "NRST Pin Reset");
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))
    strcpy(reset_cause_str, "Software Reset");
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST))
    strcpy(reset_cause_str, "IWDG Watchdog");
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST))
    strcpy(reset_cause_str, "WWDG Watchdog");
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST))
    strcpy(reset_cause_str, "Low-Power Reset");
  else
    strcpy(reset_cause_str, "Unknown");
  __HAL_RCC_CLEAR_RESET_FLAGS();
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
	  HAL_Delay(500);

    /* ====== 速度模块初始�? ====== */
    Speed_Init();



  /* 初始化OLED（内部自动检测I2C地址 0x3C/0x3D�? */
  OLED_Init();

  /* 发�?�启动信息（含复位原因，用于诊断异常重启�? */
  sprintf(uart_buf, "\r\n=== Infrared Speedometer Started ===\r\n");
  HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
  sprintf(uart_buf, "Reset cause: %s\r\n", reset_cause_str);
  HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

  if (OLED_IsReady())
  {
    sprintf(uart_buf, "OLED: Found OK\r\n");
  }
  else
  {
    sprintf(uart_buf, "OLED: Not found (check wiring/power)\r\n");
  }
  HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

  sprintf(uart_buf, "Sensor Distance: %d mm\r\n", SENSOR_DISTANCE_MM);
  HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

  /* 初始化ESP8266，并在OLED实时显示状�?? */
  #ifdef USE_ESP8266
  /* 启动UART2接收中断 */
  HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1);

  /* OLED：显示初始化进度框架 */
  OLED_Clear();
  OLED_ShowString(0, 0,  "IR Speedometer", OLED_FONT_8X16);
  OLED_ShowString(0, 20, "ESP: Init...  ", OLED_FONT_6X8);
  OLED_ShowString(0, 30, "WiFi: ---     ", OLED_FONT_6X8);
  OLED_ShowString(0, 40, "MQTT: ---     ", OLED_FONT_6X8);
  OLED_Refresh();

  sprintf(uart_buf, "\r\n=== Initializing ESP8266 ===\r\n");
  HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

  ESP8266_Status_t esp_status = ESP8266_Init();

  if (esp_status == ESP8266_OK)
  {
    OLED_ShowString(0, 20, "ESP: OK       ", OLED_FONT_6X8);
    OLED_ShowString(0, 30, "WiFi: Conn... ", OLED_FONT_6X8);
    OLED_Refresh();

    sprintf(uart_buf, "ESP8266 OK, connecting WiFi...\r\n");
    HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

    /* WiFi连接�?多重�?3次（热点响应慢时给更多机会） */
    {
      uint8_t wifi_try;
      for (wifi_try = 0; wifi_try < 3; wifi_try++)
      {
        if (wifi_try > 0)
        {
          sprintf(uart_buf, "WiFi retry %d/3...\r\n", wifi_try + 1);
          HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
          OLED_ShowString(0, 30, "WiFi: Retry...", OLED_FONT_6X8);
          OLED_Refresh();
          HAL_Delay(2000);
        }
        if (ESP8266_ConnectWiFi(WIFI_SSID, WIFI_PASSWORD) == ESP8266_OK) break;
      }
    }
    if (g_esp8266.wifi_status == WIFI_GOT_IP)
    {
      OLED_ShowString(0, 30, "WiFi: OK      ", OLED_FONT_6X8);
      OLED_ShowString(0, 40, "MQTT: Conn... ", OLED_FONT_6X8);
      OLED_Refresh();

      sprintf(uart_buf, "WiFi OK, configuring MQTT...\r\n");
      HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

      sprintf(uart_buf, "MQTT Config...\r\n");
      HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

      /* MQTT配置失败后无条件复位：AT+MQTTUSERCFG超时会破坏MQTT内部状�?�机�?
       * 仅靠AT测试无法�?测，必须RST清除后重连WiFi再重�? */
      {
        ESP8266_Status_t cfg_st = ESP8266_MQTTConfig();
        if (cfg_st != ESP8266_OK)
        {
          sprintf(uart_buf, "MQTT Config failed, resetting ESP8266...\r\n");
          HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
          ESP8266_Reset();          /* AT+RST + 内部等待3s */
          HAL_Delay(2000);          /* 额外等待模块稳定 */
          ESP8266_SendCmd("ATE0", "OK", 1000);
          ESP8266_ConnectWiFi(WIFI_SSID, WIFI_PASSWORD); /* WiFi复位后需重连 */
          sprintf(uart_buf, "MQTT Config retry...\r\n");
          HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
          cfg_st = ESP8266_MQTTConfig();
        }
        if (cfg_st == ESP8266_OK)
      {
        sprintf(uart_buf, "MQTT Config OK, connecting...\r\n");
        HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
        HAL_Delay(300); /* 等待MQTT config的UART响应全部到达，避免污染下�?条命令的缓冲�? */

        /* MQTT connect�?多重�?2次：WiFi刚连上时网络栈未稳定，ERROR后等3s重试 */
        {
          ESP8266_Status_t conn_st = ESP8266_MQTTConnect();
          if (conn_st != ESP8266_OK)
          {
            sprintf(uart_buf, "MQTT connect retry (network stabilizing)...\r\n");
            HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
            HAL_Delay(3000);
            conn_st = ESP8266_MQTTConnect();
          }

        if (conn_st == ESP8266_OK)
        {
          OLED_ShowString(0, 40, "MQTT: OK      ", OLED_FONT_6X8);
          OLED_ShowString(0, 52, "OneNET Ready! ", OLED_FONT_6X8);
          OLED_Refresh();

          sprintf(uart_buf, "OneNET MQTT Connected!\r\n\r\n");
          HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
          esp8266_enabled = 1;
          last_mqtt_activity = HAL_GetTick(); /* 从连接时刻开始计算心跳超�? */

          /* 立即发一条心跳，防止broker因idle超时断连 */
          {
            char ping_payload[128];
            snprintf(ping_payload, sizeof(ping_payload),
                     "{\"id\":\"1\",\"version\":\"1.0\",\"params\":{\"speed_kmh\":{\"value\":0},\"speed_ms\":{\"value\":0},\"time_us\":{\"value\":0}}}");
            ESP8266_MQTTPublish(MQTT_TOPIC_PROPERTY, ping_payload);
            /* ping失败不代表连接断�?（broker可能拒绝初始消息），恢复标志避免误触发重�? */
            g_esp8266.mqtt_connected = 1;
            last_mqtt_activity = HAL_GetTick();
          }
        }
        else
        {
          /* MQTT连接失败（broker暂不可达），但WiFi正常�?
           * 不进本地模式，主循环的fail_count机制会自动重连MQTT�? */
          OLED_ShowString(0, 40, "MQTT: retry.. ", OLED_FONT_6X8);
          OLED_ShowString(0, 52, "WiFi OK       ", OLED_FONT_6X8);
          OLED_Refresh();
          sprintf(uart_buf, "MQTT connect failed, main loop will retry\r\n");
          HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
          esp8266_enabled = 1; /* WiFi已连，主循环负责MQTT重连 */
          last_mqtt_activity = HAL_GetTick(); /* 防止心跳在第�?次主循环迭代就立即触�? */
        }
        } /* end conn_st block */
      }
      else
      {
        OLED_ShowString(0, 40, "MQTT: CFG ERR ", OLED_FONT_6X8);
        OLED_ShowString(0, 52, "Local mode    ", OLED_FONT_6X8);
        OLED_Refresh();
        sprintf(uart_buf, "MQTT config failed (token too long?)\r\n\r\n");
        HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
      }
      } /* end cfg_st block */
    }
    else
    {
      OLED_ShowString(0, 30, "WiFi: FAILED  ", OLED_FONT_6X8);
      OLED_ShowString(0, 52, "Local mode    ", OLED_FONT_6X8);
      OLED_Refresh();
      sprintf(uart_buf, "WiFi failed, local mode\r\n\r\n");
      HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
    }
  }
  else
  {
    OLED_ShowString(0, 20, "ESP: FAILED   ", OLED_FONT_6X8);
    OLED_ShowString(0, 52, "Local mode    ", OLED_FONT_6X8);
    OLED_Refresh();
    sprintf(uart_buf, "ESP8266 failed, local mode\r\n\r\n");
    HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
  }

  /* 若未成功联网，关闭ESP8266的WiFi射频，防止后台扫描产生电流峰值导致欠压复�? */
  if (!esp8266_enabled)
  {
    ESP8266_SendCmd("AT+CWMODE=0", "OK", 2000);
    sprintf(uart_buf, "ESP8266 RF off (local mode)\r\n");
    HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
  }

  /* 显示2秒状态后切换到待机界�? */
  HAL_Delay(2000);
  #endif

  /* OLED显示待机界面（含云端状�?�） */
  OLED_Clear();
  OLED_ShowString(0, 0, "IR Speedometer", OLED_FONT_8X16);
  OLED_ShowString(0, 20, "Dist:", OLED_FONT_6X8);
  OLED_ShowNum(30, 20, SENSOR_DISTANCE_MM, 3, OLED_FONT_6X8);
  OLED_ShowString(48, 20, "mm", OLED_FONT_6X8);
  OLED_ShowString(0, 30, "Waiting...", OLED_FONT_8X16);
  { char _bs[12]; sprintf(_bs, "Buf:0/%d   ", SPEED_MEDIAN_N); OLED_ShowString(0, 47, _bs, OLED_FONT_6X8); }  /* 中�?�滤波进�? (y=47: 8x16字体在y=30结束于y=45，留2px间距) */
  #ifdef USE_ESP8266
  if (esp8266_enabled)
    OLED_ShowString(0, 55, "Cloud:ON ", OLED_FONT_6X8);
  else
    OLED_ShowString(0, 55, "Cloud:OFF", OLED_FONT_6X8);
  #endif
  OLED_Refresh();

  sprintf(uart_buf, "Waiting for object...\r\n\r\n");
  HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();  /* Call init function for freertos objects (in freertos.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();
  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* 速度计算移至主循环（避免在ISR中做浮点运算导致电流尖峰�? */
    if (g_speed_data.state == SPEED_COMPLETED)
    {
      Speed_Calculate();

      /* 中�?�滤波缓冲未满时（第1/2次），在OLED和串口给出进度提�? */
      if (!Speed_IsNewData() && g_speed_data.median_count > 0)
      {
        char cnt_str[16];
        sprintf(cnt_str, "Buf:%d/%d   ", g_speed_data.median_count, SPEED_MEDIAN_N);
        OLED_ShowString(0, 47, cnt_str, OLED_FONT_6X8);
        OLED_Refresh();
        sprintf(uart_buf, "[Median] sample %d/%d buffered\r\n",
                g_speed_data.median_count, SPEED_MEDIAN_N);
        HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
      }
    }

    if (Speed_IsNewData())
    {
      /* �?查是否超�? */
      uint8_t is_overspeed = (Speed_GetMS() > SPEED_LIMIT_MS);

      /* 蜂鸣器提示（低电平有效） */
      if (is_overspeed) {
        /* 超�?�：长鸣500ms */
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
        HAL_Delay(500);
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
      } else {
        /* 正常：短�?100ms */
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
        HAL_Delay(100);
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
      }

      /* OLED显示测�?�结�? */

      OLED_Clear();

      /* 超�?�显示警�? */
      if (is_overspeed) {
        OLED_ShowString(0, 0, "OVERSPEED!", OLED_FONT_8X16);
      } else {
        OLED_ShowString(0, 0, "Speed Result", OLED_FONT_8X16);
      }

      /* 显示 m/s (y=20, 占用�? 20-35) */
      OLED_ShowString(0, 20, "m/s:", OLED_FONT_8X16);
      OLED_ShowFloat(40, 20, Speed_GetMS(), 2, 2, OLED_FONT_8X16);

      /* 显示 km/h (y=40, 占用�? 40-55) */
      OLED_ShowString(0, 40, "km/h:", OLED_FONT_8X16);
      OLED_ShowFloat(48, 40, Speed_GetKMH(), 2, 2, OLED_FONT_8X16);

      /* 云端上传状�?�占�? (y=56, 占用�? 56-63) */
      #ifdef USE_ESP8266
      if (esp8266_enabled)
        OLED_ShowString(0, 56, "Cloud:...  ", OLED_FONT_6X8);
      else
        OLED_ShowString(0, 56, "Cloud:OFF  ", OLED_FONT_6X8);
      #endif

      OLED_Refresh();

      /* 串口输出结果�?? */
      sprintf(uart_buf, "--- Speed Measured (median of %d) ---\r\n", SPEED_MEDIAN_N);
      HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

      /* time_diff = �?3次原始采样的时间差，仅供参�?�，速度为中值结�? */
      sprintf(uart_buf, "LastRaw: %lu us\r\n", g_speed_data.time_diff);
      HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

      sprintf(uart_buf, "Speed(median): %.2f m/s\r\n", Speed_GetMS());
      HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

      sprintf(uart_buf, "Speed(median): %.2f km/h\r\n", Speed_GetKMH());
      HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

      /* 超�?�标�? */
      if (is_overspeed) {
        sprintf(uart_buf, "*** OVERSPEED! Limit: %.2f m/s ***\r\n\r\n", SPEED_LIMIT_MS);
        HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
      } else {
        sprintf(uart_buf, "\r\n");
        HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
      }

      /* 上传数据到OneNET �? 每次发布前重建连�?
       * 原理：OneNET收到数据后会关闭TCP连接，因此每次用新连接发送（参�?�CSDN方案�?
       * reconnect=0已关闭自动重连，由此处代码统�?管理重连时机 */
      #ifdef USE_ESP8266
      if (esp8266_enabled)
      {
        char json_payload[200];
        uint8_t upload_ok = 0;
        uint8_t conn_ok   = 0;
        uint8_t conn_try;

        /* 先清理旧连接，等待ESP8266和broker完全断开（避免broker限�?�） */
        ESP8266_SendCmd("AT+MQTTDISCONN=0", "OK", 2000);
        ESP8266_SendCmd("AT+MQTTCLEAN=0",   "OK", 2000);
        HAL_Delay(3000);  /* 等待3秒，让broker释放旧会�? */

        /* 重建MQTT连接（MQTTConfig内部含DISCONNECT+CLEAN，最多尝�?2次） */
        for (conn_try = 0; conn_try < 2; conn_try++)
        {
          if (ESP8266_MQTTConfig() == ESP8266_OK)
          {
            HAL_Delay(300);
            if (ESP8266_MQTTConnect() == ESP8266_OK)
            {
              conn_ok = 1;
              break;
            }
          }
          sprintf(uart_buf, "MQTT connect retry %d/2...\r\n", conn_try + 1);
          HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
          HAL_Delay(2000);
        }

        if (conn_ok)
        {
          /* 物模型属性上报（OneNET控制台显示） */
          snprintf(json_payload, sizeof(json_payload),
                  "{\"id\":\"1\",\"version\":\"1.0\",\"params\":{\"speed_kmh\":{\"value\":%.2f},\"speed_ms\":{\"value\":%.2f},\"time_us\":{\"value\":%lu}}}",
                  Speed_GetKMH(), Speed_GetMS(), g_speed_data.time_diff);

          if (ESP8266_MQTTPublish(MQTT_TOPIC_PROPERTY, json_payload) == ESP8266_OK)
          {
            upload_ok = 1;
            last_mqtt_activity = HAL_GetTick();
            sprintf(uart_buf, "Property uploaded to OneNET\r\n");
            HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

            /* 旧版数据流上报（历史记录兼容�? */
            snprintf(json_payload, sizeof(json_payload),
                    "{\"id\":1,\"dp\":{\"speed_kmh\":[{\"v\":%.2f}],\"speed_ms\":[{\"v\":%.2f}],\"time_us\":[{\"v\":%lu}]}}",
                    Speed_GetKMH(), Speed_GetMS(), g_speed_data.time_diff);
            ESP8266_MQTTPublish(MQTT_TOPIC, json_payload);
            g_esp8266.mqtt_connected = 1;
          }
        }

        if (upload_ok)
        {
          OLED_ShowString(0, 56, "Cloud:OK   ", OLED_FONT_6X8);
          OLED_Refresh();
          sprintf(uart_buf, "Data uploaded to OneNET\r\n");
          HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
        }
        else
        {
          OLED_ShowString(0, 56, "Cloud:FAIL ", OLED_FONT_6X8);
          OLED_Refresh();
          sprintf(uart_buf, "Upload failed\r\n");
          HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);
        }

      }
      #endif

      /* 立即清除新数据标志，允许传感器开始下�?次测�? */
      Speed_ClearNewData();

      /* 延时1秒后恢复待机界面 */
      HAL_Delay(2000);
      OLED_Clear();
      OLED_ShowString(0, 0, "IR Speedometer", OLED_FONT_8X16);
      OLED_ShowString(0, 20, "Dist:", OLED_FONT_6X8);
      OLED_ShowNum(30, 20, SENSOR_DISTANCE_MM, 3, OLED_FONT_6X8);
      OLED_ShowString(48, 20, "mm", OLED_FONT_6X8);
      OLED_ShowString(0, 30, "Waiting...", OLED_FONT_8X16);
      { char _bs[12]; sprintf(_bs, "Buf:0/%d   ", SPEED_MEDIAN_N); OLED_ShowString(0, 47, _bs, OLED_FONT_6X8); }  /* 中�?�滤波进度清�? */
      #ifdef USE_ESP8266
      if (esp8266_enabled)
        OLED_ShowString(0, 55, "Cloud:ON ", OLED_FONT_6X8);
      else
        OLED_ShowString(0, 55, "Cloud:OFF", OLED_FONT_6X8);
      #endif
      OLED_Refresh();
    }

    /* MQTT keepalive：超�?25秒没有发送任何数据，发一条心�?
     * 注：条件不依�? MQTTIsConnected()，因为初始ping失败会将 mqtt_connected 清零�?
     * 导致心跳永远不触发，broker随即idle超时踢掉连接�?
     * 此处先调�? CheckConn 同步实际状�?�（支持auto-reconnect），再决定是否发心跳�?*/
    #ifdef USE_ESP8266
    if (esp8266_enabled && (HAL_GetTick() - last_mqtt_activity) >= 15000)
    {
      last_mqtt_activity = HAL_GetTick(); /* 先更新，避免CheckConn/Publish耗时期间反复触发 */
      if (!ESP8266_MQTTIsConnected())
        ESP8266_MQTTCheckConn(); /* 查询实际状�?�，处理auto-reconnect场景 */
      if (ESP8266_MQTTIsConnected())
      {
        char ping_buf[128];
        snprintf(ping_buf, sizeof(ping_buf),
                 "{\"id\":\"1\",\"version\":\"1.0\",\"params\":{\"speed_kmh\":{\"value\":0},\"speed_ms\":{\"value\":0},\"time_us\":{\"value\":0}}}");
        ESP8266_MQTTPublish(MQTT_TOPIC_PROPERTY, ping_buf);
      }
    }
    #endif

    HAL_Delay(10);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/**
  * @brief  GPIO外部中断回调函数
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == SENSOR_A_Pin)
  {
    Speed_SensorA_Trigger();
  }
  else if (GPIO_Pin == SENSOR_B_Pin)
  {
    Speed_SensorB_Trigger();

    /* Day 3：ISR 里往队列发消息，通知 Task_Speed 处理 */
    extern QueueHandle_t speedEventQueue;
    uint8_t evt = 1;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(speedEventQueue, &evt, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}

/**
  * @brief  定时器溢出回调函�??
  */

/**
  * @brief  UART接收完成回调函数
  */
#ifdef USE_ESP8266
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    ESP8266_RxCallback(uart2_rx_byte);
    HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1);
  }
}
#endif
/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM4 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM4) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
