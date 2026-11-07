"""투사체 종류 정의. 실제 체계가 아닌 교과서 수준의 일반값과 가상 명칭만 사용한다."""
from dataclasses import dataclass, field

import numpy as np


@dataclass(frozen=True)
class ProjectileSpec:
    name: str
    launch_mass_kg: float
    propellant_mass_kg: float
    diameter_m: float
    thrust_n: float
    burn_time_s: float
    launch_speed_mps: float  # 발사관/레일 이탈 속도 (포탄은 포구 속도)
    drag_mach: tuple = field(default=())
    drag_cd: tuple = field(default=())

    @property
    def reference_area_m2(self):
        return np.pi * 0.25 * self.diameter_m * self.diameter_m

    @property
    def mass_flow_kgps(self):
        if self.burn_time_s <= 0.0:
            return 0.0
        return self.propellant_mass_kg / self.burn_time_s


ROCKET_DRAG_MACH = (0.0, 0.6, 0.8, 0.95, 1.05, 1.2, 1.5, 2.0, 3.0, 4.0, 6.0)
ROCKET_DRAG_CD = (0.30, 0.30, 0.32, 0.45, 0.55, 0.52, 0.45, 0.38, 0.30, 0.26, 0.24)

SHELL_DRAG_MACH = (0.0, 0.6, 0.8, 0.95, 1.05, 1.2, 1.5, 2.0, 2.5, 3.0, 4.0)
SHELL_DRAG_CD = (0.14, 0.14, 0.15, 0.25, 0.38, 0.36, 0.32, 0.28, 0.25, 0.23, 0.21)

# 장거리 지역 표적을 가정한 가상 제원 (실제 체계와 무관한 교과서 수준 값).
# 최대 사거리(평지, 지구 회전·항력 포함): HeavyRocket ≈145km, LightRocket ≈85km, Shell ≈52km
SHELL_EXTENDED_DRAG_CD = tuple(coefficient * 0.75 for coefficient in SHELL_DRAG_CD)

KINDS = (
    ProjectileSpec(
        name="HeavyRocket",
        launch_mass_kg=1200.0,
        propellant_mass_kg=550.0,
        diameter_m=0.37,
        thrust_n=150000.0,
        burn_time_s=9.0,
        launch_speed_mps=35.0,
        drag_mach=ROCKET_DRAG_MACH,
        drag_cd=ROCKET_DRAG_CD,
    ),
    ProjectileSpec(
        name="LightRocket",
        launch_mass_kg=120.0,
        propellant_mass_kg=50.0,
        diameter_m=0.122,
        thrust_n=14000.0,
        burn_time_s=8.0,
        launch_speed_mps=30.0,
        drag_mach=ROCKET_DRAG_MACH,
        drag_cd=ROCKET_DRAG_CD,
    ),
    ProjectileSpec(
        name="Shell",
        launch_mass_kg=50.0,
        propellant_mass_kg=0.0,
        diameter_m=0.155,
        thrust_n=0.0,
        burn_time_s=0.0,
        launch_speed_mps=1100.0,
        drag_mach=SHELL_DRAG_MACH,
        drag_cd=SHELL_EXTENDED_DRAG_CD,
    ),
)

KIND_INDEX_BY_NAME = {spec.name: index for index, spec in enumerate(KINDS)}


def get_kind_index_by_name(name):
    if name not in KIND_INDEX_BY_NAME:
        raise ValueError(f"unknown projectile kind: {name}")
    return KIND_INDEX_BY_NAME[name]
