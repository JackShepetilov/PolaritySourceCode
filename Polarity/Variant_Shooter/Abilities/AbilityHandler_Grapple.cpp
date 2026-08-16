// AbilityHandler_Grapple.cpp

#include "AbilityHandler_Grapple.h"
#include "AbilityDefinition_Grapple.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Engine/World.h"

void UAbilityHandler_Grapple::OnActivate_Implementation()
{
	const UAbilityDefinition_Grapple* Def = Cast<UAbilityDefinition_Grapple>(GetDefinition());
	AShooterCharacter* Caster = GetOwningCharacter();
	if (!Def || !Caster || !Caster->GetWorld())
	{
		NotifyAbilityCancelled();
		return;
	}

	const FGrappleLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());

	// Anchored on world geometry, so it traces visibility rather than pawns: a hook that grabs an
	// enemy would be a pull, which is a different mechanic and belongs to a different class.
	const FVector Start = Caster->GetPawnViewLocation();
	const FVector End = Start + Caster->GetBaseAimRotation().Vector() * Stats.Range;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Caster);

	FHitResult Hit;
	if (!Caster->GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Grapple: nothing to anchor to within %.0f"), Stats.Range);
		NotifyAbilityCancelled();
		return;
	}

	const FVector Anchor = Hit.ImpactPoint;
	FVector ToAnchor = Anchor - Caster->GetActorLocation();
	const float Distance = ToAnchor.Size();

	// Too close is refused rather than clamped: at arm's length this stops being a hook and becomes
	// a free dash off any wall, which is not what the class is being given.
	if (Distance < Stats.MinAnchorDistance)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Grapple: anchor only %.0f away, minimum is %.0f"),
			Distance, Stats.MinAnchorDistance);
		NotifyAbilityCancelled();
		return;
	}

	ToAnchor /= Distance;
	const FVector LaunchVelocity = ToAnchor * Stats.PullSpeed + FVector(0.0f, 0.0f, Stats.UpwardBoost);

	// Both ends, for the same reason every other launch in this project does it: a server-only pull
	// argues with a client that is still predicting its own movement.
	Caster->LaunchCharacter(LaunchVelocity, true, true);
	if (!Caster->IsLocallyControlled())
	{
		Caster->Client_ApplyKnockback(LaunchVelocity);
	}

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Grapple: %s pulled %.0f toward %s at %.0f"),
		*Caster->GetName(), Distance, *Anchor.ToString(), LaunchVelocity.Size());

	NotifyAbilityComplete();
}
