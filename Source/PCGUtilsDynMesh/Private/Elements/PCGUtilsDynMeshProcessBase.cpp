#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Factories/PCGUtilsDynMeshBuilderFactory.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "Factories/PCGUtilsDynMeshProcessBuilderFactory.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGNode.h"
#include "Serialization/ArchiveCrc32.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshProcessBase"

void UPCGUtilsDynMeshProcessBaseSettings::ApplyDeprecationBeforeUpdatePins(
	UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
	TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	Super::ApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
	if (InOutNode)
	{
		InOutNode->RenameInputPin(TEXT("Selection Factory"), PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin);
	}
}

namespace
{
	/** The concrete data types every DynMesh process accepts and can answer with. */
	FPCGDataTypeIdentifier MakeConcreteMeshTypes()
	{
		FPCGDataTypeIdentifier Types(EPCGDataType::DynamicMesh);
		Types |= FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass());
		return Types;
	}

	bool BuildCompleteSelection(
		const UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::EGeometryElementType ElementType,
		UE::Geometry::FGeometrySelection& OutSelection)
	{
		using namespace UE::Geometry;
		OutSelection.InitializeTypes(ElementType, EGeometryTopologyType::Triangle);
		if (ElementType == EGeometryElementType::Vertex)
		{
			for (const int32 VertexID : Mesh.VertexIndicesItr())
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshVertex(VertexID).Encoded());
			}
			return true;
		}
		if (ElementType == EGeometryElementType::Edge)
		{
			for (const int32 EdgeID : Mesh.EdgeIndicesItr())
			{
				Mesh.EnumerateTriEdgeIDsFromEdgeID(EdgeID, [&OutSelection](FMeshTriEdgeID TriEdgeID)
				{
					OutSelection.Selection.Add(FGeoSelectionID::MeshEdge(TriEdgeID).Encoded());
				});
			}
			return true;
		}
		if (ElementType == EGeometryElementType::Face)
		{
			for (const int32 TriangleID : Mesh.TriangleIndicesItr())
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
			}
			return true;
		}
		return false;
	}
}

FPCGUtilsDynMeshProcessSelectionPolicy UPCGUtilsDynMeshProcessBaseSettings::CaptureSelectionPolicy() const
{
	FPCGUtilsDynMeshProcessSelectionPolicy Policy;
	Policy.bRequiresSelection = RequiresSelection();
	Policy.bRequiresSpecificDomain = GetRequiredSelectionDomain(Policy.RequiredDomain);
	Policy.bAllowPartialDomainInclusion = AllowPartialSelectionDomainInclusion();
	Policy.SelectorEvaluationDomain = SelectionFactoryEvaluationDomain;
	return Policy;
}

void UPCGUtilsDynMeshProcessBaseSettings::AddProcessOperationToCrc(FArchiveCrc32& Ar) const
{
	// FArchiveCrc32 is itself a valid FArchive, so UObject::Serialize walks every reflected setting for us -
	// the same trick UPCGPrimitiveBuilderFactoryData uses to avoid hand-listing properties.
	const_cast<UPCGUtilsDynMeshProcessBaseSettings*>(this)->Serialize(Ar);
}

FPCGDataTypeIdentifier UPCGUtilsDynMeshProcessBaseSettings::GetProcessDataTypes() const
{
	FPCGDataTypeIdentifier Types = MakeConcreteMeshTypes();
	if (SupportsDeferredBuilderProcessing())
	{
		Types |= FPCGDataTypeIdentifier(FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId());
	}
	return Types;
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshProcessBaseSettings::InputPinProperties() const
{
	FPCGDataTypeIdentifier InputTypes = GetProcessDataTypes();

	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(GetMainInputPinLabel(), MoveTemp(InputTypes), true, true).SetRequiredPin();
	Pins.Emplace(
		PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), false, false);
	return Pins;
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshProcessBaseSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(GetMainOutputPinLabel(), GetProcessDataTypes(), true, true)};
}

FPCGDataTypeIdentifier UPCGUtilsDynMeshProcessBaseSettings::GetCurrentPinTypesID(const UPCGPin* InPin) const
{
	// Only a migrated node narrows its pins dynamically; everything else keeps the engine's default behaviour
	// so no unmigrated process can accidentally advertise a type its executor cannot honour.
	if (!InPin || !InPin->IsOutputPin() || !SupportsDeferredBuilderProcessing())
	{
		return Super::GetCurrentPinTypesID(InPin);
	}

	const FPCGDataTypeIdentifier ConnectedTypes = GetTypeUnionIDOfIncidentEdges(GetMainInputPinLabel());
	if (!ConnectedTypes.IsValid())
	{
		// Nothing wired in yet: show the full contract this node can accept.
		return InPin->Properties.AllowedTypes;
	}

	const FPCGDataTypeIdentifier BuilderType(FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId());
	if (ConnectedTypes.Intersects(BuilderType))
	{
		// Builder in, Builder out: this node will not touch geometry at all.
		return BuilderType;
	}

	// Concrete data in: the output kind is decided by this node's own output semantics, exactly as the
	// immediate executor emits it below.
	return bOutputSelectionData
		? FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass())
		: FPCGDataTypeIdentifier(EPCGDataType::DynamicMesh);
}

