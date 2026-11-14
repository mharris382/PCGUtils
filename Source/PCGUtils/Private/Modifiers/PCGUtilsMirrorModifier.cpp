// Copyright Max Harris

#include "Modifiers/PCGUtilsMirrorModifier.h"

#include "Curve/GeneralPolygon2.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGSplineData.h"
#include "Data/PCGSplineStruct.h"
#include "Modifiers/PCGUtilsModifierStack.h"

#define LOCTEXT_NAMESPACE "PCGUtilsMirrorModifier"

namespace PCGUtilsMirrorModifier
{
	FText AxisName(int32 Axis)
	{
		return Axis == 0 ? INVTEXT("X") : (Axis == 1 ? INVTEXT("Y") : INVTEXT("Z"));
	}

	/** Side value meaning "this result mixes sides": no side attribute is written. */
	constexpr int32 MixedSide = -1;

	FSplinePoint Reversed(const FSplinePoint& Point)
	{
		FSplinePoint Result = Point;
		Result.ArriveTangent = -Point.LeaveTangent;
		Result.LeaveTangent = -Point.ArriveTangent;
		return Result;
	}
}

TArray<int32> UPCGUtilsMirrorModifier::GetActiveAxes() const
{
	TArray<int32> Axes;
	if (bMirrorX) Axes.Add(0);
	if (bMirrorY) Axes.Add(1);
	if (bMirrorZ) Axes.Add(2);
	return Axes;
}

FVector UPCGUtilsMirrorModifier::PlaneNormal(int32 Axis) const
{
	const EAxis::Type EngineAxis = Axis == 0 ? EAxis::X : (Axis == 1 ? EAxis::Y : EAxis::Z);
	return MirrorPlane.ToMatrixNoScale().GetUnitAxis(EngineAxis);
}

double UPCGUtilsMirrorModifier::PlaneDistance(int32 Axis, const FVector& Point) const
{
	return FVector::DotProduct(Point - MirrorPlane.GetLocation(), PlaneNormal(Axis));
}

FMatrix UPCGUtilsMirrorModifier::GetReflection(int32 Axis) const
{
	// Into the plane's frame (no scale: a scaled plane is still a plane), flip the axis, back out.
	const FMatrix PlaneToModifier = MirrorPlane.ToMatrixNoScale();
	FVector Flip = FVector::OneVector;
	Flip[Axis] = -1.0;
	return PlaneToModifier.Inverse() * FScaleMatrix(Flip) * PlaneToModifier;
}

FPCGUtilsModifierPath UPCGUtilsMirrorModifier::ReflectReversed(const FPCGUtilsModifierPath& Path, const FMatrix& Reflection)
{
	FPCGUtilsModifierPath Result;
	Result.bClosed = Path.bClosed;
	for (int32 Index = Path.Num() - 1; Index >= 0; --Index)
	{
		Result.Add(PCGUtilsMirrorModifier::Reversed(FPCGUtilsModifierHelpers::TransformSplinePoint(Path.Points[Index], Reflection)), Path.SourceIndices[Index]);
	}
	return Result;
}

//////////////////////////////////////////////////////////////////////////
// Markers

void UPCGUtilsMirrorModifier::ModifyPoints(const FPCGUtilsModifierContext& Context, const UPCGBasePointData* In, TArray<UPCGBasePointData*>& Out) const
{
	const int32 NumPoints = In ? In->GetNumPoints() : 0;
	const TArray<int32> Axes = GetActiveAxes();
	if (NumPoints == 0 || Axes.IsEmpty())
	{
		Out.Add(const_cast<UPCGBasePointData*>(In));
		return;
	}

	const FMatrix WorldToModifier = Context.ModifierToWorld.ToInverseMatrixWithScale();
	const FMatrix ModifierToWorld = Context.ModifierToWorld.ToMatrixWithScale();

	struct FPlaced
	{
		int32 Source;
		int32 Side;
		FMatrix LocalToModifier;
	};
	TArray<FPlaced> Placed;
	for (int32 Index = 0; Index < NumPoints; ++Index)
	{
		Placed.Add({Index, 0, In->GetTransform(Index).ToMatrixWithScale() * WorldToModifier});
	}

	// Blender order: X, then Y, then Z, each reflecting everything made so far.
	for (const int32 Axis : Axes)
	{
		const FMatrix Reflection = GetReflection(Axis);
		const int32 Count = Placed.Num();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FPlaced Original = Placed[Index];
			if (bMerge && FMath::Abs(PlaneDistance(Axis, Original.LocalToModifier.GetOrigin())) <= MergeDistance)
			{
				continue;
			}
			Placed.Add({Original.Source, Original.Side | (1 << Axis), Original.LocalToModifier * Reflection});
		}
	}

	TArray<int32> SourceIndices;
	TArray<int32> Sides;
	for (const FPlaced& Item : Placed)
	{
		SourceIndices.Add(Item.Source);
		Sides.Add(Item.Side);
	}

	UPCGBasePointData* Result = FPCGUtilsModifierHelpers::NewPointsFrom(Context, In, SourceIndices);
	TPCGValueRange<FTransform> Transforms = Result->GetTransformValueRange();
	for (int32 Index = 0; Index < Placed.Num(); ++Index)
	{
		// A reflected matrix becomes a transform with one negative scale axis, so handed things stay handed.
		Transforms[Index] = FTransform(Placed[Index].LocalToModifier * ModifierToWorld);
	}
	if (bWriteMirrorSide)
	{
		FPCGUtilsModifierHelpers::WritePointInt(Result, MirrorSideAttributeName, Sides);
	}
	Out.Add(Result);
}

