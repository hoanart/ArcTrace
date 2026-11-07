# 성능 측정 기록

측정 방법: PIE(에디터 뷰포트), `csvprofile frames=300`, 결과 CSV는 `Saved/Profiling/CSV/`.
화면: 70km 개관 시점, Cesium Google Photorealistic 3D Tiles.

## 2026-10-31 · 모드 B (ISM 탄 + HISM 궤적 + 시간 머티리얼 + 화면 크기 고정 WPO)

시나리오 `stress_600`: 600발, 1,200 이벤트, 샘플 531,732개, 0~178.3s.
- 기록 로드(비동기, 83MB CSV): 2.42s
- 궤적 선분 생성: 106,472개 / 0.05s
- t≈62~70s 구간(동시 비행 최대 600발)

| 상태 | Frame | Game | Render | GPU | DrawCalls |
|---|---|---|---|---|---|
| 재생 x1 (탄 600개 매 프레임 갱신) | 19.27 ms (p95 21.60) | 17.40 | 18.11 | 16.97 | 729 |
| 일시정지 (갱신 없음) | 19.54 ms (p95 22.30) | 17.28 | 17.53 | 16.84 | 732 |

- 재생/정지 차이가 측정 오차 범위 → 재연 갱신 비용은 프레임 대비 무시할 수준.
- 프레임 시간은 에디터 + Cesium 타일 렌더링이 지배. 탄·궤적 드로우콜은 컴포넌트 수(종류 3 + HISM 1)만큼만 늘어난다.
- 다음: ArcTrace 자체 구간에 CSV/Insights 스코프를 넣어 갱신 비용을 직접 수치화하고, 모드 A(액터 탄 + 매 프레임 CPU 궤적)와 같은 조건으로 비교한다.

## 2026-11-01 · 오버레이 조명 참여 제외 + 측정 스코프 추가 후 (세션이 달라 미확정)

같은 시나리오·시각·카메라지만 **에디터를 재시작한 다른 세션**이라 Cesium 타일 로딩 상태가 다름 (PrimitivesDrawn 90.5k → 39.1k). 경향 참고용.

| 상태 | Frame | Game | Render 대기(Visibility) | GPU | DrawCalls | RT 지오메트리 |
|---|---|---|---|---|---|---|
| 수정 전, 재생 | 19.27 ms | 17.40 | 11.91 ms | 16.97 | 729 | 13.6 MB |
| 수정 후, 재생 | 16.92 ms (60fps 상한) | 13.28 | ≈0 ms | 8.74 | 470 | 7.4 MB |
| 수정 후, 정지 | 17.02 ms (60fps 상한) | 12.05 | ≈0 ms | 8.65 | 470 | 7.4 MB |

자체 구간 (수정 후, 재생, 프레임당 평균 / p95):
- `ApplyTracks` 0.39 / 0.65 ms, `IsmApplyStates` 0.32 / 0.54 ms, `ReplayTick` 0.10 / 0.33 ms
- `ReplaySeek` 0.68 / 0.86 ms — **재생 중 300/300 프레임에서 실행 → 버그 (사례 2)**. 타임라인 슬라이더 `SetValue` → `OnValueChanged` → 탐색 되먹임. 수정 완료(빌드됨), 수치 재측정 필요.

다음 측정 (같은 세션 A/B): `ArcTrace.OverlayLighting 1/0/1` 각 300프레임 + `ProfileGPU` 패스 분해. 절차는 `PLAN.md` 5단계.

## 2026-11-01 · 기록 재생성 (표적: 서울)
`stress_600`을 새 시나리오(서울 표적 조준, 가상 장거리 제원)로 다시 생성: 600발, 샘플 707,474개, 0~194.4s, 피크 t≈70s 600발 동시 비행.
**위 측정값은 모두 이전 기록 기준.** 사례 비교는 새 기록으로 처음부터 다시 측정한다.

## 2026-11-01 오전 · 바이너리 기록 + 2000발 (같은 세션 측정)

