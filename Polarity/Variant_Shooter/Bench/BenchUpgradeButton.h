// BenchUpgradeButton.h
// One upgrade on the bench level's wall of upgrades: shoot the button and the one who shot gets the
// upgrade (or its next level); shoot it at the top level and it is taken away again [author,
// 2026-10-11]. So a fight can be tried with an upgrade and without it in one play session.
//
// Hand-placed, the upgrade set in the Details panel. Server only, like the rest of the bench.
// TODO(COOP): the label shows the state of whoever pressed last, and does not replicate.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BenchUpgradeButton.generated.h"

class UShootableButtonComponent;
class UTextRenderComponent;
class UUpgradeDefinition;

UCLASS()
class POLARITY_API ABenchUpgradeButton : public AActor
{
	GENERATED_BODY()

public:

	ABenchUpgradeButton();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UUpgradeDefinition> Upgrade;

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UShootableButtonComponent> Button;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UTextRenderComponent> Label;

private:

	UFUNCTION()
	void HandleButtonPressed(UShootableButtonComponent* Pressed, AActor* Activator);

	void ShowLevel(int32 Level);
};
