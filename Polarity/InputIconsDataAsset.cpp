// InputIconsDataAsset.cpp
// Data Asset for mapping input keys to icon textures with auto-discovery

#include "InputIconsDataAsset.h"
#include "Engine/Texture2D.h"
#include "Polarity.h"

UTexture2D* UInputIconsDataAsset::GetIconForKey(const FKey& Key) const
{
	if (!bCacheBuilt)
	{
		BuildCache();
	}

	// Check cache first
	if (const TSoftObjectPtr<UTexture2D>* Found = CachedKeyToIconMap.Find(Key))
	{
		if (UTexture2D* Texture = Found->LoadSynchronous())
		{
			return Texture;
		}
	}

	// Try auto-discovery
	TSoftObjectPtr<UTexture2D> DiscoveredTexture = FindTextureForKey(Key);
	if (UTexture2D* Texture = DiscoveredTexture.LoadSynchronous())
	{
		// Cache for next time
		CachedKeyToIconMap.Add(Key, DiscoveredTexture);
		return Texture;
	}

	// Return fallback
	return FallbackIcon.LoadSynchronous();
}

bool UInputIconsDataAsset::HasIconForKey(const FKey& Key) const
{
	if (!bCacheBuilt)
	{
		BuildCache();
	}

	// Check manual overrides
	if (CachedKeyToIconMap.Contains(Key))
	{
		return true;
	}

	// Check auto-discovery
	TSoftObjectPtr<UTexture2D> DiscoveredTexture = FindTextureForKey(Key);
	return DiscoveredTexture.LoadSynchronous() != nullptr;
}

void UInputIconsDataAsset::RebuildCache()
{
	bCacheBuilt = false;
	CachedKeyToIconMap.Empty();
	BuildCache();

	UE_LOG(LogPolarity, Log, TEXT("InputIconsDataAsset: Cache rebuilt with %d entries"), CachedKeyToIconMap.Num());
}

void UInputIconsDataAsset::PrintDiscoveredMappings() const
{
	if (!bCacheBuilt)
	{
		BuildCache();
	}

	UE_LOG(LogPolarity, Log, TEXT("=== Input Icons Mappings ==="));
	UE_LOG(LogPolarity, Log, TEXT("Directory: %s"), *IconsDirectory.Path);
	UE_LOG(LogPolarity, Log, TEXT("Prefix: %s"), *TexturePrefix);
	UE_LOG(LogPolarity, Log, TEXT("Manual Overrides: %d"), ManualOverrides.Num());
	UE_LOG(LogPolarity, Log, TEXT("Cached Entries: %d"), CachedKeyToIconMap.Num());

	for (const auto& Pair : CachedKeyToIconMap)
	{
		FString TexturePath = Pair.Value.IsNull() ? TEXT("NULL") : Pair.Value.ToString();
		UE_LOG(LogPolarity, Log, TEXT("  %s -> %s"), *Pair.Key.ToString(), *TexturePath);
	}

	UE_LOG(LogPolarity, Log, TEXT("=== End Mappings ==="));
}

void UInputIconsDataAsset::BuildCache() const
{
	CachedKeyToIconMap.Empty();

	// Add manual overrides first (they take priority)
	for (const FInputIconEntry& Entry : ManualOverrides)
	{
		if (Entry.Key.IsValid() && !Entry.Icon.IsNull())
		{
			CachedKeyToIconMap.Add(Entry.Key, Entry.Icon);
		}
	}

	bCacheBuilt = true;
}

TSoftObjectPtr<UTexture2D> UInputIconsDataAsset::FindTextureForKey(const FKey& Key) const
{
	if (!Key.IsValid() || IconsDirectory.Path.IsEmpty())
	{
		return TSoftObjectPtr<UTexture2D>();
	}

	FString TextureName = KeyToTextureName(Key);
	FString AssetPath = FString::Printf(TEXT("%s/%s.%s"),
		*IconsDirectory.Path,
		*TextureName,
		*TextureName);

	FSoftObjectPath SoftPath(AssetPath);
	TSoftObjectPtr<UTexture2D> TexturePtr;
	TexturePtr = TSoftObjectPtr<UTexture2D>(SoftPath);

	return TexturePtr;
}

FString UInputIconsDataAsset::KeyToTextureName(const FKey& Key) const
{
	// Get key name and build texture name
	FString KeyName = Key.ToString();

	// Handle special cases for cleaner names
	// EKeys::SpaceBar -> Space
	// EKeys::LeftShift -> LeftShift (keep as-is)
	// EKeys::A -> A

	if (KeyName == TEXT("SpaceBar"))
	{
		KeyName = TEXT("Space");
	}
	else if (KeyName == TEXT("LeftMouseButton"))
	{
		KeyName = TEXT("MouseLeft");
	}
	else if (KeyName == TEXT("RightMouseButton"))
	{
		KeyName = TEXT("MouseRight");
	}
	else if (KeyName == TEXT("MiddleMouseButton"))
	{
		KeyName = TEXT("MouseMiddle");
	}
	else if (KeyName == TEXT("BackSpace"))
	{
		KeyName = TEXT("Backspace");
	}

	return TexturePrefix + KeyName;
}

#if WITH_EDITOR
void UInputIconsDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Invalidate cache when properties change in editor
	bCacheBuilt = false;
	CachedKeyToIconMap.Empty();
}
#endif