"""기록 폴더(meta.json, samples.csv, events.csv) 작성. 형식은 FORMAT.md 참고."""
import hashlib
import json
from pathlib import Path

import numpy as np

from . import geodesy
from . import projectiles

FORMAT_VERSION = 1

PHASE_BOOST = 0
PHASE_ASCENT = 1
PHASE_DESCENT = 2
PHASE_IMPACT = 3

EVENT_LAUNCH = 0
EVENT_IMPACT = 1

SAMPLE_HEADER = ("time_us,track_id,kind,phase,ecef_x,ecef_y,ecef_z,vel_x,vel_y,vel_z,"
                 "lat,lon,alt,heading,pitch,speed")
SAMPLE_FORMAT = ("%d,%d,%d,%d,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,"
                 "%.9f,%.9f,%.4f,%.6f,%.6f,%.5f")
EVENT_HEADER = "time_us,track_id,type,lat,lon,alt"

# samples.bin: 헤더 32바이트 + 샘플당 84바이트 고정 레코드 (리틀 엔디언, 패딩 없음). FORMAT.md 참고.
BINARY_MAGIC = b"ARCS"
BINARY_VERSION = 1
BINARY_HEADER_DTYPE = np.dtype([
    ("magic", "S4"), ("version", "<u4"), ("sample_count", "<u8"),
    ("record_size", "<u4"), ("reserved", "<u4"), ("reserved2", "<u8"),
])
BINARY_SAMPLE_DTYPE = np.dtype([
    ("time_us", "<i8"), ("track_id", "<i4"), ("kind", "u1"), ("phase", "u1"), ("padding", "<u2"),
    ("ecef", "<f8", (3,)), ("velocity", "<f4", (3,)),
    ("lat", "<f8"), ("lon", "<f8"), ("alt", "<f4"),
    ("heading", "<f4"), ("pitch", "<f4"), ("speed", "<f4"),
])
assert BINARY_HEADER_DTYPE.itemsize == 32
assert BINARY_SAMPLE_DTYPE.itemsize == 84

SAMPLE_FORMAT_CSV = "csv"
SAMPLE_FORMAT_BINARY = "bin"
EVENT_FORMAT = "%d,%d,%d,%.9f,%.9f,%.4f"


