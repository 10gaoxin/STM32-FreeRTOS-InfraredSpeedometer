/**
  * @brief  UART接收回调函数（增强调试版本）
  * @param  data: 接收到的字节
  */
void ESP8266_RxCallback(uint8_t data)
{
    extern UART_HandleTypeDef huart1;
    static uint8_t first_byte_received = 0;

    /* 第一次收到数据时输出提示 */
    if (!first_byte_received)
    {
        char debug_buf[50];
        sprintf(debug_buf, "[DEBUG] First byte received: 0x%02X\r\n", data);
        HAL_UART_Transmit(&huart1, (uint8_t*)debug_buf, strlen(debug_buf), 100);
        first_byte_received = 1;
    }

    if (g_esp8266.rx_index < ESP8266_RX_BUF_SIZE - 1)
    {
        g_esp8266.rx_buffer[g_esp8266.rx_index++] = data;
        g_esp8266.rx_buffer[g_esp8266.rx_index] = '\0';

        /* 检查是否接收完成 (以\r\n结尾) */
        if (g_esp8266.rx_index >= 2)
        {
            if (g_esp8266.rx_buffer[g_esp8266.rx_index - 2] == '\r' &&
                g_esp8266.rx_buffer[g_esp8266.rx_index - 1] == '\n')
            {
                g_esp8266.rx_complete = 1;

                /* 输出接收到的完整数据 */
                char debug_buf[100];
                sprintf(debug_buf, "[DEBUG] RX complete: %s", g_esp8266.rx_buffer);
                HAL_UART_Transmit(&huart1, (uint8_t*)debug_buf, strlen(debug_buf), 100);
            }
        }
    }
    else
    {
        /* 缓冲区溢出，清空 */
        ESP8266_ClearRxBuffer();
    }
}
