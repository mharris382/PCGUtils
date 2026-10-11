// Copyright Max Harris
//
// A node whose output is its input, modified, must emit that input: its metadata comes through, @Data included.
// These nodes copy the mesh so they never touch upstream data, and the copy used to be a bare new dataset, which
// dropped every attribute for every process node at once.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Topology/PCGDeleteDynamicMeshSelection.h"
#include "Elements/Topology/PCGDynMeshBoolean.h"
#include "Elements/Topology/PCGSeparateSelection.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "Selections/GeometrySelection.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshMetadataPreservedTests
{
	/** A box carrying an int32 on the data itself, written the way PCG's own nodes write one: as the first entry. */
	UPCGDynamicMeshData* BoxWithDataAttribute(FName AttributeName, int32 Value, FVector Center = FVector::ZeroVector)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(),
			FGeometryScriptPrimitiveOptions(), FTransform(Center), 100.0, 100.0, 100.0, 0, 0, 0,
			EGeometryScriptPrimitiveOriginMode::Center);

		FPCGMetadataDomain* Domain = Data->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
		FPCGMetadataAttribute<int32>* Attribute = Domain->FindOrCreateAttribute<int32>(AttributeName, 0, false, false, true);
		Domain->AddEntry();
		Attribute->SetValue(PCGFirstEntryKey, Value);
		return Data;
	}

	/** The int32 on the data itself, or INDEX_NONE when the data does not have the attribute. */
	int32 ReadDataAttribute(const UPCGData* Data, FName AttributeName)
	{
		const UPCGDynamicMeshSelectionData* Selection = Cast<const UPCGDynamicMeshSelectionData>(Data);
		const UPCGData* MeshData = Selection ? static_cast<const UPCGData*>(Selection->GetSourceMeshData()) : Data;
		const UPCGMetadata* Metadata = MeshData ? MeshData->ConstMetadata() : nullptr;
		const FPCGMetadataAttribute<int32>* Attribute = Metadata
			? Metadata->GetConstTypedAttribute<int32>(FPCGAttributeIdentifier(AttributeName, PCGMetadataDomainID::Data)) : nullptr;
		return Attribute ? Attribute->GetValueFromItemKey(PCGFirstEntryKey) : INDEX_NONE;
	}

	UPCGDynamicMeshSelectionData* SelectTriangles(const UPCGDynamicMeshData* MeshData, TArrayView<const int32> TriangleIDs)
	{
		UE::Geometry::FGeometrySelection Selection;
		Selection.InitializeTypes(UE::Geometry::EGeometryElementType::Face, UE::Geometry::EGeometryTopologyType::Triangle);
		for (const int32 TriangleID : TriangleIDs)
		{
			Selection.Selection.Add(UE::Geometry::FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
		}
		UPCGDynamicMeshSelectionData* Data = NewObject<UPCGDynamicMeshSelectionData>();
		Data->Initialize(MeshData, MoveTemp(Selection));
		return Data;
	}

	TArray<FPCGTaggedData> Run(UPCGSettings* Settings, TArray<TPair<FName, const UPCGData*>> Inputs)
	{
		FPCGDataCollection Input;
		for (const TPair<FName, const UPCGData*>& Pair : Inputs)
		{
			FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Pin = Pair.Key;
			Tagged.Data = Pair.Value;
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

	const FPCGTaggedData* OnPin(const TArray<FPCGTaggedData>& Outputs, FName Pin)
	{
		return Outputs.FindByPredicate([Pin](const FPCGTaggedData& Output) { return Output.Pin == Pin; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshMetadataPreservedTest,
	"PCGUtils.DynMesh.Process.OutputKeepsTheInputsDataAttributes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshMetadataPreservedTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshMetadataPreservedTests;

	// The shared process base, through a node that changes the mesh: Delete Selection.
	{
		UPCGDynamicMeshData* Box = BoxWithDataAttribute(TEXT("HoleIndex"), 7);
		const int32 TrianglesBefore = Box->GetDynamicMesh()->GetMeshRef().TriangleCount();
		const int32 SelectedTriangles[] = { 0, 1 };
		UPCGDeleteDynamicMeshSelectionSettings* Settings = NewObject<UPCGDeleteDynamicMeshSelectionSettings>();
		const TArray<FPCGTaggedData> Outputs = Run(Settings, { { Settings->AllInputPinProperties()[0].Label, SelectTriangles(Box, SelectedTriangles) } });
		UTEST_EQUAL(TEXT("Delete Selection gives one output"), Outputs.Num(), 1);
		const UPCGDynamicMeshData* Output = Cast<UPCGDynamicMeshData>(Outputs[0].Data);
		UTEST_NOT_NULL(TEXT("Delete Selection outputs a mesh"), Output);
		TestEqual(TEXT("Delete Selection changed the mesh"), Output->GetDynamicMesh()->GetMeshRef().TriangleCount(), TrianglesBefore - 2);
		TestTrue(TEXT("Delete Selection did not touch its input"), Output != Box && Box->GetDynamicMesh()->GetMeshRef().TriangleCount() == TrianglesBefore);
		TestEqual(TEXT("Delete Selection's output keeps the input's @Data attribute"), ReadDataAttribute(Output, TEXT("HoleIndex")), 7);
	}

	// With a selection asked for as the output, the mesh under that selection keeps them too.
	{
		UPCGDynamicMeshData* Box = BoxWithDataAttribute(TEXT("HoleIndex"), 7);
		const int32 SelectedTriangles[] = { 0, 1 };
		UPCGDeleteDynamicMeshSelectionSettings* Settings = NewObject<UPCGDeleteDynamicMeshSelectionSettings>();
		Settings->bOutputSelectionData = true;
		const TArray<FPCGTaggedData> Outputs = Run(Settings, { { Settings->AllInputPinProperties()[0].Label, SelectTriangles(Box, SelectedTriangles) } });
		UTEST_EQUAL(TEXT("Delete Selection with selection output gives one output"), Outputs.Num(), 1);
		TestEqual(TEXT("The mesh under an output selection keeps the input's @Data attribute"), ReadDataAttribute(Outputs[0].Data, TEXT("HoleIndex")), 7);
	}

	// A node that splits its input in two: both halves are the input.
	{
		UPCGDynamicMeshData* Box = BoxWithDataAttribute(TEXT("HoleIndex"), 7);
		const int32 SelectedTriangles[] = { 0, 1, 2, 3 };
		UPCGSeparateSelectionSettings* Settings = NewObject<UPCGSeparateSelectionSettings>();
		const TArray<FPCGTaggedData> Outputs = Run(Settings, { { Settings->AllInputPinProperties()[0].Label, SelectTriangles(Box, SelectedTriangles) } });
		const FPCGTaggedData* Selected = OnPin(Outputs, TEXT("Selected"));
		const FPCGTaggedData* Unselected = OnPin(Outputs, TEXT("Unselected"));
		UTEST_TRUE(TEXT("Separate Selection gives both halves"), Selected && Unselected);
		TestEqual(TEXT("Separate Selection's selected half keeps the input's @Data attribute"), ReadDataAttribute(Selected->Data, TEXT("HoleIndex")), 7);
		TestEqual(TEXT("Separate Selection's unselected half keeps the input's @Data attribute"), ReadDataAttribute(Unselected->Data, TEXT("HoleIndex")), 7);
	}

	// A node with two inputs and two outputs: each output keeps the attributes of the input it is what is left of.
	{
		UPCGDynamicMeshData* A = BoxWithDataAttribute(TEXT("FromA"), 11);
		UPCGDynamicMeshData* B = BoxWithDataAttribute(TEXT("FromB"), 22, FVector(50.0, 0.0, 0.0));
		UPCGDynMeshBooleanSettings* Settings = NewObject<UPCGDynMeshBooleanSettings>();
		Settings->BooleanOperation = EGeometryScriptBooleanOperation::Subtract;
		Settings->bSeparateOperandContributions = true;
		const TArray<FPCGTaggedData> Outputs = Run(Settings, { { TEXT("InA"), A }, { TEXT("InB"), B } });
		const FPCGTaggedData* OutA = OnPin(Outputs, PCGDynMeshBooleanConstants::OutputContributionPinA);
		const FPCGTaggedData* OutB = OnPin(Outputs, PCGDynMeshBooleanConstants::OutputContributionPinB);
		UTEST_TRUE(TEXT("A separated Boolean gives both parts"), OutA && OutB);
		TestEqual(TEXT("Out A keeps InA's @Data attribute"), ReadDataAttribute(OutA->Data, TEXT("FromA")), 11);
		TestEqual(TEXT("Out B keeps InB's @Data attribute"), ReadDataAttribute(OutB->Data, TEXT("FromB")), 22);
		TestEqual(TEXT("Out B does not take InA's @Data attribute"), ReadDataAttribute(OutB->Data, TEXT("FromA")), static_cast<int32>(INDEX_NONE));
	}

	return true;
}

#endif
