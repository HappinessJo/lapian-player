; 拉片软件 — Inno Setup 6 每用户安装脚本（T7.6）
;
; 由 scripts\package.ps1 调用；也可手工编译：
;   ISCC.exe /DAppVersion=0.1.0 /DAppVersionQuad=0.1.0.0 scripts\installer.iss
;
; 设计边界（spec §3.1 / §4.5、ADR-004/006）：
;   - PrivilegesRequired=lowest：只装当前用户，不要求管理员；
;   - 默认目录 {localappdata}\Programs\拉片软件；
;   - 注册 ProgID 与 .mp4/.mov/.mkv/.webm 的 OpenWithProgids，
;     不写扩展名默认值，不强制设为默认应用；
;   - 卸载只清程序文件/快捷方式/自己注册的注册表项；
;     不删除 %APPDATA%\拉片软件（settings/marks 回退）与视频旁 sidecar。

#ifndef AppName
  #define AppName "拉片软件"
#endif
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef AppVersionQuad
  #define AppVersionQuad "0.1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\out\package\stage"
#endif
#ifndef OutputDir
  #define OutputDir "..\out\package"
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "Lapian-" + AppVersion + "-setup"
#endif

#define AppExeName "lapian.exe"
#define AppPublisher "拉片软件"
#define AppProgId "Lapian.Video"

[Setup]
; AppId 固定，保证升级/卸载识别同一产品；Inno 用 {{ 表示字面量 {
AppId={{092E026C-D8FF-4E1E-9649-D3166C36A204}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19044
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#AppExeName}
VersionInfoVersion={#AppVersionQuad}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} 安装程序
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
; 安装/卸载全程无网络动作；不检查更新，不访问任何 URL。

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："

[Files]
; stage 由 scripts\package.ps1 组装：dist 的全部程序文件 + licenses\ + README.txt。
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; 升级时清掉旧版 Qt 插件目录，避免残留旧 DLL 与新版本混用。
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\styles"

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Comment: "帧级参考片审阅器"
Name: "{group}\第三方许可与源码说明"; Filename: "{app}\licenses"; Comment: "LGPL/GPL 文本与源码获取说明"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; ProgID：只声明“能打开”，不设置扩展名默认值。
Root: HKCU; Subkey: "Software\Classes\{#AppProgId}"; ValueType: string; ValueName: ""; ValueData: "拉片视频"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\{#AppProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExeName},0"
Root: HKCU; Subkey: "Software\Classes\{#AppProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" --file ""%1"""

; “打开方式”候选：Applications\lapian.exe + 支持的扩展名。
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}"; ValueType: string; ValueName: "ApplicationVersion"; ValueData: "{#AppVersion}"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" --file ""%1"""
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".mp4"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".mov"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".mkv"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".webm"; ValueData: ""

; 把 ProgID 加进各扩展名的“打开方式”候选；不碰默认值（用户确认后才成为默认）。
Root: HKCU; Subkey: "Software\Classes\.mp4\OpenWithProgids"; ValueType: string; ValueData: ""; ValueName: "{#AppProgId}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.mp4\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.mov\OpenWithProgids"; ValueType: string; ValueData: ""; ValueName: "{#AppProgId}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.mov\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.mkv\OpenWithProgids"; ValueType: string; ValueData: ""; ValueName: "{#AppProgId}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.mkv\OpenWithProgids"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.webm\OpenWithProgids"; ValueType: string; ValueData: ""; ValueName: "{#AppProgId}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.webm\OpenWithProgids"; Flags: uninsdeletekeyifempty

[Run]
Filename: "{app}\{#AppExeName}"; Description: "启动 {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 只删空目录；用户数据（%APPDATA%\拉片软件、视频旁 sidecar、snapshots）一律不碰。
Type: dirifempty; Name: "{app}"

