// Copyright Max Harris

#include "Elements/Painters/PCGDynMeshDistancePainter.h"
#include "Helpers/PCGUtilsParallelInputs.h"

#include "Async/ParallelFor.h"
#include "Data/PCGBasePointData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsPainterDistanceField.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshDistancePainter"

namespace PCGUtilsPainterDistance
{
	const FName PointsPin = TEXT("Points");

	/** A point whose influence would cover more cells than this is tested by every vertex instead. */
	constexpr int64 MaxCellsPerPoint = 4096;

	/** Grid coordinates are clamped so a far-away point cannot overflow an int32 cell index. */
	constexpr double MaxCellCoordinate = 1 << 30;

	double MetricLength(EPCGUtilsPainterDistanceMetric Metric, const FVector& Delta)
	{
		switch (Metric)
		{
		case EPCGUtilsPainterDistanceMetric::Manhattan:
			return FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) + FMath::Abs(Delta.Z);
		case EPCGUtilsPainterDistanceMetric::Chebyshev:
			return FMath::Max3(FMath::Abs(Delta.X), FMath::Abs(Delta.Y), FMath::Abs(Delta.Z));
		case EPCGUtilsPainterDistanceMetric::Euclidean:
		default:
			return Delta.Size();
		}
	}

	int32 ToCellCoordinate(double Value, double InvCellSize)
	{
		return static_cast<int32>(FMath::Clamp(FMath::FloorToDouble(Value * InvCellSize), -MaxCellCoordinate, MaxCellCoordinate));
	}

	FDistanceField::FDistanceField(const UPCGDynMeshDistancePainterFactoryData& InFactory)
		: Factory(InFactory)
	{
		const double Reach = FMath::Max(Factory.MaxRadius, 0.0);
		Points.Reserve(Factory.Points.Num());
		double SizeSum = 0.0;
		for (const FPCGUtilsPainterDistancePoint& Source : Factory.Points)
		{
			FPreparedPoint& Point = Points.AddDefaulted_GetRef();
			Point.Transform = Source.Transform;
			Point.Location = Source.Transform.GetLocation();
			Point.BoundsMin = Source.BoundsMin.ComponentMin(Source.BoundsMax);
			Point.BoundsMax = Source.BoundsMin.ComponentMax(Source.BoundsMax);

			FBox Shape(Point.Location, Point.Location);
			if (Factory.Target == EPCGUtilsPainterDistanceShape::SphereBounds)
			{
				// PCGEx's sphere: centered on the point's location, radius = length of the scaled extents.
				Point.SphereRadius = ((Point.BoundsMax - Point.BoundsMin) * 0.5 * Point.Transform.GetScale3D().GetAbs()).Size();
				Shape = Shape.ExpandBy(Point.SphereRadius);
			}
			else if (Factory.Target == EPCGUtilsPainterDistanceShape::BoxBounds)
			{
				Shape = FBox(Point.BoundsMin, Point.BoundsMax).TransformBy(Point.Transform);
			}

			// Every metric here is at least the largest per-axis difference, so nothing in range lies outside this.
			Point.Influence = Shape.ExpandBy(Reach);
			SizeSum += Point.Influence.GetSize().GetMax();
		}

		if (Points.IsEmpty())
		{
			return;
		}

		// Cell size near the typical influence size: most points land in 1 to 8 cells, and a cell holds few points.
		const double CellSize = FMath::Max(SizeSum / Points.Num(), 1.0);
		InvCellSize = 1.0 / CellSize;

		TMap<FIntVector, TArray<int32>> Cells;
		for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
		{
			const FBox& Influence = Points[PointIndex].Influence;
			const FIntVector Lo(ToCellCoordinate(Influence.Min.X, InvCellSize), ToCellCoordinate(Influence.Min.Y, InvCellSize), ToCellCoordinate(Influence.Min.Z, InvCellSize));
			const FIntVector Hi(ToCellCoordinate(Influence.Max.X, InvCellSize), ToCellCoordinate(Influence.Max.Y, InvCellSize), ToCellCoordinate(Influence.Max.Z, InvCellSize));
			const int64 CellCount = (int64(Hi.X) - Lo.X + 1) * (int64(Hi.Y) - Lo.Y + 1) * (int64(Hi.Z) - Lo.Z + 1);
			if (CellCount > MaxCellsPerPoint)
			{
				GlobalPoints.Add(PointIndex);
				continue;
			}
			for (int32 X = Lo.X; X <= Hi.X; ++X)
			{
				for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
				{
					for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
					{
						Cells.FindOrAdd(FIntVector(X, Y, Z)).Add(PointIndex);
					}
				}
			}
		}

		// Flatten to one contiguous array so lookups touch little memory.
		CellToBucket.Reserve(Cells.Num());
		BucketStarts.Reserve(Cells.Num() + 1);
		for (TPair<FIntVector, TArray<int32>>& Cell : Cells)
		{
			CellToBucket.Add(Cell.Key, BucketStarts.Num());
			BucketStarts.Add(BucketPoints.Num());
			BucketPoints.Append(Cell.Value);
		}
		BucketStarts.Add(BucketPoints.Num());
	}

	FIntVector FDistanceField::CellOf(const FVector& WorldPosition) const
	{
		return FIntVector(
			ToCellCoordinate(WorldPosition.X, InvCellSize),
			ToCellCoordinate(WorldPosition.Y, InvCellSize),
			ToCellCoordinate(WorldPosition.Z, InvCellSize));
	}

	double FDistanceField::Measure(const FPreparedPoint& Point, const FVector& WorldPosition) const
	{
		// Mirrors PCGEx's GetSpatializedCenter for the target, with the vertex as a Center source.
		switch (Factory.Target)
		{
		case EPCGUtilsPainterDistanceShape::SphereBounds:
		{
			const FVector Surface = Point.Location + (WorldPosition - Point.Location).GetSafeNormal() * Point.SphereRadius;
			const bool bOverlap = FVector::DotProduct(Point.Location - WorldPosition, Surface - WorldPosition) < 0.0;
			if (bOverlap && Factory.bOverlapIsZero)
			{
				return 0.0;
			}
			return MetricLength(Factory.Type, Surface - WorldPosition);
		}
		case EPCGUtilsPainterDistanceShape::BoxBounds:
		{
			const FVector Local = Point.Transform.InverseTransformPosition(WorldPosition);
			const FVector Closest = Point.Transform.TransformPosition(Local.ComponentMax(Point.BoundsMin).ComponentMin(Point.BoundsMax));
			return MetricLength(Factory.Type, Closest - WorldPosition);
		}
		case EPCGUtilsPainterDistanceShape::Center:
		default:
			return MetricLength(Factory.Type, Point.Location - WorldPosition);
		}
	}

	double FDistanceField::ValueAt(double Distance) const
	{
		const double Span = Factory.MaxRadius - Factory.MinRadius;
		const double T = Span > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp((Distance - Factory.MinRadius) / Span, 0.0, 1.0) : 0.0;
		double Alpha = T;
		switch (Factory.Falloff)
		{
		case EPCGUtilsPainterDistanceFalloff::Smooth:
			Alpha = FMath::SmoothStep(0.0, 1.0, T);
			break;
		case EPCGUtilsPainterDistanceFalloff::Power:
			Alpha = FMath::Pow(T, static_cast<double>(FMath::Max(Factory.Exponent, UE_KINDA_SMALL_NUMBER)));
			break;
		case EPCGUtilsPainterDistanceFalloff::Exponential:
		{
			const double K = Factory.Steepness;
			Alpha = FMath::Abs(K) < 1e-4 ? T : (1.0 - FMath::Exp(-K * T)) / (1.0 - FMath::Exp(-K));
			break;
		}
		case EPCGUtilsPainterDistanceFalloff::Curve:
			Alpha = Factory.Curve.Eval(static_cast<float>(T));
			break;
		case EPCGUtilsPainterDistanceFalloff::Linear:
		default:
			break;
		}
		return FMath::Lerp(static_cast<double>(Factory.ValueAtMinRadius), static_cast<double>(Factory.ValueAtMaxRadius), Alpha);
	}

	void FDistanceField::Accumulate(int32 PointIndex, const FVector& WorldPosition, FAccumulator& InOut) const
	{
		const FPreparedPoint& Point = Points[PointIndex];
		if (!Point.Influence.IsInsideOrOn(WorldPosition))
		{
			return;
		}
		const double Distance = Measure(Point, WorldPosition);
		if (Distance > Factory.MaxRadius ||
			(Factory.InsideMinRadius == EPCGUtilsPainterDistanceInsideMin::OutOfRange && Distance < Factory.MinRadius))
		{
			return;
		}

		if (Factory.Blend == EPCGUtilsPainterDistanceBlend::Nearest)
		{
			// Ties go to the lower input index, so the result does not depend on grid order.
			if (Distance < InOut.NearestDistance || (Distance == InOut.NearestDistance && PointIndex < InOut.NearestIndex))
			{
				InOut.NearestDistance = Distance;
				InOut.NearestIndex = PointIndex;
			}
			++InOut.Count;
			return;
		}

		const double Value = ValueAt(Distance);
		if (InOut.Count == 0)
		{
			InOut.Value = Value;
		}
		else
		{
			switch (Factory.Blend)
			{
			case EPCGUtilsPainterDistanceBlend::Max: InOut.Value = FMath::Max(InOut.Value, Value); break;
			case EPCGUtilsPainterDistanceBlend::Min: InOut.Value = FMath::Min(InOut.Value, Value); break;
			case EPCGUtilsPainterDistanceBlend::Multiply: InOut.Value *= Value; break;
			case EPCGUtilsPainterDistanceBlend::Average:
			case EPCGUtilsPainterDistanceBlend::Add:
			default: InOut.Value += Value; break;
			}
		}
		++InOut.Count;
	}

	float FDistanceField::Finish(const FAccumulator& Accumulator) const
	{
		double Result = Factory.OutOfRangeValue;
		if (Accumulator.Count > 0)
		{
			if (Factory.Blend == EPCGUtilsPainterDistanceBlend::Nearest)
			{
				Result = ValueAt(Accumulator.NearestDistance);
			}
			else
			{
				Result = Factory.Blend == EPCGUtilsPainterDistanceBlend::Average
					? Accumulator.Value / Accumulator.Count : Accumulator.Value;
			}
		}
		return static_cast<float>(Factory.bClampValue ? FMath::Clamp(Result, 0.0, 1.0) : Result);
	}

	float FDistanceField::Evaluate(const FVector& WorldPosition) const
	{
		FAccumulator Accumulator;
		if (const int32* Bucket = CellToBucket.Find(CellOf(WorldPosition)))
		{
			for (int32 Slot = BucketStarts[*Bucket]; Slot < BucketStarts[*Bucket + 1]; ++Slot)
			{
				Accumulate(BucketPoints[Slot], WorldPosition, Accumulator);
			}
		}
		for (const int32 PointIndex : GlobalPoints)
		{
			Accumulate(PointIndex, WorldPosition, Accumulator);
		}
		return Finish(Accumulator);
	}

	float FDistanceField::EvaluateBruteForce(const FVector& WorldPosition) const
	{
		FAccumulator Accumulator;
		for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
		{
			Accumulate(PointIndex, WorldPosition, Accumulator);
		}
		return Finish(Accumulator);
	}

	double FDistanceField::DistanceTo(int32 PointIndex, const FVector& WorldPosition) const
	{
		return Measure(Points[PointIndex], WorldPosition);
	}

	class FDistancePainterOperation final : public FPCGUtilsDynMeshPainterOperation
	{
	public:
		explicit FDistancePainterOperation(const UPCGDynMeshDistancePainterFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			if (!FPCGUtilsDynMeshPainterOperation::Initialize(InPainterContext) || !Factory)
			{
				return false;
			}
			// Built here rather than in Prepare so an un-prepared Evaluate still works.
			Field = MakeUnique<FDistanceField>(*Factory);
			return true;
		}

		/** Evaluates every vertex once, in parallel, so Evaluate is a lookup. */
		virtual bool Prepare(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			const UE::Geometry::FDynamicMesh3& Mesh = *InPainterContext.Mesh;
			TArray<int32> VertexIDs;
			VertexIDs.Reserve(Mesh.VertexCount());
			for (const int32 VertexID : Mesh.VertexIndicesItr())
			{
				VertexIDs.Add(VertexID);
			}

			Values.SetNumZeroed(Mesh.MaxVertexID());
			const FTransform LocalToWorld = InPainterContext.LocalToWorld;
			ParallelFor(VertexIDs.Num(), [&](int32 Index)
			{
				const int32 VertexID = VertexIDs[Index];
				Values[VertexID] = Field->Evaluate(LocalToWorld.TransformPosition(FVector(Mesh.GetVertex(VertexID))));
			});
			PreparedMesh = &Mesh;
			return true;
		}

		virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
		{
			return EPCGUtilsDynMeshPainterValueType::Scalar;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(const FPCGUtilsDynMeshPainterSample& Sample) const override
		{
			if (PreparedMesh && Values.IsValidIndex(Sample.VertexID) && PreparedMesh->IsVertex(Sample.VertexID))
			{
				return FPCGUtilsDynMeshPainterValue::MakeScalar(Values[Sample.VertexID]);
			}
			// A sample that is not a prepared vertex is evaluated on the spot.
			return FPCGUtilsDynMeshPainterValue::MakeScalar(Field->Evaluate(Sample.WorldPosition));
		}

	private:
		TObjectPtr<const UPCGDynMeshDistancePainterFactoryData> Factory;
		TUniquePtr<FDistanceField> Field;
		TArray<float> Values;
		const UE::Geometry::FDynamicMesh3* PreparedMesh = nullptr;
	};

	void AddCurveToCrc(FArchiveCrc32& Ar, const FRichCurve& Curve)
	{
		int32 NumKeys = Curve.GetNumKeys();
		uint8 PreInfinity = Curve.PreInfinityExtrap;
		uint8 PostInfinity = Curve.PostInfinityExtrap;
		float DefaultValue = Curve.GetDefaultValue();
		Ar << NumKeys << PreInfinity << PostInfinity << DefaultValue;
		for (const FRichCurveKey& Key : Curve.GetConstRefOfKeys())
		{
			float Time = Key.Time, Value = Key.Value, Arrive = Key.ArriveTangent, Leave = Key.LeaveTangent;
			uint8 Interp = Key.InterpMode, Tangent = Key.TangentMode;
			Ar << Time << Value << Arrive << Leave << Interp << Tangent;
		}
	}
}

