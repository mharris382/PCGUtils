// Copyright Max Harris

#include "Elements/Query/PCGDynMeshProjectPoints.h"

#include "Async/ParallelFor.h"
#include <atomic>
#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicSubmesh3.h"
#include "Elements/Conversion/PCGUtilsDynMeshSurfacePathCommon.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshProjectPoints"

namespace
{
	using namespace UE::Geometry;
	namespace Common = PCGUtilsDynMeshSurfacePathCommon;

	FText ProjectNodeTitle()
	{
		return LOCTEXT("NodeTitleText", "DynMesh | Project Points");
	}

	/** One mesh input, resolved once and shared by every point data projected onto it. */
	struct FProjectionSurface
	{
		Common::FResolvedSurface Surface;

		/** The source mesh triangle-level data is read from; Surface.Mesh is a submesh of it under a Selection. */
		const FDynamicMesh3* BaseMesh = nullptr;

		FTransform MeshToOutput = FTransform::Identity;
		FTransform OutputToMesh = FTransform::Identity;

		/** Polygroup layer named by the settings, or null to read the default PolyGroups. */
		const FDynamicMeshPolygroupAttribute* PolygroupLayer = nullptr;
		bool bPolygroupsReadable = false;

		/** Position on the Mesh pin, for the Mesh Index attribute and for messages. */
		int32 MeshIndex = INDEX_NONE;

		int32 ToBaseTriangle(int32 SurfaceTriangleID) const
		{
			return Surface.Submesh ? Surface.Submesh->MapTriangleToBaseMesh(SurfaceTriangleID) : SurfaceTriangleID;
		}
	};

	/** What one point's ray found. Everything spatial is in output space; triangle IDs are the base mesh's. */
	struct FProjectionHit
	{
		bool bHit = false;
		bool bBackFace = false;
		int32 SurfaceIndex = INDEX_NONE;
		int32 BaseTriangleID = INDEX_NONE;
		FVector3d Barycentric = FVector3d::ZeroVector;

		/** Signed: negative for a backward hit. */
		double Distance = 0.0;
		FVector Location = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
	};

	/** Turns a mesh-local normal into output space. Normals take the inverse scale, so non-uniform scale stays correct. */
	FVector NormalToOutput(const FTransform& MeshToOutput, const FVector3d& LocalNormal)
	{
		const FVector Scale = MeshToOutput.GetScale3D();
		const FVector Unscaled(
			FMath::IsNearlyZero(Scale.X) ? 0.0 : LocalNormal.X / Scale.X,
			FMath::IsNearlyZero(Scale.Y) ? 0.0 : LocalNormal.Y / Scale.Y,
			FMath::IsNearlyZero(Scale.Z) ? 0.0 : LocalNormal.Z / Scale.Z);
		return MeshToOutput.GetRotation().RotateVector(Unscaled).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	}

	/** The mesh-local normal at a barycentric point of a base-mesh triangle, per NormalSource. */
	FVector3d LocalNormalAt(const FDynamicMesh3& Mesh, int32 TriangleID, const FVector3d& Bary, EPCGUtilsDynMeshProjectNormalSource Source)
	{
		const FVector3d FaceNormal = Mesh.GetTriNormal(TriangleID);
		if (Source == EPCGUtilsDynMeshProjectNormalSource::Face)
		{
			return FaceNormal;
		}

		FVector3d Blended = FVector3d::ZeroVector;
		const FDynamicMeshNormalOverlay* Normals = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryNormals() : nullptr;
		if (Normals && Normals->IsSetTriangle(TriangleID))
		{
			FVector3f A, B, C;
			Normals->GetTriElements(TriangleID, A, B, C);
			Blended = FVector3d(A) * Bary.X + FVector3d(B) * Bary.Y + FVector3d(C) * Bary.Z;
		}
		else if (Mesh.HasVertexNormals())
		{
			const FIndex3i Tri = Mesh.GetTriangle(TriangleID);
			Blended = FVector3d(Mesh.GetVertexNormal(Tri.A)) * Bary.X + FVector3d(Mesh.GetVertexNormal(Tri.B)) * Bary.Y
				+ FVector3d(Mesh.GetVertexNormal(Tri.C)) * Bary.Z;
		}
		return Blended.Normalize(UE_DOUBLE_SMALL_NUMBER) ? Blended : FaceNormal;
	}

