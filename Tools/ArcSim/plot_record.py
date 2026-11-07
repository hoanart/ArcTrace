"""기록 확인용 그래프: 사용법 python plot_record.py <기록폴더> [출력.png]"""
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

KIND_COLORS = ("tab:red", "tab:orange", "tab:blue")


def main():
    record_directory = Path(sys.argv[1])
    output_path = Path(sys.argv[2]) if len(sys.argv) > 2 else record_directory / "overview.png"
    samples = np.loadtxt(record_directory / "samples.csv", delimiter=",", skiprows=1)
    track_ids = samples[:, 1].astype(np.int64)
    figure, (ground_axis, profile_axis) = plt.subplots(1, 2, figsize=(14, 6))

    for track_id in np.unique(track_ids):
        track = samples[track_ids == track_id]
        color = KIND_COLORS[int(track[0, 2]) % len(KIND_COLORS)]
        ground_axis.plot(track[:, 11], track[:, 10], color=color, linewidth=0.6, alpha=0.6)
        ground_axis.plot(track[-1, 11], track[-1, 10], "x", color=color, markersize=3)
        elapsed = (track[:, 0] - track[0, 0]) * 1e-6
        profile_axis.plot(elapsed, track[:, 12] / 1000.0, color=color, linewidth=0.6, alpha=0.6)

    ground_axis.set_title("Ground track (x = impact)")
    ground_axis.set_xlabel("Longitude [deg]")
    ground_axis.set_ylabel("Latitude [deg]")
    ground_axis.set_aspect(1.0 / np.cos(np.radians(np.mean(samples[:, 10]))))
    profile_axis.set_title("Altitude vs flight time (red Heavy / orange Light / blue Shell)")
    profile_axis.set_xlabel("Flight time [s]")
    profile_axis.set_ylabel("Altitude [km]")
    profile_axis.grid(alpha=0.3)
    figure.tight_layout()
    figure.savefig(output_path, dpi=110)
    print(output_path)


if __name__ == "__main__":
    main()
