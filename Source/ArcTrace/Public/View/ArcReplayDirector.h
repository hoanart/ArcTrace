#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "View/ArcProjectileView.h"

#include "ArcReplayDirector.generated.h"

class ACesiumGeoreference;
class AArcTrajectoryHism;
class SArcTrackLabelOverlay;
class UMaterialParameterCollection;
class UArcTimelineWidget;
class UArcReplaySubsystem;

// 재연 코어와 화면 표현을 잇는다.
// - 기록 로드 시: Cesium 원점을 발사 지역 중심으로 옮기고 궤적을 깐다.
// - 표현 계층에는 ECEF 좌표를 넘긴다. 언리얼 좌표 변환은 각 표현의 지구 고정 앵커가 맡아 Cesium 원점 이동과 무관하다.
// - 재생 중: 샘플 인덱스가 바뀐 트랙만 골라 표현 계층(IArcProjectileView)에 일괄 전달한다.
// - 탐색 시: 전체 트랙 상태를 다시 전달한다.
UCLASS()
class ARCTRACE_API AArcReplayDirector : public AActor
{
	GENERATED_BODY()

public:
	AArcReplayDirector();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleRecordLoaded(const bool bSucceeded);
	void HandleSeeked(const int64 TimeUs);
	void HandleTimeChanged(const int64 TimeUs);

	void ResolveViewActors();
	void RecenterGeoreference(const FArcReplayRecord& Record);
	void PlaceOverviewCamera();
	void CreateTimeline();
	void CreateTrackLabelOverlay();
	void UpdateReplayTime(const int64 TimeUs);
	void ApplyTracks(const bool bApplyAllTracks);
	FArcProjectileViewState MakeViewState(const int32 TrackIndex, const int32 SampleIndex) const;

private:
	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	FString RecordName;

	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	bool bAutoLoad;

	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	bool bAutoPlay;

	// 원점을 발사 지역 중심으로 옮겨 원거리 좌표 정밀도 손실을 피한다.
	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	bool bRecenterGeoreference;

	// 기록 로드 후 조종 중인 폰(DynamicPawn)을 교전 영역 전체가 보이는 위치로 옮긴다. 이후 자유 조작.
	UPROPERTY(EditAnywhere, Category = "ArcTrace|Viewer")
	bool bUseOverviewCamera;

	// 교전 영역 중심 기준 카메라 위치(m). 국지 ENU: X 동, Y 북, Z 위.
	UPROPERTY(EditAnywhere, Category = "ArcTrace|Viewer", meta = (EditCondition = "bUseOverviewCamera"))
	FVector OverviewOffsetEnuMeters;

	UPROPERTY(EditAnywhere, Category = "ArcTrace|Viewer", meta = (EditCondition = "bUseOverviewCamera", ClampMin = "5.0", ClampMax = "120.0"))
	float OverviewFieldOfView;

	// 화면 하단에 재생 바(재생/일시정지, 탐색, 배속)를 띄운다.
	UPROPERTY(EditAnywhere, Category = "ArcTrace|Viewer")
	bool bShowTimeline;

	// 탄마다 화면 공간 라벨(심볼 + 지시선 + 데이터 블록)을 표시한다.
	UPROPERTY(EditAnywhere, Category = "ArcTrace|Viewer")
	bool bShowTrackLabels;

	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	TObjectPtr<ACesiumGeoreference> Georeference;

	// IArcProjectileView 를 구현한 액터. 비어 있으면 ISM 구현을 생성한다.
	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	TObjectPtr<AActor> ProjectileViewActor;

	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	TObjectPtr<AArcTrajectoryHism> Trajectory;

	// 재연 시각을 받는 전역 머티리얼 파라미터 (CurrentTime).
	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	TObjectPtr<UMaterialParameterCollection> ReplayParameters;

	UPROPERTY(Transient)
	TObjectPtr<UArcReplaySubsystem> ReplaySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UArcTimelineWidget> TimelineWidget;

	IArcProjectileView* ProjectileView;

	TSharedPtr<SArcTrackLabelOverlay> TrackLabelOverlay;

	TArray<int32> AppliedSampleIndices;
	TArray<FArcProjectileViewState> PendingStates;

	FDelegateHandle RecordLoadedHandle;
	FDelegateHandle SeekedHandle;
	FDelegateHandle TimeChangedHandle;
};
