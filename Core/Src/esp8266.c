/**
  ******************************************************************************
  * @file    esp8266.c
  * @brief   ESP8266 WiFi模块驱动（MQTT版，适用于AT v2.x固件）
  ******************************************************************************
  */

#include "esp8266.h"
#include <string.h>
#include <stdio.h>

/* 全局变量 */
ESP8266_t g_esp8266;
static char esp8266_debug_buf[128];

/**
  * @brief  清空接收缓冲区
  */
void ESP8266_ClearRxBuffer(void)
{
    memset(g_esp8266.rx_buffer, 0, ESP8266_RX_BUF_SIZE);
    g_esp8266.rx_index = 0;
    g_esp8266.rx_complete = 0;
}

/**
  * @brief  发送AT指令并等待响应
  */
ESP8266_Status_t ESP8266_SendCmd(const char *cmd, const char *ack, uint32_t timeout)
{
    uint32_t start_time;

    ESP8266_ClearRxBuffer();

    HAL_UART_Transmit(&ESP8266_UART, (uint8_t*)cmd, strlen(cmd), 1000);
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t*)"\r\n", 2, 1000);

    start_time = HAL_GetTick();
    while ((HAL_GetTick() - start_time) < timeout)
    {
        if (strstr(g_esp8266.rx_buffer, ack) != NULL)
            return ESP8266_OK;
        if (strstr(g_esp8266.rx_buffer, "ERROR") != NULL)
            return ESP8266_ERROR;
        if (strstr(g_esp8266.rx_buffer, "\r\nFAIL\r\n") != NULL)  /* WiFi连接失败 */
            return ESP8266_ERROR;
        HAL_Delay(10);
    }

    return ESP8266_TIMEOUT;
}

/**
  * @brief  AT测试
  */
ESP8266_Status_t ESP8266_Test(void)
{
    return ESP8266_SendCmd("AT", "OK", 1000);
}

/**
  * @brief  复位
  */
ESP8266_Status_t ESP8266_Reset(void)
{
    ESP8266_Status_t status;
    status = ESP8266_SendCmd("AT+RST", "OK", 2000);
    if (status == ESP8266_OK)
    {
        HAL_Delay(3000);
        g_esp8266.wifi_status = WIFI_DISCONNECTED;
        g_esp8266.mqtt_connected = 0;
    }
    return status;
}

/**
  * @brief  设置WiFi模式
  */
ESP8266_Status_t ESP8266_SetMode(uint8_t mode)
{
    char cmd[32];
    sprintf(cmd, "AT+CWMODE=%d", mode);
    return ESP8266_SendCmd(cmd, "OK", 2000);
}

/**
  * @brief  连接WiFi
  */
ESP8266_Status_t ESP8266_ConnectWiFi(const char *ssid, const char *password)
{
    char cmd[128];
    ESP8266_Status_t status;

    status = ESP8266_SetMode(1);
    if (status != ESP8266_OK) return status;

    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
    status = ESP8266_SendCmd(cmd, "OK", 15000);

    if (status == ESP8266_OK)
    {
        g_esp8266.wifi_status = WIFI_CONNECTED;
        HAL_Delay(1000);
        status = ESP8266_SendCmd("AT+CIFSR", "OK", 2000);
        if (status == ESP8266_OK)
        {
            g_esp8266.wifi_status = WIFI_GOT_IP;
            HAL_Delay(2000); /* 等待WiFi后台事件发完，避免干扰后续MQTT命令 */
        }
    }

    return status;
}

/**
  * @brief  获取WiFi状态
  */
WiFi_Status_t ESP8266_GetWiFiStatus(void)
{
    return g_esp8266.wifi_status;
}

/**
  * @brief  配置MQTT用户信息（AT v2.x）
  *         AT+MQTTUSERCFG=LinkID,scheme,client_id,username,password,cert_key_ID,CA_ID,path
  */
