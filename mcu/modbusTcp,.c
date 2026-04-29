#include "modbusTcp.h"
#include <string.h>

uint16_t g_holding_regs[REG_HOLDING_NREGS];
uint8_t g_coils[16];
uint8_t g_discrete_inputs[16];
uint16_t g_input_regs[REG_INPUT_NREGS];

/**
 * @brief 构造 Modbus TCP 异常响应。
 * @param resp TCP 服务层提供的响应缓冲区。
 * @param function_code 原始 Modbus 功能码。
 * @param exception_code 标准 Modbus 异常码。
 * @return 响应帧长度，单位为字节。
 */
static uint16_t modbus_exception(uint8_t *resp, uint8_t function_code, uint8_t exception_code);

/**
 * @brief 检查保持寄存器请求范围是否位于本地镜像数组内。
 * @param start_addr 请求访问的第一个保持寄存器地址。
 * @param quantity 请求访问的保持寄存器数量。
 * @return 返回 1 表示范围合法，返回 0 表示范围非法。
 */
static uint8_t holding_range_valid(uint16_t start_addr, uint16_t quantity);

/**
 * @brief 检查输入寄存器请求范围是否位于本地镜像数组内。
 * @param start_addr 请求访问的第一个输入寄存器地址。
 * @param quantity 请求访问的输入寄存器数量。
 * @return 返回 1 表示范围合法，返回 0 表示范围非法。
 */
static uint8_t input_range_valid(uint16_t start_addr, uint16_t quantity);

/**
 * @brief 初始化全部 Modbus 寄存器镜像区和 Bootload 状态机。
 * @details 由 W5500 TCP 服务初始化流程调用一次。
 */
void Modbus_App_Init(void)
{
    memset(g_holding_regs, 0, sizeof(g_holding_regs));
    memset(g_coils, 0, sizeof(g_coils));
    memset(g_discrete_inputs, 0, sizeof(g_discrete_inputs));
    memset(g_input_regs, 0, sizeof(g_input_regs));

    Bootload_Init(g_holding_regs, REG_HOLDING_NREGS);
}

/**
 * @brief 解析一帧完整的 Modbus TCP 请求，并生成对应响应。
 * @param req 原始 Modbus TCP 请求缓冲区，包含 MBAP 头。
 * @param req_len TCP 实际接收到的字节数。
 * @param resp 响应输出缓冲区，包含 MBAP 头。
 * @return 响应帧长度，单位为字节；返回 0 表示本次请求应被忽略。
 * @details 当前支持 0x03、0x04、0x06、0x10 功能码。保持寄存器写入完成后会调用
 *          Bootload_OnHoldingWrite，使写入 BOOT_REG_COMMAND 能触发升级命令。
 */
