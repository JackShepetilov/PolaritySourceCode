// ShieldFieldComponent.cpp
// See the header for what this component is for and what Phase 0 means.

#include "Variant_Shooter/Shield/ShieldFieldComponent.h"

// The one-way bridge to polarity physics. THIS DIRECTION IS ALLOWED (shield -> charge, see the
// header); the reverse never is, and nothing else in this file touches the plugin.
#include "EMFVelocityModifier.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UShieldFieldComponent::UShieldFieldComponent()
{
	// The only thing on a clock here is the Timed rebuild, and that is a timer, not a tick: an idle
	// shield costs nothing.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UShieldFieldComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Both halves of the fraction: a bar normalising Current against a local Max that never arrived
	// would draw nonsense.
	DOREPLIFETIME(UShieldFieldComponent, CurrentShield);
	DOREPLIFETIME(UShieldFieldComponent, MaxShield);
	DOREPLIFETIME(UShieldFieldComponent, FieldSign);
}

void UShieldFieldComponent::BeginPlay()
{
	Super::BeginPlay();

	// A shield starts whole. On the authority this is the truth; on a client the replicated value
	// lands a moment later and is the same number.
	if (CurrentShield <= KINDA_SMALL_NUMBER)
	{
		CurrentShield = MaxShield;
	}
	bClientShieldWasUp = IsShieldUp();
}

float UShieldFieldComponent::GetShieldFraction() const
{
	if (MaxShield <= KINDA_SMALL_NUMBER)
	{
		// A zero-sized pool reads as whole, exactly as a charge carrier with no ceiling always read:
		// the mechanic is off for this target, and nothing about it is "stripped".
		return 1.0f;
	}
	return FMath::Clamp(CurrentShield / MaxShield, 0.0f, 1.0f);
}

float UShieldFieldComponent::AbsorbDamage(float InDamage, AActor* Instigator, UPrimitiveComponent* HitComponent)
{
	if (InDamage <= 0.0f)
	{
		return 0.0f;
	}

	if (!IsShieldUp())
	{
		// Nothing to absorb: the whole hit is overflow, and this is not a shield event at all.
		return InDamage;
	}

	const float Applied = FMath::Min(CurrentShield, InDamage);
	const float Overflow = InDamage - Applied;
	SetShieldLevel(CurrentShield - Applied, Instigator, HitComponent, Applied);

	// One event per landed hit, with the break flagged on the shot that caused it - the shooter's
	// hit marker and the field's own presentation read this, not the level.
	OnShieldHit.Broadcast(Applied, Overflow, !IsShieldUp());
	return Overflow;
}

float UShieldFieldComponent::ApplyIonization(float SignedChargePerHit, float ShieldDamage, AActor* Instigator)
{
	// The sign of the shot becomes the sign of the field: this is what decides which way the owner
	// pulls and pushes once the shield is gone. Replicated plainly; it is presentation.
	if (SignedChargePerHit > KINDA_SMALL_NUMBER)
	{
		FieldSign = 1;
	}
	else if (SignedChargePerHit < -KINDA_SMALL_NUMBER)
	{
		FieldSign = -1;
	}

	const float Damage = (ShieldDamage > 0.0f) ? ShieldDamage : FMath::Abs(SignedChargePerHit);

	if (!IsShieldUp())
	{
		// Broken field: ionization restores NOTHING - a shooter must not be able to heal an enemy by
		// shooting it - but it still polarises. The polarity is what the grab's opposite-sign rule
		// and the push/pull forces read, and after a break it is the only thing left to hand over.
		FeedPolarityCharge(FMath::Sign(SignedChargePerHit) * Damage);
		return 0.0f;
	}

	const float Applied = FMath::Min(CurrentShield, Damage);
	SetShieldLevel(CurrentShield - Applied, Instigator, nullptr, Applied);
	FeedPolarityCharge(FMath::Sign(SignedChargePerHit) * Applied);

	// The shot stripped the field; it broke it iff the level hit zero with this very shot.
	OnShieldHit.Broadcast(Applied, 0.0f, !IsShieldUp());
	return Applied;
}

