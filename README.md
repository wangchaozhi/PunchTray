# PunchTray

A system-tray app for Beisen iTalent (北森 iTalent) attendance. It runs in the tray, calls the attendance `TableList` API on a timer, parses the "未打卡" (missed punch) records, and shows them in a frosted "liquid glass" card in the top-right corner of the screen.

- Left-click the tray icon to show or hide the card. Right-click for: 立即刷新 (refresh now), 设置 (settings, e.g. updating the Cookie), 打开配置文件 (open the config file), 开机自启动 (launch at login; Windows uses the HKCU Run key, macOS a LaunchAgent, Linux `~/.config/autostart`) and 退出 (quit).
- The tray icon shows a red badge with the number of missed punches, or an orange `!` when the query fails or the Cookie has expired.
- When a new missed punch appears, the card pops up automatically. For today's records it counts down to the punch time.

## Configuration

The config file is `config.json` in Qt's `AppConfigLocation`:

| Platform | Path |
| --- | --- |
| Windows | `%LOCALAPPDATA%\PunchTray\PunchTray\config.json` |
| macOS | `~/Library/Preferences/PunchTray/PunchTray/config.json` |
| Linux | `~/.config/PunchTray/PunchTray/config.json` |

Fields: `cookie`, `staffId`, `staffText`, `url`, `intervalMinutes`, `daysBack` (0 = today only), `autoPopup`.
When the Cookie expires, the server returns a 302 redirect to the login page. The card then shows "登录已失效" (session expired): copy a new Cookie from the browser and paste it into 设置 (settings).

## Build (Qt 6.5+)

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt>/6.x/<kit>
cmake --build build --config Release
```

On Windows, run `windeployqt --release build\Release\PunchTray.exe` afterwards. On macOS, use `macdeployqt`.

## Liquid glass

The card captures the desktop behind it, then blurs it, boosts its saturation and magnifies it slightly to look like refraction. It adds a rim highlight, a specular sheen and a gradient bevel on top. Text switches between light and dark based on the brightness of the background.

- **Windows 10 2004+:** the window is excluded from screen capture (`WDA_EXCLUDEFROMCAPTURE`), so the backdrop refreshes live while the card is visible. Set `PUNCHTRAY_CAPTURABLE=1` if you need to take a screenshot of the card.
- **macOS:** the backdrop is captured once each time the card is shown, which requires the "Screen Recording" permission. Without it, the card falls back to a semi-transparent gradient.
- **Linux X11:** same as macOS. On Wayland, the screen can't be captured, so the card uses the fallback gradient.
