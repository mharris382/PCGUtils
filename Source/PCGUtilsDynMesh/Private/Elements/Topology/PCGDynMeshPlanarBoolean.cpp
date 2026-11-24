// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshPlanarBoolean.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/PCGUtilsDynMeshPlanarBoolean.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGModule.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshPlanarBoolean"

namespace PCGDynMeshPlanarBooleanElement
{
	/** "InA[2]" when the data can be found on the node's pin, else the bare pin name (deferred Builder evaluation). */
	FString PlanarBooleanInputLabel(const FPCGContext* Context, FName Pin, const UPCGData* Data)
	{
		if (Context && Data)
		{
			const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(Pin);
			for (int32 I = 0; I < Inputs.Num(); ++I)
			{
				if (Inputs[I].Data == Data) { return FString::Printf(TEXT("%s[%d]"), *Pin.ToString(), I); }
			}
		}
		return Pin.ToString();
	}

	class FDynMeshPlanarBooleanOperation final : public FPCGUtilsDynMeshProcessOperation
	{
	public:
		EPCGDynMeshPlanarBooleanSpace Space = EPCGDynMeshPlanarBooleanSpace::Coplanar;
		EPCGDynMeshPlanarBooleanOperation Operation = EPCGDynMeshPlanarBooleanOperation::Union;
		EPCGDynMeshPlanarBooleanOverlap OverlapSource = EPCGDynMeshPlanarBooleanOverlap::A;
		EPCGDynMeshPlanarBooleanPlane PlaneSource = EPCGDynMeshPlanarBooleanPlane::BestFitA;
		FPCGUtilsInputShorthandVector PlaneOrigin;
		FPCGUtilsInputShorthandVector PlaneNormal;
		FPCGUtilsInputShorthandDoubleAbs PlaneTolerance;
		FPCGUtilsInputShorthandVector ProjectionDirection;
		EPCGDynMeshPlanarBooleanTrim TrimMode = EPCGDynMeshPlanarBooleanTrim::RemoveInside;
		bool bStitch = false;
		FPCGUtilsInputShorthandDoubleAbs SeamTolerance;
		bool bFillSeamGaps = true;
		double OperandWallAngle = 80.0;
		EPCGDynMeshPlanarBooleanWeld WeldPosition = EPCGDynMeshPlanarBooleanWeld::KeepB;
		EPCGDynMeshPlanarBooleanTriangulation Triangulation = EPCGDynMeshPlanarBooleanTriangulation::Delaunay;
		EPCGDynMeshPlanarBooleanInheritance Inheritance = EPCGDynMeshPlanarBooleanInheritance::Interpolate;
		bool bOffsetOperandPolygroups = true;
		bool bMergeOperandMaterials = true;
		double SnapTolerance = 0.001;

		virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation, FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override
		{
			using namespace PCGUtilsPlanarBoolean;
			UDynamicMesh* Target = Invocation.MeshData ? Invocation.MeshData->GetMutableDynamicMesh() : nullptr;
			if (!Target) { return false; }
			if (!Invocation.OperandMeshData) { return true; } // Missing operand: A passes through unchanged.
			const UDynamicMesh* Operand = Invocation.OperandMeshData->GetDynamicMesh();
			if (!Operand) { return false; }
			FPCGContext* Context = Invocation.Context;

			const UPCGData* SourceData = Invocation.SourceMeshData ? static_cast<const UPCGData*>(Invocation.SourceMeshData) : Invocation.MeshData;
			FOptions Options;
			Options.LabelA = PlanarBooleanInputLabel(Context, PCGUtilsDynMeshOperandProcessConstants::InputPin, Invocation.SourceMeshData);
			Options.LabelB = PlanarBooleanInputLabel(Context, PCGUtilsDynMeshOperandProcessConstants::OperandPin, Invocation.OperandMeshData);
			int32 AIndex = INDEX_NONE;
			{
				FString Left, Right;
				if (Options.LabelA.Split(TEXT("["), &Left, &Right)) { AIndex = FCString::Atoi(*Right); }
			}

			auto Fail = [Context](const FText& Text) { PCGLog::LogErrorOnGraph(Text, Context); return false; };
			FText ReadError;
			auto ReadVector = [&](const FPCGUtilsInputShorthandVector& Shorthand, const FText& Label, FVector& Out)
			{
				return Shorthand.TryReadDataValue(SourceData, Out, ReadError, Label, AIndex) || Fail(ReadError);
			};
			auto ReadDouble = [&](const FPCGUtilsInputShorthandDoubleAbs& Shorthand, const FText& Label, double& Out)
			{
				return Shorthand.TryReadDataValue(SourceData, Out, ReadError, Label, AIndex) || Fail(ReadError);
			};

			const UE::Geometry::FDynamicMesh3& A = *Target->GetMeshPtr();
			const UE::Geometry::FDynamicMesh3& B = *Operand->GetMeshPtr();
			FPlanarFrame Frame;
			if (Space == EPCGDynMeshPlanarBooleanSpace::Coplanar)
			{
				if (PlaneSource == EPCGDynMeshPlanarBooleanPlane::BestFitA)
				{
					if (!FitPlane(A, Frame))
					{
						return Fail(FText::Format(LOCTEXT("NoFitPlane", "DynMesh | Planar Boolean: {0} has no triangle area, so Plane Source Best Fit (A) has no plane. Use XY Plane or Explicit, or give A some triangles."),
							FText::FromString(Options.LabelA)));
					}
				}
				else if (PlaneSource == EPCGDynMeshPlanarBooleanPlane::XYPlane)
				{
					FVector3d Centroid;
					if (!AreaCentroid(A, Centroid))
					{
						return Fail(FText::Format(LOCTEXT("NoCentroid", "DynMesh | Planar Boolean: {0} has no triangle area, so Plane Source XY Plane has no height to pass through. Use Explicit, or give A some triangles."),
							FText::FromString(Options.LabelA)));
					}
					FPlanarFrame::FromNormal(Centroid, FVector3d::UnitZ(), Frame);
				}
				else
				{
					FVector Origin, Normal;
					if (!ReadVector(PlaneOrigin, LOCTEXT("PlaneOriginLabel", "Plane Origin"), Origin)) { return false; }
					if (!ReadVector(PlaneNormal, LOCTEXT("PlaneNormalLabel", "Plane Normal"), Normal)) { return false; }
					if (!FPlanarFrame::FromNormal(Origin, Normal, Frame))
					{
						return Fail(FText::Format(LOCTEXT("ZeroPlaneNormal", "DynMesh | Planar Boolean: Plane Normal for {0} is ({1}, {2}, {3}); it must be a non-zero vector."),
							FText::FromString(Options.LabelA), FText::AsNumber(Normal.X), FText::AsNumber(Normal.Y), FText::AsNumber(Normal.Z)));
					}
				}
				if (!ReadDouble(PlaneTolerance, LOCTEXT("PlaneToleranceLabel", "Plane Tolerance"), Options.PlaneTolerance)) { return false; }
				Options.bCheckCoplanar = true;
				Options.OperandMode = EOperandMode::Cut;
				Options.bOverlapFromB = OverlapSource == EPCGDynMeshPlanarBooleanOverlap::B;
				switch (Operation)
				{
				case EPCGDynMeshPlanarBooleanOperation::Union:        Options.Operation = EOperation::Union; break;
				case EPCGDynMeshPlanarBooleanOperation::Intersection: Options.Operation = EOperation::Intersection; break;
				case EPCGDynMeshPlanarBooleanOperation::Difference:   Options.Operation = EOperation::Difference; break;
				case EPCGDynMeshPlanarBooleanOperation::ExclusiveOr:  Options.Operation = EOperation::ExclusiveOr; break;
				}
			}
			else
			{
				FVector Direction;
				if (!ReadVector(ProjectionDirection, LOCTEXT("ProjectionDirectionLabel", "Projection Direction"), Direction)) { return false; }
				if (!FPlanarFrame::FromNormal(FVector3d::ZeroVector, -Direction, Frame))
				{
					return Fail(FText::Format(LOCTEXT("ZeroDirection", "DynMesh | Planar Boolean: Projection Direction for {0} is ({1}, {2}, {3}); it must be a non-zero vector."),
						FText::FromString(Options.LabelA), FText::AsNumber(Direction.X), FText::AsNumber(Direction.Y), FText::AsNumber(Direction.Z)));
				}
				Options.bCheckCoplanar = false;
				const bool bRemove = TrimMode == EPCGDynMeshPlanarBooleanTrim::RemoveInside;
				Options.Operation = bRemove ? EOperation::Difference : EOperation::Intersection;
				Options.bOverlapFromB = false;
				Options.OperandMode = (bRemove && bStitch) ? EOperandMode::Whole : EOperandMode::FootprintOnly;
				if (Options.OperandMode == EOperandMode::Whole)
				{
					if (!ReadDouble(SeamTolerance, LOCTEXT("SeamToleranceLabel", "Seam Tolerance"), Options.SeamTolerance)) { return false; }
				}
			}
			Options.bCheckFolds = true;
			Options.bFillSeamGaps = bFillSeamGaps;
			Options.OperandWallAngle = OperandWallAngle;
			Options.WeldPosition = WeldPosition == EPCGDynMeshPlanarBooleanWeld::KeepB ? EWeldPosition::KeepB : EWeldPosition::KeepA;
			Options.Triangulation = Triangulation == EPCGDynMeshPlanarBooleanTriangulation::Delaunay ? ETriangulation::Delaunay : ETriangulation::KeepSourceEdges;
			Options.VertexInheritance = Inheritance == EPCGDynMeshPlanarBooleanInheritance::Interpolate ? EVertexInheritance::Interpolate
				: (Inheritance == EPCGDynMeshPlanarBooleanInheritance::Nearest ? EVertexInheritance::Nearest : EVertexInheritance::None);
			Options.bOffsetOperandPolygroups = bOffsetOperandPolygroups;
			Options.SnapTolerance = SnapTolerance;

			// B's material slots join A's list; B's IDs are remapped to them. Applied only on success.
			TArray<TObjectPtr<UMaterialInterface>> Materials = Invocation.MeshData->GetMaterials();
			if (bMergeOperandMaterials && Options.OperandMode != EOperandMode::FootprintOnly)
			{
				for (const TObjectPtr<UMaterialInterface>& Material : Invocation.OperandMeshData->GetMaterials())
				{
					int32 Index = Materials.Find(Material);
					if (Index == INDEX_NONE) { Index = Materials.Add(Material); }
					Options.OperandMaterialRemap.Add(Index);
				}
			}

			FResult Result;
			const bool bOK = Compute(A, B, Frame, Options, Result);
			for (const FMessage& Message : Result.Messages)
			{
				const FText Text = FText::Format(LOCTEXT("Message", "DynMesh | Planar Boolean: {0}"), FText::FromString(Message.Text));
				if (Message.bNote) { UE_LOG(LogPCG, Log, TEXT("%s"), *Text.ToString()); }
				else if (Message.bError) { PCGLog::LogErrorOnGraph(Text, Context); }
				else { PCGLog::LogWarningOnGraph(Text, Context); }
			}
			if (!bOK) { return false; }

			Target->SetMesh(MoveTemp(Result.Mesh));
			Invocation.MeshData->GetMutableMaterials() = MoveTemp(Materials);
			OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;
			return true;
		}
	};
}

