// ShootableButton.h
// Standalone shootable button actor for level placement.
// Thin wrapper — all logic lives in UShootableButtonComponent.
// Component auto-binds to OnTakeAnyDamage, no TakeDamage override needed.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShootableButton.generated.h"

class UShootableButtonComponent;

UCLASS(Blueprintable)
class POLARITY_API AShootableButton : public AActor
{
	GENERATED_BODY()

public:
	AShootableButton();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Button")
	TObjectPtr<UShootableButtonComponent> ButtonComponent;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Button")
	TObjectPtr<USceneComponent> SceneRoot;
};
