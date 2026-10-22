#pragma once

// T8.2 凭据存储：只存 SESSDATA，Windows DPAPI 当前用户加密。
//
// - 默认路径：%APPDATA%\拉片软件\credentials.dat
// - 文件 = magic("LAPCRED1") + uint32 小端长度 + CryptProtectData 密文；
// - 原子写（QSaveFile）；损坏/截断/无法解密一律按“无凭据”处理，不崩；
// - 不记录/不打印明文；Load 返回的明文只在内存中短暂存在，调用方用完应
//   ClearSecret()；
// - 不做账号体系、不做多账号、不做扫码/密码登录。

#include <QString>

#include "online/Sessdata.h"

namespace lapian::online {

// %APPDATA%\拉片软件\credentials.dat；APPDATA 不可用时回退 Qt 的标准位置。
QString DefaultCredentialPath();

struct CredentialLoadResult {
  bool ok = false;             // 文件存在、格式正确、DPAPI 解密成功
  bool has_credential = false; // ok 的别名，便于调用方读语义
  bool missing = false;        // 文件不存在（不是错误）
  bool corrupt = false;        // 文件存在但损坏/属于其他用户/格式无效
  QString sessdata;            // 仅 ok 时非空；调用方负责 ClearSecret
  QString error;               // 可读错误；不含明文
};

class CredentialStore {
 public:
  // path 为空时使用 DefaultCredentialPath()。测试可注入临时路径。
  explicit CredentialStore(QString path = QString());

  QString path() const { return path_; }

  // 读取并解密；失败不抛异常。这是 Has() 的实现，也用于真正的 nav 请求。
  CredentialLoadResult Load() const;

  bool Has() const;

  // 校验 + DPAPI 加密 + 原子写。成功后磁盘上不出现明文。
  bool Save(const QString& sessdata, QString* error = nullptr);

  // 删除凭据文件；文件不存在也算成功。清除后调用方应把状态回 anonymous。
  bool Clear(QString* error = nullptr);

 private:
  QString path_;
};

}  // namespace lapian::online
