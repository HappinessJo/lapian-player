#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <cerrno>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern "C" {
#include <libavutil/error.h>
}

namespace lapian::online::native_file {

using Handle = std::intptr_t;
constexpr Handle kInvalid = -1;

inline Handle OpenForWrite(const QString& path) {
#ifdef _WIN32
  QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  if (native.startsWith(QStringLiteral("\\\\"))) {
    native = QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
  } else if (!native.startsWith(QStringLiteral("\\\\?\\"))) {
    native = QStringLiteral("\\\\?\\") + native;
  }
  HANDLE handle = CreateFileW(native.toStdWString().c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  return handle == INVALID_HANDLE_VALUE ? kInvalid : reinterpret_cast<Handle>(handle);
#else
  const QByteArray native = QFile::encodeName(path);
  const int handle = ::open(native.constData(), O_WRONLY);
  if (handle >= 0 && ::flock(handle, LOCK_SH | LOCK_NB) != 0) {
    ::close(handle);
    return kInvalid;
  }
  return handle;
#endif
}

inline bool Size(Handle handle, std::int64_t* size) {
#ifdef _WIN32
  LARGE_INTEGER value{};
  if (GetFileSizeEx(reinterpret_cast<HANDLE>(handle), &value) == FALSE) {
    return false;
  }
  *size = value.QuadPart;
  return true;
#else
  struct stat info {};
  if (::fstat(static_cast<int>(handle), &info) != 0) {
    return false;
  }
  *size = info.st_size;
  return true;
#endif
}

inline bool Seek(Handle handle, std::int64_t offset) {
#ifdef _WIN32
  LARGE_INTEGER value{};
  value.QuadPart = offset;
  return SetFilePointerEx(reinterpret_cast<HANDLE>(handle), value, nullptr, FILE_BEGIN) != FALSE;
#else
  return ::lseek(static_cast<int>(handle), static_cast<off_t>(offset), SEEK_SET) >= 0;
#endif
}

inline bool Truncate(Handle handle) {
#ifdef _WIN32
  return SetEndOfFile(reinterpret_cast<HANDLE>(handle)) != FALSE;
#else
  const off_t offset = ::lseek(static_cast<int>(handle), 0, SEEK_CUR);
  return offset >= 0 && ::ftruncate(static_cast<int>(handle), offset) == 0;
#endif
}

inline bool Write(Handle handle, const std::uint8_t* data, std::size_t size,
                  std::size_t* written) {
#ifdef _WIN32
  DWORD count = 0;
  if (WriteFile(reinterpret_cast<HANDLE>(handle), data, static_cast<DWORD>(size), &count,
                nullptr) == FALSE) {
    return false;
  }
  *written = count;
  return true;
#else
  const ssize_t count = ::write(static_cast<int>(handle), data, size);
  if (count < 0) {
    return false;
  }
  *written = static_cast<std::size_t>(count);
  return true;
#endif
}

inline void Flush(Handle handle) {
#ifdef _WIN32
  FlushFileBuffers(reinterpret_cast<HANDLE>(handle));
#else
  ::fsync(static_cast<int>(handle));
#endif
}

inline void Close(Handle handle) {
#ifdef _WIN32
  CloseHandle(reinterpret_cast<HANDLE>(handle));
#else
  ::close(static_cast<int>(handle));
#endif
}

inline int LastError() {
#ifdef _WIN32
  return static_cast<int>(GetLastError());
#else
  return errno;
#endif
}

inline QString ErrorText(int code) {
#ifdef _WIN32
  wchar_t* buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, static_cast<DWORD>(code), 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  QString text;
  if (length > 0 && buffer != nullptr) {
    text = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
  }
  if (buffer != nullptr) {
    LocalFree(buffer);
  }
  return text.isEmpty() ? QStringLiteral("Win32 %1").arg(code) : text;
#else
  return QString::fromLocal8Bit(std::strerror(code));
#endif
}

inline int AvError(int code) {
#ifdef _WIN32
  switch (code) {
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
      return AVERROR(ENOSPC);
    case ERROR_ACCESS_DENIED:
    case ERROR_WRITE_PROTECT:
      return AVERROR(EACCES);
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
      return AVERROR(ENOMEM);
    case ERROR_OPERATION_ABORTED:
      return AVERROR(ECANCELED);
    default:
      return AVERROR(EIO);
  }
#else
  switch (code) {
    case ENOSPC:
      return AVERROR(ENOSPC);
    case EACCES:
    case EPERM:
    case EROFS:
      return AVERROR(EACCES);
    case ENOMEM:
      return AVERROR(ENOMEM);
    case ECANCELED:
      return AVERROR(ECANCELED);
    default:
      return AVERROR(EIO);
  }
#endif
}

}  // namespace lapian::online::native_file