//////////////////////////////////////////////////////////////////////////
// Paths

void UPCGUtilsMirrorModifier::ModifyPath(const FPCGUtilsModifierContext& Context, const UPCGSplineData* In, TArray<UPCGSplineData*>& Out) const
{
	const TArray<int32> Axes = GetActiveAxes();
	FPCGUtilsModifierPath Source = FPCGUtilsModifierHelpers::ReadPath(Context, In);
	if (Source.Num() == 0 || Axes.IsEmpty())
	{
		Out.Add(const_cast<UPCGSplineData*>(In));
		return;
	}

	TArray<TPair<FPCGUtilsModifierPath, int32>> Current;
	Current.Emplace(MoveTemp(Source), 0);
	for (const int32 Axis : Axes)
	{
		TArray<TPair<FPCGUtilsModifierPath, int32>> Next;
		for (const TPair<FPCGUtilsModifierPath, int32>& Item : Current)
		{
			MirrorPathOnce(Context, Axis, Item.Key, Item.Value, Next);
		}
		Current = MoveTemp(Next);
	}

	for (const TPair<FPCGUtilsModifierPath, int32>& Item : Current)
	{
		UPCGSplineData* Result = FPCGUtilsModifierHelpers::WritePath(Context, In, Item.Key);
		if (bWriteMirrorSide && Item.Value != PCGUtilsMirrorModifier::MixedSide)
		{
			FPCGUtilsModifierHelpers::WriteDataInt(Result, MirrorSideAttributeName, Item.Value);
		}
		Out.Add(Result);
	}
}

void UPCGUtilsMirrorModifier::MirrorPathOnce(const FPCGUtilsModifierContext& Context, int32 Axis, const FPCGUtilsModifierPath& Path, int32 Side,
	TArray<TPair<FPCGUtilsModifierPath, int32>>& Out) const
{
	if (bFuse && Path.Num() >= 2)
	{
		const bool bFused = Path.bClosed ? UnionClosed(Context, Axis, Path, Out) : FuseOpen(Context, Axis, Path, Out);
		if (bFused)
		{
			return;
		}
	}
	const int32 MirroredSide = Side == PCGUtilsMirrorModifier::MixedSide ? Side : (Side | (1 << Axis));
	Out.Emplace(Path, Side);
	Out.Emplace(ReflectReversed(Path, GetReflection(Axis)), MirroredSide);
}

