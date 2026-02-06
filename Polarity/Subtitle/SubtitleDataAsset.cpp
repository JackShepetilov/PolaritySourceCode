// SubtitleDataAsset.cpp

#include "SubtitleDataAsset.h"
#include "Sound/SoundBase.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool USubtitleDataAsset::FindEntry(FName ID, FSubtitleEntry& OutEntry) const
{
	// Direct search - cache was causing issues with hot reload
	for (const FSubtitleEntry& Entry : Entries)
	{
		if (Entry.ID == ID)
		{
			OutEntry = Entry;
			return true;
		}
	}

	return false;
}

float USubtitleDataAsset::GetEntryDuration(FName ID) const
{
	FSubtitleEntry Entry;
	if (!FindEntry(ID, Entry))
	{
		return 0.0f;
	}

	// Use override if specified
	if (Entry.DurationOverride > 0.0f)
	{
		return Entry.DurationOverride;
	}

	// Try to get duration from sound
	if (USoundBase* Sound = Entry.Sound.LoadSynchronous())
	{
		return Sound->GetDuration();
	}

	// Fallback: estimate from text length (rough: 15 chars per second)
	const int32 TextLength = Entry.Text.ToString().Len();
	return FMath::Max(2.0f, TextLength / 15.0f);
}

bool USubtitleDataAsset::HasEntry(FName ID) const
{
	if (!bCacheBuilt)
	{
		BuildCache();
	}

	return CachedIDToIndex.Contains(ID);
}

TArray<FName> USubtitleDataAsset::GetAllEntryIDs() const
{
	TArray<FName> IDs;
	IDs.Reserve(Entries.Num());

	for (const FSubtitleEntry& Entry : Entries)
	{
		IDs.Add(Entry.ID);
	}

	return IDs;
}

void USubtitleDataAsset::BuildCache() const
{
	CachedIDToIndex.Empty(Entries.Num());

	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FName& ID = Entries[i].ID;
		if (!ID.IsNone())
		{
			CachedIDToIndex.Add(ID, i);
		}
	}

	bCacheBuilt = true;
}

#if WITH_EDITOR
EDataValidationResult USubtitleDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FName> SeenIDs;

	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FSubtitleEntry& Entry = Entries[i];

		// Check for empty ID
		if (Entry.ID.IsNone())
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("Entry %d has no ID"), i)));
			Result = EDataValidationResult::Invalid;
		}
		// Check for duplicate IDs
		else if (SeenIDs.Contains(Entry.ID))
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("Duplicate ID '%s' at entry %d"), *Entry.ID.ToString(), i)));
			Result = EDataValidationResult::Invalid;
		}
		else
		{
			SeenIDs.Add(Entry.ID);
		}

		// Check for empty text
		if (Entry.Text.IsEmpty())
		{
			Context.AddWarning(FText::FromString(FString::Printf(TEXT("Entry '%s' has empty text"), *Entry.ID.ToString())));
		}

		// Check for missing duration source
		if (Entry.Sound.IsNull() && Entry.DurationOverride <= 0.0f)
		{
			Context.AddWarning(FText::FromString(FString::Printf(TEXT("Entry '%s' has no Sound and no DurationOverride - will estimate from text length"), *Entry.ID.ToString())));
		}
	}

	return Result;
}
#endif
