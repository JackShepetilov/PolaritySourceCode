#include "DispenserBuildable.h"

#include "Coop/CoopPlayers.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

ADispenserBuildable::ADispenserBuildable()
{
	bTickWhileActive = true;
}

void ADispenserBuildable::AddFuel(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0)
	{
		return;
	}
	Fuel = FMath::Max(0, Fuel + Amount);
}

void ADispenserBuildable::TickActive(float DeltaSeconds)
{
	if (!HasAuthority() || DeltaSeconds <= 0.0f)
	{
		return;
	}

	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);
	AmmoAccumulator += AmmoRoundsPerSecond * DeltaSeconds;
	for (APawn* Pawn : Players)
	{
		AShooterCharacter* Player = Cast<AShooterCharacter>(Pawn);
		if (!Player || FVector::DistSquared(Player->GetActorLocation(), GetActorLocation()) > FMath::Square(ServiceRadius))
		{
			continue;
		}

		Player->RestoreHealth(HealPerSecond * DeltaSeconds);
		AShooterWeapon* Weapon = Player->GetCurrentWeapon();
		if (!Weapon || !Weapon->UsesEnergyReserve() || Fuel <= 0 || AmmoAccumulator < 1.0f)
		{
			continue;
		}

		const int32 Room = Weapon->GetEnergyReserveCapacity() - Weapon->GetEnergyReserve();
		const int32 Rounds = FMath::Min3(FMath::FloorToInt(AmmoAccumulator), Room, Fuel);
		if (Rounds > 0)
		{
			Weapon->SetEnergyReserve(Weapon->GetEnergyReserve() + Rounds);
			Fuel -= Rounds;
			AmmoAccumulator -= Rounds;
		}
	}
}
