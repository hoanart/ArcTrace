#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "Replay/ArcReplayTypes.h"

#include "ArcProjectileView.generated.h"

// 투사체 한 개의 화면 상태. 표현 구현(ISM, Mass 등)에 넘기는 데이터 전용 구조체.
// 좌표는 ECEF(m)로 넘긴다: 언리얼 좌표는 Cesium 원점 이동에 따라 바뀌므로 각 구현이 자기 기준으로 변환한다.
struct FArcProjectileViewState
{
	int32 TrackIndex = INDEX_NONE;
	int32 KindIndex = INDEX_NONE;
	FVector PositionEcef = FVector::ZeroVector;
	FVector VelocityEcef = FVector::ZeroVector;
	EArcProjectilePhase Phase = EArcProjectilePhase::Boost;
	bool bIsVisible = false;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UArcProjectileView : public UInterface
{
	GENERATED_BODY()
};

// 재연 상태 -> 화면 표현 경계. 식별은 트랙 번호로만 하고, 인스턴스 번호·엔티티 핸들은 구현 안에 숨긴다.
// 구현을 바꿔도(ISM -> Mass) 재연 코어와 디렉터는 바뀌지 않는다.
class ARCTRACE_API IArcProjectileView
{
	GENERATED_BODY()

public:
	// 기록이 바뀔 때 한 번: 트랙 수·종류만큼 표현 자원을 미리 확보한다.
	virtual void InitializeView(const FArcReplayRecord& Record) = 0;

	// 바뀐 트랙만 모아서 한 번에 넘긴다. 구현은 렌더 상태 갱신을 호출당 한 번으로 묶는다.
	virtual void ApplyStates(const TArray<FArcProjectileViewState>& States) = 0;

	virtual void ResetView() = 0;
};
