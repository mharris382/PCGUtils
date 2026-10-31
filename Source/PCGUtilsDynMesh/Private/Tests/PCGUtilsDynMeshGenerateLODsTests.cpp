// Copyright Max Harris
//
// DynMesh | Generate LODs: output count and order, LOD numbering, the High Quality Source output, Automatic mode,
// @Data-driven values, group attribute, metadata preservation, and that parallel output matches serial output.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Topology/PCGDynMeshGenerateLODs.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshGenerateLODsTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** A box of 6 * Segments^2 * 2 triangles. */
	UPCGDynamicMeshData* MakeBox(int32 Segments, double OffsetX = 0.0)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		const int32 Steps = Segments + 1;
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(),
			FGeometryScriptPrimitiveOptions(), FTransform(FVector(OffsetX, 0.0, 0.0)), 100.0, 100.0, 100.0,
			Steps, Steps, Steps, EGeometryScriptPrimitiveOriginMode::Center);
		return Data;
	}

	template<typename T>
	void WriteData(UPCGData* Data, FName Name, const T& Value)
	{
		FPCGMetadataDomain* Domain = Data->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
		FPCGMetadataAttribute<T>* Attribute = Domain->FindOrCreateAttribute<T>(Name, Value, false, false, true);
		if (Domain->GetItemCountForChild() == 0)
		{
			Domain->AddEntry();
		}
		Attribute->SetValue(PCGFirstEntryKey, Value);
	}

	template<typename T>
	bool ReadData(const UPCGData* Data, FName Name, T& OutValue)
	{
		const UPCGMetadata* Metadata = Data ? Data->ConstMetadata() : nullptr;
		const FPCGMetadataAttribute<T>* Attribute = Metadata
			? Metadata->GetConstTypedAttribute<T>(FPCGAttributeIdentifier(Name, PCGMetadataDomainID::Data)) : nullptr;
		if (!Attribute)
		{
			return false;
		}
		OutValue = Attribute->GetValueFromItemKey(PCGFirstEntryKey);
		return true;
	}

	int32 Triangles(const UPCGData* Data)
	{
		const UPCGDynamicMeshData* MeshData = Cast<const UPCGDynamicMeshData>(Data);
		return MeshData && MeshData->GetDynamicMesh() ? MeshData->GetDynamicMesh()->GetTriangleCount() : -1;
	}

	TArray<FPCGTaggedData> Run(UPCGDynMeshGenerateLODsSettings* Settings, const TArray<const UPCGData*>& Inputs)
	{
		FPCGDataCollection Input;
		for (const UPCGData* Data : Inputs)
		{
			FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Pin = PCGPinConstants::DefaultInputLabel;
			Tagged.Data = Data;
			Tagged.Tags.Add(TEXT("SourceTag"));
		}
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get()))
			{
				return Context->OutputData.TaggedData;
			}
		}
		return {};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshGenerateLODsListTest,
	"PCGUtils.DynMesh.GenerateLODs.List",
	PCGUtilsDynMeshGenerateLODsTests::Flags)

