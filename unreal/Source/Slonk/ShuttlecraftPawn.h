// Slonk - the shuttlecraft: a small craft launched from the starship's bay for local flight and landings.

#pragma once

#include "CoreMinimal.h"
#include "StarshipPawn.h"
#include "ShuttlecraftPawn.generated.h"

class UDynamicMeshComponent;

/** Where the shuttle is in its trip: sliding out of the bay, flying, or sliding back in. */
UENUM(BlueprintType)
enum class EShuttlePhase : uint8
{
	Launching,
	Flying,
	Docking
};

/**
 * A procedural TMP-era shuttle (boxy fuselage, sloped windscreen, two short nacelles, skids) that flies with the
 * starship's flight model scaled down: a few kilometres a second at most, no warp, and it can set down on a
 * planet's terrain. It is possessed in place of the starship while it is out; C (or "ship shuttle") brings it back
 * into the bay when the bay is in range, and control returns to the ship.
 */
UCLASS()
class SLONK_API AShuttlecraftPawn : public AStarshipPawn
{
	GENERATED_BODY()

public:
	AShuttlecraftPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void ToggleShuttle() override;
	virtual FString GetCraftName() const override { return TEXT("Shuttlecraft Galileo"); }
	virtual bool IsShuttle() const override { return true; }
	virtual void SetCameraMode(EStarshipCameraMode Mode) override;

	/** Start the launch slide out of Ship's bay; the pawn is at the bay door, facing astern. */
	void BeginLaunch(AStarshipPawn* Ship);
	/** Fly back into the bay if it is within DockRange and the shuttle is slow; false with a message otherwise. */
	UFUNCTION(BlueprintCallable, Category = "Shuttle") bool RequestDock();
	UFUNCTION(BlueprintPure, Category = "Shuttle") EShuttlePhase GetPhase() const { return Phase; }
	/** Distance from the mothership's bay door (cm), -1 without a mothership. */
	UFUNCTION(BlueprintPure, Category = "Shuttle") double DistanceToBay() const;

	/** The ship this shuttle came from and docks with. */
	UPROPERTY(BlueprintReadOnly, Category = "Shuttle") TWeakObjectPtr<AStarshipPawn> Mothership;
	/** Docking is accepted within this range of the bay door (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle") double DockRangeCm = 5.0e4;
	/** ...and below this speed relative to the ship (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle") double DockMaxSpeedCm = 5.0e3;
	/** How far astern of the door the launch slide ends and the docking approach aims (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle") float BayRunCm = 5000.f;
	/** Speed through the bay door (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle") float BaySpeedCm = 700.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UDynamicMeshComponent> HullPart;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UDynamicMeshComponent> GlowPart;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UDynamicMeshComponent> WarmPart;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UDynamicMeshComponent> GlassPart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle|Look") FLinearColor HullColor = FLinearColor(0.78f, 0.79f, 0.82f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle|Look") FLinearColor AccentColor = FLinearColor(0.32f, 0.33f, 0.37f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle|Look") FLinearColor GrilleColor = FLinearColor(0.4f, 0.7f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle|Look") FLinearColor BussardColor = FLinearColor(1.f, 0.22f, 0.08f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle|Look") FLinearColor GlassColor = FLinearColor(0.55f, 0.75f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle|Look") float GrilleGlow = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle|Look") float BussardGlow = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle|Look") float GlassGlow = 1.2f;

protected:
	virtual void SetupRunningLights() override;
	virtual void SetupThrusters() override;
	virtual void SetupWeaponBanks() override;
	virtual bool HandleCraftCommand(const FString& Op, const TArray<FString>& Args) override;
	virtual bool CanStepOutside(FString& Reason) const override;
	/** The port-side door: on the ground beside it when landed, floating beside it in space. */
	virtual FTransform GetAirlockTransform() const override;
	virtual bool PersonStartsOnHull() const override { return false; }

private:
	void BuildHull();
	void SetPhase(EShuttlePhase NewPhase);
	void TickLaunch(float DeltaTime);
	void TickDock(float DeltaTime);

	EShuttlePhase Phase = EShuttlePhase::Flying;
	float PhaseTime = 0.f;
	/** Docking: 0 flying to the approach point astern of the door, 1 sliding in. */
	int32 DockStep = 0;

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HullMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AccentMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GrilleMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BussardMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GlassMID;
};
