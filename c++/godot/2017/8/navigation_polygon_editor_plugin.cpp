/*************************************************************************/
/*  navigation_polygon_editor_plugin.cpp                                 */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2017 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2017 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/
#include "navigation_polygon_editor_plugin.h"

#include "canvas_item_editor_plugin.h"
#include "editor/editor_settings.h"
#include "os/file_access.h"

void NavigationPolygonEditor::_notification(int p_what) {

	switch (p_what) {

		case NOTIFICATION_READY: {

			button_create->set_icon(get_icon("Edit", "EditorIcons"));
			button_edit->set_icon(get_icon("MovePoint", "EditorIcons"));
			button_edit->set_pressed(true);
			get_tree()->connect("node_removed", this, "_node_removed");
			create_nav->connect("confirmed", this, "_create_nav");

		} break;
		case NOTIFICATION_FIXED_PROCESS: {

		} break;
	}
}
void NavigationPolygonEditor::_node_removed(Node *p_node) {

	if (p_node == node) {
		node = NULL;
		hide();
		canvas_item_editor->get_viewport_control()->update();
	}
}

void NavigationPolygonEditor::_create_nav() {

	if (!node)
		return;

	undo_redo->create_action(TTR("Create Navigation Polygon"));
	undo_redo->add_do_method(node, "set_navigation_polygon", Ref<NavigationPolygon>(memnew(NavigationPolygon)));
	undo_redo->add_undo_method(node, "set_navigation_polygon", Variant(REF()));
	undo_redo->commit_action();
	_menu_option(MODE_CREATE);
}

void NavigationPolygonEditor::_menu_option(int p_option) {

	switch (p_option) {

		case MODE_CREATE: {

			mode = MODE_CREATE;
			button_create->set_pressed(true);
			button_edit->set_pressed(false);
		} break;
		case MODE_EDIT: {

			mode = MODE_EDIT;
			button_create->set_pressed(false);
			button_edit->set_pressed(true);
		} break;
	}
}

void NavigationPolygonEditor::_wip_close() {

	if (wip.size() >= 3) {

		undo_redo->create_action(TTR("Create Poly"));
		undo_redo->add_undo_method(node->get_navigation_polygon().ptr(), "remove_outline", node->get_navigation_polygon()->get_outline_count());
		undo_redo->add_do_method(node->get_navigation_polygon().ptr(), "add_outline", wip);
		undo_redo->add_do_method(node->get_navigation_polygon().ptr(), "make_polygons_from_outlines");
		undo_redo->add_undo_method(node->get_navigation_polygon().ptr(), "make_polygons_from_outlines");
		undo_redo->add_do_method(canvas_item_editor->get_viewport_control(), "update");
		undo_redo->add_undo_method(canvas_item_editor->get_viewport_control(), "update");
		undo_redo->commit_action();
		mode = MODE_EDIT;
		button_edit->set_pressed(true);
		button_create->set_pressed(false);
	}

	wip.clear();
	wip_active = false;
	edited_point = -1;
}

