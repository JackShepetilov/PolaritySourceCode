// LootOutline.h
// Every pickup lying around is outlined in its rarity colour, the way Apex outlines loot (the author,
// 2026-09-26). Things without a rarity (money, ammo, a plain gun) take the Common colour.
//
// How (the standard Unreal way, after Tom Looman's multi-colour outline): each pickup's meshes write a
// custom stencil, one value per rarity (StencilBase + rarity), and one post process material draws a
// line where that stencil ends, with a soft glow around it. The line colours are the material's
// parameters, filled from the palette (ULootCardWidget::GetRarityColor).
//
// The material lives in an unbound post process volume spawned for the local player, NOT on the
// player's camera: the berserk filter zeroes the camera's post process weight whenever it is off, and
// aiming down sights hands the view to the weapon's camera. Either one hid the outline completely.
//
// Purely local: the stencil is a render setting and every machine sets its own, for its own player.
// The rarity comes from ULootCardWidget::BuildData, so the outline and the card never disagree.
// Driven by AShooterCharacter::UpdateGrappleFetchAiming on the owning client.

#pragma once

#include "CoreMinimal.h"

class AShooterCharacter;
class UMaterialInterface;
class UMaterialInstanceDynamic;

namespace LootOutline
{
	/** Stencil of a Common item; Rare, Epic and Legendary follow. Clear of UOutlineComponent's 1-4. */
	constexpr int32 StencilBase = 20;

	/** Spawn an unbound post process volume, owned by Viewer and local to this machine, carrying an
	 *  instance of Material with its colours filled from the palette. Returns the instance so the
	 *  caller can tell it is done. */
	POLARITY_API UMaterialInstanceDynamic* InstallPostProcess(AShooterCharacter* Viewer, UMaterialInterface* Material);

	/** Destroy the volume InstallPostProcess spawned for Viewer. */
	POLARITY_API void UninstallPostProcess(AShooterCharacter* Viewer);

	/** Set the stencil on every fetchable in Viewer's world. Cheap enough for a few times a second. */
	POLARITY_API void Refresh(const AShooterCharacter* Viewer);
}
