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
;     不删除 %APPDATA%\拉片软件（settings/marks 回退）与视频旁 sidecar；
;   - T8.16：卸载时询问是否清理默认在线缓存，默认“否”；只有明确确认或
;     静默卸载带 /CLEARCACHE 才删除 %LOCALAPPDATA%\拉片软件\cache，
;     不碰在线记录/截图/设置/凭据/marks/sidecar。

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
; T8.16：Inno 卸载器自身静态导入 winhttp.dll（安装/卸载均不调用网络）。
; 把它放到 {app} 之外，使安装目录只含应用负载；应用负载导入表已断言无
; QtNetwork/TLS/winhttp/wininet/urlmon。卸载器仍由 Inno 正常管理。
UninstallFilesDir={localappdata}\拉片软件\uninstaller
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#AppExeName}
VersionInfoVersion={#AppVersionQuad}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} 安装程序
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
; 安装前展示隐私/数据位置说明（同时随程序目录安装为 PRIVACY.txt）。
InfoBeforeFile={#SourceDir}\PRIVACY.txt
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

[Code]
var
  DeleteOnlineCache: Boolean;

function ClearCacheRequested(): Boolean;
var
  I: Integer;
  Param: String;
  Value: String;
begin
  Result := False;

  // 显式参数优先：/CLEARCACHE 或 /CLEARCACHE=1；/CLEARCACHE=0 表示不清理。
  for I := 1 to ParamCount do
  begin
    Param := Uppercase(ParamStr(I));
    if (Param = '/CLEARCACHE') or (Param = '--CLEARCACHE') then
    begin
      Result := True;
      Exit;
    end;
    if (Param = '/CLEARCACHE=0') or (Param = '--CLEARCACHE=0') then
    begin
      Result := False;
      Exit;
    end;
    if (Copy(Param, 1, 12) = '/CLEARCACHE=') or
       (Copy(Param, 1, 13) = '--CLEARCACHE=') then
    begin
      Result := True;
      Exit;
    end;
  end;

  // 兼容 Inno 的 {param} 常量（无用户名/密码等敏感信息）。
  Value := ExpandConstant('{param:CLEARCACHE|0}');
  if (Value <> '') and (Value <> '0') then
    Result := True;
end;

function InitializeUninstall(): Boolean;
var
  Answer: Integer;
begin
  Result := True;
  DeleteOnlineCache := False;

  if ClearCacheRequested() then
  begin
    DeleteOnlineCache := True;
    Log('T8.16: /CLEARCACHE specified; default online cache will be removed.');
    Exit;
  end;

  if UninstallSilent then
  begin
    Log('T8.16: silent uninstall without /CLEARCACHE; online cache preserved.');
    Exit;
  end;

  Answer := MsgBox(
    '是否同时删除在线缓存？' + #13#10 + #13#10 +
    '默认选“否”，保留缓存。' + #13#10 +
    '选“是”只删除：' + #13#10 +
    ExpandConstant('{localappdata}\拉片软件\cache') + #13#10 + #13#10 +
    '不会删除在线记录、截图、设置、凭据、marks 或 sidecar。',
    mbConfirmation, MB_YESNO or MB_DEFBUTTON2);
  DeleteOnlineCache := (Answer = IDYES);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  CacheRoot: String;
begin
  // usPostUninstall 只在卸载主体完成后执行；用户取消或卸载失败不会走到这里。
  if (CurUninstallStep = usPostUninstall) and DeleteOnlineCache then
  begin
    CacheRoot := ExpandConstant('{localappdata}\拉片软件\cache');
    if DirExists(CacheRoot) then
    begin
      Log('T8.16: removing default online cache: ' + CacheRoot);
      if not DelTree(CacheRoot, True, True, True) then
        Log('T8.16: WARNING cache cleanup failed; cache left in place: ' + CacheRoot);
    end
    else
      Log('T8.16: default online cache not present; nothing to remove.');
  end;
end;

