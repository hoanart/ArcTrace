#include "UI/ArcTrackLabelOverlay.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "UnrealClient.h"
#include "CesiumGeoreference.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

#include "ArcTrace.h"
#include "Replay/ArcReplaySubsystem.h"
#include "View/ArcViewSettings.h"

namespace
{
	constexpr int32 FONT_SIZE = 9;
	constexpr int32 MAX_DATA_BLOCK_COUNT = 40;
	constexpr double EXPLOSION_LABEL_SECONDS = 5.0;
	constexpr float SYMBOL_SIZE = 6.0f;
	constexpr float LEADER_OFFSET_X = 14.0f;
	constexpr float LEADER_OFFSET_Y = 18.0f;
	constexpr float BOX_PADDING = 4.0f;
	constexpr float BOX_MARGIN = 2.0f;

	// 데이터 블록 배치 후보 순서: 오른쪽 위 → 왼쪽 위 → 오른쪽 아래 → 왼쪽 아래 (Y는 화면 위쪽이 +)
	const FVector2f PLACEMENT_DIRECTIONS[] =
	{
		FVector2f(1.0f, 1.0f),
		FVector2f(-1.0f, 1.0f),
		FVector2f(1.0f, -1.0f),
		FVector2f(-1.0f, -1.0f),
	};

	// 지시선은 심볼에서 데이터 블록의 가장 가까운 모서리로 잇는다.
	FVector2f FindNearestCorner(const FVector2f& Point, const FVector2f& BoxPosition, const FVector2f& BoxSize)
	{
		const FVector2f Corners[] =
		{
			BoxPosition,
			BoxPosition + FVector2f(BoxSize.X, 0.0f),
			BoxPosition + FVector2f(0.0f, BoxSize.Y),
			BoxPosition + BoxSize,
		};
		FVector2f Nearest = Corners[0];
		for (const FVector2f& Corner : Corners)
		{
			if (FVector2f::DistSquared(Point, Corner) < FVector2f::DistSquared(Point, Nearest))
			{
				Nearest = Corner;
			}
		}
		return Nearest;
	}

	bool IsOverlappingAny(const FSlateRect& Rect, const TArrayView<const FSlateRect> PlacedRects)
	{
		for (const FSlateRect& Placed : PlacedRects)
		{
			if (FSlateRect::DoRectanglesIntersect(Rect, Placed))
			{
				return true;
			}
		}
		return false;
	}

	// 소스 인코딩과 무관하게 한글을 넣기 위해 유니코드 이스케이프로 둔다: 초기비행 / 비행 / 폭파
	const TCHAR* const STATE_BOOST_TEXT = TEXT("\uCD08\uAE30\uBE44\uD589");
	const TCHAR* const STATE_FLIGHT_TEXT = TEXT("\uBE44\uD589");
	const TCHAR* const STATE_EXPLODED_TEXT = TEXT("\uD3ED\uD30C");

	const TCHAR* GetStateText(const EArcProjectilePhase Phase)
	{
		switch (Phase)
		{
		case EArcProjectilePhase::Boost:
			return STATE_BOOST_TEXT;
		case EArcProjectilePhase::Ascent:
		case EArcProjectilePhase::Descent:
			return STATE_FLIGHT_TEXT;
		case EArcProjectilePhase::Impact:
			return STATE_EXPLODED_TEXT;
		default:
			checkNoEntry();
			return STATE_FLIGHT_TEXT;
		}
	}

	// "HeavyRocket" -> "HR": 종류 이름의 대문자만 모은다.
	FString MakeKindPrefix(const FString& KindName)
	{
		FString Prefix;
		for (const TCHAR Character : KindName)
		{
			if (FChar::IsUpper(Character))
			{
				Prefix.AppendChar(Character);
			}
		}
		return Prefix.IsEmpty() ? FString(TEXT("T")) : Prefix;
	}

	FLinearColor MakeTextColor(const FLinearColor& KindColor)
	{
		return FLinearColor(KindColor.R * 0.55f, KindColor.G * 0.55f, KindColor.B * 0.55f, 1.0f);
	}

