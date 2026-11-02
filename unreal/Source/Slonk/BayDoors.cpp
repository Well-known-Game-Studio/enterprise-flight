// Slonk - the shuttlebay's clamshell door. See BayDoors.h.
#include "BayDoors.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

UBayDoorsComponent::UBayDoorsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	OuterMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Enterprise/Constitution_3.Constitution_3")));
}

bool UBayDoorsComponent::Build()
{
	TArray<UStaticMesh*> Meshes;
	for (int32 k = 1; k <= 8; ++k)
	{
		const FString Path = FString::Printf(TEXT("%s/SM_BayDoor%d.SM_BayDoor%d"), *MeshFolder, k, k);
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Path);
		if (!M) { UE_LOG(LogTemp, Warning, TEXT("BayDoors: %s missing, no door"), *Path); return false; }
		Meshes.Add(M);
	}
	for (int32 k = 0; k < Meshes.Num(); ++k)
	{
		UStaticMeshComponent* G = NewObject<UStaticMeshComponent>(GetOwner(), *FString::Printf(TEXT("BayDoorGore%d"), k + 1));
		G->SetStaticMesh(Meshes[k]);
		const int32 Outer = G->GetMaterialIndex(TEXT("Constitution_3"));
		if (Outer != INDEX_NONE && !G->GetMaterial(Outer)) { if (UMaterialInterface* M = OuterMaterial.LoadSynchronous()) { G->SetMaterial(Outer, M); } }
		G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		G->SetMobility(EComponentMobility::Movable);
		G->CastShadow = true;
		G->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
		G->RegisterComponent();
		Gores.Add(G);
	}
	Apply();
	return true;
}

void UBayDoorsComponent::SetShown(bool bShown)
{
	for (UStaticMeshComponent* G : Gores) { if (G) { G->SetVisibility(bShown); } }
}

void UBayDoorsComponent::Apply()
{
	// The door is a half cone about its vertical axis, cut into N gores of 180/N degrees from port to starboard.
	// Opening swings each pair toward its own side: the innermost pair by the width of the three gores outside it,
	// the next by two, then one, the outermost not at all, so all N end stacked in the outermost gore's place and
	// the arch between them is clear. The stack nests inward (a shrinking radial scale), away from the hull.
	const int32 N = Gores.Num();
	if (N == 0) { return; }
	const float Width = 180.f / N;
	for (int32 k = 0; k < N; ++k)
	{
		UStaticMeshComponent* G = Gores[k];
		if (!G) { continue; }
		const float Idx = k - N * 0.5f + 0.5f;        // -3.5 .. 3.5 for eight gores
		const float Rank = FMath::Abs(Idx) - 0.5f;     // 0 innermost .. N/2 - 1 outermost
		const float Swing = (N * 0.5f - 1.f - Rank) * Width * Fraction;
		// Azimuth grows toward starboard; a positive yaw about the ship's up takes a point astern toward port.
		const float Yaw = Idx < 0.f ? Swing : -Swing;
		const float S = 1.f - 0.012f * (N * 0.5f - 1.f - Rank) * Fraction;
		G->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
		G->SetRelativeScale3D(FVector(S, S, 1.f));
	}
}

void UBayDoorsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (FMath::IsNearlyEqual(Fraction, Target)) { return; }
	const float Step = DeltaTime / FMath::Max(OpenSeconds, 0.1f);
	Fraction = FMath::FInterpConstantTo(Fraction, Target, 1.f, Step);
	Apply();
}
