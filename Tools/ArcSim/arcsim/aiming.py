"""표적 지점 조준: 종류별 사거리-고각 표를 시뮬레이션으로 만들고, 표적 거리에 맞는 고각을 고른다.

표는 기준 지점에서 방위 180°로 쏜 결과(평지, 항력·지구 회전 포함)이며 저각 분지(최대 사거리 고각 이하)만 쓴다.
교전 영역이 수십~100km 수준이라 위도·방위에 따른 사거리 차이는 표 오차보다 작다고 보고 무시한다.
표적이 최대 사거리 밖이면 최대 사거리 고각으로 쏜다(표적 방향으로 날아가 못 미쳐 떨어짐).
"""
from dataclasses import dataclass

import numpy as np

from . import dynamics
from . import geodesy
from . import projectiles

TABLE_ELEVATIONS_DEG = np.arange(12.0, 70.01, 1.0)
TABLE_DT_S = 0.05
TABLE_MAX_FLIGHT_S = 900.0
LAUNCHER_HEIGHT_M = 2.0


@dataclass
class RangeTable:
    elevations_deg: np.ndarray  # 저각 분지, 사거리 오름차순
    ranges_m: np.ndarray
    max_range_m: float
    max_range_elevation_deg: float


@dataclass
class AimSolution:
    azimuth_deg: float
    elevation_deg: float
    target_distance_m: float
    is_out_of_range: bool


def simulate_ranges(kind_index, reference_lat, reference_lon, elevations_deg):
    """고각 배열을 한 번에 적분해 지면 도달 거리(m)를 구한다."""
    count = len(elevations_deg)
    spec = projectiles.KINDS[kind_index]
    batch = dynamics.ProjectileBatch(projectiles.KINDS, np.full(count, kind_index))
    launch = geodesy.lla_to_ecef(reference_lat, reference_lon, LAUNCHER_HEIGHT_M)
    east, north, up = geodesy.enu_basis(reference_lat, reference_lon)

    elevation = np.radians(np.asarray(elevations_deg, dtype=np.float64))
    direction = -north[None, :] * np.cos(elevation)[:, None] + up[None, :] * np.sin(elevation)[:, None]
    position = np.repeat(launch[None, :], count, axis=0)
    velocity = direction * spec.launch_speed_mps
    mass = np.full(count, spec.launch_mass_kg)

    ranges = np.full(count, np.nan)
    is_done = np.zeros(count, dtype=bool)
    flight_time = 0.0
    while not is_done.all() and flight_time < TABLE_MAX_FLIGHT_S:
        active = np.flatnonzero(~is_done)
        new_position, new_velocity, new_mass = batch.step_rk4(
            position[active], velocity[active], mass[active], np.full(active.size, flight_time), TABLE_DT_S, active)
        _, _, altitude = geodesy.ecef_to_lla(new_position)
        position[active] = new_position
        velocity[active] = new_velocity
        mass[active] = new_mass

        landed = active[altitude <= 0.0]
        offset = position[landed] - launch
        ranges[landed] = np.hypot(offset @ east, offset @ north)
        is_done[landed] = True
        flight_time += TABLE_DT_S
    return ranges


def build_range_table(kind_index, reference_lat, reference_lon):
    ranges = simulate_ranges(kind_index, reference_lat, reference_lon, TABLE_ELEVATIONS_DEG)
    peak = int(np.nanargmax(ranges))
    low_elevations = TABLE_ELEVATIONS_DEG[:peak + 1]
    low_ranges = ranges[:peak + 1]
    order = np.argsort(low_ranges, kind="stable")
    return RangeTable(
        elevations_deg=low_elevations[order],
        ranges_m=low_ranges[order],
        max_range_m=float(ranges[peak]),
        max_range_elevation_deg=float(TABLE_ELEVATIONS_DEG[peak]),
    )


def compute_bearing_and_distance(launch_ecef, launch_lat, launch_lon, target_ecef):
    """발사 지점 국지 수평면 기준 방위(°, 진북 0)와 수평 거리(m)."""
    east, north, _ = geodesy.enu_basis(launch_lat, launch_lon)
    offset = target_ecef - launch_ecef
    offset_east = float(offset @ east)
    offset_north = float(offset @ north)
    azimuth = float(np.degrees(np.arctan2(offset_east, offset_north)) % 360.0)
    return azimuth, float(np.hypot(offset_east, offset_north))


def solve_aim(table, launch_ecef, launch_lat, launch_lon, target_ecef):
    azimuth, distance = compute_bearing_and_distance(launch_ecef, launch_lat, launch_lon, target_ecef)
    if distance >= table.max_range_m:
        return AimSolution(azimuth, table.max_range_elevation_deg, distance, True)
    elevation = float(np.interp(distance, table.ranges_m, table.elevations_deg))
    return AimSolution(azimuth, elevation, distance, False)
