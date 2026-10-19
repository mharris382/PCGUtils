// Copyright Max Harris

#include "Elements/Creation/PCGDynMeshPathToMesh.h"

#include "Algo/Transform.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGPolygon2DData.h"
#include "Data/PCGSplineData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Operations/ExtrudeMesh.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "VectorUtil.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGDynMeshPathToMesh)

#define LOCTEXT_NAMESPACE "PCGDynMeshPathToMesh"

namespace
{
	constexpr int32 PathToMeshPathsAlias = 0;
	constexpr int32 PathToMeshSplinesAlias = 1;

	/** The outline(s) to fill, in world space, plus the frame the input itself would supply. */
	struct FPathToMeshOutline
	{
		TArray<FVector> Outer;
		TArray<TArray<FVector>> Holes;
		/** Spline/polygon transform; identity for paths. Used when no other frame is chosen. */
		FTransform NativeTransform = FTransform::Identity;
		/** Points the centroid is averaged over: path points, spline control points, polygon outer vertices. */
		TArray<FVector> CentroidPoints;
		bool bIsPolygon = false;
	};

	FName StripPathToMeshDataPrefix(FName Name)
	{
		FString Text = Name.ToString();
		Text.RemoveFromStart(TEXT("@Data."), ESearchCase::IgnoreCase);
		return FName(Text);
	}

	FPCGAttributePropertyInputSelector MakePathToMeshDataSelector(FName AttributeName)
	{
		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(TEXT("@Data.") + AttributeName.ToString());
		return Selector;
	}

	FText PathToMeshInputText(int32 InputIndex)
	{
		return FText::Format(LOCTEXT("InputPrefix", "Path To Mesh (input {0})"), FText::AsNumber(InputIndex));
	}

	/** Reads a path's points. Fails, with the exact reason, when the path is not a closed path that encloses an area. */
	bool GatherPathToMeshPath(const UPCGBasePointData* PointData, FName ClosedAttributeName, int32 InputIndex,
		FPathToMeshOutline& OutOutline, FText& OutReason)
	{
		const FName ClosedName = StripPathToMeshDataPrefix(ClosedAttributeName);
		bool bClosed = false;
		FText ReadError;
		if (!PCGUtilsInputShorthands::ReadDataDomainValue(PointData, MakePathToMeshDataSelector(ClosedName), bClosed, ReadError))
		{
			OutReason = FText::Format(LOCTEXT("ClosedUnreadable",
				"{0}: cannot tell whether the path is closed, so it was discarded: {1}"),
				PathToMeshInputText(InputIndex), ReadError);
			return false;
		}
		if (!bClosed)
		{
			OutReason = FText::Format(LOCTEXT("PathOpen",
				"{0}: the path is open (@Data.{1} is false); only closed paths enclose an area to fill, so it was discarded."),
				PathToMeshInputText(InputIndex), FText::FromName(ClosedName));
			return false;
		}

		const TConstPCGValueRange<FTransform> Transforms = PointData->GetConstTransformValueRange();
		OutOutline.Outer.Reserve(Transforms.Num());
		for (const FTransform& Transform : Transforms)
		{
			OutOutline.Outer.Add(Transform.GetLocation());
		}
		if (OutOutline.Outer.Num() < 3)
		{
			OutReason = FText::Format(LOCTEXT("PathTooShort",
				"{0}: the path has {1} points; a closed path needs at least 3 to enclose an area, so it was discarded."),
				PathToMeshInputText(InputIndex), FText::AsNumber(OutOutline.Outer.Num()));
			return false;
		}
		OutOutline.CentroidPoints = OutOutline.Outer;
		return true;
	}

