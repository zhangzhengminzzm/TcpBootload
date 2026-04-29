/// @brief W5500 Modbus TCP 服务接口。

#ifndef __CTCPSERVER_H
#define __CTCPSERVER_H

#include "CConfig.h"
#include "socket.h"
#include "w5500.h"
#include "w5500Port.h"

/**
 * @brief 初始化 W5500 网络配置并启动 Modbus TCP 服务。
 * @details 在系统时钟、GPIO、SPI 等底层资源准备完成后调用一次。
 */
void initTcpServer(void);

/**
 * @brief TCP 服务周期任务。
 * @details 在主循环中反复调用，用于接受连接、处理 Modbus TCP 数据并执行 Bootload 周期逻辑。
 */
void tcpServerTask(void);


#endif /* __CTCPSEVER_H */