UPCGDynMeshPlanarBooleanSettings::UPCGDynMeshPlanarBooleanSettings()
	: PlaneOrigin(FVector::ZeroVector, TEXT("PlaneOrigin"))
	, PlaneNormal(FVector::UpVector, TEXT("PlaneNormal"))
	, PlaneTolerance(0.1, TEXT("PlaneTolerance"))
	, ProjectionDirection(FVector(0.0, 0.0, -1.0), TEXT("ProjectionDirection"))
	, SeamTolerance(0.1, TEXT("SeamTolerance"))
{
}

#if WITH_EDITOR
FText UPCGDynMeshPlanarBooleanSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Planar Boolean");
}

FText UPCGDynMeshPlanarBooleanSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Booleans surface meshes in 2D, without making solids first. Coplanar: union, intersection, difference or exclusive of two meshes on one plane (best fit of A, the XY plane, or an explicit plane). Projected: cuts A along B's outline seen along a direction, removing or keeping the part inside; the cut follows A's surface. Stitch adds B's top surface (walls or a skirt along its outline are left out) and welds it into the hole as one watertight mesh, filling gaps above Seam Tolerance with vertical walls; green vertices moved to meet B get one short warning per seam run. Only triangles crossing the other mesh's outline are rebuilt; the rest are kept exactly. The output is InA, mutated: its metadata, tags and material slots are kept, and rebuilt triangles inherit material, PolyGroups and vertex attributes from the triangle they lie in. Whole-mesh node: no Selection or Selector. Both meshes must share one coordinate space.");
}

