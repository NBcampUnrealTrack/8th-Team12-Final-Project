# Zen UV — operator reference

Enumerated from Blender's RNA on a licensed install: **Zen UV 5.3.3**, Blender 5.0.0. 252 operators, 33 keymap entries.

These IDs are verified, not inferred. If an ID is not in this file, do not use it — re-run the introspection snippet in `SKILL.md` against the installed version.

## Unwrap and marking (28)

| Operator | Purpose |
|---|---|
| `mesh.zenuv_mirror_seams` | Mirror Seams by axes |
| `mesh.zenuv_select_seams` | Select Edges Marked as Seams |
| `mesh.zenuv_select_sharp` | Select Edges Marked as Sharp |
| `uv.zenuv_activate_seam_group` | Set Seams from active Seam Group |
| `uv.zenuv_assign_seam_to_group` | Assign Seams to active Seam Group |
| `uv.zenuv_auto_mark` | Mark edges as Seams and/or Sharp edges by Angle |
| `uv.zenuv_auto_uv_unwrap` | Automated UV unwrapper based on utility, written by Eskil Steenberg www.ministryofflat.com |
| `uv.zenuv_mark_seams` | Mark selected edges or face borders as Seams and/or Sharp edges |
| `uv.zenuv_prepare_unwrap` | Remove all UV maps, seams, sharp edges, and finished UV marks. Apply all geometry modifications before resetting for a clean unwrap |
| `uv.zenuv_proxy_zenunwrap_all_selected` | Used for Unwrap in case all faces selected in close object |
| `uv.zenuv_seams_by_open_edges` | Mark Seams by Open Edges. The way that looks in the viewport |
| `uv.zenuv_seams_by_sharp` | Mark Seams by existing Sharp edges |
| `uv.zenuv_seams_by_uv_borders` | Mark Seams by existing UV Borders |
| `uv.zenuv_select_loop` | Selects an edge loop starting from a given edge, and stops at seam edges. Useful for selecting edge loops without crossing UV seams |
| `uv.zenuv_sharp_by_seams` | Mark Sharp edges by existing Seams |
| `uv.zenuv_sharp_by_uv_islands` | Mark Sharp by existing UV Borders |
| `uv.zenuv_unified_mark` | Mark and Convert Seams and Sharp |
| `uv.zenuv_unmark_all` | Remove all the Seams and/or Sharp edges from the mesh |
| `uv.zenuv_unwrap` | Unwrap by Marked edges. If you have selected edges or faces they will be Marked as Seams and/or Sharp edges and Unwrapped after. Objects without Seams and Edge borders will receive Isometric Projection |
| `uv.zenuv_unwrap_for_tool` | UWRP Processor |
| `uv.zenuv_unwrap_inplace` | Performs a UV unwrapping of selected polygons or a selected island while preserving their dimensions and orienting them to match their world orientation |
| `view3d.zenuv_set_smooth_by_sharp` | Toggle between Auto Smooth 180° (with sharp edges) and regular smooth modes |
| `wm.zenuv_auto_unwrap_operator_preset_add` | Add or remove an Operator Preset |
| `wm.zenuv_auto_uv_unwrap_install` | Download and install auto uv unwrapper from https://www.quelsolaar.com/ministry_of_flat/ |
| `zenuv.pie_caller_bottom` | Click to open Popup |
| `zenuv.pie_caller_left` | Click to open Popup |
| `zenuv.pie_caller_right` | Click to open Popup |
| `zenuv.pie_caller_top_right` | Click to open Popup |

## Selection (53)