### 기록 로드: CSV vs 바이너리 (`stress_600` 동일 데이터, 2회씩)
| 형식 | 크기 | 로드 |
|---|---|---|
| samples.csv | 111.4 MB | 3.49 s / 2.84 s |
| samples.bin (84B 고정 레코드) | 59.4 MB | 0.32 s / 0.21 s |
`stress_2000`(바이너리 198MB, 샘플 236만): 로드 0.83 s, 궤적 472,309 구간 생성 + 뷰 구성 0.28 s.

### 사례: 라벨 오버레이 2000발 — 게임 스레드 병목 (t=95s 정지, 2000발 비행)
| 단계 | LabelBuild | GameThread |
|---|---|---|
| 처음 (탄마다 `ProjectWorldLocationToWidgetPosition` + 탄마다 문자열 생성·측정) | 27.4 ms | 48.1 ms |
| 투영: 뷰-투영 행렬·DPI 배율을 프레임당 1회 → 행렬 곱 (`FSceneView::ProjectWorldToScreen`) | 18.8 ms (수집 단계 1.05 ms) | 33 ms |
| 레이아웃: 박스 크기 고정 + 자리 검사 먼저, 자리를 얻은 ≤40개만 문자열 생성 | **1.2 ms** (수집 0.67 + 레이아웃 0.47) | **19 ms** |
- 원인 1: 위젯 투영 함수가 호출마다 로컬 플레이어 투영 데이터를 다시 구함 (2000회/프레임).
- 원인 2: 자리를 못 얻는 대다수 탄까지 3줄 Printf + 폰트 측정을 수행 (레이아웃 17.6 ms).

### 사례 1(레이트레이싱·Lumen 제외) 재검증 — 같은 세션 A/B
`ArcTrace.OverlayLighting 1` vs `0` (2000발, t=95 정지): GPU 41.1 vs 40.9 ms, 프레임 44.2 vs 44.2 ms → **차이 없음**.
→ 이전 기록(17.0 → 8.7 ms)은 세션 간 Cesium 타일 상태 차이였던 것으로 판단. 사례 1은 폐기 또는 "통제 없는 비교의 함정"으로만 기록.

### 다음 병목: 궤적 지오메트리 (2000발, 472k 구간, t=95 정지)
`r.GPUCsvStatsEnabled 1`로 패스별 GPU:
| 설정 | GPU | Basepass | RenderVelocities | Prepass | 렌더 스레드 Visibility 대기 |
|---|---|---|---|---|---|
| 기본 | 40.9 ms | 16.2 | **15.1** | 0.25 | **35.8 ms** |
| `r.Velocity.EnableVertexDeformation 0` | 37.1 ms | 14.5 | 0.15 | **14.0** | 32.4 ms |
- WPO 머티리얼 → 속도 패스에서 지오메트리를 한 번 더 그림. 속도를 끄면 같은 비용이 깊이 프리패스로 이동 → 근본 원인은 "수십만 구간을 두 번 그림".
- 렌더 스레드 Visibility 대기 32~36 ms: HISM CPU 클러스터 트리 컬링으로 추정.
- 조치(빌드 필요): 궤적 HISM → ISM(GPU Scene 인스턴스 컬링) 전환 스위치, 오버레이 깊이 프리패스 제외(`bRenderInDepthPass=false`), `r.Velocity.EnableVertexDeformation=0`(DefaultEngine.ini). A/B 콘솔: `ArcTrace.TrajectoryInstancing hism|ism`, `ArcTrace.OverlayDepthPass 0|1`.
- 결정(사용자): 궤적은 **HISM 유지**, HISM/ISM 비교는 하지 않음. ISM 경로는 콘솔(`ArcTrace.TrajectoryInstancing`)로만 남김.
- 결정: Unlit 발광 선이라 깊이 프리패스 불필요, WPO는 크기 맞춤용(정점 이동 없음)이라 속도 패스 불필요 → 둘 다 제외가 기본. 측정은 `ArcTrace.OverlayDepthPass 0|1`, `r.Velocity.EnableVertexDeformation 0|2`로 A/B.
- 남은 일: Rider 빌드 경고 1개 확인·제거.

