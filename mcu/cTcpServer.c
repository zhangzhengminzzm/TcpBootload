/**
 * @file cTcpServer.c
 * @brief W5500 Modbus TCP 服务实现文件。
 * @details 本文件负责初始化 W5500、创建 TCP Server、接收 Modbus TCP 帧，
 *          并把收到的数据交给 Modbus/Bootload 协议层处理。
 */

#include "cTcpServer.h"
#include "w5500Port.h"
#include "w5500.h"
#include "socket.h"
#include "wizchip_conf.h"
#include "modbusTcp.h"

/* 默认屏蔽 MCU 侧日志输出，避免占用串口或半主机资源。 */
#define MCU_LOG(...) ((void)0)

/*============================================================================
 * 宏定义
 *============================================================================*/
#define TCP_SOCKET 0     ///< 使用的 W5500 Socket 编号，范围 0~7。
#define TCP_PORT 502     ///< Modbus TCP 标准端口。
#define BUFFER_SIZE 2048 ///< TCP 收发缓冲区大小，单位为字节。
#define SOCKET_COUNT 8   ///< W5500 支持的最大 Socket 数量。
#define SOCKET_TX_SIZE 2 ///< 每个 Socket 分配的发送缓冲区大小，单位为 KB。
#define SOCKET_RX_SIZE 2 ///< 每个 Socket 分配的接收缓冲区大小，单位为 KB。

/*============================================================================
 * 静态变量
 *============================================================================*/
static uint8_t tcp_rx_buf[BUFFER_SIZE]; ///< TCP 数据接收缓冲区。
static uint8_t tcp_tx_buf[BUFFER_SIZE]; ///< TCP 数据发送缓冲区。

/**
 * @brief W5500 静态网络配置。
 * @note 包含 MAC 地址、IP 地址、子网掩码、网关、DNS 和 DHCP 模式。
 */
static wiz_NetInfo netinfo =
    {
        .mac = {0x00, 0x08, 0xdc, 0x11, 0x22, 0x33},
        .ip = {192, 168, 1, 100},
        .sn = {255, 255, 255, 0},
        .gw = {192, 168, 1, 1},
        .dns = {8, 8, 8, 8},
        .dhcp = NETINFO_STATIC};

/*============================================================================
 * 静态函数声明
 *============================================================================*/
static void netInit(void);
static int8_t TCP_Server_Init(void);
static void handleEstablishedConnection(void);
static void handleDataTransmission(void);
static void handleCloseWait(void);
static int8_t handleInitState(void);
static void handleClosedState(void);
static void printNetInfo(void);

/*============================================================================
 * 公共函数实现
 *============================================================================*/

/**
 * @brief 初始化 W5500 网络并启动 Modbus TCP 监听。
 * @details 依次完成 W5500 底层初始化、Modbus 寄存器初始化、TCP Socket 创建和监听。
 */
void initTcpServer(void)
{
    /* 步骤1：初始化 W5500 硬件、SPI 回调和静态网络参数。 */
    netInit();

    /* 步骤2：创建 TCP Socket 并进入监听状态，失败时停机等待调试。 */
    if (TCP_Server_Init() != SOCK_OK)
    {
        MCU_LOG("init Tcp Server Error");
        while (1)
            ;
    }

    /* 步骤3：读取芯片内实际生效的网络参数，便于串口调试确认。 */
    wiz_NetInfo read_info;
    wizchip_getnetinfo(&read_info);
    MCU_LOG("Real IP in W5500: %d.%d.%d.%d\r\n",
            read_info.ip[0], read_info.ip[1], read_info.ip[2], read_info.ip[3]);
}

/**
 * @brief TCP 服务主任务。
 * @details 需要在主循环中周期调用，用于处理 Bootload 周期逻辑、Socket 状态和 Modbus TCP 数据。
 */
