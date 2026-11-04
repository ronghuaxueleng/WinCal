"""Python translations of the daily-luck beans in yiqi.bazi 5.0.8.

Wire field names and score strings are retained. See docs/fortune-python-module.md
in the repository root for provenance.
"""

from __future__ import annotations

from dataclasses import dataclass, field, fields
from typing import Any, Mapping, TypeVar, get_args, get_type_hints

T = TypeVar("T", bound="WireModel")


@dataclass
class WireModel:
    extra: dict[str, Any] = field(default_factory=dict, repr=False, kw_only=True)

    @classmethod
    def from_dict(cls: type[T], value: Mapping[str, Any]) -> T:
        names = {f.name for f in fields(cls)} - {"extra"}
        hints = get_type_hints(cls)
        known = {}
        for key, item in value.items():
            if key not in names:
                continue
            hint = hints[key]
            # Gson's String adapter accepts JSON numbers and booleans too.
            if item is not None and (hint is str or str in get_args(hint)):
                if isinstance(item, bool):
                    item = "true" if item else "false"
                elif isinstance(item, (int, float)):
                    item = str(item)
            known[key] = item
        return cls(**known,
                   extra={k: v for k, v in value.items() if k not in names})

    def to_dict(self) -> dict[str, Any]:
        def wire(value: Any) -> Any:
            if isinstance(value, WireModel):
                return value.to_dict()
            if isinstance(value, list):
                return [wire(item) for item in value]
            return value

        return {**self.extra, **{f.name: wire(getattr(self, f.name))
                                for f in fields(self) if f.name != "extra"}}


@dataclass
class StarGodVo(WireModel):
    Status: int = 0
    Type: int = 0
    XingShenName: str | None = None
    XingShenText: str | None = None


@dataclass
class ScoreModelBean(WireModel):
    Name: str | None = None
    Name2: str | None = None
    Score: str | None = None


@dataclass
class DailyLuckSingleResultBean(WireModel):
    BaZiString: str | None = None
    BadHour: list[str] | None = None
    Bads: list[str] | None = None
    BusinessIndex: str | None = None
    BusinessIndexStr: str | None = None
    Chong: str | None = None
    DiZhi: list[str] | None = None
    GongLiString: str | None = None
    GoodColor: str | None = None
    GoodColorImg: str | None = None
    GoodFood: list[str] | None = None
    GoodFoodImgs: list[str] | None = None
    GoodHour: list[str] | None = None
    GoodNumber: str | None = None
    GoodNumberImg: str | None = None
    Goods: list[str] | None = None
    HealthIndex: str | None = None
    HealthIndexStr: str | None = None
    Holiday: str | None = None
    IsUnLocked: str | None = None
    JianXing: str | None = None
    LikePosition: str | None = None
    LoveIndex: str | None = None
    LoveIndexStr: str | None = None
    LuckStr: str | None = None
    Motto: str | None = None
    NongLiString: str | None = None
    RiGanZhi: str | None = None
    Score: str | None = None
    ScoreIndexStr: str | None = None
    ShortDayStr: str | None = None
    TianGan: list[str] | None = None
    Title: str | None = None
    WealthPosition: str | None = None
    WeekName: str | None = None
    XingXiangList: list[StarGodVo] | None = None

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> DailyLuckSingleResultBean:
        result = super().from_dict(value)
        if result.XingXiangList is not None:
            result.XingXiangList = [StarGodVo.from_dict(v) for v in result.XingXiangList]
        return result


@dataclass
class DailyLuckListResultBean(WireModel):
    ActivityDescn: str | None = None
    ActivityFinish: str | None = None
    BaZiString: str | None = None
    DailyLuckUrl: str | None = None
    DayLuckList: list[DailyLuckSingleResultBean] | None = None
    IsUnLocked: bool = False
    IsUpgrade: str | None = None
    MainUserString: str | None = None
    MemberServiceAdInfo: dict[str, Any] | None = None
    MemberServiceStatus: bool = False
    MonthLuckUrl: str | None = None
    Name: str | None = None
    NextAction: str | None = None
    PopLockAdvObj: dict[str, Any] | None = None
    PopLockType: int = 0
    PracticeLevel: str | None = None
    PracticeLevel2: str | None = None
    RiZhu: str | None = None
    ScoreList: list[ScoreModelBean] | None = None
    ScoreMinDay: str | None = None
    ShareDescn: str | None = None
    ShareTitle: str | None = None
    ShareUrl: str | None = None
    ShowMonthLuckyCard: int = 0
    TipStatus: int = 0
    UserPractice: str | None = None
    WakeUpLuckIntroduceLink: str | None = None
    WakeUpLuckLockImg: str | None = None
    WakeUpLuckMemberServiceAdInfo: dict[str, Any] | None = None
    WakeUpLuckMemberServiceStatus: bool = False
    WakeUpLuckSuspendedAdInfo: dict[str, Any] | None = None
    WakeUpLuckWallPaperAdInfo: dict[str, Any] | None = None
    xmlns: str | None = None

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> DailyLuckListResultBean:
        result = super().from_dict(value)
        if result.ScoreList is not None:
            result.ScoreList = [ScoreModelBean.from_dict(v) for v in result.ScoreList]
        if result.DayLuckList is not None:
            result.DayLuckList = [DailyLuckSingleResultBean.from_dict(v) for v in result.DayLuckList]
        # Java/Gson primitive booleans also accept JSON string booleans.
        for name in ("IsUnLocked", "MemberServiceStatus", "WakeUpLuckMemberServiceStatus"):
            current = getattr(result, name)
            if isinstance(current, str):
                setattr(result, name, current.lower() == "true")
        return result