## 2026-11-01 11시 · 깊이 프리패스·속도 패스 → Masked vs Translucent (2000발, t=95 정지, 같은 세션)
| 설정 | GPU | Basepass | Prepass | Velocity | 렌더 스레드 Visibility |
|---|---|---|---|---|---|
| Masked, 깊이O 속도O (원래) | 29.7 | 11.0 | 0.2 | 10.8 | 25.3 |
| Masked, 깊이O 속도X | 28.4 | 10.4 | 10.4 | 0.1 | 24.3 |
| Masked, `bRenderInDepthPass=false` 속도X | 28.2 | 10.4 | 10.4 | 0.1 | 23.7 |
- 속도 패스를 끄면 같은 비용이 깊이 프리패스로 이동. `bRenderInDepthPass=false`는 효과 없음(이 플래그는 메인 패스에 안 그리는 물체를 깊이에만 그리는 용도 — 프리패스 제외 수단이 아님).
- 원인: Opaque/Masked는 이 프로젝트 설정에서 깊이 프리패스에 강제 포함 → 궤적 47만 구간을 두 번 그림.

같은 세션 재측정 (Masked 기준 → 블렌드 모드만 Translucent로 변경, Opacity = saturate(노출식)):
| 블렌드 | Frame | GPU | 궤적 관련 GPU 패스 | 렌더 스레드 | Visibility 대기 |
|---|---|---|---|---|---|
| Masked | 36.7 ms | 33.7 ms | Prepass 12.9 + Basepass 12.8 | 34.5 ms | 28.7 ms |
| **Translucent** | **28.2 ms** | **22.0 ms** | Translucency 13.8 | **16.6 ms** | 18.8 ms |
- 지오메트리를 한 번만 그림. 겹침(오버드로우)은 있지만 3px Unlit 선이라 반투명 패스(13.8)가 베이스 패스 한 번(12.8)과 비슷 → 이중 드로우 제거 이득이 오버드로우 비용보다 큼.
- 프레임은 이제 게임 스레드(26 ms) 쪽이 상한 → 다음 확인 대상.
- 머티리얼 `M_ArcInstance` = Translucent로 저장. 다음 후보: 미노출 구간을 WPO에서 크기 0으로 접어 래스터·셰이딩 제거.
- 정리 필요: 효과 없는 `ArcTrace.OverlayDepthPass` / `SetOverlayDepthPass` 코드 제거.
- 결정(사용자, 11:58): 측정 후 머티리얼을 **Masked로 되돌림** (Translucent 결과는 비교 수치로만 보존). Opacity 출력에 연결한 Saturate 노드는 Masked에서는 쓰이지 않음 (노드 삭제는 크래시 위험이라 남겨 둠).

## 2026-11-01 12시 · Lumen GI·반사 끄기 확인 (2000발, t=95 정지, 같은 세션)
| 설정 | Frame | GPU | DiffuseIndirectDenoiser |
|---|---|---|---|
| Lumen GI + Lumen 반사 (현재) | 27.5 ms | 24.7 ms | 0.66 |
| `r.DynamicGlobalIlluminationMethod 0` + `r.ReflectionMethod 0` | 27.3 ms | 23.5 ms | 0.62 |
- 정상 상태에서 Lumen 비용은 약 1 ms 수준 → 끄는 이득 작음. 설정은 원래대로 되돌림.
- 사용자 Insights에서 본 `DiffuseIndirectAndAO 36 ms`는 정상 프레임이 아닌 순간 스파이크로 판단(로드·탐색 직후 Lumen 표면 캐시 재구성, 머티리얼 재컴파일 직후 등). 발생 시점 확인 필요.

## 2026-11-01 13시 · 재연 시각 MPC 전환 + 미노출 구간 WPO 접기 (구현, 측정 전)
- `MPC_ArcReplay`(스칼라 `CurrentTime`): Director가 시간 변경 시 이 값 하나만 갱신. 궤적 MID의 CurrentTime 갱신·`SetCurrentTime` 제거.
- `M_ArcInstance`: `Step(노출 시각, CurrentTime)` → OpacityMask/Opacity. 같은 Step으로 WPO를 `Lerp(-인스턴스 로컬 위치(월드), 화면 크기 오프셋, Step)` → 미노출 구간은 인스턴스 기준점으로 접혀 면적 0 (래스터·픽셀 셰이딩 제거).
- 효과 없던 깊이 프리패스 제외 코드(`SetOverlayDepthPass`, `ArcTrace.OverlayDepthPass`) 삭제.
- 측정 예정: 2000발 t=95 정지(절반가량 미노출), 재생 초반(대부분 미노출)에서 GPU 패스 비교.

