// RunLevelComponent.h
// One player's run level: his experience and what his level gives. On the player state, because the
// character is the thing that dies and the level is for the whole run.
//
// The ladder is URunLevelDefinition (Project Settings -> Polarity -> Run Level). What the level gives
// is read where it is used, not pushed into the body: the inventory asks for the gadgets per kind,
// the suppression for its resistance. So a body that respawns has it at once, on every machine,
// with nothing to re-apply.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RunLevelComponent.generated.h"

class URunLevelDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRunLevelChanged, int32, NewLevel, int32, NewXP);

UCLASS(ClassGroup = (Polarity), meta = (BlueprintSpawnableComponent))
class POLARITY_API URunLevelComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	URunLevelComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The player's level: from his pawn, his controller or his player state. Null for anyone else. */
	static URunLevelComponent* FindFor(const AActor* Player);

	/** Experience in. Server only. Raises the level as far as it reaches. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Run Level")
	void AddXP(int32 Amount);

	/** Debug and the bench: put the player at Level exactly, experience at its threshold. Server only. */
	void SetLevelForDebug(int32 NewLevel);

	UFUNCTION(BlueprintPure, Category = "Run Level")
	int32 GetXP() const { return XP; }

	UFUNCTION(BlueprintPure, Category = "Run Level")
	int32 GetLevel() const { return Level; }

	/** Experience in all for the next level; -1 at the top or with no ladder. */
	UFUNCTION(BlueprintPure, Category = "Run Level")
	int32 GetXPForNextLevel() const;

	/** Gadgets of one kind this level gives; 0 = nothing, the inventory's own number stands. */
	UFUNCTION(BlueprintPure, Category = "Run Level")
	int32 GetGadgetsPerKind() const;

	/** Suppression ignored at this level, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Run Level")
	float GetSuppressionResistance() const;

	/** Every machine that knows this player, on every change of the experience or the level. */
	UPROPERTY(BlueprintAssignable, Category = "Run Level")
	FOnRunLevelChanged OnLevelChanged;

protected:

	UPROPERTY(ReplicatedUsing = OnRep_Progress)
	int32 XP = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Progress)
	int32 Level = 0;

	UFUNCTION()
	void OnRep_Progress();
};
