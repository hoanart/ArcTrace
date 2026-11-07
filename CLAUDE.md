# ArcTrace — AI 작업 가이드

탄도 투사체 대량 시뮬레이션 · 결정적 재연 시스템 (UE 5.8, C++). 개인 포트폴리오 프로젝트.

## 절대 규칙
- 회사 코드·명칭·데이터 사용 금지: 실제 체계명, 무기명, 제원, ICD 구조, 회사명을 코드·주석·애셋·문서 어디에도 넣지 않는다.
- 모든 코드는 새로 작성한다. 다른 프로젝트 코드를 복사하지 않는다.
- 투사체 제원은 공개 교과서 수준의 일반값과 가상 명칭만 사용한다.

## 구조
- `Tools/ArcSim`: 외부 탄도 시뮬레이터(Python, numpy). 시나리오 JSON → 기록 폴더(`Data/Records/<이름>`). 형식 계약은 `Tools/ArcSim/FORMAT.md`.
- `ArcTrace`(C++): 런타임 모듈. 기록 로더, 재생 시계, 재연(`Replay/`), 시각화(`View/`), UI(`UI/`). 진행 상황은 `PLAN.md`, 측정 기록은 `Docs/PERF_LOG.md`.
- 지형·영상: Cesium ion (Cesium World Terrain + 위성영상). 화면에 Cesium/영상 제공자 크레딧 표시 유지. ion 액세스 토큰은 저장소에 커밋하지 않는다.
- 고도 캐시 굽기는 하지 않는다. ArcSim은 평지(또는 시나리오 지정 고도)로 탄착을 계산한다.

## 재연 규칙 (핵심 요구사항)
- 시간은 `int64` 마이크로초. float/int32 시간 금지.
- 시각 t의 상태 = `UpperBound(t) - 1` 행. 보간·외삽 금지. 행이 없으면 이전 행.
- 탐색·되감기와 무관하게 궤적은 누적 유지 (궤적은 미리 깔고 머티리얼에서 시간으로 노출).
- 같은 시드·입력 → 바이트 단위 동일 출력 (결정성). 생성 상태 = 재연 상태를 자동 테스트로 검증.

## Sim 모델
- 3자유도 질점, RK4 고정 타임스텝, ECEF/WGS84 적분.
- 중력, ISA 대기밀도, 마하별 항력계수 테이블, 로켓 추력·질량 감소. 요격은 단순 비례항법.

## 렌더링 원칙
- 대량 객체는 ISM/HISM 풀. 매 프레임 액터 검색(GetActorOfClass 등) 금지.
- PerInstanceCustomData 갱신은 `bMarkRenderStateDirty=false`로 모은 뒤 `MarkRenderStateDirty()` 1회.
- 워커 스레드에서 UObject 접근 금지. 파싱은 순수 데이터, 등록은 게임 스레드.

## 코딩 컨벤션
우선순위: **언리얼 엔진 코딩 표준 > 포프 C++ 코딩 표준**. 충돌하면 언리얼을 따르고, 언리얼에 규정이 없는 부분만 포프 표준으로 보완한다. (Python 도구는 PEP 8을 따르되 람다 지양 원칙은 동일하게 적용한다.)

### 언리얼 표준 (기본)
- 타입 접두사: `A`/`U`/`F`/`E`/`T`/`I`/`S` 뒤에 프로젝트 접두사 `Arc`를 붙인다 (예: `UArcReplaySubsystem`, `AArcTrajectoryHism`). 엔진 타입과 이름 충돌을 막기 위함 (엔진에 `UReplaySubsystem`이 이미 있음). 파일명은 언리얼 규칙대로 타입 접두사만 뺀다 (`ArcReplaySubsystem.h`).
- 모든 식별자 PascalCase: 멤버·지역 변수·매개변수·private 메서드 포함. `m`/`s` 접두사 없음.
- bool은 `b` 접두사. 경로 등은 언리얼 타입(`FString`, `TArray`, `TMap`, `int32`, `int64`) 사용.
- assert는 `check`/`checkf`/`ensure`, 메모리는 `FMemory`, 소유 포인터는 `TUniquePtr`, UObject는 GC + `TObjectPtr`/`TWeakObjectPtr`.