const UPCGData* FPCGUtilsDynMeshResolvedInput::GetData() const
{
	return SelectionData ? static_cast<const UPCGData*>(SelectionData) : static_cast<const UPCGData*>(MeshData);
}

bool FPCGUtilsDynMeshProcessFunctions::ResolveSelectorFromPin(
	FPCGContext* Context, FName PinLabel, const UPCGUtilsDynMeshSelectionFactoryData*& OutFactory)
{
	check(Context);
	OutFactory = nullptr;

	if (Context->InputData.GetInputsByPin(PinLabel).IsEmpty())
	{
		return true;
	}

	TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> SelectionFactories;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(
		Context, PinLabel, SelectionFactories,
		PCGUtilsDynMeshFactories::GetSelectionFactoryTypes(), /*bRequired=*/false))
	{
		return false;
	}
	if (SelectionFactories.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("RequiresOneSelectionFactory", "DynMesh process accepts at most one Selector input."), Context);
		return false;
	}

	OutFactory = SelectionFactories[0];
	return true;
}

FPCGUtilsDynMeshResolvedInput FPCGUtilsDynMeshProcessFunctions::ResolveInput(
	const UPCGData* InputData,
	const UPCGUtilsDynMeshSelectionFactoryData* Selector,
	const FPCGUtilsDynMeshProcessSelectionPolicy& Policy,
	FPCGContext* Context)
{
	FPCGUtilsDynMeshResolvedInput Result;
	const UPCGDynamicMeshSelectionData* InputSelection = Cast<const UPCGDynamicMeshSelectionData>(InputData);
	Result.MeshData = InputSelection ? InputSelection->GetSourceMeshData() : Cast<const UPCGDynamicMeshData>(InputData);
	const UDynamicMesh* SourceObject = Result.MeshData ? Result.MeshData->GetDynamicMesh() : nullptr;
	const UE::Geometry::FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
	if (!SourceMesh)
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("ResolveInvalidInput", "DynMesh process skipped an input with no valid source mesh."), Context);
		Result.MeshData = nullptr;
		return Result;
	}

	if (Policy.bRequiresSelection && !InputSelection && !Selector)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("ResolveSelectionRequired", "This DynMesh process requires either DynMesh Selection data or a connected Selector."),
			Context);
		Result.MeshData = nullptr;
		return Result;
	}

	if (!InputSelection && !Selector)
	{
		return Result;
	}

	const UE::Geometry::EGeometryElementType EffectiveElementType = Policy.bRequiresSpecificDomain
		? Policy.RequiredDomain
		: (Selector
			? PCGUtilsDynMeshProcess::ToGeometryElementType(Policy.SelectorEvaluationDomain)
			: InputSelection->GetSelection().ElementType);

	UE::Geometry::FGeometrySelection WorkingSelection;
	bool bHasSelection = false;
	if (InputSelection)
	{
		if (InputSelection->GetSelection().ElementType == EffectiveElementType)
		{
			WorkingSelection = InputSelection->GetSelection();
		}
		else if (!PCGUtilsDynMeshSelectionDomains::ConvertSelection(
			Result.MeshData, *SourceMesh, InputSelection->GetSelection(), EffectiveElementType,
			Policy.bAllowPartialDomainInclusion, WorkingSelection))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("ResolveDomainConversionFailed", "DynMesh process could not convert its selection input to the required domain."),
				Context);
			Result.MeshData = nullptr;
			return Result;
		}
		bHasSelection = true;
	}

	if (Selector)
	{
		FPCGUtilsDynMeshSelectionDomain FactoryDomain;
		FactoryDomain.ElementType = EffectiveElementType;
		FactoryDomain.TopologyType = UE::Geometry::EGeometryTopologyType::Triangle;
		FPCGUtilsDynMeshSelectionEvaluationContext EvaluationContext(Result.MeshData, *SourceMesh, FactoryDomain);
		UE::Geometry::FGeometrySelection FactorySelection;
		if (!PCGUtilsDynMeshSelectionFactories::EvaluateFactory(
			Selector, EvaluationContext, Context, FactorySelection))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("ResolveFactoryEvaluationFailed", "DynMesh process could not evaluate Selector '{0}' in its effective selection domain."),
				FText::FromString(Selector->GetClass()->GetName())), Context);
			Result.MeshData = nullptr;
			return Result;
		}
		if (bHasSelection)
		{
			// Incoming materialized selection INTERSECT connected Selector: the module-wide default.
			WorkingSelection.Selection = WorkingSelection.Selection.Intersect(FactorySelection.Selection);
		}
		else
		{
			WorkingSelection = MoveTemp(FactorySelection);
			bHasSelection = true;
		}
	}

	if (bHasSelection)
	{
		UPCGDynamicMeshSelectionData* ResolvedSelection =
			FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
		ResolvedSelection->Initialize(Result.MeshData, MoveTemp(WorkingSelection));
		Result.SelectionData = ResolvedSelection;
	}
	return Result;
}