	TOptional<FVector2D> UVAt(const FDynamicMesh3& Mesh, int32 UVChannel, int32 TriangleID, const FVector3d& Bary)
	{
		if (!Mesh.HasAttributes() || UVChannel < 0 || UVChannel >= Mesh.Attributes()->NumUVLayers())
		{
			return {};
		}
		const FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->GetUVLayer(UVChannel);
		if (!UVs || !UVs->IsSetTriangle(TriangleID))
		{
			return {};
		}
		FVector2f A, B, C;
		UVs->GetTriElements(TriangleID, A, B, C);
		return FVector2D(A) * Bary.X + FVector2D(B) * Bary.Y + FVector2D(C) * Bary.Z;
	}

	FVector4 ColorAt(const FDynamicMesh3& Mesh, int32 TriangleID, const FVector3d& Bary)
	{
		const FDynamicMeshColorOverlay* Colors = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryColors() : nullptr;
		if (Colors && Colors->IsSetTriangle(TriangleID))
		{
			FVector4f A, B, C;
			Colors->GetTriElements(TriangleID, A, B, C);
			return FVector4(A) * Bary.X + FVector4(B) * Bary.Y + FVector4(C) * Bary.Z;
		}
		if (Mesh.HasVertexColors())
		{
			const FIndex3i Tri = Mesh.GetTriangle(TriangleID);
			const FVector3d RGB = FVector3d(Mesh.GetVertexColor(Tri.A)) * Bary.X + FVector3d(Mesh.GetVertexColor(Tri.B)) * Bary.Y
				+ FVector3d(Mesh.GetVertexColor(Tri.C)) * Bary.Z;
			return FVector4(RGB.X, RGB.Y, RGB.Z, 1.0);
		}
		return FVector4::One();
	}

