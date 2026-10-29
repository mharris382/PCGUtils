// Copyright Max Harris

#include "Elements/Attributes/PCGDynMeshPaintVertexColor.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Factories/PCGUtilsDynMeshBuilderFactory.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Selections/GeometrySelection.h"
#include "Target/PCGUtilsPainterDynMeshTarget.h"
#include "Target/PCGUtilsPainterLODTransfer.h"
#include "Target/PCGUtilsPainterTarget.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshPaintVertexColor"

namespace PCGDynMeshPaintVertexColorPrivate
{
	const FName MeshPin = TEXT("Mesh");
	const FName PainterPin = TEXT("Painter");

	EPCGUtilsDynMeshPainterColorChannel GetWriteChannels(
		const FGeometryScriptColorFlags& Flags)
	{
		EPCGUtilsDynMeshPainterColorChannel Channels =
			EPCGUtilsDynMeshPainterColorChannel::None;
		if (Flags.bRed) Channels |= EPCGUtilsDynMeshPainterColorChannel::Red;
		if (Flags.bGreen) Channels |= EPCGUtilsDynMeshPainterColorChannel::Green;
		if (Flags.bBlue) Channels |= EPCGUtilsDynMeshPainterColorChannel::Blue;
		if (Flags.bAlpha) Channels |= EPCGUtilsDynMeshPainterColorChannel::Alpha;
		return Channels;
	}

	EPCGUtilsPainterBaseColorSource GetBaseColorSource(EPCGUtilsDynMeshPainterBaseColorMode Mode)
	{
		return Mode == EPCGUtilsDynMeshPainterBaseColorMode::Existing
			? EPCGUtilsPainterBaseColorSource::CanonicalExisting
			: EPCGUtilsPainterBaseColorSource::Constant;
	}

	const FPCGMetadataAttributeBase* FindDataAttribute(const UPCGData* Data, FName Name)
	{
		const UPCGMetadata* Metadata = Data ? Data->ConstMetadata() : nullptr;
		return Metadata ? Metadata->GetConstAttribute(FPCGAttributeIdentifier(Name, PCGMetadataDomainID::Data)) : nullptr;
	}

	template <typename T>
	T ReadDataValue(const FPCGMetadataAttributeBase* Attribute)
	{
		return static_cast<const FPCGMetadataAttribute<T>*>(Attribute)->GetValueFromItemKey(PCGFirstEntryKey);
	}

	/** Reads an input's LOD number. Returns false with OutError naming the failed condition. */
	bool ReadLOD(const UPCGData* Data, FName Name, int32 InputIndex, int32& OutLOD, FText& OutError)
	{
		const FPCGMetadataAttributeBase* Attribute = FindDataAttribute(Data, Name);
		if (!Attribute)
		{
			OutError = FText::Format(LOCTEXT("NoLODAttribute",
				"Paint Vertex Color: input {0} has no @Data attribute '{1}' (LOD Attribute), so it is painted on its own "
				"instead of with its other LODs."), FText::AsNumber(InputIndex), FText::FromName(Name));
			return false;
		}
		if (Attribute->GetTypeId() == PCG::Private::MetadataTypes<int32>::Id)
		{
			OutLOD = ReadDataValue<int32>(Attribute);
			return true;
		}
		if (Attribute->GetTypeId() == PCG::Private::MetadataTypes<int64>::Id)
		{
			OutLOD = static_cast<int32>(ReadDataValue<int64>(Attribute));
			return true;
		}
		OutError = FText::Format(LOCTEXT("LODAttributeType",
			"Paint Vertex Color: input {0}: @Data attribute '{1}' (LOD Attribute) is not an integer (expected int32 or "
			"int64), so the input is painted on its own."), FText::AsNumber(InputIndex), FText::FromName(Name));
		return false;
	}