bool FPCGUtilsDynMeshGenerateLODsListTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshGenerateLODsTests;
	constexpr int32 Source = 6 * 20 * 20 * 2;

	UPCGDynMeshGenerateLODsSettings* Settings = NewObject<UPCGDynMeshGenerateLODsSettings>();
	TestEqual(TEXT("Three LODs by default"), Settings->LODs.Num(), 3);
	TestEqual(TEXT("LOD 1 defaults its attribute to LOD1Ratio"), Settings->LODs[1].Value.Attribute.GetAttributeName(), FName(TEXT("LOD1Ratio")));

	UPCGDynamicMeshData* Box = MakeBox(20);
	WriteData<FString>(Box, TEXT("Kept"), TEXT("yes"));
	TestEqual(TEXT("The test box has the expected triangle count"), Triangles(Box), Source);

	const TArray<FPCGTaggedData> Outputs = Run(Settings, {Box});
	if (!TestEqual(TEXT("One output per LOD"), Outputs.Num(), 3))
	{
		return false;
	}
	const int32 Expected[3] = {Source, Source / 2, FMath::RoundToInt32(Source * 0.2)};
	for (int32 LOD = 0; LOD < 3; ++LOD)
	{
		int32 LODValue = INDEX_NONE;
		TestTrue(*FString::Printf(TEXT("LOD %d carries @Data.LOD"), LOD), ReadData<int32>(Outputs[LOD].Data, TEXT("LOD"), LODValue));
		TestEqual(*FString::Printf(TEXT("Output %d is LOD %d"), LOD, LOD), LODValue, LOD);
		TestTrue(*FString::Printf(TEXT("LOD %d has at most its target triangles (%d, target %d)"), LOD, Triangles(Outputs[LOD].Data), Expected[LOD]),
			Triangles(Outputs[LOD].Data) <= Expected[LOD] + 2);
		TestTrue(*FString::Printf(TEXT("LOD %d is not over-reduced (%d, target %d)"), LOD, Triangles(Outputs[LOD].Data), Expected[LOD]),
			Triangles(Outputs[LOD].Data) >= Expected[LOD] * 9 / 10);
		FString Kept;
		TestTrue(*FString::Printf(TEXT("LOD %d keeps the input's @Data"), LOD), ReadData<FString>(Outputs[LOD].Data, TEXT("Kept"), Kept) && Kept == TEXT("yes"));
		TestTrue(*FString::Printf(TEXT("LOD %d keeps the input's tags"), LOD), Outputs[LOD].Tags.Contains(TEXT("SourceTag")));
		int32 Group = INDEX_NONE;
		TestTrue(*FString::Printf(TEXT("LOD %d gets the input index as its group"), LOD), ReadData<int32>(Outputs[LOD].Data, TEXT("SourceMesh"), Group) && Group == 0);
		TestTrue(*FString::Printf(TEXT("LOD %d is a new data, not the input"), LOD), Outputs[LOD].Data != Box);
	}
	TestEqual(TEXT("The input mesh is untouched"), Triangles(Box), Source);
	bool bUnused = false;
	TestFalse(TEXT("No High Quality Source attribute without the source output"), ReadData<bool>(Outputs[0].Data, TEXT("IsHighQualitySource"), bUnused));

	// High Quality Source: one more output, first, unchanged.
	Settings->bOutputHighQualitySource = true;
	const TArray<FPCGTaggedData> WithSource = Run(Settings, {Box});
	if (TestEqual(TEXT("High Quality Source adds one output"), WithSource.Num(), 4))
	{
		bool bIsSource = false;
		int32 LODValue = 0;
		TestTrue(TEXT("The source output is marked"), ReadData<bool>(WithSource[0].Data, TEXT("IsHighQualitySource"), bIsSource) && bIsSource);
		TestTrue(TEXT("The source output's LOD is -1"), ReadData<int32>(WithSource[0].Data, TEXT("LOD"), LODValue) && LODValue == -1);
		TestEqual(TEXT("The source output is unchanged"), Triangles(WithSource[0].Data), Source);
		TestTrue(TEXT("LOD outputs are marked false"), ReadData<bool>(WithSource[2].Data, TEXT("IsHighQualitySource"), bIsSource) && !bIsSource);
		TestTrue(TEXT("The first LOD after the source is LOD 0"), ReadData<int32>(WithSource[1].Data, TEXT("LOD"), LODValue) && LODValue == 0);
	}

	// An existing group value is kept.
	Settings->bOutputHighQualitySource = false;
	UPCGDynamicMeshData* Named = MakeBox(4);
	WriteData<FString>(Named, TEXT("SourceMesh"), TEXT("/Game/Rock"));
	const TArray<FPCGTaggedData> NamedOutputs = Run(Settings, {Named});
	FString GroupValue;
	TestTrue(TEXT("An input's existing SourceMesh is kept"), NamedOutputs.Num() == 3
		&& ReadData<FString>(NamedOutputs[2].Data, TEXT("SourceMesh"), GroupValue) && GroupValue == TEXT("/Game/Rock"));

	// Max Triangle Count metric: a source below the cap is kept.
	Settings->Metric = EPCGUtilsDynMeshLODMetric::MaxTriangleCount;
	Settings->LODs = {FPCGUtilsDynMeshLODLevel(100000.0, TEXT("A")), FPCGUtilsDynMeshLODLevel(1000.0, TEXT("B"))};
	const TArray<FPCGTaggedData> Capped = Run(Settings, {Box});
	if (TestEqual(TEXT("Max Triangle Count: one output per entry"), Capped.Num(), 2))
	{
		TestEqual(TEXT("A cap above the source keeps it"), Triangles(Capped[0].Data), Source);
		TestTrue(TEXT("A cap below the source is met"), Triangles(Capped[1].Data) <= 1002);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshGenerateLODsAutomaticTest,
	"PCGUtils.DynMesh.GenerateLODs.Automatic",
	PCGUtilsDynMeshGenerateLODsTests::Flags)