void tcpServerTask(void)
{
    uint8_t sock_status;
    static uint8_t last_status = 0xFF;

    /* 步骤1：先执行 Bootload 周期任务，处理延时复位或 APP 跳转。 */
    Bootload_Task();

    /* 步骤2：读取当前 Socket 状态，用于后续状态机分发。 */
    sock_status = getSn_SR(TCP_SOCKET);
    // MCU_LOG("sock_status is %x\r\n", sock_status);

    /* 步骤3：状态变化时打印一次日志，减少重复刷屏并便于定位连接过程。 */
    if (sock_status != last_status)
    {
        MCU_LOG("Socket Status Change: 0x%02X\r\n", sock_status);
        last_status = sock_status;
    }

    /* 步骤4：根据 W5500 Socket 状态执行对应处理函数。 */
    switch (sock_status)
    {
    case SOCK_ESTABLISHED: ///< TCP 连接已建立。
        handleEstablishedConnection();
        break;

    case SOCK_CLOSE_WAIT: ///< 客户端请求关闭连接。
        handleCloseWait();
        break;

    case SOCK_INIT: ///< Socket 已初始化，等待进入监听状态。
        handleInitState();
        break;

    case SOCK_CLOSED: ///< Socket 已关闭，需要重新创建。
        handleClosedState();
        break;

    case SOCK_LISTEN: ///< 正在监听，等待客户端连接。
        break;

    default: ///< 其他状态例如 FIN_WAIT、CLOSING 等暂不处理，下一轮继续轮询。
        break;
    }
}

/*============================================================================
 * 静态函数实现
 *============================================================================*/

/**
 * @brief 配置 W5500 底层回调、Socket 缓冲区和网络参数。
 * @details 本函数还会初始化 Modbus 寄存器和 Bootload 状态机。
 */
static void netInit(void)
{
    uint8_t txsize[SOCKET_COUNT] = {0}; ///< 各 Socket 发送缓冲区大小配置。
    uint8_t rxsize[SOCKET_COUNT] = {0}; ///< 各 Socket 接收缓冲区大小配置。

    /* 步骤1：为每个 Socket 分配相同大小的发送和接收缓冲区。 */
    for (int i = 0; i < SOCKET_COUNT; i++)
    {
        txsize[i] = SOCKET_TX_SIZE;
        rxsize[i] = SOCKET_RX_SIZE;
    }

    /* 步骤2：初始化 W5500 相关 GPIO、复位脚、片选脚和 SPI 外设。 */
    W5500_Port_Init();

    /* 步骤3：注册 W5500 片选控制和 SPI 单字节读写回调。 */
    reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
    reg_wizchip_spi_cbfunc(wizchipSpiReadbyte, wizchipSpiWritebyte);

    /* 步骤4：初始化 W5500 芯片，并写入 Socket 缓冲区分配表。 */
    if (wizchip_init(txsize, rxsize) != 0)
    {
        MCU_LOG("w5500 init erro\r\n");
        return;
    }

    /* 步骤5：写入静态网络参数，包括 IP、MAC、网关、子网掩码和 DNS。 */
    wizchip_setnetinfo(&netinfo);

    /* 步骤6：打印网络配置，并初始化 Modbus/Bootload 协议层状态。 */
    printNetInfo();
    Modbus_App_Init();
}

/**
 * @brief 创建 W5500 TCP Socket 并启动监听。
 * @return 返回 SOCK_OK 表示成功，其他值表示 W5500 Socket 层错误码。
 */
static int8_t TCP_Server_Init(void)
{
    int8_t ret;

    /* 步骤1：按指定 Socket 编号、TCP 模式和端口号创建 Socket。 */
    ret = socket(TCP_SOCKET, Sn_MR_TCP, TCP_PORT, 0);

    if (ret != TCP_SOCKET)
    {
        MCU_LOG("Socket create Error ret is %d\r\n", ret);
        return ret;
    }
    MCU_LOG("Socket created successfully\r\n");

    /* 步骤2：Socket 创建成功后进入 listen 状态，等待上位机连接。 */
    ret = listen(TCP_SOCKET);

    if (ret != SOCK_OK)
    {
        MCU_LOG("Listen Error");
        close(TCP_SOCKET);
        return ret;
    }
    MCU_LOG("TCP Server listening on port %d\r\n", TCP_PORT);
    return SOCK_OK;
}

/**
 * @brief 处理已建立的 TCP 连接。
 * @details 清除连接中断标志后，转入数据接收、Modbus 解析和响应发送流程。
 */
