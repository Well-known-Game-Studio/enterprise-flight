#include "PlanetTerrainComponent.h"
#include "PlanetCities.h"
#include "GroundRocks.h"
#include "SlonkMaterials.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UDynamicMesh.h"
#include "HAL/IConsoleManager.h"

// The sine of the sun elevation the canyon, rim and river searches prefer, and the half-width of the band that
// earns full marks: low by default so one wall is lit and the other in shadow; raise it for a sunlit floor.
static TAutoConsoleVariable<float> CVarTerrainSunUp(TEXT("slonk.TerrainSunUp"), 0.45f, TEXT("Preferred sine of sun elevation for terrain goto canyon/rim/river"));
static TAutoConsoleVariable<float> CVarTerrainSunUpWidth(TEXT("slonk.TerrainSunUpWidth"), 0.25f, TEXT("Half-width of the preferred band"));

using namespace UE::Geometry;

namespace
{
	/** Period of the material's detail noise, cm. Chunk origins are handed to the shader modulo this, so the
	 *  float it adds to the vertex position never loses the metres. */
	constexpr double NoisePeriodCm = 1048576.0; // 2^20

	/** Back to the pool: empty, hidden and attached to nothing, so the planet's turn no longer moves it each frame. */
	void PoolRocks(UInstancedStaticMeshComponent* R, TArray<UInstancedStaticMeshComponent*>& Free)
	{
		R->ClearInstances();
		R->SetVisibility(false);
		R->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
		Free.Add(R);
	}
}

UPlanetTerrainComponent::UPlanetTerrainComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
	SetUsingAbsoluteScale(true);
}

void UPlanetTerrainComponent::BeginDestroy()
{
	for (FNode* N : InFlight)
	{
		if (N && N->Task.IsValid()) { N->Task.Wait(); }
	}
	InFlight.Reset();
	Super::BeginDestroy();
}

