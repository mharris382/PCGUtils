#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Elements/PCGPathNeighborAttributes.h"
#include "PCGContext.h"
#include "Data/PCGPointArrayData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

namespace PathNeighborAttributesTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

class FExecute : public FPCGUtilsPathNeighborAttributesElement
{
public:
	using FPCGUtilsPathNeighborAttributesElement::ExecuteInternal;
};

const TCHAR* GraphA = TEXT("/Game/PCG/Barrier_A.Barrier_A");
const TCHAR* GraphB = TEXT("/Game/PCG/Barrier_B.Barrier_B");

/** Five points: Barrier (soft path) A A B B A, Id 0..4, X = 100 * index, plus @Data.IsClosed when bAddClosed. */
UPCGPointArrayData* MakePath(bool bAddClosed, bool bClosed)
{
	auto* Points = NewObject<UPCGPointArrayData>();
	Points->SetNumPoints(5);
	Points->AllocateProperties(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::MetadataEntry);
	auto* Elements = Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
	auto* Barrier = Elements->CreateAttribute<FSoftObjectPath>(TEXT("BarrierGeneration"), FSoftObjectPath(), false);
	auto* Id = Elements->CreateAttribute<int32>(TEXT("Id"), -7, false);
	if (bAddClosed)
	{
		auto* DataDomain = Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Data);
		auto* Closed = DataDomain->CreateAttribute<bool>(TEXT("IsClosed"), false, false);
		Closed->SetValue(DataDomain->AddEntry(), bClosed);
	}
	const TCHAR* Graphs[5] = {GraphA, GraphA, GraphB, GraphB, GraphA};
	auto Keys = Points->GetMetadataEntryValueRange();
	auto Transforms = Points->GetTransformValueRange();
	for (int32 I = 0; I < 5; ++I)
	{
		Keys[I] = Elements->AddEntry();
		Barrier->SetValue(Keys[I], FSoftObjectPath(Graphs[I]));
		Id->SetValue(Keys[I], I);
		Transforms[I] = FTransform(FVector(100.0 * I, 0, 0));
	}
	return Points;
}

FPCGUtilsPathNeighborAttributeEntry Entry(const TCHAR* Source, int32 Offset, const TCHAR* Output)
{
	FPCGUtilsPathNeighborAttributeEntry Result;
	Result.Source.Update(Source);
	Result.Offset = Offset;
	Result.OutputAttributeName = Output;
	return Result;
}

const UPCGPointArrayData* Run(FAutomationTestBase& Test, UPCGUtilsPathNeighborAttributesSettings* Settings, UPCGData* Input, FPCGContext& Context)
{
	Context.InputData.TaggedData.Emplace_GetRef().Data = Settings;
	auto& In = Context.InputData.TaggedData.Emplace_GetRef();
	In.Data = Input; In.Pin = PCGPinConstants::DefaultInputLabel; In.Tags.Add(TEXT("PreservedTag"));
	FExecute().ExecuteInternal(&Context);
	if (!Test.TestEqual(TEXT("One output"), Context.OutputData.TaggedData.Num(), 1)) return nullptr;
	Test.TestTrue(TEXT("Tag preserved"), Context.OutputData.TaggedData[0].Tags.Contains(TEXT("PreservedTag")));
	return Cast<UPCGPointArrayData>(Context.OutputData.TaggedData[0].Data);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsPathNeighborResolveTest, "PCGUtils.Path.NeighborAttributes.ResolveIndex", PathNeighborAttributesTests::Flags)
