/*************************************************************************/
/*  editor_audio_buses.cpp                                               */
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
#include "editor_audio_buses.h"

#include "editor_node.h"
#include "filesystem_dock.h"
#include "io/resource_saver.h"
#include "os/keyboard.h"
#include "servers/audio_server.h"

void EditorAudioBus::_notification(int p_what) {

	if (p_what == NOTIFICATION_READY) {

		vu_l->set_under_texture(get_icon("BusVuEmpty", "EditorIcons"));
		vu_l->set_progress_texture(get_icon("BusVuFull", "EditorIcons"));
		vu_r->set_under_texture(get_icon("BusVuEmpty", "EditorIcons"));
		vu_r->set_progress_texture(get_icon("BusVuFull", "EditorIcons"));
		scale->set_texture(get_icon("BusVuDb", "EditorIcons"));

		solo->set_icon(get_icon("AudioBusSolo", "EditorIcons"));
		mute->set_icon(get_icon("AudioBusMute", "EditorIcons"));
		bypass->set_icon(get_icon("AudioBusBypass", "EditorIcons"));

		disabled_vu = get_icon("BusVuFrozen", "EditorIcons");

		prev_active = true;
		update_bus();
		set_process(true);
	}

	if (p_what == NOTIFICATION_DRAW) {

		if (has_focus()) {
			draw_style_box(get_stylebox("focus", "Button"), Rect2(Vector2(), get_size()));
		}
	}

	if (p_what == NOTIFICATION_PROCESS) {

		float real_peak[2] = { -100, -100 };
		bool activity_found = false;

		int cc;
		switch (AudioServer::get_singleton()->get_speaker_mode()) {
			case AudioServer::SPEAKER_MODE_STEREO: cc = 1; break;
			case AudioServer::SPEAKER_SURROUND_51: cc = 4; break;
			case AudioServer::SPEAKER_SURROUND_71: cc = 5; break;
		}

		for (int i = 0; i < cc; i++) {
			if (AudioServer::get_singleton()->is_bus_channel_active(get_index(), i)) {
				activity_found = true;
				real_peak[0] = MAX(real_peak[0], AudioServer::get_singleton()->get_bus_peak_volume_left_db(get_index(), i));
				real_peak[1] = MAX(real_peak[1], AudioServer::get_singleton()->get_bus_peak_volume_right_db(get_index(), i));
			}
		}

		if (real_peak[0] > peak_l) {
			peak_l = real_peak[0];
		} else {
			peak_l -= get_process_delta_time() * 60.0;
		}

		if (real_peak[1] > peak_r) {
			peak_r = real_peak[1];
		} else {
			peak_r -= get_process_delta_time() * 60.0;
		}

		vu_l->set_value(peak_l);
		vu_r->set_value(peak_r);

		if (activity_found != prev_active) {
			if (activity_found) {
				vu_l->set_over_texture(Ref<Texture>());
				vu_r->set_over_texture(Ref<Texture>());
			} else {
				vu_l->set_over_texture(disabled_vu);
				vu_r->set_over_texture(disabled_vu);
			}

			prev_active = activity_found;
		}
	}

	if (p_what == NOTIFICATION_VISIBILITY_CHANGED) {

		peak_l = -100;
		peak_r = -100;
		prev_active = true;

		set_process(is_visible_in_tree());
	}
}

void EditorAudioBus::update_send() {

	send->clear();
	if (get_index() == 0) {
		send->set_disabled(true);
		send->set_text("Speakers");
	} else {
		send->set_disabled(false);
		StringName current_send = AudioServer::get_singleton()->get_bus_send(get_index());
		int current_send_index = 0; //by default to master

		for (int i = 0; i < get_index(); i++) {
			StringName send_name = AudioServer::get_singleton()->get_bus_name(i);
			send->add_item(send_name);
			if (send_name == current_send) {
				current_send_index = i;
			}
		}

		send->select(current_send_index);
	}
}

void EditorAudioBus::update_bus() {

	if (updating_bus)
		return;

	updating_bus = true;

	int index = get_index();

	slider->set_value(AudioServer::get_singleton()->get_bus_volume_db(index));
	track_name->set_text(AudioServer::get_singleton()->get_bus_name(index));
	if (get_index() == 0)
		track_name->set_editable(false);

	solo->set_pressed(AudioServer::get_singleton()->is_bus_solo(index));
	mute->set_pressed(AudioServer::get_singleton()->is_bus_mute(index));
	bypass->set_pressed(AudioServer::get_singleton()->is_bus_bypassing_effects(index));
	// effects..
	effects->clear();

	TreeItem *root = effects->create_item();
	for (int i = 0; i < AudioServer::get_singleton()->get_bus_effect_count(index); i++) {

		Ref<AudioEffect> afx = AudioServer::get_singleton()->get_bus_effect(index, i);

		TreeItem *fx = effects->create_item(root);
		fx->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
		fx->set_editable(0, true);
		fx->set_checked(0, AudioServer::get_singleton()->is_bus_effect_enabled(index, i));
		fx->set_text(0, afx->get_name());
		fx->set_metadata(0, i);
	}

	TreeItem *add = effects->create_item(root);
	add->set_cell_mode(0, TreeItem::CELL_MODE_CUSTOM);
	add->set_editable(0, true);
	add->set_selectable(0, false);
	add->set_text(0, "Add Effect");

	update_send();

	updating_bus = false;
}

