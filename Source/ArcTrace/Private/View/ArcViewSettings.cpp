#include "View/ArcViewSettings.h"

#include "Components/PrimitiveComponent.h"

#include "CesiumGlobeAnchorComponent.h"

FMatrix ArcView::GetEcefToActorMatrix(const UCesiumGlobeAnchorComponent& GlobeAnchor)
{
	return GlobeAnchor.GetActorToEarthCenteredEarthFixedMatrix().Inverse();
}

FTransform ArcView::MakeLocalTransform(const FMatrix& EcefToActor, const FVector& PositionEcef, const FVector& VelocityEcef, const float Scale)
{
	const FVector Location = EcefToActor.TransformPosition(PositionEcef);
	const FVector Direction = EcefToActor.TransformVector(VelocityEcef).GetSafeNormal();
	const FQuat Rotation = Direction.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromZ(Direction).ToQuat();
	return FTransform(Rotation, Location, FVector(Scale));
}

void ArcView::ConfigureOverlayComponent(UPrimitiveComponent& Component)
{
	Component.SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component.SetCastShadow(false);
	SetOverlayLightingParticipation(Component, false);
}

void ArcView::SetOverlayLightingParticipation(UPrimitiveComponent& Component, const bool bParticipate)
{
	Component.bVisibleInRayTracing = bParticipate;
	Component.bAffectDistanceFieldLighting = bParticipate;
	Component.bAffectDynamicIndirectLighting = bParticipate;
	Component.bVisibleInReflectionCaptures = bParticipate;
	Component.bVisibleInRealTimeSkyCaptures = bParticipate;
	if (Component.IsRegistered())
	{
		Component.MarkRenderStateDirty();
	}
}
