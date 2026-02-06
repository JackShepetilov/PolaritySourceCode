// SubtitleDataAsset.h
// Data asset for storing subtitle entries

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SubtitleTypes.h"
#include "SubtitleDataAsset.generated.h"

/**
 * Data Asset containing a collection of subtitle entries.
 *
 * Usage:
 * 1. Create a SubtitleDataAsset for each logical group (e.g., DA_BossDialogue, DA_NarratorLines)
 * 2. Add entries with unique IDs
 * 3. Reference the Sound to auto-calculate duration, or use DurationOverride
 * 4. Call SubtitleSubsystem->ShowSubtitle(DataAsset, "entry_id") from Blueprint
 */
UCLASS(BlueprintType)
class POLARITY_API USubtitleDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	// ==================== Entries ====================

	/** All subtitle entries in this asset */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles")
	TArray<FSubtitleEntry> Entries;

	// ==================== API ====================

	/**
	 * Find a subtitle entry by ID
	 * @param ID - The entry identifier to find
	 * @param OutEntry - The found entry (if successful)
	 * @return True if entry was found
	 */
	UFUNCTION(BlueprintCallable, Category = "Subtitles")
	bool FindEntry(FName ID, FSubtitleEntry& OutEntry) const;

	/**
	 * Get duration for an entry (from Sound or DurationOverride)
	 * @param ID - The entry identifier
	 * @return Duration in seconds, or 0 if entry not found
	 */
	UFUNCTION(BlueprintCallable, Category = "Subtitles")
	float GetEntryDuration(FName ID) const;

	/**
	 * Check if an entry exists
	 * @param ID - The entry identifier to check
	 * @return True if entry exists
	 */
	UFUNCTION(BlueprintPure, Category = "Subtitles")
	bool HasEntry(FName ID) const;

	/**
	 * Get all entry IDs (for debugging/tooling)
	 * @return Array of all entry IDs
	 */
	UFUNCTION(BlueprintCallable, Category = "Subtitles")
	TArray<FName> GetAllEntryIDs() const;

#if WITH_EDITOR
	/**
	 * Validate entries (check for duplicate IDs, missing sounds without duration override, etc.)
	 */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:

	/** Cached map for fast lookup (built on first query) */
	mutable TMap<FName, int32> CachedIDToIndex;
	mutable bool bCacheBuilt = false;

	/** Build the lookup cache */
	void BuildCache() const;
};
