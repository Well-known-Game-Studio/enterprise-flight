// Slonk - the compute shader that writes the planets' GPU-only instances into the GPU scene (PlanetInstanceWriter.usf).
// Everything on a world turns with it, and the engine keeps an instance's transform in the GPU scene with its
// primitive's already in it, so a turning primitive had every instance re-sent from the CPU each frame. These
// instances live on the GPU in their primitive's own frame instead, and this pass multiplies each by its primitive's
// transform of the frame: all the planets' instances in one dispatch (UPlanetInstancesComponent, FPlanetInstanceStore).

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
// Before the writer's parameters: SceneDefinitions.h turns on the engine's DX11 workaround (on every RHI in 5.8),
// which lays the GPU scene writer out as loose parameters (FGPUSceneWriterParameters), not its uniform buffer.
#include "SceneDefinitions.h"
#include "GPUSceneWriter.h"

class FPlanetInstanceWriterCS : public FGlobalShader
{
	DECLARE_EXPORTED_GLOBAL_SHADER(FPlanetInstanceWriterCS, SLONKSHADERS_API);
	SHADER_USE_PARAMETER_STRUCT(FPlanetInstanceWriterCS, FGlobalShader);

public:
	static constexpr uint32 ThreadGroupSize = 64;

	static SLONKSHADERS_API bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static SLONKSHADERS_API void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, SLONKSHADERS_API)
		// The loose writer parameters the engine's own writers use while ENABLE_SCENE_DATA_DX11_UB_ERROR_WORKAROUND is on.
		SHADER_PARAMETER_STRUCT_INCLUDE(FGPUSceneWriterParameters, GPUSceneWriterParameters)
		/** Per primitive: its persistent primitive id, its first instance in the GPU scene, its first float4 in Source,
		 *  and its first instance counted over all the primitives (ascending, for the search). */
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FUintVector4>, Primitives)
		/** Per instance: its transform in its primitive's frame (three float4 rows of a 3x4, translation in w), then its
		 *  custom data, a float4 at a time. */
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Source)
		SHADER_PARAMETER(uint32, NumPrimitives)
		SHADER_PARAMETER(uint32, TotalInstances)
		/** The packed instance flags every one of these primitives has (they are all made alike). */
		SHADER_PARAMETER(uint32, InstanceFlags)
		SHADER_PARAMETER(uint32, NumCustomFloats)
	END_SHADER_PARAMETER_STRUCT()
};