	// 가장 긴 줄 기준 데이터 블록 크기 (ID 4자리·초기비행, 음수 위경도, 세 자리 고도·네 자리 속도).
	FVector2f MeasureDataBlockSizeFor(const FSlateFontInfo& Font, const float LineHeight, const int32 LineCount)
	{
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FString WidestLines[] =
		{
			FString::Printf(TEXT("HR-0000  %s"), STATE_BOOST_TEXT),
			FString(TEXT("-90.0000N  -180.0000E")),
			FString(TEXT("999.99 km  9999 m/s")),
		};
		float MaxLineWidth = 0.0f;
		for (const FString& Line : WidestLines)
		{
			MaxLineWidth = FMath::Max(MaxLineWidth, static_cast<float>(FontMeasure->Measure(Line, Font).X));
		}
		return FVector2f(MaxLineWidth + 2.0f * BOX_PADDING, LineCount * LineHeight + 2.0f * BOX_PADDING);
	}
}

void SArcTrackLabelOverlay::Construct(const FArguments& InArgs)
{
	PlayerController = InArgs._PlayerController;
	ReplaySubsystem = InArgs._ReplaySubsystem;
	Georeference = InArgs._Georeference;

	Font = FCoreStyle::GetDefaultFontStyle("Regular", FONT_SIZE);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	LineHeight = static_cast<float>(FontMeasure->GetMaxCharacterHeight(Font));

	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(true);
}

void SArcTrackLabelOverlay::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	ARC_SCOPED_PROFILE(LabelBuild);
	Candidates.Reset();
	Labels.Reset();

	if (!PlayerController.IsValid() || !ReplaySubsystem.IsValid() || !Georeference.IsValid() || !ReplaySubsystem->HasRecord())
	{
		return;
	}
	CollectCandidates(AllottedGeometry);
	BuildLabels();
}

void SArcTrackLabelOverlay::CollectCandidates(const FGeometry& AllottedGeometry)
{
	ARC_SCOPED_PROFILE(LabelCollect);
	const UArcReplaySubsystem& Subsystem = *ReplaySubsystem;
	const FArcReplayRecord& Record = Subsystem.GetRecord();
	const FVector CameraLocation = PlayerController->PlayerCameraManager != nullptr
		? PlayerController->PlayerCameraManager->GetCameraLocation()
		: FVector::ZeroVector;
	// 투영 행렬·DPI 배율은 프레임당 한 번만 구하고, 트랙마다 행렬 곱만 한다.
	// (UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition은 호출마다 뷰 투영 데이터를 다시 구해 2000발에서 수십 ms가 된다.)
	ULocalPlayer* const LocalPlayer = PlayerController->GetLocalPlayer();
	FSceneViewProjectionData ProjectionData;
	if (LocalPlayer == nullptr || LocalPlayer->ViewportClient == nullptr
		|| !LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, ProjectionData))
	{
		return;
	}
	const FMatrix ViewProjection = ProjectionData.ComputeViewProjectionMatrix();
	const FIntRect ViewRect = ProjectionData.GetConstrainedViewRect();
	const float InverseViewportScale = 1.0f / FMath::Max(UWidgetLayoutLibrary::GetViewportScale(PlayerController.Get()), KINDA_SMALL_NUMBER);

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2f WidgetSize(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
	const int64 CurrentTimeUs = Subsystem.GetCurrentTimeUs();
	const int64 ExplosionWindowUs = static_cast<int64>(EXPLOSION_LABEL_SECONDS * ARC_MICROSECONDS_PER_SECOND);

	Candidates.Reserve(Record.Tracks.Num());
	for (int32 TrackIndex = 0; TrackIndex < Record.Tracks.Num(); ++TrackIndex)
	{
		const FArcTrackSample* const Sample = Subsystem.FindCurrentSample(TrackIndex);
		if (Sample == nullptr)
		{
			continue;
		}
		if (Sample->Phase == EArcProjectilePhase::Impact && CurrentTimeUs - Sample->TimeUs > ExplosionWindowUs)
		{
			continue;
		}

		const FVector WorldLocation = Georeference->TransformEarthCenteredEarthFixedPositionToUnreal(Sample->PositionEcef);
		FVector2D PixelPosition;
		if (!FSceneView::ProjectWorldToScreen(WorldLocation, ViewRect, ViewProjection, PixelPosition))
		{
			continue;
		}
		const FVector2f Position(static_cast<float>(PixelPosition.X - ViewRect.Min.X) * InverseViewportScale,
			static_cast<float>(PixelPosition.Y - ViewRect.Min.Y) * InverseViewportScale);
		if (Position.X < 0.0f || Position.Y < 0.0f || Position.X > WidgetSize.X || Position.Y > WidgetSize.Y)
		{
			continue;
		}

		FLabelCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.TrackIndex = TrackIndex;
		Candidate.DistanceSquared = FVector::DistSquared(CameraLocation, WorldLocation);
		Candidate.ScreenPosition = Position;
	}
	Candidates.Sort();
}