TSharedPtr<FPCGUtilsDynMeshPainterOperation> UPCGDynMeshDistancePainterFactoryData::CreateOperationInternal() const
{
	return MakeShared<PCGUtilsPainterDistance::FDistancePainterOperation>(this);
}

void UPCGDynMeshDistancePainterFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	uint8 TargetValue = static_cast<uint8>(Target);
	uint8 TypeValue = static_cast<uint8>(Type);
	uint8 InsideValue = static_cast<uint8>(InsideMinRadius);
	uint8 FalloffValue = static_cast<uint8>(Falloff);
	uint8 BlendValue = static_cast<uint8>(Blend);
	bool bOverlapValue = bOverlapIsZero;
	bool bClampValueCrc = bClampValue;
	double MinRadiusValue = MinRadius, MaxRadiusValue = MaxRadius;
	float ExponentValue = Exponent, SteepnessValue = Steepness;
	float StartValue = ValueAtMinRadius, EndValue = ValueAtMaxRadius, OutValue = OutOfRangeValue;
	Ar << TargetValue << TypeValue << InsideValue << FalloffValue << BlendValue << bOverlapValue << bClampValueCrc;
	Ar << MinRadiusValue << MaxRadiusValue << ExponentValue << SteepnessValue << StartValue << EndValue << OutValue;
	if (Falloff == EPCGUtilsPainterDistanceFalloff::Curve)
	{
		PCGUtilsPainterDistance::AddCurveToCrc(Ar, Curve);
	}

	int32 NumPoints = Points.Num();
	Ar << NumPoints;
	for (const FPCGUtilsPainterDistancePoint& Point : Points)
	{
		FTransform Transform = Point.Transform;
		FVector BoundsMin = Point.BoundsMin, BoundsMax = Point.BoundsMax;
		Ar << Transform << BoundsMin << BoundsMax;
	}
}

