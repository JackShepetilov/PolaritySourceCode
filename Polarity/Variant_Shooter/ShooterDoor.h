// ShooterDoor.h
// Door actor that responds to key destruction and player proximity

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CheckpointData.h"
#include "ShooterDoor.generated.h"

class UBoxComponent;
class AShooterKey;
class AShooterCharacter;
class UCheckpointSubsystem;

// Key state change events
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKeyDeath, AShooterKey*, DeadKey);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKeyRespawned, AShooterKey*, RespawnedKey);

// Player detection events
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerEnteredDoor, AShooterCharacter*, Player);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerExitedDoor, AShooterCharacter*, Player);

// Door state events
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDoorOpened);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDoorClosed);

/**
 * Door actor with key detection and player proximity sensing.
 * State persists across checkpoint respawns.
 */
UCLASS(Blueprintable)
class POLARITY_API AShooterDoor : public AActor
{
	GENERATED_BODY()

public:
	AShooterDoor();

	// ==================== Door State ====================

	/** Current door state (true = open, false = closed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door|State")
	bool bIsOpen = false;

	// ==================== Key Detection ====================

	/** Box component for detecting keys */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door|Detection")
	TObjectPtr<UBoxComponent> KeyDetectionBox;

	/** Size of the key detection box */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door|Detection")
	FVector KeyBoxExtent = FVector(200.0f, 200.0f, 200.0f);

	/** Offset of the key detection box from actor location */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door|Detection")
	FVector KeyBoxOffset = FVector::ZeroVector;

	// ==================== Player Detection ====================

	/** Box component for detecting player */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door|Detection")
	TObjectPtr<UBoxComponent> PlayerDetectionBox;

	/** Size of the player detection box */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door|Detection")
	FVector PlayerBoxExtent = FVector(300.0f, 300.0f, 200.0f);

	/** Offset of the player detection box from actor location */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door|Detection")
	FVector PlayerBoxOffset = FVector::ZeroVector;

	// ==================== Events - Key ====================

	/** Called when a tracked key dies */
	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnKeyDeath OnKeyDeath;

	/** Called when a key respawns (after checkpoint respawn) */
	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnKeyRespawned OnKeyRespawned;

	// ==================== Events - Player ====================

	/** Called when player enters the player detection box */
	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnPlayerEnteredDoor OnPlayerEntered;

	/** Called when player exits the player detection box */
	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnPlayerExitedDoor OnPlayerExited;

	// ==================== Events - Door State ====================

	/** Called when door opens (bIsOpen changes to true) */
	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnDoorOpened OnDoorOpened;

	/** Called when door closes (bIsOpen changes to false) */
	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnDoorClosed OnDoorClosed;

	// ==================== Public API ====================

	/** Open the door */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void OpenDoor();

	/** Close the door */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void CloseDoor();

	/** Toggle door state */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void ToggleDoor();

	/** Check if player is currently inside player detection box */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsPlayerInside() const { return bIsPlayerInside; }

	/** Get currently tracked key (may be null or pending kill) */
	UFUNCTION(BlueprintPure, Category = "Door")
	AShooterKey* GetTrackedKey() const { return TrackedKey.Get(); }

	/** Check if tracked key is alive */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsKeyAlive() const;

	/** Get number of alive tracked keys */
	UFUNCTION(BlueprintPure, Category = "Door")
	int32 GetAliveKeyCount() const;

	/** Update key detection box size and offset */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void UpdateKeyDetectionBox();

	/** Update player detection box size and offset */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void UpdatePlayerDetectionBox();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==================== State Tracking ====================

	/** Is player currently inside player detection box */
	bool bIsPlayerInside = false;

	/** Door state saved at last checkpoint (for respawn restore) */
	bool bStateAtCheckpoint = false;

	/** Currently tracked key (architecture supports expanding to TArray later) */
	UPROPERTY()
	TWeakObjectPtr<AShooterKey> TrackedKey;

	/** All keys that have ever been in the detection box (for respawn tracking) */
	UPROPERTY()
	TSet<TWeakObjectPtr<AShooterKey>> KnownKeys;

	/** Cached reference to checkpoint subsystem */
	UPROPERTY()
	TObjectPtr<UCheckpointSubsystem> CheckpointSubsystem;

private:
	// ==================== Key Detection Callbacks ====================

	UFUNCTION()
	void OnKeyBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnKeyBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// ==================== Player Detection Callbacks ====================

	UFUNCTION()
	void OnPlayerBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnPlayerBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// ==================== Key Event Handlers ====================

	/** Called when a tracked key dies */
	UFUNCTION()
	void HandleKeyDeath(AShooterDummy* Dummy, AActor* Killer);

	// ==================== Checkpoint Handlers ====================

	/** Called when a checkpoint is activated - save current state */
	UFUNCTION()
	void OnCheckpointActivated(const FCheckpointData& CheckpointData);

	/** Called when player respawns - restore saved state */
	UFUNCTION()
	void OnPlayerRespawned();

	/** Rescan for keys after respawn */
	void RescanForKeys();

	/** Start tracking a key */
	void StartTrackingKey(AShooterKey* Key);

	/** Stop tracking a key */
	void StopTrackingKey(AShooterKey* Key);
};
