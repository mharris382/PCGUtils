// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Painters/PCGDynMeshAxisGradientPainter.h"
#include "Elements/Painters/PCGDynMeshBlurPainter.h"
#include "Elements/Painters/PCGDynMeshConstantPainter.h"
#include "Elements/Painters/PCGDynMeshRandomValueByIslandPainter.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"

namespace PCGUtilsPainterConstantAndBlurTests
{
	using namespace UE::Geometry;

	/** A 2-row strip of 10 quads along +X: vertices at x = 0,10,...,100 and y = 0,10. Vertex ID = Column * 2 + Row. */
	FDynamicMesh3 MakeStrip(const FVector3d& Origin = FVector3d::ZeroVector)
	{
		FDynamicMesh3 Mesh;
		for (int32 Column = 0; Column <= 10; ++Column)
		{
			Mesh.AppendVertex(Origin + FVector3d(Column * 10.0, 0.0, 0.0));
			Mesh.AppendVertex(Origin + FVector3d(Column * 10.0, 10.0, 0.0));
		}
		for (int32 Column = 0; Column < 10; ++Column)
		{
			const int32 A = Column * 2, B = A + 1, C = A + 2, D = A + 3;
			Mesh.AppendTriangle(A, C, D);
			Mesh.AppendTriangle(A, D, B);
		}
		return Mesh;
	}

	/** One axis-aligned 10x10 quad at Origin. */
	void AppendQuad(FDynamicMesh3& Mesh, const FVector3d& Origin)
	{
		const int32 V0 = Mesh.AppendVertex(Origin);
		const int32 V1 = Mesh.AppendVertex(Origin + FVector3d(10, 0, 0));
		const int32 V2 = Mesh.AppendVertex(Origin + FVector3d(10, 10, 0));
		const int32 V3 = Mesh.AppendVertex(Origin + FVector3d(0, 10, 0));
		Mesh.AppendTriangle(V0, V1, V2);
		Mesh.AppendTriangle(V0, V2, V3);
	}

	/** Graph data for Mesh whose @Data domain holds one Vector4 PathColor and, optionally, a BlurRadius. */
	UPCGDynamicMeshData* MakeMeshData(const FDynamicMesh3& Mesh, const FVector4& PathColor, TOptional<double> BlurRadius = {})
	{
		UPCGDynamicMeshData* MeshData = NewObject<UPCGDynamicMeshData>();
		MeshData->Initialize(FDynamicMesh3(Mesh));
		FPCGMetadataDomain* Domain = MeshData->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
		Domain->AddEntry();
		Domain->FindOrCreateAttribute<FVector4>(TEXT("PathColor"), FVector4::Zero(), false, false, true)
			->SetValue(PCGFirstEntryKey, PathColor);
		if (BlurRadius.IsSet())
		{
			Domain->FindOrCreateAttribute<double>(TEXT("BlurRadius"), 0.0, false, false, true)
				->SetValue(PCGFirstEntryKey, BlurRadius.GetValue());
		}
		return MeshData;
	}

	TSharedPtr<FPCGUtilsDynMeshPainterOperation> Prepare(
		const UPCGUtilsDynMeshPainterFactoryData* Factory, const FPCGUtilsDynMeshPainterEvaluationContext& Context)
	{
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> Operation = Factory->CreateOperation(nullptr);
		if (!Operation || !Operation->Initialize(Context) || !Operation->Prepare(Context))
		{
			return nullptr;
		}
		return Operation;
	}

	FPCGUtilsDynMeshPainterValue EvaluateAt(
		const FPCGUtilsDynMeshPainterOperation& Operation, const FDynamicMesh3& Mesh, int32 VertexID)
	{
		FPCGUtilsDynMeshPainterSample Sample;
		Sample.VertexID = VertexID;
		Sample.LocalPosition = FVector(Mesh.GetVertex(VertexID));
		Sample.WorldPosition = Sample.LocalPosition;
		return Operation.Evaluate(Sample);
	}

