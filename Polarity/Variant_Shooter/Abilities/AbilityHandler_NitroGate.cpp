// AbilityHandler_NitroGate.cpp

#include "AbilityHandler_NitroGate.h"
#include "AbilityDefinition_NitroGate.h"
#include "NitroGate.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Engine/World.h"

void UAbilityHandler_NitroGate::MakeRoomForOneGate(int32 Limit)
{
	MyGates.RemoveAll([](const TWeakObjectPtr<ANitroGate>& Entry)
	{
		return !Entry.IsValid();
	});

	while (MyGates.Num() >= FMath::Max(1, Limit))
	{
		if (ANitroGate* Oldest = MyGates[0].Get())
		{
			Oldest->Destroy();
		}
		MyGates.RemoveAt(0);
	}
}

void UAbilityHandler_NitroGate::SpawnThrown(const FVector& Location, const FRotator& Rotation)
{
	const UAbilityDefinition_NitroGate* Def = Cast<UAbilityDefinition_NitroGate>(GetDefinition());
	AShooterCharacter* Caster = GetOwningCharacter();
	UWorld* World = Caster ? Caster->GetWorld() : nullptr;

	if (!Def || !Caster || !World || !Def->GateClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] NitroGate: no gate class set on %s"),
			Def ? *Def->GetName() : TEXT("<null definition>"));
		return;
	}

	const FNitroGateLevelStats Stats = Def->GetStatsAtLevel(GetCurrentLevel());

	MakeRoomForOneGate(Stats.MaxGatesInWorld);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = Caster;
	SpawnParams.Instigator = Caster;

	ANitroGate* Gate = World->SpawnActor<ANitroGate>(
		Def->GateClass, FTransform(Rotation, Location, FVector::OneVector), SpawnParams);
	if (!Gate)
	{
		return;
	}

	Gate->LaunchFrom(Location, Rotation, Def->ThrowSpeed, Stats.BoostSpeed, Def->PadRadius,
		Def->GateHealth, Def->bAffectsEnemies);

	MyGates.Add(Gate);

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] NitroGate: %s threw a gate (boost %.0f, %d/%d live)"),
		*Caster->GetName(), Stats.BoostSpeed, MyGates.Num(), Stats.MaxGatesInWorld);
}
