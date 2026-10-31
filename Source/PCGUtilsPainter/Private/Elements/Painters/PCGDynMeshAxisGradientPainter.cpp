// Copyright Max Harris

#include "Elements/Painters/PCGDynMeshAxisGradientPainter.h"
#include "Helpers/PCGUtilsParallelInputs.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshAxisGradientPainter"

namespace PCGUtilsAxisGradientBounds
{
	namespace
	{
		/** Distances below this (scaled to the box) count as touching. */
		double AxisGradientTolerance(const FBox& Box)
		{
			return 1e-6 * FMath::Max(1.0, Box.GetSize().GetMax());
		}

		/** Parameter interval [OutNear, OutFar] where Origin + t * Axis is inside Box (grown by Tolerance). */
		bool AxisGradientLineInterval(const FBox& Box, const FVector& Origin, const FVector& Axis, double Tolerance,
			double& OutNear, double& OutFar)
		{
			OutNear = -UE_DOUBLE_BIG_NUMBER;
			OutFar = UE_DOUBLE_BIG_NUMBER;
			// Tolerance only decides whether the line touches; the interval itself is measured on the true box.
			for (int32 I = 0; I < 3; ++I)
			{
				if (FMath::Abs(Axis[I]) < UE_DOUBLE_SMALL_NUMBER)
				{
					if (Origin[I] < Box.Min[I] - Tolerance || Origin[I] > Box.Max[I] + Tolerance)
					{
						return false;
					}
					continue;
				}
				double T0 = (Box.Min[I] - Origin[I]) / Axis[I];
				double T1 = (Box.Max[I] - Origin[I]) / Axis[I];
				if (T0 > T1)
				{
					Swap(T0, T1);
				}
				OutNear = FMath::Max(OutNear, T0);
				OutFar = FMath::Min(OutFar, T1);
			}
			if (OutNear > OutFar)
			{
				// A grazing line (corner or edge contact) can miss by rounding; within tolerance it touches at one point.
				if (OutNear - OutFar > Tolerance)
				{
					return false;
				}
				OutNear = OutFar = 0.5 * (OutNear + OutFar);
			}
			return true;
		}

