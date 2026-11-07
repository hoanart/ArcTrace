#include "View/ArcReplayDirector.h"

#include "Camera/PlayerCameraManager.h"
#include "CesiumGeoreference.h"
#include "CesiumGlobeAnchorComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "UObject/ConstructorHelpers.h"

#include "ArcTrace.h"
#include "Replay/ArcReplaySubsystem.h"
#include "UI/ArcTimelineWidget.h"
#include "UI/ArcTrackLabelOverlay.h"
#include "View/ArcProjectileIsmView.h"
#include "View/ArcTrajectoryHism.h"
#include "View/ArcViewSettings.h"

AArcReplayDirector::AArcReplayDirector()
	: RecordName(TEXT("sample_salvo"))
	, bAutoLoad(true)
	, bAutoPlay(true)
	, bRecenterGeoreference(true)
	, bUseOverviewCamera(true)
	, OverviewOffsetEnuMeters(-20000.0, -60000.0, 30000.0)
	, OverviewFieldOfView(60.0f)
	, bShowTimeline(true)
	, bShowTrackLabels(true)
	, ProjectileView(nullptr)
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> ParameterCollection(ArcView::REPLAY_PARAMETER_COLLECTION_PATH);
	ReplayParameters = ParameterCollection.Object;
}

void AArcReplayDirector::BeginPlay()
{
	Super::BeginPlay();

	ReplaySubsystem = GetWorld()->GetSubsystem<UArcReplaySubsystem>();
	if (ReplaySubsystem == nullptr)
	{
		UE_LOG(LogArcTrace, Error, TEXT("[Director] replay subsystem not available in this world"));
		return;
	}

	ResolveViewActors();
	RecordLoadedHandle = ReplaySubsystem->OnRecordLoaded.AddUObject(this, &AArcReplayDirector::HandleRecordLoaded);
	SeekedHandle = ReplaySubsystem->OnSeeked.AddUObject(this, &AArcReplayDirector::HandleSeeked);
	TimeChangedHandle = ReplaySubsystem->OnTimeChanged.AddUObject(this, &AArcReplayDirector::HandleTimeChanged);

	if (bShowTimeline)
	{
		CreateTimeline();
	}
	if (bShowTrackLabels)
	{
		CreateTrackLabelOverlay();
	}

	if (bAutoLoad && !RecordName.IsEmpty())
	{
		ReplaySubsystem->StartLoadRecord(RecordName);
	}
}

void AArcReplayDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ReplaySubsystem != nullptr)
	{
		ReplaySubsystem->OnRecordLoaded.Remove(RecordLoadedHandle);
		ReplaySubsystem->OnSeeked.Remove(SeekedHandle);
		ReplaySubsystem->OnTimeChanged.Remove(TimeChangedHandle);
	}
	if (TimelineWidget != nullptr)
	{
		TimelineWidget->RemoveFromParent();
		TimelineWidget = nullptr;
	}
	if (TrackLabelOverlay.IsValid())
	{
		UGameViewportClient* const GameViewport = GetWorld()->GetGameViewport();
		if (GameViewport != nullptr)
		{
			GameViewport->RemoveViewportWidgetContent(TrackLabelOverlay.ToSharedRef());
		}
		TrackLabelOverlay.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void AArcReplayDirector::HandleRecordLoaded(const bool bSucceeded)
{
	if (!bSucceeded || ProjectileView == nullptr)
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	const FArcReplayRecord& Record = ReplaySubsystem->GetRecord();
	if (bRecenterGeoreference)
	{
		RecenterGeoreference(Record);
	}
	ProjectileView->InitializeView(Record);
	if (Trajectory != nullptr)
	{
		Trajectory->BuildTrajectories(Record);
	}
	AppliedSampleIndices.Init(INDEX_NONE, Record.Tracks.Num());
	PendingStates.Reserve(Record.Tracks.Num());
	ApplyTracks(true);
	UpdateReplayTime(ReplaySubsystem->GetCurrentTimeUs());
	if (bUseOverviewCamera)
	{
		PlaceOverviewCamera();
	}

	UE_LOG(LogArcTrace, Log, TEXT("[Director] view built for %d tracks (%.2fs)"), Record.Tracks.Num(), FPlatformTime::Seconds() - StartSeconds);
	if (bAutoPlay)
	{
		ReplaySubsystem->Play();
	}
}

void AArcReplayDirector::HandleSeeked(const int64 TimeUs)
{
	if (ProjectileView != nullptr && AppliedSampleIndices.Num() == ReplaySubsystem->GetTrackCount())
	{
		ApplyTracks(true);
	}
}

void AArcReplayDirector::HandleTimeChanged(const int64 TimeUs)
{
	if (ProjectileView == nullptr || AppliedSampleIndices.Num() != ReplaySubsystem->GetTrackCount())
	{
		return;
	}

	ApplyTracks(false);
	UpdateReplayTime(TimeUs);
}

void AArcReplayDirector::UpdateReplayTime(const int64 TimeUs)
{
	if (ReplayParameters == nullptr)
	{
		return;
	}
	UMaterialParameterCollectionInstance* const ParameterInstance = GetWorld()->GetParameterCollectionInstance(ReplayParameters);
	if (ParameterInstance != nullptr)
	{
		const double ElapsedSeconds = (TimeUs - ReplaySubsystem->GetStartTimeUs()) / ARC_MICROSECONDS_PER_SECOND;
		ParameterInstance->SetScalarParameterValue(ArcView::CURRENT_TIME_PARAMETER_NAME, static_cast<float>(ElapsedSeconds));
	}
}

void AArcReplayDirector::ResolveViewActors()
{
	if (Georeference == nullptr)
	{
		Georeference = ACesiumGeoreference::GetDefaultGeoreferenceForActor(this);
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	if (ProjectileViewActor == nullptr)
	{
		ProjectileViewActor = GetWorld()->SpawnActor<AArcProjectileIsmView>(SpawnParameters);
	}
	ProjectileView = Cast<IArcProjectileView>(ProjectileViewActor);
	if (ProjectileView == nullptr)
	{
		UE_LOG(LogArcTrace, Error, TEXT("[Director] ProjectileViewActor does not implement IArcProjectileView"));
	}

	if (Trajectory == nullptr)
	{
		Trajectory = GetWorld()->SpawnActor<AArcTrajectoryHism>(SpawnParameters);
	}
}

void AArcReplayDirector::RecenterGeoreference(const FArcReplayRecord& Record)
{
	// 첫 샘플(발사 지점)들의 평균 위경도를 원점으로 삼는다.
	double LatitudeSum = 0.0;
	double LongitudeSum = 0.0;
	for (const FArcReplayTrack& Track : Record.Tracks)
	{
		LatitudeSum += Track.Samples[0].Latitude;
		LongitudeSum += Track.Samples[0].Longitude;
	}
	const double TrackCount = static_cast<double>(Record.Tracks.Num());
	const FVector OriginLongitudeLatitudeHeight(LongitudeSum / TrackCount, LatitudeSum / TrackCount, 0.0);
	Georeference->SetOriginLongitudeLatitudeHeight(OriginLongitudeLatitudeHeight);
	UE_LOG(LogArcTrace, Log, TEXT("[Director] georeference origin -> lon %.5f, lat %.5f"),
		OriginLongitudeLatitudeHeight.X, OriginLongitudeLatitudeHeight.Y);
}

void AArcReplayDirector::PlaceOverviewCamera()
{
	APlayerController* const PlayerController = GetWorld()->GetFirstPlayerController();
	if (PlayerController == nullptr)
	{
		UE_LOG(LogArcTrace, Warning, TEXT("[Director] no player controller - overview camera not placed"));
		return;
	}

	// 전 트랙의 발사·탄착 지점 평균을 교전 영역 중심으로 삼고, 그 지점의 국지 ENU 축으로 카메라 위치를 정한다.
	// 모든 계산을 ECEF에서 하므로 언리얼 월드 원점 위치와 무관하다.
	const FArcReplayRecord& Record = ReplaySubsystem->GetRecord();
	FVector CenterEcef = FVector::ZeroVector;
	double LatitudeSum = 0.0;
	double LongitudeSum = 0.0;
	for (const FArcReplayTrack& Track : Record.Tracks)
	{
		CenterEcef += Track.Samples[0].PositionEcef + Track.Samples.Last().PositionEcef;
		LatitudeSum += Track.Samples[0].Latitude + Track.Samples.Last().Latitude;
		LongitudeSum += Track.Samples[0].Longitude + Track.Samples.Last().Longitude;
	}
	const double PointCount = 2.0 * Record.Tracks.Num();
	CenterEcef /= PointCount;

	const double Latitude = FMath::DegreesToRadians(LatitudeSum / PointCount);
	const double Longitude = FMath::DegreesToRadians(LongitudeSum / PointCount);
	const FVector EastEcef(-FMath::Sin(Longitude), FMath::Cos(Longitude), 0.0);
	const FVector NorthEcef(-FMath::Sin(Latitude) * FMath::Cos(Longitude), -FMath::Sin(Latitude) * FMath::Sin(Longitude), FMath::Cos(Latitude));
	const FVector UpEcef(FMath::Cos(Latitude) * FMath::Cos(Longitude), FMath::Cos(Latitude) * FMath::Sin(Longitude), FMath::Sin(Latitude));
	const FVector CameraEcef = CenterEcef
		+ EastEcef * OverviewOffsetEnuMeters.X
		+ NorthEcef * OverviewOffsetEnuMeters.Y
		+ UpEcef * OverviewOffsetEnuMeters.Z;

	const FVector CenterLocation = Georeference->TransformEarthCenteredEarthFixedPositionToUnreal(CenterEcef);
	const FVector CameraLocation = Georeference->TransformEarthCenteredEarthFixedPositionToUnreal(CameraEcef);
	const FVector UpUnreal = Georeference->TransformEarthCenteredEarthFixedDirectionToUnreal(UpEcef);
	const FRotator CameraRotation = FRotationMatrix::MakeFromXZ(CenterLocation - CameraLocation, UpUnreal).Rotator();

	// 조종 중인 폰을 그대로 옮겨, 개관 시점에서 시작하되 마우스·키보드 조작이 유지되게 한다.
	APawn* const Pawn = PlayerController->GetPawn();
	if (Pawn == nullptr)
	{
		UE_LOG(LogArcTrace, Warning, TEXT("[Director] no possessed pawn - overview viewpoint not applied"));
		return;
	}
	UCesiumGlobeAnchorComponent* const PawnAnchor = Pawn->FindComponentByClass<UCesiumGlobeAnchorComponent>();
	if (PawnAnchor != nullptr)
	{
		PawnAnchor->MoveToEarthCenteredEarthFixedPosition(CameraEcef);
	}
	else
	{
		Pawn->SetActorLocation(CameraLocation);
	}
	Pawn->SetActorRotation(CameraRotation);
	PlayerController->SetControlRotation(CameraRotation);
	PlayerController->SetViewTarget(Pawn);
	if (PlayerController->PlayerCameraManager != nullptr)
	{
		PlayerController->PlayerCameraManager->SetFOV(OverviewFieldOfView);
	}

	UE_LOG(LogArcTrace, Log, TEXT("[Director] pawn moved to overview, %.1f km from engagement center"),
		FVector::Dist(CameraLocation, CenterLocation) / 100000.0);
}

void AArcReplayDirector::CreateTimeline()
{
	APlayerController* const PlayerController = GetWorld()->GetFirstPlayerController();
	if (PlayerController == nullptr)
	{
		UE_LOG(LogArcTrace, Warning, TEXT("[Director] no player controller - timeline not created"));
		return;
	}

	TimelineWidget = CreateWidget<UArcTimelineWidget>(PlayerController, UArcTimelineWidget::StaticClass());
	TimelineWidget->SetReplaySubsystem(ReplaySubsystem);
	TimelineWidget->AddToViewport();

	// 커서로 재생 바를 조작하고, 뷰포트를 누른 채 끌면 폰 시점 회전이 그대로 동작한다.
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(true);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);
}

void AArcReplayDirector::CreateTrackLabelOverlay()
{
	APlayerController* const PlayerController = GetWorld()->GetFirstPlayerController();
	UGameViewportClient* const GameViewport = GetWorld()->GetGameViewport();
	if (PlayerController == nullptr || GameViewport == nullptr || Georeference == nullptr)
	{
		UE_LOG(LogArcTrace, Warning, TEXT("[Director] track labels not created (controller, viewport or georeference missing)"));
		return;
	}

	TrackLabelOverlay = SNew(SArcTrackLabelOverlay)
		.PlayerController(PlayerController)
		.ReplaySubsystem(ReplaySubsystem)
		.Georeference(Georeference);

	// 재생 바(UMG, ZOrder 0)보다 아래에 깐다.
	constexpr int32 TRACK_LABEL_Z_ORDER = -1;
	GameViewport->AddViewportWidgetContent(TrackLabelOverlay.ToSharedRef(), TRACK_LABEL_Z_ORDER);
}

void AArcReplayDirector::ApplyTracks(const bool bApplyAllTracks)
{
	ARC_SCOPED_PROFILE(ApplyTracks);
	PendingStates.Reset();
	for (int32 TrackIndex = 0; TrackIndex < AppliedSampleIndices.Num(); ++TrackIndex)
	{
		const int32 SampleIndex = ReplaySubsystem->GetCurrentSampleIndex(TrackIndex);
		if (bApplyAllTracks || SampleIndex != AppliedSampleIndices[TrackIndex])
		{
			PendingStates.Add(MakeViewState(TrackIndex, SampleIndex));
			AppliedSampleIndices[TrackIndex] = SampleIndex;
		}
	}
	if (!PendingStates.IsEmpty())
	{
		ProjectileView->ApplyStates(PendingStates);
	}
}

FArcProjectileViewState AArcReplayDirector::MakeViewState(const int32 TrackIndex, const int32 SampleIndex) const
{
	const FArcReplayTrack& Track = ReplaySubsystem->GetRecord().Tracks[TrackIndex];

	FArcProjectileViewState State;
	State.TrackIndex = TrackIndex;
	State.KindIndex = Track.KindIndex;
	if (SampleIndex != INDEX_NONE)
	{
		const FArcTrackSample& Sample = Track.Samples[SampleIndex];
		State.PositionEcef = Sample.PositionEcef;
		State.VelocityEcef = Sample.VelocityEcef;
		State.Phase = Sample.Phase;
		State.bIsVisible = State.Phase != EArcProjectilePhase::Impact;
	}
	return State;
}