class Recorder:
    """적분 중 샘플·이벤트를 모았다가 끝에 정렬해 파일로 쓴다."""

    def __init__(self):
        self.sample_chunks = []
        self.event_chunks = []

    def add_samples(self, time_us, track_ids, kinds, phases, position, velocity):
        self.sample_chunks.append((
            np.asarray(time_us, dtype=np.int64),
            np.asarray(track_ids, dtype=np.int64),
            np.asarray(kinds, dtype=np.int64),
            np.asarray(phases, dtype=np.int64),
            np.asarray(position, dtype=np.float64),
            np.asarray(velocity, dtype=np.float64),
        ))

    def add_events(self, time_us, track_ids, event_type, position):
        count = len(track_ids)
        self.event_chunks.append((
            np.asarray(time_us, dtype=np.int64),
            np.asarray(track_ids, dtype=np.int64),
            np.full(count, event_type, dtype=np.int64),
            np.asarray(position, dtype=np.float64),
        ))

    def write(self, output_directory, scenario, track_count, sample_formats=(SAMPLE_FORMAT_CSV,)):
        """sample_formats: "csv", "bin" 중 하나 이상. 둘 다 쓰면 같은 샘플을 두 형식으로 남긴다."""
        output_directory = Path(output_directory)
        output_directory.mkdir(parents=True, exist_ok=True)

        time_us = np.concatenate([chunk[0] for chunk in self.sample_chunks])
        track_ids = np.concatenate([chunk[1] for chunk in self.sample_chunks])
        kinds = np.concatenate([chunk[2] for chunk in self.sample_chunks])
        phases = np.concatenate([chunk[3] for chunk in self.sample_chunks])
        position = np.concatenate([chunk[4] for chunk in self.sample_chunks])
        velocity = np.concatenate([chunk[5] for chunk in self.sample_chunks])

        order = np.lexsort((time_us, track_ids))
        time_us = time_us[order]
        track_ids = track_ids[order]
        kinds = kinds[order]
        phases = phases[order]
        position = position[order]
        velocity = velocity[order]

        lat, lon, alt = geodesy.ecef_to_lla(position)
        heading, pitch = geodesy.heading_pitch_from_velocity(lat, lon, velocity)
        speed = np.linalg.norm(velocity, axis=-1)

        sample_hashes = {}
        if SAMPLE_FORMAT_CSV in sample_formats:
            sample_table = np.column_stack([
                time_us, track_ids, kinds, phases,
                position[:, 0], position[:, 1], position[:, 2],
                velocity[:, 0], velocity[:, 1], velocity[:, 2],
                lat, lon, alt, heading, pitch, speed,
            ])
            samples_path = output_directory / "samples.csv"
            np.savetxt(samples_path, sample_table, fmt=SAMPLE_FORMAT, header=SAMPLE_HEADER,
                       comments="", newline="\n", encoding="utf-8")
            sample_hashes["samples_sha256"] = compute_file_hash(samples_path)
        if SAMPLE_FORMAT_BINARY in sample_formats:
            binary_path = output_directory / "samples.bin"
            write_binary_samples(binary_path, time_us, track_ids, kinds, phases, position, velocity,
                                 lat, lon, alt, heading, pitch, speed)
            sample_hashes["samples_bin_sha256"] = compute_file_hash(binary_path)

        event_time = np.concatenate([chunk[0] for chunk in self.event_chunks])
        event_track = np.concatenate([chunk[1] for chunk in self.event_chunks])
        event_type = np.concatenate([chunk[2] for chunk in self.event_chunks])
        event_position = np.concatenate([chunk[3] for chunk in self.event_chunks])
        event_order = np.lexsort((event_type, event_track, event_time))
        event_lat, event_lon, event_alt = geodesy.ecef_to_lla(event_position[event_order])
        event_table = np.column_stack([
            event_time[event_order], event_track[event_order], event_type[event_order],
            event_lat, event_lon, event_alt,
        ])
        events_path = output_directory / "events.csv"
        np.savetxt(events_path, event_table, fmt=EVENT_FORMAT, header=EVENT_HEADER,
                   comments="", newline="\n", encoding="utf-8")

        meta = {
            "format_version": FORMAT_VERSION,
            "generator": "ArcSim",
            "scenario": scenario.name,
            "seed": scenario.seed,
            "integration_dt_us": scenario.integration_dt_us,
            "record_interval_us": scenario.record_interval_us,
            "start_time_us": int(time_us.min()),
            "end_time_us": int(time_us.max()),
            "kinds": [spec.name for spec in projectiles.KINDS],
            "track_count": int(track_count),
            "sample_count": int(len(time_us)),
            "sample_formats": list(sample_formats),
            **sample_hashes,
            "events_sha256": compute_file_hash(events_path),
        }
        (output_directory / "meta.json").write_text(
            json.dumps(meta, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        return meta


def write_binary_samples(path, time_us, track_ids, kinds, phases, position, velocity,
                         lat, lon, alt, heading, pitch, speed):
    samples = np.zeros(len(time_us), dtype=BINARY_SAMPLE_DTYPE)
    samples["time_us"] = time_us
    samples["track_id"] = track_ids
    samples["kind"] = kinds
    samples["phase"] = phases
    samples["ecef"] = position
    samples["velocity"] = velocity
    samples["lat"] = lat
    samples["lon"] = lon
    samples["alt"] = alt
    samples["heading"] = heading
    samples["pitch"] = pitch
    samples["speed"] = speed

    header = np.zeros(1, dtype=BINARY_HEADER_DTYPE)
    header["magic"] = BINARY_MAGIC
    header["version"] = BINARY_VERSION
    header["sample_count"] = len(time_us)
    header["record_size"] = BINARY_SAMPLE_DTYPE.itemsize
    with open(path, "wb") as file:
        file.write(header.tobytes())
        file.write(samples.tobytes())


def read_binary_samples(path):
    """검증·테스트용 읽기. (헤더, 샘플 구조체 배열)."""
    data = Path(path).read_bytes()
    header = np.frombuffer(data, dtype=BINARY_HEADER_DTYPE, count=1)[0]
    if header["magic"] != BINARY_MAGIC or header["record_size"] != BINARY_SAMPLE_DTYPE.itemsize:
        raise ValueError("not an ArcTrace samples.bin")
    samples = np.frombuffer(data, dtype=BINARY_SAMPLE_DTYPE, offset=BINARY_HEADER_DTYPE.itemsize,
                            count=int(header["sample_count"]))
    return header, samples


def compute_file_hash(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        while True:
            block = file.read(1 << 20)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()
