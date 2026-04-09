#include "Elements/PCGRemoveVertsFromPoints.h"

#include "PCGPin.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGBasePointData.h"

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

	// ── Build float-position → VID lookup ────────────────────────────────────
	//
	// PCG stores point positions as float. DynamicMesh3 stores vertex positions
	// as double. When "To Points" converts the mesh, it downcasts double→float.
	// We do the same downcast here so the comparison is exact.
	//
	// Using FVector3f as the map key (bitwise equality). This is safe because
	// both sides perform the same double→float truncation.

	TMap<FVector3f, int32> PosToVID;
	PosToVID.Reserve(SrcRaw->VertexCount());

	for (int32 VID : SrcRaw->VertexIndicesItr())
	{
		const FVector3d V = SrcRaw->GetVertex(VID);
		PosToVID.Add(FVector3f((float)V.X, (float)V.Y, (float)V.Z), VID);
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

	// ── Match points → VIDs, collect adjacent TIDs ───────────────────────────

	const int32 PointCount = InPointData->GetNumPoints();
	const TConstPCGValueRange<FTransform> TransformRange = InPointData->GetConstTransformValueRange();

	TSet<int32> TIDsToRemove;
	TIDsToRemove.Reserve(PointCount * 6);

	int32 UnmatchedCount = 0;

	for (int32 i = 0; i < PointCount; ++i)
	{
		const FVector Loc = TransformRange[i].GetLocation();
		const FVector3f Locf((float)Loc.X, (float)Loc.Y, (float)Loc.Z);

		const int32* VIDPtr = PosToVID.Find(Locf);
		if (!VIDPtr)
		{
			++UnmatchedCount;
			continue;
		}

		const int32 VID = *VIDPtr;
		if (!RawMesh->IsVertex(VID))
			continue;

		RawMesh->EnumerateVertexTriangles(VID, [&](int32 TID)
		{
			TIDsToRemove.Add(TID);
		});
	}

	if (Settings->bWarnOnUnmatchedPoints && UnmatchedCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("UnmatchedPoints",
				"RemoveVertsFromPoints: {0} point(s) did not match any mesh vertex by position and were skipped. "
				"Ensure point positions were not modified between the To Points conversion and this node."),
			FText::AsNumber(UnmatchedCount)), InContext);
	}

	// ── Remove triangles (with isolated-vertex cleanup) ───────────────────────
	//
	// bRemoveIsolatedVertices = true: after each triangle is removed, any vertex
	// of that triangle that now has zero adjacent triangles is also deleted.
	// This covers the target VIDs and any boundary vertices that become dangling.

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