		/**
		 * The point of Box nearest to the line Origin + t * Axis (squared distance measured perpendicular to Axis).
		 * Exact: the squared distance is a convex quadratic over the box, so its minimum has each coordinate either on
		 * a face (min or max) or free. Each of the 27 face/free combinations is solved and the best feasible one kept.
		 * All-free is skipped: it is only a minimum when the line passes through the box, which the caller rules out.
		 */
		FVector AxisGradientNearestBoxPointToLine(const FBox& Box, const FVector& Origin, const FVector& Axis)
		{
			// Squared perpendicular distance is |M (P - O)|^2 with M = I - A A^T (symmetric, M^2 = M).
			auto Projector = [&Axis](int32 R, int32 C) { return (R == C ? 1.0 : 0.0) - Axis[R] * Axis[C]; };
			auto PerpendicularSquared = [&](const FVector& P)
			{
				const FVector D = P - Origin;
				return (D - Axis * FVector::DotProduct(D, Axis)).SizeSquared();
			};
			const double Tolerance = AxisGradientTolerance(Box);

			FVector Best = Box.GetCenter();
			double BestDistance = UE_DOUBLE_BIG_NUMBER;
			for (int32 Combo = 0; Combo < 27; ++Combo)
			{
				// State per coordinate: 0 = at min, 1 = at max, 2 = free.
				const int32 State[3] = {Combo % 3, (Combo / 3) % 3, Combo / 9};
				int32 Free[3];
				int32 FreeCount = 0;
				FVector P;
				for (int32 I = 0; I < 3; ++I)
				{
					if (State[I] == 2)
					{
						Free[FreeCount++] = I;
						P[I] = 0.0;
					}
					else
					{
						P[I] = State[I] == 0 ? Box.Min[I] : Box.Max[I];
					}
				}
				if (FreeCount == 3)
				{
					continue;
				}

				// Stationary in the free coordinates: (M (P - O))_f = 0 for each free f, i.e.
				// sum over free g of M_fg P_g = (M O)_f - sum over fixed n of M_fn P_n.
				double Rhs[2] = {0.0, 0.0};
				for (int32 K = 0; K < FreeCount; ++K)
				{
					const int32 F = Free[K];
					double Value = 0.0;
					for (int32 I = 0; I < 3; ++I)
					{
						Value += Projector(F, I) * Origin[I];
						if (State[I] != 2)
						{
							Value -= Projector(F, I) * P[I];
						}
					}
					Rhs[K] = Value;
				}
				if (FreeCount == 1)
				{
					const double A = Projector(Free[0], Free[0]);
					if (A < UE_DOUBLE_SMALL_NUMBER)
					{
						continue; // Axis runs along this coordinate: the edge is parallel, its ends are other combinations.
					}
					P[Free[0]] = Rhs[0] / A;
				}
				else if (FreeCount == 2)
				{
					const double A = Projector(Free[0], Free[0]), B = Projector(Free[0], Free[1]), D = Projector(Free[1], Free[1]);
					const double Det = A * D - B * B;
					if (FMath::Abs(Det) < UE_DOUBLE_SMALL_NUMBER)
					{
						continue; // Axis lies in this face's plane: the minimum set reaches the face's edges, covered elsewhere.
					}
					P[Free[0]] = (Rhs[0] * D - B * Rhs[1]) / Det;
					P[Free[1]] = (A * Rhs[1] - B * Rhs[0]) / Det;
				}

				bool bInside = true;
				for (int32 K = 0; K < FreeCount; ++K)
				{
					const int32 F = Free[K];
					bInside &= P[F] >= Box.Min[F] - Tolerance && P[F] <= Box.Max[F] + Tolerance;
				}
				if (!bInside)
				{
					continue;
				}
				const double Distance = PerpendicularSquared(P);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = P;
				}
			}
			return Best;
		}
	}

	double Measure(const FBox& Box, const FVector& Origin, const FVector& Axis, ESpanCase* OutCase, FVector* OutLineOrigin)
	{
		if (!Box.IsValid)
		{
			if (OutLineOrigin) { *OutLineOrigin = Origin; }
			if (OutCase) { *OutCase = ESpanCase::Behind; }
			return 0.0;
		}

		const double Tolerance = AxisGradientTolerance(Box);
		FVector LineOrigin = Origin;
		ESpanCase Case = ESpanCase::Direct;
		double Near = 0.0, Far = 0.0;
		if (!AxisGradientLineInterval(Box, LineOrigin, Axis, Tolerance, Near, Far))
		{
			// Slide the origin sideways (perpendicular to Axis) onto the nearest parallel line that touches the box.
			// Sliding perpendicular keeps the origin's position along Axis, so distances still start from it.
			const FVector Nearest = AxisGradientNearestBoxPointToLine(Box, Origin, Axis);
			const FVector Offset = Nearest - Origin;
			LineOrigin = Origin + (Offset - Axis * FVector::DotProduct(Offset, Axis));
			Case = ESpanCase::Shifted;
			if (!AxisGradientLineInterval(Box, LineOrigin, Axis, 4.0 * Tolerance, Near, Far))
			{
				// Only reachable through rounding: the touching line found above runs through Nearest.
				Near = Far = FVector::DotProduct(Offset, Axis);
			}
		}

		if (OutLineOrigin) { *OutLineOrigin = LineOrigin; }
		if (Far <= 0.0)
		{
			if (OutCase) { *OutCase = ESpanCase::Behind; }
			return FMath::Max(0.0, Far - Near);
		}
		if (OutCase) { *OutCase = Case; }
		return Far;
	}
}

