## 这个 PR 做了什么

<!-- 一两句话说明目的，不要只写「改了什么」 -->

## 关联 Issue

<!-- 例如 Closes #12；没有就写「无」 -->

## 验证方式

- [ ] `ctest --test-dir build\release -C Release` 全绿（或说明未跑及原因）
- [ ] 本地手工验证（说明素材/步骤；如涉及在线，说明是否缓存命中）
- [ ] 性能相关改动附前后同口径数字（`docs/perf/` 的写法）

## 检查清单

- [ ] 没有提交 `build/`、`third_party/`、`media/`、日志、`*.lapian*.json`、`__pycache__/`
- [ ] 没有提交账号 / Cookie / 凭据 / 受版权保护的视频或截图
- [ ] 新增/修改的源码为 UTF-8；行尾符合 `.gitattributes`
- [ ] 行为变更已同步到 `README.md` / `spec.md` / `docs/`（如适用）