bool FPCGUtilsPathNeighborResolveTest::RunTest(const FString&)
{
	using PCGUtilsPathNeighbor::ResolveIndex;
	bool bHas = false;
	TestEqual(TEXT("Open middle +1"), ResolveIndex(1, 5, 1, false, EPCGUtilsPathNeighborEndMode::Clamp, bHas), 2); TestTrue(TEXT("has"), bHas);
	TestEqual(TEXT("Open last +1 clamp"), ResolveIndex(4, 5, 1, false, EPCGUtilsPathNeighborEndMode::Clamp, bHas), 4); TestFalse(TEXT("no neighbor"), bHas);
	TestEqual(TEXT("Open last +2 clamp reads last"), ResolveIndex(4, 5, 2, false, EPCGUtilsPathNeighborEndMode::Clamp, bHas), 4);
	TestEqual(TEXT("Open 3 +2 clamp reads last"), ResolveIndex(3, 5, 2, false, EPCGUtilsPathNeighborEndMode::Clamp, bHas), 4);
	TestEqual(TEXT("Open 3 +2 own value"), ResolveIndex(3, 5, 2, false, EPCGUtilsPathNeighborEndMode::OwnValue, bHas), 3);
	TestEqual(TEXT("Open first -1 default"), ResolveIndex(0, 5, -1, false, EPCGUtilsPathNeighborEndMode::AttributeDefault, bHas), INDEX_NONE); TestFalse(TEXT("no neighbor"), bHas);
	TestEqual(TEXT("Closed last +1 wraps"), ResolveIndex(4, 5, 1, true, EPCGUtilsPathNeighborEndMode::Clamp, bHas), 0); TestTrue(TEXT("closed has"), bHas);
	TestEqual(TEXT("Closed first -1 wraps"), ResolveIndex(0, 5, -1, true, EPCGUtilsPathNeighborEndMode::Clamp, bHas), 4);
	TestEqual(TEXT("Closed offset beyond size"), ResolveIndex(1, 5, -12, true, EPCGUtilsPathNeighborEndMode::Clamp, bHas), 4);
	TestEqual(TEXT("Single closed point"), ResolveIndex(0, 1, 1, true, EPCGUtilsPathNeighborEndMode::Clamp, bHas), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsPathNeighborOpenTest, "PCGUtils.Path.NeighborAttributes.OpenPathSoftObjectPath", PathNeighborAttributesTests::Flags)
bool FPCGUtilsPathNeighborOpenTest::RunTest(const FString&)
{
	using namespace PathNeighborAttributesTests;
	auto* Settings = NewObject<UPCGUtilsPathNeighborAttributesSettings>();
	auto Next = Entry(TEXT("BarrierGeneration"), 1, TEXT("NextBarrierGeneration"));
	Next.bOutputHasNeighbor = true;
	Next.HasNeighborAttributeName = TEXT("HasNext");
	Settings->Attributes = {Next, Entry(TEXT("$Position"), -1, TEXT("PrevPosition"))};

	FPCGContext Context;
	const UPCGPointArrayData* Out = Run(*this, Settings, MakePath(true, false), Context);
	if (!TestNotNull(TEXT("Point output"), Out)) return false;

	const auto* NextAttr = Out->Metadata->GetConstTypedAttribute<FSoftObjectPath>(TEXT("NextBarrierGeneration"));
	const auto* HasNext = Out->Metadata->GetConstTypedAttribute<bool>(TEXT("HasNext"));
	const auto* Prev = Out->Metadata->GetConstTypedAttribute<FVector>(TEXT("PrevPosition"));
	const auto* Id = Out->Metadata->GetConstTypedAttribute<int32>(TEXT("Id"));
	const auto* Closed = Out->Metadata->GetConstTypedAttribute<bool>(FPCGAttributeIdentifier(TEXT("IsClosed"), PCGMetadataDomainID::Data));
	if (!TestNotNull(TEXT("Next written as soft path"), NextAttr) || !TestNotNull(TEXT("HasNext written"), HasNext)
		|| !TestNotNull(TEXT("PrevPosition written"), Prev) || !TestNotNull(TEXT("Id kept"), Id) || !TestNotNull(TEXT("@Data kept"), Closed)) return false;
	TestFalse(TEXT("Soft path output does not interpolate"), NextAttr->AllowsInterpolation());

	// Own Value (default End Mode): the last point reads itself, so a split on 'current != next' does not fire there.
	const TCHAR* ExpectedNext[5] = {GraphA, GraphB, GraphB, GraphA, GraphA};
	const auto Keys = Out->GetConstMetadataEntryValueRange();
	for (int32 I = 0; I < 5; ++I)
	{
		TestEqual(FString::Printf(TEXT("Next point %d"), I), NextAttr->GetValueFromItemKey(Keys[I]), FSoftObjectPath(ExpectedNext[I]));
		TestEqual(FString::Printf(TEXT("HasNext point %d"), I), HasNext->GetValueFromItemKey(Keys[I]), I < 4);
		TestEqual(FString::Printf(TEXT("PrevPosition point %d"), I), Prev->GetValueFromItemKey(Keys[I]), FVector(100.0 * FMath::Max(I - 1, 0), 0, 0));
		TestEqual(FString::Printf(TEXT("Id point %d"), I), Id->GetValueFromItemKey(Keys[I]), I);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsPathNeighborClosedTest, "PCGUtils.Path.NeighborAttributes.ClosedWrapsAndDefaults", PathNeighborAttributesTests::Flags)
bool FPCGUtilsPathNeighborClosedTest::RunTest(const FString&)
{
	using namespace PathNeighborAttributesTests;
	{
		auto* Settings = NewObject<UPCGUtilsPathNeighborAttributesSettings>();
		Settings->Attributes = {Entry(TEXT("Id"), 1, TEXT("NextId"))};
		FPCGContext Context;
		const UPCGPointArrayData* Out = Run(*this, Settings, MakePath(true, true), Context);
		const auto* NextId = Out ? Out->Metadata->GetConstTypedAttribute<int32>(TEXT("NextId")) : nullptr;
		if (!TestNotNull(TEXT("Closed NextId"), NextId)) return false;
		const auto Keys = Out->GetConstMetadataEntryValueRange();
		TestEqual(TEXT("Closed last wraps to 0"), NextId->GetValueFromItemKey(Keys[4]), 0);
		TestEqual(TEXT("Closed middle"), NextId->GetValueFromItemKey(Keys[2]), 3);
	}
	{
		// No IsClosed attribute: open. Attribute Default end mode writes the source attribute's default (-7).
		auto* Settings = NewObject<UPCGUtilsPathNeighborAttributesSettings>();
		Settings->EndMode = EPCGUtilsPathNeighborEndMode::AttributeDefault;
		Settings->Attributes = {Entry(TEXT("Id"), -1, TEXT("PrevId"))};
		FPCGContext Context;
		const UPCGPointArrayData* Out = Run(*this, Settings, MakePath(false, false), Context);
		const auto* PrevId = Out ? Out->Metadata->GetConstTypedAttribute<int32>(TEXT("PrevId")) : nullptr;
		if (!TestNotNull(TEXT("Open PrevId"), PrevId)) return false;
		const auto Keys = Out->GetConstMetadataEntryValueRange();
		TestEqual(TEXT("First point gets the default"), PrevId->GetValueFromItemKey(Keys[0]), -7);
		TestEqual(TEXT("Second point reads the first"), PrevId->GetValueFromItemKey(Keys[1]), 0);
	}
	{
		// In-place shift: output equals source.
		auto* Settings = NewObject<UPCGUtilsPathNeighborAttributesSettings>();
		Settings->ClosedMode = EPCGUtilsPathClosedMode::Closed;
		Settings->Attributes = {Entry(TEXT("Id"), 2, TEXT("Id"))};
		FPCGContext Context;
		const UPCGPointArrayData* Out = Run(*this, Settings, MakePath(false, false), Context);
		const auto* IdAttr = Out ? Out->Metadata->GetConstTypedAttribute<int32>(TEXT("Id")) : nullptr;
		if (!TestNotNull(TEXT("Shifted Id"), IdAttr)) return false;
		const auto Keys = Out->GetConstMetadataEntryValueRange();
		for (int32 I = 0; I < 5; ++I)
		{
			TestEqual(FString::Printf(TEXT("Shifted Id point %d"), I), IdAttr->GetValueFromItemKey(Keys[I]), (I + 2) % 5);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsPathNeighborErrorsTest, "PCGUtils.Path.NeighborAttributes.Errors", PathNeighborAttributesTests::Flags)
bool FPCGUtilsPathNeighborErrorsTest::RunTest(const FString&)
{
	using namespace PathNeighborAttributesTests;
	{
		AddExpectedError(TEXT("Source 'Missing' is not an attribute or property"), EAutomationExpectedErrorFlags::Contains, 1, false);
		auto* Settings = NewObject<UPCGUtilsPathNeighborAttributesSettings>();
		Settings->Attributes = {Entry(TEXT("Missing"), 1, TEXT("Out"))};
		FPCGContext Context;
		Context.InputData.TaggedData.Emplace_GetRef().Data = Settings;
		auto& In = Context.InputData.TaggedData.Emplace_GetRef();
		In.Data = MakePath(false, false); In.Pin = PCGPinConstants::DefaultInputLabel;
		FExecute().ExecuteInternal(&Context);
		TestEqual(TEXT("Skipped input"), Context.OutputData.TaggedData.Num(), 0);
	}
	{
		AddExpectedError(TEXT("Attributes[0] has Output Attribute Name None"), EAutomationExpectedErrorFlags::Contains, 1, false);
		auto* Settings = NewObject<UPCGUtilsPathNeighborAttributesSettings>();
		Settings->Attributes = {Entry(TEXT("Id"), 1, TEXT(""))};
		Settings->Attributes[0].OutputAttributeName = NAME_None;
		FPCGContext Context;
		Context.InputData.TaggedData.Emplace_GetRef().Data = Settings;
		auto& In = Context.InputData.TaggedData.Emplace_GetRef();
		In.Data = MakePath(false, false); In.Pin = PCGPinConstants::DefaultInputLabel;
		FExecute().ExecuteInternal(&Context);
		TestEqual(TEXT("Nothing output"), Context.OutputData.TaggedData.Num(), 0);
	}
	return true;
}

#endif
