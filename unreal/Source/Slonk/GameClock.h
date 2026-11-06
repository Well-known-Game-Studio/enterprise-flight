// Slonk - the one clock everything in the world runs on, scaled by the player (docs/time-design.md).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameClock.generated.h"

/** Simulation time. At x1 a ship's second is a real second; the player asks for a scale (8 and 9, `ship clock`),
 *  the possessed pawn says each frame what the situation allows (a fight, a body close, a warp transition hold x1),
 *  and the autopilot can drive the scale for a leg. Everything that models the world asks this for its seconds;
 *  everything presentational keeps the frame's own. */
UCLASS()
class SLONK_API UGameClockSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	static UGameClockSubsystem* Get(const UObject* WorldContext);
	/** This frame's simulation seconds for RealSeconds of frame time (RealSeconds back when there is no clock). */
	static float Sim(const UObject* WorldContext, float RealSeconds);
	/** The scale in effect this frame (ramped toward the smaller of the request or drive and the hold). */
	static float ScaleOf(const UObject* WorldContext);

	/** The scale the player asked for, 1..MaxScale. */
	float GetRequested() const { return Requested; }
	void SetRequested(float Scale);
	/** One preset up or down from the request. */
	void StepRequested(int32 Direction);
	/** The scale in effect this frame. */
	float GetScale();
	/** Simulation seconds since the world began. */
	double GetSeconds();
	/** What the situation allows this frame and why (the possessed pawn, every frame; nothing said means x1). */
	void Allow(float Cap, const FString& Why);
	/** The scale the autopilot wants this frame for its leg, in place of the request (expires with the frame). */
	void Drive(float Scale);
	/** The hold in effect, or empty when the request is running unhindered. */
	FString HoldReason();
	bool IsDriven() const { return DriveFrame + 1 >= GFrameCounter; }

	static const TArray<float>& Presets();
	/** The next step through the presets from a scale (pure, for the tests). */
	static float Step(float From, int32 Direction);
	/** One frame of the ramp: through log space, a decade in about half a second up, a fifth of one down (pure). */
	static float Ramp(float Current, float Target, float RealSeconds);
	static FString Format(float Scale);

	UPROPERTY(EditAnywhere, Category = "Clock") float MaxScale = 3600.f;
	UPROPERTY(EditAnywhere, Category = "Clock") float DefaultRequested = 60.f;

private:
	void Frame();
	float Requested = 60.f;
	float Effective = 1.f;
	float Cap = 1.f;
	FString CapReason;
	uint64 CapFrame = 0;
	float Driven = 1.f;
	uint64 DriveFrame = 0;
	uint64 LastFrame = 0;
	double SimSeconds = 0.0;
	bool bStarted = false;
};
