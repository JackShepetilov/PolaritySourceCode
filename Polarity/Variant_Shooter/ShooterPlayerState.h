// ShooterPlayerState.h
// What a player owns that is not the body they are wearing.
//
// Metal lives here and not on the character because the character is the thing that dies. A player
// who goes down mid-wave keeps what they collected, the same as a TF2 engineer who respawns with
// whatever the round left them. PlayerState is also the one per-player actor every machine sees,
// which is what lets a teammate's metal show up on somebody else's screen later.
//
// Metal is PER PLAYER, not a team pool. That is a default picked on 2026-09-11 to get the base
// built, not a settled design question: moving it to a shared pool means moving these two numbers
// to the GameState and nothing else here.
//
// Metal is separate from ammunition. That one IS the author's decision (2026-09-11): rounds are not
// touched by anything in this file.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ShooterPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMetalChangedDelegate, int32, Metal, int32, MaxMetal, int32, Delta);

UCLASS()
class POLARITY_API AShooterPlayerState : public APlayerState
{
	GENERATED_BODY()

public:

	AShooterPlayerState();

	// ==================== Metal ====================

	/** Most metal one player can hold. What does not fit stays on the floor for somebody else. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_MaxMetal, Category = "Metal", meta = (ClampMin = "1"))
	int32 MaxMetal = 200;

	/** Metal at the start of a run. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Metal", meta = (ClampMin = "0"))
	int32 StartingMetal = 0;

	UFUNCTION(BlueprintPure, Category = "Metal")
	int32 GetMetal() const { return Metal; }

	UFUNCTION(BlueprintPure, Category = "Metal")
	int32 GetMaxMetal() const { return MaxMetal; }

	/** How much more this player can take before the cap. */
	UFUNCTION(BlueprintPure, Category = "Metal")
	int32 GetMetalRoom() const { return FMath::Max(0, MaxMetal - Metal); }

	UFUNCTION(BlueprintPure, Category = "Metal")
	bool CanAffordMetal(int32 Cost) const { return Cost <= Metal; }

	/** Put metal in. Server only. Returns how much was actually taken, which is less than asked
	 *  when the cap is in the way: a pickup keeps the rest. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Metal")
	int32 AddMetal(int32 Amount);

	/** Take metal out for a purchase. Server only. All or nothing: false and no change when the
	 *  player cannot afford it, so a building is never paid for by half. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Metal")
	bool TrySpendMetal(int32 Cost);

	/** Fires on every machine that knows this player, whenever the number changes. Delta is
	 *  positive for a pickup and negative for a purchase. On the host it fires from the write
	 *  itself, because the host never receives its own replication. */
	UPROPERTY(BlueprintAssignable, Category = "Metal")
	FMetalChangedDelegate OnMetalChanged;

protected:

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Replicated to everyone, not only the owner. It is one integer, and a teammate being able to
	 *  see who has metal for a turret is exactly the kind of thing a coop HUD will want. */
	UPROPERTY(ReplicatedUsing = OnRep_Metal)
	int32 Metal = 0;

	UFUNCTION()
	void OnRep_Metal(int32 OldMetal);

	UFUNCTION()
	void OnRep_MaxMetal();

private:

	/** The one place a change is announced, so the host path and the replicated path cannot say
	 *  different things. */
	void SetMetalInternal(int32 NewMetal);
};
