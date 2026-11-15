#include "PlanetInstancesComponent.h"

#include "PlanetInstanceShaders.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/InstancedStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GlobalShader.h"
#include "GPUSceneWriter.h"
#include "InstanceDataSceneProxy.h"
#include "InstancedStaticMeshSceneProxyDesc.h"
#include "NaniteSceneProxy.h"
#include "PrimitiveSceneDesc.h"
#include "PrimitiveSceneInfo.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PlanetInstancesComponent)

namespace
{
	/**
	 * The planet instances on the GPU (render thread only): every registered primitive's instances in its own frame, in
	 * one buffer, written into the GPU scene once a frame for every primitive of a scene in one dispatch. A primitive's
	 * instances come in when it enters the scene and go when it leaves; a CPU copy is kept so a range can be put back
	 * where it belongs when the buffer grows.
	 */
	class FPlanetInstanceStore
	{
	public:
		static FPlanetInstanceStore& Get()
		{
			static FPlanetInstanceStore Store;
			return Store;
		}

		void Register(const FPrimitiveSceneProxy* Proxy, TArray<FVector4f>&& Data, int32 NumInstances)
		{
			check(IsInRenderingThread());
			Unregister(Proxy);
			if (NumInstances <= 0 || Data.Num() == 0) { return; }
			const uint32 Size = (uint32)Data.Num();
			const uint32 Offset = Allocate(Size);
			FMemory::Memcpy(&Mirror[Offset], Data.GetData(), Size * sizeof(FVector4f));
			Dirty.Add({ Offset, Size });
			Index.Add(Proxy, Entries.Add({ Proxy, Offset, Size, (uint32)NumInstances }));
		}

		void Unregister(const FPrimitiveSceneProxy* Proxy)
		{
			check(IsInRenderingThread());
			const int32* Found = Index.Find(Proxy);
			if (!Found) { return; }
			const int32 At = *Found;
			Release(Entries[At].Offset, Entries[At].Size);
			Index.Remove(Proxy);
			Entries.RemoveAtSwap(At, EAllowShrinking::No);
			if (Entries.IsValidIndex(At)) { Index.Add(Entries[At].Proxy, At); }
			// The last one gone (a world torn down): everything goes, the GPU buffer before the renderer does.
			if (Entries.Num() == 0)
			{
				Index.Reset();
				Mirror.Empty();
				Free.Empty();
				Dirty.Empty();
				Buffer.SafeRelease();
				Capacity = 0;
			}
		}

		/** Every registered primitive of Scene, written into the GPU scene with its primitive's transform of the frame. */
		void Write(FRDGBuilder& GraphBuilder, const FGPUSceneWriteDelegateParams& Params, const FSceneInterface* Scene)
		{
			check(IsInRenderingThread());
			uint64& Last = LastFrame.FindOrAdd(Scene, MAX_uint64);
			if (Last == GFrameCounterRenderThread || Mirror.Num() == 0) { return; }
			Last = GFrameCounterRenderThread;

			FRDGBufferRef Source = UploadSource(GraphBuilder);

			// The primitives of this scene that are in the GPU scene, with their instances numbered over all of them.
			TArray<FUintVector4> Prims;
			Prims.Reserve(Entries.Num());
			uint32 Total = 0;
			for (const FEntry& E : Entries)
			{
				if (&E.Proxy->GetScene() != Scene) { continue; }
				const FPrimitiveSceneInfo* Info = E.Proxy->GetPrimitiveSceneInfo();
				if (!Info || Info->GetInstanceSceneDataOffset() == INDEX_NONE) { continue; }
				const uint32 N = FMath::Min<uint32>(E.NumInstances, (uint32)FMath::Max(Info->GetNumInstanceSceneDataEntries(), 0));
				if (N == 0) { continue; }
				Prims.Add(FUintVector4((uint32)Info->GetPersistentIndex().Index, (uint32)Info->GetInstanceSceneDataOffset(), E.Offset, Total));
				Total += N;
			}
			if (Prims.Num() == 0) { return; }

			FRDGBufferRef PrimBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Slonk.PlanetInstancePrimitives"), sizeof(FUintVector4), Prims.Num(), Prims.GetData(), Prims.Num() * sizeof(FUintVector4), ERDGInitialDataFlags::None);
			FPlanetInstanceWriterCS::FParameters* P = GraphBuilder.AllocParameters<FPlanetInstanceWriterCS::FParameters>();
			// Deprecated in name only: with the engine's DX11 workaround on, these loose parameters are how it binds (as
			// Niagara's and PCG's writers do).
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			P->GPUSceneWriterParameters = Params.GPUWriteParams;
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
			P->Primitives = GraphBuilder.CreateSRV(PrimBuffer);
			P->Source = GraphBuilder.CreateSRV(Source);
			P->NumPrimitives = (uint32)Prims.Num();
			P->TotalInstances = Total;
			// All of these primitives are made alike (custom data only, the scatter's floats), as the one carrying the pass is.
			P->InstanceFlags = Params.PackedInstanceSceneDataFlags;
			P->NumCustomFloats = Params.NumCustomDataFloats;
			TShaderMapRef<FPlanetInstanceWriterCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Slonk.PlanetInstances %u in %d", Total, Prims.Num()), ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				Shader, P, FComputeShaderUtils::GetGroupCountWrapped(Total, FPlanetInstanceWriterCS::ThreadGroupSize));
		}