## 2026-11-01 14시 · 렌더 스레드 "Visibility 대기"의 정체 = GPU 대기 (2000발, t=95 정지, MPC·WPO 접기 적용 후)
사용자 Insights: `VisibilityCommands 21.98 ms` → 워커의 `OcclusionCullPipe` → `GPUBound_WaitingForGPUForOcclusionQueries` → `RHIGetRenderQueryResult_GPU_Wait 21.58 ms`.
| 설정 | Frame | GPU | Visibility 대기 | Prepass | Basepass |
|---|---|---|---|---|---|
| 기본 | 31.5 ms | 27.6 ms | 22.8 ms | 8.37 | 8.32 |
| `r.VRS.Enable 0` | 32.1 ms | 28.1 ms | 18.2 ms | 8.72 | 8.53 |
| + `r.AllowOcclusionQueries 0` | 30.8 ms | 27.2 ms | **0.01 ms** | 8.22 | 8.29 |
- 렌더 스레드 대기는 HISM CPU 컬링이 아니라 **GPU 오클루전 쿼리 결과를 기다리는 시간**(앞서 HISM 컬링으로 본 추정은 틀림). 쿼리를 꺼도 프레임 동일 → **GPU 바운드 확정**.
- VRS(PrepareImageBasedVRS/ContrastAdaptiveShading)는 정상 프레임 영향 없음. 두 cvar 원복.
- GPU 최대 비용: 궤적 지오메트리 Prepass 8.3 + Basepass 8.3 ≈ 16.6 ms → 정점 처리 바운드(구간 47만 × 큐브 24정점 × 2패스, WPO 포함).
- 다음 후보: 구간 메시를 큐브(24정점) → 카메라를 향하는 리본 사각형(4정점, WPO로 화면 폭 펼침)으로 교체 → 정점 약 1/6.

## 2026-11-01 17시 · 궤적 구간 메시 큐브 → 리본 사각형, 발 수별 측정 (600/1000/1500/2000)
조건: PIE(에디터 뷰포트), 각 기록 t=95 정지(4개 기록 모두 전 발 비행 중), 오버뷰 카메라 동일, 같은 세션에서 `ArcTrace.TrajectoryShape cube` → `ribbon` 순서, `csvprofile frames=120`(앞 10프레임 제외), `r.GPUCsvStatsEnabled 1`.
기록: `stress_1000`, `stress_1500`을 `stress_2000`과 같은 발사 패턴(12개 일제사격, 0.25 s 간격)으로 일제사격당 발 수만 줄여 생성.

| 발 수 | 구간 수 | 모양 | GPU | Prepass | Basepass | 렌더 스레드 Visibility 대기 | Frame |
|---|---|---|---|---|---|---|---|
| 600 | 141,617 | 큐브 | 13.45 ms | 2.63 | 2.79 | 0.03 | 28.9 |
| 600 | 141,617 | **리본** | **9.07 ms** | 0.57 | 0.79 | 0.04 | 27.8 |
| 1000 | 236,053 | 큐브 | 18.12 ms | 4.69 | 4.78 | 0.02 | 26.0 |
| 1000 | 236,053 | **리본** | **9.52 ms** | 0.75 | 0.93 | 0.01 | 27.7 |
| 1500 | 354,052 | 큐브 | 23.80 ms | 7.11 | 7.26 | 14.1 | 26.5 |
| 1500 | 354,052 | **리본** | **9.34 ms** | 0.89 | 1.08 | 0.00 | 25.9 |
| 2000 | 472,309 | 큐브 | 30.94 ms | 9.92 | 10.18 | 26.2 | 33.5 |
| 2000 | 472,309 | **리본** | **10.02 ms** | 1.08 | 1.30 | 0.01 | 25.9 |

