// ShooterKey.h
// Key actor that becomes vulnerable when nearby enemies are eliminated

#pragma once

#include "CoreMinimal.h"
#include "ShooterDummy.h"
#include "ShooterKey.generated.h"

class UBoxComponent;
class AShooterNPC;
class UCheckpointSubsystem;
class UMaterialInterface;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnemyCountChanged, int32, NewCount, int32, RequiredToVulnerable);

/**
 * Key actor that inherits from ShooterDummy.
 * Becomes vulnerable (can take damage) only when nearby enemy count drops below threshold.
 * Uses box collision volumes to detect ShooterNPC enemies.
 */
UCLASS(Blueprintable)
class POLARITY_API AShooterKey : public AShooterDummy
{
	GENERATED_BODY()

public:
	AShooterKey();

	// ==================== Invulnerability Settings ====================

	/** When enabled, invulnerability is controlled manually via SetInvulnerable() instead of enemy detection.
	 *  Enemy tracking still works but won't affect invulnerability state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key|Invulnerability")
	bool bManualMode = false;

	/** Number of enemies required to keep the key invulnerable (only used when bManualMode = false).
	 *  If enemy count > this value, key is invulnerable.
	 *  If enemy count <= this value, key is vulnerable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key|Invulnerability", meta = (ClampMin = "0", EditCondition = "!bManualMode"))
	int32 EnemyThreshold = 0;

	// ==================== Detection Box ====================

	/** Primary detection box for enemies */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Key|Detection")
	TObjectPtr<UBoxComponent> PrimaryDetectionBox;

	/** Size of the primary detection box */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key|Detection")
	FVector DetectionBoxExtent = FVector(500.0f, 500.0f, 200.0f);

	/** Offset of the primary detection box from actor location */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key|Detection")
	FVector DetectionBoxOffset = FVector::ZeroVector;

	// ==================== Overlay Materials ====================

	/** Material applied when key is invulnerable (enemies present) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key|Materials")
	TObjectPtr<UMaterialInterface> InvulnerableMaterial;

	/** Material applied when key is vulnerable (enemies eliminated) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key|Materials")
	TObjectPtr<UMaterialInterface> VulnerableMaterial;

	// ==================== Events ====================

	/** Called when the number of detected enemies changes */
	UPROPERTY(BlueprintAssignable, Category = "Key|Events")
	FOnEnemyCountChanged OnEnemyCountChanged;

	// ==================== Public API ====================

	/** Get current number of enemies in all detection boxes */
	UFUNCTION(BlueprintPure, Category = "Key")
	int32 GetCurrentEnemyCount() const { return TrackedEnemies.Num(); }

	/** Check if key is currently invulnerable */
	UFUNCTION(BlueprintPure, Category = "Key")
	bool IsInvulnerable() const { return bIsInvulnerable; }

	/** Manually set invulnerability state (only works when bManualMode = true) */
	UFUNCTION(BlueprintCallable, Category = "Key")
	void SetInvulnerable(bool bNewInvulnerable);

	/** Manually refresh enemy detection (rescans all boxes) */
	UFUNCTION(BlueprintCallable, Category = "Key")
	void RefreshEnemyDetection();

	/** Register an additional detection box component (call from Blueprint after adding box) */
	UFUNCTION(BlueprintCallable, Category = "Key")
	void RegisterAdditionalDetectionBox(UBoxComponent* BoxComponent);

	/** Unregister a detection box component */
	UFUNCTION(BlueprintCallable, Category = "Key")
	void UnregisterDetectionBox(UBoxComponent* BoxComponent);

	/** Update primary detection box size and offset (call after changing DetectionBoxExtent/Offset) */
	UFUNCTION(BlueprintCallable, Category = "Key")
	void UpdatePrimaryDetectionBox();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Current invulnerability state */
	bool bIsInvulnerable = true;

	/** Set of all currently tracked enemies (no duplicates even if in multiple boxes) */
	UPROPERTY()
	TSet<TWeakObjectPtr<AShooterNPC>> TrackedEnemies;

	/** All registered detection boxes (including primary and any Blueprint-added ones) */
	UPROPERTY()
	TArray<TWeakObjectPtr<UBoxComponent>> DetectionBoxes;

	/** Cached reference to checkpoint subsystem */
	UPROPERTY()
	TObjectPtr<UCheckpointSubsystem> CheckpointSubsystem;

private:
	/** Setup overlap callbacks for a detection box */
	void SetupBoxOverlapCallbacks(UBoxComponent* BoxComponent);

	/** Called when an actor enters any detection box */
	UFUNCTION()
	void OnDetectionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Called when an actor exits any detection box */
	UFUNCTION()
	void OnDetectionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** Called when a tracked NPC dies */
	UFUNCTION()
	void OnTrackedNPCDeath(AShooterNPC* DeadNPC);

	/** Called when player respawns at checkpoint */
	UFUNCTION()
	void OnPlayerRespawned();

	/** Add an NPC to tracking (binds death delegate) */
	void StartTrackingNPC(AShooterNPC* NPC);

	/** Remove an NPC from tracking (unbinds death delegate) */
	void StopTrackingNPC(AShooterNPC* NPC);

	/** Update invulnerability state based on current enemy count */
	void UpdateInvulnerabilityState();

	/** Scan all detection boxes and rebuild tracked enemies set */
	void RebuildTrackedEnemies();

	/** Apply the appropriate overlay material based on invulnerability state */
	void ApplyOverlayMaterial();
};
