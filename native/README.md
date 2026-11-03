# WinCal Native（主工程）

WinCal 的主工程是原生 Win32/C++ 版本。

运行时保持在一个原生进程内：Win32 窗口、托盘和任务栏拦截负责交互，Direct2D/DirectWrite 负责绘制，C++/WinRT 读取系统日历，WinHTTP 下载 ICS 并写入本地缓存。设置窗口按需创建，关闭后释放其字体、画刷和控件资源。

## 已实现

- 顶部独立的实时时钟与天气区域，显示秒、日期、农历、温度及天气图标；北京默认回退，支持自动定位和固定北京
- Win32 托盘图标与左键开关、右键菜单
- 点击 Windows 任务栏日期/时钟区域时拦截系统日历并显示 WinCal
- 再次点击托盘关闭面板，点击面板外部自动收起
- 无边框 DWM 圆角日历弹窗
- Direct2D/DirectWrite 日历绘制
- Direct2D 软件渲染目标：本机实测弹窗私有内存约 16–19 MB；硬件优先目标约 53 MB，因此不作为默认路径
- 单击日期、悬停反馈和轻量淡入动画
- 方向键切换月份/年份，鼠标滚轮切换月份；「整月翻滚」开启时新既单月整页上下滑动（相邻月按需即时渲染），关闭时按滚轮位移连续滚动，不吸附月份或周行（年切换仍为淡入）
- 每次打开恢复当前月份
- 读取现有设置并从磁盘 ICS 缓存即时展示事件点及“休/班”角标
- 使用 Windows 内置 ICU 显示中国农历与常见传统节日，正确处理闰月和腊月二十九除夕；需要 Windows 10 1903 或更新版本
- 独立获取中国大陆法定节假日与调休，显示节日名称及“休 / 班”角标，无需 ICS 订阅
- 选中日期有日程时，在日历旁显示独立浮动日程窗；空日期不显示日程窗，列表内滚轮只滚动日程
- 点击浮动日程窗中的列表项显示原生日程详情；日历关闭时日程窗一同隐藏
- 原生设置窗口：主题、实时文字大小预览、数据源、ICS 链接与刷新频率、周起始日、整月翻滚
- 设置保存后即时刷新日历外观和数据源；保留既有 JSON 设置与 ICS 别名的 Unicode 转义兼容
- 可选开机自启动，使用当前用户 Run 键并指向单体 EXE；已启用时自动从旧版可执行文件迁移
- 启动后按刷新间隔在后台更新 ICS，验证成功后原子替换缓存并重绘界面
- 展开常用 ICS 重复规则，支持 `COUNT` / `UNTIL` / `INTERVAL` / `BYDAY` / `BYMONTHDAY` / `BYMONTH`、`EXDATE` 与 `RDATE`
- 支持 `BYSETPOS`、`WKST` 和常见内嵌 `VTIMEZONE` 夏令时规则
- 将 UTC 及 IANA/Windows/内嵌 `TZID` 事件转换为系统本地时间
- 对多份 ICS 及系统日历结果做保守去重，节假日优先保留“休/班”标记
- 通过 C++/WinRT 按显示月份增量读取并缓存 Windows 系统日历，支持与 ICS 合并
- Per-Monitor DPI V2 与多显示器任务栏定位
- Explorer 重启后恢复托盘图标
- MSVC 静态运行库单体 EXE

## 待发布

- 发布前视觉与交互回归：托盘、任务栏四方向、双屏、DPI、主题、ICS/系统日历合并、日程详情面板
- 发布目录和版本信息整理

主工程已经可以独立构建和运行；上面两项是正式发布前的收尾工作，不影响原生工程作为默认开发入口。

## 构建

需要 Visual Studio 2022 C++ Build Tools（Desktop development with C++）。

```powershell
.\native\build.ps1 -Configuration Release
```

输出：`native/out/WinCal.exe`

用 `WinCal.exe --settings` 可以直接打开设置窗口；若已有 WinCal 实例，会复用该进程。

