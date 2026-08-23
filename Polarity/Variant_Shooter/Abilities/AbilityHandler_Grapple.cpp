// AbilityHandler_Grapple.cpp

#include "AbilityHandler_Grapple.h"
#include "AbilityDefinition_Grapple.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "ApexMovementComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UAbilityHandler_Grapple::OnActivate_Implementation()
{
	const UAbilityDefinition_Grapple* Def = Cast<UAbilityDefinition_Grapple>(GetDefinition());
	AShooterCharacter* Caster = GetOwningCharacter();
	UWorld* World = Caster ? Caster->GetWorld() : nullptr;
	if (!Def || !Caster || !World)
	{
		NotifyAbilityCancelled();
		return;
	}

	// A second press while a line is out drops it rather than throwing another. On Hold activation
	// this never happens; on Tap it is what makes the ability releasable at all.
	if (bLineOut)
	{
		ReleaseLine(false);
		return;
	}

	const FGrappleLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());

	// Anchored on world geometry, so it traces visibility rather than pawns: a hook that grabbed an
	// enemy would be a pull, which is a different mechanic and belongs to a different class.
	//
	// Down the character's own aim ray, the same one the weapon shoots along. This used to build its
	// own from GetPawnViewLocation() and the base aim rotation, which is a DIFFERENT origin -- the
	// capsule plus BaseEyeHeight rather than the camera the player is actually looking through -- so
	// the hook left along a line that did not pass through the crosshair and, next to any edge, bit
	// something else entirely. @see AShooterCharacter::GetAimRay
	FVector Start, End;
	Caster->GetAimRay(Stats.Range, Start, End);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Caster);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Grapple: nothing to anchor to within %.0f"), Stats.Range);
		NotifyAbilityCancelled();
		return;
	}

	PendingAnchor = Hit.ImpactPoint;
	const float Distance = FVector::Dist(Caster->GetActorLocation(), PendingAnchor);

	// Too close is refused rather than clamped: at arm's length this stops being a line and becomes
	// a free dash off any wall, which is not what the class is being given.
	if (Distance < Stats.MinAnchorDistance)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Grapple: anchor only %.0f away, minimum is %.0f"),
			Distance, Stats.MinAnchorDistance);
		NotifyAbilityCancelled();
		return;
	}

	bLineOut = true;
	bLineAttached = false;

	// The hook flies before it bites. That delay is not decoration: it is why a long throw is a
	// commitment, and it is the whole of what the cable is drawn along on every machine.
	const float TravelTime = Stats.HookTravelSpeed > 0.0f ? Distance / Stats.HookTravelSpeed : 0.0f;
	Caster->Multicast_PlayGrappleThrow(PendingAnchor, TravelTime,
		const_cast<UAbilityDefinition_Grapple*>(Def));

	if (TravelTime > 0.0f)
	{
		World->GetTimerManager().SetTimer(HookTravelTimer, this, &UAbilityHandler_Grapple::AttachLine,
			TravelTime, false);
	}
	else
	{
		AttachLine();
	}

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] Grapple: %s threw a line %.0f to (%.0f,%.0f,%.0f), travel %.2fs"),
		*Caster->GetName(), Distance, PendingAnchor.X, PendingAnchor.Y, PendingAnchor.Z, TravelTime);
}

void UAbilityHandler_Grapple::AttachLine()
{
	UAbilityDefinition_Grapple* Def = Cast<UAbilityDefinition_Grapple>(GetDefinition());
	AShooterCharacter* Caster = GetOwningCharacter();
	if (!Def || !Caster)
	{
		ReleaseLine(true);
		return;
	}

	// The player may have let go while the hook was still in the air. Nothing bites then.
	if (!bLineOut)
	{
		return;
	}

	bLineAttached = true;

	// Through the character, which sets it here AND on the machine that predicts this character's
	// movement. @see AShooterCharacter::SetGrappleLine for why both.
	Caster->SetGrappleLine(true, PendingAnchor, Def, GetCurrentLevel());

	if (Def->AttachVFX && Caster->GetWorld())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(Caster->GetWorld(), Def->AttachVFX, PendingAnchor);
	}
}

void UAbilityHandler_Grapple::OnActiveTick(float DeltaTime)
{
	if (!bLineOut || !bLineAttached)
	{
		return;
	}

	// The swing decides its own end, inside the movement simulation: it arrives at the anchor, or it
	// runs out of duration. Both happen without anybody telling this handler, so the ability watches
	// for the state going away rather than owning the moment it does.
	const AShooterCharacter* Caster = GetOwningCharacter();
	const UApexMovementComponent* Apex = Caster ? Caster->GetApexMovement() : nullptr;
	if (!Apex || !Apex->IsGrappling())
	{
		ReleaseLine(false);
	}
}

void UAbilityHandler_Grapple::OnButtonReleased_Implementation()
{
	ReleaseLine(false);
}

void UAbilityHandler_Grapple::OnCancelRequested_Implementation()
{
	ReleaseLine(true);
}

void UAbilityHandler_Grapple::OnUnequip_Implementation()
{
	// A line left attached to a character that no longer has the ability would pull forever.
	ReleaseLine(true);
}

void UAbilityHandler_Grapple::ReleaseLine(bool bCancelled)
{
	if (!bLineOut)
	{
		return;
	}

	bLineOut = false;
	bLineAttached = false;

	AShooterCharacter* Caster = GetOwningCharacter();
	if (Caster)
	{
		if (UWorld* World = Caster->GetWorld())
		{
			World->GetTimerManager().ClearTimer(HookTravelTimer);
		}

		// Nothing is done to the velocity here. The speed built on the line is the player's to keep,
		// and that is the point of the mechanic: letting go at the top of an arc is a decision worth
		// making. @see UApexMovementComponent::EndGrapple.
		Caster->SetGrappleLine(false, FVector::ZeroVector,
			Cast<UAbilityDefinition_Grapple>(GetDefinition()), GetCurrentLevel());
	}

	if (bCancelled)
	{
		NotifyAbilityCancelled();
	}
	else
	{
		NotifyAbilityComplete();
	}
}
