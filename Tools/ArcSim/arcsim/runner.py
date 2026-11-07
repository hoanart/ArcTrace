"""시나리오 실행: 고정 dt RK4로 전 트랙을 함께 적분하고 기록 간격마다 샘플을 남긴다.

시간은 정수 마이크로초로만 진행한다 (부동소수 누적 오차 방지).
탄착은 지면 위 높이(AGL)가 0 이하가 된 스텝 안에서 선형 근사로 시각·위치를 구한다.
"""
import numpy as np

from . import dynamics
from . import geodesy
from . import heightgrid
from . import projectiles
from . import recorder as recorder_module
from . import scenario as scenario_module


def classify_phases(batch, flight_time, indices, position, velocity):
    lat, lon, _ = geodesy.ecef_to_lla(position)
    _, _, up = geodesy.enu_basis(lat, lon)
    vertical_speed = np.sum(velocity * up, axis=-1)
    phases = np.where(vertical_speed > 0.0, recorder_module.PHASE_ASCENT, recorder_module.PHASE_DESCENT)
    return np.where(batch.is_burning(flight_time, indices), recorder_module.PHASE_BOOST, phases)


def compute_height_above_ground(height_provider, position):
    lat, lon, alt = geodesy.ecef_to_lla(position)
    return alt - height_provider.get_height(lat, lon)


def run_scenario(scenario, include_earth_rotation=True, include_drag=True, progress=None):
    height_provider = heightgrid.create_height_provider(scenario.height_config, scenario.base_directory)
    plan = scenario_module.build_launch_plan(scenario, height_provider)
    track_count = len(plan.launch_time_us)
    if track_count == 0:
        raise ValueError("scenario has no launches")

    batch = dynamics.ProjectileBatch(projectiles.KINDS, plan.kind_indices,
                                     include_earth_rotation=include_earth_rotation,
                                     include_drag=include_drag)
    launch_mass = np.array([projectiles.KINDS[index].launch_mass_kg for index in plan.kind_indices])

    position = plan.position.copy()
    velocity = plan.velocity.copy()
    mass = launch_mass.copy()
    is_active = np.zeros(track_count, dtype=bool)
    is_finished = np.zeros(track_count, dtype=bool)

    dt_us = scenario.integration_dt_us
    dt_s = dt_us * 1e-6
    record = recorder_module.Recorder()

    time_us = int(plan.launch_time_us[0])
    end_limit_us = time_us + scenario.max_duration_us
    next_launch = 0

    while time_us <= end_limit_us:
        while next_launch < track_count and plan.launch_time_us[next_launch] == time_us:
            next_launch += 1
        newly_launched = np.flatnonzero((plan.launch_time_us == time_us) & ~is_active & ~is_finished)
        if newly_launched.size > 0:
            is_active[newly_launched] = True
            launch_phase = classify_phases(batch, np.zeros(newly_launched.size), newly_launched,
                                           position[newly_launched], velocity[newly_launched])
            record.add_samples(np.full(newly_launched.size, time_us), newly_launched,
                               plan.kind_indices[newly_launched], launch_phase,
                               position[newly_launched], velocity[newly_launched])
            record.add_events(np.full(newly_launched.size, time_us), newly_launched,
                              recorder_module.EVENT_LAUNCH, position[newly_launched])

        active = np.flatnonzero(is_active)
        if active.size == 0 and next_launch >= track_count:
            break

        next_time_us = time_us + dt_us
        if active.size > 0:
            flight_time = (time_us - plan.launch_time_us[active]) * 1e-6
            previous_position = position[active]
            previous_velocity = velocity[active]
            previous_height = compute_height_above_ground(height_provider, previous_position)

            new_position, new_velocity, new_mass = batch.step_rk4(
                previous_position, previous_velocity, mass[active], flight_time, dt_s, active)
            new_height = compute_height_above_ground(height_provider, new_position)

            has_impacted = new_height <= 0.0
            if np.any(has_impacted):
                impacted = active[has_impacted]
                height_before = previous_height[has_impacted]
                height_after = new_height[has_impacted]
                fraction = np.clip(height_before / (height_before - height_after), 0.0, 1.0)
                offset_us = np.maximum(np.rint(fraction * dt_us).astype(np.int64), 1)
                impact_time = time_us + offset_us
                impact_position = (previous_position[has_impacted]
                                   + fraction[:, None] * (new_position[has_impacted] - previous_position[has_impacted]))
                impact_velocity = (previous_velocity[has_impacted]
                                   + fraction[:, None] * (new_velocity[has_impacted] - previous_velocity[has_impacted]))
                record.add_samples(impact_time, impacted, plan.kind_indices[impacted],
                                   np.full(impacted.size, recorder_module.PHASE_IMPACT),
                                   impact_position, impact_velocity)
                record.add_events(impact_time, impacted, recorder_module.EVENT_IMPACT, impact_position)
                is_active[impacted] = False
                is_finished[impacted] = True

            survivors = ~has_impacted
            surviving = active[survivors]
            position[surviving] = new_position[survivors]
            velocity[surviving] = new_velocity[survivors]
            mass[surviving] = new_mass[survivors]

            if next_time_us % scenario.record_interval_us == 0 and surviving.size > 0:
                surviving_flight_time = (next_time_us - plan.launch_time_us[surviving]) * 1e-6
                phases = classify_phases(batch, surviving_flight_time, surviving,
                                         position[surviving], velocity[surviving])
                record.add_samples(np.full(surviving.size, next_time_us), surviving,
                                   plan.kind_indices[surviving], phases,
                                   position[surviving], velocity[surviving])

        time_us = next_time_us
        if progress is not None and time_us % 10_000_000 == 0:
            progress(time_us, int(is_active.sum()), int(is_finished.sum()), track_count)

    unfinished = np.flatnonzero(~is_finished)
    if unfinished.size > 0:
        raise RuntimeError(f"{unfinished.size} tracks did not impact within max_duration_s")
    return record, track_count
