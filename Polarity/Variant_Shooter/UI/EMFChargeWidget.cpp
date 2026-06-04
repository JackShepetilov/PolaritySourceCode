// EMFChargeWidget.cpp
// Widget that displays EMF charge above an actor's head (NPC or Physics Prop)

#include "EMFChargeWidget.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/AI/HumanoidNPC.h"
#include "Variant_Shooter/Weapons/DroppedMeleeWeapon.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "Variant_Shooter/Weapons/RiotShieldPickup.h"
#include "ChargeAnimationComponent.h"
#include "EMFPhysicsProp.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"

void UEMFChargeWidget::UpdateScreenPosition(APlayerController* PC)
{
	if (!bIsActive || !PC)
	{
		return;
	}

	if (IsTargetDead())
	{
		SetVisibility(ESlateVisibility::Hidden);
		bWasVisibleLastFrame = false;
		return;
	}

	FVector WorldPos;
	if (!GetTargetWorldPosition(WorldPos))
	{
		SetVisibility(ESlateVisibility::Hidden);
		bWasVisibleLastFrame = false;
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
		bWasVisibleLastFrame = false;
		return;
	}

	// Occlusion check — hide if target is behind a wall
	if (bOcclusionCheck)
	{
		AActor* Target = GetBoundActor();
		if (Target)
		{
			UWorld* World = PC->GetWorld();
			if (World)
			{
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(ChargeWidgetOcclusion), true);
				Params.AddIgnoredActor(PC->GetPawn());
				Params.AddIgnoredActor(Target);

				if (World->LineTraceSingleByChannel(Hit, CameraLocation, Target->GetActorLocation(), ECC_Visibility, Params))
				{
					SetVisibility(ESlateVisibility::Hidden);
					bWasVisibleLastFrame = false;
					return;
				}
			}
		}
	}

	// Distance-based scaling using EffectiveMinScaleDistance (adjusted by clutter reduction)
	if (bEnableDistanceScaling)
	{
		float Distance = FVector::Dist(CameraLocation, WorldPos);
		float ScaleRange = FMath::Max(EffectiveMinScaleDistance - MaxScaleDistance, 1.0f);
		float Alpha = FMath::Clamp((Distance - MaxScaleDistance) / ScaleRange, 0.0f, 1.0f);
		float Scale = FMath::Lerp(MaxWidgetScale, MinWidgetScale, Alpha);

		// Hide widget when scale is effectively zero
		if (Scale < KINDA_SMALL_NUMBER)
		{
			SetVisibility(ESlateVisibility::Hidden);
			bWasVisibleLastFrame = false;
			return;
		}

		SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		FWidgetTransform WidgetTransform;
		WidgetTransform.Scale = FVector2D(Scale, Scale);
		SetRenderTransform(WidgetTransform);
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
		SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
		SetPositionInViewport(ScreenPosition, true);
		SetVisibility(ESlateVisibility::HitTestInvisible);
		bWasVisibleLastFrame = true;
	}
	else
	{
		SetVisibility(ESlateVisibility::Hidden);
		bWasVisibleLastFrame = false;
	}
}