bool UPCGUtilsMirrorModifier::FuseOpen(const FPCGUtilsModifierContext& Context, int32 Axis, const FPCGUtilsModifierPath& Path,
	TArray<TPair<FPCGUtilsModifierPath, int32>>& Out) const
{
	using namespace PCGUtilsMirrorModifier;

	const int32 Last = Path.Num() - 1;
	const double StartDistance = FMath::Abs(PlaneDistance(Axis, Path.Points[0].Position));
	const double EndDistance = FMath::Abs(PlaneDistance(Axis, Path.Points[Last].Position));
	const bool bJoinStart = StartDistance <= MergeDistance;
	const bool bJoinEnd = EndDistance <= MergeDistance;

	const FMatrix ModifierToWorld = Context.ModifierToWorld.ToMatrixWithScale();
	auto WarnEnd = [&](int32 Index, double Distance)
	{
		Context.Warn(this, FText::Format(
			LOCTEXT("EndOffPlane", "Mirror: path end {0} is {1} cm from the {2} mirror plane, more than Merge Distance ({3} cm), so it was not joined to its mirror."),
			FText::AsNumber(Index), FText::AsNumber(Distance), AxisName(Axis), FText::AsNumber(MergeDistance)),
			FVector(ModifierToWorld.TransformPosition(Path.Points[Index].Position)));
	};
	if (!bJoinStart) WarnEnd(0, StartDistance);
	if (!bJoinEnd) WarnEnd(Last, EndDistance);
	if (!bJoinStart && !bJoinEnd)
	{
		return false;
	}

	const FMatrix Reflection = GetReflection(Axis);
	const FPCGUtilsModifierPath Mirrored = ReflectReversed(Path, Reflection); // Mirrored[k] is the twin of Path[Last - k].
	const FVector Normal = PlaneNormal(Axis);

	// The welded point sits on the plane, halfway between an end and its twin. It arrives along the original's
	// tangent and leaves along the twin's, exactly as drawn.
	auto Weld = [&](const FSplinePoint& Arrive, const FSplinePoint& Leave, const FSplinePoint& Base)
	{
		FSplinePoint Result = Base;
		Result.Position = Base.Position - Normal * PlaneDistance(Axis, Base.Position);
		Result.ArriveTangent = Arrive.ArriveTangent;
		Result.LeaveTangent = Leave.LeaveTangent;
		return Result;
	};

	FPCGUtilsModifierPath Result;
	if (bJoinStart && bJoinEnd)
	{
		// P0 (welded) .. P(n-1), weld(Pn), M1 .. M(n-1); closed, so the last mirrored point leads back to P0.
		Result.bClosed = true;
		Result.Add(Weld(Mirrored.Points[Last], Path.Points[0], Path.Points[0]), Path.SourceIndices[0]);
		for (int32 Index = 1; Index < Last; ++Index) Result.Add(Path.Points[Index], Path.SourceIndices[Index]);
		Result.Add(Weld(Path.Points[Last], Mirrored.Points[0], Path.Points[Last]), Path.SourceIndices[Last]);
		for (int32 Index = 1; Index < Last; ++Index) Result.Add(Mirrored.Points[Index], Mirrored.SourceIndices[Index]);
	}
	else if (bJoinEnd)
	{
		// P0 .. P(n-1), weld(Pn), M1 .. Mn.
		for (int32 Index = 0; Index < Last; ++Index) Result.Add(Path.Points[Index], Path.SourceIndices[Index]);
		Result.Add(Weld(Path.Points[Last], Mirrored.Points[0], Path.Points[Last]), Path.SourceIndices[Last]);
		for (int32 Index = 1; Index <= Last; ++Index) Result.Add(Mirrored.Points[Index], Mirrored.SourceIndices[Index]);
	}
	else
	{
		// M0 .. M(n-1), weld(P0), P1 .. Pn.
		for (int32 Index = 0; Index < Last; ++Index) Result.Add(Mirrored.Points[Index], Mirrored.SourceIndices[Index]);
		Result.Add(Weld(Mirrored.Points[Last], Path.Points[0], Path.Points[0]), Path.SourceIndices[0]);
		for (int32 Index = 1; Index <= Last; ++Index) Result.Add(Path.Points[Index], Path.SourceIndices[Index]);
	}
	Out.Emplace(MoveTemp(Result), MixedSide);
	return true;
}

