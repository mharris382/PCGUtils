// Copyright Max Harris

#pragma once

#include "Math/Color.h"

namespace PCGUtilsDynMeshNodeColor
{
	/**
	 * Title color of every PCGUtils DynMesh-family node (processes, selections, Builders, Painters, getters).
	 *
	 * PCGUtils nodes report PCGUtils category types, which the PCG editor has no color for, so a node that does not
	 * override GetNodeTitleColor() draws colorless. The process base returns this; other nodes return it directly.
	 * PCGUtils.DynMesh.NodeColor checks every node class.
	 */
	inline const FLinearColor Title(0.413f, 0.25f, 1.0f, 1.0f);
}
