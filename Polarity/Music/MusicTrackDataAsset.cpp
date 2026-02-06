// MusicTrackDataAsset.cpp

#include "MusicTrackDataAsset.h"

const FMusicPart* UMusicTrackDataAsset::FindPart(FName PartID) const
{
	if (PartID.IsNone())
	{
		return nullptr;
	}

	for (const FMusicPart& Part : Parts)
	{
		if (Part.PartID == PartID)
		{
			return &Part;
		}
	}

	return nullptr;
}

const FMusicPart* UMusicTrackDataAsset::GetStartPart() const
{
	return FindPart(DefaultStartPart);
}

bool UMusicTrackDataAsset::IsValid() const
{
	if (Parts.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("MusicTrackDataAsset [%s]: No parts defined"), *TrackName);
		return false;
	}

	if (DefaultStartPart.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("MusicTrackDataAsset [%s]: No default start part set"), *TrackName);
		return false;
	}

	const FMusicPart* StartPart = GetStartPart();
	if (!StartPart)
	{
		UE_LOG(LogTemp, Warning, TEXT("MusicTrackDataAsset [%s]: Default start part '%s' not found in parts list"),
			*TrackName, *DefaultStartPart.ToString());
		return false;
	}

	if (!StartPart->Sound)
	{
		UE_LOG(LogTemp, Warning, TEXT("MusicTrackDataAsset [%s]: Start part '%s' has no sound assigned"),
			*TrackName, *DefaultStartPart.ToString());
		return false;
	}

	return true;
}

#if WITH_EDITOR
void UMusicTrackDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Validate on edit
	if (PropertyChangedEvent.Property)
	{
		// Log validation status
		if (IsValid())
		{
			UE_LOG(LogTemp, Log, TEXT("MusicTrackDataAsset [%s]: Validation passed"), *TrackName);
		}
	}
}
#endif