void EditorAudioBus::_name_changed(const String &p_new_name) {

	if (p_new_name == AudioServer::get_singleton()->get_bus_name(get_index()))
		return;

	String attempt = p_new_name;
	int attempts = 1;

	while (true) {

		bool name_free = true;
		for (int i = 0; i < AudioServer::get_singleton()->get_bus_count(); i++) {

			if (AudioServer::get_singleton()->get_bus_name(i) == attempt) {
				name_free = false;
				break;
			}
		}

		if (name_free) {
			break;
		}

		attempts++;
		attempt = p_new_name + " " + itos(attempts);
	}
	updating_bus = true;

	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();

	StringName current = AudioServer::get_singleton()->get_bus_name(get_index());
	ur->create_action("Rename Audio Bus");
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_name", get_index(), attempt);
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_name", get_index(), current);

	for (int i = 0; i < AudioServer::get_singleton()->get_bus_count(); i++) {
		if (AudioServer::get_singleton()->get_bus_send(i) == current) {
			ur->add_do_method(AudioServer::get_singleton(), "set_bus_send", i, attempt);
			ur->add_undo_method(AudioServer::get_singleton(), "set_bus_send", i, current);
		}
	}

	ur->add_do_method(buses, "_update_bus", get_index());
	ur->add_undo_method(buses, "_update_bus", get_index());

	ur->add_do_method(buses, "_update_sends");
	ur->add_undo_method(buses, "_update_sends");
	ur->commit_action();

	updating_bus = false;
}

void EditorAudioBus::_volume_db_changed(float p_db) {

	if (updating_bus)
		return;

	updating_bus = true;

	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();
	ur->create_action("Change Audio Bus Volume", UndoRedo::MERGE_ENDS);
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_volume_db", get_index(), p_db);
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_volume_db", get_index(), AudioServer::get_singleton()->get_bus_volume_db(get_index()));
	ur->add_do_method(buses, "_update_bus", get_index());
	ur->add_undo_method(buses, "_update_bus", get_index());
	ur->commit_action();

	updating_bus = false;
}
void EditorAudioBus::_solo_toggled() {

	updating_bus = true;

	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();
	ur->create_action("Toggle Audio Bus Solo");
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_solo", get_index(), solo->is_pressed());
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_solo", get_index(), AudioServer::get_singleton()->is_bus_solo(get_index()));
	ur->add_do_method(buses, "_update_bus", get_index());
	ur->add_undo_method(buses, "_update_bus", get_index());
	ur->commit_action();

	updating_bus = false;
}
void EditorAudioBus::_mute_toggled() {

	updating_bus = true;

	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();
	ur->create_action("Toggle Audio Bus Mute");
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_mute", get_index(), mute->is_pressed());
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_mute", get_index(), AudioServer::get_singleton()->is_bus_mute(get_index()));
	ur->add_do_method(buses, "_update_bus", get_index());
	ur->add_undo_method(buses, "_update_bus", get_index());
	ur->commit_action();

	updating_bus = false;
}
void EditorAudioBus::_bypass_toggled() {

	updating_bus = true;

	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();
	ur->create_action("Toggle Audio Bus Bypass Effects");
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_bypass_effects", get_index(), bypass->is_pressed());
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_bypass_effects", get_index(), AudioServer::get_singleton()->is_bus_bypassing_effects(get_index()));
	ur->add_do_method(buses, "_update_bus", get_index());
	ur->add_undo_method(buses, "_update_bus", get_index());
	ur->commit_action();

	updating_bus = false;
}

void EditorAudioBus::_send_selected(int p_which) {

	updating_bus = true;

	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();
	ur->create_action("Select Audio Bus Send");
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_send", get_index(), send->get_item_text(p_which));
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_send", get_index(), AudioServer::get_singleton()->get_bus_send(get_index()));
	ur->add_do_method(buses, "_update_bus", get_index());
	ur->add_undo_method(buses, "_update_bus", get_index());
	ur->commit_action();

	updating_bus = false;
}

void EditorAudioBus::_effect_selected() {

	TreeItem *effect = effects->get_selected();
	if (!effect)
		return;
	updating_bus = true;

	if (effect->get_metadata(0) != Variant()) {

		int index = effect->get_metadata(0);
		Ref<AudioEffect> effect = AudioServer::get_singleton()->get_bus_effect(get_index(), index);
		if (effect.is_valid()) {
			EditorNode::get_singleton()->push_item(effect.ptr());
		}
	}

	updating_bus = false;
}

void EditorAudioBus::_effect_edited() {

	if (updating_bus)
		return;

	TreeItem *effect = effects->get_edited();
	if (!effect)
		return;

	if (effect->get_metadata(0) == Variant()) {
		Rect2 area = effects->get_item_rect(effect);

		effect_options->set_position(effects->get_global_position() + area.pos + Vector2(0, area.size.y));
		effect_options->popup();
		//add effect
	} else {
		int index = effect->get_metadata(0);
		updating_bus = true;

		UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();
		ur->create_action("Select Audio Bus Send");
		ur->add_do_method(AudioServer::get_singleton(), "set_bus_effect_enabled", get_index(), index, effect->is_checked(0));
		ur->add_undo_method(AudioServer::get_singleton(), "set_bus_effect_enabled", get_index(), index, AudioServer::get_singleton()->is_bus_effect_enabled(get_index(), index));
		ur->add_do_method(buses, "_update_bus", get_index());
		ur->add_undo_method(buses, "_update_bus", get_index());
		ur->commit_action();

		updating_bus = false;
	}
}

