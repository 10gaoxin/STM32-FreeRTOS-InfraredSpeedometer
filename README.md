# STM32 FreeRTOS 红外测速物联网系统

基于 STM32F103C8T6 + FreeRTOS 的多任务嵌入式测速系统，双红外对射传感器测量物体速度，OLED 实时显示，MQTT 上报 OneNET 云平台。

## 系统架构

```
ISR 层
├── EXTI0/1 → Speed_SensorA/B_Trigger()
│              + xQueueSendFromISR(speedEventQueue)
└── TIM2    → Speed_TimerOverflow()

Task_Speed (优先级: High)
├── 等待 speedEventQueue
├── Speed_Calculate()
└── 发送 SpeedResult_t → display_queue + cloud_queue

Task_Display (优先级: AboveNormal)
├── 等待 display_queue
├── OLED 显示速度
└── 2 秒后恢复待机界面

Task_Cloud (优先级: Normal)
├── 等待 cloud_queue
└── MQTT Publish → OneNET

Task_Heartbeat (优先级: Low)
└── 每 15 秒 MQTT 心跳保活

defaultTask (优先级: Normal)
└── LED 500ms 翻转（系统存活指示）
```

## 硬件

| 模块 | 型号 | 接口 |
|------|------|------|
| MCU | STM32F103C8T6 | - |
| 红外传感器 x2 | E3F-20C1 | EXTI (PA0, PA1) |
| OLED 屏 | SSD1306 128x64 | I2C |
| WiFi 模组 | ESP8266-01S | UART |
| LED 指示灯 | - | PC13 |

## 技术要点

- **5 任务 + 3 队列 + 1 互斥量**的事件驱动架构
- EXTI 中断 → xQueueSendFromISR → Task，ISR 与业务逻辑解耦
- TIM2 溢出中断 + 16→32 位扩展，微秒级精度计时
- Mutex 保护 UART 共享资源，防止多任务打印交错
- MQTT 心跳保活 + 断线重连降级策略

## 开发环境

- Keil MDK V5 (ARMCC V5.06)
- STM32CubeMX
- FreeRTOS (CMSIS-RTOS V2)
- 云平台：中国移动 OneNET

## 目录结构

```
Core/
├── Inc/          — 头文件
│   ├── speed.h       — 测速模块
│   ├── oled.h        — OLED 驱动
│   └── esp8266.h     — WiFi/MQTT 模块
├── Src/          — 源文件
│   ├── freertos.c    — 所有任务定义（核心）
│   ├── main.c        — 初始化 + EXTI 回调
│   ├── speed.c       — 测速算法
│   ├── oled.c        — SSD1306 驱动
│   └── esp8266.c     — AT 指令封装
Drivers/          — HAL 库
Middlewares/      — FreeRTOS 内核
```

## 作者

高鑫 | 2026.07
