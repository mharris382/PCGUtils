// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/PCGUtilsDynMeshAttributeHelpers.h"
#include "Data/PCGPointArrayData.h"
#include "Elements/Painters/PCGDynMeshAxisGradientPainter.h"
#include "Elements/Painters/PCGDynMeshCombinePainters.h"
#include "Elements/Painters/PCGDynMeshPainterFromPoints.h"
#include "Elements/Painters/PCGDynMeshPainterMath.h"
#include "Elements/Painters/PCGDynMeshPointsToPainter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshPainterCompositionTest,
	"PCGUtils.DynMesh.Painter.NestedComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshPainterCompositionTest::RunTest(const FString&)
{
	using namespace UE::Geometry;

	UPCGDynMeshAxisGradientPainterFactoryData* Gradient =
		NewObject<UPCGDynMeshAxisGradientPainterFactoryData>();
	Gradient->Origin = FVector::ZeroVector;
	Gradient->Axis = FVector::UpVector;
	Gradient->StartDistance = 0.0f;
	Gradient->EndDistance = 100.0f;

	UPCGDynMeshPainterFromPointsFactoryData* Brush =
		NewObject<UPCGDynMeshPainterFromPointsFactoryData>();
	Brush->Falloff = EPCGUtilsDynMeshPainterFalloff::Smooth;
	Brush->Reduction = EPCGUtilsDynMeshPainterPointReduction::Max;
	Brush->bClampValue = true;
	FPCGUtilsDynMeshPreparedPaintPoint& Point = Brush->Points.Emplace_GetRef();
	Point.bUseBoundsShape = false;
	Point.OuterRadius = 100.0f;
	Point.Value = 1.0f;
	FPCGUtilsDynMeshPreparedPaintPoint& WeakerOverlappingPoint = Brush->Points.Emplace_GetRef();
	WeakerOverlappingPoint.bUseBoundsShape = false;
	WeakerOverlappingPoint.OuterRadius = 100.0f;
	WeakerOverlappingPoint.Value = 0.25f;

	const UPCGDynMeshPainterFromPointsProviderSettings* PointProviderDefaults =
		NewObject<UPCGDynMeshPainterFromPointsProviderSettings>();
	TestEqual(TEXT("Paint from Points defaults its value selector to $Density"),
		PointProviderDefaults->ValueSelector.GetPointProperty(), EPCGPointProperties::Density);
	TestEqual(TEXT("Paint from Points defaults to fitted point bounds"),
		PointProviderDefaults->RadiusSource, EPCGUtilsDynMeshPainterRadiusSource::Bounds);
	TestEqual(TEXT("Paint from Points defaults its falloff power to $Steepness"),
		PointProviderDefaults->FalloffPowerSelector.GetPointProperty(), EPCGPointProperties::Steepness);
	TestTrue(TEXT("Paint from Points exposes Clamp Value enabled by default"),
		PointProviderDefaults->bClampValue);

	UPCGDynMeshPainterMathFactoryData* Multiply = NewObject<UPCGDynMeshPainterMathFactoryData>();
	Multiply->Operation = EPCGUtilsDynMeshPainterMathOperation::Multiply;
	Multiply->A = Gradient;
	Multiply->B = Brush;

	FDynamicMesh3 Mesh;
	const FPCGUtilsDynMeshPainterEvaluationContext EvaluationContext(nullptr, Mesh, FTransform::Identity);
	const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Operation = Multiply->CreateOperation(nullptr);
	TestTrue(TEXT("Nested Painter operation is created"), Operation.IsValid());
	if (!Operation || !Operation->Initialize(EvaluationContext))
	{
		AddError(TEXT("Nested Painter operation did not initialize."));
		return false;
	}

	FPCGUtilsDynMeshPainterSample Sample;
	Sample.LocalPosition = FVector(0.0, 0.0, 50.0);
	Sample.WorldPosition = Sample.LocalPosition;
	Sample.VertexID = 7;

	// The Gradient and strongest overlapping brush both evaluate to 0.5 here. Max keeps the stronger brush,
	// then Painter Math evaluates the two children directly and multiplies them.
	TestTrue(TEXT("Axis Gradient multiplied by smooth point brush evaluates to 0.25"),
		FMath::IsNearlyEqual(Operation->Evaluate(Sample).Scalar, 0.25f, KINDA_SMALL_NUMBER));

	Sample.WorldPosition = FVector(0.0, 0.0, 200.0);
	TestTrue(TEXT("Point brush contributes zero outside its radius"),
		FMath::IsNearlyZero(Operation->Evaluate(Sample).Scalar));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshPainterColorResolutionTest,
	"PCGUtils.DynMesh.Painter.ColorResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshPainterColorResolutionTest::RunTest(const FString&)
{
	using namespace UE::Geometry;

	FVector4f Destination(0.0f, 0.1f, 0.2f, 0.3f);
	PCGUtilsDynMeshPainters::ResolveValueToColor(
		FPCGUtilsDynMeshPainterValue::MakeScalar(0.75f),
		EPCGUtilsDynMeshPainterColorChannel::Red |
			EPCGUtilsDynMeshPainterColorChannel::Blue,
		Destination);
	TestTrue(TEXT("Scalar Painter broadcasts only to requested channels"),
		Destination.Equals(FVector4f(0.75f, 0.1f, 0.75f, 0.3f)));

	Destination = FVector4f(0.0f, 0.1f, 0.2f, 0.3f);
	PCGUtilsDynMeshPainters::ResolveValueToColor(
		FPCGUtilsDynMeshPainterValue::MakeColor(
			FVector4f(0.4f, 0.5f, 0.6f, 0.7f),
			EPCGUtilsDynMeshPainterColorChannel::Red |
				EPCGUtilsDynMeshPainterColorChannel::Alpha),
		EPCGUtilsDynMeshPainterColorChannel::Green |
			EPCGUtilsDynMeshPainterColorChannel::Alpha,
		Destination);
	TestTrue(TEXT("Color Painter writes only requested channels that it defines"),
		Destination.Equals(FVector4f(0.0f, 0.1f, 0.2f, 0.7f)));

	UPCGDynMeshAxisGradientPainterFactoryData* Gradient =
		NewObject<UPCGDynMeshAxisGradientPainterFactoryData>();
	Gradient->Axis = FVector::UpVector;
	Gradient->StartDistance = 0.0f;
	Gradient->EndDistance = 100.0f;

	UPCGDynMeshPainterFromPointsFactoryData* Brush =
		NewObject<UPCGDynMeshPainterFromPointsFactoryData>();
	Brush->Falloff = EPCGUtilsDynMeshPainterFalloff::Hard;
	FPCGUtilsDynMeshPreparedPaintPoint& Point = Brush->Points.Emplace_GetRef();
	Point.bUseBoundsShape = false;
	Point.OuterRadius = 100.0f;
	Point.Value = 0.8f;

	UPCGDynMeshCombinePaintersFactoryData* Combine =
		NewObject<UPCGDynMeshCombinePaintersFactoryData>();
	Combine->ChannelPainters.SetNumZeroed(4);
	Combine->ChannelPainters[0] = Gradient;
	Combine->ChannelPainters[3] = Brush;

	FDynamicMesh3 Mesh;
	const FPCGUtilsDynMeshPainterEvaluationContext EvaluationContext(nullptr, Mesh, FTransform::Identity);
	const TSharedPtr<FPCGUtilsDynMeshPainterOperation> CombineOperation =
		Combine->CreateOperation(nullptr);
	const bool bCombineInitialized =
		CombineOperation && CombineOperation->Initialize(EvaluationContext);
	TestTrue(TEXT("Combine Painters initializes"), bCombineInitialized);
	if (!bCombineInitialized)
	{
		return false;
	}

	FPCGUtilsDynMeshPainterSample Sample;
	Sample.LocalPosition = FVector(0.0, 0.0, 50.0);
	Sample.WorldPosition = Sample.LocalPosition;
	const FPCGUtilsDynMeshPainterValue CombinedValue = CombineOperation->Evaluate(Sample);
	TestEqual(TEXT("Combine Painters returns a color Painter"),
		CombinedValue.Type, EPCGUtilsDynMeshPainterValueType::Color);
	TestTrue(TEXT("Combine Painters maps scalar children into their R and A targets"),
		CombinedValue.Color.Equals(FVector4f(0.5f, 0.0f, 0.0f, 0.8f)));
	TestEqual(TEXT("Combine Painters marks only connected output channels"),
		static_cast<uint8>(CombinedValue.ColorChannels),
		static_cast<uint8>(EPCGUtilsDynMeshPainterColorChannel::Red |
			EPCGUtilsDynMeshPainterColorChannel::Alpha));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshRadialPainterShapeTest,
	"PCGUtils.DynMesh.Painter.RadialBrushShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshRadialPainterShapeTest::RunTest(const FString&)
{
	using namespace UE::Geometry;

	UPCGDynMeshPainterFromPointsFactoryData* Brush =
		NewObject<UPCGDynMeshPainterFromPointsFactoryData>();
	Brush->Falloff = EPCGUtilsDynMeshPainterFalloff::Linear;
	Brush->Reduction = EPCGUtilsDynMeshPainterPointReduction::Max;
	Brush->bClampValue = true;
	FPCGUtilsDynMeshPreparedPaintPoint& Point = Brush->Points.Emplace_GetRef();
	Point.bUseBoundsShape = true;
	Point.LocalOuterRadii = FVector(100.0, 50.0, 25.0);
	Point.InnerRadiusFraction = 0.5f;
	Point.FalloffPower = 2.0f;
	Point.Value = 1.0f;

	FDynamicMesh3 Mesh;
	const FPCGUtilsDynMeshPainterEvaluationContext EvaluationContext(nullptr, Mesh, FTransform::Identity);
	TSharedPtr<FPCGUtilsDynMeshPainterOperation> Operation = Brush->CreateOperation(nullptr);
	const bool bInitialized = Operation && Operation->Initialize(EvaluationContext);
	TestTrue(TEXT("Bounds-fitted radial Painter initializes"), bInitialized);
	if (!bInitialized)
	{
		return false;
	}

	FPCGUtilsDynMeshPainterSample Sample;
	Sample.WorldPosition = FVector(25.0, 0.0, 0.0);
	TestTrue(TEXT("Inner radius produces a solid core"),
		FMath::IsNearlyEqual(Operation->Evaluate(Sample).Scalar, 1.0f));
	Sample.WorldPosition = FVector(75.0, 0.0, 0.0);
	TestTrue(TEXT("Falloff starts at the inner radius and applies its point power"),
		FMath::IsNearlyEqual(Operation->Evaluate(Sample).Scalar, 0.25f));
	Sample.WorldPosition = FVector(0.0, 40.0, 0.0);
	TestTrue(TEXT("Non-uniform bounds produce ellipsoidal distance"),
		FMath::IsNearlyEqual(Operation->Evaluate(Sample).Scalar, 0.16f, KINDA_SMALL_NUMBER));
	Sample.WorldPosition = FVector(0.0, 0.0, 30.0);
	TestTrue(TEXT("Samples outside the fitted ellipsoid contribute zero"),
		FMath::IsNearlyZero(Operation->Evaluate(Sample).Scalar));

	Brush->Falloff = EPCGUtilsDynMeshPainterFalloff::Hard;
	Brush->Reduction = EPCGUtilsDynMeshPainterPointReduction::Add;
	Point.Value = 0.75f;
	const FPCGUtilsDynMeshPreparedPaintPoint SecondPoint = Point;
	Brush->Points.Add(SecondPoint);
	Sample.WorldPosition = FVector::ZeroVector;
	Operation = Brush->CreateOperation(nullptr);
	Operation->Initialize(EvaluationContext);
	TestTrue(TEXT("Clamp Value clamps the reduced result to one"),
		FMath::IsNearlyEqual(Operation->Evaluate(Sample).Scalar, 1.0f));
	Brush->bClampValue = false;
	Operation = Brush->CreateOperation(nullptr);
	Operation->Initialize(EvaluationContext);
	TestTrue(TEXT("Disabling Clamp Value preserves values above one"),
		FMath::IsNearlyEqual(Operation->Evaluate(Sample).Scalar, 1.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshPointsToPainterTest,
	"PCGUtils.DynMesh.Painter.PointsToPainter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshPointsToPainterTest::RunTest(const FString&)
{
	using namespace UE::Geometry;

	const UPCGDynMeshPointsToPainterProviderSettings* Defaults =
		NewObject<UPCGDynMeshPointsToPainterProviderSettings>();
	TestEqual(TEXT("Scalar mode defaults to $Density"),
		Defaults->ScalarValueSelector.GetPointProperty(), EPCGPointProperties::Density);
	TestEqual(TEXT("Color mode defaults to $Color"),
		Defaults->ColorValueSelector.GetPointProperty(), EPCGPointProperties::Color);

	UPCGPointArrayData* FirstPoints = NewObject<UPCGPointArrayData>();
	UPCGPointArrayData* SecondPoints = NewObject<UPCGPointArrayData>();
	FirstPoints->SetNumPoints(3);
	SecondPoints->SetNumPoints(3);
	FirstPoints->GetDensityValueRange()[1] = 0.2f;
	SecondPoints->GetDensityValueRange()[1] = 0.8f;
	FirstPoints->GetColorValueRange()[1] = FVector4(0.1, 0.2, 0.3, 0.4);
	SecondPoints->GetColorValueRange()[1] = FVector4(0.5, 0.6, 0.7, 0.8);

	FDynamicMesh3 Mesh;
	Mesh.AppendVertex(FVector3d::Zero());
	Mesh.AppendVertex(FVector3d::UnitX());
	Mesh.AppendVertex(FVector3d::UnitY());
	Mesh.AppendVertex(FVector3d::UnitZ());
	Mesh.RemoveVertex(1);
	const FPCGUtilsDynMeshPainterEvaluationContext EvaluationContext(
		nullptr, Mesh, FTransform::Identity, 1, 2);
	FPCGUtilsDynMeshPainterSample Sample;
	// DynMesh To Points emits valid vertices in iterator order. With vertex ID 1 removed, vertex 2 maps to
	// point index 1 rather than being used directly as a point-array index.
	Sample.VertexID = 2;

	UPCGDynMeshPointsToPainterFactoryData* Factory =
		NewObject<UPCGDynMeshPointsToPainterFactoryData>();
	Factory->PointDataSets = {FirstPoints, SecondPoints};
	Factory->Mode = EPCGUtilsDynMeshPointsToPainterMode::Scalar;
	Factory->ValueSelector.SetPointProperty(EPCGPointProperties::Density);
	TSharedPtr<FPCGUtilsDynMeshPainterOperation> Operation = Factory->CreateOperation(nullptr);
	const bool bScalarInitialized = Operation && Operation->Initialize(EvaluationContext);
	TestTrue(TEXT("Points to Painter initializes against the matching second dataset"), bScalarInitialized);
	if (!bScalarInitialized)
	{
		return false;
	}
	TestTrue(TEXT("Scalar Painter reads point index matching the vertex index"),
		FMath::IsNearlyEqual(Operation->Evaluate(Sample).Scalar, 0.8f));

	Factory->Mode = EPCGUtilsDynMeshPointsToPainterMode::Color;
	Factory->ValueSelector.SetPointProperty(EPCGPointProperties::Color);
	Operation = Factory->CreateOperation(nullptr);
	const bool bColorInitialized = Operation && Operation->Initialize(EvaluationContext);
	TestTrue(TEXT("Color Points to Painter initializes"), bColorInitialized);
	if (!bColorInitialized)
	{
		return false;
	}
	const FPCGUtilsDynMeshPainterValue ColorValue = Operation->Evaluate(Sample);
	TestEqual(TEXT("Color mode returns a color Painter"),
		ColorValue.Type, EPCGUtilsDynMeshPainterValueType::Color);
	TestTrue(TEXT("Color Painter reads $Color from the paired dataset"),
		ColorValue.Color.Equals(FVector4f(0.5f, 0.6f, 0.7f, 0.8f)));
	TestEqual(TEXT("Color Painter defines all RGBA channels"),
		static_cast<uint8>(ColorValue.ColorChannels),
		static_cast<uint8>(EPCGUtilsDynMeshPainterColorChannel::All));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshPainterSplitOverlayTest,
	"PCGUtils.DynMesh.Painter.SplitColorOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshPainterSplitOverlayTest::RunTest(const FString&)
{
	using namespace UE::Geometry;

	FDynamicMesh3 Mesh;
	const int32 V0 = Mesh.AppendVertex(FVector3d(0.0, 0.0, 0.0));
	const int32 V1 = Mesh.AppendVertex(FVector3d(1.0, 0.0, 0.0));
	const int32 V2 = Mesh.AppendVertex(FVector3d(1.0, 1.0, 0.0));
	const int32 V3 = Mesh.AppendVertex(FVector3d(0.0, 1.0, 0.0));
	const int32 T0 = Mesh.AppendTriangle(V0, V1, V2);
	const int32 T1 = Mesh.AppendTriangle(V0, V2, V3);
	Mesh.EnableAttributes();
	Mesh.Attributes()->EnablePrimaryColors();
	FDynamicMeshColorOverlay* Overlay = Mesh.Attributes()->PrimaryColors();

	const FVector4f OldColor(0.1f, 0.2f, 0.3f, 0.4f);
	const FIndex3i FirstElements(
		Overlay->AppendElement(OldColor),
		Overlay->AppendElement(OldColor),
		Overlay->AppendElement(OldColor));
	const FIndex3i SecondElements(
		Overlay->AppendElement(OldColor),
		Overlay->AppendElement(OldColor),
		Overlay->AppendElement(OldColor));
	Overlay->SetTriangle(T0, FirstElements);
	Overlay->SetTriangle(T1, SecondElements);

	const FVector4f Painted(0.9f, 0.8f, 0.7f, 0.6f);
	PCGUtilsDynMeshAttributeHelpers::SetVertexColor(Mesh, *Overlay, V0, Painted);

	TestTrue(TEXT("First split color element is painted"),
		Overlay->GetElement(FirstElements.A).Equals(Painted));
	TestTrue(TEXT("Second split color element is painted consistently"),
		Overlay->GetElement(SecondElements.A).Equals(Painted));
	TestTrue(TEXT("Unrelated overlay elements are preserved"),
		Overlay->GetElement(FirstElements.B).Equals(OldColor));
	TestTrue(TEXT("Existing base-color lookup reads an attached overlay element"),
		PCGUtilsDynMeshAttributeHelpers::GetVertexColor(Mesh, *Overlay, V0, FVector4f::Zero()).Equals(Painted));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
