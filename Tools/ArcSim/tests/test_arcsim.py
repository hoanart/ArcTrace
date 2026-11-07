"""ArcSim 검증 테스트. 실행: python -m unittest discover -s tests -v"""
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from arcsim import atmosphere  # noqa: E402
from arcsim import dynamics  # noqa: E402
from arcsim import geodesy  # noqa: E402
from arcsim import heightgrid  # noqa: E402
from arcsim import projectiles  # noqa: E402
from arcsim import recorder as recorder_module  # noqa: E402
from arcsim import runner  # noqa: E402
from arcsim import scenario as scenario_module  # noqa: E402


def write_scenario(directory, salvo, name="test"):
    data = {
        "name": name,
        "seed": 7,
        "integration_dt_us": 10000,
        "record_interval_us": 100000,
        "max_duration_s": 600,
        "height": {"flat_height_m": 0.0},
        "sites": [{"name": "S", "lat": 35.0, "lon": -116.5}],
        "salvos": [salvo],
    }
    path = Path(directory) / f"{name}.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def compute_ground_distance(lat1, lon1, lat2, lon2):
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    d_phi = phi2 - phi1
    d_lambda = math.radians(lon2 - lon1)
    a = math.sin(d_phi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(d_lambda / 2) ** 2
    return 2.0 * 6371008.8 * math.asin(math.sqrt(a))


class GeodesyTest(unittest.TestCase):
    def test_known_point(self):
        ecef = geodesy.lla_to_ecef(0.0, 0.0, 0.0)
        np.testing.assert_allclose(ecef, [geodesy.WGS84_A, 0.0, 0.0], atol=1e-6)

    def test_round_trip(self):
        rng = np.random.default_rng(0)
        lat = rng.uniform(-89.0, 89.0, 1000)
        lon = rng.uniform(-180.0, 180.0, 1000)
        alt = rng.uniform(-500.0, 100000.0, 1000)
        back_lat, back_lon, back_alt = geodesy.ecef_to_lla(geodesy.lla_to_ecef(lat, lon, alt))
        np.testing.assert_allclose(back_lat, lat, atol=1e-9)
        np.testing.assert_allclose(back_lon, lon, atol=1e-9)
        np.testing.assert_allclose(back_alt, alt, atol=1e-4)


class AtmosphereTest(unittest.TestCase):
    def test_isa_reference_values(self):
        density, sound_speed = atmosphere.density_and_sound_speed(np.array([0.0, 11000.0, 20000.0]))
        np.testing.assert_allclose(density, [1.2250, 0.3639, 0.0889], rtol=5e-3)
        self.assertAlmostEqual(sound_speed[0], 340.29, places=1)


SHORT_SHOT_SPEED_MPS = 900.0


class DynamicsTest(unittest.TestCase):
    def test_vacuum_energy_is_conserved(self):
        shell = projectiles.get_kind_index_by_name("Shell")
        batch = dynamics.ProjectileBatch(projectiles.KINDS, np.array([shell]),
                                         include_earth_rotation=False, include_drag=False)
        indices = np.array([0])
        position = geodesy.lla_to_ecef(35.0, 127.0, 0.0)[None, :]
        east, north, up = geodesy.enu_basis(35.0, 127.0)
        velocity = ((north * math.cos(math.radians(45.0)) + up * math.sin(math.radians(45.0))) * 900.0)[None, :]
        mass = np.array([45.0])

        def compute_energy(r, v):
            radius = np.linalg.norm(r)
            sin_lat = r[0, 2] / radius
            potential = -dynamics.EARTH_GM / radius * (
                1.0 - dynamics.EARTH_J2 * (geodesy.WGS84_A / radius) ** 2 * (3.0 * sin_lat ** 2 - 1.0) / 2.0)
            return 0.5 * float(np.dot(v[0], v[0])) + potential

        initial_energy = compute_energy(position, velocity)
        for step in range(10000):
            position, velocity, mass = batch.step_rk4(position, velocity, mass, np.array([step * 0.01]), 0.01, indices)
        self.assertLess(abs(compute_energy(position, velocity) - initial_energy), 1e-3)

    def test_vacuum_short_shot_matches_parabola(self):
        # 평면 포물선 근사가 맞는 짧은 사격(약 28km)이 되도록 발사 속도를 고정한다.
        shell_speed = projectiles.KINDS[projectiles.get_kind_index_by_name("Shell")].launch_speed_mps
        with tempfile.TemporaryDirectory() as directory:
            path = write_scenario(directory, {
                "site": "S", "kind": "Shell", "count": 1, "start_s": 0.0,
                "azimuth_deg": 0.0, "elevation_deg": 10.0, "speed_scale": SHORT_SHOT_SPEED_MPS / shell_speed})
            scenario = scenario_module.load_scenario(path)
            record, _ = runner.run_scenario(scenario, include_earth_rotation=False, include_drag=False)
            record.write(Path(directory) / "out", scenario, 1)
            events = np.loadtxt(Path(directory) / "out" / "events.csv", delimiter=",", skiprows=1)
        launch = events[events[:, 2] == 0][0]
        impact = events[events[:, 2] == 1][0]
        gravity = 9.797
        speed = SHORT_SHOT_SPEED_MPS
        elevation = math.radians(10.0)
        expected_range = speed * speed * math.sin(2.0 * elevation) / gravity
        expected_time = 2.0 * speed * math.sin(elevation) / gravity
        actual_range = compute_ground_distance(launch[3], launch[4], impact[3], impact[4])
        actual_time = (impact[0] - launch[0]) * 1e-6
        self.assertAlmostEqual(actual_range / expected_range, 1.0, delta=0.015)
        self.assertAlmostEqual(actual_time / expected_time, 1.0, delta=0.015)


class AimingTest(unittest.TestCase):
    def test_target_within_range_is_hit(self):
        """조준 표(dt 0.05s)로 구한 고각으로 실제 적분(dt 0.01s)했을 때 표적 근처에 떨어져야 한다."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "aim.json"
            path.write_text(json.dumps({
                "name": "aim", "seed": 3, "height": {"flat_height_m": 0.0},
                "sites": [{"name": "S", "lat": 38.1, "lon": 127.0}],
                "targets": [{"name": "T", "lat": 37.7, "lon": 126.9}],
                "salvos": [{"site": "S", "kind": "LightRocket", "count": 1, "start_s": 0.0, "target": "T"}],
            }), encoding="utf-8")
            scenario = scenario_module.load_scenario(path)
            record, _ = runner.run_scenario(scenario)
            record.write(Path(directory) / "out", scenario, 1)
            events = np.loadtxt(Path(directory) / "out" / "events.csv", delimiter=",", skiprows=1, ndmin=2)
        impact = events[events[:, 2] == 1][0]
        miss_m = compute_ground_distance(37.7, 126.9, impact[3], impact[4])
        self.assertLess(miss_m, 1000.0)


class RecordTest(unittest.TestCase):
    def run_once(self, directory, name):
        path = write_scenario(directory, {
            "site": "S", "kind": "LightRocket", "count": 4, "start_s": 0.0, "interval_s": 0.7,
            "azimuth_deg": 90.0, "elevation_deg": 45.0, "azimuth_sigma_deg": 0.5,
            "elevation_sigma_deg": 0.5, "speed_sigma_ratio": 0.01, "site_spread_m": 30.0})
        scenario = scenario_module.load_scenario(path)
        record, track_count = runner.run_scenario(scenario)
        return record.write(Path(directory) / name, scenario, track_count, sample_formats=("csv", "bin"))

    def test_determinism_and_format(self):
        with tempfile.TemporaryDirectory() as directory:
            first = self.run_once(directory, "a")
            second = self.run_once(directory, "b")
            self.assertEqual(first["samples_sha256"], second["samples_sha256"])
            self.assertEqual(first["events_sha256"], second["events_sha256"])

            samples = np.loadtxt(Path(directory) / "a" / "samples.csv", delimiter=",", skiprows=1)
            for track_id in range(first["track_count"]):
                track = samples[samples[:, 1] == track_id]
                self.assertTrue(np.all(np.diff(track[:, 0]) > 0), "time must strictly increase per track")
                self.assertEqual(int(track[-1, 3]), 3, "last sample must be Impact")
                self.assertTrue(np.all(track[:-1, 3] != 3))
            sorted_keys = np.lexsort((samples[:, 0], samples[:, 1]))
            self.assertTrue(np.array_equal(sorted_keys, np.arange(len(samples))), "rows must be track-major")

            # 바이너리는 CSV와 같은 샘플을 같은 순서로 담아야 한다 (CSV는 소수 자릿수만큼 반올림).
            self.assertEqual(first["samples_bin_sha256"], second["samples_bin_sha256"])
            header, binary = recorder_module.read_binary_samples(Path(directory) / "a" / "samples.bin")
            self.assertEqual(int(header["sample_count"]), len(samples))
            self.assertTrue(np.array_equal(binary["time_us"], samples[:, 0].astype(np.int64)))
            self.assertTrue(np.array_equal(binary["track_id"], samples[:, 1].astype(np.int32)))
            self.assertTrue(np.array_equal(binary["phase"], samples[:, 3].astype(np.uint8)))
            self.assertTrue(np.allclose(binary["ecef"], samples[:, 4:7], atol=1e-3))
            self.assertTrue(np.allclose(binary["lat"], samples[:, 10], atol=1e-8))


class HeightGridTest(unittest.TestCase):
    def test_bilinear_lookup(self):
        with tempfile.TemporaryDirectory() as directory:
            header = {"format_version": 1, "south_lat": 35.0, "west_lon": 127.0,
                      "step_deg": 0.01, "rows": 2, "cols": 2}
            (Path(directory) / "grid.json").write_text(json.dumps(header), encoding="utf-8")
            np.array([0.0, 100.0, 200.0, 300.0], dtype="<f4").tofile(Path(directory) / "grid.bin")
            grid = heightgrid.HeightGrid(Path(directory) / "grid.json")
            self.assertAlmostEqual(float(grid.get_height(np.array([35.005]), np.array([127.005]))[0]), 150.0, places=3)


if __name__ == "__main__":
    unittest.main()