### 포프 표준 (보완)
- 중괄호는 항상 새 줄, 본문이 한 줄이어도 중괄호 사용. 포인터·참조 기호는 자료형에 붙인다.
- 메서드명은 동사+목적어. bool 반환은 `Is`/`Can`/`Has`/`Should`. 재귀 함수는 `Recursive` 접미사.
- 함수 오버로딩 대신 이름으로 구분 (`GetTrackByIndex` / `GetTrackById`). const 반환용 오버로딩만 허용.
- 가능한 모든 곳에 `const` (지역 변수·매개변수 포함). 값 타입을 const로 반환하지 않는다.
- out 매개변수는 포인터 + `Out` 접두사, 함수 안에서 `check`로 null 검사.
- 상수는 `constexpr` + `ALL_CAPS` (예: `MAX_TRACK_COUNT`). 비트 플래그 enum은 `Flags` 접미사.
- `switch`에는 항상 `default`. 도달 불가면 `checkNoEntry()`.
- `auto`는 타입이 같은 줄에서 명확할 때만. `return std::move()` 금지 (NRVO, 반환문 하나).
- 한 줄에 변수 하나, 변수 가리기(shadowing) 금지, 초기화 리스트는 한 줄에 하나씩 `:`/`,` 앞에 둔다.
- 클래스 내부 순서: public 메서드 → protected 메서드 → private 메서드 → protected 변수 → private 변수.
- 외부 노출은 getter/setter. UPROPERTY는 private/protected + 필요 시 `meta=(AllowPrivateAccess="true")`.
- 데이터 전용 구조체는 함수 없이 public 멤버만. 단 언리얼이 요구하는 것(`operator==`, `GetTypeHash`, `operator<<`)은 허용.
- 클래스 하나당 파일 하나, 파일명 = 클래스명(접두사 제외, 언리얼 규칙). 전방선언 우선, 컨테이너는 `Reserve`.
- 컴파일 경고 0.

### 람다 지양
- 익명 람다를 쓰지 않는다.
- 델리게이트: `BindUObject`/`AddUObject`/`CreateUObject`/`BindRaw`로 이름 있는 멤버 함수를 바인딩.
- 정렬·탐색: `operator<` 정의 또는 멤버 포인터 projection (예: `Algo::UpperBoundBy(Frames, TimeUs, &FFrameRecord::TimeUs)`).
- `ParallelFor`·비동기 작업: 이름 있는 functor 구조체(`operator()`)를 정의해 전달.

### 기타
- 주석 처리된 죽은 코드를 남기지 않는다. 결정 근거는 짧은 주석으로.
- Tick은 필요한 곳만. 가능하면 Subsystem·이벤트 기반.

## 작업 환경 (AI 세션용)
- 경로: `E:\02_Project\01_Unreal\ArcTrace` (원격 셸에서는 `$HOME/mnt/ArcTrace`). 엔진: `E:\Epic Games\UE_5.8`.
- 빌드: 에디터를 닫은 뒤 Rider에서 빌드 → 솔루션 빌드. 라이브 코딩·핫 리로드는 메모리 부족으로 불안정하니 쓰지 않는다. 빌드 확인은 `Binaries/Win64/UnrealEditor-ArcTrace*.dll` 갱신 시각 + Rider 빌드 출력.
- 에디터 종료: MCP `SlateInspectorToolset.Windows` (action=close, index=0). 화면 클릭보다 안정적.
- 에디터 재실행은 사용자에게 요청한다 (꺼져 있으면 실행 권한을 받을 수 없음).
- 콘솔 명령: MCP Slate로 콘솔 입력창(`Cmd` 옆 textbox)을 `Click` 후 `Type(submit=true)`. ref는 `Snapshot`으로 확인.
- **머티리얼 노드 삭제 금지**: MCP `delete_expression`/`delete_unused_expressions`는 `!IsRooted()` assertion으로 에디터를 크래시시킨다. 노드는 추가·재연결만 한다.
- 커스텀 노드 `inputs` 배열은 한 번에 하나씩, 기존 원소를 그대로 포함해 늘린다.
- 측정: PIE에서 `csvprofile frames=300` → `Saved/Profiling/CSV/`. 자체 구간은 `ARC_SCOPED_PROFILE(Name)` (CSV `ArcTrace/GameThread/<Name>` + Insights `ArcTrace_<Name>`). 비교는 반드시 같은 세션·같은 장면 상태에서 콘솔 스위치로 한다.
- 콘솔 명령: `ArcTrace.Load/Play/Pause/Seek/Rate/Status`, `ArcTrace.OverlayLighting <0|1>`.
