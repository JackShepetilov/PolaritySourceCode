// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CheckpointActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class USoundBase;
class UNiagaraSystem;
class AShooterCharacter;

/**
 * Checkpoint actor that saves player state when entered.
 * Place in level to create respawn points.
 *
 * Visual style: Translucent wall with "CHECKPOINT" text (Ultrakill-inspired).
 */
UCLASS(Blueprintable)
class POLARITY_API ACheckpointActor : public AActor
{
	GENERATED_BODY()

public:
	ACheckpointActor();

	/** Get the transform where player should respawn */
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	FTransform GetSpawnTransform() const;

	/** Get unique ID for this checkpoint */
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	FGuid GetCheckpointID() const { return CheckpointID; }

	/** Check if this checkpoint has been activated this session */
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	bool WasActivated() const { return bWasActivated; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Trigger volume for checkpoint activation */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> TriggerBox;

	/** Visual representation - placeholder wall */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	/** Text displaying "CHECKPOINT" */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTextRenderComponent> CheckpointText;

	/** Scene component for spawn location (can be offset from actor) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SpawnPoint;

	/** Sound to play on activation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint|Feedback")
	TObjectPtr<USoundBase> ActivationSound;

	/** VFX to spawn on activation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint|Feedback")
	TObjectPtr<UNiagaraSystem> ActivationVFX;

	/** Color of the checkpoint visual when not activated */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint|Visual")
	FLinearColor InactiveColor = FLinearColor(1.0f, 0.5f, 0.8f, 0.5f); // Pink, translucent

	/** Color of the checkpoint visual after activation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint|Visual")
	FLinearColor ActiveColor = FLinearColor(0.5f, 1.0f, 0.5f, 0.3f); // Green, more translucent

	/** Whether to hide visual after activation (like Ultrakill) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint|Visual")
	bool bHideAfterActivation = true;

	/** Whether this checkpoint can be re-activated after respawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint")
	bool bCanReactivate = false;

	/** Called when something overlaps the trigger */
	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Internal activation logic */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	void ActivateCheckpoint(AShooterCharacter* Character);

	/** Blueprint event for custom activation logic */
	UFUNCTION(BlueprintImplementableEvent, Category = "Checkpoint", meta = (DisplayName = "On Checkpoint Activated"))
	void BP_OnCheckpointActivated(AShooterCharacter* Character);

	/** Update visual state (color, visibility) */
	void UpdateVisualState();

private:
	/** Unique identifier for this checkpoint */
	UPROPERTY()
	FGuid CheckpointID;

	/** Whether this checkpoint was activated this session */
	bool bWasActivated = false;

	/** Dynamic material instance for visual mesh */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;
};
