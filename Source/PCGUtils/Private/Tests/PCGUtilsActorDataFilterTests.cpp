// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/PCGMarkerComponent.h"
#include "Components/PCGSplineComponent.h"
#include "Elements/PCGGetMarkerData.h"
#include "Elements/PCGGetPathData.h"
#include "Elements/PCGGetPCGSplineData.h"
#include "Elements/PCGGetPointData.h"
#include "Elements/PCGGetShapePathData.h"
#include "GameFramework/Actor.h"
#include "ShapePath/ShapePathComponent.h"

namespace PCGUtilsActorDataFilterTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	template <typename ElementType>
	struct TActorDataFilterReader : ElementType
	{
		using ElementType::ProcessActor;
	};

	template <typename ComponentType>
	ComponentType* AddFilterTestComponent(AActor* Actor, FName Name, FName Tag)
	{
		ComponentType* Component = NewObject<ComponentType>(Actor, Name);
		Actor->AddOwnedComponent(Component);
		Component->ComponentTags.Add(Tag);
		if constexpr (TIsDerivedFrom<ComponentType, UPCGSplineComponent>::Value)
		{
			Component->ClearSplinePoints(false);
			Component->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
			Component->AddSplinePoint(FVector(100, 0, 0), ESplineCoordinateSpace::Local, false);
			Component->UpdateSpline();
		}
		return Component;
	}

	/** Exercise actual reader output, including the context-only list populated by From Input. */
	template <typename SettingsType, typename ElementType, typename ComponentType>
	bool CheckComponentFilters(FAutomationTestBase& Test)
	{
		AActor* Actor = NewObject<AActor>(GetTransientPackage());
		Actor->Tags.Add(TEXT("FilterTestActor"));
		ComponentType* Selected = AddFilterTestComponent<ComponentType>(Actor, TEXT("SelectedComponent"), TEXT("Selected"));
		ComponentType* Other = AddFilterTestComponent<ComponentType>(Actor, TEXT("OtherComponent"), TEXT("Other"));
		AActor* UnrelatedActor = NewObject<AActor>(GetTransientPackage());
		ComponentType* Unrelated = AddFilterTestComponent<ComponentType>(UnrelatedActor, TEXT("UnrelatedComponent"), TEXT("Selected"));
		SettingsType* Settings = NewObject<SettingsType>();
		// Keep the saved settings unfiltered. Execution-only selection must still constrain the result.
		Settings->ComponentSelector = FPCGComponentSelectorSettings();
		TActorDataFilterReader<ElementType> Reader;
		bool bPassed = true;
		auto Check = [&](const TCHAR* Description, const FPCGComponentSelectorSettings& Selector, int32 Expected,
			FName ExpectedTag = NAME_None)
		{
			FPCGDataFromActorContext Context;
			Context.ComponentSelector = Selector;
			Reader.ProcessActor(&Context, Settings, Actor);
			bPassed &= Test.TestEqual(Description, Context.OutputData.TaggedData.Num(), Expected);
			for (const FPCGTaggedData& Output : Context.OutputData.TaggedData)
			{
				bPassed &= Test.TestNotNull(FString(Description) + TEXT(": output data exists"), Output.Data.Get());
				bPassed &= Test.TestTrue(FString(Description) + TEXT(": actor tags survive"), Output.Tags.Contains(TEXT("FilterTestActor")));
				if (!ExpectedTag.IsNone())
				{
					bPassed &= Test.TestTrue(FString(Description) + TEXT(": only the selected component contributes"),
						Output.Tags.Contains(ExpectedTag.ToString()));
				}
			}
		};

		Check(TEXT("No filter reads both eligible components"), FPCGComponentSelectorSettings(), 2);
		FPCGComponentSelectorSettings ByTag;
		ByTag.ComponentSelection = EPCGComponentSelection::ByTag;
		ByTag.ComponentSelectionTag = TEXT("Selected");
		Check(TEXT("Component tag excludes its sibling"), ByTag, 1, TEXT("Selected"));
		ByTag.ComponentSelectionTag = TEXT("Missing");
		Check(TEXT("A missing tag emits no fallback data"), ByTag, 0);
		ByTag.ComponentSelectionTag = TEXT("Selec*");
		Check(TEXT("Wildcard component tag is honored"), ByTag, 1, TEXT("Selected"));

		FPCGComponentSelectorSettings ByClass;
		ByClass.ComponentSelection = EPCGComponentSelection::ByClass;
		ByClass.ComponentSelectionClass = UBoxComponent::StaticClass();
		Check(TEXT("An unrelated component class emits nothing"), ByClass, 0);
		ByClass.ComponentSelectionClass = ComponentType::StaticClass();
		Check(TEXT("A matching component class reads both"), ByClass, 2);

		FPCGComponentSelectorSettings FromInput;
		FromInput.ComponentList.Add(Selected);
		Check(TEXT("From Input selects the referenced component"), FromInput, 1, TEXT("Selected"));
		FromInput.ComponentList.Reset();
		FromInput.ComponentList.Add(Other);
		Check(TEXT("From Input component reference does not read its sibling"), FromInput, 1, TEXT("Other"));
		FromInput.ComponentList = {Unrelated};
		Check(TEXT("From Input component from another actor does not widen the query"), FromInput, 0);
		FromInput.ComponentList = {Other};
		FromInput.ComponentSelectionTag = TEXT("Selected");
		Check(TEXT("From Input list and tag filter intersect"), FromInput, 0);

		FPCGDataFromActorContext InvalidActorContext;
		Reader.ProcessActor(&InvalidActorContext, Settings, nullptr);
		bPassed &= Test.TestEqual(TEXT("An invalid actor emits nothing"), InvalidActorContext.OutputData.TaggedData.Num(), 0);
		return bPassed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsMarkerComponentFilterTest,
	"PCGUtils.ActorData.ComponentFiltering.Markers", PCGUtilsActorDataFilterTests::Flags)
