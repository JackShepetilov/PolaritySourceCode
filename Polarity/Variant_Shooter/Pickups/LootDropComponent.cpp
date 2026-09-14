// LootDropComponent.cpp

#include "LootDropComponent.h"
#include "HealthPickup.h"
#include "ArmorPickup.h"
#include "MetalPickup.h"
#include "Variant_Shooter/Weapons/DroppedMeleeWeapon.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Polarity/Upgrades/UpgradeManagerComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	/** The player behind a killing blow: the causer itself, its owner chain, or an instigator on
	 *  the way. Same walk the enemies used before the loot moved here. */
	AShooterCharacter* ResolveKiller(AActor* DamageCauser)
	{
		for (AActor* Candidate = DamageCauser; Candidate; Candidate = Candidate->GetOwner())
		{
			if (AShooterCharacter* Character = Cast<AShooterCharacter>(Candidate))
			{
				return Character;
			}
			if (AShooterCharacter* InstigatorCharacter = Cast<AShooterCharacter>(Candidate->GetInstigator()))
			{
				return InstigatorCharacter;
			}
		}
		return nullptr;
	}
}

bool FLootDropContext::Passes(ELootKillCondition Condition) const
{
	switch (Condition)
	{
	case ELootKillCondition::Channeled:        return bChanneled;
	case ELootKillCondition::PropOrDroneKill:  return bPropOrDroneKill;
	case ELootKillCondition::NPCCollisionKill: return bNPCCollisionKill;
	case ELootKillCondition::Any:
	default:                                   return true;
	}
}

ULootDropComponent::ULootDropComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void ULootDropComponent::DropLootHere()
{
	if (AActor* Owner = GetOwner())
	{
		FLootDropContext Context;
		Context.Location = Owner->GetActorLocation();
		Context.Rotation = Owner->GetActorRotation();
		DropLoot(Context);
	}
}

void ULootDropComponent::DropLoot(const FLootDropContext& Context)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || !Owner->HasAuthority())
	{
		return;
	}

	TSet<FName> GroupsDone;

	for (const FLootDropEntry& Entry : Loot)
	{
		if (!Entry.PickupClass)
		{
			continue;
		}

		const bool bGrouped = !Entry.Group.IsNone();
		if (bGrouped && GroupsDone.Contains(Entry.Group))
		{
			continue;
		}

		if (!Context.Passes(Entry.Condition))
		{
			continue;
		}

		const float Roll = FMath::FRand();
		if (Roll >= Entry.Chance)
		{
			UE_LOG(LogTemp, Log, TEXT("[LOOT_DEBUG] %s: %s missed (roll %.2f, chance %.2f)"),
				*Owner->GetName(), *Entry.PickupClass->GetName(), Roll, Entry.Chance);
			continue;
		}

		if (bGrouped)
		{
			GroupsDone.Add(Entry.Group);
		}

		SpawnEntry(World, Entry, Context, ScatterRadius, FloorOffset, PhysicalDropOffset, Owner);
		UE_LOG(LogTemp, Log, TEXT("[LOOT_DEBUG] %s: dropped %s x%d (amount %d, group %s)"),
			*Owner->GetName(), *Entry.PickupClass->GetName(), Entry.Count, Entry.Amount, *Entry.Group.ToString());
	}
}

UClass* ULootDropComponent::FindLootClass(const UClass* BaseClass) const
{
	for (const FLootDropEntry& Entry : Loot)
	{
		if (Entry.PickupClass && Entry.PickupClass->IsChildOf(BaseClass))
		{
			return Entry.PickupClass;
		}
	}
	return nullptr;
}

