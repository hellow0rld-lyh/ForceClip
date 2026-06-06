# Clipboard Typer

Windows 后台剪贴板打字工具。按下快捷键，自动读取剪贴板文本，在当前光标处逐字模拟键盘输入。

## 功能

- **快捷键粘贴** — 按 `Ctrl + Alt + Insert` 将剪贴板内容打出到当前光标位置
- **系统托盘** — 后台运行，托盘图标右键菜单可打开设置或退出
- **自定义快捷键** — 支持两种方式：
  - **监听模式**：点击"监听"按钮，直接按下你想要的热键组合
  - **手动输入**：勾选修饰键（Ctrl/Alt/Shift/Win）+ 填写按键名称
- **开机自启动** — 在设置中勾选即可注册
- **配置持久化** — 快捷键、自启动状态保存在注册表，下次启动自动恢复

## 下载

从 [Releases](https://github.com/hellow0rld-lyh/clipboard-typer/releases) 下载编译好的 `clipboard_typer.exe`，双击运行即可。

## 编译

### 依赖

- MinGW-w64（MSYS2）或 Visual Studio
- Windows SDK（`user32.lib`、`shell32.lib`）

### 编译命令

```bash
g++ clipboard_typer.cpp -luser32 -lshell32 -o clipboard_typer.exe -O2 -s -mwindows
```

或双击 `build_cpp.bat`。

## 使用

| 操作 | 说明 |
|---|---|
| 运行 `clipboard_typer.exe` | 后台启动，托盘出现蓝色 C 图标 |
| `Ctrl + Alt + Insert` | 读取剪贴板 → 在当前光标处打出 |
| 托盘图标右键 → 设置 | 打开设置窗口 |
| 托盘图标右键 → 退出 | 退出程序 |
| `Ctrl + Alt + Q` | 退出程序（默认退出快捷键） |

## 项目结构

```
clipboard-typer/
├── clipboard_typer.cpp           # 主程序源码
├── clipboard_typer_debug.cpp     # 调试版（日志写到桌面）
├── build_cpp.bat                 # 编译脚本
├── .gitignore
└── README.md
```

## 原理

1. `RegisterHotKey()` 注册全局热键
2. `WM_HOTKEY` 触发 → `OpenClipboard()` + `GetClipboardData(CF_UNICODETEXT)` 读取剪贴板
3. `SendInput()` + `KEYEVENTF_UNICODE` 逐字符模拟键盘输入
4. 设置页面使用纯 Win32 API 创建，配置存储在 `HKCU\Software\ClipboardTyper`
