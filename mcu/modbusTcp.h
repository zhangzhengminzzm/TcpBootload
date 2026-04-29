/// @brief Modbus TCP任务
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
 * @brief Initialize Modbus register storage and Bootload protocol state.
 */
void Modbus_App_Init(void);
/**
 * @brief Parse one Modbus TCP request and generate a response frame.
 * @param req Incoming Modbus TCP frame including MBAP header.
 * @param req_len Number of bytes in req.
 * @param resp Output buffer for the response frame.
 * @return Response length in bytes, or 0 if no response should be sent.
 */
uint16_t Modbus_TCP_Process(uint8_t *req, uint16_t req_len, uint8_t *resp);
#endif
