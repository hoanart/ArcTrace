"""WGS84 좌표 변환 (LLA <-> ECEF) 과 국지 ENU 기저."""
import numpy as np

WGS84_A = 6378137.0
WGS84_F = 1.0 / 298.257223563
WGS84_B = WGS84_A * (1.0 - WGS84_F)
WGS84_E2 = WGS84_F * (2.0 - WGS84_F)
WGS84_EP2 = WGS84_E2 / (1.0 - WGS84_E2)


def lla_to_ecef(lat_deg, lon_deg, alt_m):
    """위도·경도(도), 타원체고(m) -> ECEF(m). 배열 입력 가능, (..., 3) 반환."""
    lat = np.radians(lat_deg)
    lon = np.radians(lon_deg)
    sin_lat = np.sin(lat)
    cos_lat = np.cos(lat)
    prime_vertical = WGS84_A / np.sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat)
    x = (prime_vertical + alt_m) * cos_lat * np.cos(lon)
    y = (prime_vertical + alt_m) * cos_lat * np.sin(lon)
    z = (prime_vertical * (1.0 - WGS84_E2) + alt_m) * sin_lat
    return np.stack([x, y, z], axis=-1)


def ecef_to_lla(ecef):
    """ECEF(m), (..., 3) -> (위도, 경도, 타원체고). Bowring 초기값 + 반복 보정."""
    x = ecef[..., 0]
    y = ecef[..., 1]
    z = ecef[..., 2]
    lon = np.arctan2(y, x)
    p = np.hypot(x, y)
    theta = np.arctan2(z * WGS84_A, p * WGS84_B)
    lat = np.arctan2(z + WGS84_EP2 * WGS84_B * np.sin(theta) ** 3,
                     p - WGS84_E2 * WGS84_A * np.cos(theta) ** 3)
    for _ in range(3):
        sin_lat = np.sin(lat)
        prime_vertical = WGS84_A / np.sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat)
        alt = p / np.cos(lat) - prime_vertical
        lat = np.arctan2(z, p * (1.0 - WGS84_E2 * prime_vertical / (prime_vertical + alt)))
    sin_lat = np.sin(lat)
    prime_vertical = WGS84_A / np.sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat)
    alt = p / np.cos(lat) - prime_vertical
    return np.degrees(lat), np.degrees(lon), alt


def enu_basis(lat_deg, lon_deg):
    """국지 East, North, Up 단위벡터 (ECEF 표현). 각각 (..., 3)."""
    lat = np.radians(lat_deg)
    lon = np.radians(lon_deg)
    sin_lat = np.sin(lat)
    cos_lat = np.cos(lat)
    sin_lon = np.sin(lon)
    cos_lon = np.cos(lon)
    east = np.stack([-sin_lon, cos_lon, np.zeros_like(lon)], axis=-1)
    north = np.stack([-sin_lat * cos_lon, -sin_lat * sin_lon, cos_lat], axis=-1)
    up = np.stack([cos_lat * cos_lon, cos_lat * sin_lon, sin_lat], axis=-1)
    return east, north, up


def heading_pitch_from_velocity(lat_deg, lon_deg, velocity):
    """ECEF 속도 -> (진북 기준 방위, 비행경로각) 도."""
    east, north, up = enu_basis(lat_deg, lon_deg)
    v_east = np.sum(velocity * east, axis=-1)
    v_north = np.sum(velocity * north, axis=-1)
    v_up = np.sum(velocity * up, axis=-1)
    heading = np.degrees(np.arctan2(v_east, v_north)) % 360.0
    pitch = np.degrees(np.arctan2(v_up, np.hypot(v_east, v_north)))
    return heading, pitch