namespace
{
	class FAxisGradientPainterOperation final : public FPCGUtilsDynMeshPainterOperation
	{
	public:
		explicit FAxisGradientPainterOperation(const UPCGDynMeshAxisGradientPainterFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			if (!FPCGUtilsDynMeshPainterOperation::Initialize(InPainterContext) || !Factory ||
				Factory->Axis.IsNearlyZero() ||
				FMath::IsNearlyEqual(Factory->StartDistance, Factory->EndDistance))
			{
				return false;
			}
			// The absolute gradient; Prepare replaces the origin and range when they come from the mesh's bounds.
			bWorld = Factory->CoordinateSpace == EPCGUtilsDynMeshPainterCoordinateSpace::World;
			Axis = Factory->Axis.GetSafeNormal();
			Origin = Factory->Origin;
			Start = Factory->StartDistance;
			End = Factory->EndDistance;
			return true;
		}

		virtual bool Prepare(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			const bool bNeedsBounds = Factory->OriginMode == EPCGUtilsAxisGradientOriginMode::BoundsRelative
				|| Factory->bDistancesRelativeToBounds;
			if (!bNeedsBounds)
			{
				return true;
			}

			const FBox Bounds = MeshBounds(InPainterContext);
			if (!Bounds.IsValid)
			{
				return true; // No vertices: nothing will be evaluated.
			}
			if (Factory->OriginMode == EPCGUtilsAxisGradientOriginMode::BoundsRelative)
			{
				// The aligned point of the box, then Origin as an offset from it.
				FVector Aligned = FVector::ZeroVector;
				Factory->OriginAlignment.Process(Bounds, FBox(FVector::ZeroVector, FVector::ZeroVector), Aligned);
				Origin = Aligned + Factory->Origin;
			}
			if (Factory->bDistancesRelativeToBounds)
			{
				// A flat mesh along the axis has no span; the floor turns the gradient into a step instead of a NaN.
				const double Span = FMath::Max(PCGUtilsAxisGradientBounds::Measure(Bounds, Origin, Axis), UE_DOUBLE_KINDA_SMALL_NUMBER);
				Start = Factory->StartDistance * Span;
				End = Factory->EndDistance * Span;
			}
			return true;
		}

		virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
		{
			return EPCGUtilsDynMeshPainterValueType::Scalar;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(
			const FPCGUtilsDynMeshPainterSample& Sample) const override
		{
			const FVector& Position = bWorld ? Sample.WorldPosition : Sample.LocalPosition;
			const double Projection = FVector::DotProduct(Position - Origin, Axis);
			float Value = FMath::Clamp(static_cast<float>((Projection - Start) / (End - Start)), 0.0f, 1.0f);
			Value = (Factory->bInvert ? 1.0f - Value : Value);
			if (Factory->Curve)
			{
				Value = Factory->Curve->GetFloatValue(Value);
			}
			Value = FMath::Lerp(Factory->GradientStart, Factory->GradientEnd, Value);
			return FPCGUtilsDynMeshPainterValue::MakeScalar(Value);
		}

	private:
		/** The whole mesh's axis-aligned box in the gradient's coordinate space (exact in world space, not a transformed local box). */
		FBox MeshBounds(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) const
		{
			FBox Bounds(ForceInit);
			const UE::Geometry::FDynamicMesh3* Mesh = InPainterContext.Mesh;
			if (!Mesh)
			{
				return Bounds;
			}
			for (const int32 VertexID : Mesh->VertexIndicesItr())
			{
				const FVector Local = Mesh->GetVertex(VertexID);
				Bounds += bWorld ? InPainterContext.LocalToWorld.TransformPosition(Local) : Local;
			}
			return Bounds;
		}

		TObjectPtr<const UPCGDynMeshAxisGradientPainterFactoryData> Factory;
		bool bWorld = false;
		FVector Origin = FVector::ZeroVector;
		FVector Axis = FVector::UpVector;
		double Start = 0.0;
		double End = 1.0;
	};
}

TSharedPtr<FPCGUtilsDynMeshPainterOperation>
UPCGDynMeshAxisGradientPainterFactoryData::CreateOperationInternal() const
{
	return MakeShared<FAxisGradientPainterOperation>(this);
}

void UPCGDynMeshAxisGradientPainterFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	FVector OriginValue = Origin;
	FVector AxisValue = Axis;
	float StartValue = StartDistance;
	float EndValue = EndDistance;
	float GradientStartValue = GradientStart;
	float GradientEndValue = GradientEnd;
	bool bInvertValue = bInvert;
	bool bRelativeValue = bDistancesRelativeToBounds;
	uint8 SpaceValue = static_cast<uint8>(CoordinateSpace);
	uint8 OriginModeValue = static_cast<uint8>(OriginMode);
	FString CurvePath = Curve ? Curve->GetPathName() : FString();
	Ar << OriginValue << AxisValue << StartValue << EndValue << GradientStartValue << GradientEndValue;
	Ar << bInvertValue << bRelativeValue << SpaceValue << OriginModeValue << CurvePath;

