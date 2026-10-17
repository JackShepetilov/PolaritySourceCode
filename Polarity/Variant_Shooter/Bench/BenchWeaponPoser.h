// BenchWeaponPoser.h
// A soldier on the bench who shows every third person animation of a gun, round and round
// [author, 2026-10-17]: idle, single shots, a burst, the reload with rounds left, the empty reload,
// put away, take out. Then the next gun of Weapons. For checking how a gun looks in other players'
// hands (the Infima doubles of the first person meshes, Docs/Infima_TP_Transfer_Procedure_2026-09-29.md).
//
// The soldier is an ordinary enemy (NPCClass) with no brain: no controller, no damage. It fires
// straight ahead, so point the poser at a wall. Server only, like the rest of the bench.
// A player's shot at the soldier skips to the next gun [author, 2026-10-17]. The bench fills Weapons
// with the guns that have a third person double (Source/Tools/TPDouble/jobs.json).
// TODO(COOP): put away / take out play on the server's copy only; the rest goes through the gun's
// own replicated effects.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BenchWeaponPoser.generated.h"

class AShooterNPC;
class AShooterWeapon;
class UDamageType;
class UTextRenderComponent;

UENUM()
enum class EBenchPoseStep : uint8
{
	Idle,
	SingleShots,
	Burst,
	ReloadRoundsLeft,
	ReloadEmpty,
	Holster,
	Draw,
	Count
};

UCLASS()
class POLARITY_API ABenchWeaponPoser : public AActor
{
	GENERATED_BODY()

public:

	ABenchWeaponPoser();

	/** The soldier. An enemy blueprint with a third person body (BP_ShooterNPC). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poser")
	TSubclassOf<AShooterNPC> NPCClass;

	/** Shown one after another, a whole round each. Empty: the soldier's own gun only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poser")
	TArray<TSubclassOf<AShooterWeapon>> Weapons;

	/** Seconds standing between the steps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poser", meta = (ClampMin = "0.1", Units = "s"))
	float Pause = 1.0f;

	/** How long the burst holds the trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poser", meta = (ClampMin = "0.1", Units = "s"))
	float BurstSeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poser", meta = (ClampMin = "1"))
	int32 SingleShots = 3;

	/** After a shot has moved to the next gun, shots are ignored this long: a burst or a shotgun's
	 *  pellets would otherwise skip several guns at once. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poser", meta = (ClampMin = "0", Units = "s"))
	float SkipCooldown = 0.75f;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Poser")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Poser")
	TObjectPtr<UTextRenderComponent> Label;

private:

	/** The soldier with gun number WeaponIndex. */
	void SpawnSoldier();

	/** Start Step, and arm the timer for the next one by how long it takes. */
	void RunStep();
	void NextStep();

	/** For the single shots and the burst of a gun that is not full auto. */
	void PullTrigger();
	void ReleaseTrigger();

	float PlayOnBody(class UAnimMontage* Montage) const;
	void ShowLabel(const FString& Text);

	/** The soldier was shot (AShooterNPC::OnDamageTaken, with no damage: it cannot be hurt). A player's
	 *  shot moves on to the next gun; the soldier's own and other enemies' do nothing. */
	UFUNCTION()
	void OnSoldierShot(AShooterNPC* DamagedNPC, float Damage, TSubclassOf<UDamageType> DamageType,
		FVector HitLocation, AActor* DamageCauser);

	/** Drop the current round and start the next gun from its first step. */
	void SkipToNextGun();

	/** World time of the last skip, for SkipCooldown. */
	double LastSkipTime = -1.0e9;

	UPROPERTY(Transient)
	TObjectPtr<AShooterNPC> Soldier;

	int32 WeaponIndex = 0;
	EBenchPoseStep Step = EBenchPoseStep::Idle;
	int32 ShotsLeft = 0;

	FTimerHandle StepTimer;
	FTimerHandle TriggerTimer;
	FTimerHandle BurstEndTimer;
};
