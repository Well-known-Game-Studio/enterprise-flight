// Slonk - building the meshes that are grown in code once a process (the plants in PlantMeshes.h, and whatever else is
// made of a few hundred triangles): a mesh of unshared vertices, each lit as itself, appended a vertex and a triangle
// at a time, and the transient static mesh made from its levels of detail.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

class UStaticMesh;

namespace SlonkProcMesh
{
	/** Vertex colour alpha of a solid surface (a stem, a trunk): the materials mask only the cards, by their own kinds. */
	constexpr float SolidSurface = 0.f;

	/** A mesh of unshared vertices, each with its own normal, UV and colour, so a blade or a card is lit as itself.
	 *  Elements are appended in step with the vertices, so an element's id is its vertex's. */
	struct FBuild
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		UE::Geometry::FDynamicMeshNormalOverlay* N = nullptr;
		UE::Geometry::FDynamicMeshUVOverlay* UV = nullptr;
		UE::Geometry::FDynamicMeshColorOverlay* C = nullptr;
		FBuild()
		{
			Mesh.EnableAttributes();
			Mesh.Attributes()->EnablePrimaryColors();
			N = Mesh.Attributes()->PrimaryNormals();
			UV = Mesh.Attributes()->PrimaryUV();
			C = Mesh.Attributes()->PrimaryColors();
		}
		int32 Vert(const FVector& P, const FVector& Nrm, const FVector2f& T, const FVector4f& Col)
		{
			const int32 V = Mesh.AppendVertex(P);
			N->AppendElement(FVector3f(Nrm.GetSafeNormal()));
			UV->AppendElement(T);
			C->AppendElement(Col);
			return V;
		}
		void Tri(int32 A, int32 B, int32 D)
		{
			const UE::Geometry::FIndex3i T(A, B, D);
			const int32 Tid = Mesh.AppendTriangle(T);
			if (Tid >= 0) { N->SetTriangle(Tid, T); UV->SetTriangle(Tid, T); C->SetTriangle(Tid, T); }
		}
		void Quad(int32 A, int32 B, int32 D, int32 E) { Tri(A, B, D); Tri(A, D, E); }

		/** A tapering tube along a path (a trunk, a branch, a stem): rings of Sides, normals out from the axis. Bark, unless
		 *  Part says it is foliage (1: a strange plant's pipe, drawn in its tint). */
		void Tube(const TArray<FVector>& Path, const TArray<double>& Radii, int32 Sides, double Sway0, double Sway1, float Rand, float Part = 0.f)
		{
			if (Path.Num() < 2 || Sides < 3) { return; }
			FVector Ax = FVector::ZeroVector;
			TArray<int32> Prev;
			for (int32 r = 0; r < Path.Num(); ++r)
			{
				const FVector T = (Path[FMath::Min(r + 1, Path.Num() - 1)] - Path[FMath::Max(r - 1, 0)]).GetSafeNormal();
				// The ring's frame is carried along the path, so the tube does not twist.
				if (Ax.IsZero()) { Ax = FVector::CrossProduct(T, FMath::Abs(T.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector).GetSafeNormal(); }
				Ax = (Ax - T * FVector::DotProduct(Ax, T)).GetSafeNormal();
				const FVector Ay = FVector::CrossProduct(T, Ax);
				const double F = (double)r / (Path.Num() - 1);
				const float Sw = (float)FMath::Lerp(Sway0, Sway1, F);
				TArray<int32> Ring;
				for (int32 s = 0; s <= Sides; ++s)
				{
					const double A = UE_DOUBLE_TWO_PI * s / Sides;
					const FVector D = Ax * FMath::Cos(A) + Ay * FMath::Sin(A);
					Ring.Add(Vert(Path[r] + D * Radii[r], D, FVector2f((float)s / Sides, (float)F), FVector4f(Part, Sw, Rand, SolidSurface)));
				}
				if (Prev.Num() > 0)
				{
					// Wound outward, its front the side its normals point to, as every part of a plant is (the material is
					// two-sided, so the other winding draws too, its faces seen from behind).
					for (int32 s = 0; s < Sides; ++s) { Quad(Prev[s], Ring[s], Ring[s + 1], Prev[s + 1]); }
				}
				Prev = MoveTemp(Ring);
			}
		}

		/** A leaf card: a quad facing Out, its normals bent toward Bend (out from the crown) so a crown is lit as a
		 *  mass rather than as a hundred flat planes. */
		void Card(const FVector& Centre, const FVector& Out, const FVector& Right, double W, double H, const FVector& Bend, float SwayLo, float SwayHi, float Rand, float Kind)
		{
			const FVector Up = FVector::CrossProduct(Out, Right).GetSafeNormal();
			const FVector R = Right * (W * 0.5), U = Up * (H * 0.5);
			const FVector Corner[4] = { Centre - R - U, Centre + R - U, Centre + R + U, Centre - R + U };
			const FVector2f T[4] = { FVector2f(0.f, 0.f), FVector2f(1.f, 0.f), FVector2f(1.f, 1.f), FVector2f(0.f, 1.f) };
			int32 V[4];
			for (int32 i = 0; i < 4; ++i)
			{
				const FVector Nrm = Bend * 0.65 + Out * 0.35 + (Corner[i] - Centre).GetSafeNormal() * 0.2;
				V[i] = Vert(Corner[i], Nrm, T[i], FVector4f(1.f, i < 2 ? SwayLo : SwayHi, Rand, Kind));
			}
			// Its front toward Out, the side its normals lean to, so the crown is lit from outside.
			Quad(V[0], V[3], V[2], V[1]);
		}
	};

	/**
	 * A static mesh from levels of detail built as FBuild meshes, LOD 0 first: one material slot (SlotName), each LOD's
	 * screen size from ScreenSizes (the engine's: the diameter over the screen's of the sphere round the bounds), and the
	 * bounds grown by BoundsExtension each way, for a world position offset that moves the mesh past its own box (the
	 * instances are culled by the bounds). Transient and rooted, so it survives PIE sessions; never reference it from
	 * anything saved. No collision: the caller adds its own body setup. With bTangents each vertex is given a tangent at
	 * right angles to its normal (the mesh's +x laid on its surface, its +y where the normal runs along x), so a
	 * material can turn its normals in tangent space; else its tangents are left at zero, as the plants' are.
	 */
	SLONK_API UStaticMesh* MakeStaticMesh(FName Name, FName SlotName, TArrayView<const UE::Geometry::FDynamicMesh3> Lods, TArrayView<const float> ScreenSizes, const FVector& BoundsExtension,
		bool bTangents = false);
}
