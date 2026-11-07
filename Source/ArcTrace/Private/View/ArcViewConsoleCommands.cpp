#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

#include "ArcTrace.h"
#include "View/ArcProjectileIsmView.h"
#include "View/ArcTrajectoryHism.h"
#include "Replay/ArcReplaySubsystem.h"
#include "View/ArcViewSettings.h"

// 시각화 계층 측정용 명령. 같은 세션·같은 장면 상태에서 설정만 바꿔 전후를 비교한다.
namespace
{
	int32 ApplyOverlayLighting(AActor& Actor, const bool bParticipate)
	{
		TInlineComponentArray<UPrimitiveComponent*> Components(&Actor);
		for (UPrimitiveComponent* const Component : Components)
		{
			ArcView::SetOverlayLightingParticipation(*Component, bParticipate);
		}
		return Components.Num();
	}

	void ExecuteOverlayLighting(const TArray<FString>& Arguments, UWorld* World)
	{
		if (World == nullptr || Arguments.IsEmpty())
		{
			UE_LOG(LogArcTrace, Warning, TEXT("[View] usage: ArcTrace.OverlayLighting <0|1>"));
			return;
		}

		const bool bParticipate = FCString::Atoi(*Arguments[0]) != 0;
		int32 ComponentCount = 0;
		for (TActorIterator<AArcTrajectoryHism> It(World); It; ++It)
		{
			ComponentCount += ApplyOverlayLighting(**It, bParticipate);
		}
		for (TActorIterator<AArcProjectileIsmView> It(World); It; ++It)
		{
			ComponentCount += ApplyOverlayLighting(**It, bParticipate);
		}
		UE_LOG(LogArcTrace, Display, TEXT("[View] overlay lighting participation %s (%d components)"),
			bParticipate ? TEXT("ON") : TEXT("OFF"), ComponentCount);
	}

	// 궤적 인스턴스 컴포넌트를 HISM/ISM으로 바꿔 현재 기록으로 다시 깐다.
	void ExecuteTrajectoryInstancing(const TArray<FString>& Arguments, UWorld* World)
	{
		UArcReplaySubsystem* const Subsystem = World != nullptr ? World->GetSubsystem<UArcReplaySubsystem>() : nullptr;
		if (Subsystem == nullptr || !Subsystem->HasRecord() || Arguments.IsEmpty())
		{
			UE_LOG(LogArcTrace, Warning, TEXT("[View] usage: ArcTrace.TrajectoryInstancing <hism|ism> (with a loaded record)"));
			return;
		}

		const bool bUseHierarchical = Arguments[0].Equals(TEXT("hism"), ESearchCase::IgnoreCase);
		if (!bUseHierarchical && !Arguments[0].Equals(TEXT("ism"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogArcTrace, Warning, TEXT("[View] unknown instancing '%s' (hism|ism)"), *Arguments[0]);
			return;
		}
		for (TActorIterator<AArcTrajectoryHism> It(World); It; ++It)
		{
			It->SetUseHierarchicalInstancing(bUseHierarchical);
			It->BuildTrajectories(Subsystem->GetRecord());
		}
	}

	// 궤적 구간 메시를 큐브/리본 사각형으로 바꿔 현재 기록으로 다시 깐다.
	void ExecuteTrajectoryShape(const TArray<FString>& Arguments, UWorld* World)
	{
		UArcReplaySubsystem* const Subsystem = World != nullptr ? World->GetSubsystem<UArcReplaySubsystem>() : nullptr;
		if (Subsystem == nullptr || !Subsystem->HasRecord() || Arguments.IsEmpty())
		{
			UE_LOG(LogArcTrace, Warning, TEXT("[View] usage: ArcTrace.TrajectoryShape <cube|ribbon|tangent> (with a loaded record)"));
			return;
		}

		// 알 수 없는 값(오타 등)은 조용히 기본값으로 넘기지 않고 거부한다.
		EArcTrajectoryShape Shape = EArcTrajectoryShape::Ribbon;
		if (Arguments[0].Equals(TEXT("cube"), ESearchCase::IgnoreCase))
		{
			Shape = EArcTrajectoryShape::Cube;
		}
		else if (Arguments[0].Equals(TEXT("tangent"), ESearchCase::IgnoreCase))
		{
			Shape = EArcTrajectoryShape::TangentRibbon;
		}
		else if (!Arguments[0].Equals(TEXT("ribbon"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogArcTrace, Warning, TEXT("[View] unknown shape '%s' (cube|ribbon|tangent)"), *Arguments[0]);
			return;
		}
		for (TActorIterator<AArcTrajectoryHism> It(World); It; ++It)
		{
			It->SetTrajectoryShape(Shape);
			It->BuildTrajectories(Subsystem->GetRecord());
		}
	}

	FAutoConsoleCommandWithWorldAndArgs TrajectoryShapeCommand(TEXT("ArcTrace.TrajectoryShape"),
		TEXT("ArcTrace.TrajectoryShape <cube|ribbon|tangent> : 궤적 구간 메시 전환 후 다시 깔기 (기본 ribbon)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecuteTrajectoryShape));
	FAutoConsoleCommandWithWorldAndArgs TrajectoryInstancingCommand(TEXT("ArcTrace.TrajectoryInstancing"),
		TEXT("ArcTrace.TrajectoryInstancing <hism|ism> : 궤적 인스턴스 컴포넌트 전환 후 다시 깔기 (기본 hism)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecuteTrajectoryInstancing));
	FAutoConsoleCommandWithWorldAndArgs OverlayLightingCommand(TEXT("ArcTrace.OverlayLighting"),
		TEXT("ArcTrace.OverlayLighting <0|1> : 탄·궤적의 레이트레이싱/DF/Lumen GI/반사 캡처 참여 (기본 0)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecuteOverlayLighting));
}