设置和缓存位置分别为 `%LOCALAPPDATA%\miniCal\settings.json` 与 `%LOCALAPPDATA%\WinCal\cache\`。数据源支持 `SystemCalendar`、`IcsUrl` 和 `Both`；ICS 刷新频率包括 10/30/60/120 分钟及 1 天（1440 分钟）。

设置页使用「外观 / 日历来源 / 常规」页签，不内嵌模拟日历。打开设置时同步打开真实日历并并排定位；主题、字号、周起始日立即更新真实日历。设置打开期间日历不会因点击设置而收起，保存后保留修改，取消或关闭设置会恢复原来的外观。开机启动和 ICS 订阅在保存后生效。字体和控件统一使用 DIP，切换显示器 DPI 时重新布局，工作区不足时提供滚动条。设置窗口仍然按需创建，关闭时释放字体、画刷和控件。

设置页离屏检查（不启动托盘、不读写用户配置、不访问日历服务）：

```powershell
cmake --build native/build --config Release --target WinCalSettingsPreview
# 参数：DPI、页签 0/1/2、深色 0/1、字号偏移、BMP 输出路径
.\native\build\Release\WinCalSettingsPreview.exe 120 0 0 7 native/out/settings-appearance.bmp
```

此目标仅在显式构建时生成，不包含在单体 EXE 中；同时检查字号边界、页签、主题、开关、订阅禁用，以及实时外观更新、取消恢复和保存保留逻辑。

`WinCalSettingsPreview` 是构建期的离屏检查工具，不是发布组件；正式运行只需要安装目录中的单体 `WinCal.exe`。

## 冒烟验证

```powershell
cmake --build native/build --config Release --target WinCalNativeSmoke
.\native\build\Release\WinCalNativeSmoke.exe
```

验证中国农历不为空，并确认当前用户的设置及 ICS 别名能够读取。

## 日历数据回归测试

```powershell
cmake --build native/build --config Release --target WinCalCalendarTests
ctest --test-dir native/build -C Release --output-on-failure
```

测试不访问用户配置或日历服务，覆盖跨午夜事件、结束时间边界、重复日程改期和取消、缓存到期、后台重复刷新及停止等待。
ICS 后台每分钟检查缓存是否达到配置的刷新间隔；系统日历快照有效期为一分钟，并定期重新读取当前显示月份。刷新失败保留上次成功的数据。

中国节假日使用第三方[提莫节假日接口](https://timor.tech/api/holiday/year/2026)，正式安排以国务院办公厅通知为准。当前浏览年份及相邻年份按需获取，缓存位于 `%LOCALAPPDATA%\WinCal\cache\holidays\年份.json`，有效期一天，失败后一小时重试并保留旧数据。尚未公布的年份不推算放假或补班，普通周末不额外标记。独立节假日数据优先于 ICS 标题识别，不添加个人日程或事件圆点。

数据回归还覆盖节假日解析、错误响应保留旧数据、Unicode 代理对及远年份重复日程；核心回归覆盖农历闰月、除夕和原子保存失败时保留旧配置。两组均由 CTest 执行，不访问用户配置或联网。可单独验证真实接口（需要联网）：

```powershell
.\native\build\Release\WinCalCalendarTests.exe --holiday-year 2026
```

如需离屏检查真实节假日角标，可把响应保存到构建目录后渲染（参数依次为 DPI、深色开关、翻页方向、起始年、起始月、数据文件、输出文件；下面从九月翻到十月）：

```powershell
.\native\build\Release\WinCalCalendarTests.exe --holiday-year 2026 native/build/holidays-2026.json
cmake --build native/build --config Release --target WinCalSettingsPreview
.\native\build\Release\WinCalSettingsPreview.exe --holidays 144 0 1 2026 9 native/build/holidays-2026.json native/build/holidays-2026.bmp
```

ICS 重复事件在浏览年份前两年至后十年的窗口内展开，跨年份浏览时从本地缓存重新展开；系统日历查询不再额外截断为一千条。

## 时钟与天气

顶部时钟使用系统本地时间，每秒更新；关闭日历时停止时钟计时器。右上角天气通过 Open-Meteo 获取：优先使用 Windows 位置服务（由系统控制权限），失败时使用 ipwho.is 网络大致位置，仍失败则回退北京。点击天气可以查看当前位置来源和更新时间、切换“自动定位 / 固定北京”、手动刷新；选择保存在 `WeatherAutoLocation` 设置项。

天气在独立后台线程中每 15 分钟更新，失败每 5 分钟重试。缓存位于 `%LOCALAPPDATA%\WinCal\cache\weather.json`，启动时最多使用一天内的缓存，过期或刷新失败的数据标为“缓存”，不把旧天气当作实时结果。网络定位受代理或 VPN 出口影响；Open-Meteo 会接收用于天气查询的经纬度。系统定位只显示“当前位置”，网络定位显示接口返回的城市名。

天气回归测试已注册到 CTest，默认仅使用固定数据和独立临时目录，不联网、不访问用户位置。可显式检查真实接口和定位：

```powershell
cmake --build native/build --config Release --target WinCalWeatherTests
.\native\build\Release\WinCalWeatherTests.exe --live-beijing
.\native\build\Release\WinCalWeatherTests.exe --live-auto
```

离屏日历预览使用固定的时间和北京天气数据，实际程序使用系统时间和联网结果。

## 年月选择与日程

顶部年月可以点击：第一次打开十二个月份面板，再点击年份打开十二年选择面板。标题旁的展开图标与翻页按钮分开排列，右侧箭头切换月份、年份或年份区间，最右侧的圆心图标返回今天。选好年份后选择月份即可跳转，Esc 关闭面板并保留原来月份。面板支持方向键移动、Enter 选择、PageUp/PageDown 翻页。

```powershell
cmake --build native/build --config Release --target WinCalSettingsPreview
.\native\build\Release\WinCalSettingsPreview.exe --check-picker
# DPI、深色开关、面板（0 月份 / 1 年份）、输出文件
.\native\build\Release\WinCalSettingsPreview.exe --picker 144 0 0 native/build/month-picker.bmp
```

日历保持固定高度，网格下方预留 48 DIP 留白；浮动日程窗与日历等高，不随日程数量改变高度。默认选中今天；选中日期有日程时自动显示浮动日程窗，空日期则隐藏。浮窗优先放在日历右侧，空间不足时放在左侧；两侧都不足时尝试上下放置并限制在当前显示器工作区内。每项显示标题及时间（全天事件显示“全天”），最多同时显示五项，滚轮查看更多，点击打开详情。点击外部或按 Esc 收起日历及日程窗。

离屏检查使用模拟事件，不读取用户设置、不连接日历服务：

```powershell
cmake --build native/build --config Release --target WinCalSettingsPreview
# DPI、深色开关、字号偏移、输出路径
.\native\build\Release\WinCalSettingsPreview.exe --agenda 144 1 10 native/out/agenda-dark.bmp
```

日程详情使用与日历一致的字体和配色，标题自动换行，卡片高度随内容调整；超长内容可用滚轮浏览。时间信息区显示同日时间范围、跨日开始与结束时间，全天事件的结束日期按实际最后一天展示。Esc 优先关闭详情，不收起日历。

```powershell
.\native\build\Release\WinCalSettingsPreview.exe --detail 144 1 10 native/out/detail-dark.bmp
```

连续滚动关闭整月翻滚后生效：滚轮位移可累积并随时反向，小幅滚轮输入保留小数行位移；月份标题跟随可见区域中部日期更新，点击日期不会跳回月首。重新打开日历时仍回到本月。离屏验证：

```powershell
.\native\build\Release\WinCalSettingsPreview.exe --check-continuous
.\native\build\Release\WinCalSettingsPreview.exe --continuous 96 0 1 2026 9 0.5 native/out/continuous.bmp
```

## 任务栏时钟扩展

`third_party/windhawk-clock` 包含 m417z 的 Taskbar Clock Customization 1.8.1 源码（GPL-3.0）。CMake 编译 `WinCalTaskbarClock.dll` 和无界面的 x86 宿主 `WinCalTaskbarHost.exe`，使用 Windhawk 1.7.3 引擎 DLL。引擎的会话入口需要 x86 宿主，Explorer 和时钟模块仍为 x64；不启动 Windhawk 管理器或编辑器。

扩展在系统时钟原位显示双行内容并为托盘让位。天气和农历来自 WinCal 的本机只读接口 `127.0.0.1:17864/wincal`，时间由扩展每秒更新。关闭任务栏时钟开关或退出 WinCal 后卸载扩展，恢复系统时钟。

Windhawk 1.7.3 x86/x64 引擎和时钟模块配置已随项目固定打包，`build.ps1` 会将它们安装到 `WinCal.exe` 旁的 `TaskbarExtension` 目录；无需单独下载或安装 Windhawk。运行目录需要允许当前用户写入，以便更新时钟配置。

“设置 → 常规 → 显示格式”支持两行模板、编辑预览与保存。采用软媒时间的月份/分钟约定，而非 .NET 格式：

| 标记 | 含义 |
| --- | --- |
| `yyyy` / `yy` | 四位 / 两位公历年 |
| `mm` / `m` / `dd` / `d` | 公历月、日，有 / 无前导零 |
| `HH` / `hh` / `ff` / `ss` | 24 小时 / 12 小时 / 分钟 / 秒；单字符版本不补零 |
| `ddd` / `dddd` | 周二 / 星期二 |
| `tt` / `TT` | AM、PM / 上午、下午 |
| `MD` / `M` / `D` | 农历月日 / 农历月 / 农历日 |
| `rtm` / `rhu` / `rwd` | 实时温度 / 湿度 / 风向与蒲福风级 |
| `we1`～`we7` | 今天起第 1～7 天的天气 |
| `tl1`～`tl7` / `th1`～`th7` | 对应日期的最低 / 最高温 |
| `pos` / `shichen` / `weather` / `icn` | 城市 / 时辰 / 天气与温度 / 天气符号 |
| `\n` | 两行分隔；也可在编辑框中按回车 |
| `'文字'` | 原样文字；`''` 输出单引号 |

缺少天气数据时显示 `—`。天气通常每 15 分钟更新，扩展每分钟读取最新天气字段。示例：

```text
yyyy-mm-dd HH:ff:ss rhu\nMD ddd rtm we2 rwd
```

标记含义核对了[软媒教程](https://www.win7china.com/html/22143.html)、[论坛使用讨论](https://www.52pojie.cn/thread-1162501-1-1.html)及软媒时间 3.2.0.0 的 `xmls/timeformat.xml` 帮助资源。下载包仅用于解包读取说明，未运行，产品不集成软媒程序或资源。
