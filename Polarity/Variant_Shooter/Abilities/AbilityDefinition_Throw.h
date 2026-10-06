// AbilityDefinition_Throw.h
// Definition for the "throw" archetype: tap throws at once, hold winds up and aims with a drawn arc.
//
// What is thrown is the subclass's business (a smoke canister, a heal grenade, a nitro gate). This
// base owns what every throw shares: the three montages, the moment the hand lets go, and the one
// answer to "where does it leave from and how fast" that both the preview and the real throw read.

#pragma once

#include "CoreMinimal.h"
#include "AbilityDefinition.h"
#include "AbilityDefinition_Throw.generated.h"

class UAnimMontage;
class USoundBase;
class AShooterCharacter;
struct FHitResult;

UCLASS(BlueprintType, Abstract)
class POLARITY_API UAbilityDefinition_Throw : public UAbilityDefinition
{
	GENERATED_BODY()

public:

	// ==================== Animation ====================
	// Three montages rather than one, because a held throw has a middle that lasts as long as the
	// player wants it to. A tap plays Start straight into Throw and never sees the loop.

	/** Wind-up: the hand comes back. Played on the press, whatever the press turns out to be. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Animation")
	TObjectPtr<UAnimMontage> AimStartMontage;

	/** Held pose, replayed for as long as the button stays down after the wind-up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Animation")
	TObjectPtr<UAnimMontage> AimLoopMontage;

	/** The throw itself. The payload leaves ThrowReleaseTime into it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Animation")
	TObjectPtr<UAnimMontage> ThrowMontage;

	/** Seconds into ThrowMontage (at its own play rate) at which the payload leaves the hand.
	 *  A number rather than an AnimNotify so a pack montage can be used as it ships. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Animation", meta = (ClampMin = "0.0", Units = "s"))
	float ThrowReleaseTime = 0.15f;

	/** Play rate for whatever is left of the wind-up once the button has come back up. This is what
	 *  makes a tap quick: the wind-up is not skipped, it is hurried. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Animation", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float QuickStartPlayRate = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Animation", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float ThrowPlayRate = 1.0f;

	/** Crossfade between consecutive montages, same meaning as UAbilityHandler_Burst::MontageOverlap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Animation", meta = (ClampMin = "0.0", ClampMax = "1.0", Units = "s"))
	float MontageOverlap = 0.15f;

	// ==================== Aim ====================

	/** How long the button has to stay down before the arc is drawn. Below this a press is a tap,
	 *  and a tap that flashed an arc for two frames would read as a glitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Aim", meta = (ClampMin = "0.0", Units = "s"))
	float AimPreviewDelay = 0.25f;

	/** Where the payload starts, in view space: X forward, Y right, Z up from the eyes. Authored to
	 *  sit where the hand holds the grenade in the aim pose, so the arc is seen to leave the hand.
	 *
	 *  A fixed offset rather than the hand socket on purpose. The hand rotates through the throw, so
	 *  a start read off it at the release frame is not the start the preview drew; and the server has
	 *  no trustworthy first-person pose for a remote player at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Aim")
	FVector ThrowOriginOffset = FVector(25.0f, 28.0f, -14.0f);

	/** The throw is aimed at the point this far down the crosshair, so an arc that starts off to
	 *  one side still crosses what the player is looking at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Aim", meta = (ClampMin = "100.0", Units = "cm"))
	float AimConvergeDistance = 3000.0f;

	/** Added to the aim pitch, so looking level still lobs. Zero throws exactly along the aim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Aim", meta = (ClampMin = "-45.0", ClampMax = "45.0", Units = "deg"))
	float ThrowPitchOffset = 0.0f;

	/** Radius the preview sweeps with. Match the payload's collision sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Aim", meta = (ClampMin = "0.0", Units = "cm"))
	float PreviewRadius = 16.0f;

	/** How far ahead in time the preview simulates before giving up on finding a landing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Aim", meta = (ClampMin = "0.5", ClampMax = "10.0", Units = "s"))
	float PreviewMaxTime = 4.0f;

	// ==================== Cost ====================
	// Used by the base GetCommonStatsAtLevel. An ability with its own per-level table (the nitro gate)
	// overrides that and these two are then ignored.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Cost", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "s"))
	float ThrowCooldown = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Cost", meta = (ClampMin = "0.0"))
	float ThrowMinimumCharge = 0.0f;

	// ==================== Audio ====================

	/** Played where the caster stands at the moment the payload leaves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw|Audio")
	TObjectPtr<USoundBase> ThrowSound;

	// ==================== Flight (answered by the subclass) ====================
	// Getters rather than fields: each payload already keeps its own speed where its other numbers
	// live, and a second copy here would be a second number to get wrong.

	virtual float GetThrowSpeed() const { return 2000.0f; }
	virtual float GetThrowGravityScale() const { return 1.0f; }

	// ==================== Base overrides ====================

	virtual FAbilityCommonStats GetCommonStatsAtLevel(int32 Level) const override;

	// ==================== Shared by the preview and the throw ====================

	/** Where the payload starts and which way it faces, for Caster as they are aiming right now.
	 *  The preview and the real throw both call this, which is the whole reason it exists. */
	void GetThrowStart(const AShooterCharacter* Caster, FVector& OutLocation, FRotator& OutRotation) const;

	/** Simulate the arc from GetThrowStart. Returns true when it ends on something. */
	bool PredictThrowPath(const AShooterCharacter* Caster, TArray<FVector>& OutPoints, FHitResult& OutHit) const;
};
