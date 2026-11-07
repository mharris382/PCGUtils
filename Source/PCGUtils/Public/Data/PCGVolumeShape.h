// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGVolumeShape.generated.h"

namespace UE::Geometry { class FDynamicMesh3; }

/** The shape a PCG Volume Component encloses. */
UENUM(BlueprintType)
enum class EPCGVolumeShape : uint8
{
	/** A box Box Extent from the component's origin along each axis. */
	Box,
	/** A sphere of Radius around the component's origin. */
	Sphere,
	/** A cylinder along the component's Z axis: Radius across, Half Height above and below the origin. */
	Cylinder,
	/** A cone along the component's Z axis: a base of Radius at -Half Height, its tip at +Half Height. */
	Cone,
	/** A capsule along the component's Z axis, like a Capsule Component: Half Height includes the rounded ends. */
	Capsule,
	/** The brush of the engine Volume actor the component is on. Only meaningful on a Volume actor. */
	OwnerVolume UMETA(DisplayName="Owner Volume"),
};

/**
 * A volume shape placed in the world: what a PCG Volume Component encloses, copied out of the component so it can
 * be tested and meshed off the game thread. The shape is defined in its own space and placed by ShapeToWorld,
 * which may carry rotation and non-uniform scale. Owner Volume is not representable here: it encloses nothing.
 */
USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGVolumeShapeDesc
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape")
	EPCGVolumeShape Shape = EPCGVolumeShape::Box;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape")
	FVector BoxExtent = FVector(50.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape")
	double Radius = 50.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape")
	double HalfHeight = 50.0;

	/** Segments around the round shapes when meshed. Containment tests use the exact shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape")
	int32 Sides = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape")
	FTransform ShapeToWorld = FTransform::Identity;

	/** Whether a point in the shape's own space is inside it. */
	bool ContainsLocal(const FVector& LocalPosition) const;

	/** Whether a world-space point is inside the shape. */
	bool Contains(const FVector& WorldPosition) const;

	/** The shape's bounds in its own space. Invalid for Owner Volume. */
	FBox GetLocalBounds() const;

	/** The world-space box holding the shape, rotation and scale included. Invalid for Owner Volume. */
	FBox GetWorldBounds() const;

	/**
	 * A closed mesh of the shape, outward-facing. In the shape's own space, or in world space with bWorldSpace.
	 * Leaves the mesh empty for Owner Volume.
	 */
	void BuildMesh(UE::Geometry::FDynamicMesh3& OutMesh, bool bWorldSpace) const;
};
