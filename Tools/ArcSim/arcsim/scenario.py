"""시나리오 JSON -> 발사 목록. 난수는 시드 하나로 고정해 결정성을 보장한다."""
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from . import aiming
from . import geodesy
from . import projectiles

LAUNCHER_HEIGHT_M = 2.0


@dataclass
class Scenario:
    name: str
    seed: int
    integration_dt_us: int
    record_interval_us: int
    max_duration_us: int
    sites: dict
    targets: dict
    salvos: list
    height_config: dict
    base_directory: Path


@dataclass
class LaunchPlan:
    """트랙별 발사 조건 (배열 길이 = 트랙 수, 발사 시각 오름차순)."""
    launch_time_us: np.ndarray
    kind_indices: np.ndarray
    position: np.ndarray
    velocity: np.ndarray


def load_scenario(path):
    path = Path(path)
    data = json.loads(path.read_text(encoding="utf-8"))
    scenario = Scenario(
        name=data["name"],
        seed=int(data["seed"]),
        integration_dt_us=int(data.get("integration_dt_us", 10000)),
        record_interval_us=int(data.get("record_interval_us", 100000)),
        max_duration_us=int(round(float(data.get("max_duration_s", 600.0)) * 1e6)),
        sites={site["name"]: site for site in data["sites"]},
        targets={target["name"]: target for target in data.get("targets", [])},
        salvos=data["salvos"],
        height_config=data.get("height"),
        base_directory=path.parent,
    )
    if scenario.record_interval_us % scenario.integration_dt_us != 0:
        raise ValueError("record_interval_us must be a multiple of integration_dt_us")
    return scenario


def get_range_table(range_tables, scenario, kind_index):
    """조준이 필요한 종류만 표를 만든다. 기준 지점은 발사 진지들의 평균 위치."""
    if kind_index not in range_tables:
        reference_lat = float(np.mean([site["lat"] for site in scenario.sites.values()]))
        reference_lon = float(np.mean([site["lon"] for site in scenario.sites.values()]))
        range_tables[kind_index] = aiming.build_range_table(kind_index, reference_lat, reference_lon)
    return range_tables[kind_index]


def build_launch_plan(scenario, height_provider):
    rng = np.random.default_rng(scenario.seed)
    range_tables = {}
    out_of_range_count = 0
    launch_times = []
    kinds = []
    positions = []
    velocities = []

    for salvo in scenario.salvos:
        site = scenario.sites[salvo["site"]]
        kind_index = projectiles.get_kind_index_by_name(salvo["kind"])
        spec = projectiles.KINDS[kind_index]
        count = int(salvo["count"])
        spread_m = float(salvo.get("site_spread_m", 0.0))

        site_ecef = geodesy.lla_to_ecef(site["lat"], site["lon"], 0.0)
        east, north, _ = geodesy.enu_basis(site["lat"], site["lon"])

        for round_index in range(count):
            launch_s = float(salvo["start_s"]) + round_index * float(salvo.get("interval_s", 0.0))
            launch_us = int(round(launch_s * 1e6 / scenario.integration_dt_us)) * scenario.integration_dt_us

            offset_east = rng.normal(0.0, spread_m)
            offset_north = rng.normal(0.0, spread_m)
            ground_ecef = site_ecef + east * offset_east + north * offset_north
            lat, lon, _ = geodesy.ecef_to_lla(ground_ecef)
            ground_height = float(height_provider.get_height(np.array([lat]), np.array([lon]))[0])
            position = geodesy.lla_to_ecef(lat, lon, ground_height + LAUNCHER_HEIGHT_M)

            # 표적이 있으면 표적(+조준 산포) 방향·거리로 방위/고각을 정하고, 없으면 시나리오 값을 쓴다.
            if "target" in salvo:
                target = scenario.targets[salvo["target"]]
                aim_sigma_m = float(salvo.get("aim_sigma_m", 0.0))
                target_ecef = geodesy.lla_to_ecef(target["lat"], target["lon"], 0.0)
                target_east, target_north, _ = geodesy.enu_basis(target["lat"], target["lon"])
                aim_ecef = (target_ecef + target_east * rng.normal(0.0, aim_sigma_m)
                            + target_north * rng.normal(0.0, aim_sigma_m))
                solution = aiming.solve_aim(get_range_table(range_tables, scenario, kind_index),
                                            position, lat, lon, aim_ecef)
                out_of_range_count += int(solution.is_out_of_range)
                base_azimuth = solution.azimuth_deg
                base_elevation = solution.elevation_deg
            else:
                base_azimuth = float(salvo["azimuth_deg"])
                base_elevation = float(salvo["elevation_deg"])
            azimuth = base_azimuth + rng.normal(0.0, float(salvo.get("azimuth_sigma_deg", 0.0)))
            elevation = base_elevation + rng.normal(0.0, float(salvo.get("elevation_sigma_deg", 0.0)))
            # speed_scale: 장약 등급처럼 발사 속도를 고정 비율로 조정 (기본 1.0)
            speed_scale = float(salvo.get("speed_scale", 1.0)) * (
                1.0 + rng.normal(0.0, float(salvo.get("speed_sigma_ratio", 0.0))))

            local_east, local_north, local_up = geodesy.enu_basis(lat, lon)
            azimuth_rad = np.radians(azimuth)
            elevation_rad = np.radians(elevation)
            direction = (local_north * np.cos(elevation_rad) * np.cos(azimuth_rad)
                         + local_east * np.cos(elevation_rad) * np.sin(azimuth_rad)
                         + local_up * np.sin(elevation_rad))

            launch_times.append(launch_us)
            kinds.append(kind_index)
            positions.append(position)
            velocities.append(direction * spec.launch_speed_mps * speed_scale)

    if out_of_range_count > 0:
        print(f"[ArcSim] 표적이 최대 사거리 밖인 발사 {out_of_range_count}발: 최대 사거리 고각으로 표적 방향 사격")
    order = np.argsort(np.asarray(launch_times, dtype=np.int64), kind="stable")
    return LaunchPlan(
        launch_time_us=np.asarray(launch_times, dtype=np.int64)[order],
        kind_indices=np.asarray(kinds, dtype=np.int32)[order],
        position=np.asarray(positions, dtype=np.float64)[order],
        velocity=np.asarray(velocities, dtype=np.float64)[order],
    )
