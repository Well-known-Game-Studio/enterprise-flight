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
	// Gore k of 8 sits at azimuth (k - 4 + 0.5) * 15.5 deg from the centre line, port negative. Opening swings
	// the innermost pair SwingDeg and each pair outward one gore's width less, so all eight end stacked beyond the
	// door's edge; a growing radial scale keeps the stack from fighting the panels it slides over.
	const int32 N = Gores.Num();
	for (int32 k = 0; k < N; ++k)
	{
		UStaticMeshComponent* G = Gores[k];
		if (!G) { continue; }
		const float Idx = k - N * 0.5f + 0.5f;
		const float Rank = FMath::Abs(Idx) - 0.5f;
		const float Swing = (SwingDeg - Rank * (2.f * SwingDeg / N)) * Fraction;
		// Azimuth grows toward starboard; a positive yaw about the ship's up takes a point astern toward port.
		const float Yaw = Idx < 0.f ? Swing : -Swing;
		const float S = 1.f + (0.01f + 0.012f * Rank) * Fraction;
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
