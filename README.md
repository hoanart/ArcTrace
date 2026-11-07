
# ArcTrace

탄 2000발과 궤적 47만 구간을 실사 지형 위에 재연하며, 측정으로 병목을 찾아 **25fps → 61fps**로 올린 언리얼 클라이언트 (UE 5.8, C++)

https://github.com/user-attachments/assets/773a3f52-4608-40cc-a633-89420d9f7cbc

| 항목 (2000발, 패키징 전체화면) | 전 | 후 |
| --- | --- | --- |
| 프레임 | 39.3ms (25fps) | 16.3ms (61fps) |
| GPU | 37.7ms | 15.1ms |
| 렌더 타깃 메모리 | 1457MB | 804MB |

## 문제

- 탄 2000발과 궤적 47만 구간을 한 화면에 그리면 25fps에 그쳤다.
- 병목 후보는 탄 위치 갱신(게임 스레드), 궤적 지오메트리(GPU), 라벨 UI(게임 스레드). 추정이 아니라 측정으로 가렸다.
- 게임의 대규모 전투 투사체와 궤적, 유닛 이름표 UI, 리플레이와 같은 종류의 문제다.

## 구조

- `Tools/ArcSim` (Python): 탄도를 미리 계산해 기록 파일로 만든다. 3DOF, RK4, WGS84 ECEF.
- 언리얼: 기록 전체를 비동기로 올리고, 현재 시각의 샘플을 골라 바뀐 트랙만 ISM에 반영한다.
- 궤적: HISM + 카메라를 향해 WPO로 펼치는 4정점 리본. 지형과 위성지도는 Cesium.

| 결정 | 고른 방식 | 버린 방식 | 이유 |
| --- | --- | --- | --- |
| 프레임당 갱신 대상 | 샘플이 바뀐 트랙만 | 매 프레임 전체 | 비용은 갱신 호출 수에 비례, 더티 플래그 횟수는 무관 (별도 벤치 측정) |
| 재생 위치 이동 | 그 시각 상태를 이진 탐색 | 처음부터 다시 재생 | 샘플마다 그 시각 상태가 있어 앞 기록이 필요 없음 |
| 샘플 사이 위치 | 마지막 샘플 그대로 | 보간 | 분석 목적이라 기록된 위치만 표시 (기록 간격 100ms) |

## 측정 결과

조건: i7-11370H / RTX 3050 Ti Laptop(4GB), Development 패키징, 1600×1067, `csvprofile frames=300`, 같은 세션 안에서만 A/B. 전체 기록은 [Docs/PERF_LOG.md](Docs/PERF_LOG.md).

| 항목 | 전 | 후 |
| --- | --- | --- |
| 궤적 GPU: 24정점 큐브 → 4정점 리본 (에디터) | 30.9ms | 10.0ms |
| 라벨 게임 스레드: 투영 행렬 1회 + 자리 먼저 검사 (에디터) | 48.1ms | 19ms |
| 재생 갱신 `ReplayTick` 평균 (패키징) | — | 0.33ms, 재생·정지 프레임 차이 없음 |

- **가설이 틀렸다.** 갱신 호출이 병목일 거라 봤지만 평균 0.33ms였다. 프로파일러가 가리킨 GPU와 UI로 옮겨 갔다.
- **통제 없는 비교를 버렸다.** 세션을 넘겨 본 17.0→8.7ms 개선은 같은 세션에서 41.1 vs 40.9ms로 차이가 없어 폐기했다.
- **추정을 측정으로 고쳤다.** 렌더 스레드 대기는 GPU 오클루전 쿼리 대기였다. GPU 바운드를 확인하고 정점 수를 줄였다.
- **최종 수치는 패키징에서 냈다.** 에디터에서 1ms로 보인 Lumen이 패키징에서는 GPU +2.7ms, 메모리 +638MB였다.

## 실행

1. UE 5.8, [Cesium for Unreal](https://cesium.com/platform/cesium-for-unreal/) 플러그인 설치. 프로젝트를 연 뒤 Cesium 패널에서 본인의 Cesium ion 계정으로 연결한다 (토큰은 저장소에 없음).
2. 기록 생성 (`Data/Records/`는 저장소에 없음):
   ```
   cd Tools/ArcSim
   pip install -r requirements.txt
   python run_sim.py scenarios/sample_salvo.json
   python run_sim.py scenarios/stress_2000.json
   ```
3. 실행 중 콘솔 명령:
   ```
   ArcTrace.Load stress_2000
   ArcTrace.Play / ArcTrace.Pause
   ArcTrace.Seek <초>
   ArcTrace.Rate <배속 0~64>
   ArcTrace.Status
   ArcTrace.TrajectoryShape cube|ribbon|tangent
   ```

## 한계와 AI 사용

- 기록 재생이라 네트워크 지연과 손실은 다루지 않는다. 노트북 한 대 기준이라 절대값보다 경향을 봐야 한다.
- 투사체 명칭과 제원은 교과서 수준의 가상 값이다. 회사 코드와 데이터는 사용하지 않았다.
- 요구사항, 채택·폐기 판단, 측정 실행과 Insights 분석은 직접 했다. 구현 코드, 측정 구간·콘솔 명령, CSV 집계, 문서 초안은 AI로 작성했다.