void EditorAudioBus::_effect_add(int p_which) {

	if (updating_bus)
		return;

	StringName name = effect_options->get_item_metadata(p_which);

	Object *fx = ClassDB::instance(name);
	ERR_FAIL_COND(!fx);
	AudioEffect *afx = fx->cast_to<AudioEffect>();
	ERR_FAIL_COND(!afx);
	Ref<AudioEffect> afxr = Ref<AudioEffect>(afx);

	afxr->set_name(effect_options->get_item_text(p_which));

	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();
	ur->create_action("Add Audio Bus Effect");
	ur->add_do_method(AudioServer::get_singleton(), "add_bus_effect", get_index(), afxr, -1);
	ur->add_undo_method(AudioServer::get_singleton(), "remove_bus_effect", get_index(), AudioServer::get_singleton()->get_bus_effect_count(get_index()));
	ur->add_do_method(buses, "_update_bus", get_index());
	ur->add_undo_method(buses, "_update_bus", get_index());
	ur->commit_action();
}

void EditorAudioBus::_gui_input(const InputEvent &p_event) {

	if (p_event.type == InputEvent::KEY && p_event.key.pressed && p_event.key.scancode == KEY_DELETE && !p_event.key.echo) {
		accept_event();
		emit_signal("delete_request");
	}
	if (p_event.type == InputEvent::MOUSE_BUTTON && p_event.mouse_button.button_index == 2 && p_event.mouse_button.pressed) {

		Vector2 pos = Vector2(p_event.mouse_button.x, p_event.mouse_button.y);
		delete_popup->set_position(get_global_position() + pos);
		delete_popup->popup();
	}
}

void EditorAudioBus::_delete_pressed(int p_option) {

	if (p_option == 1) {
		emit_signal("delete_request");
	} else if (p_option == 0) {
		//duplicate
		emit_signal("duplicate_request", get_index());
	}
}

Variant EditorAudioBus::get_drag_data(const Point2 &p_point) {

	if (get_index() == 0) {
		return Variant();
	}

	Control *c = memnew(Control);
	Panel *p = memnew(Panel);
	c->add_child(p);
	p->add_style_override("panel", get_stylebox("focus", "Button"));
	p->set_size(get_size());
	p->set_position(-p_point);
	set_drag_preview(c);
	Dictionary d;
	d["type"] = "move_audio_bus";
	d["index"] = get_index();
	emit_signal("drop_end_request");
	return d;
}

bool EditorAudioBus::can_drop_data(const Point2 &p_point, const Variant &p_data) const {

	if (get_index() == 0)
		return false;
	Dictionary d = p_data;
	if (d.has("type") && String(d["type"]) == "move_audio_bus") {
		return true;
	}

	return false;
}
void EditorAudioBus::drop_data(const Point2 &p_point, const Variant &p_data) {

	Dictionary d = p_data;
	emit_signal("dropped", d["index"], get_index());
}

Variant EditorAudioBus::get_drag_data_fw(const Point2 &p_point, Control *p_from) {

	print_line("drag fw");
	TreeItem *item = effects->get_item_at_pos(p_point);
	if (!item) {
		print_line("no item");
		return Variant();
	}

	Variant md = item->get_metadata(0);

	if (md.get_type() == Variant::INT) {
		Dictionary fxd;
		fxd["type"] = "audio_bus_effect";
		fxd["bus"] = get_index();
		fxd["effect"] = md;

		Label *l = memnew(Label);
		l->set_text(item->get_text(0));
		effects->set_drag_preview(l);

		return fxd;
	}

	return Variant();
}

bool EditorAudioBus::can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) const {

	Dictionary d = p_data;
	if (!d.has("type") || String(d["type"]) != "audio_bus_effect")
		return false;

	TreeItem *item = effects->get_item_at_pos(p_point);
	if (!item)
		return false;

	effects->set_drop_mode_flags(Tree::DROP_MODE_INBETWEEN);

	return true;
}

