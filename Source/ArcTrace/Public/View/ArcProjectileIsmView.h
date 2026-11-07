#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "View/ArcProjectileView.h"

#include "ArcProjectileIsmView.generated.h"

class UCesiumGlobeAnchorComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

// 투사체 표현의 ISM 구현. 종류마다 ISM 컴포넌트 하나, 트랙마다 인스턴스 하나를 미리 확보하고
// 숨김은 크기 0 변환으로 처리한다 (인스턴스 추가·삭제로 인덱스가 흔들리지 않게).
UCLASS()
class ARCTRACE_API AArcProjectileIsmView : public AActor, public IArcProjectileView
{
	GENERATED_BODY()

public:
	AArcProjectileIsmView();

	virtual void InitializeView(const FArcReplayRecord& Record) override;
	virtual void ApplyStates(const TArray<FArcProjectileViewState>& States) override;
	virtual void ResetView() override;


private:
	UInstancedStaticMeshComponent* CreateKindComponent(const int32 KindIndex);

private:
	UPROPERTY(VisibleAnywhere, Category = "ArcTrace")
	TObjectPtr<USceneComponent> SceneRoot;

	// Cesium 원점이 이동(origin shift)해도 인스턴스가 지구 위 같은 자리에 머물게 한다.
	UPROPERTY(VisibleAnywhere, Category = "ArcTrace")
	TObjectPtr<UCesiumGlobeAnchorComponent> GlobeAnchor;

	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	TObjectPtr<UStaticMesh> ProjectileMesh;

	UPROPERTY(EditAnywhere, Category = "ArcTrace")
	TObjectPtr<UMaterialInterface> InstanceMaterial;

	// 메시 원본 크기(1m) 대비 배율. 실제 화면 크기는 머티리얼 WPO가 ProjectilePixelSize로 맞추므로 바운드 계산용이다.
	UPROPERTY(EditAnywhere, Category = "ArcTrace", meta = (ClampMin = "0.01"))
	float ProjectileScale;

	// 거리와 무관하게 탄이 화면에서 차지하는 크기(px).
	UPROPERTY(EditAnywhere, Category = "ArcTrace", meta = (ClampMin = "1.0"))
	float ProjectilePixelSize;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> KindComponents;

	TArray<int32> InstanceIndexByTrack;
	TArray<int32> KindIndexByTrack;
};
