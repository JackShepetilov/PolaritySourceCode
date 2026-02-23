// EMFAcceleratorPlate.h
// Placeable accelerator plate with EMF AcceleratorPlate field type.
// Can be carried by the player's capture system with overridden behavior:
// - Snaps to a configurable offset relative to the player camera
// - No charge dependency, no EMF-based carry mechanics
// - Freezes in place on release (no reverse capture)
// - Lowest capture priority (only captured when no other targets in range)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EMFAcceleratorPlate.generated.h"

class UEMF_FieldComponent;

UCLASS(Blueprintable)
class POLARITY_API AEMFAcceleratorPlate : public AActor
{
	GENERATED_BODY()

public:
	AEMFAcceleratorPlate();

	// ==================== Components ====================

	/** Root scene component (user adds meshes in Blueprint) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** EMF field component configured as AcceleratorPlate */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	// ==================== EMF Settings ====================

	/** Surface charge density for the accelerator plate field */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge")
	float SurfaceChargeDensity = 1.0f;

	/** Plate dimensions (Width x Height in cm) for the EMF field boundary */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMF|Charge")
	FVector2D PlateDimensions = FVector2D(200.0f, 200.0f);

	// ==================== Capture Settings ====================

	/** Offset from camera when held by player (local space: X = forward, Y = right, Z = up) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	FVector HoldOffset = FVector(200.0f, 0.0f, 0.0f);

	/** Additional rotation applied on top of face-toward-camera (adjust to match mesh orientation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	FRotator HoldRotationOffset = FRotator::ZeroRotator;

	/** Can this plate be captured by the player? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	bool bCanBeCaptured = true;

	// ==================== Capture API ====================

	/** Begin capture — plate starts following the player camera */
	UFUNCTION(BlueprintCallable, Category = "Capture")
	void StartCapture();

	/** End capture — plate freezes at current position */
	UFUNCTION(BlueprintCallable, Category = "Capture")
	void StopCapture();

	/** Is this plate currently being held? */
	UFUNCTION(BlueprintPure, Category = "Capture")
	bool IsCaptured() const { return bIsCaptured; }

	/**
	 * Update position to follow camera offset.
	 * Called by ChargeAnimationComponent each frame during channeling.
	 */
	void UpdateHoldPosition(const FVector& CameraLoc, const FRotator& CameraRot);

protected:
	virtual void BeginPlay() override;

private:
	bool bIsCaptured = false;
};