	UPCGDynMeshConstantPainterFactoryData* MakeScalarConstant(float Value)
	{
		UPCGDynMeshConstantPainterFactoryData* Factory = NewObject<UPCGDynMeshConstantPainterFactoryData>();
		Factory->ValueType = EPCGUtilsDynMeshPainterValueType::Scalar;
		Factory->Value = FPCGUtilsInputShorthandFloat(Value, TEXT("Value"));
		return Factory;
	}

	/** Linear X gradient: value = x / 100 on the strip. */
	UPCGDynMeshAxisGradientPainterFactoryData* MakeXGradient()
	{
		UPCGDynMeshAxisGradientPainterFactoryData* Factory = NewObject<UPCGDynMeshAxisGradientPainterFactoryData>();
		Factory->Axis = FVector::XAxisVector;
		Factory->StartDistance = 0.0f;
		Factory->EndDistance = 100.0f;
		return Factory;
	}

	UPCGDynMeshBlurPainterFactoryData* MakeBlur(
		const UPCGUtilsDynMeshPainterFactoryData* Source, double Radius, EPCGUtilsPainterBlurFalloff Falloff)
	{
		UPCGDynMeshBlurPainterFactoryData* Factory = NewObject<UPCGDynMeshBlurPainterFactoryData>();
		Factory->Source = Source;
		Factory->Radius = FPCGUtilsInputShorthandDoubleAbs(Radius, TEXT("BlurRadius"));
		Factory->Falloff = Falloff;
		Factory->Space = EPCGUtilsDynMeshPainterCoordinateSpace::Local;
		return Factory;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsPainterConstantTest,
	"PCGUtils.Painter.Constant.ConstantAndPerMeshDataValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsPainterConstantTest::RunTest(const FString&)
{
	using namespace PCGUtilsPainterConstantAndBlurTests;

	const FDynamicMesh3 Mesh = MakeStrip();

	// A constant scalar is the same everywhere and needs no data.
	{
		const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Mesh, FTransform::Identity);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Prepare(MakeScalarConstant(0.25f), Context);
		if (!TestNotNull(TEXT("Constant scalar prepared"), Op.Get()))
		{
			return false;
		}
		TestEqual(TEXT("Constant scalar output type"), static_cast<uint8>(Op->GetOutputType()), static_cast<uint8>(EPCGUtilsDynMeshPainterValueType::Scalar));
		TestEqual(TEXT("Constant scalar at the first vertex"), EvaluateAt(*Op, Mesh, 0).Scalar, 0.25f);
		TestEqual(TEXT("Constant scalar at the last vertex"), EvaluateAt(*Op, Mesh, 21).Scalar, 0.25f);
	}

	// A constant color defines only the enabled channels.
	{
		UPCGDynMeshConstantPainterFactoryData* Factory = NewObject<UPCGDynMeshConstantPainterFactoryData>();
		Factory->ValueType = EPCGUtilsDynMeshPainterValueType::Color;
		Factory->Color = FPCGUtilsInputShorthandVector4(FVector4(0.1, 0.2, 0.3, 0.4), TEXT("PathColor"));
		Factory->ColorChannels.bGreen = false;
		Factory->ColorChannels.bBlue = false;
		const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Mesh, FTransform::Identity);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Prepare(Factory, Context);
		if (!TestNotNull(TEXT("Constant color prepared"), Op.Get()))
		{
			return false;
		}
		const FPCGUtilsDynMeshPainterValue Value = EvaluateAt(*Op, Mesh, 3);
		TestEqual(TEXT("Constant color is a color"), static_cast<uint8>(Value.Type), static_cast<uint8>(EPCGUtilsDynMeshPainterValueType::Color));
		TestEqual(TEXT("Only R and A are defined"), static_cast<uint8>(Value.ColorChannels),
			static_cast<uint8>(EPCGUtilsDynMeshPainterColorChannel::Red | EPCGUtilsDynMeshPainterColorChannel::Alpha));
		TestTrue(TEXT("R carries the constant"), FMath::IsNearlyEqual(Value.Color.X, 0.1f));
		TestTrue(TEXT("A carries the constant"), FMath::IsNearlyEqual(Value.Color.W, 0.4f));
	}