- 궤적 패스(Prepass+Basepass): 2000발 20.1 ms → 2.4 ms (약 1/8). 정점 24→4개, 삼각형 12→2개. 큐브는 발 수에 비례해 증가(구간 1만 개당 약 0.43 ms), 리본은 600→2000에서 1.36→2.38 ms로 거의 평탄.
- GPU 전체: 2000발 30.9 → 10.0 ms. 리본에서는 발 수와 무관하게 GPU 9~10 ms(나머지는 지형·하늘 등 고정 비용).
- 큐브 1500발부터 렌더 스레드가 GPU 오클루전 결과를 기다림(14~26 ms). 리본에서는 사라짐 → GPU 바운드 해소.
- 이제 프레임 상한은 게임 스레드(약 26 ms). 그중 `Exclusive/GameThread/UI` 약 18.8 ms = 에디터 UI(측정 중 Slate 관찰자 해제 후에도 남음) → 에디터 밖(패키징) 측정이 필요.
- 시각 확인: 2000발 t=95 리본 폭·색 큐브와 동일, t=30으로 되감기 시 지나온 구간만 보임. 근접(수 km 이내) 확인은 아직.
- 구현: `M_ArcRibbon`(M_ArcInstance 복제, WPO 커스텀 노드만 교체: 구간 중심선 정점 → 시선×구간 방향 외적 방향으로 화면 폭의 절반씩), 메시 `/Engine/BasicShapes/Plane`(4정점). `ArcTrace.TrajectoryShape <cube|ribbon>`, 기본값은 아직 cube.

## 2026-11-01 17시 20분 · 60fps 가능성 확인 (2000발 t=95, 같은 세션)
사용자 Insights(17:16): GPU 프레임 약 20 ms, 상위에 RayTracingScene 3.5 / RayTracingSkyLight 1.9 / DenoiseSkyLight 1.2 / VRS·CAS 3.5, 게임 스레드 Slate::Tick 12 ms. 단 그 시점 궤적은 cube(17:15 전환)였고 트레이스 중이었음.
| 설정 | GPU | Prepass | Basepass | 게임 스레드 | Frame |
|---|---|---|---|---|---|
| cube, RT 켬 | 20.13 | 6.01 | 6.04 | 39.2 | 39.2 |
| ribbon, RT 켬 | 11.38 | 1.17 | 1.24 | 53.6 | 53.8 |
| ribbon, `r.RayTracing.Enable 0` | **8.76** | 1.05 | 1.18 | 34.3 | 34.3 |
- 레이 트레이싱이 아직 켜져 있음(`DefaultEngine.ini`의 `r.RayTracing=True`). 런타임으로 끄면 GPU −2.6 ms(RayTracingSkylight, DiffuseIndirectDenoiser, RayTracingScene 제거).
- 리본 + RT 끔에서 GPU 8.8 ms → 60fps 예산(16.7 ms) 안. **60fps를 막는 것은 GPU가 아니라 게임 스레드**: `Exclusive/GameThread/UI` 18.8 ms(에디터 Slate), 자체 라벨 합계 약 4.4 ms(Build 1.86 / Collect 1.15 / Paint 0.72 / Layout 0.63).
- 이 측정 구간의 게임 스레드 변동(34~54 ms)은 Insights 트레이스 동시 실행 등 에디터 환경 영향으로 판단 → 최종 숫자는 패키징 빌드에서.
- `r.RayTracing.Enable 0`은 이 세션에만 적용 상태로 둠.

