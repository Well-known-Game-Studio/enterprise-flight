// Slonk - the crew roster: generation, the ship's day, the people's days, the captain's hand, and the save.

#include "CrewRoster.h"
#include "Kismet/GameplayStatics.h"

const TCHAR* UCrewRosterSubsystem::SlotName = TEXT("Crew");

namespace
{
	// ---- name tables (the TMP-era mix) ----
	const TCHAR* HumanFirst[] = { TEXT("Amara"), TEXT("Benjamin"), TEXT("Chidi"), TEXT("Dana"), TEXT("Elias"), TEXT("Farah"), TEXT("Gideon"), TEXT("Hana"), TEXT("Ivan"), TEXT("Jonah"), TEXT("Keiko"), TEXT("Lucas"), TEXT("Mei"), TEXT("Nikolai"), TEXT("Oona"), TEXT("Priya"), TEXT("Quentin"), TEXT("Rosa"), TEXT("Samir"), TEXT("Tomas"), TEXT("Uma"), TEXT("Viktor"), TEXT("Wren"), TEXT("Ximena"), TEXT("Yusuf"), TEXT("Zara"), TEXT("Arjun"), TEXT("Beatriz"), TEXT("Callum"), TEXT("Delphine"), TEXT("Emeka"), TEXT("Freya"), TEXT("Gabriel"), TEXT("Hiro"), TEXT("Ingrid"), TEXT("Jamal"), TEXT("Kirsten"), TEXT("Leandro"), TEXT("Maren"), TEXT("Nadia"), TEXT("Oscar"), TEXT("Paulo"), TEXT("Rhiannon"), TEXT("Sanjay"), TEXT("Tamsin"), TEXT("Ulysses"), TEXT("Valentina"), TEXT("Wesley"), TEXT("Yara"), TEXT("Zoran") };
	const TCHAR* HumanLast[] = { TEXT("Adeyemi"), TEXT("Brandt"), TEXT("Castellano"), TEXT("Duval"), TEXT("Eriksen"), TEXT("Fontaine"), TEXT("Grieve"), TEXT("Hartmann"), TEXT("Ibarra"), TEXT("Jensen"), TEXT("Kowalski"), TEXT("Lindqvist"), TEXT("Mbeki"), TEXT("Nakamura"), TEXT("Okafor"), TEXT("Park"), TEXT("Quinlan"), TEXT("Reyes"), TEXT("Sato"), TEXT("Tremblay"), TEXT("Underhill"), TEXT("Varga"), TEXT("Whitlock"), TEXT("Yamada"), TEXT("Zielinski"), TEXT("Achterberg"), TEXT("Bakshi"), TEXT("Calloway"), TEXT("Dimitriou"), TEXT("Esposito"), TEXT("Farouk"), TEXT("Gallagher"), TEXT("Holm"), TEXT("Iqbal"), TEXT("Jafari"), TEXT("Kimura"), TEXT("Lorenz"), TEXT("Moreau"), TEXT("Nwosu"), TEXT("Olsen"), TEXT("Petrov"), TEXT("Rahimi"), TEXT("Singh"), TEXT("Tanaka"), TEXT("Uribe"), TEXT("Vance"), TEXT("Weiss"), TEXT("Xu"), TEXT("Young"), TEXT("Zhou") };
	const TCHAR* VulcanNames[] = { TEXT("Sarel"), TEXT("T'Pau"), TEXT("Stavek"), TEXT("T'Mir"), TEXT("Sovar"), TEXT("T'Vran"), TEXT("Selek"), TEXT("T'Lin"), TEXT("Savik"), TEXT("T'Rea"), TEXT("Sutok"), TEXT("T'Ven"), TEXT("Soval"), TEXT("T'Kel") };
	const TCHAR* AndorianNames[] = { TEXT("Thelin"), TEXT("Shran"), TEXT("Tarah"), TEXT("Thy'lek"), TEXT("Jhamel"), TEXT("Keval"), TEXT("Talas"), TEXT("Thon"), TEXT("Shras"), TEXT("Lissan") };
	const TCHAR* TellariteNames[] = { TEXT("Gral"), TEXT("Naarg"), TEXT("Tevrin"), TEXT("Brell"), TEXT("Gorv"), TEXT("Skalaar"), TEXT("Mor"), TEXT("Tholvik") };
	const TCHAR* CaitianNames[] = { TEXT("M'Ress"), TEXT("R'Shal"), TEXT("K'Vara"), TEXT("S'Rel"), TEXT("M'Tar"), TEXT("T'Shen") };
	const TCHAR* BolianNames[] = { TEXT("Chell"), TEXT("Zim"), TEXT("Mot"), TEXT("Rixx"), TEXT("Boq"), TEXT("Hars") };
	const TCHAR* DeltanNames[] = { TEXT("Ilia"), TEXT("Tavo"), TEXT("Rhaan"), TEXT("Sela"), TEXT("Dovan") };
	const TCHAR* BetazoidNames[] = { TEXT("Lwaxana"), TEXT("Tam"), TEXT("Devinoni"), TEXT("Kestra"), TEXT("Rael") };
	const TCHAR* RigelianNames[] = { TEXT("Vaal"), TEXT("Orin"), TEXT("Kesh"), TEXT("Tarn") };
	const TCHAR* SaurianNames[] = { TEXT("Sssk"), TEXT("Rrash"), TEXT("Lssar") };
	const TCHAR* Homes[] = { TEXT("Lagos, Earth"), TEXT("San Francisco, Earth"), TEXT("Nairobi, Earth"), TEXT("Kyoto, Earth"), TEXT("Montreal, Earth"), TEXT("Mumbai, Earth"), TEXT("Sao Paulo, Earth"), TEXT("Reykjavik, Earth"), TEXT("Luna, Tycho City"), TEXT("Mars, Utopia Planitia"), TEXT("Alpha Centauri"), TEXT("Deneva"), TEXT("Tarsus IV"), TEXT("Cestus III"), TEXT("Vega Colony"), TEXT("Rigel II"), TEXT("Starbase 11"), TEXT("the Bradbury colony") };
	const TCHAR* Classes[] = { TEXT("Academy class of '74"), TEXT("Academy class of '77"), TEXT("Academy class of '79"), TEXT("Academy class of '81"), TEXT("enlisted at Mars"), TEXT("enlisted at Earth"), TEXT("transferred from the Lexington"), TEXT("transferred from the Yorktown"), TEXT("transferred from Starbase 11"), TEXT("transferred from the Hood") };
	const TCHAR* Hobbies[] = { TEXT("chess"), TEXT("the rec deck's handball court"), TEXT("Andorian flute"), TEXT("old films"), TEXT("the hydroponics bay"), TEXT("cards"), TEXT("tri-dimensional chess"), TEXT("sketching the nebulae"), TEXT("the ship's choir"), TEXT("fencing") };

	const TCHAR* FamilyRelations[] = { TEXT("mother"), TEXT("father"), TEXT("sister"), TEXT("brother"), TEXT("daughter"), TEXT("son"), TEXT("partner"), TEXT("grandmother"), TEXT("uncle"), TEXT("cousin") };
	const TCHAR* FriendRelations[] = { TEXT("academy roommate"), TEXT("oldest friend"), TEXT("former captain"), TEXT("first chief"), TEXT("shipmate from the Lexington"), TEXT("childhood friend"), TEXT("old flame"), TEXT("mentor at the Academy") };
	const TCHAR* NewsGood[] = { TEXT("has been promoted"), TEXT("is getting married"), TEXT("has had a child"), TEXT("has a new posting"), TEXT("sends a recording of the harvest festival"), TEXT("writes that the colony is thriving") };
	const TCHAR* NewsBad[] = { TEXT("is in hospital"), TEXT("writes that the harvest failed"), TEXT("has lost a friend"), TEXT("writes of trouble at the border"), TEXT("has not written in a long while") };

	struct FTraitDef { const TCHAR* Tag; const TCHAR* Text; float Weight; };
	const FTraitDef TraitDefs[] = {
		{ TEXT("Veteran"), TEXT("Veteran: steady under fire, restless on idle days"), 0.8f },
		{ TEXT("Homesick"), TEXT("Homesick: letters and leave matter"), 1.0f },
		{ TEXT("TransporterShy"), TEXT("Transporter-shy: dreads the pad"), 0.5f },
		{ TEXT("Pacifist"), TEXT("Pacifist: a fight costs, mending suits"), 0.5f },
		{ TEXT("ThrillSeeker"), TEXT("Thrill-seeker: wants the away missions"), 0.8f },
		{ TEXT("Devout"), TEXT("Devout: keeps the observances"), 0.5f },
		{ TEXT("Gambler"), TEXT("Gambler: cards after watch"), 0.6f },
		{ TEXT("ChessPlayer"), TEXT("Chess player: finds the other players"), 0.9f },
		{ TEXT("WoundedPride"), TEXT("Wounded pride: a reprimand lingers"), 0.6f },
		{ TEXT("Bookish"), TEXT("Bookish: the library over the rec deck"), 0.8f },
		{ TEXT("Musician"), TEXT("Musician: plays in the mess"), 0.7f },
		{ TEXT("Loner"), TEXT("Loner: company costs, quiet restores"), 0.5f },
		{ TEXT("Joker"), TEXT("Joker: lifts the watch"), 0.7f },
		{ TEXT("Perfectionist"), TEXT("Perfectionist: slow, sure, hard on the careless"), 0.6f },
	};
	struct FWantDef { const TCHAR* Tag; const TCHAR* Text; };
	const FWantDef DesireDefs[] = {
		{ TEXT("Promotion"), TEXT("a promotion") }, { TEXT("AwayMission"), TEXT("a place on an away mission") }, { TEXT("ShoreLeave"), TEXT("shore leave at a starbase") },
		{ TEXT("Letter"), TEXT("a letter from home") }, { TEXT("BridgePosting"), TEXT("a posting on the bridge") }, { TEXT("SeeNebula"), TEXT("a close look at a nebula") },
		{ TEXT("SeeGiant"), TEXT("a flight past a gas giant") }, { TEXT("Commendation"), TEXT("a commendation") }, { TEXT("Teach"), TEXT("someone to train up") },
		{ TEXT("Visit"), TEXT("a visit home") },
	};
	const FWantDef FearDefs[] = {
		{ TEXT("Vacuum"), TEXT("death in the dark between the stars") }, { TEXT("Klingons"), TEXT("the Klingons") }, { TEXT("Transporter"), TEXT("the transporter") },
		{ TEXT("Disgrace"), TEXT("disgrace before the crew") }, { TEXT("LosingFriend"), TEXT("losing a friend") }, { TEXT("Fire"), TEXT("fire in a sealed compartment") },
		{ TEXT("Borg"), TEXT("the thing that took the Reliant's sister ship") },
	};

	template <int N> const TCHAR* Pick(FRandomStream& R, const TCHAR* const (&Table)[N]) { return Table[R.RandRange(0, N - 1)]; }
	float Facetish(FRandomStream& R) { return (R.FRandRange(0.f, 100.f) + R.FRandRange(0.f, 100.f)) * 0.5f; }
	ECrewRank RankFromIndex(FRandomStream& R, int32 Index, int32 Count)
	{
		// Within a department: the first after the head and second are the officers, then the chiefs, the rest the ranks.
		const float F = (float)Index / FMath::Max(Count, 1);
		if (F < 0.06f) { return R.FRand() < 0.5f ? ECrewRank::Lieutenant : ECrewRank::LieutenantJG; }
		if (F < 0.15f) { return ECrewRank::Ensign; }
		if (F < 0.22f) { return ECrewRank::Chief; }
		if (F < 0.40f) { return ECrewRank::PettyOfficer; }
		if (F < 0.70f) { return ECrewRank::AbleCrewman; }
		return ECrewRank::Crewman;
	}
	bool IsOfficer(ECrewRank R) { return R >= ECrewRank::Ensign; }
}

// ---- names ----

FString UCrewRosterSubsystem::DepartmentName(ECrewDepartment D)
{
	switch (D)
	{
	case ECrewDepartment::Command: return TEXT("Command");
	case ECrewDepartment::Helm: return TEXT("Helm");
	case ECrewDepartment::Operations: return TEXT("Operations");
	case ECrewDepartment::Engineering: return TEXT("Engineering");
	case ECrewDepartment::Science: return TEXT("Science");
	case ECrewDepartment::Medical: return TEXT("Medical");
	case ECrewDepartment::Tactical: return TEXT("Tactical");
	case ECrewDepartment::Security: return TEXT("Security");
	case ECrewDepartment::Services: return TEXT("Services");
	default: return TEXT("-");
	}
}

FString UCrewRosterSubsystem::RankName(ECrewRank R)
{
	switch (R)
	{
	case ECrewRank::Crewman: return TEXT("Crewman");
	case ECrewRank::AbleCrewman: return TEXT("Able Crewman");
	case ECrewRank::PettyOfficer: return TEXT("Petty Officer");
	case ECrewRank::Chief: return TEXT("Chief Petty Officer");
	case ECrewRank::Ensign: return TEXT("Ensign");
	case ECrewRank::LieutenantJG: return TEXT("Lieutenant j.g.");
	case ECrewRank::Lieutenant: return TEXT("Lieutenant");
	case ECrewRank::LtCommander: return TEXT("Lieutenant Commander");
	case ECrewRank::Commander: return TEXT("Commander");
	case ECrewRank::Captain: return TEXT("Captain");
	default: return TEXT("-");
	}
}

FString UCrewRosterSubsystem::RankShort(ECrewRank R)
{
	switch (R)
	{
	case ECrewRank::Crewman: return TEXT("Crewman");
	case ECrewRank::AbleCrewman: return TEXT("Crewman");
	case ECrewRank::PettyOfficer: return TEXT("PO");
	case ECrewRank::Chief: return TEXT("Chief");
	case ECrewRank::Ensign: return TEXT("Ensign");
	case ECrewRank::LieutenantJG: return TEXT("Lt. j.g.");
	case ECrewRank::Lieutenant: return TEXT("Lt.");
	case ECrewRank::LtCommander: return TEXT("Lt. Cmdr.");
	case ECrewRank::Commander: return TEXT("Cmdr.");
	case ECrewRank::Captain: return TEXT("Capt.");
	default: return TEXT("");
	}
}

FString UCrewRosterSubsystem::SkillName(ECrewSkill S)
{
	static const TCHAR* N[] = { TEXT("Command"), TEXT("Helm"), TEXT("Operations"), TEXT("Engineering"), TEXT("Science"), TEXT("Medicine"), TEXT("Tactical"), TEXT("Security"), TEXT("Diplomacy"), TEXT("Trade") };
	return (int32)S < (int32)ECrewSkill::Count ? N[(int32)S] : TEXT("-");
}
FString UCrewRosterSubsystem::FacetName(ECrewFacet F)
{
	static const TCHAR* N[] = { TEXT("Resolve"), TEXT("Empathy"), TEXT("Curiosity"), TEXT("Discipline"), TEXT("Courage"), TEXT("Sociability"), TEXT("Ambition") };
	return (int32)F < (int32)ECrewFacet::Count ? N[(int32)F] : TEXT("-");
}
FString UCrewRosterSubsystem::NeedName(ECrewNeed N)
{
	static const TCHAR* S[] = { TEXT("Rest"), TEXT("Health"), TEXT("Safety"), TEXT("Belonging"), TEXT("Purpose"), TEXT("Recreation"), TEXT("Recognition") };
	return (int32)N < (int32)ECrewNeed::Count ? S[(int32)N] : TEXT("-");
}
FString UCrewRosterSubsystem::WatchName(ECrewWatch W) { static const TCHAR* S[] = { TEXT("Alpha"), TEXT("Beta"), TEXT("Gamma") }; return (int32)W < 3 ? S[(int32)W] : TEXT("-"); }
FString UCrewRosterSubsystem::ConditionName(ECrewCondition C)
{
	static const TCHAR* S[] = { TEXT("Fit"), TEXT("Wounded"), TEXT("Critical"), TEXT("Dead"), TEXT("On leave"), TEXT("Away"), TEXT("Transferred") };
	return (int32)C < (int32)ECrewCondition::Count ? S[(int32)C] : TEXT("-");
}
FString UCrewRosterSubsystem::ActivityName(ECrewActivity A)
{
	static const TCHAR* S[] = { TEXT("On watch"), TEXT("Asleep"), TEXT("In the mess"), TEXT("Off watch"), TEXT("In sickbay"), TEXT("On leave"), TEXT("Away") };
	return (int32)A < (int32)ECrewActivity::Count ? S[(int32)A] : TEXT("-");
}
FString UCrewRosterSubsystem::RelationName(ECrewRelation R)
{
	static const TCHAR* S[] = { TEXT("acquaintance"), TEXT("friend"), TEXT("close friend"), TEXT("rival"), TEXT("mentor"), TEXT("protege"), TEXT("partner"), TEXT("family") };
	return (int32)R < (int32)ECrewRelation::Count ? S[(int32)R] : TEXT("-");
}
FString UCrewRosterSubsystem::TraitText(FName Trait)
{
	for (const FTraitDef& T : TraitDefs) { if (Trait == FName(T.Tag)) { return T.Text; } }
	if (Trait == TEXT("Survivor")) { return TEXT("Survivor: walked out of a compartment that killed others"); }
	if (Trait == TEXT("Disillusioned")) { return TEXT("Disillusioned: saw the captain take a life that could have been spared"); }
	return Trait.ToString();
}
bool UCrewRosterSubsystem::ParseDepartment(const FString& Word, ECrewDepartment& Out)
{
	const FString W = Word.ToLower();
	for (int32 i = 0; i < (int32)ECrewDepartment::Count; ++i)
	{
		if (DepartmentName((ECrewDepartment)i).ToLower().StartsWith(W) && W.Len() >= 3) { Out = (ECrewDepartment)i; return true; }
	}
	if (W == TEXT("eng")) { Out = ECrewDepartment::Engineering; return true; }
	if (W == TEXT("sci")) { Out = ECrewDepartment::Science; return true; }
	if (W == TEXT("med")) { Out = ECrewDepartment::Medical; return true; }
	if (W == TEXT("ops")) { Out = ECrewDepartment::Operations; return true; }
	if (W == TEXT("tac")) { Out = ECrewDepartment::Tactical; return true; }
	if (W == TEXT("sec")) { Out = ECrewDepartment::Security; return true; }
	return false;
}
bool UCrewRosterSubsystem::ParseSkill(const FString& Word, ECrewSkill& Out)
{
	const FString W = Word.ToLower();
	for (int32 i = 0; i < (int32)ECrewSkill::Count; ++i)
	{
		if (SkillName((ECrewSkill)i).ToLower().StartsWith(W) && W.Len() >= 3) { Out = (ECrewSkill)i; return true; }
	}
	return false;
}
ECrewSkill UCrewRosterSubsystem::SkillOf(ECrewDepartment D)
{
	switch (D)
	{
	case ECrewDepartment::Command: return ECrewSkill::Command;
	case ECrewDepartment::Helm: return ECrewSkill::Helm;
	case ECrewDepartment::Operations: return ECrewSkill::Operations;
	case ECrewDepartment::Engineering: return ECrewSkill::Engineering;
	case ECrewDepartment::Science: return ECrewSkill::Science;
	case ECrewDepartment::Medical: return ECrewSkill::Medicine;
	case ECrewDepartment::Tactical: return ECrewSkill::Tactical;
	case ECrewDepartment::Security: return ECrewSkill::Security;
	default: return ECrewSkill::Trade;
	}
}
ECrewDepartment UCrewRosterSubsystem::DepartmentOf(EStation Station)
{
	switch (Station)
	{
	case EStation::Engineering: return ECrewDepartment::Engineering;
	case EStation::Tactical: return ECrewDepartment::Tactical;
	case EStation::Science: return ECrewDepartment::Science;
	case EStation::Helm: return ECrewDepartment::Helm;
	case EStation::Operations: return ECrewDepartment::Operations;
	case EStation::Medical: return ECrewDepartment::Medical;
	case EStation::Security: return ECrewDepartment::Security;
	default: return ECrewDepartment::Command;
	}
}
EStation UCrewRosterSubsystem::StationOf(ECrewDepartment D)
{
	switch (D)
	{
	case ECrewDepartment::Engineering: return EStation::Engineering;
	case ECrewDepartment::Tactical: return EStation::Tactical;
	case ECrewDepartment::Science: return EStation::Science;
	case ECrewDepartment::Helm: return EStation::Helm;
	case ECrewDepartment::Operations: return EStation::Operations;
	case ECrewDepartment::Medical: return EStation::Medical;
	case ECrewDepartment::Security: return EStation::Security;
	default: return EStation::Count;
	}
}

