# 安全策略

## 支持范围

只对**最新 Release** 与 `main` 分支的最新提交提供安全修复。

## 报告安全问题

请**不要**在公开 Issue 里贴可利用细节、账号、Cookie 或凭据。

优先使用 GitHub 的私密报告通道：
**Security → Advisories → Report a vulnerability**（仓库 Security 标签页）。

如果该入口不可用，请通过维护者 GitHub 主页公开的联系方式私下联系，并说明：

- 影响版本（exe SHA256 前 8 位 / Release tag）
- 复现步骤与影响（信息泄露 / 任意文件读写 / 崩溃 / 远程代码执行等）
- 是否已在公开场合披露

## 我们的处理

- 尽力在 7 天内确认收到，30 天内给出修复计划或说明；
- 修复发布后会在 Release notes 与 Advisory 中致谢（如果你愿意署名）。

## 范围说明

本项目会处理：

- B 站登录凭据（SESSDATA）——使用 Windows DPAPI 加密存储，日志脱敏；
- 本地视频文件与 sidecar（`.lapian.json` / `.lapian-workrange.json`）；
- 在线 HTTP 响应与媒体数据。

**不在范围**：需要用户主动提供账号才能触发的服务端问题、B 站平台自身的问题、以及未使用最新版本的问题。