void UPlanetTerrainComponent::Configure(const FPlanetTerrainParams& InParams, UMaterialInterface* InMaterial)
{
	ResetTree();
	Params = InParams;
	Material = InMaterial;
	MID = nullptr;
	if (Material)
	{
		MID = UMaterialInstanceDynamic::Create(Material, this);
		MID->SetScalarParameterValue(TEXT("PlanetRadiusCm"), (float)Params.RadiusCm);
		MID->SetScalarParameterValue(TEXT("ReliefCm"), (float)Params.ReliefCm);
		MID->SetScalarParameterValue(TEXT("OceanDepthCm"), (float)Params.OceanDepthCm);
		MID->SetScalarParameterValue(TEXT("NoisePeriodCm"), (float)NoisePeriodCm);
	}
	for (UDynamicMeshComponent* C : Pool)
	{
		if (C && MID) { C->SetMaterial(0, MID); }
	}
	if (!RockMID)
	{
		// The rock material reads the tint, size and bed from each instance; without it the asteroids' will do.
		UMaterialInterface* RockMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Slonk/Materials/M_GroundRock.M_GroundRock"), nullptr, LOAD_Quiet | LOAD_NoWarn);
		if (!RockMat) { RockMat = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Asteroid.M_Asteroid"))), TEXT("GroundRock"), true); }
		if (RockMat) { RockMID = UMaterialInstanceDynamic::Create(RockMat, this); }
	}
	MakeRoots();
	bConfigured = true;
	UE_LOG(LogTemp, Log, TEXT("PlanetTerrain: configured %s R %.0f km relief %.1f km sea %.2f"), GetOwner() ? *GetOwner()->GetName() : TEXT("?"), Params.RadiusCm / 1e5, Params.ReliefCm / 1e5, Params.SeaLevel);
}

void UPlanetTerrainComponent::MakeRoots()
{
	for (int32 F = 0; F < 6; ++F)
	{
		Roots[F] = MakeUnique<FNode>();
		Roots[F]->Face = F;
		ChunkFrame(F, 0, 0, 0, Roots[F]->Centre, Roots[F]->EdgeCm);
		Roots[F]->Origin = Roots[F]->Centre * Params.RadiusCm;
	}
}

void UPlanetTerrainComponent::SetScatterOptions(bool bEnabled, float Density, bool bCities)
{
	Density = FMath::Max(Density, 0.f);
	if (Scatter.bEnabled == bEnabled && FMath::IsNearlyEqual(Scatter.Density, Density) && Scatter.bCities == bCities) { return; }
	Scatter.bEnabled = bEnabled;
	Scatter.Density = Density;
	Scatter.bCities = bCities;
	// The rocks are built with the chunks: the chunks go round again.
	if (bConfigured)
	{
		ResetTree();
		MakeRoots();
	}
}

void UPlanetTerrainComponent::SetScalarParameter(FName Name, float Value)
{
	if (MID) { MID->SetScalarParameterValue(Name, Value); }
}

void UPlanetTerrainComponent::SetVectorParameter(FName Name, const FLinearColor& Value)
{
	if (MID) { MID->SetVectorParameterValue(Name, Value); }
	// The rocks are tinted from the same palette when their chunk is built, and wear the same snow.
	if (Name == TEXT("MountainColor")) { Scatter.Palette.Mountain = Value; }
	else if (Name == TEXT("LandColor")) { Scatter.Palette.Land = Value; }
	else if (Name == TEXT("LandColor2")) { Scatter.Palette.Land2 = Value; }
	else if (Name == TEXT("IceColor"))
	{
		Scatter.Palette.Ice = Value;
		if (RockMID) { RockMID->SetVectorParameterValue(Name, Value); }
	}
}

void UPlanetTerrainComponent::ResetTree()
{
	for (FNode* N : InFlight)
	{
		if (N && N->Task.IsValid()) { N->Task.Wait(); }
	}
	InFlight.Reset();
	Requests.Reset();
	for (int32 F = 0; F < 6; ++F)
	{
		if (Roots[F]) { ReleaseSubtree(*Roots[F]); Roots[F].Reset(); }
	}
	ClearSolidRocks();
	VisibleChunks = BuiltChunks = PendingBuilds = DeepestVisible = ParkedChunks = VisibleRocks = 0;
}

void UPlanetTerrainComponent::ReleaseSubtree(FNode& N)
{
	for (int32 i = 0; i < 4; ++i)
	{
		if (N.Children[i]) { ReleaseSubtree(*N.Children[i]); N.Children[i].Reset(); }
	}
	ReleaseMesh(N);
	N.bBuilding = false;
	N.Task = UE::Tasks::TTask<void>();
}

void UPlanetTerrainComponent::ReleaseMesh(FNode& N)
{
	ReleaseRocks(N);
	if (N.Mesh)
	{
		N.Mesh->SetVisibility(false);
		FreeMeshes.Add(N.Mesh);
		N.Mesh = nullptr;
		--BuiltChunks;
	}
	N.bReady = false;
	N.Result.Reset();
	N.Heights.Empty();
	N.HeightSteps = 0;
}

/** Hides the subtree under N. The chunks stay built and parked: flying back over them costs nothing, and
 *  EvictParked lets the oldest go once more than ParkedBudget are parked. */
void UPlanetTerrainComponent::FreeSubtree(FNode& N)
{
	for (int32 i = 0; i < 4; ++i)
	{
		if (!N.Children[i]) { continue; }
		FNode& C = *N.Children[i];
		FreeSubtree(C);
		if (C.Mesh && C.Mesh->IsVisible()) { SetChunkVisible(C, false); }
	}
}

void UPlanetTerrainComponent::EvictParked()
{
	// Gather the hidden, finished chunks nobody asked for this update, oldest first.
	struct FParked { uint32 Wanted; FNode* Node; };
	TArray<FParked> Parked;
	TFunction<void(FNode&)> Gather = [&](FNode& N)
	{
		if (N.Mesh && N.bReady && !N.Mesh->IsVisible() && N.LastWanted != Stamp) { Parked.Add({ N.LastWanted, &N }); }
		for (int32 i = 0; i < 4; ++i) { if (N.Children[i]) { Gather(*N.Children[i]); } }
	};
	for (int32 F = 0; F < 6; ++F) { if (Roots[F]) { Gather(*Roots[F]); } }
	ParkedChunks = Parked.Num();
	if (Parked.Num() > ParkedBudget)
	{
		Parked.Sort([](const FParked& A, const FParked& B) { return A.Wanted < B.Wanted; });
		for (int32 i = 0; i < Parked.Num() - ParkedBudget; ++i) { ReleaseMesh(*Parked[i].Node); }
		ParkedChunks = ParkedBudget;
	}
	// Prune the nodes that hold nothing any more (no mesh, no build, nothing wanted below) so the tree does not
	// keep every chunk ever visited. Roots and anything touched this update stay.
	TFunction<bool(FNode&)> Prune = [&](FNode& N) -> bool
	{
		bool bEmpty = !N.Mesh && !N.bBuilding && N.LastWanted != Stamp;
		for (int32 i = 0; i < 4; ++i)
		{
			if (!N.Children[i]) { continue; }
			if (Prune(*N.Children[i])) { N.Children[i].Reset(); }
			else { bEmpty = false; }
		}
		return bEmpty;
	};
	for (int32 F = 0; F < 6; ++F) { if (Roots[F]) { Prune(*Roots[F]); } }
}

bool UPlanetTerrainComponent::AllChildrenReady(const FNode& N) const
{
	for (int32 i = 0; i < 4; ++i)
	{
		if (!N.Children[i] || !N.Children[i]->bReady) { return false; }
	}
	return true;
}

void UPlanetTerrainComponent::MakeChildren(FNode& N)
{
	for (int32 i = 0; i < 4; ++i)
	{
		if (N.Children[i]) { continue; }
		TUniquePtr<FNode> C = MakeUnique<FNode>();
		C->Face = N.Face;
		C->Depth = N.Depth + 1;
		C->X = N.X * 2 + (i & 1);
		C->Y = N.Y * 2 + (i >> 1);
		ChunkFrame(C->Face, C->Depth, C->X, C->Y, C->Centre, C->EdgeCm);
		C->Origin = C->Centre * Params.RadiusCm;
		C->CentreHeightCm = FMath::Max(SlonkTerrain::Sample(Params, C->Centre, C->EdgeCm * 0.25).HeightCm, 0.0);
		N.Children[i] = MoveTemp(C);
	}
}

FVector UPlanetTerrainComponent::CubeDir(int32 Face, double U, double V)
{
	// Tangent warp: equal steps in U are near-equal angles on the sphere, so chunks are the same size at a face's
	// middle and its corners (plain cube projection squeezes the corners to half size).
	const double S = FMath::Tan(U * UE_DOUBLE_PI * 0.25);
	const double T = FMath::Tan(V * UE_DOUBLE_PI * 0.25);
	FVector D;
	switch (Face)
	{
	case 0: D = FVector(1.0, S, T); break;
	case 1: D = FVector(-1.0, -S, T); break;
	case 2: D = FVector(-S, 1.0, T); break;
	case 3: D = FVector(S, -1.0, T); break;
	case 4: D = FVector(-T, S, 1.0); break;
	default: D = FVector(T, S, -1.0); break;
	}
	return D.GetSafeNormal();
}

void UPlanetTerrainComponent::ChunkFrame(int32 Face, int32 Depth, int64 X, int64 Y, FVector& OutCentre, double& OutEdge) const
{
	const double Cells = (double)((int64)1 << Depth);
	const double U0 = -1.0 + 2.0 * X / Cells, U1 = -1.0 + 2.0 * (X + 1) / Cells;
	const double V0 = -1.0 + 2.0 * Y / Cells, V1 = -1.0 + 2.0 * (Y + 1) / Cells;
	const double Um = 0.5 * (U0 + U1), Vm = 0.5 * (V0 + V1);
	OutCentre = CubeDir(Face, Um, Vm);
	const FVector A = CubeDir(Face, U0, Vm), B = CubeDir(Face, U1, Vm);
	const FVector C = CubeDir(Face, Um, V0), D = CubeDir(Face, Um, V1);
	// atan2 of the cross and dot products, not acos of the dot: at the fine levels the dot rounds to 1 and acos
	// to 0, and a zero cell froze the slope step's doubling loop.
	const double AngU = FMath::Atan2(FVector::CrossProduct(A, B).Size(), FVector::DotProduct(A, B));
	const double AngV = FMath::Atan2(FVector::CrossProduct(C, D).Size(), FVector::DotProduct(C, D));
	OutEdge = FMath::Max(AngU, AngV) * Params.RadiusCm;
}

UDynamicMeshComponent* UPlanetTerrainComponent::AcquireMesh()
{
	if (FreeMeshes.Num() > 0)
	{
		return FreeMeshes.Pop(EAllowShrinking::No);
	}
	UDynamicMeshComponent* C = NewObject<UDynamicMeshComponent>(GetOwner() ? (UObject*)GetOwner() : (UObject*)this, NAME_None, RF_Transient);
	C->SetupAttachment(this);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->bAffectDistanceFieldLighting = false;
	// Shadows are switched on per chunk by size in ApplyResult; the dynamic flag has to be on for any to cast.
	C->CastShadow = false;
	C->bCastDynamicShadow = true;
	// On foot the sun's near cascades reach 400 m and far cascades take the rest: the hills beyond stay shadowed.
	C->bCastFarShadow = true;
	C->SetVisibility(false);
	C->RegisterComponent();
	if (MID) { C->SetMaterial(0, MID); }
	Pool.Add(C);
	return C;
}

void UPlanetTerrainComponent::ShowNode(FNode& N, bool bShow)
{
	if (N.Mesh)
	{
		const bool bWant = bShow && bTerrainVisible;
		SetChunkVisible(N, bWant);
		if (bWant) { ++VisibleChunks; VisibleRocks += N.RockCount; DeepestVisible = FMath::Max(DeepestVisible, N.Depth); }
	}
}

void UPlanetTerrainComponent::SetChunkVisible(FNode& N, bool bShow)
{
	if (N.Mesh && N.Mesh->IsVisible() != bShow) { N.Mesh->SetVisibility(bShow); }
	for (UInstancedStaticMeshComponent* R : N.Rocks)
	{
		if (R && R->IsVisible() != bShow) { R->SetVisibility(bShow); }
	}
}

void UPlanetTerrainComponent::SetTerrainVisible(bool bShow)
{
	bTerrainVisible = bShow;
	if (!bShow)
	{
		for (UDynamicMeshComponent* C : Pool) { if (C) { C->SetVisibility(false); } }
		for (UInstancedStaticMeshComponent* R : RockPool) { if (R) { R->SetVisibility(false); } }
		ClearSolidRocks();
	}
}

UInstancedStaticMeshComponent* UPlanetTerrainComponent::AcquireRocks(int32 Kind)
{
	if (FreeRocks[Kind].Num() > 0)
	{
		return FreeRocks[Kind].Pop(EAllowShrinking::No);
	}
	// Attached to a chunk's mesh when used (ApplyRocks), to nothing while pooled.
	UInstancedStaticMeshComponent* R = NewObject<UInstancedStaticMeshComponent>(GetOwner() ? (UObject*)GetOwner() : (UObject*)this, NAME_None, RF_Transient);
	R->SetMobility(EComponentMobility::Movable);
	R->SetStaticMesh(SlonkGroundRocks::GetMesh((EGroundRock)Kind));
	R->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	R->SetGenerateOverlapEvents(false);
	R->SetCanEverAffectNavigation(false);
	R->bAffectDistanceFieldLighting = false;
	// The planet turns every frame, and an instanced component that moves works its bounds out afresh from every
	// instance: a hundred thousand rocks were ten milliseconds of the game thread. The chunk's own bounds hold its
	// rocks but for the tops of the few at its edge.
	R->bUseAttachParentBound = true;
	R->SetNumCustomDataFloats(SlonkScatter::NumCustomFloats);
	// Shadows by chunk size in ApplyRocks, as for the chunks themselves.
	R->CastShadow = false;
	R->bCastDynamicShadow = true;
	R->SetVisibility(false);
	if (RockMID) { R->SetMaterial(0, RockMID); }
	R->RegisterComponent();
	RockPool.Add(R);
	return R;
}

void UPlanetTerrainComponent::ApplyRocks(FNode& N, SlonkScatter::FChunkScatter& Sc)
{
	N.RockCount = 0;
	const bool bShadow = N.EdgeCm < ScatterShadowEdgeCm;
	for (int32 K = 0; K < SlonkGroundRocks::NumKinds; ++K)
	{
		N.Solids[K] = MoveTemp(Sc.Solids[K]);
		const TArray<FTransform>& Xf = Sc.Transforms[K];
		UInstancedStaticMeshComponent*& R = N.Rocks[K];
		if (Xf.Num() == 0)
		{
			if (R) { PoolRocks(R, FreeRocks[K]); R = nullptr; }
			continue;
		}
		if (!R) { R = AcquireRocks(K); }
		// On the chunk's mesh, so the instances are chunk-relative (floats stay exact) and move with it.
		if (R->GetAttachParent() != N.Mesh) { R->AttachToComponent(N.Mesh, FAttachmentTransformRules::KeepRelativeTransform); }
		R->SetRelativeTransform(FTransform::Identity);
		R->ClearInstances();
		R->AddInstances(Xf, false, false, false);
		R->SetCustomData(0, Xf.Num() - 1, Sc.Custom[K]);
		if (R->CastShadow != bShadow) { R->SetCastShadow(bShadow); }
		R->SetVisibility(N.Mesh->IsVisible());
		N.RockCount += Xf.Num();
	}
}

void UPlanetTerrainComponent::ReleaseRocks(FNode& N)
{
	for (int32 K = 0; K < SlonkGroundRocks::NumKinds; ++K)
	{
		if (UInstancedStaticMeshComponent* R = N.Rocks[K])
		{
			PoolRocks(R, FreeRocks[K]);
			N.Rocks[K] = nullptr;
		}
		N.Solids[K].Empty();
	}
	N.RockCount = 0;
}

int32 UPlanetTerrainComponent::CheckSolidRocks(int32 MaxRocks, int32& OutHits) const
{
	OutHits = 0;
	UWorld* World = GetWorld();
	if (!World) { return 0; }
	struct FNear { double DistSq; FVector Local; double HeightCm; };
	TArray<FNear> Near;
	for (const UInstancedStaticMeshComponent* C : SolidSets)
	{
		for (int32 i = 0; C && i < C->GetInstanceCount(); ++i)
		{
			FTransform T;
			C->GetInstanceTransform(i, T, false);
			Near.Add({ FVector::DistSquared(T.GetLocation(), PawnLocal), T.GetLocation(), T.GetScale3D().Z * SlonkGroundRocks::UnitCm });
		}
	}
	Near.Sort([](const FNear& A, const FNear& B) { return A.DistSq < B.DistSq; });
	Near.SetNum(FMath::Min(Near.Num(), MaxRocks));
	const FTransform& CT = GetComponentTransform();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RockCheck), true);
	for (const FNear& R : Near)
	{
		// Straight down through the middle of the rock, from well above its top to below its centre.
		const FVector Up = R.Local.GetSafeNormal();
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, CT.TransformPosition(R.Local + Up * (R.HeightCm + 300.0)), CT.TransformPosition(R.Local - Up * (R.HeightCm * 0.25)), ECC_GameTraceChannel1, Query)
			&& Hit.GetComponent() && SolidSets.Contains(Hit.GetComponent()))
		{
			++OutHits;
		}
	}
	return Near.Num();
}

