// SmokeCanisterProjectile.cpp

#include "SmokeCanisterProjectile.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

ASmokeCanisterProjectile::ASmokeCanisterProjectile()
{
	// The canister does NOTHING on contact. Zero here is an EXPLICIT zero, not the inherit value:
	// the base ships HitDamage negative, meaning "take the firing weapon's number", and a projectile
	// meant purely as a delivery mechanism must not quietly pick up whatever gun launched it.
	HitDamage = 0.0f;
	CharacterKnockbackForce = 0.0f;
	KnockbackUpwardBias = 0.0f;

	PrimaryActorTick.bCanEverTick = false;

	if (ProjectileMovement)
	{
		ProjectileMovement->bIsHomingProjectile = false;
		ProjectileMovement->HomingTargetComponent = nullptr;
		ProjectileMovement->bRotationFollowsVelocity = true;
		// Real gravity, unlike the Wizard's straight bolt: a canister is thrown, and the arc is what
		// tells the player it will land short of where they are looking.
		ProjectileMovement->ProjectileGravityScale = 0.6f;
	}
}

void ASmokeCanisterProjectile::Launch(float Speed, float GravityScale, TSubclassOf<ASmokeCloud> InCloudClass,
	const FSmokeCloudShape& InShape, float InSightPenetration, float InTurnRateMultiplier,
	int32 InSplitCount, float InSplitSpacing)
{
	CloudClass = InCloudClass;
	Shape = InShape;
	SightPenetration = InSightPenetration;
	TurnRateMultiplier = InTurnRateMultiplier;
	SplitCount = FMath::Clamp(InSplitCount, 1, 9);
	SplitSpacing = FMath::Max(0.0f, InSplitSpacing);

	if (ProjectileMovement)
	{
		ProjectileMovement->ProjectileGravityScale = GravityScale;
		ProjectileMovement->InitialSpeed = Speed;
		ProjectileMovement->MaxSpeed = Speed;
		ProjectileMovement->Velocity = GetActorForwardVector() * Speed;
	}
}

void ASmokeCanisterProjectile::BecomeSubmunition(TSubclassOf<ASmokeCloud> InCloudClass,
	const FSmokeCloudShape& InShape, float InSightPenetration, float InTurnRateMultiplier,
	const FVector& Velocity)
{
	// Called on the piece itself rather than on whoever made it: a wall can now be deployed by a prop
	// that has no canister of its own to copy from, so the payload has to arrive as arguments.
	CloudClass = InCloudClass;
	Shape = InShape;
	SightPenetration = InSightPenetration;
	TurnRateMultiplier = InTurnRateMultiplier;
	bSubmunition = true;

	if (ProjectileMovement)
	{
		ProjectileMovement->InitialSpeed = 0.0f;   // the velocity below is the whole story
		ProjectileMovement->MaxSpeed = 0.0f;
		ProjectileMovement->Velocity = Velocity;
	}
}

void ASmokeCanisterProjectile::SplitInto(const FVector& ImpactPoint, const FVector& HitDirection)
{
	// The thrown canister's own landing. Everything about HOW a wall forms now lives in the static
	// below, because the prop has to be able to form the same one without flying first.
	FVector Travel = HitDirection.GetSafeNormal2D();
	if (Travel.IsNearlyZero())
	{
		Travel = GetActorForwardVector().GetSafeNormal2D();
	}

	DeploySmokeWall(GetWorld(), GetClass(), GetOwner(), GetInstigator(), ImpactPoint, Travel,
		Shape, CloudClass, SightPenetration, TurnRateMultiplier, SplitCount, SplitSpacing);
}

