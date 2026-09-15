#include "DispenserBuildable.h"

#include "Coop/CoopPlayers.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Net/UnrealNetwork.h"

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
	Fuel = FMath::Clamp(Fuel + Amount, 0, MaxFuel);
	OnBuildableChanged.Broadcast(this);
}

bool ADispenserBuildable::AcceptWeaponForFuel(AShooterCharacter* Donor, AShooterWeapon* Weapon)
{
	if (!HasAuthority() || !Donor || !Weapon || Weapon->IsMeleeWeapon() || !IsActive())
	{
		return false;
	}

	const float Reach = ServiceRadius + 150.0f;
	if (FVector::DistSquared(Donor->GetActorLocation(), GetActorLocation()) > FMath::Square(Reach))
	{
		return false;
	}

	int32 Loaded = 0;
	int32 Reserve = -1;
	if (!Donor->ReleaseWeaponToMount(Weapon, Loaded, Reserve))
	{
		return false;
	}

	// An endless class weapon has no removable reserve.  Its chassis still has its tuned value;
	// its default full reserve is used solely for the authored initial-price ratio.
	if (Reserve < 0)
	{
		Reserve = Weapon->UsesEnergyReserve() ? Weapon->GetEnergyReserveCapacity() : 0;
	}
	AddFuel(Weapon->GetDispenserFuelValue(Loaded, Reserve));
	UE_LOG(LogTemp, Log, TEXT("[DISPENSER_DEBUG] %s sacrificed %s for %d fuel"),
		*Donor->GetName(), *GetNameSafe(Weapon), Fuel);
	return true;
}

void ADispenserBuildable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADispenserBuildable, Fuel);
}

void ADispenserBuildable::OnRep_Fuel()
{
	OnBuildableChanged.Broadcast(this);
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
