// TutorialTriggerVolume.h
// Trigger volume for activating tutorials

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TutorialTypes.h"
#include "TutorialTriggerVolume.generated.h"

class UBoxComponent;
class UBillboardComponent;
class UInputAction;
class UTutorialSubsystem;

/**
 * Trigger volume that activates tutorials when player enters.
 * Place in the level and configure tutorial type and content.
 */
UCLASS(Blueprintable)
class POLARITY_API ATutorialTriggerVolume : public AActor
{
	GENERATED_BODY()

public:

	ATutorialTriggerVolume();

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==================== Components ====================

	/** Trigger box component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> TriggerBox;

#if WITH_EDITORONLY_DATA
	/** Editor billboard for visibility */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UBillboardComponent> EditorBillboard;
#endif

	// ==================== Configuration ====================

	/** Unique identifier for this tutorial */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FName TutorialID;

	/** Type of tutorial to show */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	ETutorialType TutorialType = ETutorialType::Hint;

	/** Data for hint-type tutorials */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Hint", 
		meta = (EditCondition = "TutorialType == ETutorialType::Hint", EditConditionHides))
	FTutorialHintData HintData;

	/** Data for slide-type tutorials */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Slide",
		meta = (EditCondition = "TutorialType == ETutorialType::Slide", EditConditionHides))
	FTutorialSlideData SlideData;

	/** Only trigger for actors with this tag (leave empty to trigger for any pawn) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Filtering")
	FName RequiredActorTag = FName("Player");

	/** If true, tutorial can only be triggered once per game session */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Behavior")
	bool bTriggerOnce = true;

	/** If true, hint hides when player exits the volume (only for Hints with OnExitVolume completion) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Behavior",
		meta = (EditCondition = "TutorialType == ETutorialType::Hint", EditConditionHides))
	bool bHideOnExit = false;

	// ==================== State ====================

	/** Has this trigger been activated this session */
	bool bHasTriggered = false;

	/** Is player currently inside the volume */
	bool bPlayerInside = false;

	// ==================== Overlap Handlers ====================

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, 
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// ==================== Internal ====================

	/** Check if actor passes filter requirements */
	bool PassesFilter(AActor* Actor) const;

	/** Get tutorial subsystem */
	UTutorialSubsystem* GetTutorialSubsystem() const;

	/** Bind to input action for hint completion */
	void BindInputCompletion();

	/** Unbind from input action */
	void UnbindInputCompletion();

	/** Called when required input action is triggered */
	void OnInputActionTriggered();

	/** Handle for input action binding */
	FDelegateHandle InputBindingHandle;
};
