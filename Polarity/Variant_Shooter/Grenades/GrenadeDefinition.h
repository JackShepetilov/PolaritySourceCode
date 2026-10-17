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
class UStaticMesh;
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

	/** Not a limit any more: grenades left the bag's cells for the gadget section on 2026-10-16, and
	 *  how many are carried is UInventoryComponent's (GetGadgetsPerKind, GetGadgetTotalLimit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Bag", meta = (ClampMin = "1"))
	int32 MaxPerCell = 3;

	/** What a pile of this type looks like on the floor: a pickup dropped to make room for another kind,
	 *  or one put down without a mesh of its own. Empty: an invisible pickup, as before. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Bag")
	TObjectPtr<UStaticMesh> PickupMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Bag")
	FVector PickupMeshScale = FVector::OneVector;

	// ==================== What the enemy makes of it ====================

	/** How close to the grenade an enemy feels it has to get away. Usually the radius where it hurts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|AI", meta = (ClampMin = "0.0", Units = "cm"))
	float DangerRadius = 650.0f;

	/** An enemy this close to where it bounces hears it land, whatever it was looking at (cm). Small on
	 *  purpose: at nine metres every grenade was heard [author, 2026-10-15]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|AI", meta = (ClampMin = "0.0", Units = "cm"))
	float BounceHearRadius = 400.0f;

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

	// ==================== Working on after it goes off ====================
	// A grenade that does not just go off but keeps working where it lies for a while (the
	// microwave). Zero ActiveSeconds: an ordinary grenade, gone with the bang.

	/** Seconds it keeps working after going off. 0: it is spent the moment it goes off. */
	virtual float GetActiveSeconds() const { return 0.0f; }

	/** While working: Pulse every this many seconds. */
	virtual float GetPulseInterval() const { return 0.5f; }

	/** While working, every GetPulseInterval. Authority. */
	virtual void Pulse(AGrenadeProjectile* Grenade, const FVector& Origin) const {}

	/** While working, how far from it an enemy wants to be. */
	virtual float GetWorkingDangerRadius() const { return DangerRadius; }

	/** While working, how far it reaches at all: the size its effects are drawn at (User.Radius). */
	virtual float GetWorkingEffectRadius() const { return GetWorkingDangerRadius(); }

	/** A wall between it and a soldier protects him: he may hide behind one instead of running out. */
	virtual bool DoWallsProtect() const { return true; }

	/** While working, the enemies backing out of its reach shoot at it. */
	virtual bool IsShotAtWhileWorking() const { return false; }

	/** While working, the chance a round aimed at it lands, whoever fires it. Below 0: the shooter's
	 *  own aim decides. @see IAimTargetInterface::GetAimHitChanceOverride */
	virtual float GetWorkingHitChance() const { return -1.0f; }

	/** While working, this many hits from the enemy's side break it. 0: nothing breaks it. */
	virtual int32 GetHitsToBreak() const { return 0; }

	/** Looping on it while it works, on every machine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Presentation")
	TObjectPtr<USoundBase> WorkingLoopSound;

	/** Played at every pulse while it works, on every machine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Presentation")
	TObjectPtr<UNiagaraSystem> PulseFX;

	/** Played when it stops working, on every machine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Presentation")
	TObjectPtr<USoundBase> WorkingEndSound;
};