FString FCrewMember::Styled() const
{
	if (Department == ECrewDepartment::Medical && Rank >= ECrewRank::Lieutenant) { return TEXT("Dr. ") + FullName(); }
	const FString R = UCrewRosterSubsystem::RankShort(Rank);
	return R.IsEmpty() ? FullName() : R + TEXT(" ") + FullName();
}

float FCrewMember::Bias(FName Group) const
{
	for (const FCrewBias& B : Biases) { if (B.Group == Group) { return B.Disposition; } }
	return 0.f;
}

FString FCrewTie::Where() const
{
	if (Place.IsEmpty()) { return Kind == ECrewTiePlace::World ? TEXT("on a colony world") : Kind == ECrewTiePlace::Ship ? TEXT("aboard a starship") : TEXT("at a starbase"); }
	return Kind == ECrewTiePlace::Ship ? TEXT("aboard the ") + Place : (Kind == ECrewTiePlace::Station ? TEXT("at ") : TEXT("on ")) + Place;
}

const FCrewRelationship* FCrewMember::RelationWith(int32 Other) const
{
	for (const FCrewRelationship& R : Relationships) { if (R.Other == Other) { return &R; } }
	return nullptr;
}

// ---- lifetime ----

void UCrewRosterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Load();
	if (Roster.Num() == 0 && Lost.Num() == 0) { NewComplement(0); }   // a roster emptied by its losses stays so; ship crew reset starts over
	Rng.Initialize(Seed ^ (int32)(Hours * 7.0));
}

FString UCrewRosterSubsystem::StardateText() const
{
	return FString::Printf(TEXT("%.1f"), GetStardate());
}

void UCrewRosterSubsystem::Load()
{
	Roster.Reset(); Lost.Reset();
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		if (const USlonkCrewSaveGame* Save = Cast<USlonkCrewSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
		{
			Roster = Save->Roster; Lost = Save->Lost; Hours = Save->Hours; Seed = Save->Seed; NextId = Save->NextId; ShipName = Save->ShipName;
			MourningDays = Save->MourningDays; HourAccumulator = Save->HourAccumulator; PeerHours = Save->PeerHours; Targets = Save->Targets; Incidents = Save->Incidents;
			// The dead are kept in Lost; a relaunch must not count them as members again.
			Roster.RemoveAll([](const FCrewMember& M) { return M.Condition == ECrewCondition::Dead; });
			Validate();
			UE_LOG(LogTemp, Log, TEXT("Crew: loaded %d aboard, %d lost, stardate %s"), Roster.Num(), Lost.Num(), *StardateText());
		}
	}
	Dirty();
}

void UCrewRosterSubsystem::Validate()
{
	// A save from an older layout: the fixed arrays to their current lengths, the values to their ranges, one head
	// and one second per department, ids unique and NextId past them all.
	TSet<int32> Seen;
	int32 MaxId = -1;
	UsedNames.Reset();
	for (FCrewMember& M : Roster)
	{
		UsedNames.Add(M.FullName());
		if (M.Skills.Num() < (int32)ECrewSkill::Count) { M.Skills.SetNumZeroed((int32)ECrewSkill::Count); }
		if (M.SkillXP.Num() < (int32)ECrewSkill::Count) { M.SkillXP.SetNumZeroed((int32)ECrewSkill::Count); }
		if (M.Facets.Num() < (int32)ECrewFacet::Count) { const int32 Was = M.Facets.Num(); M.Facets.SetNum((int32)ECrewFacet::Count); for (int32 i = Was; i < M.Facets.Num(); ++i) { M.Facets[i] = 50.f; } }
		if (M.Needs.Num() < (int32)ECrewNeed::Count) { const int32 Was = M.Needs.Num(); M.Needs.SetNum((int32)ECrewNeed::Count); for (int32 i = Was; i < M.Needs.Num(); ++i) { M.Needs[i] = 0.7f; } }
		for (float& V : M.Needs) { V = FMath::Clamp(V, 0.f, 1.f); }
		M.Mood = FMath::Clamp(M.Mood, 0.f, 1.f); M.Stress = FMath::Clamp(M.Stress, 0.f, 1.f);
		if (M.Pronoun.IsEmpty()) { M.Pronoun = TEXT("they"); }
		if (!M.bOpinionsSeeded) { FRandomStream R(M.Id * 104729 + Seed); SeedOpinions(R, M); }
		if (Seen.Contains(M.Id) || M.Id < 0) { const int32 Old = M.Id; M.Id = ++MaxId + 1000000; UE_LOG(LogTemp, Warning, TEXT("Crew: %s had id %d already in use; now %d (their bonds and others' point at the other holder)"), *M.FullName(), Old, M.Id); }
		Seen.Add(M.Id); MaxId = FMath::Max(MaxId, M.Id);
	}
	for (const FCrewMember& L : Lost) { MaxId = FMath::Max(MaxId, L.Id); }
	NextId = FMath::Max(NextId, MaxId + 1);
	for (int32 d = 0; d < (int32)ECrewDepartment::Count; ++d)
	{
		const ECrewDepartment Dept = (ECrewDepartment)d;
		int32 Heads = 0, Seconds = 0;
		for (FCrewMember& M : Roster) { if (M.Department == Dept && M.Condition != ECrewCondition::Dead && M.Condition != ECrewCondition::Transferred) { if (M.bHead) { if (++Heads > 1) { M.bHead = false; } } if (M.bSecond) { if (++Seconds > 1) { M.bSecond = false; } } } }
		if (Heads == 0) { PromoteToHead(Dept, TEXT("the department had no head on the books")); }
		else if (Seconds == 0) { EnsureSecond(Dept); }
	}
}

void UCrewRosterSubsystem::Save()
{
	USlonkCrewSaveGame* Save = Cast<USlonkCrewSaveGame>(UGameplayStatics::CreateSaveGameObject(USlonkCrewSaveGame::StaticClass()));
	if (!Save) { return; }
	Save->Roster = Roster; Save->Lost = Lost; Save->Hours = Hours; Save->Seed = Seed; Save->NextId = NextId; Save->ShipName = ShipName;
	Save->MourningDays = MourningDays; Save->HourAccumulator = HourAccumulator; Save->PeerHours = PeerHours; Save->Targets = Targets; Save->Incidents = Incidents;
	UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
	bSaveWanted = false;
}

void UCrewRosterSubsystem::RequestSave()
{
	// Saves are coalesced: a few seconds after the last change, or at the day's end; the payload is every record.
	bSaveWanted = true;
	if (SaveDelaySeconds <= 0.f) { SaveDelaySeconds = 4.f; }
}

void UCrewRosterSubsystem::Reset()
{
	UGameplayStatics::DeleteGameInSlot(SlotName, 0);
	Roster.Reset(); Lost.Reset(); Recruits.Reset(); Hours = 0.0; NextId = 0; MourningDays = 0;
	NewComplement(0);
}

// ---- generation ----

void UCrewRosterSubsystem::MakeNamed(FRandomStream& R, FCrewMember& M, const TCHAR* First, const TCHAR* Last, const TCHAR* Species, const TCHAR* Pronoun)
{
	M.FirstName = First; M.LastName = Last; M.Species = Species; M.Pronoun = Pronoun;
}

FString UCrewRosterSubsystem::PostingFor(FRandomStream& R, ECrewDepartment Dept, int32 Index) const
{
	switch (Dept)
	{
	case ECrewDepartment::Command: return TEXT("Main bridge");
	case ECrewDepartment::Helm: return Index < 6 ? TEXT("Main bridge") : TEXT("Shuttle bay");
	case ECrewDepartment::Operations: { static const TCHAR* P[] = { TEXT("Main bridge"), TEXT("Computer core"), TEXT("Cargo bays"), TEXT("Shuttle bay"), TEXT("Environmental control") }; return Index < 6 ? P[0] : P[1 + (Index % 4)]; }
	case ECrewDepartment::Engineering: { static const TCHAR* P[] = { TEXT("Main engineering"), TEXT("Reactor"), TEXT("Impulse engineering"), TEXT("Shield generators"), TEXT("Environmental control"), TEXT("Deflector"), TEXT("Port nacelle"), TEXT("Starboard nacelle"), TEXT("Main engineering") }; return P[Index % 9]; }
	case ECrewDepartment::Science: { static const TCHAR* P[] = { TEXT("Computer core"), TEXT("Sensor bay"), TEXT("Computer core") }; return Index < 3 ? TEXT("Main bridge") : P[Index % 3]; }
	case ECrewDepartment::Medical: return TEXT("Sickbay");
	case ECrewDepartment::Tactical: { static const TCHAR* P[] = { TEXT("Phaser control"), TEXT("Torpedo bay") }; return Index < 3 ? TEXT("Main bridge") : P[Index % 2]; }
	case ECrewDepartment::Security: { static const TCHAR* P[] = { TEXT("Security and armoury"), TEXT("Cargo bays"), TEXT("Shuttle bay"), TEXT("Main bridge"), TEXT("Security and armoury") }; return P[Index % 5]; }
	default: { static const TCHAR* P[] = { TEXT("Crew quarters"), TEXT("Officers' quarters"), TEXT("Cargo bays") }; return P[Index % 3]; }
	}
}

namespace
{
	// A made-up name in a species' style, for when the tables run out.
	FString CoinName(FRandomStream& R, const FString& Species)
	{
		static const TCHAR* VulcanStart[] = { TEXT("S"), TEXT("T'"), TEXT("V"), TEXT("St"), TEXT("Sol") };
		static const TCHAR* AndorianStart[] = { TEXT("Th"), TEXT("Sh"), TEXT("Tal"), TEXT("Kel"), TEXT("Thr") };
		static const TCHAR* Mid[] = { TEXT("a"), TEXT("e"), TEXT("o"), TEXT("i"), TEXT("u"), TEXT("ar"), TEXT("el"), TEXT("or"), TEXT("ir") };
		static const TCHAR* VulcanEnd[] = { TEXT("k"), TEXT("v"), TEXT("n"), TEXT("ral"), TEXT("vik"), TEXT("ris"), TEXT("lan"), TEXT("mek") };
		static const TCHAR* OtherEnd[] = { TEXT("n"), TEXT("r"), TEXT("sh"), TEXT("k"), TEXT("th"), TEXT("v"), TEXT("ss"), TEXT("l") };
		const bool bVulcan = Species == TEXT("Vulcan");
		const bool bAndorian = Species == TEXT("Andorian");
		FString S = bVulcan ? Pick(R, VulcanStart) : bAndorian ? Pick(R, AndorianStart) : FString(Pick(R, OtherEnd)).ToUpper();
		S += Pick(R, Mid); if (R.FRand() < 0.5f) { S += Pick(R, Mid); }
		S += bVulcan ? Pick(R, VulcanEnd) : Pick(R, OtherEnd);
		return S;
	}
}

