// Slonk - a procedural starship bridge in the style of the original Enterprise: a sunken central well
// with the captain's chair and the two-seat helm/navigation console, a raised outer deck edged in red with
// red handrails, a continuous ring of sloped button consoles under a band of monitors, blue-grey upper
// walls with framed viewers, and swivel chairs on white pedestals. Built from revolved profiles
// (GeometryScript) plus engine primitives around the bridge camera.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "BridgeInteriorComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UDynamicMeshComponent;
class UPointLightComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EBridgeStyle : uint8
{
	/** The original Constitution-class bridge: sunken well, red rails, blue-grey walls, button consoles. */
	Original,
	/** The Enterprise-A bridge of Star Trek VI: light-grey panelling, white light strips, teal displays, grey chairs, burgundy turbolift doors. */
	Refit
};

/**
 * Attach to the ship so that this component's origin is the well floor under the captain's chair (the
 * centre of the bridge), +X facing the viewscreen. The bridge camera belongs at EyeOffset. Geometry is
 * built once on BeginPlay; everything is collision-free and hidden unless SetVisibility(true, true).
 * Angles below are yaw degrees: 0 = toward the viewscreen, 90 = starboard, 180 = aft.
 */
UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UBridgeInteriorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UBridgeInteriorComponent();

	virtual void BeginPlay() override;

	/** Which bridge to build. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	EBridgeStyle Style = EBridgeStyle::Refit;

	/** Where the seated captain's eyes are, relative to this component (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	FVector EyeOffset = FVector(12.0, 0.0, 165.0);

	/** Radius of the wall / console ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float WallRadius = 560.f;

	/** Inner radius of the raised outer deck (the central well is inside this). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float DeckRadius = 335.f;

	/** Height of the outer deck above the well floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float DeckHeight = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float CeilingHeight = 300.f;

	/** Half-angle of the viewscreen opening in the forward wall (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float ViewscreenHalfAngle = 34.f;

	/** Bottom and top of the viewscreen opening (cm above the well floor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float ViewscreenBottom = 85.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float ViewscreenTop = 275.f;

	/** Bearing of the turbolift alcove (degrees, aft-port by default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float TurboliftAngle = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float TurboliftHalfAngle = 13.f;

	/** Number of monitor / viewer bays around the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge", meta = (ClampMin = "8", ClampMax = "32"))
	int32 WallBays = 16;

	/** Seated crew figures at the helm, navigation and some perimeter stations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	bool bShowCrew = true;

	/** Point light intensity (candelas) of the overhead lights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float LightIntensity = 150.f;

	/** Emissive strength of the ceiling light ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float LightPanelEmissive = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	FLinearColor LightColor = FLinearColor(1.0f, 0.95f, 0.9f);

	/** Emissive strength of the button panels, monitors and viewers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge")
	float DisplayIntensity = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Materials") TSoftObjectPtr<UMaterialInterface> LitMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Materials") TSoftObjectPtr<UMaterialInterface> DisplayMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Materials") TSoftObjectPtr<UMaterialInterface> ButtonsMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Materials") TSoftObjectPtr<UMaterialInterface> ViewerMaterial;

	/** (Re)build the set. Safe to call again; existing pieces are destroyed first. */
	UFUNCTION(BlueprintCallable, Category = "Bridge")
	void Build();

	UFUNCTION(BlueprintCallable, Category = "Bridge")
	void SetStyle(EBridgeStyle NewStyle) { Style = NewStyle; Build(); }

	UFUNCTION(BlueprintPure, Category = "Bridge")
	bool IsBuilt() const { return Pieces.Num() > 0; }

