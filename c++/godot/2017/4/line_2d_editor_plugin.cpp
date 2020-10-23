/*************************************************************************/
/*  line_2d_editor_plugin.cpp                                            */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                    http://www.godotengine.org                         */
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
#include "line_2d_editor_plugin.h"

#include "canvas_item_editor_plugin.h"
#include "editor/editor_settings.h"
#include "os/file_access.h"
#include "os/keyboard.h"

//----------------------------------------------------------------------------
// Line2DEditor
//----------------------------------------------------------------------------

void Line2DEditor::_node_removed(Node *p_node) {
	if (p_node == node) {
		node = NULL;
		hide();
	}
}

void Line2DEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_VISIBILITY_CHANGED:
			// This widget is not a child but should have the same visibility state
			base_hb->set_visible(is_visible());
			break;
	}
}

Vector2 Line2DEditor::mouse_to_local_pos(Vector2 gpoint, bool alt) {
	Transform2D xform = canvas_item_editor->get_canvas_transform() * node->get_global_transform();
	return !alt ? canvas_item_editor->snap_point(xform.affine_inverse().xform(gpoint)) : node->get_global_transform().affine_inverse().xform(canvas_item_editor->snap_point(canvas_item_editor->get_canvas_transform().affine_inverse().xform(gpoint)));
}

int Line2DEditor::get_point_index_at(Vector2 gpos) {
	ERR_FAIL_COND_V(node == 0, -1);

	real_t grab_treshold = EDITOR_DEF("editors/poly_editor/point_grab_radius", 8);
	Transform2D xform = canvas_item_editor->get_canvas_transform() * node->get_global_transform();

	for (int i = 0; i < node->get_point_count(); ++i) {
		Point2 p = xform.xform(node->get_point_pos(i));
		if (gpos.distance_to(p) < grab_treshold) {
			return i;
		}
	}

	return -1;
}

bool Line2DEditor::forward_gui_input(const InputEvent &p_event) {

	if (!node)
		return false;

	if (!node->is_visible())
		return false;

	switch (p_event.type) {

		case InputEvent::MOUSE_BUTTON: {

			const InputEventMouseButton &mb = p_event.mouse_button;

			Vector2 gpoint = Point2(mb.x, mb.y);
			Vector2 cpoint = mouse_to_local_pos(gpoint, mb.mod.alt);

			if (mb.pressed && _dragging == false) {
				int i = get_point_index_at(gpoint);
				if (i != -1) {
					if (mb.button_index == BUTTON_LEFT && !mb.mod.shift && mode == MODE_EDIT) {
						_dragging = true;
						action_point = i;
						moving_from = node->get_point_pos(i);
						moving_screen_from = gpoint;
					} else if ((mb.button_index == BUTTON_RIGHT && mode == MODE_EDIT) || (mb.button_index == BUTTON_LEFT && mode == MODE_DELETE)) {
						undo_redo->create_action(TTR("Remove Point from Line2D"));
						undo_redo->add_do_method(node, "remove_point", i);
						undo_redo->add_undo_method(node, "add_point", node->get_point_pos(i), i);
						undo_redo->add_do_method(canvas_item_editor->get_viewport_control(), "update");
						undo_redo->add_undo_method(canvas_item_editor->get_viewport_control(), "update");
						undo_redo->commit_action();
					}
					return true;
				}
			}

			if (mb.pressed && mb.button_index == BUTTON_LEFT && ((mb.mod.command && mode == MODE_EDIT) || mode == MODE_CREATE)) {

				undo_redo->create_action(TTR("Add Point to Line2D"));
				undo_redo->add_do_method(node, "add_point", cpoint);
				undo_redo->add_undo_method(node, "remove_point", node->get_point_count());
				undo_redo->add_do_method(canvas_item_editor->get_viewport_control(), "update");
				undo_redo->add_undo_method(canvas_item_editor->get_viewport_control(), "update");
				undo_redo->commit_action();

				_dragging = true;
				action_point = node->get_point_count() - 1;
				moving_from = node->get_point_pos(action_point);
				moving_screen_from = gpoint;

				canvas_item_editor->get_viewport_control()->update();

				return true;
			}

			if (!mb.pressed && mb.button_index == BUTTON_LEFT && _dragging) {
				undo_redo->create_action(TTR("Move Point in Line2D"));
				undo_redo->add_do_method(node, "set_point_pos", action_point, cpoint);
				undo_redo->add_undo_method(node, "set_point_pos", action_point, moving_from);
				undo_redo->add_do_method(canvas_item_editor->get_viewport_control(), "update");
				undo_redo->add_undo_method(canvas_item_editor->get_viewport_control(), "update");
				undo_redo->commit_action();
				_dragging = false;
				return true;
			}
		} break;

		case InputEvent::MOUSE_MOTION: {
			if (_dragging) {
				const InputEventMouseMotion &mm = p_event.mouse_motion;
				Vector2 cpoint = mouse_to_local_pos(Vector2(mm.x, mm.y), mm.mod.alt);
				node->set_point_pos(action_point, cpoint);
				canvas_item_editor->get_viewport_control()->update();
				return true;
			}
		} break;
	}

	return false;
}

