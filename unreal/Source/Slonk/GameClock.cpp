#include "GameClock.h"

#include "Engine/World.h"

bool UGameClockSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UGameClockSubsystem* UGameClockSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	return W ? W->GetSubsystem<UGameClockSubsystem>() : nullptr;
}

float UGameClockSubsystem::Sim(const UObject* WorldContext, float RealSeconds)
{
	UGameClockSubsystem* C = Get(WorldContext);
	return C ? RealSeconds * C->GetScale() : RealSeconds;
}

float UGameClockSubsystem::ScaleOf(const UObject* WorldContext)
{
	UGameClockSubsystem* C = Get(WorldContext);
	return C ? C->GetScale() : 1.f;
}

const TArray<float>& UGameClockSubsystem::Presets()
{
	static const TArray<float> P = { 1.f, 10.f, 60.f, 600.f, 3600.f };
	return P;
}

float UGameClockSubsystem::Step(float From, int32 Direction)
{
	const TArray<float>& P = Presets();
	if (Direction > 0) { for (float S : P) { if (S > From * 1.001f) { return S; } } return P.Last(); }
	for (int32 i = P.Num() - 1; i >= 0; --i) { if (P[i] < From * 0.999f) { return P[i]; } }
	return P[0];
}

float UGameClockSubsystem::Ramp(float Current, float Target, float RealSeconds)
{
	Current = FMath::Max(Current, 1.f); Target = FMath::Max(Target, 1.f);
	if (FMath::IsNearlyEqual(Current, Target, 1e-3f)) { return Target; }
	// Log space, so x1 to x10 takes as long as x60 to x600; down is quick, a hold is there for a reason.
	const float Rate = Target < Current ? 5.f : 2.f;   // decades per second
	const float L = FMath::Log2(Current) / FMath::Log2(10.f), T = FMath::Log2(Target) / FMath::Log2(10.f);
	const float Next = FMath::FInterpConstantTo(L, T, RealSeconds, Rate);
	return Next == T ? Target : FMath::Pow(10.f, Next);   // land exactly, not at the pow's rounding of the target
}

FString UGameClockSubsystem::Format(float Scale)
{
	return Scale >= 10.f ? FString::Printf(TEXT("x%.0f"), Scale) : FString::Printf(TEXT("x%.1f"), Scale);
}

void UGameClockSubsystem::SetRequested(float Scale)
{
	Requested = FMath::Clamp(Scale, 1.f, MaxScale);
}

void UGameClockSubsystem::StepRequested(int32 Direction)
{
	SetRequested(Step(Requested, Direction));
}

void UGameClockSubsystem::Allow(float InCap, const FString& Why)
{
	Frame();
	// Several voices in a frame: the lowest holds.
	if (CapFrame == GFrameCounter) { if (InCap < Cap) { Cap = InCap; CapReason = Why; } return; }
	Cap = InCap; CapReason = Why; CapFrame = GFrameCounter;
}

void UGameClockSubsystem::Drive(float Scale)
{
	Frame();
	Driven = FMath::Clamp(Scale, 1.f, MaxScale); DriveFrame = GFrameCounter;
}

FString UGameClockSubsystem::HoldReason()
{
	Frame();
	const bool bHeld = CapFrame + 1 >= GFrameCounter;
	const float Want = IsDriven() ? Driven : Requested;
	if (!bHeld) { return Want > 1.001f ? TEXT("on foot") : FString(); }
	return Cap < Want - 0.001f ? CapReason : FString();
}

float UGameClockSubsystem::GetScale()
{
	Frame();
	return Effective;
}

double UGameClockSubsystem::GetSeconds()
{
	Frame();
	return SimSeconds;
}

void UGameClockSubsystem::Frame()
{
	if (!bStarted) { bStarted = true; Requested = FMath::Clamp(DefaultRequested, 1.f, MaxScale); }
	if (LastFrame == GFrameCounter) { return; }
	LastFrame = GFrameCounter;
	const UWorld* W = GetWorld();
	const float Dt = W ? FMath::Min(W->GetDeltaSeconds(), 0.1f) : 0.f;
	// Holds and drives are said during a frame's ticks and govern the next: with nobody speaking, x1.
	const float Hold = CapFrame + 1 >= GFrameCounter ? Cap : 1.f;
	const float Want = IsDriven() ? Driven : Requested;
	Effective = Ramp(Effective, FMath::Clamp(FMath::Min(Want, Hold), 1.f, MaxScale), Dt);
	SimSeconds += Dt * Effective;
}
