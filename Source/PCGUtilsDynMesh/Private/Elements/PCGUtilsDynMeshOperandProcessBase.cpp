// Copyright Max Harris

#include "Elements/PCGUtilsDynMeshOperandProcessBase.h"

#include "Data/PCGDynamicMeshData.h"
#include "Factories/PCGUtilsDynMeshOperandProcessBuilderFactory.h"
#include "PCGContext.h"
#include "PCGEdge.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshOperandProcessBase"

FPCGDataTypeIdentifier UPCGUtilsDynMeshOperandProcessBaseSettings::GetOperandProcessDataTypes() const
{
	const FPCGDataTypeIdentifier MeshType(EPCGDataType::DynamicMesh);
	const FPCGDataTypeIdentifier BuilderType(FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId());
	const FPCGDataTypeIdentifier AllTypes = MeshType | BuilderType;

	// Traverse connected operand nodes as one type constraint. Do not read their previously narrowed pin
	// properties: those would retain a stale type after the last concrete connection was removed.
	// Other dynamic nodes can ask us for their upstream type, so protect that recursion as well.
	static thread_local TSet<const UPCGUtilsDynMeshOperandProcessBaseSettings*> Resolving;
	if (Resolving.Contains(this))
	{
		return AllTypes;
	}
	Resolving.Add(this);
	TArray<const UPCGUtilsDynMeshOperandProcessBaseSettings*> Pending { this };
	TSet<const UPCGUtilsDynMeshOperandProcessBaseSettings*> Visited;
	bool bMesh = false;
	bool bBuilder = false;
	while (!Pending.IsEmpty())
	{
		const UPCGUtilsDynMeshOperandProcessBaseSettings* Current = Pending.Pop();
		if (Visited.Contains(Current))
		{
			continue;
		}
		Visited.Add(Current);
		const UPCGNode* Node = Cast<UPCGNode>(Current->GetOuter());
		if (!Node)
		{
			continue;
		}
		const UPCGPin* Pins[] = { Node->GetInputPin(Current->GetMainInputPinLabel()),
			Node->GetInputPin(Current->GetOperandInputPinLabel()), Node->GetOutputPin(Current->GetMainOutputPinLabel()) };
		for (const UPCGPin* Pin : Pins)
		{
			if (!Pin) { continue; }
			for (const UPCGEdge* Edge : Pin->Edges)
			{
				const UPCGPin* Other = Edge ? Edge->GetOtherPin(Pin) : nullptr;
				if (!Other || !Other->Node) { continue; }
				const auto* OtherSettings = Cast<UPCGUtilsDynMeshOperandProcessBaseSettings>(Other->Node->GetSettings());
				if (OtherSettings && (Other->Properties.Label == OtherSettings->GetMainInputPinLabel() ||
					Other->Properties.Label == OtherSettings->GetOperandInputPinLabel() ||
					Other->Properties.Label == OtherSettings->GetMainOutputPinLabel()))
				{
					Pending.Add(OtherSettings);
					continue;
				}
				const FPCGDataTypeIdentifier Types = Other->IsOutputPin()
					? Other->GetCurrentTypesID() : Other->Properties.AllowedTypes;
				const bool bAcceptsMesh = Types.Intersects(MeshType);
				const bool bAcceptsBuilder = Types.Intersects(BuilderType);
				bMesh |= bAcceptsMesh && !bAcceptsBuilder;
				bBuilder |= bAcceptsBuilder && !bAcceptsMesh;
			}
		}
	}
	Resolving.Remove(this);
	// In a stale mixed graph, prefer concrete meshes; UpdatePins removes incompatible Builder edges.
	return bMesh ? MeshType : (bBuilder ? BuilderType : AllTypes);
}

