#include "FaunaMeshes.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "ProcMeshBuild.h"

using namespace UE::Geometry;

namespace
{
	using SlonkProcMesh::FBuild;
	namespace Vc = SlonkFauna::Vc;

	TMap<uint32, UStaticMesh*> GFaunaMeshes;

	FVector4f VertexCol(float Surface, float Flex, float Side, float Joint) { return FVector4f(Surface, Flex, Side, Joint); }

	/** A triangle wound so its front (the side the renderer shows, as the mesh reads its winding) faces Facing. */
	void TriFacing(FBuild& B, int32 A, int32 Bv, int32 C, const FVector& Facing)
	{
		const FVector Pa = (FVector)B.Mesh.GetVertex(A), Pb = (FVector)B.Mesh.GetVertex(Bv), Pc = (FVector)B.Mesh.GetVertex(C);
		// FDynamicMesh3's own triangle normal (VectorUtil::Normal), for the engine's left-handed frame.
		const FVector N = FVector::CrossProduct(Pc - Pa, Pb - Pa);
		if (FVector::DotProduct(N, Facing) >= 0.0) { B.Tri(A, Bv, C); }
		else { B.Tri(A, C, Bv); }
	}

	/** A wing's colour at a point of it: its flex from the hinge to the tip, its side. */
	FVector4f WingCol(double Y)
	{
		const double Tip = SlonkFauna::UnitCm * 0.5;
		const float Flex = (float)FMath::Clamp((FMath::Abs(Y) - Vc::WingRootCm) / (Tip - Vc::WingRootCm), 0.0, 1.0);
		return VertexCol(Vc::SurfaceWing, Flex, Y > 0.0 ? Vc::SideRight : Vc::SideLeft, Vc::JointWing);
	}