private:
	void BuildOriginal();
	void BuildRefit();
	/** Refit furniture: a grey upholstered swivel chair on a black pedestal, optionally with a crew member. */
	void RefitChair(const FVector& FloorPos, float YawDeg, bool bCrew, UMaterialInterface* UniformMat);
	/** Refit perimeter station: light-grey cabinet with three light bars at its foot, a black desk and a large raked display bank. */
	void RefitStation(float AngleDeg, float Chord, float& Seed, bool bConsole);
	/** A teal-on-black display instance with the refit palette and sparse cells. */
	UMaterialInstanceDynamic* RefitScreen(const TCHAR* Name, float Seed, float Intensity, float CellScale, float Gap);
	/** Three stacked white light bars on a vertical face at a bearing (the refit's kick lighting). */
	void LightBars(float AngleDeg, float Radial, float Z, float Width, int32 Count = 3, float Pitch = 11.f);
	UMaterialInstanceDynamic* MakeLit(const TCHAR* Name, FLinearColor Base, float Roughness, float Metallic = 0.f, FLinearColor Emissive = FLinearColor::Black, float EmissiveStrength = 0.f);
	UMaterialInstanceDynamic* MakeScreen(const TSoftObjectPtr<UMaterialInterface>& Parent, const TCHAR* Name, float Seed, float Intensity, float CellScale = 1.f);

	void PrepPrimitive(class UPrimitiveComponent* C, bool bShadow);
	UStaticMeshComponent* Piece(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FRotator& Rot, UMaterialInterface* Mat, bool bShadow = true);
	UStaticMeshComponent* Box(const FVector& Center, const FVector& Size, const FRotator& Rot, UMaterialInterface* Mat, bool bShadow = true) { return Piece(CubeMesh, Center, Size, Rot, Mat, bShadow); }
	UStaticMeshComponent* Cyl(const FVector& Center, float Radius, float Height, UMaterialInterface* Mat, const FRotator& Rot = FRotator::ZeroRotator, bool bShadow = true) { return Piece(CylinderMesh, Center, FVector(Radius * 2.f, Radius * 2.f, Height), Rot, Mat, bShadow); }
	UStaticMeshComponent* Ball(const FVector& Center, float Radius, UMaterialInterface* Mat) { return Piece(SphereMesh, Center, FVector(Radius * 2.f), FRotator::ZeroRotator, Mat, true); }
	/** Revolve a counter-clockwise (radius, height) profile about the vertical axis through the origin over [StartDeg, EndDeg]. */
	UDynamicMeshComponent* Revolve(const TArray<FVector2D>& Profile, float StartDeg, float EndDeg, UMaterialInterface* Mat, bool bShadow = true);
	void Light(const FVector& Pos, float Intensity, float Radius);

	/** A box lying on the wall at the given bearing: Radial = distance from the centre, Width tangential, Height vertical. */
	UStaticMeshComponent* WallBox(float AngleDeg, float Radial, float Z, float Thickness, float Width, float Height, UMaterialInterface* Mat, float PitchDeg = 0.f, bool bShadow = true);
	void SwivelChair(const FVector& FloorPos, float YawDeg, bool bCrew, UMaterialInterface* UniformMat);
	void Crew(const FVector& SeatPos, float YawDeg, UMaterialInterface* UniformMat);

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Pieces;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	// Palette, filled by Build().
	UMaterialInterface* WallBlue = nullptr;
	UMaterialInterface* CeilingGrey = nullptr;
	UMaterialInterface* Deck = nullptr;
	UMaterialInterface* WellFloor = nullptr;
	UMaterialInterface* ConsoleBlack = nullptr;
	UMaterialInterface* Red = nullptr;
	UMaterialInterface* LightGrey = nullptr;
	UMaterialInterface* ChairBlack = nullptr;
	UMaterialInterface* Skin = nullptr;
	UMaterialInterface* Hair = nullptr;
	UMaterialInterface* LightPanel = nullptr;
	UMaterialInterface* UniformGold = nullptr;
	UMaterialInterface* UniformBlue = nullptr;
	UMaterialInterface* UniformRed = nullptr;
	// Refit palette.
	UMaterialInterface* PanelGrey = nullptr;
	UMaterialInterface* PanelDark = nullptr;
	UMaterialInterface* Carpet = nullptr;
	UMaterialInterface* StripWhite = nullptr;
	UMaterialInterface* Burgundy = nullptr;
	UMaterialInterface* SeatGrey = nullptr;
	UMaterialInterface* RailGrey = nullptr;
	UMaterialInterface* ScreenDark = nullptr;
	UMaterialInterface* UniformMaroon = nullptr;
	UMaterialInterface* ClockRed = nullptr;
	int32 PieceCount = 0;
};
