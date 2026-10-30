// T5.1 sidecar 存储单测：路径规范化/哈希、roundtrip、合并、损坏备份、
// 空文件删除、AppData 回退与迁回、frame+pts 校正、原子写失败保护。
// 不依赖 FFmpeg/D3D11，只链接 Qt Core 与 lapian_marks。

#ifdef _WIN32
#include <windows.h>
#endif

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTemporaryDir>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/MarkStore.h"

namespace {

using lapian::core::FrameLookup;
using lapian::core::Mark;
using lapian::core::MarkPaths;
using lapian::core::MarkStore;
using lapian::core::MarkStoreOptions;
using lapian::core::MarkWarning;
using lapian::core::MarkWarningKind;
using lapian::core::SaveResult;
using lapian::core::VideoIdentity;
using lapian::core::WorkRange;
using lapian::core::WorkRangeKind;
using lapian::core::WorkRangePaths;
using lapian::core::WorkRangeRecord;
using lapian::core::WorkRangeStore;

int g_failures = 0;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void CheckEq(const std::string& actual, const std::string& expected,
             const std::string& what) {
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n  实际：%s\n  期望：%s\n", what.c_str(), actual.c_str(),
                expected.c_str());
    ++g_failures;
  }
}

void CheckEq64(std::int64_t actual, std::int64_t expected,
               const std::string& what) {
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n  实际：%lld\n  期望：%lld\n", what.c_str(),
                static_cast<long long>(actual), static_cast<long long>(expected));
    ++g_failures;
  }
}

std::string ToUtf8(const QString& text) { return text.toUtf8().toStdString(); }

