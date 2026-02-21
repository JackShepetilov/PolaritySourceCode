// EMFChargeWidget.cpp
// Widget that displays EMF charge above an actor's head (NPC or Physics Prop)

#include "EMFChargeWidget.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "EMFPhysicsProp.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"

void UEMFChargeWidget::UpdateScreenPosition(APlayerController* PC)
{
	if (!bIsActive || !PC)
	{
		return;
	}

	if (IsTargetDead())
	{
		SetVisibility(ESlateVisibility::Hidden);
		return;
	}

	FVector WorldPos;
	if (!GetTargetWorldPosition(WorldPos))
	{
		SetVisibility(ESlateVisibility::Hidden);
		return;
	}

	// Check if point is in front of camera
	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	FVector CameraForward = CameraRotation.Vector();

	FVector ToPoint = WorldPos - CameraLocation;
	float DotProduct = FVector::DotProduct(ToPoint.GetSafeNormal(), CameraForward);

	if (DotProduct <= 0.0f)
	{
		SetVisibility(ESlateVisibility::Hidden);
		return;
	}

	// Project to screen
	FVector2D ScreenPosition;
	bool bOnScreen = PC->ProjectWorldLocationToScreen(WorldPos, ScreenPosition, false);

	int32 ViewportSizeX, ViewportSizeY;
	PC->GetViewportSize(ViewportSizeX, ViewportSizeY);

	bool bValidPosition = bOnScreen &&
		ScreenPosition.X >= -200.0f && ScreenPosition.X <= ViewportSizeX + 200.0f &&
		ScreenPosition.Y >= -200.0f && ScreenPosition.Y <= ViewportSizeY + 200.0f;

	if (bValidPosition)
	{
		FVector2D CenteredPosition = ScreenPosition - WidgetHalfSize;
		SetPositionInViewport(CenteredPosition, true);
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		SetVisibility(ESlateVisibility::Hidden);
	}
}

void UEMFChargeWidget::BindToNPC(AShooterNPC* InNPC, float InVerticalOffset)
{
	if (!InNPC)
	{
		return;
	}

	BoundNPC = InNPC;
	BoundProp.Reset();
	VerticalOffset = InVerticalOffset;
	bIsActive = true;

	// Bind to charge update delegate
	InNPC->OnChargeUpdated.AddDynamic(this, &UEMFChargeWidget::OnNPCChargeUpdated);

	// Cache max charge and get initial state
	if (UEMFVelocityModifier* EMF = InNPC->FindComponentByClass<UEMFVelocityModifier>())
	{
		CachedMaxCharge = EMF->MaxBaseCharge + EMF->MaxBonusCharge;
		float Charge = EMF->GetTotalCharge();
		float AbsCharge = FMath::Abs(Charge);

		CurrentCharge = AbsCharge;
		CurrentPolarity = (FMath::IsNearlyZero(Charge, 0.1f)) ? 0 : (Charge > 0.0f ? 1 : 2);
		NormalizedCharge = (CachedMaxCharge > 0.0f) ? FMath::Clamp(AbsCharge / CachedMaxCharge, 0.0f, 1.0f) : 0.0f;
	}

	BP_OnBoundToNPC();
	BP_OnChargeUpdated(CurrentCharge, CurrentPolarity, NormalizedCharge);
}

