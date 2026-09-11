# PCGUtils Repository Guidance

These rules apply to the whole PCGUtils plugin repository. Module-specific guidance may add constraints; for
`PCGUtilsDynMesh`, read `Source/PCGUtilsDynMesh/claude.md` and `Docs/PCGUtilsDynMesh.md` before editing.

## PCGUtilsDynMesh principles

- DynMesh processing must feel PCG-native: unified pins, overrideable settings, useful typing/colors, predictable
  categories, and consistent PCG-world/DynMesh-local coordinate handling.
- Expose Geometry Script comprehensively. When Geometry Script lacks a useful operation, a faithful,
  license-compatible Blender-derived implementation is preferred to reinventing the algorithm.
- Every operation should support DynMesh Selection data and the optional Selector input unless partial
  application is semantically invalid. Document exceptions.
- A node that consumes DynMesh/Selection data must derive from `UPCGUtilsDynMeshProcessBaseSettings` unless it is
  a selection-authoring/filter node with a more specific selection base. Specialized executors must resolve input
  through `FPCGUtilsDynMeshProcessFunctions` or `FPCGUtilsMeshTargetFunctions::CreateTarget(..., Settings)`.
- Selection-only operations override `RequiresSelection()`. Domain-specific operations override
  `GetRequiredSelectionDomain()` and rely on shared conversion.
- Whole-mesh-only Geometry Script operations use `FPCGUtilsMeshTargetHandle` and the restoration method matching
  their result: region reinsertion for topology, vertex-position restoration for deformation, or a future
  attribute-domain compositor.
- Selection modifiers such as Expand, Contract, Select Connected, and Select Boundary derive from
  `UPCGUtilsDynMeshSelectionOperationSettings`. One element must support both materialized `Selection` mode and
  reusable `Selector` decorator mode; do not create parallel standalone/provider nodes.
- Do not derive a DynMesh process directly from `UPCGSettings`/`IPCGElement`, hand-roll mesh/selection copying, or
  add a Selector pin that the executor does not actually consume.

## Naming

Use `DynMesh`, not `DynamicMesh`, in all PCGUtils-owned filenames, identifiers, node names/titles, categories,
pin/data display names, and documentation. Keep official engine names such as `UDynamicMesh`, `FDynamicMesh3`,
`UPCGDynamicMeshData`, and Geometry Script APIs unchanged. For existing reflected classes, prefer user-facing
display-name changes first; perform C++ renames only with appropriate Core Redirects.

`GC` and `GeometryCollection` split by audience, in opposite directions to the `DynMesh` rule.

**User-facing text uses `GC`**: node titles, categories, pin labels, data-type display names, and attribute
prefixes (`GC_BoneIndex`). Spelled out, titles become unusably long - `Geometry Collection Bones To Points`
versus `GC Bones To Points` - and those titles are what users type in the palette.

**C++ uses the full `GeometryCollection`**: class names, filenames, namespaces. `UPCGGeometryCollectionBonesToPointsSettings`,
not `UPCGGCBonesToPointsSettings`. The abbreviation is ambiguous in source - `GC` reads as garbage collection
to anyone who has not seen the node names - and C++ identifiers are read far more often than they are typed.
Spelling it out in code is also what makes the short display name unambiguous when someone goes looking for the
class behind a node.

Keep official engine names such as `FGeometryCollection` and `FManagedArrayCollection` unchanged. Put synonyms
in `UCLASS(meta=(Keywords="..."))` rather than `GetNodeTitleAliases()`, which adds a duplicate palette entry
per alias.

`Factory` is an internal C++ implementation term only. Never expose it in node titles, pin labels, tooltips,
data-type display names, errors, or end-user documentation. Use the family term: `Selector`, `Builder`, or `Painter`. Existing
reflected C++ types containing `Factory` remain unchanged until a redirect-backed compatibility migration exists.

In `PCGUtilsFracture` the equivalent user-facing pin labels are `GC`, `Fracture` and `Selection`.

Name explicit mesh-element correspondence by its key, not its container: `by Vertex ID` / `Vertex IDs` for
point attributes identifying vertices. Do not call this pattern merely `From Points`. Spatial influence is a
different operation: `Bounds Brush Painter` uses point bounds and falloff, not vertex correspondence. Keep
`point`/`points` as search keywords for both patterns; do not add duplicate palette aliases for synonyms.

### Node titles and subtitles are a space budget

A PCG node is as wide as its widest line of text, so every character in a title or subtitle costs graph area and
buys fewer nodes on screen. Treat brevity as a feature of the node, not a nicety: **say what the title does not
already say, and stop.**

