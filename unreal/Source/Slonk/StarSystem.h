#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StarSystem.generated.h"

class AStarBody;
class APlanetBody;

/** Spectral class of a system's star: sets its size, colour and brightness. */
UENUM(BlueprintType)
enum class EStarClass : uint8
{
	M UMETA(DisplayName = "M (red dwarf)"),
	K UMETA(DisplayName = "K (orange)"),
	G UMETA(DisplayName = "G (yellow)"),
	F UMETA(DisplayName = "F (yellow-white)"),
	A UMETA(DisplayName = "A (white)"),
	WhiteDwarf UMETA(DisplayName = "White dwarf"),
	None UMETA(DisplayName = "None")
};

/** Where a neighbouring system sits: a real-sky bearing (right ascension, declination) and distance in light-years. */
USTRUCT(BlueprintType)
struct FStarSystemSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "System") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "System") float RightAscensionHours = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "System") float DeclinationDeg = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "System") float DistanceLy = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "System") EStarClass StarClass = EStarClass::G;
	/** A companion star (None for a single star) this many AU from the primary. The primary's planets stay
	 *  inside a third of the separation, where orbits are stable; the companion gets a few worlds of its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "System") EStarClass CompanionClass = EStarClass::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "System") float CompanionSeparationAU = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "System") int32 Seed = 1;
};

/**
 * One star system: an anchor every body of the system hangs from, and the generator that fills it.
 *
 * Bodies are attached to the anchor so the floating origin moves one transform per system and the bodies keep
 * their exact relative positions: at light-year distances a double has a few metres of resolution, and moving
 * each body on its own would let them drift apart by metres over a long warp leg. The anchor has no primitive
 * of its own, so it never trips the GPU precision ensure however far away it sits; the bodies are far bodies
 * and hide themselves beyond the render horizon.
 */
UCLASS()
class SLONK_API AStarSystem : public AActor
{
	GENERATED_BODY()

public:
	AStarSystem();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "System") FString SystemName;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "System") int32 Seed = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "System") EStarClass StarClass = EStarClass::G;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "System") TObjectPtr<AStarBody> Star;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "System") TObjectPtr<AStarBody> Companion;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "System") TArray<TObjectPtr<APlanetBody>> Planets;
	/** Everything within this radius of the star belongs to the system (cm). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "System") double RadiusCm = 6.0e15;

	static constexpr double LightYearCm = 9.4607e17;
	static constexpr double AuCm = 1.495978707e13;

	/** The anchor for the hand-made home system: at the origin, adopting every body already in the level. */
	static AStarSystem* CreateHome(UWorld* World, const FString& Name);
	/** A new system generated from its spec, at the spec's bearing and distance from the home star. */
	static AStarSystem* Generate(UWorld* World, const FStarSystemSpec& Spec, const FVector& HomeStarLocation);
	/** Direction (unit vector) of a sky bearing: X toward the vernal equinox, Z to the north celestial pole. */
	static FVector DirectionFromSky(float RightAscensionHours, float DeclinationDeg);

	/** Attach a body to this anchor, keeping its world transform. */
	void Adopt(AActor* Body);
	/** Attach every unattached far body within RadiusCm of the star (moons, nebulae, ships spawned later). */
	int32 AdoptLoose();
	/** The system whose star is nearest to a location, or null. */
	static AStarSystem* Nearest(UWorld* World, const FVector& Location);
	FVector GetStarLocation() const;
	double DistanceFrom(const FVector& Location) const { return FVector::Dist(GetStarLocation(), Location); }

	/** sqrt of a class's luminosity relative to the Sun: orbit radii scale by it so the temperate band stays put. */
	static double LuminosityScale(EStarClass Class);

	/** Roman numeral for a planet or moon's place (0-based), "I".."XII". */
	static const TCHAR* Numeral(int32 Index);
	/** A proper name for a world, Trek-flavoured syllables from the stream; never one already in Used. */
	static FString GenerateWorldName(FRandomStream& Rng, TSet<FString>& Used);
	/** "<Primary> <numeral>" with the first numeral no moon of that primary carries yet. */
	static FString MoonDesignation(UWorld* World, const APlanetBody* Primary);
	/** The planet a moon orbits: the nearest non-moon world within 400 of its radii, else null. */
	static APlanetBody* PrimaryOf(UWorld* World, const APlanetBody* Moon);
	/** Give every unnamed world of this system a name: planets a generated one in orbit order, moons their primary's designation. */
	void NameUnnamedWorlds();

private:
	static void ApplyStarClass(AStarBody* S, EStarClass Class, FRandomStream& Rng);
	/** Planets on geometric orbits out from `StartAu` about `Centre`, named "<Prefix> I, II, ..." until MaxAu. */
	void SpawnPlanets(UWorld* World, FRandomStream& Rng, const FVector& Centre, const FString& Prefix, EStarClass Class, int32 Count, double MaxAu, TArray<APlanetBody*>& Out);
	UPROPERTY() TObjectPtr<USceneComponent> Anchor;
};
