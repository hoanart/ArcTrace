#include "View/ArcTrajectoryHism.h"

#include "CesiumGlobeAnchorComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

#include "ArcTrace.h"
#include "View/ArcViewSettings.h"

namespace
{
	// /Engine/BasicShapes/Cube 한 변, /Engine/BasicShapes/Plane(XY 평면) 한 변 길이.
	constexpr double SEGMENT_MESH_SIZE_CM = 100.0;
	// 탄젠트 리본은 양쪽 가장자리 정점이 중심선에 겹친 "폭 0" 메시를 전제한다. 0 스케일은 역행렬이 없어 아주 작은 값으로 둔다.
	constexpr double ZERO_WIDTH_SCALE = 1.0e-4;
}

AArcTrajectoryHism::AArcTrajectoryHism()
	: bUseHierarchicalInstancing(true)
	, TrajectoryShape(EArcTrajectoryShape::Ribbon)
	, SampleStride(5)
	, LineWidthCm(4000.0f)
	, LinePixelWidth(3.0f)
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	RootComponent = SceneRoot;

	SegmentHism = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("SegmentHism"));
	SegmentIsm = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SegmentIsm"));
	UInstancedStaticMeshComponent* const SegmentComponents[] = { SegmentHism, SegmentIsm };
	for (UInstancedStaticMeshComponent* const Component : SegmentComponents)
	{
		Component->SetupAttachment(SceneRoot);
		Component->SetMobility(EComponentMobility::Movable);
		ArcView::ConfigureOverlayComponent(*Component);
		Component->NumCustomDataFloats = ArcView::CUSTOM_DATA_FLOAT_COUNT;
	}
	GlobeAnchor = CreateDefaultSubobject<UCesiumGlobeAnchorComponent>(TEXT("GlobeAnchor"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = CubeMeshFinder.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CubeMaterialFinder(ArcView::INSTANCE_MATERIAL_PATH);
	CubeMaterial = CubeMaterialFinder.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RibbonMeshFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	RibbonMesh = RibbonMeshFinder.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RibbonMaterialFinder(ArcView::RIBBON_MATERIAL_PATH);
	RibbonMaterial = RibbonMaterialFinder.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TangentRibbonMaterialFinder(ArcView::TANGENT_RIBBON_MATERIAL_PATH);
	TangentRibbonMaterial = TangentRibbonMaterialFinder.Object;
}

void AArcTrajectoryHism::BuildTrajectories(const FArcReplayRecord& Record)
{
	ARC_SCOPED_PROFILE(BuildTrajectories);
	ClearTrajectories();
	UInstancedStaticMeshComponent* const Segments = GetActiveSegmentComponent();
	const FMatrix EcefToActor = ArcView::GetEcefToActorMatrix(*GlobeAnchor);
	const bool bUseTangentRibbon = TrajectoryShape == EArcTrajectoryShape::TangentRibbon;
	const bool bUseRibbon = TrajectoryShape == EArcTrajectoryShape::Ribbon || bUseTangentRibbon;
	UMaterialInterface* const ShapeMaterial = bUseTangentRibbon ? TangentRibbonMaterial.Get()
		: (bUseRibbon ? RibbonMaterial.Get() : CubeMaterial.Get());
	Segments->SetStaticMesh(bUseRibbon ? RibbonMesh : CubeMesh);
	TrajectoryMaterial = UMaterialInstanceDynamic::Create(ShapeMaterial, this);
	TrajectoryMaterial->SetScalarParameterValue(ArcView::SCREEN_PIXELS_PARAMETER_NAME, LinePixelWidth);
	TrajectoryMaterial->SetVectorParameterValue(ArcView::SCALE_AXIS_MASK_PARAMETER_NAME, ArcView::SEGMENT_SCALE_AXIS_MASK);
	Segments->SetMaterial(0, TrajectoryMaterial);

	int32 SegmentEstimate = 0;
	for (const FArcReplayTrack& Track : Record.Tracks)
	{
		SegmentEstimate += Track.Samples.Num() / SampleStride + 1;
	}

	TArray<FTransform> Transforms;
	TArray<float> CustomData;
	Transforms.Reserve(SegmentEstimate);
	CustomData.Reserve(SegmentEstimate * ArcView::CUSTOM_DATA_FLOAT_COUNT);

	const double LineScale = LineWidthCm / SEGMENT_MESH_SIZE_CM;
	// 사각형은 Z 두께가 없으므로 Z 스케일은 의미가 없다. 1로 두어 바운드만 X·Y로 잡는다.
	const double ThicknessScale = bUseRibbon ? 1.0 : LineScale;
	// 탄젠트 리본은 메시 폭을 머티리얼에서 빼지 않으므로 폭 0으로 깐다. 나머지 모양은 바운드용 폭을 둔다.
	const double WidthScale = bUseTangentRibbon ? ZERO_WIDTH_SCALE : LineScale;
	for (int32 TrackIndex = 0; TrackIndex < Record.Tracks.Num(); ++TrackIndex)
	{
		const FArcReplayTrack& Track = Record.Tracks[TrackIndex];
		const FLinearColor Color = ArcView::GetDefaultKindColor(Track.KindIndex);
		const int32 LastSampleIndex = Track.Samples.Num() - 1;

		int32 StartIndex = 0;
		while (StartIndex < LastSampleIndex)
		{
			// 마지막 구간은 탄착 샘플에서 끝나도록 끝 인덱스를 자른다.
			const int32 EndIndex = FMath::Min(StartIndex + SampleStride, LastSampleIndex);
			const FVector Start = EcefToActor.TransformPosition(Track.Samples[StartIndex].PositionEcef);
			const FVector End = EcefToActor.TransformPosition(Track.Samples[EndIndex].PositionEcef);
			const FVector Delta = End - Start;
			const double Length = Delta.Size();
			if (Length > KINDA_SMALL_NUMBER)
			{
				const FQuat Rotation = FRotationMatrix::MakeFromX(Delta / Length).ToQuat();
				Transforms.Emplace(Rotation, (Start + End) * 0.5, FVector(Length / SEGMENT_MESH_SIZE_CM, WidthScale, ThicknessScale));

				// 구간은 끝 샘플 시각에 드러난다: 투사체보다 앞서 그려지지 않는다.
				const double RevealSeconds = (Track.Samples[EndIndex].TimeUs - Record.StartTimeUs) / ARC_MICROSECONDS_PER_SECOND;
				CustomData.Add(static_cast<float>(RevealSeconds));
				CustomData.Add(Color.R);
				CustomData.Add(Color.G);
				CustomData.Add(Color.B);
			}
			StartIndex = EndIndex;
		}
	}

	Segments->AddInstances(Transforms, false, false);
	for (int32 InstanceIndex = 0; InstanceIndex < Transforms.Num(); ++InstanceIndex)
	{
		const TArrayView<const float> InstanceData(&CustomData[InstanceIndex * ArcView::CUSTOM_DATA_FLOAT_COUNT], ArcView::CUSTOM_DATA_FLOAT_COUNT);
		Segments->SetCustomData(InstanceIndex, InstanceData, false);
	}
	Segments->MarkRenderStateDirty();

	UE_LOG(LogArcTrace, Log, TEXT("[Trajectory] %d segments (stride %d, %s, %s)"), Transforms.Num(), SampleStride,
		bUseHierarchicalInstancing ? TEXT("HISM") : TEXT("ISM"), bUseTangentRibbon ? TEXT("tangent") : (bUseRibbon ? TEXT("ribbon") : TEXT("cube")));
}

void AArcTrajectoryHism::ClearTrajectories()
{
	SegmentHism->ClearInstances();
	SegmentIsm->ClearInstances();
}

int32 AArcTrajectoryHism::GetSegmentCount() const
{
	return GetActiveSegmentComponent()->GetInstanceCount();
}

void AArcTrajectoryHism::SetUseHierarchicalInstancing(const bool bUseHierarchical)
{
	bUseHierarchicalInstancing = bUseHierarchical;
}

bool AArcTrajectoryHism::IsUsingHierarchicalInstancing() const
{
	return bUseHierarchicalInstancing;
}

void AArcTrajectoryHism::SetTrajectoryShape(const EArcTrajectoryShape NewShape)
{
	TrajectoryShape = NewShape;
}

EArcTrajectoryShape AArcTrajectoryHism::GetTrajectoryShape() const
{
	return TrajectoryShape;
}

UInstancedStaticMeshComponent* AArcTrajectoryHism::GetActiveSegmentComponent() const
{
	return bUseHierarchicalInstancing ? SegmentHism.Get() : SegmentIsm.Get();
}
