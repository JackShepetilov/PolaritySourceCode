// BenchTeleport.h
// A button on the bench level that puts whoever shoots it somewhere else: the hub's row of buttons
// (one per fight room) and the "back to the hub" button in every room. The level has grown too big
// to walk [author, 2026-10-11].
//
// Hand-placed, with a hand-placed destination actor: where it stands is where the player lands, and
// the way it faces is the way the player looks. Server only, like the rest of the bench: the shot is
// resolved on the server, which moves the pawn, and the move replicates.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BenchTeleport.generated.h"

class UShootableButtonComponent;
class UTextRenderComponent;

UCLASS()
class POLARITY_API ABenchTeleport : public AActor
{
	GENERATED_BODY()

public:

	ABenchTeleport();

	/** Shown over the button. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teleport")
	FString Title = TEXT("TELEPORT");

	/** Where the one who pressed it lands, and which way they look. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Teleport")
	TObjectPtr<AActor> Destination;

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleport")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleport")
	TObjectPtr<UShootableButtonComponent> Button;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleport")
	TObjectPtr<UTextRenderComponent> Label;

private:

	UFUNCTION()
	void HandleButtonPressed(UShootableButtonComponent* Pressed, AActor* Activator);
};
