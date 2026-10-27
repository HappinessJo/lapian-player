# 贡献指南

感谢关注。提交前请先看 [README](README.md) 与 [AGENTS.md](AGENTS.md)（工程约定）。

## 环境

- Windows 10/11 x64 + Visual Studio 2022（MSVC x64）
- 依赖不入库，按 [docs/deps.md](docs/deps.md) 用 `scripts/` 下的脚本拉取/构建
- 构建：`scripts\configure.ps1` → `scripts\build-release.ps1`
- 测试：`ctest --test-dir build\release -C Release --output-on-failure`（基线 38/38）

## 约定

- 只提交源码 / 测试 / 脚本 / 文档；**不要**提交 `build/`、`dist/`、`out/`、`third_party/`、`media/`、日志、
  `*.lapian.json`、`*.lapian-workrange.json`、`__pycache__/`。
- 二进制产物走 Releases，不进 git。
- 不提交任何账号、Cookie、SESSDATA、凭据或受版权保护的视频内容。
- 中文源码/文档请保持 UTF-8；行尾由 `.gitattributes` 统一（源码 LF，PowerShell CRLF）。
- 改动请附带测试；性能相关改动请给出前后同口径数字（`docs/perf/`）。

## 提交

- 一个逻辑改动一个 commit，说明「为什么」而不只是「改了什么」。
- 提交前：`git status` 干净、`ctest` 全绿、无新增未跟踪产物。
