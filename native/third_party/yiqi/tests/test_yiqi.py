"""Offline regressions; HTTP tests bind only to a local test server."""

import copy
from decimal import Decimal
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import sys
import threading
import unittest
from urllib.error import HTTPError
from urllib.parse import parse_qs

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from yiqi import (AndroidSoftApi, ApiContext, DailyLuckListResultBean, SoftHostApi,
                  XiYongSettings, bezier_segments, chart_points, daily_params,
                  div, minimum_indices, parse_daily_result, parse_xml, score_band)
from yiqi.api import CAR_DAILY_202207, DAY_LUCK_202406, DAY_LUCK_GREEN_202411


def fixture():
    # Synthetic display data. The repeated 6 is not a measured reference score.
    return {"Name": "test", "IsUnLocked": False, "MemberServiceStatus": "false",
            "ScoreMinDay": "B、C、D", "FutureField": {"kept": True},
            "ScoreList": [{"Name": str(i), "Name2": label, "Score": score}
                          for i, (label, score) in enumerate(zip("ABCDE", ["34", "6", "6", "6", "50"]))],
            "DayLuckList": [{"Score": "34", "LoveIndex": "4",
                             "BusinessIndex": "3.5", "HealthIndex": "2",
                             "Goods": ["阅读&休息"], "IsUnLocked": "false",
                             "XingXiangList": [{"XingShenName": "合星小吉",
                                                "Status": 1, "Type": 2,
                                                "FormulaLink": "example"}]}]}


class ModelTests(unittest.TestCase):
    def test_live_wire_types_follow_original_gson_string_conversion(self):
        result = parse_daily_result(json.dumps({
            "ScoreList": [{"Name2": "今天", "Score": 34, "IsUnLocked": True}],
            "DayLuckList": [{"Score": 34, "LoveIndex": 4.0,
                             "BusinessIndex": 3.5, "HealthIndex": 2.0,
                             "IsUnLocked": False}],
        }))
        self.assertEqual(result.ScoreList[0].Score, "34")
        self.assertEqual(result.ScoreList[0].extra["IsUnLocked"], True)
        day = result.DayLuckList[0]
        self.assertEqual((day.Score, day.LoveIndex, day.BusinessIndex, day.HealthIndex,
                          day.IsUnLocked), ("34", "4.0", "3.5", "2.0", "false"))

    def test_scores_and_ratings_remain_independent_strings(self):
        result = parse_daily_result(json.dumps(fixture()))
        day = result.DayLuckList[0]
        self.assertEqual((day.Score, day.LoveIndex, day.BusinessIndex, day.HealthIndex),
                         ("34", "4", "3.5", "2"))
        self.assertFalse(result.IsUnLocked)
        self.assertFalse(result.MemberServiceStatus)
        self.assertEqual(day.IsUnLocked, "false")

    def test_nested_models_unknown_fields_and_round_trip(self):
        raw = fixture()
        result = DailyLuckListResultBean.from_dict(raw)
        self.assertEqual(result.ScoreList[0].Score, "34")
        self.assertEqual(result.DayLuckList[0].XingXiangList[0].XingShenName, "合星小吉")
        output = result.to_dict()
        self.assertEqual(output["FutureField"], {"kept": True})
        self.assertEqual(output["DayLuckList"][0]["XingXiangList"][0]["FormulaLink"], "example")
        self.assertEqual(output["ScoreList"], raw["ScoreList"])
        self.assertEqual(DailyLuckListResultBean.from_dict(output), result)
        self.assertEqual(raw, fixture())

    def test_null_lists_and_missing_scores_are_not_filled_in(self):
        result = parse_daily_result('{"ScoreList": null, "DayLuckList": [{}]}')
        self.assertIsNone(result.ScoreList)
        self.assertIsNone(result.DayLuckList[0].Score)

    def test_plain_xml_cdata_and_escaped_json(self):
        text = json.dumps(fixture(), ensure_ascii=False)
        escaped = text.replace("&", "&amp;").replace("<", "&lt;")
        forms = [text, '\ufeff' + text,
                 '<string xmlns="http://soft.yiqibazi.com/">\n' + escaped + '\n</string>',
                 '<string xmlns="urn:test"><![CDATA[' + text + ']]></string>']
        for payload in forms:
            with self.subTest(payload=payload[:40]):
                self.assertEqual(parse_daily_result(payload).DayLuckList[0].Goods, ["阅读&休息"])

    def test_error_payload_does_not_become_an_empty_success(self):
        for payload in ['{"Error":"denied"}', '[]', '<html>error</html>', '', '{broken']:
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                parse_daily_result(payload)