QString FromUtf8(const std::string& text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

bool WriteAll(const QString& path, const QByteArray& bytes) {
  const QFileInfo info(path);
  if (!QDir().mkpath(info.absolutePath())) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const bool ok = file.write(bytes) == bytes.size();
  file.close();
  return ok;
}

QByteArray ReadAll(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

QString NewVideo(const QString& directory, const QString& name) {
  QDir().mkpath(directory);
  const QString path = directory + QStringLiteral("/") + name;
  WriteAll(path, QByteArrayLiteral("lapian-test-video"));
  return path;
}

MarkStoreOptions OptionsFor(const QString& appdata_dir) {
  MarkStoreOptions options;
  // 选项是完整的 marks 目录（默认 %APPDATA%\拉片软件\marks），
  // 测试传入 AppData 根时在这里补上 marks。
  options.appdata_marks_dir = ToUtf8(appdata_dir + QStringLiteral("/marks"));
  return options;
}

Mark MakeMark(const std::string& id, std::int64_t frame, std::int64_t pts,
              const std::string& label, const std::string& note,
              const std::string& updated_at) {
  Mark mark;
  mark.id = id;
  mark.frame = frame;
  mark.pts_us = pts;
  mark.label = label;
  mark.note = note;
  mark.created_at = updated_at;
  mark.updated_at = updated_at;
  return mark;
}

const Mark* FindIn(const std::vector<Mark>& marks, const std::string& id) {
  for (const Mark& mark : marks) {
    if (mark.id == id) {
      return &mark;
    }
  }
  return nullptr;
}

const Mark* FindIn(const MarkStore& store, const std::string& id) {
  return store.FindMark(id);
}

bool HasWarning(const MarkStore& store, MarkWarningKind kind) {
  for (const MarkWarning& warning : store.warnings()) {
    if (warning.kind == kind) {
      return true;
    }
  }
  return false;
}

QString SidecarOf(const MarkPaths& paths) { return FromUtf8(paths.sidecar); }
QString FallbackOf(const MarkPaths& paths) { return FromUtf8(paths.fallback); }

void TestPathNormalization(const QString& root) {
  std::printf("\n-- 路径规范化 / 哈希 --\n");
  const QString directory = root + QStringLiteral("/paths");
  const QString video = NewVideo(directory, QStringLiteral("Video.MP4"));
  const QString other = NewVideo(directory, QStringLiteral("other.mp4"));
  const QByteArray video_bytes = ReadAll(video);

  const std::string canonical = ToUtf8(video);
  const std::string upper = ToUtf8(directory + QStringLiteral("/VIDEO.MP4"));
  const std::string dotted = ToUtf8(directory + QStringLiteral("/./Video.MP4"));

  CheckEq(lapian::core::NormalizeVideoPath(canonical),
          lapian::core::NormalizeVideoPath(upper), "大小写不同 -> 同一路径键");
  CheckEq(lapian::core::NormalizeVideoPath(canonical),
          lapian::core::NormalizeVideoPath(dotted), "./ 规范化 -> 同一路径键");
#ifdef _WIN32
  Check(lapian::core::NormalizeVideoPath(canonical).find('/') == std::string::npos,
        "路径键使用反斜杠");
#else
  Check(lapian::core::NormalizeVideoPath(canonical).find('/') != std::string::npos,
        "路径键使用 POSIX 分隔符");
#endif
  Check(lapian::core::NormalizeVideoPath(canonical) !=
            lapian::core::NormalizeVideoPath(ToUtf8(other)),
        "不同视频路径键不同");

  const VideoIdentity identity =
      lapian::core::MakeVideoIdentity(canonical, 1'234'567);
  Check(!identity.empty(), "视频标识有效");
  CheckEq64(static_cast<std::int64_t>(identity.size), video_bytes.size(),
            "视频大小来自文件");
  CheckEq64(identity.duration_us, 1'234'567, "视频时长来自参数");
  CheckEq(identity.path_key, lapian::core::NormalizeVideoPath(canonical),
          "path_key 与 NormalizeVideoPath 一致");

  const MarkPaths paths =
      lapian::core::ResolveMarkPaths(canonical, OptionsFor(root + QStringLiteral("/appdata")));
  CheckEq(paths.sidecar, identity.path + ".lapian.json",
          "sidecar = 视频完整文件名 + .lapian.json");
  Check(paths.path_hash.size() == 64, "路径哈希为 64 位 SHA-256 hex");
  CheckEq(paths.fallback,
          ToUtf8(root + QStringLiteral("/appdata/marks/") +
                 FromUtf8(paths.path_hash) + QStringLiteral(".json")),
          "回退路径 = AppData/marks/<hash>.json");

  const MarkPaths default_paths = lapian::core::ResolveMarkPaths(canonical);
  Check(default_paths.fallback.find("拉片软件") != std::string::npos,
        "默认真实回退目录含拉片软件");
  Check(default_paths.fallback.find("marks") != std::string::npos,
        "默认真实回退目录含 marks");

  Check(lapian::core::MarkPathHash(lapian::core::NormalizeVideoPath(canonical)) ==
            paths.path_hash,
        "MarkPathHash 与 ResolveMarkPaths 一致");
  Check(lapian::core::MarkPathHash(lapian::core::NormalizeVideoPath(canonical)) !=
            lapian::core::MarkPathHash(lapian::core::NormalizeVideoPath(ToUtf8(other))),
        "不同视频哈希不同");

  const std::string unc1 =
      lapian::core::NormalizeVideoPath("\\\\Server\\Share\\dir\\..\\Video.MP4");
  const std::string unc2 =
      lapian::core::NormalizeVideoPath("//server/share/Video.MP4");
  CheckEq(unc1, unc2, "UNC 大小写/分隔符/.. 规范化");
  CheckEq(unc1, "\\\\server\\share\\video.mp4", "UNC 规范化结果");
}

void TestRoundtrip(const QString& root) {
  std::printf("\n-- roundtrip / 删除最后标记 --\n");
  const QString directory = root + QStringLiteral("/roundtrip");
  const QString video = NewVideo(directory, QStringLiteral("测试.mp4"));
  const MarkStoreOptions options =
      OptionsFor(root + QStringLiteral("/appdata-roundtrip"));
  const MarkPaths paths = lapian::core::ResolveMarkPaths(ToUtf8(video), options);
  const QString sidecar = SidecarOf(paths);
  const QString fallback = FallbackOf(paths);
  const std::int64_t duration = 5'000'000;

  MarkStore store = MarkStore::Open(ToUtf8(video), duration, options);
  Check(store.valid(), "打开有效视频");
  Check(store.marks().empty(), "初始无标记");
  Check(!QFileInfo::exists(sidecar), "F6-A6：无标记不产生 sidecar");
  Check(!QFileInfo::exists(fallback), "F6-A6：无标记不产生回退文件");

  std::string id100;
  std::string id50;
  Check(store.AddMark(100, 3'333'333, "转场", "第一行\n第二行 \"引号\"", &id100),
        "添加第 100 帧标记");
  Check(store.AddMark(50, 1'666'666, "", "", &id50),
        "添加第 50 帧标记（空标签默认命名）");
  Check(store.marks().size() == 2, "内存中两个标记");
  Check(store.marks()[0].frame == 50 && store.marks()[1].frame == 100,
        "按帧号排序");
  CheckEq(store.marks()[0].label, "帧 50", "空标签默认“帧 N”");
  Check(!id100.empty() && id100 != id50, "标记 id 唯一");

  const SaveResult saved = store.Save();
  Check(saved.ok && saved.wrote, "保存成功");
  CheckEq(saved.path, paths.sidecar, "保存到 sidecar");
  Check(store.storage_path() == paths.sidecar && !store.using_fallback(),
        "实际存储位置为 sidecar");
  Check(QFileInfo::exists(sidecar), "sidecar 已生成");
  Check(!QFileInfo::exists(fallback), "未写回退文件");

  const QByteArray json = ReadAll(sidecar);
  QJsonParseError parse_error;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parse_error);
  Check(parse_error.error == QJsonParseError::NoError && document.isObject(),
        "sidecar 是合法 JSON");
  const QJsonObject root_object = document.object();
  CheckEq(root_object.value(QStringLiteral("schema")).toString().toUtf8().toStdString(),
          "lapian.marks", "schema 名称");
  CheckEq64(root_object.value(QStringLiteral("schema_version")).toInteger(), 1,
            "schema_version=1");
  const QJsonObject video_object =
      root_object.value(QStringLiteral("video")).toObject();
  CheckEq(video_object.value(QStringLiteral("path")).toString().toUtf8().toStdString(),
          store.video().path, "video.path 为规范化路径");
  CheckEq64(video_object.value(QStringLiteral("size")).toInteger(),
            static_cast<std::int64_t>(ReadAll(video).size()), "video.size");
  CheckEq64(video_object.value(QStringLiteral("duration_us")).toInteger(), duration,
            "video.duration_us");
  const QJsonArray mark_array = root_object.value(QStringLiteral("marks")).toArray();
  Check(mark_array.size() == 2, "marks 数组两条");
  const QJsonObject first = mark_array.at(0).toObject();
  CheckEq(first.value(QStringLiteral("id")).toString().toUtf8().toStdString(), id50,
          "mark.id 写入");
  CheckEq64(first.value(QStringLiteral("frame")).toInteger(), 50, "mark.frame 写入");
  CheckEq64(first.value(QStringLiteral("pts")).toInteger(), 1'666'666,
            "mark.pts 写入");
  Check(!first.value(QStringLiteral("created_at")).toString().isEmpty(),
        "created_at 非空");
  Check(!first.value(QStringLiteral("updated_at")).toString().isEmpty(),
        "updated_at 非空");
  Check(first.value(QStringLiteral("note")).toString().isEmpty(),
        "空备注写入空串");

  MarkStore reopened = MarkStore::Open(ToUtf8(video), duration, options);
  Check(reopened.marks().size() == 2, "重开恢复两个标记");
  const Mark* mark100 = FindIn(reopened, id100);
  Check(mark100 != nullptr && mark100->frame == 100 && mark100->pts_us == 3'333'333,
        "重开 frame/pts 一致");
  Check(mark100 != nullptr && mark100->label == "转场" &&
            mark100->note == "第一行\n第二行 \"引号\"",
        "重开 UTF-8 标签/备注一致");
  Check(reopened.warnings().empty(), "正常重开无警告");

  Check(reopened.SetMarkText(id100, "转场2", "改过的备注"), "编辑标签/备注");
  Check(reopened.Save().ok, "编辑后保存");
  const Mark* edited = FindIn(reopened, id100);
  Check(edited != nullptr && edited->label == "转场2" &&
            edited->note == "改过的备注",
        "内存中的编辑内容");

  MarkStore reopened2 = MarkStore::Open(ToUtf8(video), duration, options);
  const Mark* edited2 = FindIn(reopened2, id100);
  Check(edited2 != nullptr && edited2->label == "转场2" &&
            edited2->note == "改过的备注",
        "编辑内容已落盘");

  Check(reopened2.RemoveMark(id100), "删除第 100 帧标记");
  Check(reopened2.Save().ok, "删除后保存");
  MarkStore reopened3 = MarkStore::Open(ToUtf8(video), duration, options);
  Check(reopened3.marks().size() == 1 && FindIn(reopened3, id100) == nullptr,
        "删除后重开不存在");
  Check(!reopened3.RemoveMark("not-found-id"), "删除不存在的 id 返回 false");

  Check(reopened3.RemoveMark(id50), "删除最后一个标记");
  const SaveResult last = reopened3.Save();
  Check(last.ok && last.removed, "删除最后标记时删除附属文件（F6-A6）");
  Check(!QFileInfo::exists(sidecar), "sidecar 已删除");
  Check(!QFileInfo::exists(fallback), "回退文件不存在");
  Check(!QFileInfo::exists(sidecar + QStringLiteral(".bak")), "没有多余备份");
}

void TestMerge(const QString& root) {
  std::printf("\n-- 合并 --\n");
  const Mark a_old =
      MakeMark("a", 10, 1000, "A-old", "", "2026-01-01T00:00:00.000Z");
  const Mark a_new =
      MakeMark("a", 10, 1000, "A-new", "", "2026-01-02T00:00:00.000Z");
  const Mark b =
      MakeMark("b", 20, 2000, "B", "", "2026-01-01T00:00:00.000Z");
  const Mark c =
      MakeMark("c", 30, 3000, "C", "", "2026-01-03T00:00:00.000Z");

  const std::vector<Mark> merged =
      lapian::core::MergeMarks({a_old, b}, {a_new, c});
  Check(merged.size() == 3, "不同 id 取并集");
  Check(merged[0].id == "a" && merged[0].label == "A-new",
        "冲突取 updated_at 较新者");
  Check(merged[1].id == "b" && merged[2].id == "c", "合并结果按帧号排序");

  const std::vector<Mark> sidecar_wins = lapian::core::MergeMarks(
      {MakeMark("t", 1, 0, "side", "", "2026-01-01T00:00:00.000Z")},
      {MakeMark("t", 1, 0, "fallback", "", "2026-01-01T00:00:00.000Z")});
  Check(sidecar_wins.size() == 1 && sidecar_wins[0].label == "side",
        "updated_at 相同保留 sidecar 版本");

  const std::vector<Mark> sidecar_newer =
      lapian::core::MergeMarks({a_new}, {a_old});
  Check(sidecar_newer.size() == 1 && sidecar_newer[0].label == "A-new",
        "sidecar 较新时保留 sidecar");

  const std::vector<Mark> deduped =
      lapian::core::MergeMarks({a_old, a_new}, {});
  Check(deduped.size() == 1 && deduped[0].label == "A-new",
        "同一文件内重复 id 取较新者");

  const QString directory = root + QStringLiteral("/merge");
  const QString video = NewVideo(directory, QStringLiteral("merge.mp4"));
  const MarkStoreOptions options =
      OptionsFor(root + QStringLiteral("/appdata-merge"));
  const MarkPaths paths = lapian::core::ResolveMarkPaths(ToUtf8(video), options);
  const QString sidecar = SidecarOf(paths);
  const QString fallback = FallbackOf(paths);
  const VideoIdentity identity =
      lapian::core::MakeVideoIdentity(ToUtf8(video), 9'000'000);

  Check(WriteAll(sidecar, QByteArray::fromStdString(
                              lapian::core::SerializeMarkFile(identity, {a_old, b}))),
        "写入 sidecar");
  Check(WriteAll(fallback, QByteArray::fromStdString(
                               lapian::core::SerializeMarkFile(identity, {a_new, c}))),
        "写入回退文件");

  MarkStore store = MarkStore::Open(ToUtf8(video), 9'000'000, options);
  Check(store.marks().size() == 3, "打开时合并 sidecar 与回退");
  Check(FindIn(store, "a") != nullptr && FindIn(store, "a")->label == "A-new",
        "界面数据取较新版本");
  Check(QFileInfo::exists(sidecar), "合并后写回 sidecar");
  Check(!QFileInfo::exists(fallback), "合并后删除回退副本");

  MarkStore reopened = MarkStore::Open(ToUtf8(video), 9'000'000, options);
  Check(reopened.marks().size() == 3, "合并结果落盘后可重开");
  Check(FindIn(reopened, "a") != nullptr &&
            FindIn(reopened, "a")->label == "A-new",
        "落盘内容为较新版本");
}

void TestCorruptAndSchema(const QString& root) {
  std::printf("\n-- 损坏备份 / schema --\n");
  const QString directory = root + QStringLiteral("/corrupt");
  const QString video = NewVideo(directory, QStringLiteral("corrupt.mp4"));
  const MarkStoreOptions options =
      OptionsFor(root + QStringLiteral("/appdata-corrupt"));
  const MarkPaths paths = lapian::core::ResolveMarkPaths(ToUtf8(video), options);
  const QString sidecar = SidecarOf(paths);
  const QString fallback = FallbackOf(paths);

  const QByteArray broken = QByteArrayLiteral("{\"schema\":\"lapian.marks\",");
  Check(WriteAll(sidecar, broken), "写入损坏 sidecar");
  MarkStore store = MarkStore::Open(ToUtf8(video), 5'000'000, options);
  Check(store.valid(), "损坏 JSON 不阻塞打开");
  Check(store.marks().empty(), "损坏文件按无标记打开");
  Check(QFileInfo::exists(sidecar + QStringLiteral(".bak")), "损坏文件备份为 .bak");
  Check(ReadAll(sidecar + QStringLiteral(".bak")) == broken, "备份内容原样保留");
  Check(!QFileInfo::exists(sidecar), "损坏文件已移走");
  Check(HasWarning(store, MarkWarningKind::kCorruptBackup), "损坏警告");

  Check(store.AddMark(10, 1000, "新标记", "", nullptr), "损坏后仍可添加标记");
  Check(store.Save().ok, "损坏后仍可保存新 sidecar");
  Check(QFileInfo::exists(sidecar), "新 sidecar 已生成");
  Check(QFileInfo::exists(sidecar + QStringLiteral(".bak")), "旧备份保留");

  Check(WriteAll(fallback, QByteArrayLiteral("{oops")), "写入损坏回退文件");
  MarkStore with_good_sidecar = MarkStore::Open(ToUtf8(video), 5'000'000, options);
  Check(with_good_sidecar.marks().size() == 1, "回退损坏不丢 sidecar 标记");
  Check(QFileInfo::exists(fallback + QStringLiteral(".bak")), "回退损坏也备份");
  Check(HasWarning(with_good_sidecar, MarkWarningKind::kCorruptBackup),
        "回退损坏警告");

  // 更高 schema：保留原文件，不备份、不覆盖。
  const QString future_directory = root + QStringLiteral("/future");
  const QString future_video =
      NewVideo(future_directory, QStringLiteral("future.mp4"));
  const MarkStoreOptions future_options =
      OptionsFor(root + QStringLiteral("/appdata-future"));
  const MarkPaths future_paths =
      lapian::core::ResolveMarkPaths(ToUtf8(future_video), future_options);
  const QString future_sidecar = SidecarOf(future_paths);
  const VideoIdentity future_identity =
      lapian::core::MakeVideoIdentity(ToUtf8(future_video), 2'000'000);

  QJsonObject future_root;
  future_root.insert(QStringLiteral("schema"), QStringLiteral("lapian.marks"));
  future_root.insert(QStringLiteral("schema_version"), 2);
  QJsonObject future_video_object;
  future_video_object.insert(QStringLiteral("path"), FromUtf8(future_identity.path));
  future_video_object.insert(QStringLiteral("path_key"),
                             FromUtf8(future_identity.path_key));
  future_video_object.insert(QStringLiteral("size"),
                             static_cast<double>(future_identity.size));
  future_video_object.insert(QStringLiteral("duration_us"),
                             static_cast<double>(future_identity.duration_us));
  future_root.insert(QStringLiteral("video"), future_video_object);
  future_root.insert(QStringLiteral("marks"), QJsonArray());
  const QByteArray future_json = QJsonDocument(future_root).toJson();
  Check(WriteAll(future_sidecar, future_json), "写入 schema_version=2 文件");

  MarkStore future =
      MarkStore::Open(ToUtf8(future_video), 2'000'000, future_options);
  Check(future.marks().empty(), "更高 schema 不读取标记");
  Check(QFileInfo::exists(future_sidecar), "更高 schema 原文件保留");
  Check(!QFileInfo::exists(future_sidecar + QStringLiteral(".bak")),
        "更高 schema 不当作损坏备份");
  Check(HasWarning(future, MarkWarningKind::kUnsupportedSchema),
        "更高 schema 警告");
  Check(!future.Save().ok, "更高 schema 阻止覆盖");

  // 结构合法但 mark 字段缺失：按损坏备份，不阻塞。
  const QString invalid_directory = root + QStringLiteral("/invalid-mark");
  const QString invalid_video =
      NewVideo(invalid_directory, QStringLiteral("invalid.mp4"));
  const MarkStoreOptions invalid_options =
      OptionsFor(root + QStringLiteral("/appdata-invalid"));
  const MarkPaths invalid_paths =
      lapian::core::ResolveMarkPaths(ToUtf8(invalid_video), invalid_options);
  const QString invalid_sidecar = SidecarOf(invalid_paths);
  const VideoIdentity invalid_identity =
      lapian::core::MakeVideoIdentity(ToUtf8(invalid_video), 1'000'000);
  QJsonObject invalid_root;
  invalid_root.insert(QStringLiteral("schema"), QStringLiteral("lapian.marks"));
  invalid_root.insert(QStringLiteral("schema_version"), 1);
  QJsonObject invalid_video_object;
  invalid_video_object.insert(QStringLiteral("path"),
                              FromUtf8(invalid_identity.path));
  invalid_video_object.insert(QStringLiteral("path_key"),
                              FromUtf8(invalid_identity.path_key));
  invalid_video_object.insert(QStringLiteral("size"),
                              static_cast<double>(invalid_identity.size));
  invalid_video_object.insert(QStringLiteral("duration_us"),
                              static_cast<double>(invalid_identity.duration_us));
  invalid_root.insert(QStringLiteral("video"), invalid_video_object);
  QJsonArray invalid_marks;
  QJsonObject invalid_mark;
  invalid_mark.insert(QStringLiteral("frame"), 1);
  invalid_mark.insert(QStringLiteral("pts"), 0);
  invalid_marks.append(invalid_mark);
  invalid_root.insert(QStringLiteral("marks"), invalid_marks);
  Check(WriteAll(invalid_sidecar, QJsonDocument(invalid_root).toJson()),
        "写入缺 id 的标记文件");

  MarkStore invalid =
      MarkStore::Open(ToUtf8(invalid_video), 1'000'000, invalid_options);
  Check(invalid.marks().empty(), "缺 id 的标记文件按损坏处理");
  Check(QFileInfo::exists(invalid_sidecar + QStringLiteral(".bak")),
        "缺 id 的文件已备份");
  Check(!QFileInfo::exists(invalid_sidecar), "缺 id 的原文件已移走");
  Check(HasWarning(invalid, MarkWarningKind::kCorruptBackup), "缺 id 有损坏警告");
}

void TestEmpty(const QString& root) {
  std::printf("\n-- 空文件清理 --\n");
  const QString directory = root + QStringLiteral("/empty");
  const QString video = NewVideo(directory, QStringLiteral("empty.mp4"));
  const MarkStoreOptions options =
      OptionsFor(root + QStringLiteral("/appdata-empty"));
  const MarkPaths paths = lapian::core::ResolveMarkPaths(ToUtf8(video), options);
  const QString sidecar = SidecarOf(paths);
  const QString fallback = FallbackOf(paths);
  const VideoIdentity identity =
      lapian::core::MakeVideoIdentity(ToUtf8(video), 1'000'000);

  MarkStore opened = MarkStore::Open(ToUtf8(video), 1'000'000, options);
  Check(!QFileInfo::exists(sidecar) && !QFileInfo::exists(fallback),
        "F6-A6：无标记打开不产生文件");

  Check(WriteAll(sidecar, QByteArray::fromStdString(
                              lapian::core::SerializeMarkFile(identity, {}))),
        "预置空 sidecar");
  Check(QFileInfo::exists(sidecar), "空 sidecar 存在");
  MarkStore emptied = MarkStore::Open(ToUtf8(video), 1'000'000, options);
  Check(emptied.marks().empty(), "空 sidecar 没有标记");
  Check(!QFileInfo::exists(sidecar), "打开后删除空 sidecar（F6-A6）");

  Check(WriteAll(sidecar, QByteArray::fromStdString(
                              lapian::core::SerializeMarkFile(identity, {}))),
        "再次预置空 sidecar");
  Check(WriteAll(fallback, QByteArray::fromStdString(
                               lapian::core::SerializeMarkFile(identity, {}))),
        "预置空回退文件");
  MarkStore both = MarkStore::Open(ToUtf8(video), 1'000'000, options);
  Check(both.marks().empty(), "两个空文件都按无标记处理");
  Check(!QFileInfo::exists(sidecar) && !QFileInfo::exists(fallback),
        "打开后删除两个空附属文件");
}

void TestReconcile(const QString& root) {
  std::printf("\n-- frame+pts 校正 --\n");
  FrameLookup lookup;
  lookup.frame_count = 10;
  lookup.pts_of_frame = [](std::int64_t frame, std::int64_t* pts) {
    if (frame < 0 || frame >= 10) {
      return false;
    }
    *pts = frame * 1'000'000;
    return true;
  };
  lookup.frame_of_pts = [](std::int64_t pts, std::int64_t* frame) {
    if (pts < 0 || pts % 1'000'000 != 0) {
      return false;
    }
    const std::int64_t candidate = pts / 1'000'000;
    if (candidate < 0 || candidate >= 10) {
      return false;
    }
    *frame = candidate;
    return true;
  };

  std::vector<Mark> marks = {
      MakeMark("a", 5, 6'000'000, "A", "", "2026-01-01T00:00:00.000Z"),
      MakeMark("b", 5, 5'000'000, "B", "", "2026-01-01T00:00:00.000Z"),
      MakeMark("c", 5, 9'999'999, "C", "", "2026-01-01T00:00:00.000Z"),
      MakeMark("d", 20, 2'000'000, "D", "", "2026-01-01T00:00:00.000Z"),
      MakeMark("e", 20, 19'000'000, "E", "", "2026-01-01T00:00:00.000Z"),
  };
  std::vector<MarkWarning> warnings;
  lapian::core::ReconcileMarks(&marks, lookup, &warnings);

  const Mark* a = FindIn(marks, "a");
  Check(a != nullptr && a->frame == 6 && a->pts_us == 6'000'000,
        "PTS 可定位：按 PTS 校正帧号并更新 pts");
  const Mark* b = FindIn(marks, "b");
  Check(b != nullptr && b->frame == 5 && b->pts_us == 5'000'000,
        "frame/pts 一致：不改变");
  const Mark* c = FindIn(marks, "c");
  Check(c != nullptr && c->frame == 5 && c->pts_us == 5'000'000,
        "PTS 不可定位：保留帧号并更新 pts");
  const Mark* d = FindIn(marks, "d");
  Check(d != nullptr && d->frame == 2 && d->pts_us == 2'000'000,
        "帧号越界但 PTS 可定位：按 PTS 校正");
  const Mark* e = FindIn(marks, "e");
  Check(e != nullptr && e->frame == 20 && e->pts_us == 19'000'000,
        "帧号与 PTS 都无法校验：保留原值");
  Check(warnings.size() == 4, "四条不一致标记各产生一条提示");
  bool all_pts_warnings = true;
  for (const MarkWarning& warning : warnings) {
    all_pts_warnings = all_pts_warnings && warning.kind == MarkWarningKind::kPtsMismatch;
  }
  Check(all_pts_warnings, "提示类型为 kPtsMismatch");

  const QString directory = root + QStringLiteral("/vfr");
  const QString video = NewVideo(directory, QStringLiteral("vfr.mp4"));
  const MarkStoreOptions options =
      OptionsFor(root + QStringLiteral("/appdata-vfr"));
  const MarkPaths paths = lapian::core::ResolveMarkPaths(ToUtf8(video), options);
  const QString sidecar = SidecarOf(paths);
  const VideoIdentity identity =
      lapian::core::MakeVideoIdentity(ToUtf8(video), 10'000'000);
  Check(WriteAll(sidecar, QByteArray::fromStdString(lapian::core::SerializeMarkFile(
                              identity,
                              {MakeMark("x", 5, 6'000'000, "X", "",
                                        "2026-01-01T00:00:00.000Z")}))),
        "写入 frame/pts 不一致的 sidecar");

  MarkStore corrected = MarkStore::Open(ToUtf8(video), 10'000'000, options, lookup);
  Check(corrected.marks().size() == 1 &&
            corrected.marks()[0].frame == 6 &&
            corrected.marks()[0].pts_us == 6'000'000,
        "打开时按 PTS 校正");
  Check(HasWarning(corrected, MarkWarningKind::kPtsMismatch), "校正时给出提示");

  MarkStore reopened = MarkStore::Open(ToUtf8(video), 10'000'000, options);
  Check(reopened.marks().size() == 1 && reopened.marks()[0].frame == 6 &&
            reopened.marks()[0].pts_us == 6'000'000,
        "校正结果已写回 sidecar");
}

void TestFallbackAndMigration(const QString& root) {
  std::printf("\n-- 只读 sidecar 回退 / 迁回 --\n");
  const QString directory = root + QStringLiteral("/fallback");
  const QString video = NewVideo(directory, QStringLiteral("readonly.mp4"));
  const MarkStoreOptions options =
      OptionsFor(root + QStringLiteral("/appdata-fallback"));
  const MarkPaths paths = lapian::core::ResolveMarkPaths(ToUtf8(video), options);
  const QString sidecar = SidecarOf(paths);
  const QString fallback = FallbackOf(paths);
  const VideoIdentity identity =
      lapian::core::MakeVideoIdentity(ToUtf8(video), 5'000'000);
  const Mark a =
      MakeMark("a", 10, 1000, "A", "", "2026-01-01T00:00:00.000Z");
  Check(WriteAll(sidecar, QByteArray::fromStdString(
                              lapian::core::SerializeMarkFile(identity, {a}))),
        "写入初始 sidecar");

  const QFile::Permissions original_permissions = QFileInfo(directory).permissions();
  Check(QFile::setPermissions(directory, QFile::ReadOwner | QFile::ReadGroup |
                                           QFile::ReadOther | QFile::ExeOwner |
                                           QFile::ExeGroup | QFile::ExeOther),
        "设置 sidecar 只读（模拟源目录不可写）");

  MarkStore store = MarkStore::Open(ToUtf8(video), 5'000'000, options);
  Check(store.marks().size() == 1, "只读 sidecar 仍可读");
  Check(store.AddMark(20, 2000, "B", "", nullptr), "添加第二个标记");
  const SaveResult saved = store.Save();
  Check(saved.ok && saved.wrote, "sidecar 不可写时保存成功");
  CheckEq(saved.path, paths.fallback, "保存到 AppData 回退");
  Check(store.using_fallback(), "using_fallback=true");
  Check(QFileInfo::exists(fallback), "回退文件存在");
  Check(QFileInfo::exists(sidecar), "只读 sidecar 保留");
  Check(!QFileInfo::exists(sidecar + QStringLiteral(".bak")),
        "只读不是损坏，不产生 .bak");
  Check(HasWarning(store, MarkWarningKind::kMigrationFailed),
        "未能删除旧 sidecar 时有迁移提示");

  MarkStore reopened = MarkStore::Open(ToUtf8(video), 5'000'000, options);
  Check(reopened.marks().size() == 2, "重开合并只读 sidecar 与回退");
  Check(reopened.using_fallback(), "只读时仍使用回退");
  Check(HasWarning(reopened, MarkWarningKind::kMigrationFailed),
        "重开时仍提示旧 sidecar 未删除");

  Check(QFile::setPermissions(directory, original_permissions),
        "恢复 sidecar 可写（模拟源目录恢复）");
  MarkStore migrated = MarkStore::Open(ToUtf8(video), 5'000'000, options);
  Check(migrated.marks().size() == 2, "迁回后标记数量不变");
  Check(!migrated.using_fallback(), "迁回 sidecar");
  CheckEq(migrated.storage_path(), paths.sidecar, "实际存储位置为 sidecar");
  Check(!QFileInfo::exists(fallback), "迁回后删除回退文件");
  Check(QFileInfo::exists(sidecar), "迁回后 sidecar 存在");

  MarkStore final_store = MarkStore::Open(ToUtf8(video), 5'000'000, options);
  Check(final_store.marks().size() == 2, "迁回结果可重开");
  Check(!HasWarning(final_store, MarkWarningKind::kMigrationFailed),
        "迁回成功后没有迁移提示");
}

void TestAtomicFailure(const QString& root) {
  std::printf("\n-- 原子写失败保护 --\n");
  const QString directory = root + QStringLiteral("/atomic");
  const QString video = NewVideo(directory, QStringLiteral("atomic.mp4"));
  const QString appdata = root + QStringLiteral("/appdata-atomic");
  QDir().mkpath(appdata);
  // 用普通文件占住 AppData 的 marks 目录，使回退写也失败。
  Check(WriteAll(appdata + QStringLiteral("/marks"), QByteArrayLiteral("blocked")),
        "创建阻止回退目录的文件");
  const MarkStoreOptions options = OptionsFor(appdata);
  const MarkPaths paths = lapian::core::ResolveMarkPaths(ToUtf8(video), options);
  const QString sidecar = SidecarOf(paths);

  MarkStore store = MarkStore::Open(ToUtf8(video), 5'000'000, options);
  Check(store.AddMark(10, 1000, "A", "", nullptr), "添加标记 A");
  Check(store.Save().ok, "首次保存到 sidecar");
  const QByteArray before = ReadAll(sidecar);
  Check(!before.isEmpty(), "旧 sidecar 非空");
  Check(store.AddMark(20, 2000, "B", "", nullptr), "添加标记 B");

  const QFile::Permissions original_permissions = QFileInfo(directory).permissions();
  Check(QFile::setPermissions(directory, QFile::ReadOwner | QFile::ReadGroup |
                                           QFile::ReadOther | QFile::ExeOwner |
                                           QFile::ExeGroup | QFile::ExeOther),
        "设置 sidecar 目录只读");
  const SaveResult failed = store.Save();
  Check(!failed.ok, "sidecar 目录只读且回退不可写时保存失败");
  Check(!failed.error.empty(), "保存失败返回错误信息");
  Check(QFile::setPermissions(directory, original_permissions),
        "恢复 sidecar 目录可写");
  Check(ReadAll(sidecar) == before, "失败不破坏旧 sidecar");
  const QStringList leftovers =
      QDir(directory).entryList(QStringList() << QStringLiteral("*.tmp-*"),
                                QDir::Files);
  Check(leftovers.isEmpty(), "失败后清理同目录临时文件");

  const SaveResult retried = store.Save();
  Check(retried.ok && retried.wrote, "解锁后可重新保存");
  const QJsonDocument document =
      QJsonDocument::fromJson(ReadAll(sidecar));
  Check(document.object().value(QStringLiteral("marks")).toArray().size() == 2,
        "重新保存后包含两个标记");
}

void TestLongPathAtomicWrite(const QString& root) {
  std::printf("\n-- >MAX_PATH 中文长路径 sidecar --\n");
  QString directory = root + QStringLiteral("/long");
  const QString segment = QStringLiteral("很长的中文目录名称_用于测试长路径");
  int index = 0;
  while (directory.size() < 290) {
    directory += QLatin1Char('/') + segment + QString::number(index++);
  }
  const QString video = NewVideo(directory, QStringLiteral("长路径.mp4"));
  Check(video.size() > 260, "测试视频路径 >260 字符");

  const MarkStoreOptions options = OptionsFor(root + QStringLiteral("/appdata-long"));
  const MarkPaths paths = ResolveMarkPaths(ToUtf8(video), options);
  const QString sidecar = FromUtf8(paths.sidecar);
  const QString fallback = FromUtf8(paths.fallback);
  MarkStore store = MarkStore::Open(ToUtf8(video), 5'000'000, options);
  Check(store.valid(), "长路径 MarkStore 有效");
  Check(store.AddMark(3, 3000, "长路径", "note", nullptr), "长路径添加标记");
  const SaveResult saved = store.Save();
  Check(saved.ok, "长路径 sidecar 保存成功");
  Check(saved.wrote, "长路径 sidecar 确实写入");
  Check(!saved.path.empty() && FromUtf8(saved.path) == sidecar,
        "写入 sidecar 而不是 AppData 回退");
  Check(!store.using_fallback(), "长路径不标记为 AppData 回退");
  Check(QFileInfo::exists(sidecar), "长路径 sidecar 文件存在");
  Check(!QFileInfo::exists(fallback), "长路径没有产生 AppData 回退文件");

  MarkStore reopened = MarkStore::Open(ToUtf8(video), 5'000'000, options);
  Check(reopened.marks().size() == 1, "重开读回长路径 sidecar");
  Check(!reopened.using_fallback(), "重开仍使用 sidecar");
}

void TestIdentityMismatch(const QString& root) {
  std::printf("\n-- 视频标识不一致 --\n");
  const QString directory = root + QStringLiteral("/identity");
  const QString video = NewVideo(directory, QStringLiteral("current.mp4"));
  const QString other = NewVideo(directory, QStringLiteral("other.mp4"));
  const MarkStoreOptions options =
      OptionsFor(root + QStringLiteral("/appdata-identity"));
  const MarkPaths paths = lapian::core::ResolveMarkPaths(ToUtf8(video), options);
  const QString sidecar = SidecarOf(paths);

  const VideoIdentity other_identity =
      lapian::core::MakeVideoIdentity(ToUtf8(other), 999'999);
  Check(WriteAll(sidecar, QByteArray::fromStdString(lapian::core::SerializeMarkFile(
                              other_identity,
                              {MakeMark("m", 3, 3000, "M", "",
                                        "2026-01-01T00:00:00.000Z")}))),
        "写入标识来自另一视频的 sidecar");

  MarkStore store = MarkStore::Open(ToUtf8(video), 5'000'000, options);
  Check(store.marks().size() == 1, "标识不一致仍按文件位置加载");
  Check(HasWarning(store, MarkWarningKind::kIdentityMismatch),
        "标识不一致给出警告");
  Check(QFileInfo::exists(sidecar), "不自动覆盖标识不一致的 sidecar");
}

QJsonObject ReadJsonObject(const QString& path) {
  return QJsonDocument::fromJson(ReadAll(path)).object();
}

bool WriteJsonObject(const QString& path, const QJsonObject& object) {
  return WriteAll(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
}

// W1：工作区 A/B 按视频记忆——路径派生（本地 sidecar / AppData 回退 / 在线）。
void TestWorkRangePaths(const QString& root) {
  std::printf("\n-- W1 工作区路径派生 --\n");
  const QString directory = root + QStringLiteral("/workrange/paths");
  const QString video = NewVideo(directory, QStringLiteral("中文 视频.mp4"));
  const QString other = NewVideo(directory, QStringLiteral("另一个.mp4"));
  const MarkStoreOptions options =
      OptionsFor(root + QStringLiteral("/appdata-workrange"));
  const MarkPaths mark_paths = ResolveMarkPaths(ToUtf8(video), options);
  const WorkRangePaths paths = ResolveWorkRangePaths(ToUtf8(video), options);

  const std::string normalized_video =
      lapian::core::MakeVideoIdentity(ToUtf8(video), 0).path;
  CheckEq(paths.sidecar, normalized_video + ".lapian-workrange.json",
          "W1：sidecar = 视频完整名 + .lapian-workrange.json");
  CheckEq(paths.path_hash, mark_paths.path_hash, "W1：path_hash 与标记 sidecar 一致");
  CheckEq(paths.fallback,
          ToUtf8(root + QStringLiteral("/appdata-workrange/marks/") +
                 FromUtf8(paths.path_hash) + QStringLiteral(".workrange.json")),
          "W1：回退 = marks 目录/<hash>.workrange.json");
  Check(paths.sidecar != FromUtf8(mark_paths.sidecar).toStdString(),
        "W1：工作区文件与标记 sidecar 分开");

  const WorkRangePaths other_paths = ResolveWorkRangePaths(ToUtf8(other), options);
  Check(other_paths.sidecar != paths.sidecar &&
            other_paths.fallback != paths.fallback,
        "W1：不同视频路径与回退都不同（换视频不串）");

  const std::string online = lapian::core::OnlineWorkRangePath(
      ToUtf8(root + QStringLiteral("/online-records")), "BV1BMnv6jE5S",
      42915922022LL);
  CheckEq(online,
          ToUtf8(root + QStringLiteral(
                          "/online-records/BV1BMnv6jE5S-42915922022"
                          ".lapian-workrange.json")),
          "W1：在线工作区 = <bvid>-<cid>.lapian-workrange.json");
  Check(lapian::core::OnlineWorkRangePath(ToUtf8(root), "bad/../bvid", 1).empty(),
        "W1：非法 bvid 不派生在线路径");
  Check(lapian::core::OnlineWorkRangePath(ToUtf8(root), "BV1", 0).empty(),
        "W1：cid<=0 不派生在线路径");

  // >MAX_PATH 中文长目录：路径派生不能炸。
  QString long_dir = root + QStringLiteral("/workrange/long");
  int index = 0;
  while (long_dir.size() < 280) {
    long_dir += QLatin1Char('/') + QStringLiteral("很长的中文目录名称_工作区") +
                QString::number(index++);
  }
  const QString long_video = NewVideo(long_dir, QStringLiteral("长路径.mp4"));
  Check(long_video.size() > 260, "W1：长路径视频 >260 字符");
  const WorkRangePaths long_paths =
      ResolveWorkRangePaths(ToUtf8(long_video), options);
  const std::string expected_suffix = ".lapian-workrange.json";
  Check(long_paths.sidecar.size() >= expected_suffix.size() &&
            long_paths.sidecar.compare(long_paths.sidecar.size() - expected_suffix.size(),
                                       expected_suffix.size(), expected_suffix) == 0,
        "W1：长路径 sidecar 后缀正确");
  WorkRangeStore long_store = WorkRangeStore::Open(ToUtf8(long_video), options);
  Check(long_store.valid() && !long_store.has_record(), "W1：长路径打开不崩、无记录");
}

// W1：JSON 序列化/解析（roundtrip、损坏、空、schema 更高、非法区间）。
void TestWorkRangeRoundtrip() {
  std::printf("\n-- W1 工作区 roundtrip / 坏值 --\n");
  WorkRangeRecord record;
  record.kind = WorkRangeKind::kLocal;
  record.id = "c:\\视频\\测试.mp4";
  record.size = 12345;
  record.mtime_ms = 1690000000000LL;
  record.frames = 3000;
  record.range = WorkRange{100, 160};
  record.updated_at = "2026-10-27T00:00:00.000Z";

  const std::string json = lapian::core::SerializeWorkRangeFile(record);
  WorkRangeRecord parsed;
  std::string error;
  Check(lapian::core::ParseWorkRangeFile(json, &parsed, &error),
        "W1：serialize/parse roundtrip");
  Check(parsed.id == record.id, "W1：id roundtrip");
  Check(parsed.kind == WorkRangeKind::kLocal, "W1：kind roundtrip local");
  CheckEq64(static_cast<std::int64_t>(parsed.size),
            static_cast<std::int64_t>(record.size), "W1：size roundtrip");
  CheckEq64(parsed.mtime_ms, record.mtime_ms, "W1：mtime_ms roundtrip");
  CheckEq64(parsed.frames, 3000, "W1：frames roundtrip");
  Check(parsed.range == record.range, "W1：in/out roundtrip");
  CheckEq(parsed.updated_at, record.updated_at, "W1：updated_at roundtrip");

  const QJsonObject object = QJsonDocument::fromJson(
                                 QByteArray::fromStdString(json)).object();
  CheckEq64(object.value(QStringLiteral("schema")).toInteger(), 1,
            "W1：schema=1");
  CheckEq64(object.value(QStringLiteral("in")).toInteger(), 100, "W1：JSON in");
  CheckEq64(object.value(QStringLiteral("out")).toInteger(), 160, "W1：JSON out");
  CheckEq(object.value(QStringLiteral("kind")).toString().toUtf8().toStdString(),
          "local", "W1：JSON kind=local");

  WorkRangeRecord online = record;
  online.kind = WorkRangeKind::kOnline;
  online.id = "BV1BMnv6jE5S-42915922022";
  online.size = 0;
  online.mtime_ms = 0;
  Check(lapian::core::ParseWorkRangeFile(
            lapian::core::SerializeWorkRangeFile(online), &parsed, &error),
        "W1：在线记录 roundtrip");
  Check(parsed.kind == WorkRangeKind::kOnline && parsed.id == online.id,
        "W1：在线 kind/id roundtrip");

  Check(!lapian::core::ParseWorkRangeFile("", &parsed, &error), "W1：空内容无效");
  Check(!lapian::core::ParseWorkRangeFile("{", &parsed, &error), "W1：损坏 JSON 无效");
  Check(!lapian::core::ParseWorkRangeFile("[]", &parsed, &error),
        "W1：根不是对象无效");

  auto modified = [&](const char* key, const QJsonValue& value) {
    QJsonObject copy = object;
    copy.insert(QString::fromLatin1(key), value);
    return QJsonDocument(copy).toJson().toStdString();
  };
  auto removed = [&](const char* key) {
    QJsonObject copy = object;
    copy.remove(QString::fromLatin1(key));
    return QJsonDocument(copy).toJson().toStdString();
  };
  bool unsupported = false;
  Check(!lapian::core::ParseWorkRangeFile(modified("schema", 2), &parsed, &error,
                                          &unsupported) &&
            unsupported,
        "W1：schema 更高 = 无记录且禁止覆盖");
  unsupported = false;
  Check(!lapian::core::ParseWorkRangeFile(modified("schema", 0), &parsed, &error,
                                          &unsupported) &&
            !unsupported,
        "W1：schema=0 无效");
  Check(!lapian::core::ParseWorkRangeFile(modified("in", -1), &parsed, &error),
        "W1：in<0 无效");
  Check(!lapian::core::ParseWorkRangeFile(modified("out", 50), &parsed, &error),
        "W1：out<in 无效");
  Check(!lapian::core::ParseWorkRangeFile(removed("frames"), &parsed, &error),
        "W1：frames 缺失无效");
  Check(!lapian::core::ParseWorkRangeFile(removed("in"), &parsed, &error),
        "W1：in 缺失无效");
  Check(!lapian::core::ParseWorkRangeFile(removed("id"), &parsed, &error),
        "W1：id 缺失无效");
  Check(!lapian::core::ParseWorkRangeFile(modified("kind", "bogus"), &parsed,
                                          &error),
        "W1：kind 非法无效");
  Check(!lapian::core::ParseWorkRangeFile(modified("in", QStringLiteral("100")),
                                          &parsed, &error),
        "W1：in 不是整数无效");
}

// W1：指纹 + 取值校验 / 按当前帧数 clamp。
void TestWorkRangeValidate() {
  std::printf("\n-- W1 工作区校验 / clamp --\n");
  WorkRangeRecord record;
  record.kind = WorkRangeKind::kLocal;
  record.id = "c:\\video\\a.mp4";
  record.size = 123;
  record.mtime_ms = 1'690'000'000'000LL;
  record.frames = 100;
  record.range = WorkRange{10, 90};

  WorkRange out;
  Check(lapian::core::ValidateWorkRange(record, WorkRangeKind::kLocal, record.id,
                                        123, record.mtime_ms, 100, &out, nullptr),
        "W1：指纹一致通过");
  Check(out == (WorkRange{10, 90}), "W1：范围原样返回");
  Check(!lapian::core::ValidateWorkRange(record, WorkRangeKind::kLocal, record.id,
                                         124, record.mtime_ms, 100, &out, nullptr),
        "W1：size 不符 = 无记录");
  Check(!lapian::core::ValidateWorkRange(record, WorkRangeKind::kLocal, record.id,
                                         123, record.mtime_ms + 5000, 100, &out,
                                         nullptr),
        "W1：mtime 差 >2000ms = 无记录");
  Check(lapian::core::ValidateWorkRange(record, WorkRangeKind::kLocal, record.id,
                                        123, record.mtime_ms + 1500, 100, &out,
                                        nullptr),
        "W1：mtime 差 ≤2000ms 仍有效");
  Check(!lapian::core::ValidateWorkRange(record, WorkRangeKind::kOnline,
                                         record.id, 123, record.mtime_ms, 100,
                                         &out, nullptr),
        "W1：kind 不符 = 无记录");
  Check(!lapian::core::ValidateWorkRange(record, WorkRangeKind::kLocal, record.id,
                                         123, record.mtime_ms, 0, &out, nullptr),
        "W1：帧数未知 = 无记录");

  record.range = WorkRange{10, 900};
  Check(lapian::core::ValidateWorkRange(record, WorkRangeKind::kLocal, record.id,
                                        123, record.mtime_ms, 100, &out, nullptr) &&
            out == (WorkRange{10, 99}),
        "W1：out 超 frames → clamp 到末帧");
  record.range = WorkRange{500, 600};
  Check(lapian::core::ValidateWorkRange(record, WorkRangeKind::kLocal, record.id,
                                        123, record.mtime_ms, 300, &out, nullptr) &&
            out == (WorkRange{299, 299}),
        "W1：in/out 都超新帧数 → 双 clamp 仍 out>=in");

  WorkRangeRecord online;
  online.kind = WorkRangeKind::kOnline;
  online.id = "BV1BMnv6jE5S-42915922022";
  online.frames = 100;
  online.range = WorkRange{10, 20};
  Check(lapian::core::ValidateWorkRange(online, WorkRangeKind::kOnline, online.id,
                                        0, 0, 100, &out, nullptr),
        "W1：在线 bvid/cid 一致通过");
  Check(!lapian::core::ValidateWorkRange(online, WorkRangeKind::kOnline,
                                         "BV1BMnv6jE5S-1", 0, 0, 100, &out,
                                         nullptr),
        "W1：在线 bvid/cid 不符 = 无记录");
}

// W1：store 原子写/读回/覆盖/删除/指纹不符/只读回退/在线。
void TestWorkRangeStore(const QString& root) {
  std::printf("\n-- W1 工作区 store（原子写/回退/在线） --\n");
  const QString directory = root + QStringLiteral("/workrange/store");
  const QString video = NewVideo(directory, QStringLiteral("存储.mp4"));
  const MarkStoreOptions options =
      OptionsFor(root + QStringLiteral("/appdata-workrange-store"));
  const WorkRangePaths paths = ResolveWorkRangePaths(ToUtf8(video), options);
  const QString sidecar = FromUtf8(paths.sidecar);
  const QString fallback = FromUtf8(paths.fallback);
  const std::int64_t video_size = ReadAll(video).size();
  const std::int64_t mtime_ms =
      QFileInfo(video).lastModified().toMSecsSinceEpoch();

  WorkRangeStore store = WorkRangeStore::Open(ToUtf8(video), options);
  Check(store.valid(), "W1：本地 store 有效");
  Check(!store.has_record(), "W1：初始无记录");
  CheckEq(store.path(), paths.sidecar, "W1：path 指向 sidecar");
  WorkRange out;
  Check(!store.Restore(120, &out), "W1：无记录 Restore=false");

  std::string saved_path;
  std::string error;
  Check(store.Save(WorkRange{100, 160}, 300, &saved_path, &error),
        "W1：保存工作区");
  CheckEq(saved_path, paths.sidecar, "W1：保存到 sidecar");
  Check(QFileInfo::exists(sidecar), "W1：sidecar 文件存在");
  Check(!QFileInfo::exists(fallback), "W1：未写回退");
  Check(store.has_record(), "W1：保存后 has_record");

  const QJsonObject saved = ReadJsonObject(sidecar);
  CheckEq64(saved.value(QStringLiteral("schema")).toInteger(), 1, "W1：落盘 schema=1");
  CheckEq64(saved.value(QStringLiteral("in")).toInteger(), 100, "W1：落盘 in");
  CheckEq64(saved.value(QStringLiteral("out")).toInteger(), 160, "W1：落盘 out");
  CheckEq(saved.value(QStringLiteral("kind")).toString().toUtf8().toStdString(),
          "local", "W1：落盘 kind=local");
  CheckEq(saved.value(QStringLiteral("id")).toString().toUtf8().toStdString(),
          store.id(), "W1：落盘 id=path_key");
  CheckEq64(saved.value(QStringLiteral("size")).toInteger(), video_size,
            "W1：落盘 size");
  const std::int64_t saved_mtime =
      saved.value(QStringLiteral("mtime_ms")).toInteger();
  Check(std::llabs(saved_mtime - mtime_ms) <= 2000, "W1：落盘 mtime 为视频 mtime");
  CheckEq64(saved.value(QStringLiteral("frames")).toInteger(), 300,
            "W1：落盘 frames");
  Check(!saved.value(QStringLiteral("updated_at")).toString().isEmpty(),
        "W1：updated_at 非空");
  Check(store.Restore(300, &out) && out == (WorkRange{100, 160}),
        "W1：Restore 读回 A/B");

  WorkRangeStore reopened = WorkRangeStore::Open(ToUtf8(video), options);
  Check(reopened.has_record(), "W1：重开有记录");
  Check(reopened.Restore(300, &out) && out == (WorkRange{100, 160}),
        "W1：重开恢复 A/B");
  Check(!reopened.using_fallback(), "W1：重开用 sidecar");
  Check(reopened.Restore(120, &out) && out == (WorkRange{100, 119}),
        "W1：帧数变小时按新帧数 clamp B");
  Check(reopened.Restore(2000, &out) && out == (WorkRange{100, 160}),
        "W1：帧数变大不变");

  Check(reopened.Save(WorkRange{0, 50}, 300, &saved_path, &error),
        "W1：覆盖保存");
  WorkRangeStore overwritten = WorkRangeStore::Open(ToUtf8(video), options);
  Check(overwritten.Restore(300, &out) && out == (WorkRange{0, 50}),
        "W1：覆盖后读回");

  // 指纹不符（size 或 mtime 被改）：按无记录，不恢复。
  QJsonObject tampered = ReadJsonObject(sidecar);
  tampered.insert(QStringLiteral("size"), video_size + 1);
  Check(WriteJsonObject(sidecar, tampered), "W1：写入错误 size 的记录");
  WorkRangeStore wrong_size = WorkRangeStore::Open(ToUtf8(video), options);
  Check(!wrong_size.has_record(), "W1：size 不符不恢复");
  Check(!wrong_size.Restore(300, &out), "W1：size 不符 Restore=false");

  tampered = ReadJsonObject(sidecar);
  tampered.insert(QStringLiteral("mtime_ms"), saved_mtime - 60'000);
  Check(WriteJsonObject(sidecar, tampered), "W1：写入错误 mtime 的记录");
  WorkRangeStore wrong_mtime = WorkRangeStore::Open(ToUtf8(video), options);
  Check(!wrong_mtime.has_record(), "W1：mtime 不符不恢复");

  QJsonObject broken = ReadJsonObject(sidecar);
  broken.insert(QStringLiteral("out"), -5);
  Check(WriteJsonObject(sidecar, broken), "W1：写入 out<0 的记录");
  WorkRangeStore broken_store = WorkRangeStore::Open(ToUtf8(video), options);
  Check(!broken_store.has_record(), "W1：非法记录按无记录");

  Check(overwritten.Save(WorkRange{10, 20}, 300, &saved_path, &error),
        "W1：重新写入正常记录");
  Check(overwritten.Remove(&error), "W1：删除工作区文件");
  Check(!QFileInfo::exists(sidecar), "W1：sidecar 已删除");
  Check(!overwritten.has_record(), "W1：删除后 has_record=false");

  // sidecar 路径被目录占用 → 原子写失败 → 静默回退 AppData。
  Check(QDir().mkpath(sidecar), "W1：用目录占住 sidecar 路径（模拟不可写）");
  WorkRangeStore blocked = WorkRangeStore::Open(ToUtf8(video), options);
  Check(blocked.valid() && !blocked.has_record(), "W1：sidecar 被占用时仍可打开");
  Check(blocked.Save(WorkRange{30, 40}, 300, &saved_path, &error),
        "W1：sidecar 不可写时保存成功");
  CheckEq(saved_path, paths.fallback, "W1：回退到 AppData");
  Check(blocked.using_fallback(), "W1：标记为 AppData 回退");
  Check(QFileInfo::exists(fallback), "W1：回退文件存在");
  Check(QDir(sidecar).exists(), "W1：sidecar 目录未被破坏");
  WorkRangeStore from_fallback = WorkRangeStore::Open(ToUtf8(video), options);
  Check(from_fallback.has_record() && from_fallback.using_fallback(),
        "W1：重开读回退");
  Check(from_fallback.Restore(300, &out) && out == (WorkRange{30, 40}),
        "W1：回退恢复 A/B");
  // sidecar 路径仍被目录占用：Remove 对目录不删，整体返回 false（只按日志记录）。
  const bool removed_fallback = from_fallback.Remove(&error);
  Check(!removed_fallback && !error.empty(), "W1：目录占位时 Remove 报错但不删目录");
  Check(!QFileInfo::exists(fallback), "W1：回退文件已删除");
  Check(QDir(sidecar).exists(), "W1：目录不当文件删除");
  QDir().rmdir(sidecar);

  // 在线：<records>/<bvid>-<cid>.lapian-workrange.json。
  const QString records_root = root + QStringLiteral("/workrange/online-records");
  WorkRangeStore online =
      WorkRangeStore::OpenOnline("BV1BMnv6jE5S", 42915922022LL,
                                 ToUtf8(records_root));
  Check(online.valid() && !online.has_record(), "W1：在线 store 有效/初始无记录");
  Check(online.Save(WorkRange{10, 20}, 100, &saved_path, &error),
        "W1：在线保存成功");
  const std::string expected_online = lapian::core::OnlineWorkRangePath(
      ToUtf8(records_root), "BV1BMnv6jE5S", 42915922022LL);
  CheckEq(saved_path, expected_online, "W1：在线保存路径");
  Check(QFileInfo::exists(FromUtf8(saved_path)), "W1：在线文件存在");
  const QJsonObject online_json = ReadJsonObject(FromUtf8(saved_path));
  CheckEq(online_json.value(QStringLiteral("kind")).toString().toUtf8().toStdString(),
          "online", "W1：在线记录 kind=online");
  CheckEq(online_json.value(QStringLiteral("id")).toString().toUtf8().toStdString(),
          "BV1BMnv6jE5S-42915922022", "W1：在线记录 id=bvid-cid");

  WorkRangeStore online_reopened = WorkRangeStore::OpenOnline(
      "BV1BMnv6jE5S", 42915922022LL, ToUtf8(records_root));
  Check(online_reopened.has_record() &&
            online_reopened.Restore(100, &out) && out == (WorkRange{10, 20}),
        "W1：在线重开恢复 A/B");
  WorkRangeStore other_cid = WorkRangeStore::OpenOnline(
      "BV1BMnv6jE5S", 42915922023LL, ToUtf8(records_root));
  Check(!other_cid.has_record(), "W1：换 cid 不串");
  WorkRangeStore other_bvid = WorkRangeStore::OpenOnline(
      "BV1BMnv6jE5T", 42915922022LL, ToUtf8(records_root));
  Check(!other_bvid.has_record(), "W1：换 bvid 不串");
  Check(!WorkRangeStore::OpenOnline("bad/bvid", 1, ToUtf8(records_root)).valid(),
        "W1：非法 bvid 在线 store 无效");
  Check(online_reopened.Remove(&error), "W1：在线删除");
  Check(!QFileInfo::exists(FromUtf8(expected_online)), "W1：在线文件已删除");
}

// 手动入口（不注册到 ctest）：用真实只读目录验证 sidecar 写入失败后回退 AppData。
// 用法：设置 LAPIAN_MANUAL_VIDEO / LAPIAN_MANUAL_APPDATA 后运行
//       lapian_mark_store_test --manual-readonly
// 路径用环境变量传递，避免 Windows main() 的 ANSI argv 破坏中文路径。
int RunManualReadOnly(const std::string& video_utf8,
                      const std::string& appdata_marks_dir) {
  std::printf("T5.1 手动只读目录检查\n");
  MarkStoreOptions options;
  options.appdata_marks_dir = appdata_marks_dir;
  const MarkPaths paths = lapian::core::ResolveMarkPaths(video_utf8, options);

  MarkStore store = MarkStore::Open(video_utf8, 5'000'000, options);
  Check(store.valid(), "打开只读目录中的视频");
  Check(store.marks().empty(), "初始无标记");
  std::string id;
  Check(store.AddMark(12, 500'000, "只读目录", "fallback", &id), "添加标记");
  const SaveResult saved = store.Save();
  std::printf("save: ok=%d wrote=%d path=%s error=%s\n", saved.ok ? 1 : 0,
              saved.wrote ? 1 : 0, saved.path.c_str(), saved.error.c_str());
  for (const MarkWarning& warning : store.warnings()) {
    std::printf("warning[%d] %s (%s)\n", static_cast<int>(warning.kind),
                warning.message.c_str(), warning.path.c_str());
  }
  Check(saved.ok && saved.wrote, "保存成功");
  Check(store.using_fallback(), "保存到 AppData 回退");
  CheckEq(saved.path, paths.fallback, "实际保存路径为回退文件");
  Check(!QFileInfo(SidecarOf(paths)).isFile(), "只读目录没有生成 sidecar 文件");

  MarkStore reopened = MarkStore::Open(video_utf8, 5'000'000, options);
  Check(reopened.marks().size() == 1 && reopened.FindMark(id) != nullptr,
        "重开恢复标记");
  Check(reopened.using_fallback(), "重开仍使用回退");
  std::printf("storage_path=%s\n", reopened.storage_path().c_str());

  if (g_failures > 0) {
    std::printf("手动只读目录检查失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("手动只读目录检查通过\n");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--manual-readonly") {
    const QString video = qEnvironmentVariable("LAPIAN_MANUAL_VIDEO");
    const QString appdata = qEnvironmentVariable("LAPIAN_MANUAL_APPDATA");
    if (video.isEmpty() || appdata.isEmpty()) {
      std::printf(
          "缺少 LAPIAN_MANUAL_VIDEO / LAPIAN_MANUAL_APPDATA，无法做只读目录检查\n");
      return 2;
    }
    return RunManualReadOnly(video.toUtf8().toStdString(),
                             appdata.toUtf8().toStdString());
  }

  QTemporaryDir temp;
  if (!temp.isValid()) {
    std::printf("[FAIL] 无法创建临时目录\n");
    return 1;
  }

  std::printf("T5.1 sidecar 存储测试\n");
  TestPathNormalization(temp.path());
  TestRoundtrip(temp.path());
  TestMerge(temp.path());
  TestCorruptAndSchema(temp.path());
  TestEmpty(temp.path());
  TestReconcile(temp.path());
  TestFallbackAndMigration(temp.path());
  TestAtomicFailure(temp.path());
  TestLongPathAtomicWrite(temp.path());
  TestIdentityMismatch(temp.path());
  TestWorkRangePaths(temp.path());
  TestWorkRangeRoundtrip();
  TestWorkRangeValidate();
  TestWorkRangeStore(temp.path());

  if (g_failures > 0) {
    std::printf("\n失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("\n全部通过\n");
  return 0;
}
