// EMFChargeWidget.cpp
// Widget that displays EMF charge above an actor's head (NPC or Physics Prop)

#include "EMFChargeWidget.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/AI/HumanoidNPC.h"
#include "Variant_Shooter/Weapons/DroppedMeleeWeapon.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "Variant_Shooter/Weapons/RiotShieldPickup.h"
#include "ChargeAnimationComponent.h"
#include "Variant_Shooter/MeleeAttackComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "EMFPhysicsProp.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

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

	// Props carry no shield until the player actually puts charge into them — an untouched
	// prop is scenery, so it shows nothing at all.
	if (bHidePropsUntilFirstCharge && BoundProp.IsValid() && !bPropHasBeenCharged)
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

	if (!bValidPosition)
	{
		SetVisibility(ESlateVisibility::Hidden);
		bWasVisibleLastFrame = false;
		return;
	}

	// Screen-centre focus: only targets the player is actually looking at get an indicator.
	// Measured in screen space (not as a world cone) so the gate stays constant regardless of
	// distance, and normalised by viewport height so it is resolution- and aspect-independent.
	if (bRequireScreenCenterFocus)
	{
		const FVector2D ViewportCenter(ViewportSizeX * 0.5f, ViewportSizeY * 0.5f);
		const float RefSize = FMath::Max(static_cast<float>(ViewportSizeY), 1.0f);
		const float NormalizedDist = FVector2D::Distance(ScreenPosition, ViewportCenter) / RefSize;

		const float Outer = FMath::Max(ScreenCenterOuterRadius, ScreenCenterInnerRadius);
		const float FadeBand = FMath::Max(Outer - ScreenCenterInnerRadius, KINDA_SMALL_NUMBER);
		const float FocusAlpha = 1.0f - FMath::Clamp((NormalizedDist - ScreenCenterInnerRadius) / FadeBand, 0.0f, 1.0f);

		if (FocusAlpha <= KINDA_SMALL_NUMBER)
		{
			SetVisibility(ESlateVisibility::Hidden);
			bWasVisibleLastFrame = false;
			return;
		}

		SetRenderOpacity(FocusAlpha);
	}
	else
	{
		SetRenderOpacity(1.0f);
	}

	SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
	SetPositionInViewport(ScreenPosition, true);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	bWasVisibleLastFrame = true;
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
	InNPC->OnHealthChanged.AddDynamic(this, &UEMFChargeWidget::OnNPCHealthChanged);

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

	UpdateShieldState(false);
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

	// A level-placed prop may already carry an authored charge; that does not count as the
	// player having charged it, so the indicator stays hidden until OnChargeChanged fires.
	bPropHasBeenCharged = false;

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

	UpdateShieldState(false);
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

	UpdateShieldState(false);
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

	UpdateShieldState(false);
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

	UpdateShieldState(false);
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
		NPC->OnHealthChanged.RemoveDynamic(this, &UEMFChargeWidget::OnNPCHealthChanged);
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

	// Pooled widgets must not inherit the previous target's shield/focus state.
	ShieldRemaining = 1.0f;
	ShieldDisplayValue = ShieldMaterialMaxValue;
	bShieldBroken = false;
	bPropHasBeenCharged = false;
	SetRenderOpacity(1.0f);

	// A pooled widget must not open on the previous target's damage readout. Cleared without firing
	// the Blueprint event: the reset event below is where a Blueprint does its own tidying up.
	DamagePreview = 0.0f;
	bHasDamagePreview = false;

	BP_OnWidgetReset();
}

void UEMFChargeWidget::SetDamagePreview(float PredictedDamage, bool bHasPreview)
{
	// The transition matters as much as the number: a target that drops off the list has to be told
	// to hide the text, and that is a change from true to false with no change in the value at all.
	const bool bVisibilityChanged = (bHasPreview != bHasDamagePreview);
	const bool bValueChanged = bHasPreview
		&& FMath::Abs(PredictedDamage - DamagePreview) > FMath::Max(0.0f, DamagePreviewEpsilon);

	bHasDamagePreview = bHasPreview;
	if (bHasPreview)
	{
		DamagePreview = PredictedDamage;
	}

	if (bVisibilityChanged || bValueChanged)
	{
		BP_OnDamagePreviewUpdated(DamagePreview, bHasDamagePreview);
	}
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

	// First charge a prop receives at runtime is what reveals its indicator.
	if (BoundProp.IsValid() && AbsCharge > PropFirstChargeThreshold)
	{
		bPropHasBeenCharged = true;
	}

	// Shield state first: BP_OnChargeUpdated handlers read ShieldDisplayValue, so it has to be
	// current by the time that event fires.
	UpdateShieldState(true);
	BP_OnChargeUpdated(CurrentCharge, CurrentPolarity, NormalizedCharge);
}