class ChartTests(unittest.TestCase):
    def test_original_android_minimum_cases(self):
        self.assertEqual(minimum_indices(["34", "6", "6", "6", "50"]), [1, 2, 3])
        self.assertEqual(minimum_indices(["1", "10", "20", "30", "40"]), [])
        self.assertEqual(minimum_indices(["1", "1", "1"]), [1, 2])
        self.assertEqual(minimum_indices([]), [])

    def test_java_bigdecimal_half_up(self):
        self.assertEqual(div(34, 100, 2), Decimal("0.34"))
        self.assertEqual(div(6, 100, 2), Decimal("0.06"))
        self.assertEqual(div(34.5, 100, 2), Decimal("0.35"))
        with self.assertRaises(ValueError):
            div(34, 100, -1)

    def test_score_band_boundaries(self):
        for value, band in [("34", "low"), ("39", "low"), ("39.1", "middle"),
                            ("69", "middle"), ("69.1", "high")]:
            self.assertEqual(score_band(value), band)

    def test_geometry_and_bezier_controls(self):
        pts = chart_points(["34", "6", "6", "6", "50"], list("ABCDE"),
                           width=600, height=300, x_text_width=25, y_text_height=20)
        self.assertEqual([p.x for p in pts], [30, 172, 314, 456, 598])
        self.assertEqual(pts[0].y, 181)
        self.assertEqual([i for i, p in enumerate(pts) if p.minimum], [1, 2, 3])
        segment = bezier_segments(pts)[0]
        self.assertEqual(segment, (pts[0].x, pts[0].y, 101, pts[0].y,
                                   101, pts[1].y, pts[1].x, pts[1].y))

    def test_geometry_invalid_lengths(self):
        self.assertEqual(chart_points([], [], width=100, height=100), [])
        for scores, labels in [(["1"], ["A"]), (["1", "2"], ["A"])]:
            with self.assertRaises(ValueError):
                chart_points(scores, labels, width=100, height=100)


class ApiTests(unittest.TestCase):
    def test_original_form_keys_and_xiyong(self):
        self.assertEqual(ApiContext("test", "test").soft_host_params()["memberId"], "0")
        context = ApiContext("test-key", "test-device", member_id="test-member", market="test")
        params = daily_params(context, "synthetic-profile", XiYongSettings("喜金水", "测试地区", "1"))
        self.assertEqual(params, {"key": "test-key", "deviceId": "test-device",
                                 "memberId": "test-member", "market": "test",
                                 "fr": "yiqi", "version": "5.0.8", "lang": "", "source": "1",
                                 "userString": "synthetic-profile", "shouDong": "喜金水",
                                 "area": "测试地区", "useSolarTime": "1"})
        self.assertEqual(XiYongSettings().to_params(),
                         {"shouDong": "", "area": "", "useSolarTime": ""})

    def test_http_post_and_both_daily_routes(self):
        received = []

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                body = self.rfile.read(int(self.headers["Content-Length"]))
                received.append((self.path, parse_qs(body.decode("utf-8"), keep_blank_values=True),
                                 self.headers["Content-Type"]))
                data = json.dumps(fixture(), ensure_ascii=False).replace("&", "&amp;")
                encoded = ('<string xmlns="http://soft.yiqibazi.com/">\n' + data + '\n</string>').encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/xml; charset=utf-8")
                self.end_headers()
                self.wfile.write(encoded)

            def log_message(self, *args):
                pass

        with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                api = SoftHostApi(base_url=f"http://127.0.0.1:{server.server_port}")
                params = daily_params(ApiContext("test", "test"), "synthetic & profile")
                saved = copy.deepcopy(params)
                self.assertEqual(api.get_day_luck_by_member_card_202406(params).DayLuckList[0].Score, "34")
                self.assertEqual(api.get_day_luck_by_member_card_202411_green(params).ScoreList[0].Score, "34")
                self.assertEqual(params, saved)
                self.assertEqual([v[0] for v in received], ["/" + DAY_LUCK_202406, "/" + DAY_LUCK_GREEN_202411])
                self.assertEqual(received[0][1], {k: [v] for k, v in params.items()})
                self.assertTrue(received[0][2].startswith("application/x-www-form-urlencoded"))
            finally:
                server.shutdown()
                worker.join()

    def test_android_encrypted_response_stays_raw_until_decoded(self):
        calls = []

        def transport(url, params, timeout):
            calls.append((url, dict(params), timeout))
            return '<string xmlns="urn:test">ciphertext</string>'

        raw = AndroidSoftApi(transport=transport, timeout=7)
        self.assertEqual(raw.get_car_all_info_for_daily_202207({"key": "test"}), "ciphertext")
        self.assertTrue(calls[0][0].endswith("/" + CAR_DAILY_202207))
        self.assertEqual(calls[0][2], 7)
        decoded = AndroidSoftApi(transport=transport, response_decoder=lambda v: "decoded:" + v)
        self.assertEqual(decoded.get_car_all_info_for_daily_202207({}), "decoded:ciphertext")

    def test_transport_failure_propagates(self):
        def fail(*args):
            raise HTTPError("http://127.0.0.1/", 403, "denied", {}, None)

        with self.assertRaises(HTTPError):
            SoftHostApi(transport=fail).get_day_luck_by_member_card_202406({})


if __name__ == "__main__":
    unittest.main()