EChargeWidgetCategory UEMFChargeWidget::GetCategory() const
{
	if (BoundNPC.IsValid())
	{
		return EChargeWidgetCategory::NPC;
	}
	if (BoundProp.IsValid())
	{
		return EChargeWidgetCategory::Prop;
	}
	// DroppedMeleeWeapon and DroppedRangedWeapon
	return EChargeWidgetCategory::Weapon;
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

	// Bind to stun and health delegates
	InNPC->OnStunStart.AddDynamic(this, &UEMFChargeWidget::OnNPCStunStart);
	InNPC->OnStunEnd.AddDynamic(this, &UEMFChargeWidget::OnNPCStunEnd);
	InNPC->OnDamageTaken.AddDynamic(this, &UEMFChargeWidget::OnNPCDamageTaken);

	// Cache max HP for normalization
	CachedMaxHP = InNPC->CurrentHP;

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
	BP_OnHealthChanged(InNPC->CurrentHP, CachedMaxHP, 1.0f);
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

void UEMFChargeWidget::BindToDroppedWeapon(ADroppedMeleeWeapon* InWeapon, float InVerticalOffset)
{
	if (!InWeapon)
	{
		return;
	}

	BoundDroppedWeapon = InWeapon;
	BoundNPC.Reset();
	BoundProp.Reset();
	VerticalOffset = InVerticalOffset;
	bIsActive = true;

	// Static charge — read once
	float Charge = InWeapon->GetCharge();
	float AbsCharge = FMath::Abs(Charge);

	CachedMaxCharge = FMath::Max(AbsCharge * 2.0f, 50.0f);
	CurrentCharge = AbsCharge;
	CurrentPolarity = (FMath::IsNearlyZero(Charge, 0.1f)) ? 0 : (Charge > 0.0f ? 1 : 2);
	NormalizedCharge = (CachedMaxCharge > 0.0f) ? FMath::Clamp(AbsCharge / CachedMaxCharge, 0.0f, 1.0f) : 0.0f;

	BP_OnBoundToNPC();
	BP_OnChargeUpdated(CurrentCharge, CurrentPolarity, NormalizedCharge);
}

void UEMFChargeWidget::BindToDroppedRangedWeapon(ADroppedRangedWeapon* InWeapon, float InVerticalOffset)
{
	if (!InWeapon)
	{
		return;
	}

	BoundDroppedRangedWeapon = InWeapon;
	BoundNPC.Reset();
	BoundProp.Reset();
	BoundDroppedWeapon.Reset();
	BoundRiotShieldPickup.Reset();
	VerticalOffset = InVerticalOffset;
	bIsActive = true;

	// Static charge — read once
	float Charge = InWeapon->GetCharge();
	float AbsCharge = FMath::Abs(Charge);

	CachedMaxCharge = FMath::Max(AbsCharge * 2.0f, 50.0f);
	CurrentCharge = AbsCharge;
	CurrentPolarity = (FMath::IsNearlyZero(Charge, 0.1f)) ? 0 : (Charge > 0.0f ? 1 : 2);
	NormalizedCharge = (CachedMaxCharge > 0.0f) ? FMath::Clamp(AbsCharge / CachedMaxCharge, 0.0f, 1.0f) : 0.0f;

	BP_OnBoundToNPC();
	BP_OnChargeUpdated(CurrentCharge, CurrentPolarity, NormalizedCharge);
}

void UEMFChargeWidget::BindToRiotShieldPickup(ARiotShieldPickup* InPickup, float InVerticalOffset)
{
	if (!InPickup)
	{
		return;
	}

	BoundRiotShieldPickup = InPickup;
	BoundNPC.Reset();
	BoundProp.Reset();
	BoundDroppedWeapon.Reset();
	BoundDroppedRangedWeapon.Reset();
	VerticalOffset = InVerticalOffset;
	bIsActive = true;

	// Static charge — read once (mirrors DroppedRangedWeapon path).
	float Charge = InPickup->GetCharge();
	float AbsCharge = FMath::Abs(Charge);

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
		NPC->OnStunStart.RemoveDynamic(this, &UEMFChargeWidget::OnNPCStunStart);
		NPC->OnStunEnd.RemoveDynamic(this, &UEMFChargeWidget::OnNPCStunEnd);
		NPC->OnDamageTaken.RemoveDynamic(this, &UEMFChargeWidget::OnNPCDamageTaken);
	}
	if (AEMFPhysicsProp* Prop = BoundProp.Get())
	{
		Prop->OnChargeChanged.RemoveDynamic(this, &UEMFChargeWidget::OnPropChargeUpdated);
	}

	bIsActive = false;
	bWasVisibleLastFrame = false;
	bIsInCaptureZone = false;
	BoundNPC.Reset();
	BoundProp.Reset();
	BoundDroppedWeapon.Reset();
	BoundDroppedRangedWeapon.Reset();
	BoundRiotShieldPickup.Reset();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UEMFChargeWidget::ResetWidget()
{
	Unbind();
	CurrentCharge = 0.0f;
	CurrentPolarity = 0;
	NormalizedCharge = 0.0f;
	CachedMaxCharge = 50.0f;
	CachedMaxHP = 100.0f;
	EffectiveMinScaleDistance = MinScaleDistance;
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
	if (ADroppedMeleeWeapon* Weapon = BoundDroppedWeapon.Get())
	{
		return Weapon;
	}
	if (ADroppedRangedWeapon* RangedWeapon = BoundDroppedRangedWeapon.Get())
	{
		return RangedWeapon;
	}
	if (ARiotShieldPickup* ShieldPickup = BoundRiotShieldPickup.Get())
	{
		return ShieldPickup;
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
		// Use PropMesh bounds directly — GetActorBounds includes ALL primitive components
		// (Niagara, etc.) which may stay at spawn position when PropMesh moves via physics
		if (Prop->PropMesh)
		{
			const FBoxSphereBounds& MeshBounds = Prop->PropMesh->Bounds;
			OutPosition = MeshBounds.Origin + FVector(0.0f, 0.0f, MeshBounds.BoxExtent.Z + VerticalOffset);
			return true;
		}
		return false;
	}

	if (ADroppedMeleeWeapon* Weapon = BoundDroppedWeapon.Get())
	{
		FVector Origin, BoxExtent;
		Weapon->GetActorBounds(false, Origin, BoxExtent);
		OutPosition = Origin + FVector(0.0f, 0.0f, BoxExtent.Z + VerticalOffset);
		return true;
	}

	if (ADroppedRangedWeapon* RangedWeapon = BoundDroppedRangedWeapon.Get())
	{
		FVector Origin, BoxExtent;
		RangedWeapon->GetActorBounds(false, Origin, BoxExtent);
		OutPosition = Origin + FVector(0.0f, 0.0f, BoxExtent.Z + VerticalOffset);
		return true;
	}

	if (ARiotShieldPickup* ShieldPickup = BoundRiotShieldPickup.Get())
	{
		FVector Origin, BoxExtent;
		ShieldPickup->GetActorBounds(false, Origin, BoxExtent);
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
	if (ADroppedMeleeWeapon* Weapon = BoundDroppedWeapon.Get())
	{
		return Weapon->IsPullComplete(); // "dead" once pulled/collected
	}
	if (ADroppedRangedWeapon* RangedWeapon = BoundDroppedRangedWeapon.Get())
	{
		return RangedWeapon->IsPullComplete(); // "dead" once pulled/collected
	}
	if (ARiotShieldPickup* ShieldPickup = BoundRiotShieldPickup.Get())
	{
		return ShieldPickup->IsBeingPulled(); // hide widget once pull starts (about to be equipped)
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

void UEMFChargeWidget::OnNPCStunStart(AShooterNPC* StunnedNPC, float Duration)
{
	BP_OnStunStart(Duration);
}

void UEMFChargeWidget::OnNPCStunEnd(AShooterNPC* StunnedNPC)
{
	BP_OnStunEnd();
}

void UEMFChargeWidget::OnNPCDamageTaken(AShooterNPC* DamagedNPC, float Damage, TSubclassOf<UDamageType> DamageType, FVector HitLocation, AActor* DamageCauser)
{
	if (AShooterNPC* NPC = BoundNPC.Get())
	{
		float HP = FMath::Max(NPC->CurrentHP, 0.0f);
		float Normalized = (CachedMaxHP > 0.0f) ? FMath::Clamp(HP / CachedMaxHP, 0.0f, 1.0f) : 0.0f;
		BP_OnHealthChanged(HP, CachedMaxHP, Normalized);
	}
}

// ==================== Capture Zone ====================

bool UEMFChargeWidget::EvaluateCaptureCandidate(
	const APawn* Player,
	const FVector& CameraLoc,
	const FVector& CameraForward,
	float& OutAngleCos) const
{
	OutAngleCos = -1.0f;
	if (!bIsActive || !Player) return false;

	AActor* Target = GetBoundActor();
	if (!Target) return false;

	const UChargeAnimationComponent* ChargeComp = Player->FindComponentByClass<UChargeAnimationComponent>();
	if (!ChargeComp) return false;

	// Already holding a captured target? In press-press capture mode the channel button now only
	// LAUNCHES — it can't start a new capture — so the player cannot capture anything right now.
	// Suppress the capture highlight/reticle for every target until the held object is thrown.
	if (ChargeComp->bUsePressPressCaptureMode)
	{
		const EChargeAnimationState AnimState = ChargeComp->GetAnimationState();
		if (AnimState == EChargeAnimationState::Channeling ||
			AnimState == EChargeAnimationState::ReverseChanneling ||
			AnimState == EChargeAnimationState::CaptureLockout)
		{
			return false;
		}
	}

	UEMFVelocityModifier* PlayerMod = Player->FindComponentByClass<UEMFVelocityModifier>();
	if (!PlayerMod) return false;
	const float PlayerCharge = PlayerMod->GetCharge();
	if (FMath::IsNearlyZero(PlayerCharge)) return false;

	// Resolve target |charge|, opposite-sign requirement, and (for humanoid yank targets) an
	// explicit capture range that replaces the generic curve.
	float TargetCharge = 0.0f;
	bool bRequiresOppositeSign = false;
	float CaptureRangeOverride = -1.0f; // <0 = resolve from the shared capture-range curve below
	if (AShooterNPC* NPC = BoundNPC.Get())
	{
		if (UEMFVelocityModifier* Mod = NPC->FindComponentByClass<UEMFVelocityModifier>())
		{
			TargetCharge = Mod->GetCharge();
		}
		bRequiresOppositeSign = true;

		// HumanoidNPCs are never body-captured — they are weapon/shield YANK targets, so the
		// highlight must follow the YANK gate, not the generic NPC capture-range curve. Mirror
		// UpdateCaptureRaycast's humanoid branch: require the weapon (or shield) to be yankable
		// right now, and use the yank range. CanBeYanked() folds in the boss's YankChargeThreshold
		// (sub-threshold → not yankable), and CalculateWeaponYankRange() returns 0 when it isn't,
		// so the boss only lights up once it's charged enough AND within yank distance.
		if (AHumanoidNPC* Humanoid = Cast<AHumanoidNPC>(NPC))
		{
			const bool bShieldYankable = Humanoid->CanShieldBeYanked();
			const bool bWeaponYankable = !bShieldYankable && Humanoid->CanBeYanked();
			if (!bShieldYankable && !bWeaponYankable) return false;
			CaptureRangeOverride = bShieldYankable
				? Humanoid->CalculateShieldYankRange()
				: Humanoid->CalculateWeaponYankRange();
		}
	}
	else if (AEMFPhysicsProp* Prop = BoundProp.Get())
	{
		TargetCharge = Prop->GetCharge();
	}
	else if (ADroppedMeleeWeapon* Weapon = BoundDroppedWeapon.Get())
	{
		TargetCharge = Weapon->GetCharge();
		bRequiresOppositeSign = true;
	}
	else if (ADroppedRangedWeapon* RangedWeapon = BoundDroppedRangedWeapon.Get())
	{
		TargetCharge = RangedWeapon->GetCharge();
		bRequiresOppositeSign = true;
	}
	else if (ARiotShieldPickup* ShieldPickup = BoundRiotShieldPickup.Get())
	{
		TargetCharge = ShieldPickup->GetCharge();
	}
	else
	{
		return false;
	}

	if (FMath::IsNearlyZero(TargetCharge)) return false;

	if (bRequiresOppositeSign && PlayerCharge * TargetCharge > 0.0f)
	{
		return false;
	}

	// Range gate: humanoid yank targets use their yank range (set above); everything else uses
	// the shared per-target capture-range curve.
	const float CaptureRange = (CaptureRangeOverride >= 0.0f)
		? CaptureRangeOverride
		: ChargeComp->EvaluateCaptureRange(FMath::Abs(TargetCharge));
	if (CaptureRange < 1.0f) return false;

	const FVector ToTarget = Target->GetActorLocation() - CameraLoc;
	const float DistSq = ToTarget.SizeSquared();
	if (DistSq < 1.0f || DistSq > CaptureRange * CaptureRange) return false;

	// Adaptive angle cone — matches UpdateCaptureRaycast:
	// near (≤NearFieldRadius) → 90°, far → CaptureMaxAngle.
	const FVector DirToTarget = ToTarget.GetUnsafeNormal();
	const float AngleCos = FVector::DotProduct(CameraForward, DirToTarget);

	constexpr float NearFieldRadius = 500.0f;
	const float Dist = FMath::Sqrt(DistSq);
	const float T = FMath::Clamp(Dist / NearFieldRadius, 0.0f, 1.0f);
	const float EffectiveAngle = FMath::Lerp(90.0f, ChargeComp->CaptureMaxAngle, T);
	const float MaxAngleCos = FMath::Cos(FMath::DegreesToRadians(EffectiveAngle));

	if (AngleCos < MaxAngleCos) return false;

	OutAngleCos = AngleCos;
	return true;
}

void UEMFChargeWidget::SetCaptureZoneState(bool bInZone)
{
	if (bIsInCaptureZone == bInZone) return;
	bIsInCaptureZone = bInZone;
	BP_OnCaptureZoneChanged(bInZone);
}

bool UEMFChargeWidget::GetTargetCenterAndRadius(FVector& OutCenter, float& OutRadius) const
{
	if (AShooterNPC* NPC = BoundNPC.Get())
	{
		// Capsule origin == actor location == body center. Use half-height as the radius so the
		// brackets wrap the whole body vertically.
		OutCenter = NPC->GetActorLocation();
		float HalfHeight = 88.0f;
		if (const UCapsuleComponent* Capsule = NPC->GetCapsuleComponent())
		{
			HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
		OutRadius = HalfHeight;
		return true;
	}

	if (AEMFPhysicsProp* Prop = BoundProp.Get())
	{
		// Use PropMesh bounds directly (mirrors GetTargetWorldPosition) — GetActorBounds would
		// include Niagara components that can lag behind physics movement.
		if (Prop->PropMesh)
		{
			OutCenter = Prop->PropMesh->Bounds.Origin;
			OutRadius = FMath::Max(static_cast<float>(Prop->PropMesh->Bounds.SphereRadius), 1.0f);
			return true;
		}
		return false;
	}

	// Dropped melee/ranged weapons and the riot shield pickup — use collider bounds.
	if (AActor* Target = GetBoundActor())
	{
		FVector Origin, BoxExtent;
		Target->GetActorBounds(false, Origin, BoxExtent);
		OutCenter = Origin;
		OutRadius = FMath::Max3(BoxExtent.X, BoxExtent.Y, BoxExtent.Z);
		return true;
	}

	return false;
}