- Never put a fully-qualified enum value in a subtitle. `UEnum::GetValueAsString` returns
  `EGeometryScriptBooleanOperation::Union`, which under a title already reading `DynMesh | Boolean` roughly
  doubles the node's width to tell you nothing. Use `UEnum::GetDisplayValueAsText(Value).ToString()`, which
  gives `Union` and matches the label in the details panel.
- A subtitle earns its place by showing the one setting someone reads the graph to check - the slice grid, the
  site count, the blend mode. `GetAdditionalTitleInformation()` returning an empty `FString` for the default
  case is the right answer, not a fallback: see `GC | To DynMesh`, which labels only `Per Piece`.
- Do not repeat the family prefix, the data type, or the word the title already carries. `Union`, not
  `Boolean Union`; `2x2x2`, not `Slices 2x2x2`.
- The same applies to the title itself, which is why the element name never repeats its category.

### Context-menu naming

The palette is the only map users have of this library, and PCG gives a native element no free-form category
string: `UPCGEditorGraphSchema::GetNativeElementActions` derives the category solely from
`StaticEnum<EPCGSettingsType>()->GetDisplayNameTextByValue(GetType())`. There is no per-class hook and no
extension point, so every element here lands under `Dynamic Mesh` and **the family prefix in the node title is
the grouping mechanism.** Treat the prefix as structural, not decorative.

Every user-visible palette entry - a node title *and* every `FPCGPreConfiguredSettingsInfo::Label` - takes the
form `[CATEGORY] | [ELEMENT_NAME]`, with a space on **both** sides of every pipe. The spaces are part of the
convention: `DynMesh | Extrude Faces`, never `DynMesh|Extrude Faces`. (They cost nothing in search - the engine
splits the search text on spaces and concatenates the words with no separator, so both forms glob to the same
`dynmesh|extrudefaces`.)

| Family | Form | Examples |
| --- | --- | --- |
| DynMesh process | `DynMesh \| [PROCESS]` | `DynMesh \| Extrude Faces`, `DynMesh \| Remesh` |
| Primitive builder | `Builder \| [SHAPE]` | `Builder \| Box`, `Builder \| Cylinder` |
| DynMesh selection | `Select \| [NAME]` | `Select \| In Bounds`, `Select \| By Normal` |
| Painter | `Painter \| [NAME]` | `Painter \| Bounds Brush`, `Painter \| Blend` |
| Fracture element | `GC \| [NAME]` | `GC \| Fracture`, `GC \| Prune` |
| Fracture factory | `Fracture \| [TYPE]` | `Fracture \| Planar`, `Fracture \| Brick` |
| GC selection | `GC \| Select \| [NAME]` | `GC \| Select \| Contact`, `GC \| Select \| Parent` |

The fracture factories are the one family that splits off the module's usual `GC | ` prefix: they are the
operations that plug into `GC | Fracture`'s `Fracture` pin (`Fracture | Planar`, `Fracture | Slice`,
`Fracture | Brick`, `Fracture | Uniform Voronoi`, `Fracture | Voronoi From Points`), grouped together under
`Fracture | ` since fracture behavior is implicitly GC-related. `GC | Fracture` itself is the executor, not a
factory, and keeps the `GC | ` prefix. A factory's title carries no `GC`/`Geometry Collection` text, but its
`Keywords` metadata still does, so a "GC" search continues to return the whole module.

**The name never repeats its family.** `DynMesh | Extrude Faces`, not `DynMesh | Extrude DynMesh Faces`. A
selection name contains none of `select`, `selection` or `selector`: `Select | In Bounds`, not
`Select | Select in Bounds`. A GC selection name additionally omits `GC`; a fracture factory name additionally
omits `Fracture` (`Fracture | Planar`, not `Fracture | Planar Fracture`). A compact node's title - the text drawn
on the node when `ShouldShowCompactNodeTitle()` is true and no `GetCompactNodeIcon()` is supplied - follows the
same exclusions and carries no prefix, because the pins already say what it operates on.

Search is what the prefixes buy, and it behaves in one specific way worth knowing before inventing keywords.
`FEdGraphSchemaAction::UpdateSearchText` globs the entry's title, its `Keywords` metadata and its category into
one lowercased string, splitting each on spaces and concatenating the words with **no separator**;
`SGraphActionMenu` then requires every space-separated search term to appear as a substring. So:

