// ShoveComponent.h
// [author, 2026-10-14] Being shoved by a melee hit, one logic for players and NPCs.
//
//   - Hit from the front half (the attacker in front of the body): the body is turned square to the
//     attacker and plays a front reaction (F). From the back half: turned with its back to him, a
//     back reaction (B).
//   - The reaction montage's root motion is the shape of the push, and a root motion source carries
//     it, stretched to the distance and the time asked for. The montage's own root motion is not
//     applied while it plays: in the engine animation root motion overrides every root motion source
//     (UCharacterMovementComponent::ApplyRootMotionToVelocity), so the two cannot simply be added.
//   - A wall in the way: the hit on the wall does damage, the push ends, and the opposite reaction
//     plays (F becomes B) with a push of its own root motion's shape and length off the wall.
//   - Off the ground at any moment (shoved off a ledge, already in the air): the montage is cut and
//     the body falls with the speed it has. No hanging in the air.
//
// Players: the camera is never turned, so their body is not either; the push goes along the hit all
// the same. NPCs are turned square to it.
// Server decides; the montage reaches everybody by multicast, the push reaches the owning client of
// a player by a client call so its own prediction moves the same way.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShoveComponent.generated.h"

class ACharacter;
class AController;
class UAnimMontage;
class UCurveFloat;

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class POLARITY_API UShoveComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UShoveComponent();

	/** Played when hit from the front half; one at random. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shove")
	TArray<TSoftObjectPtr<UAnimMontage>> FrontReactions;

	/** Played when hit from the back half; one at random. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shove")
	TArray<TSoftObjectPtr<UAnimMontage>> BackReactions;

	/** Damage taken when the shove drives the body into a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shove", meta = (ClampMin = "0.0"))
	float WallSlamDamage = 15.0f;

	/** How far ahead along the push a wall is looked for (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shove", meta = (ClampMin = "1.0", Units = "cm"))
	float WallProbeDistance = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shove", meta = (ClampMin = "0.0", Units = "s"))
	float ReactionBlendIn = 0.08f;

	/** The reaction is cut over this long when the body leaves the ground (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shove", meta = (ClampMin = "0.0", Units = "s"))
	float AirborneBlendOut = 0.12f;

	/** Shove Victim Distance cm away from AttackerLocation over Duration s. Server only. False when the
	 *  victim has no shove component (not a character of ours), so the caller can do it its old way. */
	static bool TryShove(AActor* Victim, const FVector& AttackerLocation, float Distance, float Duration, AController* Instigator);

	bool ShoveOnAuthority(const FVector& AttackerLocation, float Distance, float Duration, AController* Instigator);

	/** A shove is playing out on this body. */
	bool IsShoved() const { return bActive; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayReaction(UAnimMontage* Montage, float Yaw, bool bTurn, float BlendIn);

	/** The shove is over: root motion back as it was; the reaction cut too when bCut. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_EndReaction(bool bCut, float BlendOut);

	/** The push, on the machine that predicts a player's movement. Shape: the montage's root motion. */
	UFUNCTION(Client, Reliable)
	void Client_StartPush(FVector Velocity, float Duration, UAnimMontage* Shape);

	UFUNCTION(Client, Reliable)
	void Client_StopPush();

private:

	bool bActive = false;
	bool bFromFront = true;
	bool bWallHandled = false;
	bool bIsPlayer = false;
	FVector Direction = FVector::ZeroVector;
	float StartTime = 0.0f;
	float EndTime = 0.0f;
	float PushEndTime = 0.0f;
	float FacingYaw = 0.0f;
	uint16 PushID = 0;
	TWeakObjectPtr<AController> InstigatorController;

	ACharacter* GetCharacter() const;
	UAnimMontage* PickReaction(bool bFront) const;

	/** How far the montage's root moves over its whole length (cm). */
	static float RootTravel(const UAnimMontage* Montage);

	void PlayReactionLocal(UAnimMontage* Montage, float Yaw, bool bTurn, float BlendIn);
	void EndReactionLocal(bool bCut, float BlendOut);

	/** Push so that the body covers Distance along Dir over Duration, shaped like Shape's root motion.
	 *  Returns the peak velocity it was given (for the client call). */
	FVector StartPush(const FVector& Dir, float Distance, float Duration, UAnimMontage* Shape);
	void StartPushLocal(const FVector& Velocity, float Duration, UAnimMontage* Shape);
	void StopPushLocal();

	/** Root motion mode before the shove switched extraction off, to put back. */
	uint8 SavedRootMotionMode = 0;
	bool bRootMotionSaved = false;

	/** The wall in the way, if there is one right ahead along the push. */
	bool FindWallAhead(FHitResult& OutHit) const;

	void OnWallSlam(const FHitResult& Wall);
	void Finish(bool bCutMontage, float BlendOut);

	/** Strength over the push's time, peak 1: the montage's root speed when it has root motion, a
	 *  linear fade from 1 to 0 when it has none. OutAverage: the mean strength over the push. */
	static UCurveFloat* GetPushShape(const UAnimMontage* Montage, float& OutAverage);
};
