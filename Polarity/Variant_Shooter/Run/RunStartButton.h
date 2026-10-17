// RunStartButton.h
// RUN START on the bench level: shoot it and the run skeleton begins again from the room it names,
// every room reset and the whole team put down there (ARunRoom::StartRunAt). After a team wipe the
// bench brings everybody back to the hub, and this button is how the run is started over.
//
// Server only, like the rest of the bench: the shot is resolved on the server, which moves the pawns,
// and the moves replicate.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RunStartButton.generated.h"

class UShootableButtonComponent;
class UTextRenderComponent;

UCLASS()
class POLARITY_API ARunStartButton : public AActor
{
	GENERATED_BODY()

public:

	ARunStartButton();

	/** The room the run starts at (ARunRoom::RoomIndex). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "1"))
	int32 FirstRoom = 1;

	/** Shown over the button. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run")
	FString Title = TEXT("RUN START");

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Run")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Run")
	TObjectPtr<UShootableButtonComponent> Button;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Run")
	TObjectPtr<UTextRenderComponent> Label;

private:

	UFUNCTION()
	void HandleButtonPressed(UShootableButtonComponent* Pressed, AActor* Activator);
};
