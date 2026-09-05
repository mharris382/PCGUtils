// Copyright Max Harris
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Elements/Painters/PCGDynMeshPainterMath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsPainterBlendTest, "PCGUtils.DynMesh.Painter.BlendValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPCGUtilsPainterBlendTest::RunTest(const FString&)
{
	using V = FPCGUtilsDynMeshPainterValue;
	using Op = EPCGUtilsDynMeshPainterMathOperation;
	using C = EPCGUtilsDynMeshPainterColorChannel;
	const float Expected[] = { 1.0f, -0.6f, 0.16f, 0.2f, 0.8f, 0.8f, 0.84f };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Expected); ++Index)
	{
		const V Result = PCGUtilsPainters::BlendValues(V::MakeScalar(0.2f), V::MakeScalar(0.8f), static_cast<Op>(Index), 1.0f);
		TestTrue(FString::Printf(TEXT("Blend mode %d: got %.9f expected %.9f"), Index, Result.Scalar, Expected[Index]), FMath::IsNearlyEqual(Result.Scalar, Expected[Index], KINDA_SMALL_NUMBER));
	}
	TestTrue(TEXT("Mix factor"), FMath::IsNearlyEqual(PCGUtilsPainters::BlendValues(V::MakeScalar(0.2f), V::MakeScalar(0.8f), Op::Mix, 0.5f).Scalar, 0.5f));
	const V Base = V::MakeColor(FVector4f(0.2f, 0.3f, 0.4f, 0.5f));
	const V Blend = V::MakeColor(FVector4f(0.8f, 0, 0, 0), C::Red);
	TestTrue(TEXT("Undefined blend channels preserve base"), PCGUtilsPainters::BlendValues(Base, Blend, Op::Mix, 1).Color.Equals(FVector4f(0.8f, 0.3f, 0.4f, 0.5f)));
	TestTrue(TEXT("Scalar broadcasts including alpha"), PCGUtilsPainters::BlendValues(Base, V::MakeScalar(2), Op::Multiply, 1).Color.Equals(FVector4f(0.4f, 0.6f, 0.8f, 1.0f)));
	TestTrue(TEXT("Zero factor is base"), PCGUtilsPainters::BlendValues(Base, Blend, Op::Add, 0).Color.Equals(Base.Color));
	const V RedOnly = PCGUtilsPainters::BlendValues(V::MakeScalar(0.5f), Blend, Op::Multiply, 1);
	TestEqual(TEXT("Scalar does not invent color channels"), static_cast<uint8>(RedOnly.ColorChannels), static_cast<uint8>(C::Red));
	return true;
}
#endif