bool FPCGUtilsDynMeshGenerateLODsAutomaticTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshGenerateLODsTests;
	constexpr int32 Source = 6 * 20 * 20 * 2;

	UPCGDynMeshGenerateLODsSettings* Settings = NewObject<UPCGDynMeshGenerateLODsSettings>();
	Settings->Mode = EPCGUtilsDynMeshLODMode::Automatic;
	Settings->LODCount = 4;
	Settings->LOD0Target = EPCGUtilsDynMeshLOD0Target::MaxTriangleCount;
	Settings->LOD0MaxTriangleCount.Constant = 2400;
	Settings->ReductionPerLOD.Constant = 0.5;

	const TArray<FPCGTaggedData> Outputs = Run(Settings, {MakeBox(20)});
	if (!TestEqual(TEXT("LOD Count outputs"), Outputs.Num(), 4))
	{
		return false;
	}
	int32 Target = 2400;
	for (int32 LOD = 0; LOD < 4; ++LOD, Target /= 2)
	{
		const int32 Count = Triangles(Outputs[LOD].Data);
		TestTrue(*FString::Printf(TEXT("LOD %d halves the one before (%d, target %d)"), LOD, Count, Target),
			Count <= Target + 2 && Count >= Target * 9 / 10);
	}

	// Decimation ratio read from @Data.
	Settings->LOD0Target = EPCGUtilsDynMeshLOD0Target::DecimationRatio;
	Settings->LOD0DecimationRatio.Input = EPCGUtilsInputValueType::Attribute;
	Settings->LODCount = 2;
	UPCGDynamicMeshData* Box = MakeBox(20);
	WriteData<double>(Box, TEXT("LOD0Ratio"), 0.75);
	const TArray<FPCGTaggedData> FromData = Run(Settings, {Box});
	if (TestEqual(TEXT("@Data ratio: LOD Count outputs"), FromData.Num(), 2))
	{
		TestTrue(TEXT("LOD 0 removes the @Data ratio"), Triangles(FromData[0].Data) <= Source / 4 + 2);
		TestTrue(TEXT("LOD 1 halves LOD 0"), Triangles(FromData[1].Data) <= Source / 8 + 2);
	}

	// A missing @Data attribute skips that input; the other still gets its LODs.
	UPCGDynamicMeshData* Missing = MakeBox(4);
	AddExpectedError(TEXT("LOD 0 Decimation Ratio"), EAutomationExpectedErrorFlags::Contains, 0);
	const TArray<FPCGTaggedData> Mixed = Run(Settings, {Missing, Box});
	TestEqual(TEXT("Only the input with the attribute gives LODs"), Mixed.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshGenerateLODsValidationTest,
	"PCGUtils.DynMesh.GenerateLODs.Validation",
	PCGUtilsDynMeshGenerateLODsTests::Flags)

