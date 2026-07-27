/**
  ******************************************************************************
  * @file    speed.h
  * @brief   红外测速模块头文件
  ******************************************************************************
  */

#ifndef __SPEED_H
#define __SPEED_H

#include "main.h"

/* 传感器间距配置 (单位: 毫米) */
/* 测人/测车时修改此值为实际安装距离，如 2000 表示 2 米 */
#define SENSOR_DISTANCE_MM    100    /* 默认 100mm = 10cm，用于小球测试 */

/* 速度有效性校验阈值 */
/* 10000us 对应 100mm/10ms = 10m/s = 36km/h，高于此速度的触发视为噪声丢弃 */
#define SPEED_MIN_TIME_US     10000

/* 中值滤波：N=1 时等同于直接输出（每次遮挡立即出结果）；改为3可开启滤波 */
#define SPEED_MEDIAN_N        1

/* 速度单位转换 */
#define SPEED_UNIT_MS         0      /* 米/秒 */
#define SPEED_UNIT_KMH        1      /* 公里/小时 */

/* 测速状态 */
typedef enum {
    SPEED_IDLE = 0,         /* 空闲，等待触发 */
    SPEED_WAIT_SENSOR_B,    /* 传感器A已触发，等待传感器B */
    SPEED_COMPLETED         /* 测速完成 */
} SpeedState_t;

/* 测速结果结构体 */
typedef struct {
    SpeedState_t state;         /* 当前状态 */
    uint32_t time_a;            /* 传感器A触发时间 (微秒) */
    uint32_t time_b;            /* 传感器B触发时间 (微秒) */
    uint32_t time_diff;         /* 时间差 (微秒) */
    float speed_ms;             /* 速度 (米/秒) */
    float speed_kmh;            /* 速度 (公里/小时) */
    uint8_t new_data;           /* 新数据标志 */
    uint32_t overflow_count;    /* 定时器溢出次数 */
    /* 中值滤波缓冲区 */
    float median_buf[SPEED_MEDIAN_N]; /* 缓存最近 N 次原始速度 (m/s) */
    uint8_t median_count;             /* 已缓存次数，满 N 后归零并输出中值 */
} SpeedData_t;

/* 外部变量声明 */
extern SpeedData_t g_speed_data;

/* 函数声明 */
void Speed_Init(void);
void Speed_SensorA_Trigger(void);
void Speed_SensorB_Trigger(void);
void Speed_TimerOverflow(void);
void Speed_Calculate(void);
void Speed_Reset(void);
float Speed_GetMS(void);
float Speed_GetKMH(void);
uint8_t Speed_IsNewData(void);
void Speed_ClearNewData(void);

#endif /* __SPEED_H */