UPCGDynMeshDistancePainterProviderSettings::UPCGDynMeshDistancePainterProviderSettings()
{
	FRichCurve* Curve = FalloffCurve.GetRichCurve();
	Curve->AddKey(0.0f, 0.0f);
	Curve->AddKey(1.0f, 1.0f);
}

#if WITH_EDITOR
FText UPCGDynMeshDistancePainterProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Painter | Distance");
}

FText UPCGDynMeshDistancePainterProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Paints each vertex by its distance to the PCG points in range (world space). Distance is measured to each point's center, bounds sphere or bounds box. Within range the value blends from Value At Min Radius to Value At Max Radius with the chosen falloff; vertices with no point in range get Out Of Range Value. Several points in range combine by Nearest, Max, Min, Average, Add or Multiply.");
}

FString UPCGDynMeshDistancePainterProviderSettings::GetAdditionalTitleInformation() const
{
	const UEnum* BlendEnum = StaticEnum<EPCGUtilsPainterDistanceBlend>();
	return FString::Printf(TEXT("%s, %s to %s"),
		*BlendEnum->GetDisplayNameTextByValue(static_cast<int64>(Blend)).ToString(),
		*FString::SanitizeFloat(MinRadius, 0), *FString::SanitizeFloat(MaxRadius, 0));
}
#endif

FName UPCGDynMeshDistancePainterProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshPainterConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshDistancePainterProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshDistancePainterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGUtilsPainterDistance::PointsPin, EPCGDataType::Point, true, true).SetRequiredPin();
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshDistancePainterProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	if (MinRadius < 0.0)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("NegativeMinRadius", "Painter | Distance: Min Radius is {0}; it must be 0 or more."),
			FText::AsNumber(MinRadius)), InContext);
		return nullptr;
	}
	if (MaxRadius < 0.0)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("NegativeMaxRadius", "Painter | Distance: Max Radius is {0}; it must be 0 or more."),
			FText::AsNumber(MaxRadius)), InContext);
		return nullptr;
	}
	if (MinRadius > MaxRadius)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("MinAboveMax", "Painter | Distance: Min Radius ({0}) is larger than Max Radius ({1}); it must be at most Max Radius."),
			FText::AsNumber(MinRadius), FText::AsNumber(MaxRadius)), InContext);
		return nullptr;
	}
	if (Falloff == EPCGUtilsPainterDistanceFalloff::Power && Exponent <= 0.0f)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("BadExponent", "Painter | Distance: Exponent is {0}; Power falloff needs an Exponent above 0."),
			FText::AsNumber(Exponent)), InContext);
		return nullptr;
	}
	const FRichCurve* SourceCurve = FalloffCurve.GetRichCurveConst();
	if (Falloff == EPCGUtilsPainterDistanceFalloff::Curve && (!SourceCurve || SourceCurve->GetNumKeys() == 0))
	{
		PCGLog::LogErrorOnGraph(FalloffCurve.ExternalCurve
			? FText::Format(LOCTEXT("EmptyExternalCurve", "Painter | Distance: Falloff is Curve, but the external curve '{0}' has no keys."),
				FText::FromString(FalloffCurve.ExternalCurve->GetPathName()))
			: LOCTEXT("EmptyCurve", "Painter | Distance: Falloff is Curve, but Falloff Curve has no keys."), InContext);
		return nullptr;
	}

	TArray<FPCGUtilsPainterDistancePoint> Points;
	bool bSawPointData = false;
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(PCGUtilsPainterDistance::PointsPin))
	{
		const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Input.Data);
		if (!PointData)
		{
			continue;
		}
		bSawPointData = true;

		const int32 NumPoints = PointData->GetNumPoints();
		const TConstPCGValueRange<FTransform> Transforms = PointData->GetConstTransformValueRange();
		const TConstPCGValueRange<FVector> BoundsMins = PointData->GetConstBoundsMinValueRange();
		const TConstPCGValueRange<FVector> BoundsMaxs = PointData->GetConstBoundsMaxValueRange();
		const int32 FirstNew = Points.Num();
		Points.SetNum(FirstNew + NumPoints);
		for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
		{
			FPCGUtilsPainterDistancePoint& Point = Points[FirstNew + PointIndex];
			Point.Transform = Transforms[PointIndex];
			Point.BoundsMin = BoundsMins[PointIndex];
			Point.BoundsMax = BoundsMaxs[PointIndex];
		}
	}

	if (!bSawPointData)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("MissingPoints", "Painter | Distance: the Points pin has no point data."), InContext);
		return nullptr;
	}

	UPCGDynMeshDistancePainterFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshDistancePainterFactoryData>(InFactory)
		: PCGUtilsAsyncObjects::NewObject<UPCGDynMeshDistancePainterFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->Points = MoveTemp(Points);
	Factory->Target = Target;
	Factory->Type = Type;
	Factory->bOverlapIsZero = bOverlapIsZero;
	Factory->MinRadius = MinRadius;
	Factory->MaxRadius = MaxRadius;
	Factory->InsideMinRadius = InsideMinRadius;
	Factory->Falloff = Falloff;
	Factory->Exponent = Exponent;
	Factory->Steepness = Steepness;
	Factory->Curve = SourceCurve ? *SourceCurve : FRichCurve();
	Factory->ValueAtMinRadius = ValueAtMinRadius;
	Factory->ValueAtMaxRadius = ValueAtMaxRadius;
	Factory->OutOfRangeValue = OutOfRangeValue;
	Factory->Blend = Blend;
	Factory->bClampValue = bClampValue;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
