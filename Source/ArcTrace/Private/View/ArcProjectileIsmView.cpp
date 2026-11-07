#include "View/ArcProjectileIsmView.h"

#include "CesiumGlobeAnchorComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

#include "ArcTrace.h"
#include "View/ArcViewSettings.h"

namespace
{
	const FTransform HIDDEN_TRANSFORM(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
}

AArcProjectileIsmView::AArcProjectileIsmView()
	: ProjectileScale(150.0f)
	, ProjectilePixelSize(14.0f)
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
	SceneRoot->SetMobility(EComponentMobility::Movable);
	GlobeAnchor = CreateDefaultSubobject<UCesiumGlobeAnchorComponent>(TEXT("GlobeAnchor"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	ProjectileMesh = ConeMesh.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(ArcView::INSTANCE_MATERIAL_PATH);
	InstanceMaterial = Material.Object;
}

void AArcProjectileIsmView::InitializeView(const FArcReplayRecord& Record)
{
	ResetView();

	KindComponents.Reserve(Record.KindNames.Num());
	for (int32 KindIndex = 0; KindIndex < Record.KindNames.Num(); ++KindIndex)
	{
		KindComponents.Add(CreateKindComponent(KindIndex));
	}

	const int32 TrackCount = Record.Tracks.Num();
	InstanceIndexByTrack.Init(INDEX_NONE, TrackCount);
	KindIndexByTrack.Init(INDEX_NONE, TrackCount);

	TArray<TArray<FTransform>> InitialTransformsByKind;
	InitialTransformsByKind.SetNum(KindComponents.Num());
	for (int32 TrackIndex = 0; TrackIndex < TrackCount; ++TrackIndex)
	{
		const int32 KindIndex = Record.Tracks[TrackIndex].KindIndex;
		TArray<FTransform>& KindTransforms = InitialTransformsByKind[KindIndex];
		InstanceIndexByTrack[TrackIndex] = KindTransforms.Num();
		KindIndexByTrack[TrackIndex] = KindIndex;
		KindTransforms.Add(HIDDEN_TRANSFORM);
	}

	for (int32 KindIndex = 0; KindIndex < KindComponents.Num(); ++KindIndex)
	{
		UInstancedStaticMeshComponent* const Component = KindComponents[KindIndex];
		const TArray<FTransform>& KindTransforms = InitialTransformsByKind[KindIndex];
		Component->AddInstances(KindTransforms, false, false);

		const FLinearColor Color = ArcView::GetDefaultKindColor(KindIndex);
		float CustomData[ArcView::CUSTOM_DATA_FLOAT_COUNT];
		CustomData[ArcView::CUSTOM_DATA_REVEAL_TIME] = ArcView::ALWAYS_VISIBLE_REVEAL_TIME;
		CustomData[ArcView::CUSTOM_DATA_COLOR_R] = Color.R;
		CustomData[ArcView::CUSTOM_DATA_COLOR_R + 1] = Color.G;
		CustomData[ArcView::CUSTOM_DATA_COLOR_R + 2] = Color.B;
		for (int32 InstanceIndex = 0; InstanceIndex < KindTransforms.Num(); ++InstanceIndex)
		{
			Component->SetCustomData(InstanceIndex, MakeArrayView(CustomData), false);
		}
		Component->MarkRenderStateDirty();
	}
}

void AArcProjectileIsmView::ApplyStates(const TArray<FArcProjectileViewState>& States)
{
	ARC_SCOPED_PROFILE(IsmApplyStates);
	TArray<bool> IsKindDirty;
	IsKindDirty.Init(false, KindComponents.Num());
	const FMatrix EcefToActor = ArcView::GetEcefToActorMatrix(*GlobeAnchor);

	for (const FArcProjectileViewState& State : States)
	{
		check(InstanceIndexByTrack.IsValidIndex(State.TrackIndex));

		const int32 KindIndex = KindIndexByTrack[State.TrackIndex];
		UInstancedStaticMeshComponent* const Component = KindComponents[KindIndex];
		const FTransform Transform = State.bIsVisible
			? ArcView::MakeLocalTransform(EcefToActor, State.PositionEcef, State.VelocityEcef, ProjectileScale)
			: HIDDEN_TRANSFORM;
		Component->UpdateInstanceTransform(InstanceIndexByTrack[State.TrackIndex], Transform, false, false, true);
		IsKindDirty[KindIndex] = true;
	}

	// 렌더 상태 갱신은 컴포넌트당 한 번으로 묶는다.
	for (int32 KindIndex = 0; KindIndex < KindComponents.Num(); ++KindIndex)
	{
		if (IsKindDirty[KindIndex])
		{
			KindComponents[KindIndex]->MarkRenderStateDirty();
		}
	}
}

void AArcProjectileIsmView::ResetView()
{
	for (UInstancedStaticMeshComponent* const Component : KindComponents)
	{
		if (Component != nullptr)
		{
			Component->DestroyComponent();
		}
	}
	KindComponents.Reset();
	InstanceIndexByTrack.Reset();
	KindIndexByTrack.Reset();
}

UInstancedStaticMeshComponent* AArcProjectileIsmView::CreateKindComponent(const int32 KindIndex)
{
	const FName ComponentName(*FString::Printf(TEXT("ProjectileIsm_%d"), KindIndex));
	UInstancedStaticMeshComponent* const Component = NewObject<UInstancedStaticMeshComponent>(this, ComponentName);
	Component->SetupAttachment(SceneRoot);
	Component->SetStaticMesh(ProjectileMesh);
	UMaterialInstanceDynamic* const Material = Component->CreateAndSetMaterialInstanceDynamicFromMaterial(0, InstanceMaterial);
	if (Material != nullptr)
	{
		Material->SetScalarParameterValue(ArcView::SCREEN_PIXELS_PARAMETER_NAME, ProjectilePixelSize);
	}
	Component->SetMobility(EComponentMobility::Movable);
	ArcView::ConfigureOverlayComponent(*Component);
	Component->RegisterComponent();
	Component->SetNumCustomDataFloats(ArcView::CUSTOM_DATA_FLOAT_COUNT);
	AddInstanceComponent(Component);
	return Component;
}
