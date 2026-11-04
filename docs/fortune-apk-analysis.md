# 易奇 Android APK 检查与 Python 移植

## 样本与范围

- 用户提供本地 `base.apk`，包名 `yiqi.bazi`，版本 `5.0.8`，versionCode `240`。
- APK SHA-256：`6db50df98e83a0264b79d27513742881206c2b2093a4f0eadfb7201191c0c5ad`。
- 使用 JADX 1.2.0 阅读四份主 DEX，并使用 Androguard 直接核对关键方法字节码。JADX 报告了部分反编译错误，因此不能将本次结果解释为完整恢复全部源码。
- 初步静态检查后，经用户要求搭建 Android 模拟器，断网安装并启动 APK，提取运行时业务 DEX。没有提交生辰到服务端、请求测算接口或修改付费状态；没有将 APK、数据库、原始反编译源码或密钥加入项目。Python 移植位于 `native/third_party/yiqi`。

## 找到的每日运势接口

`me.goldze.mvvmhabit.http.api.SoftHostApi` 声明了 Retrofit 表单 POST 方法：

- `IOSService.asmx/GetDayLuckByMemberCard202406`
- `IOSService.asmx/GetDayLuckByMemberCard202411_Green`

`AndroidSoftApi` 还声明了 `GetCarAllInfoForDaily202207`。这些方法接受字符串键值表，返回 `Observable<String>`。接口声明能证明客户端具备请求每日运势服务的路径，但在没有恢复调用方与响应解析链的情况下，不能单凭方法名称证明截图必然来自其中某一个版本。

## 分数与星级的数据结构

`me.goldze.mvvmhabit.pojo.LuckDayVo` 包含：

- `ScoreList.ScoreModel`：每个点包含 `CurDay`、`Score`、`MinScore`、`Name`、`Name2` 等字段。
- `DayLuckList.DailyLuckSingleResult`：包含 `Score`、`LoveIndex`、`BusinessIndex`、`HealthIndex`、`RiGanZhi`、`XingXiangList` 等字段。
- `DefaultXiYongString`、`XiYongString`、`ShouDong`、`SolarTime`、`UseSolarTime` 等字段。

直接读取 DEX 字节码验证：曲线点与单日结果的两个 `getScore`，以及 `getLoveIndex`、`getBusinessIndex`、`getHealthIndex`，均只有 `iget-object` 和 `return-object`，即原样读取保存的字符串字段，未在 getter 中根据生辰计算分数。这不能排除上游另有本地计算或转换，但这些模型本身不是评分引擎。

`XingShenModelBean` 的 `Formula` 与 `FormulaLink` 也是结果数据字段；名称中有 Formula 并不等于 APK 内包含完整可执行公式。

## 数据库核对

检查 APK 内全部五份 SQLite 数据库的表结构：

| 文件 | 主要内容 |
| --- | --- |
| `res/1kA.db` | 凶神解释 |
| `res/CbO.db` | 万年历、宜忌、地区数据 |
| `res/Ys6.db` | 万年历、宜忌、地区数据 |
| `res/Quw.sqlite` | 城市及日期时间表 |
| `res/Rwm.sqlite` | 梦境解释 |

两份主要万年历库的 `wan_nian_li_new` 均能查询到 `RiQi=20261104`，`JinRiBaZi` 为 `丙午|戊戌|壬午|辛亥`；前六字与截图年月日干支一致。末两字是数据库的通用字段内容，不能解释成用户出生时柱。

没有发现个人日运评分表；两份主要库的表结构也没有包含 `score`、`yongshen`、`xiyong` 的列。本次查表不能代替对全部受保护代码的审计。

## 静态检查时的限制

Manifest 的 Application 为 `com.sagittarius.v6.StubApplication`，APK 带有 `libbaiduprotect.so`、`baiduprotect*.jar` 及附属 DEX。六个 `baiduprotect*.i.dex` ZIP 中的 DEX，经解析均为零个类；同名 jar 为非普通 ZIP 数据。部分业务代码仍受加固保护，尚未恢复截图页面到网络返回或本地运算的完整调用链。

因此目前有直接证据证明客户端存在每日运势网络接口及完整分数模型，但还没有取得能本地重算 34 分的公式，不能断言全部评分都在服务端。没有据此更改 WinCal 权重，也没有把接口响应模型当作可独立运行的评分算法。


## 后续：模拟器提取与调用链恢复

环境为 `E:\Tools\AndroidSDK` 中的 Android Emulator、Android 9 / API 28 默认 x86_64 镜像，AVD 为 `WinCalResearch`，使用 WHPX 加速、ADB 端口 5580。运行时关闭 Wi-Fi 与移动网络。

Frida 注入未成功，随后重启应用，改为通过模拟器 root 权限读取 `/proc/<pid>/mem` 中的 `dalvik-DEX data` 映射，未改动业务方法。

共获得六份 DEX，其中五份原始 SHA-1 与 Adler-32 校验均通过。两份包含业务类的原始文件：

| 大小 | 类数 | `yiqi.bazi` 类数 | 原始 SHA-256 |
| --- | ---: | ---: | --- |
| 9,282,408 字节 | 8,791 | 8,690 | `1e6a157fac5c7ac24f5bb82010982dc83a8c38c9483e736c2b18f637699bc75d` |
| 3,968,152 字节 | 2,890 | 2,789 | `9811c80bc5fece8d99da06835d86a7b5aff78be15e11c599fd6ff17e0066f105` |

