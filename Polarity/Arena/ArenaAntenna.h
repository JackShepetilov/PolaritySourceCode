// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaAntenna.generated.h"

class AShootableButton;
class UShootableButtonComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UDataTable;

/**
 * Lifecycle of an antenna placed on an arena rooftop.
 *
 * Designers don't usually flip these by hand — ArenaManager drives the transitions
 * based on its own state (Active / Completed), and the antenna self-completes
 * when its paired button is pressed.
 */
UENUM(BlueprintType)
enum class EAntennaState : uint8
{
	/** Arena not cleared yet (idle OR in combat) — antenna is locked, button presses are rejected */
	Inactive,
	/** Legacy: pressing mid-fight used to cut the fight short. ArenaManager no longer sets
	 *  this state (antennas stay Inactive until the arena is cleared); kept for BP compat. */
	AvailableMidFight,
	/** Arena cleared the normal way; beacon VFX guides the player to the antenna */
	AvailablePostFight,
	/** Data uploaded — antenna is spent */
	Activated
};

/**
 * One dialogue selection rule: "if the next antenna is between MinDistance and MaxDistance
 * away, play this subtitle sequence." Designer fills in a small table per antenna so the
 * line length scales with the travel distance to the next objective.
 */
USTRUCT(BlueprintType)
struct FAntennaDialogueChoice
{
	GENERATED_BODY()

	/** Subtitle DataTable to read from (rows must follow SubtitleSubsystem's prefix scheme). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Antenna|Dialogue")
	TObjectPtr<UDataTable> SubtitleTable = nullptr;

	/** Prefix passed to ShowSubtitleSequence — picks up all rows matching "Prefix_*". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Antenna|Dialogue")
	FString SubtitlePrefix;

	/** Minimum distance (cm) to the next antenna for this rule to apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Antenna|Dialogue", meta = (ClampMin = "0.0"))
	float MinDistance = 0.0f;

	/** Maximum distance (cm) to the next antenna for this rule to apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Antenna|Dialogue", meta = (ClampMin = "0.0"))
	float MaxDistance = 100000.0f;
};

class AArenaAntenna;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAntennaActivated, AArenaAntenna*, Antenna);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAntennaStateChanged, AArenaAntenna*, Antenna, EAntennaState, NewState);

/**
 * Antenna placed on top of a building inside an arena.
 *
 * Lore: it's a data uplink — the player uploads stolen telemetry from the billionaire's
 * server and the arena ends.
 *
 * Mechanically: paired one-to-one with a ShootableButton (designer-assigned in Details).
 * The button is the actual interactable; pressing it activates the antenna. The antenna's
 * own visuals are a beacon (Niagara) that turns on when the fight is over to point the
 * player to the upload location.
 */
UCLASS(Blueprintable)
class POLARITY_API AArenaAntenna : public AActor
{
	GENERATED_BODY()

public:
	AArenaAntenna();

	// ==================== Components ====================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Antenna")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Visible antenna mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Antenna")
	TObjectPtr<UStaticMeshComponent> AntennaMesh;

	/** Beacon VFX — Niagara component that ramps up when the antenna becomes the next objective. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Antenna")
	TObjectPtr<UNiagaraComponent> BeaconVFX;

	// ==================== Configuration ====================

	/** Niagara system to play as the sky beacon. Designer-assigned (no default).
	 *  If null, BeaconVFX component is left untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Antenna|Beacon")
	TObjectPtr<UNiagaraSystem> BeaconVFXAsset = nullptr;

	/** Paired ShootableButton — pressing it triggers TryActivate().
	 *  Soft pointer so it can live in the same sublevel without hard-loading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Antenna|Interaction")
	TSoftObjectPtr<AShootableButton> InteractionButton;

	/** Ordered list of dialogue rules — the first one whose distance range matches
	 *  the distance to the nearest other antenna in the world wins. If nothing matches,
	 *  the first entry is used as a fallback (or nothing if the array is empty). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Antenna|Dialogue")
	TArray<FAntennaDialogueChoice> DialogueChoices;

	/** Logical arena tag for lore purposes — the same tag can repeat across levels.
	 *  When the antenna activates, ULoreSubsystem uses this to pick an arena-specific
	 *  lore entry (or fall back to biome-general / global). Empty = biome-general only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Antenna|Lore")
	FName ArenaTagForLore;

	// ==================== State (read-only) ====================

	/** Current state — driven by ArenaManager and the button press. */
	UPROPERTY(BlueprintReadOnly, Category = "Antenna|State")
	EAntennaState State = EAntennaState::Inactive;

	// ==================== Events ====================

	/** Fired when the player presses the paired button while the antenna is in an Available state.
	 *  Arena listens to this to wrap up combat, flush deferred popups, etc. */
	UPROPERTY(BlueprintAssignable, Category = "Antenna|Events")
	FOnAntennaActivated OnActivated;

	/** Fired any time State changes — useful for BP cosmetics (mesh material swap, etc.) */
	UPROPERTY(BlueprintAssignable, Category = "Antenna|Events")
	FOnAntennaStateChanged OnStateChanged;

	// ==================== API ====================

	/** Set the antenna's logical state. ArenaManager calls this on lifecycle transitions;
	 *  Activated state can also come from a successful button press via TryActivate(). */
	UFUNCTION(BlueprintCallable, Category = "Antenna")
	void SetState(EAntennaState NewState);

	/** Try to activate the antenna right now. No-op if it's not in an Available state.
	 *  Called by the button-press handler and exposed for BP scripted activations.
	 *  Virtual so subclasses (e.g. AGateAntenna) can intercept presses while locked. */
	UFUNCTION(BlueprintCallable, Category = "Antenna")
	virtual void TryActivate();

	/** Resolves the dialogue choice for the current distance-to-next-antenna and plays it
	 *  through the SubtitleSubsystem. Called automatically inside TryActivate's success path. */
	UFUNCTION(BlueprintCallable, Category = "Antenna|Dialogue")
	void PlayContextualDialogue();

	/** World-space distance (cm) from this antenna to the nearest other antenna that is
	 *  NOT this one and NOT in Activated state. Returns -1 if no other antenna exists. */
	UFUNCTION(BlueprintPure, Category = "Antenna|Dialogue")
	float GetDistanceToNearestOtherAntenna() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Bound to InteractionButton's OnButtonPressed in BeginPlay. */
	UFUNCTION()
	void HandleButtonPressed(UShootableButtonComponent* Button, AActor* Activator);

	/** Picks the first FAntennaDialogueChoice whose [Min, Max] band contains the given distance.
	 *  Returns nullptr if nothing matches and DialogueChoices is empty; falls back to the first
	 *  entry if there's at least one entry but no exact match. */
	const FAntennaDialogueChoice* PickDialogueChoiceForDistance(float Distance) const;

	/** Apply visual side-effects of a state transition (beacon on/off, etc.) */
	void ApplyStateVisuals(EAntennaState NewState);

	/** Cached: have we already bound OnButtonPressed? Prevents double-bind if SetState is bumped early. */
	bool bBoundToButton = false;
};