bool FPCGUtilsMarkerComponentFilterTest::RunTest(const FString&)
{
	return PCGUtilsActorDataFilterTests::CheckComponentFilters<UPCGGetMarkerDataSettings, FPCGGetMarkerDataElement, UPCGMarkerComponent>(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsPointProviderComponentFilterTest,
	"PCGUtils.ActorData.ComponentFiltering.PointProviders", PCGUtilsActorDataFilterTests::Flags)
bool FPCGUtilsPointProviderComponentFilterTest::RunTest(const FString&)
{
	return PCGUtilsActorDataFilterTests::CheckComponentFilters<UPCGGetPointDataSettings, FPCGGetPointDataElement, UPCGMarkerComponent>(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsPathProviderComponentFilterTest,
	"PCGUtils.ActorData.ComponentFiltering.PathProviders", PCGUtilsActorDataFilterTests::Flags)
bool FPCGUtilsPathProviderComponentFilterTest::RunTest(const FString&)
{
	return PCGUtilsActorDataFilterTests::CheckComponentFilters<UPCGGetPathDataSettings, FPCGGetPathElement, UPCGSplineComponent>(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsShapePathComponentFilterTest,
	"PCGUtils.ActorData.ComponentFiltering.ShapePaths", PCGUtilsActorDataFilterTests::Flags)
bool FPCGUtilsShapePathComponentFilterTest::RunTest(const FString&)
{
	return PCGUtilsActorDataFilterTests::CheckComponentFilters<UPCGGetShapePathSettings, FPCGGetShapePathElement, UShapePathComponent>(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsSplineComponentFilterTest,
	"PCGUtils.ActorData.ComponentFiltering.PCGSplines", PCGUtilsActorDataFilterTests::Flags)
bool FPCGUtilsSplineComponentFilterTest::RunTest(const FString&)
{
	return PCGUtilsActorDataFilterTests::CheckComponentFilters<UPCGGetPCGSplineDataSettings, FPCGGetPCGSplineDataElement, UPCGSplineComponent>(*this);
}

#endif