void EditorAudioBus::drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) {

	Dictionary d = p_data;

	TreeItem *item = effects->get_item_at_pos(p_point);
	if (!item)
		return;
	int pos = effects->get_drop_section_at_pos(p_point);
	Variant md = item->get_metadata(0);

	int paste_at;
	int bus = d["bus"];
	int effect = d["effect"];

	if (md.get_type() == Variant::INT) {
		paste_at = md;
		if (pos > 0)
			paste_at++;

		if (bus == get_index() && paste_at > effect) {
			paste_at--;
		}
	} else {
		paste_at = -1;
	}

	bool enabled = AudioServer::get_singleton()->is_bus_effect_enabled(bus, effect);

	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();
	ur->create_action("Move Bus Effect");
	ur->add_do_method(AudioServer::get_singleton(), "remove_bus_effect", bus, effect);
	ur->add_do_method(AudioServer::get_singleton(), "add_bus_effect", get_index(), AudioServer::get_singleton()->get_bus_effect(bus, effect), paste_at);

	if (paste_at == -1) {
		paste_at = AudioServer::get_singleton()->get_bus_effect_count(get_index());
		if (bus == get_index()) {
			paste_at--;
		}
	}
	if (!enabled) {
		ur->add_do_method(AudioServer::get_singleton(), "set_bus_effect_enabled", get_index(), paste_at, false);
	}

	ur->add_undo_method(AudioServer::get_singleton(), "remove_bus_effect", get_index(), paste_at);
	ur->add_undo_method(AudioServer::get_singleton(), "add_bus_effect", bus, AudioServer::get_singleton()->get_bus_effect(bus, effect), effect);
	if (!enabled) {
		ur->add_undo_method(AudioServer::get_singleton(), "set_bus_effect_enabled", bus, effect, false);
	}

	ur->add_do_method(buses, "_update_bus", get_index());
	ur->add_undo_method(buses, "_update_bus", get_index());
	if (get_index() != bus) {
		ur->add_do_method(buses, "_update_bus", bus);
		ur->add_undo_method(buses, "_update_bus", bus);
	}
	ur->commit_action();
}

void EditorAudioBus::_delete_effect_pressed(int p_option) {

	TreeItem *item = effects->get_selected();
	if (!item)
		return;

	if (item->get_metadata(0).get_type() != Variant::INT)
		return;

	int index = item->get_metadata(0);

	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();
	ur->create_action("Delete Bus Effect");
	ur->add_do_method(AudioServer::get_singleton(), "remove_bus_effect", get_index(), index);
	ur->add_undo_method(AudioServer::get_singleton(), "add_bus_effect", get_index(), AudioServer::get_singleton()->get_bus_effect(get_index(), index), index);
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_effect_enabled", get_index(), index, AudioServer::get_singleton()->is_bus_effect_enabled(get_index(), index));
	ur->add_do_method(buses, "_update_bus", get_index());
	ur->add_undo_method(buses, "_update_bus", get_index());
	ur->commit_action();
}

void EditorAudioBus::_effect_rmb(const Vector2 &p_pos) {

	TreeItem *item = effects->get_selected();
	if (!item)
		return;

	if (item->get_metadata(0).get_type() != Variant::INT)
		return;

	delete_effect_popup->set_position(get_global_mouse_position());
	delete_effect_popup->popup();
}

void EditorAudioBus::_bind_methods() {

	ClassDB::bind_method("update_bus", &EditorAudioBus::update_bus);
	ClassDB::bind_method("update_send", &EditorAudioBus::update_send);
	ClassDB::bind_method("_name_changed", &EditorAudioBus::_name_changed);
	ClassDB::bind_method("_volume_db_changed", &EditorAudioBus::_volume_db_changed);
	ClassDB::bind_method("_solo_toggled", &EditorAudioBus::_solo_toggled);
	ClassDB::bind_method("_mute_toggled", &EditorAudioBus::_mute_toggled);
	ClassDB::bind_method("_bypass_toggled", &EditorAudioBus::_bypass_toggled);
	ClassDB::bind_method("_name_focus_exit", &EditorAudioBus::_name_focus_exit);
	ClassDB::bind_method("_send_selected", &EditorAudioBus::_send_selected);
	ClassDB::bind_method("_effect_edited", &EditorAudioBus::_effect_edited);
	ClassDB::bind_method("_effect_selected", &EditorAudioBus::_effect_selected);
	ClassDB::bind_method("_effect_add", &EditorAudioBus::_effect_add);
	ClassDB::bind_method("_gui_input", &EditorAudioBus::_gui_input);
	ClassDB::bind_method("_delete_pressed", &EditorAudioBus::_delete_pressed);
	ClassDB::bind_method("get_drag_data_fw", &EditorAudioBus::get_drag_data_fw);
	ClassDB::bind_method("can_drop_data_fw", &EditorAudioBus::can_drop_data_fw);
	ClassDB::bind_method("drop_data_fw", &EditorAudioBus::drop_data_fw);
	ClassDB::bind_method("_delete_effect_pressed", &EditorAudioBus::_delete_effect_pressed);
	ClassDB::bind_method("_effect_rmb", &EditorAudioBus::_effect_rmb);

	ADD_SIGNAL(MethodInfo("duplicate_request"));
	ADD_SIGNAL(MethodInfo("delete_request"));
	ADD_SIGNAL(MethodInfo("drop_end_request"));
	ADD_SIGNAL(MethodInfo("dropped"));
}