- an element whose title lacks the family word needs it in `Keywords` - this is why every `Select | `, `Builder | `
  and `Painter | ` entry carries `DynMesh` as a keyword, so one search returns the whole library;
- a term can straddle two adjacent words, so check a new keyword list for accidental substrings. In
  particular nothing in a `Select | ` entry may contain `gc`, and nothing in a `GC | Select | ` entry may contain
  `dynmesh`, or the two families stop being separable by search;
- the `Dynamic Mesh` category contributes `dynamicmesh`, which deliberately does not contain `dynmesh`.

`PCGUtils.Palette.SearchContract` (in `PCGUtilsFracture/Private/Tests`) reimplements that search model and
asserts the whole contract. Extend it when you add a family; do not hand-verify in the editor instead.

One more consequence of the category rule: an element deriving straight from `UPCGSettings` must override
`GetType()`. Forgetting it is silent and drops the node into `Generic`, where nobody will find it - this is
exactly how the Builder materializer went missing until it was rebuilt as `DynMesh | Realize Builders`.

## Attributes written to PCG data

Two rules, both about the user being able to see and control what a node produces.

**1. Every attribute a node writes must expose its name as a setting.** No exceptions. An attribute whose name
lives only in C++ is undiscoverable: the user has to run the graph, open the attribute table and read the names
back before they can filter on anything. That is a bad enough experience on its own, and it also silently
prevents the name from being changed to avoid a collision. Exposing the name fixes both, and doubles as
documentation - the details panel becomes the list of what this node can produce.

**2. One flag per attribute, with the name gated behind it.** Never a single flag that bulk-writes several
attributes: the user cannot tell what they are getting, cannot see what any of them are called, and pays for
ones they did not want. Each optional attribute gets its own `bOutput<Thing>` boolean plus its own
`<Thing>AttributeName`, with the name hidden until the flag is on:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface", meta=(PCG_Overridable))
bool bOutputExposureRatio = false;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface",
    meta=(PCG_Overridable, EditCondition="bOutputExposureRatio", EditConditionHides))
FName ExposureRatioAttributeName = TEXT("GC_ExposureRatio");
```

Default optional attributes to **off**. Skip the work that computes them when nothing that needs it is enabled,
and warn on the graph if a flag is on but its name is `None`.

The one permitted exception is an attribute that forms a hard contract with another node - one that, if absent,
makes the data useless to its intended consumer. Such an attribute may be written unconditionally, but its name
must still be exposed so it can be matched against the consuming node. Say so in the property comment.

**3. Never expose uninitialized point metadata keys.** When `UPCGPointArrayData::SetNumPoints(..., false)` is
followed by a mutable metadata-entry range, set every new entry to `PCGInvalidEntryKey` before calling
`InitializeOnSet()`, or initialize point values when sizing the array. `InitializeOnSet()` does not repair an
arbitrary nonnegative value left by uninitialized storage, which can produce unreadable attributes and the graph
warning that an output does not have valid point metadata.

## Unity builds

A module's `.cpp` files are concatenated into one translation unit (`Module.<Name>.cpp`) unless something pulls
them apart, so **an anonymous namespace is module-wide, not file-local.** Two files declaring a same-named
file-local helper are a redefinition error in that blob.

This does not reliably show up while you are working, because UBT's *adaptive* unity build excludes files in the
working set - the ones you just edited - and compiles them standalone. A file you are actively changing is
therefore the one file whose collisions you cannot see. The same code then fails for someone who syncs it and
builds clean, which is exactly how a collision between two pre-existing files can appear to be "caused" by an
unrelated change: adding `.cpp` files to a module repartitions the blobs.

So:

- Name a file-local helper for its subject, not its shape. `BoneSelectionModeFromPreconfiguredIndex`, not
  `ModeFromPreconfiguredIndex`; the generic name is the one another file will also want.
- Keep `using namespace` out of an anonymous namespace. It leaks into the whole blob, and what it makes
  ambiguous is some other file's code.
- A member function of a class declared in an anonymous namespace is scoped to that class and is safe -
  `TestElement`, `Evaluate` and `Initialize` repeat freely across the selector and painter factories.

**Before pushing a change that adds or removes module `.cpp` files, build once with `-DisableAdaptiveUnity`.**
That forces every file into its blob and is the only local build that sees what a fresh checkout sees.

## Validation

Build the affected Unreal target after C++ changes. Inspect graph pins for process nodes: the primary input must
accept DynMesh and DynMesh Selection data, and `Selector` must be present and functional unless the node
is a documented exception.