void UPlanetTerrainComponent::ClearSolidRocks()
{
	for (UInstancedStaticMeshComponent* C : SolidSets)
	{
		if (C && C->GetInstanceCount() > 0) { C->ClearInstances(); }
	}
	SolidSignature = 0;
	SolidCentre = FVector(1.0e30, 0.0, 0.0);
	SolidRocks = 0;
}

void UPlanetTerrainComponent::UpdateSolidRocks()
{
	const FVector Moved = PawnLocal - SolidLastPawn;
	SolidLastPawn = PawnLocal;
	if (!Scatter.bEnabled || !bTerrainVisible) { ClearSolidRocks(); return; }
	// Only for someone on the ground or near it, going at a walk or a run: a craft racing over the rocks needs
	// none and would rebuild them every update.
	const FVector PawnDir = PawnLocal.GetSafeNormal();
	double Ground = 0.0;
	if (Moved.Size() > 2000.0 || !DrawnHeightAtLocalDir(PawnDir, Ground) || PawnLocal.Size() - Params.RadiusCm - Ground > 3000.0)
	{
		if (SolidRocks > 0) { ClearSolidRocks(); }
		return;
	}
	const double Reach = ScatterSolidReachCm;
	// The chunks showing round the pawn; rebuilt when they change or the pawn has gone a quarter of the reach.
	TArray<const FNode*> Near;
	TFunction<void(const FNode&)> Walk = [&](const FNode& N)
	{
		const double AwayCm = FMath::Acos(FMath::Clamp(FVector::DotProduct(PawnDir, N.Centre), -1.0, 1.0)) * Params.RadiusCm;
		if (AwayCm > N.EdgeCm * 0.75 + Reach) { return; }
		if (N.Mesh && N.bReady && N.Mesh->IsVisible() && N.RockCount > 0) { Near.Add(&N); }
		for (const TUniquePtr<FNode>& C : N.Children) { if (C) { Walk(*C); } }
	};
	for (const TUniquePtr<FNode>& Root : Roots) { if (Root) { Walk(*Root); } }
	uint64 Sig = 1469598103934665603ULL;
	for (const FNode* N : Near)
	{
		Sig = (Sig ^ (uint64)(UPTRINT)N) * 1099511628211ULL;
		Sig = (Sig ^ (uint64)N->RockCount) * 1099511628211ULL;
	}
	if (Sig == SolidSignature && FVector::DistSquared(PawnLocal, SolidCentre) < FMath::Square(Reach * 0.25)) { return; }
	SolidSignature = Sig;
	SolidCentre = PawnLocal;
	if (SolidSets.Num() == 0)
	{
		for (int32 K = 0; K < SlonkGroundRocks::NumKinds; ++K)
		{
			UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(GetOwner() ? (UObject*)GetOwner() : (UObject*)this, NAME_None, RF_Transient);
			C->SetupAttachment(this);
			C->SetMobility(EComponentMobility::Movable);
			C->SetStaticMesh(SlonkGroundRocks::GetMesh((EGroundRock)K));
			// Solid to the crew's boots (their floor and wall traces) and nothing else.
			C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			C->SetCollisionResponseToAllChannels(ECR_Ignore);
			C->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
			C->SetGenerateOverlapEvents(false);
			C->SetCanEverAffectNavigation(false);
			C->SetVisibility(false);		// collision only: the visual copy is on the chunk
			C->SetHiddenInGame(true);
			C->CastShadow = false;
			C->RegisterComponent();
			SolidSets.Add(C);
		}
	}
	SolidRocks = 0;
	for (int32 K = 0; K < SlonkGroundRocks::NumKinds; ++K)
	{
		TArray<FTransform> Planet;
		for (const FNode* N : Near)
		{
			// Chunk-relative to planet-local: the chunk's mesh sits at its origin, unrotated.
			const FVector O = N->Mesh->GetRelativeLocation();
			for (const FTransform& T : N->Solids[K])
			{
				if (FVector::DistSquared(T.GetLocation() + O, PawnLocal) > Reach * Reach) { continue; }
				FTransform W = T;
				W.AddToTranslation(O);
				Planet.Add(W);
			}
		}
		UInstancedStaticMeshComponent* C = SolidSets[K];
		if (C->GetInstanceCount() > 0) { C->ClearInstances(); }
		if (Planet.Num() > 0) { C->AddInstances(Planet, false, false, false); }
		SolidRocks += Planet.Num();
	}
}

void UPlanetTerrainComponent::RequestBuild(FNode& N, double Priority)
{
	if (N.bReady || N.bBuilding) { return; }
	Requests.Add({ &N, Priority });
}

