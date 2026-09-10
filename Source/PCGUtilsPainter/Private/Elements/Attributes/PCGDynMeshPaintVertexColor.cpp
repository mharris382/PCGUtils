// Copyright Max Harris

#include "Elements/Attributes/PCGDynMeshPaintVertexColor.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Selections/GeometrySelection.h"
#include "Target/PCGUtilsPainterDynMeshTarget.h"
#include "Target/PCGUtilsPainterTarget.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshPaintVertexColor"

namespace
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
}

#if WITH_EDITOR
FText UPCGDynMeshPaintVertexColorSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh|Paint Vertex Color");
}

FText UPCGDynMeshPaintVertexColorSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Evaluates one Painter while traversing the DynMesh once. Scalar values broadcast to every enabled write channel; color values contribute their matching enabled channels.");
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
	return Operation;
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

	const FTransform LocalToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		Invocation.Context,
		Invocation.SourceMeshData ? Invocation.SourceMeshData : MeshData,
		bMeshIsActorLocal);

	// A native Dynamic Mesh target: the canonical mesh IS the input mesh, evaluation writes in place, Commit
	// is a no-op. Routing through the shared target layer keeps this node on exactly the same write path as
	// every other Painter consumer.
	FPCGUtilsPainterDynMeshTarget Target(MeshData, LocalToWorld, Invocation.InputIndex, Invocation.InputCount);

	TSet<int32> SelectedVertices;
	if (Invocation.SelectionData)
	{
		for (const uint64 EncodedID : Invocation.SelectionData->GetSelection().Selection)
		{
			SelectedVertices.Add(static_cast<int32>(UE::Geometry::FGeoSelectionID(EncodedID).GeometryID));
		}
	}

	FPCGUtilsPainterGraphEvaluation Evaluation;
	Evaluation.Painter = Painter;
	Evaluation.WriteChannels = GetWriteChannels(WriteChannels);
	Evaluation.BaseColorSource = (BaseColorMode == EPCGUtilsDynMeshPainterBaseColorMode::Existing)
		? EPCGUtilsPainterBaseColorSource::CanonicalExisting
		: EPCGUtilsPainterBaseColorSource::Constant;
	Evaluation.ConstantBaseColor =
		FVector4f(ConstantBaseColor.R, ConstantBaseColor.G, ConstantBaseColor.B, ConstantBaseColor.A);
	Evaluation.SelectedVertexIDs = Invocation.SelectionData ? &SelectedVertices : nullptr;

	if (!PCGUtilsPainter::EvaluatePainterGraphOntoTarget(Target, Evaluation, Invocation.Context))
	{
		return false;
	}

	return Target.Commit(Invocation.Context);
}

#undef LOCTEXT_NAMESPACE