| Operator | Purpose |
|---|---|
| `mesh.zenuv_select_cylinder_cap` | Select the caps of cylinder meshes. Selects faces or boundary edges depending on mesh selection mode |
| `mesh.zenuv_select_cylinder_faces` | Select connected quad faces that form a cylindrical surface. Useful for detecting cylindrical geometry in meshes. Selects faces or boundary edges depending on mesh selection mode |
| `mesh.zenuv_select_faces_by_normal` | Select polygons by reference normal |
| `mesh.zenuv_select_material_boundary_edges` | Selects all edges where the adjacent faces have different material assignments |
| `object.zenuv_select_doubled_vertices` | Selects vertices that have the same coordinates |
| `object.zenuv_select_edges_with_multiple_loops` | Select edges with Multiple Loops |
| `object.zenuv_select_edges_without_faces` | Select edges without faces |
| `object.zenuv_select_elements_by_index` | Select elements by their indices |
| `object.zenuv_select_free_vertices` | Selects vertices that have no connected faces or edges |
| `uv.zenuv_assign_selection_to_group` | Creates seams from the selection and assigns them to the active group |
| `uv.zenuv_filter_islands_by_property` | Filter and select UV islands based on specific properties such as height, width, or aspect ratio, and divide them into ranges using customizable methods |
| `uv.zenuv_grab_sel_area` | Get the area of the selected. This value is used in the 'Select by UV Area' operator |
| `uv.zenuv_isolate_island` | Isolate Islands (Toggle) by selected edge/face of the Islands |
| `uv.zenuv_isolate_part` | Isolate mesh part (Toggle) by selected edge/face |
| `uv.zenuv_paste_object_uv` | Paste the UV coordinates |
| `uv.zenuv_select_by_current_texel_density` | Select the islands by texel density, which is displayed in the value in the panel |
| `uv.zenuv_select_by_texel_density` | Select islands by custom texel density |
| `uv.zenuv_select_by_uv_value` | Select faces by their UV values |
| `uv.zenuv_select_edges_by_direction` | Select edges by direction in UV space |
| `uv.zenuv_select_empty_objects` | Selects objects that do not contain faces |
| `uv.zenuv_select_faces_less_than_pixel` | Selects faces with an area less than the specified number of pixels |
| `uv.zenuv_select_finished` | Select Islands tagged as Finished |
| `uv.zenuv_select_flipped` | Select Flipped Islands |
| `uv.zenuv_select_half` | Selects a part of the model according to its location relative to the coordinate axis |
| `uv.zenuv_select_holed_islands` | Selects islands that have holes within their geometry |
| `uv.zenuv_select_in_tile` | Selects islands depending on their location bounding box relative to the UV Area or UDIM tile |
| `uv.zenuv_select_island` | Select Islands by selected edge/face of the Islands |
| `uv.zenuv_select_island_by_direction` | Select islands by their orientation: horizontal, vertical, radial, or not aligned to any axis |
| `uv.zenuv_select_island_by_trim` | Select Islands inside active Trim |
| `uv.zenuv_select_island_by_trim_name` | Select Islands inside Trim defined by name |
| `uv.zenuv_select_islands_by_size` | Select Islands by their size |
| `uv.zenuv_select_linked_loops` | Selects all loops belonging to the mesh vertex based on any already selected loop |
| `uv.zenuv_select_m_stack` | Select Islands in the Stack |
| `uv.zenuv_select_open_edges` | Select open edges. The way that looks in the 3d viewport |
| `uv.zenuv_select_pack_excluded` | Select Islands tagged as Excluded from Packing |
| `uv.zenuv_select_quaded_islands` | Selects islands that consist only of quads |
| `uv.zenuv_select_self_intersecting` | Select faces that have intersecting uv edges |
| `uv.zenuv_select_similar` | Select Islands similar to selected |
| `uv.zenuv_select_splits_edges` | Selects island edges that belong to the same mesh edge and split the island by itself |
| `uv.zenuv_select_stack` | Select Stack Parts |
| `uv.zenuv_select_stacked` | Clear Selection and Select Stacked Islands * (Zen Modifier Key) - Append to existing Selection |
| `uv.zenuv_select_stretched_faces` | Selects faces with stretched UV coordinates |
| `uv.zenuv_select_uv_borders` | Select existing UV Borders |
| `uv.zenuv_select_uv_overlap` | Select Overlapped Islands |
| `uv.zenuv_select_zero_area_faces` | Selects faces with zero UV area. |
| `uv.zenuv_selection_to_circle` | Transform selection to the circular shape |
| `uv.zenuv_sync_select` | Keep UV and edit mode mesh selection in sync, showing all islands of unhided mesh elements. * Edges and Faces - only supported! |
| `uv.zenuv_tool_transform_fit_to_selection` | Fit UV transform gizmo to islands or selection |
| `uv.zenuv_trim_select_by_face` | Select and make Trim Active by selected Face |
| `wm.zenuv_palette_select_color` | Select this color |
| `zenuv.pie_caller_top` | Click to open Popup |
| `zenuv.pie_caller_top_left` | Click to open Popup |
| `zenuvlist.entry_remove` | Remove the selected entry from the list |

## Transform (65)