	/** Reads the value that groups an input with its other LODs. Returns false with OutError naming the failed condition. */
	bool ReadGroup(const UPCGData* Data, FName Name, int32 InputIndex, FString& OutGroup, FText& OutError)
	{
		const FPCGMetadataAttributeBase* Attribute = FindDataAttribute(Data, Name);
		if (!Attribute)
		{
			OutError = FText::Format(LOCTEXT("NoSourceAttribute",
				"Paint Vertex Color: input {0} has no @Data attribute '{1}' (LOD Source Attribute), so it is painted on "
				"its own instead of with its other LODs."), FText::AsNumber(InputIndex), FText::FromName(Name));
			return false;
		}
		const uint16 Type = Attribute->GetTypeId();
		if (Type == PCG::Private::MetadataTypes<FSoftObjectPath>::Id) { OutGroup = ReadDataValue<FSoftObjectPath>(Attribute).ToString(); }
		else if (Type == PCG::Private::MetadataTypes<FSoftClassPath>::Id) { OutGroup = ReadDataValue<FSoftClassPath>(Attribute).ToString(); }
		else if (Type == PCG::Private::MetadataTypes<FString>::Id) { OutGroup = ReadDataValue<FString>(Attribute); }
		else if (Type == PCG::Private::MetadataTypes<FName>::Id) { OutGroup = ReadDataValue<FName>(Attribute).ToString(); }
		else if (Type == PCG::Private::MetadataTypes<int32>::Id) { OutGroup = LexToString(ReadDataValue<int32>(Attribute)); }
		else if (Type == PCG::Private::MetadataTypes<int64>::Id) { OutGroup = LexToString(ReadDataValue<int64>(Attribute)); }
		else
		{
			OutError = FText::Format(LOCTEXT("SourceAttributeType",
				"Paint Vertex Color: input {0}: @Data attribute '{1}' (LOD Source Attribute) is not a soft object path, "
				"string, name or integer, so the input is painted on its own."), FText::AsNumber(InputIndex), FText::FromName(Name));
			return false;
		}
		if (OutGroup.IsEmpty())
		{
			OutError = FText::Format(LOCTEXT("SourceAttributeEmpty",
				"Paint Vertex Color: input {0}: @Data attribute '{1}' (LOD Source Attribute) is empty, so the input is "
				"painted on its own."), FText::AsNumber(InputIndex), FText::FromName(Name));
			return false;
		}
		return true;
	}

	const UPCGDynamicMeshData* GetMeshData(const UPCGData* Data)
	{
		const UPCGDynamicMeshSelectionData* Selection = Cast<const UPCGDynamicMeshSelectionData>(Data);
		return Selection ? Selection->GetSourceMeshData() : Cast<const UPCGDynamicMeshData>(Data);
	}

	TSet<int32> GetSelectedVertices(const UPCGDynamicMeshSelectionData* SelectionData)
	{
		TSet<int32> Vertices;
		if (SelectionData)
		{
			for (const uint64 EncodedID : SelectionData->GetSelection().Selection)
			{
				Vertices.Add(static_cast<int32>(UE::Geometry::FGeoSelectionID(EncodedID).GeometryID));
			}
		}
		return Vertices;
	}
}

using namespace PCGDynMeshPaintVertexColorPrivate;

#if WITH_EDITOR
FText UPCGDynMeshPaintVertexColorSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Paint Vertex Color");
}

FText UPCGDynMeshPaintVertexColorSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Evaluates one Painter while traversing the DynMesh once. Scalar values broadcast to every enabled write channel; color values contribute their matching enabled channels. "
		"With Paint LODs From Highest Detail, LODs of one mesh are painted once, on the most detailed, and the rest receive a copy.");
}
#endif

bool UPCGDynMeshPaintVertexColorSettings::GetRequiredSelectionDomain(
	UE::Geometry::EGeometryElementType& OutElementType) const
{
	OutElementType = UE::Geometry::EGeometryElementType::Vertex;
	return true;
}

FPCGDataTypeIdentifier UPCGDynMeshPaintVertexColorSettings::GetCurrentPinTypesID(
	const UPCGPin* InPin) const
{
	if (InPin && InPin->IsOutputPin())
	{
		return bOutputSelectionData
			? FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass())
			: FPCGDataTypeIdentifier(EPCGDataType::DynamicMesh);
	}

	return Super::GetCurrentPinTypesID(InPin);
}

FName UPCGDynMeshPaintVertexColorSettings::GetMainInputPinLabel() const
{
	return MeshPin;
}

