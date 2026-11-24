# Third-Party Notices

`词舱 PromptPod` 采用 MIT License 开源发布（Copyright © 2026 Rex），基于 Windows 10 / 11 系统原生图形与窗口接口构建，不打包任何第三方动态链接库或浏览器运行时。

在编译生成单文件可执行程序的过程中，静态链接了以下工具链的基础运行时启动与标准库组件，特此声明其原有许可：

---

## 1. MinGW-w64 Runtime

- **用途**：Windows 平台 C 运行时启动代码与系统 API 导入声明。
- **许可**：Zope Public License (ZPL) 2.1 / Public Domain / MIT-style Permissive Licenses。
- **说明**：允许静态链接并随编译产物独立分发。

---

## 2. GCC Runtime Libraries (`libgcc` / `libstdc++`)

- **用途**：C++17 基础语言支持与标准容器运行时。
- **许可**：GNU General Public License v3.0 with **GCC Runtime Library Exception v3.1**（GCC 运行时库例外条款 3.1）。
- **说明**：根据 Free Software Foundation 发布的 GCC Runtime Library Exception 3.1，使用未修改的合格编译流程生成的目标代码可按作者自行选择的许可条款（包括 MIT 许可）独立分发。

---

## 3. 内置精选 Prompt 出处与致谢

本软件内置的 12 条精选默认 Prompt（10 条指挥 AI Agent 深度思考与工程复盘提示词 + 2 条产品用户体验与动效审查提示词）均整理自知名 AI 创作者**「数字生命卡兹克」**的公开分享，特此向原作者致谢：

- X（Twitter）：https://x.com/Khazix0918
- GitHub：https://github.com/KKKKhazix
