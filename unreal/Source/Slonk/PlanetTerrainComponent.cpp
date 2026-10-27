#include "PlanetTerrainComponent.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UDynamicMesh.h"

using namespace UE::Geometry;

namespace
{
	/** Period of the material's detail noise, cm. Chunk origins are handed to the shader modulo this, so the
	 *  float it adds to the vertex position never loses the metres. */
	constexpr double NoisePeriodCm = 1048576.0; // 2^20
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
	for (int32 F = 0; F < 6; ++F)
	{
		Roots[F] = MakeUnique<FNode>();
		Roots[F]->Face = F;
		ChunkFrame(F, 0, 0, 0, Roots[F]->Centre, Roots[F]->EdgeCm);
		Roots[F]->Origin = Roots[F]->Centre * Params.RadiusCm;
	}
	bConfigured = true;
	UE_LOG(LogTemp, Log, TEXT("PlanetTerrain: configured %s R %.0f km relief %.1f km sea %.2f"), GetOwner() ? *GetOwner()->GetName() : TEXT("?"), Params.RadiusCm / 1e5, Params.ReliefCm / 1e5, Params.SeaLevel);
}

void UPlanetTerrainComponent::SetScalarParameter(FName Name, float Value)
{
	if (MID) { MID->SetScalarParameterValue(Name, Value); }
}

void UPlanetTerrainComponent::SetVectorParameter(FName Name, const FLinearColor& Value)
{
	if (MID) { MID->SetVectorParameterValue(Name, Value); }
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
		if (Roots[F]) { FreeSubtree(*Roots[F]); ReleaseMesh(*Roots[F]); Roots[F].Reset(); }
	}
	VisibleChunks = BuiltChunks = PendingBuilds = DeepestVisible = 0;
}

void UPlanetTerrainComponent::ReleaseMesh(FNode& N)
{
	if (N.Mesh)
	{
		N.Mesh->SetVisibility(false);
		FreeMeshes.Add(N.Mesh);
		N.Mesh = nullptr;
		--BuiltChunks;
	}
	N.bReady = false;
	N.Result.Reset();
}

/** Hides the subtree under N and frees whatever is not still being built (builds in flight keep their node
 *  alive; the next update frees them once they land). */
void UPlanetTerrainComponent::FreeSubtree(FNode& N)
{
	for (int32 i = 0; i < 4; ++i)
	{
		if (!N.Children[i]) { continue; }
		FNode& C = *N.Children[i];
		FreeSubtree(C);
		if (C.Mesh) { C.Mesh->SetVisibility(false); }
		bool bBusy = C.bBuilding;
		for (int32 k = 0; k < 4 && !bBusy; ++k) { bBusy = C.Children[k].IsValid(); }
		if (!bBusy)
		{
			ReleaseMesh(C);
			N.Children[i].Reset();
		}
	}
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

FVector UPlanetTerrainComponent::CubeDir(int32 Face, double U, double V) const
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
	const double AngU = FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.0, 1.0));
	const double AngV = FMath::Acos(FMath::Clamp(FVector::DotProduct(C, D), -1.0, 1.0));
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
	C->CastShadow = false;
	C->bCastDynamicShadow = false;
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
		if (N.Mesh->IsVisible() != bWant) { N.Mesh->SetVisibility(bWant); }
		if (bWant) { ++VisibleChunks; DeepestVisible = FMath::Max(DeepestVisible, N.Depth); }
	}
}