ESP8266_Status_t ESP8266_MQTTConfig(void)
{
    /* 验证模块就绪，避免在超时卡死状态下发送后续命令 */
    if (ESP8266_Test() != ESP8266_OK)
        return ESP8266_ERROR;

    /* 先断开已有连接（忽略返回值，无连接时返回ERROR属正常） */
    ESP8266_SendCmd("AT+MQTTDISCONN=0", "OK", 2000);
    HAL_Delay(500);

    /* 清除旧的MQTT连接状态，忽略返回值 */
    ESP8266_SendCmd("AT+MQTTCLEAN=0", "OK", 2000);
    HAL_Delay(500);

    /* 缓冲区需足够大容纳完整Token（约300字节） */
    static char cmd[350];
    snprintf(cmd, sizeof(cmd),
             "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"",
             MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
    {
        ESP8266_Status_t st = ESP8266_SendCmd(cmd, "OK", 5000);
        if (st != ESP8266_OK) return st;
    }

    /* 配置连接参数：keepalive=30s + cleanSession=true
     * keepalive=30  → AT固件每30s自动发PINGREQ，broker允许45s空闲；
     *                 解决OneNET约20s无数据即踢连接的问题
     * cleanSession=0 disable_clean_session参数=0 → 即cleanSession=true，
     *                 每次重连broker清除旧会话，不再因"会话冲突"返回ERROR
     * 部分固件版本不支持此命令，返回ERROR时忽略（后续依靠代码层heartbeat兜底） */
    ESP8266_SendCmd("AT+MQTTCONNCFG=0,15,0,\"\",\"\",0,0", "OK", 2000);
    HAL_Delay(200);

    return ESP8266_OK;
}

/**
  * @brief  连接MQTT Broker（AT v2.x）
  *         AT+MQTTCONN=LinkID,host,port,reconnect
  * @note   自定义等待循环，忽略期间出现的WiFi事件（CONNECTED/DISCONNECT/GOT IP），
  *         避免热点短暂抖动导致误判为broker不可达。
  */
ESP8266_Status_t ESP8266_MQTTConnect(void)
{
    char cmd[80];
    uint32_t start_time;

    /* 先查询当前状态：reconnect=1时ESP8266可能已在后台自动重连。
     * 若已连接则直接返回OK，避免向broker重复发CONNREQ导致ERROR（broker拒绝已有会话）*/
    ESP8266_ClearRxBuffer();
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t*)"AT+MQTTCONN?\r\n", 14, 1000);
    HAL_Delay(600);
    if (strstr(g_esp8266.rx_buffer, "+MQTTCONN:0,3") != NULL ||
        strstr(g_esp8266.rx_buffer, "+MQTTCONN:0,4") != NULL)
    {
        g_esp8266.mqtt_connected = 1;
        return ESP8266_OK;  /* 已连接，无需再发AT+MQTTCONN */
    }

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTCONN=0,\"%s\",%d,0",
             MQTT_BROKER, MQTT_PORT);

    ESP8266_ClearRxBuffer();
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t*)cmd, strlen(cmd), 1000);
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t*)"\r\n", 2, 1000);

    start_time = HAL_GetTick();
    while ((HAL_GetTick() - start_time) < 15000)
    {
        if (strstr(g_esp8266.rx_buffer, "OK") != NULL)
        {
            g_esp8266.mqtt_connected = 1;
            return ESP8266_OK;
        }
        if (strstr(g_esp8266.rx_buffer, "ERROR") != NULL)
            return ESP8266_ERROR;

        /* WiFi状态事件（热点抖动产生），清缓冲区继续等MQTT结果 */
        if (strstr(g_esp8266.rx_buffer, "WIFI ") != NULL)
            ESP8266_ClearRxBuffer();

        HAL_Delay(10);
    }

    return ESP8266_TIMEOUT;
}

/**
  * @brief  发布MQTT消息（AT v2.x，使用RAW模式避免转义问题）
  *         AT+MQTTPUBRAW=LinkID,topic,length,qos,retain
  */
ESP8266_Status_t ESP8266_MQTTPublish(const char *topic, const char *payload)
{
    char cmd[128];
    uint16_t len = strlen(payload);
    uint32_t start_time;

    /* 发送 MQTTPUBRAW 命令 */
    snprintf(cmd, sizeof(cmd), "AT+MQTTPUBRAW=0,\"%s\",%d,0,0", topic, len);

    ESP8266_ClearRxBuffer();
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t*)cmd, strlen(cmd), 1000);
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t*)"\r\n", 2, 1000);

    /* 等待 ">" 提示符（ERROR立即返回，不等满3秒） */
    start_time = HAL_GetTick();
    while ((HAL_GetTick() - start_time) < 3000)
    {
        if (strstr(g_esp8266.rx_buffer, ">") != NULL) break;
        if (strstr(g_esp8266.rx_buffer, "ERROR") != NULL)
        {
            g_esp8266.mqtt_connected = 0;
            return ESP8266_ERROR;
        }
        HAL_Delay(10);
    }
    if (strstr(g_esp8266.rx_buffer, ">") == NULL)
    {
        g_esp8266.mqtt_connected = 0;
        return ESP8266_TIMEOUT;
    }

    /* 发送 JSON 数据 */
    ESP8266_ClearRxBuffer();
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t*)payload, len, 2000);

    /* 等待 "+MQTTPUB:OK" */
    start_time = HAL_GetTick();
    while ((HAL_GetTick() - start_time) < 8000)
    {
        if (strstr(g_esp8266.rx_buffer, "+MQTTPUB:OK") != NULL)
            return ESP8266_OK;
        if (strstr(g_esp8266.rx_buffer, "+MQTTPUB:FAIL") != NULL)
        {
            g_esp8266.mqtt_connected = 0;
            return ESP8266_ERROR;
        }
        HAL_Delay(10);
    }

    g_esp8266.mqtt_connected = 0;
    return ESP8266_TIMEOUT;
}

