// Slonk - the ship as a crewed machine: subsystems with integrity and power, an alert state, the senior staff who
// carry out the captain's orders and report back, and the bridge log of what they said.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShipSystemsComponent.generated.h"

UENUM(BlueprintType)
enum class EShipSystem : uint8
{
	Shields = 0,
	Phasers,
	Torpedoes,
	Warp,
	Impulse,
	Sensors,
	LifeSupport,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EAlertState : uint8 { Green, Yellow, Red };

UENUM(BlueprintType)
enum class EStation : uint8
{
	Engineering = 0,
	Tactical,
	Science,
	Helm,
	Operations,
	Medical,
	Security,
	Count UMETA(Hidden)
};

USTRUCT(BlueprintType)
struct FShipSystemState
{
	GENERATED_BODY()
	/** 0..1: how much of the system still works. */
	UPROPERTY(BlueprintReadOnly) float Integrity = 1.f;
	/** Power share, 1 = its normal draw. Diverting raises one and lowers the rest. */
	UPROPERTY(BlueprintReadOnly) float Power = 1.f;
	/** Set while the damage-control team is on it. */
	UPROPERTY(BlueprintReadOnly) bool bRepairing = false;
};

USTRUCT(BlueprintType)
struct FOfficer
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Rank;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EStation Station = EStation::Engineering;
	/** The junior who takes the post when the officer is in sickbay or lost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Acting;
	UPROPERTY(BlueprintReadOnly) bool bInjured = false;
	UPROPERTY(BlueprintReadOnly) bool bLost = false;
	float RecoverSeconds = 0.f;
	/** Who answers for the station right now. */
	FString Speaker() const;
	bool OnDuty() const { return !bInjured && !bLost; }
};

USTRUCT(BlueprintType)
struct FShipCrew
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Complement = 502;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SickbayBeds = 24;
	/** Fractional so casualties can accrue slowly; shown rounded. */
	UPROPERTY(BlueprintReadOnly) float Fit = 502.f;
	UPROPERTY(BlueprintReadOnly) float Injured = 0.f;
	UPROPERTY(BlueprintReadOnly) float Critical = 0.f;
	UPROPERTY(BlueprintReadOnly) float Dead = 0.f;
	int32 Casualties() const { return FMath::RoundToInt(Injured + Critical + Dead); }
};

USTRUCT(BlueprintType)
struct FShipStores
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 TorpedoCapacity = 96;
	/** 0..1 of the lockers: repairs use parts, treatment uses medical supplies. */
	UPROPERTY(BlueprintReadOnly) float SpareParts = 1.f;
	UPROPERTY(BlueprintReadOnly) float MedicalSupplies = 1.f;
};

UENUM(BlueprintType)
enum class ETriage : uint8 { CriticalFirst, WalkingWounded };

USTRUCT(BlueprintType)
struct FBridgeLogEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) float Time = 0.f;
	UPROPERTY(BlueprintReadOnly) EStation Station = EStation::Engineering;
	UPROPERTY(BlueprintReadOnly) FString Speaker;
	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
};

class AStarshipPawn;

/** A place inside the ship, in pawn space, with what lives there. See ShipCompartments.h for the map. */
struct FCompartment
{
	FString Name;
	FString Deck;
	/** The system that lives here, or Count for quarters, bays and the like. */
	EShipSystem System = EShipSystem::Count;
	/** The senior officer whose post is here, or Count. The bridge holds several; see bBridge. */
	EStation Station = EStation::Count;
	bool bBridge = false;
	/** Pawn-space box, cm. */
	FVector Center = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	/** Share of the complement normally in here, for casualties. */
	float CrewShare = 0.f;
	/** Visible damage 0..1 for the display; eases off with repairs. */
	float Damage = 0.f;
	float LastHitTime = -1000.f;

	bool Contains(const FVector& P) const { return FMath::Abs(P.X - Center.X) <= Extent.X && FMath::Abs(P.Y - Center.Y) <= Extent.Y && FMath::Abs(P.Z - Center.Z) <= Extent.Z; }
	FBox Box() const { return FBox(Center - Extent, Center + Extent); }
};

