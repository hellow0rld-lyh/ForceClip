# ForceClip

> 突破限制，一键打字。ForceClip 能绕过那些禁止复制粘贴的网站和应用——**复制**后按快捷键，文字会自动"打"出来。

## 功能

- **突破粘贴限制** — 在禁止粘贴的输入框中，按 `Ctrl + Alt + Insert` 即可自动逐字输入剪贴板内容
- **系统托盘** — 后台静默运行，托盘图标右键菜单可打开设置或退出
- **自定义快捷键** — 两种设置方式：
  - **监听模式**：点击"监听"，直接按下你想要的热键组合
  - **手动输入**：勾选修饰键 + 填写按键名称
- **单例运行** — 重复启动时自动检测，可选择唤出旧实例或强制重启
- **开机自启动** — 在设置中勾选即可注册
- **配置持久化** — 快捷键和自启动状态保存在注册表，下次开机自动恢复

## 下载

从 [Releases](https://github.com/hellow0rld-lyh/autoInput/releases) 下载 `ForceClip.exe`，双击运行即可。

## 编译

### 依赖

- MinGW-w64（MSYS2）或 Visual Studio
- Windows SDK（`user32.lib`、`shell32.lib`）

### 编译命令

```bash
g++ forceclip.cpp -luser32 -lshell32 -o ForceClip.exe -O2 -s -mwindows
```

或双击 `build_cpp.bat`。

## 使用

| 操作 | 说明 |
|---|---|
| 运行 `ForceClip.exe` | 后台启动，托盘出现蓝色 F 图标 |
| `Ctrl + Alt + Insert` | 读取剪贴板 → 在当前光标处打出（突破粘贴限制）|
| 托盘图标右键 → 设置 | 打开设置窗口 |
| 托盘图标右键 → 退出 | 退出程序 |
| `Ctrl + Alt + Q` | 退出程序（默认退出快捷键） |

## 项目结构

```
ForceClip/
├── forceclip.cpp                # 主程序源码
├── clipboard_typer_debug.cpp    # 调试版（日志写到桌面）
├── build_cpp.bat                # 编译脚本
├── .gitignore
└── README.md
```

## 原理

1. `RegisterHotKey()` 注册全局热键
2. `WM_HOTKEY` 触发 → `OpenClipboard()` + `GetClipboardData(CF_UNICODETEXT)` 读取剪贴板
3. `SendInput()` + `KEYEVENTF_UNICODE` 逐字符模拟键盘输入（绕过网页/应用的 Ctrl+V 拦截）
4. 设置页面使用纯 Win32 API 创建，配置存储在 `HKCU\Software\ForceClip`

## 许可证

MIT
