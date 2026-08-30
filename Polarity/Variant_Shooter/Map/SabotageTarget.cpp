// SabotageTarget.cpp

#include "Variant_Shooter/Map/SabotageTarget.h"

#include "Variant_Shooter/Map/FactionHq.h"

#include "Components/SceneComponent.h"

ASabotageTarget::ASabotageTarget()
{
	PrimaryActorTick.bCanEverTick = false;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	// Damage arrives through the mesh a Blueprint subclass adds; this class only counts it.
	SetCanBeDamaged(true);
}

float ASabotageTarget::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	if (bBroken || !HasAuthority())
	{
		return Applied;
	}

	Health -= Damage;
	if (Health <= 0.0f)
	{
		Break(DamageCauser);
	}

	return Applied;
}

void ASabotageTarget::Break(AActor* Breaker)
{
	if (bBroken || !HasAuthority())
	{
		return;
	}

	bBroken = true;
	Health = 0.0f;

	// The headquarters tells the director, not this actor: the effect belongs to the faction, and
	// the faction is what the headquarters knows and this object does not.
	if (AFactionHq* Hq = OwningHq.Get())
	{
		Hq->NotifyTargetBroken(this);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MAP_DEBUG] %s was broken but belongs to no HQ. Drag it into the HQ's SabotageTargets."),
			*GetName());
	}

	UE_LOG(LogTemp, Log, TEXT("[MAP_DEBUG] Sabotage target %s broken by %s"),
		*GetName(), Breaker ? *Breaker->GetName() : TEXT("nobody"));

	OnBroken.Broadcast(this);
}
