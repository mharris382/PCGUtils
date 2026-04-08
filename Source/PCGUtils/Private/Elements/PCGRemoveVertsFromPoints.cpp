#include "Elements/PCGRemoveVertsFromPoints.h"

#include "PCGPin.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"

#define LOCTEXT_NAMESPACE "PCGRemoveVertsFromPointsElement"

using namespace UE::Geometry;

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────

TArray<FPCGPinProperties> UPCGRemoveVertsFromPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Props;
	Props.Emplace_GetRef(PCGRemoveVertsFromPointsConstants::InDynamicMeshLabel,
		EPCGDataType::DynamicMesh, true, false).SetRequiredPin();
	Props.Emplace_GetRef(PCGRemoveVertsFromPointsConstants::InPointsLabel,
		EPCGDataType::Point, true, false).SetRequiredPin();
	return Props;
}

TArray<FPCGPinProperties> UPCGRemoveVertsFromPointsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Props;
	Props.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, false);
	return Props;
}

FPCGElementPtr UPCGRemoveVertsFromPointsSettings::CreateElement() const
{
	return MakeShared<FPCGRemoveVertsFromPointsElement>();
}

// ─────────────────────────────────────────────────────────────────────────────
// Element
// ─────────────────────────────────────────────────────────────────────────────

FPCGContext* FPCGRemoveVertsFromPointsElement::CreateContext()
{
	return new FPCGRemoveVertsFromPointsContext();
}

bool FPCGRemoveVertsFromPointsElement::ProcessSinglePair(
	FPCGContext* InContext,
	const FPCGTaggedData& MeshTaggedData,
	const FPCGTaggedData& PointTaggedData,
	const UPCGRemoveVertsFromPointsSettings* Settings) const
{
	const UPCGDynamicMeshData* InMeshData  = Cast<const UPCGDynamicMeshData>(MeshTaggedData.Data);
	const UPCGBasePointData*   InPointData = Cast<const UPCGBasePointData>(PointTaggedData.Data);

	if (!InMeshData || !InPointData)
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("BadInputTypes", "RemoveVertsFromPoints: input data is wrong type."), InContext);
		return false;
	}

	const FDynamicMesh3* SrcRaw = InMeshData->GetDynamicMesh()
		? InMeshData->GetDynamicMesh()->GetMeshPtr() : nullptr;

	if (!SrcRaw)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NullMesh", "RemoveVertsFromPoints: source mesh is null."), InContext);
		return false;
	}

	// ── Build VID accessor ────────────────────────────────────────────────────

	const FPCGAttributePropertyInputSelector Fixed = Settings->VertexIndexAttribute.CopyAndFixLast(InPointData);
	TUniquePtr<const IPCGAttributeAccessor>     Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InPointData, Fixed);
	TUniquePtr<const IPCGAttributeAccessorKeys> Keys     = PCGAttributeAccessorHelpers::CreateConstKeys(InPointData, Fixed);

	if (!Accessor || !Keys)
	{
		PCGLog::Metadata::LogFailToCreateAccessorError(Fixed, InContext);
		// Pass mesh through unchanged so the graph doesn't go dark
		InContext->OutputData.TaggedData.Add(MeshTaggedData);
		return true;
	}

	// ── CopyOrSteal mutable mesh ──────────────────────────────────────────────

	FPCGTaggedData MeshTaggedDataCopy = MeshTaggedData;
	UPCGDynamicMeshData* OutMeshData = CopyOrSteal(MeshTaggedDataCopy, InContext);
	if (!OutMeshData)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("CopyFailed", "RemoveVertsFromPoints: CopyOrSteal failed."), InContext);
		return false;
	}

	FDynamicMesh3* RawMesh = OutMeshData->GetMutableDynamicMesh()->GetMeshPtr();
	if (!RawMesh)
		return false;

	// ── Collect VIDs and adjacent TIDs ───────────────────────────────────────

	const int32 PointCount = InPointData->GetNumPoints();

	TSet<int32> TIDsToRemove;
	TIDsToRemove.Reserve(PointCount * 6); // rough guess: ~6 tris per vert on average

	int32 InvalidCount = 0;

	for (int32 i = 0; i < PointCount; ++i)
	{
		int32 VID = INDEX_NONE;
		Accessor->Get<int32>(VID, i, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);

		if (!RawMesh->IsVertex(VID))
		{
			++InvalidCount;
			continue;
		}

		RawMesh->EnumerateVertexTriangles(VID, [&](int32 TID)
		{
			TIDsToRemove.Add(TID);
		});
	}

	if (Settings->bWarnOnInvalidIndex && InvalidCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("InvalidVIDs", "RemoveVertsFromPoints: {0} point(s) contained invalid vertex IDs and were skipped."),
			FText::AsNumber(InvalidCount)), InContext);
	}

	// ── Remove triangles (with isolated-vertex cleanup) ───────────────────────
	//
	// bRemoveIsolatedVertices = true: after each triangle is removed, any of
	// its three vertices that now have zero adjacent triangles are also deleted.
	// This covers both the explicitly-targeted VIDs and any boundary vertices
	// that become dangling as a side-effect.

	for (int32 TID : TIDsToRemove)
	{
		if (RawMesh->IsTriangle(TID))
			RawMesh->RemoveTriangle(TID, /*bRemoveIsolatedVertices=*/true, /*bPreserveManifold=*/false);
	}

	// ── Emit output ───────────────────────────────────────────────────────────

	OutMeshData->GetMutableDynamicMesh(); // touch dirty flag

	FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef(MeshTaggedData);
	Output.Data = OutMeshData;

	return true;
}

bool FPCGRemoveVertsFromPointsElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGRemoveVertsFromPointsElement::ExecuteInternal);

	check(InContext);
	const UPCGRemoveVertsFromPointsSettings* Settings =
		InContext->GetInputSettings<UPCGRemoveVertsFromPointsSettings>();
	check(Settings);

	TArray<FPCGTaggedData> MeshInputs =
		InContext->InputData.GetInputsByPin(PCGRemoveVertsFromPointsConstants::InDynamicMeshLabel);
	TArray<FPCGTaggedData> PointInputs =
		InContext->InputData.GetInputsByPin(PCGRemoveVertsFromPointsConstants::InPointsLabel);

	if (MeshInputs.IsEmpty() || PointInputs.IsEmpty())
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("MissingInputs", "RemoveVertsFromPoints: missing mesh or point input."), InContext);
		return true;
	}

	if (MeshInputs.Num() != PointInputs.Num())
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("SetCountMismatch",
				"RemoveVertsFromPoints: mesh input count ({0}) != point input count ({1}). "
				"Inputs must be paired 1:1 in matching order."),
			FText::AsNumber(MeshInputs.Num()),
			FText::AsNumber(PointInputs.Num())), InContext);
		return true;
	}

	for (int32 i = 0; i < MeshInputs.Num(); ++i)
	{
		ProcessSinglePair(InContext, MeshInputs[i], PointInputs[i], Settings);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
