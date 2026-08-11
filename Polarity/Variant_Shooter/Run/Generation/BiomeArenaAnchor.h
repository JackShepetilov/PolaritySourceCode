// Persistent-level transform marking one arena position in a biome layout.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BiomeArenaAnchor.generated.h"
class UArrowComponent;
class USceneComponent;
UCLASS()
class POLARITY_API ABiomeArenaAnchor : public AActor
{
	GENERATED_BODY()
public:
	ABiomeArenaAnchor();
	/** Matches FBiomeArenaPool::SlotId. The arena inherits this actor's complete transform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome Arena")
	FName SlotId;
protected:
	UPROPERTY(VisibleAnywhere, Category = "Biome Arena")
	TObjectPtr<USceneComponent> SceneRoot;
#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "Biome Arena")
	TObjectPtr<UArrowComponent> DirectionArrow;
#endif
};