FCrewMember UCrewRosterSubsystem::MakePerson(FRandomStream& R, ECrewDepartment Dept, ECrewRank Rank, int32 Index, const FCrewNamed* Named)
{
	FCrewMember M;
	M.Id = NextId++;
	// Species: three in four human, the rest the Federation's neighbours; the named staff are who they are, and
	// everything that follows from a species (facets, home, family) follows from theirs.
	const float S = Named ? -1.f : R.FRand();
	const bool bShe = R.FRand() < 0.5f;
	if (Named) { MakeNamed(R, M, Named->First, Named->Last, Named->Species, Named->Pronoun); Rank = Named->Rank; }
	else if (S < 0.74f) { MakeNamed(R, M, Pick(R, HumanFirst), Pick(R, HumanLast), TEXT("Human"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.82f) { MakeNamed(R, M, Pick(R, VulcanNames), TEXT(""), TEXT("Vulcan"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.87f) { MakeNamed(R, M, Pick(R, AndorianNames), TEXT(""), TEXT("Andorian"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.90f) { MakeNamed(R, M, Pick(R, TellariteNames), TEXT(""), TEXT("Tellarite"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.93f) { MakeNamed(R, M, Pick(R, CaitianNames), TEXT(""), TEXT("Caitian"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.95f) { MakeNamed(R, M, Pick(R, BolianNames), TEXT(""), TEXT("Bolian"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.97f) { MakeNamed(R, M, Pick(R, DeltanNames), TEXT(""), TEXT("Deltan"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.985f) { MakeNamed(R, M, Pick(R, BetazoidNames), TEXT(""), TEXT("Betazoid"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.995f) { MakeNamed(R, M, Pick(R, RigelianNames), TEXT(""), TEXT("Rigelian"), TEXT("they")); }
	else { MakeNamed(R, M, Pick(R, SaurianNames), TEXT(""), TEXT("Saurian"), TEXT("they")); }
	// No two aboard alike: re-draw a human, coin a name for the single-name peoples.
	for (int32 Try = 0; Try < 24 && UsedNames.Contains(M.FullName()); ++Try)
	{
		if (M.Species == TEXT("Human")) { M.FirstName = Pick(R, HumanFirst); if (Try > 6) { M.LastName = Pick(R, HumanLast); } }
		else { M.FirstName = CoinName(R, M.Species); }
	}
	UsedNames.Add(M.FullName());
	M.Rank = Rank;
	M.Department = Dept;
	M.Age = (int32)FMath::Clamp(22 + (int32)Rank * 3 + R.RandRange(-3, 9), 19, 64);
	M.Home = M.Species == TEXT("Human") ? Pick(R, Homes) : (M.Species == TEXT("Vulcan") ? TEXT("Vulcan") : M.Species == TEXT("Andorian") ? TEXT("Andoria") : M.Species == TEXT("Tellarite") ? TEXT("Tellar Prime") : M.Species == TEXT("Caitian") ? TEXT("Cait") : M.Species == TEXT("Bolian") ? TEXT("Bolarus IX") : M.Species == TEXT("Deltan") ? TEXT("Delta IV") : M.Species == TEXT("Betazoid") ? TEXT("Betazed") : M.Species == TEXT("Rigelian") ? TEXT("Rigel V") : TEXT("Sauria"));
	M.Backstory = FString::Printf(TEXT("%s; %s"), Pick(R, Classes), Pick(R, Hobbies));
	M.Birthday = R.RandRange(0, 364);
	M.Watch = (ECrewWatch)(Index % 3);
	M.Posting = PostingFor(R, Dept, Index);
	// Skills: the department's by rank, the rest a little.
	M.Skills.SetNumZeroed((int32)ECrewSkill::Count); M.SkillXP.SetNumZeroed((int32)ECrewSkill::Count);
	static const float Base[] = { 18.f, 28.f, 38.f, 52.f, 38.f, 48.f, 58.f, 68.f, 78.f, 86.f };
	for (int32 i = 0; i < (int32)ECrewSkill::Count; ++i) { M.Skills[i] = R.FRandRange(4.f, 26.f); }
	M.Skills[(int32)SkillOf(Dept)] = FMath::Clamp(Base[(int32)Rank] + R.FRandRange(-6.f, 10.f), 5.f, 97.f);
	if (IsOfficer(Rank)) { M.Skills[(int32)ECrewSkill::Command] = FMath::Max(M.Skills[(int32)ECrewSkill::Command], Base[(int32)Rank] * 0.6f + R.FRandRange(0.f, 12.f)); }
	if (Dept == ECrewDepartment::Command) { M.Skills[(int32)ECrewSkill::Diplomacy] = FMath::Max(M.Skills[(int32)ECrewSkill::Diplomacy], Base[(int32)Rank] * 0.7f); }
	// Facets, with a little species colour.
	M.Facets.SetNumZeroed((int32)ECrewFacet::Count);
	for (int32 i = 0; i < (int32)ECrewFacet::Count; ++i) { M.Facets[i] = Facetish(R); }
	if (M.Species == TEXT("Vulcan")) { M.Facets[(int32)ECrewFacet::Discipline] = FMath::Min(M.Facets[(int32)ECrewFacet::Discipline] + 25.f, 100.f); M.Facets[(int32)ECrewFacet::Resolve] += 15.f; M.Facets[(int32)ECrewFacet::Sociability] -= 20.f; }
	if (M.Species == TEXT("Andorian")) { M.Facets[(int32)ECrewFacet::Courage] += 15.f; }
	if (M.Species == TEXT("Tellarite")) { M.Facets[(int32)ECrewFacet::Empathy] -= 15.f; M.Facets[(int32)ECrewFacet::Sociability] += 10.f; }
	if (M.Species == TEXT("Betazoid") || M.Species == TEXT("Deltan")) { M.Facets[(int32)ECrewFacet::Empathy] += 25.f; }
	for (float& F : M.Facets) { F = FMath::Clamp(F, 0.f, 100.f); }
	// Traits: one to three, by weight.
	const int32 NT = R.FRand() < 0.2f ? 1 : (R.FRand() < 0.6f ? 2 : 3);
	float Total = 0.f; for (const FTraitDef& T : TraitDefs) { Total += T.Weight; }
	for (int32 n = 0; n < NT; ++n)
	{
		float X = R.FRandRange(0.f, Total);
		for (const FTraitDef& T : TraitDefs) { X -= T.Weight; if (X <= 0.f) { M.Traits.AddUnique(FName(T.Tag)); break; } }
	}
	// Needs, mood.
	M.Needs.SetNumZeroed((int32)ECrewNeed::Count);
	for (float& N : M.Needs) { N = R.FRandRange(0.6f, 0.95f); }
	M.Needs[(int32)ECrewNeed::Health] = 1.f;
	// Wants: desires from the facets and traits, one fear.
	auto Desire = [&](const TCHAR* Tag) { for (const FWantDef& D : DesireDefs) { if (FCString::Strcmp(D.Tag, Tag) == 0) { FCrewWant W; W.Tag = Tag; W.Text = D.Text; W.SinceDay = GetDay(); M.Wants.Add(W); } } };
	if (M.Facet(ECrewFacet::Ambition) > 60.f && Rank < ECrewRank::Commander) { Desire(TEXT("Promotion")); }
	if (M.Facet(ECrewFacet::Curiosity) > 60.f) { Desire(R.FRand() < 0.5f ? TEXT("SeeNebula") : TEXT("SeeGiant")); }
	if (M.HasTrait(TEXT("ThrillSeeker")) || (M.Facet(ECrewFacet::Courage) > 70.f && R.FRand() < 0.5f)) { Desire(TEXT("AwayMission")); }
	if (M.HasTrait(TEXT("Homesick"))) { Desire(TEXT("Letter")); }
	if (M.Facet(ECrewFacet::Sociability) > 65.f && R.FRand() < 0.5f) { Desire(TEXT("ShoreLeave")); }
	if (M.Wants.Num() == 0) { Desire(R.FRand() < 0.5f ? TEXT("Commendation") : TEXT("ShoreLeave")); }
	{
		const FWantDef& F = M.HasTrait(TEXT("TransporterShy")) ? FearDefs[2] : FearDefs[R.RandRange(0, UE_ARRAY_COUNT(FearDefs) - 1)];
		FCrewWant W; W.Tag = F.Tag; W.Text = F.Text; W.bFear = true; W.SinceDay = GetDay(); M.Wants.Add(W);
	}
	MakeTies(R, M);
	SeedOpinions(R, M);
	if (M.HasTrait(TEXT("Homesick")) || (M.Facet(ECrewFacet::Sociability) > 60.f && R.FRand() < 0.3f))
	{
		for (const FCrewTie& T : M.Ties) { if (T.bFamily) { FCrewWant W; W.Tag = TEXT("Visit"); W.Text = FString::Printf(TEXT("a visit to %s %s"), *T.Relation, *T.Name); W.SinceDay = GetDay(); M.Wants.Add(W); break; } }
	}
	M.Service.JoinedStardate = GetStardate();
	M.Condition = ECrewCondition::Fit;
	UpdateMood(M);
	return M;
}

void UCrewRosterSubsystem::MakeTies(FRandomStream& R, FCrewMember& M)
{
	// Family at home (named for the species), and for most a friend in the fleet or at a station; the places
	// stay placeholders until the galaxy's own names are known.
	auto NameLike = [&](const FCrewMember& Of) -> FString
	{
		if (Of.Species == TEXT("Human")) { return FString(Pick(R, HumanFirst)) + TEXT(" ") + (R.FRand() < 0.7f && !Of.LastName.IsEmpty() ? Of.LastName : FString(Pick(R, HumanLast))); }
		if (Of.Species == TEXT("Vulcan")) { return Pick(R, VulcanNames); }
		if (Of.Species == TEXT("Andorian")) { return Pick(R, AndorianNames); }
		if (Of.Species == TEXT("Tellarite")) { return Pick(R, TellariteNames); }
		if (Of.Species == TEXT("Caitian")) { return Pick(R, CaitianNames); }
		if (Of.Species == TEXT("Bolian")) { return Pick(R, BolianNames); }
		if (Of.Species == TEXT("Deltan")) { return Pick(R, DeltanNames); }
		if (Of.Species == TEXT("Betazoid")) { return Pick(R, BetazoidNames); }
		return FString(Pick(R, HumanFirst)) + TEXT(" ") + Pick(R, HumanLast);
	};
	const int32 Family = R.FRand() < 0.15f ? 0 : (R.FRand() < 0.6f ? 1 : 2);
	for (int32 i = 0; i < Family; ++i)
	{
		FCrewTie T; T.Name = NameLike(M); T.Relation = Pick(R, FamilyRelations); T.bFamily = true; T.Affinity = R.FRandRange(0.5f, 0.95f);
		// Most family is at home; some has gone to the fleet or a station.
		const float P = R.FRand();
		if (P < 0.7f) { T.Kind = ECrewTiePlace::World; T.Place = M.Home.Contains(TEXT(", Earth")) ? TEXT("Earth") : M.Home; if (M.Home.StartsWith(TEXT("Starbase"))) { T.Kind = ECrewTiePlace::Station; T.Place = M.Home; } }
		else if (P < 0.88f) { T.Kind = ECrewTiePlace::Ship; }
		else { T.Kind = ECrewTiePlace::Station; }
		M.Ties.Add(T);
	}
	if (R.FRand() < 0.75f)
	{
		FCrewTie T; T.Name = NameLike(M); T.Relation = Pick(R, FriendRelations); T.Affinity = R.FRandRange(0.3f, 0.8f);
		const float P = R.FRand();
		T.Kind = P < 0.45f ? ECrewTiePlace::Ship : P < 0.75f ? ECrewTiePlace::Station : ECrewTiePlace::World;
		if (T.Relation.Contains(TEXT("Lexington"))) { T.Kind = ECrewTiePlace::Ship; T.Place = TEXT("U.S.S. Lexington"); }
		M.Ties.Add(T);
	}
}

void UCrewRosterSubsystem::SeedOpinions(FRandomStream& R, FCrewMember& M)
{
	// Trust in the captain starts a little above nothing, more for the loyal by nature, less for the ambitious.
	M.Trust = FMath::Clamp(0.15f + (M.Facet(ECrewFacet::Discipline) - 50.f) / 250.f - (M.Facet(ECrewFacet::Ambition) - 50.f) / 400.f + R.FRandRange(-0.1f, 0.1f), -0.3f, 0.6f);
	M.Biases.Reset();
	auto Add = [&](const TCHAR* Group, float D, const FString& Why) { if (FMath::Abs(D) < 0.05f) { return; } for (FCrewBias& B : M.Biases) { if (B.Group == Group) { B.Disposition = FMath::Clamp(B.Disposition + D, -1.f, 1.f); return; } } FCrewBias B; B.Group = Group; B.Disposition = FMath::Clamp(D, -1.f, 1.f); B.Reason = Why; M.Biases.Add(B); };
	// The powers: the border colonies remember the Klingons, the veterans whoever they fought, the odd one the Orions.
	const bool bBorder = M.Home.Contains(TEXT("Cestus")) || M.Home.Contains(TEXT("Deneva")) || M.Home.Contains(TEXT("Tarsus")) || M.Home.Contains(TEXT("Rigel")) || M.Home.Contains(TEXT("Starbase"));
	if (bBorder && R.FRand() < 0.5f) { Add(TEXT("Klingon"), -R.FRandRange(0.2f, 0.6f), TEXT("grew up on the border")); }
	if (M.HasTrait(TEXT("Veteran"))) { Add(R.FRand() < 0.7f ? TEXT("Klingon") : TEXT("Romulan"), -R.FRandRange(0.2f, 0.5f), TEXT("fought them")); }
	if (R.FRand() < 0.12f) { Add(TEXT("Orion"), -R.FRandRange(0.2f, 0.5f), TEXT("the syndicate took someone")); }
	if (R.FRand() < 0.08f) { Add(TEXT("Klingon"), R.FRandRange(0.1f, 0.3f), TEXT("served alongside one")); }
	// The peoples: a few old grudges and a few admirations, mostly mild; the empathetic hold fewer.
	const float Prej = R.FRand() * (1.3f - M.Facet(ECrewFacet::Empathy) / 100.f);
	if (M.Species == TEXT("Vulcan")) { if (Prej > 0.5f) { Add(TEXT("Tellarite"), -R.FRandRange(0.1f, 0.4f), TEXT("finds them illogical")); } if (R.FRand() < 0.3f) { Add(TEXT("Andorian"), -R.FRandRange(0.1f, 0.3f), TEXT("old history")); } }
	else if (M.Species == TEXT("Andorian")) { if (Prej > 0.4f) { Add(TEXT("Vulcan"), -R.FRandRange(0.1f, 0.4f), TEXT("old history")); } if (R.FRand() < 0.3f) { Add(TEXT("Human"), R.FRandRange(0.1f, 0.3f), TEXT("fought beside them")); } }
	else if (M.Species == TEXT("Tellarite")) { if (Prej > 0.4f) { Add(TEXT("Vulcan"), -R.FRandRange(0.1f, 0.3f), TEXT("finds them cold")); } }
	else if (M.Species == TEXT("Human"))
	{
		if (Prej > 0.75f) { static const TCHAR* Others[] = { TEXT("Tellarite"), TEXT("Andorian"), TEXT("Vulcan"), TEXT("Caitian"), TEXT("Bolian"), TEXT("Deltan") }; Add(Others[R.RandRange(0, 5)], -R.FRandRange(0.15f, 0.4f), TEXT("never left Earth before the Academy")); }
		if (R.FRand() < 0.2f) { Add(TEXT("Vulcan"), R.FRandRange(0.1f, 0.3f), TEXT("admires the discipline")); }
	}
	if (M.Facet(ECrewFacet::Empathy) < 30.f && R.FRand() < 0.3f) { Add(TEXT("Betazoid"), -R.FRandRange(0.1f, 0.3f), TEXT("dislikes being read")); }
	M.Suspicions.Reset();
	M.bOpinionsSeeded = true;
}

void UCrewRosterSubsystem::BindPlaces(const TArray<FString>& Worlds, const TArray<FString>& Stations, const TArray<FString>& Ships)
{
	int32 Bound = 0;
	for (FCrewMember& M : Roster)
	{
		FRandomStream R(M.Id * 7919 + Seed);
		for (FCrewTie& T : M.Ties)
		{
			if (!T.Place.IsEmpty()) { continue; }
			const TArray<FString>& From = T.Kind == ECrewTiePlace::World ? Worlds : T.Kind == ECrewTiePlace::Station ? Stations : Ships;
			if (From.Num() == 0) { continue; }
			T.Place = From[R.RandRange(0, From.Num() - 1)];
			++Bound;
		}
	}
	if (Bound > 0) { UE_LOG(LogTemp, Log, TEXT("Crew: %d ties bound to the galaxy's names (%d worlds, %d stations, %d ships)"), Bound, Worlds.Num(), Stations.Num(), Ships.Num()); Dirty(); RequestSave(); }
}

bool UCrewRosterSubsystem::HasUnboundTies() const
{
	for (const FCrewMember& M : Roster) { for (const FCrewTie& T : M.Ties) { if (T.Place.IsEmpty()) { return true; } } }
	return false;
}

void UCrewRosterSubsystem::Reunion(FCrewMember& M, FCrewTie& T, bool bAshore)
{
	T.LastContactDay = GetDay();
	const FString Who = FString::Printf(TEXT("%s %s"), *T.Relation, *T.Name);
	if (bAshore)
	{
		Remember(M, FString::Printf(TEXT("An evening with %s"), *T.Name), 0.18f * (0.5f + T.Affinity));
		Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("Alongside %s: an evening ashore with %s."), *T.Place, *Who), true);
		M.Needs[(int32)ECrewNeed::Belonging] = FMath::Min(M.Needs[(int32)ECrewNeed::Belonging] + 0.3f, 1.f);
		for (FCrewWant& W : M.Wants) { if (W.Tag == TEXT("Visit") && !W.bGranted && T.bFamily) { W.bGranted = true; Remember(M, TEXT("Home, for a few hours"), 0.2f, true); } }
		Report(T.bFamily ? ECrewDepartment::Command : M.Department, FString::Printf(TEXT("%s has family at %s; %s asked for a few hours ashore and had them."), *M.Styled(), *T.Place, *M.Pronoun));
	}
	else
	{
		Remember(M, FString::Printf(TEXT("A signal from %s"), *T.Name), 0.1f * (0.5f + T.Affinity));
		Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("Within hailing range of %s: a few minutes on subspace with %s."), T.Kind == ECrewTiePlace::Ship ? *(TEXT("the ") + T.Place) : *T.Place, *Who));
		if (T.Kind == ECrewTiePlace::Ship) { Report(ECrewDepartment::Operations, FString::Printf(TEXT("The %s is in range; %s has %s aboard her and asked for a channel. Granted."), *T.Place, *M.Styled(), *Who)); }
	}
}

void UCrewRosterSubsystem::NewComplement(int32 InSeed, int32 Complement)
{
	Seed = InSeed != 0 ? InSeed : (int32)(FDateTime::Now().GetTicks() % 1000003);
	FRandomStream R(Seed);
	Rng.Initialize(Seed);
	Roster.Reset(); Lost.Reset(); Recruits.Reset(); NextId = 0; Hours = 0.0; HourAccumulator = 0.0; DeadBase = 0; MourningDays = 0; PeerHours.Reset(); UsedNames.Reset();
	Incidents.Reset(); IncidentsToday = 0; RequestsToday = 0; RiftsReported = -100; Reports.Reset();
	Targets.SetNum((int32)ECrewDepartment::Count);
	// Department strengths for a refit's 502.
	const int32 Sizes[(int32)ECrewDepartment::Count] = { 6, 24, 60, 150, 70, 30, 36, 60, 66 };
	int32 Scale = FMath::Max(Complement, 20);
	// The senior staff by name: the ship's own, then a first officer and a chief of the boat.
	using FNamed = FCrewNamed;
	const FNamed Heads[] = {
		{ ECrewDepartment::Command, TEXT("Ilse"), TEXT("Varga"), TEXT("Human"), TEXT("she"), ECrewRank::Commander },
		{ ECrewDepartment::Engineering, TEXT("Rhys"), TEXT("Dahl"), TEXT("Human"), TEXT("he"), ECrewRank::LtCommander },
		{ ECrewDepartment::Tactical, TEXT("Sorek"), TEXT(""), TEXT("Vulcan"), TEXT("he"), ECrewRank::Lieutenant },
		{ ECrewDepartment::Science, TEXT("T'Lara"), TEXT(""), TEXT("Vulcan"), TEXT("she"), ECrewRank::Commander },
		{ ECrewDepartment::Helm, TEXT("Imani"), TEXT("Okonkwo"), TEXT("Human"), TEXT("she"), ECrewRank::Lieutenant },
		{ ECrewDepartment::Operations, TEXT("Pell"), TEXT("Haskins"), TEXT("Human"), TEXT("he"), ECrewRank::Lieutenant },
		{ ECrewDepartment::Medical, TEXT("Elena"), TEXT("Vasquez"), TEXT("Human"), TEXT("she"), ECrewRank::Commander },
		{ ECrewDepartment::Security, TEXT("Vreen"), TEXT(""), TEXT("Andorian"), TEXT("he"), ECrewRank::Lieutenant },
		{ ECrewDepartment::Services, TEXT("Marguerite"), TEXT("Okafor"), TEXT("Human"), TEXT("she"), ECrewRank::Chief },
	};
	const FNamed Seconds[] = {
		{ ECrewDepartment::Engineering, TEXT("Mara"), TEXT("Quill"), TEXT("Human"), TEXT("she"), ECrewRank::Ensign },
		{ ECrewDepartment::Medical, TEXT("Anand"), TEXT("Kessler"), TEXT("Human"), TEXT("he"), ECrewRank::Lieutenant },
	};
	for (int32 d = 0; d < (int32)ECrewDepartment::Count; ++d)
	{
		const ECrewDepartment Dept = (ECrewDepartment)d;
		const int32 Count = FMath::Max(2, (int32)FMath::RoundToInt(Sizes[d] * (float)Scale / 502.f));
		Targets[d] = Count;
		for (int32 i = 0; i < Count; ++i)
		{
			ECrewRank Rank = i == 0 ? ECrewRank::LtCommander : (i == 1 ? ECrewRank::Lieutenant : RankFromIndex(R, i - 2, Count - 2));
			const FNamed* Named = nullptr;
			if (i == 0) { for (const FNamed& H : Heads) { if (H.Dept == Dept) { Named = &H; } } }
			if (i == 1) { for (const FNamed& H : Seconds) { if (H.Dept == Dept) { Named = &H; } } }
			FCrewMember M = MakePerson(R, Dept, Rank, i, Named);
			if (i == 0)
			{
				M.bHead = true;
				M.Skills[(int32)SkillOf(Dept)] = FMath::Max(M.Skills[(int32)SkillOf(Dept)], 72.f + R.FRandRange(0.f, 20.f));
				M.Skills[(int32)ECrewSkill::Command] = FMath::Max(M.Skills[(int32)ECrewSkill::Command], 55.f + R.FRandRange(0.f, 25.f));
				M.Watch = ECrewWatch::Alpha;
				M.Age = FMath::Max(M.Age, 34);
			}
			else if (i == 1)
			{
				M.bSecond = true;
				M.Skills[(int32)SkillOf(Dept)] = FMath::Max(M.Skills[(int32)SkillOf(Dept)], 52.f + R.FRandRange(0.f, 16.f));
				M.Watch = ECrewWatch::Beta;
			}
			Record(M, ECrewEventKind::Joined, FString::Printf(TEXT("Joined the %s's %s department; %s."), ShipName.IsEmpty() ? TEXT("ship") : *ShipName, *DepartmentName(Dept).ToLower(), *M.Backstory), true);
			Roster.Add(M);
		}
	}
	// Bonds: a few friends within the department and watch, the odd rival, the head a mentor to the second.
	for (int32 i = 0; i < Roster.Num(); ++i)
	{
		FCrewMember& A = Roster[i];
		const int32 Friends = R.RandRange(1, 3);
		for (int32 n = 0; n < Friends; ++n)
		{
			const int32 j = R.RandRange(0, Roster.Num() - 1);
			if (j == i) { continue; }
			FCrewMember& B = Roster[j];
			if (B.Department != A.Department && R.FRand() < 0.7f) { continue; }
			Bond(A, B, R.FRandRange(0.25f, 0.55f), ECrewRelation::Friend);
		}
		if (R.FRand() < 0.1f)
		{
			const int32 j = R.RandRange(0, Roster.Num() - 1);
			if (j != i) { Bond(A, Roster[j], -R.FRandRange(0.2f, 0.5f), ECrewRelation::Rival); }
		}
	}
	for (int32 d = 0; d < (int32)ECrewDepartment::Count; ++d)
	{
		FCrewMember* H = const_cast<FCrewMember*>(Head((ECrewDepartment)d)); FCrewMember* S2 = const_cast<FCrewMember*>(Second((ECrewDepartment)d));
		if (H && S2) { SetRelation(*H, S2->Id, ECrewRelation::Mentor, 0.4f); SetRelation(*S2, H->Id, ECrewRelation::Protege, 0.4f); }
	}
	for (FCrewMember& M : Roster) { Place(M); UpdateMood(M); }
	UE_LOG(LogTemp, Log, TEXT("Crew: new complement of %d from seed %d"), Roster.Num(), Seed);
	Dirty();
	Save();
}

void UCrewRosterSubsystem::MakeRecruits(const FString& StationName)
{
	// A pool for this station and day: the vacancies' departments first, then a spread.
	Recruits.Reset();
	RecruitStation = StationName; RecruitDay = GetDay();
	FRandomStream R(GetTypeHash(StationName) ^ (RecruitDay * 2654435761u) ^ Seed);
	int32 Counts[(int32)ECrewDepartment::Count] = { 0 };
	for (const FCrewMember& M : Roster) { if (M.Condition != ECrewCondition::Transferred && M.Condition != ECrewCondition::Dead) { ++Counts[(int32)M.Department]; } }
	TArray<ECrewDepartment> Pool;
	for (int32 d = 0; d < (int32)ECrewDepartment::Count; ++d) { for (int32 k = Counts[d]; k < TargetFor((ECrewDepartment)d); ++k) { Pool.Add((ECrewDepartment)d); } }
	const int32 SavedNext = NextId;
	for (int32 i = 0; i < 8; ++i)
	{
		const ECrewDepartment Dept = i < Pool.Num() ? Pool[i] : (ECrewDepartment)R.RandRange(1, (int32)ECrewDepartment::Count - 1);
		NextId = 100000 + RecruitDay * 100 + i;   // provisional ids; a real one on signing
		FCrewMember M = MakePerson(R, Dept, RankFromIndex(R, R.RandRange(0, 9), 10), R.RandRange(0, 20));
		M.Backstory = FString::Printf(TEXT("%s; met at %s"), Pick(R, Classes), *StationName);
		if (R.FRand() < 0.4f && M.Ties.Num() > 0) { M.Ties[0].Kind = ECrewTiePlace::Station; M.Ties[0].Place = StationName; }
		Recruits.Add(M);
	}
	NextId = SavedNext;
	Dirty();
}

// ---- queries ----

int32 UCrewRosterSubsystem::TargetFor(ECrewDepartment D) const
{
	static const int32 Sizes[(int32)ECrewDepartment::Count] = { 6, 24, 60, 150, 70, 30, 36, 60, 66 };
	return Targets.IsValidIndex((int32)D) && Targets[(int32)D] > 0 ? Targets[(int32)D] : Sizes[(int32)D];
}

const FCrewMember* UCrewRosterSubsystem::Find(int32 Id) const
{
	for (const FCrewMember& M : Roster) { if (M.Id == Id) { return &M; } }
	return nullptr;
}
FCrewMember* UCrewRosterSubsystem::FindMutable(int32 Id)
{
	for (FCrewMember& M : Roster) { if (M.Id == Id) { return &M; } }
	return nullptr;
}
const FCrewMember* UCrewRosterSubsystem::Head(ECrewDepartment Dept) const
{
	for (const FCrewMember& M : Roster) { if (M.bHead && M.Department == Dept && M.Condition != ECrewCondition::Dead && M.Condition != ECrewCondition::Transferred) { return &M; } }
	return nullptr;
}
const FCrewMember* UCrewRosterSubsystem::Second(ECrewDepartment Dept) const
{
	for (const FCrewMember& M : Roster) { if (M.bSecond && M.Department == Dept && M.Condition != ECrewCondition::Dead && M.Condition != ECrewCondition::Transferred) { return &M; } }
	return nullptr;
}
const FCrewMember* UCrewRosterSubsystem::FindByName(const FString& Name) const
{
	const FString N = Name.ToLower();
	if (N.IsEmpty()) { return nullptr; }
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		// Those aboard first; the ashore, away or dead only when nobody aboard answers to it.
		auto Ok = [&](const FCrewMember& M) { return Pass == 1 || M.IsAboard(); };
		for (const FCrewMember& M : Roster) { if (Ok(M) && M.FullName().ToLower() == N) { return &M; } }
		for (const FCrewMember& M : Roster) { if (Ok(M) && (M.LastName.ToLower() == N || M.FirstName.ToLower() == N)) { return &M; } }
		for (const FCrewMember& M : Roster) { if (Ok(M) && M.FullName().ToLower().Contains(N)) { return &M; } }
	}
	return nullptr;
}
int32 UCrewRosterSubsystem::CountAboard() const
{
	int32 N = 0; for (const FCrewMember& M : Roster) { if (M.IsAboard()) { ++N; } } return N;
}
int32 UCrewRosterSubsystem::CountCondition(ECrewCondition C) const
{
	int32 N = 0; for (const FCrewMember& M : Roster) { if (M.Condition == C) { ++N; } } return N;
}
float UCrewRosterSubsystem::Morale() const
{
	float Sum = 0.f; int32 N = 0;
	for (const FCrewMember& M : Roster) { if (M.IsAboard()) { Sum += M.Mood; ++N; } }
	return N > 0 ? Sum / N : 0.5f;
}
TArray<int32> UCrewRosterSubsystem::InTrouble() const
{
	TArray<TPair<float, int32>> T;
	for (const FCrewMember& M : Roster)
	{
		if (!M.IsAboard()) { continue; }
		float Worst = 1.f; for (float N : M.Needs) { Worst = FMath::Min(Worst, N); }
		const float Score = FMath::Min(Worst, 1.f - M.Stress) - (M.Condition != ECrewCondition::Fit ? 0.3f : 0.f);
		if (Worst < 0.25f || M.Stress > 0.7f || M.Condition != ECrewCondition::Fit) { T.Add({ Score, M.Id }); }
	}
	T.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	TArray<int32> Out; for (const TPair<float, int32>& P : T) { Out.Add(P.Value); } return Out;
}
TArray<int32> UCrewRosterSubsystem::PresentIn(const FString& Compartment, bool bFitOnly) const
{
	TArray<int32> Out;
	for (const FCrewMember& M : Roster) { if (M.IsAboard() && M.Where == Compartment && (!bFitOnly || M.IsFit())) { Out.Add(M.Id); } }
	return Out;
}
void UCrewRosterSubsystem::Candidates(const FString& Compartment, TArray<int32>& OutLocal, TArray<int32>& OutElsewhere) const
{
	// Those in the compartment hit (within the last ship's hour) are taken first; the rest of the fit crew only
	// once they are spent.
	OutLocal.Reset();
	if (!Compartment.IsEmpty() && Hours - LastHitHours < 1.0)
	{
		// The compartment of the latest hit first, then the others struck within the hour.
		OutLocal = PresentIn(Compartment, true);
		for (int32 i = RecentHitCompartments.Num() - 1; i >= 0; --i)
		{
			if (RecentHitCompartments[i].Key == Compartment || Hours - RecentHitCompartments[i].Value >= 1.0) { continue; }
			for (int32 Id : PresentIn(RecentHitCompartments[i].Key, true)) { OutLocal.AddUnique(Id); }
		}
	}
	TSet<int32> Local(OutLocal);
	OutElsewhere.Reset();
	for (const FCrewMember& M : Roster) { if (M.IsFit() && M.IsAboard() && !Local.Contains(M.Id)) { OutElsewhere.Add(M.Id); } }
}

int32 UCrewRosterSubsystem::CountMembers() const
{
	int32 N = 0; for (const FCrewMember& M : Roster) { if (M.Condition != ECrewCondition::Transferred && M.Condition != ECrewCondition::OnLeave && M.Condition != ECrewCondition::Away) { ++N; } } return N;
}

// ---- the record ----

void UCrewRosterSubsystem::Record(FCrewMember& M, ECrewEventKind Kind, const FString& Text, bool bMilestone, const TArray<int32>& Others)
{
	FCrewEvent E; E.Stardate = GetStardate(); E.Kind = Kind; E.Text = Text; E.bMilestone = bMilestone; E.Others = Others;
	M.History.Add(E);
	if (M.History.Num() > 80)
	{
		for (int32 i = 1; i < M.History.Num(); ++i) { if (!M.History[i].bMilestone) { M.History.RemoveAt(i); break; } }
		if (M.History.Num() > 120) { M.History.RemoveAt(1); }   // never the first line, the joining
	}
	Dirty();
}

void UCrewRosterSubsystem::Remember(FCrewMember& M, const FString& Text, float Weight, bool bPermanent)
{
	FCrewMemory Mem; Mem.Text = Text; Mem.Weight = Weight; Mem.Stardate = GetStardate(); Mem.bPermanent = bPermanent;
	M.Memories.Add(Mem);
	// Two dozen at most, ten of them for good: the lightest of the rest goes first, then the lightest permanent.
	int32 Permanent = 0; for (const FCrewMemory& Me : M.Memories) { if (Me.bPermanent) { ++Permanent; } }
	auto DropLightest = [&](bool bPerm)
	{
		int32 Lightest = -1;
		for (int32 i = 0; i < M.Memories.Num(); ++i) { if (M.Memories[i].bPermanent == bPerm && (Lightest < 0 || FMath::Abs(M.Memories[i].Weight) < FMath::Abs(M.Memories[Lightest].Weight))) { Lightest = i; } }
		if (Lightest >= 0) { M.Memories.RemoveAt(Lightest); return true; }
		return false;
	};
	if (Permanent > 10) { DropLightest(true); }
	if (M.Memories.Num() > 24 && !DropLightest(false)) { DropLightest(true); }
}

void UCrewRosterSubsystem::Bond(FCrewMember& A, FCrewMember& B, float Delta, ECrewRelation KindIfNew)
{
	auto Apply = [&](FCrewMember& From, int32 To)
	{
		for (FCrewRelationship& R : From.Relationships)
		{
			if (R.Other == To)
			{
				R.Affinity = FMath::Clamp(R.Affinity + Delta, -1.f, 1.f);
				if (R.Kind == ECrewRelation::Acquaintance || R.Kind == ECrewRelation::Friend || R.Kind == ECrewRelation::CloseFriend || R.Kind == ECrewRelation::Rival)
				{
					R.Kind = R.Affinity > 0.7f ? ECrewRelation::CloseFriend : R.Affinity > 0.2f ? ECrewRelation::Friend : R.Affinity < -0.2f ? ECrewRelation::Rival : ECrewRelation::Acquaintance;
				}
				return;
			}
		}
		FCrewRelationship R; R.Other = To; R.Affinity = FMath::Clamp(Delta, -1.f, 1.f); R.Kind = KindIfNew;
		From.Relationships.Add(R);
		if (From.Relationships.Num() > 16)
		{
			int32 Lightest = 0;
			for (int32 i = 1; i < From.Relationships.Num() - 1; ++i) { if (FMath::Abs(From.Relationships[i].Affinity) < FMath::Abs(From.Relationships[Lightest].Affinity)) { Lightest = i; } }
			From.Relationships.RemoveAt(Lightest);
		}
	};
	Apply(A, B.Id);
	Apply(B, A.Id);
}

void UCrewRosterSubsystem::SetRelation(FCrewMember& From, int32 To, ECrewRelation Kind, float Affinity)
{
	// One direction only, the kind as given (a mentor's protege, a protege's mentor).
	for (FCrewRelationship& R : From.Relationships) { if (R.Other == To) { R.Kind = Kind; R.Affinity = FMath::Clamp(FMath::Max(R.Affinity, Affinity), -1.f, 1.f); return; } }
	FCrewRelationship R; R.Other = To; R.Affinity = FMath::Clamp(Affinity, -1.f, 1.f); R.Kind = Kind;
	From.Relationships.Add(R);
}

void UCrewRosterSubsystem::Grow(FCrewMember& M, ECrewSkill S, float XP)
{
	const int32 i = (int32)S;
	if (!M.Skills.IsValidIndex(i)) { return; }
	M.SkillXP[i] += XP;
	// Harder with every point: a crewman's first year is quick, a chief's last ten points a career.
	while (M.Skills[i] < 100.f)
	{
		const float Cost = 8.f + M.Skills[i] * 0.9f;
		if (M.SkillXP[i] < Cost) { break; }
		M.SkillXP[i] -= Cost; M.Skills[i] = FMath::Min(M.Skills[i] + 1.f, 100.f);
	}
}

void UCrewRosterSubsystem::UpdateMood(FCrewMember& M)
{
	// The needs weighted by what the person is: rest and purpose for the disciplined, belonging for the sociable,
	// safety for the timid, recognition for the ambitious.
	const float W[] = { 1.f, 1.2f, 0.6f + (100.f - M.Facet(ECrewFacet::Courage)) / 100.f, 0.5f + M.Facet(ECrewFacet::Sociability) / 100.f, 0.6f + M.Facet(ECrewFacet::Discipline) / 150.f, 0.8f, 0.4f + M.Facet(ECrewFacet::Ambition) / 100.f };
	float Sum = 0.f, Tot = 0.f;
	for (int32 i = 0; i < (int32)ECrewNeed::Count && i < M.Needs.Num(); ++i) { Sum += M.Needs[i] * W[i]; Tot += W[i]; }
	float Mood = 0.12f + 0.7f * (Tot > 0.f ? Sum / Tot : 0.5f);
	float Mem = 0.f; for (const FCrewMemory& Me : M.Memories) { Mem += Me.Weight; }
	Mood += FMath::Clamp(Mem, -0.45f, 0.35f);
	int32 Friends = 0; for (const FCrewRelationship& R : M.Relationships) { if (R.Affinity > 0.2f) { ++Friends; } }
	Mood += FMath::Min(Friends, 4) * 0.02f;
	Mood -= M.Stress * 0.25f;
	M.Mood = FMath::Clamp(Mood, 0.f, 1.f);
}

void UCrewRosterSubsystem::Place(FCrewMember& M)
{
	switch (M.Condition)
	{
	case ECrewCondition::Critical: M.Where = TEXT("Sickbay"); M.Activity = ECrewActivity::Sickbay; return;
	case ECrewCondition::Injured: M.Where = TEXT("Sickbay"); M.Activity = ECrewActivity::Sickbay; return;
	case ECrewCondition::OnLeave: M.Where = TEXT("Ashore"); M.Activity = ECrewActivity::Leave; return;
	case ECrewCondition::Away: M.Where = TEXT("Away team"); M.Activity = ECrewActivity::Away; return;
	case ECrewCondition::Dead: case ECrewCondition::Transferred: M.Where = TEXT("-"); M.Activity = ECrewActivity::Away; return;
	default: break;
	}
	const int32 Hour = GetHourOfDay();
	const int32 Start = (int32)M.Watch * 8;
	const int32 Since = (Hour - Start + 24) % 24;   // hours since the watch began
	const bool bOfficer = IsOfficer(M.Rank);
	const FString Quarters = bOfficer ? TEXT("Officers' quarters") : TEXT("Crew quarters");
	if (Since < 8) { M.Where = M.Posting; M.Activity = ECrewActivity::OnWatch; }
	else if (Since < 9) { M.Where = Quarters; M.Activity = ECrewActivity::Mess; }
	else if (Since < 16) { M.Where = Quarters; M.Activity = ECrewActivity::Sleeping; }
	else if (Since < 17) { M.Where = Quarters; M.Activity = ECrewActivity::Mess; }
	else { M.Where = M.HasTrait(TEXT("Bookish")) || M.HasTrait(TEXT("Loner")) ? Quarters : TEXT("Crew quarters"); M.Activity = ECrewActivity::Recreation; }
}

// ---- the ship's day ----

void UCrewRosterSubsystem::AdvanceSeconds(float Seconds, const FCrewShipState& Ship)
{
	ReportCooldownSeconds = FMath::Max(ReportCooldownSeconds - Seconds, 0.f);
	bAlongside = Ship.bAlongside; AlongsideStation = Ship.StationName;
	if (bSaveWanted) { SaveDelaySeconds -= Seconds; if (SaveDelaySeconds <= 0.f) { Save(); } }
	HourAccumulator += Seconds / FMath::Max(SecondsPerShipHour, 1.f);
	int32 Guard = 0;
	while (HourAccumulator >= 1.0 && Guard++ < 48)
	{
		HourAccumulator -= 1.0;
		Hours += 1.0;
		LiveHour(Ship);
		if (GetHourOfDay() == 0) { LiveDay(Ship); }
	}
}

void UCrewRosterSubsystem::DevAdvanceHours(int32 HoursToLive, const FCrewShipState& Ship)
{
	for (int32 h = 0; h < HoursToLive; ++h)
	{
		Hours += 1.0;
		LiveHour(Ship);
		if (GetHourOfDay() == 0) { LiveDay(Ship); }
	}
	Save();
}

void UCrewRosterSubsystem::LearnFromPeers()
{
	// People learn best from those a little ahead of them: on each watch and posting, everyone finds the colleague
	// whose skill is three to twenty-five points above their own (the nearer the better, a friend or mentor better
	// still) and takes experience from working beside them; the one teaching settles their own knowledge a little
	// and finds purpose in it. Far above is a lecture that goes over heads; below is nothing to learn.
	TMap<FString, TArray<int32>> Groups;
	for (int32 i = 0; i < Roster.Num(); ++i)
	{
		const FCrewMember& M = Roster[i];
		if (M.Activity == ECrewActivity::OnWatch && M.IsFit()) { Groups.FindOrAdd(M.Posting + TEXT("/") + FString::FromInt((int32)M.Watch)).Add(i); }
	}
	for (const TPair<FString, TArray<int32>>& G : Groups)
	{
		if (G.Value.Num() < 2) { continue; }
		for (int32 Li : G.Value)
		{
			FCrewMember& L = Roster[Li];
			const ECrewSkill S = SkillOf(L.Department);
			const float Own = L.Skill(S);
			int32 BestT = -1; float BestGain = 0.f;
			for (int32 Ti : G.Value)
			{
				if (Ti == Li) { continue; }
				const FCrewMember& T = Roster[Ti];
				if (T.Department != L.Department) { continue; }
				const float Ahead = T.Skill(S) - Own;
				if (Ahead < 3.f || Ahead > 25.f) { continue; }
				float Gain = 0.9f * (1.f - FMath::Abs(Ahead - 10.f) / 15.f);   // best about ten points ahead
				if (const FCrewRelationship* R = L.RelationWith(T.Id)) { Gain *= R->Kind == ECrewRelation::Protege ? 1.6f : R->Affinity > 0.2f ? 1.3f : R->Affinity < -0.2f ? 0.6f : 1.f; }
				if (T.HasTrait(TEXT("Perfectionist"))) { Gain *= 1.15f; }
				if (Gain > BestGain) { BestGain = Gain; BestT = Ti; }
			}
			if (BestT < 0) { continue; }
			Grow(L, S, BestGain * (0.6f + L.Facet(ECrewFacet::Curiosity) / 250.f));
			FCrewMember& T = Roster[BestT];
			Grow(T, S, 0.15f);
			T.Needs[(int32)ECrewNeed::Purpose] = FMath::Min(T.Needs[(int32)ECrewNeed::Purpose] + 0.01f, 1.f);
			PeerHours.FindOrAdd(T.Id) += 1;
			if (PeerHours[T.Id] % 40 == 0)
			{
				// Forty hours of showing someone the ropes: a bond, a line in both logs, and the desire to teach met.
				Bond(L, T, 0.08f, ECrewRelation::Friend);
				Record(L, ECrewEventKind::Moment, FString::Printf(TEXT("Learning the %s from %s on the %s watch."), *SkillName(S).ToLower(), *T.Styled(), *WatchName(L.Watch).ToLower()), false, { T.Id });
				Record(T, ECrewEventKind::Moment, FString::Printf(TEXT("Bringing %s on in the %s."), *L.Styled(), *SkillName(S).ToLower()), false, { L.Id });
				for (FCrewWant& W : T.Wants) { if (W.Tag == TEXT("Teach") && !W.bGranted) { W.bGranted = true; Remember(T, FString::Printf(TEXT("%s coming on well"), *L.FirstName), 0.15f, true); } }
			}
		}
	}
}

void UCrewRosterSubsystem::LiveHour(const FCrewShipState& Ship)
{
	const ECrewWatch Duty = WatchOnDuty();
	const bool bRed = Ship.Alert == EAlertState::Red, bYellow = Ship.Alert == EAlertState::Yellow;
	for (FCrewMember& M : Roster) { if (M.Condition != ECrewCondition::Dead && M.Condition != ECrewCondition::Transferred) { Place(M); } }
	LearnFromPeers();
	for (FCrewMember& M : Roster)
	{
		if (M.Condition == ECrewCondition::Dead || M.Condition == ECrewCondition::Transferred) { continue; }
		auto N = [&](ECrewNeed Need) -> float& { return M.Needs[(int32)Need]; };
		const float Disc = M.Facet(ECrewFacet::Discipline) / 100.f, Soc = M.Facet(ECrewFacet::Sociability) / 100.f, Cour = M.Facet(ECrewFacet::Courage) / 100.f, Res = M.Facet(ECrewFacet::Resolve) / 100.f;
		switch (M.Activity)
		{
		case ECrewActivity::OnWatch:
			N(ECrewNeed::Purpose) += 0.03f * (0.5f + Disc); N(ECrewNeed::Rest) -= 0.04f; N(ECrewNeed::Recreation) -= 0.02f; N(ECrewNeed::Belonging) += 0.005f;
			Grow(M, SkillOf(M.Department), 1.f * (0.6f + M.Facet(ECrewFacet::Curiosity) / 200.f));
			if (M.HasTrait(TEXT("Veteran")) && !bRed && !bYellow) { N(ECrewNeed::Purpose) -= 0.02f; }
			break;
		case ECrewActivity::Sleeping: N(ECrewNeed::Rest) += 0.055f; N(ECrewNeed::Belonging) -= 0.006f; N(ECrewNeed::Purpose) -= 0.008f; break;
		case ECrewActivity::Mess: N(ECrewNeed::Belonging) += 0.03f * (0.5f + Soc); N(ECrewNeed::Rest) += 0.01f; N(ECrewNeed::Recreation) += 0.02f; N(ECrewNeed::Purpose) -= 0.01f; break;
		case ECrewActivity::Recreation: N(ECrewNeed::Recreation) += 0.03f; N(ECrewNeed::Belonging) += 0.008f * (M.HasTrait(TEXT("Loner")) ? 0.f : Soc + 0.5f); N(ECrewNeed::Purpose) -= 0.012f; if (M.HasTrait(TEXT("Loner"))) { N(ECrewNeed::Rest) += 0.02f; } break;
		case ECrewActivity::Sickbay: N(ECrewNeed::Rest) += 0.05f; N(ECrewNeed::Purpose) -= 0.02f; N(ECrewNeed::Safety) -= 0.01f; break;
		case ECrewActivity::Leave: for (float& V : M.Needs) { V += 0.08f; } break;
		default: break;
		}
		if (!M.IsAboard())
		{
			// Ashore or away they live their own hour: no alert, no scare, no quarrel on a watch they are not on.
			for (float& V : M.Needs) { V = FMath::Clamp(V, 0.f, 1.f); }
			M.Stress = FMath::Max(M.Stress - 0.02f, 0.f);
			if (M.Condition == ECrewCondition::OnLeave)
			{
				M.ConditionHours = FMath::Max(M.ConditionHours - 1.f, 0.f);
				if (M.ConditionHours <= 0.f && Ship.bAlongside && (M.LeaveStation.IsEmpty() || Ship.StationName == M.LeaveStation)) { M.Condition = ECrewCondition::Fit; M.LeaveStation.Empty(); Record(M, ECrewEventKind::Leave, TEXT("Back aboard from leave.")); Place(M); }
			}
			UpdateMood(M);
			continue;
		}
		// Recognition settles toward middling on its own (a watch well kept is noticed a little); only the captain's
		// word, a commendation, a promotion or leave lift it above that.
		N(ECrewNeed::Recognition) += (0.5f - N(ECrewNeed::Recognition)) * 0.01f + (M.Activity == ECrewActivity::OnWatch ? 0.002f : 0.f);
		if (bRed) { N(ECrewNeed::Safety) -= 0.05f * (1.4f - Cour); N(ECrewNeed::Rest) -= 0.015f; }
		else if (bYellow) { N(ECrewNeed::Safety) -= 0.012f; }
		else { N(ECrewNeed::Safety) += 0.012f; }
		if (Ship.bUnderFire) { N(ECrewNeed::Safety) -= 0.08f * (1.4f - Cour); }
		const float HealthTarget = M.Condition == ECrewCondition::Fit ? 1.f : (M.Condition == ECrewCondition::Injured ? 0.45f : 0.15f);
		N(ECrewNeed::Health) = FMath::FInterpConstantTo(N(ECrewNeed::Health), HealthTarget, 1.f, 0.12f);
		for (float& V : M.Needs) { V = FMath::Clamp(V, 0.f, 1.f); }
		// Stress.
		if (bRed) { M.Stress += (0.04f + 0.01f * FMath::Min((float)(Hours - AlertSinceHours) / 24.f, 2.f)) * (1.3f - Cour); }
		if (Ship.bUnderFire) { M.Stress += 0.08f * (1.3f - Cour) * (M.HasTrait(TEXT("Veteran")) ? 0.5f : 1.f) * (1.f - FMath::Clamp(M.Trust, 0.f, 1.f) * 0.3f); }
		if (!bRed && !Ship.bUnderFire) { M.Stress -= 0.03f * (0.5f + Res); }
		if (M.Condition == ECrewCondition::Critical) { M.Stress += 0.02f; }
		M.Stress = FMath::Clamp(M.Stress, 0.f, 1.f);
		// Memories fade; the wound and leave clocks run.
		for (int32 i = M.Memories.Num() - 1; i >= 0; --i)
		{
			FCrewMemory& Me = M.Memories[i];
			if (Me.bPermanent) { Me.Weight = FMath::FInterpConstantTo(Me.Weight, Me.Weight < 0.f ? -0.04f : 0.04f, 1.f, 0.002f * (MourningDays > 0 ? 3.f : 1.f)); continue; }
			Me.Weight *= MourningDays > 0 && Me.Weight < 0.f ? 0.97f : 0.985f;
			if (FMath::Abs(Me.Weight) < 0.01f) { M.Memories.RemoveAt(i); }
		}
		if (M.Condition == ECrewCondition::Injured || M.Condition == ECrewCondition::Critical) { M.ConditionHours = FMath::Max(M.ConditionHours - 1.f, 0.f); }
		// A watch shared at the same posting makes acquaintances, slowly, and warms a friendship a little.
		if (M.Activity == ECrewActivity::OnWatch && Rng.FRand() < 0.04f)
		{
			for (int32 Try = 0; Try < 6; ++Try)
			{
				FCrewMember& O = Roster[Rng.RandRange(0, Roster.Num() - 1)];
				if (O.Id != M.Id && O.Activity == ECrewActivity::OnWatch && O.Posting == M.Posting && O.Watch == M.Watch && O.IsFit())
				{
					const FCrewRelationship* R = M.RelationWith(O.Id);
					if (!R || R->Affinity > -0.2f) { Bond(M, O, M.HasTrait(TEXT("Loner")) || O.HasTrait(TEXT("Loner")) ? 0.01f : 0.03f * (0.5f + M.Facet(ECrewFacet::Sociability) / 100.f), ECrewRelation::Acquaintance); }
					break;
				}
			}
		}
		// Someone they know, within hailing distance: a signal, or an evening ashore when alongside.
		if (Ship.Nearby.Num() > 0 && M.IsAboard())
		{
			for (FCrewTie& T : M.Ties)
			{
				if (T.Place.IsEmpty() || GetDay() - T.LastContactDay < 5) { continue; }
				bool bNear = false; for (const FString& Near : Ship.Nearby) { if (Near == T.Place || (T.Kind == ECrewTiePlace::Ship && Near.Contains(T.Place))) { bNear = true; break; } }
				if (bNear && Rng.FRand() < 0.5f) { Reunion(M, T, Ship.bAlongside && T.Kind == ECrewTiePlace::Station && Ship.StationName == T.Place); break; }
			}
		}
		UpdateMood(M);
		PersonalMoment(M, Ship);
	}
	Dirty();
}

void UCrewRosterSubsystem::LiveDay(const FCrewShipState& Ship)
{
	const int32 Day = GetDay();
	if (MourningDays > 0) { --MourningDays; }
	int32 Low = 0, Pressed = 0;
	for (FCrewMember& M : Roster)
	{
		if (!M.IsAboard()) { continue; }
		if (M.Mood < 0.35f) { ++Low; }
		for (FCrewWant& W : M.Wants)
		{
			if (W.bFear || W.bGranted) { continue; }
			const int32 Waited = Day - W.SinceDay;
			if (Waited > 20 && Waited % 10 == 0) { Remember(M, FString::Printf(TEXT("Still waiting for %s"), *W.Text), -0.06f); ++Pressed; }
		}
	}
	// Promises: a want promised and then had is a promise kept; one promised and still waiting after 15 days, broken.
	// Ashore or away counts: a leave promised and granted is kept while they are on it.
	for (FCrewMember& M : Roster)
	{
		if (M.Condition == ECrewCondition::Dead || M.Condition == ECrewCondition::Transferred) { continue; }
		for (FCrewWant& W : M.Wants)
		{
			if (!W.bPromised) { continue; }
			if (W.bGranted) { W.bPromised = false; FCrewExperience E; E.Tag = TEXT("Promise.Kept"); E.Intensity = 0.6f; E.Involved = { M.Id }; E.Text = FString::Printf(TEXT("The captain kept a promise: %s"), *W.Text); E.bCaptainsChoice = true; Experience(E); }
			else if (Day - W.PromisedDay > 15) { W.bPromised = false; FCrewExperience E; E.Tag = TEXT("Promise.Broken"); E.Intensity = 0.6f; E.Involved = { M.Id }; E.Text = FString::Printf(TEXT("The captain's promise of %s came to nothing"), *W.Text); E.bCaptainsChoice = true; Experience(E); }
		}
	}
	// Those who have lost faith ask to go; the rifts between peoples are counted.
	int32 Transfers = 0, Rifts = 0;
	for (FCrewMember& M : Roster)
	{
		if (!M.IsAboard()) { continue; }
		for (const FCrewBias& B : M.Biases) { if (B.Disposition < -0.4f) { ++Rifts; break; } }
		if (M.Trust < -0.4f && M.Mood < 0.5f && Rng.FRand() < 0.15f)
		{
			bool bAsked = false; for (const FCrewWant& W : M.Wants) { if (W.Tag == TEXT("Transfer")) { bAsked = true; } }
			if (!bAsked) { FCrewWant W; W.Tag = TEXT("Transfer"); W.Text = TEXT("a transfer off the ship"); W.SinceDay = Day; M.Wants.Add(W); Record(M, ECrewEventKind::Transfer, TEXT("Put in for a transfer off the ship."), true); ++Transfers; }
		}
	}
	IncidentsToday = 0;
	const FCrewMember* XO = Head(ECrewDepartment::Command);
	if (XO)
	{
		const float Mor = Morale(), Tr = MeanTrust();
		FString Line = FString::Printf(TEXT("Morning report, stardate %s: morale %s (%.0f%%), %d aboard"), *StardateText(), Mor > 0.7f ? TEXT("high") : Mor > 0.5f ? TEXT("fair") : Mor > 0.35f ? TEXT("low") : TEXT("poor"), Mor * 100.f, CountAboard());
		if (Low > 0) { Line += FString::Printf(TEXT(", %d in low spirits"), Low); }
		if (Pressed > 0) { Line += FString::Printf(TEXT(", %d requests outstanding"), Pressed); }
		Line += FString::Printf(TEXT("; the ship %s you"), Tr > 0.5f ? TEXT("would follow") : Tr > 0.2f ? TEXT("trusts") : Tr > -0.1f ? TEXT("is reserving judgement on") : Tr > -0.4f ? TEXT("doubts") : TEXT("has lost faith in"));
		if (Transfers > 0) { Line += FString::Printf(TEXT(", %d put in for transfer"), Transfers); }
		if (FMath::Abs(Rifts - RiftsReported) >= 5) { RiftsReported = Rifts; Line += FString::Printf(TEXT(", %d carry a grudge against a people or a power"), Rifts); }
		Line += TEXT(".");
		Report(ECrewDepartment::Command, Line, Mor < 0.35f ? FLinearColor(1.f, 0.6f, 0.4f) : FLinearColor(0.75f, 0.9f, 1.f), true);
	}
	RequestsToday = 0;
	Save();
}

void UCrewRosterSubsystem::PersonalMoment(FCrewMember& M, const FCrewShipState& Ship)
{
	// A small chance an hour of something personal; the kind by traits, needs and company.
	if (Rng.FRand() > 0.014f) { return; }
	const bool bOnWatch = M.Activity == ECrewActivity::OnWatch;
	const bool bRed = Ship.Alert == EAlertState::Red;
	struct FKind { int32 K; float W; };
	TArray<FKind> Kinds;
	Kinds.Add({ 0, 1.0f * (0.5f + M.Facet(ECrewFacet::Sociability) / 100.f) });            // friendship
	Kinds.Add({ 1, 0.5f + (M.Stress > 0.5f ? 0.8f : 0.f) + (M.HasTrait(TEXT("WoundedPride")) ? 0.4f : 0.f) });   // quarrel
	Kinds.Add({ 2, (M.HasTrait(TEXT("Homesick")) ? 3.f : 0.6f) });                           // letter
	if (bOnWatch && M.Department == ECrewDepartment::Science) { Kinds.Add({ 3, 0.6f + M.Facet(ECrewFacet::Curiosity) / 100.f }); }   // breakthrough
	if (bOnWatch && M.Department == ECrewDepartment::Engineering) { Kinds.Add({ 4, 0.6f + M.Facet(ECrewFacet::Discipline) / 100.f }); } // fine work
	if (bRed) { Kinds.Add({ 5, 1.2f * (1.5f - M.Facet(ECrewFacet::Courage) / 100.f) }); }    // a scare
	if (M.Facet(ECrewFacet::Ambition) > 55.f && RequestsToday < 2) { Kinds.Add({ 6, 0.5f }); }   // a request to see the captain, two a day at most
	Kinds.Add({ 7, 0.4f });                                                                   // off colour
	if (!M.bHead) { Kinds.Add({ 8, 0.5f + (M.Facet(ECrewFacet::Curiosity) / 200.f) }); }     // mentoring
	if (M.HasTrait(TEXT("ChessPlayer")) || M.HasTrait(TEXT("Musician")) || M.HasTrait(TEXT("Gambler"))) { Kinds.Add({ 9, 1.2f }); }   // the hobby
	if (M.Stress > 0.8f && M.Mood < 0.35f) { Kinds.Add({ 10, 3.f }); }                       // breakdown
	if (M.Birthday == GetDay() % 365 && M.Activity == ECrewActivity::Mess) { Kinds.Add({ 11, 6.f }); }   // birthday
	float Tot = 0.f; for (const FKind& K : Kinds) { Tot += K.W; }
	float X = Rng.FRandRange(0.f, Tot); int32 Kind = 0;
	for (const FKind& K : Kinds) { X -= K.W; if (X <= 0.f) { Kind = K.K; break; } }
	auto Colleague = [&](bool bSameDept) -> FCrewMember*
	{
		for (int32 Try = 0; Try < 12; ++Try)
		{
			FCrewMember& O = Roster[Rng.RandRange(0, Roster.Num() - 1)];
			if (O.Id != M.Id && O.IsAboard() && (!bSameDept || (O.Department == M.Department && O.Watch == M.Watch))) { return &O; }
		}
		return nullptr;
	};
	switch (Kind)
	{
	case 0: if (FCrewMember* O = Colleague(Rng.FRand() < 0.75f))
		{
			// A friendship across a grudge, on either side, comes slower, and softens it when it comes.
			const float B = M.Bias(FName(*O->Species)), B2 = O->Bias(FName(*M.Species));
			if ((B < -0.2f && Rng.FRand() < -B) || (B2 < -0.2f && Rng.FRand() < -B2)) { break; }
			if (B < 0.f) { MoveBias(M, FName(*O->Species), 0.05f, O->FullName()); }
			if (B2 < 0.f) { MoveBias(*O, FName(*M.Species), 0.05f, M.FullName()); }
			const FString Hobby = Pick(Rng, Hobbies);
			Bond(M, *O, 0.25f, ECrewRelation::Friend);
			Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("Struck up a friendship with %s over %s."), *O->Styled(), *Hobby), false, { O->Id });
			Record(*O, ECrewEventKind::Moment, FString::Printf(TEXT("Struck up a friendship with %s over %s."), *M.Styled(), *Hobby), false, { M.Id });
			Remember(M, TEXT("A new friend"), 0.06f); Remember(*O, TEXT("A new friend"), 0.06f);
		} break;
	case 1: {
			FCrewMember* O = nullptr;
			for (const FCrewRelationship& R : M.Relationships) { if (R.Kind == ECrewRelation::Rival) { O = FindMutable(R.Other); break; } }
			if (!O) { O = Colleague(true); }
			if (O && O->IsAboard() && M.Bias(FName(*O->Species)) < -0.25f && Rng.FRand() < 0.5f) { Incident(M, *O, true); break; }
			if (O && O->IsAboard())
			{
				Bond(M, *O, -0.15f, ECrewRelation::Rival);
				Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("Words with %s on the %s watch."), *O->Styled(), *WatchName(M.Watch).ToLower()), false, { O->Id });
				Record(*O, ECrewEventKind::Moment, FString::Printf(TEXT("Words with %s."), *M.Styled()), false, { M.Id });
				Remember(M, TEXT("A quarrel"), -0.08f); Remember(*O, TEXT("A quarrel"), -0.06f);
				M.Stress = FMath::Min(M.Stress + 0.05f, 1.f);
			}
		} break;
	case 2: {
			const FCrewTie* T = M.Ties.Num() > 0 ? &M.Ties[Rng.RandRange(0, M.Ties.Num() - 1)] : nullptr;
			const bool bBad = T && Rng.FRand() < 0.18f;
			const FString News = T ? FString::Printf(TEXT("%s %s %s %s."), *T->Relation, *T->Name, *T->Where(), bBad ? Pick(Rng, NewsBad) : Pick(Rng, NewsGood)) : FString();
			Record(M, ECrewEventKind::Letter, T ? FString::Printf(TEXT("A letter from %s: %s"), *T->Name, *News) : FString::Printf(TEXT("A letter from home, %s."), *M.Home));
			if (bBad) { Remember(M, FString::Printf(TEXT("Worry for %s"), *T->Name), -0.12f * (0.5f + T->Affinity)); M.Stress = FMath::Min(M.Stress + 0.08f, 1.f); }
			else { Remember(M, T ? FString::Printf(TEXT("A letter from %s"), *T->Name) : TEXT("A letter from home"), (M.HasTrait(TEXT("Homesick")) ? 0.2f : 0.1f) * (T ? 0.6f + T->Affinity * 0.6f : 1.f)); }
			if (T) { const_cast<FCrewTie*>(T)->LastContactDay = GetDay(); }
			for (FCrewWant& W : M.Wants) { if (W.Tag == TEXT("Letter") && !W.bGranted) { W.bGranted = true; Remember(M, TEXT("Word from home at last"), 0.15f); } }
		} break;
	case 3: Record(M, ECrewEventKind::Discovery, TEXT("A small breakthrough at the station: a pattern in the sensor returns nobody had seen.")); Remember(M, TEXT("A breakthrough"), 0.15f); Grow(M, ECrewSkill::Science, 8.f);
		if (Rng.FRand() < 0.25f) { Report(ECrewDepartment::Science, FString::Printf(TEXT("%s has found something in the sensor returns worth a second look."), *M.Styled())); }
		break;
	case 4: Record(M, ECrewEventKind::Moment, TEXT("A fine piece of work on the watch: a fault found before it showed.")); Remember(M, TEXT("Good work noticed"), 0.1f); Grow(M, ECrewSkill::Engineering, 5.f);
		if (Rng.FRand() < 0.15f) { Report(ECrewDepartment::Engineering, FString::Printf(TEXT("%s caught a fault in the %s before it showed. Good hands."), *M.Styled(), *M.Posting.ToLower())); }
		break;
	case 5: M.Stress = FMath::Min(M.Stress + 0.15f, 1.f); Remember(M, TEXT("A scare at red alert"), -0.12f); Record(M, ECrewEventKind::Moment, TEXT("A bad moment at red alert; it passed."));
		for (FCrewWant& W : M.Wants) { if (W.bFear && M.Facet(ECrewFacet::Courage) < 40.f && Rng.FRand() < 0.3f) { Remember(M, FString::Printf(TEXT("Thought of %s"), *W.Text), -0.1f); } }
		break;
	case 6: {
			const FCrewWant* Want = nullptr; for (const FCrewWant& W : M.Wants) { if (!W.bFear && !W.bGranted) { Want = &W; break; } }
			if (Want && Report(ECrewDepartment::Command, FString::Printf(TEXT("%s asks for a word with the captain about %s."), *M.Styled(), *Want->Text))) { ++RequestsToday; Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("Asked to see the captain about %s."), *Want->Text)); }
		} break;
	case 7: M.Needs[(int32)ECrewNeed::Health] = FMath::Max(M.Needs[(int32)ECrewNeed::Health] - 0.25f, 0.f); Remember(M, TEXT("Off colour"), -0.05f); break;
	case 8: if (const FCrewMember* H = Head(M.Department))
		{
			if (FCrewMember* HM = FindMutable(H->Id)) { Grow(M, SkillOf(M.Department), 6.f); Bond(M, *HM, 0.08f, ECrewRelation::Acquaintance); Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("An hour with %s over the %s procedures."), *HM->Styled(), *DepartmentName(M.Department).ToLower()), false, { HM->Id }); }
		} break;
	case 9: if (FCrewMember* O = Colleague(false))
		{
			const TCHAR* What = M.HasTrait(TEXT("ChessPlayer")) ? TEXT("a game of chess") : M.HasTrait(TEXT("Musician")) ? TEXT("an evening of music in the mess") : TEXT("a hand of cards");
			Bond(M, *O, 0.1f, ECrewRelation::Friend); Remember(M, What, 0.07f); Remember(*O, What, 0.05f);
			Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("%s with %s."), What, *O->Styled()), false, { O->Id });
		} break;
	case 10: M.Stress = FMath::Max(M.Stress - 0.35f, 0.f);
		{
			const bool bMistake = bOnWatch;
			if (M.Trust < -0.3f && bOnWatch)
			{
				// It comes out as a refusal when they do not believe in the captain.
				Record(M, ECrewEventKind::Moment, TEXT("Refused an order on watch and said why; written up."), true);
				Remember(M, TEXT("Said no"), -0.05f);
				M.Trust = FMath::Max(M.Trust - 0.05f, -1.f);
				Report(M.Department, FString::Printf(TEXT("%s refused an order on watch, Captain. I've written it up; I think it's you %s has the quarrel with."), *M.Styled(), *M.Pronoun), FLinearColor(1.f, 0.6f, 0.5f), true);
				break;
			}
			Record(M, ECrewEventKind::Moment, bMistake ? TEXT("Lost the thread on watch: a mistake the station caught, and a dressing down.") : TEXT("An outburst in the mess; walked it off."), true);
			Remember(M, TEXT("Came apart for a moment"), -0.15f);
			Report(bMistake ? M.Department : ECrewDepartment::Command, FString::Printf(TEXT("%s %s; I'd keep an eye on %s."), *M.Styled(), bMistake ? TEXT("fumbled the station on watch") : TEXT("lost their temper off watch"), M.Pronoun == TEXT("they") ? TEXT("them") : (M.Pronoun == TEXT("she") ? TEXT("her") : TEXT("him"))), FLinearColor(1.f, 0.75f, 0.4f), true);
		} break;
	case 11: Record(M, ECrewEventKind::Moment, TEXT("A birthday in the mess: a cake from the galley and a song.")); Remember(M, TEXT("A birthday aboard"), 0.2f); M.Needs[(int32)ECrewNeed::Belonging] = FMath::Min(M.Needs[(int32)ECrewNeed::Belonging] + 0.25f, 1.f);
		if (Rng.FRand() < 0.3f) { Report(ECrewDepartment::Services, FString::Printf(TEXT("A birthday in the mess for %s; the galley did a cake."), *M.Styled())); }
		break;
	default: break;
	}
}

bool UCrewRosterSubsystem::DevIncident(int32 Offender, int32 Victim)
{
	FCrewMember* M = FindMutable(Offender); FCrewMember* O = FindMutable(Victim);
	if (!M || !O || M == O) { return false; }
	Incident(*M, *O, true);
	Dirty(); RequestSave();
	return true;
}

void UCrewRosterSubsystem::Incident(FCrewMember& M, FCrewMember& O, bool bBias)
{
	// Words across a grudge, in the mess or on the watch: the one it was said to loses a little faith in a ship
	// that lets it pass, the one who said it hardens unless the captain answers it. Two a day reach the log.
	Bond(M, O, -0.2f, ECrewRelation::Rival);
	const FString What = M.Activity == ECrewActivity::OnWatch ? TEXT("on the watch") : TEXT("in the mess");
	const FString Them = O.Pronoun == TEXT("they") ? TEXT("them") : (O.Pronoun == TEXT("she") ? TEXT("her") : TEXT("him"));
	Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("Words with %s %s about what %ss are like; it went too far."), *O.Styled(), *What, *O.Species), true, { O.Id });
	Record(O, ECrewEventKind::Moment, FString::Printf(TEXT("%s had words for %s %s, and for %ss generally."), *M.Styled(), *Them, *What, *O.Species), true, { M.Id });
	Remember(O, FString::Printf(TEXT("What %s said about %ss"), *M.FullName(), *O.Species), -0.12f);
	O.Trust = FMath::Max(O.Trust - 0.03f, -1.f);
	O.Stress = FMath::Min(O.Stress + 0.05f, 1.f);
	bool bHad = false; for (FCrewSuspicion& X : O.Suspicions) { if (X.Of == M.Id) { X.Weight = FMath::Min(X.Weight + 0.2f, 1.f); bHad = true; } }
	if (!bHad) { FCrewSuspicion S; S.Of = M.Id; S.Weight = 0.3f; S.Why = FString::Printf(TEXT("what %s said about %ss"), *M.Pronoun, *O.Species); S.SinceDay = GetDay(); O.Suspicions.Add(S); }
	bool bPending = false; for (FCrewIncident& P : Incidents) { if (P.Offender == M.Id && P.Victim == O.Id) { P.Day = GetDay(); bPending = true; } }
	if (!bPending) { FCrewIncident P; P.Offender = M.Id; P.Victim = O.Id; P.Day = GetDay(); Incidents.Add(P); }
	if (IncidentsToday++ < 2)
	{
		Report(ECrewDepartment::Command, FString::Printf(TEXT("An incident %s: %s had words for %s about %ss. %s let it pass; I'd rather you didn't."), *What, *M.Styled(), *O.Styled(), *O.Species, *DepartmentName(M.Department)), FLinearColor(1.f, 0.7f, 0.5f), true);
	}
}

