#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

class FSlonkShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The project's shaders as /Slonk/...: unreal/Shaders.
		AddShaderSourceDirectoryMapping(TEXT("/Slonk"), FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders")));
	}
};

IMPLEMENT_MODULE(FSlonkShadersModule, SlonkShaders);
