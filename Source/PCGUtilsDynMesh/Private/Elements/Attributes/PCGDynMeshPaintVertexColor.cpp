// Copyright Max Harris

#include "Elements/Attributes/PCGDynMeshPaintVertexColor.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/PCGUtilsDynMeshAttributeHelpers.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Selections/GeometrySelection.h"
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
	return LOCTEXT("Title", "Paint DynMesh Vertex Color");
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

	const FVector4f ConstantColor(
		ConstantBaseColor.R, ConstantBaseColor.G, ConstantBaseColor.B, ConstantBaseColor.A);
	UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay =
		PCGUtilsDynMeshAttributeHelpers::EnsurePrimaryColorOverlay(*Mesh, ConstantColor);
	if (!ColorOverlay)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("ColorOverlayFailure", "Paint DynMesh Vertex Color could not initialize the primary color overlay."),
			Invocation.Context);
		return false;
	}

	const FTransform LocalToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		Invocation.Context,
		Invocation.SourceMeshData ? Invocation.SourceMeshData : MeshData,
		bMeshIsActorLocal);
	const FPCGUtilsDynMeshPainterEvaluationContext PainterContext(
		MeshData, *Mesh, LocalToWorld, Invocation.InputIndex, Invocation.InputCount);

	TSharedPtr<FPCGUtilsDynMeshPainterOperation> PainterOperation =
		Painter ? Painter->CreateOperation(Invocation.Context) : nullptr;
	if (!PainterOperation || !PainterOperation->Initialize(PainterContext))
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("PainterInitializationFailure", "Paint DynMesh Vertex Color could not initialize its Painter."),
			Invocation.Context);
		return false;
	}
	const EPCGUtilsDynMeshPainterColorChannel RequestedChannels =
		GetWriteChannels(WriteChannels);

	TSet<int32> SelectedVertices;
	if (Invocation.SelectionData)
	{
		for (const uint64 EncodedID : Invocation.SelectionData->GetSelection().Selection)
		{
			SelectedVertices.Add(static_cast<int32>(UE::Geometry::FGeoSelectionID(EncodedID).GeometryID));
		}
	}

	for (const int32 VertexID : Mesh->VertexIndicesItr())
	{
		if (Invocation.SelectionData && !SelectedVertices.Contains(VertexID))
		{
			continue;
		}

		FPCGUtilsDynMeshPainterSample Sample;
		Sample.VertexID = VertexID;
		Sample.LocalPosition = FVector(Mesh->GetVertex(VertexID));
		Sample.WorldPosition = LocalToWorld.TransformPosition(Sample.LocalPosition);
		if (Mesh->HasVertexNormals())
		{
			Sample.LocalNormal = FVector(Mesh->GetVertexNormal(VertexID)).GetSafeNormal();
		}
		Sample.WorldNormal = LocalToWorld.TransformVectorNoScale(Sample.LocalNormal).GetSafeNormal();

		FVector4f Color = BaseColorMode == EPCGUtilsDynMeshPainterBaseColorMode::Existing
			? PCGUtilsDynMeshAttributeHelpers::GetVertexColor(
				*Mesh, *ColorOverlay, VertexID, ConstantColor)
			: ConstantColor;
		PCGUtilsDynMeshPainters::ResolveValueToColor(
			PainterOperation->Evaluate(Sample), RequestedChannels, Color);

		PCGUtilsDynMeshAttributeHelpers::SetVertexColor(*Mesh, *ColorOverlay, VertexID, Color);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
