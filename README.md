# TcpBootload Host Workspace

本仓库用于 `W5500 + STM32F407VGT6` 仪表 Bootload 方案中的上位机开发与文档沉淀。

## 目标范围

- 上位机语言：`Python`
- 图形界面：`PyQt`（建议 `PyQt6`）
- 传输介质：以太网
- 业务协议现状：设备 App 运行 `Modbus TCP`
- 本仓库定位：仅上位机工程与文档，不包含下位机固件代码

## 目录结构

```text
TcpBootload/
├─ docs/                         # 方案与开发文档
│  ├─ 10-总体架构.md
│  ├─ 20-通信与升级协议框架.md
│  ├─ 30-上位机软件架构-Python-PyQt.md
│  └─ 40-开发计划与里程碑.md
└─ pc_tool/                      # Python 上位机工程
   ├─ pyproject.toml
   ├─ requirements.txt
   └─ src/
      └─ tcp_bootload_host/
         ├─ main.py
         ├─ app.py
         ├─ ui/
         │  ├─ main_window.py
         │  └─ widgets/
         ├─ service/
         │  ├─ upgrade_service.py
         │  └─ device_session.py
         ├─ protocol/
         │  ├─ modbus_client.py
         │  └─ bootload_protocol.py
         ├─ model/
         │  └─ dto.py
         └─ util/
            ├─ crc.py
            └─ firmware.py
```

## 快速开始

1. 进入上位机目录：`cd pc_tool`
2. 创建虚拟环境并安装依赖：
   - `python -m venv .venv`
   - `.venv\Scripts\activate`
   - `pip install -r requirements.txt`
3. 启动程序：`python -m tcp_bootload_host.main`
4. 或一键启动：在 `pc_tool` 目录执行 `powershell -ExecutionPolicy Bypass -File .\run.ps1`

## 文档阅读顺序

1. `docs/10-总体架构.md`
2. `docs/20-通信与升级协议框架.md`
3. `docs/30-上位机软件架构-Python-PyQt.md`
4. `docs/40-开发计划与里程碑.md`