EditorAudioBus::EditorAudioBus(EditorAudioBuses *p_buses) {

	buses = p_buses;
	updating_bus = false;

	VBoxContainer *vb = memnew(VBoxContainer);
	add_child(vb);

	set_v_size_flags(SIZE_EXPAND_FILL);

	track_name = memnew(LineEdit);
	vb->add_child(track_name);
	track_name->connect("text_entered", this, "_name_changed");
	track_name->connect("focus_exited", this, "_name_focus_exit");

	HBoxContainer *hbc = memnew(HBoxContainer);
	vb->add_child(hbc);
	hbc->add_spacer();
	solo = memnew(ToolButton);
	solo->set_tooltip(TTR("Toggle Solo"));
	solo->set_toggle_mode(true);
	// solo->set_modulate(Color(0.8, 1.2, 0.8));
	solo->set_focus_mode(FOCUS_NONE);
	solo->connect("pressed", this, "_solo_toggled");
	hbc->add_child(solo);
	mute = memnew(ToolButton);
	mute->connect("pressed", this, "_mute_toggled");
	mute->set_toggle_mode(true);
	// mute->set_modulate(Color(1.2, 0.8, 0.8));
	mute->set_focus_mode(FOCUS_NONE);
	mute->set_tooltip(TTR("Toggle Mute"));
	hbc->add_child(mute);
	bypass = memnew(ToolButton);
	bypass->set_tooltip(TTR("Toggle Bypass"));
	bypass->set_toggle_mode(true);
	// bypass->set_modulate(Color(1.1, 1.1, 0.8));
	bypass->set_focus_mode(FOCUS_NONE);
	bypass->connect("pressed", this, "_bypass_toggled");
	hbc->add_child(bypass);
	hbc->add_spacer();

	HBoxContainer *hb = memnew(HBoxContainer);
	vb->add_child(hb);
	slider = memnew(VSlider);
	slider->set_min(-80);
	slider->set_max(24);
	slider->set_step(0.1);

	slider->connect("value_changed", this, "_volume_db_changed");
	hb->add_child(slider);
	vu_l = memnew(TextureProgress);
	vu_l->set_fill_mode(TextureProgress::FILL_BOTTOM_TO_TOP);
	hb->add_child(vu_l);
	vu_l->set_min(-80);
	vu_l->set_max(24);
	vu_l->set_step(0.1);

	vu_r = memnew(TextureProgress);
	vu_r->set_fill_mode(TextureProgress::FILL_BOTTOM_TO_TOP);
	hb->add_child(vu_r);
	vu_r->set_min(-80);
	vu_r->set_max(24);
	vu_r->set_step(0.1);

	scale = memnew(TextureRect);
	hb->add_child(scale);

	//add_child(hb);

	effects = memnew(Tree);
	effects->set_hide_root(true);
	effects->set_custom_minimum_size(Size2(0, 90) * EDSCALE);
	effects->set_hide_folding(true);
	vb->add_child(effects);
	effects->connect("item_edited", this, "_effect_edited");
	effects->connect("cell_selected", this, "_effect_selected");
	effects->set_edit_checkbox_cell_only_when_checkbox_is_pressed(true);
	effects->set_drag_forwarding(this);
	effects->connect("item_rmb_selected", this, "_effect_rmb");
	effects->set_allow_rmb_select(true);

	send = memnew(OptionButton);
	send->set_clip_text(true);
	send->connect("item_selected", this, "_send_selected");
	vb->add_child(send);

	set_focus_mode(FOCUS_CLICK);

	effect_options = memnew(PopupMenu);
	effect_options->connect("index_pressed", this, "_effect_add");
	add_child(effect_options);
	List<StringName> effects;
	ClassDB::get_inheriters_from_class("AudioEffect", &effects);
	effects.sort_custom<StringName::AlphCompare>();
	for (List<StringName>::Element *E = effects.front(); E; E = E->next()) {
		if (!ClassDB::can_instance(E->get()))
			continue;

		Ref<Texture> icon;
		if (has_icon(E->get(), "EditorIcons")) {
			icon = get_icon(E->get(), "EditorIcons");
		}
		String name = E->get().operator String().replace("AudioEffect", "");
		effect_options->add_item(name);
		effect_options->set_item_metadata(effect_options->get_item_count() - 1, E->get());
		effect_options->set_item_icon(effect_options->get_item_count() - 1, icon);
	}

	delete_popup = memnew(PopupMenu);
	delete_popup->add_item("Duplicate");
	delete_popup->add_item("Delete");
	add_child(delete_popup);
	delete_popup->connect("index_pressed", this, "_delete_pressed");

	delete_effect_popup = memnew(PopupMenu);
	delete_effect_popup->add_item("Delete Effect");
	add_child(delete_effect_popup);
	delete_effect_popup->connect("index_pressed", this, "_delete_effect_pressed");
}

bool EditorAudioBusDrop::can_drop_data(const Point2 &p_point, const Variant &p_data) const {

	Dictionary d = p_data;
	if (d.has("type") && String(d["type"]) == "move_audio_bus") {
		return true;
	}

	return false;
}
void EditorAudioBusDrop::drop_data(const Point2 &p_point, const Variant &p_data) {

	Dictionary d = p_data;
	emit_signal("dropped", d["index"], -1);
}

void EditorAudioBusDrop::_bind_methods() {

	ADD_SIGNAL(MethodInfo("dropped"));
}

EditorAudioBusDrop::EditorAudioBusDrop() {
}

