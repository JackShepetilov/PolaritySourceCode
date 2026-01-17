// EMFSetupHelper.cpp

#include "EMFSetupHelper.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"

void UEMFSetupHelper::SetupPlayerEMF(UEMFVelocityModifier* EMFModifier, UEMF_FieldComponent* FieldComp)
{
	if (FieldComp)
	{
		FieldComp->SetOwnerType(EEMSourceOwnerType::Player);
	}

	if (EMFModifier)
	{
		EMFModifier->SetOwnerType(EEMSourceOwnerType::Player);
		// Player doesn't react to NPC EM forces
		EMFModifier->NPCForceMultiplier = 0.0f;
		// Player reacts to everything else normally
		EMFModifier->PlayerForceMultiplier = 1.0f;
		EMFModifier->ProjectileForceMultiplier = 1.0f;
		EMFModifier->EnvironmentForceMultiplier = 1.0f;
		EMFModifier->UnknownForceMultiplier = 1.0f;
	}
}

void UEMFSetupHelper::SetupNPCEMF(UEMFVelocityModifier* EMFModifier, UEMF_FieldComponent* FieldComp)
{
	if (FieldComp)
	{
		FieldComp->SetOwnerType(EEMSourceOwnerType::NPC);
	}

	if (EMFModifier)
	{
		EMFModifier->SetOwnerType(EEMSourceOwnerType::NPC);
		// NPCs don't react to other NPCs' EM forces
		EMFModifier->NPCForceMultiplier = 0.0f;
		// NPCs react to everything else normally
		EMFModifier->PlayerForceMultiplier = 1.0f;
		EMFModifier->ProjectileForceMultiplier = 1.0f;
		EMFModifier->EnvironmentForceMultiplier = 1.0f;
		EMFModifier->UnknownForceMultiplier = 1.0f;
	}
}

void UEMFSetupHelper::SetupProjectileEMF(UEMFVelocityModifier* EMFModifier, UEMF_FieldComponent* FieldComp)
{
	if (FieldComp)
	{
		FieldComp->SetOwnerType(EEMSourceOwnerType::Projectile);
	}

	if (EMFModifier)
	{
		EMFModifier->SetOwnerType(EEMSourceOwnerType::Projectile);
		// Projectiles react to ALL EM forces
		EMFModifier->NPCForceMultiplier = 1.0f;
		EMFModifier->PlayerForceMultiplier = 1.0f;
		EMFModifier->ProjectileForceMultiplier = 1.0f;
		EMFModifier->EnvironmentForceMultiplier = 1.0f;
		EMFModifier->UnknownForceMultiplier = 1.0f;
	}
}