void UPlanetTerrainComponent::SetTerrainVisible(bool bVisible)
{
	bTerrainVisible = bVisible;
	if (!bVisible)
	{
		for (UDynamicMeshComponent* C : Pool) { if (C) { C->SetVisibility(false); } }
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
	const double Dist = FMath::Max(FVector::Dist(CamLocal, N.Origin + N.Centre * N.CentreHeightCm) - N.EdgeCm * 0.75, 0.0);
	const double CellCm = N.EdgeCm / FMath::Max(GridSteps, 1);
	const bool bWantSplit = N.Depth < MaxDepth && CellCm > MinCellCm && Dist < SplitFactor * N.EdgeCm;
	if (bWantSplit)
	{
		MakeChildren(N);
		for (int32 i = 0; i < 4; ++i)
		{
			RequestBuild(*N.Children[i], FVector::Dist(CamLocal, N.Children[i]->Origin));
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
		for (int32 i = 0; i < 4; ++i) { N.Children[i]->LastWanted = Stamp; if (N.Children[i]->Mesh) { N.Children[i]->Mesh->SetVisibility(false); } }
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
		const int32 Face = N.Face, Depth = N.Depth, Steps = FMath::Clamp(GridSteps, 8, 128);
		const int64 X = N.X, Y = N.Y;
		N.Task = UE::Tasks::Launch(TEXT("SlonkTerrainChunk"), [P, Face, Depth, X, Y, Steps, Out]()
		{
			BuildChunk(P, Face, Depth, X, Y, Steps, *Out);
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
	if (MID && C->GetMaterial(0) != MID) { C->SetMaterial(0, MID); }
	N.Result.Reset();
	N.bReady = true;
}

void UPlanetTerrainComponent::UpdateView(const FVector& ViewLocationWorld)
{
	if (!bConfigured) { return; }
	++Stamp;
	VisibleChunks = 0;
	DeepestVisible = 0;
	const FVector CamLocal = GetComponentTransform().InverseTransformPositionNoScale(ViewLocationWorld);
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
}

double UPlanetTerrainComponent::SurfaceHeightAtWorld(const FVector& WorldPos) const
{
	if (!bConfigured) { return 0.0; }
	const FVector Local = GetComponentTransform().InverseTransformPositionNoScale(WorldPos);
	return SlonkTerrain::SurfaceHeightCm(Params, Local.GetSafeNormal());
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
		// Canyons and rims prefer temperate latitudes (no snow on the plateau) and sit away from the ice.
		const double Temperate = -3.0 * FMath::Abs(D.Z) - 6.0 * S.Ice;
		if (K == TEXT("canyon")) { return S.bWater ? -1e9 : S.Canyon * 10.0 + Temperate + Near * 0.5; }
		// The rim: top of a wall, just outside the cut, high up.
		if (K == TEXT("rim")) { return S.bWater ? -1e9 : S.Wall * 6.0 - S.Canyon * 8.0 + S.HeightCm / FMath::Max(Params.ReliefCm, 1.0) * 4.0 + Temperate + Near * 0.5; }
		if (K == TEXT("mountain")) { return S.bWater ? -1e9 : S.HeightCm / FMath::Max(Params.ReliefCm, 1.0) * 10.0 + Near * 0.5; }
		if (K == TEXT("coast")) { return S.bWater ? -1e9 : -FMath::Abs(S.HeightCm) / FMath::Max(Params.ReliefCm, 1.0) * 10.0 + Near * 3.0; }
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
	// Refine: hill-climb on a shrinking step so the result sits on the feature, not 30 km from it.
	double Step = CoarseWavelength / Params.RadiusCm;
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

void UPlanetTerrainComponent::BuildChunk(const FPlanetTerrainParams& P, int32 Face, int32 Depth, int64 X, int64 Y, int32 Steps, FBuildResult& Out)
{
	const double R = P.RadiusCm;
	const double Cells = (double)((int64)1 << Depth);
	const double U0 = -1.0 + 2.0 * X / Cells, U1 = -1.0 + 2.0 * (X + 1) / Cells;
	const double V0 = -1.0 + 2.0 * Y / Cells, V1 = -1.0 + 2.0 * (Y + 1) / Cells;
	auto Dir = [&](int32 i, int32 j)
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
	const double CellCm = FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir(0, Steps / 2), Dir(1, Steps / 2)), -1.0, 1.0)) * R;
	const double MinWavelength = CellCm * 2.0;

	// Sample a grid with a one-cell margin so the normals at the border use real neighbours.
	const int32 W = Steps + 3;
	TArray<FVector> Pos; Pos.SetNumUninitialized(W * W);
	TArray<FPlanetTerrainSample> Smp; Smp.SetNumUninitialized(W * W);
	TArray<FVector> Dirs; Dirs.SetNumUninitialized(W * W);
	for (int32 j = -1; j <= Steps + 1; ++j)
	{
		for (int32 i = -1; i <= Steps + 1; ++i)
		{
			const int32 k = (j + 1) * W + (i + 1);
			const FVector D = Dir(i, j);
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, D, MinWavelength);
			const double H = S.bWater ? 0.0 : S.HeightCm;
			Dirs[k] = D;
			Smp[k] = S;
			Pos[k] = D * (R + H) - Origin;
		}
	}

	TUniquePtr<FDynamicMesh3> M = MakeUnique<FDynamicMesh3>();
	M->EnableAttributes();
	M->Attributes()->SetNumUVLayers(3);
	M->Attributes()->EnablePrimaryColors();
	FDynamicMeshNormalOverlay* NO = M->Attributes()->PrimaryNormals();
	FDynamicMeshUVOverlay* UV0 = M->Attributes()->GetUVLayer(0);
	FDynamicMeshUVOverlay* UV1 = M->Attributes()->GetUVLayer(1);
	FDynamicMeshUVOverlay* UV2 = M->Attributes()->GetUVLayer(2);
	FDynamicMeshColorOverlay* CO = M->Attributes()->PrimaryColors();

	const int32 GW = Steps + 1;
	TArray<int32> Vid; Vid.SetNumUninitialized(GW * GW);
	TArray<FVector3f> Normals; Normals.SetNumUninitialized(GW * GW);
	auto AddVert = [&](const FVector& Pv, const FVector3f& Nv, const FVector2f& A, const FVector2f& B, const FVector2f& C, const FVector4f& Col)
	{
		const int32 V = M->AppendVertex(Pv);
		NO->AppendElement(Nv);
		UV0->AppendElement(A);
		UV1->AppendElement(B);
		UV2->AppendElement(C);
		CO->AppendElement(Col);
		return V;
	};
	FBox Bounds(ForceInit);
	for (int32 j = 0; j <= Steps; ++j)
	{
		for (int32 i = 0; i <= Steps; ++i)
		{
			const int32 k = (j + 1) * W + (i + 1);
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
			Vid[j * GW + i] = AddVert(Pos[k], Nf, FVector2f(HeightN, Slope), FVector2f(S.Wet, S.Rock), FVector2f(S.Ice, S.Canyon),
				FVector4f(S.Crater, S.bWater ? 1.f : 0.f, (float)FMath::Abs(D.Z), 1.f));
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
	// another depth. A few per cent of the edge covers the height a coarser neighbour can miss by.
	const double SkirtCm = FMath::Max(CellCm * 1.5, 100.0);
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
		const int32 k = (j + 1) * W + (i + 1);
		const FVector Pv = M->GetVertex(V) - Dirs[k] * SkirtCm;
		Lower.Add(AddVert(Pv, Normals[G], UV0->GetElement(V), UV1->GetElement(V), UV2->GetElement(V), CO->GetElement(V)));
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