	private:
		struct FEntry
		{
			const FPrimitiveSceneProxy* Proxy = nullptr;
			uint32 Offset = 0;
			uint32 Size = 0;
			uint32 NumInstances = 0;
		};
		struct FRange
		{
			uint32 Offset = 0;
			uint32 Size = 0;
		};

		/** The first free range big enough, else more at the end. */
		uint32 Allocate(uint32 Size)
		{
			for (int32 i = 0; i < Free.Num(); ++i)
			{
				if (Free[i].Size < Size) { continue; }
				const uint32 Offset = Free[i].Offset;
				Free[i].Offset += Size;
				Free[i].Size -= Size;
				if (Free[i].Size == 0) { Free.RemoveAt(i); }
				return Offset;
			}
			const uint32 Offset = (uint32)Mirror.Num();
			Mirror.AddUninitialized(Size);
			return Offset;
		}

		/** Back to the free ranges (kept in order, neighbours merged). */
		void Release(uint32 Offset, uint32 Size)
		{
			int32 At = 0;
			while (At < Free.Num() && Free[At].Offset < Offset) { ++At; }
			Free.Insert({ Offset, Size }, At);
			if (At + 1 < Free.Num() && Free[At].Offset + Free[At].Size == Free[At + 1].Offset) { Free[At].Size += Free[At + 1].Size; Free.RemoveAt(At + 1); }
			if (At > 0 && Free[At - 1].Offset + Free[At - 1].Size == Free[At].Offset) { Free[At - 1].Size += Free[At].Size; Free.RemoveAt(At); }
		}

		/** The source buffer, grown to fit (and filled again whole) or with the ranges changed since last frame copied in. */
		FRDGBufferRef UploadSource(FRDGBuilder& GraphBuilder)
		{
			const uint32 Needed = (uint32)Mirror.Num();
			if (!Buffer.IsValid() || Capacity < Needed)
			{
				Capacity = FMath::Max3(Needed, Capacity * 2, 1u << 16);
				FRDGBufferRef Source = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), Capacity), TEXT("Slonk.PlanetInstanceSource"));
				GraphBuilder.QueueBufferUpload(Source, Mirror.GetData(), (uint64)Needed * sizeof(FVector4f), ERDGInitialDataFlags::None);
				Buffer = GraphBuilder.ConvertToExternalBuffer(Source);
				Dirty.Reset();
				return Source;
			}
			FRDGBufferRef Source = GraphBuilder.RegisterExternalBuffer(Buffer);
			if (Dirty.Num() > 0)
			{
				uint32 Staged = 0;
				for (const FRange& R : Dirty) { Staged += R.Size; }
				TArray<FVector4f> Data;
				Data.Reserve(Staged);
				for (const FRange& R : Dirty) { Data.Append(&Mirror[R.Offset], R.Size); }
				FRDGBufferRef Upload = CreateStructuredBuffer(GraphBuilder, TEXT("Slonk.PlanetInstanceUpload"), sizeof(FVector4f), Data.Num(), Data.GetData(), (uint64)Data.Num() * sizeof(FVector4f), ERDGInitialDataFlags::None);
				uint64 From = 0;
				for (const FRange& R : Dirty)
				{
					AddCopyBufferPass(GraphBuilder, Source, (uint64)R.Offset * sizeof(FVector4f), Upload, From * sizeof(FVector4f), (uint64)R.Size * sizeof(FVector4f));
					From += R.Size;
				}
				Dirty.Reset();
			}
			return Source;
		}

		TArray<FEntry> Entries;
		TMap<const FPrimitiveSceneProxy*, int32> Index;
		TArray<FVector4f> Mirror;
		TArray<FRange> Free;
		TArray<FRange> Dirty;
		TRefCountPtr<FRDGPooledBuffer> Buffer;
		uint32 Capacity = 0;
		TMap<const FSceneInterface*, uint64> LastFrame;
	};

	/** A proxy of either kind the engine makes for instances, whose instances join the store while it is in the scene. */
	template <typename TBase>
	class TPlanetInstancesProxy : public TBase
	{
	public:
		template <typename... TArgs>
		TPlanetInstancesProxy(TArray<FVector4f>&& InPacked, int32 InNumInstances, TArgs&&... Args)
			: TBase(Forward<TArgs>(Args)...), Packed(MoveTemp(InPacked)), NumInstances(InNumInstances)
		{
		}
		virtual ~TPlanetInstancesProxy() override
		{
			if (IsInRenderingThread()) { FPlanetInstanceStore::Get().Unregister(this); }
		}
		virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override
		{
			TBase::CreateRenderThreadResources(RHICmdList);
			FPlanetInstanceStore::Get().Register(this, MoveTemp(Packed), NumInstances);
		}
		virtual void DestroyRenderThreadResources() override
		{
			FPlanetInstanceStore::Get().Unregister(this);
			TBase::DestroyRenderThreadResources();
		}

	private:
		TArray<FVector4f> Packed;
		int32 NumInstances = 0;
	};
}