/**
  * @brief  检查MQTT连接状态（本地标志，轻量）
  */
uint8_t ESP8266_MQTTIsConnected(void)
{
    return g_esp8266.mqtt_connected;
}

/**
  * @brief  查询ESP8266实际MQTT状态（AT+MQTTCONN?）
  * @note   reconnect=1时ESP8266会自动重连，此函数可同步本地标志
  *         state=4 表示已连接到broker
  */
uint8_t ESP8266_MQTTCheckConn(void)
{
    ESP8266_ClearRxBuffer();
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t*)"AT+MQTTCONN?\r\n", 14, 1000);
    HAL_Delay(500);
    /* state=3: MQTT已连接(未订阅); state=4: MQTT已连接(已订阅) */
    if (strstr(g_esp8266.rx_buffer, "+MQTTCONN:0,3") != NULL ||
        strstr(g_esp8266.rx_buffer, "+MQTTCONN:0,4") != NULL)
    {
        g_esp8266.mqtt_connected = 1;
        return 1;
    }
    g_esp8266.mqtt_connected = 0;
    return 0;
}

/**
  * @brief  ESP8266初始化
  */
ESP8266_Status_t ESP8266_Init(void)
{
    ESP8266_Status_t status;
    extern UART_HandleTypeDef huart1;

    g_esp8266.wifi_status = WIFI_DISCONNECTED;
    g_esp8266.mqtt_connected = 0;
    ESP8266_ClearRxBuffer();

    /* 等待模块启动 */
    sprintf(esp8266_debug_buf, "[ESP8266] Waiting for startup...\r\n");
    HAL_UART_Transmit(&huart1, (uint8_t*)esp8266_debug_buf, strlen(esp8266_debug_buf), 100);
    HAL_Delay(2000);

    /* 测试AT，最多尝试3次（每次1秒间隔，兼容启动慢的模块） */
    {
        uint8_t at_try;
        status = ESP8266_TIMEOUT;
        for (at_try = 0; at_try < 3 && status != ESP8266_OK; at_try++)
        {
            status = ESP8266_Test();
            if (status != ESP8266_OK) HAL_Delay(1000);
        }
    }
    if (status != ESP8266_OK)
    {
        /* 三次AT失败，尝试硬复位 */
        ESP8266_Reset();
        HAL_Delay(3000);
        status = ESP8266_Test();
    }

    if (status == ESP8266_OK)
    {
        /* 关闭回显 */
        ESP8266_SendCmd("ATE0", "OK", 1000);

        sprintf(esp8266_debug_buf, "[ESP8266] Init OK\r\n");
        HAL_UART_Transmit(&huart1, (uint8_t*)esp8266_debug_buf, strlen(esp8266_debug_buf), 100);
    }
    else
    {
        sprintf(esp8266_debug_buf, "[ESP8266] Init FAILED - Check wiring & firmware v2.x\r\n");
        HAL_UART_Transmit(&huart1, (uint8_t*)esp8266_debug_buf, strlen(esp8266_debug_buf), 100);
    }

    return status;
}

/**
  * @brief  获取最近一次AT响应原文
  */
const char* ESP8266_GetLastResponse(void)
{
    return g_esp8266.rx_buffer;
}

/**
  * @brief  UART接收回调
  */
void ESP8266_RxCallback(uint8_t data)
{
    if (g_esp8266.rx_index < ESP8266_RX_BUF_SIZE - 1)
    {
        g_esp8266.rx_buffer[g_esp8266.rx_index++] = data;
        g_esp8266.rx_buffer[g_esp8266.rx_index] = '\0';

        if (g_esp8266.rx_index >= 2)
        {
            if (g_esp8266.rx_buffer[g_esp8266.rx_index - 2] == '\r' &&
                g_esp8266.rx_buffer[g_esp8266.rx_index - 1] == '\n')
            {
                g_esp8266.rx_complete = 1;
            }
        }
    }
    else
    {
        ESP8266_ClearRxBuffer();
    }
}