void UEMFChargeWidget::UpdateShieldState(bool bAllowBreakEffects)
{
	ShieldRemaining = FMath::Clamp(1.0f - NormalizedCharge, 0.0f, 1.0f);
	ShieldDisplayValue = ShieldRemaining * ShieldMaterialMaxValue;

	const bool bNowBroken = (ShieldRemaining <= ShieldBrokenThreshold);
	const bool bTransitioned = (bNowBroken != bShieldBroken);
	bShieldBroken = bNowBroken;

	BP_OnShieldUpdated(ShieldRemaining, ShieldDisplayValue, CurrentPolarity, bShieldBroken);

	if (!bTransitioned)
	{
		return;
	}

	if (bNowBroken)
	{
		if (bAllowBreakEffects)
		{
			PlayShieldBreakSound();
		}
		BP_OnShieldBroken();
	}
	else
	{
		BP_OnShieldRestored();
	}
}

void UEMFChargeWidget::PlayShieldBreakSound() const
{
	if (!ShieldBreakSound)
	{
		return;
	}

	const AActor* Target = GetBoundActor();
	if (!Target)
	{
		return;
	}

	// A target that carries a charge component announces its own break, from the same number that
	// decides whether bullets reach its health, on every machine, whether or not a health bar
	// happens to be on screen. Playing here as well would simply double it.
	// @see UEMFVelocityModifier::CheckShieldStateChanged
	if (Target->FindComponentByClass<UEMFVelocityModifier>())
	{
		return;
	}

	UWorld* World = Target->GetWorld();
	if (!World)
	{
		return;
	}

	UGameplayStatics::PlaySoundAtLocation(World, ShieldBreakSound, Target->GetActorLocation(), ShieldBreakSoundVolume);
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
	// Kept for whatever else this hook feeds; the bar itself is driven by OnNPCHealthChanged now,
	// which is the only one of the two that reaches a client.
}

void UEMFChargeWidget::OnNPCHealthChanged(AShooterNPC* NPC, float NewHP)
{
	if (NPC != BoundNPC.Get())
	{
		return;
	}

	// CachedMaxHP is taken from the NPC's HP at bind time. On the authority that is spawn time and
	// therefore full health. On a client the widget binds when the enemy becomes relevant, which can
	// be after it has already been hurt, and the bar would then read full at a damaged value. It
	// self-corrects downward the moment more damage lands, and a real maximum on the NPC would fix
	// it properly — there is no MaxHP field on the class today.
	const float HP = FMath::Max(NewHP, 0.0f);
	const float Normalized = (CachedMaxHP > 0.0f) ? FMath::Clamp(HP / CachedMaxHP, 0.0f, 1.0f) : 0.0f;
	BP_OnHealthChanged(HP, CachedMaxHP, Normalized);
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
			// Mirrors the acquisition scan: an enemy body is grabbable only at the charge cap, which
			// is the instant its shield reads empty, and at a flat range rather than off the curve.
			if (!Cast<AHumanoidNPC>(NPC) && !Mod->IsAtMaxCharge())
			{
				return false;
			}
			TargetCharge = Mod->GetCharge();
		}
		bRequiresOppositeSign = true;
		if (!Cast<AHumanoidNPC>(NPC))
		{
			CaptureRangeOverride = ChargeComp->NPCCaptureFixedRange;
		}

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
		// The same gate the acquisition scan runs, called rather than copied. Brackets that appear on
		// a prop the scan will then refuse are worse than no brackets: they read as a bug in the grab.
		if (!Prop->CanBeGrabbedBy(Player))
		{
			return false;
		}
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

bool UEMFChargeWidget::EvaluateLungeCandidate(
	const APawn* Player,
	const FVector& CameraLoc,
	const FVector& CameraForward,
	float& OutAngleCos) const
{
	OutAngleCos = -1.0f;
	if (!bIsActive || !Player)
	{
		return false;
	}

	// Enemies only. A prop is not something a swing flies at, and a teammate is a target the lunge
	// itself already refuses.
	AShooterNPC* NPC = BoundNPC.Get();
	if (!NPC || NPC->IsDead())
	{
		return false;
	}

	const AShooterCharacter* Swinger = Cast<AShooterCharacter>(Player);
	if (!Swinger)
	{
		return false;
	}

	// One call, and deliberately not "get a range and an angle and test them here". There are two
	// lunges in this project with different settings AND different shapes of aim test, and only the
	// character knows which one is currently in charge. Brackets built from a reconstructed cone
	// would be right for one of them and wrong for the other, which is worse than having none: they
	// would promise a lunge that then does not happen.
	if (!Swinger->WouldLungeAt(NPC, CameraLoc, CameraForward))
	{
		return false;
	}

	// Only for ranking one candidate against another, the way the capture check uses it. The accept
	// or reject has already been made above.
	const FVector ToTarget = NPC->GetActorLocation() - CameraLoc;
	OutAngleCos = FVector::DotProduct(CameraForward, ToTarget.GetSafeNormal());
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
