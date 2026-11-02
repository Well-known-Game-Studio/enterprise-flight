// Slonk - the ship's interior as rooms you can walk: main engineering, sickbay, the shuttle bay and the transporter
// room, built from engine primitives at the places the compartment map puts them, each shown only while someone is in
// it. The bridge is the modelled set on UBridgeInteriorComponent; the turbolift joins them all.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ShipInterior.generated.h"

class AStarshipPawn;
class UStaticMesh;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** The places the turbolift goes. Airlock is the saucer hatch: the lift puts you out on the hull. */
UENUM(BlueprintType)
enum class EShipRoom : uint8
{
	Bridge,
	Engineering,
	Sickbay,
	ShuttleBay,
	Transporter,
	Airlock,
	Count UMETA(Hidden)
};

/** A room: its box in pawn space, where the lift lets you out, and the one thing in it to use. */
struct FShipRoom
{
	EShipRoom Id = EShipRoom::Count;
	FString Name;
	FString Deck;
	/** Centre and half-size of the walkable box, pawn space (cm). */
	FVector Center = FVector::ZeroVector;
	FVector Half = FVector::ZeroVector;
	/** Feet on the floor by the lift door, facing into the room (pawn space). */
	FTransform Lift;
	/** Feet at the room's thing to use (the chair, the pad, the shuttle's door, the master console), facing it. */
	FTransform Use;
	/** What F does there, for the HUD: "sit down", "energise", "board the shuttle", "open the board". */
	FString UseLabel;
	/** Everything drawn for the room, and the subset that stands in the way of the Boots channel. */
	TArray<TWeakObjectPtr<USceneComponent>> Pieces;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Solids;
};

/**
 * Attach to the ship's root. Build() reads the ship's length and bay from the owner and lays the rooms out in pawn
 * space; ShowRoom() turns one room's pieces visible and solid to the Boots channel (and all the others off), so hull
 * walkers and the airlock trace never meet a wall that is not around them.
 */
UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UShipInteriorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UShipInteriorComponent();
	virtual void BeginPlay() override;

	void Build();
	bool IsBuilt() const { return Rooms.Num() > 0; }
	const FShipRoom* Room(EShipRoom Which) const;
	/** Show one room (or none with Count): visibility and Boots collision together. */
	void ShowRoom(EShipRoom Which);
	EShipRoom Shown() const { return ShownRoom; }
	static FString RoomName(EShipRoom Which);
	static FString DeckOf(EShipRoom Which);
	/** The room a pawn-space point is in, or Count. */
	EShipRoom RoomAt(const FVector& PawnLocal) const;
	/** The bridge's spots come from the modelled set: the pawn sets them once it knows where the set stands. */
	void SetRoomSpots(EShipRoom Which, const FTransform& Lift, const FTransform& Use, const FString& UseLabel);
	/** The parked shuttle in the bay is its own mesh: hidden while the real one is out. */
	void SetBayShuttleShown(bool bShown);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Materials") TSoftObjectPtr<UMaterialInterface> LitMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Materials") TSoftObjectPtr<UMaterialInterface> ButtonsMaterial;
	/** The rooms' screens: the Okudagram material with /Game/Slonk/Bridge/T_Okuda_<slot> for every Screen_<slot>. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Materials") TSoftObjectPtr<UMaterialInterface> OkudagramMaterial;
	/** Bound to the meshes' GlassPanel slot (partitions, cabinet fronts, the bay gallery). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Materials") TSoftObjectPtr<UMaterialInterface> GlassMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Materials") float ScreenIntensity = 2.2f;
	/** The modelled rooms (soft references, so the cooker carries them); a missing mesh falls back to the primitive room. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> EngineeringMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> SickbayMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> ShuttleBayMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> BayShuttleMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> TransporterMesh;

private:
	TArray<FShipRoom> Rooms;
	EShipRoom ShownRoom = EShipRoom::Count;
	int32 PieceCount = 0;
	FShipRoom* Building = nullptr;

	UMaterialInstanceDynamic* MakeLit(const TCHAR* Name, FLinearColor Base, float Roughness, float Metallic = 0.f, FLinearColor Emissive = FLinearColor::Black, float EmissiveStrength = 0.f);
	UMaterialInstanceDynamic* MakeScreen(const TCHAR* Name, float Seed, float Intensity);
	/** An Okudagram screen instance showing /Game/Slonk/Bridge/T_Okuda_<Name>; null when the texture or material is missing. */
	UMaterialInstanceDynamic* MakeOkuda(const TCHAR* TextureName, float Intensity);
	/**
	 * The modelled room from the Blender build, its origin on the floor at the room's
	 * centre, solid to the Boots channel through its own triangles while shown; slots bound by name. Null when
	 * the asset is missing, and the caller builds the primitive room instead.
	 */
	UStaticMeshComponent* RoomMesh(const TSoftObjectPtr<UStaticMesh>& Asset, const FVector& FloorCenter);
	void BindSlots(UStaticMeshComponent* C);
	/** The parked shuttle built from primitives at S (the bay's placeholder when its mesh is missing). */
	void PrimitiveShuttle(const FVector& S);
	TWeakObjectPtr<UStaticMeshComponent> BayShuttle;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> BayShuttlePieces;
	bool bBayShuttleShown = true;
	UStaticMeshComponent* Piece(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FRotator& Rot, UMaterialInterface* Mat, bool bSolid = true);
	UStaticMeshComponent* Box(const FVector& Center, const FVector& Size, UMaterialInterface* Mat, float YawDeg = 0.f, bool bSolid = true) { return Piece(CubeMesh, Center, Size, FRotator(0.f, YawDeg, 0.f), Mat, bSolid); }
	UStaticMeshComponent* Cyl(const FVector& Center, float Radius, float Height, UMaterialInterface* Mat, bool bSolid = true) { return Piece(CylinderMesh, Center, FVector(Radius * 2.f, Radius * 2.f, Height), FRotator::ZeroRotator, Mat, bSolid); }
	void Light(const FVector& Pos, float Intensity, float Radius, FLinearColor Color);
	/** Floor, ceiling and the four walls of a room's box, with a burgundy lift door set in one wall at a pawn-space point. */
	void Shell(const FShipRoom& R, const FVector& LiftDoor, const FVector& LiftNormal, bool bOpenAft);
	/** A console: dark desk with a raked teal screen, facing +X rotated by YawDeg. */
	void Console(const FVector& FloorPos, float YawDeg, float Width, float Seed);
	void BuildEngineering(FShipRoom& R);
	void BuildSickbay(FShipRoom& R);
	void BuildShuttleBay(FShipRoom& R);
	void BuildTransporter(FShipRoom& R);

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> AllPieces;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	// Palette, filled by Build().
	UMaterialInterface* PanelGrey = nullptr;
	UMaterialInterface* PanelDark = nullptr;
	UMaterialInterface* Carpet = nullptr;
	UMaterialInterface* DeckPlate = nullptr;
	UMaterialInterface* Burgundy = nullptr;
	UMaterialInterface* StripWhite = nullptr;
	UMaterialInterface* ConsoleBlack = nullptr;
	UMaterialInterface* Teal = nullptr;
	UMaterialInterface* CoreBlue = nullptr;
	UMaterialInterface* BedWhite = nullptr;
	UMaterialInterface* HullGrey = nullptr;
	UMaterialInterface* PadGlow = nullptr;
};