bool UCrewRosterSubsystem::Report(ECrewDepartment From, const FString& Text, const FLinearColor& Color, bool bAlways)
{
	if (!bAlways && ReportCooldownSeconds > 0.f && Reports.Num() > 2) { return false; }
	FCrewReport R; R.From = From; R.Text = Text; R.Color = Color;
	Reports.Add(R);
	ReportCooldownSeconds = 12.f;
	return true;
}

// ---- the ship's events ----

void UCrewRosterSubsystem::MoveBias(FCrewMember& M, FName Group, float Delta, const FString& Reason)
{
	if (Group.IsNone() || FMath::IsNearlyZero(Delta)) { return; }
	for (FCrewBias& B : M.Biases)
	{
		if (B.Group == Group)
		{
			// Hardened dispositions move slowly; what moves one toward nothing is easier than what moves it away.
			const bool bSoftening = (B.Disposition > 0.f) != (Delta > 0.f);
			const float Was = B.Disposition;
			B.Disposition = FMath::Clamp(B.Disposition + Delta * (bSoftening ? 1.f : 0.6f) * (1.f - FMath::Abs(B.Disposition) * 0.5f), -1.f, 1.f);
			// Small steps accumulate; the reason follows a step worth telling, and a grudge crossed back to nothing is "got over".
			if (FMath::Abs(Was) >= 0.2f && FMath::Abs(B.Disposition) < 0.05f) { B.Reason = TEXT("got over it"); }
			else if (!Reason.IsEmpty() && FMath::Abs(Delta) > 0.08f) { B.Reason = Reason; }
			return;
		}
	}
	FCrewBias B; B.Group = Group; B.Disposition = FMath::Clamp(Delta, -1.f, 1.f); B.Reason = Reason.IsEmpty() ? TEXT("first impressions") : Reason; M.Biases.Add(B);
}

