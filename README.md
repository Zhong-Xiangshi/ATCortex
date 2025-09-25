# ATCortex

正在开发中的 一个轻量、可移植的 **AT 指令框架**。特点：

* **零依赖/易移植**：不依赖C标准库以外的其他库。仅需实现 `port/at_port.*` 的 4 个函数
* **多端口**：多块模块/多路串口同时管理
* **命令队列**：异步发送、按行解析、回调返回
* **URC 分发**：基于前缀的异步提示（URC）处理
* **事务发送**：支持两类数据阶段


---

## 目录结构

```
.
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
└── src
    ├── core
    │   ├── at_dispatcher.c/.h   # URC 分发器
    │   ├── at_engine.c/.h       # 引擎（队列、状态机、超时、事务）
    │   ├── at_log.c/.h          # 日志封装（可开关）
    │   ├── at_parser.c/.h       # 行解析器（'\n' 结束，忽略 '\r'）
    │   ├── at_queue.c/.h        # 循环队列（命令上下文）
    ├── example
    │   ├── main.c               # 最小示例
    │   └── test.c               # 自测/演示用例（含事务、URC、超时等）
    ├── include
    │   └── at.h                 # **对外 API（doxygen 中文注释）**
    └── port
        ├── at_port.c            # 需按平台实现（串口/时钟）
        └── at_port.h
```