	bool GatherPathToMeshSpline(const UPCGSplineData* SplineData, double ErrorTolerance, int32 InputIndex,
		FPathToMeshOutline& OutOutline, FText& OutReason)
	{
		if (!SplineData->IsClosed())
		{
			OutReason = FText::Format(LOCTEXT("SplineOpen",
				"{0}: the spline is not a closed loop; only closed splines enclose an area to fill, so it was discarded."),
				PathToMeshInputText(InputIndex));
			return false;
		}

		// Same sampling call as the engine's Spline To Mesh, so the two produce the same outline.
		SplineData->SplineStruct.ConvertSplineToPolyLine(ESplineCoordinateSpace::World, ErrorTolerance, OutOutline.Outer);
		if (OutOutline.Outer.Num() < 3)
		{
			OutReason = FText::Format(LOCTEXT("SplineTooShort",
				"{0}: the spline sampled to {1} points; at least 3 are needed to enclose an area, so it was discarded."),
				PathToMeshInputText(InputIndex), FText::AsNumber(OutOutline.Outer.Num()));
			return false;
		}

		OutOutline.NativeTransform = SplineData->SplineStruct.Transform;
		const int32 NumControlPoints = SplineData->SplineStruct.GetNumberOfPoints();
		for (int32 Index = 0; Index < NumControlPoints; ++Index)
		{
			OutOutline.CentroidPoints.Add(SplineData->SplineStruct.GetLocationAtSplineInputKey(
				static_cast<float>(Index), ESplineCoordinateSpace::World));
		}
		return true;
	}

	void GatherPathToMeshPolygon(const UPCGPolygon2DData* PolygonData, FPathToMeshOutline& OutOutline)
	{
		const FTransform Transform = PolygonData->GetTransform();
		auto ToWorld = [&Transform](const UE::Geometry::FPolygon2d& Polygon, TArray<FVector>& OutVertices)
		{
			OutVertices.Reset();
			Algo::Transform(Polygon.GetVertices(), OutVertices,
				[&Transform](const FVector2D& Vertex) { return Transform.TransformPosition(FVector(Vertex, 0.0)); });
		};

		const UE::Geometry::FGeneralPolygon2d& Polygon = PolygonData->GetPolygon();
		ToWorld(Polygon.GetOuter(), OutOutline.Outer);
		for (const UE::Geometry::FPolygon2d& Hole : Polygon.GetHoles())
		{
			ToWorld(Hole, OutOutline.Holes.Emplace_GetRef());
		}
		OutOutline.NativeTransform = Transform;
		OutOutline.CentroidPoints = OutOutline.Outer;
		OutOutline.bIsPolygon = true;
	}