	// @Data mode: one Painter, two meshes, each painted with its own @Data.PathColor.G.
	{
		UPCGDynMeshConstantPainterFactoryData* Factory = MakeScalarConstant(0.0f);
		Factory->Value.Input = EPCGUtilsInputValueType::Attribute;
		Factory->Value.Attribute.Update(TEXT("@Data.PathColor.G"));

		const UPCGDynamicMeshData* First = MakeMeshData(Mesh, FVector4(0.9, 0.3, 0.0, 1.0));
		const UPCGDynamicMeshData* Second = MakeMeshData(Mesh, FVector4(0.9, 0.7, 0.0, 1.0));
		const FPCGUtilsDynMeshPainterEvaluationContext FirstContext(First, Mesh, FTransform::Identity, 0, 2);
		const FPCGUtilsDynMeshPainterEvaluationContext SecondContext(Second, Mesh, FTransform::Identity, 1, 2);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> FirstOp = Prepare(Factory, FirstContext);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> SecondOp = Prepare(Factory, SecondContext);
		if (!TestNotNull(TEXT("First mesh prepared"), FirstOp.Get()) || !TestNotNull(TEXT("Second mesh prepared"), SecondOp.Get()))
		{
			return false;
		}
		TestTrue(TEXT("First mesh reads its own PathColor.G"), FMath::IsNearlyEqual(EvaluateAt(*FirstOp, Mesh, 5).Scalar, 0.3f));
		TestTrue(TEXT("Second mesh reads its own PathColor.G"), FMath::IsNearlyEqual(EvaluateAt(*SecondOp, Mesh, 5).Scalar, 0.7f));

		// Color mode reads the whole Vector4.
		UPCGDynMeshConstantPainterFactoryData* ColorFactory = NewObject<UPCGDynMeshConstantPainterFactoryData>();
		ColorFactory->ValueType = EPCGUtilsDynMeshPainterValueType::Color;
		ColorFactory->Color = FPCGUtilsInputShorthandVector4(FVector4::Zero(), TEXT("PathColor"));
		ColorFactory->Color.Input = EPCGUtilsInputValueType::Attribute;
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> ColorOp = Prepare(ColorFactory, SecondContext);
		if (TestNotNull(TEXT("Color from @Data prepared"), ColorOp.Get()))
		{
			const FPCGUtilsDynMeshPainterValue Value = EvaluateAt(*ColorOp, Mesh, 0);
			TestTrue(TEXT("Color from @Data R"), FMath::IsNearlyEqual(Value.Color.X, 0.9f));
			TestTrue(TEXT("Color from @Data G"), FMath::IsNearlyEqual(Value.Color.Y, 0.7f));
		}
	}

	// @Data mode on a mesh without the attribute fails Prepare with an error that names the attribute.
	{
		UPCGDynMeshConstantPainterFactoryData* Factory = MakeScalarConstant(0.0f);
		Factory->Value.Input = EPCGUtilsInputValueType::Attribute;
		Factory->Value.Attribute.Update(TEXT("@Data.Missing"));
		const UPCGDynamicMeshData* MeshData = MakeMeshData(Mesh, FVector4::One());
		const FPCGUtilsDynMeshPainterEvaluationContext Context(MeshData, Mesh, FTransform::Identity, 3, 4);
		AddExpectedError(TEXT("attribute 'Missing' was not found"), EAutomationExpectedErrorFlags::Contains, 1);
		TestNull(TEXT("Missing @Data attribute fails"), Prepare(Factory, Context).Get());
	}

