/**
  ******************************************************************************
  * @file    speed.c
  * @brief   红外测速模块实现
  ******************************************************************************
  */

#include "speed.h"
#include "tim.h"

/* 全局测速数据 */
SpeedData_t g_speed_data;

/**
  * @brief  中值滤波辅助：对长度为 SPEED_MEDIAN_N 的浮点数组原地排序（插入排序），返回中间值
  * @note   仅供 Speed_Calculate 内部使用，N 通常为 3，开销极小
  */
static float Speed_Median(float *buf, uint8_t n)
{
    uint8_t i, j;
    float key;
    for (i = 1; i < n; i++)
    {
        key = buf[i];
        j = i;
        while (j > 0 && buf[j - 1] > key)
        {
            buf[j] = buf[j - 1];
            j--;
        }
        buf[j] = key;
    }
    return buf[n / 2];
}

/**
  * @brief  测速模块初始化
  */
void Speed_Init(void)
{
    uint8_t i;
    g_speed_data.state = SPEED_IDLE;
    g_speed_data.time_a = 0;
    g_speed_data.time_b = 0;
    g_speed_data.time_diff = 0;
    g_speed_data.speed_ms = 0.0f;
    g_speed_data.speed_kmh = 0.0f;
    g_speed_data.new_data = 0;
    g_speed_data.overflow_count = 0;
    g_speed_data.median_count = 0;
    for (i = 0; i < SPEED_MEDIAN_N; i++)
        g_speed_data.median_buf[i] = 0.0f;

    /* 启动定时器 */
    HAL_TIM_Base_Start_IT(&htim2);
}

/**
  * @brief  传感器A触发处理 (物体进入)
  */
void Speed_SensorA_Trigger(void)
{
    if (g_speed_data.state == SPEED_IDLE)
    {
        /* 记录传感器A触发时间 */
        g_speed_data.time_a = __HAL_TIM_GET_COUNTER(&htim2);
        g_speed_data.overflow_count = 0;
        g_speed_data.state = SPEED_WAIT_SENSOR_B;

        /* LED指示 - 开始测速 */
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    }
}

/**
  * @brief  传感器B触发处理 (物体进入B)
  * @note   仅记录时间戳，不在ISR中做浮点运算。
  *         速度计算由主循环调用 Speed_Calculate() 完成。
  */
void Speed_SensorB_Trigger(void)
{
    if (g_speed_data.state == SPEED_WAIT_SENSOR_B)
    {
        /* 仅记录时间戳，设置完成标志 */
        g_speed_data.time_b = __HAL_TIM_GET_COUNTER(&htim2);
        g_speed_data.state = SPEED_COMPLETED;
        /* LED指示 - 待计算 */
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    }
}

/**
  * @brief  定时器溢出处理
  */
void Speed_TimerOverflow(void)
{
    if (g_speed_data.state == SPEED_WAIT_SENSOR_B)
    {
        g_speed_data.overflow_count++;

        /* 超时保护：如果超过3秒没有触发传感器B，重置状态 */
        /* 3秒 = 3000000微秒，每次溢出65536微秒 */
        /* 3000000 / 65536 ≈ 46 次 */
        if (g_speed_data.overflow_count > 46)
        {
            Speed_Reset();
        }
    }
}

/**
  * @brief  计算速度
  */
void Speed_Calculate(void)
{
    uint32_t total_time;

    /* 计算总时间（考虑定时器溢出）
     * 正确公式：total = overflow_count × 65536 + (time_b - time_a)
     * time_b < time_a 时用减法，等价于 overflow_count×65536 - (time_a - time_b)
     */
    total_time = (uint32_t)g_speed_data.overflow_count * 65536UL;
    if (g_speed_data.time_b >= g_speed_data.time_a)
    {
        total_time += g_speed_data.time_b - g_speed_data.time_a;
    }
    else
    {
        total_time -= g_speed_data.time_a - g_speed_data.time_b;
    }

    /* 有效性校验：过滤噪声触发（时间差过小则丢弃） */
    if (total_time < SPEED_MIN_TIME_US)
    {
        g_speed_data.state = SPEED_IDLE;
        return;
    }

    g_speed_data.time_diff = total_time;

    /* 计算速度 */
    /* 速度 = 距离(mm) / 时间(us) = 距离(mm) * 1000 / 时间(us) (mm/s) */
    /* 转换为 m/s: 除以 1000 */
    /* 最终: 速度(m/s) = 距离(mm) / 时间(us) * 1000 / 1000 = 距离(mm) / 时间(us) */

    if (total_time > 0)
    {
        float raw_ms;
        float sorted_buf[SPEED_MEDIAN_N];
        uint8_t i;

        /* 本次原始速度 */
        raw_ms = (float)SENSOR_DISTANCE_MM / (float)total_time * 1000.0f;

        /* 存入中值滤波缓冲区 */
        g_speed_data.median_buf[g_speed_data.median_count] = raw_ms;
        g_speed_data.median_count++;

        if (g_speed_data.median_count >= SPEED_MEDIAN_N)
        {
            /* 复制缓冲区（排序会修改数组，不能直接操作 median_buf） */
            for (i = 0; i < SPEED_MEDIAN_N; i++)
                sorted_buf[i] = g_speed_data.median_buf[i];

            /* 取中值作为本次输出速度 */
            g_speed_data.speed_ms  = Speed_Median(sorted_buf, SPEED_MEDIAN_N);
            g_speed_data.speed_kmh = g_speed_data.speed_ms * 3.6f;
            g_speed_data.new_data  = 1;

            /* 缓冲区归零，开始下一轮 */
            g_speed_data.median_count = 0;
        }
        /* 缓冲未满时不输出，等待后续测量 */
    }

    /* 重置状态，准备下次测量 */
    g_speed_data.state = SPEED_IDLE;
}

/**
  * @brief  重置测速状态
  * @note   超时/异常时调用。median_count 同步归零：超时意味着本轮测量无效，
  *         不能让脏槽位污染下一组中值滤波缓冲。
  */
void Speed_Reset(void)
{
    g_speed_data.state = SPEED_IDLE;
    g_speed_data.overflow_count = 0;
    g_speed_data.median_count = 0;   /* 丢弃未完成的缓冲轮次 */
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
}

/**
  * @brief  获取速度 (米/秒)
  */
float Speed_GetMS(void)
{
    return g_speed_data.speed_ms;
}

/**
  * @brief  获取速度 (公里/小时)
  */
float Speed_GetKMH(void)
{
    return g_speed_data.speed_kmh;
}

/**
  * @brief  检查是否有新数据
  */
uint8_t Speed_IsNewData(void)
{
    return g_speed_data.new_data;
}

/**
  * @brief  清除新数据标志
  */
void Speed_ClearNewData(void)
{
    g_speed_data.new_data = 0;
}
