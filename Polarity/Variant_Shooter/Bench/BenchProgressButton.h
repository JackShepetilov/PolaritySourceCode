// BenchProgressButton.h
// One button on the bench's progression wall: shoot it and the shooter's run level, the mastery of the
// gun in his hands or his gadget kinds change at once [author, 2026-10-16]. So a level is tried by a
// shot, not by fighting up to it. Same shape as ABenchUpgradeButton.
//
// Hand-placed by Source/Tools/CombatBench/build_bench_armory.py (step_progress), the action set in
// the Details panel. Server only, like the rest of the bench.
// TODO(COOP): the label shows the state of whoever pressed last, and does not replicate.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BenchProgressButton.generated.h"

class UShootableButtonComponent;
class UTextRenderComponent;
class AShooterCharacter;

UENUM(BlueprintType)
enum class EBenchProgressAction : uint8
{
	MasteryUp,
	MasteryDown,
	MasteryReset,
	MasteryMax,
	RunLevelUp,
	RunLevelReset,
	GadgetKindsTwo,
	GadgetKindsThree
};

UCLASS()
class POLARITY_API ABenchProgressButton : public AActor
{
	GENERATED_BODY()

public:

	ABenchProgressButton();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progress")
	EBenchProgressAction Action = EBenchProgressAction::MasteryUp;

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Progress")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Progress")
	TObjectPtr<UShootableButtonComponent> Button;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Progress")
	TObjectPtr<UTextRenderComponent> Label;

private:

	UFUNCTION()
	void HandleButtonPressed(UShootableButtonComponent* Pressed, AActor* Activator);

	/** What the button does, for the label. */
	FString GetTitle() const;

	/** The title, and under it what the shooter has now. */
	void ShowState(const AShooterCharacter* Shooter);
};
