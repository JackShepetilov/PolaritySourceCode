// GrenadeDefinition_Flash.h
// The flashbang: a short fuse, and everybody who sees it is out for a few seconds.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "GrenadeDefinition_Flash.generated.h"

class USoundMix;
class UMaterialInterface;

/**
 * Blinds everybody with a clear line from the flash to their eyes: enemies, the team and the thrower
 * [author, 2026-10-14]. How hard depends on two things, multiplied: how close (full inside
 * FullEffectRadius, nothing at MaxRadius) and whether they were looking at it (full inside
 * FacingFullAngle of their view, BehindFactor with their back to it).
 *
 * An enemy foot soldier is stunned on the spot (AShooterNPC::ApplyFlashbang: the start, loop, end
 * montage) and loses its target. A drone, a turret or a tank shoots blind instead. A player who
 * looked at it gets a burn on the eye; one close by is deafened and suppressed whatever he looked at
 * (UGrenadeComponent::Client_Flashbang). He keeps his controls.
 */
UCLASS(BlueprintType)
class POLARITY_API UGrenadeDefinition_Flash : public UGrenadeDefinition
{
	GENERATED_BODY()

public:

	UGrenadeDefinition_Flash();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash", meta = (ClampMin = "0.0", Units = "cm"))
	float FullEffectRadius = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxRadius = 1500.0f;

	/** Inside this angle of where one looks, the flash counts in full. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float FacingFullAngle = 60.0f;

	/** What is left of it with one's back turned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BehindFactor = 0.35f;

	/** Below this, nothing happens at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinExposure = 0.15f;

	/** An enemy's stun at full exposure; it scales down with exposure to MinStunDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Enemy", meta = (ClampMin = "0.0", Units = "s"))
	float MaxStunDuration = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Enemy", meta = (ClampMin = "0.0", Units = "s"))
	float MinStunDuration = 1.0f;

	/** What a drone, a turret or a tank's spread is multiplied by while it is blind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Enemy", meta = (ClampMin = "1.0"))
	float BlindSpreadMultiplier = 4.0f;

	// ==================== Player: the eyes ====================
	// A player is burnt only if he LOOKED at it [author, 2026-10-15]: a clear line to his eyes and the
	// flash inside PlayerLookAngle of where he was looking. What he gets is a burn on the eye where the
	// flash was on his screen, as after looking at the sun: bigger and brighter the closer it was.

	/** Half-angle from the middle of the view within which the flash counts as looked at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Player Eyes", meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
	float PlayerLookAngle = 50.0f;

	/** How long the burn lasts at full strength; a faint one lasts down to PlayerBurnMinTime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Player Eyes", meta = (ClampMin = "0.0", Units = "s"))
	float PlayerBurnMaxTime = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Player Eyes", meta = (ClampMin = "0.0", Units = "s"))
	float PlayerBurnMinTime = 1.0f;

	/** Size of the burnt spot in screen heights, at full strength and at the faintest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Player Eyes", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float PlayerBurnMaxSize = 0.32f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Player Eyes", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float PlayerBurnMinSize = 0.08f;

	/** The post-process material that draws the burn. Parameters: Center (UV), Size, Strength, Age (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Player Eyes")
	TObjectPtr<UMaterialInterface> PlayerBurnMaterial;

	// ==================== Player: the ears ====================
	// Close by, the bang alone is enough, wherever one looks: the world goes muffled, the ears ring
	// and the player is suppressed (UPlayerSuppressionComponent), strongest at the grenade.

	/** Inside this the bang deafens. Nothing at the edge, full at the grenade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Player Ears", meta = (ClampMin = "0.0", Units = "cm"))
	float PlayerLoudRadius = 900.0f;

	/** What is left of the bang through a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Player Ears", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallMuffling = 0.5f;

	/** Suppression added at full loudness (1 = the whole bar). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Player Ears", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlayerSuppressionAtFull = 1.0f;

	/** How long the world stays muffled at full loudness, scaled down with it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Player Ears", meta = (ClampMin = "0.0", Units = "s"))
	float PlayerDeafMaxTime = 5.0f;

	/** Pushed for as long as the deafness lasts: the world's sound classes down and low-passed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Player Ears")
	TObjectPtr<USoundMix> PlayerDeafMix;

	/** The ringing in a deafened player's ears, 2D, only for him, louder the louder the bang. Keep it
	 *  out of the classes PlayerDeafMix turns down (the UI class), or it is muffled with the rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flash|Player Ears")
	TObjectPtr<USoundBase> PlayerRingSound;

	virtual void Detonate(AGrenadeProjectile* Grenade, const FVector& Origin) const override;

	/** 0..1: how hard the flash at Origin hits one who looks from Eyes along ViewDirection. Line of
	 *  sight is the caller's business. */
	float ComputeExposure(const FVector& Origin, const FVector& Eyes, const FVector& ViewDirection) const;
};