void UCrewRosterSubsystem::Experience(const FCrewExperience& E)
{
	// The event's plain valence for a neutral person, then what each person is makes of it.
	struct FKind { const TCHAR* Tag; float Valence; float StressK; };
	static const FKind Kinds[] = {
		{ TEXT("Combat.EnemyDestroyed"), 0.08f, 0.05f }, { TEXT("Combat.ShipSpared"), 0.04f, 0.f }, { TEXT("Combat.FriendlyLost"), -0.3f, 0.2f },
		{ TEXT("Contact.First"), 0.15f, 0.05f }, { TEXT("Contact.Friendly"), 0.08f, 0.f }, { TEXT("Contact.Hostile"), -0.08f, 0.15f },
		{ TEXT("Rescue"), 0.2f, 0.05f }, { TEXT("Loss.Civilians"), -0.25f, 0.15f }, { TEXT("Parley.Held"), 0.12f, 0.f }, { TEXT("Parley.Failed"), -0.1f, 0.05f },
		{ TEXT("Away.Danger"), -0.05f, 0.25f }, { TEXT("Away.Routine"), 0.06f, 0.f }, { TEXT("Order.Refused"), -0.12f, 0.1f },
		{ TEXT("Promise.Kept"), 0.15f, 0.f }, { TEXT("Promise.Broken"), -0.25f, 0.05f }, { TEXT("Captain.Fair"), 0.1f, 0.f }, { TEXT("Captain.Unfair"), -0.15f, 0.05f },
		{ TEXT("Discovery"), 0.1f, 0.f }, { TEXT("Shore"), 0.1f, 0.f },
	};
	float Valence = 0.05f, StressK = 0.05f;
	for (const FKind& K : Kinds) { if (E.Tag == FName(K.Tag)) { Valence = K.Valence; StressK = K.StressK; } }
	const FString TagS = E.Tag.ToString();
	const bool bKill = TagS == TEXT("Combat.EnemyDestroyed"), bSpared = TagS == TEXT("Combat.ShipSpared"), bFirst = TagS == TEXT("Contact.First"), bRescue = TagS == TEXT("Rescue");
	const bool bCivilians = TagS == TEXT("Loss.Civilians"), bDanger = TagS == TEXT("Away.Danger"), bHostile = TagS == TEXT("Contact.Hostile"), bFriendlyContact = TagS == TEXT("Contact.Friendly") || TagS == TEXT("Parley.Held");
	const bool bUnfair = TagS == TEXT("Captain.Unfair"), bFair = TagS == TEXT("Captain.Fair"), bFriendlyLost = TagS == TEXT("Combat.FriendlyLost");
	const float I = FMath::Clamp(E.Intensity, 0.05f, 1.f);
	int32 Reached = 0;
	for (FCrewMember& M : Roster)
	{
		const bool bThere = E.Involved.Contains(M.Id);
		if (M.Condition == ECrewCondition::Dead || M.Condition == ECrewCondition::Transferred) { continue; }
		if (!M.IsAboard() && !bThere) { continue; }   // the ashore and away hear nothing unless it was theirs
		if (E.Involved.Num() > 0 && !bThere && I < 0.3f) { continue; }   // a small thing stays with those who were there
		float Share = bThere ? 1.f : 0.35f;
		float V = Valence;
		const float Emp = M.Facet(ECrewFacet::Empathy) / 100.f, Cur = M.Facet(ECrewFacet::Curiosity) / 100.f, Cour = M.Facet(ECrewFacet::Courage) / 100.f, Disc = M.Facet(ECrewFacet::Discipline) / 100.f;
		const float Bias = M.Bias(E.Group);
		// Who they are reads the event.
		if (bKill) { V = M.HasTrait(TEXT("Pacifist")) ? -0.2f : 0.05f + (1.f - Emp) * 0.1f + (M.HasTrait(TEXT("Veteran")) ? 0.05f : 0.f); V -= FMath::Max(Bias, 0.f) * 0.3f; V += FMath::Max(-Bias, 0.f) * 0.15f; }
		if (bSpared) { V = (M.HasTrait(TEXT("Pacifist")) ? 0.2f : (Emp - 0.5f) * 0.3f) - FMath::Max(-Bias, 0.f) * 0.3f; }
		if (bFirst) { V *= 0.5f + Cur; }
		if (bRescue) { V *= 0.5f + Emp; }
		if (bCivilians) { V *= 0.5f + Emp; }
		if (bDanger) { V = bThere ? (M.HasTrait(TEXT("ThrillSeeker")) ? 0.18f : (Cour - 0.6f) * 0.3f) : 0.f; }
		if (bHostile) { V -= FMath::Max(-Bias, 0.f) * 0.15f; }
		if (bFriendlyContact) { V += FMath::Max(Bias, 0.f) * 0.1f; }
		if (bFriendlyLost) { V *= 0.7f + Emp * 0.6f; }
		if (bUnfair || bFair) { V *= (E.Group.IsNone() || M.Species == E.Group.ToString()) ? 1.5f : 1.f; if (bUnfair && M.Facet(ECrewFacet::Empathy) < 35.f && Bias < -0.2f) { V = 0.03f; } }
		// Their own people at the place live it twice; a fear of the group is a fear met.
		bool bTied = false; for (const FCrewTie& T : M.Ties) { if (!E.Place.IsEmpty() && T.Place == E.Place) { bTied = true; } }
		if (bTied) { Share = 1.f; V *= 2.f; }
		float Stress = StressK * I * Share * (1.4f - Cour) * (1.f - FMath::Clamp(M.Trust, 0.f, 1.f) * 0.3f);
		for (const FCrewWant& W : M.Wants) { if (W.bFear && !E.Group.IsNone() && W.Tag.ToString().Contains(E.Group.ToString())) { Stress += 0.12f * I; V -= 0.05f; } }
		const float Weight = V * I * Share;
		if (FMath::Abs(Weight) < 0.01f && Stress < 0.02f) { continue; }
		++Reached;
		M.Stress = FMath::Clamp(M.Stress + Stress, 0.f, 1.f);
		if (bHostile || bDanger || bFriendlyLost) { M.Needs[(int32)ECrewNeed::Safety] = FMath::Max(M.Needs[(int32)ECrewNeed::Safety] - 0.15f * I * Share, 0.f); }
		Remember(M, E.Text, Weight, FMath::Abs(Weight) > 0.25f || (bTied && FMath::Abs(Weight) > 0.12f));
		if (bThere || bTied || FMath::Abs(Weight) > 0.08f)
		{
			Record(M, ECrewEventKind::Mission, FString::Printf(TEXT("%s%s"), *E.Text, bThere ? TEXT("") : TEXT(" (heard of it)")), bThere && FMath::Abs(Weight) > 0.1f, E.Involved);
		}
		// The captain's choice, as they saw it.
		if (E.bCaptainsChoice) { M.Trust = FMath::Clamp(M.Trust + Weight * 0.6f * (0.7f + Disc * 0.6f), -1.f, 1.f); }
		// A disposition moved by the experience: a group met well softens, a group met badly hardens.
		if (!E.Group.IsNone())
		{
			if (bFriendlyContact || bFirst || bRescue) { MoveBias(M, E.Group, 0.06f * I * Share, FString::Printf(TEXT("met them well: %s"), *E.Text)); }
			if (bHostile || bFriendlyLost || bCivilians) { MoveBias(M, E.Group, -0.08f * I * Share, E.Text); }
			if (bKill && !M.HasTrait(TEXT("Pacifist"))) { MoveBias(M, E.Group, -0.02f * I * Share, FString()); }
		}
		// Now and then the event leaves a mark that stays.
		if (bThere && bDanger && Rng.FRand() < 0.15f * I) { M.Traits.AddUnique(TEXT("Veteran")); }
		if (bCivilians && Weight < -0.15f && Rng.FRand() < 0.3f) { bool bHas = false; for (const FCrewWant& W : M.Wants) { if (W.Tag == TEXT("LosingPeople")) { bHas = true; } } if (!bHas) { FCrewWant W; W.Tag = TEXT("LosingPeople"); W.Text = TEXT("failing the people in their charge"); W.bFear = true; W.SinceDay = GetDay(); M.Wants.Add(W); } }
		if (bRescue && bThere && M.HasTrait(TEXT("ThrillSeeker"))) { for (FCrewWant& W : M.Wants) { if (W.Tag == TEXT("AwayMission") && !W.bGranted) { W.bGranted = true; } } }
		if (bKill && M.HasTrait(TEXT("Pacifist")) && E.bCaptainsChoice && Rng.FRand() < 0.2f * I) { M.Traits.AddUnique(TEXT("Disillusioned")); }
		UpdateMood(M);
	}
	if (Reached > 0)
	{
		Dirty(); RequestSave();
		UE_LOG(LogTemp, Log, TEXT("Crew: experience %s (%.2f) reached %d: %s"), *TagS, I, Reached, *E.Text);
	}
}

