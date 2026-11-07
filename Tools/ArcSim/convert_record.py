"""기존 CSV 기록에 samples.bin을 추가한다 (재연기의 CSV/바이너리 로드 비교용).

사용법: python convert_record.py <기록폴더>
"""
import json
import sys
from pathlib import Path

import numpy as np

from arcsim import recorder


def main():
    directory = Path(sys.argv[1])
    table = np.loadtxt(directory / "samples.csv", delimiter=",", skiprows=1)
    binary_path = directory / "samples.bin"
    recorder.write_binary_samples(
        binary_path,
        table[:, 0].astype(np.int64), table[:, 1].astype(np.int64), table[:, 2].astype(np.int64),
        table[:, 3].astype(np.int64), table[:, 4:7], table[:, 7:10],
        table[:, 10], table[:, 11], table[:, 12], table[:, 13], table[:, 14], table[:, 15])

    meta_path = directory / "meta.json"
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    meta["sample_formats"] = ["csv", "bin"]
    meta["samples_bin_sha256"] = recorder.compute_file_hash(binary_path)
    meta_path.write_text(json.dumps(meta, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"[ArcSim] {directory.name}: samples.bin {binary_path.stat().st_size / 1e6:.1f}MB ({len(table)} samples)")


if __name__ == "__main__":
    main()
