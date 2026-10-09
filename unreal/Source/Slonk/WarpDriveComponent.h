// Slonk - warp drive state machine. Owns the speed profile; the pawn renders the effects.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WarpDriveComponent.generated.h"

class UStarshipMovementComponent;

UENUM(BlueprintType)
enum class EWarpState : uint8
{
	Idle,
	Charging,
	Jumping,
	AtWarp,
	Dropping
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWarpStateChanged, EWarpState, OldState, EWarpState, NewState);

/**
 * Warp drive: Idle -> Charging -> Jumping -> AtWarp -> Dropping -> Idle.
 * Speed follows the TNG-style curve Speed(w) = Warp1Speed * w^(10/3), scaled down to game distances.
 * Exposes normalized effect intensities (glow, streaks, flash) that the pawn maps onto lights,
 * materials, post-process and the star-streak tunnel.
 */
UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UWarpDriveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWarpDriveComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---- Tuning ----

	/** Speed at warp 1 in cm/s. 100 km/s by default (game scale; real warp 1 would cross the map instantly). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp")
	double Warp1Speed = 1.0e7;

	/** Exponent of the warp curve. 10/3 matches the TNG scale below warp 9. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp")
	float WarpSpeedExponent = 3.3333f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp", meta = (ClampMin = "1", ClampMax = "9.99"))
	float MinWarpFactor = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp", meta = (ClampMin = "1", ClampMax = "9.99"))
	float MaxWarpFactor = 9.9f;

	/** Seconds the nacelles charge before the jump. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp|Timing")
	float ChargeDuration = 2.0f;

	/** Seconds spent accelerating from impulse to warp speed (the stretch / flash). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp|Timing")
	float JumpDuration = 0.7f;

	/** Seconds spent decelerating back to impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp|Timing")
	float DropDuration = 1.2f;

	/** How fast the cruise speed follows a changed warp factor while at warp (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp|Timing")
	float CruiseSpeedResponse = 1.5f;

	/** Nacelle / hull glow multiplier while at warp (1 = normal running lights). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp|FX")
	float WarpGlowMultiplier = 3.5f;

	// ---- State ----

	UPROPERTY(BlueprintReadOnly, Category = "Warp")
	float WarpFactor = 2.f;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnWarpStateChanged OnWarpStateChanged;

	// ---- API ----

	UFUNCTION(BlueprintCallable, Category = "Starship|Warp")
	void EngageWarp();

	UFUNCTION(BlueprintCallable, Category = "Starship|Warp")
	void DisengageWarp();

	UFUNCTION(BlueprintCallable, Category = "Starship|Warp")
	void ToggleWarp();

	UFUNCTION(BlueprintCallable, Category = "Starship|Warp")
	void SetWarpFactor(float NewFactor);

	UFUNCTION(BlueprintCallable, Category = "Starship|Warp")
	void AdjustWarpFactor(float Delta);

	UFUNCTION(BlueprintPure, Category = "Starship|Warp")
	EWarpState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Starship|Warp")
	bool IsWarpEngaged() const { return State != EWarpState::Idle; }

	UFUNCTION(BlueprintPure, Category = "Starship|Warp")
	double GetWarpSpeedForFactor(float Factor) const;

	/** Speed the drive is currently commanding (cm/s), 0 when idle. */
	UFUNCTION(BlueprintPure, Category = "Starship|Warp")
	double GetCurrentWarpSpeed() const { return CurrentSpeed; }

	/** 1 = normal running lights, up to WarpGlowMultiplier during charge/warp. */
	UFUNCTION(BlueprintPure, Category = "Starship|Warp")
	float GetGlowMultiplier() const { return GlowMultiplier; }

	/** 0..1 intensity of the star-streak tunnel and warp post-processing. */
	UFUNCTION(BlueprintPure, Category = "Starship|Warp")
	float GetStreakIntensity() const { return StreakIntensity; }

	/** 0..1 screen flash at the moment of jump / drop. */
	UFUNCTION(BlueprintPure, Category = "Starship|Warp")
	float GetFlashIntensity() const { return FlashIntensity; }

	/** 0..1 charge progress (useful for HUD / audio). */
	UFUNCTION(BlueprintPure, Category = "Starship|Warp")
	float GetChargeProgress() const;

private:
	void SetState(EWarpState NewState);
	void ResolveMovement();

	UPROPERTY()
	TObjectPtr<UStarshipMovementComponent> Movement;

	EWarpState State = EWarpState::Idle;
	float StateTime = 0.f;

	/** Speed we were doing when the jump started / cruise speed we drop back to. */
	double EntrySpeed = 0.0;
	double CurrentSpeed = 0.0;
	double CruiseSpeed = 0.0;

	float GlowMultiplier = 1.f;
	float StreakIntensity = 0.f;
	float FlashIntensity = 0.f;
};