FPCGDataTypeIdentifier UPCGUtilsDynMeshOperandProcessBaseSettings::GetCurrentPinTypesID(const UPCGPin* InPin) const
{
	if (InPin && (InPin->Properties.Label == GetMainInputPinLabel() ||
		InPin->Properties.Label == GetOperandInputPinLabel() || InPin->Properties.Label == GetMainOutputPinLabel()))
	{
		return GetOperandProcessDataTypes();
	}
	return Super::GetCurrentPinTypesID(InPin);
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshOperandProcessBaseSettings::InputPinProperties() const
{
	const FPCGDataTypeIdentifier Types = GetOperandProcessDataTypes();
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(GetMainInputPinLabel(), Types, true, true).SetRequiredPin();
	Pins.Emplace(GetOperandInputPinLabel(), Types, true, true,
		LOCTEXT("OperandTooltip", "Optional operand. When no data arrives, the primary input passes through unchanged. Both inputs must be DynMeshes or both must be Builders."));
	return Pins;
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshOperandProcessBaseSettings::OutputPinProperties() const
{
	return { FPCGPinProperties(GetMainOutputPinLabel(), GetOperandProcessDataTypes(), true, true) };
}

FPCGElementPtr UPCGUtilsDynMeshOperandProcessBaseSettings::CreateElement() const
{
	return MakeShared<FPCGUtilsDynMeshOperandProcessBaseElement>();
}

#if WITH_EDITOR
TArray<FPCGSettingsOverridableParam> UPCGUtilsDynMeshOperandProcessBaseSettings::GatherOverridableParams() const
{
	TArray<FPCGSettingsOverridableParam> Params = Super::GatherOverridableParams();
	// HideCategories handles the details panel; remove the inherited selection override pins as well.
	Params.RemoveAll([](const FPCGSettingsOverridableParam& Param)
	{
		return Param.PropertiesNames.Contains(GET_MEMBER_NAME_CHECKED(UPCGUtilsDynMeshProcessBaseSettings, SelectionFactoryEvaluationDomain)) ||
			Param.PropertiesNames.Contains(GET_MEMBER_NAME_CHECKED(UPCGUtilsDynMeshProcessBaseSettings, bRequireSelection)) ||
			Param.PropertiesNames.Contains(GET_MEMBER_NAME_CHECKED(UPCGUtilsDynMeshProcessBaseSettings, bOutputSelectionData));
	});
	return Params;
}
#endif

bool FPCGUtilsDynMeshOperandProcessBaseElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const auto* Settings = Context->GetInputSettings<UPCGUtilsDynMeshOperandProcessBaseSettings>();
	check(Settings);
	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(Settings->GetMainInputPinLabel());
	const TArray<FPCGTaggedData> Operands = Context->InputData.GetInputsByPin(Settings->GetOperandInputPinLabel());
	if (Inputs.IsEmpty()) { return true; }

	const bool bBuilders = Cast<UPCGUtilsDynMeshBuilderFactoryData>(Inputs[0].Data) != nullptr;
	auto Validate = [Context, bBuilders](const TArray<FPCGTaggedData>& Data)
	{
		for (const FPCGTaggedData& Input : Data)
		{
			if ((bBuilders && !Cast<UPCGUtilsDynMeshBuilderFactoryData>(Input.Data)) ||
				(!bBuilders && !Cast<UPCGDynamicMeshData>(Input.Data)))
			{
				PCGLog::LogErrorOnGraph(LOCTEXT("MixedInputs",
					"DynMesh operand processes require only DynMeshes on both inputs or only Builders on both inputs. Selections and mixed types are not supported."), Context);
				return false;
			}
			if (!bBuilders && !FPCGUtilsDynMeshProcessFunctions::ResolveInput(
				Input.Data, nullptr, FPCGUtilsDynMeshProcessSelectionPolicy(), Context).IsValid())
			{
				return false;
			}
		}
		return true;
	};
	if (!Validate(Inputs) || !Validate(Operands)) { return true; }

	if (Operands.IsEmpty())
	{
		for (const FPCGTaggedData& Input : Inputs)
		{
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
			Output.Pin = Settings->GetMainOutputPinLabel();
		}
		return true;
	}

	const bool bPairwise = Settings->Mode == EPCGBooleanOperationMode::EachAWithEachB;
	const bool bSequential = Settings->Mode == EPCGBooleanOperationMode::EachAWithEachBSequentially;
	if (bPairwise && Inputs.Num() != 1 && Operands.Num() != 1 && Inputs.Num() != Operands.Num())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("CountMismatch", "Pairwise DynMesh operations require N:N, N:1, or 1:N inputs."), Context);
		return true;
	}

	const TSharedPtr<const FPCGUtilsDynMeshProcessOperation> Operation = Settings->CreateProcessOperation(Context);
	if (!Operation)
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("MissingOperation", "DynMesh operand process did not provide an operation."), Context);
		return true;
	}
	FArchiveCrc32 ConfigAr;
	if (bBuilders) { Settings->AddProcessOperationToCrc(ConfigAr); }

	const int32 OutputCount = bPairwise ? FMath::Max(Inputs.Num(), Operands.Num()) :
		(bSequential ? Inputs.Num() : Inputs.Num() * Operands.Num());
	for (int32 OutputIndex = 0; OutputIndex < OutputCount; ++OutputIndex)
	{
		const int32 AIndex = bPairwise ? OutputIndex % Inputs.Num() :
			(bSequential ? OutputIndex : OutputIndex / Operands.Num());
		const int32 FirstB = bSequential ? 0 : OutputIndex % Operands.Num();
		const int32 EndB = bSequential ? Operands.Num() : FirstB + 1;
		FPCGTaggedData Output = Inputs[AIndex];
		Output.Pin = Settings->GetMainOutputPinLabel();
		UPCGDynamicMeshData* WorkingMesh = bBuilders ? nullptr :
			CastChecked<UPCGDynamicMeshData>(Output.Data->DuplicateData(Context));
		bool bSucceeded = true;
		for (int32 BIndex = FirstB; BIndex < EndB; ++BIndex)
		{
			if (bBuilders)
			{
				auto* Builder = FPCGContext::NewObject_AnyThread<UPCGUtilsDynMeshOperandProcessBuilderFactoryData>(Context);
				Builder->PrimaryBuilder = CastChecked<UPCGUtilsDynMeshBuilderFactoryData>(Output.Data);
				Builder->OperandBuilder = CastChecked<UPCGUtilsDynMeshBuilderFactoryData>(Operands[BIndex].Data);
				Builder->Operation = Operation;
				Builder->OperationConfigCrc = ConfigAr.GetCrc();
				Builder->AddDataDependency(Builder->PrimaryBuilder);
				Builder->AddDataDependency(Builder->OperandBuilder);
				Output.Data = Builder;
			}
			else
			{
				FPCGUtilsDynMeshProcessInvocation Invocation;
				Invocation.Context = Context;
				Invocation.MeshData = WorkingMesh;
				Invocation.SourceMeshData = CastChecked<UPCGDynamicMeshData>(Inputs[AIndex].Data);
				Invocation.OperandMeshData = CastChecked<UPCGDynamicMeshData>(Operands[BIndex].Data);
				Invocation.InputIndex = OutputIndex;
				Invocation.InputCount = OutputCount;
				FPCGUtilsDynMeshProcessOutcome Outcome;
				if (!Operation->Execute(Invocation, Outcome))
				{
					bSucceeded = false;
					break;
				}
				Output.Data = WorkingMesh;
			}
			if (Settings->TagInheritanceMode == EPCGBooleanOperationTagInheritanceMode::B)
			{
				Output.Tags = Operands[BIndex].Tags;
			}
			else if (Settings->TagInheritanceMode == EPCGBooleanOperationTagInheritanceMode::Both)
			{
				Output.Tags.Append(Operands[BIndex].Tags);
			}
		}
		if (bSucceeded) { Context->OutputData.TaggedData.Add(MoveTemp(Output)); }
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