float UShieldFieldComponent::AddShield(float Amount)
{
	if (Amount <= 0.0f || MaxShield <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	// You can only hand back what was taken, which is what makes an untouched enemy worth nothing
	// and a nearly stripped one worth everything - the incentive ShieldRestore is written around.
	const float Restored = FMath::Min(MaxShield - CurrentShield, Amount);
	if (Restored <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	SetShieldLevel(CurrentShield + Restored, nullptr, nullptr, 0.0f);
	return Restored;
}

void UShieldFieldComponent::ResetForReuse()
{
	LastShieldHitTime = -1000.0f;
	FieldSign = 0;
	SetShieldLevel(MaxShield, nullptr, nullptr, 0.0f);
}

void UShieldFieldComponent::SetMaxShield(float NewMaxShield)
{
	MaxShield = FMath::Max(0.0f, NewMaxShield);
	SetShieldLevel(FMath::Min(CurrentShield, MaxShield), nullptr, nullptr, 0.0f);
}

void UShieldFieldComponent::SetShieldTier(int32 NewTier)
{
	// The four armour tiers as pool sizes. MaxShield stays the truth; this only authors it.
	static constexpr float TierShieldAmounts[4] = { 50.0f, 75.0f, 100.0f, 125.0f };
	if (NewTier < 0 || NewTier >= 4)
	{
		return;
	}

	MaxShield = TierShieldAmounts[NewTier];
	SetShieldLevel(FMath::Min(CurrentShield, MaxShield), nullptr, nullptr, 0.0f);
}

void UShieldFieldComponent::SetShieldLevel(float NewShield, AActor* Instigator, UPrimitiveComponent* HitComponent, float IncomingDamage)
{
	const float Clamped = FMath::Clamp(NewShield, 0.0f, FMath::Max(MaxShield, 0.0f));
	if (FMath::IsNearlyEqual(Clamped, CurrentShield, KINDA_SMALL_NUMBER))
	{
		return;
	}

	const bool bWasUp = IsShieldUp();
	CurrentShield = Clamped;
	const bool bIsUp = IsShieldUp();

	if (IncomingDamage > KINDA_SMALL_NUMBER)
	{
		if (const UWorld* const World = GetWorld())
		{
			LastShieldHitTime = World->GetTimeSeconds();
		}
	}

	UpdateRegenTimer();

	// The mirror the old listeners still read, in the only allowed direction: the shield tells the
	// charge component what the shield state is; it never asks back.
	if (UEMFVelocityModifier* const Modifier = GetOwner() ? GetOwner()->FindComponentByClass<UEMFVelocityModifier>() : nullptr)
	{
		Modifier->MirrorShieldBrokenState(!bIsUp);
	}

	OnShieldChanged.Broadcast(CurrentShield, MaxShield);

	if (bWasUp != bIsUp)
	{
		HandleShieldTransition(bWasUp, bIsUp, Instigator, HitComponent);
	}
}

void UShieldFieldComponent::HandleShieldTransition(bool bWasUp, bool bIsUp, AActor* Instigator, UPrimitiveComponent* HitComponent)
{
	// The server walks this in the mutation, the client in OnRep, so a break reads the same on every
	// machine without an RPC. The world break sound rides the same mirror (the modifier's own
	// fields, already configured on the assets), so it is deliberately not played here as well.
	if (bWasUp && !bIsUp)
	{
		OnShieldBroken.Broadcast(Instigator, HitComponent);
	}
	else if (!bWasUp && bIsUp)
	{
		OnShieldRestored.Broadcast();
	}
}

void UShieldFieldComponent::OnRep_Shield()
{
	// The client catches the edge instead of the level, exactly as the server does, so the break
	// event and the mirror fire once per break and not once per replicated byte.
	const bool bIsUp = IsShieldUp();
	if (bClientShieldWasUp != bIsUp)
	{
		HandleShieldTransition(bClientShieldWasUp, bIsUp, nullptr, nullptr);
		bClientShieldWasUp = bIsUp;
	}

	OnShieldChanged.Broadcast(CurrentShield, MaxShield);
}

void UShieldFieldComponent::UpdateRegenTimer()
{
	UWorld* const World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// Only a broken shield in Timed mode has a clock at all. Manual mode never starts one: the
	// abilities are what bring a shield back there.
	if (RegenMode != EShieldRegenMode::Timed || IsShieldUp() || MaxShield <= KINDA_SMALL_NUMBER)
	{
		World->GetTimerManager().ClearTimer(RegenTimerHandle);
		return;
	}

	if (!RegenTimerHandle.IsValid())
	{
		World->GetTimerManager().SetTimer(RegenTimerHandle, this, &UShieldFieldComponent::TickRegen, RegenTickInterval, true);
	}
}

void UShieldFieldComponent::TickRegen()
{
	UWorld* const World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (RegenMode != EShieldRegenMode::Timed || IsShieldUp())
	{
		World->GetTimerManager().ClearTimer(RegenTimerHandle);
		return;
	}

	// The delay is measured from the last hit the SHIELD took, and a new hit restarts it from zero
	// by way of SetShieldLevel stamping the clock again.
	if (World->GetTimeSeconds() - LastShieldHitTime < RegenDelay)
	{
		return;
	}

	SetShieldLevel(CurrentShield + RegenRate * RegenTickInterval, nullptr, nullptr, 0.0f);
}

void UShieldFieldComponent::FeedPolarityCharge(float SignedAmount)
{
	if (!bFeedPolarityCharge || FMath::IsNearlyZero(SignedAmount))
	{
		return;
	}

	const AActor* const Owner = GetOwner();
	UEMFVelocityModifier* const Modifier = Owner ? Owner->FindComponentByClass<UEMFVelocityModifier>() : nullptr;
	if (!Modifier)
	{
		return;
	}

	// AddPermanentCharge keeps its own ceiling, which is where the push/pull forces max out. What
	// happens to the charge after this line is the meter's business; the shield never reads back.
	Modifier->AddPermanentCharge(SignedAmount);
}

UShieldFieldComponent* UShieldFieldStatics::GetShieldField(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UShieldFieldComponent>() : nullptr;
}

bool UShieldFieldStatics::IsShieldUp(const AActor* Actor)
{
	if (const UShieldFieldComponent* const Field = GetShieldField(Actor))
	{
		return Field->IsShieldUp();
	}

	return false;
}
