// GrenadeDefinition_FPV.cpp

#include "Variant_Shooter/Grenades/GrenadeDefinition_FPV.h"
#include "Variant_Shooter/AI/KamikazeDroneNPC.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Engine/World.h"

UGrenadeDefinition_FPV::UGrenadeDefinition_FPV()
{
	// One drone a cell: it is a machine, not a can.
	MaxPerCell = 1;
	// Thrown out level with the aim; the drone climbs by itself.
	ThrowPitchOffset = 0.0f;
	// Nobody runs from it: it is not lying there ticking, it is coming for them.
	DangerRadius = 0.0f;
}

AKamikazeDroneNPC* UGrenadeDefinition_FPV::LaunchDrone(AShooterCharacter* Thrower, const FVector& Location, const FRotator& Rotation) const
{
	UWorld* const World = Thrower ? Thrower->GetWorld() : nullptr;
	if (!World || !Thrower->HasAuthority() || !DroneClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRENADE_DEBUG] %s: no DroneClass, or no world"), *GetName());
		return nullptr;
	}

	// The side is set before BeginPlay: sight registers the team a pawn has at that moment
	// (Docs/Gotchas/AI_StateTree.md). So are the drops: a drone of ours shot down drops nothing.
	const FTransform Transform(Rotation, Location);
	AKamikazeDroneNPC* const Drone = World->SpawnActorDeferred<AKamikazeDroneNPC>(DroneClass, Transform, Thrower, Thrower,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Drone)
	{
		return nullptr;
	}
	Drone->SetGenericTeamId(Thrower->GetGenericTeamId());
	Drone->bSuppressDeathDrops = true;
	Drone->FinishSpawning(Transform);

	// Out of the hand without catching on the one who threw it.
	Drone->MoveIgnoreActorAdd(Thrower);
	Thrower->MoveIgnoreActorAdd(Drone);

	// The carrier's way: off along the throw, then each drone picks its own enemy and dives.
	Drone->LaunchAsHomingMunition(Rotation.Vector() * LaunchSpeed);

	UE_LOG(LogTemp, Log, TEXT("[GRENADE_DEBUG] %s launched FPV %s (team %d) from %s"),
		*GetNameSafe(Thrower), *GetNameSafe(Drone), Drone->GetGenericTeamId().GetId(), *Location.ToCompactString());
	return Drone;
}