UCLASS(ClassGroup = (Slonk), meta = (BlueprintSpawnableComponent))
class SLONK_API UShipSystemsComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShipSystemsComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---- the captain's orders ----
	UFUNCTION(BlueprintCallable, Category = "Ship|Orders") void SetAlert(EAlertState NewAlert);
	/** "Divert power to shields": that system to 150 %, the others share what is left of the budget. */
	UFUNCTION(BlueprintCallable, Category = "Ship|Orders") void DivertPower(EShipSystem To);
	UFUNCTION(BlueprintCallable, Category = "Ship|Orders") void BalancePower();
	/** Put the damage-control team on one system; "auto" (Count) lets the chief engineer pick the worst. */
	UFUNCTION(BlueprintCallable, Category = "Ship|Orders") void SetRepairPriority(EShipSystem System);
	/** Sickbay works the critical first (default) or returns the walking wounded to duty first. */
	UFUNCTION(BlueprintCallable, Category = "Ship|Orders") void SetTriage(ETriage NewTriage);
	/** Take on torpedoes, parts, supplies and replacements from a station within reach; a minute alongside. */
	UFUNCTION(BlueprintCallable, Category = "Ship|Orders") bool BeginResupply();
	/** Is a station within ResupplyRangeKm and the ship slow enough? OutWhy says what is missing. */
	bool CanResupply(FString& OutWhy, AActor** OutStation = nullptr) const;
	/** The range and speed conditions against one station in particular. */
	bool CanResupplyAt(const AActor* Station, FString& OutWhy) const;
	/** An order typed at the console: "alert red", "power shields", "repair warp", "balance", "triage", "resupply". */
	bool HandleOrder(const TArray<FString>& Args, FString& OutReply);
	/** Shields: reinforce one facing (0 fore, 1 aft, 2 port, 3 starboard) at the others' expense, or -1 for even. */
	UFUNCTION(BlueprintCallable, Category = "Ship|Orders") void ReinforceShields(int32 Facing);
	int32 GetReinforcedFacing() const { return ReinforcedFacing; }
	static FString FacingName(int32 Facing);
	/** Dev: set a system's integrity outright. */
	void DevSetIntegrity(EShipSystem System, float Integrity) { Systems[(int32)System].Integrity = FMath::Clamp(Integrity, 0.f, 1.f); }

	// ---- what happens to the ship ----
	/** Called by the pawn when a hit lands: some of what got through the shields breaks something. */
	void OnHit(float Applied, float ThroughShields);
	/** A located hit: LocalPoint and LocalDir in pawn space pick the compartment, and what is in it suffers. */
	void OnHitAt(float Applied, float ThroughShields, const FVector& LocalPoint, const FVector& LocalDir);
	const TArray<FCompartment>& GetCompartments() const { return Compartments; }
	TArray<FCompartment>& MutableCompartments() { return Compartments; }
	/** Rebuild the compartment map from the pawn's hull anchors (called at BeginPlay; again if the hull changes). */
	void BuildCompartments();
	/** The scale a system's output should have now: integrity and power together, with the alert's posture. */
	UFUNCTION(BlueprintPure, Category = "Ship") float Effectiveness(EShipSystem System) const;
	UFUNCTION(BlueprintPure, Category = "Ship") EAlertState GetAlert() const { return Alert; }
	UFUNCTION(BlueprintPure, Category = "Ship") const FShipSystemState& GetSystem(EShipSystem System) const { return Systems[(int32)System]; }
	UFUNCTION(BlueprintPure, Category = "Ship") EShipSystem GetRepairPriority() const { return RepairPriority; }
	const TArray<FOfficer>& GetOfficers() const { return Officers; }
	const FShipCrew& GetCrew() const { return Crew; }
	const FShipStores& GetStores() const { return Stores; }
	/** Breathable air across the decks, 0..1; falls when life support cannot keep up. */
	float GetAtmosphere() const { return Atmosphere; }
	/** Where the air is heading given life support's effectiveness right now. */
	float AtmosphereTarget() const;
	ETriage GetTriage() const { return Triage; }
	bool IsResupplying() const { return ResupplyLeft > 0.f; }
	float ResupplySecondsLeft() const { return ResupplyLeft; }
	/** Fit crew over complement, floored: fewer hands means slower repairs and sloppier systems. */
	float CrewFactor() const;
	/** Wounded treated per second right now, after supplies, staff and air. */
	float TreatRatePerSecond() const;
	int32 SickbayInUse() const { return FMath::Min(FMath::RoundToInt(Crew.Critical + Crew.Injured * 0.25f), Crew.SickbayBeds); }
	const TArray<FBridgeLogEntry>& GetLog() const { return Log; }
	/** Counts every report ever made; the log itself is capped, so watch this to know it changed. */
	int32 GetLogRevision() const { return LogRevision; }
	/** The officer at a station, or null if the post is empty. */
	const FOfficer* OfficerAt(EStation Station) const;
	/** An officer speaks: on the screen, and into the bridge log. */
	void Report(EStation From, const FString& Text, const FLinearColor& Color = FLinearColor(0.75f, 0.9f, 1.f));
	static FString SystemName(EShipSystem System);
	static FString StationName(EStation Station);
	static FString AlertName(EAlertState Alert);
	static bool ParseSystem(const FString& Word, EShipSystem& Out);

	/** The senior staff. Edit here to rename the crew. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Crew")
	TArray<FOfficer> Officers;
	/** Repair rate per second for the team's priority system; others heal at a third of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship") float RepairPerSecond = 0.008f;
	/** How much integrity one point of hull damage takes from a system it hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship") float DamagePerHullPoint = 0.004f;
	/** Wounded per point of damage through the shields; a third of them critical. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Crew") float WoundedPerHullPoint = 0.12f;
	/** Treatment rate at full staff and supplies, people per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Crew") float BaseTreatRate = 0.15f;
	/** Off for small craft: no casualties, no sickbay, no air model; repairs and orders still work. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Crew") bool bCrewModel = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Crew") FShipCrew Crew;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Stores") FShipStores Stores;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Stores") float ResupplyRangeKm = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Stores") float ResupplySeconds = 45.f;

private:
	UPROPERTY() TArray<FShipSystemState> Systems;
	EAlertState Alert = EAlertState::Green;
	EShipSystem RepairPriority = EShipSystem::Count;		// Count = the chief picks
	TArray<FBridgeLogEntry> Log;
	int32 LogRevision = 0;
	float ReportCooldown[(int32)EShipSystem::Count] = { 0 };
	UPROPERTY(VisibleAnywhere, Category = "Ship|Crew") float Atmosphere = 1.f;
	ETriage Triage = ETriage::CriticalFirst;
	float ResupplyLeft = 0.f;
	TWeakObjectPtr<AActor> ResupplyStation;
	float MedicalReportCooldown = 0.f, LifeSupportReportCooldown = 0.f, PendingWounded = 0.f, PendingDead = 0.f;
	bool bPartsOutReported = false, bSuppliesOutReported = false, bAirWarned = false;
	TArray<FCompartment> Compartments;
	int32 ReinforcedFacing = -1;
	/** Break a system by Loss, with the chief's report; Where names the compartment when known. */
	void DamageSystem(int32 Index, float Loss, const FString& Where);
	int32 PickRandomSystem() const;
	void InjureOfficer(float ThroughShields, const FCompartment* Where);
	/** Wound and kill in proportion to what got through; Scale weights it by how many people are where it landed. */
	void ApplyCasualties(float ThroughShields, float Scale = 1.f);
	void TickCrew(float DeltaTime);
	void TickLifeSupport(float DeltaTime);
	void TickResupply(float DeltaTime);
	void FinishResupply();
	// the pawn's own numbers before any system scales them
	float BaseMaxShield = 100.f, BaseShieldRecharge = 5.f, BasePhaserDps = 30.f, BasePhaserRecharge = 8.f, BaseTorpedoCooldown = 2.5f, BaseMaxWarp = 9.99f;
	double BaseImpulse = 7.4948e9;
	bool bBaseCached = false;
	void CacheBase();
	void ApplyEffects();
	void TickRepairs(float DeltaTime);
	AStarshipPawn* Ship() const;
};
