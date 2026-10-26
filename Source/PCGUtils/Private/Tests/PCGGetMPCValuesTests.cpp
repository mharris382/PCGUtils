// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGGetMPCValues.h"
#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "PCGParamData.h"

namespace PCGGetMPCValuesTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Scalars Wetness = 0.25, Season = 2; vector Wind = (1, 2, 3, 4). */
	UMaterialParameterCollection* MakeCollection()
	{
		UMaterialParameterCollection* Collection = NewObject<UMaterialParameterCollection>(GetTransientPackage(), TEXT("MPC_Test"), RF_Transient);
		FCollectionScalarParameter& Wetness = Collection->ScalarParameters.AddDefaulted_GetRef();
		Wetness.ParameterName = TEXT("Wetness");
		Wetness.DefaultValue = 0.25f;
		FCollectionScalarParameter& Season = Collection->ScalarParameters.AddDefaulted_GetRef();
		Season.ParameterName = TEXT("Season");
		Season.DefaultValue = 2.0f;
		FCollectionVectorParameter& Wind = Collection->VectorParameters.AddDefaulted_GetRef();
		Wind.ParameterName = TEXT("Wind");
		Wind.DefaultValue = FLinearColor(1, 2, 3, 4);
		return Collection;
	}

	UPCGGetMPCValuesSettings* MakeSettings(UMaterialParameterCollection* Collection)
	{
		UPCGGetMPCValuesSettings* Settings = NewObject<UPCGGetMPCValuesSettings>();
		Settings->Collection = Collection;
		return Settings;
	}

	void AddRead(UPCGGetMPCValuesSettings* Settings, FName Parameter, FName Attribute,
		EPCGUtilsMPCVectorOutput Output = EPCGUtilsMPCVectorOutput::Vector4)
	{
		Settings->Parameters.Add({Parameter, Attribute, Output});
	}

	template <typename T>
	bool ReadAttribute(const UPCGParamData* Param, FName Name, T& OutValue)
	{
		const FPCGMetadataAttribute<T>* Attribute = Param ? Param->ConstMetadata()->GetConstTypedAttribute<T>(Name) : nullptr;
		if (!Attribute)
		{
			return false;
		}
		OutValue = Attribute->GetValueFromItemKey(0);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGGetMPCValuesDefaultsTest, "PCGUtils.Params.GetMPCValues.Defaults", PCGGetMPCValuesTests::Flags)
