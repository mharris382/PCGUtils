"""Copy legacy path fields into PathData for components in the current editor world.

Run from Unreal's Python console with:
    exec(open(unreal.Paths.project_plugins_dir() +
         "PCGUtils/Content/Python/migrate_path_component_data.py").read())

Only actors currently loaded in the editor world are processed. This matters for
World Partition levels: load the desired cells before running the script.
"""

import unreal


COMPONENT_CLASSES = (
    unreal.ShapePathComponent,
    unreal.PCGSplineComponent,
)


def copy_legacy_path_data_in_current_scene() -> int:
    """Migrate all supported components and return the number processed."""
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actor_subsystem.get_all_level_actors()
    migrated_count = 0

    with unreal.ScopedEditorTransaction("Copy Legacy PCG Path Data"):
        for actor in actors:
            if not unreal.SystemLibrary.is_valid(actor):
                continue

            for component_class in COMPONENT_CLASSES:
                for component in actor.get_components_by_class(component_class):
                    if not unreal.SystemLibrary.is_valid(component):
                        continue

                    component.copy_legacy_path_data()
                    migrated_count += 1

                    unreal.log(
                        "Copied legacy path data: {} ({})".format(
                            component.get_path_name(),
                            component_class.__name__,
                        )
                    )

    unreal.log(
        "PCG path-data migration complete: {} component(s) processed across "
        "{} loaded actor(s).".format(migrated_count, len(actors))
    )
    return migrated_count


if __name__ == "__main__":
    copy_legacy_path_data_in_current_scene()