static void handleEstablishedConnection(void)
{
    uint8_t irq_flags; ///< W5500 Socket 中断标志。

    /* 步骤1：读取并清除连接建立中断标志，防止同一事件重复处理。 */
    irq_flags = getSn_IR(TCP_SOCKET);
    if (irq_flags & Sn_IR_CON)
    {
        setSn_IR(TCP_SOCKET, Sn_IR_CON);
    }

    /* 步骤2：处理当前连接上的数据收发。 */
    handleDataTransmission();
}

/**
 * @brief 接收 TCP 数据、执行 Modbus TCP 处理并发送响应。
 * @details 收到完整数据后调用 Modbus_TCP_Process，若返回非 0 长度则通过 W5500 发回响应。
 */
static void handleDataTransmission(void)
{
    int32_t len;

    /* 步骤1：读取 W5500 当前 Socket 接收缓冲区中等待处理的数据长度。 */
    len = getSn_RX_RSR(TCP_SOCKET);

    if (len > 0)
    {
        /* 步骤2：限制单次读取长度，避免超过本地缓冲区。 */
        if (len > BUFFER_SIZE)
            len = BUFFER_SIZE;

        /* 步骤3：从 W5500 读取原始 TCP 数据到接收缓冲区。 */
        len = recv(TCP_SOCKET, tcp_rx_buf, len);

        if (len > 0)
        {
            /* 步骤4：调用 Modbus TCP 协议处理函数生成响应帧。 */
            uint16_t resp_len = Modbus_TCP_Process(tcp_rx_buf, (uint16_t)len, tcp_tx_buf);

            /* 步骤5：有合法响应时，通过同一个 Socket 发回上位机。 */
            if (resp_len > 0)
            {
                send(TCP_SOCKET, tcp_tx_buf, resp_len);
            }
        }
    }
}

/**
 * @brief 处理 SOCK_CLOSE_WAIT 状态。
 * @details 当上位机主动关闭连接时，MCU 侧调用 disconnect 释放当前连接。
 */
static void handleCloseWait(void)
{
    /* 步骤1：主动断开连接，让 Socket 回到 CLOSED/INIT 状态。 */
    disconnect(TCP_SOCKET);
}

/**
 * @brief 处理 SOCK_INIT 状态。
 * @return 返回 SOCK_OK 表示重新监听成功，其他值表示监听失败。
 */
static int8_t handleInitState(void)
{
    int8_t ret;

    /* 步骤1：Socket 已经初始化时，重新进入监听状态。 */
    ret = listen(TCP_SOCKET);

    if (ret != SOCK_OK)
    {
        /* 步骤2：监听失败时关闭 Socket，后续 CLOSED 分支会再次创建。 */
        close(TCP_SOCKET);
        return ret;
    }

    return SOCK_OK;
}

/**
 * @brief 处理 SOCK_CLOSED 状态。
 * @details Socket 被关闭后重新创建，并尽量立即进入监听状态。
 */
static void handleClosedState(void)
{
    int8_t ret;

    /* 步骤1：重新创建 TCP Socket。 */
    ret = socket(TCP_SOCKET, Sn_MR_TCP, TCP_PORT, 0);

    if (ret == TCP_SOCKET)
    {
        /* 步骤2：创建成功后重新监听；若失败则下一轮任务继续尝试。 */
        listen(TCP_SOCKET);
    }
}

/**
 * @brief 打印 W5500 当前网络配置。
 * @details 主要用于串口调试，确认芯片内实际生效的 MAC、IP、网关、子网掩码和 DNS。
 */
static void printNetInfo(void)
{
    wiz_NetInfo info;
    wizchip_getnetinfo(&info);

    MCU_LOG("MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
            info.mac[0], info.mac[1], info.mac[2],
            info.mac[3], info.mac[4], info.mac[5]);
    MCU_LOG("IP: %d.%d.%d.%d\r\n",
            info.ip[0], info.ip[1], info.ip[2], info.ip[3]);
    MCU_LOG("Gateway: %d.%d.%d.%d\r\n",
            info.gw[0], info.gw[1], info.gw[2], info.gw[3]);
    MCU_LOG("Subnet Mask: %d.%d.%d.%d\r\n",
            info.sn[0], info.sn[1], info.sn[2], info.sn[3]);
    MCU_LOG("DNS: %d.%d.%d.%d\r\n",
            info.dns[0], info.dns[1], info.dns[2], info.dns[3]);
}
