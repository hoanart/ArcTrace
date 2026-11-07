"""ISA 표준대기 (0~86 km 지오포텐셜 층 모델). 그 위는 지수 감쇠."""
import numpy as np

GAS_CONSTANT_AIR = 287.05287
GAMMA_AIR = 1.4
G0 = 9.80665
EARTH_RADIUS_GEOPOTENTIAL = 6356766.0

# (기저 지오포텐셜 고도 m, 기저 온도 K, 기온 감률 K/m, 기저 기압 Pa)
ISA_LAYERS = np.array([
    [0.0, 288.15, -0.0065, 101325.0],
    [11000.0, 216.65, 0.0, 22632.06],
    [20000.0, 216.65, 0.001, 5474.889],
    [32000.0, 228.65, 0.0028, 868.0187],
    [47000.0, 270.65, 0.0, 110.9063],
    [51000.0, 270.65, -0.0028, 66.93887],
    [71000.0, 214.65, -0.002, 3.956420],
])
ISA_TOP = 84852.0


def density_and_sound_speed(altitude_m):
    """기하 고도(m) -> (공기밀도 kg/m^3, 음속 m/s). 배열 입력."""
    geometric = np.clip(altitude_m, -1000.0, None)
    geopotential = EARTH_RADIUS_GEOPOTENTIAL * geometric / (EARTH_RADIUS_GEOPOTENTIAL + geometric)
    clipped = np.clip(geopotential, None, ISA_TOP)
    layer_index = np.searchsorted(ISA_LAYERS[:, 0], clipped, side="right") - 1
    layer_index = np.clip(layer_index, 0, len(ISA_LAYERS) - 1)
    base_height = ISA_LAYERS[layer_index, 0]
    base_temperature = ISA_LAYERS[layer_index, 1]
    lapse = ISA_LAYERS[layer_index, 2]
    base_pressure = ISA_LAYERS[layer_index, 3]
    delta_height = clipped - base_height
    temperature = base_temperature + lapse * delta_height

    has_lapse = lapse != 0.0
    safe_lapse = np.where(has_lapse, lapse, 1.0)
    pressure_gradient = base_pressure * (base_temperature / temperature) ** (G0 / (GAS_CONSTANT_AIR * safe_lapse))
    pressure_isothermal = base_pressure * np.exp(-G0 * delta_height / (GAS_CONSTANT_AIR * base_temperature))
    pressure = np.where(has_lapse, pressure_gradient, pressure_isothermal)

    density = pressure / (GAS_CONSTANT_AIR * temperature)
    above_top = geopotential > ISA_TOP
    density = np.where(above_top, density * np.exp(-(geopotential - ISA_TOP) / 6000.0), density)
    sound_speed = np.sqrt(GAMMA_AIR * GAS_CONSTANT_AIR * temperature)
    return density, sound_speed