| Operator | Purpose |
|---|---|
| `uv.zenuv_align` | Align selected Islands or Selection |
| `uv.zenuv_align_grab_position` | Grab Position |
| `uv.zenuv_align_to_trim` | Align Islands to active Trim |
| `uv.zenuv_arrange_transform` | Arrange selected islands |
| `uv.zenuv_distribute_islands` | Distributes and sorts selected islands |
| `uv.zenuv_distribute_verts` | Distribute vertices along the line |
| `uv.zenuv_fit` | Fit into Region |
| `uv.zenuv_fit_grab_region` | Grab Region |
| `uv.zenuv_fit_hide_region` | Hide Fit region |
| `uv.zenuv_fit_region` | Fit into Region |
| `uv.zenuv_fit_show_region` | Show region using Annotations |
| `uv.zenuv_fit_to_trim` | Fit Islands into active Trim |
| `uv.zenuv_fit_to_trim_hotspot` | Map Islands by matching Islands to predefined Trims from Trimsheet |
| `uv.zenuv_flip` | Flip selected Islands or Selection |
| `uv.zenuv_flip_in_trim` | Flip Islands relative to the center of active Trim |
| `uv.zenuv_match_stitch` | Matching the position, rotation, and scale of the island. Stitch the vertices together, if possible |
| `uv.zenuv_move` | Move selected Islands or Selection |
| `uv.zenuv_move_cursor_to` | Move 2D Cursor To |
| `uv.zenuv_move_in_trim` | Move Islands inside active Trim. Islands outside active Trim will be ignored |
| `uv.zenuv_move_to_trim` | Move selected Islands to Active Trim |
| `uv.zenuv_move_to_uv_area` | Move islands to UV area, active UDIM tile, or UDIM tile defined by number |
| `uv.zenuv_move_to_uv_area_mouseover` | Move center of the selected Islands to the UV Area using mouse position |
| `uv.zenuv_move_to_uv_position_mouseover` | Move center of the selected Islands to the UV coordinates defined by mouse |
| `uv.zenuv_orient_island` | Orient Island |
| `uv.zenuv_quadrify` | Straighten rectangular shaped Island |
| `uv.zenuv_randomize_transform` | Randomize Transformation |
| `uv.zenuv_relax` | Relax selected islands |
| `uv.zenuv_relax_along_axis` | Relaxes the selected vertices using an axis-constrained method. |
| `uv.zenuv_remove_active_udim_tile` | Remove the active UDIM tile |
| `uv.zenuv_reshape_island` | Changes the form of the island in accordance with the input parameters |
| `uv.zenuv_rotate` | Rotate selected Islands or Selection |
| `uv.zenuv_rotate_in_trim` | Rotate Islands inside active Trim |
| `uv.zenuv_scale` | Scale selected Islands or Selection |
| `uv.zenuv_scale_grab_size` | Grab size from the selection |
| `uv.zenuv_scale_in_trim` | Scale Islands inside active Trim. Islands outside active Trim will be ignored |
| `uv.zenuv_tool_snap_transform_gizmo` | Snap Zen UV tool transform gizmo to selected |
| `uv.zenuv_tool_transform` | Transform UVs using Zen UV gizmo tool |
| `uv.zenuv_tool_transform_enum` | Set enum value in transform mode |
| `uv.zenuv_tool_transform_handle_click` | Defines an action when when transform handle is clicked |
| `uv.zenuv_tool_transform_mouse_draw` | Draw UV transform gizmo using mouse |
| `uv.zenuv_tool_transform_set_perpendicular` | Set the gizmo perpendicular to its current orientation while preserving its length |
| `uv.zenuv_tool_transform_setup_modal` | Setup UV Transform Gizmo Modal |
| `uv.zenuv_tool_transform_swap_handles` | Swap transform gizmo pivot and angle handles |
| `uv.zenuv_tr_scale_tuner` | Tune Scale parameters |
| `uv.zenuv_transform_move_islands` | Move islands with no respect to the current element mode |
| `uv.zenuv_transform_rotate_islands` | Rotate islands with no respect to the current element mode |
| `uv.zenuv_transform_scale_islands` | Scale islands with no respect to the current element mode |
| `uv.zenuv_unified_transform` | Move Islands |
| `uv.zenuv_world_orient` | Rotate Islands the way they are oriented on the Models. Each method (Organic/Hard Surface) uses a heuristic approach and correctly orients most of the Islands in its area |
| `view3d.zenuv_checker_remove` | Remove Zen UV Checker Nodes from the scene materials |
| `view3d.zenuv_move_in_trim` | Move Islands inside active Trim. Islands outside active Trim will be ignored |
| `view3d.zenuv_rotate` | Rotate selected Islands or Selection |
| `view3d.zenuv_rotate_in_trim` | Rotate Islands inside active Trim |
| `view3d.zenuv_scale_in_trim` | Scale Islands inside active Trim. Islands outside active Trim will be ignored |
| `wm.zenuv_palette_color_move` | Moves active color up\|down in the active Palette |
| `wm.zenuv_palette_color_remove` | Removes active color from the active Palette |
| `wm.zenuv_trim_scroll_fit` | Scroll active trim forward-backward and fit island(s) to it |
| `wm.zenuv_trim_scroll_fit_internal` | Scroll active trim forward-backward and fit island(s) to it |
| `zenuv.pie_caller_bottom_left` | Click to open Popup |
| `zenuv.tool_area_update` | Internal Zen UV tool operator to update UV and 3D areas by hotkeys |
| `zenuv.tool_exit_create` | Exit Zen Uv tool create trims mode |
| `zenuv.tool_snap_handle` | Click to change snap pivot |
| `zenuv.tool_transform_handle` | Rotate selected islands or selection by handle direction |
| `zenuv.tool_trim_handle` | (undocumented operator) |
| `zenuvlist.entry_move` | Move an entry in the list up or down |

## Stack (8)

| Operator | Purpose |
|---|---|
| `uv.zenuv_analyze_stack` | Analyze Islands Similarities in the Stack. You can find details in the Zen UV Manual Stack Analyze document in the Text Editor |
| `uv.zenuv_assign_manual_stack` | Append selected Islands to the active Stack |
| `uv.zenuv_collect_manual_stacks` | Collect Islands on Manual Stacks |
| `uv.zenuv_simple_stack` | Places the islands in the stack, with no respect for their topology |
| `uv.zenuv_simple_unstack` | Shifting the islands in a given direction |
| `uv.zenuv_stack_similar` | Collect all similar islands on stacks |
| `uv.zenuv_unstack` | Shift Islands from stacks in the given direction |
| `uv.zenuv_unstack_manual_stack` | Shift Islands from Manual Stacks in the given direction |

## Texel density (6)

| Operator | Purpose |
|---|---|
| `uv.zenuv_bake_td_to_vc` | Bake texel density to the vertex color |
| `uv.zenuv_clear_baked_texel_density` | Remove texel density vertex color layer from object |
| `uv.zenuv_get_texel_density` | Get Texel Density from selected Islands |
| `uv.zenuv_set_texel_density` | Set Texel Density to selected Islands |
| `zenuv.get_td_range` | Calculates the current minimum and maximum TD values for the entire scene |
| `zenuv.td_calculator` | Calculates texture size or texel density for selected objects |

## Pack (8)

| Operator | Purpose |
|---|---|
| `uv.zenuv_calculate_recommended_margin` | Calculates suggested margin based on the image size |
| `uv.zenuv_get_uv_coverage` | Get UV Coverage |
| `uv.zenuv_hide_pack_excluded` | Hide islands tagged as excluded from packing |
| `uv.zenuv_offset_pack_excluded` | Move Islands tagged as Excluded from Packing out of UV Area |
| `uv.zenuv_pack` | Pack all Islands |
| `uv.zenuv_sync_to_uvp` | Transfer Pack settings from Zen UV to UVPackmaster |
| `uv.zenuv_tag_pack_excluded` | Tag Islands as Excluded from Packing |
| `uv.zenuv_untag_pack_excluded` | Untag Islands tagged as Excluded from Packing |

