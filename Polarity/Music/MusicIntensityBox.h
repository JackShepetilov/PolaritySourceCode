// MusicIntensityBox.h
// Trigger volume that activates intense music when player enters

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MusicIntensityBox.generated.h"

class UBoxComponent;
class UMusicTrackDataAsset;
class UMusicPlayerSubsystem;
class AShooterNPC;

DECLARE_LOG_CATEGORY_EXTERN(LogMusicIntensityBox, Log, All);

/**
 * Trigger volume that controls music intensity.
 * - When player enters: starts music (with fade on first entry) and sets intense mode
 * - When player exits: switches to calm mode (music continues at lower volume)
 * - Tracks enemies inside; deactivates when all enemies are dead
 */
UCLASS(Blueprintable)
class POLARITY_API AMusicIntensityBox : public AActor
{
	GENERATED_BODY()

public:
	AMusicIntensityBox();

	// ==================== Configuration ====================

	/** The music track to play when player enters this box */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
	TObjectPtr<UMusicTrackDataAsset> MusicTrack;

	/** Size of the trigger box */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music|Trigger")
	FVector BoxExtent = FVector(500.0f, 500.0f, 200.0f);

	// ==================== State ====================

	/** Is this the first time music will play from any MIB? (Determines fade in) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music|Debug")
	bool bIsFirstMusicEntry = true;

	/** Is this MIB currently active? Deactivates when all enemies inside are dead. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music|Debug")
	bool bIsActive = true;

	/** Is the player currently inside this box? */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music|Debug")
	bool bPlayerInside = false;

	// ==================== Public API ====================

	/** Get current number of tracked enemies */
	UFUNCTION(BlueprintPure, Category = "Music")
	int32 GetTrackedEnemyCount() const { return TrackedEnemies.Num(); }

	/** Manually reactivate this MIB (e.g., after checkpoint respawn) */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void Reactivate();

	/** Manually refresh enemy detection */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void RefreshEnemyDetection();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ==================== Components ====================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> TriggerBox;

	// ==================== Internal State ====================

	/** Cached reference to music subsystem */
	UPROPERTY()
	TObjectPtr<UMusicPlayerSubsystem> MusicSubsystem;

	/** Set of tracked enemies inside this box */
	UPROPERTY()
	TSet<TWeakObjectPtr<AShooterNPC>> TrackedEnemies;

private:
	// ==================== Overlap Handlers ====================

	UFUNCTION()
	void OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// ==================== Player Handling ====================

	void OnPlayerEntered();
	void OnPlayerExited();

	// ==================== Enemy Tracking ====================

	void StartTrackingNPC(AShooterNPC* NPC);
	void StopTrackingNPC(AShooterNPC* NPC);

	UFUNCTION()
	void OnTrackedNPCDeath(AShooterNPC* DeadNPC);

	void RebuildTrackedEnemies();
	void UpdateActiveState();

	// ==================== Debug ====================

	void LogDebug(const FString& Message) const;
	void LogWarning(const FString& Message) const;
};
