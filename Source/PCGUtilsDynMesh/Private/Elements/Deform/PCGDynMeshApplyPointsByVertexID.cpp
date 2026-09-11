// Copyright Max Harris

#include "Elements/Deform/PCGDynMeshApplyPointsByVertexID.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/PCGUtilsDynMeshAttributeHelpers.h"
#include "GameFramework/Actor.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshApplyPointsByVertexID"

using namespace UE::Geometry;

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────

TArray<FPCGPinProperties> UPCGDynMeshApplyPointsByVertexIDSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins.Emplace_GetRef(PCGDynMeshApplyPointsByVertexIDConstants::PointsInputPin, EPCGDataType::Point, true, true)
		.SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGDynMeshApplyPointsByVertexIDSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshApplyPointsByVertexIDElement>();
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers (file-local)
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/** Ensure the mesh has a UV overlay at the given layer index, matching the legacy ApplyPointsToDynamicMesh. */
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

	/** Set all overlay elements that belong to VID to NewValue. */
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
	 * Builds the set of vertex IDs the handle's selection contains. Empty optional means "no selection" (a bare
	 * mesh source, or a Selection source resolved via GetRequiredSelectionDomain elsewhere) - every point-named
	 * vertex is then eligible.
	 */
	TOptional<TSet<int32>> BuildAllowedVertexSet(const FPCGUtilsMeshTargetHandle& Handle, const FDynamicMesh3& Mesh)
	{
		if (!Handle.IsSelection())
		{
			return {};
		}

		TArray<int32> AllowedVertexIDs;
		Handle.GetSelection().ConvertToMeshIndexArray(Mesh, AllowedVertexIDs, EGeometryScriptIndexType::Vertex);
		return TSet<int32>(AllowedVertexIDs);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ExecuteInternal
// ─────────────────────────────────────────────────────────────────────────────

bool FPCGDynMeshApplyPointsByVertexIDElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshApplyPointsByVertexIDElement::ExecuteInternal);

	check(Context);
	const UPCGDynMeshApplyPointsByVertexIDSettings* Settings =
		Context->GetInputSettings<UPCGDynMeshApplyPointsByVertexIDSettings>();
	check(Settings);

	if (Settings->SourceVertexIndexAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("EmptyVertexIndexAttribute", "Apply Points By Vertex ID requires a Source Vertex Index Attribute name."),
			Context);
		return true;
	}

	// Settings->GetMainInputPinLabel()/GetMainOutputPinLabel() are protected to the process base and its element
	// (friendship is not inherited), and this Settings class never overrides them, so the default pin names are
	// used directly here.
	const TArray<FPCGTaggedData> MeshInputs =
		Context->InputData.GetInputsByPin(PCGUtilsDynMeshProcessConstants::InputPin);
	const TArray<FPCGTaggedData> PointInputs =
		Context->InputData.GetInputsByPin(PCGDynMeshApplyPointsByVertexIDConstants::PointsInputPin);

	if (MeshInputs.IsEmpty() || PointInputs.IsEmpty())
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("MissingInputs", "Apply Points By Vertex ID: missing mesh or point input."), Context);
		return true;
	}

	if (MeshInputs.Num() != PointInputs.Num())
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("SetCountMismatch",
				"Apply Points By Vertex ID: mesh input count ({0}) != point input count ({1}). Inputs must be "
				"paired 1:1 in matching order."),
			FText::AsNumber(MeshInputs.Num()), FText::AsNumber(PointInputs.Num())), Context);
		return true;
	}

	for (int32 InputIndex = 0; InputIndex < MeshInputs.Num(); ++InputIndex)
	{
		const FPCGTaggedData& MeshTaggedData = MeshInputs[InputIndex];
		const FPCGTaggedData& PointTaggedData = PointInputs[InputIndex];

		const UPCGBasePointData* InPointData = Cast<const UPCGBasePointData>(PointTaggedData.Data);
		if (!InPointData)
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("BadPointInput", "Apply Points By Vertex ID skipped an input with no valid point data."), Context);
			continue;
		}

		// CreateTarget() resolves either a bare DynMesh or a DynMesh Selection into one mutable working copy,
		// applying the connected Selector and domain conversion (Vertex, per GetRequiredSelectionDomain) via
		// Settings. FullMeshCopy is correct here: this operation already knows how to scope its own effect (it
		// only ever touches vertices the point set names), so there is no submesh/weld-back to pay for.
		FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTarget(
			MeshTaggedData.Data, EPCGUtilsMeshTargetPreparation::FullMeshCopy, Context, Settings);
		if (!Handle.IsValid())
		{
			continue;
		}

		FDynamicMesh3* RawMesh = Handle.GetTargetMesh() ? Handle.GetTargetMesh()->GetMeshPtr() : nullptr;
		if (!RawMesh)
		{
			continue;
		}

		const TOptional<TSet<int32>> AllowedVertexIDs = BuildAllowedVertexSet(Handle, *RawMesh);

		// Build the Vertex ID accessor once per input.
		FPCGAttributePropertyInputSelector IDSelector;
		IDSelector.SetAttributeName(Settings->SourceVertexIndexAttribute);
		const TUniquePtr<const IPCGAttributeAccessor> IDAccessor =
			PCGAttributeAccessorHelpers::CreateConstAccessor(InPointData, IDSelector);
		const TUniquePtr<const IPCGAttributeAccessorKeys> IDKeys =
			PCGAttributeAccessorHelpers::CreateConstKeys(InPointData, IDSelector);
		if (!IDAccessor || !IDKeys)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MissingVertexIndexAttribute",
					"Apply Points By Vertex ID could not find integer attribute '{0}' on its point input."),
				FText::FromName(Settings->SourceVertexIndexAttribute)), Context);
			continue;
		}

		const bool bCopyNormals = Settings->bCopyRotationAsNormals
			&& RawMesh->HasAttributes()
			&& RawMesh->Attributes()->PrimaryNormals() != nullptr;
		FDynamicMeshNormalOverlay* NormalOverlay = bCopyNormals ? RawMesh->Attributes()->PrimaryNormals() : nullptr;

		FDynamicMeshColorOverlay* ColorOverlay = Settings->bWriteVertexColors
			? PCGUtilsDynMeshAttributeHelpers::EnsurePrimaryColorOverlay(*RawMesh) : nullptr;

		FTransform PointsToMesh = FTransform::Identity;
		if (Settings->bConvertToLocalSpace)
		{
			if (const AActor* TargetActor = Context->GetTargetActor(Handle.GetSourceMeshData()))
			{
				PointsToMesh = TargetActor->GetActorTransform().Inverse();
			}
			else
			{
				PCGLog::LogWarningOnGraph(
					LOCTEXT("MissingTargetActor",
						"Apply Points By Vertex ID could not resolve a target actor; point positions remain unchanged."),
					Context);
			}
		}

		// ── Component-wise vertex color accessors, built once per input ───────
		TUniquePtr<const IPCGAttributeAccessor> RAccessor, GAccessor, BAccessor, AAccessor;
		TUniquePtr<const IPCGAttributeAccessorKeys> RKeys, GKeys, BKeys, AKeys;
		bool bHasR = false, bHasG = false, bHasB = false, bHasA = false;
		if (Settings->bWriteVertexColors && Settings->VertexColorMode == EPCGDynMeshApplyPointsVertexColorMode::ComponentWise)
		{
			const FPCGDynMeshApplyPointsVertexColorComponentMapping& CM = Settings->ComponentColorMapping;
			auto TryBuild = [&](bool bEnabled, const FPCGAttributePropertyInputSelector& Sel,
				TUniquePtr<const IPCGAttributeAccessor>& OutAcc, TUniquePtr<const IPCGAttributeAccessorKeys>& OutKeys) -> bool
			{
				if (!bEnabled) return false;
				const FPCGAttributePropertyInputSelector Fixed = Sel.CopyAndFixLast(InPointData);
				OutAcc = PCGAttributeAccessorHelpers::CreateConstAccessor(InPointData, Fixed);
				OutKeys = PCGAttributeAccessorHelpers::CreateConstKeys(InPointData, Fixed);
				return OutAcc.IsValid() && OutKeys.IsValid();
			};
			bHasR = TryBuild(CM.bWriteR, CM.RSource, RAccessor, RKeys);
			bHasG = TryBuild(CM.bWriteG, CM.GSource, GAccessor, GKeys);
			bHasB = TryBuild(CM.bWriteB, CM.BSource, BAccessor, BKeys);
			bHasA = TryBuild(CM.bWriteA, CM.ASource, AAccessor, AKeys);
		}

		// ── UV accessors, built once per input ─────────────────────────────────
		struct FResolvedUVMapping
		{
			int32 UVChannelIndex = 0;
			TUniquePtr<const IPCGAttributeAccessor> Accessor;
			TUniquePtr<const IPCGAttributeAccessorKeys> Keys;
		};
		TArray<FResolvedUVMapping> ResolvedUVMappings;
		for (const FPCGDynMeshApplyPointsUVChannelMapping& UVMap : Settings->UVChannelMappings)
		{
			if (UVMap.UVChannelIndex < 0)
			{
				PCGLog::LogWarningOnGraph(
					LOCTEXT("NegativeUVIndex", "Apply Points By Vertex ID: UV channel index is negative, skipping entry."), Context);
				continue;
			}
			const FPCGAttributePropertyInputSelector Fixed = UVMap.SourceAttribute.CopyAndFixLast(InPointData);
			FResolvedUVMapping Mapping;
			Mapping.UVChannelIndex = UVMap.UVChannelIndex;
			Mapping.Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InPointData, Fixed);
			Mapping.Keys = PCGAttributeAccessorHelpers::CreateConstKeys(InPointData, Fixed);
			if (!Mapping.Accessor || !Mapping.Keys)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("InvalidUVAttribute",
						"Apply Points By Vertex ID: UV channel {0} source attribute is invalid or not found, skipping."),
					FText::AsNumber(UVMap.UVChannelIndex)), Context);
				continue;
			}
			ResolvedUVMappings.Add(MoveTemp(Mapping));
		}

		const TConstPCGValueRange<FTransform> TransformRange = InPointData->GetConstTransformValueRange();
		const int32 NumPoints = InPointData->GetNumPoints();

		int32 NumSkippedInvalidID = 0;
		int32 NumSkippedOutsideSelection = 0;

		for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
		{
			int32 VID = INDEX_NONE;
			if (!IDAccessor->Get<int32>(VID, PointIndex, *IDKeys) || !RawMesh->IsVertex(VID))
			{
				++NumSkippedInvalidID;
				continue;
			}
			if (AllowedVertexIDs.IsSet() && !AllowedVertexIDs->Contains(VID))
			{
				++NumSkippedOutsideSelection;
				continue;
			}

			const FTransform& PT = TransformRange[PointIndex];
			const FVector MeshPosition = Settings->bConvertToLocalSpace
				? PointsToMesh.TransformPosition(PT.GetLocation()) : PT.GetLocation();
			RawMesh->SetVertex(VID, FVector3d(MeshPosition));

			if (NormalOverlay)
			{
				const FVector PointNormal = PT.GetRotation().GetUpVector();
				const FVector Normal = Settings->bConvertToLocalSpace
					? PointsToMesh.TransformVectorNoScale(PointNormal).GetSafeNormal() : PointNormal;
				const FVector3f Normalf(Normal.X, Normal.Y, Normal.Z);
				SetVertexOverlayElements(RawMesh, NormalOverlay, VID, Normalf);
			}

			if (ColorOverlay)
			{
				if (Settings->VertexColorMode == EPCGDynMeshApplyPointsVertexColorMode::FullOverwrite)
				{
					const auto C = InPointData->GetPointPropertyValue<EPCGPointNativeProperties::Color>(PointIndex);
					PCGUtilsDynMeshAttributeHelpers::SetVertexColor(
						*RawMesh, *ColorOverlay, VID, FVector4f(C.X, C.Y, C.Z, C.W));
				}
				else
				{
					FVector4f NewColor = PCGUtilsDynMeshAttributeHelpers::GetVertexColor(
						*RawMesh, *ColorOverlay, VID, FVector4f(1.f, 1.f, 1.f, 1.f));
					float Val = 0.f;
					if (bHasR) { RAccessor->Get<float>(Val, PointIndex, *RKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible); NewColor.X = Val; }
					if (bHasG) { GAccessor->Get<float>(Val, PointIndex, *GKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible); NewColor.Y = Val; }
					if (bHasB) { BAccessor->Get<float>(Val, PointIndex, *BKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible); NewColor.Z = Val; }
					if (bHasA) { AAccessor->Get<float>(Val, PointIndex, *AKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible); NewColor.W = Val; }
					PCGUtilsDynMeshAttributeHelpers::SetVertexColor(*RawMesh, *ColorOverlay, VID, NewColor);
				}
			}

			for (const FResolvedUVMapping& Mapping : ResolvedUVMappings)
			{
				FDynamicMeshUVOverlay* UVOverlay = EnsureUVOverlay(RawMesh, Mapping.UVChannelIndex);
				FVector2D UV(0.0, 0.0);
				Mapping.Accessor->Get<FVector2D>(UV, PointIndex, *Mapping.Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
				SetVertexOverlayElements(RawMesh, UVOverlay, VID, FVector2f((float)UV.X, (float)UV.Y));
			}
		}

		if (NumSkippedInvalidID > 0)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("SkippedInvalidID",
					"Apply Points By Vertex ID skipped {0} point(s) with a missing or invalid target vertex ID."),
				FText::AsNumber(NumSkippedInvalidID)), Context);
		}
		if (NumSkippedOutsideSelection > 0)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("SkippedOutsideSelection",
					"Apply Points By Vertex ID skipped {0} point(s) whose target vertex is outside the resolved Selection."),
				FText::AsNumber(NumSkippedOutsideSelection)), Context);
		}

		// The operation mutated Handle's FullMeshCopy target directly and completely - no domain compositor is
		// needed (no Restore* call), matching Bevel Edges' pattern for an already-selection-aware operation.
		FPCGUtilsMeshTargetFunctions::EmitOutput(Context, MeshTaggedData, Handle, PCGUtilsDynMeshProcessConstants::OutputPin);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