## Trim sheet and hotspot (7)

| Operator | Purpose |
|---|---|
| `mesh.zenuv_set_face_normal_to_trim_props` | Retrieve the normal vector from the selected face and apply it to the active trim |
| `uv.zenuv_directional_hotspot` | Hotspot mapping using island normal vector |
| `uv.zenuv_new_trim` | Add Trim to Trimsheet |
| `uv.zenuv_quick_hotspot` | The classic way to use Hotspot technology, which includes unwrap and several preprocessing steps to obtain fast results. |
| `uv.zenuv_set_active_trim_mouseover` | Set active trim using mouse position |
| `uv.zenuv_trim_to_mesh` | Create Trim representation in Mesh as single face |
| `wm.zenuv_set_props_to_trims` | (undocumented operator) |

## UV maps (2)

| Operator | Purpose |
|---|---|
| `uv.zenuv_copy_uv` | Copy the UV coordinates of the selection in active UV map |
| `uv.zenuv_paste_uv` | Paste the UV coordinates |

## Checker and display (16)

| Operator | Purpose |
|---|---|
| `ops.zenuv_checker_reset_path` | Reset Checker Library path to Default State |
| `uv.zenuv_display_finished` | Display Finished/Unfinished Islands in the viewport |
| `uv.zenuv_hide_finished` | Hide/Unhide islands tagged as finished |
| `uv.zenuv_set_viewport_display_mode` | Set viewport display mode for all selected objects |
| `uv.zenuv_tag_finished` | Tag Islands as Finished |
| `uv.zenuv_untag_finished` | Tag Islands as Unfinished |
| `view3d.zenuv_checker_append_checker_file` | Open File Browser and add the selected texture to the Checker Library |
| `view3d.zenuv_checker_collect_images` | Refresh Textures from Checker Library Folder |
| `view3d.zenuv_checker_off` | Remove checker texture from the selected mesh |
| `view3d.zenuv_checker_on` | Add checker texture to the selected mesh |
| `view3d.zenuv_checker_open_editor` | Open Shader Editor with Zen UV Checker Node |
| `view3d.zenuv_checker_reset` | Reset Zen UV Checker to Default state |
| `view3d.zenuv_checker_toggle` | Add Checker Texture to the mesh |
| `wm.zenuv_get_checker_override_image` | Get texture checker override image |
| `wm.zenuv_set_draw_mode` | Set the active draw mode for visualization |
| `zenuv.pie_caller_bottom_right` | Click to open Popup |

## UI, presets, preferences (13)

| Operator | Purpose |
|---|---|
| `ops.zenuv_show_prefs` | Set shortcuts for Zen UV menus |
| `preferences.zenuv_keymaps_resolve_collisions` | Disable all other keymap items with the same key sequence |
| `preferences.zenuv_keymaps_restore_category` | Restore Zen UV keymap category |
| `preferences.zenuv_keymaps_restore_kmi` | Restore Zen UV keymap item |
| `wm.zenuv_open_presets_folder` | Open Presets Folder |
| `wm.zenuv_presets_load_default` | Copy default presets that are shipped with the addon |
| `wm.zenuv_presets_save_operator_default` | Save default operator properties values |
| `wm.zenuv_rmb_add_to_favs` | Add to user defined ZenUV quick favourites menu bar |
| `wm.zenuv_set_texture_size_preset` | Get texture size preset from active image size |
| `zenuv.call_popup` | Calls Zen UV popup panel |
| `zenuv.reset_preferences` | Reset preferences to the Default state |
| `zenuvlist.entry_add` | Add an entry to the list after the current active item |
| `zenuvlist.entry_delete_all` | Delete all entries from the list |

## Other (46)