	/**
	 * Casts one ray against one surface. Direction is a unit vector in output space; MaxDistance is in output units,
	 * 0 for unlimited. On a hit, fills everything in OutHit except SurfaceIndex.
	 */
	bool CastRay(const FProjectionSurface& Surface, const FVector& Origin, const FVector& Direction, double MaxDistance,
		bool bIgnoreBackFaces, EPCGUtilsDynMeshProjectNormalSource NormalSource, FProjectionHit& OutHit)
	{
		// A unit output-space step is LocalScale long in mesh space, so the ray parameter converts back by dividing.
		const FVector3d LocalOrigin = Surface.OutputToMesh.TransformPosition(Origin);
		const FVector3d LocalStep = Surface.OutputToMesh.TransformVector(Direction);
		const double LocalScale = LocalStep.Length();
		if (LocalScale <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		const FVector3d LocalDirection = LocalStep / LocalScale;

		const FDynamicMesh3& Mesh = *Surface.Surface.Mesh;
		IMeshSpatial::FQueryOptions Options;
		if (MaxDistance > 0.0)
		{
			Options.MaxDistance = MaxDistance * LocalScale;
		}
		if (bIgnoreBackFaces)
		{
			Options.TriangleFilterF = [&Mesh, LocalDirection](int32 TriangleID)
			{
				return Mesh.GetTriNormal(TriangleID).Dot(LocalDirection) < 0.0;
			};
		}

		double RayT = 0.0;
		int32 TriangleID = INDEX_NONE;
		FVector3d Bary = FVector3d::ZeroVector;
		const FRay3d Ray(LocalOrigin, LocalDirection, /*bIsNormalized=*/true);
		if (!Surface.Surface.Tree->FindNearestHitTriangle(Ray, RayT, TriangleID, Bary, Options) || !Mesh.IsTriangle(TriangleID))
		{
			return false;
		}

		const double Distance = RayT / LocalScale;
		if (MaxDistance > 0.0 && Distance > MaxDistance)
		{
			return false;
		}

		// Normals and per-corner data are read from the base mesh: a submesh keeps each triangle's corner order, so
		// the barycentrics carry over unchanged.
		const int32 BaseTriangleID = Surface.ToBaseTriangle(TriangleID);
		const FDynamicMesh3& BaseMesh = *Surface.BaseMesh;
		if (!BaseMesh.IsTriangle(BaseTriangleID))
		{
			return false;
		}

		OutHit.bHit = true;
		OutHit.bBackFace = BaseMesh.GetTriNormal(BaseTriangleID).Dot(LocalDirection) > 0.0;
		OutHit.BaseTriangleID = BaseTriangleID;
		OutHit.Barycentric = Bary;
		OutHit.Distance = Distance;
		OutHit.Location = Surface.MeshToOutput.TransformPosition(Ray.PointAt(RayT));
		OutHit.Normal = NormalToOutput(Surface.MeshToOutput, LocalNormalAt(BaseMesh, BaseTriangleID, Bary, NormalSource));
		return true;
	}

	FVector AxisOf(const FQuat& Rotation, EPCGUtilsDynMeshProjectAlignAxis Axis)
	{
		switch (Axis)
		{
		case EPCGUtilsDynMeshProjectAlignAxis::X: return Rotation.GetAxisX();
		case EPCGUtilsDynMeshProjectAlignAxis::Y: return Rotation.GetAxisY();
		case EPCGUtilsDynMeshProjectAlignAxis::NegativeX: return -Rotation.GetAxisX();
		case EPCGUtilsDynMeshProjectAlignAxis::NegativeY: return -Rotation.GetAxisY();
		case EPCGUtilsDynMeshProjectAlignAxis::NegativeZ: return -Rotation.GetAxisZ();
		case EPCGUtilsDynMeshProjectAlignAxis::Z:
		default: return Rotation.GetAxisZ();
		}
	}

	/** An enabled output attribute with no name cannot be written; says which setting is at fault. */
	bool IsWritable(bool bEnabled, FName Name, const TCHAR* SettingName, FPCGContext* Context)
	{
		if (!bEnabled)
		{
			return false;
		}
		if (Name.IsNone())
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("UnnamedAttribute", "DynMesh | Project Points: {0} is None, so that attribute is not written. Give it a name or turn its output off."),
				FText::FromString(SettingName)), Context);
			return false;
		}
		return true;
	}

	template <typename T>
	FPCGMetadataAttribute<T>* CreateAttribute(UPCGMetadata* Metadata, FName Name, const T& Default, bool bAllowsInterpolation)
	{
		// Override and overwrite: the input may already carry an attribute of this name, possibly of another type,
		// and this node's answer replaces it.
		return Metadata->FindOrCreateAttribute<T>(FPCGAttributeIdentifier(Name, PCGMetadataDomainID::Elements), Default,
			bAllowsInterpolation, /*bOverrideParent=*/true, /*bOverwriteIfTypeMismatch=*/true);
	}

	/** Reads each point's direction, before point-space rotation, inversion and normalization. False after logging. */
	bool ReadDirections(const UPCGDynMeshProjectPointsSettings& Settings, const UPCGBasePointData& Points, int32 InputIndex,
		TArray<FVector>& OutDirections, FPCGContext* Context)
	{
		const int32 NumPoints = Points.GetNumPoints();
		if (Settings.DirectionSource == EPCGUtilsDynMeshProjectDirectionSource::Constant)
		{
			OutDirections.Init(Settings.Direction, NumPoints);
			return true;
		}

		const FPCGAttributePropertyInputSelector Selector = Settings.DirectionAttribute.CopyAndFixLast(&Points);
		const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(&Points, Selector);
		const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(&Points, Selector);
		if (!Accessor || !Keys)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MissingDirectionAttribute", "DynMesh | Project Points: Direction Attribute '{0}' does not exist on point input {1}."),
				Selector.GetDisplayText(), FText::AsNumber(InputIndex)), Context);
			return false;
		}

		OutDirections.SetNumUninitialized(NumPoints);
		if (!Accessor->GetRange<FVector>(OutDirections, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("UnreadableDirectionAttribute", "DynMesh | Project Points: Direction Attribute '{0}' on point input {1} cannot be read as a Vector."),
				Selector.GetDisplayText(), FText::AsNumber(InputIndex)), Context);
			return false;
		}
		return true;
	}
}

