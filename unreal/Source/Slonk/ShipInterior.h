// Slonk - the ship's interior as places you can walk: the signature rooms (main engineering, sickbay, the shuttle
// bay, the transporter room, the airlock's staging area, deck G's corridor, the rec deck, the officers' mess, the
// captain's quarters, the crew quarters, the torpedo bay, the cargo bay), modelled sets (or engine primitives
// while a set is missing) at the places the compartment map puts them; and the ship's plan (ShipPlan.h), decks A
// to U at the ship's real size, their corridors and rooms drawn from instanced boxes zone by zone as the walker
// comes near. The bridge is the modelled set on UBridgeInteriorComponent; the turbolift's car carries you between
// the stops (docs/interiors-design.md).
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ShipPlan.h"
#include "ShipInterior.generated.h"

class AStarshipPawn;
class APersonPawn;
class UStaticMesh;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UPrimitiveComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** A place: a signature room, a stretch of the plan's corridor, a generated room or the turbolift's car. Its box
 *  in pawn space (oriented by Yaw about Up), where the lift lets you out, and the one thing in it to use. */
struct FShipRoom
{
	int32 Index = INDEX_NONE;
	/** The kind: a signature room's own value, or one of the plan's kinds. */
	EShipRoom Id = EShipRoom::Count;
	FString Name;
	FString Deck;
	/** Centre and half-size of the walkable box, pawn space (cm), turned by Yaw (radians) about Up. */
	FVector Center = FVector::ZeroVector;
	FVector Half = FVector::ZeroVector;
	double Yaw = 0.0;
	/** Further boxes of the place (a corridor's second leg; a stretch's bays), pawn space, axis-aligned. */
	TArray<FBox> Legs;
	/** The places whose boxes touch this one: shown and solid along with it, so a doorway shows the room beyond. */
	TArray<int32> Neighbours;
	/** Feet on the floor by the lift door, facing into the room (pawn space). */
	FTransform Lift;
	/** Feet at the room's thing to use (the chair, the pad, the shuttle's door, the master console), facing it. */
	FTransform Use;
	/** What F does there, for the HUD: "sit down", "energise", "board the shuttle", "open the board". */
	FString UseLabel;
	/** The plan's stretch this place is or belongs to (its zone), and the plan's room; -1 for the signature rooms. */
	int32 Stretch = INDEX_NONE;
	int32 PlanRoom = INDEX_NONE;
	/** The turbolift stop in this place, or -1. */
	int32 Stop = INDEX_NONE;
	/** Everything drawn for a signature room, and the subset that stands in the way of the Boots channel. */
	TArray<TWeakObjectPtr<USceneComponent>> Pieces;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Solids;
	bool IsSignature() const { return (int32)Id < SlonkSignatureRooms; }
};

/**
 * Attach to the ship's root. Build() reads the ship's length and bay from the owner, lays the signature rooms out
 * in pawn space and builds the plan round them; ShowPlace() turns the place's pieces visible and solid to the Boots
 * channel along with everything within sight of it (and the rest off), so hull walkers and the airlock trace never
 * meet a wall that is not around them.
 */
UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UShipInteriorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UShipInteriorComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void Build();
	bool IsBuilt() const { return Rooms.Num() > 0; }
	/** A signature room by kind (the first place of that kind). */
	const FShipRoom* Room(EShipRoom Which) const;
	int32 IndexOf(EShipRoom Which) const;
	const FShipRoom* Place(int32 Index) const { return Rooms.IsValidIndex(Index) ? &Rooms[Index] : nullptr; }
	int32 NumPlaces() const { return Rooms.Num(); }
	const FShipPlan& GetPlan() const { return Plan; }
	/** Show the places around one (or none with INDEX_NONE): visibility and Boots collision together. `Feet` is
	 *  where the walker stands, for the stretch of corridor to keep live. */
	void ShowPlace(int32 Index, const FVector* Feet = nullptr);
	/** Refresh the live set for a walker who has moved within the shown place. */
	void UpdateLive(const FVector& Feet);
	int32 Shown() const { return ShownPlace; }
	EShipRoom ShownKind() const { return Rooms.IsValidIndex(ShownPlace) ? Rooms[ShownPlace].Id : EShipRoom::Count; }
	/** A signature room's name, or the generic name of a generated kind. */
	static FString RoomName(EShipRoom Which);
	/** A signature room's deck letter; empty for the generated kinds (each place has its own). */
	static FString DeckOf(EShipRoom Which);
	FString NameOf(int32 Index) const { return Rooms.IsValidIndex(Index) ? Rooms[Index].Name : FString(); }
	FString DeckOfPlace(int32 Index) const { return Rooms.IsValidIndex(Index) ? Rooms[Index].Deck : FString(); }
	EShipRoom KindOf(int32 Index) const { return Rooms.IsValidIndex(Index) ? Rooms[Index].Id : EShipRoom::Count; }
	/** The room the lift stops at for a signature room: itself, or the corridor it opens off (sickbay, the
	 *  transporter room and the rec deck are reached along deck G's corridor). */
	static EShipRoom LiftStop(EShipRoom Which);
	/** Whether the lift has a door in a signature room (its set's). */
	static bool IsLiftStop(EShipRoom Which) { return Which != EShipRoom::Count && (int32)Which < SlonkSignatureRooms && LiftStop(Which) == Which; }
	/** Whether a place has a turbolift stop (a door of its own). */
	bool HasStop(int32 Index) const { return Rooms.IsValidIndex(Index) && Rooms[Index].Stop != INDEX_NONE; }
	/** The place a pawn-space point is in, or INDEX_NONE. `Current` is kept while the point is still within its box
	 *  (a doorway lies in two places' tolerance), so a person's place changes only as they leave one. */
	int32 RoomAt(const FVector& PawnLocal, int32 Current = INDEX_NONE) const;
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

	// ---- the turbolift -------------------------------------------------------------------------------------
	/** The car's place (a room like any other while it stands at a stop). */
	int32 LiftPlace() const { return LiftPlaceIndex; }
	/** Whether feet in the craft's frame stand in the car's doorway: past the sensor line, half a metre from the
	 *  centre toward the doors (the leaves meet 98 cm out). The doors will not shut on them, and the deck buttons
	 *  wait until they are clear of it. */
	bool InCarDoorway(const FVector& FeetInCraft) const;
	/** The stop a walker in a place would take: the place's own, else the nearest on its deck; INDEX_NONE if none. */
	int32 StopFor(int32 Place, const FVector& Feet) const;
	/** The stop whose door is within reach of a point in a place, or INDEX_NONE. */
	int32 StopAtDoor(int32 Place, const FVector& Feet, double Reach = 220.0) const;
	/** The door of a stop on the floor (where to stand), facing from the car into the passage. */
	FTransform StopDoor(int32 Stop) const;
	/** Bring the car to a stop with its doors open, for a walker to step in; false if the stop has a set's own door. */
	bool OpenLift(int32 Stop);
	/** Put a rider straight into the car at a stop (a fade's worth of teleport) and send it to another. */
	void BoardAndRide(APersonPawn* Rider, int32 FromStop, int32 ToStop);
	/** Send the car, with whoever stands in it, from where it is to a stop. */
	bool Ride(APersonPawn* Rider, int32 ToStop);
	bool IsRiding() const { return RidePhase != ERide::Idle && RidePhase != ERide::Open; }
	int32 LiftAtStop() const { return CarStop; }
	/** Called when a ride ends: the stop arrived at and the rider. */
	TFunction<void(int32 Stop, APersonPawn* Rider)> OnArrived;
	/** A line for the rider's screen while the car moves (the decks passed). */
	TFunction<void(const FString&)> OnLiftMessage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Materials") TSoftObjectPtr<UMaterialInterface> LitMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Materials") TSoftObjectPtr<UMaterialInterface> ButtonsMaterial;
	/** The rooms' screens: the Okudagram material with /Game/Slonk/Bridge/T_Okuda_<slot> for every Screen_<slot>. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Materials") TSoftObjectPtr<UMaterialInterface> OkudagramMaterial;
	/** Bound to the meshes' GlassPanel slot (partitions, cabinet fronts, the bay gallery). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Materials") TSoftObjectPtr<UMaterialInterface> GlassMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Materials") float ScreenIntensity = 2.2f;
	/** How far the plan is drawn round the walker, and how far it is solid (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Plan") float LiveRadiusCm = 6000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Plan") float SolidRadiusCm = 1400.f;
	/** Point lights lent to the nearest bays of the plan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior|Plan") int32 LampPoolSize = 24;
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
	FShipPlan Plan;
	int32 ShownPlace = INDEX_NONE;
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
	UStaticMeshComponent* Piece(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FRotator& Rot, UMaterialInterface* Mat, bool bSolid = true, USceneComponent* Parent = nullptr);
	UStaticMeshComponent* Box(const FVector& Center, const FVector& Size, UMaterialInterface* Mat, float YawDeg = 0.f, bool bSolid = true) { return Piece(CubeMesh, Center, Size, FRotator(0.f, YawDeg, 0.f), Mat, bSolid); }
	UStaticMeshComponent* Cyl(const FVector& Center, float Radius, float Height, UMaterialInterface* Mat, bool bSolid = true) { return Piece(CylinderMesh, Center, FVector(Radius * 2.f, Radius * 2.f, Height), FRotator::ZeroRotator, Mat, bSolid); }
	UPointLightComponent* Light(const FVector& Pos, float Intensity, float Radius, FLinearColor Color, USceneComponent* Parent = nullptr);
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
	/** Whether a pawn-space point is within a place's boxes, with a tolerance in cm. */
	/** Whether a point (pawn space) is in a place, a tolerance out from its walls: a passage by its bays as they
	 *  lie (an axis box round a bay on the diagonal would bulge a metre past its wall), a room by its oriented box. */
	bool InRoom(const FShipRoom& R, const FVector& P, double Tol) const;

	// ---- the plan's zones ------------------------------------------------------------------------------------
	/** One stretch of the plan and its rooms, drawn from instanced boxes (one component per material) the first
	 *  time it comes into view; its solids are a second, unseen set of instances that block the Boots channel. */
	/** A run of up to ZoneBays bays of one stretch with the rooms off them: what is built, shown and made solid
	 *  together. A ring is many zones, so only the arc near the walker is live. */
	struct FZone
	{
		TMap<UMaterialInterface*, UInstancedStaticMeshComponent*> Visual, VisualCyl;
		UInstancedStaticMeshComponent* Solid = nullptr;
		TArray<FVector> Lamps;
		FBox Bounds = FBox(ForceInit);
		int32 Deck = INDEX_NONE;
		int32 Stretch = INDEX_NONE, Bay0 = 0, Bay1 = 0;   // bays [Bay0, Bay1) of the stretch
		bool bBuilt = false, bShown = false, bSolidOn = false;
	};
	static constexpr int32 ZoneBays = 12;
	TArray<FZone> Zones;
	/** The first zone of each stretch; its bays' zones follow in order. */
	TArray<int32> StretchZone0;
	int32 ZoneOf(int32 Stretch, int32 Bay) const;
	TArray<TObjectPtr<UPointLightComponent>> LampPool;
	FVector LastLiveFeet = FVector(1e9, 1e9, 1e9);
	void BuildPlan(double Scale);
	void BuildZone(int32 Zone);
	/** An instanced box (or cylinder) in a zone: centre and size in pawn space, a rotation; solid ones also block the boots. */
	void Inst(FZone& Z, const FVector& Center, const FVector& Size, const FQuat& Rot, UMaterialInterface* Mat, bool bSolid, bool bCylinder = false);
	void DrawBay(FZone& Z, const FPlanStretch& S, int32 BayIndex, double Floor);
	void DrawEndWall(FZone& Z, const FPlanBay& Bay, bool bAtStart, double Floor, int32 Stop);
	/** A canted panel across a run's section, from (across lo, z lo) to (across hi, z hi) on one side. */
	void Cant(FZone& Z, const struct FRunFrameRef& Fr, double Along, double Length, double Sign, double ULo, double ZLo, double UHi, double ZHi, UMaterialInterface* Mat, double Thick, bool bSolid);
	void MakeStopDoor(int32 Stop, int32 Zone, const FVector& Center, const FVector& Along, const FQuat& Rot);
	void DrawRoom(FZone& Z, const FPlanRoom& R, double Floor);
	void Furnish(FZone& Z, const FPlanRoom& R, double Floor);
	void SetZoneShown(int32 Stretch, bool bShown, bool bSolid);
	void PlaceLamps(const FVector& Feet);

	// ---- the turbolift's car and stop doors ------------------------------------------------------------------
	enum class ERide : uint8 { Idle, Open, Closing, Travelling, Opening };
	ERide RidePhase = ERide::Idle;
	int32 LiftPlaceIndex = INDEX_NONE;
	int32 CarStop = INDEX_NONE, RideTo = INDEX_NONE;
	double RideT = 0.0, RideDuration = 0.0;
	int32 PassedDeck = INDEX_NONE;
	double IdleT = 0.0;
	bool bCarShown = false;
	TWeakObjectPtr<APersonPawn> Rider;
	UPROPERTY() TObjectPtr<USceneComponent> CarRoot;
	TArray<TWeakObjectPtr<USceneComponent>> CarPieces;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> CarSolids;
	/** The car's own doors: two leaves across its gap, inside the posts, that shut for the ride (and stand shut at a
	 *  set's stop, where no landing doors cover the gap) and slide apart behind the wall panels at a landing. */
	static constexpr double CarR = 127.0;
	static constexpr double CarDoorX = 98.0;
	static constexpr double CarLeafW = 66.0;
	UStaticMeshComponent* CarLeaf[2] = { nullptr, nullptr };
	float CarDoorOpen = 0.f;
	void SetCarDoor(float Open);
	/** The landing's doors and the car's together. */
	void SetDoors(float Open) { SetStopDoor(CarStop, Open); SetCarDoor(Open); }
	/** The stop doors of the plan's stops: two leaves each, slid open while the car stands there. */
	struct FStopDoor { UStaticMeshComponent* Leaf[2] = { nullptr, nullptr }; FVector Shut[2]; FVector Along; float Open = 0.f; int32 Zone = INDEX_NONE; };
	TMap<int32, FStopDoor> StopDoors;
	void BuildCar();
	void MoveCarTo(int32 Stop);
	void SetCarShown(bool bShown);
	void SetStopDoor(int32 Stop, float Open);
	void TickRide(float DeltaTime);

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> AllPieces;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	// Palette, filled by Build().
	UMaterialInterface* PanelGrey = nullptr;
	UMaterialInterface* PanelLight = nullptr;
	UMaterialInterface* PanelDark = nullptr;
	UMaterialInterface* Carpet = nullptr;
	UMaterialInterface* Grating = nullptr;
	UMaterialInterface* DeckPlate = nullptr;
	UMaterialInterface* Burgundy = nullptr;
	UMaterialInterface* StripWhite = nullptr;
	UMaterialInterface* SoftWhite = nullptr;
	UMaterialInterface* ConsoleBlack = nullptr;
	UMaterialInterface* Teal = nullptr;
	UMaterialInterface* CoreBlue = nullptr;
	UMaterialInterface* BedWhite = nullptr;
	UMaterialInterface* HullGrey = nullptr;
	UMaterialInterface* PadGlow = nullptr;
	UMaterialInterface* Metal = nullptr;
	UMaterialInterface* StripBlue = nullptr;
	UMaterialInterface* Wood = nullptr;
	UMaterialInterface* Crate = nullptr;
	UMaterialInterface* Touch = nullptr;
};
