// Viewport-authored Landscape Grass exclusion shape stored inside an arena level.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaGrassExclusionVolume.generated.h"

class UBoxComponent;

/**
 * Place one or more of these actors inside an arena .umap. Their box transforms are authored
 * visually in the viewport and are registered with Landscape Grass when the arena streams in.
 */
UCLASS()
class POLARITY_API AArenaGrassExclusionVolume : public AActor
{
	GENERATED_BODY()

public:
	AArenaGrassExclusionVolume();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landscape Grass")
	TObjectPtr<UBoxComponent> ExclusionBox;

	FBox GetWorldExclusionBox() const;
};
