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
	UPROPERTY(BlueprintReadOnly) bool bInjured = false;
};

USTRUCT(BlueprintType)
struct FBridgeLogEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) float Time = 0.f;
	UPROPERTY(BlueprintReadOnly) FString Speaker;
	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
};

class AStarshipPawn;

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
	/** An order typed at the console: "alert red", "power shields", "repair warp", "balance". */
	bool HandleOrder(const TArray<FString>& Args, FString& OutReply);

	// ---- what happens to the ship ----
	/** Called by the pawn when a hit lands: some of what got through the shields breaks something. */
	void OnHit(float Applied, float ThroughShields);
	/** The scale a system's output should have now: integrity and power together, with the alert's posture. */
	UFUNCTION(BlueprintPure, Category = "Ship") float Effectiveness(EShipSystem System) const;
	UFUNCTION(BlueprintPure, Category = "Ship") EAlertState GetAlert() const { return Alert; }
	UFUNCTION(BlueprintPure, Category = "Ship") const FShipSystemState& GetSystem(EShipSystem System) const { return Systems[(int32)System]; }
	UFUNCTION(BlueprintPure, Category = "Ship") EShipSystem GetRepairPriority() const { return RepairPriority; }
	const TArray<FOfficer>& GetOfficers() const { return Officers; }
	const TArray<FBridgeLogEntry>& GetLog() const { return Log; }
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship") float RepairPerSecond = 0.012f;
	/** How much integrity one point of hull damage takes from a system it hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship") float DamagePerHullPoint = 0.004f;

private:
	UPROPERTY() TArray<FShipSystemState> Systems;
	EAlertState Alert = EAlertState::Green;
	EShipSystem RepairPriority = EShipSystem::Count;		// Count = the chief picks
	TArray<FBridgeLogEntry> Log;
	float ReportCooldown[(int32)EShipSystem::Count] = { 0 };
	// the pawn's own numbers before any system scales them
	float BaseMaxShield = 100.f, BaseShieldRecharge = 5.f, BasePhaserDps = 30.f, BasePhaserRecharge = 8.f, BaseTorpedoCooldown = 2.5f, BaseMaxWarp = 9.99f, BaseScanRadii = 6.f;
	double BaseImpulse = 7.4948e9;
	bool bBaseCached = false;
	void CacheBase();
	void ApplyEffects();
	void TickRepairs(float DeltaTime);
	AStarshipPawn* Ship() const;
};