	/**
	 * A bird of the plan, at a LOD: 60, 12 or 2 triangles. Its body a spindle along x (five sides and four rings, a cone
	 * to the nose and one to the rump), a bill, a fanned tail, and two wings each of three panels and a tip from the hinge
	 * on either side of the back to the tip half a span out; at LOD 1 a three-sided spindle, a panel a wing and the tail;
	 * at LOD 2 a triangle a wing, which still beats.
	 */
	void BuildBird(FBuild& B, const FFaunaBodyPlan& Plan, int32 Lod)
	{
		const double U = SlonkFauna::UnitCm;
		const double L = Plan.Length * U;
		const double Rg = FMath::Max(Plan.Girth * U * 0.5, 1.0);
		const double Nose = 0.36 * L, Rear = -0.4 * L;
		const double Root = Vc::WingRootCm, Tip = U * 0.5;
		const double Chord = FMath::Max(Plan.Chord * U, 4.0);
		const double TailRoot = -0.27 * L, TailLen = FMath::Max(Plan.Tail * U, 2.0);
		const FVector4f BodyC = VertexCol(Vc::SurfaceBody, 0.f, Vc::SideMiddle, Vc::JointRigid);
		const double WingZ = 0.3 * Rg;
		if (Lod >= 2)
		{
			for (const double S : { -1.0, 1.0 })
			{
				const int32 A = B.Vert(FVector(8.0, S * Root, WingZ), FVector::ZAxisVector, FVector2f(0.f, 0.f), WingCol(S * Root));
				const int32 Bv = B.Vert(FVector(Rear - 4.0, S * Root, WingZ), FVector::ZAxisVector, FVector2f(1.f, 0.f), WingCol(S * Root));
				const int32 C = B.Vert(FVector(-4.0, S * Tip, WingZ), FVector::ZAxisVector, FVector2f(0.5f, 1.f), WingCol(S * Tip));
				TriFacing(B, A, Bv, C, FVector::ZAxisVector);
			}
			return;
		}
		// The body.
		const int32 Sides = Lod == 0 ? 5 : 3;
		const TArray<double> Stations = Lod == 0 ? TArray<double>{ 0.12, 0.42, 0.72, 0.9 } : TArray<double>{ 0.5 };
		const TArray<double> Radii = Lod == 0 ? TArray<double>{ 0.55, 1.0, 0.85, 0.55 } : TArray<double>{ 1.0 };
		TArray<TArray<int32>> Rings;
		TArray<double> RingX;
		for (int32 r = 0; r < Stations.Num(); ++r)
		{
			const double X = FMath::Lerp(Rear, Nose, Stations[r]);
			TArray<int32> Ring;
			for (int32 k = 0; k < Sides; ++k)
			{
				const double A = UE_DOUBLE_TWO_PI * k / Sides + UE_DOUBLE_HALF_PI;
				const FVector D(0.0, FMath::Cos(A), FMath::Sin(A));
				Ring.Add(B.Vert(FVector(X, D.Y * Rg * Radii[r], D.Z * Rg * Radii[r] * 0.85), D, FVector2f((float)k / Sides, (float)Stations[r]), BodyC));
			}
			Rings.Add(Ring);
			RingX.Add(X);
		}
		auto Centroid = [&B](int32 A, int32 Bv, int32 C) { return ((FVector)B.Mesh.GetVertex(A) + (FVector)B.Mesh.GetVertex(Bv) + (FVector)B.Mesh.GetVertex(C)) / 3.0; };
		for (int32 r = 0; r + 1 < Rings.Num(); ++r)
		{
			for (int32 k = 0; k < Sides; ++k)
			{
				const int32 A = Rings[r][k], Bv = Rings[r][(k + 1) % Sides], C = Rings[r + 1][(k + 1) % Sides], Dv = Rings[r + 1][k];
				const FVector M1 = Centroid(A, Bv, C), M2 = Centroid(A, C, Dv);
				TriFacing(B, A, Bv, C, FVector(0.0, M1.Y, M1.Z));
				TriFacing(B, A, C, Dv, FVector(0.0, M2.Y, M2.Z));
			}
		}
		// Coned to the head and to the rump, each from a point inside the body.
		const int32 Head = B.Vert(FVector(Lod == 0 ? Nose : Nose + 3.0, 0.0, 0.0), FVector::XAxisVector, FVector2f(0.5f, 1.f), BodyC);
		const int32 Rump = B.Vert(FVector(Rear, 0.0, 0.0), -FVector::XAxisVector, FVector2f(0.5f, 0.f), BodyC);
		for (int32 k = 0; k < Sides; ++k)
		{
			const TArray<int32>& Front = Rings.Last();
			const TArray<int32>& Back = Rings[0];
			TriFacing(B, Front[k], Front[(k + 1) % Sides], Head, Centroid(Front[k], Front[(k + 1) % Sides], Head) - FVector(RingX.Last() - 2.0, 0.0, 0.0));
			TriFacing(B, Back[k], Back[(k + 1) % Sides], Rump, Centroid(Back[k], Back[(k + 1) % Sides], Rump) - FVector(RingX[0] + 2.0, 0.0, 0.0));
		}
		if (Lod == 0)
		{
			// The bill, a three-sided point from inside the head.
			const FVector4f BillC = VertexCol(Vc::SurfaceBeak, 0.f, Vc::SideMiddle, Vc::JointRigid);
			const double BillLen = 0.11 * L;
			const int32 B0 = B.Vert(FVector(Nose - 1.0, -0.3 * Rg, -0.15 * Rg), FVector(0.0, -1.0, -0.4), FVector2f(0.f, 0.f), BillC);
			const int32 B1 = B.Vert(FVector(Nose - 1.0, 0.3 * Rg, -0.15 * Rg), FVector(0.0, 1.0, -0.4), FVector2f(1.f, 0.f), BillC);
			const int32 B2 = B.Vert(FVector(Nose - 1.0, 0.0, 0.3 * Rg), FVector(0.0, 0.0, 1.0), FVector2f(0.5f, 0.f), BillC);
			const int32 BT = B.Vert(FVector(Nose + BillLen, 0.0, -0.05 * Rg), FVector::XAxisVector, FVector2f(0.5f, 1.f), BillC);
			const FVector Inside(Nose - 1.0, 0.0, 0.0);
			TriFacing(B, B0, B1, BT, Centroid(B0, B1, BT) - Inside);
			TriFacing(B, B1, B2, BT, Centroid(B1, B2, BT) - Inside);
			TriFacing(B, B2, B0, BT, Centroid(B2, B0, BT) - Inside);
		}
		// The tail: a fan behind the rump, notched at LOD 0, flexing from its root.
		{
			auto TailC = [&](double X) { return VertexCol(Vc::SurfaceTail, (float)FMath::Clamp((TailRoot - X) / TailLen, 0.0, 1.0), Vc::SideMiddle, Vc::JointTail); };
			const double TZ = 0.2 * Rg, End = TailRoot - TailLen;
			const int32 RL = B.Vert(FVector(TailRoot, -0.55 * Rg, TZ), FVector::ZAxisVector, FVector2f(0.f, 0.f), TailC(TailRoot));
			const int32 RR = B.Vert(FVector(TailRoot, 0.55 * Rg, TZ), FVector::ZAxisVector, FVector2f(1.f, 0.f), TailC(TailRoot));
			const int32 TL = B.Vert(FVector(End, -1.45 * Rg, TZ), FVector::ZAxisVector, FVector2f(0.f, 1.f), TailC(End));
			const int32 TR = B.Vert(FVector(End, 1.45 * Rg, TZ), FVector::ZAxisVector, FVector2f(1.f, 1.f), TailC(End));
			if (Lod == 0)
			{
				const double Notch = TailRoot - 0.85 * TailLen;
				const int32 TC = B.Vert(FVector(Notch, 0.0, TZ), FVector::ZAxisVector, FVector2f(0.5f, 0.85f), TailC(Notch));
				TriFacing(B, RL, RR, TC, FVector::ZAxisVector);
				TriFacing(B, RL, TC, TL, FVector::ZAxisVector);
				TriFacing(B, RR, TR, TC, FVector::ZAxisVector);
			}
			else
			{
				TriFacing(B, RL, RR, TR, FVector::ZAxisVector);
				TriFacing(B, RL, TR, TL, FVector::ZAxisVector);
			}
		}
		// The wings: from the hinge out, the leading edge swept back a little toward the tip, the chord narrowing.
		for (const double S : { -1.0, 1.0 })
		{
			auto V = [&](double X, double Y, float UVx, float UVy) { return B.Vert(FVector(X, S * Y, WingZ), FVector::ZAxisVector, FVector2f(UVx, UVy), WingCol(S * Y)); };
			if (Lod == 0)
			{
				const double Ys[4] = { Root, 18.0, 34.0, 46.0 };
				const double Lead[4] = { 5.0, 5.0, 3.0, -1.0 };
				const double Trail[4] = { 5.0 - Chord, 5.0 - 0.92 * Chord, 3.0 - 0.67 * Chord, -1.0 - 0.33 * Chord };
				int32 Le[4], Te[4];
				for (int32 i = 0; i < 4; ++i)
				{
					const float Fy = (float)((Ys[i] - Root) / (Tip - Root));
					Le[i] = V(Lead[i], Ys[i], 0.f, Fy);
					Te[i] = V(Trail[i], Ys[i], 1.f, Fy);
				}
				for (int32 i = 0; i < 3; ++i)
				{
					TriFacing(B, Le[i], Te[i], Te[i + 1], FVector::ZAxisVector);
					TriFacing(B, Le[i], Te[i + 1], Le[i + 1], FVector::ZAxisVector);
				}
				TriFacing(B, Le[3], Te[3], V(-4.0 - 0.15 * Chord, Tip, 0.5f, 1.f), FVector::ZAxisVector);
			}
			else
			{
				const int32 A = V(5.0, Root, 0.f, 0.f), Bv = V(5.0 - Chord, Root, 1.f, 0.f), C = V(-7.0, Tip, 1.f, 1.f), Dv = V(-2.0, Tip, 0.f, 1.f);
				TriFacing(B, A, Bv, C, FVector::ZAxisVector);
				TriFacing(B, A, C, Dv, FVector::ZAxisVector);
			}
		}
	}
}