| Operator | Purpose |
|---|---|
| `mesh.zenuv_set_uv_name_by_pos` | Set UV map name by given index in the list of UV maps |
| `object.zenuv_calc_uv_edges_length` | Calculates the lengths of the selected edges |
| `uv.zenuv_add_missed_udim` | Creates the missing UDIM Tile at the island location. The image source must already be "UDIM Tiles" |
| `uv.zenuv_calculate_uv_pixel_size` | Calculates the size of a specified number of pixels in UV units and as a percentage |
| `uv.zenuv_clear_mesh_attrs` | Clear mesh attributes used in the Zen UV. Finished and Excluded |
| `uv.zenuv_convert_sel_edges_to_sel_loops` | Convert selected mesh edges to UV loops and vice versa |
| `uv.zenuv_convert_sel_faces_to_sel_loops` | Convert selected mesh faces to UV loops and vice versa |
| `uv.zenuv_copy_object_uv` | Copy active UV map of the active object to clipboard |
| `uv.zenuv_copy_param` | Copy parameters of selected Islands/Faces and save them |
| `uv.zenuv_correct_self_intersecting` | Correct faces that have intersecting uv edges by re-unwrapping its island |
| `uv.zenuv_get_island_size` | Get the size of the island in units or pixels |
| `uv.zenuv_get_world_size` | Get the texture size in real units |
| `uv.zenuv_grab_offset` | Get the distance between two vertices or edge lendth and use it as the offset value for the move |
| `uv.zenuv_island_counter` | Count UV islands in selected objects and display the result |
| `uv.zenuv_islands_sorting` | Sort Islands by Tags. Finished Islands move to the right side from Main UV Tile, Unfinished - to the left |
| `uv.zenuv_math_vis` | Add vector or group of vectors to annotation for debugging math purposes |
| `uv.zenuv_merge_uv_verts` | Merge UV vertices belonging to the same mesh vertex |
| `uv.zenuv_mirror_uv` | Mirroring UV coordinates in a mirrored mesh |
| `uv.zenuv_paste_param` | Paste the parameters saved earlier to selected Islands/Faces |
| `uv.zenuv_rectify` | Transforms the selected island into a rectangular shape |
| `uv.zenuv_round_uv_coordinates` | Rounds the value of each UV coordinate to the specified value |
| `uv.zenuv_set_active_udim` | Sets the active UDIM tile based on the selection |
| `uv.zenuv_set_active_udim_mouseover` | Sets the active UDIM tile over which the mouse cursor is positioned. |
| `uv.zenuv_set_island_size` | Set the size of the island in units or pixels |
| `uv.zenuv_set_world_size` | Sets the size of the island so that the texture of this island in the scene fills the size specified in the Size field |
| `uv.zenuv_show_island_area` | Calculates and show selected islands mesh area and UV Area |
| `uv.zenuv_show_sim_index` | Show Sim Index of the selected island |
| `uv.zenuv_split` | Splits selected in the UV |
| `uv.zenuv_unregister_library` | Library will be deleted from application memory, so it will be available to be deleted from disk or reinstalled |
| `uv.zenuv_update_addon` | Update Zen UV add-on |
| `view3d.zenuv_check_library` | Check Zen UV Dynamic Library |
| `view3d.zenuv_install_library` | Install and Register Zen UV Dynamic Library |
| `view3d.zenuv_math_vis` | Add vector or group of vectors to annotation for debugging math purposes |
| `wm.zenuv_export_to_json` | Export RNA struct to json file |
| `wm.zenuv_import_from_json` | Import RNA struct from json file |
| `wm.zenuv_operator_defaults` | Set operator to its default values |
| `wm.zenuv_palette_add_new` | Adds New Palette and sets as Active |
| `wm.zenuv_palette_color_add` | Adds color to active Palette |
| `wm.zenuv_palette_color_assign_to_group` | Assign Palette Color to active or all Group(s) |
| `wm.zenuv_palette_color_sort` | Sort Palette Colors By: Hue, Saturation, Value, Luminance |
| `wm.zenuv_reload_icons` | Reload and check all Zen UV icons and optionally show the preview |
| `wm.zenuv_report` | (undocumented operator) |
| `wm.zenuv_script_exec` | (undocumented operator) |
| `wm.zenuv_show_addon_version_info` | Open Zen UV version information from the official website |
| `wm.zenuv_text_exec` | (undocumented operator) |
| `wm.zenuv_update_toggle` | Toggles value with updating viewports |

---

## Key operators in detail

### `uv.zenuv_unwrap`

Unwrap by Marked edges. If you have selected edges or faces they will be Marked as Seams and/or Sharp edges and Unwrapped after. Objects without Seams and Edge borders will receive Isometric Projection

| Property | Type | Default | Meaning |
|---|---|---|---|
| `action` | enum | `DEFAULT` — `DEFAULT` / `AUTO` / `CONTINUE` |  |
| `UnwrapMethod` | enum | `ANGLE_BASED` — `ANGLE_BASED` / `CONFORMAL` / `MINIMUM_STRETCH` |  |
| `ProcessingMode` | enum | `WHOLE_MESH` — `SEL_ONLY` / `WHOLE_MESH` / `SEAM_SWITCH` |  |
| `packAfUnwrap` | boolean | True | Execute Pack after Zen Unwrap operation. Not available in the Selected Only mode |
| `post_td_mode` | enum | `SKIP` — `AVERAGED` / `GLOBAL_PRESET` / `SKIP` | Set texel density. Not available in the Seam Switch mode or if Pack Unwrapped is On |
| `fill_holes` | boolean | True | Virtual fill holes in mesh before unwrapping, to better avoid overlaps and preserve symmetry |
| `correct_aspect` | boolean | True | Map UVs taking image aspect ratio into account |
| `markSeamEdges` | boolean | False | Automatically assign Seams |
| `markSharpEdges` | boolean | False | Automatically assign Sharp edges |
| `unwrapAutoSorting` | boolean | False | Sort Islands by Tags after Zen Unwrap operation. Finished Islands move to the right side from Main UV Tile, Unfinished - to the left |

### `uv.zenuv_auto_uv_unwrap`

Automated UV unwrapper based on utility, written by Eskil Steenberg www.ministryofflat.com

