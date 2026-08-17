// AbilityHandler_TankPassive.h
// Runs the Tank's three passive effects. Authority only, like every handler that decides anything.

#pragma once

#include "CoreMinimal.h"
#include "AbilityHandler.h"
#include "AbilityHandler_TankPassive.generated.h"

class AShooterNPC;
class AShooterWeapon;
class UThreatComponent;
class UDamageType;

/**
 * Three effects, three different shapes of hook, and none of them a tick:
 *
 *  - kills nearby   — AShooterNPC::OnAnyNPCDeath, the static one every NPC broadcasts as it dies
 *  - damage return  — UAbilityComponent::NotifyOwnerDamaged, called from the owner's TakeDamage
 *  - provocation    — AShooterWeapon::OnShotFired on whatever is currently in hand
 *
 * The one thing that does need the tick is noticing that the weapon changed. The component's tick
 * calls OnPassiveTick, and all this does with it is compare two pointers.
 */
UCLASS()
class POLARITY_API UAbilityHandler_TankPassive : public UAbilityHandler
{
	GENERATED_BODY()

public:
	virtual void OnEquip_Implementation() override;
	virtual void OnUnequip_Implementation() override;

	virtual void OnPassiveTick(float DeltaTime) override;
	virtual void OnOwnerDamaged(float Damage, AActor* DamageCauser, AController* InstigatedBy, const FHitResult& HitInfo) override;

protected:
	/** Where the return should land on this enemy: the same bone the Tank was hit in when the enemy
	 *  has one by that name, and its centre when it does not. A drone shares no bone names with a
	 *  humanoid, so it takes the whole thing in the middle rather than at a bone that never matched. */
	FVector ResolveMirroredPoint(const AShooterNPC* Enemy, FName HitBone) const;

	/** An enemy died somewhere in the world. Pays out only if it died close enough. */
	void HandleAnyNPCDeath(AShooterNPC* DeadNPC, TSubclassOf<UDamageType> KillingDamageType, AActor* KillingCauser);

	UFUNCTION()
	void HandleShotFired();

	/** Point the OnShotFired binding at whatever the character is holding now. Safe to call with
	 *  nothing equipped, and a no-op when the weapon has not changed. */
	void RebindWeapon();

	/** The character's threat component, created on demand. Nothing else in the project adds one
	 *  yet, and a passive whose provocation silently does nothing because a component is missing is
	 *  worse than one that brings its own. */
	UThreatComponent* ResolveThreatComponent() const;

	/** Handle for the static death delegate, kept so unequip can take it back off. */
	FDelegateHandle DeathDelegateHandle;

	/** What OnShotFired is currently bound to. Weak: the weapon can be destroyed under us, and a
	 *  raw pointer would then be the only thing keeping a stale address around. */
	TWeakObjectPtr<AShooterWeapon> BoundWeapon;
};
