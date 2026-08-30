// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Creation/CreatePrimitive/PCGCreatePrimitiveSettingsBase.h"
#include "Elements/Creation/PrimitiveBuilder/PCGPrimitiveBuilderFactory.h"
#include "Elements/Deform/PCGTransformDynMesh.h"
#include "Elements/Topology/PCGDynMeshBoolean.h"
#include "Factories/PCGUtilsDynMeshOperandProcessBuilderFactory.h"
#include "Factories/PCGUtilsDynMeshProcessBuilderFactory.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "PCGContext.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshBooleanTests
{
	UPCGDynamicMeshData* Box(FVector Center = FVector::ZeroVector, double Size = 100.0)
	{
		auto* Data = NewObject<UPCGDynamicMeshData>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(),
			FGeometryScriptPrimitiveOptions(), FTransform(Center), Size, Size, Size, 0, 0, 0,
			EGeometryScriptPrimitiveOriginMode::Center);
		return Data;
	}

	UPCGPrimitiveBuilderFactoryData* BoxBuilder(double Size)
	{
		auto* Primitive = NewObject<UPCGCreatePrimitiveBoxSettings>();
		Primitive->DimensionX = Primitive->DimensionY = Primitive->DimensionZ = Size;
		Primitive->Origin = EGeometryScriptPrimitiveOriginMode::Center;
		auto* Builder = NewObject<UPCGPrimitiveBuilderFactoryData>();
		Builder->Primitive = Primitive;
		Builder->Fitting.ScaleToFit.ScaleToFitMode = EPCGUtilsFitMode::None;
		return Builder;
	}

	FPCGTaggedData Tagged(const UPCGData* Data, const TCHAR* Tag)
	{
		FPCGTaggedData Result;
		Result.Data = Data;
		Result.Tags.Add(Tag);
		return Result;
	}

	TArray<FPCGTaggedData> Run(UPCGDynMeshBooleanSettings* Settings,
		TArray<FPCGTaggedData> A, TArray<FPCGTaggedData> B)
	{
		FPCGDataCollection Input;
		for (FPCGTaggedData& Data : A) { Data.Pin = TEXT("InA"); Input.TaggedData.Add(Data); }
		for (FPCGTaggedData& Data : B) { Data.Pin = TEXT("InB"); Input.TaggedData.Add(Data); }
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get())) { return Context->OutputData.TaggedData; }
		}
		return {};
	}

	const UE::Geometry::FDynamicMesh3& Mesh(const UPCGDynamicMeshData* Data)
	{
		return *Data->GetDynamicMesh()->GetMeshPtr();
	}

	bool SameMesh(const UPCGDynamicMeshData* A, const UPCGDynamicMeshData* B)
	{
		const auto& MA = Mesh(A);
		const auto& MB = Mesh(B);
		if (MA.VertexCount() != MB.VertexCount() || MA.TriangleCount() != MB.TriangleCount()) { return false; }
		for (int32 ID : MA.VertexIndicesItr())
		{
			if (!MB.IsVertex(ID) || !MA.GetVertex(ID).Equals(MB.GetVertex(ID), 0.001)) { return false; }
		}
		for (int32 ID : MA.TriangleIndicesItr())
		{
			if (!MB.IsTriangle(ID) || MA.GetTriangle(ID) != MB.GetTriangle(ID) ||
				MA.GetTriangleGroup(ID) != MB.GetTriangleGroup(ID)) { return false; }
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshBooleanPassthroughTest,
	"PCGUtils.DynMesh.Boolean.MissingOperandPassthrough", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshBooleanPassthroughTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBooleanTests;
	auto* Settings = NewObject<UPCGDynMeshBooleanSettings>();
	const UPCGData* Sources[] = { Box(), BoxBuilder(100.0) };
	for (const UPCGData* Source : Sources)
	{
		for (int32 Op = 0; Op <= static_cast<int32>(EGeometryScriptBooleanOperation::NewPolyGroupOutside); ++Op)
		{
			Settings->BooleanOperation = static_cast<EGeometryScriptBooleanOperation>(Op);
			for (int32 Mode = 0; Mode < 3; ++Mode)
			{
				Settings->Mode = static_cast<EPCGBooleanOperationMode>(Mode);
				Settings->TagInheritanceMode = EPCGBooleanOperationTagInheritanceMode::B;
				Settings->bSelfUnionOperand = Settings->bAssignOperandPolygroup = true;
				const auto Output = Run(Settings, { Tagged(Source, TEXT("Primary")), Tagged(Source, TEXT("Second")) }, {});
				TestEqual(TEXT("Missing operand emits every primary"), Output.Num(), 2);
				if (Output.Num() != 2) { return false; }
				TestTrue(TEXT("Passthrough retains object identity"), Output[0].Data == Source && Output[1].Data == Source);
				TestTrue(TEXT("Passthrough retains tags even in B-only tag mode"), Output[0].Tags.Contains(TEXT("Primary")));
				TestEqual(TEXT("Passthrough uses Out pin"), Output[0].Pin, FName(TEXT("Out")));
			}
		}
	}
	TestEqual(TEXT("No primary produces no output"), Run(Settings, {}, { Tagged(Box(), TEXT("Operand")) }).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshBooleanVanillaTest,
	"PCGUtils.DynMesh.Boolean.VanillaParityAndOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshBooleanVanillaTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBooleanTests;
	auto* A = Box();
	auto* B = Box(FVector(40.0, 15.0, 10.0), 80.0);
	auto* ACopy = CastChecked<UPCGDynamicMeshData>(A->DuplicateData(nullptr));
	auto* BCopy = CastChecked<UPCGDynamicMeshData>(B->DuplicateData(nullptr));
	auto* Settings = NewObject<UPCGDynMeshBooleanSettings>();
	Settings->BooleanOperationOptions.bAllowEmptyResult = true;
	for (int32 Op = 0; Op <= static_cast<int32>(EGeometryScriptBooleanOperation::NewPolyGroupOutside); ++Op)
	{
		Settings->BooleanOperation = static_cast<EGeometryScriptBooleanOperation>(Op);
		auto* Expected = CastChecked<UPCGDynamicMeshData>(A->DuplicateData(nullptr));
		UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(Expected->GetMutableDynamicMesh(), FTransform::Identity,
			B->GetMutableDynamicMesh(), FTransform::Identity, Settings->BooleanOperation, Settings->BooleanOperationOptions);
		const auto Output = Run(Settings, { Tagged(A, TEXT("A")) }, { Tagged(B, TEXT("B")) });
		if (!TestEqual(TEXT("Boolean emits one result"), Output.Num(), 1)) { return false; }
		const auto* Actual = CastChecked<UPCGDynamicMeshData>(Output[0].Data);
		TestTrue(TEXT("Geometry matches vanilla Geometry Script"), SameMesh(Expected, Actual));
		TestTrue(TEXT("Output has both tags"), Output[0].Tags.Contains(TEXT("A")) && Output[0].Tags.Contains(TEXT("B")));
		TestTrue(TEXT("Output owns a new mesh"), Actual != A);
	}
	TestTrue(TEXT("Primary unchanged"), SameMesh(A, ACopy));
	TestTrue(TEXT("Operand unchanged"), SameMesh(B, BCopy));
	// Present-but-empty is distinct from no data: intersection must run and can return empty.
	Settings->BooleanOperation = EGeometryScriptBooleanOperation::Intersection;
	const auto EmptyOutput = Run(Settings, { Tagged(A, TEXT("A")) }, { Tagged(NewObject<UPCGDynamicMeshData>(), TEXT("B")) });
	if (!TestEqual(TEXT("Empty operand still executes"), EmptyOutput.Num(), 1)) { return false; }
	TestEqual(TEXT("Intersection with empty mesh is empty"), Mesh(CastChecked<UPCGDynamicMeshData>(EmptyOutput[0].Data)).TriangleCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshBooleanPairingTest,
	"PCGUtils.DynMesh.Boolean.PairingAndTags", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshBooleanPairingTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBooleanTests;
	auto* Settings = NewObject<UPCGDynMeshBooleanSettings>();
	Settings->BooleanOperation = EGeometryScriptBooleanOperation::Union;
	const TArray<FPCGTaggedData> A { Tagged(Box(), TEXT("A1")), Tagged(Box(FVector(10, 0, 0)), TEXT("A2")) };
	const TArray<FPCGTaggedData> B { Tagged(Box(FVector(25, 0, 0)), TEXT("B1")), Tagged(Box(FVector(50, 0, 0)), TEXT("B2")) };
	for (int32 Mode = 0; Mode < 3; ++Mode)
	{
		Settings->Mode = static_cast<EPCGBooleanOperationMode>(Mode);
		for (int32 TagMode = 0; TagMode < 3; ++TagMode)
		{
			Settings->TagInheritanceMode = static_cast<EPCGBooleanOperationTagInheritanceMode>(TagMode);
			const auto Output = Run(Settings, A, B);
			if (!TestEqual(TEXT("Correct output count"), Output.Num(), Mode == 2 ? 4 : 2)) { return false; }
			for (int32 Index = 0; Index < Output.Num(); ++Index)
			{
				const int32 AI = Mode == 2 ? Index / 2 : Index;
				const int32 BI = Mode == 1 ? 1 : Index % 2;
				TestEqual(TEXT("Primary tag inheritance"), Output[Index].Tags.Contains(A[AI].Tags.Array()[0]), TagMode != 2);
				TestEqual(TEXT("Operand tag inheritance"), Output[Index].Tags.Contains(B[BI].Tags.Array()[0]), TagMode != 1);
				if (Mode == 1 && TagMode == 0) { TestTrue(TEXT("Sequential Both includes earlier operands"), Output[Index].Tags.Contains(TEXT("B1"))); }
			}
		}
	}
	Settings->Mode = EPCGBooleanOperationMode::EachAWithEachB;
	TestEqual(TEXT("1:N broadcast"), Run(Settings, { A[0] }, B).Num(), 2);
	TestEqual(TEXT("N:1 broadcast"), Run(Settings, A, { B[0] }).Num(), 2);
	AddExpectedError(TEXT("Pairwise DynMesh operations require"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("Mismatched pair counts rejected"), Run(Settings, A, { B[0], B[1], B[0] }).Num(), 0);
	AddExpectedError(TEXT("Selections and mixed types are not supported"), EAutomationExpectedErrorFlags::Contains, 2);
	TestEqual(TEXT("Mixed modes rejected"), Run(Settings, A, { Tagged(BoxBuilder(100), TEXT("B")) }).Num(), 0);
	auto* Selection = NewObject<UPCGDynamicMeshSelectionData>();
	UE::Geometry::FGeometrySelection EmptySelection;
	EmptySelection.InitializeTypes(UE::Geometry::EGeometryElementType::Face, UE::Geometry::EGeometryTopologyType::Triangle);
	Selection->Initialize(CastChecked<UPCGDynamicMeshData>(A[0].Data), MoveTemp(EmptySelection));
	TestEqual(TEXT("Selections rejected even with no operand"), Run(Settings, { Tagged(Selection, TEXT("S")) }, {}).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshBooleanGroupsTest,
	"PCGUtils.DynMesh.Boolean.OperandGroupsAndSelfUnion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshBooleanGroupsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBooleanTests;
	auto* A = Box();
	auto* B = Box(FVector(35, 10, 5), 80);
	A->GetMutableDynamicMesh()->EditMesh([](UE::Geometry::FDynamicMesh3& M)
	{
		M.EnableTriangleGroups(0);
		for (int32 TID : M.TriangleIndicesItr()) { M.SetTriangleGroup(TID, 17); }
	});
	// Two overlapping solids in one operand exercise self-union, not just its no-op path.
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(B->GetMutableDynamicMesh(), FGeometryScriptPrimitiveOptions(),
		FTransform(FVector(55, 20, 10)), 60, 60, 60, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center);
	auto* BeforeB = CastChecked<UPCGDynamicMeshData>(B->DuplicateData(nullptr));
	auto* Settings = NewObject<UPCGDynMeshBooleanSettings>();
	Settings->bAssignOperandPolygroup = Settings->bSelfUnionOperand = true;
	for (auto Op : { EGeometryScriptBooleanOperation::Union, EGeometryScriptBooleanOperation::Subtract })
	{
		Settings->BooleanOperation = Op;
		const auto Output = Run(Settings, { Tagged(A, TEXT("A")) }, { Tagged(B, TEXT("B")) });
		if (!TestEqual(TEXT("Boolean with operand preparation emits"), Output.Num(), 1)) { return false; }
		const auto& Result = Mesh(CastChecked<UPCGDynamicMeshData>(Output[0].Data));
		TSet<int32> Groups;
		for (int32 TID : Result.TriangleIndicesItr()) { Groups.Add(Result.GetTriangleGroup(TID)); }
		TestEqual(TEXT("One primary group and one operand group"), Groups.Num(), 2);
		TestTrue(TEXT("Original primary group survives"), Groups.Contains(17));
		Groups.Remove(17);
		if (Groups.Num() == 1) { TestTrue(TEXT("Operand group is fresh"), Groups.Array()[0] > 17); }
		TestTrue(TEXT("Operand geometry and groups remain unchanged"), SameMesh(B, BeforeB));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshBooleanDeferredTest,
	"PCGUtils.DynMesh.Boolean.DeferredParityPerSeed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshBooleanDeferredTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBooleanTests;
	auto* A = BoxBuilder(100);
	auto* B = BoxBuilder(60);
	auto* Settings = NewObject<UPCGDynMeshBooleanSettings>();
	Settings->BooleanOperation = EGeometryScriptBooleanOperation::Subtract;
	Settings->bAssignOperandPolygroup = Settings->bSelfUnionOperand = true;
	const auto Output = Run(Settings, { Tagged(A, TEXT("A")) }, { Tagged(B, TEXT("B")) });
	if (!TestEqual(TEXT("Binary process emits one Builder"), Output.Num(), 1)) { return false; }
	const auto* Deferred = Cast<UPCGUtilsDynMeshOperandProcessBuilderFactoryData>(Output[0].Data);
	if (!TestNotNull(TEXT("Output is a binary Builder"), Deferred)) { return false; }
	const auto Runtime = Deferred->CreateOperation(nullptr);
	if (!TestTrue(TEXT("Deferred expression prepares"), Runtime.IsValid())) { return false; }
	for (int32 Seed = 0; Seed < 2; ++Seed)
	{
		FPCGUtilsDynMeshBuildContext BuildContext;
		BuildContext.SeedTransform = FTransform(FRotator(0, 15, 0), FVector(1000 * Seed, 50 * Seed, 0));
		BuildContext.SeedLocalBounds = FBox(FVector(-50), FVector(50));
		FPCGUtilsDynMeshBuildResult BuiltA, BuiltB, Actual;
		if (!A->CreateOperation(nullptr)->Build(BuildContext, BuiltA) || !B->CreateOperation(nullptr)->Build(BuildContext, BuiltB)) { return false; }
		const auto Expected = Run(Settings, { Tagged(BuiltA.MeshData, TEXT("A")) }, { Tagged(BuiltB.MeshData, TEXT("B")) });
		if (!TestEqual(TEXT("Immediate counterpart emitted"), Expected.Num(), 1) ||
			!TestTrue(TEXT("Deferred boolean evaluates"), Runtime->Build(BuildContext, Actual))) { return false; }
		TestTrue(TEXT("Deferred matches immediate for this seed"), SameMesh(Actual.MeshData, CastChecked<UPCGDynamicMeshData>(Expected[0].Data)));
		TestFalse(TEXT("Binary result has no active selection"), Actual.HasSelection());
		TestTrue(TEXT("Primary Builder frame survives"), Actual.bHasBuilderFrame && Actual.BuilderFrame.Equals(BuiltA.BuilderFrame));
	}
	// Settings may disappear/change before materialization: the existing expression must retain Subtract.
	Settings->BooleanOperation = EGeometryScriptBooleanOperation::Union;
	FPCGUtilsDynMeshBuildContext Seed;
	Seed.SeedLocalBounds = FBox(FVector(-50), FVector(50));
	FPCGUtilsDynMeshBuildResult Captured;
	if (!Runtime->Build(Seed, Captured)) { return false; }
	TestTrue(TEXT("Captured difference retains an inner cavity after settings change"), Mesh(Captured.MeshData).TriangleCount() > 12);
	auto* Reversed = NewObject<UPCGUtilsDynMeshOperandProcessBuilderFactoryData>();
	Reversed->PrimaryBuilder = B;
	Reversed->OperandBuilder = A;
	Reversed->OperationConfigCrc = Deferred->OperationConfigCrc;
	Reversed->Operation = Deferred->Operation;
	Reversed->AddDataDependency(A);
	Reversed->AddDataDependency(B);
	TestTrue(TEXT("Ordered operands affect CRC"), Deferred->GetOrComputeCrc(true).GetValue() != Reversed->GetOrComputeCrc(true).GetValue());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshBooleanPinsTest,
	"PCGUtils.DynMesh.Boolean.UnifiedPins", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshBooleanPinsTest::RunTest(const FString&)
{
	const FPCGDataTypeIdentifier MeshType(EPCGDataType::DynamicMesh);
	const FPCGDataTypeIdentifier BuilderType(FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId());
	const FPCGDataTypeIdentifier SelectionType(UPCGDynamicMeshSelectionData::StaticClass());
	for (bool bBuilder : { false, true })
	{
		for (FName Anchor : { FName(TEXT("InA")), FName(TEXT("InB")), FName(TEXT("Out")) })
		{
			auto* Node = NewObject<UPCGNode>();
			auto* Settings = NewObject<UPCGDynMeshBooleanSettings>(Node);
			Node->SetSettingsInterface(Settings);
			TestNull(TEXT("No Selector pin"), Node->GetInputPin(TEXT("Selector")));
			TestNull(TEXT("No selection output override pin"), Node->GetInputPin(TEXT("bOutputSelectionData")));
			TestTrue(TEXT("Primary is required"), Node->GetInputPin(TEXT("InA"))->Properties.IsRequiredPin());
			TestFalse(TEXT("Operand is optional"), Node->GetInputPin(TEXT("InB"))->Properties.IsRequiredPin());
			auto* AnchorNode = NewObject<UPCGNode>();
			// A trivial node's disconnected pin is a concrete type anchor, in either direction.
			AnchorNode->SetSettingsInterface(AnchorNode->GetSettings());
			UPCGPin* FixedPin = Anchor == TEXT("Out")
				? AnchorNode->GetInputPin(PCGPinConstants::DefaultInputLabel)
				: AnchorNode->GetOutputPin(PCGPinConstants::DefaultOutputLabel);
			FixedPin->Properties.AllowedTypes = bBuilder ? BuilderType : MeshType;
			UPCGPin* ConnectedPin = Anchor == TEXT("Out") ? Node->GetOutputPin(Anchor) : Node->GetInputPin(Anchor);
			ConnectedPin->AddEdgeTo(FixedPin);
			Node->SetSettingsInterface(Settings);
			for (UPCGPin* Pin : { Node->GetInputPin(TEXT("InA")), Node->GetInputPin(TEXT("InB")), Node->GetOutputPin(TEXT("Out")) })
			{
				TestTrue(TEXT("All geometry pins share anchored type"), Pin->GetCurrentTypesID() == (bBuilder ? BuilderType : MeshType));
				TestFalse(TEXT("Geometry pins never accept selections"), Pin->Properties.AllowedTypes.Intersects(SelectionType));
				TestTrue(TEXT("Allowed types enforce homogeneous connections"), Pin->Properties.AllowedTypes == (bBuilder ? BuilderType : MeshType));
			}
			ConnectedPin->BreakAllEdges();
			Node->SetSettingsInterface(Settings);
			TestTrue(TEXT("Disconnect restores mesh choice"), Node->GetInputPin(TEXT("InA"))->Properties.AllowedTypes.Intersects(MeshType));
			TestTrue(TEXT("Disconnect restores Builder choice"), Node->GetInputPin(TEXT("InA"))->Properties.AllowedTypes.Intersects(BuilderType));
		}
	}
	return true;
}

#endif