| Property | Type | Default | Meaning |
|---|---|---|---|
| `force_hard_surface` | boolean | False | Makes the algorithm treat the surface as Hard Surface, ensuring precise handling of sharp edges and rigid structures |
| `separate_by_material` | boolean | False | Mesh is splitted by materials before unwrapping |
| `separate_sharp_edges` | boolean | False | Mesh is splitted by sharp edges before unwrapping |
| `triangulate_mesh` | boolean | False | All ngons with four or more vertices will be triangulated |
| `triangulate_ngon_method` | enum | `CLIP` — `BEAUTY` / `CLIP` | Method for splitting the n-gons into triangles |
| `triangulate_quad_method` | enum | `BEAUTY` — `BEAUTY` / `FIXED` / `FIXED_ALTERNATE` / `SHORTEST_DIAGONAL` / `LONGEST_DIAGONAL` | Method for splitting the quads into triangles |
| `auto_detect_hard_edges` | boolean | False | UV unwrapper will try to separate all hard edges. Useful for lightmapping and Normalmapping |
| `use_normal` | boolean | False | Use the models normals to help classify polygons |
| `overlap_identical_parts` | boolean | False | Overlap identtical parts to take up the same texture space |
| `overlap_mirrored_parts` | boolean | False | Overlap mirrored parts to take up the same texture space |
| `world_scale` | boolean | True | Scales the UVs to match their real world scale going beyound the zero to one range |
| `use_texel_density` | boolean | False | Scales the UVs to match the texel density value |
| `texel_density` | float | 1024 | Texel Density value |
| `TD_TextureSizeX` | int | 1024 | Image Size for Texel Density computation, X axis |
| `TD_TextureSizeY` | int | 1024 | Image Size for Texel Density computation, Y axis |
| `td_im_size_presets` | enum | `1024` — `16` / `32` / `64` / `128` / `256` / `512` / `1024` / `2048` / `4096` / `8192` | Image Size Preset |
| `mark_seam_edges` | boolean | True | Automatically assign seams |
| `correct_self_intersecting` | boolean | True | Correct uv faces that have self-intersecting uv edges |
| `stretch` | boolean | False | Stretch any island that is too wide to fit in the image |
| `packing` | boolean | True | Pack islands in to a rectangle |
| `cut` | boolean | False | Cut down awkward shapes in order to optimize layout coverage |
| `squares` | boolean | False | Finds various individual polygons that have right angles |
| `quads` | boolean | True | Searches the model for triangle pairs that make good quads. Improves the use of patches |
| `weld` | boolean | False | Merges duplicate vertices, Does not efect the out put polygon or vertext data |
| `debug_info` | boolean | False | Enables terminal output of detailed diagnostics during auto unwrap processing |
| `extra_cmd` | string |  | Extended command line arguments that can be passed to the uv unwrapping application |

### `uv.zenuv_prepare_unwrap`

Remove all UV maps, seams, sharp edges, and finished UV marks. Apply all geometry modifications before resetting for a clean unwrap

| Property | Type | Default | Meaning |
|---|---|---|---|
| `prepare_uvs` | enum | `CREATE_ISO` — `REMOVE_ALL` / `CREATE_ISO` / `SKIP` | Choose how to prepare the UV maps |
| `clear_seams` | boolean | True | Remove all marked UV seams from mesh edges before preparing for a new unwrap |
| `clear_sharps` | boolean | True | Remove all sharp edge markings from the mesh before preparing for a new unwrap |
| `tag_unfinished` | boolean | True | Remove all finished island marks and tag the mesh as unfinished for a fresh unwrap |
| `apply_scale` | boolean | False | Apply object scale transformations before preparing the mesh for UV unwrapping |
| `apply_modifiers` | boolean | False | Apply all geometry modifiers to the mesh before preparing for UV unwrapping |
| `display_seams` | boolean | True | Display UV unwrapping seams and switch off all other overlays (Crease, Sharp, Bevel) |
| `enable_uv_checker` | boolean | True | Enable a UV checker texture overlay to visually inspect UV layout quality during unwrapping |
| `darken_image` | boolean | False | Darken the UV checker or background image overlay to improve contrast during inspection |

### `uv.zenuv_mark_seams`

Mark selected edges or face borders as Seams and/or Sharp edges

| Property | Type | Default | Meaning |
|---|---|---|---|
| `action` | enum | `MARK` — `MARK` / `UNMARK` | Action |
| `markSeamEdges` | boolean | True | Automatically assign Seams |
| `markSharpEdges` | boolean | False | Automatically assign Sharp edges |
| `clear_inside` | boolean | True | Clear marking inside of selected faces |

### `uv.zenuv_auto_mark`

Mark edges as Seams and/or Sharp edges by Angle

| Property | Type | Default | Meaning |
|---|---|---|---|
| `keep_init` | boolean | False | Keep the state of initial seam and sharp |
| `respect_selection` | boolean | False | Mark only within current selection |
| `angle` | float | 30.03 | Set angle |
| `markSharpEdges` | boolean | False | Automatically assign Sharp edges |
| `markSeamEdges` | boolean | True | Automatically assign Seams |

### `uv.zenuv_quadrify`

Straighten rectangular shaped Island

| Property | Type | Default | Meaning |
|---|---|---|---|
| `algorithm` | enum | `ZEN_UV` — `ZEN_UV` / `BLEN` | Calculation algorithm |
| `influence` | enum | `ISLAND` — `ISLAND` / `SELECTION` | Transform Influence. Affect Islands or Selection |
| `edge_length_mode` | enum | `LENGTH_AVERAGE` — `LENGTH_AVERAGE` / `LENGTH` / `EVEN` | Method to space UV edge loops for native Blender algorithm |
| `shape` | enum | `PER_FACE` — `PER_FACE` / `FACE` / `EVEN` | The face shape for Zen UV algorithm |
| `average_shape` | boolean | True | Averages the shape depending on the shape of the faces in the faceloop |
| `use_selected_edges` | boolean | False | Selected Edges will be used as Seams during Quadrify Islands operation. Works only in edge selection mode |
| `quadrify_edge_detection_limit` | int | 4 | The maximum number of edges used to create the seam. If the number of selected edges is greater than this number, the seams will not be created |
| `skip_non_quads` | boolean | False | Skip islands that contain faces other than quads |
| `mark_borders` | boolean | True | Mark Island boundaries after Quadrify Islands operation |
| `mark_seams` | boolean | True | Mark seam in case Mark Borders is on |
| `mark_sharp` | boolean | False | Mark sharp in case Mark Borders is on |
| `orient` | enum | `ALIGN_TO_AXIS` — `SKIP` / `ALIGN_TO_AXIS` / `VERTICAL` / `HORIZONTAL` | Orient Quadrified Islands |
| `post_td_mode` | enum | `SKIP` — `AVERAGED` / `GLOBAL_PRESET` / `SKIP` | Set texel density. Not available if Pack Quadrified is On |
| `use_pack_islands` | boolean | False | Pack Islands after Quadrify Islands operation |
| `auto_pin` | enum | `SKIP` — `QUADS` / `ISLAND` / `SKIP` | Auto Pin |
| `tag_finished` | boolean | False | Tag Quadrified island as finished |
| `is_correct_aspect` | boolean | False | Map UVs taking image aspect ratio into account |

