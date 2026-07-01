# PCGUtils
This is a growing collection of tools and subgraphs that build upon the PCG Plugin for UE.   

Currently supports UE 5.7+

This plugin relies on [PCG-ExtendedToolkit](https://github.com/PCGEx/PCGExtendedToolkit).  Some nodes subgraphs will not function if PCG-ExtendedToolkit is not installed, but should still compile without pcg-ex.

# Contents
- [PCGActorBase][1]: base class for creating pcg tools
- [PCGUtil Components][2]: set of utility components for working with pcg
- [PCG Dynamic Mesh Node library][3]: collection of nodes for working with dynamic meshes in PCG

[1]:https://github.com/mharris382/PCGUtils/edit/main/README.md#pcgactorbase
[2]:https://github.com/mharris382/PCGUtils/edit/main/README.md#components
[3]:https://github.com/mharris382/PCGUtils/edit/main/README.md#dynamic-mesh-blueprint-elementssubgraphs

# PCGActorBase
This actor is a replacement to the vanilla PCGVolumeActor, that is created when a pcg graph is dragged into a level.  Using this actor for PCG tools adds a number of convience functionalies and helps streamline various pcg workflows, particularly when saving dynamic meshes or assets from pcg.    The actor provides a no-physics box component, which is automatically updated from a bounds computation function.  The box component will automatically be calculated from all PCGUtil components and splines on the actor, but you can also override the function to provide any custom bounds computation logic.   There is additionally a bounds padding value in the actor details under PCG/Bounds.

The PCGActorBase also automatically provides a globally unique save name to be used when baking assets from pcg.  You can specify the folder to save the asset under the **PCG/Bake** settings in the details panel.  

### Baking Dynamic Meshes with PCGActorBase
There is a subgraph called that can be used to bake a dynamic mesh to a static mesh called **BakeMesh**.  This graph handles the entire bake process for you, including the prebake and postbake subgraph hooks.   These subgraphs are found under **PCG/Bake** and allow you to build pcg tools that can be easily extended as needed through these subgraphs.   The plugin provides pcg graph templates for both PreBake and PostBake.  

> NOTE: if you are baking more than one dynamic mesh from the same graph, you will need to assign each pcg dynamic mesh data set a **@Data.Label** string value to prevent the BakeMesh node from overwritting previously saved static meshes.

#### Additional Convience Subgraphs with PCGActorBase
- **ActorSeed**: returns a public int32 seed property on PCGActorBase, with optional seed offset

# Components
There are several utility components in PCGUtils that are helpful when working with PCG.  Each of these components has an editor flag **bRegeneratePCGOnEdits** which causes the component to trigger pcg regeneration of it's parent actor when the component is editted, regardless of whether the pcg component is set to regenerate in editor.  This feature only works when the component is on a `PCGActorBase`

## PCGMarkerComponent
This component is a designer positioned box that PCG can query.  It is intended as a replacement for actors with a box component when designers need a way to manually mark regions for PCG.  
To access Markers from PCG use the **GetMarkerData** node. The component is data only and has no geometry. You can add as many of these as you want to a single actor and retrieve them directly as point data in PCG.

## ShapePathComponent
There are cases when you may want a path of points, like a spline, but want a *perfect* shape rather than manually editing spline points.    The shape path component effectively does this.  There are a number of different shape factories implemented, each provides a path in a specific shape with settings for that shape.   PCG can retrieve point data from this component as an ordered path and use it interchangebly with data from a pcg spline component.
ShapePaths: 
- Circle
- Arc
- Rectangle
- Polygon
- Star

## PCGSplineComponent
This is a subclass of the spline component which just adds a few pcg specific quality of life improvements.   you have the option to supply a path processor override graph, providing a standardized way to make paths extensible from editor assigned PCG graph objects.  


# Dynamic Mesh Node library

there are a number of useful dynamic mesh C++ elements, bp elements and subgraphs in this plugin.  The most useful are listed below:
- C++ Elements:
  - **ApplyPointsToDynMesh**: very useful function that allows you to manipulate the verts of a dynamic mesh through point data, which is by far the easiest data type to work with.   You can use this node to author vertex positions and/or vertex colors through point data. The workflow is as follows: take a dynamic mesh pin and cast to point data (which creates a point per vertex).  You then manipulate the position or color of those points from any pcg node.   Then feed the original dynmic mesh pin and the manipulated points into the ApplyPointToDynMesh.   
  - **SetMaterial**: assigns a material to the entire dynamic mesh. 
- DynMesh Blueprint Elements
  - **AutoUV**: auto uv a dynamic mesh, using any of the ue auto-uv methods
  - **ApplySelfUnion**: self unions a dynamic mesh
  - **ApplyPlanarCut**: applies a planar cut to a dyn mesh using a transform
  - **Remesh**:  dynamic mesh remesh, useful for managing tri count or adding topology
  - **MovePivot**: moves the dyn mesh pivot
- DynMesh subgraphs
  - **DynMesh_LocalToWorld**: converts a dynamic mesh's vertices from local space to world space
  - **DynMesh_WorldToLocal**: converts a dynamic mesh's vertices from world space to local space
  - **PointsToDynMesh**: creates a dynamic mesh (encapsulating the AppendMeshFromPoints) from a set of points. 
  - **RecenterPivot**: recenter a dyn mesh pivot around it's geometry's center or bottom
  - **DynMesh_SeparateOnBounds**: splits a dynamic mesh into 2 based on the verts or tris that overlap a set of points.  Useful when you want to treat specific areas of a dynamic mesh differently.

