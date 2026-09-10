// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "PCGSettings.h"
#include "UObject/UObjectIterator.h"

/**
 * Guards the context-menu naming contract for the whole PCGUtils modelling library.
 *
 * This lives in PCGUtilsFracture because it is the only module that can see both halves of the contract
 * (PCGUtilsDynMesh must never depend on PCGUtilsFracture), and it reaches the classes through
 * TObjectIterator rather than includes so it needs no dependency on PCGUtilsPainter either.
 *
 * The search model below is a deliberate reimplementation of the engine's own, from
 * FEdGraphSchemaAction::UpdateSearchText and SGraphActionMenu::GenerateFilteredItems:
 *
 *   - the haystack is the entry's title, its Keywords metadata and its palette category, each split on
 *     spaces, lowercased, and concatenated with NO separator between words;
 *   - the needle is the filter string split on spaces, and EVERY term must appear as a substring.
 *
 * The missing separator is the subtle part: it means a term may straddle two adjacent words, which is why
 * these assertions check real concatenated text instead of a word set. The category comes from
 * UPCGSettings::GetType() through the EPCGSettingsType enum - PCG offers no per-class category hook, so
 * "Dynamic Mesh" is the root bucket for this library and the family prefix in the title is what groups it.
 */
namespace PCGUtilsPaletteSearchContract
{
	/** One context-menu action: a node title, or one of a node's preconfigured entries. */
	struct FEntry
	{
		FString Label;
		FString SearchText;
		FString ClassName;
		FString Package;
	};

	FString MakeSearchText(const FString& Label, const FString& Keywords, const FString& Category)
	{
		FString Out;
		for (const FString& Part : {Label, Keywords, Category})
		{
			TArray<FString> Words;
			Part.ParseIntoArray(Words, TEXT(" "), true);
			for (const FString& Word : Words)
			{
				Out += Word.ToLower();
			}
			// The engine separates the three fields, so a term can never straddle field boundaries.
			Out += TEXT("\n");
		}
		return Out;
	}

	bool Matches(const FString& SearchText, const FString& Filter)
	{
		TArray<FString> Terms;
		Filter.ToLower().ParseIntoArray(Terms, TEXT(" "), true);
		for (const FString& Term : Terms)
		{
			if (!SearchText.Contains(Term, ESearchCase::CaseSensitive))
			{
				return false;
			}
		}
		return true;
	}

	/** Nodes that live in a DynMesh module but are genuinely not DynMesh elements.
	 *  Names are as UClass::GetName() reports them, i.e. without the U prefix. */
	bool IsDynMeshFamilyExempt(const FString& ClassName)
	{
		return ClassName == TEXT("PCGPaintStaticMeshVertexColorSettings");
	}

