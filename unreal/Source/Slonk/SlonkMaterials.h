// Slonk - central list of the project's effect materials and a safe loader with engine fallbacks.
// The materials themselves are authored in the editor (see Content/Slonk/Materials); the game never
// hard-fails if one is missing, it just looks worse and logs a warning.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

namespace SlonkMaterials
{
	inline const TCHAR* WarpStreakPath = TEXT("/Game/Slonk/Materials/M_WarpStreak.M_WarpStreak");
	inline const TCHAR* PhaserBeamPath = TEXT("/Game/Slonk/Materials/M_PhaserBeam.M_PhaserBeam");
	inline const TCHAR* TorpedoPath = TEXT("/Game/Slonk/Materials/M_Torpedo.M_Torpedo");
	inline const TCHAR* ExplosionPath = TEXT("/Game/Slonk/Materials/M_Explosion.M_Explosion");
	inline const TCHAR* AsteroidPath = TEXT("/Game/Slonk/Materials/M_Asteroid.M_Asteroid");
	inline const TCHAR* HullPath = TEXT("/Game/Slonk/Materials/M_Hull.M_Hull");
	inline const TCHAR* GlowPath = TEXT("/Game/Slonk/Materials/M_Glow.M_Glow");
	inline const TCHAR* ShockwavePath = TEXT("/Game/Slonk/Materials/M_Shockwave.M_Shockwave");
	inline const TCHAR* ShieldPath = TEXT("/Game/Slonk/Materials/M_Shield.M_Shield");
	inline const TCHAR* TractorBeamPath = TEXT("/Game/Slonk/Materials/M_TractorBeam.M_TractorBeam");
	inline const TCHAR* BorgBeamPath = TEXT("/Game/Slonk/Materials/M_BorgBeam.M_BorgBeam");

	/**
	 * Primitives further than this (cm) from the world origin lose GPU matrix precision (the engine's
	 * double-float relative origin only holds ~2^21 cm of residual, which a float position past ~1.1 AU
	 * cannot provide) and trigger the "precision loss converting matrix" ensure. Bodies hide themselves
	 * beyond it; the sun is covered by UDistantStarsComponent.
	 */
	inline const double RenderHorizonCm = 1.2e13;

	/** Unlit engine material used when a project material is missing. */
	inline const TCHAR* FallbackUnlitPath = TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial");
	/** Lit engine material used when a project material is missing. */
	inline const TCHAR* FallbackLitPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	SLONK_API UMaterialInterface* LoadOrFallback(const TSoftObjectPtr<UMaterialInterface>& Material, const TCHAR* DebugName, bool bLitFallback = false);
}