	/**
	 * Reads an optional @Data Transform. Returns false when the input does not carry the attribute, which is not an
	 * error. OutWarning is set when it is there but cannot be used.
	 */
	bool ReadPathToMeshFrameAttribute(const UPCGData* Data, FName AttributeName, int32 InputIndex,
		FTransform& OutTransform, FText& OutWarning)
	{
		const FName Name = StripPathToMeshDataPrefix(AttributeName);
		const UPCGMetadata* Metadata = Data->ConstMetadata();
		const FPCGMetadataDomain* DataDomain = Metadata ? Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		const FPCGMetadataAttributeBase* Attribute = DataDomain ? DataDomain->GetConstAttribute(Name) : nullptr;
		if (!Attribute)
		{
			return false;
		}

		const EPCGMetadataTypes Type = static_cast<EPCGMetadataTypes>(Attribute->GetTypeId());
		if (Type != EPCGMetadataTypes::Transform)
		{
			OutWarning = FText::Format(LOCTEXT("FrameWrongType",
				"{0}: @Data.{1} is {2}, not a Transform, so it was not used as the mesh frame."),
				PathToMeshInputText(InputIndex), FText::FromName(Name),
				StaticEnum<EPCGMetadataTypes>()->GetDisplayNameTextByValue(static_cast<int64>(Type)));
			return false;
		}

		const FPCGAttributePropertyInputSelector Selector = MakePathToMeshDataSelector(Name);
		const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Data, Selector, /*bQuiet=*/true);
		const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Data, Selector);
		if (!Accessor || !Keys || Keys->GetNum() < 1 || !Accessor->Get<FTransform>(OutTransform, 0, *Keys))
		{
			OutWarning = FText::Format(LOCTEXT("FrameNoValue",
				"{0}: @Data.{1} holds no value, so it was not used as the mesh frame."),
				PathToMeshInputText(InputIndex), FText::FromName(Name));
			return false;
		}
		return true;
	}

	/** Reverses every triangle and every normal, including the normal overlay the triangulation writes. */
	void ReversePathToMeshFacing(UE::Geometry::FDynamicMesh3& Mesh)
	{
		Mesh.ReverseOrientation(/*bFlipNormals=*/true);
		if (Mesh.HasAttributes())
		{
			if (UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals())
			{
				for (const int32 ElementID : Normals->ElementIndicesItr())
				{
					Normals->SetElement(ElementID, -Normals->GetElement(ElementID));
				}
			}
		}
	}

	/** Sum of area-weighted triangle normals in world space. */
	FVector3d PathToMeshWorldAreaNormal(const UE::Geometry::FDynamicMesh3& Mesh, const FTransform& MeshFrame)
	{
		FVector3d Sum = FVector3d::ZeroVector;
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(TriangleID, A, B, C);
			A = MeshFrame.TransformPosition(A);
			B = MeshFrame.TransformPosition(B);
			C = MeshFrame.TransformPosition(C);
			Sum += UE::Geometry::VectorUtil::Normal(A, B, C) * UE::Geometry::VectorUtil::Area(A, B, C);
		}
		return Sum;
	}

	/** Writes one @Data value, replacing an attribute of the same name (for example one copied from the input). */
	template <typename T>
	void WritePathToMeshDataValue(FPCGMetadataDomain& Domain, FName Name, const T& Value)
	{
		if (Domain.HasAttribute(Name))
		{
			Domain.DeleteAttribute(Name);
		}
		FPCGMetadataAttribute<T>* Attribute = Domain.CreateAttribute<T>(Name, Value,
			/*bAllowsInterpolation=*/false, /*bOverrideParent=*/true);
		if (!Attribute)
		{
			return;
		}
		const PCGMetadataEntryKey Key = Domain.GetItemCountForChild() > 0 ? PCGFirstEntryKey : Domain.AddEntry();
		Attribute->SetValue(Key, Value);
	}
}

UPCGDynMeshPathToMeshSettings::UPCGDynMeshPathToMeshSettings()
{
	// Paths are point data, whose default domain is the points: the attributes must name @Data explicitly.
	UpDirection.Constant = FVector::UpVector;
	UpDirection.Attribute.Update(TEXT("@Data.Normal"));
	Thickness.Constant = 0.0;
	Thickness.Attribute.Update(TEXT("@Data.Thickness"));
}

#if WITH_EDITOR
FName UPCGDynMeshPathToMeshSettings::GetDefaultNodeName() const
{
	return bInputSplines ? TEXT("DynMeshSplineToMesh") : TEXT("DynMeshPathToMesh");
}

FText UPCGDynMeshPathToMeshSettings::GetDefaultNodeTitle() const
{
	return bInputSplines
		? LOCTEXT("SplineTitle", "DynMesh | Spline To Mesh")
		: LOCTEXT("PathTitle", "DynMesh | Path To Mesh");
}

FText UPCGDynMeshPathToMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Fills each closed path (or, with Input Splines on, each closed spline or 2D polygon) with a triangulated "
		"DynMesh, optionally extruded into a solid. Open inputs are discarded with a warning. The input's @Data "
		"attributes are copied onto the mesh, the mesh is framed at the path centroid or at a @Data transform, and "
		"the faces can be turned toward an up direction. The frame is written to @Data.MeshTransform, as the "
		"engine's Spline To Mesh does.");
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGDynMeshPathToMeshSettings::GetPreconfiguredInfo() const
{
	return {
		{PathToMeshPathsAlias, LOCTEXT("PathAlias", "DynMesh | Path To Mesh"),
			LOCTEXT("PathAliasTooltip", "Fills closed paths (point data marked closed in @Data) with a DynMesh.")},
		{PathToMeshSplinesAlias, LOCTEXT("SplineAlias", "DynMesh | Spline To Mesh"),
			LOCTEXT("SplineAliasTooltip", "Fills closed splines and 2D polygons with a DynMesh, keeping their @Data attributes.")}
	};
}

void UPCGDynMeshPathToMeshSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	bInputSplines = PreconfiguredInfo.PreconfiguredIndex == PathToMeshSplinesAlias;
}

EPCGChangeType UPCGDynMeshPathToMeshSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGDynMeshPathToMeshSettings, bInputSplines))
	{
		// Switches the In pin between point data and spline/polygon data, and the node title with it.
		ChangeType |= EPCGChangeType::Structural | EPCGChangeType::Cosmetic;
	}
	return ChangeType;
}

TArray<FPCGSettingsOverridableParam> UPCGDynMeshPathToMeshSettings::GatherOverridableParams() const
{
	TArray<FPCGSettingsOverridableParam> Params = Super::GatherOverridableParams();
	PCGUtilsInputShorthands::ApplyOverrideLabels(Params);
	return Params;
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshPathToMeshSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	if (bInputSplines)
	{
		Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Spline | EPCGDataType::Polygon2D, true, true,
			LOCTEXT("SplinesPinTooltip", "Closed splines or 2D polygons. One mesh per input.")).SetRequiredPin();
	}
	else
	{
		Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
			LOCTEXT("PathsPinTooltip", "Closed paths: point data with points in path order and a @Data Boolean marking it closed. One mesh per path."))
			.SetRequiredPin();
	}
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshPathToMeshSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true);
	return Pins;
}

FPCGElementPtr UPCGDynMeshPathToMeshSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshPathToMeshElement>();
}

bool FPCGDynMeshPathToMeshElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshPathToMeshElement::ExecuteInternal);
	check(Context);

	const UPCGDynMeshPathToMeshSettings* Settings = Context->GetInputSettings<UPCGDynMeshPathToMeshSettings>();
	check(Settings);

	if (!Settings->bInputSplines && StripPathToMeshDataPrefix(Settings->ClosedAttributeName).IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoClosedAttribute",
			"Path To Mesh: Closed Attribute Name is empty. Name the @Data Boolean that marks a path closed (usually IsClosed)."), Context);
		return true;
	}
	if (StripPathToMeshDataPrefix(Settings->MeshTransformAttributeName).IsNone())
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NoMeshTransformName",
			"Path To Mesh: Mesh Transform Attribute Name is empty, so the outputs carry no frame and cannot be placed in the world."), Context);
	}
	if (Settings->bUseTransformAttribute && StripPathToMeshDataPrefix(Settings->TransformAttributeName).IsNone())
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NoFrameAttributeName",
			"Path To Mesh: Use Transform Attribute is on but Transform Attribute Name is empty; no input will be framed by an attribute."), Context);
	}

	const bool bUseDefaultExtrusion =
		(Settings->ExtrusionVector - FVector::ZAxisVector).IsNearlyZero(UE_SMALL_NUMBER) && Settings->bExtrudeInLocalSpace;
	const FName ClosedName = StripPathToMeshDataPrefix(Settings->ClosedAttributeName);
	const FName MeshTransformName = StripPathToMeshDataPrefix(Settings->MeshTransformAttributeName);

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const FPCGTaggedData& Input = Inputs[InputIndex];

		// ── Outline ───────────────────────────────────────────────────────────
		FPathToMeshOutline Outline;
		FText Reason;
		bool bGathered = false;
		if (!Settings->bInputSplines)
		{
			const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Input.Data);
			if (!PointData)
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("NotAPath",
					"{0}: expected a path (point data) but got {1}; it was skipped. Turn on Input Splines for splines."),
					PathToMeshInputText(InputIndex), Input.Data ? Input.Data->GetClass()->GetDisplayNameText() : LOCTEXT("NoData", "no data")), Context);
				continue;
			}
			bGathered = GatherPathToMeshPath(PointData, Settings->ClosedAttributeName, InputIndex, Outline, Reason);
		}
		else if (const UPCGSplineData* SplineData = Cast<const UPCGSplineData>(Input.Data))
		{
			bGathered = GatherPathToMeshSpline(SplineData, Settings->ErrorTolerance, InputIndex, Outline, Reason);
		}
		else if (const UPCGPolygon2DData* PolygonData = Cast<const UPCGPolygon2DData>(Input.Data))
		{
			GatherPathToMeshPolygon(PolygonData, Outline);
			bGathered = true;
		}
		else
		{
			PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("NotASpline",
				"{0}: expected a spline or 2D polygon but got {1}; it was skipped. Turn off Input Splines for paths."),
				PathToMeshInputText(InputIndex), Input.Data ? Input.Data->GetClass()->GetDisplayNameText() : LOCTEXT("NoData", "no data")), Context);
			continue;
		}
		if (!bGathered)
		{
			PCGLog::LogWarningOnGraph(Reason, Context);
			continue;
		}

		// ── Per-input settings ────────────────────────────────────────────────
		double ThicknessValue = 0.0;
		FText ReadError;
		if (!Settings->Thickness.TryReadDataValue(Input.Data, ThicknessValue, ReadError, LOCTEXT("ThicknessLabel", "Thickness"), InputIndex))
		{
			PCGLog::LogErrorOnGraph(ReadError, Context);
			continue;
		}

		FVector Up = FVector::UpVector;
		if (Settings->bFaceUpDirection)
		{
			if (!Settings->UpDirection.TryReadDataValue(Input.Data, Up, ReadError, LOCTEXT("UpLabel", "Up Direction"), InputIndex))
			{
				PCGLog::LogErrorOnGraph(ReadError, Context);
				continue;
			}
			if (Up.IsNearlyZero())
			{
				PCGLog::LogErrorOnGraph(PCGUtilsInputShorthands::FormatError(LOCTEXT("UpLabel", "Up Direction"), InputIndex,
					FText::Format(LOCTEXT("UpZero", "the direction is {0}, a zero vector, so there is no side to face"),
						FText::FromString(Up.ToString()))), Context);
				continue;
			}
		}

		// ── Frame ─────────────────────────────────────────────────────────────
		FTransform Frame = Outline.NativeTransform;
		bool bFramed = false;
		if (Settings->bUseTransformAttribute && !StripPathToMeshDataPrefix(Settings->TransformAttributeName).IsNone())
		{
			FText FrameWarning;
			bFramed = ReadPathToMeshFrameAttribute(Input.Data, Settings->TransformAttributeName, InputIndex, Frame, FrameWarning);
			if (!FrameWarning.IsEmpty())
			{
				PCGLog::LogWarningOnGraph(FrameWarning, Context);
			}
		}
		if (!bFramed && Settings->bUseCentroid && !Outline.CentroidPoints.IsEmpty())
		{
			FVector Centroid = FVector::ZeroVector;
			for (const FVector& Point : Outline.CentroidPoints)
			{
				Centroid += Point;
			}
			Frame = FTransform(Centroid / Outline.CentroidPoints.Num());
			bFramed = true;
		}
		if (!bFramed)
		{
			Frame = Outline.NativeTransform;
		}

		// ── Triangulate ───────────────────────────────────────────────────────
		UE::Geometry::FTriangulateCurvesOp TriangulateOp;
		// Thickness is applied below, after the facing is settled, so the solid grows toward the faces.
		TriangulateOp.Thickness = 0.0;
		// With facing on, the flip is applied after orientation instead.
		TriangulateOp.bFlipResult = Settings->bFlipResult && !Settings->bFaceUpDirection;
		TriangulateOp.FlattenMethod = Settings->FlattenMethod;
		TriangulateOp.CurveOffset = Settings->CurveOffset;
		TriangulateOp.OffsetOpenMethod = EOffsetOpenCurvesMethod::TreatAsClosed;
		TriangulateOp.OffsetJoinMethod = Settings->JoinMethod;
		TriangulateOp.MiterLimit = Settings->MiterLimit;
		TriangulateOp.OffsetClosedMethod = Settings->CurveOffset == 0.0
			? EOffsetClosedCurvesMethod::DoNotOffset
			: Settings->OffsetClosedCurves;

		TriangulateOp.AddWorldCurve(Outline.Outer, /*bClosed=*/true, Frame);
		for (const TArray<FVector>& Hole : Outline.Holes)
		{
			TriangulateOp.AddWorldCurve(Hole, /*bClosed=*/true, Frame);
		}
		if (Outline.bIsPolygon)
		{
			// As in the engine node: holes are only cut out of flattened curves.
			TriangulateOp.CombineMethod = ECombineCurvesMethod::Difference;
			TriangulateOp.FlattenMethod = EFlattenCurveMethod::ToBestFitPlane;
		}

		TriangulateOp.CalculateResult(nullptr);
		const FTransform MeshFrame = TriangulateOp.GetResultTransform();
		TUniquePtr<UE::Geometry::FDynamicMesh3> Mesh = TriangulateOp.ExtractResult();
		if (!Mesh || Mesh->TriangleCount() == 0)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("TriangulationFailed",
				"{0}: triangulation produced no triangles (the outline may be degenerate or self-intersecting)."),
				PathToMeshInputText(InputIndex)), Context);
			continue;
		}

		// ── Facing ────────────────────────────────────────────────────────────
		if (Settings->bFaceUpDirection)
		{
			if (PathToMeshWorldAreaNormal(*Mesh, MeshFrame).Dot(Up) < 0.0)
			{
				ReversePathToMeshFacing(*Mesh);
			}
			if (Settings->bFlipResult)
			{
				ReversePathToMeshFacing(*Mesh);
			}
		}

		// ── Extrude ───────────────────────────────────────────────────────────
		if (ThicknessValue > 0.0)
		{
			UE::Geometry::FExtrudeMesh Extrude(Mesh.Get());
			if (bUseDefaultExtrusion)
			{
				// Matches the triangulation's own thickness pass, UV scale included.
				FBox Bounds(Outline.Outer);
				const double MaxDim = Bounds.GetSize().GetMax();
				Extrude.DefaultExtrudeDistance = ThicknessValue;
				Extrude.UVScaleFactor = MaxDim > UE_SMALL_NUMBER ? 1.0 / MaxDim : 1.0;
			}
			else
			{
				const FVector ExtrusionVector = Settings->ExtrusionVector;
				const bool bLocal = Settings->bExtrudeInLocalSpace;
				Extrude.ExtrudedPositionFunc = [ExtrusionVector, bLocal, ThicknessValue](const FVector3d& P, const FVector3f& N, int) -> FVector3d
				{
					if (bLocal)
					{
						const FQuat RelativeRotation = FQuat::FindBetweenNormals(FVector::ZAxisVector, FVector(N));
						return P + RelativeRotation.RotateVector(ExtrusionVector) * ThicknessValue;
					}
					return P + ExtrusionVector * ThicknessValue;
				};
			}
			Extrude.Apply();
		}

		// ── Output ────────────────────────────────────────────────────────────
		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(MoveTemp(*Mesh));

		FPCGMetadataDomain* OutputDomain = OutputData->MutableMetadata()
			? OutputData->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		if (OutputDomain)
		{
			if (Settings->bCopyDataAttributes)
			{
				const UPCGMetadata* SourceMetadata = Input.Data->ConstMetadata();
				if (const FPCGMetadataDomain* SourceDomain = SourceMetadata
					? SourceMetadata->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr)
				{
					FPCGMetadataDomainInitializeParams Params(SourceDomain);
					if (!Settings->bInputSplines && !Settings->bCopyClosedAttribute)
					{
						Params.FilteredAttributes = TSet<FName>{ClosedName};
						Params.FilterMode = EPCGMetadataFilterMode::ExcludeAttributes;
					}
					OutputDomain->InitializeAsCopy(Params);
				}
			}
			if (!MeshTransformName.IsNone())
			{
				WritePathToMeshDataValue<FTransform>(*OutputDomain, MeshTransformName, MeshFrame);
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