UPlanetInstancesComponent::UPlanetInstancesComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// What GPU-only instance data cannot do (the engine's own GPU-only instances turn these off too): the Lumen scene
	// and the distance fields read every instance on the CPU.
	bAffectDynamicIndirectLighting = false;
	bAffectDistanceFieldLighting = false;
	bVisibleInRayTracing = false;
	BodyInstance.bSimulatePhysics = false;
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	bNavigationRelevant = false;
	bCanEverAffectNavigation = false;
	Mobility = EComponentMobility::Movable;
}

void UPlanetInstancesComponent::SetNumCustomDataFloats(int32 InNumCustomDataFloats)
{
	InNumCustomDataFloats = FMath::Max(InNumCustomDataFloats, 0);
	if (InNumCustomDataFloats == NumCustomFloats) { return; }
	NumCustomFloats = InNumCustomDataFloats;
	ClearPieces();
}

void UPlanetInstancesComponent::SetPieces(const TArray<FTransform>& Transforms, TConstArrayView<float> CustomData)
{
	const int32 Custom4 = (NumCustomFloats + 3) / 4;
	const int32 Stride = 3 + Custom4;
	const bool bCustom = NumCustomFloats > 0 && CustomData.Num() == Transforms.Num() * NumCustomFloats;
	NumInstances = Transforms.Num();
	Packed.SetNumUninitialized(NumInstances * Stride);
	LocalBox.Init();
	const FBox Mesh = GetStaticMesh() ? GetStaticMesh()->GetBounds().GetBox() : FBox(FVector(-50.0), FVector(50.0));
	for (int32 i = 0; i < NumInstances; ++i)
	{
		const FTransform& T = Transforms[i];
		LocalBox += Mesh.TransformBy(T);
		const FMatrix44f M(T.ToMatrixWithScale());
		FVector4f* Out = &Packed[i * Stride];
		Out[0] = FVector4f(M.M[0][0], M.M[0][1], M.M[0][2], M.M[3][0]);
		Out[1] = FVector4f(M.M[1][0], M.M[1][1], M.M[1][2], M.M[3][1]);
		Out[2] = FVector4f(M.M[2][0], M.M[2][1], M.M[2][2], M.M[3][2]);
		for (int32 c = 0; c < Custom4; ++c)
		{
			float V[4] = { 0.f, 0.f, 0.f, 0.f };
			for (int32 k = 0; k < 4; ++k)
			{
				const int32 F = c * 4 + k;
				if (bCustom && F < NumCustomFloats) { V[k] = CustomData[i * NumCustomFloats + F]; }
			}
			Out[3 + c] = FVector4f(V[0], V[1], V[2], V[3]);
		}
	}
	MarkRenderStateDirty();
	UpdateBounds();
}

void UPlanetInstancesComponent::ClearPieces()
{
	if (NumInstances == 0 && Packed.Num() == 0) { return; }
	NumInstances = 0;
	Packed.Reset();
	LocalBox.Init();
	MarkRenderStateDirty();
	UpdateBounds();
}

