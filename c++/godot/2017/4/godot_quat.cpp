/*************************************************************************/
/*  godot_quat.cpp                                                       */
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
#include "godot_quat.h"

#include "math/quat.h"

#ifdef __cplusplus
extern "C" {
#endif

void _quat_api_anchor() {
}

void GDAPI godot_quat_new(godot_quat *p_quat) {
	Quat *quat = (Quat *)p_quat;
	*quat = Quat();
}

void GDAPI godot_quat_new_with_elements(godot_quat *p_quat, const godot_real x, const godot_real y, const godot_real z, const godot_real w) {
	Quat *quat = (Quat *)p_quat;
	*quat = Quat(x, y, z, w);
}

void GDAPI godot_quat_new_with_rotation(godot_quat *p_quat, const godot_vector3 *p_axis, const godot_real p_angle) {
	Quat *quat = (Quat *)p_quat;
	const Vector3 *axis = (const Vector3 *)p_axis;
	*quat = Quat(*axis, p_angle);
}

void GDAPI godot_quat_new_with_shortest_arc(godot_quat *p_quat, const godot_vector3 *p_v0, const godot_vector3 *p_v1) {
	Quat *quat = (Quat *)p_quat;
	const Vector3 *v0 = (const Vector3 *)p_v0;
	const Vector3 *v1 = (const Vector3 *)p_v1;
	*quat = Quat(*v0, *v1);
}

godot_vector3 GDAPI godot_quat_get_euler(const godot_quat *p_quat) {
	Quat *quat = (Quat *)p_quat;
	Vector3 euler = quat->get_euler();
	return *(godot_vector3 *)&euler;
}

void GDAPI godot_quat_set_euler(godot_quat *p_quat, const godot_vector3 *p_euler) {
	Quat *quat = (Quat *)p_quat;
	const Vector3 *euler = (const Vector3 *)p_euler;
	quat->set_euler(*euler);
}

godot_real GDAPI *godot_quat_index(godot_quat *p_quat, const godot_int p_idx) {
	Quat *quat = (Quat *)p_quat;
	switch (p_idx) {
		case 0:
			return &quat->x;
		case 1:
			return &quat->y;
		case 2:
			return &quat->z;
		default:
			return &quat->y;
	}
}

godot_real GDAPI godot_quat_const_index(const godot_quat *p_quat, const godot_int p_idx) {
	const Quat *quat = (const Quat *)p_quat;
	switch (p_idx) {
		case 0:
			return quat->x;
		case 1:
			return quat->y;
		case 2:
			return quat->z;
		default:
			return quat->y;
	}
}

#ifdef __cplusplus
}
#endif
