/**
 * @file cTcpServer.c
 * @brief TCP服务器实现文件
 * @details 基于W5500实现的TCP服务器，监听端口5000，支持数据回显功能
 */

#include "cTcpServer.h" // 修正头文件拼写错误
#include "w5500Port.h"
#include "w5500.h" // 添加必要的头文件
#include "socket.h"
#include "wizchip_conf.h"
#include "stdio.h"
#include "modbusTcp.h"

/*============================================================================
 * 宏定义
 *============================================================================*/
#define TCP_SOCKET 0     ///< 使用的Socket编号，范围0-7
#define TCP_PORT 502     ///< Modbus TCP standard port
#define BUFFER_SIZE 2048 ///< 收发缓冲区大小
#define SOCKET_COUNT 8   ///< W5500支持的最大Socket数
#define SOCKET_TX_SIZE 2 ///< 每个Socket的发送缓存大小(2KB)
#define SOCKET_RX_SIZE 2 ///< 每个Socket的接收缓存大小(2KB)

/*============================================================================
 * 静态变量
 *============================================================================*/
static uint8_t tcp_rx_buf[BUFFER_SIZE]; ///< TCP数据接收缓冲区
static uint8_t tcp_tx_buf[BUFFER_SIZE]; ///< TCP数据发送缓冲区

/**
 * @brief 网络配置结构体
 * @note 包含MAC地址、IP地址、子网掩码、网关、DNS和DHCP模式
 */
static wiz_NetInfo netinfo =
    {
        .mac = {0x00, 0x08, 0xdc, 0x11, 0x22, 0x33},
        .ip = {192, 168, 1, 100}, ///< 改为 192.168.1.100
        .sn = {255, 255, 255, 0}, ///< 掩码用最通用的
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
 * @brief 初始化TCP服务器
 * @details 执行网络初始化和TCP服务器初始化
 * @note 在系统启动时调用一次
 */
/**
 * @brief Initialize W5500 networking and start the TCP listener.
 * @details Configures W5500 SPI callbacks, writes static network parameters,
 *          initializes Modbus register storage, creates the TCP socket, and
 *          enters listen state.
 */
void initTcpServer(void)
{
    netInit(); // 1. 初始化W5500硬件和网络参数

    if (TCP_Server_Init() != SOCK_OK) // 2. 初始化TCP服务器
    {
        // 这里可以添加错误处理，如LED指示或日志记录
        printf("init Tcp Server Error");
        while (1)
            ; // 初始化失败，系统暂停
    }
    wiz_NetInfo read_info;
    wizchip_getnetinfo(&read_info);
    printf("Real IP in W5500: %d.%d.%d.%d\r\n",
           read_info.ip[0], read_info.ip[1], read_info.ip[2], read_info.ip[3]);
}

/**
 * @brief TCP服务器主任务
 * @details 需要在主循环中周期调用，处理TCP连接和数据收发
 * @note 调用频率建议不低于10ms一次
 */
/**
 * @brief Poll the W5500 socket state and process Modbus TCP traffic.
 * @details Call this function frequently from the main loop. It also calls
 *          Bootload_Task so the Bootloader can jump to App when appropriate.
 */
void tcpServerTask(void)
{
    uint8_t sock_status;

    Bootload_Task();

    // 获取当前Socket状态
    sock_status = getSn_SR(TCP_SOCKET);
    //		printf("sock_status is %x\r\n",sock_status);
    static uint8_t last_status = 0xFF;

    if (sock_status != last_status)
    {
        printf("Socket Status Change: 0x%02X\r\n", sock_status);
        last_status = sock_status;
    }

    switch (sock_status)
    {
    case SOCK_ESTABLISHED: ///< 连接已建立
        handleEstablishedConnection();
        break;

    case SOCK_CLOSE_WAIT: ///< 客户端发起关闭连接
        handleCloseWait();
        break;

    case SOCK_INIT: ///< Socket已初始化，等待监听
        handleInitState();
        break;

    case SOCK_CLOSED: ///< Socket已关闭
        handleClosedState();
        break;

    case SOCK_LISTEN: ///< 监听状态
        // 监听状态下无需操作，等待客户端连接
        break;

    default: ///< 其他状态（如SOCK_FIN_WAIT等）
        // 可根据需要添加状态处理
        break;
    }
}

/*============================================================================
 * 静态函数实现
 *============================================================================*/

/**
 * @brief 网络初始化
 * @details 初始化W5500硬件、SPI接口、注册回调函数并设置网络参数
 */
/**
 * @brief Configure W5500 low-level callbacks and network parameters.
 * @details Initializes chip-select/SPI callbacks, allocates socket buffers,
 *          writes MAC/IP/gateway/subnet settings, and initializes the Modbus app.
 */
static void netInit(void)
{
    uint8_t txsize[SOCKET_COUNT] = {0}; ///< 各Socket发送缓冲区大小
    uint8_t rxsize[SOCKET_COUNT] = {0}; ///< 各Socket接收缓冲区大小

    // 为所有Socket分配相同大小的缓冲区
    for (int i = 0; i < SOCKET_COUNT; i++)
    {
        txsize[i] = SOCKET_TX_SIZE; // 每个Socket分配2KB发送缓冲区
        rxsize[i] = SOCKET_RX_SIZE; // 每个Socket分配2KB接收缓冲区
    }

    // 1. 初始化W5500控制引脚（RST、CS等）
    W5500_Port_Init();

    // 2. 注册CS控制回调函数（片选控制）
    reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);

    // 3. 注册SPI读写回调函数
    reg_wizchip_spi_cbfunc(wizchipSpiReadbyte, wizchipSpiWritebyte);

    // 4. 初始化W5500芯片，分配缓冲区
    if (wizchip_init(txsize, rxsize) != 0)
    {
        // 初始化失败处理
        printf("w5500 init erro\r\n");
        return;
    }

    // 5. 设置网络参数（IP、MAC等）
    wizchip_setnetinfo(&netinfo);

    // 6. 可选：打印网络配置信息
    printNetInfo();

    Modbus_App_Init();
}

