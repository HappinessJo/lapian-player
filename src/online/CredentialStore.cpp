#include "online/CredentialStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <cstring>
#include <utility>

#include <windows.h>
#include <dpapi.h>

namespace lapian::online {
namespace {

constexpr char kMagic[8] = {'L', 'A', 'P', 'C', 'R', 'E', 'D', '1'};
constexpr qint64 kHeaderBytes = 8 + 4;
constexpr qint64 kMaxFileBytes = 64 * 1024;

const QByteArray& CredentialEntropy() {
  static const QByteArray entropy = QByteArrayLiteral("lapian-credentials-v1");
  return entropy;
}

DATA_BLOB MakeBlob(const QByteArray& bytes) {
  DATA_BLOB blob{};
  blob.cbData = static_cast<DWORD>(bytes.size());
  blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(bytes.constData()));
  return blob;
}

bool Protect(const QByteArray& plain, QByteArray* cipher, QString* error) {
  DATA_BLOB input = MakeBlob(plain);
  DATA_BLOB entropy = MakeBlob(CredentialEntropy());
  DATA_BLOB output{};
  // 只允许当前用户：不使用 CRYPTPROTECT_LOCAL_MACHINE。
  if (!CryptProtectData(&input, L"Lapian credential", &entropy, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &output)) {
    *error = QStringLiteral("DPAPI 加密失败（错误 %1）").arg(GetLastError());
    return false;
  }
  *cipher = QByteArray(reinterpret_cast<const char*>(output.pbData),
                      static_cast<qsizetype>(output.cbData));
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return true;
}

bool Unprotect(const QByteArray& cipher, QByteArray* plain, QString* error) {
  DATA_BLOB input = MakeBlob(cipher);
  DATA_BLOB entropy = MakeBlob(CredentialEntropy());
  DATA_BLOB output{};
  if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
    *error = QStringLiteral("凭据无法解密（可能属于其他 Windows 用户或已损坏）");
    return false;
  }
  *plain = QByteArray(reinterpret_cast<const char*>(output.pbData),
                     static_cast<qsizetype>(output.cbData));
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return true;
}

void WipeBytes(QByteArray* bytes) {
  if (bytes == nullptr) {
    return;
  }
  if (!bytes->isEmpty()) {
    SecureZeroMemory(bytes->data(), static_cast<SIZE_T>(bytes->size()));
  }
  bytes->clear();
}

QByteArray BuildFileBlob(const QByteArray& cipher) {
  QByteArray blob;
  blob.reserve(static_cast<int>(kHeaderBytes) + cipher.size());
  blob.append(kMagic, sizeof(kMagic));
  const quint32 size = static_cast<quint32>(cipher.size());
  blob.append(static_cast<char>(size & 0xff));
  blob.append(static_cast<char>((size >> 8) & 0xff));
  blob.append(static_cast<char>((size >> 16) & 0xff));
  blob.append(static_cast<char>((size >> 24) & 0xff));
  blob.append(cipher);
  return blob;
}

bool ParseFileBlob(const QByteArray& blob, QByteArray* cipher, QString* error) {
  if (blob.size() < kHeaderBytes) {
    *error = QStringLiteral("凭据文件过短");
    return false;
  }
  if (std::memcmp(blob.constData(), kMagic, sizeof(kMagic)) != 0) {
    *error = QStringLiteral("凭据文件格式不正确");
    return false;
  }
  const auto* raw = reinterpret_cast<const unsigned char*>(blob.constData() + 8);
  const quint32 size = static_cast<quint32>(raw[0]) |
                       (static_cast<quint32>(raw[1]) << 8) |
                       (static_cast<quint32>(raw[2]) << 16) |
                       (static_cast<quint32>(raw[3]) << 24);
  if (size == 0 || static_cast<qint64>(size) != blob.size() - kHeaderBytes) {
    *error = QStringLiteral("凭据文件长度不正确");
    return false;
  }
  *cipher = blob.mid(static_cast<int>(kHeaderBytes));
  return true;
}

}  // namespace