TArray<FPCGPinProperties> UPCGDynMeshPaintVertexColorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins.Emplace_GetRef(
		PainterPin, FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGDynMeshPaintVertexColorSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshPaintVertexColorElement>();
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation>
UPCGDynMeshPaintVertexColorSettings::CreateProcessOperation(FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshPaintVertexColorOperation> Operation =
		MakeShared<FPCGUtilsDynMeshPaintVertexColorOperation>();
	if (!PCGUtilsDynMeshPainterFactories::GetSinglePainter(
		InContext, PainterPin, Operation->Painter, true))
	{
		return nullptr;
	}

	Operation->BaseColorMode = BaseColorMode;
	Operation->ConstantBaseColor = ConstantBaseColor;
	Operation->WriteChannels = WriteChannels;
	Operation->bMeshIsActorLocal = bMeshIsActorLocal;
	Operation->bKeepIslandsTogether = bKeepIslandsTogether;

	if (!bPaintLODsFromHighest || !InContext)
	{
		return Operation;
	}

	if (LODAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("LODAttributeNone",
			"Paint Vertex Color: LOD Attribute is None while Paint LODs From Highest Detail is on. Name the @Data "
			"attribute holding each input's LOD number."), InContext);
		return nullptr;
	}
	if (LODSourceAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("LODSourceAttributeNone",
			"Paint Vertex Color: LOD Source Attribute is None while Paint LODs From Highest Detail is on. Name the "
			"@Data attribute naming the mesh each input is a LOD of."), InContext);
		return nullptr;
	}

	// Resolved again here so a primary reached out of order is painted exactly as its own invocation would paint
	// it. The base has already rejected an unusable Selector pin before creating the operation.
	if (!FPCGUtilsDynMeshProcessFunctions::ResolveSelectorFromPin(
		InContext, PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin, Operation->Selector))
	{
		return nullptr;
	}
	Operation->SelectionPolicy = CaptureSelectionPolicy();

	// Concrete inputs, counted exactly as the process base counts them for Invocation.InputIndex.
	FPCGUtilsDynMeshPaintVertexColorOperation::FLODPlan& Plan = Operation->LODPlan;
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(GetMainInputPinLabel()))
	{
		if (Input.Data && Input.Data->IsA<UPCGUtilsDynMeshBuilderFactoryData>())
		{
			continue;
		}
		Plan.Inputs.Add(Input.Data);
	}
	Plan.PrimaryOfInput.Init(INDEX_NONE, Plan.Inputs.Num());
	Plan.LODOfInput.Init(0, Plan.Inputs.Num());
	Plan.GroupOfInput.SetNum(Plan.Inputs.Num());

	TMap<FString, TArray<int32>> Groups;
	for (int32 InputIndex = 0; InputIndex < Plan.Inputs.Num(); ++InputIndex)
	{
		const UPCGDynamicMeshData* MeshData = GetMeshData(Plan.Inputs[InputIndex]);
		if (!MeshData)
		{
			continue; // The base reports inputs with no mesh.
		}
		FText Error;
		if (!ReadGroup(MeshData, LODSourceAttribute, InputIndex, Plan.GroupOfInput[InputIndex], Error)
			|| !ReadLOD(MeshData, LODAttribute, InputIndex, Plan.LODOfInput[InputIndex], Error))
		{
			PCGLog::LogWarningOnGraph(Error, InContext);
			continue;
		}
		Groups.FindOrAdd(Plan.GroupOfInput[InputIndex]).Add(InputIndex);
	}

	for (const TPair<FString, TArray<int32>>& Group : Groups)
	{
		if (Group.Value.Num() < 2)
		{
			continue;
		}
		// The most detailed LOD paints; between equal LODs, the first input. Equal-LOD duplicates receive a copy.
		int32 Primary = Group.Value[0];
		for (const int32 Member : Group.Value)
		{
			if (Plan.LODOfInput[Member] < Plan.LODOfInput[Primary])
			{
				Primary = Member;
			}
		}
		for (const int32 Member : Group.Value)
		{
			Plan.PrimaryOfInput[Member] = Primary;
		}
	}
	return Operation;
}