### `uv.zenuv_relax`

Relax selected islands

| Property | Type | Default | Meaning |
|---|---|---|---|
| `method` | enum | `ZENRELAX` — `ZENRELAX` / `ANGLE_BASED` / `CONFORMAL` / `MINIMUM_STRETCH` | Unwrapping method |
| `select` | boolean | False | Select relaxed Island |
| `relax` | boolean | True | Relax |
| `show_log` | boolean | True | Show Log |
| `correct_aspect` | boolean | True | Taking image aspect ratio into accaunt |
| `relax_mode` | string |  |  |
| `use_zensets` | boolean | False |  |

### `uv.zenuv_world_orient`

Rotate Islands the way they are oriented on the Models. Each method (Organic/Hard Surface) uses a heuristic approach and correctly orients most of the Islands in its area

| Property | Type | Default | Meaning |
|---|---|---|---|
| `further_orient` | boolean | True | Further rotation for horizontal or vertical alignment |

### `uv.zenuv_pack`

Pack all Islands

| Property | Type | Default | Meaning |
|---|---|---|---|
| `display_uv` | boolean | False |  |
| `disable_overlay` | boolean | False |  |
| `fast_mode` | boolean | False |  |

### `uv.zenuv_get_texel_density`

Get Texel Density from selected Islands

### `uv.zenuv_set_texel_density`

Set Texel Density to selected Islands

| Property | Type | Default | Meaning |
|---|---|---|---|
| `global_mode` | boolean | True | In global mode, all settings are taken from the "Texture Density" panel |
| `td_value` | float | 10.24 | Texel density value |
| `units` | enum | `m` — `km` / `m` / `cm` / `mm` / `um` / `mil` / `ft` / `in` / `th` | Texel density calculation units |
| `island_pivot` | enum | `cen` — `br` / `bl` / `tr` / `tl` / `cen` / `rc` / `lc` / `bc` / `tc` | Set texel density pivot point |
| `set_mode` | enum | `OVERALL` — `ISLAND` / `OVERALL` | Mode for setting Texel Density |
| `image_size_x` | int | 1024 |  |
| `image_size_y` | int | 1024 |  |

### `uv.zenuv_quick_hotspot`

The classic way to use Hotspot technology, which includes unwrap and several preprocessing steps to obtain fast results.

| Property | Type | Default | Meaning |
|---|---|---|---|
| `uv_mode` | enum | `CREATE` — `CREATE` / `USE_EXISTING` |  |
| `world_orient` | boolean | False | Orient to World |
| `allow_flip` | boolean | False | Allow flipping variation |
| `inset` | float | 0 | Trim Inset |
| `matching_scale` | float | 1 | The value for manually adjusting the area matching scale |
| `seed` | int | 123 | Seed of variable island distribution in similar trims |

### `uv.zenuv_fit_to_trim_hotspot`

Map Islands by matching Islands to predefined Trims from Trimsheet

| Property | Type | Default | Meaning |
|---|---|---|---|
| `trims_preselection` | enum | `ALL` — `ALL` / `SELECTED` | Use all Trims or selected Trims to map Islands |
| `orient` | enum | `AS_IS` — `AS_IS` / `WORLD` / `ORIENT` | Perform some Island rotation before Hotspotting |
| `priority` | enum | `ASPECT` — `ASPECT` / `AREA` / `WORLD_SIZE` | Prioritizing the similarities between the Trim and the Island. Determines the criterion that takes effect first |
| `area_match` | enum | `AS_IS` — `AS_IS` / `MAX` / `MIN` / `MANUAL` | How to match Trim area with Island area |
| `manual_scale` | float | 1 | The value for manually adjusting the area matching scale |
| `allow_rotation` | boolean | True | Allows Islands rotation by 90 degrees to achieve the best match |
| `allow_rotation_variation` | boolean | False | Allows random rotation of Trim by 180 degrees to increase variability |
| `allow_location_variation` | boolean | False | Allows islands to be placed in other trims with similar parameters. Requires two or more identical trims |
| `allow_offset_variation` | boolean | False | Allows islands to be shifted within the trim if the trim settings permit it. This works only if the Fit Axis option is set to Trim Settings and Fit Axis in trim options is set to Min |
| `allow_flip_variation` | boolean | False | Allows islands to be flipped |
| `loc_var_offset` | float | 0 | The amount by which the shift will be performed, if possible |
| `fit_axis` | enum | `AUTO` — `AUTO` / `FROM_TRIM` | Where to get the Fit Axis parameter |
| `inset_mode` | enum | `MANUAL` — `MANUAL` / `FROM_TRIM` / `COMBINED` | Where to get the Inset parameter |
| `inset` | float | 0 | Trim Inset |
| `seed` | int | 132 | Seed of variable island distribution in similar trims |
| `keep_proportion` | enum | `YES` — `YES` / `NO` / `FROM_TRIM` | Keep proportion |
| `detect_radial` | boolean | False | Detect Radial Islands and place them in Trims with Tag 'HOTSPOT Radial' |
| `select_radials` | boolean | False | Select Radial Islands |
| `aspect_precision` | float | 0 | Aspect influence for calculating the correspondence proportions between Trim and Island |
| `cat` | string | HOTSPOT |  |
| `tag` | string | Radial |  |
| `allow_variability` | boolean | False |  |

