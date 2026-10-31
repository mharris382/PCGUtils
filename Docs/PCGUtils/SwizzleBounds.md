# Point | Swizzle Bounds

Exchanges two local axes of every point **without moving the box the point occupies**. Rotation, scale and
bounds all change; the eight world-space corners of each point's box stay where they were.

Typical use: a fit-to-bounds step (e.g. PCGEx mesh fitting) puts a mesh into the right box but along the wrong
axis - a plank laid across a dock instead of along it. Swizzle the points first, and the box's long side moves
from one axis label to the other, so the fitted mesh comes out turned correctly.

## Pins

| Pin | Type | Notes |
| --- | --- | --- |
| In | Point | Points to swizzle. |
| Out | Point | One output per input: the input, duplicated, with only transforms and bounds changed. Metadata (all domains), tags, other point properties and the target actor are preserved. |

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| Axes | `X <> Y` | Which pair to exchange: `X <> Y` (turns about Z), `X <> Z` (about Y), `Y <> Z` (about X). |
| Flip | off | Turns the frame the other way. The box is identical either way; the fitted mesh ends up rotated 180 degrees about the third axis. |
| Preserve | Point Location | **Point Location** keeps the point's world position; an off-center bounds offset turns with the axes. **Local Center** keeps the bounds' local center values (e.g. `(5,0,0)` stays `(5,0,0)`) and moves the point's location to compensate. Same result for centered bounds. |

All three are overridable.

## How it works

For pair (A, B), new local axis A points along old +B and new local axis B along old -A (both negated when
Flip is on). That is one swap plus one negation, so the result is always a proper 90 degree rotation - never a
mirror. Scale components move with their axes, and bounds are remapped (with min/max exchanged on the negated
axis). Applying the same swizzle four times returns the original point.

The math is exposed as `PCGUtilsSwizzleBounds::Apply` for reuse in C++.

## Tests

`PCGUtils.Points.SwizzleBounds.*` - world box unchanged for every axes/flip/preserve combination, location vs
local-center invariants, four-turn identity, and metadata/tag/property preservation through the node.
