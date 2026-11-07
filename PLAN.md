# ArcTrace 진행 계획

탄도 투사체 대량 시뮬레이션 · 결정적 재연 시스템 (UE 5.8, C++). 규칙과 컨벤션은 `CLAUDE.md`, 측정 기록은 `Docs/PERF_LOG.md`.

## 목표 (포트폴리오)
게임사 클라이언트·렌더링 프로그래머 지원용. 보여줄 것은 **최적화 증명**: 측정 → 원인 → 구조 변경 → 수치.
- 메인 스토리: 같은 시나리오를 모드 A(액터 + 매 프레임 CPU 궤적)와 모드 B(ISM + 커스텀 데이터 + 시간 머티리얼)로 돌려 비교.
- 보조 사례: 프로파일링으로 찾은 병목 제거 (사례 1·2, 아래).

## 구조
- `Tools/ArcSim` (Python): 시나리오 → 기록 폴더 `Data/Records/<이름>` (형식: `Tools/ArcSim/FORMAT.md`). 기록: `sample_salvo`(26발, 0~156s), `stress_600`(600발, 0~194s, t≈70s에 600발 동시 비행).
- 시나리오: 발사 진지는 DMZ 북측(위도 38.0~38.33), 표적은 서울(`Seoul` 37.5665/126.978, 포탄은 `SeoulNorth` 37.63/126.93). `arcsim/aiming.py`가 종류별 사거리-고각 표로 방위·고각을 자동 계산 (조준 산포 `aim_sigma_m`). 가상 제원 최대 사거리: HeavyRocket ≈145km, LightRocket ≈85km, Shell ≈52km. 탄착 오차 중앙값 1.3km.
- `Source/ArcTrace` (C++): 재연 코어(`Replay/`) · 시각화(`View/`) · UI(`UI/`)
- 지형·영상: Cesium ion (Google Photorealistic 3D Tiles). 고도 캐시 굽기는 하지 않기로 함.
- 레벨: `/Game/CesiumSettings/ArcTrace/Main` (에디터 시작 맵). 머티리얼: `/Game/ArcTrace/Materials/M_ArcInstance`

## 단계
| # | 내용 | 상태 |
|---|---|---|
| 1 | 기록 포맷 명세 | 완료 |
| 2 | Python 탄도 시뮬레이터 (3DOF, RK4, ECEF/WGS84) | 완료 |
| 3 | 재연 코어 (비동기 로더, int64 µs 시계, 증분 전진/이진 탐색, 이벤트, 콘솔, 자동 테스트) | 완료 |
| 4 | 시각화 모드 B + 카메라 + 타임라인 UI | 완료 |
| 5 | 측정 장치 + 사례 1·2 + 모드 A 비교 | **진행 중 ← 다음 세션 시작점** |
| 5b | 탄별 라벨 UI (Tacview식 데이터 블록 + 지시선, 단일 SWidget OnPaint) | 완료 (화면 확인) |
| 6 | README · 사례 문서 · 시연 영상 · 공개 전 점검 | |
| 7 | (선택) Translucent vs Masked GPU 사례, Mass 뷰, 요격탄(모드 C) | |

## 완료된 것 (4단계 요약)
- 뷰 인터페이스 `IArcProjectileView` (나중에 Mass·모드 A로 교체 가능)
- `AArcProjectileIsmView`: 탄 종류별 ISM, ECEF → 앵커 로컬 변환, 변경 트랙만 갱신, `MarkRenderStateDirty` 1회
- `AArcTrajectoryHism`: 궤적 HISM(선분 = 큐브 인스턴스), 커스텀 데이터 [0]노출시각 [1..3]색, MID `CurrentTime`으로 되감기 지원
- `M_ArcInstance`: Masked/Unlit, 시간 노출 + **화면 픽셀 크기 고정 WPO**(정점별 깊이 기준, Instance 공간). 파라미터 `ScreenPixels`·`ScaleAxisMask`·`MeshReferenceSize`·`EmissiveIntensity`(0.7)
- `AArcReplayDirector`: 기록 로드 → 원점 재설정 → 뷰 구성 → DynamicPawn을 70km 개관 위치로 이동 → 타임라인 생성
- `UArcTimelineWidget`: C++ 구성 UMG (재생/일시정지, 시간 슬라이더, 배속 x0.25~16, 비행 수)
- `CesiumSunSky.AerialPerspectiveViewDistanceScale` = 0.25 (원거리 헤이즈 감소)

## 다음 세션 시작점: 규모 확장 600 → 2000 → 4000 (5단계보다 먼저)
유도탄·요격은 넣지 않는다(교전 표현이 목적이 아님). 탄 수만 늘린다.
1. **바이너리 기록 `.arcrec`**: CSV 로드 후 바이너리 캐시 생성 또는 ArcSim이 직접 출력. 4000발 CSV는 약 740MB 예상 → 크기·로드 시간 비교 수치 기록.
2. **시나리오 단계적 확장**: `stress_600`(유지) → `stress_2000` → `stress_4000`. 기존 진지·서울 표적 유지, 피크 시 대부분 동시 비행하도록 발사 시각 배치. 2000에서 GPU·메모리 추이를 보고 4000 진행 여부·대응 결정. 규모별 비용 곡선(600/2000/4000)은 포폴 자료로 남긴다.
   - 사양: i7-11370H, RAM 32GB, RTX 3050 Ti Laptop(VRAM 4GB) — 궤적 GPU 비용과 VRAM이 관건.
   - **시나리오 선택 UI**: 타임라인 바에 기록 선택 콤보박스(UMG `UComboBoxString`, `Data/Records` 폴더 목록) → 선택 시 `StartLoadRecord`. 콘솔 `ArcTrace.Load <이름>`과 Director `RecordName`(시작 기록)은 유지.
   - 생성 시간 약 10분 예상 → 원격 셸 3분 제한 때문에 사용자가 터미널에서 직접 실행하거나 진지별 분할 생성 후 병합.