## 2026-11-01 17시 40분 · 사용자 Insights 재확인 + CAS 끄기 (2000발 t=95, 리본)
- 17:15~17:20 구간은 사용자 입력 `ArcTrace.TrajectoryShape ribborn`(오타)이 명령 처리에서 조용히 cube로 넘어가 **큐브 상태**였음 → 명령이 알 수 없는 값을 거부하도록 수정(`TrajectoryInstancing`도 동일).
- 사용자 트레이스: 비동기 컴퓨트 큐 `PrepareImageBasedVRS → ContrastAdaptiveShading 6.31 ms`, RayTracingScene 4.09 ms(RT 끄기 전 프레임).
| 설정 (리본, RT 끔) | GPU | Frame | 게임 스레드 |
|---|---|---|---|
| 기본 | 9.40 ms | 19.1 | 19.0 |
| `r.VRS.ContrastAdaptiveShading 0` + `r.VRS.EnableSoftware 0` | 8.76 ms | 20.8 | 20.7 |
- CAS는 비동기 큐에서 그래픽 작업과 겹쳐 돌아 실제 절감은 약 0.6 ms. Unlit 오버레이 위주 장면이라 끔.
- 적용(재시작 필요): `DefaultEngine.ini` `r.RayTracing=False`, `[SystemSettings] r.VRS.ContrastAdaptiveShading=0`. 궤적 기본 모양 = ribbon.
- 패키징 준비: `DefaultGame.ini`에 `+DirectoriesToAlwaysStageAsNonUFS=(Path="../Data/Records")`.

## 2026-11-01 19시 · 패키징 빌드(Development) 2000발: 해상도 의존 GPU 비용 → 동적 해상도
- 사용자 Insights(패키징, 전체화면): 프레임 약 22 ms(40~45 fps), 게임 스레드 22 ms 중 `GameThreadWaitForTask`/`Sync_RenderingThread` 약 15 ms → GPU 바운드. GPU 약 20 ms: Lumen `DiffuseIndirectAndAO`(ScreenProbeGather) 7~9.5 ms, 스카이라이트 실시간 캡처 `CaptureConvolveSkyEnvMap` 7.5 ms(번갈아), PrePass 5.9 / BasePass 4.6 ms(지형 타일 포함), FXSystemPreRender 3.7 ms.
- 에디터 뷰포트 측정(Lumen 끄기 −1 ms)과 결과가 달랐던 이유: 에디터 뷰포트는 화면 일부·내부 해상도가 낮음 → 픽셀 비례 비용이 작게 보임. **최종 판단은 패키징 빌드에서.**
- 해상도를 낮추면 GPU 프레임 약 13 ms, 궤적 패스 PrePass 1.2 / BasePass 1.5 ms → 남은 비용은 해상도 비례(픽셀) 비용.
- 적용: `r.AntiAliasingMethod=4`(TSR, 이전 0=없음), `r.DynamicRes.OperationMode=2`, `FrameTimeBudget=16.6`, `Min/MaxScreenPercentage=50/100`. 측정 예정.
- 낮은 해상도에서 게임 스레드 13~22 ms: Slate `Paint Game UI` 3.5 ms(라벨 Build 2 ms 포함), `Slate::DrawWindows` 6.3 ms(GatherWindowElements 2.1 / AddElements 2.1), Cesium 타일 Tick 약 4 ms.
- 19시 30분 적용(측정 전): 라벨 심볼을 데이터 블록이 붙은 탄(최대 40)에만 그림 + 자리 40개가 차면 루프 종료 / `r.DynamicGlobalIlluminationMethod=0`, `r.ReflectionMethod=0`(Lumen GI·반사 끔) / `[SystemSettings] r.SkyLight.RealTimeReflectionCapture=0`. 확인할 것: 지형 밝기(스카이라이트 정적 캡처로 바뀜), TSR 잔상, 게임 스레드 Slate 비용.

## 2026-11-01 21시 40분 · 패키징 빌드 같은 세션 되돌리기 측정 (2000발 t=95 정지, 창 1333×999, VSync 끔)
조건: 재부팅·전원 최고 성능·에디터/OBS 끔, Development 패키징, 카메라 고정, `csvprofile frames=300`(앞 10프레임 제외). 패키징 빌드는 GPU 패스별 CSV 통계(`r.GPUCsvStatsEnabled`)가 꺼져 있어 전체 시간만 비교.
| 단계 | Frame | Game | Render | GPU | 렌더 타깃 풀 |
|---|---|---|---|---|---|
| ① 최종 (동적 해상도 켬, 실제 100% 유지) | 12.47 ms (80 fps) | 4.48 | 12.45 | 11.42 | 709 MB |
| ② 동적 해상도 끔 (100% 고정) | 12.96 ms | 4.74 | 12.96 | 11.90 | 709 MB |
| ③ + Lumen GI·반사·스카이 실시간 캡처 원복 | 15.95 ms (63 fps) | 4.89 | 15.96 | 14.61 | 1347 MB |
| ④ + 궤적 cube 원복 | (미측정) | | | | |
- 이 창 크기에서는 동적 해상도가 개입하지 않음(100%) → ①≈②.
- Lumen·스카이 캡처 비용: GPU +2.7 ms, 렌더 타깃 메모리 +638 MB(4 GB VRAM 노트북에서 큼).
- 이전 "패키징 전체화면 40 fps / GPU 20 ms"는 CPU 클럭 제한(0.79 GHz)·백그라운드 부하(핸들 858만, 가동 7일) 상태 + 전체화면 해상도 → 이 표와 조건이 달라 직접 비교하지 않는다.
- 남은 측정: ④ cube, 그리고 전체화면(실사용 해상도)에서 ①~④.

