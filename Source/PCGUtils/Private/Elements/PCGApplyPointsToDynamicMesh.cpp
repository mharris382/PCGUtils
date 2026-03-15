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
		EPCGDataType::DynamicMesh, true, false).SetRequiredPin();
	Props.Emplace_GetRef(PCGApplyPointsToDynamicMeshConstants::InPointsLabel,
		EPCGDataType::Point, true, false).SetRequiredPin();
	return Props;
}

TArray<FPCGPinProperties> UPCGApplyPointsToDynamicMeshSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Props;
	Props.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, false);
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

bool FPCGApplyPointsToDynamicMeshElement::ProcessSinglePair(
    FPCGContext* InContext,
    const FPCGTaggedData& MeshTaggedData,
    const FPCGTaggedData& PointTaggedData,
    const UPCGApplyPointsToDynamicMeshSettings* Settings) const
{
    const UPCGDynamicMeshData* InMeshData =
        Cast<const UPCGDynamicMeshData>(MeshTaggedData.Data);
    const UPCGBasePointData* InPointData =
        Cast<const UPCGBasePointData>(PointTaggedData.Data);
    FPCGApplyPointsToDynamicMeshContext* Context =
        static_cast<FPCGApplyPointsToDynamicMeshContext*>(InContext);
    check(Context);

    if (!InMeshData || !InPointData)
    {
        PCGE_LOG(Error, GraphAndLog,LOCTEXT("BadInputTypes", "ApplyPointsToDynamicMesh: input data is wrong type."));
        return false;
    }

    const FDynamicMesh3* SrcRaw = InMeshData->GetDynamicMesh()
        ? InMeshData->GetDynamicMesh()->GetMeshPtr() : nullptr;

    if (!SrcRaw)
    {
        PCGE_LOG(Warning, GraphAndLog,LOCTEXT("NullMesh", "ApplyPointsToDynamicMesh: source mesh is null."));
        return false;
    }

    const int32 VertexCount = SrcRaw->VertexCount();
    const int32 PointCount = InPointData->GetNumPoints();

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
            InContext->OutputData.TaggedData.Add(MeshTaggedData);
            return true;
        }
        else
        {
            PCGE_LOG(Error, GraphAndLog, Msg);
            return false;
        }
    }

    // ── CopyOrSteal mutable mesh ──────────────────────────────────────────────
    // We pass a non-const copy of MeshTaggedData since CopyOrSteal takes it by value.
    FPCGTaggedData MeshTaggedDataCopy = MeshTaggedData;
    UPCGDynamicMeshData* OutMeshData = CopyOrSteal(MeshTaggedDataCopy, InContext);
    if (!OutMeshData)
    {
        PCGE_LOG(Warning, GraphAndLog,
            LOCTEXT("CopyFailed", "ApplyPointsToDynamicMesh: CopyOrSteal failed."));
        return false;
    }

    FDynamicMesh3* RawMesh = OutMeshData->GetMutableDynamicMesh()->GetMeshPtr();
    if (!RawMesh)
    {
        return false;
    }

    // ── Zip positions (and optionally normals) ────────────────────────────────

    const TConstPCGValueRange<FTransform> TransformRange =
        InPointData->GetConstTransformValueRange();

    const bool bCopyNormals = Settings->bCopyRotationAsNormals
        && RawMesh->HasAttributes()
        && RawMesh->Attributes()->PrimaryNormals() != nullptr;

    FDynamicMeshNormalOverlay* NormalOverlay = bCopyNormals
        ? RawMesh->Attributes()->PrimaryNormals() : nullptr;

    int32 PointIdx = 0;
    for (int32 VID : RawMesh->VertexIndicesItr())
    {
        const FTransform& PT = TransformRange[PointIdx];

        RawMesh->SetVertex(VID, FVector3d(PT.GetLocation()));

        if (NormalOverlay)
        {
            const FVector Normal = PT.GetRotation().GetUpVector();
            const FVector3f Normalf(Normal.X, Normal.Y, Normal.Z);

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

    OutMeshData->GetMutableDynamicMesh(); // touch dirty flag

    // ── Emit output, preserving mesh input tags ───────────────────────────────
    FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef(MeshTaggedData);
    Output.Data = OutMeshData;

    return true;
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

    TArray<FPCGTaggedData> MeshInputs =
        InContext->InputData.GetInputsByPin(PCGApplyPointsToDynamicMeshConstants::InDynamicMeshLabel);
    TArray<FPCGTaggedData> PointInputs =
        InContext->InputData.GetInputsByPin(PCGApplyPointsToDynamicMeshConstants::InPointsLabel);

    if (MeshInputs.IsEmpty() || PointInputs.IsEmpty())
    {
        PCGE_LOG(Warning, GraphAndLog,
            LOCTEXT("MissingInputs", "ApplyPointsToDynamicMesh: missing mesh or point input."));
        return true;
    }

    if (MeshInputs.Num() != PointInputs.Num())
    {
        PCGE_LOG(Error, GraphAndLog,
            FText::Format(
                LOCTEXT("SetCountMismatch",
                    "ApplyPointsToDynamicMesh: mesh input count ({0}) != point input count ({1}). "
                    "Inputs must be paired 1:1 in matching order."),
                FText::AsNumber(MeshInputs.Num()),
                FText::AsNumber(PointInputs.Num())));
        return true;
    }

    for (int32 i = 0; i < MeshInputs.Num(); ++i)
    {
        ProcessSinglePair(InContext, MeshInputs[i], PointInputs[i], Settings);
    }

    return true;
}

#undef LOCTEXT_NAMESPACE