	TArray<FEntry> CollectEntries()
	{
		static const TSet<FString> Packages = {
			TEXT("/Script/PCGUtilsDynMesh"), TEXT("/Script/PCGUtilsFracture"), TEXT("/Script/PCGUtilsPainter")};

		TArray<FEntry> Entries;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->IsChildOf(UPCGSettings::StaticClass()) ||
				Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_Hidden))
			{
				continue;
			}

			const FString Package = Class->GetOutermost()->GetName();
			if (!Packages.Contains(Package))
			{
				continue;
			}

			const UPCGSettings* Settings = Class->GetDefaultObject<UPCGSettings>();
			if (!Settings || !Settings->bExposeToLibrary)
			{
				continue;
			}

			const FString Keywords = Class->GetMetaData(TEXT("Keywords"));
			const FString Category = StaticEnum<EPCGSettingsType>()
				->GetDisplayNameTextByValue(static_cast<int64>(Settings->GetType())).ToString();

			const TArray<FPCGPreConfiguredSettingsInfo> Presets = Settings->GetPreconfiguredInfo();
			if (Presets.IsEmpty() || !Settings->OnlyExposePreconfiguredSettings())
			{
				const FString Label = Settings->GetDefaultNodeTitle().ToString();
				Entries.Add({Label, MakeSearchText(Label, Keywords, Category), Class->GetName(), Package});
			}
			for (const FPCGPreConfiguredSettingsInfo& Preset : Presets)
			{
				const FString Label = Preset.Label.ToString();
				const FString PresetKeywords = Keywords + TEXT(" ") + Preset.SearchHints.ToString();
				Entries.Add({Label, MakeSearchText(Label, PresetKeywords, Category), Class->GetName(), Package});
			}
		}
		return Entries;
	}

	bool IsDynMeshSelect(const FEntry& Entry) { return Entry.Label.StartsWith(TEXT("Select | ")); }
	bool IsGCSelect(const FEntry& Entry) { return Entry.Label.StartsWith(TEXT("GC | Select | ")); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsPaletteSearchContractTest,
	"PCGUtils.Palette.SearchContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsPaletteSearchContractTest::RunTest(const FString&)
{
	using namespace PCGUtilsPaletteSearchContract;

	const TArray<FEntry> Entries = CollectEntries();
	if (!TestTrue(TEXT("The palette contract found entries to check"), Entries.Num() > 20))
	{
		return false;
	}

	int32 DynMeshSelectCount = 0;
	int32 GCSelectCount = 0;
	int32 FractureCount = 0;
	int32 BuilderCount = 0;
	bool bFoundRealizeBuilders = false;

	for (const FEntry& Entry : Entries)
	{
		const FString Where = FString::Printf(TEXT("%s (%s)"), *Entry.Label, *Entry.ClassName);
		const bool bIsFracture = Entry.Package == TEXT("/Script/PCGUtilsFracture");

		// "DynMesh" returns every DynMesh element.
		if (!bIsFracture && !IsDynMeshFamilyExempt(Entry.ClassName))
		{
			TestTrue(*FString::Printf(TEXT("'DynMesh' finds %s"), *Where), Matches(Entry.SearchText, TEXT("DynMesh")));
		}

		// "GC" returns every PCGUtilsFracture element.
		if (bIsFracture)
		{
			++FractureCount;
			TestTrue(*FString::Printf(TEXT("'GC' finds %s"), *Where), Matches(Entry.SearchText, TEXT("GC")));
			TestTrue(*FString::Printf(TEXT("%s carries the GC| prefix"), *Where), Entry.Label.StartsWith(TEXT("GC | ")));
		}

		if (IsDynMeshSelect(Entry))
		{
			++DynMeshSelectCount;
			TestTrue(*FString::Printf(TEXT("'Select' finds %s"), *Where), Matches(Entry.SearchText, TEXT("Select")));
			TestTrue(*FString::Printf(TEXT("'DynMesh Select' finds %s"), *Where),
				Matches(Entry.SearchText, TEXT("DynMesh Select")));
			// A DynMesh selection must never answer a "GC Select" search.
			TestFalse(*FString::Printf(TEXT("'GC Select' excludes %s"), *Where),
				Matches(Entry.SearchText, TEXT("GC Select")));

			const FString Name = Entry.Label.RightChop(FString(TEXT("Select | ")).Len()).ToLower();
			TestFalse(*FString::Printf(TEXT("%s name omits select/selection/selector"), *Where),
				Name.Contains(TEXT("select")));
		}

		if (IsGCSelect(Entry))
		{
			++GCSelectCount;
			TestTrue(*FString::Printf(TEXT("'Select' finds %s"), *Where), Matches(Entry.SearchText, TEXT("Select")));
			TestTrue(*FString::Printf(TEXT("'GC Select' finds %s"), *Where),
				Matches(Entry.SearchText, TEXT("GC Select")));
			// A GC selection must never answer a "DynMesh Select" search.
			TestFalse(*FString::Printf(TEXT("'DynMesh Select' excludes %s"), *Where),
				Matches(Entry.SearchText, TEXT("DynMesh Select")));

			const FString Name = Entry.Label.RightChop(FString(TEXT("GC | Select | ")).Len()).ToLower();
			TestFalse(*FString::Printf(TEXT("%s name omits select/selection/selector"), *Where),
				Name.Contains(TEXT("select")));
			TestFalse(*FString::Printf(TEXT("%s name omits GC"), *Where), Name.Contains(TEXT("gc")));
		}

		// "Builder" and "Build" return the basic Builders and the node that realizes them.
		if (Entry.Label.StartsWith(TEXT("Builder | ")))
		{
			++BuilderCount;
			TestTrue(*FString::Printf(TEXT("'Builder' finds %s"), *Where), Matches(Entry.SearchText, TEXT("Builder")));
			TestTrue(*FString::Printf(TEXT("'Build' finds %s"), *Where), Matches(Entry.SearchText, TEXT("Build")));
		}
		if (Entry.ClassName == TEXT("PCGDynMeshRealizeBuildersSettings"))
		{
			bFoundRealizeBuilders = true;
			TestTrue(TEXT("'Builder' finds Realize Builders"), Matches(Entry.SearchText, TEXT("Builder")));
			TestTrue(TEXT("'Build' finds Realize Builders"), Matches(Entry.SearchText, TEXT("Build")));
			TestEqual(TEXT("Realize Builders keeps its agreed title"), Entry.Label,
				FString(TEXT("DynMesh | Realize Builders")));
		}

		// A family prefix means nothing if the process name repeats the family.
		if (Entry.Label.StartsWith(TEXT("DynMesh | ")))
		{
			const FString Name = Entry.Label.RightChop(FString(TEXT("DynMesh | ")).Len()).ToLower();
			TestFalse(*FString::Printf(TEXT("%s name omits DynMesh"), *Where), Name.Contains(TEXT("dynmesh")));
			TestFalse(*FString::Printf(TEXT("%s name omits DynamicMesh"), *Where),
				Name.Contains(TEXT("dynamicmesh")));
		}
	}

	// The withdrawn Create Primitive node must not be reachable from the menu any more.
	TestFalse(TEXT("Create Primitive is no longer exposed"),
		Entries.ContainsByPredicate([](const FEntry& Entry)
		{
			return Entry.ClassName == TEXT("PCGCreatePrimitiveSettings");
		}));

	TestTrue(TEXT("Realize Builders is exposed"), bFoundRealizeBuilders);
	TestEqual(TEXT("Every basic Builder is present"), BuilderCount, 11);
	TestTrue(TEXT("DynMesh selections were found"), DynMeshSelectCount > 5);
	TestTrue(TEXT("GC selections were found"), GCSelectCount > 5);
	TestTrue(TEXT("Fracture elements were found"), FractureCount > 5);
	return true;
}

#endif
