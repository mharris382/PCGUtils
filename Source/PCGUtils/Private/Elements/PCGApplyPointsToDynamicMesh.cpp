#include "Elements/PCGApplyPointsToDynamicMesh.h"

#include "PCGPin.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

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

// ─────────────────────────────────────────────────────────────────────────────
// Helpers (file-local)
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/**
	 * Ensure the mesh has an attributes set and a primary color overlay.
	 * If the overlay is newly created (empty), initialize one element per vertex
	 * (shared across all triangles → no seams) with a default white color.
	 * Returns the color overlay, never null after this call.
	 */
	FDynamicMeshColorOverlay* EnsureColorOverlay(FDynamicMesh3* Mesh)
	{
		if (!Mesh->HasAttributes())
			Mesh->EnableAttributes();

		FDynamicMeshAttributeSet* Attrs = Mesh->Attributes();
		if (!Attrs->PrimaryColors())
			Attrs->EnablePrimaryColors();

		FDynamicMeshColorOverlay* Overlay = Attrs->PrimaryColors();
		check(Overlay);

		if (Overlay->ElementCount() == 0)
		{
			// Build one overlay element per vertex (seamless)
			TArray<int32> VIDToElem;
			VIDToElem.Init(INDEX_NONE, Mesh->MaxVertexID());
			for (int32 VID : Mesh->VertexIndicesItr())
				VIDToElem[VID] = Overlay->AppendElement(FVector4f(1.f, 1.f, 1.f, 1.f));

			for (int32 TID : Mesh->TriangleIndicesItr())
			{
				FIndex3i T = Mesh->GetTriangle(TID);
				Overlay->SetTriangle(TID, FIndex3i(VIDToElem[T[0]], VIDToElem[T[1]], VIDToElem[T[2]]));
			}
		}

		return Overlay;
	}

	/**
	 * Ensure the mesh has a UV overlay at the given layer index.
	 * If it doesn't exist it is created and initialized with one element per
	 * vertex (no seams), UV defaulting to (0,0).
	 * Returns the UV overlay, never null after this call.
	 */
	FDynamicMeshUVOverlay* EnsureUVOverlay(FDynamicMesh3* Mesh, int32 LayerIndex)
	{
		if (!Mesh->HasAttributes())
			Mesh->EnableAttributes();

		FDynamicMeshAttributeSet* Attrs = Mesh->Attributes();
		const int32 NumNeeded = LayerIndex + 1;
		if (Attrs->NumUVLayers() < NumNeeded)
			Attrs->SetNumUVLayers(NumNeeded);

		FDynamicMeshUVOverlay* Overlay = Attrs->GetUVLayer(LayerIndex);
		check(Overlay);

		if (Overlay->ElementCount() == 0)
		{
			TArray<int32> VIDToElem;
			VIDToElem.Init(INDEX_NONE, Mesh->MaxVertexID());
			for (int32 VID : Mesh->VertexIndicesItr())
				VIDToElem[VID] = Overlay->AppendElement(FVector2f(0.f, 0.f));

			for (int32 TID : Mesh->TriangleIndicesItr())
			{
				FIndex3i T = Mesh->GetTriangle(TID);
				Overlay->SetTriangle(TID, FIndex3i(VIDToElem[T[0]], VIDToElem[T[1]], VIDToElem[T[2]]));
			}
		}

		return Overlay;
	}

	/**
	 * Set all overlay elements that belong to VID to NewValue.
	 * Works for any TDynamicMeshVectorOverlay type (colors, UVs, etc.).
	 */
	template<typename OverlayType, typename ValueType>
	void SetVertexOverlayElements(FDynamicMesh3* Mesh, OverlayType* Overlay, int32 VID, const ValueType& NewValue)
	{
		Mesh->EnumerateVertexTriangles(VID, [&](int32 TID)
		{
			if (!Overlay->IsSetTriangle(TID)) return;
			FIndex3i TVerts = Mesh->GetTriangle(TID);
			FIndex3i TElems = Overlay->GetTriangle(TID);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				if (TVerts[Corner] == VID && Overlay->IsElement(TElems[Corner]))
					Overlay->SetElement(TElems[Corner], NewValue);
			}
		});
	}

	/**
	 * Read the first overlay element value for VID. Returns DefaultValue if none found.
	 */
	template<typename OverlayType, typename ValueType>
	ValueType GetFirstVertexOverlayElement(FDynamicMesh3* Mesh, OverlayType* Overlay, int32 VID, const ValueType& DefaultValue)
	{
		ValueType Result = DefaultValue;
		bool bFound = false;
		Mesh->EnumerateVertexTriangles(VID, [&](int32 TID)
		{
			if (bFound || !Overlay->IsSetTriangle(TID)) return;
			FIndex3i TVerts = Mesh->GetTriangle(TID);
			FIndex3i TElems = Overlay->GetTriangle(TID);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				if (TVerts[Corner] == VID && Overlay->IsElement(TElems[Corner]))
				{
					Result = Overlay->GetElement(TElems[Corner]);
					bFound = true;
					break;
				}
			}
		});
		return Result;
	}
} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ProcessSinglePair
// ─────────────────────────────────────────────────────────────────────────────

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
    check(InContext);

    if (!InMeshData || !InPointData)
    {
        PCGLog::LogErrorOnGraph(LOCTEXT("BadInputTypes", "ApplyPointsToDynamicMesh: input data is wrong type."), InContext);
        return false;
    }

    const FDynamicMesh3* SrcRaw = InMeshData->GetDynamicMesh()
        ? InMeshData->GetDynamicMesh()->GetMeshPtr() : nullptr;

    if (!SrcRaw)
    {
        PCGLog::LogWarningOnGraph(LOCTEXT("NullMesh", "ApplyPointsToDynamicMesh: source mesh is null."), InContext);
        return false;
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
            PCGLog::LogWarningOnGraph(Msg, InContext);
            InContext->OutputData.TaggedData.Add(MeshTaggedData);
            return true;
        }
        else
        {
            PCGLog::LogErrorOnGraph(Msg, InContext);
            return false;
        }
    }

    // ── CopyOrSteal mutable mesh ──────────────────────────────────────────────
    FPCGTaggedData MeshTaggedDataCopy = MeshTaggedData;
    UPCGDynamicMeshData* OutMeshData = CopyOrSteal(MeshTaggedDataCopy, InContext);
    if (!OutMeshData)
    {
        PCGLog::LogWarningOnGraph(LOCTEXT("CopyFailed", "ApplyPointsToDynamicMesh: CopyOrSteal failed."), InContext);
        return false;
    }

    FDynamicMesh3* RawMesh = OutMeshData->GetMutableDynamicMesh()->GetMeshPtr();
    if (!RawMesh)
        return false;

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
                        NormalOverlay->SetElement(NormElems[Corner], Normalf);
                }
            });
        }

        ++PointIdx;
    }

    // ── Vertex Colors ─────────────────────────────────────────────────────────

    if (Settings->bWriteVertexColors)
    {
        FDynamicMeshColorOverlay* ColorOverlay = EnsureColorOverlay(RawMesh);

        if (Settings->VertexColorMode == EApplyPointsVertexColorMode::FullOverwrite)
        {
            // Read the native $Color property — one FVector4f per point
            PointIdx = 0;
            for (int32 VID : RawMesh->VertexIndicesItr())
            {
            	const auto c = InPointData->GetPointPropertyValue<EPCGPointNativeProperties::Color>(PointIdx);
            	const FVector4f C = FVector4f(c.X, c.Y, c.Z, c.W);
                SetVertexOverlayElements(RawMesh, ColorOverlay, VID, C);
                ++PointIdx;
            }
        }
        else // ComponentWise
        {
            const FApplyPointsVertexColorComponentMapping& CM = Settings->ComponentColorMapping;

            // Build per-channel float accessors
            TUniquePtr<const IPCGAttributeAccessor> RAccessor, GAccessor, BAccessor, AAccessor;
            TUniquePtr<const IPCGAttributeAccessorKeys> RKeys, GKeys, BKeys, AKeys;
            bool bHasR = false, bHasG = false, bHasB = false, bHasA = false;

            auto TryBuild = [&](bool bEnabled,
                const FPCGAttributePropertyInputSelector& Sel,
                TUniquePtr<const IPCGAttributeAccessor>& OutAcc,
                TUniquePtr<const IPCGAttributeAccessorKeys>& OutKeys) -> bool
            {
                if (!bEnabled) return false;
                const FPCGAttributePropertyInputSelector Fixed = Sel.CopyAndFixLast(InPointData);
                OutAcc  = PCGAttributeAccessorHelpers::CreateConstAccessor(InPointData, Fixed);
                OutKeys = PCGAttributeAccessorHelpers::CreateConstKeys(InPointData, Fixed);
                return OutAcc.IsValid() && OutKeys.IsValid();
            };

            bHasR = TryBuild(CM.bWriteR, CM.RSource, RAccessor, RKeys);
            bHasG = TryBuild(CM.bWriteG, CM.GSource, GAccessor, GKeys);
            bHasB = TryBuild(CM.bWriteB, CM.BSource, BAccessor, BKeys);
            bHasA = TryBuild(CM.bWriteA, CM.ASource, AAccessor, AKeys);

            PointIdx = 0;
            for (int32 VID : RawMesh->VertexIndicesItr())
            {
                // Read the existing color (so we only overwrite enabled channels)
                FVector4f NewColor = GetFirstVertexOverlayElement(RawMesh, ColorOverlay, VID, FVector4f(1.f, 1.f, 1.f, 1.f));

                float Val = 0.f;
                if (bHasR) { RAccessor->Get<float>(Val, PointIdx, *RKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible); NewColor.X = Val; }
                if (bHasG) { GAccessor->Get<float>(Val, PointIdx, *GKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible); NewColor.Y = Val; }
                if (bHasB) { BAccessor->Get<float>(Val, PointIdx, *BKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible); NewColor.Z = Val; }
                if (bHasA) { AAccessor->Get<float>(Val, PointIdx, *AKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible); NewColor.W = Val; }

                SetVertexOverlayElements(RawMesh, ColorOverlay, VID, NewColor);
                ++PointIdx;
            }
        }
    }

    // ── UV Channels ───────────────────────────────────────────────────────────

    for (const FApplyPointsUVChannelMapping& UVMap : Settings->UVChannelMappings)
    {
        const int32 UVIdx = UVMap.UVChannelIndex;
        if (UVIdx < 0)
        {
            PCGLog::LogWarningOnGraph(LOCTEXT("NegativeUVIndex", "ApplyPointsToDynamicMesh: UV channel index is negative, skipping entry."), InContext);
            continue;
        }

        // Build accessor for the source attribute
        const FPCGAttributePropertyInputSelector Fixed = UVMap.SourceAttribute.CopyAndFixLast(InPointData);
        TUniquePtr<const IPCGAttributeAccessor> UVAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InPointData, Fixed);
        TUniquePtr<const IPCGAttributeAccessorKeys> UVKeys  = PCGAttributeAccessorHelpers::CreateConstKeys(InPointData, Fixed);

        if (!UVAccessor || !UVKeys)
        {
            PCGLog::LogWarningOnGraph(FText::Format(
                LOCTEXT("InvalidUVAttribute", "ApplyPointsToDynamicMesh: UV channel {0} source attribute is invalid or not found, skipping."),
                FText::AsNumber(UVIdx)), InContext);
            continue;
        }

        FDynamicMeshUVOverlay* UVOverlay = EnsureUVOverlay(RawMesh, UVIdx);

        PointIdx = 0;
        for (int32 VID : RawMesh->VertexIndicesItr())
        {
            FVector2D UV(0.0, 0.0);
            UVAccessor->Get<FVector2D>(UV, PointIdx, *UVKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
            SetVertexOverlayElements(RawMesh, UVOverlay, VID, FVector2f((float)UV.X, (float)UV.Y));
            ++PointIdx;
        }
    }

    // ── Emit output ───────────────────────────────────────────────────────────

    OutMeshData->GetMutableDynamicMesh(); // touch dirty flag

    FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef(MeshTaggedData);
    Output.Data = OutMeshData;

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ExecuteInternal
// ─────────────────────────────────────────────────────────────────────────────