void EditorAudioBuses::_update_buses() {

	while (bus_hb->get_child_count() > 0) {
		memdelete(bus_hb->get_child(0));
	}

	drop_end = NULL;

	for (int i = 0; i < AudioServer::get_singleton()->get_bus_count(); i++) {

		EditorAudioBus *audio_bus = memnew(EditorAudioBus(this));
		if (i == 0) {
			audio_bus->set_self_modulate(Color(0.7, 0.7, 0.7));
		}
		bus_hb->add_child(audio_bus);
		audio_bus->connect("delete_request", this, "_delete_bus", varray(audio_bus), CONNECT_DEFERRED);
		audio_bus->connect("duplicate_request", this, "_duplicate_bus", varray(), CONNECT_DEFERRED);
		audio_bus->connect("drop_end_request", this, "_request_drop_end");
		audio_bus->connect("dropped", this, "_drop_at_index", varray(), CONNECT_DEFERRED);
	}
}

EditorAudioBuses *EditorAudioBuses::register_editor() {

	EditorAudioBuses *audio_buses = memnew(EditorAudioBuses);
	EditorNode::get_singleton()->add_bottom_panel_item("Audio", audio_buses);
	return audio_buses;
}

void EditorAudioBuses::_notification(int p_what) {

	if (p_what == NOTIFICATION_READY) {
		_update_buses();
	}

	if (p_what == NOTIFICATION_DRAG_END) {
		if (drop_end) {
			drop_end->queue_delete();
			drop_end = NULL;
		}
	}

	if (p_what == NOTIFICATION_PROCESS) {

		//check if anything was edited
		bool edited = AudioServer::get_singleton()->is_edited();
		for (int i = 0; i < AudioServer::get_singleton()->get_bus_count(); i++) {
			for (int j = 0; j < AudioServer::get_singleton()->get_bus_effect_count(i); j++) {
				Ref<AudioEffect> effect = AudioServer::get_singleton()->get_bus_effect(i, j);
				if (effect->is_edited()) {
					edited = true;
					effect->set_edited(false);
				}
			}
		}

		AudioServer::get_singleton()->set_edited(false);

		if (edited) {

			save_timer->start();
		}
	}
}

void EditorAudioBuses::_add_bus() {

	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();

	//need to simulate new name, so we can undi :(
	ur->create_action("Add Audio Bus");
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_count", AudioServer::get_singleton()->get_bus_count() + 1);
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_count", AudioServer::get_singleton()->get_bus_count());
	ur->add_do_method(this, "_update_buses");
	ur->add_undo_method(this, "_update_buses");
	ur->commit_action();
}

void EditorAudioBuses::_update_bus(int p_index) {

	if (p_index >= bus_hb->get_child_count())
		return;

	bus_hb->get_child(p_index)->call("update_bus");
}

void EditorAudioBuses::_update_sends() {

	for (int i = 0; i < bus_hb->get_child_count(); i++) {
		bus_hb->get_child(i)->call("update_send");
	}
}

void EditorAudioBuses::_delete_bus(Object *p_which) {

	EditorAudioBus *bus = p_which->cast_to<EditorAudioBus>();
	int index = bus->get_index();
	if (index == 0) {
		EditorNode::get_singleton()->show_warning("Master bus can't be deleted!");
		return;
	}

	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();

	ur->create_action("Delete Audio Bus");
	ur->add_do_method(AudioServer::get_singleton(), "remove_bus", index);
	ur->add_undo_method(AudioServer::get_singleton(), "add_bus", index);
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_name", index, AudioServer::get_singleton()->get_bus_name(index));
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_volume_db", index, AudioServer::get_singleton()->get_bus_volume_db(index));
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_send", index, AudioServer::get_singleton()->get_bus_send(index));
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_solo", index, AudioServer::get_singleton()->is_bus_solo(index));
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_mute", index, AudioServer::get_singleton()->is_bus_mute(index));
	ur->add_undo_method(AudioServer::get_singleton(), "set_bus_bypass_effects", index, AudioServer::get_singleton()->is_bus_bypassing_effects(index));
	for (int i = 0; i < AudioServer::get_singleton()->get_bus_effect_count(index); i++) {

		ur->add_undo_method(AudioServer::get_singleton(), "add_bus_effect", index, AudioServer::get_singleton()->get_bus_effect(index, i));
		ur->add_undo_method(AudioServer::get_singleton(), "set_bus_effect_enabled", index, i, AudioServer::get_singleton()->is_bus_effect_enabled(index, i));
	}
	ur->add_do_method(this, "_update_buses");
	ur->add_undo_method(this, "_update_buses");
	ur->commit_action();
}

void EditorAudioBuses::_duplicate_bus(int p_which) {

	int add_at_pos = p_which + 1;
	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();
	ur->create_action("Duplicate Audio Bus");
	ur->add_do_method(AudioServer::get_singleton(), "add_bus", add_at_pos);
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_name", add_at_pos, AudioServer::get_singleton()->get_bus_name(p_which) + " Copy");
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_volume_db", add_at_pos, AudioServer::get_singleton()->get_bus_volume_db(p_which));
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_send", add_at_pos, AudioServer::get_singleton()->get_bus_send(p_which));
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_solo", add_at_pos, AudioServer::get_singleton()->is_bus_solo(p_which));
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_mute", add_at_pos, AudioServer::get_singleton()->is_bus_mute(p_which));
	ur->add_do_method(AudioServer::get_singleton(), "set_bus_bypass_effects", add_at_pos, AudioServer::get_singleton()->is_bus_bypassing_effects(p_which));
	for (int i = 0; i < AudioServer::get_singleton()->get_bus_effect_count(p_which); i++) {

		ur->add_do_method(AudioServer::get_singleton(), "add_bus_effect", add_at_pos, AudioServer::get_singleton()->get_bus_effect(p_which, i));
		ur->add_do_method(AudioServer::get_singleton(), "set_bus_effect_enabled", add_at_pos, i, AudioServer::get_singleton()->is_bus_effect_enabled(p_which, i));
	}
	ur->add_undo_method(AudioServer::get_singleton(), "remove_bus", add_at_pos);
	ur->add_do_method(this, "_update_buses");
	ur->add_undo_method(this, "_update_buses");
	ur->commit_action();
}