void Line2DEditor::_canvas_draw() {

	if (!node)
		return;

	if (!node->is_visible())
		return;

	Transform2D xform = canvas_item_editor->get_canvas_transform() * node->get_global_transform();
	Ref<Texture> handle = get_icon("EditorHandle", "EditorIcons");
	Size2 handle_size = handle->get_size();

	int len = node->get_point_count();
	Control *vpc = canvas_item_editor->get_viewport_control();

	for (int i = 0; i < len; ++i) {
		Vector2 point = xform.xform(node->get_point_pos(i));
		vpc->draw_texture_rect(handle, Rect2(point - handle_size * 0.5, handle_size), false);
	}
}

void Line2DEditor::_node_visibility_changed() {
	if (!node)
		return;
	canvas_item_editor->get_viewport_control()->update();
}

void Line2DEditor::edit(Node *p_line2d) {

	if (!canvas_item_editor)
		canvas_item_editor = CanvasItemEditor::get_singleton();

	if (p_line2d) {
		node = p_line2d->cast_to<Line2D>();
		if (!canvas_item_editor->get_viewport_control()->is_connected("draw", this, "_canvas_draw"))
			canvas_item_editor->get_viewport_control()->connect("draw", this, "_canvas_draw");
		if (!node->is_connected("visibility_changed", this, "_node_visibility_changed"))
			node->connect("visibility_changed", this, "_node_visibility_changed");
	} else {
		if (canvas_item_editor->get_viewport_control()->is_connected("draw", this, "_canvas_draw"))
			canvas_item_editor->get_viewport_control()->disconnect("draw", this, "_canvas_draw");
		// node may have been deleted at this point
		if (node && node->is_connected("visibility_changed", this, "_node_visibility_changed"))
			node->disconnect("visibility_changed", this, "_node_visibility_changed");
		node = NULL;
	}
}

void Line2DEditor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_canvas_draw"), &Line2DEditor::_canvas_draw);
	ClassDB::bind_method(D_METHOD("_node_visibility_changed"), &Line2DEditor::_node_visibility_changed);
	ClassDB::bind_method(D_METHOD("_mode_selected"), &Line2DEditor::_mode_selected);
}

void Line2DEditor::_mode_selected(int p_mode) {
	for (unsigned int i = 0; i < _MODE_COUNT; ++i) {
		toolbar_buttons[i]->set_pressed(i == p_mode);
	}
	mode = Mode(p_mode);
}

Line2DEditor::Line2DEditor(EditorNode *p_editor) {

	canvas_item_editor = NULL;
	editor = p_editor;
	undo_redo = editor->get_undo_redo();

	_dragging = false;

	base_hb = memnew(HBoxContainer);
	CanvasItemEditor::get_singleton()->add_control_to_menu_panel(base_hb);

	sep = memnew(VSeparator);
	base_hb->add_child(sep);

	{
		ToolButton *b = memnew(ToolButton);
		b->set_icon(EditorNode::get_singleton()->get_gui_base()->get_icon("CurveEdit", "EditorIcons"));
		b->set_toggle_mode(true);
		b->set_focus_mode(Control::FOCUS_NONE);
		b->set_tooltip(
				TTR("Select Points") + "\n" + TTR("Shift+Drag: Select Control Points") + "\n" + keycode_get_string(KEY_MASK_CMD) + TTR("Click: Add Point") + "\n" + TTR("Right Click: Delete Point"));
		b->connect("pressed", this, "_mode_selected", varray(MODE_EDIT));
		toolbar_buttons[MODE_EDIT] = b;
		base_hb->add_child(b);
	}

	{
		ToolButton *b = memnew(ToolButton);
		b->set_icon(EditorNode::get_singleton()->get_gui_base()->get_icon("CurveCreate", "EditorIcons"));
		b->set_toggle_mode(true);
		b->set_focus_mode(Control::FOCUS_NONE);
		b->set_tooltip(TTR("Add Point (in empty space)") + "\n" + TTR("Split Segment (in line)"));
		b->connect("pressed", this, "_mode_selected", varray(MODE_CREATE));
		toolbar_buttons[MODE_CREATE] = b;
		base_hb->add_child(b);
	}

	{
		ToolButton *b = memnew(ToolButton);
		b->set_icon(EditorNode::get_singleton()->get_gui_base()->get_icon("CurveDelete", "EditorIcons"));
		b->set_toggle_mode(true);
		b->set_focus_mode(Control::FOCUS_NONE);
		b->set_tooltip(TTR("Delete Point"));
		b->connect("pressed", this, "_mode_selected", varray(MODE_DELETE));
		toolbar_buttons[MODE_DELETE] = b;
		base_hb->add_child(b);
	}

	base_hb->hide();
	hide();

	_mode_selected(MODE_CREATE);
}

//----------------------------------------------------------------------------
// Line2DEditorPlugin
//----------------------------------------------------------------------------

void Line2DEditorPlugin::edit(Object *p_object) {
	line2d_editor->edit(p_object->cast_to<Node>());
}

bool Line2DEditorPlugin::handles(Object *p_object) const {
	return p_object->is_class("Line2D");
}

void Line2DEditorPlugin::make_visible(bool p_visible) {
	line2d_editor->set_visible(p_visible);
	if (p_visible == false)
		line2d_editor->edit(NULL);
}

Line2DEditorPlugin::Line2DEditorPlugin(EditorNode *p_node) {
	editor = p_node;
	line2d_editor = memnew(Line2DEditor(p_node));
	CanvasItemEditor::get_singleton()->add_control_to_menu_panel(line2d_editor);
	line2d_editor->hide();
}
