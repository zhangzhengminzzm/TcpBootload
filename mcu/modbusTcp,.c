#include "modbusTcp.h"
#include <string.h>

uint16_t g_holding_regs[REG_HOLDING_NREGS];
uint8_t g_coils[16];
uint8_t g_discrete_inputs[16];
uint16_t g_input_regs[REG_INPUT_NREGS];

/**
 * @brief Build a Modbus TCP exception response.
 * @param resp Response buffer supplied by the TCP server.
 * @param function_code Original Modbus function code.
 * @param exception_code Standard Modbus exception code.
 * @return Response length in bytes.
 */
static uint16_t modbus_exception(uint8_t *resp, uint8_t function_code, uint8_t exception_code);

/**
 * @brief Check whether a holding-register request is inside the local mirror.
 * @param start_addr First requested holding register.
 * @param quantity Number of requested registers.
 * @return 1 if the range is valid, otherwise 0.
 */
static uint8_t holding_range_valid(uint16_t start_addr, uint16_t quantity);

/**
 * @brief Check whether an input-register request is inside the local mirror.
 * @param start_addr First requested input register.
 * @param quantity Number of requested registers.
 * @return 1 if the range is valid, otherwise 0.
 */
static uint8_t input_range_valid(uint16_t start_addr, uint16_t quantity);

/**
 * @brief Initialize all Modbus register mirrors and Bootload state.
 * @details Called once from the W5500 TCP server initialization path.
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
 * @brief Parse one complete Modbus TCP request and build its response.
 * @param req Raw Modbus TCP request buffer, including MBAP header.
 * @param req_len Number of bytes received from TCP.
 * @param resp Output buffer for the response, including MBAP header.
 * @return Response length in bytes. Returns 0 when the request should be ignored.
 * @details Supported function codes are 0x03, 0x04, 0x06, and 0x10. Holding
 *          register writes are forwarded to Bootload_OnHoldingWrite so writing
 *          BOOT_REG_COMMAND can trigger upgrade commands.
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

    if ((req == 0) || (resp == 0) || (req_len < 8U))
    {
        return 0U;
    }

    transaction_id = ((uint16_t)req[0] << 8) | req[1];
    protocol_id = ((uint16_t)req[2] << 8) | req[3];
    unit_id = req[6];
    function_code = req[7];

    (void)transaction_id;

    if (protocol_id != 0U)
    {
        return 0U;
    }

    resp[0] = req[0];
    resp[1] = req[1];
    resp[2] = 0U;
    resp[3] = 0U;
    resp[6] = unit_id;

    if (req_len < 12U)
    {
        return modbus_exception(resp, function_code, 0x03U);
    }

    start_addr = ((uint16_t)req[8] << 8) | req[9];
    quantity = ((uint16_t)req[10] << 8) | req[11];

    switch (function_code)
    {
    case 0x03:
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
        return modbus_exception(resp, function_code, 0x01U);
    }

    resp[4] = (uint8_t)(((uint16_t)(1U + pdu_len)) >> 8);
    resp[5] = (uint8_t)((uint16_t)(1U + pdu_len) & 0xFFU);
    return (uint16_t)(7U + pdu_len);
}


/**
 * @brief Build a compact Modbus TCP exception response in-place.
 * @param resp Response buffer with MBAP transaction/unit fields already copied.
 * @param function_code Original function code.
 * @param exception_code Exception code such as 0x01, 0x02, or 0x03.
 * @return Response length in bytes.
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
 * @brief Validate holding-register start/count against REG_HOLDING_* macros.
 * @param start_addr First register address.
 * @param quantity Number of registers.
 * @return 1 if valid, otherwise 0.
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
 * @brief Validate input-register start/count against REG_INPUT_* macros.
 * @param start_addr First register address.
 * @param quantity Number of registers.
 * @return 1 if valid, otherwise 0.
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
