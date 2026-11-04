"""Data-only translation of DayLuckView; coordinates are Android pixel units."""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal, ROUND_HALF_UP, localcontext
from typing import Sequence


def div(value: float | str, divisor: float | str, scale: int) -> Decimal:
    """DayLuckView.div: BigDecimal division using ROUND_HALF_UP."""
    if scale < 0:
        raise ValueError("The scale must be a positive integer or zero")
    with localcontext() as context:
        context.prec = max(28, scale + 20)
        return (Decimal(str(value)) / Decimal(str(divisor))).quantize(
            Decimal(1).scaleb(-scale), rounding=ROUND_HALF_UP)


def score_band(score: str | float) -> str:
    """LuckDayFragment2/GreenLuckDayFragment: >69, >39, otherwise low."""
    number = float(score)
    return "high" if number > 69 else "middle" if number > 39 else "low"


def minimum_indices(scores: Sequence[str]) -> list[int]:
    """Original chart labels every global minimum except point zero."""
    if not scores:
        return []
    values = [int(v) for v in scores]
    minimum = min(values)
    return [i for i, value in enumerate(values) if i > 0 and value == minimum]


@dataclass(frozen=True)
class ChartPoint:
    x: float
    y: float
    score: str
    label: str
    minimum: bool


def chart_points(
    scores: Sequence[str], labels: Sequence[str], *, width: int, height: int,
    padding_left: int = 0, padding_top: int = 0,
    padding_right: int = 0, padding_bottom: int = 0,
    x_text_width: int = 0, y_text_height: int = 0, text_space: int = 5,
) -> list[ChartPoint]:
    """Port onDraw's point calculation, including Java integer x spacing.

    Text metrics must come from the caller's renderer. No score is calculated
    here. Scores outside 0..100 are not clamped, matching the original view.
    """
    if len(scores) != len(labels):
        raise ValueError("scores and labels must have the same length")
    if not scores:
        return []
    if len(scores) == 1:
        raise ValueError("DayLuckView requires at least two points")
    plot_width = width - padding_left - padding_right - x_text_width - text_space
    step = int(plot_width / (len(scores) - 1))
    plot_height = height - padding_top - padding_bottom - y_text_height - text_space
    minima = set(minimum_indices(scores))
    return [ChartPoint(
        x=padding_left + x_text_width + text_space + step * i,
        y=int((1.0 - float(div(float(score), 100.0, 2))) * plot_height + padding_top),
        score=score, label=labels[i], minimum=i in minima,
    ) for i, score in enumerate(scores)]


def bezier_segments(points: Sequence[ChartPoint]) -> list[tuple[float, ...]]:
    """Return (start x/y, control 1 x/y, control 2 x/y, end x/y)."""
    return [(a.x, a.y, (a.x + b.x) / 2, a.y,
             (a.x + b.x) / 2, b.y, b.x, b.y)
            for a, b in zip(points, points[1:])]