float UCrewRosterSubsystem::MeanTrust() const
{
	float Sum = 0.f; int32 N = 0;
	for (const FCrewMember& M : Roster) { if (M.IsAboard()) { Sum += M.Trust; ++N; } }
	return N > 0 ? Sum / N : 0.f;
}

float UCrewRosterSubsystem::Cohesion() const
{
	// Trust in the captain, the bonds between people, and the biases that divide them.
	float Trust = 0.f, Bonds = 0.f, Division = 0.f; int32 N = 0;
	for (const FCrewMember& M : Roster)
	{
		if (!M.IsAboard()) { continue; }
		++N; Trust += (M.Trust + 1.f) * 0.5f;
		float Pos = 0.f; for (const FCrewRelationship& R : M.Relationships) { const FCrewMember* O = Find(R.Other); if (R.Affinity > 0.2f && O && O->IsAboard()) { Pos += 1.f; } }
		Bonds += FMath::Min(Pos, 4.f) / 4.f;
		float Worst = 0.f; for (const FCrewBias& B : M.Biases) { if (B.Disposition < 0.f) { Worst = FMath::Max(Worst, -B.Disposition); } }
		Division += Worst;
	}
	if (N == 0) { return 0.5f; }
	return FMath::Clamp(0.5f * Trust / N + 0.3f * Bonds / N + 0.2f * (1.f - Division / N), 0.f, 1.f);
}

void UCrewRosterSubsystem::NoteAlert(EAlertState NewAlert)
{
	if (NewAlert == Alert) { return; }
	Alert = NewAlert; AlertSinceHours = Hours;
}

void UCrewRosterSubsystem::NoteMedicalHazard()
{
	// Bad air is no hit: the next casualties are nobody's compartment, and its dead are the patients first.
	LastHitCompartment.Empty(); LastHitHours = -1000.0; RecentHitCompartments.Reset();
}

void UCrewRosterSubsystem::NoteHitCompartment(const FString& Compartment)
{
	LastHitCompartment = Compartment; LastHitHours = Hours;
	// Several hits can land before the next reconciliation; each keeps its compartment in the draw for the hour.
	for (int32 i = RecentHitCompartments.Num() - 1; i >= 0; --i) { if (Hours - RecentHitCompartments[i].Value >= 1.0 || RecentHitCompartments[i].Key == Compartment) { RecentHitCompartments.RemoveAt(i); } }
	if (!Compartment.IsEmpty()) { RecentHitCompartments.Add(TPair<FString, double>(Compartment, Hours)); }   // one entry each, the latest last
}