bool FPCGGetMPCValuesDefaultsTest::RunTest(const FString&)
{
	using namespace PCGGetMPCValuesTests;
	UMaterialParameterCollection* Collection = MakeCollection();
	UPCGGetMPCValuesSettings* Settings = MakeSettings(Collection);
	AddRead(Settings, TEXT("Wetness"), TEXT("Wet"));
	AddRead(Settings, TEXT("Wind"), TEXT("Wind4"), EPCGUtilsMPCVectorOutput::Vector4);
	AddRead(Settings, TEXT("Wind"), TEXT("Wind3"), EPCGUtilsMPCVectorOutput::Vector);
	AddRead(Settings, TEXT("Wind"), TEXT("Wind2"), EPCGUtilsMPCVectorOutput::Vector2);
	AddRead(Settings, TEXT("Wind"), TEXT("WindB"), EPCGUtilsMPCVectorOutput::B);
	AddRead(Settings, TEXT("Season"), TEXT("Season"), EPCGUtilsMPCVectorOutput::Vector);

	const UPCGParamData* Param = FPCGGetMPCValuesElement::ReadValues(nullptr, *Settings, Collection, nullptr);
	if (!TestNotNull(TEXT("Attribute set produced"), Param))
	{
		return false;
	}
	TestEqual(TEXT("One entry"), Param->ConstMetadata()->GetItemCountForChild(), int64(1));
	TestEqual(TEXT("One attribute per entry"), Param->ConstMetadata()->GetAttributeCount(), 6);

	double Wet = 0, WindB = 0, Season = 0;
	FVector4 Wind4;
	FVector Wind3;
	FVector2D Wind2;
	TestTrue(TEXT("Scalar is a double"), ReadAttribute(Param, TEXT("Wet"), Wet));
	TestEqual(TEXT("Scalar default"), Wet, 0.25);
	TestTrue(TEXT("Vector4"), ReadAttribute(Param, TEXT("Wind4"), Wind4));
	TestEqual(TEXT("Vector4 value"), Wind4, FVector4(1, 2, 3, 4));
	TestTrue(TEXT("Vector"), ReadAttribute(Param, TEXT("Wind3"), Wind3));
	TestEqual(TEXT("Vector value"), Wind3, FVector(1, 2, 3));
	TestTrue(TEXT("Vector2"), ReadAttribute(Param, TEXT("Wind2"), Wind2));
	TestEqual(TEXT("Vector2 value"), Wind2, FVector2D(1, 2));
	TestTrue(TEXT("Channel is a double"), ReadAttribute(Param, TEXT("WindB"), WindB));
	TestEqual(TEXT("Channel B value"), WindB, 3.0);
	TestTrue(TEXT("Vector Output is ignored for scalars"), ReadAttribute(Param, TEXT("Season"), Season));
	TestEqual(TEXT("Second scalar"), Season, 2.0);

	TestTrue(TEXT("Defaults are cacheable"), FPCGGetMPCValuesElement().IsCacheable(Settings));
	TestEqual(TEXT("Subtitle is the collection"), Settings->GetAdditionalTitleInformation(), FString(TEXT("MPC_Test")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGGetMPCValuesValidationTest, "PCGUtils.Params.GetMPCValues.Validation", PCGGetMPCValuesTests::Flags)
bool FPCGGetMPCValuesValidationTest::RunTest(const FString&)
{
	using namespace PCGGetMPCValuesTests;
	UMaterialParameterCollection* Collection = MakeCollection();
	UPCGGetMPCValuesSettings* Settings = MakeSettings(Collection);
	AddRead(Settings, TEXT("Wetness"), TEXT("Value"));
	AddRead(Settings, TEXT("Missing"), TEXT("Other"));
	AddRead(Settings, NAME_None, TEXT("Third"));
	AddRead(Settings, TEXT("Season"), NAME_None);
	AddRead(Settings, TEXT("Wind"), TEXT("Value"));

	AddExpectedError(TEXT("Parameters[1]: Parameter 'Missing' is not a scalar or vector parameter"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("Parameters[2]: Parameter is None"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("Parameters[3]: Attribute Name is None"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("Parameters[4]: Attribute Name 'Value' is already written by Parameters[0]"), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestNull(TEXT("Invalid settings produce no output"), FPCGGetMPCValuesElement::ReadValues(nullptr, *Settings, Collection, nullptr));

	Settings->Parameters.Reset();
	Settings->Source = EPCGUtilsMPCValueSource::World;
	AddRead(Settings, TEXT("Wetness"), TEXT("Wet"));
	AddExpectedError(TEXT("Source is World, but this graph is not running in a world"), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestNull(TEXT("World source without a world fails"), FPCGGetMPCValuesElement::ReadValues(nullptr, *Settings, Collection, nullptr));
	TestFalse(TEXT("World is not cacheable"), FPCGGetMPCValuesElement().IsCacheable(Settings));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGGetMPCValuesWorldTest, "PCGUtils.Params.GetMPCValues.World", PCGGetMPCValuesTests::Flags)
bool FPCGGetMPCValuesWorldTest::RunTest(const FString&)
{
	using namespace PCGGetMPCValuesTests;
	UMaterialParameterCollection* Collection = MakeCollection();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UPCGGetMPCValuesSettings* Settings = MakeSettings(Collection);
	Settings->Source = EPCGUtilsMPCValueSource::World;
	AddRead(Settings, TEXT("Wetness"), TEXT("Wet"));
	AddRead(Settings, TEXT("Wind"), TEXT("Wind"));

	if (!World->GetParameterCollectionInstance(Collection))
	{
		World->AddParameterCollectionInstance(Collection, false);
	}
	UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection);
	if (!TestNotNull(TEXT("World has an instance"), Instance))
	{
		return false;
	}
	Instance->SetScalarParameterValue(TEXT("Wetness"), 0.75f);
	Instance->SetVectorParameterValue(TEXT("Wind"), FLinearColor(5, 6, 7, 8));

	const UPCGParamData* Param = FPCGGetMPCValuesElement::ReadValues(nullptr, *Settings, Collection, World);
	double Wet = 0;
	FVector4 Wind;
	TestTrue(TEXT("Scalar read"), ReadAttribute(Param, TEXT("Wet"), Wet));
	TestEqual(TEXT("World scalar value, not the default"), Wet, 0.75);
	TestTrue(TEXT("Vector read"), ReadAttribute(Param, TEXT("Wind"), Wind));
	TestEqual(TEXT("World vector value, not the default"), Wind, FVector4(5, 6, 7, 8));

	Settings->Source = EPCGUtilsMPCValueSource::Defaults;
	Param = FPCGGetMPCValuesElement::ReadValues(nullptr, *Settings, Collection, World);
	TestTrue(TEXT("Defaults read"), ReadAttribute(Param, TEXT("Wet"), Wet));
	TestEqual(TEXT("Defaults ignore the world"), Wet, 0.25);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGGetMPCValuesEditorTest, "PCGUtils.Params.GetMPCValues.EditorHelpers", PCGGetMPCValuesTests::Flags)
bool FPCGGetMPCValuesEditorTest::RunTest(const FString&)
{
	using namespace PCGGetMPCValuesTests;
	UMaterialParameterCollection* Collection = MakeCollection();
	UPCGGetMPCValuesSettings* Settings = MakeSettings(Collection);

	TestEqual(TEXT("Dropdown lists scalars then vectors"), Settings->GetMPCParameterNameOptions(),
		TArray<FString>{TEXT("Wetness"), TEXT("Season"), TEXT("Wind")});

	AddRead(Settings, TEXT("Season"), TEXT("CurrentSeason"));
	Settings->AddAllParameters();
	if (!TestEqual(TEXT("Add All adds only the missing parameters"), Settings->Parameters.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("Existing entry keeps its attribute name"), Settings->Parameters[0].AttributeName, FName(TEXT("CurrentSeason")));
	TestEqual(TEXT("Added entry writes the parameter's name"), Settings->Parameters[1].AttributeName, FName(TEXT("Wetness")));

	AddRead(Settings, TEXT("Wind"), NAME_None);
	FPropertyChangedEvent Event(FindFProperty<FProperty>(UPCGGetMPCValuesSettings::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPCGGetMPCValuesSettings, Parameters)));
	Settings->PostEditChangeProperty(Event);
	TestEqual(TEXT("An empty attribute name is filled from the parameter"), Settings->Parameters.Last().AttributeName, FName(TEXT("Wind")));
	return true;
}
#endif

#endif