uint16_t Modbus_TCP_Process(uint8_t *req, uint16_t req_len, uint8_t *resp)
{
    uint16_t transaction_id;
    uint16_t protocol_id;
    uint8_t unit_id;
    uint8_t function_code;
    uint16_t start_addr;
    uint16_t quantity;
    uint16_t pdu_len = 0U;
    uint16_t i;

    /* 步骤1：检查输入输出缓冲区和最小 MBAP/PDU 长度，异常帧直接忽略。 */
    if ((req == 0) || (resp == 0) || (req_len < 8U))
    {
        return 0U;
    }

    /* 步骤2：解析 MBAP 头和功能码，Modbus TCP 的 protocol_id 必须为 0。 */
    transaction_id = ((uint16_t)req[0] << 8) | req[1];
    protocol_id = ((uint16_t)req[2] << 8) | req[3];
    unit_id = req[6];
    function_code = req[7];

    (void)transaction_id;

    if (protocol_id != 0U)
    {
        return 0U;
    }

    /* 步骤3：先复制响应 MBAP 中的事务号、协议号和单元 ID。 */
    resp[0] = req[0];
    resp[1] = req[1];
    resp[2] = 0U;
    resp[3] = 0U;
    resp[6] = unit_id;

    if (req_len < 12U)
    {
        return modbus_exception(resp, function_code, 0x03U);
    }

    /* 步骤4：读取功能码通用的起始地址和寄存器数量/写入值字段。 */
    start_addr = ((uint16_t)req[8] << 8) | req[9];
    quantity = ((uint16_t)req[10] << 8) | req[11];

    /* 步骤5：按功能码分别处理读保持寄存器、读输入寄存器、写单个/多个保持寄存器。 */
    switch (function_code)
    {
    case 0x03:
        /* 步骤5.1：读取保持寄存器，要求数量 1~125 且访问范围合法。 */
        if ((quantity == 0U) || (quantity > 125U) || !holding_range_valid(start_addr, quantity))
        {
            return modbus_exception(resp, function_code, 0x02U);
        }

        resp[7] = function_code;
        resp[8] = (uint8_t)(quantity * 2U);
        for (i = 0U; i < quantity; i++)
        {
            uint16_t value = g_holding_regs[start_addr + i];
            resp[9U + (i * 2U)] = (uint8_t)(value >> 8);
            resp[10U + (i * 2U)] = (uint8_t)(value & 0xFFU);
        }
        pdu_len = (uint16_t)(2U + (quantity * 2U));
        break;

    case 0x04:
        /* 步骤5.2：读取输入寄存器，要求数量 1~125 且访问范围合法。 */
        if ((quantity == 0U) || (quantity > 125U) || !input_range_valid(start_addr, quantity))
        {
            return modbus_exception(resp, function_code, 0x02U);
        }

        resp[7] = function_code;
        resp[8] = (uint8_t)(quantity * 2U);
        for (i = 0U; i < quantity; i++)
        {
            uint16_t value = g_input_regs[start_addr + i];
            resp[9U + (i * 2U)] = (uint8_t)(value >> 8);
            resp[10U + (i * 2U)] = (uint8_t)(value & 0xFFU);
        }
        pdu_len = (uint16_t)(2U + (quantity * 2U));
        break;

    case 0x06:
        /* 步骤5.3：写单个保持寄存器，写入后立即通知 Bootload 检查命令寄存器。 */
        if (!holding_range_valid(start_addr, 1U))
        {
            return modbus_exception(resp, function_code, 0x02U);
        }

        g_holding_regs[start_addr] = quantity;
        Bootload_OnHoldingWrite(start_addr, 1U);

        resp[7] = function_code;
        resp[8] = req[8];
        resp[9] = req[9];
        resp[10] = req[10];
        resp[11] = req[11];
        pdu_len = 5U;
        break;

    case 0x10:
    {
        uint8_t byte_count;

        /* 步骤5.4：写多个保持寄存器，先检查长度字段，再批量写入寄存器镜像。 */
        if (req_len < 13U)
        {
            return modbus_exception(resp, function_code, 0x03U);
        }

        byte_count = req[12];
        if ((quantity == 0U) || (quantity > 123U) || (byte_count != (uint8_t)(quantity * 2U)))
        {
            return modbus_exception(resp, function_code, 0x03U);
        }

        if (req_len < (uint16_t)(13U + byte_count))
        {
            return 0U;
        }

        if (!holding_range_valid(start_addr, quantity))
        {
            return modbus_exception(resp, function_code, 0x02U);
        }

        for (i = 0U; i < quantity; i++)
        {
            uint16_t value = ((uint16_t)req[13U + (i * 2U)] << 8) | req[14U + (i * 2U)];
            g_holding_regs[start_addr + i] = value;
        }
        Bootload_OnHoldingWrite(start_addr, quantity);

        resp[7] = function_code;
        resp[8] = req[8];
        resp[9] = req[9];
        resp[10] = req[10];
        resp[11] = req[11];
        pdu_len = 5U;
        break;
    }

    default:
        /* 步骤5.5：不支持的功能码返回非法功能异常。 */
        return modbus_exception(resp, function_code, 0x01U);
    }

    /* 步骤6：回填 MBAP 长度字段，并返回最终响应帧长度。 */
    resp[4] = (uint8_t)(((uint16_t)(1U + pdu_len)) >> 8);
    resp[5] = (uint8_t)((uint16_t)(1U + pdu_len) & 0xFFU);
    return (uint16_t)(7U + pdu_len);
}


/**
 * @brief 在原响应缓冲区中构造精简 Modbus TCP 异常响应。
 * @param resp 已经填好 MBAP 事务号和单元 ID 的响应缓冲区。
 * @param function_code 原始功能码。
 * @param exception_code 异常码，例如 0x01、0x02 或 0x03。
 * @return 异常响应长度，单位为字节。
 */
static uint16_t modbus_exception(uint8_t *resp, uint8_t function_code, uint8_t exception_code)
{
    resp[7] = (uint8_t)(function_code | 0x80U);
    resp[8] = exception_code;
    resp[4] = 0U;
    resp[5] = 3U;
    return 9U;
}

/**
 * @brief 根据 REG_HOLDING_* 宏检查保持寄存器起始地址和数量。
 * @param start_addr 第一个保持寄存器地址。
 * @param quantity 保持寄存器数量。
 * @return 返回 1 表示范围合法，返回 0 表示范围非法。
 */
static uint8_t holding_range_valid(uint16_t start_addr, uint16_t quantity)
{
    if (start_addr < REG_HOLDING_START)
    {
        return 0U;
    }
    if ((uint32_t)start_addr + quantity > (uint32_t)REG_HOLDING_START + REG_HOLDING_NREGS)
    {
        return 0U;
    }
    return 1U;
}

/**
 * @brief 根据 REG_INPUT_* 宏检查输入寄存器起始地址和数量。
 * @param start_addr 第一个输入寄存器地址。
 * @param quantity 输入寄存器数量。
 * @return 返回 1 表示范围合法，返回 0 表示范围非法。
 */
static uint8_t input_range_valid(uint16_t start_addr, uint16_t quantity)
{
    if (start_addr < REG_INPUT_START)
    {
        return 0U;
    }
    if ((uint32_t)start_addr + quantity > (uint32_t)REG_INPUT_START + REG_INPUT_NREGS)
    {
        return 0U;
    }
    return 1U;
}