bool FPCGApplyPointsToDynamicMeshElement::ExecuteInternal(FPCGContext* InContext) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FPCGApplyPointsToDynamicMeshElement::ExecuteInternal);

    check(InContext);
    const UPCGApplyPointsToDynamicMeshSettings* Settings =
        InContext->GetInputSettings<UPCGApplyPointsToDynamicMeshSettings>();
    check(Settings);

    TArray<FPCGTaggedData> MeshInputs =
        InContext->InputData.GetInputsByPin(PCGApplyPointsToDynamicMeshConstants::InDynamicMeshLabel);
    TArray<FPCGTaggedData> PointInputs =
        InContext->InputData.GetInputsByPin(PCGApplyPointsToDynamicMeshConstants::InPointsLabel);

    if (MeshInputs.IsEmpty() || PointInputs.IsEmpty())
    {
        PCGLog::LogWarningOnGraph(LOCTEXT("MissingInputs", "ApplyPointsToDynamicMesh: missing mesh or point input."), InContext);
        return true;
    }

    if (MeshInputs.Num() != PointInputs.Num())
    {
        PCGLog::LogErrorOnGraph(FText::Format(
                LOCTEXT("SetCountMismatch",
                    "ApplyPointsToDynamicMesh: mesh input count ({0}) != point input count ({1}). "
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
