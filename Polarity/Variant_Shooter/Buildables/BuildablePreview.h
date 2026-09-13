// BuildablePreview.h
// The blueprint hologram: where the building would stand, green when it can, red when it cannot.
//
// Lives only on the machine of the player placing it. Never replicated, never collides, never
// ticks on its own; UBuilderComponent moves it every frame and tells it whether the spot is good.
// It shows the real building's mesh, copied off the actor class, so what the player sees is what
// the server will put down.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildablePreview.generated.h"

class UBuildableDefinition;
class UMaterialInterface;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class POLARITY_API ABuildablePreview : public AActor
{
	GENERATED_BODY()

public:

	ABuildablePreview();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Drawn over every material slot while the spot is good. Unset = the building's own look. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preview")
	TObjectPtr<UMaterialInterface> ValidMaterial;

	/** Drawn while the spot is refused. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preview")
	TObjectPtr<UMaterialInterface> InvalidMaterial;

	/** Take the mesh, offset and scale from the definition's actor class. */
	void SetupFromDefinition(const UBuildableDefinition* Definition);

	UFUNCTION(BlueprintCallable, Category = "Preview")
	void SetValid(bool bValid);

	UFUNCTION(BlueprintPure, Category = "Preview")
	bool IsValidSpot() const { return bLastValid; }

private:

	void ApplyMaterial(UMaterialInterface* Material);

	bool bLastValid = true;
	bool bMaterialApplied = false;
};