void EditorAudioBuses::_request_drop_end() {

	if (!drop_end && bus_hb->get_child_count()) {
		drop_end = memnew(EditorAudioBusDrop);

		bus_hb->add_child(drop_end);
		drop_end->set_custom_minimum_size(bus_hb->get_child(0)->cast_to<Control>()->get_size());
		drop_end->connect("dropped", this, "_drop_at_index", varray(), CONNECT_DEFERRED);
	}
}

void EditorAudioBuses::_drop_at_index(int p_bus, int p_index) {

	UndoRedo *ur = EditorNode::get_singleton()->get_undo_redo();

	//need to simulate new name, so we can undi :(
	ur->create_action("Move Audio Bus");
	ur->add_do_method(AudioServer::get_singleton(), "move_bus", p_bus, p_index);
	int final_pos;
	if (p_index == p_bus) {
		final_pos = p_bus;
	} else if (p_index == -1) {
		final_pos = AudioServer::get_singleton()->get_bus_count() - 1;
	} else if (p_index < p_bus) {
		final_pos = p_index;
	} else {
		final_pos = p_index - 1;
	}
	ur->add_undo_method(AudioServer::get_singleton(), "move_bus", final_pos, p_bus);

	ur->add_do_method(this, "_update_buses");
	ur->add_undo_method(this, "_update_buses");
	ur->commit_action();
}

void EditorAudioBuses::_server_save() {

	Ref<AudioBusLayout> state = AudioServer::get_singleton()->generate_bus_layout();
	ResourceSaver::save(edited_path, state);
}

void EditorAudioBuses::_select_layout() {

	EditorNode::get_singleton()->get_filesystem_dock()->select_file(edited_path);
}

void EditorAudioBuses::_save_as_layout() {

	file_dialog->set_mode(EditorFileDialog::MODE_SAVE_FILE);
	file_dialog->set_title(TTR("Save Audio Bus Layout As.."));
	file_dialog->set_current_path(edited_path);
	file_dialog->popup_centered_ratio();
	new_layout = false;
}

void EditorAudioBuses::_new_layout() {

	file_dialog->set_mode(EditorFileDialog::MODE_SAVE_FILE);
	file_dialog->set_title(TTR("Location for New Layout.."));
	file_dialog->set_current_path(edited_path);
	file_dialog->popup_centered_ratio();
	new_layout = true;
}

void EditorAudioBuses::_load_layout() {

	file_dialog->set_mode(EditorFileDialog::MODE_OPEN_FILE);
	file_dialog->set_title(TTR("Open Audio Bus Layout"));
	file_dialog->set_current_path(edited_path);
	file_dialog->popup_centered_ratio();
	new_layout = false;
}

void EditorAudioBuses::_load_default_layout() {

	Ref<AudioBusLayout> state = ResourceLoader::load("res://default_bus_layout.tres");
	if (state.is_null()) {
		EditorNode::get_singleton()->show_warning("There is no 'res://default_bus_layout.tres' file.");
		return;
	}

	edited_path = "res://default_bus_layout.tres";
	file->set_text(edited_path.get_file());
	AudioServer::get_singleton()->set_bus_layout(state);
	_update_buses();
	EditorNode::get_singleton()->get_undo_redo()->clear_history();
	call_deferred("_select_layout");
}

void EditorAudioBuses::_file_dialog_callback(const String &p_string) {

	if (file_dialog->get_mode() == EditorFileDialog::MODE_OPEN_FILE) {
		Ref<AudioBusLayout> state = ResourceLoader::load(p_string);
		if (state.is_null()) {
			EditorNode::get_singleton()->show_warning("Invalid file, not an audio bus layout.");
			return;
		}

		edited_path = p_string;
		file->set_text(p_string.get_file());
		AudioServer::get_singleton()->set_bus_layout(state);
		_update_buses();
		EditorNode::get_singleton()->get_undo_redo()->clear_history();
		call_deferred("_select_layout");

	} else if (file_dialog->get_mode() == EditorFileDialog::MODE_SAVE_FILE) {

		if (new_layout) {
			Ref<AudioBusLayout> empty_state;
			empty_state.instance();
			AudioServer::get_singleton()->set_bus_layout(empty_state);
		}

		Error err = ResourceSaver::save(p_string, AudioServer::get_singleton()->generate_bus_layout());

		if (err != OK) {
			EditorNode::get_singleton()->show_warning("Error saving file: " + p_string);
			return;
		}

		edited_path = p_string;
		file->set_text(p_string.get_file());
		_update_buses();
		EditorNode::get_singleton()->get_undo_redo()->clear_history();
		call_deferred("_select_layout");
	}
}