/**
 * @brief TCP服务器初始化
 * @return SOCK_OK 成功，其他值失败
 */
/**
 * @brief Create the W5500 TCP socket and start listening.
 * @return SOCK_OK on success; W5500 socket-layer error code on failure.
 */
static int8_t TCP_Server_Init(void)
{
    int8_t ret;

    // 1. 创建TCP Socket
    // 参数：Socket号，TCP模式，端口号，标志位(0表示无标志)
    ret = socket(TCP_SOCKET, Sn_MR_TCP, TCP_PORT, 0);

    if (ret != TCP_SOCKET)
    {

        // Socket创建失败
        printf("Socket create Error ret is %d\r\n", ret);
        return ret;
    }
    printf("Socket created successfully\r\n");

    // 2. 开始监听
    ret = listen(TCP_SOCKET);

    if (ret != SOCK_OK)
    {
        // 监听失败，关闭Socket
        printf("Listen Error");
        close(TCP_SOCKET);
        return ret;
    }
    printf("TCP Server listening on port %d\r\n", TCP_PORT);
    return SOCK_OK;
}

/**
 * @brief 处理已建立的TCP连接
 */
/**
 * @brief Handle an established TCP connection.
 * @details Clears the connection interrupt flag and delegates receive/send work
 *          to handleDataTransmission.
 */
static void handleEstablishedConnection(void)
{
    uint16_t len;      ///< 接收到的数据长度
    uint8_t irq_flags; ///< 中断标志

    // 1. 清除连接中断标志（防止重复触发）
    irq_flags = getSn_IR(TCP_SOCKET);
    if (irq_flags & Sn_IR_CON)
    {
        setSn_IR(TCP_SOCKET, Sn_IR_CON); // 写1清除标志
    }

    // 2. 处理数据收发
    handleDataTransmission();
}

/**
 * @brief 处理数据收发
 */
/**
 * @brief Receive TCP bytes, process Modbus TCP, and send the response.
 * @details The function reads the W5500 RX size, clamps it to local buffer size,
 *          calls Modbus_TCP_Process, then sends any non-empty response.
 */
static void handleDataTransmission(void)
{
    int32_t len;

    len = getSn_RX_RSR(TCP_SOCKET);

    if (len > 0)
    {
        if (len > BUFFER_SIZE)
            len = BUFFER_SIZE;

        // 接收原始 TCP 包
        len = recv(TCP_SOCKET, tcp_rx_buf, len);

        if (len > 0)
        {
            // 调用 Modbus 处理引擎
            uint16_t resp_len = Modbus_TCP_Process(tcp_rx_buf, (uint16_t)len, tcp_tx_buf);

            // 如果有合法的响应，则通过 W5500 发出
            if (resp_len > 0)
            {
                send(TCP_SOCKET, tcp_tx_buf, resp_len);
            }
        }
    }
}

/**
 * @brief 处理SOCK_CLOSE_WAIT状态（客户端发起关闭）
 */
/**
 * @brief Handle peer-requested TCP close.
 * @details Sends a disconnect request so the socket can return to CLOSED/INIT.
 */
static void handleCloseWait(void)
{
    // 1. 主动断开连接
    disconnect(TCP_SOCKET);

    // 2. 可选：等待一段时间确保断开完成
    // delay_ms(10);
}

/**
 * @brief 处理SOCK_INIT状态（等待监听）
 * @return SOCK_OK 成功，其他值失败
 */
/**
 * @brief Start listening again when the socket is in SOCK_INIT.
 * @return SOCK_OK on success; W5500 socket-layer error code on failure.
 */
static int8_t handleInitState(void)
{
    int8_t ret;

    // 开始监听
    ret = listen(TCP_SOCKET);

    if (ret != SOCK_OK)
    {
        // 监听失败，关闭Socket重新初始化
        close(TCP_SOCKET);
        return ret;
    }

    return SOCK_OK;
}

/**
 * @brief 处理SOCK_CLOSED状态（Socket关闭）
 */
/**
 * @brief Recreate and relisten the TCP socket after it becomes closed.
 */
static void handleClosedState(void)
{
    int8_t ret;

    // 1. 重新创建Socket
    ret = socket(TCP_SOCKET, Sn_MR_TCP, TCP_PORT, 0);

    if (ret == TCP_SOCKET)
    {
        // 2. 开始监听
        listen(TCP_SOCKET);
    }
    // 如果创建失败，下次任务会再次尝试
}

/**
 * @brief 打印网络配置信息（调试用）
 */
/**
 * @brief Print the active W5500 network configuration for debugging.
 */
static void printNetInfo(void)
{
    wiz_NetInfo info;
    wizchip_getnetinfo(&info);

    printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           info.mac[0], info.mac[1], info.mac[2],
           info.mac[3], info.mac[4], info.mac[5]);
    printf("IP: %d.%d.%d.%d\r\n",
           info.ip[0], info.ip[1], info.ip[2], info.ip[3]);
    printf("Gateway: %d.%d.%d.%d\r\n",
           info.gw[0], info.gw[1], info.gw[2], info.gw[3]);
    printf("Subnet Mask: %d.%d.%d.%d\r\n",
           info.sn[0], info.sn[1], info.sn[2], info.sn[3]);
    printf("DNS: %d.%d.%d.%d\r\n",
           info.dns[0], info.dns[1], info.dns[2], info.dns[3]);
}