bool FPCGUtilsDynMeshGenerateLODsValidationTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshGenerateLODsTests;

	UPCGDynMeshGenerateLODsSettings* Settings = NewObject<UPCGDynMeshGenerateLODsSettings>();
	Settings->LODs[1].Value.Constant = 1.5;
	AddExpectedError(TEXT("LODs[1]: the decimation ratio is 1.5"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("A ratio above 1 skips the input"), Run(Settings, {MakeBox(4)}).Num(), 0);

	Settings->LODs[1].Value.Constant = 0.5;
	Settings->GroupAttribute = TEXT("LOD");
	AddExpectedError(TEXT("Group Attribute and LOD Attribute are both 'LOD'"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("Colliding attribute names make nothing"), Run(Settings, {MakeBox(4)}).Num(), 0);

	Settings->GroupAttribute = TEXT("SourceMesh");
	Settings->LODs = {FPCGUtilsDynMeshLODLevel(0.8, TEXT("A")), FPCGUtilsDynMeshLODLevel(0.2, TEXT("B"))};
	AddExpectedError(TEXT("LOD 1 has"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("A LOD finer than the one before still outputs, with a warning"), Run(Settings, {MakeBox(10)}).Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshGenerateLODsParallelTest,
	"PCGUtils.DynMesh.GenerateLODs.ParallelMatchesSerial",
	PCGUtilsDynMeshGenerateLODsTests::Flags)

bool FPCGUtilsDynMeshGenerateLODsParallelTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshGenerateLODsTests;

	TArray<const UPCGData*> Boxes;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Boxes.Add(MakeBox(6 + Index * 3, 300.0 * Index));
	}

	UPCGDynMeshGenerateLODsSettings* Settings = NewObject<UPCGDynMeshGenerateLODsSettings>();
	Settings->bOutputHighQualitySource = true;

	const int32 RunsBefore = PCGUtilsParallelInputs::GetParallelRunCount();
	const TArray<FPCGTaggedData> Parallel = Run(Settings, Boxes);
	TestEqual(TEXT("The default run went parallel"), PCGUtilsParallelInputs::GetParallelRunCount() - RunsBefore, 1);

	Settings->bDisableParallel = true;
	const int32 SerialBefore = PCGUtilsParallelInputs::GetParallelRunCount();
	const TArray<FPCGTaggedData> Serial = Run(Settings, Boxes);
	TestEqual(TEXT("Disable Parallel stays serial"), PCGUtilsParallelInputs::GetParallelRunCount() - SerialBefore, 0);

	if (!TestEqual(TEXT("Same output count"), Parallel.Num(), Serial.Num()) || !TestEqual(TEXT("Four outputs per input"), Parallel.Num(), 20))
	{
		return false;
	}
	for (int32 Index = 0; Index < Parallel.Num(); ++Index)
	{
		const UE::Geometry::FDynamicMesh3& A = Cast<UPCGDynamicMeshData>(Parallel[Index].Data)->GetDynamicMesh()->GetMeshRef();
		const UE::Geometry::FDynamicMesh3& B = Cast<UPCGDynamicMeshData>(Serial[Index].Data)->GetDynamicMesh()->GetMeshRef();
		TestEqual(*FString::Printf(TEXT("Output %d: same triangle count"), Index), A.TriangleCount(), B.TriangleCount());
		TestEqual(*FString::Printf(TEXT("Output %d: same vertex count"), Index), A.VertexCount(), B.VertexCount());
		bool bSamePositions = A.MaxVertexID() == B.MaxVertexID();
		for (int32 Vid = 0; bSamePositions && Vid < A.MaxVertexID(); ++Vid)
		{
			bSamePositions = A.IsVertex(Vid) == B.IsVertex(Vid) && (!A.IsVertex(Vid) || A.GetVertex(Vid) == B.GetVertex(Vid));
		}
		TestTrue(*FString::Printf(TEXT("Output %d: same vertex positions"), Index), bSamePositions);
		int32 LODA = 0, LODB = 0;
		ReadData<int32>(Parallel[Index].Data, TEXT("LOD"), LODA);
		ReadData<int32>(Serial[Index].Data, TEXT("LOD"), LODB);
		TestEqual(*FString::Printf(TEXT("Output %d: same LOD"), Index), LODA, LODB);
	}
	return true;
}

#endif