bool NavigationPolygonEditor::forward_gui_input(const Ref<InputEvent> &p_event) {

	if (!node)
		return false;

	if (node->get_navigation_polygon().is_null()) {

		Ref<InputEventMouseButton> mb = p_event;

		if (mb.is_valid() && mb->get_button_index() == 1 && mb->is_pressed()) {
			create_nav->set_text("No NavigationPolygon resource on this node.\nCreate and assign one?");
			create_nav->popup_centered_minsize();
		}
		return (mb.is_valid() && mb->get_button_index() == 1);
	}

	Ref<InputEventMouseButton> mb = p_event;

	if (mb.is_valid()) {

		Transform2D xform = canvas_item_editor->get_canvas_transform() * node->get_global_transform();

		Vector2 gpoint = mb->get_position();
		Vector2 cpoint = canvas_item_editor->get_canvas_transform().affine_inverse().xform(gpoint);
		cpoint = canvas_item_editor->snap_point(cpoint);
		cpoint = node->get_global_transform().affine_inverse().xform(cpoint);

		//first check if a point is to be added (segment split)
		real_t grab_threshold = EDITOR_DEF("editors/poly_editor/point_grab_radius", 8);

		switch (mode) {

			case MODE_CREATE: {

				if (mb->get_button_index() == BUTTON_LEFT && mb->is_pressed()) {

					if (!wip_active) {

						wip.clear();
						wip.push_back(cpoint);
						wip_active = true;
						edited_point_pos = cpoint;
						edited_outline = -1;
						canvas_item_editor->get_viewport_control()->update();
						edited_point = 1;
						return true;
					} else {

						if (wip.size() > 1 && xform.xform(wip[0]).distance_to(gpoint) < grab_threshold) {
							//wip closed
							_wip_close();

							return true;
						} else {

							wip.push_back(cpoint);
							edited_point = wip.size();
							canvas_item_editor->get_viewport_control()->update();
							return true;

							//add wip point
						}
					}
				} else if (mb->get_button_index() == BUTTON_RIGHT && mb->is_pressed() && wip_active) {
					_wip_close();
				}

			} break;

			case MODE_EDIT: {

				if (mb->get_button_index() == BUTTON_LEFT) {
					if (mb->is_pressed()) {

						if (mb->get_control()) {

							//search edges
							int closest_outline = -1;
							int closest_idx = -1;
							Vector2 closest_pos;
							real_t closest_dist = 1e10;

							for (int j = 0; j < node->get_navigation_polygon()->get_outline_count(); j++) {

								PoolVector<Vector2> points = node->get_navigation_polygon()->get_outline(j);

								int pc = points.size();
								PoolVector<Vector2>::Read poly = points.read();

								for (int i = 0; i < pc; i++) {

									Vector2 points[2] = { xform.xform(poly[i]),
										xform.xform(poly[(i + 1) % pc]) };

									Vector2 cp = Geometry::get_closest_point_to_segment_2d(gpoint, points);
									if (cp.distance_squared_to(points[0]) < CMP_EPSILON2 || cp.distance_squared_to(points[1]) < CMP_EPSILON2)
										continue; //not valid to reuse point

									real_t d = cp.distance_to(gpoint);
									if (d < closest_dist && d < grab_threshold) {
										closest_dist = d;
										closest_outline = j;
										closest_pos = cp;
										closest_idx = i;
									}
								}
							}

							if (closest_idx >= 0) {

								pre_move_edit = node->get_navigation_polygon()->get_outline(closest_outline);
								PoolVector<Point2> poly = pre_move_edit;
								poly.insert(closest_idx + 1, xform.affine_inverse().xform(closest_pos));
								edited_point = closest_idx + 1;
								edited_outline = closest_outline;
								edited_point_pos = xform.affine_inverse().xform(closest_pos);
								node->get_navigation_polygon()->set_outline(closest_outline, poly);
								canvas_item_editor->get_viewport_control()->update();
								return true;
							}
						} else {

							//look for points to move
							int closest_outline = -1;
							int closest_idx = -1;
							Vector2 closest_pos;
							real_t closest_dist = 1e10;

							for (int j = 0; j < node->get_navigation_polygon()->get_outline_count(); j++) {

								PoolVector<Vector2> points = node->get_navigation_polygon()->get_outline(j);

								int pc = points.size();
								PoolVector<Vector2>::Read poly = points.read();

								for (int i = 0; i < pc; i++) {

									Vector2 cp = xform.xform(poly[i]);

									real_t d = cp.distance_to(gpoint);
									if (d < closest_dist && d < grab_threshold) {
										closest_dist = d;
										closest_pos = cp;
										closest_outline = j;
										closest_idx = i;
									}
								}
							}

							if (closest_idx >= 0) {

								pre_move_edit = node->get_navigation_polygon()->get_outline(closest_outline);
								edited_point = closest_idx;
								edited_outline = closest_outline;
								edited_point_pos = xform.affine_inverse().xform(closest_pos);
								canvas_item_editor->get_viewport_control()->update();
								return true;
							}
						}
					} else {

						if (edited_point != -1) {

							//apply

							PoolVector<Vector2> poly = node->get_navigation_polygon()->get_outline(edited_outline);
							ERR_FAIL_INDEX_V(edited_point, poly.size(), false);
							poly.set(edited_point, edited_point_pos);
							undo_redo->create_action(TTR("Edit Poly"));
							undo_redo->add_do_method(node->get_navigation_polygon().ptr(), "set_outline", edited_outline, poly);
							undo_redo->add_undo_method(node->get_navigation_polygon().ptr(), "set_outline", edited_outline, pre_move_edit);
							undo_redo->add_do_method(node->get_navigation_polygon().ptr(), "make_polygons_from_outlines");
							undo_redo->add_undo_method(node->get_navigation_polygon().ptr(), "make_polygons_from_outlines");
							undo_redo->add_do_method(canvas_item_editor->get_viewport_control(), "update");
							undo_redo->add_undo_method(canvas_item_editor->get_viewport_control(), "update");
							undo_redo->commit_action();

							edited_point = -1;
							return true;
						}
					}
				} else if (mb->get_button_index() == BUTTON_RIGHT && mb->is_pressed() && edited_point == -1) {

					int closest_outline = -1;
					int closest_idx = -1;
					Vector2 closest_pos;
					real_t closest_dist = 1e10;

					for (int j = 0; j < node->get_navigation_polygon()->get_outline_count(); j++) {

						PoolVector<Vector2> points = node->get_navigation_polygon()->get_outline(j);

						int pc = points.size();
						PoolVector<Vector2>::Read poly = points.read();

						for (int i = 0; i < pc; i++) {

							Vector2 cp = xform.xform(poly[i]);

							real_t d = cp.distance_to(gpoint);
							if (d < closest_dist && d < grab_threshold) {
								closest_dist = d;
								closest_pos = cp;
								closest_outline = j;
								closest_idx = i;
							}
						}
					}

					if (closest_idx >= 0) {

						PoolVector<Vector2> poly = node->get_navigation_polygon()->get_outline(closest_outline);

						if (poly.size() > 3) {
							undo_redo->create_action(TTR("Edit Poly (Remove Point)"));
							undo_redo->add_undo_method(node->get_navigation_polygon().ptr(), "set_outline", closest_outline, poly);
							poly.remove(closest_idx);
							undo_redo->add_do_method(node->get_navigation_polygon().ptr(), "set_outline", closest_outline, poly);
							undo_redo->add_do_method(node->get_navigation_polygon().ptr(), "make_polygons_from_outlines");
							undo_redo->add_undo_method(node->get_navigation_polygon().ptr(), "make_polygons_from_outlines");
							undo_redo->add_do_method(canvas_item_editor->get_viewport_control(), "update");
							undo_redo->add_undo_method(canvas_item_editor->get_viewport_control(), "update");
							undo_redo->commit_action();
						} else {

							undo_redo->create_action(TTR("Remove Poly And Point"));
							undo_redo->add_undo_method(node->get_navigation_polygon().ptr(), "add_outline_at_index", poly, closest_outline);
							poly.remove(closest_idx);
							undo_redo->add_do_method(node->get_navigation_polygon().ptr(), "remove_outline", closest_outline);
							undo_redo->add_do_method(node->get_navigation_polygon().ptr(), "make_polygons_from_outlines");
							undo_redo->add_undo_method(node->get_navigation_polygon().ptr(), "make_polygons_from_outlines");
							undo_redo->add_do_method(canvas_item_editor->get_viewport_control(), "update");
							undo_redo->add_undo_method(canvas_item_editor->get_viewport_control(), "update");
							undo_redo->commit_action();
						}
						return true;
					}
				}

			} break;
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;

	if (mm.is_valid()) {

		if (edited_point != -1 && (wip_active || mm->get_button_mask() & BUTTON_MASK_LEFT)) {

			Vector2 gpoint = mm->get_position();
			Vector2 cpoint = canvas_item_editor->get_canvas_transform().affine_inverse().xform(gpoint);
			cpoint = canvas_item_editor->snap_point(cpoint);
			edited_point_pos = node->get_global_transform().affine_inverse().xform(cpoint);

			canvas_item_editor->get_viewport_control()->update();
		}
	}

	return false;
}
void NavigationPolygonEditor::_canvas_draw() {

	if (!node)
		return;

	Control *vpc = canvas_item_editor->get_viewport_control();
	if (node->get_navigation_polygon().is_null())
		return;

	Transform2D xform = canvas_item_editor->get_canvas_transform() * node->get_global_transform();
	Ref<Texture> handle = get_icon("EditorHandle", "EditorIcons");

	for (int j = -1; j < node->get_navigation_polygon()->get_outline_count(); j++) {
		Vector<Vector2> poly;

		if (wip_active && j == edited_outline) {
			poly = wip;
		} else {
			if (j == -1)
				continue;
			poly = Variant(node->get_navigation_polygon()->get_outline(j));
		}

		for (int i = 0; i < poly.size(); i++) {

			Vector2 p, p2;
			p = (j == edited_outline && i == edited_point) ? edited_point_pos : poly[i];
			if (j == edited_outline && ((wip_active && i == poly.size() - 1) || (((i + 1) % poly.size()) == edited_point)))
				p2 = edited_point_pos;
			else
				p2 = poly[(i + 1) % poly.size()];

			Vector2 point = xform.xform(p);
			Vector2 next_point = xform.xform(p2);

			Color col = Color(1, 0.3, 0.1, 0.8);
			vpc->draw_line(point, next_point, col, 2);
			vpc->draw_texture(handle, point - handle->get_size() * 0.5);
		}
	}
}

void NavigationPolygonEditor::edit(Node *p_collision_polygon) {

	if (!canvas_item_editor) {
		canvas_item_editor = CanvasItemEditor::get_singleton();
	}

	if (p_collision_polygon) {

		node = Object::cast_to<NavigationPolygonInstance>(p_collision_polygon);
		//Enable the pencil tool if the polygon is empty
		if (!node->get_navigation_polygon().is_null()) {
			if (node->get_navigation_polygon()->get_polygon_count() == 0)
				_menu_option(MODE_CREATE);
		}
		if (!canvas_item_editor->get_viewport_control()->is_connected("draw", this, "_canvas_draw"))
			canvas_item_editor->get_viewport_control()->connect("draw", this, "_canvas_draw");
		wip.clear();
		wip_active = false;
		edited_point = -1;
		canvas_item_editor->get_viewport_control()->update();

	} else {
		node = NULL;

		if (canvas_item_editor->get_viewport_control()->is_connected("draw", this, "_canvas_draw"))
			canvas_item_editor->get_viewport_control()->disconnect("draw", this, "_canvas_draw");
	}
}

void NavigationPolygonEditor::_bind_methods() {

	ClassDB::bind_method(D_METHOD("_menu_option"), &NavigationPolygonEditor::_menu_option);
	ClassDB::bind_method(D_METHOD("_canvas_draw"), &NavigationPolygonEditor::_canvas_draw);
	ClassDB::bind_method(D_METHOD("_node_removed"), &NavigationPolygonEditor::_node_removed);
	ClassDB::bind_method(D_METHOD("_create_nav"), &NavigationPolygonEditor::_create_nav);
}

NavigationPolygonEditor::NavigationPolygonEditor(EditorNode *p_editor) {
	node = NULL;
	canvas_item_editor = NULL;
	editor = p_editor;
	undo_redo = editor->get_undo_redo();

	add_child(memnew(VSeparator));
	button_create = memnew(ToolButton);
	add_child(button_create);
	button_create->connect("pressed", this, "_menu_option", varray(MODE_CREATE));
	button_create->set_toggle_mode(true);
	button_create->set_tooltip(TTR("Create a new polygon from scratch."));

	button_edit = memnew(ToolButton);
	add_child(button_edit);
	button_edit->connect("pressed", this, "_menu_option", varray(MODE_EDIT));
	button_edit->set_toggle_mode(true);
	button_edit->set_tooltip(TTR("Edit existing polygon:") + "\n" + TTR("LMB: Move Point.") + "\n" + TTR("Ctrl+LMB: Split Segment.") + "\n" + TTR("RMB: Erase Point."));
	create_nav = memnew(ConfirmationDialog);
	add_child(create_nav);
	create_nav->get_ok()->set_text(TTR("Create"));

	mode = MODE_EDIT;
	wip_active = false;
	edited_outline = -1;
}

void NavigationPolygonEditorPlugin::edit(Object *p_object) {

	collision_polygon_editor->edit(Object::cast_to<Node>(p_object));
}

bool NavigationPolygonEditorPlugin::handles(Object *p_object) const {

	return p_object->is_class("NavigationPolygonInstance");
}

void NavigationPolygonEditorPlugin::make_visible(bool p_visible) {

	if (p_visible) {
		collision_polygon_editor->show();
	} else {

		collision_polygon_editor->hide();
		collision_polygon_editor->edit(NULL);
	}
}

NavigationPolygonEditorPlugin::NavigationPolygonEditorPlugin(EditorNode *p_node) {

	editor = p_node;
	collision_polygon_editor = memnew(NavigationPolygonEditor(p_node));
	CanvasItemEditor::get_singleton()->add_control_to_menu_panel(collision_polygon_editor);

	collision_polygon_editor->hide();
}

NavigationPolygonEditorPlugin::~NavigationPolygonEditorPlugin() {
}
