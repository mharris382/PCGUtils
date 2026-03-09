#include "Elements/PCGApplyPointsToDynamicMesh.h"

#include "PCGPin.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGBasePointData.h"

#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

#define LOCTEXT_NAMESPACE "PCGApplyPointsToDynamicMeshElement"

using namespace UE::Geometry;

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────

TArray<FPCGPinProperties> UPCGApplyPointsToDynamicMeshSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Props;
	Props.Emplace_GetRef(PCGApplyPointsToDynamicMeshConstants::InDynamicMeshLabel,
		EPCGDataType::DynamicMesh, false, false).SetRequiredPin();
	Props.Emplace_GetRef(PCGApplyPointsToDynamicMeshConstants::InPointsLabel,
		EPCGDataType::Point, false, false).SetRequiredPin();
	return Props;
}

TArray<FPCGPinProperties> UPCGApplyPointsToDynamicMeshSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Props;
	Props.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, false, false);
	return Props;
}

FPCGElementPtr UPCGApplyPointsToDynamicMeshSettings::CreateElement() const
{
	return MakeShared<FPCGApplyPointsToDynamicMeshElement>();
}

// ─────────────────────────────────────────────────────────────────────────────
// Element
// ─────────────────────────────────────────────────────────────────────────────

FPCGContext* FPCGApplyPointsToDynamicMeshElement::CreateContext()
{
	return new FPCGApplyPointsToDynamicMeshContext();
}

bool FPCGApplyPointsToDynamicMeshElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGApplyPointsToDynamicMeshElement::ExecuteInternal);

	FPCGApplyPointsToDynamicMeshContext* Context =
		static_cast<FPCGApplyPointsToDynamicMeshContext*>(InContext);
	check(Context);

	const UPCGApplyPointsToDynamicMeshSettings* Settings =
		InContext->GetInputSettings<UPCGApplyPointsToDynamicMeshSettings>();
	check(Settings);

	// ── Fetch inputs ─────────────────────────────────────────────────────────

	TArray<FPCGTaggedData> MeshInputs  =
		InContext->InputData.GetInputsByPin(PCGApplyPointsToDynamicMeshConstants::InDynamicMeshLabel);
	TArray<FPCGTaggedData> PointInputs =
		InContext->InputData.GetInputsByPin(PCGApplyPointsToDynamicMeshConstants::InPointsLabel);

	if (MeshInputs.IsEmpty() || PointInputs.IsEmpty())
	{
		PCGE_LOG(Warning, GraphAndLog,
			LOCTEXT("MissingInputs", "ApplyPointsToDynamicMesh: missing mesh or point input."));
		return true;
	}

	const UPCGDynamicMeshData* InMeshData =
		Cast<const UPCGDynamicMeshData>(MeshInputs[0].Data);
	const UPCGBasePointData* InPointData =
		Cast<const UPCGBasePointData>(PointInputs[0].Data);

	if (!InMeshData || !InPointData)
	{
		PCGE_LOG(Error, GraphAndLog,
			LOCTEXT("BadInputTypes", "ApplyPointsToDynamicMesh: input data is wrong type."));
		return true;
	}

	// ── Validate count match ──────────────────────────────────────────────────

	// Get vertex count from the raw mesh
	const FDynamicMesh3* SrcRaw = InMeshData->GetDynamicMesh()
		? InMeshData->GetDynamicMesh()->GetMeshPtr() : nullptr;

	if (!SrcRaw)
	{
		PCGE_LOG(Warning, GraphAndLog,
			LOCTEXT("NullMesh", "ApplyPointsToDynamicMesh: source mesh is null."));
		return true;
	}

	const int32 VertexCount = SrcRaw->VertexCount();
	const int32 PointCount  = InPointData->GetNumPoints();

	if (VertexCount != PointCount)
	{
		const FText Msg = FText::Format(
			LOCTEXT("CountMismatch",
				"ApplyPointsToDynamicMesh: vertex count ({0}) != point count ({1}). "
				"Ensure no nodes between To Points and this node add/remove points."),
			FText::AsNumber(VertexCount), FText::AsNumber(PointCount));

		if (Settings->bPassthroughOnCountMismatch)
		{
			PCGE_LOG(Warning, GraphAndLog, Msg);
			// Pass mesh through unchanged
			InContext->OutputData.TaggedData.Add(MeshInputs[0]);
			return true;
		}
		else
		{
			PCGE_LOG(Error, GraphAndLog, Msg);
			return true;
		}
	}

	// ── CopyOrSteal mutable mesh copy ─────────────────────────────────────────

	UPCGDynamicMeshData* OutMeshData = CopyOrSteal(MeshInputs[0], InContext);
	if (!OutMeshData)
	{
		PCGE_LOG(Warning, GraphAndLog,
			LOCTEXT("CopyFailed", "ApplyPointsToDynamicMesh: CopyOrSteal failed."));
		return true;
	}

	FDynamicMesh3* RawMesh = OutMeshData->GetMutableDynamicMesh()->GetMeshPtr();
	if (!RawMesh)
	{
		return true;
	}

	// ── Build point position (and optionally rotation) arrays ─────────────────
	// Read all at once before touching the mesh.

	const TConstPCGValueRange<FTransform> TransformRange =
		InPointData->GetConstTransformValueRange();

	// ── Zip: VertexIndicesItr() order == ToPointData() order ─────────────────
	// Both iterate VertexIndicesItr() internally in ascending VID order.
	// See PCGDynamicMeshData.cpp ToBasePointData — straight VertexIndicesItr loop.

	const bool bCopyNormals = Settings->bCopyRotationAsNormals
		&& RawMesh->HasAttributes()
		&& RawMesh->Attributes()->PrimaryNormals() != nullptr;

	FDynamicMeshNormalOverlay* NormalOverlay = bCopyNormals
		? RawMesh->Attributes()->PrimaryNormals() : nullptr;

	int32 PointIdx = 0;
	for (int32 VID : RawMesh->VertexIndicesItr())
	{
		const FTransform& PT = TransformRange[PointIdx];

		// Copy position
		RawMesh->SetVertex(VID, FVector3d(PT.GetLocation()));

		// Optionally copy rotation as normal (Z-axis of point rotation = surface normal
		// after a Project node)
		if (NormalOverlay)
		{
			const FVector Normal = PT.GetRotation().GetUpVector();
			const FVector3f Normalf(Normal.X, Normal.Y, Normal.Z);

			// Update all overlay elements that reference this VID
			// (a VID can appear in multiple triangle corners with different overlay EIDs)
			RawMesh->EnumerateVertexTriangles(VID, [&](int32 TID)
			{
				FIndex3i TriVerts = RawMesh->GetTriangle(TID);
				FIndex3i NormElems = NormalOverlay->GetTriangle(TID);
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					if (TriVerts[Corner] == VID && NormalOverlay->IsElement(NormElems[Corner]))
					{
						NormalOverlay->SetElement(NormElems[Corner], Normalf);
					}
				}
			});
		}

		++PointIdx;
	}

	// Bounds are now dirty — GetMutableDynamicMesh already set the flag,
	// but we bypassed it by going through GetMeshPtr directly. Signal it manually.
	OutMeshData->GetMutableDynamicMesh(); // touches the dirty flag

	FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef(MeshInputs[0]);
	Output.Data = OutMeshData;

	return true;
}

#undef LOCTEXT_NAMESPACE