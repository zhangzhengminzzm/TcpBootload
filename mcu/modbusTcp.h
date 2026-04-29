/// @brief Modbus TCP 任务接口
#ifndef __MODBUS_TCP_H__
#define __MODBUS_TCP_H__

#include <stdint.h>
#include "cTcpServer.h"
#include "bootload_mcu.h"

// 寄存器地址定义
#define REG_HOLDING_START 0
#define REG_HOLDING_NREGS 0x300

#define REG_INPUT_START 0
#define REG_INPUT_NREGS 100

#define REG_COIL_START 0
#define REG_COIL_NREGS 10

// 函数声明
/**
 * @brief 初始化 Modbus 寄存器镜像区和 Bootload 协议状态。
 */
void Modbus_App_Init(void);

/**
 * @brief 解析一帧 Modbus TCP 请求并生成响应帧。
 * @param req 输入的 Modbus TCP 帧，包含 MBAP 头。
 * @param req_len req 中的有效字节数。
 * @param resp 响应帧输出缓冲区。
 * @return 响应帧长度，单位为字节；返回 0 表示无需发送响应。
 */
uint16_t Modbus_TCP_Process(uint8_t *req, uint16_t req_len, uint8_t *resp);

#endif
