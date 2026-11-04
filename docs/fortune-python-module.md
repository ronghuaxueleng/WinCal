# 易奇日运 Python 移植

来源是用户提供的 `yiqi.bazi` Android 5.0.8（versionCode 240）APK，
SHA-256 为 `6db50df98e83a0264b79d27513742881206c2b2093a4f0eadfb7201191c0c5ad`。
原类经本地 Android 模拟器加载、提取后核对。原始类的权利属于原权利人，
本目录不将其声明为 MIT 或其他已确认的开源授权。

本模块使用 Python 3.10+ 标准库，无额外依赖。

| Python 文件 | 对应原类 / 行为 |
| --- | --- |
| `models.py` | `DayLuckBean.DailyLuckListResultBean`、`ScoreModelBean`、`DailyLuckSingleResultBean`、`StarGodVo` |
| `chart.py` | `DayLuckView.div/onDraw/drawLine/drawCircle`，以及日运页的分数颜色分档 |
| `api.py` | `ApiHelper.getSoftHostParams`、喜用参数、`SoftHostApi` 两个日运接口、`AndroidSoftApi` 日运接口 |

模型保留接口字段名和字符串分数；未知字段放入 `extra`，可通过 `to_dict()` 输出。
广告数据保留为字典，没有移植广告、支付、登录或设备标识生成逻辑。
绘图部分输出坐标、贝塞尔控制点和低谷标记，交由调用方渲染。

## API

- `SoftHostApi.get_day_luck_by_member_card_202406`：
  `POST https://soft.yiqibazi.com/IOSService.asmx/GetDayLuckByMemberCard202406`
- `SoftHostApi.get_day_luck_by_member_card_202411_green`：
  `POST https://soft.yiqibazi.com/IOSService.asmx/GetDayLuckByMemberCard202411_Green`
- `AndroidSoftApi.get_car_all_info_for_daily_202207`：
  原 APK 使用 `POST http://soft.yiqibazi.com/AndroidServiceEncrypt.asmx/GetCarAllInfoForDaily202207`。
  此接口先拆出 XML 字符串，默认返回原加密文本；可通过 `response_decoder` 接入解码器。
  尚未移植原生 `AESEncryption`，没有猜测其算法。请求参数按原 `FieldMap` 发送。

前两个接口在已核对的响应拦截路径中不走原生 AES 解密，支持 ASMX XML 字符串或直接 JSON。
错误响应、HTTP 错误和无效数据会抛出异常，不生成虚构分数。
`base_url`、`timeout`、`transport` 可替换，方便接入项目 HTTP 层和本地测试。

```python
# 在仓库根目录启动 Python。
import os
from native.third_party.yiqi import ApiContext, SoftHostApi, XiYongSettings, daily_params

context = ApiContext(
    key=os.environ["YIQI_API_KEY"],
    device_id=os.environ["YIQI_DEVICE_ID"],
    member_id=os.environ.get("YIQI_MEMBER_ID", "0"),
)
params = daily_params(context, os.environ["YIQI_USER_STRING"], XiYongSettings())
result = SoftHostApi().get_day_luck_by_member_card_202406(params)
for point in result.ScoreList or []:
    print(point.Name2, point.Score)
```

只有显式调用接口方法才联网；导入模块、解析响应和计算图表坐标都可离线运行。
没有内置从 APK 提取的接口密钥、用户凭据或个人生辰。
2026-11-04 已实测两个日运接口，匿名 `memberId=0` 均返回 HTTP 200。两者均返回 16 个日期（昨天加今天起的 15 天）的综合分；后续日期的详情仍受锁定状态限制，不能把零值分项当作有效数据。服务返回数值型分数和布尔锁定状态，模型按原 Gson String 字段规则转换。详细记录见 `docs/fortune-api-verification.json`。

## 已核对的规则

- 日运总分与爱情、事业、健康星级分别读取响应字段，不相互求平均。
- 分数颜色分档为 `>69`、`>39`、其余。
- 图表比例为分数除以 100，保留两位、四舍五入；坐标按原布局公式换算。
- 对整段曲线求最小整数分，所有并列最小值都标记低谷，唯独第一个点不显示低谷标记。
- 相邻点使用横向中点为控制点的三次贝塞尔曲线。

这些是客户端的数据与显示规则，**不包含从生辰生成日运分数的服务端公式**。
本目录作为独立第三方 Python 模块保存，尚未替换原生 C++ 运势引擎或加入 EXE 安装包。

## 测试

```powershell
python -B -m unittest discover -s native/third_party/yiqi/tests -v
```

测试覆盖原类已验证的合成数据、嵌套模型、未知字段、XML/JSON、低谷与坐标、
请求参数、两个真实路由名的本地 HTTP POST，以及加密接口的解码扩展点。
合成样本中的 `6` 仅用于测试并列低谷，不是截图日期的已知分数。
