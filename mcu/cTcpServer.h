/// @brief TCP server 初始化

#ifndef __CTCPSERVER_H
#define __CTCPSERVER_H

#include "CConfig.h"
#include "socket.h"
#include "w5500.h"
#include "w5500Port.h"

/**
 * @brief Initialize W5500 network settings and start the Modbus TCP server.
 * @details Call once during MCU startup after clocks/GPIO/SPI prerequisites are ready.
 */
void initTcpServer(void);

/**
 * @brief Periodic TCP server task.
 * @details Call repeatedly from the main loop to accept connections, process
 *          Modbus TCP packets, and run Bootload periodic logic.
 */
void tcpServerTask(void);


#endif /* __CTCPSEVER_H */

