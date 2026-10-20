// T5.1 sidecar 存储单测：路径规范化/哈希、roundtrip、合并、损坏备份、
// 空文件删除、AppData 回退与迁回、frame+pts 校正、原子写失败保护。
// 不依赖 FFmpeg/D3D11，只链接 Qt Core 与 lapian_marks。

#include <windows.h>

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
  Check(lapian::core::NormalizeVideoPath(canonical).find('/') == std::string::npos,
        "路径键使用反斜杠");
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

  const std::wstring sidecar_w = sidecar.toStdWString();
  const DWORD original_attributes = GetFileAttributesW(sidecar_w.c_str());
  Check(original_attributes != INVALID_FILE_ATTRIBUTES, "读取 sidecar 属性");
  Check(SetFileAttributesW(sidecar_w.c_str(),
                           original_attributes | FILE_ATTRIBUTE_READONLY) != 0,
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

  Check(SetFileAttributesW(sidecar_w.c_str(), original_attributes) != 0,
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

  const std::wstring sidecar_w = sidecar.toStdWString();
  HANDLE lock = CreateFileW(sidecar_w.c_str(), GENERIC_READ, 0, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  Check(lock != INVALID_HANDLE_VALUE, "以独占共享模式锁住 sidecar");
  const SaveResult failed = store.Save();
  Check(!failed.ok, "sidecar 锁定且回退不可写时保存失败");
  Check(!failed.error.empty(), "保存失败返回错误信息");
  if (lock != INVALID_HANDLE_VALUE) {
    CloseHandle(lock);
  }
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
  TestIdentityMismatch(temp.path());

  if (g_failures > 0) {
    std::printf("\n失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("\n全部通过\n");
  return 0;
}
