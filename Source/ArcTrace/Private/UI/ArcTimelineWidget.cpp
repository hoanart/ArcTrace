#include "UI/ArcTimelineWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#include "Replay/ArcRecordLoader.h"
#include "Replay/ArcReplaySubsystem.h"

namespace
{
	constexpr double PLAYBACK_RATES[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
	constexpr int32 FONT_SIZE = 13;
	constexpr float BAR_MARGIN = 24.0f;
	constexpr float BAR_HEIGHT = 52.0f;

	int32 FindNearestRateIndex(const double Rate)
	{
		int32 BestIndex = 0;
		for (int32 Index = 1; Index < UE_ARRAY_COUNT(PLAYBACK_RATES); ++Index)
		{
			if (FMath::Abs(PLAYBACK_RATES[Index] - Rate) < FMath::Abs(PLAYBACK_RATES[BestIndex] - Rate))
			{
				BestIndex = Index;
			}
		}
		return BestIndex;
	}

	double ToSeconds(const int64 TimeUs)
	{
		return static_cast<double>(TimeUs) / ARC_MICROSECONDS_PER_SECOND;
	}
}

UArcTimelineWidget::UArcTimelineWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bIsScrubbing(false)
	, bWasPlayingBeforeScrub(false)
{
}

void UArcTimelineWidget::SetReplaySubsystem(UArcReplaySubsystem* const InReplaySubsystem)
{
	ReplaySubsystem = InReplaySubsystem;
}

TSharedRef<SWidget> UArcTimelineWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildWidgetTree();
	}
	return Super::RebuildWidget();
}

void UArcTimelineWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshDisplay();
}

void UArcTimelineWidget::BuildWidgetTree()
{
	UCanvasPanel* const Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Canvas;

	// 화면 하단에 가로로 늘어나는 반투명 바.
	UBorder* const Bar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Bar"));
	Bar->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	Bar->SetPadding(FMargin(12.0f, 6.0f));
	UCanvasPanelSlot* const BarSlot = Canvas->AddChildToCanvas(Bar);
	BarSlot->SetAnchors(FAnchors(0.0f, 1.0f, 1.0f, 1.0f));
	BarSlot->SetAlignment(FVector2D(0.0f, 1.0f));
	BarSlot->SetOffsets(FMargin(BAR_MARGIN, -BAR_MARGIN, BAR_MARGIN, BAR_HEIGHT));

	UHorizontalBox* const Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
	Bar->SetContent(Row);

	// 기록 선택: Data/Records 아래 폴더 목록. 고르면 그 기록을 다시 불러온다.
	RecordCombo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("RecordCombo"));
	PopulateRecordList();
	RecordCombo->OnSelectionChanged.AddDynamic(this, &UArcTimelineWidget::HandleRecordSelected);
	UHorizontalBoxSlot* const ComboSlot = Row->AddChildToHorizontalBox(RecordCombo);
	ComboSlot->SetVerticalAlignment(VAlign_Center);
	ComboSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));

	UTextBlock* Label = nullptr;
	UButton* const PlayPauseButton = AddButton(*Row, FText::FromString(TEXT("Play")), Label);
	PlayPauseButton->OnClicked.AddDynamic(this, &UArcTimelineWidget::HandlePlayPauseClicked);
	PlayPauseLabel = Label;

	TimeText = AddText(*Row, FText::FromString(TEXT("Loading...")));

	TimeSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("TimeSlider"));
	TimeSlider->SetMinValue(0.0f);
	TimeSlider->SetMaxValue(1.0f);
	TimeSlider->SetStepSize(0.0005f);
	TimeSlider->SetSliderBarColor(FLinearColor(0.3f, 0.3f, 0.3f));
	TimeSlider->SetSliderHandleColor(FLinearColor(1.0f, 0.6f, 0.1f));
	TimeSlider->OnValueChanged.AddDynamic(this, &UArcTimelineWidget::HandleSliderValueChanged);
	TimeSlider->OnMouseCaptureBegin.AddDynamic(this, &UArcTimelineWidget::HandleSliderCaptureBegin);
	TimeSlider->OnMouseCaptureEnd.AddDynamic(this, &UArcTimelineWidget::HandleSliderCaptureEnd);
	UHorizontalBoxSlot* const SliderSlot = Row->AddChildToHorizontalBox(TimeSlider);
	SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SliderSlot->SetVerticalAlignment(VAlign_Center);
	SliderSlot->SetPadding(FMargin(8.0f, 0.0f));

	UTextBlock* UnusedLabel = nullptr;
	AddButton(*Row, FText::FromString(TEXT("<<")), UnusedLabel)->OnClicked.AddDynamic(this, &UArcTimelineWidget::HandleSlowerClicked);
	RateText = AddText(*Row, FText::FromString(TEXT("x1")));
	AddButton(*Row, FText::FromString(TEXT(">>")), UnusedLabel)->OnClicked.AddDynamic(this, &UArcTimelineWidget::HandleFasterClicked);
	FlyingText = AddText(*Row, FText::GetEmpty());
}