void UEMFChargeWidget::BindToProp(AEMFPhysicsProp* InProp, float InVerticalOffset)
{
	if (!InProp)
	{
		return;
	}

	BoundProp = InProp;
	BoundNPC.Reset();
	VerticalOffset = InVerticalOffset;
	bIsActive = true;

	// Bind to charge changed delegate
	InProp->OnChargeChanged.AddDynamic(this, &UEMFChargeWidget::OnPropChargeUpdated);

	// Get initial state from FieldComponent
	float Charge = InProp->GetCharge();
	float AbsCharge = FMath::Abs(Charge);

	// Props don't have MaxBaseCharge/MaxBonusCharge — use current charge as reference
	CachedMaxCharge = FMath::Max(AbsCharge * 2.0f, 50.0f);

	CurrentCharge = AbsCharge;
	CurrentPolarity = (FMath::IsNearlyZero(Charge, 0.1f)) ? 0 : (Charge > 0.0f ? 1 : 2);
	NormalizedCharge = (CachedMaxCharge > 0.0f) ? FMath::Clamp(AbsCharge / CachedMaxCharge, 0.0f, 1.0f) : 0.0f;

	BP_OnBoundToNPC();
	BP_OnChargeUpdated(CurrentCharge, CurrentPolarity, NormalizedCharge);
}

void UEMFChargeWidget::Unbind()
{
	if (AShooterNPC* NPC = BoundNPC.Get())
	{
		NPC->OnChargeUpdated.RemoveDynamic(this, &UEMFChargeWidget::OnNPCChargeUpdated);
	}
	if (AEMFPhysicsProp* Prop = BoundProp.Get())
	{
		Prop->OnChargeChanged.RemoveDynamic(this, &UEMFChargeWidget::OnPropChargeUpdated);
	}

	bIsActive = false;
	BoundNPC.Reset();
	BoundProp.Reset();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UEMFChargeWidget::ResetWidget()
{
	Unbind();
	CurrentCharge = 0.0f;
	CurrentPolarity = 0;
	NormalizedCharge = 0.0f;
	CachedMaxCharge = 50.0f;
	BP_OnWidgetReset();
}

AActor* UEMFChargeWidget::GetBoundActor() const
{
	if (AShooterNPC* NPC = BoundNPC.Get())
	{
		return NPC;
	}
	if (AEMFPhysicsProp* Prop = BoundProp.Get())
	{
		return Prop;
	}
	return nullptr;
}

bool UEMFChargeWidget::GetTargetWorldPosition(FVector& OutPosition) const
{
	if (AShooterNPC* NPC = BoundNPC.Get())
	{
		FVector Location = NPC->GetActorLocation();
		float CapsuleHalfHeight = 0.0f;
		if (const UCapsuleComponent* Capsule = NPC->GetCapsuleComponent())
		{
			CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
		OutPosition = Location + FVector(0.0f, 0.0f, CapsuleHalfHeight + VerticalOffset);
		return true;
	}

	if (AEMFPhysicsProp* Prop = BoundProp.Get())
	{
		// Use bounds top for props
		FVector Origin, BoxExtent;
		Prop->GetActorBounds(false, Origin, BoxExtent);
		OutPosition = Origin + FVector(0.0f, 0.0f, BoxExtent.Z + VerticalOffset);
		return true;
	}

	return false;
}

bool UEMFChargeWidget::IsTargetDead() const
{
	if (AShooterNPC* NPC = BoundNPC.Get())
	{
		return NPC->IsDead();
	}
	if (AEMFPhysicsProp* Prop = BoundProp.Get())
	{
		return Prop->IsDead();
	}
	return true; // No valid target
}

void UEMFChargeWidget::OnNPCChargeUpdated(float InChargeValue, uint8 InPolarity)
{
	HandleChargeUpdate(InChargeValue, InPolarity);
}

void UEMFChargeWidget::OnPropChargeUpdated(float InNewCharge, uint8 InNewPolarity)
{
	HandleChargeUpdate(InNewCharge, InNewPolarity);
}

void UEMFChargeWidget::HandleChargeUpdate(float InChargeValue, uint8 InPolarity)
{
	float AbsCharge = FMath::Abs(InChargeValue);
	NormalizedCharge = (CachedMaxCharge > 0.0f) ? FMath::Clamp(AbsCharge / CachedMaxCharge, 0.0f, 1.0f) : 0.0f;
	CurrentCharge = AbsCharge;
	CurrentPolarity = InPolarity;

	BP_OnChargeUpdated(CurrentCharge, CurrentPolarity, NormalizedCharge);
}
