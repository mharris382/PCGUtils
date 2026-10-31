// Copyright Max Harris
//
// Process-base nodes run each input's operation in parallel (Docs/PCGUtilsDynMesh/ParallelInputs.md). These tests
// check the gates that force the serial path, that parallel output is identical to serial output, and that
// objects created on worker threads lose their Async flag so the garbage collector can track them.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Deform/PCGSmoothDynamicMesh.h"
#include "Elements/Topology/PCGDynMeshDecimate.h"
#include "Elements/Topology/PCGDynMeshMergeByDistance.h"
#include "Elements/Topology/PCGDynMeshSelfUnion.h"
#include "Elements/Topology/PCGDynMeshSeparateIslands.h"
#include "Elements/Topology/PCGDynMeshSimplify.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "HAL/IConsoleManager.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshParallelInputsTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	const FName IndexAttribute = TEXT("InputIndex");

	/** A box of the given density, tagged on the data itself with its input index so output order can be checked. */
	UPCGDynamicMeshData* MakeBox(int32 Index, int32 Segments)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		const int32 Steps = Segments + 1;
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(),
			FGeometryScriptPrimitiveOptions(), FTransform(FVector(300.0 * Index, 0.0, 0.0)), 100.0, 100.0, 100.0,
			Steps, Steps, Steps, EGeometryScriptPrimitiveOriginMode::Center);

		FPCGMetadataDomain* Domain = Data->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
		FPCGMetadataAttribute<int32>* Attribute = Domain->FindOrCreateAttribute<int32>(IndexAttribute, 0, false, false, true);
		Domain->AddEntry();
		Attribute->SetValue(PCGFirstEntryKey, Index);
		return Data;
	}

	TArray<const UPCGData*> MakeBoxes(int32 Count)
	{
		TArray<const UPCGData*> Boxes;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			// Different sizes, so the parallel bodies finish out of order.
			Boxes.Add(MakeBox(Index, 2 + (Index * 3) % 7));
		}
		return Boxes;
	}

	int32 ReadIndex(const UPCGData* Data)
	{
		const UPCGMetadata* Metadata = Data ? Data->ConstMetadata() : nullptr;
		const FPCGMetadataAttribute<int32>* Attribute = Metadata
			? Metadata->GetConstTypedAttribute<int32>(FPCGAttributeIdentifier(IndexAttribute, PCGMetadataDomainID::Data)) : nullptr;
		return Attribute ? Attribute->GetValueFromItemKey(PCGFirstEntryKey) : INDEX_NONE;
	}

	TArray<FPCGTaggedData> Run(UPCGUtilsDynMeshProcessBaseSettings* Settings, const TArray<const UPCGData*>& Inputs)
	{
		FPCGDataCollection Input;
		const FName InputPin = Settings->AllInputPinProperties()[0].Label;
		for (const UPCGData* Data : Inputs)
		{
			FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Pin = InputPin;
			Tagged.Data = Data;
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

	/** How many parallel runs a call started. */
	template<typename FuncType>
	int32 CountParallelRuns(FuncType&& Func)
	{
		const int32 Before = PCGUtilsParallelInputs::GetParallelRunCount();
		Func();
		return PCGUtilsParallelInputs::GetParallelRunCount() - Before;
	}

	/** Sets pcgutils.ParallelInputs for the lifetime of the object. */
	struct FScopedParallelCVar
	{
		IConsoleVariable* Variable = nullptr;
		int32 Previous = 1;

		explicit FScopedParallelCVar(int32 Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("pcgutils.ParallelInputs"));
			if (Variable)
			{
				Previous = Variable->GetInt();
				Variable->Set(Value, ECVF_SetByCode);
			}
		}
		~FScopedParallelCVar()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByCode);
			}
		}
	};

	const UE::Geometry::FDynamicMesh3* MeshOf(const UPCGData* Data)
	{
		const UPCGDynamicMeshData* MeshData = Cast<const UPCGDynamicMeshData>(Data);
		const UDynamicMesh* Object = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		return Object ? Object->GetMeshPtr() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshParallelInputsGatesTest,
	"PCGUtils.DynMesh.ParallelInputs.Gates",
	PCGUtilsDynMeshParallelInputsTests::Flags)