void UArcTimelineWidget::PopulateRecordList()
{
	// 기록 폴더 이름 = 레코드 이름 (GetRecordDirectory의 역).
	const FString RecordsRoot = FPaths::GetPath(FArcRecordLoader::GetRecordDirectory(TEXT("Placeholder")));
	TArray<FString> RecordNames;
	IFileManager::Get().FindFiles(RecordNames, *FPaths::Combine(RecordsRoot, TEXT("*")), false, true);
	RecordNames.Sort();

	RecordCombo->ClearOptions();
	for (const FString& RecordName : RecordNames)
	{
		if (FPaths::FileExists(FPaths::Combine(RecordsRoot, RecordName, TEXT("meta.json"))))
		{
			RecordCombo->AddOption(RecordName);
		}
	}
}

UButton* UArcTimelineWidget::AddButton(UHorizontalBox& Row, const FText& Label, UTextBlock*& OutLabel)
{
	UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Button->SetBackgroundColor(FLinearColor(0.15f, 0.15f, 0.15f, 0.9f));

	OutLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	OutLabel->SetText(Label);
	FSlateFontInfo Font = OutLabel->GetFont();
	Font.Size = FONT_SIZE;
	OutLabel->SetFont(Font);
	OutLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Button->SetContent(OutLabel);

	UHorizontalBoxSlot* const ButtonSlot = Row.AddChildToHorizontalBox(Button);
	ButtonSlot->SetVerticalAlignment(VAlign_Center);
	ButtonSlot->SetPadding(FMargin(4.0f, 0.0f));
	return Button;
}

UTextBlock* UArcTimelineWidget::AddText(UHorizontalBox& Row, const FText& Text)
{
	UTextBlock* const TextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	TextBlock->SetText(Text);
	FSlateFontInfo Font = TextBlock->GetFont();
	Font.Size = FONT_SIZE;
	TextBlock->SetFont(Font);
	TextBlock->SetColorAndOpacity(FSlateColor(FLinearColor::White));

	UHorizontalBoxSlot* const TextSlot = Row.AddChildToHorizontalBox(TextBlock);
	TextSlot->SetVerticalAlignment(VAlign_Center);
	TextSlot->SetPadding(FMargin(8.0f, 0.0f));
	return TextBlock;
}

void UArcTimelineWidget::HandlePlayPauseClicked()
{
	if (ReplaySubsystem == nullptr || !ReplaySubsystem->HasRecord())
	{
		return;
	}

	if (ReplaySubsystem->IsPlaying())
	{
		ReplaySubsystem->Pause();
		return;
	}

	// 끝까지 재생한 뒤 다시 누르면 처음부터 재생한다.
	if (ReplaySubsystem->GetCurrentTimeUs() >= ReplaySubsystem->GetEndTimeUs())
	{
		ReplaySubsystem->SeekToTime(ReplaySubsystem->GetStartTimeUs());
	}
	ReplaySubsystem->Play();
}

void UArcTimelineWidget::HandleSlowerClicked()
{
	StepPlaybackRate(-1);
}

void UArcTimelineWidget::HandleFasterClicked()
{
	StepPlaybackRate(1);
}