void ULootDropComponent::SpawnEntry(UWorld* World, const FLootDropEntry& Entry, const FLootDropContext& Context,
	float InScatterRadius, float InFloorOffset, const FVector& InPhysicalDropOffset, AActor* DroppedBy)
{
	if (!World || !Entry.PickupClass || Entry.Count <= 0 || World->GetNetMode() == NM_Client)
	{
		return;
	}

	UClass* Class = Entry.PickupClass;

	// Pickups that fly out of the kill and land around it.
	if (Class->IsChildOf(AHealthPickup::StaticClass()) || Class->IsChildOf(AMetalPickup::StaticClass()))
	{
		TArray<FVector> Points;
		if (Class->IsChildOf(AMetalPickup::StaticClass()))
		{
			// Coins scatter briefly at the kill, then fly to a player. Do not trace for a floor:
			// an airborne kill can be above a cliff or outside the old 50 m trace range.
			const float StartAngle = FMath::FRandRange(0.0f, 2.0f * PI);
			for (int32 Index = 0; Index < Entry.Count; ++Index)
			{
				const float Angle = StartAngle + 2.0f * PI * Index / Entry.Count;
				Points.Add(Context.Location + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f)
					* FMath::Clamp(InScatterRadius, 0.0f, 150.0f));
			}
		}
		else
		{
			ComputeScatterPoints(World, Context.Location, Entry.Count, InScatterRadius, InFloorOffset, Points);
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		for (const FVector& Point : Points)
		{
			AActor* Spawned = World->SpawnActor<AActor>(Class, Context.Location, FRotator::ZeroRotator, Params);
			if (AHealthPickup* Health = Cast<AHealthPickup>(Spawned))
			{
				if (Entry.Amount > 0)
				{
					Health->HealAmount = Entry.Amount;
				}
				Health->InitBurst(Point);
			}
			else if (AMetalPickup* Metal = Cast<AMetalPickup>(Spawned))
			{
				if (Entry.Amount > 0)
				{
					Metal->Amount = Entry.Amount;
				}
				Metal->InitBurst(Point);
			}
		}
		return;
	}

	// Armour appears where the enemy was, as it always did.
	if (Class->IsChildOf(AArmorPickup::StaticClass()))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 Index = 0; Index < Entry.Count; ++Index)
		{
			World->SpawnActor<AActor>(Class, Context.Location, FRotator::ZeroRotator, Params);
		}
		return;
	}

	// Weapons and anything else: spawned a little above the drop point and left to physics.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FVector Where = Context.Location + InPhysicalDropOffset;

	for (int32 Index = 0; Index < Entry.Count; ++Index)
	{
		AActor* Spawned = World->SpawnActor<AActor>(Class, Where, Context.Rotation, Params);
		if (!Spawned)
		{
			UE_LOG(LogTemp, Error, TEXT("[LOOT_DEBUG] SpawnActor failed for %s"), *Class->GetName());
			continue;
		}

		if (ADroppedMeleeWeapon* Melee = Cast<ADroppedMeleeWeapon>(Spawned))
		{
			if (!FMath::IsNearlyZero(Context.Charge))
			{
				Melee->SetCharge(Context.Charge);
			}
		}
		else if (ADroppedRangedWeapon* Ranged = Cast<ADroppedRangedWeapon>(Spawned))
		{
			if (!FMath::IsNearlyZero(Context.Charge))
			{
				Ranged->SetCharge(Context.Charge);
			}

			// The killer's upgrades may want to know a gun came out of their kill.
			if (AShooterCharacter* Killer = ResolveKiller(Context.KillingDamageCauser))
			{
				if (UUpgradeManagerComponent* Upgrades = Killer->GetUpgradeManager())
				{
					Upgrades->NotifyEnemyDroppedRangedWeapon(Ranged, DroppedBy);
				}
			}
		}
	}
}

void ULootDropComponent::ComputeScatterPoints(UWorld* World, const FVector& Origin, int32 Count,
	float InScatterRadius, float InFloorOffset, TArray<FVector>& OutPoints)
{
	OutPoints.Reset();
	if (!World || Count <= 0)
	{
		return;
	}

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(LootScatter), false);

	// Floor under the drop point. Flying enemies die in the air, so this can be a long way down.
	float FloorZ = Origin.Z;
	FHitResult FloorHit;
	if (World->LineTraceSingleByChannel(FloorHit, Origin, Origin - FVector(0.0f, 0.0f, 5000.0f), ECC_Visibility, TraceParams))
	{
		FloorZ = FloorHit.ImpactPoint.Z;
	}
	const FVector Base(Origin.X, Origin.Y, FloorZ + InFloorOffset);

	if (Count == 1)
	{
		OutPoints.Add(Base);
		return;
	}

	const float AngleStep = 360.0f / Count;
	const float StartAngle = FMath::FRandRange(0.0f, 360.0f);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float AngleRad = FMath::DegreesToRadians(StartAngle + AngleStep * Index);
		const FVector Dir(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.0f);
		FVector Point = Base + Dir * InScatterRadius;

		// Not into a wall.
		FHitResult WallHit;
		if (World->LineTraceSingleByChannel(WallHit, Base, Point, ECC_Visibility, TraceParams))
		{
			Point = WallHit.ImpactPoint - Dir * 20.0f;
		}

		// Not floating over a ledge.
		FHitResult PointFloor;
		const FVector Top(Point.X, Point.Y, Origin.Z + 200.0f);
		const FVector Bottom(Point.X, Point.Y, Origin.Z - 5000.0f);
		if (World->LineTraceSingleByChannel(PointFloor, Top, Bottom, ECC_Visibility, TraceParams))
		{
			Point.Z = PointFloor.ImpactPoint.Z + InFloorOffset;
		}

		OutPoints.Add(Point);
	}
}