void UPlanetTerrainComponent::Select(FNode& N, const FVector& CamLocal, double CamDist, const FVector& CamDir, double HorizonCos)
{
	N.LastWanted = Stamp;
	// Entirely over the horizon: hide it and let the subtree go.
	const double HalfDiagAng = N.EdgeCm * 0.75 / Params.RadiusCm;
	const double Ang = FMath::Acos(FMath::Clamp(FVector::DotProduct(CamDir, N.Centre), -1.0, 1.0));
	if (Ang - HalfDiagAng > FMath::Acos(FMath::Clamp(HorizonCos, -1.0, 1.0)))
	{
		ShowNode(N, false);
		FreeSubtree(N);
		return;
	}
	const FVector NodeAt = N.Origin + N.Centre * N.CentreHeightCm;
	const double Dist = FMath::Max(FMath::Min(FVector::Dist(CamLocal, NodeAt), FVector::Dist(PawnLocal, NodeAt)) - N.EdgeCm * 0.75, 0.0);
	const double CellCm = N.EdgeCm / FMath::Max(GridSteps, 1);
	const bool bWantSplit = N.Depth < MaxDepth && CellCm > MinCellCm && Dist < SplitFactor * N.EdgeCm;
	if (bWantSplit)
	{
		MakeChildren(N);
		for (int32 i = 0; i < 4; ++i)
		{
			RequestBuild(*N.Children[i], FMath::Min(FVector::Dist(CamLocal, N.Children[i]->Origin), FVector::Dist(PawnLocal, N.Children[i]->Origin)));
		}
		if (AllChildrenReady(N))
		{
			ShowNode(N, false);
			for (int32 i = 0; i < 4; ++i) { Select(*N.Children[i], CamLocal, CamDist, CamDir, HorizonCos); }
			return;
		}
		// Children still cooking: keep this level on screen (build it too if it never was).
		RequestBuild(N, Dist);
		ShowNode(N, true);
		for (int32 i = 0; i < 4; ++i) { N.Children[i]->LastWanted = Stamp; if (N.Children[i]->Mesh) { SetChunkVisible(*N.Children[i], false); } }
		return;
	}
	// This level is the right one.
	if (N.bReady)
	{
		ShowNode(N, true);
		FreeSubtree(N);
		return;
	}
	RequestBuild(N, Dist);
	// Until it lands, whatever the subtree currently shows stays as it is.
	bool bChildShowing = false;
	for (int32 i = 0; i < 4; ++i)
	{
		if (N.Children[i])
		{
			N.Children[i]->LastWanted = Stamp;
			if (N.Children[i]->Mesh && N.Children[i]->Mesh->IsVisible()) { bChildShowing = true; ++VisibleChunks; }
		}
	}
	(void)bChildShowing;
}

void UPlanetTerrainComponent::LaunchBuilds()
{
	Requests.Sort([](const FRequest& A, const FRequest& B) { return A.Priority < B.Priority; });
	for (const FRequest& R : Requests)
	{
		if (InFlight.Num() >= MaxConcurrentBuilds) { break; }
		FNode& N = *R.Node;
		if (N.bReady || N.bBuilding) { continue; }
		N.bBuilding = true;
		N.Result = MakeShared<FBuildResult, ESPMode::ThreadSafe>();
		TSharedPtr<FBuildResult, ESPMode::ThreadSafe> Out = N.Result;
		const FPlanetTerrainParams P = Params;
		const FScatterSettings Sc = Scatter;
		const int32 Face = N.Face, Depth = N.Depth, Steps = FMath::Clamp(GridSteps, 8, 128);
		const int64 X = N.X, Y = N.Y;
		N.Task = UE::Tasks::Launch(TEXT("SlonkTerrainChunk"), [P, Sc, Face, Depth, X, Y, Steps, Out]()
		{
			BuildChunk(P, Sc, Face, Depth, X, Y, Steps, *Out);
		});
		InFlight.Add(&N);
	}
	if (Requests.Num() > 0 && InFlight.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlanetTerrain: %d requests but nothing in flight (max %d)"), Requests.Num(), MaxConcurrentBuilds);
	}
	PendingBuilds = Requests.Num() + InFlight.Num();
	Requests.Reset();
}

void UPlanetTerrainComponent::CollectFinished()
{
	int32 Uploaded = 0;
	for (int32 i = InFlight.Num() - 1; i >= 0; --i)
	{
		FNode* N = InFlight[i];
		if (!N->Task.IsCompleted()) { continue; }
		if (Uploaded >= MaxUploadsPerUpdate) { break; }
		ApplyResult(*N);
		++Uploaded;
		InFlight.RemoveAtSwap(i);
	}
}

void UPlanetTerrainComponent::ApplyResult(FNode& N)
{
	N.bBuilding = false;
	N.Task = UE::Tasks::TTask<void>();
	if (!N.Result || !N.Result->Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlanetTerrain: chunk f%d d%d (%lld,%lld) produced no mesh"), N.Face, N.Depth, N.X, N.Y);
		N.Result.Reset();
		return;
	}
	UE_LOG(LogTemp, Verbose, TEXT("PlanetTerrain: chunk f%d d%d (%lld,%lld) landed, %d tris"), N.Face, N.Depth, N.X, N.Y, N.Result->Mesh->TriangleCount());
	if (!N.Mesh)
	{
		N.Mesh = AcquireMesh();
		++BuiltChunks;
	}
	UDynamicMeshComponent* C = N.Mesh;
	C->SetVisibility(false);
	C->SetRelativeLocation(N.Result->Origin);
	C->SetRelativeRotation(FRotator::ZeroRotator);
	C->GetDynamicMesh()->SetMesh(MoveTemp(*N.Result->Mesh));
	const bool bShadow = N.EdgeCm < ShadowCastEdgeCm;
	if (C->CastShadow != bShadow)
	{
		C->SetCastShadow(bShadow);
	}
	// The shader's detail noise is periodic in NoisePeriodCm; the origin modulo that period keeps it continuous
	// across chunks without a 6e8 cm float in the vertex shader.
	const FVector O = N.Result->Origin;
	auto Wrap = [](double V) { double M = FMath::Fmod(V, NoisePeriodCm); if (M < 0.0) { M += NoisePeriodCm; } return (float)M; };
	C->SetCustomPrimitiveDataFloat(0, Wrap(O.X));
	C->SetCustomPrimitiveDataFloat(1, Wrap(O.Y));
	C->SetCustomPrimitiveDataFloat(2, Wrap(O.Z));
	C->SetCustomPrimitiveDataFloat(3, (float)(N.EdgeCm / FMath::Max(GridSteps, 1)));
	// The chunk origin over the planet radius: the material rebuilds the radial direction from it (the origin
	// itself is a 6e8 cm float) to lay its texture projections in a frame tied to the ground, not the world axes.
	const FVector OR = O / Params.RadiusCm;
	C->SetCustomPrimitiveDataFloat(4, (float)OR.X);
	C->SetCustomPrimitiveDataFloat(5, (float)OR.Y);
	C->SetCustomPrimitiveDataFloat(6, (float)OR.Z);
	if (MID && C->GetMaterial(0) != MID) { C->SetMaterial(0, MID); }
	N.Heights = MoveTemp(N.Result->Heights);
	N.HeightSteps = N.Result->Steps;
	ApplyRocks(N, N.Result->Scatter);
	N.Result.Reset();
	N.bReady = true;
}

void UPlanetTerrainComponent::UpdateView(const FVector& ViewLocationWorld, const FVector& PawnLocationWorld)
{
	if (!bConfigured) { return; }
	++Stamp;
	VisibleChunks = 0;
	VisibleRocks = 0;
	DeepestVisible = 0;
	const FVector CamLocal = GetComponentTransform().InverseTransformPositionNoScale(ViewLocationWorld);
	// The chunks refine for the camera (what is seen) and for the pawn (what is stood on, a chase camera's length
	// behind it): the split distance is the nearer of the two.
	PawnLocal = GetComponentTransform().InverseTransformPositionNoScale(PawnLocationWorld);
	LastCamLocal = CamLocal;
	const double CamDist = CamLocal.Size();
	const FVector CamDir = CamDist > 1.0 ? CamLocal / CamDist : FVector::ZAxisVector;
	// Horizon from the camera, lowered by the relief so peaks past the geometric horizon still draw.
	const double Rh = FMath::Max(Params.RadiusCm - Params.ReliefCm * 1.5, Params.RadiusCm * 0.9);
	const double HorizonCos = CamDist > Rh ? Rh / CamDist : 1.0;
	CollectFinished();
	for (int32 F = 0; F < 6; ++F)
	{
		if (Roots[F]) { Select(*Roots[F], CamLocal, CamDist, CamDir, HorizonCos); }
	}
	LaunchBuilds();
	if ((Stamp % 8) == 0) { EvictParked(); }
	UpdateSolidRocks();
}

