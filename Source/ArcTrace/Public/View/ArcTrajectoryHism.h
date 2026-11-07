#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Replay/ArcReplayTypes.h"

#include "ArcTrajectoryHism.generated.h"

class UCesiumGlobeAnchorComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class USceneComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

// 궤적 구간 하나를 그리는 메시 모양. 같은 샘플·같은 화면 폭을 유지하고 정점 수만 다르다.
UENUM()
enum class EArcTrajectoryShape : uint8
{
	// 정점 24개 큐브. 두께 두 축을 화면 폭으로 맞춘다.
	Cube,
	// 정점 4개 사각형. 시선과 선분의 외적 방향으로 화면 폭만큼 벌린다. 기본값 (2000발 GPU 30.9 → 10.0 ms).
	Ribbon,
	// 정점 4개 사각형, 폭 0으로 깔고 정점 탄젠트 기준으로 옆 방향을 구한다. 인스턴스 변환에 의존하지 않아 스플라인·절차적 리본에도 쓸 수 있는 방식.
	TangentRibbon,
};

// 전체 궤적을 기록 로드 시 한 번에 구간 인스턴스로 깔아 두고, 머티리얼이 시각으로 노출 여부를 정한다.
// 재생·탐색 중에는 MPC_ArcReplay의 CurrentTime 하나만 바뀌므로 CPU 비용이 트랙 수와 무관하다 (Director가 갱신).
// 탐색·되감기와 무관하게 "지금까지 지나온 궤적"이 항상 정확히 보인다.
UCLASS()
class ARCTRACE_API AArcTrajectoryHism : public AActor
{
	GENERATED_BODY()

public:
	AArcTrajectoryHism();

	// 샘플 ECEF 좌표를 지구 고정 앵커 기준 로컬 좌표로 바꿔 구간 인스턴스를 깐다.
	void BuildTrajectories(const FArcReplayRecord& Record);
	void ClearTrajectories();


	int32 GetSegmentCount() const;

	// 측정용 A/B: true면 HISM(CPU 클러스터 트리 컬링), false면 ISM(GPU Scene 인스턴스 컬링). 다음 BuildTrajectories부터 적용.
	void SetUseHierarchicalInstancing(const bool bUseHierarchical);
	bool IsUsingHierarchicalInstancing() const;

	// 측정용 A/B: 구간 메시 모양. 다음 BuildTrajectories부터 적용 (ArcTrace.TrajectoryShape).
	void SetTrajectoryShape(const EArcTrajectoryShape NewShape);
	EArcTrajectoryShape GetTrajectoryShape() const;

private:
	UInstancedStaticMeshComponent* GetActiveSegmentComponent() const;

private:
	UPROPERTY(VisibleAnywhere, Category = "ArcTrace")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "ArcTrace")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> SegmentHism;

	UPROPERTY(VisibleAnywhere, Category = "ArcTrace")
	TObjectPtr<UInstancedStaticMeshComponent> SegmentIsm;

	// 기본은 HISM. ISM은 측정·비교용으로만 남겨 둔다 (ArcTrace.TrajectoryInstancing).
	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	bool bUseHierarchicalInstancing;

	// Cesium 원점이 이동해도 궤적이 지구 위 같은 자리에 머물게 한다.
	UPROPERTY(VisibleAnywhere, Category = "ArcTrace")
	TObjectPtr<UCesiumGlobeAnchorComponent> GlobeAnchor;

	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	EArcTrajectoryShape TrajectoryShape;

	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	TObjectPtr<UMaterialInterface> CubeMaterial;

	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	TObjectPtr<UStaticMesh> RibbonMesh;

	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	TObjectPtr<UMaterialInterface> RibbonMaterial;

	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	TObjectPtr<UMaterialInterface> TangentRibbonMaterial;

	// 구간 하나가 건너뛰는 샘플 수. 기록 간격 100ms 기준 5면 0.5초 단위 구간.
	UPROPERTY(EditAnywhere, Category = "ArcTrace", meta = (ClampMin = "1"))
	int32 SampleStride;

	// 메시 기준 단면 폭. 실제 화면 폭은 머티리얼 WPO가 LinePixelWidth로 맞추므로 바운드 계산용이다.
	UPROPERTY(EditAnywhere, Category = "ArcTrace", meta = (ClampMin = "1.0"))
	float LineWidthCm;

	// 거리와 무관하게 궤적이 화면에서 차지하는 폭(px).
	UPROPERTY(EditAnywhere, Category = "ArcTrace", meta = (ClampMin = "0.5"))
	float LinePixelWidth;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TrajectoryMaterial;
};