void SArcTrackLabelOverlay::BuildLabels()
{
	ARC_SCOPED_PROFILE(LabelLayout);
	const FArcReplayRecord& Record = ReplaySubsystem->GetRecord();

	// 데이터 블록 크기는 가장 긴 경우의 문자열로 프레임당 한 번만 잰다.
	// 자리 검사를 먼저 하고, 자리를 얻은 탄(최대 MAX_DATA_BLOCK_COUNT)만 문자열을 만든다.
	// (탄마다 문자열 생성·측정을 먼저 하면 2000발에서 레이아웃이 17ms가 된다.)
	const FVector2f BoxSize = MeasureDataBlockSizeFor(Font, LineHeight, LINE_COUNT);

	// 심볼은 데이터 블록 자리를 얻은 탄에만 그린다. 탄 자체는 3D 인스턴스(ArcProjectileIsmView)가 이미 그리므로,
	// 전 트랙에 Slate 심볼을 그리면 2000발에서 Slate 요소 수집·배치만 약 4 ms가 된다 (Docs/PERF_LOG.md).
	TArray<FSlateRect, TInlineAllocator<MAX_DATA_BLOCK_COUNT>> PlacedBoxes;
	Labels.Reserve(MAX_DATA_BLOCK_COUNT);
	for (const FLabelCandidate& Candidate : Candidates)
	{
		if (PlacedBoxes.Num() >= MAX_DATA_BLOCK_COUNT)
		{
			break;
		}

		for (const FVector2f& Direction : PLACEMENT_DIRECTIONS)
		{
			// 방향 (±1, ±1): 지시선 끝에서 박스가 바깥쪽으로 펼쳐지도록 왼쪽·위쪽이면 박스 크기만큼 당긴다.
			const FVector2f LeaderEnd = Candidate.ScreenPosition + FVector2f(Direction.X * LEADER_OFFSET_X, -Direction.Y * LEADER_OFFSET_Y);
			const FVector2f BoxPosition(
				Direction.X > 0.0f ? LeaderEnd.X : LeaderEnd.X - BoxSize.X,
				Direction.Y > 0.0f ? LeaderEnd.Y - BoxSize.Y : LeaderEnd.Y);
			const FSlateRect BoxRect(BoxPosition.X - BOX_MARGIN, BoxPosition.Y - BOX_MARGIN,
				BoxPosition.X + BoxSize.X + BOX_MARGIN, BoxPosition.Y + BoxSize.Y + BOX_MARGIN);
			if (!IsOverlappingAny(BoxRect, PlacedBoxes))
			{
				PlacedBoxes.Add(BoxRect);
				FTrackLabel& Label = Labels.AddDefaulted_GetRef();
				Label.SymbolPosition = Candidate.ScreenPosition;
				Label.Color = ArcView::GetDefaultKindColor(Record.Tracks[Candidate.TrackIndex].KindIndex);
				FillDataBlock(Candidate.TrackIndex, &Label);
				Label.BoxPosition = BoxPosition;
				Label.BoxSize = BoxSize;
				Label.bHasDataBlock = true;
				break;
			}
		}
	}
}