double UPlanetTerrainComponent::SurfaceHeightAtWorld(const FVector& WorldPos) const
{
	if (!bConfigured) { return 0.0; }
	const FVector Dir = GetComponentTransform().InverseTransformPositionNoScale(WorldPos).GetSafeNormal();
	double Drawn;
	if (DrawnHeightAtLocalDir(Dir, Drawn)) { return Drawn; }
	return SlonkTerrain::SurfaceHeightCm(Params, Dir);
}

bool UPlanetTerrainComponent::FaceUV(const FVector& Dir, int32& OutFace, double& OutU, double& OutV)
{
	const FVector A = Dir.GetAbs();
	double S, T;
	if (A.X >= A.Y && A.X >= A.Z)
	{
		if (A.X <= 0.0) { return false; }
		OutFace = Dir.X > 0.0 ? 0 : 1;
		S = Dir.Y / Dir.X; T = Dir.X > 0.0 ? Dir.Z / Dir.X : -Dir.Z / Dir.X;
	}
	else if (A.Y >= A.Z)
	{
		OutFace = Dir.Y > 0.0 ? 2 : 3;
		S = -Dir.X / Dir.Y; T = Dir.Y > 0.0 ? Dir.Z / Dir.Y : -Dir.Z / Dir.Y;
	}
	else
	{
		OutFace = Dir.Z > 0.0 ? 4 : 5;
		T = -Dir.X / Dir.Z; S = Dir.Z > 0.0 ? Dir.Y / Dir.Z : -Dir.Y / Dir.Z;
	}
	OutU = FMath::Atan(S) * 4.0 / UE_DOUBLE_PI;
	OutV = FMath::Atan(T) * 4.0 / UE_DOUBLE_PI;
	// The mapping is checked rather than trusted: a sign slip here would put feet on the wrong chunk.
	return FVector::DotProduct(CubeDir(OutFace, OutU, OutV), Dir.GetSafeNormal()) > 0.999999;
}

bool UPlanetTerrainComponent::DrawnHeightAtLocalDir(const FVector& DirLocal, double& OutHeightCm) const
{
	int32 Face; double U, V;
	if (!bConfigured || !bTerrainVisible || !FaceUV(DirLocal, Face, U, V) || !Roots[Face]) { return false; }
	// Down the quadtree under (U, V) to the deepest chunk on screen there.
	const FNode* Best = nullptr;
	for (const FNode* N = Roots[Face].Get(); N;)
	{
		if (N->Mesh && N->bReady && N->Mesh->IsVisible() && N->Heights.Num() == (N->HeightSteps + 1) * (N->HeightSteps + 1) && N->HeightSteps > 0) { Best = N; }
		const double Cells = (double)((int64)1 << (N->Depth + 1));
		const int64 CX = FMath::Clamp((int64)FMath::FloorToDouble((U + 1.0) * 0.5 * Cells), (int64)0, (int64)Cells - 1);
		const int64 CY = FMath::Clamp((int64)FMath::FloorToDouble((V + 1.0) * 0.5 * Cells), (int64)0, (int64)Cells - 1);
		const int64 k = (CX - N->X * 2) + 2 * (CY - N->Y * 2);
		N = (k >= 0 && k < 4) ? N->Children[k].Get() : nullptr;
	}
	if (!Best) { return false; }
	OutHeightCm = GridHeightAt(Best->Heights, Best->HeightSteps, Best->Depth, Best->X, Best->Y, U, V);
	return true;
}

double UPlanetTerrainComponent::GridHeightAt(const TArray<float>& Heights, int32 Steps, int32 Depth, int64 X, int64 Y, double U, double V)
{
	// The chunk's grid is uniform in (U, V); the cell's two triangles split along the (i+1, j)-(i, j+1) diagonal.
	const int32 GW = Steps + 1;
	const double Cells = (double)((int64)1 << Depth);
	const double Wd = 2.0 / Cells, U0 = -1.0 + Wd * X, V0 = -1.0 + Wd * Y;
	const double Fu = FMath::Clamp((U - U0) / Wd, 0.0, 1.0) * Steps, Fv = FMath::Clamp((V - V0) / Wd, 0.0, 1.0) * Steps;
	const int32 i = FMath::Min((int32)Fu, Steps - 1), j = FMath::Min((int32)Fv, Steps - 1);
	const double fx = Fu - i, fy = Fv - j;
	const float* H = Heights.GetData();
	const double hA = H[j * GW + i], hB = H[j * GW + i + 1], hC = H[(j + 1) * GW + i], hD = H[(j + 1) * GW + i + 1];
	return (fx + fy <= 1.0) ? hA + fx * (hB - hA) + fy * (hC - hA) : hD + (1.0 - fx) * (hC - hD) + (1.0 - fy) * (hB - hD);
}

bool UPlanetTerrainComponent::ChunkAt(const FVector& Dir, int32 Depth, int32& OutFace, int64& OutX, int64& OutY)
{
	double U, V;
	if (!FaceUV(Dir, OutFace, U, V)) { return false; }
	const double Cells = (double)((int64)1 << Depth);
	OutX = FMath::Clamp((int64)FMath::FloorToDouble((U + 1.0) * 0.5 * Cells), (int64)0, (int64)Cells - 1);
	OutY = FMath::Clamp((int64)FMath::FloorToDouble((V + 1.0) * 0.5 * Cells), (int64)0, (int64)Cells - 1);
	return true;
}

void UPlanetTerrainComponent::BuildChunkScatterForTest(const FPlanetTerrainParams& P, const FScatterSettings& S, int32 Face, int32 Depth, int64 X, int64 Y, int32 Steps,
	TArray<FTransform> (&OutPlanetLocal)[SlonkGroundRocks::NumKinds], TArray<double> (&OutDrawnHeightCm)[SlonkGroundRocks::NumKinds])
{
	FBuildResult R;
	BuildChunk(P, S, Face, Depth, X, Y, Steps, R);
	for (int32 K = 0; K < SlonkGroundRocks::NumKinds; ++K)
	{
		for (const FTransform& T : R.Scatter.Transforms[K])
		{
			FTransform W = T;
			W.AddToTranslation(R.Origin);
			OutPlanetLocal[K].Add(W);
			int32 F; double U, V;
			FaceUV(W.GetLocation().GetSafeNormal(), F, U, V);
			OutDrawnHeightCm[K].Add(GridHeightAt(R.Heights, R.Steps, Depth, X, Y, U, V));
		}
	}
}

FPlanetTerrainSample UPlanetTerrainComponent::SampleAtWorld(const FVector& WorldPos, double MinWavelengthCm) const
{
	if (!bConfigured) { return FPlanetTerrainSample(); }
	const FVector Local = GetComponentTransform().InverseTransformPositionNoScale(WorldPos);
	return SlonkTerrain::Sample(Params, Local.GetSafeNormal(), MinWavelengthCm);
}