void EditorAudioBuses::_bind_methods() {

	ClassDB::bind_method("_add_bus", &EditorAudioBuses::_add_bus);
	ClassDB::bind_method("_update_buses", &EditorAudioBuses::_update_buses);
	ClassDB::bind_method("_update_bus", &EditorAudioBuses::_update_bus);
	ClassDB::bind_method("_update_sends", &EditorAudioBuses::_update_sends);
	ClassDB::bind_method("_delete_bus", &EditorAudioBuses::_delete_bus);
	ClassDB::bind_method("_request_drop_end", &EditorAudioBuses::_request_drop_end);
	ClassDB::bind_method("_drop_at_index", &EditorAudioBuses::_drop_at_index);
	ClassDB::bind_method("_server_save", &EditorAudioBuses::_server_save);
	ClassDB::bind_method("_select_layout", &EditorAudioBuses::_select_layout);
	ClassDB::bind_method("_save_as_layout", &EditorAudioBuses::_save_as_layout);
	ClassDB::bind_method("_load_layout", &EditorAudioBuses::_load_layout);
	ClassDB::bind_method("_load_default_layout", &EditorAudioBuses::_load_default_layout);
	ClassDB::bind_method("_new_layout", &EditorAudioBuses::_new_layout);
	ClassDB::bind_method("_duplicate_bus", &EditorAudioBuses::_duplicate_bus);

	ClassDB::bind_method("_file_dialog_callback", &EditorAudioBuses::_file_dialog_callback);
}

EditorAudioBuses::EditorAudioBuses() {

	drop_end = NULL;
	top_hb = memnew(HBoxContainer);
	add_child(top_hb);

	add = memnew(Button);
	top_hb->add_child(add);
	;
	add->set_text(TTR("Add Bus"));

	add->connect("pressed", this, "_add_bus");

	top_hb->add_spacer();

	file = memnew(ToolButton);
	file->set_text("default_bus_layout.tres");
	top_hb->add_child(file);
	file->connect("pressed", this, "_select_layout");

	load = memnew(Button);
	load->set_text(TTR("Load"));
	top_hb->add_child(load);
	load->connect("pressed", this, "_load_layout");

	save_as = memnew(Button);
	save_as->set_text(TTR("Save As"));
	top_hb->add_child(save_as);
	save_as->connect("pressed", this, "_save_as_layout");

	_default = memnew(Button);
	_default->set_text(TTR("Default"));
	top_hb->add_child(_default);
	_default->connect("pressed", this, "_load_default_layout");

	_new = memnew(Button);
	_new->set_text(TTR("Create"));
	top_hb->add_child(_new);
	_new->connect("pressed", this, "_new_layout");

	bus_scroll = memnew(ScrollContainer);
	bus_scroll->set_v_size_flags(SIZE_EXPAND_FILL);
	bus_scroll->set_enable_h_scroll(true);
	bus_scroll->set_enable_v_scroll(false);
	add_child(bus_scroll);
	bus_hb = memnew(HBoxContainer);
	bus_scroll->add_child(bus_hb);

	save_timer = memnew(Timer);
	save_timer->set_wait_time(0.8);
	save_timer->set_one_shot(true);
	add_child(save_timer);
	save_timer->connect("timeout", this, "_server_save");

	set_v_size_flags(SIZE_EXPAND_FILL);

	edited_path = "res://default_bus_layout.tres";

	file_dialog = memnew(EditorFileDialog);
	List<String> ext;
	ResourceLoader::get_recognized_extensions_for_type("AudioServerState", &ext);
	for (List<String>::Element *E = ext.front(); E; E = E->next()) {
		file_dialog->add_filter("*." + E->get() + "; Audio Bus State");
	}
	add_child(file_dialog);
	file_dialog->connect("file_selected", this, "_file_dialog_callback");

	set_process(true);
}
void EditorAudioBuses::open_layout(const String &p_path) {

	EditorNode::get_singleton()->make_bottom_panel_item_visible(this);

	Ref<AudioBusLayout> state = ResourceLoader::load(p_path);
	if (state.is_null()) {
		EditorNode::get_singleton()->show_warning("Invalid file, not an audio bus layout.");
		return;
	}

	edited_path = p_path;
	file->set_text(p_path.get_file());
	AudioServer::get_singleton()->set_bus_layout(state);
	_update_buses();
	EditorNode::get_singleton()->get_undo_redo()->clear_history();
	call_deferred("_select_layout");
}

void AudioBusesEditorPlugin::edit(Object *p_node) {

	if (p_node->cast_to<AudioBusLayout>()) {

		String path = p_node->cast_to<AudioBusLayout>()->get_path();
		if (path.is_resource_file()) {
			audio_bus_editor->open_layout(path);
		}
	}
}

bool AudioBusesEditorPlugin::handles(Object *p_node) const {

	return (p_node->cast_to<AudioBusLayout>() != NULL);
}

void AudioBusesEditorPlugin::make_visible(bool p_visible) {
}

AudioBusesEditorPlugin::AudioBusesEditorPlugin(EditorAudioBuses *p_node) {

	audio_bus_editor = p_node;
}

AudioBusesEditorPlugin::~AudioBusesEditorPlugin() {
}
