/**
  ******************************************************************************
  * @file    esp8266.h
  * @brief   ESP8266 WiFi模块驱动头文件（MQTT版，适用于AT v2.x固件）
  ******************************************************************************
  */

#ifndef __ESP8266_H
#define __ESP8266_H

#include "main.h"
#include "usart.h"

/* ESP8266配置参数 */
#define ESP8266_UART            huart2          /* ESP8266使用USART2 */
#define ESP8266_TIMEOUT_MS      5000            /* 超时时间 (ms) */
#define ESP8266_RX_BUF_SIZE     512             /* 接收缓冲区大小 */

/* WiFi配置 */
#define WIFI_SSID               "jiang"
#define WIFI_PASSWORD           "123456789"

/* OneNET MQTT配置 */
#define MQTT_BROKER             "mqtts.heclouds.com"
#define MQTT_PORT               1883
#define MQTT_CLIENT_ID          "stm32_speed"
#define MQTT_USERNAME           "0GHmR8500z"
#define MQTT_PASSWORD           "version=2018-10-31&res=products%2F0GHmR8500z%2Fdevices%2Fstm32_speed&et=1893456000&method=md5&sign=AtIKqVqExLyzS4bR9ROiww%3D%3D"
#define MQTT_TOPIC              "$sys/0GHmR8500z/stm32_speed/dp/post/json"
/* 物模型属性上报Topic（用于OneNET属性页面显示） */
#define MQTT_TOPIC_PROPERTY     "$sys/0GHmR8500z/stm32_speed/thing/property/post"

/* ESP8266状态 */
typedef enum {
    ESP8266_OK = 0,
    ESP8266_ERROR,
    ESP8266_TIMEOUT,
    ESP8266_BUSY
} ESP8266_Status_t;

/* WiFi状态 */
typedef enum {
    WIFI_DISCONNECTED = 0,
    WIFI_CONNECTED,
    WIFI_GOT_IP
} WiFi_Status_t;

/* ESP8266数据结构 */
typedef struct {
    WiFi_Status_t wifi_status;
    uint8_t mqtt_connected;
    char rx_buffer[ESP8266_RX_BUF_SIZE];
    uint16_t rx_index;
    uint8_t rx_complete;
} ESP8266_t;

/* 外部变量 */
extern ESP8266_t g_esp8266;

/* 函数声明 */
ESP8266_Status_t ESP8266_Init(void);
ESP8266_Status_t ESP8266_Reset(void);
ESP8266_Status_t ESP8266_Test(void);
ESP8266_Status_t ESP8266_SendCmd(const char *cmd, const char *ack, uint32_t timeout);
void ESP8266_ClearRxBuffer(void);

/* WiFi */
ESP8266_Status_t ESP8266_SetMode(uint8_t mode);
ESP8266_Status_t ESP8266_ConnectWiFi(const char *ssid, const char *password);
WiFi_Status_t ESP8266_GetWiFiStatus(void);

/* MQTT */
ESP8266_Status_t ESP8266_MQTTConfig(void);
ESP8266_Status_t ESP8266_MQTTConnect(void);
ESP8266_Status_t ESP8266_MQTTPublish(const char *topic, const char *payload);
uint8_t ESP8266_MQTTIsConnected(void);
uint8_t ESP8266_MQTTCheckConn(void);  /* 查询ESP8266实际状态，处理auto-reconnect */

/* 回调 */
void ESP8266_RxCallback(uint8_t data);

/* 调试：获取最近一次AT响应原文（用于打印诊断） */
const char* ESP8266_GetLastResponse(void);

#endif /* __ESP8266_H */
