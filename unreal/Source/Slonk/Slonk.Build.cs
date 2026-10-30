// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Slonk : ModuleRules
{
	public Slonk(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Niagara", "GeometryCore", "GeometryFramework", "GeometryScriptingCore", "RHI", "MeshDescription", "StaticMeshDescription", "MeshConversion", "PhysicsCore", "UMG", "Slate", "SlateCore", "CommonUI", "CommonInput" });

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
