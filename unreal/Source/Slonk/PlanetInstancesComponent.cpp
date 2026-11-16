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
	 * instances come in when it enters the scene and go when it leaves. They are kept on the GPU alone: a primitive's
	 * data waits here only until the next write uploads it, and a buffer outgrown is copied into its successor there.
	 */
	class FPlanetInstanceStore
	{
	public:
		static FPlanetInstanceStore& Get()
		{
			static FPlanetInstanceStore Store;
			return Store;
		}

		void Register(const FPrimitiveSceneProxy* Proxy, TArray<FVector4f>&& Data, int32 NumInstances, int32 NumCustomFloats)
		{
			check(IsInRenderingThread());
			Unregister(Proxy);
			if (NumInstances <= 0 || Data.Num() == 0) { return; }
			const uint32 Size = (uint32)Data.Num();
			const uint32 Offset = Allocate(Size);
			Pending.Add({ Offset, MoveTemp(Data) });
			Index.Add(Proxy, Entries.Add({ Proxy, Offset, Size, (uint32)NumInstances, (uint32)FMath::Max(NumCustomFloats, 0) }));
		}

		void Unregister(const FPrimitiveSceneProxy* Proxy)
		{
			check(IsInRenderingThread());
			const int32* Found = Index.Find(Proxy);
			if (!Found) { return; }
			const int32 At = *Found;
			const uint32 Offset = Entries[At].Offset;
			// Its data not uploaded yet goes with it, so every upload waiting is a live primitive's (and none overlap).
			Pending.RemoveAll([Offset](const FUpload& U) { return U.Offset == Offset; });
			Release(Offset, Entries[At].Size);
			Index.Remove(Proxy);
			Entries.RemoveAtSwap(At, EAllowShrinking::No);
			if (Entries.IsValidIndex(At)) { Index.Add(Entries[At].Proxy, At); }
			// The last one gone (a world torn down): everything goes, the GPU buffer before the renderer does.
			if (Entries.Num() == 0)
			{
				Index.Reset();
				Pending.Empty();
				Free.Empty();
				End = 0;
				Buffer.SafeRelease();
				Capacity = 0;
				LastFrame.Reset();
			}
		}

		/** Every registered primitive of Scene, written into the GPU scene with its primitive's transform of the frame. */
		void Write(FRDGBuilder& GraphBuilder, const FGPUSceneWriteDelegateParams& Params, const FSceneInterface* Scene)
		{
			check(IsInRenderingThread());
			uint64& Last = LastFrame.FindOrAdd(Scene, MAX_uint64);
			if (Last == GFrameCounterRenderThread || End == 0) { return; }
			Last = GFrameCounterRenderThread;

			FRDGBufferRef Source = UploadSource(GraphBuilder);

			// The primitives of this scene that are in the GPU scene, with their instances numbered over all of them. The
			// pass takes its layout from the primitive carrying it, so one made with another count of custom floats (whose
			// stride and payload differ) is left out.
			TArray<FUintVector4> Prims;
			Prims.Reserve(Entries.Num());
			uint32 Total = 0;
			bool bUnlike = false;
			for (const FEntry& E : Entries)
			{
				if (&E.Proxy->GetScene() != Scene) { continue; }
				if (E.NumCustomFloats != (uint32)Params.NumCustomDataFloats) { bUnlike = true; continue; }
				const FPrimitiveSceneInfo* Info = E.Proxy->GetPrimitiveSceneInfo();
				if (!Info || Info->GetInstanceSceneDataOffset() == INDEX_NONE) { continue; }
				const uint32 N = FMath::Min<uint32>(E.NumInstances, (uint32)FMath::Max(Info->GetNumInstanceSceneDataEntries(), 0));
				if (N == 0) { continue; }
				Prims.Add(FUintVector4((uint32)Info->GetPersistentIndex().Index, (uint32)Info->GetInstanceSceneDataOffset(), E.Offset, Total));
				Total += N;
			}
			ensureMsgf(!bUnlike, TEXT("Planet instances with a count of custom floats other than the writer's (%d) are not written"), Params.NumCustomDataFloats);
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
			uint32 NumCustomFloats = 0;
		};
		struct FRange
		{
			uint32 Offset = 0;
			uint32 Size = 0;
		};
		/** A primitive's data on its way to its range of the buffer. */
		struct FUpload
		{
			uint32 Offset = 0;
			TArray<FVector4f> Data;
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
			const uint32 Offset = End;
			End += Size;
			return Offset;
		}

		/** Back to the free ranges (kept in order, neighbours merged); a free range at the end gives the end back. */
		void Release(uint32 Offset, uint32 Size)
		{
			int32 At = 0;
			while (At < Free.Num() && Free[At].Offset < Offset) { ++At; }
			Free.Insert({ Offset, Size }, At);
			if (At + 1 < Free.Num() && Free[At].Offset + Free[At].Size == Free[At + 1].Offset) { Free[At].Size += Free[At + 1].Size; Free.RemoveAt(At + 1); }
			if (At > 0 && Free[At - 1].Offset + Free[At - 1].Size == Free[At].Offset) { Free[At - 1].Size += Free[At].Size; Free.RemoveAt(At); }
			if (Free.Last().Offset + Free.Last().Size == End) { End = Free.Last().Offset; Free.Pop(EAllowShrinking::No); }
		}

		/** The source buffer: grown to fit if outgrown (what it held copied over on the GPU), and the primitives come in
		 *  since the last write uploaded into their ranges. */
		FRDGBufferRef UploadSource(FRDGBuilder& GraphBuilder)
		{
			FRDGBufferRef Source = nullptr;
			if (!Buffer.IsValid() || Capacity < End)
			{
				// What the old one held that is still in use (a range freed past the end since is not).
				const uint32 Held = Buffer.IsValid() ? FMath::Min(Capacity, End) : 0;
				Capacity = FMath::Max3(End, Capacity * 2, 1u << 16);
				Source = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), Capacity), TEXT("Slonk.PlanetInstanceSource"));
				if (Held > 0)
				{
					AddCopyBufferPass(GraphBuilder, Source, 0, GraphBuilder.RegisterExternalBuffer(Buffer), 0, (uint64)Held * sizeof(FVector4f));
				}
				Buffer = GraphBuilder.ConvertToExternalBuffer(Source);
			}
			else
			{
				Source = GraphBuilder.RegisterExternalBuffer(Buffer);
			}
			if (Pending.Num() > 0)
			{
				uint32 Staged = 0;
				for (const FUpload& U : Pending) { Staged += (uint32)U.Data.Num(); }
				TArray<FVector4f> Data;
				Data.Reserve(Staged);
				for (const FUpload& U : Pending) { Data.Append(U.Data); }
				FRDGBufferRef Upload = CreateStructuredBuffer(GraphBuilder, TEXT("Slonk.PlanetInstanceUpload"), sizeof(FVector4f), Data.Num(), Data.GetData(), (uint64)Data.Num() * sizeof(FVector4f), ERDGInitialDataFlags::None);
				uint64 From = 0;
				for (const FUpload& U : Pending)
				{
					AddCopyBufferPass(GraphBuilder, Source, (uint64)U.Offset * sizeof(FVector4f), Upload, From * sizeof(FVector4f), (uint64)U.Data.Num() * sizeof(FVector4f));
					From += U.Data.Num();
				}
				Pending.Reset();
			}
			return Source;
		}

		TArray<FEntry> Entries;
		TMap<const FPrimitiveSceneProxy*, int32> Index;
		TArray<FUpload> Pending;
		TArray<FRange> Free;
		/** Where the ranges in use end: every free range lies before it. */
		uint32 End = 0;
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
		TPlanetInstancesProxy(TArray<FVector4f>&& InPacked, int32 InNumInstances, int32 InNumCustomFloats, float InInstanceRadius, TArgs&&... Args)
			: TBase(Forward<TArgs>(Args)...), Packed(MoveTemp(InPacked)), NumInstances(InNumInstances), NumCustomFloats(InNumCustomFloats), InstanceRadius(InInstanceRadius)
		{
		}
		/** The radius the renderer narrows the set's range of LODs by before the GPU picks each instance's: the engine's
		 *  is the unit mesh's, unscaled, so a set of trees ten and twenty times that size more than a hundred metres off
		 *  would get its coarsest LOD alone. The set's largest instance, as the GPU bounds it. */
		virtual float GetGpuLodInstanceRadius() const override
		{
			const float Base = TBase::GetGpuLodInstanceRadius();
			return Base > 0.f ? FMath::Max(Base, InstanceRadius) : Base;
		}
		virtual ~TPlanetInstancesProxy() override
		{
			if (IsInRenderingThread()) { FPlanetInstanceStore::Get().Unregister(this); }
		}
		virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override
		{
			TBase::CreateRenderThreadResources(RHICmdList);
			FPlanetInstanceStore::Get().Register(this, MoveTemp(Packed), NumInstances, NumCustomFloats);
		}
		virtual void DestroyRenderThreadResources() override
		{
			FPlanetInstanceStore::Get().Unregister(this);
			TBase::DestroyRenderThreadResources();
		}

	private:
		TArray<FVector4f> Packed;
		int32 NumInstances = 0;
		int32 NumCustomFloats = 0;
		float InstanceRadius = 0.f;
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
	// The set's box from its instances' (as padded as the GPU culls them by), so the set is never culled whole where one
	// of them is seen.
	const FBox Mesh = InstanceBox();
	// And its largest instance's radius as the GPU picks LODs by (the sphere round the instance's box), so the renderer
	// leaves the GPU every LOD it may want.
	const double MeshRadius = Mesh.GetExtent().Length();
	MaxInstanceRadius = 0.0;
	for (int32 i = 0; i < NumInstances; ++i)
	{
		const FTransform& T = Transforms[i];
		LocalBox += Mesh.TransformBy(T);
		MaxInstanceRadius = FMath::Max(MaxInstanceRadius, MeshRadius * T.GetMaximumAxisScale());
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
	MaxInstanceRadius = 0.0;
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
		View.InstanceLocalBounds.SetNum(1);
		View.InstanceLocalBounds[0] = FRenderBounds(InstanceBox());
		View.Flags.bHasPerInstanceCustomData = NumCustomFloats > 0;
		Buffers.EndWriteAccess(AccessTag);
		Buffers.ValidateData();
	}

	FInstancedStaticMeshSceneProxyDesc Desc;
	Desc.InitializeFromStaticMeshComponent(this);
	Desc.InstanceDataSceneProxy = MakeShared<FInstanceDataSceneProxy, ESPMode::ThreadSafe>(MoveTemp(Buffers));
	Desc.bUseGpuLodSelection = true;

	TArray<FVector4f> Copy = Packed;
	const float Radius = (float)(MaxInstanceRadius * GetComponentTransform().GetMaximumAxisScale());
	if (bCreateNanite)
	{
		return ::new TPlanetInstancesProxy<Nanite::FSceneProxy>(MoveTemp(Copy), NumInstances, NumCustomFloats, Radius, NaniteMaterials, Desc);
	}
	return ::new TPlanetInstancesProxy<FInstancedStaticMeshSceneProxy>(MoveTemp(Copy), NumInstances, NumCustomFloats, Radius, Desc, World->GetFeatureLevel());
}

void UPlanetInstancesComponent::PrecachePSOs()
{
	// The one carrying the writes is never drawn, so it needs no pipeline states: precaching none, its primitive is made
	// at once rather than waiting on them (every set's instances wait on it).
	if (bCarriesWrites) { return; }
	Super::PrecachePSOs();
}

FBox UPlanetInstancesComponent::InstanceBox() const
{
	const FBox Mesh = GetStaticMesh() ? GetStaticMesh()->GetBounds().GetBox() : FBox(FVector(-50.0), FVector(50.0));
	return Mesh.ExpandBy(Mesh.GetExtent() * 0.15 + FVector(20.0));
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
