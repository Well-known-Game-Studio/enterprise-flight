// Slonk - the project's own global shaders. A module of its own because shaders must be known before the engine
// compiles its global shader map, so it loads at PostConfigInit, ahead of the game module.

using UnrealBuildTool;

public class SlonkShaders : ModuleRules
{
	public SlonkShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "RenderCore", "RHI", "Renderer" });
		PrivateDependencyModuleNames.AddRange(new string[] { "CoreUObject", "Engine", "Projects" });
	}
}