void SArcTrackLabelOverlay::FillDataBlock(const int32 TrackIndex, FTrackLabel* const OutLabel) const
{
	check(OutLabel != nullptr);
	const FArcReplayRecord& Record = ReplaySubsystem->GetRecord();
	const FArcReplayTrack& Track = Record.Tracks[TrackIndex];
	const FArcTrackSample* const Sample = ReplaySubsystem->FindCurrentSample(TrackIndex);
	check(Sample != nullptr);

	const FString KindName = Record.KindNames.IsValidIndex(Track.KindIndex) ? Record.KindNames[Track.KindIndex] : FString();
	OutLabel->Lines[0] = FString::Printf(TEXT("%s-%03d  %s"), *MakeKindPrefix(KindName), Track.TrackId, GetStateText(Sample->Phase));
	OutLabel->Lines[1] = FString::Printf(TEXT("%.4fN  %.4fE"), Sample->Latitude, Sample->Longitude);
	// 탄착 샘플은 보간으로 -0.00이 나올 수 있어 0 아래는 0으로 표시한다.
	OutLabel->Lines[2] = FString::Printf(TEXT("%.2f km  %.0f m/s"), FMath::Max(Sample->Altitude, 0.0) / 1000.0, Sample->Speed);
}

int32 SArcTrackLabelOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	ARC_SCOPED_PROFILE(LabelPaint);
	const FSlateBrush* const WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FLinearColor BoxBackground(1.0f, 1.0f, 1.0f, 0.78f);

	// 같은 종류의 요소를 같은 레이어에 모아 그려 배치(드로우콜)를 최소화한다.
	// 레이어 0: 심볼·박스 배경, 레이어 1: 지시선·테두리, 레이어 2: 글자
	const int32 FillLayer = LayerId;
	const int32 LineLayer = LayerId + 1;
	const int32 TextLayer = LayerId + 2;

	TArray<FVector2f> Points;
	Points.SetNum(5);
	for (const FTrackLabel& Label : Labels)
	{
		const FVector2f SymbolOrigin = Label.SymbolPosition - FVector2f(SYMBOL_SIZE * 0.5f);
		FSlateDrawElement::MakeBox(OutDrawElements, FillLayer,
			AllottedGeometry.ToPaintGeometry(FVector2f(SYMBOL_SIZE), FSlateLayoutTransform(1.0f, SymbolOrigin)),
			WhiteBrush, ESlateDrawEffect::None, Label.Color);
		if (!Label.bHasDataBlock)
		{
			continue;
		}
		FSlateDrawElement::MakeBox(OutDrawElements, FillLayer,
			AllottedGeometry.ToPaintGeometry(Label.BoxSize, FSlateLayoutTransform(1.0f, Label.BoxPosition)),
			WhiteBrush, ESlateDrawEffect::None, BoxBackground);

		const FVector2f BoxBottomLeft = Label.BoxPosition + FVector2f(0.0f, Label.BoxSize.Y);
		TArray<FVector2f> LeaderPoints;
		LeaderPoints.Add(Label.SymbolPosition);
		LeaderPoints.Add(FindNearestCorner(Label.SymbolPosition, Label.BoxPosition, Label.BoxSize));
		FSlateDrawElement::MakeLines(OutDrawElements, LineLayer, AllottedGeometry.ToPaintGeometry(), LeaderPoints,
			ESlateDrawEffect::None, Label.Color, true, 1.0f);

		Points[0] = Label.BoxPosition;
		Points[1] = Label.BoxPosition + FVector2f(Label.BoxSize.X, 0.0f);
		Points[2] = Label.BoxPosition + Label.BoxSize;
		Points[3] = BoxBottomLeft;
		Points[4] = Label.BoxPosition;
		FSlateDrawElement::MakeLines(OutDrawElements, LineLayer, AllottedGeometry.ToPaintGeometry(), Points,
			ESlateDrawEffect::None, Label.Color, true, 1.5f);

		const FLinearColor TextColor = MakeTextColor(Label.Color);
		for (int32 LineIndex = 0; LineIndex < LINE_COUNT; ++LineIndex)
		{
			const FVector2f LinePosition = Label.BoxPosition + FVector2f(BOX_PADDING, BOX_PADDING + LineIndex * LineHeight);
			FSlateDrawElement::MakeText(OutDrawElements, TextLayer,
				AllottedGeometry.ToPaintGeometry(FVector2f(Label.BoxSize.X, LineHeight), FSlateLayoutTransform(1.0f, LinePosition)),
				Label.Lines[LineIndex], Font, ESlateDrawEffect::None, TextColor);
		}
	}
	return TextLayer;
}

FVector2D SArcTrackLabelOverlay::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D::ZeroVector;
}