void UPlanetInstancesComponent::QueueWriteAll()
{
	UWorld* World = GetWorld();
	FSceneInterface* Scene = World ? World->Scene : nullptr;
	if (!Scene || !SceneProxy) { return; }
	FPrimitiveSceneDesc Desc;
	Desc.SceneProxy = SceneProxy;
	// Marked experimental in 5.8: the engine's own GPU-only instances (PCG's, Niagara's) are written through it too.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Scene->UpdatePrimitiveInstancesFromCompute(&Desc, FGPUSceneWriteDelegate::CreateLambda(
		[Scene](FRDGBuilder& GraphBuilder, const FGPUSceneWriteDelegateParams& Params)
		{
			FPlanetInstanceStore::Get().Write(GraphBuilder, Params, Scene);
		}));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FPrimitiveSceneProxy* UPlanetInstancesComponent::CreateSceneProxy()
{
	if (NumInstances <= 0 || !GetStaticMesh()) { return nullptr; }
	return Super::CreateSceneProxy();
}

FPrimitiveSceneProxy* UPlanetInstancesComponent::CreateStaticMeshSceneProxy(Nanite::FMaterialAudit& NaniteMaterials, bool bCreateNanite)
{
	UWorld* World = GetWorld();
	if (!World || !World->Scene || !UseGPUScene(World->Scene->GetShaderPlatform())) { return nullptr; }
	if (CheckPSOPrecachingAndBoostPriority() && GetPSOPrecacheProxyCreationStrategy() == EPSOPrecacheProxyCreationStrategy::DelayUntilPSOPrecached) { return nullptr; }

	FInstanceSceneDataBuffers Buffers(/*InbInstanceDataIsGPUOnly=*/true);
	{
		FInstanceSceneDataBuffers::FAccessTag AccessTag(PointerHash(this));
		FInstanceSceneDataBuffers::FWriteView View = Buffers.BeginWriteAccess(AccessTag);
		Buffers.SetPrimitiveLocalToWorld(GetRenderMatrix(), AccessTag);
		View.NumInstancesGPUOnly = NumInstances;
		View.NumCustomDataFloats = NumCustomFloats;
		// One box for all, the mesh's, padded for the wind the plants sway in.
		const FBox MeshBox = GetStaticMesh()->GetBounds().GetBox();
		View.InstanceLocalBounds.SetNum(1);
		View.InstanceLocalBounds[0] = FRenderBounds(MeshBox.ExpandBy(MeshBox.GetExtent() * 0.15 + FVector(20.0)));
		View.Flags.bHasPerInstanceCustomData = NumCustomFloats > 0;
		Buffers.EndWriteAccess(AccessTag);
		Buffers.ValidateData();
	}

	FInstancedStaticMeshSceneProxyDesc Desc;
	Desc.InitializeFromStaticMeshComponent(this);
	Desc.InstanceDataSceneProxy = MakeShared<FInstanceDataSceneProxy, ESPMode::ThreadSafe>(MoveTemp(Buffers));
	Desc.bUseGpuLodSelection = true;

	TArray<FVector4f> Copy = Packed;
	if (bCreateNanite)
	{
		return ::new TPlanetInstancesProxy<Nanite::FSceneProxy>(MoveTemp(Copy), NumInstances, NaniteMaterials, Desc);
	}
	return ::new TPlanetInstancesProxy<FInstancedStaticMeshSceneProxy>(MoveTemp(Copy), NumInstances, Desc, World->GetFeatureLevel());
}

FMatrix UPlanetInstancesComponent::GetRenderMatrix() const
{
	return GetComponentTransform().ToMatrixWithScale();
}

FBoxSphereBounds UPlanetInstancesComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!LocalBox.IsValid) { return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0); }
	FBoxSphereBounds B(LocalBox.TransformBy(LocalToWorld));
	B.BoxExtent *= BoundsScale;
	B.SphereRadius *= BoundsScale;
	return B;
}

bool UPlanetInstancesComponent::GetMaterialStreamingData(int32 MaterialIndex, FPrimitiveMaterialInfo& MaterialData) const
{
	// The mesh's, over the whole set's box (as PCG's procedural instances do).
	if (GetStaticMesh())
	{
		MaterialData.Material = GetMaterial(MaterialIndex);
		MaterialData.UVChannelData = GetStaticMesh()->GetUVChannelData(MaterialIndex);
		MaterialData.PackedRelativeBox = PackedRelativeBox_Identity;
	}
	return MaterialData.IsValid();
}
