// GrenadeDefinition_Frag.h
// The frag: fragments and a blast that puts the survivors on the ground.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "GrenadeDefinition_Frag.generated.h"

class UDamageType;

/**
 * Damage that falls off with distance and stops at walls (the engine's radial damage on the
 * Visibility channel), of the fragmentation type: part of it goes past body armour straight to health,
 * the rest into the armour [author, 2026-10-14]. @see UDamageType_Fragmentation
 *
 * Every enemy foot soldier it does not kill inside KnockdownRadius goes down and gets up again
 * (AShooterNPC::ApplyBlastKnockdown). Players take the damage and the shake, not the fall.
 */
UCLASS(BlueprintType)
class POLARITY_API UGrenadeDefinition_Frag : public UGrenadeDefinition
{
	GENERATED_BODY()

public:

	UGrenadeDefinition_Frag();

	/** Damage inside InnerRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frag", meta = (ClampMin = "0.0"))
	float BaseDamage = 120.0f;

	/** Damage at OuterRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frag", meta = (ClampMin = "0.0"))
	float MinimumDamage = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frag", meta = (ClampMin = "0.0", Units = "cm"))
	float InnerRadius = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frag", meta = (ClampMin = "0.0", Units = "cm"))
	float OuterRadius = 650.0f;

	/** 1 is a straight line from BaseDamage to MinimumDamage between the radii. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frag", meta = (ClampMin = "0.0"))
	float DamageFalloff = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frag")
	TSubclassOf<UDamageType> DamageType;

	/** Survivors inside this, with a clear line to the blast, go down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frag", meta = (ClampMin = "0.0", Units = "cm"))
	float KnockdownRadius = 450.0f;

	/** Players are hurt too: the thrower and his team as much as anybody. Off = only enemies.
	 *  [?] author to confirm, Docs/Grenades_Plan_2026-10-14.md §6. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frag")
	bool bHurtsPlayers = true;

	virtual void Detonate(AGrenadeProjectile* Grenade, const FVector& Origin) const override;
};