void UCrewRosterSubsystem::Wound(FCrewMember& M, bool bCritical, const FString& Where)
{
	M.Condition = bCritical ? ECrewCondition::Critical : ECrewCondition::Injured;
	M.ConditionHours = bCritical ? 72.f : 24.f;
	++M.Service.Wounds;
	Record(M, ECrewEventKind::Wounded, FString::Printf(TEXT("%s in %s."), bCritical ? TEXT("Critically wounded") : TEXT("Wounded"), *Where), true);
	Remember(M, bCritical ? TEXT("Nearly died") : TEXT("Wounded"), bCritical ? -0.25f : -0.12f);
	M.Stress = FMath::Min(M.Stress + (bCritical ? 0.3f : 0.15f), 1.f);
	Place(M);
}

bool UCrewRosterSubsystem::WoundMember(int32 Id, bool bCritical, bool bKilled, const FString& Where)
{
	FCrewMember* M = FindMutable(Id);
	if (!M || !M->IsFit()) { return false; }
	if (bKilled) { Kill(*M, Where); } else { Wound(*M, bCritical, Where); }
	Dirty(); RequestSave();
	return true;
}

void UCrewRosterSubsystem::Kill(FCrewMember& M, const FString& Where)
{
	M.Condition = ECrewCondition::Dead;
	Record(M, ECrewEventKind::Died, FString::Printf(TEXT("Killed in %s, stardate %s."), *Where, *StardateText()), true);
	Grieve(M);
	Place(M);
	// A few of those beside them in a hit compartment carry it (not a death in sickbay, not the whole deck).
	if (Where != TEXT("Sickbay") && Where != TEXT("the ship"))
	{
		TArray<int32> Beside = PresentIn(Where, true);
		for (int32 Draw = 0; Draw < 3 && Beside.Num() > 0; ++Draw)
		{
			const int32 k = Rng.RandRange(0, Beside.Num() - 1);
			FCrewMember* S = FindMutable(Beside[k]); Beside.RemoveAtSwap(k);
			if (!S || S->Id == M.Id) { continue; }
			Remember(*S, FString::Printf(TEXT("%s died beside me"), *M.FullName()), -0.2f);
			if (!S->HasTrait(TEXT("Survivor")) && Rng.FRand() < 0.4f) { S->Traits.Add(TEXT("Survivor")); S->Facets[(int32)ECrewFacet::Resolve] = FMath::Min(S->Facets[(int32)ECrewFacet::Resolve] + 8.f, 100.f); Record(*S, ECrewEventKind::Moment, FString::Printf(TEXT("Walked out of %s when %s did not."), *Where, *M.Styled()), true, { M.Id }); }
		}
	}
	if (M.bHead) { PromoteToHead(M.Department, FString::Printf(TEXT("%s was killed"), *M.Styled())); }
	else if (M.bSecond) { M.bSecond = false; EnsureSecond(M.Department); }
	Lost.Add(M);
}

void UCrewRosterSubsystem::Grieve(const FCrewMember& Dead)
{
	for (const FCrewRelationship& R : Dead.Relationships)
	{
		if (R.Affinity <= 0.2f) { continue; }
		if (FCrewMember* F = FindMutable(R.Other))
		{
			const float Depth = R.Affinity;
			Remember(*F, FString::Printf(TEXT("%s is gone"), *Dead.FullName()), -0.35f * Depth, Depth > 0.6f);
			Record(*F, ECrewEventKind::Bereaved, FString::Printf(TEXT("Lost %s, %s."), *Dead.Styled(), *RelationName(R.Kind)), true, { Dead.Id });
			F->Stress = FMath::Min(F->Stress + 0.2f * Depth, 1.f);
			if (Depth > 0.6f && Rng.FRand() < 0.4f) { FCrewWant W; W.Tag = TEXT("LosingFriend"); W.Text = TEXT("losing another friend"); W.bFear = true; W.SinceDay = GetDay(); F->Wants.Add(W); }
		}
	}
}

void UCrewRosterSubsystem::Heal(FCrewMember& M)
{
	const bool bWasCritical = M.Condition == ECrewCondition::Critical;
	M.Condition = ECrewCondition::Fit; M.ConditionHours = 0.f;
	Record(M, ECrewEventKind::Recovered, bWasCritical ? TEXT("Out of danger and back on the roster.") : TEXT("Fit for duty again."));
	Place(M);
}

void UCrewRosterSubsystem::EnsureSecond(ECrewDepartment Dept)
{
	if (Second(Dept)) { return; }
	float Best = -1.f; FCrewMember* Next = nullptr;
	for (FCrewMember& M : Roster) { if (M.Department == Dept && M.IsAboard() && M.IsFit() && !M.bHead && M.Skill(SkillOf(Dept)) > Best) { Best = M.Skill(SkillOf(Dept)); Next = &M; } }
	if (Next) { Next->bSecond = true; Dirty(); }
}

void UCrewRosterSubsystem::PromoteToHead(ECrewDepartment Dept, const FString& Why)
{
	const FCrewMember* S = Second(Dept);
	if (!S)
	{
		// The most skilled fit officer of the department.
		float Best = -1.f; for (const FCrewMember& M : Roster) { if (M.Department == Dept && M.IsAboard() && !M.bHead && M.Skill(SkillOf(Dept)) > Best) { Best = M.Skill(SkillOf(Dept)); S = &M; } }
	}
	if (!S) { return; }
	FCrewMember* New = FindMutable(S->Id);
	for (FCrewMember& M : Roster) { if (M.Department == Dept && M.bHead) { M.bHead = false; } }
	New->bHead = true; New->bSecond = false;
	if (New->Rank < ECrewRank::Lieutenant) { New->Rank = ECrewRank::Lieutenant; ++New->Service.Promotions; }
	New->Posting = PostingFor(Rng, Dept, 0);
	Record(*New, ECrewEventKind::Promoted, FString::Printf(TEXT("Took the %s department when %s."), *DepartmentName(Dept).ToLower(), *Why), true);
	Remember(*New, TEXT("Stepped up"), 0.1f);
	EnsureSecond(Dept);
	Report(ECrewDepartment::Command, FString::Printf(TEXT("%s has the %s department now."), *New->Styled(), *DepartmentName(Dept).ToLower()), FLinearColor(1.f, 0.8f, 0.5f), true);
	Dirty();
}

void UCrewRosterSubsystem::SyncCasualties(float InjuredF, float CriticalF, float DeadF, TArray<FString>& OutLines)
{
	// Rounded once each from the model's fractions: the total under treatment as one number, so a fraction moving
	// from critical to wounded cannot flicker the rounded sum.
	const int32 Critical = FMath::RoundToInt(CriticalF), Dead = FMath::RoundToInt(DeadF);
	const int32 TargetTreat = FMath::RoundToInt(InjuredF + CriticalF);
	const int32 Injured = FMath::Max(TargetTreat - Critical, 0);
	// Match the medical model's counts with people. The model keeps fractions and rounds each count on its own,
	// so a patient stabilised from critical to wounded can show as the wounded rising before the critical falls:
	// the reconciliation therefore works on the number under treatment (wounded + critical) for who is hurt and
	// who is healed, and only then on the split between critical and wounded.
	const int32 HaveDead = FMath::Max(Lost.Num() - DeadBase, 0), HaveCrit = CountCondition(ECrewCondition::Critical), HaveInj = CountCondition(ECrewCondition::Injured);
	if (Dead == HaveDead && Critical == HaveCrit && TargetTreat == HaveInj + HaveCrit) { return; }   // the usual tick: nothing moved
	TArray<FString> Killed, Wounded, Healed;
	TArray<int32> Local, Elsewhere;
	Candidates(LastHitCompartment, Local, Elsewhere);
	const bool bFreshHit = Hours - LastHitHours < 1.0;
	const FString Where = (LastHitCompartment.IsEmpty() || !bFreshHit) ? TEXT("the ship") : LastHitCompartment;
	auto Take = [&]() -> FCrewMember*
	{
		TArray<int32>& Pool = Local.Num() > 0 ? Local : Elsewhere;
		if (Pool.Num() == 0) { return nullptr; }
		const int32 k = Rng.RandRange(0, Pool.Num() - 1);
		FCrewMember* M = FindMutable(Pool[k]);
		Pool.RemoveAtSwap(k);
		return M;
	};
	auto LongestCritical = [&]() -> FCrewMember* { FCrewMember* P = nullptr; for (FCrewMember& M : Roster) { if (M.Condition == ECrewCondition::Critical && (!P || M.ConditionHours < P->ConditionHours)) { P = &M; } } return P; };
	auto LongestInjured = [&]() -> FCrewMember* { FCrewMember* P = nullptr; for (FCrewMember& M : Roster) { if (M.Condition == ECrewCondition::Injured && (!P || M.ConditionHours < P->ConditionHours)) { P = &M; } } return P; };
	// The dead: the critical the model lost (its count fell further than the treated total rose) died in sickbay,
	// the rest, while a hit is fresh, in the compartment; with no fresh hit all of them are sickbay's.
	int32 FromSickbay = bFreshHit ? FMath::Max(0, HaveCrit - Critical) : Dead - HaveDead;
	int32 Sickbay = 0;
	for (int32 n = HaveDead; n < Dead; ++n)
	{
		FCrewMember* Patient = (FromSickbay > 0 || !bFreshHit || Local.Num() + Elsewhere.Num() == 0) ? LongestCritical() : nullptr;
		if (!Patient && (!bFreshHit || Local.Num() + Elsewhere.Num() == 0)) { Patient = LongestInjured(); }   // bad air takes the wounded too
		if (Patient) { --FromSickbay; ++Sickbay; Kill(*Patient, TEXT("Sickbay")); Killed.Add(Patient->Styled()); continue; }
		if (FCrewMember* M = Take()) { Kill(*M, bFreshHit && M->Where != TEXT("Sickbay") && M->Activity != ECrewActivity::Leave ? M->Where : Where); Killed.Add(M->Styled()); }
	}
	// Under treatment: more than before means new wounds (critical first while the model's critical count is
	// above ours), fewer means the longest-treated return to duty.
	const int32 NowTreat = CountCondition(ECrewCondition::Injured) + CountCondition(ECrewCondition::Critical);
	for (int32 n = NowTreat; n < TargetTreat; ++n)
	{
		FCrewMember* M = Take();
		if (!M) { break; }
		const bool bCritical = CountCondition(ECrewCondition::Critical) < Critical;
		Wound(*M, bCritical, bFreshHit && M->Where != TEXT("Sickbay") ? M->Where : Where); Wounded.Add(M->Styled());
	}
	for (int32 n = TargetTreat; n < NowTreat; ++n)
	{
		FCrewMember* P = LongestInjured();
		if (!P) { P = LongestCritical(); }
		if (!P) { break; }
		Heal(*P); Healed.Add(P->Styled());
	}
	// The split: stabilised out of danger, or (rarely, the rounding) taken a turn for the worse.
	for (int32 n = Critical; n < CountCondition(ECrewCondition::Critical); ++n)
	{
		FCrewMember* P = LongestCritical();
		if (!P) { break; }
		P->Condition = ECrewCondition::Injured; P->ConditionHours = 24.f; Record(*P, ECrewEventKind::Recovered, TEXT("Out of danger."));
	}
	for (int32 n = CountCondition(ECrewCondition::Critical); n < Critical; ++n)
	{
		FCrewMember* P = nullptr; for (FCrewMember& M : Roster) { if (M.Condition == ECrewCondition::Injured && (!P || M.ConditionHours > P->ConditionHours)) { P = &M; } }
		if (!P) { break; }
		P->Condition = ECrewCondition::Critical; P->ConditionHours = 72.f; Record(*P, ECrewEventKind::Wounded, TEXT("Took a turn for the worse in sickbay."));
	}
	auto Join = [](const TArray<FString>& Names) { FString S; for (int32 i = 0; i < Names.Num() && i < 3; ++i) { S += (i ? TEXT(", ") : TEXT("")) + Names[i]; } if (Names.Num() > 3) { S += FString::Printf(TEXT(" and %d more"), Names.Num() - 3); } return S; };
	if (Killed.Num()) { OutLines.Add(FString::Printf(TEXT("Lost %s: %s."), Sickbay == Killed.Num() ? TEXT("in sickbay") : bFreshHit ? *(TEXT("in ") + Where) : TEXT("aboard"), *Join(Killed))); }
	if (Wounded.Num()) { OutLines.Add(FString::Printf(TEXT("Wounded %s: %s."), bFreshHit ? *(TEXT("in ") + Where) : TEXT("aboard"), *Join(Wounded))); }
	Dirty(); RequestSave();   // something moved: a wound, a death, a recovery or the split
	if (Healed.Num() && Rng.FRand() < 0.3f) { OutLines.Add(FString::Printf(TEXT("Back on their feet: %s."), *Join(Healed))); }
}

void UCrewRosterSubsystem::NoteHeadHurt(ECrewDepartment Dept, bool bLost, float Seconds)
{
	const FCrewMember* H = Head(Dept);
	if (!H) { return; }
	FCrewMember* M = FindMutable(H->Id);
	const FString Where = LastHitCompartment.IsEmpty() ? TEXT("the ship") : LastHitCompartment;
	if (bLost) { Kill(*M, Where); }
	else if (M->Condition == ECrewCondition::Fit) { Wound(*M, false, Where); M->ConditionHours = FMath::Max(Seconds / FMath::Max(SecondsPerShipHour, 1.f), 26.f); }   // the ship's timer returns them, not sickbay's general rate
	Dirty(); RequestSave();
}

bool UCrewRosterSubsystem::HealHead(ECrewDepartment Dept)
{
	const FCrewMember* H = Head(Dept);
	if (!H || (H->Condition != ECrewCondition::Injured && H->Condition != ECrewCondition::Critical)) { return false; }
	Heal(*FindMutable(H->Id));
	Dirty(); RequestSave();
	return true;
}

void UCrewRosterSubsystem::NoteRepair(const FString& SystemName, bool bUnderFire)
{
	for (FCrewMember& M : Roster)
	{
		if (M.Department == ECrewDepartment::Engineering && M.Activity == ECrewActivity::OnWatch && M.IsFit())
		{
			Grow(M, ECrewSkill::Engineering, bUnderFire ? 6.f : 3.f);
			if (bUnderFire) { Remember(M, FString::Printf(TEXT("%s back under fire"), *SystemName), 0.08f); }
		}
	}
	Dirty(); RequestSave();
}

void UCrewRosterSubsystem::NoteScan(const FString& Label, int32 Points, bool bDiscovery)
{
	for (FCrewMember& M : Roster)
	{
		if (M.Department == ECrewDepartment::Science && M.Activity == ECrewActivity::OnWatch && M.IsFit())
		{
			Grow(M, ECrewSkill::Science, FMath::Clamp(Points / 30.f, 1.f, 8.f));
			if (bDiscovery) { Remember(M, FString::Printf(TEXT("Part of the %s discovery"), *Label), 0.12f + M.Facet(ECrewFacet::Curiosity) / 800.f); Record(M, ECrewEventKind::Discovery, FString::Printf(TEXT("On the science watch for the %s discovery."), *Label)); }
		}
	}
	Dirty(); RequestSave();   // the science log has already kept the scan; the watch's growth goes with it
}

void UCrewRosterSubsystem::NoteSight(FName Tag, const FString& Text)
{
	for (FCrewMember& M : Roster)
	{
		if (!M.IsAboard()) { continue; }
		for (FCrewWant& W : M.Wants)
		{
			if (!W.bFear && !W.bGranted && W.Tag == Tag) { W.bGranted = true; Remember(M, Text, 0.2f); Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("%s: %s, as %s had wanted."), *Text, *Text, *M.Pronoun)); }
		}
		if (M.Facet(ECrewFacet::Curiosity) > 70.f) { Remember(M, Text, 0.05f); }
	}
	Dirty(); RequestSave();
}

void UCrewRosterSubsystem::NoteStarbase(const FString& StationName)
{
	if (RecruitStation != StationName || RecruitDay != GetDay()) { MakeRecruits(StationName); }
	// Letters come aboard with the station's mail, and anyone with family or a friend at this station goes ashore.
	int32 Ashore = 0;
	for (FCrewMember& M : Roster)
	{
		if (!M.IsAboard()) { continue; }
		if (M.HasTrait(TEXT("Homesick")) || Rng.FRand() < 0.3f)
		{
			Remember(M, TEXT("Mail from the station"), M.HasTrait(TEXT("Homesick")) ? 0.15f : 0.06f);
			for (FCrewWant& W : M.Wants) { if (W.Tag == TEXT("Letter") && !W.bGranted) { W.bGranted = true; } }
		}
		for (FCrewTie& T : M.Ties)
		{
			if (T.Kind == ECrewTiePlace::Station && T.Place == StationName && GetDay() - T.LastContactDay >= 1) { Reunion(M, T, true); ++Ashore; break; }
		}
	}
	if (Ashore > 1) { Report(ECrewDepartment::Command, FString::Printf(TEXT("%d of the crew have people at %s; I've let them go across while we're alongside."), Ashore, *StationName), FLinearColor(0.75f, 0.9f, 1.f), true); }
	Dirty(); RequestSave();
}

void UCrewRosterSubsystem::NoteResupplyDone(const FString& StationName, TArray<FString>& OutLines)
{
	// The wounded transferred ashore come back fit (the medical model's reading), and the dead are replaced from
	// the station's pool: new names signing on for the vacancies.
	int32 Restored = 0;
	for (FCrewMember& M : Roster) { if (M.Condition == ECrewCondition::Injured || M.Condition == ECrewCondition::Critical) { Heal(M); ++Restored; } }
	int32 Counts[(int32)ECrewDepartment::Count] = { 0 };
	for (const FCrewMember& M : Roster) { if (M.Condition != ECrewCondition::Dead && M.Condition != ECrewCondition::Transferred) { ++Counts[(int32)M.Department]; } }
	FRandomStream R(GetTypeHash(StationName) ^ Seed ^ (int32)Hours);
	TArray<FString> Names;
	for (int32 d = 0; d < (int32)ECrewDepartment::Count; ++d)
	{
		for (int32 k = Counts[d]; k < TargetFor((ECrewDepartment)d); ++k)
		{
			FCrewMember M = MakePerson(R, (ECrewDepartment)d, RankFromIndex(R, R.RandRange(3, 9), 10), R.RandRange(3, 30));
			M.Backstory = FString::Printf(TEXT("%s; signed on at %s"), Pick(R, Classes), *StationName);
			Record(M, ECrewEventKind::Joined, FString::Printf(TEXT("Signed on at %s to fill a vacancy in %s."), *StationName, *DepartmentName((ECrewDepartment)d).ToLower()), true);
			Place(M);
			Roster.Add(M);
			Names.Add(M.Styled());
		}
	}
	for (int32 i = Roster.Num() - 1; i >= 0; --i) { if (Roster[i].Condition == ECrewCondition::Dead) { Roster.RemoveAt(i); } }
	DeadBase = Lost.Num();   // the medical model's count starts again from nothing
	if (Restored > 0) { OutLines.Add(FString::Printf(TEXT("%d wounded treated ashore and back aboard."), Restored)); }
	if (Names.Num() > 0) { OutLines.Add(FString::Printf(TEXT("%d replacements signed on at %s%s."), Names.Num(), *StationName, Names.Num() <= 3 ? *(TEXT(": ") + FString::Join(Names, TEXT(", "))) : TEXT(""))); }
	Dirty(); RequestSave();
}

// ---- the captain's hand ----

