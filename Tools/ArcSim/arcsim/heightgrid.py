"""탄착 판정용 지형 고도 제공자.

HeightGrid 파일 형식 (언리얼 고도 캐시 굽기 도구가 생성):
  <name>.json : {"format_version":1, "south_lat":.., "west_lon":.., "step_deg":.., "rows":.., "cols":..}
  <name>.bin  : float32 little-endian, rows*cols, 남→북 행 순서, 각 행은 서→동, 타원체고(m)
"""
import json
from pathlib import Path

import numpy as np


class FlatHeightProvider:
    """모든 지점의 지형 고도를 상수(기본 0 = 타원체면)로 본다."""

    def __init__(self, height_m=0.0):
        self.height_m = float(height_m)

    def get_height(self, lat_deg, lon_deg):
        return np.full(np.shape(lat_deg), self.height_m)


class HeightGrid:
    """위경도 정격자 고도. 격자 사이는 쌍선형 보간(지형 표면 근사이며 재연 보간과 무관)."""

    def __init__(self, json_path):
        json_path = Path(json_path)
        header = json.loads(json_path.read_text(encoding="utf-8"))
        if header.get("format_version") != 1:
            raise ValueError(f"unsupported height grid version: {header.get('format_version')}")
        self.south_lat = float(header["south_lat"])
        self.west_lon = float(header["west_lon"])
        self.step_deg = float(header["step_deg"])
        self.rows = int(header["rows"])
        self.cols = int(header["cols"])
        raw = np.fromfile(json_path.with_suffix(".bin"), dtype="<f4")
        if raw.size != self.rows * self.cols:
            raise ValueError("height grid size mismatch")
        self.heights = raw.reshape(self.rows, self.cols).astype(np.float64)

    def get_height(self, lat_deg, lon_deg):
        row_f = (np.asarray(lat_deg) - self.south_lat) / self.step_deg
        col_f = (np.asarray(lon_deg) - self.west_lon) / self.step_deg
        row_f = np.clip(row_f, 0.0, self.rows - 1.000001)
        col_f = np.clip(col_f, 0.0, self.cols - 1.000001)
        row0 = np.floor(row_f).astype(np.int64)
        col0 = np.floor(col_f).astype(np.int64)
        row_t = row_f - row0
        col_t = col_f - col0
        h00 = self.heights[row0, col0]
        h01 = self.heights[row0, col0 + 1]
        h10 = self.heights[row0 + 1, col0]
        h11 = self.heights[row0 + 1, col0 + 1]
        south = h00 + (h01 - h00) * col_t
        north = h10 + (h11 - h10) * col_t
        return south + (north - south) * row_t


def create_height_provider(config, base_directory):
    if not config:
        return FlatHeightProvider(0.0)
    if "flat_height_m" in config:
        return FlatHeightProvider(config["flat_height_m"])
    return HeightGrid(Path(base_directory) / config["grid"])
