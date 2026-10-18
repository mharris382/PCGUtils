// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshInset.h"

#include "Data/PCGDynamicMeshData.h"
#include "Elements/Topology/PCGDynMeshFaceRegion.h"
#include "Operations/InsetMeshRegion.h"
#include "PCGUtilsCustomVersion.h"
#include "PCGUtilsDynMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshInset"

namespace
{
	class FInsetOperation final : public FPCGUtilsDynMeshTopologyOperation
	{
	public:
		FPCGUtilsInputShorthandDouble Distance;
		FGeometryScriptMeshInsetOutsetFacesOptions Options;
		EPCGUtilsDynMeshFaceRegionResult ResultRegion = EPCGUtilsDynMeshFaceRegionResult::Faces;

		virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
			FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override
		{
			if (Distance.IsConstant())
			{
				return FPCGUtilsDynMeshTopologyOperation::Execute(Invocation, OutOutcome);
			}

			// The operation is shared across inputs and must stay immutable, so the per-input distance is
			// resolved into a local copy that runs this one invocation.
			const UPCGDynamicMeshData* Source = Invocation.SourceMeshData ? Invocation.SourceMeshData : Invocation.MeshData;
			double Value = 0.0;
			FText Error;
			if (!Distance.TryReadDataValue(Source, Value, Error, LOCTEXT("DistanceLabel", "Distance"), Invocation.InputIndex))
			{
				PCGLog::LogErrorOnGraph(Error, Invocation.Context);
				return false;
			}
			FInsetOperation Resolved(*this);
			Resolved.Distance.Input = EPCGUtilsInputValueType::Constant;
			Resolved.Options.Distance = static_cast<float>(Value);
			return Resolved.FPCGUtilsDynMeshTopologyOperation::Execute(Invocation, OutOutcome);
		}

	protected:
		virtual bool Apply(UE::Geometry::FDynamicMesh3& Mesh, const UE::Geometry::FGeometrySelection* Selection,
			TArray<int32>& OutTriangles) const override
		{
			for (TArray<int32>& Area : PCGUtilsDynMeshFaceRegion::SplitAreas(Mesh, Selection, Options.AreaMode))
			{
				UE::Geometry::FInsetMeshRegion Inset(&Mesh);
				Inset.Triangles = MoveTemp(Area);
				Inset.InsetDistance = Options.Distance;
				Inset.bReproject = Options.Distance >= 0 && Options.bReproject;
				Inset.Softness = Options.Softness;
				Inset.bSolveRegionInteriors = !Options.bBoundaryOnly;
				Inset.AreaCorrection = Options.AreaScale;
				Inset.UVScaleFactor = Options.UVScale;
				if (!Inset.Apply()) { return false; }
				for (const auto& Region : Inset.InsetRegions)
				{
					PCGUtilsDynMeshFaceRegion::ApplyCapGroups(Mesh, Region.InitialTriangles, Options.GroupOptions);
					PCGUtilsDynMeshFaceRegion::AppendResult(Region.InitialTriangles, Region.StitchTriangles, ResultRegion, OutTriangles);
				}
			}
			return true;
		}
	};
}

UPCGDynMeshInsetSettings::UPCGDynMeshInsetSettings()
	: Distance(1.0, TEXT("InsetDistance"))
{
}

#if WITH_EDITOR
FText UPCGDynMeshInsetSettings::GetDefaultNodeTitle() const { return LOCTEXT("Title", "DynMesh | Inset Faces"); }
FText UPCGDynMeshInsetSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Insets selected faces (negative distance outsets). Distance is a constant or is read per input from an @Data attribute. The inner faces are the result selection by default; border faces or both are optional. Can assign a named result PolyGroup and emit a reusable Selector on the Result pin. Supports DynMesh, Selection and Builder inputs (Builders only with a constant Distance).");
}

EPCGChangeType UPCGDynMeshInsetSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGDynMeshInsetSettings, Distance))
	{
		// Switching Distance between Constant and @Data decides whether the main pin accepts Builders.
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

FText UPCGDynMeshInsetSettings::GetBuilderInputRejectionReason() const
{
	return LOCTEXT("BuilderRejected", "Inset Faces reads Distance from @Data, and a Builder has no @Data until it is realized. Realize the Builder first, or set Distance to Constant.");
}

void UPCGDynMeshInsetSettings::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerCustomVersion(FPCGUtilsCustomVersion::GUID) < FPCGUtilsCustomVersion::InputShorthands)
	{
		Distance.Input = EPCGUtilsInputValueType::Constant;
		Distance.Constant = Options_DEPRECATED.Distance;
		bReproject = Options_DEPRECATED.bReproject;
		bBoundaryOnly = Options_DEPRECATED.bBoundaryOnly;
		Softness = Options_DEPRECATED.Softness;
		AreaScale = Options_DEPRECATED.AreaScale;
		AreaMode = Options_DEPRECATED.AreaMode;
		GroupOptions = Options_DEPRECATED.GroupOptions;
		UVScale = Options_DEPRECATED.UVScale;
		UE_LOG(LogPCGUtilsDynMesh, Log, TEXT("Inset Faces '%s': migrated Options to flattened settings (Distance %g as Constant)."),
			*GetPathName(), Distance.Constant);
	}
}

TSharedPtr<FPCGUtilsDynMeshTopologyOperation> UPCGDynMeshInsetSettings::CreateTopologyOperation(FPCGContext*) const
{
	auto Operation = MakeShared<FInsetOperation>();
	Operation->Distance = Distance;
	Operation->Options.Distance = static_cast<float>(Distance.Constant);
	Operation->Options.bReproject = bReproject;
	Operation->Options.bBoundaryOnly = bBoundaryOnly;
	Operation->Options.Softness = Softness;
	Operation->Options.AreaScale = AreaScale;
	Operation->Options.AreaMode = AreaMode;
	Operation->Options.GroupOptions = GroupOptions;
	Operation->Options.UVScale = UVScale;
	Operation->ResultRegion = ResultRegion;
	return Operation;
}

#undef LOCTEXT_NAMESPACE