bool FPCGUtilsDynMeshPaintVertexColorOperation::PaintInPlace(
	UPCGDynamicMeshData* MeshData, const UPCGDynamicMeshSelectionData* SelectionData,
	const UPCGDynamicMeshData* SourceMeshData, FPCGContext* Context, int32 InputIndex, int32 InputCount) const
{
	const FTransform LocalToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		Context, SourceMeshData ? SourceMeshData : MeshData, bMeshIsActorLocal);

	// A native Dynamic Mesh target: the canonical mesh IS the input mesh, evaluation writes in place, Commit
	// is a no-op. Routing through the shared target layer keeps this node on exactly the same write path as
	// every other Painter consumer.
	FPCGUtilsPainterDynMeshTarget Target(MeshData, LocalToWorld, InputIndex, InputCount);

	const TSet<int32> SelectedVertices = GetSelectedVertices(SelectionData);

	FPCGUtilsPainterGraphEvaluation Evaluation;
	Evaluation.Painter = Painter;
	Evaluation.WriteChannels = GetWriteChannels(WriteChannels);
	Evaluation.BaseColorSource = GetBaseColorSource(BaseColorMode);
	Evaluation.ConstantBaseColor =
		FVector4f(ConstantBaseColor.R, ConstantBaseColor.G, ConstantBaseColor.B, ConstantBaseColor.A);
	Evaluation.SelectedVertexIDs = SelectionData ? &SelectedVertices : nullptr;

	if (!PCGUtilsPainter::EvaluatePainterGraphOntoTarget(Target, Evaluation, Context))
	{
		return false;
	}

	return Target.Commit(Context);
}

const UE::Geometry::FDynamicMesh3* FPCGUtilsDynMeshPaintVertexColorOperation::GetPaintedPrimary(
	int32 PrimaryIndex, FPCGContext* Context, int32 InputCount) const
{
	if (const TSharedPtr<UE::Geometry::FDynamicMesh3>* Painted = PaintedPrimaries.Find(PrimaryIndex))
	{
		return Painted->Get();
	}
	if (FailedPrimaries.Contains(PrimaryIndex))
	{
		return nullptr;
	}

	// Paint the primary on a working copy, prepared exactly as the process base prepares its own invocation.
	const UPCGData* Input = LODPlan.Inputs[PrimaryIndex];
	const UPCGDynamicMeshSelectionData* InputSelection = Cast<const UPCGDynamicMeshSelectionData>(Input);
	const UPCGDynamicMeshData* SourceData = GetMeshData(Input);
	const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
	const UE::Geometry::FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
	if (!SourceMesh)
	{
		FailedPrimaries.Add(PrimaryIndex);
		return nullptr;
	}

	TArray<UMaterialInterface*> Materials;
	for (UMaterialInterface* Material : SourceData->GetMaterials())
	{
		Materials.Add(Material);
	}
	UPCGDynamicMeshData* Working = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
	Working->InitializeFromData(SourceData);
	Working->Initialize(UE::Geometry::FDynamicMesh3(*SourceMesh), MoveTemp(Materials));

	const UPCGData* Rebased = Working;
	if (InputSelection)
	{
		UPCGDynamicMeshSelectionData* RebasedSelection = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
		RebasedSelection->Initialize(Working, InputSelection->GetSelection());
		Rebased = RebasedSelection;
	}
	const FPCGUtilsDynMeshResolvedInput Resolved =
		FPCGUtilsDynMeshProcessFunctions::ResolveInput(Rebased, Selector, SelectionPolicy, Context);
	if (!Resolved.IsValid() || !PaintInPlace(Working, Resolved.SelectionData, SourceData, Context, PrimaryIndex, InputCount))
	{
		FailedPrimaries.Add(PrimaryIndex);
		return nullptr;
	}

	const TSharedPtr<UE::Geometry::FDynamicMesh3> Painted =
		MakeShared<UE::Geometry::FDynamicMesh3>(*Working->GetDynamicMesh()->GetMeshPtr());
	PaintedPrimaries.Add(PrimaryIndex, Painted);
	return Painted.Get();
}

