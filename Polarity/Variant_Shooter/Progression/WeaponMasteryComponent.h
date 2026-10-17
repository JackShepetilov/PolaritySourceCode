// WeaponMasteryComponent.h
// One player's mastery of every weapon he has fought with this run. On the player state, like the
// run level: a gun can be sold and a body can die, the skill stays with the player.
//
// Experience comes from hits and kills with the gun in hand, decided on the server
// (AShooterNPC::ReportDamageNumber). The list replicates to everyone: what a level gives also sets
// the play rate of the swap and reload that the other players see.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponMasteryComponent.generated.h"

class UWeaponMasteryDefinition;

/** Progress with one mastery asset. */
USTRUCT(BlueprintType)
struct FWeaponMasteryProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Mastery")
	TObjectPtr<UWeaponMasteryDefinition> Definition;

	UPROPERTY(BlueprintReadOnly, Category = "Mastery")
	int32 XP = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Mastery")
	int32 Level = 0;
};

UCLASS(ClassGroup = (Polarity), meta = (BlueprintSpawnableComponent))
class POLARITY_API UWeaponMasteryComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UWeaponMasteryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The player's mastery: from his pawn, his controller or his player state. */
	static UWeaponMasteryComponent* FindFor(const AActor* Player);

	/** Experience with this mastery. Server only. */
	void AddXP(const UWeaponMasteryDefinition* Definition, int32 Amount);

	/** Debug and the bench: this mastery at Level exactly, experience at its threshold. Server only. */
	void SetLevelForDebug(const UWeaponMasteryDefinition* Definition, int32 NewLevel);

	UFUNCTION(BlueprintPure, Category = "Mastery")
	int32 GetLevel(const UWeaponMasteryDefinition* Definition) const;

	UFUNCTION(BlueprintPure, Category = "Mastery")
	int32 GetXP(const UWeaponMasteryDefinition* Definition) const;

	UFUNCTION(BlueprintPure, Category = "Mastery")
	const TArray<FWeaponMasteryProgress>& GetProgress() const { return Progress; }

protected:

	UPROPERTY(Replicated)
	TArray<FWeaponMasteryProgress> Progress;
};
