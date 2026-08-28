import unittest

from run_editor_performance_benchmark import (
    _aggregate_runs,
    _metric_statistics,
    _nearest_rank_percentile,
)


def make_report(startup, main_window, map_open, zoom, pan, coverage=0.75):
    iterations = len(zoom)
    return {
        "schema_version": 1,
        "valid": True,
        "map_path": "C:/assets/map/test.map",
        "assets_path": "C:/assets",
        "map_file_bytes": 1024,
        "qt_version": "6.11.0",
        "platform": "Test OS",
        "cpu_architecture": "test-arch",
        "build_type": "Debug",
        "contract": {"interaction_iterations": iterations},
        "map_size": {"width": 100, "height": 200},
        "canvas_size": {"width": 400, "height": 651},
        "process_to_main_window_first_paint_ms": startup,
        "main_window_to_first_paint_ms": main_window,
        "map_open_to_first_paint_ms": map_open,
        "zoom": {
            "valid": True,
            "iterations_completed": iterations,
            "paint_events": iterations,
            "samples_ms": zoom,
        },
        "pan": {
            "valid": True,
            "iterations_completed": iterations,
            "paint_events": iterations,
            "samples_ms": pan,
        },
        "map_art_coverage_ratio": coverage,
    }


class EditorPerformanceBenchmarkRunnerTests(unittest.TestCase):
    def test_nearest_rank_percentile(self):
        self.assertEqual(_nearest_rank_percentile([1, 2, 3, 4, 5], 0.95), 5)
        self.assertEqual(_nearest_rank_percentile([5, 1, 3, 2, 4], 0.50), 3)

    def test_metric_statistics_reports_median_and_p95(self):
        statistics = _metric_statistics([1, 2, 3, 20])
        self.assertEqual(statistics["sample_count"], 4)
        self.assertEqual(statistics["median_ms"], 2.5)
        self.assertEqual(statistics["p95_ms"], 20)

    def test_aggregate_runs_flattens_interaction_samples(self):
        aggregate = _aggregate_runs(
            [
                make_report(100, 60, 300, [10, 20], [30, 40], 0.70),
                make_report(120, 70, 340, [12, 22], [32, 42], 0.80),
            ]
        )
        self.assertEqual(
            aggregate["process_to_main_window_first_paint_ms"]["median_ms"],
            110,
        )
        self.assertEqual(aggregate["zoom_frame_ms"]["sample_count"], 4)
        self.assertEqual(aggregate["pan_frame_ms"]["maximum_ms"], 42)
        self.assertEqual(
            aggregate["map_art_coverage_ratio"],
            {"minimum": 0.70, "maximum": 0.80},
        )

    def test_invalid_report_is_rejected(self):
        report = make_report(100, 60, 300, [10], [20])
        report["valid"] = False
        with self.assertRaises(ValueError):
            _aggregate_runs([report])

    def test_inconsistent_contract_is_rejected(self):
        first = make_report(100, 60, 300, [10, 20], [30, 40])
        second = make_report(120, 70, 340, [12, 22], [32, 42])
        second["canvas_size"]["width"] = 500
        with self.assertRaisesRegex(ValueError, "canvas_size"):
            _aggregate_runs([first, second])

    def test_incomplete_sequence_is_rejected(self):
        report = make_report(100, 60, 300, [10, 20], [30, 40])
        report["zoom"]["paint_events"] = 1
        with self.assertRaisesRegex(ValueError, "incomplete zoom"):
            _aggregate_runs([report])


if __name__ == "__main__":
    unittest.main()