bool FPCGUtilsDynMeshPaintVertexColorOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	UPCGDynamicMeshData* MeshData = Invocation.MeshData;
	UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetMutableDynamicMesh() : nullptr;
	UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
	if (!Mesh)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("MissingMesh", "Paint DynMesh Vertex Color requires a valid DynMesh input."),
			Invocation.Context);
		return false;
	}

	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Preserve;

	// The LOD plan describes this node's own inputs. Anything else (a Builder chain evaluated elsewhere, an input
	// in no group) is painted on its own.
	const int32 InputIndex = Invocation.InputIndex;
	const bool bPlanned = LODPlan.PrimaryOfInput.IsValidIndex(InputIndex)
		&& LODPlan.PrimaryOfInput[InputIndex] != INDEX_NONE
		&& Invocation.SourceMeshData == GetMeshData(LODPlan.Inputs[InputIndex]);
	if (!bPlanned)
	{
		return PaintInPlace(MeshData, Invocation.SelectionData, Invocation.SourceMeshData, Invocation.Context,
			InputIndex, Invocation.InputCount);
	}

	const int32 PrimaryIndex = LODPlan.PrimaryOfInput[InputIndex];
	if (PrimaryIndex == InputIndex)
	{
		if (const TSharedPtr<UE::Geometry::FDynamicMesh3>* Painted = PaintedPrimaries.Find(PrimaryIndex))
		{
			// Already painted for a lower LOD that came first: the same input, so the same result.
			*Mesh = **Painted;
			return true;
		}
		if (FailedPrimaries.Contains(PrimaryIndex))
		{
			return false;
		}
		if (!PaintInPlace(MeshData, Invocation.SelectionData, Invocation.SourceMeshData, Invocation.Context,
			InputIndex, Invocation.InputCount))
		{
			FailedPrimaries.Add(PrimaryIndex);
			return false;
		}
		PaintedPrimaries.Add(PrimaryIndex, MakeShared<UE::Geometry::FDynamicMesh3>(*Mesh));
		return true;
	}

	const UE::Geometry::FDynamicMesh3* Painted = GetPaintedPrimary(PrimaryIndex, Invocation.Context, Invocation.InputCount);
	if (!Painted)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("PrimaryFailed",
			"Paint Vertex Color: input {0} (LOD {1} of '{2}') was not painted, because its most detailed LOD, input {3} "
			"(LOD {4}), failed to paint."),
			FText::AsNumber(InputIndex), FText::AsNumber(LODPlan.LODOfInput[InputIndex]),
			FText::FromString(LODPlan.GroupOfInput[InputIndex]), FText::AsNumber(PrimaryIndex),
			FText::AsNumber(LODPlan.LODOfInput[PrimaryIndex])), Invocation.Context);
		return false;
	}

	const TSet<int32> SelectedVertices = GetSelectedVertices(Invocation.SelectionData);
	PCGUtilsPainterLODTransfer::FTransferOptions Options;
	Options.Channels = GetWriteChannels(WriteChannels);
	Options.BaseColorSource = GetBaseColorSource(BaseColorMode);
	Options.ConstantBaseColor = FVector4f(ConstantBaseColor.R, ConstantBaseColor.G, ConstantBaseColor.B, ConstantBaseColor.A);
	Options.bKeepIslandsTogether = bKeepIslandsTogether;
	Options.SelectedVertexIDs = Invocation.SelectionData ? &SelectedVertices : nullptr;

	PCGUtilsPainterLODTransfer::FTransferResult Result;
	if (!PCGUtilsPainterLODTransfer::TransferPaint(*Painted, *Mesh, Options, Result))
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("TransferFailed",
			"Paint Vertex Color: input {0} (LOD {1} of '{2}') could not take the paint of input {3}: the painted LOD has "
			"no triangles or no vertex colors."),
			FText::AsNumber(InputIndex), FText::AsNumber(LODPlan.LODOfInput[InputIndex]),
			FText::FromString(LODPlan.GroupOfInput[InputIndex]), FText::AsNumber(PrimaryIndex)), Invocation.Context);
		return false;
	}
	if (Result.NumFailed > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("TransferGaps",
			"Paint Vertex Color: input {0} (LOD {1} of '{2}'): {3} of {4} vertices found no painted surface and kept "
			"the base color."),
			FText::AsNumber(InputIndex), FText::AsNumber(LODPlan.LODOfInput[InputIndex]),
			FText::FromString(LODPlan.GroupOfInput[InputIndex]), FText::AsNumber(Result.NumFailed),
			FText::AsNumber(Result.NumFailed + Result.NumTransferred)), Invocation.Context);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
