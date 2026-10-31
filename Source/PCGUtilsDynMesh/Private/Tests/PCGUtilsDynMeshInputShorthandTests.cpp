// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Data/PCGPointArrayData.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Creation/CreatePrimitive/PCGCreatePrimitiveSettingsBase.h"
#include "Elements/Creation/PrimitiveBuilder/PCGPrimitiveBuilderFactory.h"
#include "Elements/Selections/PCGDynMeshNormalSelectionFactory.h"
#include "Elements/Topology/PCGBevelEdges.h"
#include "Elements/Topology/PCGDynMeshExtrude.h"
#include "Elements/Topology/PCGDynMeshInset.h"
#include "Factories/PCGUtilsDynMeshBuilderFactory.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "Settings/PCGUtilsDynMeshSettings.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGSettings.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshInputShorthandTests
{
	UPCGDynamicMeshData* MakeBox()
	{
		auto* Box = NewObject<UPCGCreatePrimitiveBoxSettings>();
		Box->DimensionX = Box->DimensionY = Box->DimensionZ = 100;
		Box->Origin = EGeometryScriptPrimitiveOriginMode::Center;
		auto* Builder = NewObject<UPCGPrimitiveBuilderFactoryData>();
		Builder->Primitive = Box;
		Builder->Fitting.ScaleToFit.ScaleToFitMode = EPCGUtilsFitMode::None;
		FPCGUtilsDynMeshBuildResult Result;
		auto Operation = Builder->CreateOperation(nullptr);
		FPCGUtilsDynMeshBuildContext Context;
		if (!Operation || !Operation->Build(Context, Result) || !Result.IsValid()) { return nullptr; }
		// Own a fresh data object so each test input carries only the @Data attributes it is given.
		auto* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(UE::Geometry::FDynamicMesh3(*Result.MeshData->GetDynamicMesh()->GetMeshPtr()));
		return Data;
	}

	UPCGPrimitiveBuilderFactoryData* MakeBoxBuilder()
	{
		auto* Box = NewObject<UPCGCreatePrimitiveBoxSettings>();
		Box->DimensionX = Box->DimensionY = Box->DimensionZ = 100;
		Box->Origin = EGeometryScriptPrimitiveOriginMode::Center;
		auto* Builder = NewObject<UPCGPrimitiveBuilderFactoryData>();
		Builder->Primitive = Box;
		Builder->Fitting.ScaleToFit.ScaleToFitMode = EPCGUtilsFitMode::None;
		return Builder;
	}

	/** Writes one @Data value. Call with bAddEntry on the first attribute written to a data object only. */
	template <typename T>
	void SetDataValue(UPCGData* Data, FName Name, const T& Value, bool bAddEntry = true)
	{
		FPCGMetadataDomain* Domain = Data->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
		FPCGMetadataAttribute<T>* Attribute = Domain->FindOrCreateAttribute<T>(Name, T{}, false, false, true);
		if (bAddEntry) { Domain->AddEntry(); }
		Attribute->SetValue(PCGFirstEntryKey, Value);
	}

	FPCGAttributePropertyInputSelector Selector(const TCHAR* Text)
	{
		FPCGAttributePropertyInputSelector Result;
		Result.Update(FString(Text));
		return Result;
	}

	FPCGDataCollection Run(UPCGSettings* Settings, TArray<const UPCGData*> Inputs, const UPCGData* SelectorData,
		FName MainPin = TEXT("In"))
	{
		FPCGDataCollection Input;
		for (const UPCGData* Data : Inputs)
		{
			auto& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Data = Data;
			Tagged.Pin = MainPin;
		}
		if (SelectorData)
		{
			auto& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Data = SelectorData;
			Tagged.Pin = TEXT("Selector");
		}
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get())) { return Context->OutputData; }
		}
		return {};
	}

	double MaxZ(const UPCGData* Data)
	{
		const auto* Selection = Cast<UPCGDynamicMeshSelectionData>(Data);
		const auto* Mesh = Selection ? Selection->GetSourceMeshData() : Cast<UPCGDynamicMeshData>(Data);
		return Mesh ? Mesh->GetDynamicMesh()->GetMeshPtr()->GetBounds().Max.Z : -1.0;
	}

	/** Smallest half-extent (max of |X|, |Y|) among the vertices on the box's top (Z = 50): the inner ring. */
	double TopInnerHalfExtent(const UPCGData* Data)
	{
		const auto* Selection = Cast<UPCGDynamicMeshSelectionData>(Data);
		const auto* Mesh = Selection ? Selection->GetSourceMeshData() : Cast<UPCGDynamicMeshData>(Data);
		if (!Mesh) { return -1.0; }
		const UE::Geometry::FDynamicMesh3& M = *Mesh->GetDynamicMesh()->GetMeshPtr();
		double Result = TNumericLimits<double>::Max();
		for (const int32 ID : M.VertexIndicesItr())
		{
			const FVector3d P = M.GetVertex(ID);
			if (FMath::IsNearlyEqual(P.Z, 50.0, 0.01))
			{
				Result = FMath::Min(Result, FMath::Max(FMath::Abs(P.X), FMath::Abs(P.Y)));
			}
		}
		return Result;
	}

	TSet<FName> OverrideLabels(const UPCGSettings* Settings)
	{
		TSet<FName> Labels;
		for (const FPCGSettingsOverridableParam& Param : Settings->OverridableParams())
		{
			Labels.Add(Param.Label);
		}
		return Labels;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsInputShorthandReadTest,
	"PCGUtils.InputShorthands.DataDomainReads", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsInputShorthandReadTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshInputShorthandTests;
	UPCGDynamicMeshData* Mesh = MakeBox();
	if (!TestNotNull(TEXT("Box mesh builds"), Mesh)) { return false; }
	SetDataValue<double>(Mesh, TEXT("PathHeight"), 250.0);
	SetDataValue<int32>(Mesh, TEXT("Floors"), 3, false);
	SetDataValue<FString>(Mesh, TEXT("Label"), FString(TEXT("Room")), false);
	SetDataValue<FVector>(Mesh, TEXT("Size"), FVector(1, 2, 3), false);
	SetDataValue<double>(Mesh, TEXT("Negative"), -5.0, false);

	double Value = 0;
	FText Error;
	TestTrue(TEXT("Bare name reads @Data on DynMesh"), PCGUtilsInputShorthands::ReadDataDomainValue(Mesh, Selector(TEXT("PathHeight")), Value, Error));
	TestEqual(TEXT("Bare name value"), Value, 250.0);
	TestTrue(TEXT("@Data prefix accepted"), PCGUtilsInputShorthands::ReadDataDomainValue(Mesh, Selector(TEXT("@Data.PathHeight")), Value, Error));
	TestTrue(TEXT("Integer attribute reads as a double"), PCGUtilsInputShorthands::ReadDataDomainValue(Mesh, Selector(TEXT("Floors")), Value, Error));
	TestEqual(TEXT("Integer widened"), Value, 3.0);
	TestTrue(TEXT("Extractor reads one component"), PCGUtilsInputShorthands::ReadDataDomainValue(Mesh, Selector(TEXT("Size.Z")), Value, Error));
	TestEqual(TEXT("Extracted component"), Value, 3.0);

	TestFalse(TEXT("Missing attribute fails"), PCGUtilsInputShorthands::ReadDataDomainValue(Mesh, Selector(TEXT("RoomHeight")), Value, Error));
	TestTrue(TEXT("Missing attribute names itself"), Error.ToString().Contains(TEXT("'RoomHeight' was not found in the input's @Data domain")));
	TestFalse(TEXT("String attribute fails for a number"), PCGUtilsInputShorthands::ReadDataDomainValue(Mesh, Selector(TEXT("Label")), Value, Error));
	TestTrue(TEXT("Wrong type names found and expected types"), Error.ToString().Contains(TEXT("String")) && Error.ToString().Contains(TEXT("needs a number")));
	TestFalse(TEXT("Vector attribute fails for a number"), PCGUtilsInputShorthands::ReadDataDomainValue(Mesh, Selector(TEXT("Size")), Value, Error));
	TestFalse(TEXT("Property selector fails"), PCGUtilsInputShorthands::ReadDataDomainValue(Mesh, Selector(TEXT("$Position")), Value, Error));
	TestTrue(TEXT("Property selector is called out"), Error.ToString().Contains(TEXT("selects a property")));
	TestFalse(TEXT("No data fails"), PCGUtilsInputShorthands::ReadDataDomainValue(nullptr, Selector(TEXT("PathHeight")), Value, Error));

	int32 Integer = 0;
	TestFalse(TEXT("Double attribute does not truncate into an integer"), PCGUtilsInputShorthands::ReadDataDomainValue(Mesh, Selector(TEXT("PathHeight")), Integer, Error));
	TestTrue(TEXT("Integer target accepts an integer attribute"), PCGUtilsInputShorthands::ReadDataDomainValue(Mesh, Selector(TEXT("Floors")), Integer, Error) && Integer == 3);

	UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
	SetDataValue<double>(Points, TEXT("PathHeight"), 10.0);
	TestFalse(TEXT("Point data needs an explicit @Data prefix"), PCGUtilsInputShorthands::ReadDataDomainValue(Points, Selector(TEXT("PathHeight")), Value, Error));
	TestTrue(TEXT("Domain error suggests the prefix"), Error.ToString().Contains(TEXT("'@Data.PathHeight'")));
	TestTrue(TEXT("Explicit prefix reads point @Data"), PCGUtilsInputShorthands::ReadDataDomainValue(Points, Selector(TEXT("@Data.PathHeight")), Value, Error) && Value == 10.0);

	FPCGUtilsInputShorthandDoubleAbs Abs(4.0, TEXT("Negative"));
	TestTrue(TEXT("Constant mode returns the constant"), Abs.TryReadDataValue(Mesh, Value, Error) && Value == 4.0);
	Abs.Input = EPCGUtilsInputValueType::Attribute;
	TestFalse(TEXT("Ranged shorthand rejects an out-of-range @Data value"), Abs.TryReadDataValue(Mesh, Value, Error, FText::FromString(TEXT("Inset")), 2));
	TestTrue(TEXT("Range error names setting, input, value and constraint"),
		Error.ToString().StartsWith(TEXT("Inset (input 2):")) && Error.ToString().Contains(TEXT("-5")) && Error.ToString().Contains(TEXT("at least 0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsExtrudeDataDistanceTest,
	"PCGUtils.InputShorthands.ExtrudeReadsDataDistance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsExtrudeDataDistanceTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshInputShorthandTests;
	UPCGDynamicMeshData* Short = MakeBox();
	UPCGDynamicMeshData* Tall = MakeBox();
	UPCGDynamicMeshData* Missing = MakeBox();
	if (!TestTrue(TEXT("Boxes build"), Short && Tall && Missing)) { return false; }
	SetDataValue<double>(Short, TEXT("PathHeight"), 100.0);
	SetDataValue<float>(Tall, TEXT("PathHeight"), 250.0f);

	auto* Settings = NewObject<UPCGDynMeshExtrudeSettings>();
	Settings->Distance.Input = EPCGUtilsInputValueType::Attribute;
	Settings->Distance.Attribute = Selector(TEXT("PathHeight"));
	auto* Up = NewObject<UPCGDynMeshNormalSelectionFactoryData>();

	AddExpectedErrorPlain(TEXT("Distance (input 2): attribute 'PathHeight' was not found in the input's @Data domain"), EAutomationExpectedErrorFlags::Contains, 1);
	const FPCGDataCollection Output = Run(Settings, { Short, Tall, Missing }, Up);
	const TArray<FPCGTaggedData> Results = Output.GetInputsByPin(TEXT("Out"));
	if (!TestEqual(TEXT("The input without the attribute is skipped, not given the constant"), Results.Num(), 2)) { return false; }
	TestEqual(TEXT("Each input extrudes by its own @Data value (100)"), MaxZ(Results[0].Data), 150.0, 0.001);
	TestEqual(TEXT("Each input extrudes by its own @Data value (250, from a Float)"), MaxZ(Results[1].Data), 300.0, 0.001);

	const auto* OutMesh = Cast<UPCGDynamicMeshData>(Results[0].Data);
	const auto* OutSelection = Cast<UPCGDynamicMeshSelectionData>(Results[0].Data);
	const UPCGDynamicMeshData* OutData = OutSelection ? OutSelection->GetSourceMeshData() : OutMesh;
	const FPCGMetadataAttributeBase* Kept = OutData && OutData->ConstMetadata()
		? OutData->ConstMetadata()->GetConstAttribute(FPCGAttributeIdentifier(TEXT("PathHeight"), PCGMetadataDomainID::Data)) : nullptr;
	TestNotNull(TEXT("Output keeps the input's @Data"), Kept);

	TestFalse(TEXT("@Data mode does not accept Builders"), Settings->SupportsDeferredBuilderProcessing());
	AddExpectedErrorPlain(TEXT("a Builder has no @Data until it is realized"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("A Builder input is refused with an error"), Run(Settings, { MakeBoxBuilder() }, Up).GetInputsByPin(TEXT("Out")).Num(), 0);

	Settings->Distance.Input = EPCGUtilsInputValueType::Constant;
	Settings->Distance.Constant = 20.0;
	TestTrue(TEXT("Constant mode accepts Builders again"), Settings->SupportsDeferredBuilderProcessing());
	const TArray<FPCGTaggedData> ConstantResults = Run(Settings, { Missing }, Up).GetInputsByPin(TEXT("Out"));
	TestTrue(TEXT("Constant mode ignores @Data"), ConstantResults.Num() == 1 && FMath::IsNearlyEqual(MaxZ(ConstantResults[0].Data), 70.0, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsInputShorthandOverrideLabelTest,
	"PCGUtils.InputShorthands.OverridePinLabels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsInputShorthandOverrideLabelTest::RunTest(const FString&)
{
	auto* Settings = NewObject<UPCGDynMeshExtrudeSettings>();
	TSet<FName> Labels;
	for (const FPCGSettingsOverridableParam& Param : Settings->OverridableParams())
	{
		Labels.Add(Param.Label);
	}
	// These labels are what keep existing graphs wired: they match the pins the pre-shorthand node exposed.
	for (const TCHAR* Expected : { TEXT("Distance"), TEXT("DirectionMode"), TEXT("Direction"), TEXT("AreaMode"),
		TEXT("GroupMode"), TEXT("ConstantGroup"), TEXT("UVScale"), TEXT("bSolidsToShells") })
	{
		TestTrue(FString::Printf(TEXT("Override pin '%s' keeps its legacy label"), Expected), Labels.Contains(FName(Expected)));
	}
	TestTrue(TEXT("Attribute name is overridable through DistanceAttribute"), Labels.Contains(TEXT("DistanceAttribute")));
	TestFalse(TEXT("No bare Constant pin"), Labels.Contains(TEXT("Constant")));
	TestFalse(TEXT("Input mode is not overridable"), Labels.Contains(TEXT("Input")) || Labels.Contains(TEXT("DistanceInput")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsInsetDataDistanceTest,
	"PCGUtils.InputShorthands.InsetReadsDataDistance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsInsetDataDistanceTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshInputShorthandTests;
	UPCGDynamicMeshData* Small = MakeBox();
	UPCGDynamicMeshData* Large = MakeBox();
	UPCGDynamicMeshData* Missing = MakeBox();
	if (!TestTrue(TEXT("Boxes build"), Small && Large && Missing)) { return false; }
	SetDataValue<double>(Small, TEXT("InsetDistance"), 10.0);
	SetDataValue<int32>(Large, TEXT("InsetDistance"), 20);

	auto* Settings = NewObject<UPCGDynMeshInsetSettings>();
	Settings->Distance.Input = EPCGUtilsInputValueType::Attribute;
	auto* Up = NewObject<UPCGDynMeshNormalSelectionFactoryData>();

	AddExpectedErrorPlain(TEXT("Distance (input 2): attribute 'InsetDistance' was not found in the input's @Data domain"), EAutomationExpectedErrorFlags::Contains, 1);
	const TArray<FPCGTaggedData> Results = Run(Settings, { Small, Large, Missing }, Up).GetInputsByPin(TEXT("Out"));
	if (!TestEqual(TEXT("The input without the attribute is skipped, not given the constant"), Results.Num(), 2)) { return false; }
	TestEqual(TEXT("Inset by its own @Data value (10)"), TopInnerHalfExtent(Results[0].Data), 40.0, 0.01);
	TestEqual(TEXT("Inset by its own @Data value (20, from an Integer32)"), TopInnerHalfExtent(Results[1].Data), 30.0, 0.01);

	TestFalse(TEXT("@Data mode does not accept Builders"), Settings->SupportsDeferredBuilderProcessing());
	Settings->Distance.Input = EPCGUtilsInputValueType::Constant;
	Settings->Distance.Constant = 5.0;
	TestTrue(TEXT("Constant mode accepts Builders"), Settings->SupportsDeferredBuilderProcessing());
	const TArray<FPCGTaggedData> ConstantResults = Run(Settings, { Missing }, Up).GetInputsByPin(TEXT("Out"));
	TestTrue(TEXT("Constant mode ignores @Data"), ConstantResults.Num() == 1 && FMath::IsNearlyEqual(TopInnerHalfExtent(ConstantResults[0].Data), 45.0, 0.01));

	const TSet<FName> Labels = OverrideLabels(Settings);
	for (const TCHAR* Expected : { TEXT("Distance"), TEXT("bReproject"), TEXT("bBoundaryOnly"), TEXT("Softness"), TEXT("AreaScale"),
		TEXT("AreaMode"), TEXT("GroupMode"), TEXT("ConstantGroup"), TEXT("UVScale"), TEXT("DistanceAttribute") })
	{
		TestTrue(FString::Printf(TEXT("Inset override pin '%s' exists"), Expected), Labels.Contains(FName(Expected)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBevelDataDistanceTest,
	"PCGUtils.InputShorthands.BevelReadsDataDistance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsBevelDataDistanceTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshInputShorthandTests;
	UPCGDynamicMeshData* Small = MakeBox();
	UPCGDynamicMeshData* Large = MakeBox();
	UPCGDynamicMeshData* Negative = MakeBox();
	if (!TestTrue(TEXT("Boxes build"), Small && Large && Negative)) { return false; }
	SetDataValue<double>(Small, TEXT("BevelDistance"), 5.0);
	SetDataValue<float>(Large, TEXT("BevelDistance"), 10.0f);
	SetDataValue<double>(Negative, TEXT("BevelDistance"), -2.0);

	auto* Settings = NewObject<UPCGBevelEdgesSettings>();
	TestEqual(TEXT("Default constant is unchanged"), Settings->BevelDistance.Constant, 1.0);
	Settings->BevelDistance.Input = EPCGUtilsInputValueType::Attribute;

	AddExpectedErrorPlain(TEXT("Bevel Distance (input 2):"), EAutomationExpectedErrorFlags::Contains, 1);
	const TArray<FPCGTaggedData> Results = Run(Settings, { Small, Large, Negative }, nullptr, TEXT("Mesh")).GetInputsByPin(TEXT("Out"));
	if (!TestEqual(TEXT("The negative input is rejected, not clamped"), Results.Num(), 2)) { return false; }
	TestEqual(TEXT("Bevel by its own @Data value (5)"), TopInnerHalfExtent(Results[0].Data), 45.0, 0.01);
	TestEqual(TEXT("Bevel by its own @Data value (10, from a Float)"), TopInnerHalfExtent(Results[1].Data), 40.0, 0.01);

	TestFalse(TEXT("@Data mode does not accept Builders"), Settings->SupportsDeferredBuilderProcessing());
	AddExpectedErrorPlain(TEXT("a Builder has no @Data until it is realized"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("A Builder input is refused with an error"),
		Run(Settings, { MakeBoxBuilder() }, nullptr, TEXT("Mesh")).GetInputsByPin(TEXT("Out")).Num(), 0);

	const TSet<FName> Labels = OverrideLabels(Settings);
	TestTrue(TEXT("Bevel Distance keeps its legacy override pin"), Labels.Contains(TEXT("BevelDistance")));
	TestTrue(TEXT("Attribute name is overridable"), Labels.Contains(TEXT("BevelDistanceAttribute")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsConcreteOutputPinTypeTest,
	"PCGUtils.DynMesh.ConcreteOutputPinTypes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsConcreteOutputPinTypeTest::RunTest(const FString&)
{
	const FPCGDataTypeIdentifier MeshType(EPCGDataType::DynamicMesh);
	const FPCGDataTypeIdentifier SelectionType(UPCGDynamicMeshSelectionData::StaticClass());
	const FPCGDataTypeIdentifier BuilderType(FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId());
	UPCGUtilsDynMeshSettings* Options = GetMutableDefault<UPCGUtilsDynMeshSettings>();
	const bool bSaved = Options->bShowConcreteOutputPinTypes;
	Options->bShowConcreteOutputPinTypes = true;

	auto* Node = NewObject<UPCGNode>();
	auto* Settings = NewObject<UPCGDynMeshExtrudeSettings>(Node);
	Node->SetSettingsInterface(Settings);
	UPCGPin* Out = Node->GetOutputPin(TEXT("Out"));
	TestTrue(TEXT("Unconnected output shows Dynamic Mesh"), Out->GetCurrentTypesID() == MeshType);
	Settings->bOutputSelectionData = true;
	TestTrue(TEXT("Output Selection Data shows DynMesh Selection"), Out->GetCurrentTypesID() == SelectionType);
	Settings->bOutputSelectionData = false;
	Settings->Distance.Input = EPCGUtilsInputValueType::Attribute;
	TestTrue(TEXT("A node that cannot take Builders still shows Dynamic Mesh"), Out->GetCurrentTypesID() == MeshType);
	TestTrue(TEXT("Companion Result pin keeps its own type"),
		Node->GetOutputPin(TEXT("Result"))->GetCurrentTypesID() == FPCGDataTypeIdentifier(FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId()));
	Settings->Distance.Input = EPCGUtilsInputValueType::Constant;

	auto* Anchor = NewObject<UPCGNode>();
	Anchor->SetSettingsInterface(Anchor->GetSettings());
	Anchor->GetOutputPin(TEXT("Out"))->Properties.AllowedTypes = BuilderType;
	Node->GetInputPin(TEXT("In"))->AddEdgeTo(Anchor->GetOutputPin(TEXT("Out")));
	TestTrue(TEXT("A Builder input still turns the output into a Builder"), Out->GetCurrentTypesID() == BuilderType);

	Options->bShowConcreteOutputPinTypes = false;
	Node->GetInputPin(TEXT("In"))->BreakAllEdges();
	TestFalse(TEXT("Option off: unconnected output shows the full union again"), Out->GetCurrentTypesID() == MeshType);
	Options->bShowConcreteOutputPinTypes = bSaved;
	return true;
}

#endif
