// HelmetComponent.h
// A helmet: what a hit in the head does to an enemy that wears one.
//
// Docs/Damage_Model_Rework_Plan_2026-10-03.md §8. The head is not under the body armour: a hit in
// the head is an ordinary hit and goes straight to health. The helmet is NOT a second health bar and
// absorbs nothing. It only counts the damage the head has taken, and that count has two marks on it
// [author, 2026-10-12]:
//   KnockdownThreshold  the enemy goes down for a couple of seconds. Once per life.
//   HelmetPool          the helmet comes off. No knockdown. From then on any bullet in the head kills.
//
// Hung on whichever enemy blueprints the author wants to wear one. How the helmet coming off LOOKS
// differs per mesh, so this component only says that it happened (OnHelmetKnockedOff, every machine)
// and plays one effect at the head; the blueprint does the rest.
//
// The NPC damage funnel asks it before armour and health (AShooterNPC::TakeDamage). Server decides;
// the helmet's state replicates.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HelmetComponent.generated.h"

class UAnimMontage;
class UHelmetComponent;
class UNiagaraSystem;
class USoundAttenuation;
class USoundBase;
struct FDamageEvent;

/** What one hit in the head did. */
struct FHelmetOutcome
{
	/** The hit was in the head of an enemy with this component: it goes to health past the armour. */
	bool bHandled = false;
	/** This hit crossed the knockdown mark. */
	bool bKnockdown = false;
	/** This hit took the helmet off. */
	bool bHelmetOff = false;
	/** The helmet was already off and this was a bullet: the enemy dies. */
	bool bLethal = false;
};

/** The helmet came off. Every machine (clients through the replicated flag, with no instigator). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHelmetKnockedOff, UHelmetComponent*, Helmet, AActor*, Instigator);

/** A hit in the head put the owner on the ground. Server. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHelmetKnockdown, UHelmetComponent*, Helmet, AActor*, Instigator);

UCLASS(ClassGroup = (Damage), meta = (BlueprintSpawnableComponent))
class POLARITY_API UHelmetComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UHelmetComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The helmet on this actor, or null when it wears none. */
	static UHelmetComponent* Find(const AActor* Actor);

	/** Bodies of the physics asset that count as the head, matched exactly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helmet")
	TArray<FName> HeadBones;

	/** Head damage that puts the owner on the ground. 0 = never. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helmet", meta = (ClampMin = "0.0"))
	float KnockdownThreshold = 50.0f;

	/** Head damage that takes the helmet off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helmet", meta = (ClampMin = "1.0"))
	float HelmetPool = 100.0f;

	/** How long the owner is out when it has no fall to play (no montage below, no room to fall, in
	 *  the air): it staggers on the spot with its own stun montage. A fall lasts as long as its
	 *  montage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helmet", meta = (ClampMin = "0.1", Units = "s"))
	float KnockdownDuration = 2.0f;

	/** The fall when the hit came from the front: the owner is turned to face the hit exactly and
	 *  goes over backwards, away from it. The montage's root motion carries the body; the owner
	 *  shortens it, or turns the fall aside, when there is no room (AShooterNPC::ApplyHeadKnockdown). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helmet|Knockdown")
	TObjectPtr<UAnimMontage> KnockdownMontageFront;

	/** The fall when the hit came from behind. The owner is not turned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helmet|Knockdown")
	TObjectPtr<UAnimMontage> KnockdownMontageBack;

	/** How far past the capsule the lying body reaches along the way it fell (cm), front fall: the
	 *  room that has to be clear beyond where the root motion stops, or the head ends up in a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helmet|Knockdown", meta = (ClampMin = "0.0"))
	float KnockdownReachFront = 75.0f;

	/** The same for the fall from a hit behind. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helmet|Knockdown", meta = (ClampMin = "0.0"))
	float KnockdownReachBack = 55.0f;

	/** With the helmet off, a bullet in the head kills. Off = the head is then an ordinary hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helmet")
	bool bLethalWithoutHelmet = true;

	/** Played at the head when the helmet comes off (every machine). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helmet|Presentation")
	TObjectPtr<UNiagaraSystem> HelmetOffEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helmet|Presentation")
	TObjectPtr<USoundBase> HelmetOffSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helmet|Presentation")
	TObjectPtr<USoundAttenuation> HelmetOffSoundAttenuation;

	/** The helmet came off: hide the helmet mesh, swap the head, whatever this body needs. */
	UPROPERTY(BlueprintAssignable, Category = "Helmet")
	FOnHelmetKnockedOff OnHelmetKnockedOff;

	UPROPERTY(BlueprintAssignable, Category = "Helmet")
	FOnHelmetKnockdown OnHelmetKnockdown;

	UFUNCTION(BlueprintPure, Category = "Helmet")
	bool IsHelmetOn() const { return !bHelmetOff; }

	UFUNCTION(BlueprintPure, Category = "Helmet")
	bool IsHeadBone(FName BoneName) const;

	/** What is left of the helmet, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Helmet")
	float GetHelmetFraction() const;

	UFUNCTION(BlueprintPure, Category = "Helmet")
	float GetHeadDamageTaken() const { return HeadDamageTaken; }

	/** Server: sort one hit out. Not handled = not a head hit, the funnel goes on as before. */
	FHelmetOutcome ProcessHit(float Damage, const FDamageEvent& DamageEvent, AActor* Attacker);

	/** Helmet back on, nothing spent. For the NPC pool's recycle. Server. */
	void ResetForReuse();

private:

	/** Head damage taken so far, capped at HelmetPool. */
	UPROPERTY(Replicated)
	float HeadDamageTaken = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_HelmetOff)
	bool bHelmetOff = false;

	UFUNCTION()
	void OnRep_HelmetOff();

	/** The knockdown is spent for this life. */
	bool bKnockdownSpent = false;

	/** What OnRep last saw, so a client presents each loss once. */
	bool bSeenHelmetOff = false;

	/** The effect, the sound and the event. Every machine. */
	void PresentHelmetOff(AActor* Instigator);
};
