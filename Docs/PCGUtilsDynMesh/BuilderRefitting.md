# Builder orientation and refitting

Primitive Builders expose Fitting > Orientation, using the PCGEx axis-order and rotation-construction icon controls (ported assets, MIT). Identity XYZ / XY preserves the default orientation. Axis Order permutes the target axes; Rotation Construction selects which remapped axes determine the rotation. Two-axis construction retains deterministic roll. Odd permutations correct the unused axis sign rather than mirroring geometry. Bounds, including asymmetric minima and maxima, are inverse-remapped so the physical fitting volume stays fixed.

Padding is applied in the original target axes before remapping. Scale and alignment then use the remapped axes: after ZYX, primitive Z follows target X (with its resolved sign). These are local reference axes, not world axes. Local Transform remains an additional transform; it is not the orientation swizzle.

## Builder | Refit

Connect one recipe to **Builder**. The entire completed result is fitted and aligned as one object; internal primitive settings are retained. Scale defaults to None. Enable fitting to resize the assembly. An optional **Target** builder supplies reference bounds and is not included in output. Without Target, fitting uses the incoming fitting target, initially the original seed.

A compound leg can feed four Refit nodes with different X/Y From/To anchors. Gather those outputs for realization or further composition. Refit creates a new recipe; evaluating one branch does not modify the source or another branch. Material assignments, vertex/triangle IDs, mesh attributes and active selection membership survive whole-result placement. There is no Selector pin because placement must apply to the complete object.

## Builder | Retarget

Connect the recipe to **Builder**, and its reference recipe to **Target**. Target is evaluated in the incoming context. Its geometry is measured in its rigid Builder frame by default; disable **Use Target Frame** to retain the incoming reference axes. The source recipe is then evaluated with those bounds and frame as its effective fitting target. Its leaf scale and alignment settings still apply. Target Orientation and Target Padding change that scoped target. The original seed data/index and original seed transform/bounds are retained separately.

Without a Target connection this node decorates the incoming target with orientation and padding. Nested nodes scope their changes to their own source subtree. Reference recipes must be upstream, acyclic expressions; Target accepts exactly one recipe. Empty reference geometry is an error, not a fallback to seed bounds. Target geometry is measured from vertices in the chosen frame, not by rotating an actor-space AABB.

For an arch at a tower's local X front: place the doorway box against the tower; target that box from a Retarget node wrapping the cylinder; choose ZYX with XY construction, then configure the cylinder's alignment in the remapped axes. To overlap the box with the lower half of the cylinder, place the cylinder's radial center at the box's top. Signed axes can reverse which Min/Max is the top: with ZYX/XY, new X follows old +Z, so X To Max reaches the doorway top.

Radius sizing remains manual. Filling both radial axes independently can produce an ellipse; linked two-axis uniform fitting is deliberately deferred.

## Validation

Automation tests cover all 54 order/construction combinations preserving asymmetric bounds under a rotated, nonuniformly scaled frame, source recipe reuse at different anchors, and builder-target swizzled filling. The existing Builder suite covers deferred transforms and composition. The editor target is built with DisableAdaptiveUnity to check newly added source files in unity compilation.