bool UCrewRosterSubsystem::Promote(int32 Id, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (!M->IsAboard()) { OutReply = FString::Printf(TEXT("%s is %s"), *M->Styled(), *ConditionName(M->Condition).ToLower()); return false; }
	if (M->Rank >= ECrewRank::Commander) { OutReply = FString::Printf(TEXT("%s holds the highest rank aboard short of yours"), *M->Styled()); return false; }
	M->Rank = (ECrewRank)((int32)M->Rank + 1);
	++M->Service.Promotions;
	Record(*M, ECrewEventKind::Promoted, FString::Printf(TEXT("Promoted to %s by the captain."), *RankName(M->Rank)), true);
	Remember(*M, TEXT("Promoted"), 0.25f + M->Facet(ECrewFacet::Ambition) / 400.f);
	M->Needs[(int32)ECrewNeed::Recognition] = 1.f;
	for (FCrewWant& W : M->Wants) { if (W.Tag == TEXT("Promotion") && !W.bGranted) { W.bGranted = true; Remember(*M, TEXT("The promotion I wanted"), 0.15f, true); } }
	// Rivals resent it.
	for (const FCrewRelationship& R : M->Relationships) { if (R.Kind == ECrewRelation::Rival) { if (FCrewMember* O = FindMutable(R.Other)) { Remember(*O, FString::Printf(TEXT("%s promoted over me"), *M->FullName()), -0.1f); } } }
	OutReply = FString::Printf(TEXT("%s, with the captain's congratulations"), *M->Styled());
	Dirty(); RequestSave();
	return true;
}

bool UCrewRosterSubsystem::Demote(int32 Id, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (!M->IsAboard()) { OutReply = FString::Printf(TEXT("%s is %s"), *M->Styled(), *ConditionName(M->Condition).ToLower()); return false; }
	if (M->Rank == ECrewRank::Crewman) { OutReply = TEXT("Nowhere lower to go"); return false; }
	M->Rank = (ECrewRank)((int32)M->Rank - 1);
	Record(*M, ECrewEventKind::Demoted, FString::Printf(TEXT("Reduced to %s by the captain."), *RankName(M->Rank)), true);
	Remember(*M, TEXT("Demoted"), -0.3f, M->HasTrait(TEXT("WoundedPride")));
	M->Stress = FMath::Min(M->Stress + 0.2f, 1.f);
	OutReply = FString::Printf(TEXT("%s, reduced in rank"), *M->Styled());
	Dirty(); RequestSave();
	return true;
}

bool UCrewRosterSubsystem::Commend(int32 Id, const FString& For, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (!M->IsAboard()) { OutReply = FString::Printf(TEXT("%s is %s"), *M->Styled(), *ConditionName(M->Condition).ToLower()); return false; }
	++M->Service.Commendations;
	Record(*M, ECrewEventKind::Commended, FString::Printf(TEXT("Commended by the captain%s."), For.IsEmpty() ? TEXT("") : *(TEXT(" for ") + For)), true);
	Remember(*M, TEXT("Commended by the captain"), 0.2f);
	M->Needs[(int32)ECrewNeed::Recognition] = FMath::Min(M->Needs[(int32)ECrewNeed::Recognition] + 0.5f, 1.f);
	for (FCrewWant& W : M->Wants) { if (W.Tag == TEXT("Commendation") && !W.bGranted) { W.bGranted = true; } }
	OutReply = FString::Printf(TEXT("%s commended; it goes in the record"), *M->Styled());
	Dirty(); RequestSave();
	return true;
}

bool UCrewRosterSubsystem::Reprimand(int32 Id, const FString& For, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (!M->IsAboard()) { OutReply = FString::Printf(TEXT("%s is %s"), *M->Styled(), *ConditionName(M->Condition).ToLower()); return false; }
	++M->Service.Reprimands;
	Record(*M, ECrewEventKind::Reprimanded, FString::Printf(TEXT("Reprimanded by the captain%s."), For.IsEmpty() ? TEXT("") : *(TEXT(" for ") + For)), true);
	// Answering an incident: every pending one this person caused in the last two days is settled; the grudge is
	// squashed a little and the one it was said to sees it. Older ones have gone cold.
	for (int32 i = Incidents.Num() - 1; i >= 0; --i)
	{
		const FCrewIncident P = Incidents[i];
		if (GetDay() - P.Day > 2) { Incidents.RemoveAt(i); continue; }
		if (P.Offender != M->Id) { continue; }
		Incidents.RemoveAt(i);
		if (FCrewMember* V = FindMutable(P.Victim))
		{
			MoveBias(*M, FName(*V->Species), 0.15f, TEXT("the captain's reprimand"));
			V->Trust = FMath::Min(V->Trust + 0.12f, 1.f); Remember(*V, TEXT("The captain answered it"), 0.12f);
			for (FCrewSuspicion& X : V->Suspicions) { if (X.Of == M->Id) { X.Weight *= 0.5f; } }
			FCrewExperience E; E.Tag = TEXT("Captain.Fair"); E.Intensity = 0.4f; E.Group = FName(*V->Species); E.Text = FString::Printf(TEXT("The captain came down on %s over what was said about %ss"), *M->Styled(), *V->Species); E.bCaptainsChoice = true;
			Experience(E);
		}
	}
	const bool bProud = M->HasTrait(TEXT("WoundedPride"));
	Remember(*M, TEXT("Reprimanded"), bProud ? -0.25f : -0.15f, bProud);
	M->Stress = FMath::Min(M->Stress + 0.15f, 1.f);
	if (M->Facet(ECrewFacet::Discipline) > 60.f) { M->Needs[(int32)ECrewNeed::Purpose] = FMath::Min(M->Needs[(int32)ECrewNeed::Purpose] + 0.1f, 1.f); }
	OutReply = FString::Printf(TEXT("%s reprimanded"), *M->Styled());
	Dirty(); RequestSave();
	return true;
}

bool UCrewRosterSubsystem::SetWatch(int32 Id, ECrewWatch Watch, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (!M->IsAboard()) { OutReply = FString::Printf(TEXT("%s is %s"), *M->Styled(), *ConditionName(M->Condition).ToLower()); return false; }
	M->Watch = Watch;
	Record(*M, ECrewEventKind::Transfer, FString::Printf(TEXT("Moved to the %s watch."), *WatchName(Watch).ToLower()));
	Place(*M);
	OutReply = FString::Printf(TEXT("%s to the %s watch"), *M->Styled(), *WatchName(Watch).ToLower());
	Dirty(); RequestSave();
	return true;
}

bool UCrewRosterSubsystem::SetPosting(int32 Id, ECrewDepartment Dept, const FString& Compartment, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (!M->IsAboard()) { OutReply = FString::Printf(TEXT("%s is %s"), *M->Styled(), *ConditionName(M->Condition).ToLower()); return false; }
	if (M->bHead) { OutReply = TEXT("A department head keeps the department; promote a successor first"); return false; }
	const ECrewDepartment Was = M->Department;
	M->Department = Dept;
	if (M->bSecond) { M->bSecond = false; EnsureSecond(Was); }
	M->Posting = Compartment.IsEmpty() ? PostingFor(Rng, Dept, 5) : Compartment;
	Record(*M, ECrewEventKind::Transfer, FString::Printf(TEXT("Posted to %s, %s department."), *M->Posting, *DepartmentName(Dept).ToLower()), true);
	for (FCrewWant& W : M->Wants) { if (W.Tag == TEXT("BridgePosting") && !W.bGranted && M->Posting == TEXT("Main bridge")) { W.bGranted = true; Remember(*M, TEXT("A bridge posting at last"), 0.2f, true); } }
	Place(*M);
	OutReply = FString::Printf(TEXT("%s posted to %s"), *M->Styled(), *M->Posting);
	Dirty(); RequestSave();
	return true;
}

bool UCrewRosterSubsystem::GrantLeave(int32 Id, int32 Days, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (M->Condition != ECrewCondition::Fit) { OutReply = FString::Printf(TEXT("%s is %s"), *M->Styled(), *ConditionName(M->Condition).ToLower()); return false; }
	if (!bAlongside) { OutReply = TEXT("Shore leave needs a station alongside"); return false; }
	if (M->bHead) { OutReply = TEXT("The ship keeps its department heads; the second may go"); return false; }
	M->Condition = ECrewCondition::OnLeave; M->ConditionHours = FMath::Max(Days, 1) * 24.f; M->LeaveStation = AlongsideStation;
	if (M->bSecond) { M->bSecond = false; EnsureSecond(M->Department); }
	Record(*M, ECrewEventKind::Leave, FString::Printf(TEXT("%d days' leave granted at %s."), Days, *AlongsideStation));
	Remember(*M, TEXT("Shore leave"), 0.2f);
	for (FCrewWant& W : M->Wants) { if (W.Tag == TEXT("ShoreLeave") && !W.bGranted) { W.bGranted = true; Remember(*M, TEXT("The leave I asked for"), 0.1f); } }
	Place(*M);
	OutReply = FString::Printf(TEXT("%s ashore for %d days"), *M->Styled(), Days);
	Dirty(); RequestSave();
	return true;
}

bool UCrewRosterSubsystem::Talk(int32 Id, const FString& How, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (!M->IsAboard()) { OutReply = FString::Printf(TEXT("%s is %s"), *M->Styled(), *ConditionName(M->Condition).ToLower()); return false; }
	const FString H = How.ToLower();
	const FCrewWant* Want = nullptr; for (const FCrewWant& W : M->Wants) { if (!W.bFear && !W.bGranted) { Want = &W; break; } }
	M->Needs[(int32)ECrewNeed::Recognition] = FMath::Min(M->Needs[(int32)ECrewNeed::Recognition] + 0.3f, 1.f);
	if (H == TEXT("listen"))
	{
		M->Trust = FMath::Min(M->Trust + 0.04f, 1.f);
		M->Stress = FMath::Max(M->Stress - 0.15f * (0.5f + M->Facet(ECrewFacet::Empathy) / 200.f), 0.f);
		Remember(*M, TEXT("The captain listened"), 0.1f);
		OutReply = Want ? FString::Printf(TEXT("%s talks, in the end, about %s"), *M->Styled(), *Want->Text) : FString::Printf(TEXT("%s has nothing pressing, and is glad to be asked"), *M->Styled());
		Record(*M, ECrewEventKind::Moment, TEXT("The captain asked how things were, and listened."));
	}
	else if (H == TEXT("encourage"))
	{
		M->Needs[(int32)ECrewNeed::Purpose] = FMath::Min(M->Needs[(int32)ECrewNeed::Purpose] + 0.2f, 1.f);
		Remember(*M, TEXT("A word from the captain"), 0.12f);
		OutReply = FString::Printf(TEXT("%s stands a little straighter"), *M->Styled());
		Record(*M, ECrewEventKind::Moment, TEXT("A word of encouragement from the captain."));
	}
	else if (H == TEXT("order"))
	{
		M->Needs[(int32)ECrewNeed::Purpose] = FMath::Min(M->Needs[(int32)ECrewNeed::Purpose] + 0.1f, 1.f);
		if (M->Facet(ECrewFacet::Discipline) < 40.f) { Remember(*M, TEXT("Ordered about"), -0.06f); }
		OutReply = FString::Printf(TEXT("%s: \"Aye, Captain.\""), *M->Styled());
		Record(*M, ECrewEventKind::Moment, TEXT("Orders from the captain, in person."));
	}
	else if (H == TEXT("promise"))
	{
		if (Want && Want->bPromised)
		{
			OutReply = FString::Printf(TEXT("%s has your word on %s already, since day %d"), *M->Styled(), *Want->Text, Want->PromisedDay);
		}
		else if (Want)
		{
			M->Trust = FMath::Min(M->Trust + 0.06f, 1.f);
			const_cast<FCrewWant*>(Want)->bPromised = true; const_cast<FCrewWant*>(Want)->PromisedDay = GetDay();
			Remember(*M, FString::Printf(TEXT("The captain promised %s"), *Want->Text), 0.18f);
			OutReply = FString::Printf(TEXT("%s will hold you to %s"), *M->Styled(), *Want->Text);
			Record(*M, ECrewEventKind::Moment, FString::Printf(TEXT("The captain promised %s."), *Want->Text));
		}
		else { OutReply = FString::Printf(TEXT("%s wants for nothing you can promise"), *M->Styled()); }
	}
	else { OutReply = TEXT("talk <name> listen|encourage|order|promise"); return false; }
	UpdateMood(*M);
	Dirty(); RequestSave();
	return true;
}

bool UCrewRosterSubsystem::SignOn(int32 RecruitIndex, FString& OutReply)
{
	if (!Recruits.IsValidIndex(RecruitIndex)) { OutReply = TEXT("No such recruit; ship crew recruits lists them alongside a station"); return false; }
	if (!bAlongside || AlongsideStation != RecruitStation) { OutReply = FString::Printf(TEXT("%s's recruits sign on alongside %s"), *RecruitStation, *RecruitStation); return false; }
	FCrewMember M = Recruits[RecruitIndex];
	Recruits.RemoveAt(RecruitIndex);
	M.Id = NextId++;
	M.Service.JoinedStardate = GetStardate();
	M.History.Reset();
	Record(M, ECrewEventKind::Joined, FString::Printf(TEXT("Signed on at %s; %s."), *RecruitStation, *M.Backstory), true);
	Place(M);
	Roster.Add(M);
	OutReply = FString::Printf(TEXT("%s signed on, %s department"), *M.Styled(), *DepartmentName(M.Department).ToLower());
	Dirty(); RequestSave();
	return true;
}

bool UCrewRosterSubsystem::Discharge(int32 Id, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (M->bHead) { OutReply = TEXT("Promote a successor before discharging a department head"); return false; }
	if (M->Condition != ECrewCondition::Fit) { OutReply = FString::Printf(TEXT("%s is %s; not a transfer"), *M->Styled(), *ConditionName(M->Condition).ToLower()); return false; }
	if (!bAlongside) { OutReply = TEXT("A transfer ashore needs a station alongside"); return false; }
	M->Condition = ECrewCondition::Transferred;
	Record(*M, ECrewEventKind::Transfer, FString::Printf(TEXT("Transferred off the ship at %s."), *AlongsideStation), true);
	if (M->bSecond) { M->bSecond = false; EnsureSecond(M->Department); }
	for (const FCrewRelationship& R : M->Relationships) { if (R.Affinity > 0.4f) { if (FCrewMember* F = FindMutable(R.Other)) { Remember(*F, FString::Printf(TEXT("%s transferred away"), *M->FullName()), -0.1f); } } }
	OutReply = FString::Printf(TEXT("%s transferred ashore"), *M->Styled());
	Place(*M);
	Dirty(); RequestSave();
	return true;
}

FString UCrewRosterSubsystem::Memorial()
{
	MourningDays = 3;
	TArray<FString> Names; for (const FCrewMember& L : Lost) { Names.Add(L.Styled()); }
	for (FCrewMember& M : Roster)
	{
		if (!M.IsAboard()) { continue; }
		bool bBereaved = false; for (const FCrewMemory& Me : M.Memories) { if (Me.bPermanent && Me.Weight < 0.f) { bBereaved = true; } }
		if (bBereaved) { Remember(M, TEXT("The memorial"), 0.12f); M.Stress = FMath::Max(M.Stress - 0.1f, 0.f); }
		M.Needs[(int32)ECrewNeed::Belonging] = FMath::Min(M.Needs[(int32)ECrewNeed::Belonging] + 0.1f, 1.f);
		if (Names.Num() > 0 && Rng.FRand() < 0.1f) { Record(M, ECrewEventKind::Memorial, FString::Printf(TEXT("Stood for the memorial: %s."), *FString::Join(Names, TEXT(", ")))); }
	}
	Dirty(); RequestSave();
	return Names.Num() > 0 ? FString::Printf(TEXT("The ship stands for %s"), *FString::Join(Names, TEXT(", "))) : TEXT("The ship stands a moment for those lost elsewhere");
}

FCrewCheck UCrewRosterSubsystem::Check(ECrewSkill Skill, float Difficulty, const TArray<int32>& InIds, float CaptainBonus, FName Group)
{
	FCrewCheck Out;
	TArray<int32> Ids = InIds;
	if (Ids.Num() == 0)
	{
		// The department's head and second for this skill.
		ECrewDepartment D = ECrewDepartment::Command;
		for (int32 d = 0; d < (int32)ECrewDepartment::Count; ++d) { if (SkillOf((ECrewDepartment)d) == Skill) { D = (ECrewDepartment)d; break; } }
		if (const FCrewMember* H = Head(D)) { Ids.Add(H->Id); }
		if (const FCrewMember* S = Second(D)) { Ids.Add(S->Id); }
	}
	// Each person once, and only those who can make the attempt.
	TArray<int32> Party;
	for (int32 Id : Ids) { const FCrewMember* M = Find(Id); if (M && (M->IsAboard() || M->Condition == ECrewCondition::Away) && M->Condition != ECrewCondition::Critical) { Party.AddUnique(Id); } }
	Ids = Party;
	float Best = -FLT_MAX, Worst = FLT_MAX, Sum = 0.f; int32 N = 0;
	for (int32 Id : Ids)
	{
		const FCrewMember* M = Find(Id);
		if (!M) { continue; }
		float E = M->Skill(Skill) + (M->Facet(ECrewFacet::Discipline) - 50.f) / 10.f - M->Stress * 20.f - (1.f - M->Need(ECrewNeed::Rest)) * 12.f;
		if (M->Condition == ECrewCondition::Injured) { E -= 15.f; }
		if (M->HasTrait(TEXT("Perfectionist"))) { E += 4.f; }
		E += M->Trust * 6.f;   // steadier under a captain they believe in
		// Dealing with a people or a power: a grudge shows, a sympathy helps, most of all in a parley or a search.
		if (!Group.IsNone()) { const float B = M->Bias(Group); E += (Skill == ECrewSkill::Diplomacy || Skill == ECrewSkill::Security) ? (B < 0.f ? B * 20.f : B * 8.f) : B * 4.f; }
		if (E > Best) { Best = E; Out.BestId = Id; }
		if (E < Worst) { Worst = E; Out.WorstId = Id; }
		Sum += E; ++N;
	}
	if (N == 0) { Out.Summary = TEXT("Nobody to make the attempt"); return Out; }
	const float Support = N > 1 ? (Sum - Best) / (N - 1) * 0.25f : 0.f;
	const float Roll = Rng.FRandRange(-15.f, 15.f);
	const float Effective = Best + Support + CaptainBonus + Roll;
	Out.Margin = Effective - Difficulty;
	Out.bSuccess = Out.Margin >= 0.f;
	const FCrewMember* B = Find(Out.BestId);
	for (int32 Id : Ids) { if (FCrewMember* M = FindMutable(Id)) { Grow(*M, Skill, Id == Out.BestId ? 8.f : 4.f); ++M->Service.Missions; } }
	if (FCrewMember* BM = FindMutable(Out.BestId))
	{
		Record(*BM, ECrewEventKind::Check, FString::Printf(TEXT("%s the %s problem (difficulty %.0f) with %s."), Out.bSuccess ? TEXT("Carried") : TEXT("Could not crack"), *SkillName(Skill).ToLower(), Difficulty, Out.Margin > 20.f ? TEXT("room to spare") : Out.bSuccess ? TEXT("little to spare") : TEXT("nothing to show")), Out.bSuccess && Out.Margin > 20.f);
		Remember(*BM, Out.bSuccess ? TEXT("Carried it") : TEXT("Fell short"), Out.bSuccess ? 0.1f : -0.1f);
	}
	Out.Summary = FString::Printf(TEXT("%s: %s led, effective %.0f against %.0f (margin %+.0f)"), Out.bSuccess ? TEXT("Success") : TEXT("Failure"), B ? *B->Styled() : TEXT("nobody"), Effective, Difficulty, Out.Margin);
	Dirty(); RequestSave();
	return Out;
}