	// Color mode with no channel enabled is refused in Initialize.
	{
		UPCGDynMeshConstantPainterFactoryData* Factory = NewObject<UPCGDynMeshConstantPainterFactoryData>();
		Factory->ValueType = EPCGUtilsDynMeshPainterValueType::Color;
		Factory->ColorChannels.bRed = Factory->ColorChannels.bGreen = Factory->ColorChannels.bBlue = Factory->ColorChannels.bAlpha = false;
		const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Mesh, FTransform::Identity);
		AddExpectedError(TEXT("Color Channels has no channel enabled"), EAutomationExpectedErrorFlags::Contains, 1);
		TestNull(TEXT("Color with no channels fails"), Prepare(Factory, Context).Get());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsPainterBlurTest,
	"PCGUtils.Painter.Blur.AveragesTheFieldWithinRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsPainterBlurTest::RunTest(const FString&)
{
	using namespace PCGUtilsPainterConstantAndBlurTests;

	const FDynamicMesh3 Mesh = MakeStrip();
	const FPCGUtilsDynMeshPainterEvaluationContext Context(nullptr, Mesh, FTransform::Identity);
	const UPCGDynMeshAxisGradientPainterFactoryData* Gradient = MakeXGradient();

	// Uniform, radius 15, at vertex 0 (0,0): the ball holds (0,0), (0,10), (10,0) and (10,10), values 0,0,0.1,0.1.
	{
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Prepare(MakeBlur(Gradient, 15.0, EPCGUtilsPainterBlurFalloff::Uniform), Context);
		if (!TestNotNull(TEXT("Uniform blur prepared"), Op.Get()))
		{
			return false;
		}
		TestEqual(TEXT("Blur keeps the scalar output type"), static_cast<uint8>(Op->GetOutputType()), static_cast<uint8>(EPCGUtilsDynMeshPainterValueType::Scalar));
		TestTrue(TEXT("Uniform blur at the strip end is the box average 0.05"),
			FMath::IsNearlyEqual(EvaluateAt(*Op, Mesh, 0).Scalar, 0.05f, 1e-5f));
	}

	// A symmetric kernel leaves a linear field unchanged away from the ends, and pulls the ends inward.
	for (const EPCGUtilsPainterBlurFalloff Falloff :
		{EPCGUtilsPainterBlurFalloff::Uniform, EPCGUtilsPainterBlurFalloff::Linear, EPCGUtilsPainterBlurFalloff::Gaussian})
	{
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Prepare(MakeBlur(Gradient, 25.0, Falloff), Context);
		if (!TestNotNull(TEXT("Blur prepared"), Op.Get()))
		{
			return false;
		}
		const FString Name = StaticEnum<EPCGUtilsPainterBlurFalloff>()->GetNameStringByValue(static_cast<int64>(Falloff));
		TestTrue(*FString::Printf(TEXT("%s blur keeps the linear field at x=50"), *Name),
			FMath::IsNearlyEqual(EvaluateAt(*Op, Mesh, 10).Scalar, 0.5f, 1e-5f));
		TestTrue(*FString::Printf(TEXT("%s blur raises the x=0 end"), *Name), EvaluateAt(*Op, Mesh, 0).Scalar > 0.001f);
		TestTrue(*FString::Printf(TEXT("%s blur lowers the x=100 end"), *Name), EvaluateAt(*Op, Mesh, 20).Scalar < 0.999f);
	}

	// Radius 0 leaves the Painter unchanged.
	{
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Prepare(MakeBlur(Gradient, 0.0, EPCGUtilsPainterBlurFalloff::Gaussian), Context);
		if (TestNotNull(TEXT("Zero-radius blur prepared"), Op.Get()))
		{
			TestTrue(TEXT("Zero radius is a pass-through"), FMath::IsNearlyEqual(EvaluateAt(*Op, Mesh, 4).Scalar, 0.2f, 1e-5f));
		}
	}

	// A blurred color keeps its defined channels and a constant stays constant.
	{
		UPCGDynMeshConstantPainterFactoryData* Red = NewObject<UPCGDynMeshConstantPainterFactoryData>();
		Red->ValueType = EPCGUtilsDynMeshPainterValueType::Color;
		Red->Color = FPCGUtilsInputShorthandVector4(FVector4(0.6, 0.0, 0.0, 1.0), TEXT("PathColor"));
		Red->ColorChannels.bGreen = Red->ColorChannels.bBlue = Red->ColorChannels.bAlpha = false;
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Prepare(MakeBlur(Red, 30.0, EPCGUtilsPainterBlurFalloff::Linear), Context);
		if (TestNotNull(TEXT("Color blur prepared"), Op.Get()))
		{
			const FPCGUtilsDynMeshPainterValue Value = EvaluateAt(*Op, Mesh, 7);
			TestEqual(TEXT("Color blur output type"), static_cast<uint8>(Op->GetOutputType()), static_cast<uint8>(EPCGUtilsDynMeshPainterValueType::Color));
			TestEqual(TEXT("Color blur defines only R"), static_cast<uint8>(Value.ColorChannels), static_cast<uint8>(EPCGUtilsDynMeshPainterColorChannel::Red));
			TestTrue(TEXT("Blurring a constant color keeps it"), FMath::IsNearlyEqual(Value.Color.X, 0.6f, 1e-5f));
		}
	}

	// Two islands 5 units apart: a big blur mixes them unless Keep Islands Separate is on.
	{
		FDynamicMesh3 Islands;
		AppendQuad(Islands, FVector3d(0, 0, 0));    // vertices 0..3
		AppendQuad(Islands, FVector3d(15, 0, 0));   // vertices 4..7
		const FPCGUtilsDynMeshPainterEvaluationContext IslandContext(nullptr, Islands, FTransform::Identity);

		UPCGDynMeshRandomValueByIslandPainterFactoryData* Random = NewObject<UPCGDynMeshRandomValueByIslandPainterFactoryData>();
		Random->Seed = 11;
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> RandomOp = Prepare(Random, IslandContext);
		UPCGDynMeshBlurPainterFactoryData* Mixed = MakeBlur(Random, 100.0, EPCGUtilsPainterBlurFalloff::Uniform);
		UPCGDynMeshBlurPainterFactoryData* Separate = MakeBlur(Random, 100.0, EPCGUtilsPainterBlurFalloff::Uniform);
		Separate->bKeepIslandsSeparate = true;
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> MixedOp = Prepare(Mixed, IslandContext);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> SeparateOp = Prepare(Separate, IslandContext);
		if (!TestNotNull(TEXT("Island source prepared"), RandomOp.Get()) || !TestNotNull(TEXT("Mixed blur prepared"), MixedOp.Get())
			|| !TestNotNull(TEXT("Separate blur prepared"), SeparateOp.Get()))
		{
			return false;
		}
		const float A = EvaluateAt(*RandomOp, Islands, 0).Scalar;
		const float B = EvaluateAt(*RandomOp, Islands, 4).Scalar;
		TestNotEqual(TEXT("The two islands start with different values"), A, B);
		TestTrue(TEXT("Mixed blur averages both islands"),
			FMath::IsNearlyEqual(EvaluateAt(*MixedOp, Islands, 0).Scalar, (A + B) * 0.5f, 1e-5f));
		TestTrue(TEXT("Separate blur keeps island A"), FMath::IsNearlyEqual(EvaluateAt(*SeparateOp, Islands, 0).Scalar, A, 1e-5f));
		TestTrue(TEXT("Separate blur keeps island B"), FMath::IsNearlyEqual(EvaluateAt(*SeparateOp, Islands, 4).Scalar, B, 1e-5f));
	}

	// @Data radius: read per mesh; a missing attribute is an error naming it.
	{
		UPCGDynMeshBlurPainterFactoryData* Blur = MakeBlur(Gradient, 0.0, EPCGUtilsPainterBlurFalloff::Uniform);
		Blur->Radius.Input = EPCGUtilsInputValueType::Attribute;

		const UPCGDynamicMeshData* WithRadius = MakeMeshData(Mesh, FVector4::One(), 15.0);
		const FPCGUtilsDynMeshPainterEvaluationContext WithContext(WithRadius, Mesh, FTransform::Identity);
		const TSharedPtr<FPCGUtilsDynMeshPainterOperation> Op = Prepare(Blur, WithContext);
		if (TestNotNull(TEXT("@Data radius prepared"), Op.Get()))
		{
			TestTrue(TEXT("@Data radius 15 gives the box average"), FMath::IsNearlyEqual(EvaluateAt(*Op, Mesh, 0).Scalar, 0.05f, 1e-5f));
		}

		const UPCGDynamicMeshData* WithoutRadius = MakeMeshData(Mesh, FVector4::One());
		const FPCGUtilsDynMeshPainterEvaluationContext WithoutContext(WithoutRadius, Mesh, FTransform::Identity, 2, 3);
		AddExpectedError(TEXT("attribute 'BlurRadius' was not found"), EAutomationExpectedErrorFlags::Contains, 1);
		TestNull(TEXT("Missing @Data radius fails"), Prepare(Blur, WithoutContext).Get());
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