bool FPCGUtilsDynMeshParallelInputsGatesTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshParallelInputsTests;
	const TArray<const UPCGData*> Boxes = MakeBoxes(4);

	{
		FScopedParallelCVar CVar(1);
		UPCGDynMeshDecimateSettings* Settings = NewObject<UPCGDynMeshDecimateSettings>();
		TestEqual(TEXT("With no gate set, a node with several inputs takes the parallel path"),
			CountParallelRuns([&] { Run(Settings, Boxes); }), 1);

		const TArray<const UPCGData*> OneBox = { Boxes[0] };
		TestEqual(TEXT("One input stays serial"), CountParallelRuns([&] { Run(Settings, OneBox); }), 0);

		Settings->bDisableParallelInputs = true;
		TestEqual(TEXT("Disable Parallel forces the serial path"), CountParallelRuns([&] { Run(Settings, Boxes); }), 0);
	}
	{
		FScopedParallelCVar CVar(0);
		UPCGDynMeshDecimateSettings* Settings = NewObject<UPCGDynMeshDecimateSettings>();
		TestEqual(TEXT("pcgutils.ParallelInputs 0 forces the serial path"), CountParallelRuns([&] { Run(Settings, Boxes); }), 0);
	}

	// Node opt-out: the Disable Parallel row shows only where the parallel path applies.
	TestTrue(TEXT("Decimate shows Disable Parallel"), GetDefault<UPCGDynMeshDecimateSettings>()->bShowDisableParallelInputs);
	TestFalse(TEXT("Separate Islands (own executor) opts out and hides Disable Parallel"),
		GetDefault<UPCGDynMeshSeparateIslandsSettings>()->bShowDisableParallelInputs);
	TestFalse(TEXT("A node that opts out never allows parallel inputs"),
		GetDefault<UPCGDynMeshSeparateIslandsSettings>()->AllowsParallelInputs());

	// The runner itself.
	TestFalse(TEXT("ForEach with bParallel false runs serially"),
		PCGUtilsParallelInputs::ForEach(nullptr, 8, false, [](int32) {}));
	TestFalse(TEXT("ForEach with one item runs serially"),
		PCGUtilsParallelInputs::ForEach(nullptr, 1, true, [](int32) {}));
	TArray<int32> Visited;
	Visited.Init(0, 32);
	TestTrue(TEXT("ForEach with several items runs in parallel"),
		PCGUtilsParallelInputs::ForEach(nullptr, Visited.Num(), true, [&Visited](int32 Index) { ++Visited[Index]; }));
	TestTrue(TEXT("ForEach visits every index exactly once"), !Visited.ContainsByPredicate([](int32 Count) { return Count != 1; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshParallelInputsMatchesSerialTest,
	"PCGUtils.DynMesh.ParallelInputs.MatchesSerial",
	PCGUtilsDynMeshParallelInputsTests::Flags)

bool FPCGUtilsDynMeshParallelInputsMatchesSerialTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshParallelInputsTests;
	FScopedParallelCVar CVar(1);
	const TArray<const UPCGData*> Boxes = MakeBoxes(8);

	const TArray<TSubclassOf<UPCGUtilsDynMeshProcessBaseSettings>> Nodes = {
		UPCGDynMeshDecimateSettings::StaticClass(),
		UPCGDynMeshSimplifySettings::StaticClass(),
		UPCGSmoothDynamicMeshSettings::StaticClass(),
		UPCGDynMeshMergeByDistanceSettings::StaticClass(),
		UPCGDynMeshSelfUnionSettings::StaticClass(),
	};

	for (const TSubclassOf<UPCGUtilsDynMeshProcessBaseSettings>& NodeClass : Nodes)
	{
		const FString Name = NodeClass->GetName();
		UPCGUtilsDynMeshProcessBaseSettings* Serial = NewObject<UPCGUtilsDynMeshProcessBaseSettings>(GetTransientPackage(), NodeClass);
		Serial->bDisableParallelInputs = true;
		UPCGUtilsDynMeshProcessBaseSettings* Parallel = NewObject<UPCGUtilsDynMeshProcessBaseSettings>(GetTransientPackage(), NodeClass);

		TArray<FPCGTaggedData> SerialOut;
		TArray<FPCGTaggedData> ParallelOut;
		const int32 SerialRuns = CountParallelRuns([&] { SerialOut = Run(Serial, Boxes); });
		const int32 ParallelRuns = CountParallelRuns([&] { ParallelOut = Run(Parallel, Boxes); });
		TestEqual(FString::Printf(TEXT("%s: the serial run did not go parallel"), *Name), SerialRuns, 0);
		TestEqual(FString::Printf(TEXT("%s: the parallel run went parallel"), *Name), ParallelRuns, 1);

		if (!TestEqual(FString::Printf(TEXT("%s: same number of outputs"), *Name), ParallelOut.Num(), SerialOut.Num()))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: one output per input"), *Name), ParallelOut.Num(), Boxes.Num());

		for (int32 OutputIndex = 0; OutputIndex < SerialOut.Num(); ++OutputIndex)
		{
			const FPCGTaggedData& A = SerialOut[OutputIndex];
			const FPCGTaggedData& B = ParallelOut[OutputIndex];
			const FString Where = FString::Printf(TEXT("%s output %d"), *Name, OutputIndex);
			TestEqual(Where + TEXT(": same pin"), B.Pin, A.Pin);
			TestEqual(Where + TEXT(": outputs stay in input order"), ReadIndex(B.Data), OutputIndex);
			TestEqual(Where + TEXT(": same input as serial"), ReadIndex(B.Data), ReadIndex(A.Data));

			const UE::Geometry::FDynamicMesh3* MeshA = MeshOf(A.Data);
			const UE::Geometry::FDynamicMesh3* MeshB = MeshOf(B.Data);
			if (!TestTrue(Where + TEXT(": both are meshes"), MeshA && MeshB))
			{
				continue;
			}
			TestEqual(Where + TEXT(": same vertex count"), MeshB->VertexCount(), MeshA->VertexCount());
			TestEqual(Where + TEXT(": same triangle count"), MeshB->TriangleCount(), MeshA->TriangleCount());
			bool bSamePositions = MeshA->MaxVertexID() == MeshB->MaxVertexID();
			for (int32 VertexID = 0; bSamePositions && VertexID < MeshA->MaxVertexID(); ++VertexID)
			{
				bSamePositions = MeshA->IsVertex(VertexID) == MeshB->IsVertex(VertexID)
					&& (!MeshA->IsVertex(VertexID) || MeshA->GetVertex(VertexID) == MeshB->GetVertex(VertexID));
			}
			TestTrue(Where + TEXT(": identical vertex positions"), bSamePositions);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshParallelInputsAsyncFlagsTest,
	"PCGUtils.DynMesh.ParallelInputs.AsyncFlagsCleared",
	PCGUtilsDynMeshParallelInputsTests::Flags)

bool FPCGUtilsDynMeshParallelInputsAsyncFlagsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshParallelInputsTests;
	FScopedParallelCVar CVar(1);

	// Objects made inside a parallel run, from whichever thread ran each index.
	TArray<UPCGDynamicMeshData*> Made;
	Made.Init(nullptr, 64);
	PCGUtilsParallelInputs::ForEach(nullptr, Made.Num(), true, [&Made](int32 Index)
	{
		Made[Index] = PCGUtilsAsyncObjects::NewObject<UPCGDynamicMeshData>(nullptr);
	});
	bool bAllMade = true;
	bool bNoneAsync = true;
	for (const UPCGDynamicMeshData* Data : Made)
	{
		bAllMade &= Data != nullptr;
		if (Data)
		{
			bNoneAsync &= !Data->HasAnyInternalFlags(EInternalObjectFlags::Async);
			bNoneAsync &= !Data->GetDynamicMesh() || !Data->GetDynamicMesh()->HasAnyInternalFlags(EInternalObjectFlags::Async);
		}
	}
	TestTrue(TEXT("Every index made its object"), bAllMade);
	TestTrue(TEXT("No object made in the parallel run, or its subobjects, keeps the Async flag"), bNoneAsync);

	// The same through a node.
	UPCGDynMeshDecimateSettings* Settings = NewObject<UPCGDynMeshDecimateSettings>();
	const TArray<FPCGTaggedData> Outputs = Run(Settings, MakeBoxes(8));
	bool bOutputsClean = !Outputs.IsEmpty();
	for (const FPCGTaggedData& Output : Outputs)
	{
		bOutputsClean &= Output.Data && !Output.Data->HasAnyInternalFlags(EInternalObjectFlags::Async);
	}
	TestTrue(TEXT("No output of a parallel node run keeps the Async flag"), bOutputsClean);
	return true;
}

#endif
