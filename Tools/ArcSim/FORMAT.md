# ArcTrace 기록 포맷 (v1)

외부 시뮬레이터(ArcSim)와 언리얼 재연기 사이의 계약. 이 문서가 바뀌면 `format_version`을 올린다.

## 파일 구성
하나의 기록 = 폴더 하나.

| 파일 | 내용 |
|---|---|
| `meta.json` | 버전, 시드, 시간 간격, 트랙 목록, 출력 해시 |
| `samples.csv` | 트랙별 상태 샘플 (트랙 ID → 시간 오름차순 정렬) — 선택 |
| `samples.bin` | 같은 샘플의 바이너리 판 (아래) — 선택. 둘 중 하나 이상 필수, 둘 다 있으면 재연기는 bin을 쓴다 |
| `events.csv` | 발사·탄착 이벤트 (시간 오름차순) |

## 공통 규칙
- 시간: `time_us` = 시나리오 시작 기준 **정수 마이크로초(int64)**.
- 좌표: **WGS84 ECEF, 미터, double**. `lat`/`lon`/`alt`는 확인용 중복 값(도, 도, 타원체고 m).
- 각도: 도(degree).
- 첫 줄은 헤더, 구분자 `,`, 소수점 `.`, 인코딩 UTF-8(BOM 없음).

## samples.csv
| 열 | 타입 | 설명 |
|---|---|---|
| time_us | int64 | 샘플 시각 |
| track_id | int32 | 0부터 시작하는 트랙 번호 |
| kind | int32 | 투사체 종류 (`meta.json`의 `kinds` 인덱스) |
| phase | int32 | 0 Boost, 1 Ascent, 2 Descent, 3 Impact |
| ecef_x, ecef_y, ecef_z | double | 위치 |
| vel_x, vel_y, vel_z | double | 지구 고정 좌표계 속도 (m/s) |
| lat, lon, alt | double | 위치의 위경도·타원체고 |
| heading | double | 진북 기준 시계방향 진행 방위 |
| pitch | double | 수평면 기준 비행경로각 (상승 +) |
| speed | double | 속력 (m/s) |

- 샘플은 `record_interval_us` 간격 격자 시각에 기록한다. 단, **발사 시각과 탄착 시각 샘플은 격자 밖일 수 있다.**
- 트랙의 마지막 샘플은 `phase = 3 (Impact)`.
- 재연 규칙: 시각 t의 트랙 상태 = `time_us <= t`인 마지막 샘플. 첫 샘플 이전이면 미존재. 보간하지 않는다.

## samples.bin
CSV 파싱 비용과 크기를 줄이기 위한 고정 길이 레코드. 리틀 엔디언, 패딩 없음. 샘플 순서·의미는 samples.csv와 같다.

헤더 32바이트:
| 오프셋 | 타입 | 내용 |
|---|---|---|
| 0 | char[4] | `ARCS` |
| 4 | uint32 | 버전 (1) |
| 8 | uint64 | sample_count |
| 16 | uint32 | record_size (84) |
| 20 | uint32, uint64 | 예약 (0) |

레코드 84바이트:
| 오프셋 | 타입 | 내용 |
|---|---|---|
| 0 | int64 | time_us |
| 8 | int32 | track_id |
| 12 | uint8 | kind |
| 13 | uint8 | phase |
| 14 | uint16 | 패딩 (0) |
| 16 | double[3] | ecef_x, ecef_y, ecef_z |
| 40 | float[3] | vel_x, vel_y, vel_z |
| 52 | double | lat |
| 60 | double | lon |
| 68 | float | alt |
| 72 | float | heading |
| 76 | float | pitch |
| 80 | float | speed |

## events.csv
| 열 | 타입 | 설명 |
|---|---|---|
| time_us | int64 | 이벤트 시각 |
| track_id | int32 | 트랙 |
| type | int32 | 0 Launch, 1 Impact |
| lat, lon, alt | double | 이벤트 위치 |

## meta.json
```json
{
  "format_version": 1,
  "generator": "ArcSim",
  "scenario": "sample_salvo",
  "seed": 1234,
  "integration_dt_us": 10000,
  "record_interval_us": 100000,
  "start_time_us": 0,
  "end_time_us": 123456789,
  "kinds": ["HeavyRocket", "LightRocket", "Shell"],
  "track_count": 24,
  "sample_count": 34567,
  "sample_formats": ["csv", "bin"],
  "samples_sha256": "...",
  "samples_bin_sha256": "...",
  "events_sha256": "..."
}
```