bool SlonkFaunaMeshes::BuildLod(const FFaunaBodyPlan& Plan, int32 Lod, FDynamicMesh3& Out)
{
	if (Plan.Wings == EFaunaWings::None) { return false; }
	FBuild B;
	BuildBird(B, Plan, FMath::Clamp(Lod, 0, NumLods - 1));
	Out = MoveTemp(B.Mesh);
	return true;
}

UStaticMesh* SlonkFaunaMeshes::GetMesh(const FFaunaBodyPlan& Plan, EFaunaKind Kind)
{
	const uint32 Key = HashCombine(Plan.Hash(), GetTypeHash((uint8)Kind));
	if (UStaticMesh* const* Found = GFaunaMeshes.Find(Key))
	{
		if (*Found && IsValid(*Found)) { return *Found; }
	}
	FDynamicMesh3 Lods[NumLods];
	for (int32 L = 0; L < NumLods; ++L)
	{
		if (!BuildLod(Plan, L, Lods[L])) { return nullptr; }
	}
	// The material moves the vertices past the mesh's box by up to the kind's padding, which the instances' boxes take in
	// (UPlanetInstancesComponent::SetCullPadding): the mesh's own bounds stay its own, and its LOD screen sizes are the
	// kind's, set for the padded box the GPU picks them by.
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(Kind);
	UStaticMesh* Mesh = SlonkProcMesh::MakeStaticMesh(*FString::Printf(TEXT("SlonkFauna_%s_%08x"), SlonkFauna::KindName(Kind), Key), TEXT("Fauna"),
		TArrayView<const FDynamicMesh3>(Lods, NumLods), TArrayView<const float>(D.LodScreenSize, NumLods), FVector::ZeroVector);
	GFaunaMeshes.Add(Key, Mesh);
	return Mesh;
}