## 2026-11-01 21시 55분 · 같은 측정을 전체화면에서 (2000발 t=95 정지, 1600×1067, VSync 끔, 같은 세션)
| 단계 | Frame | Game | GPU | 내부 해상도 | 렌더 타깃 풀 | 프레임 수 |
|---|---|---|---|---|---|---|
| ① 최종 (동적 해상도) | **16.27 ms (61 fps)** | 6.67 | **15.13** | 68.5% | 804 MB | 156 |
| ② 동적 해상도 끔 (100%) | 18.09 ms | 7.30 | 16.88 | 100% | 790 MB | 31 (짧음) |
| ③ + Lumen·스카이 캡처 원복 | (CSV 0프레임, 실패) | | | | | 0 |
| ④ + Lumen·스카이 + 궤적 cube (= 최적화 전 렌더링 설정) | **39.30 ms (25 fps)** | 11.46 | **37.68** | 100% | 1457 MB | 159 |
- 전체화면 최종 vs 되돌린 상태: Frame 39.3 → 16.3 ms, GPU 37.7 → 15.1 ms, 렌더 타깃 메모리 1457 → 804 MB.
- 단계별 분리: Lumen·스카이 몫은 창 모드 측정(GPU +2.7 ms, +638 MB), 궤적 cube→ribbon 몫은 에디터 같은 세션 측정(2000발 GPU 30.9 → 10.0 ms) 참고. 전체화면 ③은 재측정 필요.
- 라벨 심볼 축소는 코드 변경이라 이 표의 ①~④ 모두에 적용된 상태(되돌리지 않음).
- 화면 크기에 따라 지형 타일 수가 크게 달라짐(Prims 창 6.7만 → 전체화면 17.5만): Cesium은 창 크기 기준으로 타일 디테일을 고르므로 동적 해상도와 무관하게 늘어난다.

## 2026-11-01 22시 · 리본 방식 갈래 A(탄젠트 리본) 추가 — B(M_ArcRibbon)는 그대로
- `M_ArcRibbonTangent` (M_ArcRibbon 복제, WPO 커스텀 노드만 교체): 입력 = 카메라 기준 월드 위치(`WPT_CameraRelativeNoOffsets`), 정점 탄젠트(`VertexTangentWS`), UV.y(가장자리 구분), ScreenPixels.
  - 옆 방향 = normalize(cross(카메라→정점, 탄젠트)), 평행 시 대체 축(예외 처리), 깊이 = dot(카메라→정점, ViewForward), 폭 = 픽셀 × WorldPerPixel.
  - 메시 원래 폭을 빼지 않는 대신 C++에서 폭 0(Y 스케일 1e-4)으로 깔아 "가장자리 정점 = 중심선" 전제를 맞춤.
  - 노출 Step·미노출 접기·색은 B와 동일.
- B와 차이: 인스턴스 변환 4번 → 변환 없음(정점 셰이더 ALU 감소 기대), 스플라인·절차적 리본에도 적용 가능. 대신 폭 0 메시가 필요하고 바운드 폭이 0.
- `ArcTrace.TrajectoryShape tangent`로 전환. 확인할 것: Plane 메시의 탄젠트가 +X, UV.y가 폭 방향인지(아니면 선이 안 보이거나 길이 방향으로 펼쳐짐), 평행 시점 깜빡임 없음, B와 GPU 비교.
