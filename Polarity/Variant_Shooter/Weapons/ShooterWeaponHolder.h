// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Variant_Shooter/Feedback/HitFeedbackSet.h"
#include "ShooterWeaponHolder.generated.h"

class AShooterWeapon;
class UAnimMontage;


// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UShooterWeaponHolder : public UInterface
{
	GENERATED_BODY()
};

/**
 *  Common interface for Shooter Game weapon holder classes
 */
class POLARITY_API IShooterWeaponHolder
{
	GENERATED_BODY()

public:

	/** Attaches a weapon's meshes to the owner */
	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) = 0;

	/** Plays the firing montage for the weapon */
	virtual void PlayFiringMontage(UAnimMontage* Montage) = 0;

	/** Plays the reload montage for the weapon. Separate from the firing one because the two are
	 *  seen by different people: a firing montage plays every shot next to muzzle flash and sound
	 *  that already carry, while a reload is a rare, long animation the OTHER players have to see
	 *  to read what this one is doing. Holders with no reload animation of their own fall back to
	 *  the firing path, which is where this used to go. */
	virtual void PlayReloadMontage(UAnimMontage* Montage) { PlayFiringMontage(Montage); }

	/** Applies weapon recoil to the owner */
	virtual void AddWeaponRecoil(float Recoil) = 0;

	/** Updates the weapon's HUD with the current ammo count */
	virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize) = 0;

	/** Calculates and returns the aim location for the weapon */
	virtual FVector GetWeaponTargetLocation() = 0;

	/** WHO this holder is shooting at, when that is a knowable thing. Null by default, and null for
	 *  a player, who aims at a direction rather than at a person.
	 *
	 *  Distinct from GetWeaponTargetLocation on purpose, because the two answer different questions.
	 *  That one returns an aim POINT: an AI runs its accuracy spread and then traces out to AimRange,
	 *  so what comes back is wherever the ray ended up - a wall behind the enemy, or empty space
	 *  thousands of units past them, since pawns do not block the visibility channel here. Perfect
	 *  for a hitscan, which only needs a direction to fire along and stops at the first thing it
	 *  meets.
	 *
	 *  A ballistic solver cannot use it. Asked to arc onto a point ten thousand units away it
	 *  produces the launch angle that reaches ten thousand units, and the shell sails over the enemy
	 *  and lands in the distance - which is what a grenadier firing into the sky actually was. It
	 *  needs the position of the BODY it is trying to hit. */
	virtual AActor* GetWeaponAimActor() const { return nullptr; }

	/** Gives a weapon of this class to the owner */
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) = 0;

	/** Activates the passed weapon */
	virtual void OnWeaponActivated(AShooterWeapon* Weapon) = 0;

	/** Deactivates the passed weapon */
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) = 0;

	/** Notifies the owner that the weapon cooldown has expired and it's ready to shoot again */
	virtual void OnSemiWeaponRefire() = 0;

	/**
	 * Notifies the owner that a hit was registered
	 * @param HitLocation World location of the hit
	 * @param HitDirection Direction from weapon to hit
	 * @param Damage Amount of damage dealt
	 * @param bHeadshot True if this was a headshot
	 * @param bKilled True if target was killed
	 * @param HitActor The actor that was hit (can be nullptr for backwards compatibility)
	 */
	virtual void OnWeaponHit(const FVector& HitLocation, const FVector& HitDirection, float Damage, bool bHeadshot, bool bKilled, AActor* HitActor = nullptr)
	{
		// Default empty implementation for backwards compatibility
	}

	/**
	 * The one door hit feedback goes through.
	 *
	 * Everything OnWeaponHit carries plus the part it could never say: whether the shield was still
	 * holding, whether this shot is the one that broke it, whether the hit wrote any health at all,
	 * and which feedback set the firing weapon speaks with. Callers that have that context should
	 * use this; the older call still works and simply arrives with the extra fields left at their
	 * defaults.
	 */
	virtual void OnWeaponHitFeedback(const FHitFeedbackContext& Context)
	{
		// Anything that has not overridden this keeps the behaviour it had before the context
		// existed, rather than falling silent.
		OnWeaponHit(Context.HitLocation, Context.HitDirection, Context.Damage,
			Context.bHeadshot, Context.bKilled, Context.HitActor);
	}
};
