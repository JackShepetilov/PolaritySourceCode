// EMFChannelingPlateActor.h
// Invisible actor carrying UEMF_FieldComponent configured as FinitePlate
// Spawned in front of the camera during channeling ability

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EMFChannelingPlateActor.generated.h"

class UEMF_FieldComponent;

/**
 * Minimal invisible actor that serves as a charged plate field source.
 * Auto-registers in UEMF_SourceRegistry so enemies can react to it.
 * No visual, no collision — purely an EMF field source.
 */
UCLASS()
class POLARITY_API AEMFChannelingPlateActor : public AActor
{
	GENERATED_BODY()

public:
	AEMFChannelingPlateActor();

	/** Root scene component (required for actor to be movable) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<USceneComponent> SceneRoot;

	/** The EMF field component configured as FinitePlate */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> PlateFieldComponent;

	/** Draw debug visualization of the plate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Debug")
	bool bDrawDebugPlate = false;

	// ==================== API ====================

	/** Set the plate's surface charge density (sign determines polarity) */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetPlateChargeDensity(float Density);

	/** Get current surface charge density */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetPlateChargeDensity() const;

	/** Configure plate dimensions (Width x Height in cm) */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetPlateDimensions(FVector2D Dimensions);

	/** Update position and rotation to follow camera */
	void UpdateTransformFromCamera(const FVector& CameraLocation, const FRotator& CameraRotation, const FVector& LocalOffset);

	// ==================== Capture ====================

	/** Set the NPC currently captured by this plate */
	void SetCapturedNPC(AActor* NPC) { CapturedNPC = NPC; }

	/** Get the captured NPC (nullptr if none) */
	AActor* GetCapturedNPC() const { return CapturedNPC.Get(); }

	/** Clear the captured NPC reference */
	void ClearCapturedNPC() { CapturedNPC.Reset(); }

	/** Enable reverse mode (tangential-only damping for launch) */
	void SetReverseMode(bool bReverse) { bReverseMode = bReverse; }

	/** Is plate in reverse channeling mode? */
	bool IsInReverseMode() const { return bReverseMode; }

	/** Get plate normal (forward direction) */
	FVector GetPlateNormal() const { return CachedPlateNormal; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

private:
	/** Cached dimensions for debug drawing */
	FVector2D CachedDimensions = FVector2D(200.0f, 200.0f);

	/** Cached plate normal (forward direction), updated each frame */
	FVector CachedPlateNormal = FVector::ForwardVector;

	/** NPC currently captured by this plate */
	UPROPERTY()
	TWeakObjectPtr<AActor> CapturedNPC;

	/** Reverse channeling mode: tangential-only damping */
	bool bReverseMode = false;

	/** Draw debug visualization */
	void DrawDebug() const;
};
