#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "ArcTimelineWidget.generated.h"

class UArcReplaySubsystem;
class UButton;
class UComboBoxString;
class UHorizontalBox;
class USlider;
class UTextBlock;

// 재연 재생 바: 재생/일시정지, 시간 슬라이더(탐색), 배속, 현재 시각.
// 위젯 블루프린트 없이 위젯 트리를 C++에서 구성한다.
UCLASS()
class ARCTRACE_API UArcTimelineWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UArcTimelineWidget(const FObjectInitializer& ObjectInitializer);

	void SetReplaySubsystem(UArcReplaySubsystem* const InReplaySubsystem);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION()
	void HandlePlayPauseClicked();

	UFUNCTION()
	void HandleSlowerClicked();

	UFUNCTION()
	void HandleFasterClicked();

	UFUNCTION()
	void HandleSliderValueChanged(float Value);

	UFUNCTION()
	void HandleSliderCaptureBegin();

	UFUNCTION()
	void HandleSliderCaptureEnd();

	UFUNCTION()
	void HandleRecordSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

	void BuildWidgetTree();
	void PopulateRecordList();
	UButton* AddButton(UHorizontalBox& Row, const FText& Label, UTextBlock*& OutLabel);
	UTextBlock* AddText(UHorizontalBox& Row, const FText& Text);
	void StepPlaybackRate(const int32 Direction);
	void RefreshDisplay();

private:
	UPROPERTY(Transient)
	TObjectPtr<UArcReplaySubsystem> ReplaySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> RecordCombo;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PlayPauseLabel;

	UPROPERTY(Transient)
	TObjectPtr<USlider> TimeSlider;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TimeText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RateText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FlyingText;

	bool bIsScrubbing;
	bool bWasPlayingBeforeScrub;
};
