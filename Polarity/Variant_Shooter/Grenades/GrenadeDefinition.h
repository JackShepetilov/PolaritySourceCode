// GrenadeDefinition.h
// One type of grenade: how it flies, how many fit in a cell, and what it does where it goes off.
//
// Docs/Grenades_Plan_2026-10-14.md. A grenade is an ITEM [author, 2026-10-14]: it lies in the bag a
// few to a cell (EInventorySlotKind::Grenade, Payload = this asset) and is thrown on its own key
// (UGrenadeComponent). It is built on the throw archetype so the wind-up, the held arc and the
// release are the very ones the thrown abilities use; there is no cooldown, the bag is the limit.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Abilities/AbilityDefinition_Throw.h"
#include "GrenadeDefinition.generated.h"

class AGrenadeProjectile;
class UNiagaraSystem;
class USoundBase;
class USoundAttenuation;

UCLASS(BlueprintType, Abstract)
class POLARITY_API UGrenadeDefinition : public UAbilityDefinition_Throw
{
	GENERATED_BODY()

public:

	UGrenadeDefinition();

	// ==================== Flight ====================

	/** The grenade in flight. A Blueprint child of AGrenadeProjectile in practice, for the mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Flight")
	TSubclassOf<AGrenadeProjectile> ProjectileClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Flight", meta = (ClampMin = "100.0", Units = "cm/s"))
	float ThrowSpeed = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Flight", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float GravityScale = 1.0f;

	/** Seconds from leaving the hand to going off. Counted from the throw, not from landing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Flight", meta = (ClampMin = "0.1", Units = "s"))
	float FuseTime = 3.0f;

	virtual float GetThrowSpeed() const override { return ThrowSpeed; }
	virtual float GetThrowGravityScale() const override { return GravityScale; }

	// ==================== Bag ====================

	/** How many of this type fit in one cell of the bag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Bag", meta = (ClampMin = "1"))
	int32 MaxPerCell = 3;

	// ==================== What the enemy makes of it ====================

	/** How close to the grenade an enemy feels it has to get away. Usually the radius where it hurts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|AI", meta = (ClampMin = "0.0", Units = "cm"))
	float DangerRadius = 650.0f;

	// ==================== Going off ====================

	/** Played where it goes off, on every machine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Presentation")
	TObjectPtr<UNiagaraSystem> DetonationFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Presentation")
	TObjectPtr<USoundBase> DetonationSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Presentation")
	TObjectPtr<USoundAttenuation> DetonationAttenuation;

	/** Played on every bounce, on every machine. The AI hears it too (AShooterNPC::NotifyGrenadeThreat). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Presentation")
	TObjectPtr<USoundBase> BounceSound;

	/** What it does where it went off. Authority only; the picture and the sound are the projectile's. */
	virtual void Detonate(AGrenadeProjectile* Grenade, const FVector& Origin) const {}
};
