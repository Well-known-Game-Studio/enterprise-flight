#include "SlonkMaterials.h"

#include "Materials/MaterialInterface.h"

UMaterialInterface* SlonkMaterials::LoadOrFallback(const TSoftObjectPtr<UMaterialInterface>& Material, const TCHAR* DebugName, bool bLitFallback)
{
	if (UMaterialInterface* Loaded = Material.LoadSynchronous())
	{
		return Loaded;
	}
	UE_LOG(LogTemp, Warning, TEXT("Slonk: material '%s' for %s not found, using engine fallback."), *Material.ToString(), DebugName);
	return LoadObject<UMaterialInterface>(nullptr, bLitFallback ? FallbackLitPath : FallbackUnlitPath);
}