void SlonkFaunaMeshes::BuildMoteCloudLod(int32 Lod, FDynamicMesh3& Out)
{
	namespace Mo = SlonkFauna::Mote;
	Lod = FMath::Clamp(Lod, 0, NumLods - 1);
	FBuild B;
	const int32 Cards = Mo::CardsAtLod[Lod];
	const int32 Gnats = Lod == 0 ? Mo::GnatsPerCloud : (Lod == 1 ? 2 : 0);
	const int32 Spread = Cards - Gnats;
	const int32 Free = Mo::MotesPerCloud - Mo::GnatsPerCloud;
	const double H = Mo::CardHalfCm;
	auto Card = [&](const FVector& C, float Stands, float Rnd)
	{
		const FVector4f Col(Vc::SurfaceMote, Stands / (float)Mo::MotesPerCloud, Rnd, Vc::JointRigid);
		int32 V[4];
		for (int32 k = 0; k < 4; ++k)
		{
			const float U = (k == 1 || k == 2) ? 1.f : 0.f, Vv = k >= 2 ? 1.f : 0.f;
			V[k] = B.Vert(C + FVector((2.0 * U - 1.0) * H, 0.0, (2.0 * Vv - 1.0) * H), FVector::ZAxisVector, FVector2f(U, Vv), Col);
		}
		// Wound as a leaf card is (FBuild::Card): the material lays a card's U to the camera's right and its V up, so the
		// camera sees its front, and the two-sided shading keeps its normal up rather than turning it into the ground.
		B.Quad(V[0], V[3], V[2], V[1]);
	};
	// The gnats' swarm: a ball 8 cm of the unit mesh across (about half a metre at a cloud's span), off the middle, about
	// a fifth of a span up; their randoms under GnatShare.
	const FVector Swarm(9.0, -7.0, 21.0);
	for (int32 i = 0; i < Gnats; ++i)
	{
		const double A = i * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0)), Z = 1.0 - 2.0 * (i + 0.5) / Gnats, Rr = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0));
		Card(Swarm + FVector(Rr * FMath::Cos(A), Rr * FMath::Sin(A), Z) * 4.0, (float)Mo::GnatsPerCloud / Gnats, (0.5f + i) / Gnats * (Mo::GnatShare - 0.02f));
	}
	// The rest spread over a disc half a span round (a sunflower's spiral), at heights spread through 0.04 to 0.3 of a
	// span; their randoms through the rest of the range, each its own.
	for (int32 j = 0; j < Spread; ++j)
	{
		const double Rr = 45.0 * FMath::Sqrt((j + 0.5) / Spread), A = j * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0)) + 0.7 * Lod;
		const double Z = 4.0 + 26.0 * FMath::Frac(j * 0.6180339887 + 0.37 + 0.11 * Lod);
		const float Rnd = Mo::GnatShare + 0.02f + (1.f - Mo::GnatShare - 0.04f) * (float)FMath::Frac((j + 0.5) / Spread * 0.999 + 0.29 * Lod);
		Card(FVector(Rr * FMath::Cos(A), Rr * FMath::Sin(A), Z), (float)Free / Spread, Rnd);
	}
	Out = MoveTemp(B.Mesh);
}

UStaticMesh* SlonkFaunaMeshes::GetMoteCloudMesh()
{
	static UStaticMesh* Cached = nullptr;
	if (Cached && IsValid(Cached)) { return Cached; }
	FDynamicMesh3 Lods[NumLods];
	for (int32 L = 0; L < NumLods; ++L) { BuildMoteCloudLod(L, Lods[L]); }
	Cached = SlonkProcMesh::MakeStaticMesh(TEXT("SlonkFauna_MoteCloud"), TEXT("Fauna"), TArrayView<const FDynamicMesh3>(Lods, NumLods),
		TArrayView<const float>(MoteLodScreenSize, NumLods), FVector(SlonkFauna::Mote::ReachCm));
	return Cached;
}
