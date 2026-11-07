"""ECEF(지구 고정, 회전 좌표계) 3자유도 질점 운동방정식과 RK4 적분.

가속도 = 추력/질량 + 항력/질량 + 중력(J2) - 2ω×v - ω×(ω×r)
대기는 지구와 함께 회전한다고 가정(바람 없음)하므로 항력은 ECEF 속도 기준.
"""
import numpy as np

from . import atmosphere
from . import geodesy

EARTH_GM = 3.986004418e14
EARTH_J2 = 1.08262668e-3
EARTH_OMEGA = 7.2921150e-5
EARTH_OMEGA_VECTOR = np.array([0.0, 0.0, EARTH_OMEGA])


def compute_gravity(position):
    radius_squared = np.sum(position * position, axis=-1)
    radius = np.sqrt(radius_squared)
    z_ratio_squared = (position[..., 2] * position[..., 2]) / radius_squared
    j2_term = 1.5 * EARTH_J2 * (geodesy.WGS84_A * geodesy.WGS84_A) / radius_squared
    common = -EARTH_GM / (radius_squared * radius)
    gravity = np.empty_like(position)
    gravity[..., 0] = common * position[..., 0] * (1.0 + j2_term * (1.0 - 5.0 * z_ratio_squared))
    gravity[..., 1] = common * position[..., 1] * (1.0 + j2_term * (1.0 - 5.0 * z_ratio_squared))
    gravity[..., 2] = common * position[..., 2] * (1.0 + j2_term * (3.0 - 5.0 * z_ratio_squared))
    return gravity


class ProjectileBatch:
    """같은 시각에 함께 적분하는 투사체 묶음의 고정 물성(종류별 값을 개체별 배열로 펼친 것)."""

    def __init__(self, specs, kind_indices, include_earth_rotation=True, include_drag=True):
        self.reference_area = np.array([specs[index].reference_area_m2 for index in kind_indices])
        self.thrust = np.array([specs[index].thrust_n for index in kind_indices])
        self.burn_time = np.array([specs[index].burn_time_s for index in kind_indices])
        self.mass_flow = np.array([specs[index].mass_flow_kgps for index in kind_indices])
        self.kind_indices = np.asarray(kind_indices)
        self.drag_tables = [(np.asarray(spec.drag_mach), np.asarray(spec.drag_cd)) for spec in specs]
        self.include_earth_rotation = include_earth_rotation
        self.include_drag = include_drag

    def compute_drag_coefficient(self, mach, indices):
        drag_coefficient = np.empty_like(mach)
        kinds = self.kind_indices[indices]
        for kind_index, (mach_table, cd_table) in enumerate(self.drag_tables):
            mask = kinds == kind_index
            if np.any(mask):
                drag_coefficient[mask] = np.interp(mach[mask], mach_table, cd_table)
        return drag_coefficient

    def compute_derivatives(self, position, velocity, mass, flight_time, indices):
        """상태 미분 (dr/dt, dv/dt, dm/dt).

        indices: 이 호출에 포함된 트랙 번호 배열 (상태 배열과 같은 순서).
        flight_time: 발사 후 경과 시간(s).
        """
        acceleration = compute_gravity(position)

        speed = np.linalg.norm(velocity, axis=-1)
        safe_speed = np.where(speed > 1e-6, speed, 1.0)
        direction = velocity / safe_speed[:, None]

        is_burning = flight_time < self.burn_time[indices]
        thrust = np.where(is_burning, self.thrust[indices], 0.0)
        acceleration = acceleration + direction * (thrust / mass)[:, None]
        mass_rate = np.where(is_burning, -self.mass_flow[indices], 0.0)

        if self.include_drag:
            _, _, altitude = geodesy.ecef_to_lla(position)
            density, sound_speed = atmosphere.density_and_sound_speed(altitude)
            drag_coefficient = self.compute_drag_coefficient(speed / sound_speed, indices)
            drag_force = 0.5 * density * speed * speed * drag_coefficient * self.reference_area[indices]
            acceleration = acceleration - direction * (drag_force / mass)[:, None]

        if self.include_earth_rotation:
            coriolis = 2.0 * np.cross(EARTH_OMEGA_VECTOR, velocity)
            centrifugal = np.cross(EARTH_OMEGA_VECTOR, np.cross(EARTH_OMEGA_VECTOR, position))
            acceleration = acceleration - coriolis - centrifugal

        return velocity, acceleration, mass_rate

    def is_burning(self, flight_time, indices):
        return flight_time < self.burn_time[indices]

    def step_rk4(self, position, velocity, mass, flight_time, dt, indices):
        k1_r, k1_v, k1_m = self.compute_derivatives(position, velocity, mass, flight_time, indices)
        half = 0.5 * dt
        k2_r, k2_v, k2_m = self.compute_derivatives(
            position + half * k1_r, velocity + half * k1_v, mass + half * k1_m, flight_time + half, indices)
        k3_r, k3_v, k3_m = self.compute_derivatives(
            position + half * k2_r, velocity + half * k2_v, mass + half * k2_m, flight_time + half, indices)
        k4_r, k4_v, k4_m = self.compute_derivatives(
            position + dt * k3_r, velocity + dt * k3_v, mass + dt * k3_m, flight_time + dt, indices)
        sixth = dt / 6.0
        next_position = position + sixth * (k1_r + 2.0 * k2_r + 2.0 * k3_r + k4_r)
        next_velocity = velocity + sixth * (k1_v + 2.0 * k2_v + 2.0 * k3_v + k4_v)
        next_mass = mass + sixth * (k1_m + 2.0 * k2_m + 2.0 * k3_m + k4_m)
        return next_position, next_velocity, next_mass
