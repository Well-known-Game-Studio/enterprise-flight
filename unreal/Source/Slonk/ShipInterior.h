// Slonk - the ship's interior as rooms you can walk: main engineering, sickbay, the shuttle bay, the transporter
// room, the airlock's staging area, deck G's corridor, the rec deck, the officers' mess, the captain's quarters,
// the crew quarters, the torpedo bay and the cargo bay, modelled sets (or engine primitives while a set is
// missing) at the places the compartment map puts them, a room shown with its neighbours while someone is in it.
// The bridge is the modelled set on UBridgeInteriorComponent; the turbolift joins the rest (docs/interiors-design.md).
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

/** The rooms. Airlock is the staging area under the saucer hatch; its hatch puts you out on the hull. Corridor is
 *  deck G's, off which sickbay, the transporter room and the rec deck open: the lift stops there for the three. */
UENUM(BlueprintType)
enum class EShipRoom : uint8
{
	Bridge,
	Engineering,
	Sickbay,
	ShuttleBay,
	Transporter,
	Airlock,
	Corridor,
	RecDeck,
	Mess,
	Quarters,
	CrewQuarters,
	TorpedoBay,
	CargoBay,
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
	/** Further boxes of the room (a corridor's second leg), pawn space. */
	TArray<FBox> Legs;
	/** The rooms whose boxes touch this one: shown and solid along with it, so a doorway shows the room beyond. */
	TArray<EShipRoom> Neighbours;
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
	/** The room the lift stops at for a room: itself, or the corridor it opens off (sickbay, the transporter room
	 *  and the rec deck are reached along deck G's corridor). */
	static EShipRoom LiftStop(EShipRoom Which);
	/** Whether the lift has a door in the room (the panel's stops). */
	static bool IsLiftStop(EShipRoom Which) { return Which != EShipRoom::Count && LiftStop(Which) == Which; }
	/** The room a pawn-space point is in, or Count. `Current` is kept while the point is still within its box (a
	 *  doorway lies in two rooms' tolerance), so a person's room changes only as they leave one. */
	EShipRoom RoomAt(const FVector& PawnLocal, EShipRoom Current = EShipRoom::Count) const;
	/** The bridge's spots come from the modelled set: the pawn sets them once it knows where the set stands. */
	void SetRoomSpots(EShipRoom Which, const FTransform& Lift, const FTransform& Use, const FString& UseLabel);
	/** The parked shuttle in the bay is its own mesh: hidden while the real one is out. */
	void SetBayShuttleShown(bool bShown);
	/** The bay is drawn while the clamshell door is anything but shut, whoever is inside: it shows through the opening. */
	void SetBayShownFromOutside(bool bShown);
	/** A room's box in pawn space (centre, half extents); false for a room without one. */
	bool GetRoomBox(EShipRoom Room, FVector& OutCenter, FVector& OutHalf) const;
	/** The transporter pad's glow follows the shimmer while someone beams (0 = idle). */
	void SetPadGlow(float Pulse);

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> AirlockMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> CorridorMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> RecDeckMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> MessMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> QuartersMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> CrewQuartersMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> TorpedoBayMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Meshes") TSoftObjectPtr<UStaticMesh> CargoBayMesh;

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
public:
	/** Give a set mesh's slots their materials by the Blender slot names (screens, strips, glows, glass); the
	 *  shuttle pawn dresses the bay's shuttle model with it. */
	void BindSlots(UStaticMeshComponent* C);
private:
	/** The parked shuttle built from primitives at S (the bay's placeholder when its mesh is missing). */
	void PrimitiveShuttle(const FVector& S);
	TWeakObjectPtr<UStaticMeshComponent> BayShuttle;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> BayShuttlePieces;
	bool bBayShuttleShown = true;
	bool bBayFromOutside = false;
	UStaticMeshComponent* Piece(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FRotator& Rot, UMaterialInterface* Mat, bool bSolid = true);
	UStaticMeshComponent* Box(const FVector& Center, const FVector& Size, UMaterialInterface* Mat, float YawDeg = 0.f, bool bSolid = true) { return Piece(CubeMesh, Center, Size, FRotator(0.f, YawDeg, 0.f), Mat, bSolid); }
	UStaticMeshComponent* Cyl(const FVector& Center, float Radius, float Height, UMaterialInterface* Mat, bool bSolid = true) { return Piece(CylinderMesh, Center, FVector(Radius * 2.f, Radius * 2.f, Height), FRotator::ZeroRotator, Mat, bSolid); }
	void Light(const FVector& Pos, float Intensity, float Radius, FLinearColor Color);
	/** Floor, ceiling and the four walls of a room's box, with a burgundy lift door set in one wall at a pawn-space point. */
	void Shell(const FShipRoom& R, const FVector& LiftDoor, const FVector& LiftNormal, bool bOpenAft, const TArray<FVector>& Doorways = {});
	/** A wall along x (at y = Pos) or along y (at x = Pos) from A0 to A1, Z0..Z1, cut by doorways centred at Cuts along it. */
	void WallRun(bool bAlongX, double Pos, double A0, double A1, double Z0, double Z1, double Thick, UMaterialInterface* Mat, const TArray<double>& Cuts);
	/** A console: dark desk with a raked teal screen, facing +X rotated by YawDeg. */
	void Console(const FVector& FloorPos, float YawDeg, float Width, float Seed);
	void BuildEngineering(FShipRoom& R);
	void BuildSickbay(FShipRoom& R);
	void BuildShuttleBay(FShipRoom& R);
	void BuildTransporter(FShipRoom& R);
	void BuildAirlock(FShipRoom& R);
	void BuildCorridor(FShipRoom& R);
	void BuildRecDeck(FShipRoom& R);
	void BuildMess(FShipRoom& R);
	void BuildQuarters(FShipRoom& R);
	void BuildCrewQuarters(FShipRoom& R);
	void BuildTorpedoBay(FShipRoom& R);
	void BuildCargoBay(FShipRoom& R);
	/** The primitive room's doorway (no leaves) in the wall at a pawn-space point, the frame proud of it. */
	void DoorFrame(const FVector& Door, const FVector& Normal);
	/** Whether a pawn-space point is within a room's boxes, with a tolerance in cm. */
	static bool InRoom(const FShipRoom& R, const FVector& P, double Tol);

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