void ASmokeCanisterProjectile::DeploySmokeWall(UWorld* World, TSubclassOf<ASmokeCanisterProjectile> CanisterClass,
	AActor* InOwner, APawn* InInstigator, const FVector& ImpactPoint, const FVector& TravelDirection,
	const FSmokeCloudShape& InShape, TSubclassOf<ASmokeCloud> InCloudClass,
	float InSightPenetration, float InTurnRateMultiplier, int32 InSplitCount, float InSplitSpacing)
{
	if (!World || !CanisterClass || !InCloudClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SmokeScreen: wall refused, canister=%s cloud=%s"),
			*GetNameSafe(CanisterClass), *GetNameSafe(InCloudClass));
		return;
	}

	// The line lies ACROSS the shot, which is the reference's "perpendicular to where it was launched
	// from". Taken from the direction of travel rather than the thrower's facing, so a canister that
	// arced steeply or bounced still walls off the line it actually came down.
	FVector Travel = TravelDirection.GetSafeNormal2D();
	if (Travel.IsNearlyZero())
	{
		Travel = FVector::ForwardVector;
	}

	// The flight numbers are the CDO's, so they stay a Blueprint setting rather than an argument
	// every caller has to remember to pass.
	const ASmokeCanisterProjectile* Defaults = CanisterClass.GetDefaultObject();
	const float UpSpeed      = Defaults ? Defaults->SplitUpSpeed : 110.0f;
	const float LaunchHeight = Defaults ? Defaults->SplitLaunchHeight : 40.0f;
	const float MaxFlight    = Defaults ? Defaults->SplitMaxFlightTime : 0.8f;

	// Speed is DERIVED from the hang time, so a piece lands at its spacing instead of wherever a
	// hand-picked speed happened to put it. Gravity is read off the class the pieces will fly with.
	float GravityScale = 1.0f;
	if (Defaults && Defaults->ProjectileMovement)
	{
		GravityScale = Defaults->ProjectileMovement->ProjectileGravityScale;
	}
	const float Gravity = FMath::Abs(World->GetGravityZ()) * FMath::Max(0.05f, GravityScale);
	const float HangTime = FMath::Max(0.15f, 2.0f * UpSpeed / Gravity);

	// The pieces scatter into a FAN around the impact, not along a rail: the canisters visibly go
	// their own separate ways and the wall is what their clouds add up to.
	static constexpr float SplitFanDegrees = 65.0f;
	const int32 Count = FMath::Clamp(InSplitCount, 1, 9);
	const FVector Start = ImpactPoint + FVector(0.0f, 0.0f, LaunchHeight);

	TArray<ASmokeCanisterProjectile*> Spawned;
	Spawned.Reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		// Spread evenly across the fan: with three pieces that is left, straight on, right.
		const float Angle = (Count > 1)
			? -SplitFanDegrees + (2.0f * SplitFanDegrees * Index) / (Count - 1)
			: 0.0f;
		const FVector Direction = Travel.RotateAngleAxis(Angle, FVector::UpVector).GetSafeNormal2D();

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.Owner = InOwner;
		SpawnParams.Instigator = InInstigator;

		// Born already a little apart, so pieces spawned in the same frame do not start inside each
		// other.
		const FVector SubStart = Start + Direction * 25.0f;

		ASmokeCanisterProjectile* Sub = World->SpawnActor<ASmokeCanisterProjectile>(
			CanisterClass, SubStart, Travel.Rotation(), SpawnParams);
		if (!Sub)
		{
			continue;
		}

		Sub->BecomeSubmunition(InCloudClass, InShape, InSightPenetration, InTurnRateMultiplier,
			Direction * (InSplitSpacing / HangTime) + FVector(0.0f, 0.0f, UpSpeed));

		// Pieces born in the same frame a few centimetres apart WILL hit each other, and a
		// submunition that hits something deploys. Without these ignores the wall collapses into one
		// cloud at the impact point, which is the bug this whole split exists to avoid.
		if (Sub->CollisionComponent)
		{
			for (ASmokeCanisterProjectile* Sibling : Spawned)
			{
				Sub->CollisionComponent->IgnoreActorWhenMoving(Sibling, true);
				if (Sibling->CollisionComponent)
				{
					Sibling->CollisionComponent->IgnoreActorWhenMoving(Sub, true);
				}
			}
		}
		Spawned.Add(Sub);

		// The one that never lands still deploys. @see SplitMaxFlightTime
		World->GetTimerManager().SetTimer(Sub->DeployTimer, Sub,
			&ASmokeCanisterProjectile::DeployWhereverIAm, MaxFlight, false);
	}

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SmokeScreen: wall of %d at %s, spacing %.0f, hang %.2fs"),
		Count, *ImpactPoint.ToCompactString(), InSplitSpacing, HangTime);
}

void ASmokeCanisterProjectile::Deploy(const FVector& ImpactPoint)
{
	if (bDeployed || !HasAuthority())
	{
		return;
	}
	bDeployed = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeployTimer);

		if (!CloudClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] SmokeScreen: submunition landed with no CloudClass set"));
			return;
		}

		const FVector CloudLocation = FindFloorUnder(ImpactPoint) + FVector(0.0f, 0.0f, CloudHeightAboveFloor);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.Owner = GetOwner();
		SpawnParams.Instigator = GetInstigator();

		if (ASmokeCloud* Cloud = World->SpawnActor<ASmokeCloud>(
			CloudClass, CloudLocation, FRotator::ZeroRotator, SpawnParams))
		{
			Cloud->Begin(Shape, SightPenetration, TurnRateMultiplier);
		}
	}
}

void ASmokeCanisterProjectile::DeployWhereverIAm()
{
	Deploy(GetActorLocation());
	Destroy();
}

FVector ASmokeCanisterProjectile::FindFloorUnder(const FVector& ImpactPoint) const
{
	const UWorld* World = GetWorld();
	if (!World || FloorSearchDistance <= 0.0f)
	{
		return ImpactPoint;
	}

	// Started slightly above the impact so a hit that landed exactly on the floor does not begin the
	// trace inside it and miss.
	const FVector Start = ImpactPoint + FVector(0.0f, 0.0f, 20.0f);
	const FVector End = ImpactPoint - FVector(0.0f, 0.0f, FloorSearchDistance);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	if (GetInstigator())
	{
		Params.AddIgnoredActor(GetInstigator());
	}

	FHitResult Floor;
	if (World->LineTraceSingleByChannel(Floor, Start, End, ECC_WorldStatic, Params))
	{
		return Floor.ImpactPoint;
	}

	// Nothing under it -- thrown out over a gap. Leave the cloud where the canister stopped rather
	// than swallowing the cast: smoke in a strange place is a bad throw, no smoke at all is a bug.
	return ImpactPoint;
}

void ASmokeCanisterProjectile::ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp,
	const FVector& HitLocation, const FVector& HitDirection)
{
	if (HasAuthority())
	{
		if (bSubmunition)
		{
			Deploy(HitLocation);
		}
		else
		{
			SplitInto(FindFloorUnder(HitLocation), HitDirection);
		}
	}

	Super::ProcessHit(HitActor, HitComp, HitLocation, HitDirection);
}
