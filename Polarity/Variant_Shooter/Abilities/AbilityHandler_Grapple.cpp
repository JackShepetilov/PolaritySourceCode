// AbilityHandler_Grapple.cpp

#include "AbilityHandler_Grapple.h"
#include "AbilityDefinition_Grapple.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "GrappleFetchable.h"
#include "ApexMovementComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
	PendingAnchor = bHit ? Hit.ImpactPoint : End;
	const float Distance = FVector::Dist(Caster->GetActorLocation(), PendingAnchor);
	const bool bCanAttach = bHit && Distance >= Stats.MinAnchorDistance;

	// A miss still launches the hook all the way down the aim ray. The visual retracts on its own;
	// only a valid hit is allowed to start the movement simulation.
	// The aim ray starts at the camera, unlike the actor origin used by MinAnchorDistance.
	// Use the ray distance for flight timing so a miss reaches exactly Range at the authored speed.
	const float FlightDistance = FVector::Dist(Start, PendingAnchor);
	const float TravelTime = FlightDistance / FMath::Max(Stats.HookTravelSpeed, 1.0f);
	Caster->Multicast_PlayGrappleThrow(PendingAnchor, TravelTime, bCanAttach,
		const_cast<UAbilityDefinition_Grapple*>(Def), nullptr);
	if (!bCanAttach)
	{
		UE_LOG(LogTemp, Log, TEXT("[ABILITY_DEBUG] Grapple: fired but no valid anchor (hit=%d, distance=%.0f)"),
			bHit ? 1 : 0, Distance);
		NotifyAbilityCancelled();
		return;
	}

	bLineOut = true;
	bLineAttached = false;

	// The hook flies before it bites. That delay is not decoration: it is why a long throw is a
	// commitment, and it is the whole of what the cable is drawn along on every machine.
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

// ==================== Fetch ====================
// Only the two rules live here now. The fetch itself is the character's, on its own key, so that it
// works whatever ability is equipped. @see AShooterCharacter::Server_FetchItem

bool UAbilityHandler_Grapple::IsFetchable(const AShooterCharacter* Caster, const AActor* Target,
	const UAbilityDefinition_Grapple* Def, float ExtraRadius)
{
	if (!Caster || !IsValid(Target) || !Def || !Def->bCanFetchWeapons)
	{
		return false;
	}

	// The item decides whether it is free and whether this player may have it.
	const IGrappleFetchable* const Fetchable = GrappleFetch::Resolve(Target);
	if (!Fetchable || !Fetchable->CanBeGrappleFetchedBy(Caster))
	{
		return false;
	}

	const float Radius = Def->WeaponFetchRadius + FMath::Max(0.0f, ExtraRadius);
	return FVector::DistSquared(Caster->GetActorLocation(), Target->GetActorLocation()) <= FMath::Square(Radius);
}

AActor* UAbilityHandler_Grapple::FindFetchTarget(const AShooterCharacter* Caster,
	const UAbilityDefinition_Grapple* Def)
{
	UWorld* World = Caster ? Caster->GetWorld() : nullptr;
	if (!World || !Def || !Def->bCanFetchWeapons)
	{
		return nullptr;
	}

	// Down the character's own aim ray, the same one the throw leaves along. @see GetAimRay
	FVector Start, End;
	Caster->GetAimRay(1.0f, Start, End);
	const FVector AimDirection = (End - Start).GetSafeNormal();
	if (AimDirection.IsNearlyZero())
	{
		return nullptr;
	}
	const float AimCos = FMath::Cos(FMath::DegreesToRadians(Def->WeaponFetchAimAngle));

	struct FCandidate
	{
		AActor* Drop;
		FVector Center;
		float Cos;
	};
	TArray<FCandidate, TInlineAllocator<8>> Candidates;

	// Every fetchable in the world, which is a handful: the radius test inside IsFetchable throws
	// almost all of them out before any vector maths.
	TArray<AActor*> Fetchables;
	GrappleFetch::GetAll(World, Fetchables);
	for (AActor* Drop : Fetchables)
	{
		if (!IsFetchable(Caster, Drop, Def))
		{
			continue;
		}

		// The middle of the mesh, not the actor origin: a rifle's pivot can sit at its butt, and the
		// brackets and the hook both go where the player sees the gun.
		FVector Center, Extent;
		Drop->GetActorBounds(true, Center, Extent);
		const FVector ToDrop = Center - Start;
		const float Distance = ToDrop.Size();
		if (Distance < 1.0f)
		{
			continue;
		}

		// Looked at = within the authored angle, OR the crosshair is on the drop's own silhouette.
		// The second matters up close, where a gun at your feet is far wider than a few degrees.
		const float Cos = FVector::DotProduct(AimDirection, ToDrop / Distance);
		const float SizeCos = FMath::Cos(FMath::Atan2(Extent.Size(), Distance));
		if (Cos < FMath::Min(AimCos, SizeCos))
		{
			continue;
		}

		Candidates.Add({ Drop, Center, Cos });
	}

	// Closest to the crosshair first, and only the winners pay for a trace.
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Cos > B.Cos; });

	for (const FCandidate& Candidate : Candidates)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleFetchSight), false, Caster);
		Params.AddIgnoredActor(Candidate.Drop);
		// And whatever holds it: a slot machine item lies inside its dispenser's collision and hitbox,
		// which would otherwise hide it from every angle.
		Params.AddIgnoredActor(Candidate.Drop->GetOwner());

		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start, Candidate.Center, ECC_Visibility, Params))
		{
			return Candidate.Drop;
		}
	}

	return nullptr;
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
