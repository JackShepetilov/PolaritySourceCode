// MusicExitBox.h
// Trigger volume that stops music when player enters

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MusicExitBox.generated.h"

class UBoxComponent;
class UMusicPlayerSubsystem;

DECLARE_LOG_CATEGORY_EXTERN(LogMusicExitBox, Log, All);

/**
 * Trigger volume that stops music with fade out when player enters.
 * Place between level sections to cleanly transition between music tracks.
 */
UCLASS(Blueprintable)
class POLARITY_API AMusicExitBox : public AActor
{
	GENERATED_BODY()

public:
	AMusicExitBox();

	// ==================== Configuration ====================

	/** Size of the trigger box */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music|Trigger")
	FVector BoxExtent = FVector(200.0f, 200.0f, 200.0f);

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ==================== Components ====================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> TriggerBox;

	// ==================== Internal ====================

	UPROPERTY()
	TObjectPtr<UMusicPlayerSubsystem> MusicSubsystem;

private:
	UFUNCTION()
	void OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void LogDebug(const FString& Message) const;
};