bool UPlanetTerrainComponent::FindFeature(const FString& Kind, const FVector& NearDir, FVector& OutDir, double& OutHeightCm, const FVector& SunDirLocal) const
{
	if (!bConfigured) { return false; }
	if (Kind.ToLower() == TEXT("city") || Kind.ToLower() == TEXT("town"))
	{
		// The biggest town within 600 km of the bearing (or of the sub-point), weighted toward the near ones.
		const FVector Centre = (NearDir.IsNearlyZero() ? FVector::ZAxisVector : NearDir.GetSafeNormal()) * Params.RadiusCm;
		TArray<FTownSite> Towns;
		SlonkCities::EnumerateTowns(Params, Centre, 6.0e7, Towns, 2000);
		const FTownSite* Best = nullptr; double BestScore = -1.0e9;
		for (const FTownSite& T : Towns)
		{
			// Daylight preferred, not required.
			const double Lit = SunDirLocal.IsNearlyZero() ? 0.0 : (FVector::DotProduct(T.Dir, SunDirLocal.GetSafeNormal()) < 0.26 ? -20.0 : 0.0);
			// Cities first, then the big ones, near ones, lit ones, and the temperate latitudes over the snow.
			const double Score = (T.bCity ? 6.0 : 0.0) + T.Strength * 10.0 - FVector::Dist(T.LocalCm, Centre) / 6.0e7 * 3.0 + Lit - FMath::Max(0.0, FMath::Abs(T.Dir.Z) - 0.5) * 12.0;
			if (Score > BestScore) { BestScore = Score; Best = &T; }
		}
		if (!Best) { return false; }
		OutDir = Best->Dir;
		OutHeightCm = SlonkTerrain::SampleStructure(Params, Best->Dir).HeightCm;
		return true;
	}
	const FString K = Kind.ToLower();
	// Fibonacci sphere: even coverage; the coarse pass samples at ~30 km, the refine pass walks the best few.
	const int32 N = 6000;
	double BestScore = -1e30;
	FVector Best = FVector::ZeroVector;
	const double Golden = UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
	auto Score = [&](const FVector& D, const FPlanetTerrainSample& S) -> double
	{
		const double Near = NearDir.IsNearlyZero() ? 0.0 : FVector::DotProduct(D, NearDir.GetSafeNormal());
		// With a sun given, only daylit ground qualifies (the sun at least 15 degrees up).
		if (!SunDirLocal.IsNearlyZero() && FVector::DotProduct(D, SunDirLocal.GetSafeNormal()) < 0.26) { return -1e9; }
		// Canyons and rims prefer temperate latitudes (no snow on the plateau) and sit away from the ice, and a
		// sun 12 to 40 degrees up, which throws the walls into relief instead of lighting them flat from above.
		double Temperate = -3.0 * FMath::Abs(D.Z) - 6.0 * S.Ice;
		if (!SunDirLocal.IsNearlyZero())
		{
			const double SunUp = FVector::DotProduct(D, SunDirLocal.GetSafeNormal());
			Temperate += 3.0 * (1.0 - FMath::Clamp(FMath::Abs(SunUp - CVarTerrainSunUp.GetValueOnGameThread()) / CVarTerrainSunUpWidth.GetValueOnGameThread() - 1.0, 0.0, 1.0));
		}
		// The deepest cut wins, not merely the widest floor.
		if (K == TEXT("canyon")) { return S.bWater ? -1e9 : S.Canyon * 4.0 + S.CutCm / FMath::Max(Params.CanyonDepthCm, 1.0) * 10.0 + Temperate + Near * 0.5; }
		// The rim: top of a wall, just outside the cut, high up.
		if (K == TEXT("rim")) { return S.bWater ? -1e9 : S.Wall * 6.0 - S.Canyon * 8.0 + S.HeightCm / FMath::Max(Params.ReliefCm, 1.0) * 4.0 + S.CutCm / FMath::Max(Params.CanyonDepthCm, 1.0) * 4.0 + Temperate + Near * 0.5; }
		// The river: the gorge's deepest cut leads to within a bend of it, then the river mask takes over.
		if (K == TEXT("river")) { return S.River * 10.0 + S.Canyon * 2.0 + S.CutCm / FMath::Max(Params.CanyonDepthCm, 1.0) * 4.0 + Temperate + Near * 0.5; }
		if (K == TEXT("mountain")) { return S.bWater ? -1e9 : S.HeightCm / FMath::Max(Params.ReliefCm, 1.0) * 10.0 + Near * 0.5; }
		if (K == TEXT("linea")) { return S.Linea * 10.0 + Near * 3.0; }
		if (K == TEXT("coast"))
		{
			if (S.bWater) { return -1e9; }
			// Land near the sea: the score climbs as the nearest water (sampled along four directions at 300 m,
			// 1, 3 and 10 km) gets closer, so the hill-climb walks to the shore; water on every side at 300 m is
			// a sandbar, not a shore.
			const FVector E = FVector::CrossProduct(FVector::ZAxisVector, D).GetSafeNormal(), Nn = FVector::CrossProduct(D, E);
			double NearestKm = 30.0; int32 WetClose = 0;
			for (const FVector& Off : { E, -E, Nn, -Nn })
			{
				for (double Km : { 0.3, 1.0, 3.0, 10.0 })
				{
					if (Km >= NearestKm) { break; }
					if (SlonkTerrain::SampleStructure(Params, (D + Off * (Km * 1.0e5 / Params.RadiusCm)).GetSafeNormal(), 2.0e5).bWater)
					{
						NearestKm = Km; if (Km <= 0.3) { ++WetClose; } break;
					}
				}
			}
			// And land behind: dry ground 3 km away in at least one direction, or this is an islet or a spit.
			int32 Dry3 = 0;
			for (const FVector& Off : { E, -E, Nn, -Nn })
			{
				if (!SlonkTerrain::SampleStructure(Params, (D + Off * (3.0e5 / Params.RadiusCm)).GetSafeNormal(), 2.0e5).bWater) { ++Dry3; }
			}
			return -NearestKm * 0.6 - (WetClose == 4 ? 6.0 : 0.0) - (Dry3 == 0 ? 6.0 : 0.0) - FMath::Abs(S.HeightCm) / FMath::Max(Params.ReliefCm, 1.0) * 4.0 + Near * 3.0;
		}
		return S.bWater ? -1e9 : Near * 10.0 + (S.HeightCm > 0.0 ? 1.0 : 0.0);
	};
	const double CoarseWavelength = 2.0 * UE_DOUBLE_PI * Params.RadiusCm / FMath::Sqrt((double)N * UE_DOUBLE_PI);
	for (int32 i = 0; i < N; ++i)
	{
		const double Z = 1.0 - 2.0 * (i + 0.5) / N;
		const double Rr = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
		const double Phi = Golden * i;
		const FVector D(Rr * FMath::Cos(Phi), Rr * FMath::Sin(Phi), Z);
		const double Sc = Score(D, SlonkTerrain::Sample(Params, D, CoarseWavelength));
		if (Sc > BestScore) { BestScore = Sc; Best = D; }
	}
	if (BestScore <= -1e8) { return false; }
	// Canyons are thin lines on broad tablelands and the coarse pass mostly misses them: around the best few
	// tableland samples, comb a fine grid (2 km spacing, 80 km across) for the deepest cut.
	if (K == TEXT("canyon") || K == TEXT("rim") || K == TEXT("river"))
	{
		struct FCand { double Score; FVector D; };
		TArray<FCand> Cands;
		for (int32 i = 0; i < N; ++i)
		{
			const double Z = 1.0 - 2.0 * (i + 0.5) / N;
			const double Rr = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
			const double Phi = Golden * i;
			const FVector D(Rr * FMath::Cos(Phi), Rr * FMath::Sin(Phi), Z);
			if (!SunDirLocal.IsNearlyZero() && FVector::DotProduct(D, SunDirLocal.GetSafeNormal()) < 0.3) { continue; }
			const FPlanetTerrainSample S = SlonkTerrain::Sample(Params, D, CoarseWavelength);
			if (S.bWater || S.Table < 0.3f) { continue; }
			Cands.Add({ S.Table - 2.0 * FMath::Abs(D.Z) - 4.0 * S.Ice, D });
		}
		Cands.Sort([](const FCand& A, const FCand& B) { return A.Score > B.Score; });
		const double Spacing = 2.0e5 / Params.RadiusCm;
		for (int32 c = 0; c < FMath::Min(Cands.Num(), 12); ++c)
		{
			const FVector Ctr = Cands[c].D;
			const FVector E = FVector::CrossProduct(FVector::ZAxisVector, Ctr).GetSafeNormal();
			const FVector Nn = FVector::CrossProduct(Ctr, E).GetSafeNormal();
			for (int32 j = -20; j <= 20; ++j)
			{
				for (int32 i = -20; i <= 20; ++i)
				{
					const FVector D = (Ctr + E * (i * Spacing) + Nn * (j * Spacing)).GetSafeNormal();
					const double Sc = Score(D, SlonkTerrain::Sample(Params, D, 1.0e5));
					if (Sc > BestScore) { BestScore = Sc; Best = D; }
				}
			}
		}
	}
	// Refine: hill-climb on a shrinking step so the result sits on the feature, not 30 km from it.
	double Step = (K == TEXT("canyon") || K == TEXT("rim") || K == TEXT("river")) ? 2.0e5 / Params.RadiusCm : CoarseWavelength / Params.RadiusCm;
	for (int32 It = 0; It < 6; ++It)
	{
		bool bMoved = true;
		while (bMoved)
		{
			bMoved = false;
			const FVector E = FVector::CrossProduct(FVector::ZAxisVector, Best).GetSafeNormal();
			const FVector Nn = FVector::CrossProduct(Best, E).GetSafeNormal();
			for (const FVector& Off : { E * Step, -E * Step, Nn * Step, -Nn * Step })
			{
				const FVector D = (Best + Off).GetSafeNormal();
				const double Sc = Score(D, SlonkTerrain::Sample(Params, D, Step * Params.RadiusCm * 0.5));
				if (Sc > BestScore) { BestScore = Sc; Best = D; bMoved = true; }
			}
		}
		Step *= 0.4;
	}
	OutDir = Best;
	OutHeightCm = SlonkTerrain::SurfaceHeightCm(Params, Best);
	return true;
}