两份业务 DEX 的 JADX 反编译报告五处错误；本次结论依赖的日运类和绘图类可读，不将此视为全 APK 无遗漏恢复。

### 已确认的日运显示链

1. `yiqi.bazi.ui.yunshi.luck.LuckDayFragment2.getData` 发送 `userString`、喜用/地区/真太阳时配置及通用参数，调用 `GetDayLuckByMemberCard202406`。
2. 网络成功回调将响应 JSON 解析为 `DayLuckBean.DailyLuckListResultBean`，再调用 `setData`。
3. `setData` 从 `ScoreList` 逐项读取 `Name2` 和 `Score`，直接交给 `DayLuckView.setData`，低谷日期说明读取独立的 `ScoreMinDay` 字段。
4. `showUi` 从单日对象读取 `Score` 后直接设置文本；颜色按 `>69`、`>39`、其余分档。
5. `LoveIndex`、`BusinessIndex`、`HealthIndex` 分别解析成 float 后交给三个 RatingBar，没有用星级均值重新生成综合分。
6. 绿色版本 `GreenLuckDayFragment2/3` 调用 `GetDayLuckByMemberCard202411_Green`，同样直接使用响应字段。

因此，在已经追踪的日运显示路径中，数字来源是网络响应，不是客户端根据生辰重算。这比仅看到接口声明与 getter 的初步证据更完整；从生辰生成分数的服务端公式仍未取得。

### 原曲线的低谷与比例

`DayLuckView.drawCircle` 对整段 `lineData` 的整数分求全局最小值，为所有并列最低点绘制“低谷”，但不为索引 0 绘制该标记。分数 25 的判断只决定文字位置，不是低谷阈值。

`onDraw` 通过原 `div(score, 100, 2)` 获取比例，以 BigDecimal ROUND_HALF_UP 保留两位，再换算为像素。横向间隔包含 Java 整数除法；曲线控制点位于相邻点的横向中点。字段默认示例数组和其他未被此页面使用的图表类不能当作用户真实分数。

### 原类运行测试

使用独立 Java 测试入口，在 Android `app_process` 中加载上表两份原始且校验通过的 DEX。只反射调用原始类，不修改类方法或 APK 状态，使用合成数据，共 12 项通过：

- 总分 `34`、爱情 `4`、事业 `3.5`、健康 `2` 分别原样读写（四项）。
- 原 `div` 对 `34/100`、合成 `6/100` 输出 `0.34`、`0.06`（两项）。
- 合成序列 `[34, 6, 6, 6, 50]` 的五个点逐一核对：只有中间三个点实际绘制紫色低谷标签（五项）。
- 首点独自最低时不绘制低谷标签（一项）。

绘图测试实际调用原 `drawCircle(Canvas)`，通过离屏 Bitmap 的标签颜色像素核验。合成的 6 不代表截图中某天的已知分数。这些测试验证客户端显示规则，未测试生辰到分数的计算。

## Python 移植

按用户要求将对应类与 API 转为标准库 Python 模块，放在 `native/third_party/yiqi`，参见 [模块说明](fortune-python-module.md)。包括字段模型、未知字段保留、图表数学规则、ASMX XML/JSON 解析、两个日运接口、加密运程车接口的原始响应与解码扩展点。

通用参数字段经 APK 常量核对：`Config.KEY_DEVICE_TOKEN` 实际值为 `deviceId`，`AgooConstants.MESSAGE_FROM_PKG` 实际值为 `source`，`fr` 值为 `yiqi`。密钥、设备 ID 和会员 ID 由调用方提供。

两个日运 IOSService 路由在已核对的 `ResponseDecryptInterceptor` 路径中不执行原生 AES 解密；AndroidServiceEncrypt 路由则需要解码，当前 Python 移植保留原加密文本或调用传入的解码器，没有猜测原生 AES 实现。

Python 14 项回归测试通过，HTTP 部分只连接本机测试服务器。初次移植时尚未测试线上服务，后续实测结果见下节；原生 C++ 运势引擎未替换。


## 线上验证（2026-11-04）

用户明确要求验证真实返回和 15 天数据后，使用 APK 中该接口的原始应用参数、本轮参考档案及匿名身份调用两个日运接口，没有改动锁定状态或调用解锁接口。密钥与生辰未写入仓库。

首轮空字符串 `memberId` 导致 HTTP 500，公开 WSDL 确认它是必填整数；进一步核对原 `UserUtils.getMemberId`，未登录时默认值为 `0`。Python 默认值已修正为 `0`，并加回归测试。

修正后两个接口均返回 HTTP 200，`ScoreList` 与 `DayLuckList` 均为 16 项：2026-11-03～18。两个接口的综合分完全一致。今天 34 分，11 月 6～8 日均为 6 分，与此前截图的分值和低谷位置吻合；现在才有接口实测依据将这些日期记录为 6 分。

`IsUnLocked=false`、`MemberServiceStatus=false`。单日详情只在昨天、今天、明天标记为已解锁；从 11 月 6 日起仍返回综合分，但爱情、事业、健康指数为零且详情锁定，零值不能当作有效测算结果。返回综合分不代表完整详情都可用。

服务实际以 JSON number 返回分数和星级、boolean 返回单日锁定状态；Python 模型已对齐原 Gson 的 String 字段转换行为。保存的真实响应离线解析通过，16 个曲线分与对应详情分逐日一致。匿名精简结果见 [验证数据](fortune-api-verification.json)。