UPCGDynMeshProjectPointsSettings::UPCGDynMeshProjectPointsSettings()
{
	DirectionAttribute.Update(TEXT("$Rotation.Up"));
}

#if WITH_EDITOR
FText UPCGDynMeshProjectPointsSettings::GetDefaultNodeTitle() const
{
	return ProjectNodeTitle();
}

FText UPCGDynMeshProjectPointsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Casts a ray from every point along a constant or per-point direction and moves the point onto the first DynMesh "
		"surface it hits, optionally turning it to the surface and writing what the hit found. The points keep all their "
		"attributes. Several meshes act as one scene; a Selection or Selector limits the surface to the selected triangles.");
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshProjectPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();

	// The points are what the output is a mutation of, so they go first, ahead of the base class's required Mesh
	// pin and optional Selector pin.
	Pins.EmplaceAt_GetRef(0, PCGDynMeshProjectPointsConstants::PointsInputPin, EPCGDataType::Point, true, true)
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshProjectPointsSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGDynMeshProjectPointsConstants::PointsOutputPin, EPCGDataType::Point, true, true)};
}

FPCGElementPtr UPCGDynMeshProjectPointsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshProjectPointsElement>();
}

bool FPCGDynMeshProjectPointsElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshProjectPointsElement::ExecuteInternal);

	check(Context);
	const UPCGDynMeshProjectPointsSettings* Settings = Context->GetInputSettings<UPCGDynMeshProjectPointsSettings>();
	check(Settings);

	if (Settings->MaxDistance < 0.0)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("NegativeMaxDistance", "DynMesh | Project Points: Max Distance is {0}; it must be 0 (unlimited) or greater."),
			FText::AsNumber(Settings->MaxDistance)), Context);
		return true;
	}

	// Resolve every mesh input once; each is one AABB tree shared by every point data.
	TArray<FProjectionSurface> Surfaces;
	const TArray<FPCGTaggedData> MeshInputs = Context->InputData.GetInputsByPin(PCGDynMeshProjectPointsConstants::MeshInputPin);
	for (int32 MeshIndex = 0; MeshIndex < MeshInputs.Num(); ++MeshIndex)
	{
		FProjectionSurface Surface;
		Surface.Surface = Common::ResolveSurface(MeshInputs[MeshIndex].Data, Settings, Context, ProjectNodeTitle());
		if (!Surface.Surface.IsValid())
		{
			continue;
		}
		const UDynamicMesh* SourceMesh = Surface.Surface.MeshData->GetDynamicMesh();
		Surface.BaseMesh = SourceMesh ? SourceMesh->GetMeshPtr() : nullptr;
		if (!Surface.BaseMesh)
		{
			continue;
		}
		Surface.MeshIndex = MeshIndex;
		Surface.MeshToOutput = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, Surface.Surface.MeshData, Settings->bWorldSpace);
		Surface.OutputToMesh = Surface.MeshToOutput.Inverse();

		if (Settings->bOutputPolygroup)
		{
			if (Settings->PolygroupLayer.IsNone())
			{
				Surface.bPolygroupsReadable = Surface.BaseMesh->HasTriangleGroups();
			}
			else if (Surface.BaseMesh->HasAttributes())
			{
				for (int32 LayerIndex = 0; LayerIndex < Surface.BaseMesh->Attributes()->NumPolygroupLayers(); ++LayerIndex)
				{
					const FDynamicMeshPolygroupAttribute* Layer = Surface.BaseMesh->Attributes()->GetPolygroupLayer(LayerIndex);
					if (Layer && Layer->GetName() == Settings->PolygroupLayer)
					{
						Surface.PolygroupLayer = Layer;
						Surface.bPolygroupsReadable = true;
						break;
					}
				}
			}
			if (!Surface.bPolygroupsReadable)
			{
				PCGLog::LogWarningOnGraph(Settings->PolygroupLayer.IsNone()
					? FText::Format(LOCTEXT("NoDefaultPolygroups",
						"DynMesh | Project Points: mesh input {0} has no default PolyGroups; points that hit it get no PolyGroup attribute."),
						FText::AsNumber(MeshIndex))
					: FText::Format(LOCTEXT("NoPolygroupLayer",
						"DynMesh | Project Points: mesh input {0} has no PolyGroup layer named '{1}'; points that hit it get no PolyGroup attribute."),
						FText::AsNumber(MeshIndex), FText::FromName(Settings->PolygroupLayer)), Context);
			}
		}
		Surfaces.Add(MoveTemp(Surface));
	}

	if (Surfaces.IsEmpty())
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("NoSurfaces", "DynMesh | Project Points has no usable mesh on the Mesh pin, so no point hits anything."), Context);
	}

	const bool bWriteSuccess = IsWritable(Settings->bOutputSuccess, Settings->SuccessAttributeName, TEXT("Success Attribute Name"), Context);
	const bool bWriteDistance = IsWritable(Settings->bOutputDistance, Settings->DistanceAttributeName, TEXT("Distance Attribute Name"), Context);
	const bool bWriteLocation = IsWritable(Settings->bOutputHitLocation, Settings->HitLocationAttributeName, TEXT("Hit Location Attribute Name"), Context);
	const bool bWriteNormal = IsWritable(Settings->bOutputHitNormal, Settings->HitNormalAttributeName, TEXT("Hit Normal Attribute Name"), Context);
	const bool bWriteBackFace = IsWritable(Settings->bOutputBackFace, Settings->BackFaceAttributeName, TEXT("Back Face Attribute Name"), Context);
	const bool bWriteTriangle = IsWritable(Settings->bOutputTriangleId, Settings->TriangleIdAttributeName, TEXT("Triangle Id Attribute Name"), Context);
	const bool bWriteBary = IsWritable(Settings->bOutputBarycentric, Settings->BarycentricAttributeName, TEXT("Barycentric Attribute Name"), Context);
	const bool bWriteUV = IsWritable(Settings->bOutputUV, Settings->UVAttributeName, TEXT("UV Attribute Name"), Context);
	const bool bWriteColor = IsWritable(Settings->bOutputColor, Settings->ColorAttributeName, TEXT("Color Attribute Name"), Context);
	const bool bWritePolygroup = IsWritable(Settings->bOutputPolygroup, Settings->PolygroupAttributeName, TEXT("Polygroup Attribute Name"), Context);
	const bool bWriteMaterialId = IsWritable(Settings->bOutputMaterialId, Settings->MaterialIdAttributeName, TEXT("Material Id Attribute Name"), Context);
	const bool bWriteMaterial = IsWritable(Settings->bOutputMaterial, Settings->MaterialAttributeName, TEXT("Material Attribute Name"), Context);
	const bool bWriteMeshIndex = IsWritable(Settings->bOutputMeshIndex, Settings->MeshIndexAttributeName, TEXT("Mesh Index Attribute Name"), Context);
	const bool bWriteAnyAttribute = bWriteSuccess || bWriteDistance || bWriteLocation || bWriteNormal || bWriteBackFace || bWriteTriangle
		|| bWriteBary || bWriteUV || bWriteColor || bWritePolygroup || bWriteMaterialId || bWriteMaterial || bWriteMeshIndex;

	const EPCGUtilsDynMeshProjectAxes Axes = static_cast<EPCGUtilsDynMeshProjectAxes>(Settings->PositionAxes);

	const TArray<FPCGTaggedData> PointInputs = Context->InputData.GetInputsByPin(PCGDynMeshProjectPointsConstants::PointsInputPin);
	for (int32 InputIndex = 0; InputIndex < PointInputs.Num(); ++InputIndex)
	{
		const FPCGTaggedData& PointInput = PointInputs[InputIndex];
		const UPCGBasePointData* InPoints = Cast<const UPCGBasePointData>(PointInput.Data);
		if (!InPoints)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("NotPoints", "DynMesh | Project Points: input {0} on the In pin is not point data and was skipped."),
				FText::AsNumber(InputIndex)), Context);
			continue;
		}

		const int32 NumPoints = InPoints->GetNumPoints();
		TArray<FVector> Directions;
		if (!ReadDirections(*Settings, *InPoints, InputIndex, Directions, Context))
		{
			continue;
		}

		const TConstPCGValueRange<FTransform> InTransforms = InPoints->GetConstTransformValueRange();
		TArray<FProjectionHit> Hits;
		Hits.SetNum(NumPoints);
		std::atomic<int32> NumZeroDirections = 0;
		std::atomic<int32> FirstZeroDirection = MAX_int32;

		ParallelFor(NumPoints, [&](int32 PointIndex)
		{
			FVector Direction = Directions[PointIndex];
			if (Settings->bDirectionInPointSpace)
			{
				Direction = InTransforms[PointIndex].GetRotation().RotateVector(Direction);
			}
			if (Settings->bInvertDirection)
			{
				Direction = -Direction;
			}
			if (!Direction.Normalize())
			{
				++NumZeroDirections;
				int32 Expected = FirstZeroDirection.load();
				while (PointIndex < Expected && !FirstZeroDirection.compare_exchange_weak(Expected, PointIndex)) {}
				return;
			}

			const FVector Origin = InTransforms[PointIndex].GetLocation();
			FProjectionHit& Best = Hits[PointIndex];
			for (int32 SurfaceIndex = 0; SurfaceIndex < Surfaces.Num(); ++SurfaceIndex)
			{
				for (const double Sign : {1.0, -1.0})
				{
					if (Sign < 0.0 && !Settings->bBidirectional)
					{
						break;
					}
					FProjectionHit Candidate;
					if (CastRay(Surfaces[SurfaceIndex], Origin, Direction * Sign, Settings->MaxDistance,
						Settings->bIgnoreBackFaces, Settings->NormalSource, Candidate))
					{
						Candidate.Distance *= Sign;
						Candidate.SurfaceIndex = SurfaceIndex;
						// Nearest wins; a forward hit wins a tie because it is tested first.
						if (!Best.bHit || FMath::Abs(Candidate.Distance) < FMath::Abs(Best.Distance))
						{
							Best = Candidate;
						}
					}
				}
			}
		});

		if (NumZeroDirections > 0)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("ZeroDirection", "DynMesh | Project Points: {0} point(s) of input {1} have a zero-length direction, starting at point {2}; they were not projected."),
				FText::AsNumber(NumZeroDirections.load()), FText::AsNumber(InputIndex), FText::AsNumber(FirstZeroDirection.load())), Context);
		}

		// The output is the input, moved: initialized from it so every attribute, tag and the target actor carry over.
		TArray<int32> KeptIndices;
		KeptIndices.Reserve(NumPoints);
		for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
		{
			if (Hits[PointIndex].bHit || !Settings->bPruneMisses)
			{
				KeptIndices.Add(PointIndex);
			}
		}

		UPCGPointArrayData* OutPoints = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
		FPCGInitializeFromDataParams InitParams(InPoints);
		InitParams.bInheritSpatialData = false;
		OutPoints->InitializeFromDataWithParams(InitParams);
		UPCGBasePointData::SetPoints(InPoints, OutPoints, KeptIndices, /*bCopyAll=*/false);
		OutPoints->AllocateProperties(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::MetadataEntry);

		TPCGValueRange<FTransform> OutTransforms = OutPoints->GetTransformValueRange();
		for (int32 OutIndex = 0; OutIndex < KeptIndices.Num(); ++OutIndex)
		{
			const FProjectionHit& Hit = Hits[KeptIndices[OutIndex]];
			if (!Hit.bHit)
			{
				continue;
			}
			FTransform& Transform = OutTransforms[OutIndex];
			if (Settings->bProjectPosition)
			{
				const FVector Target = Hit.Location + Hit.Normal * Settings->SurfaceOffset;
				FVector Position = Transform.GetLocation();
				if (EnumHasAnyFlags(Axes, EPCGUtilsDynMeshProjectAxes::X)) { Position.X = Target.X; }
				if (EnumHasAnyFlags(Axes, EPCGUtilsDynMeshProjectAxes::Y)) { Position.Y = Target.Y; }
				if (EnumHasAnyFlags(Axes, EPCGUtilsDynMeshProjectAxes::Z)) { Position.Z = Target.Z; }
				Transform.SetLocation(Position);
			}
			if (Settings->bProjectRotation)
			{
				const FQuat Rotation = Transform.GetRotation();
				const FQuat Turn = FQuat::FindBetweenNormals(AxisOf(Rotation, Settings->AlignAxis), Hit.Normal);
				Transform.SetRotation((Turn * Rotation).GetNormalized());
			}
		}

		if (bWriteAnyAttribute && !KeptIndices.IsEmpty())
		{
			UPCGMetadata* Metadata = OutPoints->MutableMetadata();
			FPCGMetadataDomain* Elements = Metadata ? Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
			if (Elements)
			{
				FPCGMetadataAttribute<bool>* SuccessAttr = bWriteSuccess ? CreateAttribute<bool>(Metadata, Settings->SuccessAttributeName, false, false) : nullptr;
				FPCGMetadataAttribute<double>* DistanceAttr = bWriteDistance ? CreateAttribute<double>(Metadata, Settings->DistanceAttributeName, 0.0, true) : nullptr;
				FPCGMetadataAttribute<FVector>* LocationAttr = bWriteLocation ? CreateAttribute<FVector>(Metadata, Settings->HitLocationAttributeName, FVector::ZeroVector, true) : nullptr;
				FPCGMetadataAttribute<FVector>* NormalAttr = bWriteNormal ? CreateAttribute<FVector>(Metadata, Settings->HitNormalAttributeName, FVector::ZeroVector, true) : nullptr;
				FPCGMetadataAttribute<bool>* BackFaceAttr = bWriteBackFace ? CreateAttribute<bool>(Metadata, Settings->BackFaceAttributeName, false, false) : nullptr;
				FPCGMetadataAttribute<int32>* TriangleAttr = bWriteTriangle ? CreateAttribute<int32>(Metadata, Settings->TriangleIdAttributeName, INDEX_NONE, false) : nullptr;
				FPCGMetadataAttribute<FVector>* BaryAttr = bWriteBary ? CreateAttribute<FVector>(Metadata, Settings->BarycentricAttributeName, FVector::ZeroVector, true) : nullptr;
				FPCGMetadataAttribute<FVector2D>* UVAttr = bWriteUV ? CreateAttribute<FVector2D>(Metadata, Settings->UVAttributeName, FVector2D::ZeroVector, true) : nullptr;
				FPCGMetadataAttribute<FVector4>* ColorAttr = bWriteColor ? CreateAttribute<FVector4>(Metadata, Settings->ColorAttributeName, FVector4::One(), true) : nullptr;
				FPCGMetadataAttribute<int32>* PolygroupAttr = bWritePolygroup ? CreateAttribute<int32>(Metadata, Settings->PolygroupAttributeName, INDEX_NONE, false) : nullptr;
				FPCGMetadataAttribute<int32>* MaterialIdAttr = bWriteMaterialId ? CreateAttribute<int32>(Metadata, Settings->MaterialIdAttributeName, INDEX_NONE, false) : nullptr;
				FPCGMetadataAttribute<FSoftObjectPath>* MaterialAttr = bWriteMaterial ? CreateAttribute<FSoftObjectPath>(Metadata, Settings->MaterialAttributeName, FSoftObjectPath(), false) : nullptr;
				FPCGMetadataAttribute<int32>* MeshIndexAttr = bWriteMeshIndex ? CreateAttribute<int32>(Metadata, Settings->MeshIndexAttributeName, INDEX_NONE, false) : nullptr;

				TPCGValueRange<int64> Entries = OutPoints->GetMetadataEntryValueRange();
				for (int32 OutIndex = 0; OutIndex < KeptIndices.Num(); ++OutIndex)
				{
					const FProjectionHit& Hit = Hits[KeptIndices[OutIndex]];
					// Only the success flag says anything about a miss; everything else keeps its default there.
					if (!Hit.bHit && !SuccessAttr)
					{
						continue;
					}

					int64& Entry = Entries[OutIndex];
					Elements->InitializeOnSet(Entry);
					if (SuccessAttr) { SuccessAttr->SetValue(Entry, Hit.bHit); }
					if (!Hit.bHit)
					{
						continue;
					}

					const FProjectionSurface& Surface = Surfaces[Hit.SurfaceIndex];
					const FDynamicMesh3& Mesh = *Surface.BaseMesh;
					if (DistanceAttr) { DistanceAttr->SetValue(Entry, Hit.Distance); }
					if (LocationAttr) { LocationAttr->SetValue(Entry, Hit.Location); }
					if (NormalAttr) { NormalAttr->SetValue(Entry, Hit.Normal); }
					if (BackFaceAttr) { BackFaceAttr->SetValue(Entry, Hit.bBackFace); }
					if (TriangleAttr) { TriangleAttr->SetValue(Entry, Hit.BaseTriangleID); }
					if (BaryAttr) { BaryAttr->SetValue(Entry, FVector(Hit.Barycentric)); }
					if (MeshIndexAttr) { MeshIndexAttr->SetValue(Entry, Surface.MeshIndex); }
					if (UVAttr)
					{
						if (const TOptional<FVector2D> UV = UVAt(Mesh, Settings->UVChannel, Hit.BaseTriangleID, Hit.Barycentric))
						{
							UVAttr->SetValue(Entry, UV.GetValue());
						}
					}
					if (ColorAttr) { ColorAttr->SetValue(Entry, ColorAt(Mesh, Hit.BaseTriangleID, Hit.Barycentric)); }
					if (PolygroupAttr && Surface.bPolygroupsReadable)
					{
						PolygroupAttr->SetValue(Entry, Surface.PolygroupLayer
							? Surface.PolygroupLayer->GetValue(Hit.BaseTriangleID)
							: Mesh.GetTriangleGroup(Hit.BaseTriangleID));
					}
					if ((MaterialIdAttr || MaterialAttr) && Mesh.HasAttributes() && Mesh.Attributes()->HasMaterialID())
					{
						const int32 MaterialID = Mesh.Attributes()->GetMaterialID()->GetValue(Hit.BaseTriangleID);
						if (MaterialIdAttr) { MaterialIdAttr->SetValue(Entry, MaterialID); }
						const TArray<TObjectPtr<UMaterialInterface>>& Materials = Surface.Surface.MeshData->GetMaterials();
						if (MaterialAttr && Materials.IsValidIndex(MaterialID) && Materials[MaterialID])
						{
							MaterialAttr->SetValue(Entry, FSoftObjectPath(Materials[MaterialID]));
						}
					}
				}
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(PointInput);
		Output.Data = OutPoints;
		Output.Pin = PCGDynMeshProjectPointsConstants::PointsOutputPin;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