// ---- off-thread chunk build ----

void UPlanetTerrainComponent::BuildChunk(const FPlanetTerrainParams& P, const FScatterSettings& Sc, int32 Face, int32 Depth, int64 X, int64 Y, int32 Steps, FBuildResult& Out)
{
	const double R = P.RadiusCm;
	const double Cells = (double)((int64)1 << Depth);
	const double U0 = -1.0 + 2.0 * X / Cells, U1 = -1.0 + 2.0 * (X + 1) / Cells;
	const double V0 = -1.0 + 2.0 * Y / Cells, V1 = -1.0 + 2.0 * (Y + 1) / Cells;
	auto Dir = [&](double i, double j)
	{
		const double U = U0 + (U1 - U0) * i / Steps;
		const double V = V0 + (V1 - V0) * j / Steps;
		const double S = FMath::Tan(U * UE_DOUBLE_PI * 0.25);
		const double T = FMath::Tan(V * UE_DOUBLE_PI * 0.25);
		FVector D;
		switch (Face)
		{
		case 0: D = FVector(1.0, S, T); break;
		case 1: D = FVector(-1.0, -S, T); break;
		case 2: D = FVector(-S, 1.0, T); break;
		case 3: D = FVector(S, -1.0, T); break;
		case 4: D = FVector(-T, S, 1.0); break;
		default: D = FVector(T, S, -1.0); break;
		}
		return D.GetSafeNormal();
	};
	const FVector CentreDir = Dir(Steps / 2, Steps / 2);
	const FVector Origin = CentreDir * R;
	// Cell size from the chunk's middle row; everything finer than two cells would only alias.
	const FVector D0 = Dir(0, Steps / 2), D1 = Dir(1, Steps / 2);
	const double CellCm = FMath::Max(FMath::Atan2(FVector::CrossProduct(D0, D1).Size(), FVector::DotProduct(D0, D1)) * R, 1.0e-3);
	const double MinWavelength = CellCm * 2.0;
	const double N_EdgeCm = CellCm * Steps;

	// Sample the structure on a grid with a two-cell margin: the refinement wants the structure's slope at every
	// vertex, and the border normals want refined neighbours, so the slope needs one more ring beyond that.
	const int32 Margin = 2;
	const int32 W = Steps + 1 + 2 * Margin;
	TArray<FVector> Pos; Pos.SetNumUninitialized(W * W);
	TArray<FPlanetTerrainSample> Smp; Smp.SetNumUninitialized(W * W);
	TArray<FVector> Dirs; Dirs.SetNumUninitialized(W * W);
	for (int32 j = -Margin; j <= Steps + Margin; ++j)
	{
		for (int32 i = -Margin; i <= Steps + Margin; ++i)
		{
			const int32 k = (j + Margin) * W + (i + Margin);
			Dirs[k] = Dir(i, j);
			Smp[k] = SlonkTerrain::SampleStructure(P, Dirs[k], MinWavelength);
		}
	}
	// Refinement from the slope of the structure (rise over run by central differences), as Outerra adds its
	// fractal detail from the slope of its elevation model: every level finer than 200 m cells runs it.
	if (MinWavelength < SlonkTerrain::RefineStartCm)
	{
		// The slope is measured over the same distance at every level. Taken over the chunk's own cell it changed
		// with the level (a 5 m cell and a 50 m cell read different slopes off the same ground), and since it
		// scales the whole of the refinement, neighbouring chunks at different levels stood metres apart along
		// their shared edge: walls and trenches round every level change. The step is the chunk's cell halved
		// or doubled into (SlopeStepCm / 4, SlopeStepCm / 2]: one value for every level of a planet, since the
		// cells halve level by level. Finer chunks take it as every Lm-th grid point (child lattices fall on their
		// parents'); the few coarser ones, whose cells are wider than the step, sample the structure that far to
		// each side of every grid point. Between lattice points the slope is interpolated.
		double StepCm = CellCm;
		while (StepCm > SlonkTerrain::SlopeStepCm * 0.5) { StepCm *= 0.5; }
		while (StepCm * 2.0 <= SlonkTerrain::SlopeStepCm * 0.5) { StepCm *= 2.0; }
		const int32 Lm = FMath::Max(FMath::RoundToInt32(StepCm / CellCm), 1);
		const double SubStep = StepCm / CellCm;          // in grid cells; under 1 only on the coarse levels
		const int32 LA = Steps / Lm;                     // lattice cells across the chunk
		const int32 LMargin = 3, LW = LA + 1 + 2 * LMargin;   // the slope one past the vertex ring one past the chunk
		TArray<double> GxL, GyL; GxL.SetNumUninitialized(LW * LW); GyL.SetNumUninitialized(LW * LW);
		auto StructureAt = [&](double i, double j) -> double
		{
			const int32 ii = FMath::RoundToInt32(i), jj = FMath::RoundToInt32(j);
			const bool bOnGrid = FMath::IsNearlyEqual(i, (double)ii) && FMath::IsNearlyEqual(j, (double)jj) && ii >= -Margin && ii <= Steps + Margin && jj >= -Margin && jj <= Steps + Margin;
			return bOnGrid ? Smp[(jj + Margin) * W + (ii + Margin)].HeightCm : SlonkTerrain::SampleStructure(P, Dir(i, j), MinWavelength).HeightCm;
		};
		for (int32 b = -LMargin; b <= LA + LMargin; ++b)
		{
			for (int32 a = -LMargin; a <= LA + LMargin; ++a)
			{
				const double i = (double)a * Lm, j = (double)b * Lm;
				const int32 k = (b + LMargin) * LW + (a + LMargin);
				GxL[k] = (StructureAt(i + SubStep, j) - StructureAt(i - SubStep, j)) / (2.0 * StepCm);
				GyL[k] = (StructureAt(i, j + SubStep) - StructureAt(i, j - SubStep)) / (2.0 * StepCm);
			}
		}
		auto LatticeSlope = [&](int32 a, int32 b, double& Gx, double& Gy)
		{
			const int32 k = (b + LMargin) * LW + (a + LMargin);
			Gx = GxL[k]; Gy = GyL[k];
		};
		for (int32 j = -1; j <= Steps + 1; ++j)
		{
			for (int32 i = -1; i <= Steps + 1; ++i)
			{
				const int32 k = (j + Margin) * W + (i + Margin);
				if (Smp[k].bWater) { continue; }
				const int32 a0 = FMath::FloorToInt32((double)i / Lm), b0 = FMath::FloorToInt32((double)j / Lm);
				const double Fa = (double)(i - a0 * Lm) / Lm, Fb = (double)(j - b0 * Lm) / Lm;
				double G00x, G00y, G10x, G10y, G01x, G01y, G11x, G11y;
				LatticeSlope(a0, b0, G00x, G00y); LatticeSlope(a0 + 1, b0, G10x, G10y);
				LatticeSlope(a0, b0 + 1, G01x, G01y); LatticeSlope(a0 + 1, b0 + 1, G11x, G11y);
				const double Gx = FMath::Lerp(FMath::Lerp(G00x, G10x, Fa), FMath::Lerp(G01x, G11x, Fa), Fb);
				const double Gy = FMath::Lerp(FMath::Lerp(G00y, G10y, Fa), FMath::Lerp(G01y, G11y, Fa), Fb);
				SlonkTerrain::Refine(P, Dirs[k], FMath::Sqrt(Gx * Gx + Gy * Gy), MinWavelength, Smp[k]);
			}
		}
	}
	for (int32 k = 0; k < W * W; ++k)
	{
		const double H = Smp[k].bWater ? Smp[k].WaterCm : Smp[k].HeightCm;
		Pos[k] = Dirs[k] * (R + H) - Origin;
	}
	// The rocks on this chunk, on these vertices' triangles.
	if (Sc.bEnabled && N_EdgeCm <= SlonkScatter::MaxChunkEdgeCm())
	{
		SlonkScatter::FChunkGrid Grid;
		Grid.Face = Face; Grid.Depth = Depth; Grid.X = X; Grid.Y = Y;
		Grid.Steps = Steps; Grid.Margin = Margin; Grid.W = W;
		Grid.EdgeCm = N_EdgeCm;
		Grid.Samples = Smp.GetData(); Grid.Dirs = Dirs.GetData(); Grid.Pos = Pos.GetData();
		Grid.Origin = Origin;
		SlonkScatter::ScatterChunk(P, Sc, Grid, Out.Scatter);
	}
	// The grid's heights go back with the mesh: ground queries read the drawn surface from them.
	Out.Steps = Steps;
	Out.Heights.SetNumUninitialized((Steps + 1) * (Steps + 1));
	for (int32 j = 0; j <= Steps; ++j)
	{
		for (int32 i = 0; i <= Steps; ++i)
		{
			const FPlanetTerrainSample& S = Smp[(j + Margin) * W + (i + Margin)];
			Out.Heights[j * (Steps + 1) + i] = (float)(S.bWater ? S.WaterCm : S.HeightCm);
		}
	}

	TUniquePtr<FDynamicMesh3> M = MakeUnique<FDynamicMesh3>();
	M->EnableAttributes();
	M->Attributes()->SetNumUVLayers(4);
	M->Attributes()->EnablePrimaryColors();
	FDynamicMeshNormalOverlay* NO = M->Attributes()->PrimaryNormals();
	FDynamicMeshUVOverlay* UV0 = M->Attributes()->GetUVLayer(0);
	FDynamicMeshUVOverlay* UV1 = M->Attributes()->GetUVLayer(1);
	FDynamicMeshUVOverlay* UV2 = M->Attributes()->GetUVLayer(2);
	FDynamicMeshUVOverlay* UV3 = M->Attributes()->GetUVLayer(3);
	FDynamicMeshColorOverlay* CO = M->Attributes()->PrimaryColors();

	const int32 GW = Steps + 1;
	TArray<int32> Vid; Vid.SetNumUninitialized(GW * GW);
	TArray<FVector3f> Normals; Normals.SetNumUninitialized(GW * GW);
	auto AddVert = [&](const FVector& Pv, const FVector3f& Nv, const FVector2f& A, const FVector2f& B, const FVector2f& C, const FVector2f& D, const FVector4f& Col)
	{
		const int32 V = M->AppendVertex(Pv);
		NO->AppendElement(Nv);
		UV0->AppendElement(A);
		UV1->AppendElement(B);
		UV2->AppendElement(C);
		UV3->AppendElement(D);
		CO->AppendElement(Col);
		return V;
	};
	FBox Bounds(ForceInit);
	for (int32 j = 0; j <= Steps; ++j)
	{
		for (int32 i = 0; i <= Steps; ++i)
		{
			const int32 k = (j + Margin) * W + (i + Margin);
			const FPlanetTerrainSample& S = Smp[k];
			const FVector& D = Dirs[k];
			FVector N;
			if (S.bWater)
			{
				N = D;
			}
			else
			{
				const FVector Du = Pos[k + 1] - Pos[k - 1];
				const FVector Dv = Pos[k + W] - Pos[k - W];
				N = FVector::CrossProduct(Du, Dv).GetSafeNormal();
				if (FVector::DotProduct(N, D) < 0.0) { N = -N; }
				if (N.IsNearlyZero()) { N = D; }
			}
			const float Slope = (float)FMath::Clamp(1.0 - FVector::DotProduct(N, D), 0.0, 1.0);
			const float HeightN = (float)(S.HeightCm / FMath::Max(S.HeightCm >= 0.0 ? P.ReliefCm : P.OceanDepthCm, 1.0));
			const FVector3f Nf(N);
			Normals[j * GW + i] = Nf;
			Vid[j * GW + i] = AddVert(Pos[k], Nf, FVector2f(HeightN, Slope), FVector2f(S.Wet, S.Rock), FVector2f(S.Ice, S.Canyon), FVector2f(S.Linea, 0.f),
				FVector4f(S.Crater, S.bWater ? 1.f : 0.f, S.Wall, S.Stratum));
			Bounds += Pos[k];
		}
	}
	TArray<int32> Tris;
	auto AddTri = [&](int32 A, int32 B, int32 C)
	{
		const int32 T = M->AppendTriangle(FIndex3i(A, B, C));
		if (T >= 0)
		{
			NO->SetTriangle(T, FIndex3i(A, B, C));
			UV0->SetTriangle(T, FIndex3i(A, B, C));
			UV1->SetTriangle(T, FIndex3i(A, B, C));
			UV2->SetTriangle(T, FIndex3i(A, B, C));
			UV3->SetTriangle(T, FIndex3i(A, B, C));
			CO->SetTriangle(T, FIndex3i(A, B, C));
			Tris.Add(T);
		}
	};
	for (int32 j = 0; j < Steps; ++j)
	{
		for (int32 i = 0; i < Steps; ++i)
		{
			const int32 A = Vid[j * GW + i], B = Vid[j * GW + i + 1], C = Vid[(j + 1) * GW + i], D = Vid[(j + 1) * GW + i + 1];
			AddTri(A, C, B);
			AddTri(B, C, D);
		}
	}
	// Skirt: the border loop dropped a little below the surface hides the hairline gap against a neighbour at
	// another depth. Two per cent of the edge covers the height a coarser neighbour can miss by (the octaves
	// between the two levels' cutoffs); a cell and a half was a metre on fine chunks, nothing like it.
	const double SkirtCm = FMath::Max(FMath::Max(CellCm * 1.5, N_EdgeCm * 0.02), 100.0);
	TArray<int32> Loop;
	for (int32 i = 0; i <= Steps; ++i) { Loop.Add(i); }
	for (int32 j = 1; j <= Steps; ++j) { Loop.Add(j * GW + Steps); }
	for (int32 i = Steps - 1; i >= 0; --i) { Loop.Add(Steps * GW + i); }
	for (int32 j = Steps - 1; j >= 1; --j) { Loop.Add(j * GW); }
	TArray<int32> Lower;
	for (int32 G : Loop)
	{
		const int32 V = Vid[G];
		const int32 i = G % GW, j = G / GW;
		const int32 k = (j + Margin) * W + (i + Margin);
		const FVector Pv = M->GetVertex(V) - Dirs[k] * SkirtCm;
		Lower.Add(AddVert(Pv, Normals[G], UV0->GetElement(V), UV1->GetElement(V), UV2->GetElement(V), UV3->GetElement(V), CO->GetElement(V)));
	}
	for (int32 k = 0; k < Loop.Num(); ++k)
	{
		const int32 Pa = Vid[Loop[k]], Qa = Vid[Loop[(k + 1) % Loop.Num()]];
		const int32 Pd = Lower[k], Qd = Lower[(k + 1) % Loop.Num()];
		AddTri(Pa, Qa, Pd);
		AddTri(Qa, Qd, Pd);
	}
	// Faces outward whatever the cube face's handedness.
	if (Tris.Num() > 0)
	{
		const FIndex3i T0 = M->GetTriangle(Tris[0]);
		const FVector Centroid = (M->GetVertex(T0.A) + M->GetVertex(T0.B) + M->GetVertex(T0.C)) / 3.0 + Origin;
		if (FVector::DotProduct(M->GetTriNormal(Tris[0]), Centroid) < 0.0)
		{
			M->ReverseOrientation(false);
		}
	}
	Out.Mesh = MoveTemp(M);
	Out.Origin = Origin;
	Out.LocalBounds = Bounds;
}