QString DefaultCredentialPath() {
  QString appdata = qEnvironmentVariable("APPDATA");
  if (appdata.isEmpty()) {
    appdata = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  }
  if (appdata.isEmpty()) {
    appdata = QDir::homePath() + QStringLiteral("/AppData/Roaming");
  }
  QDir base(appdata);
  if (base.dirName() == QStringLiteral("拉片软件")) {
    return base.filePath(QStringLiteral("credentials.dat"));
  }
  return base.filePath(QStringLiteral("拉片软件/credentials.dat"));
}

CredentialStore::CredentialStore(QString path)
    : path_(path.isEmpty() ? DefaultCredentialPath() : std::move(path)) {}

CredentialLoadResult CredentialStore::Load() const {
  CredentialLoadResult result;
  if (path_.isEmpty()) {
    result.missing = true;
    result.error = QStringLiteral("凭据路径为空");
    return result;
  }

  const QFileInfo info(path_);
  if (!info.exists()) {
    result.missing = true;
    return result;
  }
  if (!info.isFile()) {
    result.corrupt = true;
    result.error = QStringLiteral("凭据路径不是普通文件");
    return result;
  }
  if (info.size() < kHeaderBytes || info.size() > kMaxFileBytes) {
    result.corrupt = true;
    result.error = QStringLiteral("凭据文件大小异常");
    return result;
  }

  QFile file(path_);
  if (!file.open(QIODevice::ReadOnly)) {
    result.corrupt = true;
    result.error = QStringLiteral("凭据文件无法读取");
    return result;
  }
  const QByteArray blob = file.readAll();
  file.close();

  QByteArray cipher;
  if (!ParseFileBlob(blob, &cipher, &result.error)) {
    result.corrupt = true;
    return result;
  }

  QByteArray plain;
  if (!Unprotect(cipher, &plain, &result.error)) {
    result.corrupt = true;
    return result;
  }

  const SessdataNormalization normalized =
      NormalizeSessdata(QString::fromUtf8(plain));
  WipeBytes(&plain);
  if (!normalized.ok) {
    result.corrupt = true;
    result.error = QStringLiteral("凭据内容无效：") + normalized.error;
    return result;
  }

  result.ok = true;
  result.has_credential = true;
  result.sessdata = normalized.value;
  return result;
}

bool CredentialStore::Has() const { return Load().has_credential; }

bool CredentialStore::Save(const QString& sessdata, QString* error) {
  SessdataNormalization normalized = NormalizeSessdata(sessdata);
  if (!normalized.ok) {
    if (error != nullptr) {
      *error = normalized.error;
    }
    return false;
  }

  QByteArray plain = normalized.value.toUtf8();
  ClearSecret(&normalized.value);
  QByteArray cipher;
  QString crypto_error;
  const bool protected_ok = Protect(plain, &cipher, &crypto_error);
  WipeBytes(&plain);
  if (!protected_ok) {
    if (error != nullptr) {
      *error = crypto_error;
    }
    return false;
  }

  const QFileInfo info(path_);
  QDir parent(info.absolutePath());
  if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
    if (error != nullptr) {
      *error = QStringLiteral("无法创建凭据目录：%1").arg(parent.absolutePath());
    }
    return false;
  }

  QSaveFile file(path_);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error != nullptr) {
      *error = QStringLiteral("无法写入凭据文件");
    }
    return false;
  }
  const QByteArray blob = BuildFileBlob(cipher);
  if (file.write(blob) != blob.size()) {
    file.cancelWriting();
    if (error != nullptr) {
      *error = QStringLiteral("凭据文件写入不完整");
    }
    return false;
  }
  if (!file.commit()) {
    if (error != nullptr) {
      *error = QStringLiteral("凭据文件保存失败");
    }
    return false;
  }
  return true;
}

bool CredentialStore::Clear(QString* error) {
  if (path_.isEmpty()) {
    return true;
  }
  if (!QFileInfo::exists(path_)) {
    return true;
  }
  if (!QFile::remove(path_)) {
    if (error != nullptr) {
      *error = QStringLiteral("凭据文件删除失败");
    }
    return false;
  }
  return true;
}

}  // namespace lapian::online
