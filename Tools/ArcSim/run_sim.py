"""사용법: python run_sim.py scenarios/sample_salvo.json [--out 출력폴더]"""
import argparse
import time
from pathlib import Path

from arcsim import runner
from arcsim import scenario as scenario_module


def print_progress(time_us, active_count, finished_count, track_count):
    print(f"  t={time_us / 1e6:7.1f}s  비행 {active_count:5d}  탄착 {finished_count:5d}/{track_count}")


def main():
    parser = argparse.ArgumentParser(description="ArcSim 탄도 시뮬레이터")
    parser.add_argument("scenario", help="시나리오 JSON 경로")
    parser.add_argument("--out", help="기록 폴더 (기본: ../../Data/Records/<시나리오명>)")
    parser.add_argument("--formats", default="csv,bin",
                        help="샘플 출력 형식: csv, bin 또는 csv,bin (기본 csv,bin). 대규모는 bin 권장")
    arguments = parser.parse_args()

    scenario = scenario_module.load_scenario(arguments.scenario)
    output_directory = Path(arguments.out) if arguments.out else (
        Path(__file__).resolve().parents[2] / "Data" / "Records" / scenario.name)

    print(f"[ArcSim] {scenario.name} (seed {scenario.seed})")
    started = time.perf_counter()
    record, track_count = runner.run_scenario(scenario, progress=print_progress)
    simulated = time.perf_counter()
    sample_formats = tuple(name.strip() for name in arguments.formats.split(",") if name.strip())
    meta = record.write(output_directory, scenario, track_count, sample_formats=sample_formats)
    finished = time.perf_counter()

    print(f"[ArcSim] 트랙 {meta['track_count']}개, 샘플 {meta['sample_count']}개, "
          f"구간 {meta['start_time_us'] / 1e6:.1f}~{meta['end_time_us'] / 1e6:.1f}s")
    print(f"[ArcSim] 적분 {simulated - started:.1f}s, 기록 {finished - simulated:.1f}s -> {output_directory}")


if __name__ == "__main__":
    main()
