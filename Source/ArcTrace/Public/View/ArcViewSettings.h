#pragma once

#include "CoreMinimal.h"

class UCesiumGlobeAnchorComponent;
class UPrimitiveComponent;

// 시각화 공통 상수. 인스턴스 커스텀 데이터 배치: [0] 노출 시각(초), [1..3] 색(RGB).
namespace ArcView
{
	inline constexpr int32 CUSTOM_DATA_FLOAT_COUNT = 4;
	inline constexpr int32 CUSTOM_DATA_REVEAL_TIME = 0;
	inline constexpr int32 CUSTOM_DATA_COLOR_R = 1;

	// 머티리얼은 RevealTime <= CurrentTime 인 인스턴스만 그린다. 항상 보일 인스턴스에 쓰는 값.
	inline constexpr float ALWAYS_VISIBLE_REVEAL_TIME = -100000.0f;

	// 재연 시각(기록 시작 기준 초)은 머티리얼 파라미터 컬렉션 하나로 모든 머티리얼에 전달한다.
	// CPU는 매 프레임 이 값 하나만 쓰고, 노출 판정(Step)·미노출 구간 접기(WPO)는 GPU가 한다.
	const FName CURRENT_TIME_PARAMETER_NAME(TEXT("CurrentTime"));
	const TCHAR* const REPLAY_PARAMETER_COLLECTION_PATH = TEXT("/Game/ArcTrace/Materials/MPC_ArcReplay.MPC_ArcReplay");
	// 화면 크기 고정(WPO): 인스턴스가 화면에서 차지할 픽셀 수, 크기를 맞출 로컬 축 마스크.
	const FName SCREEN_PIXELS_PARAMETER_NAME(TEXT("ScreenPixels"));
	const FName SCALE_AXIS_MASK_PARAMETER_NAME(TEXT("ScaleAxisMask"));

	// 궤적 선분은 진행축(X) 길이를 유지하고 단면(Y, Z)만 픽셀 폭으로 맞춘다.
	inline const FLinearColor SEGMENT_SCALE_AXIS_MASK(0.0f, 1.0f, 1.0f, 0.0f);

	const TCHAR* const INSTANCE_MATERIAL_PATH = TEXT("/Game/ArcTrace/Materials/M_ArcInstance.M_ArcInstance");
	// 궤적 리본 모양용: 납작한 사각형을 카메라 쪽으로 세워 화면 폭만큼 벌린다.
	const TCHAR* const RIBBON_MATERIAL_PATH = TEXT("/Game/ArcTrace/Materials/M_ArcRibbon.M_ArcRibbon");
	// 탄젠트 리본: 정점 탄젠트·카메라 기준 위치·UV만으로 옆 방향을 구한다(인스턴스 변환 불필요, 폭 0 메시 전제).
	const TCHAR* const TANGENT_RIBBON_MATERIAL_PATH = TEXT("/Game/ArcTrace/Materials/M_ArcRibbonTangent.M_ArcRibbonTangent");

	// 종류 인덱스별 기본 색 (HeavyRocket, LightRocket, Shell 순).
	inline FLinearColor GetDefaultKindColor(const int32 KindIndex)
	{
		static const FLinearColor KIND_COLORS[] =
		{
			FLinearColor(1.0f, 0.25f, 0.1f),
			FLinearColor(1.0f, 0.75f, 0.1f),
			FLinearColor(0.2f, 0.6f, 1.0f),
		};
		return KIND_COLORS[FMath::Abs(KindIndex) % UE_ARRAY_COUNT(KIND_COLORS)];
	}

	// 탄·궤적 같은 발광 오버레이용 컴포넌트 설정.
	// 그림자·충돌·레이트레이싱·GI·반사 캡처에 참여할 이유가 없으므로 모두 제외한다.
	void ConfigureOverlayComponent(UPrimitiveComponent& Component);

	// 레이트레이싱·DF 조명·Lumen 간접광·반사 캡처 참여 여부. 측정용 A/B 전환에도 쓴다.
	void SetOverlayLightingParticipation(UPrimitiveComponent& Component, const bool bParticipate);

	// 지구 고정 앵커 기준 ECEF(m) -> 액터 로컬(cm) 변환 행렬. 언리얼 월드 원점과 무관하다.
	FMatrix GetEcefToActorMatrix(const UCesiumGlobeAnchorComponent& GlobeAnchor);

	// 원뿔 메시의 +Z 축을 진행 방향으로 맞춘 로컬 변환.
	FTransform MakeLocalTransform(const FMatrix& EcefToActor, const FVector& PositionEcef, const FVector& VelocityEcef, const float Scale);
}
