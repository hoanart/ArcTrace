#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/SLeafWidget.h"

class ACesiumGeoreference;
class APlayerController;
class UArcReplaySubsystem;

// 화면 공간 탄 라벨: 심볼 + 지시선 + 데이터 블록(ID·상태, 위경도, 고도·속도).
// 탄마다 위젯을 만들지 않고 이 위젯 하나의 OnPaint에서 그린다. 전 트랙을 투영해 가까운 순으로 자리를 잡고,
// 자리를 얻은 탄(최대 40)에만 심볼·데이터 블록을 그린다 (탄 자체는 3D 인스턴스가 그림).
// Tick에서 화면 투영·겹침 정리를 끝내고, OnPaint는 정리된 배열을 레이어별로 한 번에 그린다.
class ARCTRACE_API SArcTrackLabelOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SArcTrackLabelOverlay)
		{
		}
		SLATE_ARGUMENT(TWeakObjectPtr<APlayerController>, PlayerController)
		SLATE_ARGUMENT(TWeakObjectPtr<UArcReplaySubsystem>, ReplaySubsystem)
		SLATE_ARGUMENT(TWeakObjectPtr<ACesiumGeoreference>, Georeference)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	static constexpr int32 LINE_COUNT = 3;

	// 화면 안에 들어온 트랙. 카메라에 가까운 순서로 데이터 블록 자리를 먼저 차지한다.
	struct FLabelCandidate
	{
		int32 TrackIndex = INDEX_NONE;
		double DistanceSquared = 0.0;
		FVector2f ScreenPosition = FVector2f::ZeroVector;

		bool operator<(const FLabelCandidate& Other) const
		{
			return DistanceSquared < Other.DistanceSquared;
		}
	};

	struct FTrackLabel
	{
		FVector2f SymbolPosition = FVector2f::ZeroVector;
		FVector2f BoxPosition = FVector2f::ZeroVector;
		FVector2f BoxSize = FVector2f::ZeroVector;
		FLinearColor Color = FLinearColor::White;
		FString Lines[LINE_COUNT];
		bool bHasDataBlock = false;
	};

	void CollectCandidates(const FGeometry& AllottedGeometry);
	void BuildLabels();
	void FillDataBlock(const int32 TrackIndex, FTrackLabel* const OutLabel) const;

private:
	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<UArcReplaySubsystem> ReplaySubsystem;
	TWeakObjectPtr<ACesiumGeoreference> Georeference;

	FSlateFontInfo Font;
	float LineHeight = 0.0f;

	TArray<FLabelCandidate> Candidates;
	TArray<FTrackLabel> Labels;
};