bool UPCGUtilsMirrorModifier::UnionClosed(const FPCGUtilsModifierContext& Context, int32 Axis, const FPCGUtilsModifierPath& Path,
	TArray<TPair<FPCGUtilsModifierPath, int32>>& Out) const
{
	using namespace UE::Geometry;
	using namespace PCGUtilsMirrorModifier;

	// Trace both shapes as polylines in component space.
	auto Trace = [this](const FPCGUtilsModifierPath& Shape)
	{
		FPCGSplineStruct Spline;
		TArray<FSplinePoint> Points = Shape.Points;
		for (int32 Index = 0; Index < Points.Num(); ++Index) Points[Index].InputKey = static_cast<float>(Index);
		Spline.Initialize(Points, /*bIsClosedLoop=*/true, FTransform::Identity);
		TArray<FVector> Polyline;
		Spline.ConvertSplineToPolyLine(ESplineCoordinateSpace::Local, static_cast<float>(FMath::Square(UnionTolerance)), Polyline);
		if (Polyline.Num() > 1 && Polyline.Last().Equals(Polyline[0], UE_KINDA_SMALL_NUMBER))
		{
			Polyline.Pop();
		}
		return Polyline;
	};
	const TArray<FVector> Original = Trace(Path);
	const TArray<FVector> Mirrored = Trace(ReflectReversed(Path, GetReflection(Axis)));
	if (Original.Num() < 3 || Mirrored.Num() < 3)
	{
		return false;
	}

	// The union happens in the path's own plane, which must contain the mirror normal.
	FVector PathNormal = FVector::ZeroVector;
	for (int32 Index = 0; Index < Original.Num(); ++Index)
	{
		PathNormal += FVector::CrossProduct(Original[Index], Original[(Index + 1) % Original.Num()]);
	}
	const FVector U = PlaneNormal(Axis);
	if (!PathNormal.Normalize() || FMath::Abs(FVector::DotProduct(PathNormal, U)) > 0.5)
	{
		Context.Warn(this, FText::Format(
			LOCTEXT("UnionPlane", "Mirror: Fuse on a closed path needs the {0} mirror plane to cut across the path's plane; it lies along it, so the two shapes were kept as separate paths."),
			AxisName(Axis)));
		return false;
	}
	const FVector V = FVector::CrossProduct(PathNormal, U).GetSafeNormal();
	const FVector W = FVector::CrossProduct(U, V);

	auto ToPolygon = [&](const TArray<FVector>& Polyline)
	{
		FPolygon2d Polygon;
		for (const FVector& Point : Polyline)
		{
			Polygon.AppendVertex(FVector2d(FVector::DotProduct(Point, U), FVector::DotProduct(Point, V)));
		}
		if (Polygon.IsClockwise())
		{
			Polygon.Reverse();
		}
		return FGeneralPolygon2d(Polygon);
	};
	const TArray<FGeneralPolygon2d> Inputs = {ToPolygon(Original), ToPolygon(Mirrored)};
	TArray<FGeneralPolygon2d> Union;
	if (!PolygonsUnion(Inputs, Union, /*bCopyInputOnFailure=*/false) || Union.IsEmpty())
	{
		Context.Warn(this, LOCTEXT("UnionFailed", "Mirror: the union of the closed path and its mirror failed; they were kept as separate paths."));
		return false;
	}

	// Height off the union plane comes from the nearest traced point of either shape.
	TArray<FVector> Traced = Original;
	Traced.Append(Mirrored);
	auto Lift = [&](const FVector2d& Point2d)
	{
		const FVector Flat = U * Point2d.X + V * Point2d.Y;
		double Best = TNumericLimits<double>::Max();
		double Height = 0.0;
		for (const FVector& Point : Traced)
		{
			const FVector Delta = Point - Flat;
			const double Distance = FMath::Square(FVector::DotProduct(Delta, U)) + FMath::Square(FVector::DotProduct(Delta, V));
			if (Distance < Best)
			{
				Best = Distance;
				Height = FVector::DotProduct(Point, W);
			}
		}
		return Flat + W * Height;
	};
	auto Emit = [&](const FPolygon2d& Polygon)
	{
		FPCGUtilsModifierPath Result;
		Result.bClosed = true;
		for (const FVector2d& Vertex : Polygon.GetVertices())
		{
			FSplinePoint Point;
			Point.Position = Lift(Vertex);
			Point.ArriveTangent = FVector::ZeroVector;
			Point.LeaveTangent = FVector::ZeroVector;
			Point.Rotation = FRotator::ZeroRotator;
			Point.Scale = FVector::OneVector;
			Point.Type = ESplinePointType::Linear;
			Result.Add(Point, INDEX_NONE);
		}
		Out.Emplace(MoveTemp(Result), MixedSide);
	};

	if (Union.Num() > 1)
	{
		Context.Warn(this, FText::Format(
			LOCTEXT("UnionApart", "Mirror: the closed path and its {0} mirror do not overlap, so their union is {1} separate shapes."),
			AxisName(Axis), FText::AsNumber(Union.Num())));
	}
	for (const FGeneralPolygon2d& Shape : Union)
	{
		Emit(Shape.GetOuter());
		for (const FPolygon2d& Hole : Shape.GetHoles())
		{
			Context.Warn(this, LOCTEXT("UnionHole", "Mirror: the union of the closed path and its mirror encloses a hole; the hole is output as its own closed path."));
			Emit(Hole);
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