void UArcTimelineWidget::HandleSliderValueChanged(float Value)
{
	// RefreshDisplay의 SetValue도 이 이벤트를 발생시킨다. 사용자가 조작 중일 때만 탐색해야
	// 재생 중 매 프레임 전체 재탐색(되먹임)이 일어나지 않는다.
	if (!bIsScrubbing || ReplaySubsystem == nullptr || !ReplaySubsystem->HasRecord())
	{
		return;
	}

	const int64 StartTimeUs = ReplaySubsystem->GetStartTimeUs();
	const int64 DurationUs = ReplaySubsystem->GetEndTimeUs() - StartTimeUs;
	ReplaySubsystem->SeekToTime(StartTimeUs + static_cast<int64>(static_cast<double>(DurationUs) * Value));
}

void UArcTimelineWidget::HandleSliderCaptureBegin()
{
	if (ReplaySubsystem == nullptr)
	{
		return;
	}

	// 드래그 중에는 멈춰 두고, 놓으면 이전 재생 상태로 돌아간다.
	bIsScrubbing = true;
	bWasPlayingBeforeScrub = ReplaySubsystem->IsPlaying();
	ReplaySubsystem->Pause();
}

void UArcTimelineWidget::HandleSliderCaptureEnd()
{
	bIsScrubbing = false;
	if (ReplaySubsystem != nullptr && bWasPlayingBeforeScrub)
	{
		ReplaySubsystem->Play();
	}
}

void UArcTimelineWidget::HandleRecordSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// 코드에서 현재 기록 이름으로 맞출 때(Direct)는 다시 불러오지 않는다.
	if (SelectionType == ESelectInfo::Direct || ReplaySubsystem == nullptr || SelectedItem.IsEmpty())
	{
		return;
	}
	ReplaySubsystem->StartLoadRecord(SelectedItem);
}

void UArcTimelineWidget::StepPlaybackRate(const int32 Direction)
{
	if (ReplaySubsystem == nullptr)
	{
		return;
	}

	const int32 CurrentIndex = FindNearestRateIndex(ReplaySubsystem->GetPlaybackRate());
	const int32 NextIndex = FMath::Clamp(CurrentIndex + Direction, 0, static_cast<int32>(UE_ARRAY_COUNT(PLAYBACK_RATES)) - 1);
	ReplaySubsystem->SetPlaybackRate(PLAYBACK_RATES[NextIndex]);
}

void UArcTimelineWidget::RefreshDisplay()
{
	if (TimeText == nullptr)
	{
		return;
	}

	if (ReplaySubsystem == nullptr || !ReplaySubsystem->HasRecord() || ReplaySubsystem->IsLoading())
	{
		TimeText->SetText(FText::FromString(TEXT("Loading...")));
		return;
	}

	const FString& CurrentRecordName = ReplaySubsystem->GetRecord().ScenarioName;
	if (!RecordCombo->IsOpen() && RecordCombo->GetSelectedOption() != CurrentRecordName)
	{
		RecordCombo->SetSelectedOption(CurrentRecordName);
	}

	const int64 StartTimeUs = ReplaySubsystem->GetStartTimeUs();
	const int64 DurationUs = FMath::Max<int64>(ReplaySubsystem->GetEndTimeUs() - StartTimeUs, 1);
	const int64 ElapsedUs = ReplaySubsystem->GetCurrentTimeUs() - StartTimeUs;

	if (!bIsScrubbing)
	{
		TimeSlider->SetValue(static_cast<float>(static_cast<double>(ElapsedUs) / static_cast<double>(DurationUs)));
	}
	TimeText->SetText(FText::FromString(FString::Printf(TEXT("%6.1f / %.1f s"), ToSeconds(ElapsedUs), ToSeconds(DurationUs))));
	PlayPauseLabel->SetText(FText::FromString(ReplaySubsystem->IsPlaying() ? TEXT("Pause") : TEXT("Play")));
	RateText->SetText(FText::FromString(FString::Printf(TEXT("x%g"), ReplaySubsystem->GetPlaybackRate())));
	FlyingText->SetText(FText::FromString(FString::Printf(TEXT("Flying %d / %d"), ReplaySubsystem->GetFlyingTrackCount(), ReplaySubsystem->GetTrackCount())));
}
