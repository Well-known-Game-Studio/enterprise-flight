#include "PlanetInstanceShaders.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "SceneManagement.h"

bool FPlanetInstanceWriterCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	// The GPU scene is what it writes to.
	return UseGPUScene(Parameters.Platform);
}

void FPlanetInstanceWriterCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
	OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), ThreadGroupSize);
}

IMPLEMENT_GLOBAL_SHADER(FPlanetInstanceWriterCS, "/Slonk/PlanetInstanceWriter.usf", "WritePlanetInstancesCS", SF_Compute);