FPCGUtilsDynMeshResolvedInput FPCGUtilsDynMeshProcessFunctions::ResolveInput(
	const UPCGData* InputData, const UPCGUtilsDynMeshProcessBaseSettings* Settings, FPCGContext* Context)
{
	check(Settings);
	check(Context);

	const UPCGUtilsDynMeshSelectionFactoryData* Selector = nullptr;
	if (!ResolveSelectorFromPin(
		Context, PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin, Selector))
	{
		return FPCGUtilsDynMeshResolvedInput();
	}

	return ResolveInput(InputData, Selector, Settings->CaptureSelectionPolicy(), Context);
}

int32 FPCGUtilsDynMeshProcessBaseElement::EmitDeferredBuilders(
	FPCGContext* Context,
	const UPCGUtilsDynMeshProcessBaseSettings* Settings,
	const UPCGUtilsDynMeshSelectionFactoryData* Selector) const
{
	const TArray<FPCGTaggedData>& Inputs =
		Context->InputData.GetInputsByPin(Settings->GetMainInputPinLabel());

	int32 BuilderCount = 0;
	TSharedPtr<const FPCGUtilsDynMeshProcessOperation> Operation;
	uint32 ConfigCrc = 0;

	for (const FPCGTaggedData& Input : Inputs)
	{
		const UPCGUtilsDynMeshBuilderFactoryData* ChildBuilder =
			Cast<const UPCGUtilsDynMeshBuilderFactoryData>(Input.Data);
		if (!ChildBuilder)
		{
			continue;
		}

		if (!Operation)
		{
			// Resolved once, while *this* node is executing - the whole point of the operation abstraction.
			Operation = Settings->CreateProcessOperation(Context);
			if (!Operation)
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("NoDeferredOperation", "This DynMesh process advertises Builder support but produced no process operation."),
					Context);
				return 0;
			}

			FArchiveCrc32 ConfigAr;
			Settings->AddProcessOperationToCrc(ConfigAr);
			ConfigCrc = ConfigAr.GetCrc();
		}

		UPCGUtilsDynMeshProcessBuilderFactoryData* Decorator =
			FPCGContext::NewObject_AnyThread<UPCGUtilsDynMeshProcessBuilderFactoryData>(Context);
		Decorator->ChildBuilder = ChildBuilder;
		Decorator->Selector = Selector;
		Decorator->Operation = Operation;
		Decorator->Policy = Settings->CaptureSelectionPolicy();
		Decorator->OperationConfigCrc = ConfigCrc;
#if WITH_EDITOR
		Decorator->ProcessLabel = Settings->GetDefaultNodeName();
#endif

		// Reuse the existing factory dependency infrastructure for cache identity rather than inventing one.
		Decorator->AddDataDependency(ChildBuilder);
		if (Selector)
		{
			Decorator->AddDataDependency(Selector);
		}

		if (!Decorator->Prepare(Context))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("DeferredPrepareFailed", "Deferred DynMesh process Builder preparation failed."), Context);
			continue;
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = Decorator;
		Output.Pin = Settings->GetMainOutputPinLabel();
		++BuilderCount;
	}

	return BuilderCount;
}

bool FPCGUtilsDynMeshProcessBaseElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGUtilsDynMeshProcessBaseSettings* Settings =
		Context->GetInputSettings<UPCGUtilsDynMeshProcessBaseSettings>();
	check(Settings);

	const FPCGUtilsDynMeshProcessSelectionPolicy Policy = Settings->CaptureSelectionPolicy();

	const UPCGUtilsDynMeshSelectionFactoryData* SelectionFactory = nullptr;
	if (!FPCGUtilsDynMeshProcessFunctions::ResolveSelectorFromPin(
		Context, PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin, SelectionFactory))
	{
		return true;
	}

	// Resolved once, while this node executes - never rediscovered per input, and never from another context.
	const TSharedPtr<const FPCGUtilsDynMeshProcessOperation> Operation = Settings->CreateProcessOperation(Context);

	if (Settings->SupportsDeferredBuilderProcessing())
	{
		// Builder inputs are wrapped, never evaluated. Concrete inputs on the same pin still run immediately.
		EmitDeferredBuilders(Context, Settings, SelectionFactory);
	}

	const TArray<FPCGTaggedData>& ProcessInputs =
		Context->InputData.GetInputsByPin(Settings->GetMainInputPinLabel());
	int32 ConcreteInputCount = 0;
	for (const FPCGTaggedData& Input : ProcessInputs)
	{
		ConcreteInputCount += !Input.Data ||
			!Input.Data->IsA<UPCGUtilsDynMeshBuilderFactoryData>() ? 1 : 0;
	}
	int32 ConcreteInputIndex = 0;
	for (const FPCGTaggedData& Input : ProcessInputs)
	{
		if (Input.Data && Input.Data->IsA<UPCGUtilsDynMeshBuilderFactoryData>())
		{
			continue;
		}
		const int32 InvocationInputIndex = ConcreteInputIndex++;

		const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* SourceData = SelectionData
			? SelectionData->GetSourceMeshData() : Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidInput", "DynMesh processor skipped an input with no valid source mesh."), Context);
			continue;
		}
		if (Policy.bRequiresSelection && !SelectionData && !SelectionFactory)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("SelectionRequired", "This DynMesh process requires either DynMesh Selection data or a connected Selector."),
				Context);
			continue;
		}

		TArray<UMaterialInterface*> Materials;
		Materials.Reserve(SourceData->GetMaterials().Num());
		for (UMaterialInterface* Material : SourceData->GetMaterials())
		{
			Materials.Add(Material);
		}

		// Immediate mode never mutates upstream data: the process always operates on its own copy.
		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(UE::Geometry::FDynamicMesh3(*SourceMesh), MoveTemp(Materials));

		// Rebind the input onto the freshly copied mesh, then run the same explicit resolver the deferred path
		// uses, so selection semantics are identical in both modes.
		const UPCGData* RebasedInput = static_cast<const UPCGData*>(OutputData);
		if (SelectionData)
		{
			UPCGDynamicMeshSelectionData* RebasedSelection =
				FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
			RebasedSelection->Initialize(OutputData, SelectionData->GetSelection());
			RebasedInput = RebasedSelection;
		}

		const FPCGUtilsDynMeshResolvedInput Resolved = FPCGUtilsDynMeshProcessFunctions::ResolveInput(
			RebasedInput, SelectionFactory, Policy, Context);
		if (!Resolved.IsValid())
		{
			continue;
		}

		const UPCGDynamicMeshSelectionData* ProcessSelectionData = Resolved.SelectionData;

		bool bProcessed = false;
		if (Operation)
		{
			FPCGUtilsDynMeshProcessInvocation Invocation;
			Invocation.Context = Context;
			Invocation.MeshData = OutputData;
			Invocation.SelectionData = ProcessSelectionData;
			// Immediate mode still has the untouched upstream data around; hand it to the operation for
			// anything the working copy does not carry (Data-domain attributes and the like).
			Invocation.SourceMeshData = SourceData;
			Invocation.InputIndex = InvocationInputIndex;
			Invocation.InputCount = ConcreteInputCount;

			FPCGUtilsDynMeshProcessOutcome Outcome;
			bProcessed = Operation->Execute(Invocation, Outcome);
		}
		else
		{
			bProcessed = ProcessMesh(OutputData, ProcessSelectionData, Context);
		}

		if (!bProcessed)
		{
			continue;
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		if (Settings->bOutputSelectionData)
		{
			UE::Geometry::FGeometrySelection OutputSelection;
			if (ProcessSelectionData)
			{
				OutputSelection = ProcessSelectionData->GetSelection();
			}
			else
			{
				const UDynamicMesh* ProcessedObject = OutputData->GetDynamicMesh();
				const UE::Geometry::FDynamicMesh3* ProcessedMesh =
					ProcessedObject ? ProcessedObject->GetMeshPtr() : nullptr;
				const UE::Geometry::EGeometryElementType OutputElementType =
					Policy.bRequiresSpecificDomain
						? Policy.RequiredDomain
						: PCGUtilsDynMeshProcess::ToGeometryElementType(Policy.SelectorEvaluationDomain);
				if (!ProcessedMesh || !BuildCompleteSelection(
					*ProcessedMesh, OutputElementType, OutputSelection))
				{
					PCGLog::LogErrorOnGraph(
						LOCTEXT("CompleteSelectionOutputFailed", "DynMesh processor could not create its full-mesh selection output."),
						Context);
					continue;
				}
			}
			UPCGDynamicMeshSelectionData* OutputSelectionData =
				FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
			OutputSelectionData->Initialize(OutputData, MoveTemp(OutputSelection));
			Output.Data = OutputSelectionData;
		}
		else
		{
			Output.Data = OutputData;
		}
		Output.Pin = Settings->GetMainOutputPinLabel();
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
