// InputIconsDataAsset.h
// Data Asset for mapping input keys to icon textures with auto-discovery

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputCoreTypes.h"
#include "InputIconsDataAsset.generated.h"

/**
 * Single entry mapping a key to its icon texture (for manual overrides)
 */
USTRUCT(BlueprintType)
struct FInputIconEntry
{
	GENERATED_BODY()

	/** The input key */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Icon")
	FKey Key;

	/** Icon texture for this key */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Icon")
	TSoftObjectPtr<UTexture2D> Icon;
};

/**
 * Data Asset containing mappings from input keys to icon textures.
 *
 * АВТОМАТИЧЕСКИЙ РЕЖИМ:
 * 1. Укажи IconsDirectory (например /Game/UI/InputIcons/)
 * 2. Импортируй текстуры с именами T_Key_E, T_Key_Space, T_Key_LeftShift и т.д.
 * 3. Data Asset автоматически найдет их по имени
 *
 * РУЧНОЙ РЕЖИМ:
 * Заполни массив ManualOverrides для конкретных клавиш
 */
UCLASS(BlueprintType)
class POLARITY_API UInputIconsDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	// ==================== Auto-Discovery ====================

	/**
	 * Directory to search for key icons (e.g., /Game/UI/InputIcons/)
	 * Textures should be named: T_Key_{KeyName}
	 * Examples: T_Key_E, T_Key_Space, T_Key_LeftShift, T_Key_Escape
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Auto-Discovery", meta = (ContentDir))
	FDirectoryPath IconsDirectory;

	/** Prefix for auto-discovered textures (default: T_Key_) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Auto-Discovery")
	FString TexturePrefix = TEXT("T_Key_");

	// ==================== Manual Overrides ====================

	/** Manual key-to-icon mappings (overrides auto-discovery) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Manual Overrides")
	TArray<FInputIconEntry> ManualOverrides;

	/** Fallback icon when key is not found */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fallback")
	TSoftObjectPtr<UTexture2D> FallbackIcon;

	// ==================== API ====================

	/**
	 * Get icon texture for a specific key
	 * @param Key - The input key to look up
	 * @return Icon texture, or FallbackIcon if not found
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Icons")
	UTexture2D* GetIconForKey(const FKey& Key) const;

	/**
	 * Check if an icon exists for the given key
	 * @param Key - The input key to check
	 * @return True if a specific icon exists (not fallback)
	 */
	UFUNCTION(BlueprintPure, Category = "Input Icons")
	bool HasIconForKey(const FKey& Key) const;

	/**
	 * Rebuild the icon cache (call after importing new textures)
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Input Icons")
	void RebuildCache();

	/**
	 * Log all discovered key mappings (for debugging)
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Input Icons")
	void PrintDiscoveredMappings() const;

protected:

	/** Cached map for fast lookup */
	mutable TMap<FKey, TSoftObjectPtr<UTexture2D>> CachedKeyToIconMap;

	/** Whether cache has been built */
	mutable bool bCacheBuilt = false;

	/** Build the lookup cache */
	void BuildCache() const;

	/** Try to find texture for key via auto-discovery */
	TSoftObjectPtr<UTexture2D> FindTextureForKey(const FKey& Key) const;

	/** Convert FKey to texture asset name */
	FString KeyToTextureName(const FKey& Key) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};