// Slonk - a procedural starship bridge built from engine primitives around the bridge camera, so the
// bridge view looks out of the viewscreen from the captain's chair past the helm and navigation stations.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "BridgeInteriorComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Attach to the ship so that this component's origin is the floor under the captain's chair, +X facing
 * the viewscreen. The bridge camera belongs at EyeOffset. Geometry is built once on BeginPlay from the
 * parameters below; everything is collision-free and hidden unless SetVisibility(true, true) is called.
 */
UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UBridgeInteriorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UBridgeInteriorComponent();

	virtual void BeginPlay() override;

	/** Where the seated captain's eyes are, relative to this component (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	FVector EyeOffset = FVector(10.0, 0.0, 185.0);

	/** Centre of the circular bridge relative to this component; the chair sits forward of centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	FVector BridgeCenter = FVector(-120.0, 0.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float WallRadius = 545.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float CeilingHeight = 285.f;

	/** Number of wall segments around the room. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge", meta = (ClampMin = "8", ClampMax = "32"))
	int32 WallSegments = 16;

	/** Wall segments (centred on the forward axis) left open as the viewscreen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge", meta = (ClampMin = "1", ClampMax = "7"))
	int32 ViewscreenSegments = 3;

	/** Bottom and top of the viewscreen opening (cm above the deck). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float ViewscreenBottom = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float ViewscreenTop = 262.f;

	/** Seated crew figures at the helm, navigation and perimeter stations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	bool bShowCrew = true;

	/** Point light intensity (candelas) of the overhead lights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float LightIntensity = 260.f;

	/** Emissive strength of the ceiling light strips. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float LightPanelEmissive = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	FLinearColor LightColor = FLinearColor(1.0f, 0.93f, 0.85f);

	/** Emissive strength of the console displays. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float DisplayIntensity = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Materials") TSoftObjectPtr<UMaterialInterface> LitMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Materials") TSoftObjectPtr<UMaterialInterface> DisplayMaterial;

	/** (Re)build the set. Safe to call again; existing pieces are destroyed first. */
	UFUNCTION(BlueprintCallable, Category = "Bridge")
	void Build();

	UFUNCTION(BlueprintPure, Category = "Bridge")
	bool IsBuilt() const { return Pieces.Num() > 0; }

private:
	UMaterialInstanceDynamic* MakeLit(const TCHAR* Name, FLinearColor Base, float Roughness, float Metallic = 0.f, FLinearColor Emissive = FLinearColor::Black, float EmissiveStrength = 0.f);
	UMaterialInstanceDynamic* MakeDisplay(const TCHAR* Name, float Seed, float Intensity);

	UStaticMeshComponent* Piece(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FRotator& Rot, UMaterialInterface* Mat, bool bShadow = true);
	UStaticMeshComponent* Box(const FVector& Center, const FVector& Size, const FRotator& Rot, UMaterialInterface* Mat, bool bShadow = true) { return Piece(CubeMesh, Center, Size, Rot, Mat, bShadow); }
	UStaticMeshComponent* Cyl(const FVector& Center, float Radius, float Height, UMaterialInterface* Mat, const FRotator& Rot = FRotator::ZeroRotator, bool bShadow = true) { return Piece(CylinderMesh, Center, FVector(Radius * 2.f, Radius * 2.f, Height), Rot, Mat, bShadow); }
	UStaticMeshComponent* Ball(const FVector& Center, float Radius, UMaterialInterface* Mat) { return Piece(SphereMesh, Center, FVector(Radius * 2.f), FRotator::ZeroRotator, Mat, true); }
	void Light(const FVector& Pos, float Intensity, float Radius);

	/** A station: console with a slanted display and a chair facing +Forward, at Pos (console front edge). */
	void Station(const FVector& ConsolePos, float YawDeg, float Width, bool bWallDisplay, bool bCrew, float Seed);
	void Chair(const FVector& FloorPos, float YawDeg, float SeatHeight, bool bCrew, bool bArmrests);
	void Crew(const FVector& SeatPos, float YawDeg);

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Pieces;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	// Palette, filled by Build().
	UMaterialInterface* Panel = nullptr;
	UMaterialInterface* DarkPanel = nullptr;
	UMaterialInterface* Trim = nullptr;
	UMaterialInterface* Carpet = nullptr;
	UMaterialInterface* PlatformCarpet = nullptr;
	UMaterialInterface* ChairMat = nullptr;
	UMaterialInterface* Uniform = nullptr;
	UMaterialInterface* Skin = nullptr;
	UMaterialInterface* Hair = nullptr;
	UMaterialInterface* LightPanel = nullptr;
	UMaterialInterface* Rail = nullptr;
	int32 PieceCount = 0;
};
