// NPCMeleeStrikeComponent.h
// [author, 2026-10-14] What a shooter does when its magazine runs dry in a close fight. A fork:
//   - the enemy is far (over ChargeDistance), or this one is shaken, or the trade does not pay:
//     back to the gun, and reload on the move;
//   - otherwise: close in and strike. One montage per strike. The weapon's holster montage starts
//     with it at full weight and hands over to the strike over HolsterBlendSeconds. The strike drags
//     the body some way at the target, hits in the window the montage marks ("Melee: Damage Window
//     State", the author places it) and shoves. After it the fork is asked again: strike again with
//     the gun still away, or draw it and reload on the move.
// Server only. The montages reach the other machines through AShooterNPC::Multicast_PlayMeleeStrike.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCMeleeStrikeComponent.generated.h"

class AShooterNPC;
class UAnimMontage;

UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class POLARITY_API UNPCMeleeStrikeComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UNPCMeleeStrikeComponent();

	/** Off: the magazine running dry always means backing off to reload, as before. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike")
	bool bEnabled = true;

	/** One is picked at random for each strike. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike")
	TArray<TSoftObjectPtr<UAnimMontage>> StrikeMontages;

	/** The holster montage hands over to the strike over this long (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike", meta = (ClampMin = "0.0", Units = "s"))
	float HolsterBlendSeconds = 0.25f;

	// ---- The fork ----

	/** Farther than this the strike is not worth the run: back off and reload (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Fork", meta = (ClampMin = "0.0", Units = "cm"))
	float ChargeDistance = 700.0f;

	/** More shaken than this it does not go in (UAIAccuracyComponent::GetStress, 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Fork", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxStressToCharge = 0.9f;

	// ---- Getting there ----

	/** The strike starts within this of the target; farther, it runs at him first (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Reach", meta = (ClampMin = "0.0", Units = "cm"))
	float StrikeStartDistance = 300.0f;

	/** Running at him for longer than this: give up and go back to the gun (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Reach", meta = (ClampMin = "0.1", Units = "s"))
	float ChargeTimeout = 2.0f;

	/** How far the strike may drag the body at the target, in all (cm). To be found in play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Reach", meta = (ClampMin = "0.0", Units = "cm"))
	float PullMaxDistance = 200.0f;

	/** How fast it is dragged (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Reach", meta = (ClampMin = "0.0"))
	float PullSpeed = 900.0f;

	/** The drag stops this close to the target (cm, centre to centre). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Reach", meta = (ClampMin = "0.0", Units = "cm"))
	float PullStopDistance = 110.0f;

	// ---- The hit ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Hit", meta = (ClampMin = "0.0"))
	float Damage = 35.0f;

	/** How far the one struck is shoved, and over how long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Hit", meta = (ClampMin = "0.0", Units = "cm"))
	float KnockbackDistance = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Hit", meta = (ClampMin = "0.01", Units = "s"))
	float KnockbackDuration = 0.25f;

	/** The sphere swept in front of the body while the window is open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Hit", meta = (ClampMin = "1.0", Units = "cm"))
	float TraceRadius = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Hit", meta = (ClampMin = "1.0", Units = "cm"))
	float TraceReach = 130.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Hit", meta = (Units = "cm"))
	float TraceHeight = 40.0f;

	/** Where the window is when the montage has no "Melee: Damage Window State": share of its length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Hit", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FallbackWindowStart = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Strike|Hit", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FallbackWindowEnd = 0.6f;

	/** The fork, asked when the magazine runs dry in a close fight and again after each strike. */
	bool WantsToStrike(const AActor* Threat) const;

	/** Go at Threat: run in if it is far, then strike. The gun is assumed in the hands. */
	void Begin(AActor* Threat);

	/** Charging or striking. */
	bool IsBusy() const { return Phase != EPhase::None; }

	/** Just decided to go back to the gun: the reload this starts is not to be forked again. */
	bool JustChoseTheGun() const;

	/** From "Melee: Damage Window State". Authority only, the rest return at once. */
	void ActivateDamageWindow();
	void DeactivateDamageWindow();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	enum class EPhase : uint8 { None, Charging, Striking };

	EPhase Phase = EPhase::None;
	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<UAnimMontage> CurrentStrike;

	/** The gun is away: a strike in a row plays no holster, the way back plays the draw. */
	bool bGunAway = false;

	float ChargeStartTime = 0.0f;
	float PulledSoFar = 0.0f;
	bool bWindowOpen = false;
	bool bHitThisStrike = false;
	float ChoseGunTime = -1000.0f;

	FTimerHandle FallbackOpenTimer;
	FTimerHandle FallbackCloseTimer;
	FTimerHandle StrikeEndTimer;

	AShooterNPC* GetNPC() const;

	void StartStrike();
	void TickCharge();
	void TickPull(float DeltaTime);
	void SweepForHit();

	/** The strike montage started blending out: strike again or go back to the gun. */
	void OnStrikeFinished();

	/** Back to the gun: the gun comes out (if it was away) and the reload starts. */
	void ChooseGun();

	void ClearTimers();

	UAnimMontage* PickStrikeMontage() const;
	static bool HasDamageWindowNotify(const UAnimMontage* Montage);
};
