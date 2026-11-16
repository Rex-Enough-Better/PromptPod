# Third-Party Notices

`PromptPod`（词舱）为专有软件（Copyright © 2026 Rex, All Rights Reserved），基于 Windows 10 / 11 系统原生图形与窗口接口构建，不打包任何第三方动态链接库或浏览器运行时。

在编译生成可执行文件的过程中，静态链接了以下工具链的基础运行时启动与标准库组件，特此声明其原有许可：

---

## 1. MinGW-w64 Runtime

- **用途**：Windows 平台 C 运行时启动代码与系统 API 导入声明。
- **许可**：Zope Public License (ZPL) 2.1 / Public Domain / MIT-style Permissive Licenses。
- **说明**：允许在商业及专有闭源软件中静态链接并分发二进制程序。

---

## 2. GCC Runtime Libraries (`libgcc` / `libstdc++`)

- **用途**：C++17 基础语言支持与标准容器运行时。
- **许可**：GNU General Public License v3.0 with **GCC Runtime Library Exception v3.1**（GCC 运行时库例外条款 3.1）。
- **说明**：根据 Free Software Foundation 发布的 GCC Runtime Library Exception 3.1，使用未修改的合格编译流程生成的目标代码可按作者自行选择的许可条款（包括闭源专有许可）独立分发。