EPCGChangeType UPCGDynMeshPlanarBooleanSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	static const TSet<FName> BuilderGates = {
		GET_MEMBER_NAME_CHECKED(UPCGDynMeshPlanarBooleanSettings, Space),
		GET_MEMBER_NAME_CHECKED(UPCGDynMeshPlanarBooleanSettings, PlaneSource),
		GET_MEMBER_NAME_CHECKED(UPCGDynMeshPlanarBooleanSettings, PlaneOrigin),
		GET_MEMBER_NAME_CHECKED(UPCGDynMeshPlanarBooleanSettings, PlaneNormal),
		GET_MEMBER_NAME_CHECKED(UPCGDynMeshPlanarBooleanSettings, PlaneTolerance),
		GET_MEMBER_NAME_CHECKED(UPCGDynMeshPlanarBooleanSettings, ProjectionDirection),
		GET_MEMBER_NAME_CHECKED(UPCGDynMeshPlanarBooleanSettings, TrimMode),
		GET_MEMBER_NAME_CHECKED(UPCGDynMeshPlanarBooleanSettings, bStitch),
		GET_MEMBER_NAME_CHECKED(UPCGDynMeshPlanarBooleanSettings, SeamTolerance) };
	if (BuilderGates.Contains(PropertyChangedEvent.GetMemberPropertyName()))
	{
		// Whether a shorthand reads @Data decides whether the pins accept Builders.
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

bool UPCGDynMeshPlanarBooleanSettings::SupportsDeferredBuilderProcessing() const
{
	if (Space == EPCGDynMeshPlanarBooleanSpace::Coplanar)
	{
		if (!PlaneTolerance.IsConstant()) { return false; }
		if (PlaneSource == EPCGDynMeshPlanarBooleanPlane::Explicit && (!PlaneOrigin.IsConstant() || !PlaneNormal.IsConstant())) { return false; }
		return true;
	}
	if (!ProjectionDirection.IsConstant()) { return false; }
	if (TrimMode == EPCGDynMeshPlanarBooleanTrim::RemoveInside && bStitch && !SeamTolerance.IsConstant()) { return false; }
	return true;
}

FText UPCGDynMeshPlanarBooleanSettings::GetBuilderInputRejectionReason() const
{
	return LOCTEXT("BuilderRejected", "Planar Boolean reads a setting from @Data, and a Builder has no @Data until it is realized. Realize the Builders first, or set that setting to Constant.");
}

FString UPCGDynMeshPlanarBooleanSettings::GetAdditionalTitleInformation() const
{
	if (Space == EPCGDynMeshPlanarBooleanSpace::Coplanar)
	{
		return UEnum::GetDisplayValueAsText(Operation).ToString();
	}
	if (TrimMode == EPCGDynMeshPlanarBooleanTrim::KeepInside) { return TEXT("Keep Inside"); }
	return bStitch ? TEXT("Trim + Stitch") : TEXT("Trim");
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshPlanarBooleanSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<PCGDynMeshPlanarBooleanElement::FDynMeshPlanarBooleanOperation> Op = MakeShared<PCGDynMeshPlanarBooleanElement::FDynMeshPlanarBooleanOperation>();
	Op->Space = Space;
	Op->Operation = Operation;
	Op->OverlapSource = OverlapSource;
	Op->PlaneSource = PlaneSource;
	Op->PlaneOrigin = PlaneOrigin;
	Op->PlaneNormal = PlaneNormal;
	Op->PlaneTolerance = PlaneTolerance;
	Op->ProjectionDirection = ProjectionDirection;
	Op->TrimMode = TrimMode;
	Op->bStitch = bStitch;
	Op->SeamTolerance = SeamTolerance;
	Op->bFillSeamGaps = bFillSeamGaps;
	Op->OperandWallAngle = OperandWallAngle;
	Op->WeldPosition = WeldPosition;
	Op->Triangulation = Triangulation;
	Op->Inheritance = VertexAttributeInheritance;
	Op->bOffsetOperandPolygroups = bOffsetOperandPolygroups;
	Op->bMergeOperandMaterials = bMergeOperandMaterials;
	Op->SnapTolerance = SnapTolerance;
	return Op;
}

#undef LOCTEXT_NAMESPACE
