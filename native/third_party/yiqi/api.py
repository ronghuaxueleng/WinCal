"""Form POST adapters for the daily-luck APIs declared in the supplied APK.

Importing this module never sends a request. Credentials are caller supplied.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
from typing import Callable, Mapping
from urllib.parse import urlencode
from urllib.request import Request, urlopen
import xml.etree.ElementTree as ET

from .models import DailyLuckListResultBean

SOFT_HOST = "https://soft.yiqibazi.com/"
ANDROID_SOFT_BASE = "http://soft.yiqibazi.com/AndroidServiceEncrypt.asmx/"
DAY_LUCK_202406 = "IOSService.asmx/GetDayLuckByMemberCard202406"
DAY_LUCK_GREEN_202411 = "IOSService.asmx/GetDayLuckByMemberCard202411_Green"
CAR_DAILY_202207 = "GetCarAllInfoForDaily202207"


@dataclass(frozen=True)
class ApiContext:
    key: str
    device_id: str
    member_id: str = "0"
    market: str = ""
    version: str = "5.0.8"
    fr: str = "yiqi"

    def soft_host_params(self) -> dict[str, str]:
        """Translation of ApiHelper.getSoftHostParams; no device-ID fabrication."""
        return {"key": self.key, "fr": self.fr, "lang": "", "market": self.market,
                "deviceId": self.device_id, "version": self.version,
                "memberId": self.member_id, "source": "1"}


@dataclass(frozen=True)
class XiYongSettings:
    shou_dong: str = ""
    area: str = ""
    use_solar_time: str = ""

    def to_params(self) -> dict[str, str]:
        return {"shouDong": self.shou_dong, "area": self.area,
                "useSolarTime": self.use_solar_time}


def daily_params(context: ApiContext, user_string: str,
                 xiyong: XiYongSettings | None = None) -> dict[str, str]:
    return {**context.soft_host_params(), "userString": user_string,
            **(xiyong or XiYongSettings()).to_params()}


def parse_xml(value: str) -> str:
    """Decode an ASMX <string> response; also accept already decoded JSON.

    XML parsing handles namespaces, CDATA and escaped JSON text, unlike the
    Android interceptor's single-line regular expression.
    """
    text = value.lstrip("\ufeff").strip()
    if not text.startswith("<"):
        return text
    root = ET.fromstring(text)
    if root.tag.rsplit("}", 1)[-1] != "string":
        raise ValueError("Expected an ASMX string response")
    return "".join(root.itertext()).strip()


def parse_daily_result(value: str) -> DailyLuckListResultBean:
    data = json.loads(parse_xml(value))
    if not isinstance(data, dict) or not ({"DayLuckList", "ScoreList"} & data.keys()):
        raise ValueError("Expected a daily-luck result with DayLuckList or ScoreList")
    return DailyLuckListResultBean.from_dict(data)


# A transport can be replaced by tests or an application-owned HTTP client.
Transport = Callable[[str, Mapping[str, str], float], str]


def post_form(url: str, params: Mapping[str, str], timeout: float) -> str:
    body = urlencode(params).encode("utf-8")
    request = Request(url, data=body, method="POST", headers={
        "Content-Type": "application/x-www-form-urlencoded; charset=utf-8",
        "Accept": "application/json, text/xml",
    })
    with urlopen(request, timeout=timeout) as response:
        return response.read().decode(response.headers.get_content_charset() or "utf-8")


class SoftHostApi:
    def __init__(self, *, base_url: str = SOFT_HOST, timeout: float = 15,
                 transport: Transport = post_form):
        self.base_url = base_url.rstrip("/") + "/"
        self.timeout = timeout
        self.transport = transport

    def get_day_luck_by_member_card_202406(
        self, params: Mapping[str, str],
    ) -> DailyLuckListResultBean:
        return parse_daily_result(self.transport(
            self.base_url + DAY_LUCK_202406, params, self.timeout))

    def get_day_luck_by_member_card_202411_green(
        self, params: Mapping[str, str],
    ) -> DailyLuckListResultBean:
        return parse_daily_result(self.transport(
            self.base_url + DAY_LUCK_GREEN_202411, params, self.timeout))


class AndroidSoftApi:
    """Raw adapter for the encrypted Android daily-car endpoint.

    Pass the original FieldMap and a response decoder when available. The
    APK's native AESEncryption is not replaced with a guessed AES mode/key.
    Without a decoder this method returns the XML-unwrapped encrypted text.
    """

    def __init__(self, *, base_url: str = ANDROID_SOFT_BASE, timeout: float = 15,
                 transport: Transport = post_form,
                 response_decoder: Callable[[str], str] | None = None):
        self.base_url = base_url.rstrip("/") + "/"
        self.timeout = timeout
        self.transport = transport
        self.response_decoder = response_decoder

    def get_car_all_info_for_daily_202207(self, params: Mapping[str, str]) -> str:
        value = parse_xml(self.transport(self.base_url + CAR_DAILY_202207,
                                         params, self.timeout))
        return self.response_decoder(value) if self.response_decoder else value