3. **궤적 인스턴스**: 0.5초 간격이면 약 71만 개 예상. 그대로 측정 후 판단.
   - GPU 사례 후보: 미노출 선분도 매 프레임 정점 셰이더(WPO)·래스터화·clip 비용을 냄 → WPO에서 미노출 선분을 크기 0으로 접어 래스터화 제거 → 전후 GPU 패스 수치.
   - 대안(비교용): 트랙당 리본 메시 1개(정점에 시각) → 인스턴스 4000개.
4. 이후 아래 5단계 재측정·모드 A를 **4000발 기준**으로 진행 (측정 1회로 끝내기 위해).

## 5단계: 남은 작업 (순서대로)
0. **기록을 새로 생성했으므로(서울 표적) 기존 PERF_LOG 수치는 이전 데이터 기준** — 아래 측정은 새 기록으로 다시 잡는다.
1. **사례 1 같은 세션 A/B 확정** — `stress_600`, `Seek 70` + `Pause`, 개관 시점, Cesium 타일 안정 후
   - `ArcTrace.OverlayLighting 1` → `csvprofile frames=300` → `0` → 300 → `1` → 300 (재현 확인)
   - 같은 상태에서 `ProfileGPU`로 패스별 GPU ms 분해 (Lumen/RT/BasePass 등)
   - 병목 판정 기록: Frame/GT/RT/GPU 비교 + `r.ScreenPercentage 50` 테스트
2. **사례 2 확인** — 재생 중 300프레임: `ArcTrace/GameThread/ReplaySeek`가 매 프레임 사라졌는지, `ApplyTracks`·`IsmApplyStates` 비용
3. **모드 A 구현** — `IArcProjectileView` 구현체: 탄 = 액터(컴포넌트) 600개, 궤적 = 매 프레임 CPU 샘플링으로 다시 그림. Director에서 모드 전환(속성 + 콘솔). 같은 조건으로 A/B 측정
4. Standalone(`-game`)에서 한 번 더 측정해 PIE와 경향 일치 확인

## 사례 기록 (포폴용)
- **사례 1: 오버레이 인스턴스의 레이트레이싱·Lumen 등록 제거.** 탄·궤적(인스턴스 10만+)이 RT 장면·DF·Lumen GI·반사 캡처에 등록돼 있었음. `ArcView::ConfigureOverlayComponent`로 제외. 세션이 다른 비교에서 GPU 17.0→8.7ms, 렌더 스레드 Visibility 대기 11.9→≈0ms, RT 지오메트리 13.6→7.4MB — **Cesium 타일 상태가 달라 미확정, 같은 세션 A/B 필요**. 렌더 스레드(CPU) 개선이 핵심일 수 있으니 "GPU 개선"으로 단정하지 말 것.
- **사례 2: 계측으로 찾은 UI 되먹임.** 측정 스코프를 넣자 재생 중 `ReplaySeek`(600트랙 이진 탐색 + 전체 상태 반영)이 300/300 프레임에서 실행(평균 0.68ms). 원인: 타임라인이 `USlider::SetValue`로 표시 갱신 → `OnValueChanged` → 탐색. 수정: 드래그 중(`bIsScrubbing`)일 때만 탐색. **수정 후 수치 측정 필요.**

## 6단계 체크리스트
- README: 문제 → 측정 → 원인 → 수정 → 결과 → 배운 점. 사례당 한 장, 제목에 결과 수치, Insights 전후 캡처 + 표 + 핵심 코드 5~10줄, 측정 환경(i7-11370H 노트북, UE 5.8) 명시
- 시연 영상: 개관 → 근접 → 되감기 → 모드 전환 + 수치 오버레이
- 공개 전 점검: ion 토큰 미포함, 회사·실제 체계 명칭 없음, 발사 지역 표현

## 5b. 탄별 라벨 UI
- 구현: `SArcTrackLabelOverlay` (`UI/ArcTrackLabelOverlay`). Director `bShowTrackLabels`. 내용: `HR-007  상태` / 위경도 / 고도(km)·속도(m/s). 상태: Boost→초기비행, Ascent·Descent→비행, Impact→폭파(탄착 후 5초). 데이터 블록 최대 40개, 카메라 가까운 순으로 4방향(우상·좌상·우하·좌하) 중 겹치지 않는 자리에 배치(사각형 교차 검사), 자리 없으면 심볼만. 측정 스코프 `LabelBuild`·`LabelPaint`.
- 형태: 탄 심볼 위에 지시선 + 데이터 블록 (ID/종류, 고도·속도·상승/하강, 탄착까지 남은 시간). 종류별 색.
- 구현: 탄마다 위젯을 만들지 않고 **단일 커스텀 SWidget(SLeafWidget) 오버레이의 OnPaint**에서 전체를 그린다 (박스·텍스트·선 `FSlateDrawElement`). 화면 좌표 투영은 게임 스레드에서 배열로 미리 계산.
- 정리 규칙: 600개 전부 상세 라벨은 읽을 수 없으므로 상세는 선택·근접 N개, 나머지는 짧은 태그 또는 숨김. 화면 격자 버킷으로 겹침 회피.
- 측정 비교 후보(포폴): 탄별 UMG/WidgetComponent vs 단일 OnPaint — 라벨 수별 Slate Prepass·Paint ms.