	if (OriginMode == EPCGUtilsAxisGradientOriginMode::BoundsRelative)
	{
		// Reuse the shared alignment CRC; padding and local transform stay at their zero defaults here.
		FPCGUtilsBoundsRelativeTransformDetails Placement;
		Placement.Alignment = OriginAlignment;
		Placement.AddToCrc(Ar);
	}
}

UPCGDynMeshAxisGradientPainterProviderSettings::UPCGDynMeshAxisGradientPainterProviderSettings()
{
	// Default to the common "ground up" case: centered in X and Y, on the bottom in Z.
	OriginAlignment.JustifyX.To = EPCGUtilsJustifyTo::Center;
	OriginAlignment.JustifyY.To = EPCGUtilsJustifyTo::Center;
	OriginAlignment.JustifyZ.To = EPCGUtilsJustifyTo::Min;
}

#if WITH_EDITOR
FText UPCGDynMeshAxisGradientPainterProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Painter | Axis Gradient");
}

FText UPCGDynMeshAxisGradientPainterProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Creates a reusable linear scalar gradient projected along an axis in DynMesh-local or world space. The origin can "
		"sit on the mesh's bounding box (Bounds Relative), and Start/End can be fractions of the distance the axis runs "
		"through the box from the origin, so a bottom-to-top gradient needs no Bounds From Mesh or math nodes.");
}
#endif

FName UPCGDynMeshAxisGradientPainterProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshPainterConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshAxisGradientPainterProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId();
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshAxisGradientPainterProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	if (Axis.IsNearlyZero())
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("ZeroAxis", "Axis Gradient Painter: Axis is {0}; expected a non-zero direction."),
			FText::FromString(Axis.ToString())), InContext);
		return nullptr;
	}
	if (FMath::IsNearlyEqual(StartDistance, EndDistance))
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("InvalidRange", "Axis Gradient Painter: Start Distance and End Distance are both {0}; expected different values."),
			FText::AsNumber(StartDistance)), InContext);
		return nullptr;
	}

	UPCGDynMeshAxisGradientPainterFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshAxisGradientPainterFactoryData>(InFactory)
		: PCGUtilsAsyncObjects::NewObject<UPCGDynMeshAxisGradientPainterFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->OriginMode = OriginMode;
	Factory->OriginAlignment = OriginAlignment;
	Factory->Origin = Origin;
	Factory->Axis = Axis.GetSafeNormal();
	Factory->bDistancesRelativeToBounds = bDistancesRelativeToBounds;
	Factory->StartDistance = StartDistance;
	Factory->EndDistance = EndDistance;
	Factory->bInvert = bInvert;
	Factory->CoordinateSpace = CoordinateSpace;
	Factory->GradientStart = GradientStart;
	Factory->GradientEnd = GradientEnd;
	Factory->Curve = Curve.Get();
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