### `view3d.zenuv_checker_toggle`

Add Checker Texture to the mesh

| Property | Type | Default | Meaning |
|---|---|---|---|
| `checked` | boolean | False |  |
| `action` | enum | `TOGGLE` — `ON` / `OFF` / `TOGGLE` | Action |

---

## Registered keymap

| Keymap | Keys | Operator |
|---|---|---|
| 3D View Tool: Edit Mesh, Zen UV Transform | `Ctrl+Shift+Alt+Cmd+LEFT_CTRL` | `zenuv.tool_area_update` |
| 3D View Tool: Edit Mesh, Zen UV Transform | `Ctrl+Shift+Alt+Cmd+LEFT_SHIFT` | `zenuv.tool_area_update` |
| 3D View Tool: Edit Mesh, Zen UV Transform | `Ctrl+Shift+Alt+Cmd+RIGHT_CTRL` | `zenuv.tool_area_update` |
| 3D View Tool: Edit Mesh, Zen UV Transform | `Ctrl+Shift+Alt+Cmd+RIGHT_SHIFT` | `zenuv.tool_area_update` |
| 3D View Tool: Edit Mesh, Zen UV Transform | `Ctrl+Shift+WHEELDOWNMOUSE` | `wm.zenuv_trim_scroll_fit_internal` |
| 3D View Tool: Edit Mesh, Zen UV Transform | `Ctrl+Shift+WHEELUPMOUSE` | `wm.zenuv_trim_scroll_fit_internal` |
| 3D View Tool: Edit Mesh, Zen UV Transform | `D` | `wm.zenuv_update_toggle` |
| 3D View Tool: Edit Mesh, Zen UV Transform | `F` | `wm.zenuv_update_toggle` |
| 3D View Tool: Edit Mesh, Zen UV Transform | `Shift+F` | `wm.zenuv_update_toggle` |
| Image | `Alt+LEFTMOUSE` | `uv.zenuv_transform_move_islands` |
| Image | `Ctrl+Alt+LEFTMOUSE` | `uv.zenuv_transform_scale_islands` |
| Image | `Shift+Alt+LEFTMOUSE` | `uv.zenuv_transform_rotate_islands` |
| Image | `Shift+U` | `zenuv.call_popup` |
| Image Editor Tool: Uv, Zen UV Touch | `Ctrl+LEFTMOUSE` | `uv.zenuv_tool_transform_handle_click` |
| Image Editor Tool: Uv, Zen UV Touch | `G` | `uv.zenuv_tool_transform_enum` |
| Image Editor Tool: Uv, Zen UV Touch | `G` | `uv.zenuv_tool_transform_enum` |
| Image Editor Tool: Uv, Zen UV Touch | `LEFTMOUSE` | `uv.zenuv_tool_transform_handle_click` |
| Image Editor Tool: Uv, Zen UV Touch | `R` | `uv.zenuv_tool_transform_enum` |
| Image Editor Tool: Uv, Zen UV Touch | `R` | `uv.zenuv_tool_transform_enum` |
| Image Editor Tool: Uv, Zen UV Touch | `S` | `uv.zenuv_tool_transform_enum` |
| Image Editor Tool: Uv, Zen UV Touch | `S` | `uv.zenuv_tool_transform_enum` |
| Image Editor Tool: Uv, Zen UV Transform | `Ctrl+Shift+Alt+Cmd+LEFT_CTRL` | `zenuv.tool_area_update` |
| Image Editor Tool: Uv, Zen UV Transform | `Ctrl+Shift+Alt+Cmd+LEFT_SHIFT` | `zenuv.tool_area_update` |
| Image Editor Tool: Uv, Zen UV Transform | `Ctrl+Shift+Alt+Cmd+RIGHT_CTRL` | `zenuv.tool_area_update` |
| Image Editor Tool: Uv, Zen UV Transform | `Ctrl+Shift+Alt+Cmd+RIGHT_SHIFT` | `zenuv.tool_area_update` |
| Image Editor Tool: Uv, Zen UV Transform | `Ctrl+Shift+WHEELDOWNMOUSE` | `wm.zenuv_trim_scroll_fit_internal` |
| Image Editor Tool: Uv, Zen UV Transform | `Ctrl+Shift+WHEELUPMOUSE` | `wm.zenuv_trim_scroll_fit_internal` |
| Image Editor Tool: Uv, Zen UV Transform | `D` | `wm.zenuv_update_toggle` |
| Image Editor Tool: Uv, Zen UV Transform | `ESC` | `zenuv.tool_exit_create` |
| Image Editor Tool: Uv, Zen UV Transform | `F` | `wm.zenuv_update_toggle` |
| Mesh | `Shift+U` | `zenuv.call_popup` |
| Object Mode | `Shift+U` | `zenuv.call_popup` |
| Window | `Alt+T` | `view3d.zenuv_checker_toggle` |
