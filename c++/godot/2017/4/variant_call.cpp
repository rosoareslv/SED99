/*************************************************************************/
/*  variant_call.cpp                                                     */
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
#include "variant.h"

#include "core_string_names.h"
#include "object.h"
#include "os/os.h"
#include "script_language.h"

typedef void (*VariantFunc)(Variant &r_ret, Variant &p_self, const Variant **p_args);
typedef void (*VariantConstructFunc)(Variant &r_ret, const Variant **p_args);

VARIANT_ENUM_CAST(Image::CompressMode);
//VARIANT_ENUM_CAST(Image::Format);

struct _VariantCall {

	static void Vector3_dot(Variant &r_ret, Variant &p_self, const Variant **p_args) {

		r_ret = reinterpret_cast<Vector3 *>(p_self._data._mem)->dot(*reinterpret_cast<const Vector3 *>(p_args[0]->_data._mem));
	}

	struct FuncData {

		int arg_count;
		Vector<Variant> default_args;
		Vector<Variant::Type> arg_types;
		Vector<StringName> arg_names;
		Variant::Type return_type;

#ifdef DEBUG_ENABLED
		bool returns;
#endif
		VariantFunc func;

		_FORCE_INLINE_ bool verify_arguments(const Variant **p_args, Variant::CallError &r_error) {

			if (arg_count == 0)
				return true;

			Variant::Type *tptr = &arg_types[0];

			for (int i = 0; i < arg_count; i++) {

				if (!tptr[i] || tptr[i] == p_args[i]->type)
					continue; // all good
				if (!Variant::can_convert(p_args[i]->type, tptr[i])) {
					r_error.error = Variant::CallError::CALL_ERROR_INVALID_ARGUMENT;
					r_error.argument = i;
					r_error.expected = tptr[i];
					return false;
				}
			}
			return true;
		}

		_FORCE_INLINE_ void call(Variant &r_ret, Variant &p_self, const Variant **p_args, int p_argcount, Variant::CallError &r_error) {
#ifdef DEBUG_ENABLED
			if (p_argcount > arg_count) {
				r_error.error = Variant::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS;
				r_error.argument = arg_count;
				return;
			} else
#endif
					if (p_argcount < arg_count) {
				int def_argcount = default_args.size();
#ifdef DEBUG_ENABLED
				if (p_argcount < (arg_count - def_argcount)) {
					r_error.error = Variant::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
					r_error.argument = arg_count - def_argcount;
					return;
				}

#endif
				ERR_FAIL_COND(p_argcount > VARIANT_ARG_MAX);
				const Variant *newargs[VARIANT_ARG_MAX];
				for (int i = 0; i < p_argcount; i++)
					newargs[i] = p_args[i];
				int defargcount = def_argcount;
				for (int i = p_argcount; i < arg_count; i++)
					newargs[i] = &default_args[defargcount - (i - p_argcount) - 1]; //default arguments
#ifdef DEBUG_ENABLED
				if (!verify_arguments(newargs, r_error))
					return;
#endif
				func(r_ret, p_self, newargs);
			} else {
#ifdef DEBUG_ENABLED
				if (!verify_arguments(p_args, r_error))
					return;
#endif
				func(r_ret, p_self, p_args);
			}
		}
	};

	struct TypeFunc {

		Map<StringName, FuncData> functions;
	};

	static TypeFunc *type_funcs;

	struct Arg {
		StringName name;
		Variant::Type type;
		Arg() { type = Variant::NIL; }
		Arg(Variant::Type p_type, const StringName &p_name) {
			name = p_name;
			type = p_type;
		}
	};

	//void addfunc(Variant::Type p_type, const StringName& p_name,VariantFunc p_func);

	static void make_func_return_variant(Variant::Type p_type, const StringName &p_name) {

#ifdef DEBUG_ENABLED
		type_funcs[p_type].functions[p_name].returns = true;
#endif
	}

	static void addfunc(Variant::Type p_type, Variant::Type p_return, const StringName &p_name, VariantFunc p_func, const Vector<Variant> &p_defaultarg, const Arg &p_argtype1 = Arg(), const Arg &p_argtype2 = Arg(), const Arg &p_argtype3 = Arg(), const Arg &p_argtype4 = Arg(), const Arg &p_argtype5 = Arg()) {

		FuncData funcdata;
		funcdata.func = p_func;
		funcdata.default_args = p_defaultarg;
#ifdef DEBUG_ENABLED
		funcdata.return_type = p_return;
		funcdata.returns = p_return != Variant::NIL;
#endif

		if (p_argtype1.name) {
			funcdata.arg_types.push_back(p_argtype1.type);
#ifdef DEBUG_ENABLED
			funcdata.arg_names.push_back(p_argtype1.name);
#endif

		} else
			goto end;

		if (p_argtype2.name) {
			funcdata.arg_types.push_back(p_argtype2.type);
#ifdef DEBUG_ENABLED
			funcdata.arg_names.push_back(p_argtype2.name);
#endif

		} else
			goto end;

		if (p_argtype3.name) {
			funcdata.arg_types.push_back(p_argtype3.type);
#ifdef DEBUG_ENABLED
			funcdata.arg_names.push_back(p_argtype3.name);
#endif

		} else
			goto end;

		if (p_argtype4.name) {
			funcdata.arg_types.push_back(p_argtype4.type);
#ifdef DEBUG_ENABLED
			funcdata.arg_names.push_back(p_argtype4.name);
#endif
		} else
			goto end;

		if (p_argtype5.name) {
			funcdata.arg_types.push_back(p_argtype5.type);
#ifdef DEBUG_ENABLED
			funcdata.arg_names.push_back(p_argtype5.name);
#endif
		} else
			goto end;

	end:

		funcdata.arg_count = funcdata.arg_types.size();
		type_funcs[p_type].functions[p_name] = funcdata;
	}

#define VCALL_LOCALMEM0(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { reinterpret_cast<m_type *>(p_self._data._mem)->m_method(); }
#define VCALL_LOCALMEM0R(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { r_ret = reinterpret_cast<m_type *>(p_self._data._mem)->m_method(); }
#define VCALL_LOCALMEM1(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { reinterpret_cast<m_type *>(p_self._data._mem)->m_method(*p_args[0]); }
#define VCALL_LOCALMEM1R(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { r_ret = reinterpret_cast<m_type *>(p_self._data._mem)->m_method(*p_args[0]); }
#define VCALL_LOCALMEM2(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { reinterpret_cast<m_type *>(p_self._data._mem)->m_method(*p_args[0], *p_args[1]); }
#define VCALL_LOCALMEM2R(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { r_ret = reinterpret_cast<m_type *>(p_self._data._mem)->m_method(*p_args[0], *p_args[1]); }
#define VCALL_LOCALMEM3(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { reinterpret_cast<m_type *>(p_self._data._mem)->m_method(*p_args[0], *p_args[1], *p_args[2]); }
#define VCALL_LOCALMEM3R(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { r_ret = reinterpret_cast<m_type *>(p_self._data._mem)->m_method(*p_args[0], *p_args[1], *p_args[2]); }
#define VCALL_LOCALMEM4(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { reinterpret_cast<m_type *>(p_self._data._mem)->m_method(*p_args[0], *p_args[1], *p_args[2], *p_args[3]); }
#define VCALL_LOCALMEM4R(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { r_ret = reinterpret_cast<m_type *>(p_self._data._mem)->m_method(*p_args[0], *p_args[1], *p_args[2], *p_args[3]); }
#define VCALL_LOCALMEM5(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { reinterpret_cast<m_type *>(p_self._data._mem)->m_method(*p_args[0], *p_args[1], *p_args[2], *p_args[3], *p_args[4]); }
#define VCALL_LOCALMEM5R(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { r_ret = reinterpret_cast<m_type *>(p_self._data._mem)->m_method(*p_args[0], *p_args[1], *p_args[2], *p_args[3], *p_args[4]); }

	// built-in functions of localmem based types

	VCALL_LOCALMEM1R(String, casecmp_to);
	VCALL_LOCALMEM1R(String, nocasecmp_to);
	VCALL_LOCALMEM0R(String, length);
	VCALL_LOCALMEM2R(String, substr);
	VCALL_LOCALMEM2R(String, find);
	VCALL_LOCALMEM1R(String, find_last);
	VCALL_LOCALMEM2R(String, findn);
	VCALL_LOCALMEM2R(String, rfind);
	VCALL_LOCALMEM2R(String, rfindn);
	VCALL_LOCALMEM1R(String, match);
	VCALL_LOCALMEM1R(String, matchn);
	VCALL_LOCALMEM1R(String, begins_with);
	VCALL_LOCALMEM1R(String, ends_with);
	VCALL_LOCALMEM1R(String, is_subsequence_of);
	VCALL_LOCALMEM1R(String, is_subsequence_ofi);
	VCALL_LOCALMEM0R(String, bigrams);
	VCALL_LOCALMEM1R(String, similarity);
	VCALL_LOCALMEM2R(String, format);
	VCALL_LOCALMEM2R(String, replace);
	VCALL_LOCALMEM2R(String, replacen);
	VCALL_LOCALMEM2R(String, insert);
	VCALL_LOCALMEM0R(String, capitalize);
	VCALL_LOCALMEM2R(String, split);
	VCALL_LOCALMEM2R(String, split_floats);
	VCALL_LOCALMEM0R(String, to_upper);
	VCALL_LOCALMEM0R(String, to_lower);
	VCALL_LOCALMEM1R(String, left);
	VCALL_LOCALMEM1R(String, right);
	VCALL_LOCALMEM2R(String, strip_edges);
	VCALL_LOCALMEM0R(String, get_extension);
	VCALL_LOCALMEM0R(String, get_basename);
	VCALL_LOCALMEM1R(String, plus_file);
	VCALL_LOCALMEM1R(String, ord_at);
	VCALL_LOCALMEM2(String, erase);
	VCALL_LOCALMEM0R(String, hash);
	VCALL_LOCALMEM0R(String, md5_text);
	VCALL_LOCALMEM0R(String, sha256_text);
	VCALL_LOCALMEM0R(String, md5_buffer);
	VCALL_LOCALMEM0R(String, sha256_buffer);
	VCALL_LOCALMEM0R(String, empty);
	VCALL_LOCALMEM0R(String, is_abs_path);
	VCALL_LOCALMEM0R(String, is_rel_path);
	VCALL_LOCALMEM0R(String, get_base_dir);
	VCALL_LOCALMEM0R(String, get_file);
	VCALL_LOCALMEM0R(String, xml_escape);
	VCALL_LOCALMEM0R(String, xml_unescape);
	VCALL_LOCALMEM0R(String, c_escape);
	VCALL_LOCALMEM0R(String, c_unescape);
	VCALL_LOCALMEM0R(String, json_escape);
	VCALL_LOCALMEM0R(String, percent_encode);
	VCALL_LOCALMEM0R(String, percent_decode);
	VCALL_LOCALMEM0R(String, is_valid_identifier);
	VCALL_LOCALMEM0R(String, is_valid_integer);
	VCALL_LOCALMEM0R(String, is_valid_float);
	VCALL_LOCALMEM0R(String, is_valid_html_color);
	VCALL_LOCALMEM0R(String, is_valid_ip_address);
	VCALL_LOCALMEM0R(String, to_int);
	VCALL_LOCALMEM0R(String, to_float);
	VCALL_LOCALMEM0R(String, hex_to_int);
	VCALL_LOCALMEM1R(String, pad_decimals);
	VCALL_LOCALMEM1R(String, pad_zeros);

	static void _call_String_to_ascii(Variant &r_ret, Variant &p_self, const Variant **p_args) {

		String *s = reinterpret_cast<String *>(p_self._data._mem);
		CharString charstr = s->ascii();

		PoolByteArray retval;
		size_t len = charstr.length();
		retval.resize(len);
		PoolByteArray::Write w = retval.write();
		copymem(w.ptr(), charstr.ptr(), len);
		w = PoolVector<uint8_t>::Write();

		r_ret = retval;
	}

	static void _call_String_to_utf8(Variant &r_ret, Variant &p_self, const Variant **p_args) {

		String *s = reinterpret_cast<String *>(p_self._data._mem);
		CharString charstr = s->utf8();

		PoolByteArray retval;
		size_t len = charstr.length();
		retval.resize(len);
		PoolByteArray::Write w = retval.write();
		copymem(w.ptr(), charstr.ptr(), len);
		w = PoolVector<uint8_t>::Write();

		r_ret = retval;
	}

	VCALL_LOCALMEM0R(Vector2, normalized);
	VCALL_LOCALMEM0R(Vector2, length);
	VCALL_LOCALMEM0R(Vector2, length_squared);
	VCALL_LOCALMEM0R(Vector2, is_normalized);
	VCALL_LOCALMEM1R(Vector2, distance_to);
	VCALL_LOCALMEM1R(Vector2, distance_squared_to);
	VCALL_LOCALMEM1R(Vector2, angle_to);
	VCALL_LOCALMEM1R(Vector2, angle_to_point);
	VCALL_LOCALMEM2R(Vector2, linear_interpolate);
	VCALL_LOCALMEM4R(Vector2, cubic_interpolate);
	VCALL_LOCALMEM1R(Vector2, rotated);
	VCALL_LOCALMEM0R(Vector2, tangent);
	VCALL_LOCALMEM0R(Vector2, floor);
	VCALL_LOCALMEM1R(Vector2, snapped);
	VCALL_LOCALMEM0R(Vector2, aspect);
	VCALL_LOCALMEM1R(Vector2, dot);
	VCALL_LOCALMEM1R(Vector2, slide);
	VCALL_LOCALMEM1R(Vector2, bounce);
	VCALL_LOCALMEM1R(Vector2, reflect);
	VCALL_LOCALMEM0R(Vector2, angle);
	//VCALL_LOCALMEM1R(Vector2,cross);
	VCALL_LOCALMEM0R(Vector2, abs);
	VCALL_LOCALMEM1R(Vector2, clamped);

	VCALL_LOCALMEM0R(Rect2, get_area);
	VCALL_LOCALMEM1R(Rect2, intersects);
	VCALL_LOCALMEM1R(Rect2, encloses);
	VCALL_LOCALMEM0R(Rect2, has_no_area);
	VCALL_LOCALMEM1R(Rect2, clip);
	VCALL_LOCALMEM1R(Rect2, merge);
	VCALL_LOCALMEM1R(Rect2, has_point);
	VCALL_LOCALMEM1R(Rect2, grow);
	VCALL_LOCALMEM1R(Rect2, expand);

	VCALL_LOCALMEM0R(Vector3, min_axis);
	VCALL_LOCALMEM0R(Vector3, max_axis);
	VCALL_LOCALMEM0R(Vector3, length);
	VCALL_LOCALMEM0R(Vector3, length_squared);
	VCALL_LOCALMEM0R(Vector3, is_normalized);
	VCALL_LOCALMEM0R(Vector3, normalized);
	VCALL_LOCALMEM0R(Vector3, inverse);
	VCALL_LOCALMEM1R(Vector3, snapped);
	VCALL_LOCALMEM2R(Vector3, rotated);
	VCALL_LOCALMEM2R(Vector3, linear_interpolate);
	VCALL_LOCALMEM4R(Vector3, cubic_interpolate);
	VCALL_LOCALMEM1R(Vector3, dot);
	VCALL_LOCALMEM1R(Vector3, cross);
	VCALL_LOCALMEM1R(Vector3, outer);
	VCALL_LOCALMEM0R(Vector3, to_diagonal_matrix);
	VCALL_LOCALMEM0R(Vector3, abs);
	VCALL_LOCALMEM0R(Vector3, floor);
	VCALL_LOCALMEM0R(Vector3, ceil);
	VCALL_LOCALMEM1R(Vector3, distance_to);
	VCALL_LOCALMEM1R(Vector3, distance_squared_to);
	VCALL_LOCALMEM1R(Vector3, angle_to);
	VCALL_LOCALMEM1R(Vector3, slide);
	VCALL_LOCALMEM1R(Vector3, bounce);
	VCALL_LOCALMEM1R(Vector3, reflect);

	VCALL_LOCALMEM0R(Plane, normalized);
	VCALL_LOCALMEM0R(Plane, center);
	VCALL_LOCALMEM0R(Plane, get_any_point);
	VCALL_LOCALMEM1R(Plane, is_point_over);
	VCALL_LOCALMEM1R(Plane, distance_to);
	VCALL_LOCALMEM2R(Plane, has_point);
	VCALL_LOCALMEM1R(Plane, project);

	//return vector3 if intersected, nil if not
	static void _call_Plane_intersect_3(Variant &r_ret, Variant &p_self, const Variant **p_args) {
		Vector3 result;
		if (reinterpret_cast<Plane *>(p_self._data._mem)->intersect_3(*p_args[0], *p_args[1], &result))
			r_ret = result;
		else
			r_ret = Variant();
	}

	static void _call_Plane_intersects_ray(Variant &r_ret, Variant &p_self, const Variant **p_args) {
		Vector3 result;
		if (reinterpret_cast<Plane *>(p_self._data._mem)->intersects_ray(*p_args[0], *p_args[1], &result))
			r_ret = result;
		else
			r_ret = Variant();
	}

	static void _call_Plane_intersects_segment(Variant &r_ret, Variant &p_self, const Variant **p_args) {
		Vector3 result;
		if (reinterpret_cast<Plane *>(p_self._data._mem)->intersects_segment(*p_args[0], *p_args[1], &result))
			r_ret = result;
		else
			r_ret = Variant();
	}

	VCALL_LOCALMEM0R(Quat, length);
	VCALL_LOCALMEM0R(Quat, length_squared);
	VCALL_LOCALMEM0R(Quat, normalized);
	VCALL_LOCALMEM0R(Quat, is_normalized);
	VCALL_LOCALMEM0R(Quat, inverse);
	VCALL_LOCALMEM1R(Quat, dot);
	VCALL_LOCALMEM1R(Quat, xform);
	VCALL_LOCALMEM2R(Quat, slerp);
	VCALL_LOCALMEM2R(Quat, slerpni);
	VCALL_LOCALMEM4R(Quat, cubic_slerp);

	VCALL_LOCALMEM0R(Color, to_32);
	VCALL_LOCALMEM0R(Color, to_ARGB32);
	VCALL_LOCALMEM0R(Color, gray);
	VCALL_LOCALMEM0R(Color, inverted);
	VCALL_LOCALMEM0R(Color, contrasted);
	VCALL_LOCALMEM2R(Color, linear_interpolate);
	VCALL_LOCALMEM1R(Color, blend);
	VCALL_LOCALMEM1R(Color, to_html);

	VCALL_LOCALMEM0R(RID, get_id);

	VCALL_LOCALMEM0R(NodePath, is_absolute);
	VCALL_LOCALMEM0R(NodePath, get_name_count);
	VCALL_LOCALMEM1R(NodePath, get_name);
	VCALL_LOCALMEM0R(NodePath, get_subname_count);
	VCALL_LOCALMEM1R(NodePath, get_subname);
	VCALL_LOCALMEM0R(NodePath, get_property);
	VCALL_LOCALMEM0R(NodePath, is_empty);

	VCALL_LOCALMEM0R(Dictionary, size);
	VCALL_LOCALMEM0R(Dictionary, empty);
	VCALL_LOCALMEM0(Dictionary, clear);
	VCALL_LOCALMEM1R(Dictionary, has);
	VCALL_LOCALMEM1R(Dictionary, has_all);
	VCALL_LOCALMEM1(Dictionary, erase);
	VCALL_LOCALMEM0R(Dictionary, hash);
	VCALL_LOCALMEM0R(Dictionary, keys);
	VCALL_LOCALMEM0R(Dictionary, values);

	VCALL_LOCALMEM2(Array, set);
	VCALL_LOCALMEM1R(Array, get);
	VCALL_LOCALMEM0R(Array, size);
	VCALL_LOCALMEM0R(Array, empty);
	VCALL_LOCALMEM0(Array, clear);
	VCALL_LOCALMEM0R(Array, hash);
	VCALL_LOCALMEM1(Array, push_back);
	VCALL_LOCALMEM1(Array, push_front);
	VCALL_LOCALMEM0R(Array, pop_back);
	VCALL_LOCALMEM0R(Array, pop_front);
	VCALL_LOCALMEM1(Array, append);
	VCALL_LOCALMEM1(Array, resize);
	VCALL_LOCALMEM2(Array, insert);
	VCALL_LOCALMEM1(Array, remove);
	VCALL_LOCALMEM0R(Array, front);
	VCALL_LOCALMEM0R(Array, back);
	VCALL_LOCALMEM2R(Array, find);
	VCALL_LOCALMEM2R(Array, rfind);
	VCALL_LOCALMEM1R(Array, find_last);
	VCALL_LOCALMEM1R(Array, count);
	VCALL_LOCALMEM1R(Array, has);
	VCALL_LOCALMEM1(Array, erase);
	VCALL_LOCALMEM0(Array, sort);
	VCALL_LOCALMEM2(Array, sort_custom);
	VCALL_LOCALMEM0(Array, invert);

	static void _call_PoolByteArray_get_string_from_ascii(Variant &r_ret, Variant &p_self, const Variant **p_args) {

		PoolByteArray *ba = reinterpret_cast<PoolByteArray *>(p_self._data._mem);
		String s;
		if (ba->size() >= 0) {
			PoolByteArray::Read r = ba->read();
			CharString cs;
			cs.resize(ba->size() + 1);
			copymem(cs.ptr(), r.ptr(), ba->size());
			cs[ba->size()] = 0;

			s = cs.get_data();
		}
		r_ret = s;
	}

	static void _call_PoolByteArray_get_string_from_utf8(Variant &r_ret, Variant &p_self, const Variant **p_args) {

		PoolByteArray *ba = reinterpret_cast<PoolByteArray *>(p_self._data._mem);
		String s;
		if (ba->size() >= 0) {
			PoolByteArray::Read r = ba->read();
			s.parse_utf8((const char *)r.ptr(), ba->size());
		}
		r_ret = s;
	}

	VCALL_LOCALMEM0R(PoolByteArray, size);
	VCALL_LOCALMEM2(PoolByteArray, set);
	VCALL_LOCALMEM1R(PoolByteArray, get);
	VCALL_LOCALMEM1(PoolByteArray, push_back);
	VCALL_LOCALMEM1(PoolByteArray, resize);
	VCALL_LOCALMEM2R(PoolByteArray, insert);
	VCALL_LOCALMEM1(PoolByteArray, remove);
	VCALL_LOCALMEM1(PoolByteArray, append);
	VCALL_LOCALMEM1(PoolByteArray, append_array);
	VCALL_LOCALMEM0(PoolByteArray, invert);
	VCALL_LOCALMEM2R(PoolByteArray, subarray);

	VCALL_LOCALMEM0R(PoolIntArray, size);
	VCALL_LOCALMEM2(PoolIntArray, set);
	VCALL_LOCALMEM1R(PoolIntArray, get);
	VCALL_LOCALMEM1(PoolIntArray, push_back);
	VCALL_LOCALMEM1(PoolIntArray, resize);
	VCALL_LOCALMEM2R(PoolIntArray, insert);
	VCALL_LOCALMEM1(PoolIntArray, remove);
	VCALL_LOCALMEM1(PoolIntArray, append);
	VCALL_LOCALMEM1(PoolIntArray, append_array);
	VCALL_LOCALMEM0(PoolIntArray, invert);

	VCALL_LOCALMEM0R(PoolRealArray, size);
	VCALL_LOCALMEM2(PoolRealArray, set);
	VCALL_LOCALMEM1R(PoolRealArray, get);
	VCALL_LOCALMEM1(PoolRealArray, push_back);
	VCALL_LOCALMEM1(PoolRealArray, resize);
	VCALL_LOCALMEM2R(PoolRealArray, insert);
	VCALL_LOCALMEM1(PoolRealArray, remove);
	VCALL_LOCALMEM1(PoolRealArray, append);
	VCALL_LOCALMEM1(PoolRealArray, append_array);
	VCALL_LOCALMEM0(PoolRealArray, invert);

	VCALL_LOCALMEM0R(PoolStringArray, size);
	VCALL_LOCALMEM2(PoolStringArray, set);
	VCALL_LOCALMEM1R(PoolStringArray, get);
	VCALL_LOCALMEM1(PoolStringArray, push_back);
	VCALL_LOCALMEM1(PoolStringArray, resize);
	VCALL_LOCALMEM2R(PoolStringArray, insert);
	VCALL_LOCALMEM1(PoolStringArray, remove);
	VCALL_LOCALMEM1(PoolStringArray, append);
	VCALL_LOCALMEM1(PoolStringArray, append_array);
	VCALL_LOCALMEM0(PoolStringArray, invert);
	VCALL_LOCALMEM1R(PoolStringArray, join);

	VCALL_LOCALMEM0R(PoolVector2Array, size);
	VCALL_LOCALMEM2(PoolVector2Array, set);
	VCALL_LOCALMEM1R(PoolVector2Array, get);
	VCALL_LOCALMEM1(PoolVector2Array, push_back);
	VCALL_LOCALMEM1(PoolVector2Array, resize);
	VCALL_LOCALMEM2R(PoolVector2Array, insert);
	VCALL_LOCALMEM1(PoolVector2Array, remove);
	VCALL_LOCALMEM1(PoolVector2Array, append);
	VCALL_LOCALMEM1(PoolVector2Array, append_array);
	VCALL_LOCALMEM0(PoolVector2Array, invert);

	VCALL_LOCALMEM0R(PoolVector3Array, size);
	VCALL_LOCALMEM2(PoolVector3Array, set);
	VCALL_LOCALMEM1R(PoolVector3Array, get);
	VCALL_LOCALMEM1(PoolVector3Array, push_back);
	VCALL_LOCALMEM1(PoolVector3Array, resize);
	VCALL_LOCALMEM2R(PoolVector3Array, insert);
	VCALL_LOCALMEM1(PoolVector3Array, remove);
	VCALL_LOCALMEM1(PoolVector3Array, append);
	VCALL_LOCALMEM1(PoolVector3Array, append_array);
	VCALL_LOCALMEM0(PoolVector3Array, invert);

	VCALL_LOCALMEM0R(PoolColorArray, size);
	VCALL_LOCALMEM2(PoolColorArray, set);
	VCALL_LOCALMEM1R(PoolColorArray, get);
	VCALL_LOCALMEM1(PoolColorArray, push_back);
	VCALL_LOCALMEM1(PoolColorArray, resize);
	VCALL_LOCALMEM2R(PoolColorArray, insert);
	VCALL_LOCALMEM1(PoolColorArray, remove);
	VCALL_LOCALMEM1(PoolColorArray, append);
	VCALL_LOCALMEM1(PoolColorArray, append_array);
	VCALL_LOCALMEM0(PoolColorArray, invert);

#define VCALL_PTR0(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { reinterpret_cast<m_type *>(p_self._data._ptr)->m_method(); }
#define VCALL_PTR0R(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { r_ret = reinterpret_cast<m_type *>(p_self._data._ptr)->m_method(); }
#define VCALL_PTR1(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { reinterpret_cast<m_type *>(p_self._data._ptr)->m_method(*p_args[0]); }
#define VCALL_PTR1R(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { r_ret = reinterpret_cast<m_type *>(p_self._data._ptr)->m_method(*p_args[0]); }
#define VCALL_PTR2(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { reinterpret_cast<m_type *>(p_self._data._ptr)->m_method(*p_args[0], *p_args[1]); }
#define VCALL_PTR2R(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { r_ret = reinterpret_cast<m_type *>(p_self._data._ptr)->m_method(*p_args[0], *p_args[1]); }
#define VCALL_PTR3(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { reinterpret_cast<m_type *>(p_self._data._ptr)->m_method(*p_args[0], *p_args[1], *p_args[2]); }
#define VCALL_PTR3R(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { r_ret = reinterpret_cast<m_type *>(p_self._data._ptr)->m_method(*p_args[0], *p_args[1], *p_args[2]); }
#define VCALL_PTR4(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { reinterpret_cast<m_type *>(p_self._data._ptr)->m_method(*p_args[0], *p_args[1], *p_args[2], *p_args[3]); }
#define VCALL_PTR4R(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { r_ret = reinterpret_cast<m_type *>(p_self._data._ptr)->m_method(*p_args[0], *p_args[1], *p_args[2], *p_args[3]); }
#define VCALL_PTR5(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { reinterpret_cast<m_type *>(p_self._data._ptr)->m_method(*p_args[0], *p_args[1], *p_args[2], *p_args[3], *p_args[4]); }
#define VCALL_PTR5R(m_type, m_method) \
	static void _call_##m_type##_##m_method(Variant &r_ret, Variant &p_self, const Variant **p_args) { r_ret = reinterpret_cast<m_type *>(p_self._data._ptr)->m_method(*p_args[0], *p_args[1], *p_args[2], *p_args[3], *p_args[4]); }

	VCALL_PTR0R(Image, get_format);
	VCALL_PTR0R(Image, get_width);
	VCALL_PTR0R(Image, get_height);
	VCALL_PTR0R(Image, empty);
	VCALL_PTR0R(Image, get_used_rect);
	VCALL_PTR1R(Image, load);
	VCALL_PTR1R(Image, save_png);
	VCALL_PTR1R(Image, get_rect);
	VCALL_PTR1R(Image, compressed);
	VCALL_PTR0R(Image, decompressed);
	VCALL_PTR3R(Image, resized);
	VCALL_PTR0R(Image, get_data);
	VCALL_PTR3(Image, blit_rect);
	VCALL_PTR1R(Image, converted);
	VCALL_PTR0(Image, fix_alpha_edges);

	VCALL_PTR0R(Rect3, get_area);
	VCALL_PTR0R(Rect3, has_no_area);
	VCALL_PTR0R(Rect3, has_no_surface);
	VCALL_PTR1R(Rect3, intersects);
	VCALL_PTR1R(Rect3, encloses);
	VCALL_PTR1R(Rect3, merge);
	VCALL_PTR1R(Rect3, intersection);
	VCALL_PTR1R(Rect3, intersects_plane);
	VCALL_PTR2R(Rect3, intersects_segment);
	VCALL_PTR1R(Rect3, has_point);
	VCALL_PTR1R(Rect3, get_support);
	VCALL_PTR0R(Rect3, get_longest_axis);
	VCALL_PTR0R(Rect3, get_longest_axis_index);
	VCALL_PTR0R(Rect3, get_longest_axis_size);
	VCALL_PTR0R(Rect3, get_shortest_axis);
	VCALL_PTR0R(Rect3, get_shortest_axis_index);
	VCALL_PTR0R(Rect3, get_shortest_axis_size);
	VCALL_PTR1R(Rect3, expand);
	VCALL_PTR1R(Rect3, grow);
	VCALL_PTR1R(Rect3, get_endpoint);

	VCALL_PTR0R(Transform2D, inverse);
	VCALL_PTR0R(Transform2D, affine_inverse);
	VCALL_PTR0R(Transform2D, get_rotation);
	VCALL_PTR0R(Transform2D, get_origin);
	VCALL_PTR0R(Transform2D, get_scale);
	VCALL_PTR0R(Transform2D, orthonormalized);
	VCALL_PTR1R(Transform2D, rotated);
	VCALL_PTR1R(Transform2D, scaled);
	VCALL_PTR1R(Transform2D, translated);
	VCALL_PTR2R(Transform2D, interpolate_with);

	static void _call_Transform2D_xform(Variant &r_ret, Variant &p_self, const Variant **p_args) {

		switch (p_args[0]->type) {

			case Variant::VECTOR2: r_ret = reinterpret_cast<Transform2D *>(p_self._data._ptr)->xform(p_args[0]->operator Vector2()); return;
			case Variant::RECT2: r_ret = reinterpret_cast<Transform2D *>(p_self._data._ptr)->xform(p_args[0]->operator Rect2()); return;
			default: r_ret = Variant();
		}
	}

	static void _call_Transform2D_xform_inv(Variant &r_ret, Variant &p_self, const Variant **p_args) {

		switch (p_args[0]->type) {

			case Variant::VECTOR2: r_ret = reinterpret_cast<Transform2D *>(p_self._data._ptr)->xform_inv(p_args[0]->operator Vector2()); return;
			case Variant::RECT2: r_ret = reinterpret_cast<Transform2D *>(p_self._data._ptr)->xform_inv(p_args[0]->operator Rect2()); return;
			default: r_ret = Variant();
		}
	}

	static void _call_Transform2D_basis_xform(Variant &r_ret, Variant &p_self, const Variant **p_args) {

		switch (p_args[0]->type) {

			case Variant::VECTOR2: r_ret = reinterpret_cast<Transform2D *>(p_self._data._ptr)->basis_xform(p_args[0]->operator Vector2()); return;
			default: r_ret = Variant();
		}
	}

	static void _call_Transform2D_basis_xform_inv(Variant &r_ret, Variant &p_self, const Variant **p_args) {

		switch (p_args[0]->type) {

			case Variant::VECTOR2: r_ret = reinterpret_cast<Transform2D *>(p_self._data._ptr)->basis_xform_inv(p_args[0]->operator Vector2()); return;
			default: r_ret = Variant();
		}
	}

	VCALL_PTR0R(Basis, inverse);
	VCALL_PTR0R(Basis, transposed);
	VCALL_PTR0R(Basis, determinant);
	VCALL_PTR2R(Basis, rotated);
	VCALL_PTR1R(Basis, scaled);
	VCALL_PTR0R(Basis, get_scale);
	VCALL_PTR0R(Basis, get_euler);
	VCALL_PTR1(Basis, set_scale);
	VCALL_PTR1(Basis, set_rotation_euler);
	VCALL_PTR2(Basis, set_rotation_axis_angle);
	VCALL_PTR1R(Basis, tdotx);
	VCALL_PTR1R(Basis, tdoty);
	VCALL_PTR1R(Basis, tdotz);
	VCALL_PTR1R(Basis, xform);
	VCALL_PTR1R(Basis, xform_inv);
	VCALL_PTR0R(Basis, get_orthogonal_index);
	VCALL_PTR0R(Basis, orthonormalized);

	VCALL_PTR0R(Transform, inverse);
	VCALL_PTR0R(Transform, affine_inverse);
	VCALL_PTR2R(Transform, rotated);
	VCALL_PTR1R(Transform, scaled);
	VCALL_PTR1R(Transform, translated);
	VCALL_PTR0R(Transform, orthonormalized);
	VCALL_PTR2R(Transform, looking_at);

	static void _call_Transform_xform(Variant &r_ret, Variant &p_self, const Variant **p_args) {

		switch (p_args[0]->type) {

			case Variant::VECTOR3: r_ret = reinterpret_cast<Transform *>(p_self._data._ptr)->xform(p_args[0]->operator Vector3()); return;
			case Variant::PLANE: r_ret = reinterpret_cast<Transform *>(p_self._data._ptr)->xform(p_args[0]->operator Plane()); return;
			case Variant::RECT3: r_ret = reinterpret_cast<Transform *>(p_self._data._ptr)->xform(p_args[0]->operator Rect3()); return;
			default: r_ret = Variant();
		}
	}

	static void _call_Transform_xform_inv(Variant &r_ret, Variant &p_self, const Variant **p_args) {

		switch (p_args[0]->type) {

			case Variant::VECTOR3: r_ret = reinterpret_cast<Transform *>(p_self._data._ptr)->xform_inv(p_args[0]->operator Vector3()); return;
			case Variant::PLANE: r_ret = reinterpret_cast<Transform *>(p_self._data._ptr)->xform_inv(p_args[0]->operator Plane()); return;
			case Variant::RECT3: r_ret = reinterpret_cast<Transform *>(p_self._data._ptr)->xform_inv(p_args[0]->operator Rect3()); return;
			default: r_ret = Variant();
		}
	}

	/*
	VCALL_PTR0( Transform, invert );
	VCALL_PTR0( Transform, affine_invert );
	VCALL_PTR2( Transform, rotate );
	VCALL_PTR1( Transform, scale );
	VCALL_PTR1( Transform, translate );
	VCALL_PTR0( Transform, orthonormalize ); */

	VCALL_PTR0R(InputEvent, is_pressed);
	VCALL_PTR1R(InputEvent, is_action);
	VCALL_PTR1R(InputEvent, is_action_pressed);
	VCALL_PTR1R(InputEvent, is_action_released);
	VCALL_PTR0R(InputEvent, is_echo);
	VCALL_PTR2(InputEvent, set_as_action);

	struct ConstructData {

		int arg_count;
		Vector<Variant::Type> arg_types;
		Vector<String> arg_names;
		VariantConstructFunc func;
	};

	struct ConstructFunc {

		List<ConstructData> constructors;
	};

	static ConstructFunc *construct_funcs;

	static void Vector2_init1(Variant &r_ret, const Variant **p_args) {

		r_ret = Vector2(*p_args[0], *p_args[1]);
	}

	static void Rect2_init1(Variant &r_ret, const Variant **p_args) {

		r_ret = Rect2(*p_args[0], *p_args[1]);
	}

	static void Rect2_init2(Variant &r_ret, const Variant **p_args) {

		r_ret = Rect2(*p_args[0], *p_args[1], *p_args[2], *p_args[3]);
	}

	static void Transform2D_init2(Variant &r_ret, const Variant **p_args) {

		Transform2D m(*p_args[0], *p_args[1]);
		r_ret = m;
	}

	static void Transform2D_init3(Variant &r_ret, const Variant **p_args) {

		Transform2D m;
		m[0] = *p_args[0];
		m[1] = *p_args[1];
		m[2] = *p_args[2];
		r_ret = m;
	}

	static void Vector3_init1(Variant &r_ret, const Variant **p_args) {

		r_ret = Vector3(*p_args[0], *p_args[1], *p_args[2]);
	}

	static void Plane_init1(Variant &r_ret, const Variant **p_args) {

		r_ret = Plane(*p_args[0], *p_args[1], *p_args[2], *p_args[3]);
	}

	static void Plane_init2(Variant &r_ret, const Variant **p_args) {

		r_ret = Plane(*p_args[0], *p_args[1], *p_args[2]);
	}

	static void Plane_init3(Variant &r_ret, const Variant **p_args) {

		r_ret = Plane(p_args[0]->operator Vector3(), p_args[1]->operator real_t());
	}
	static void Plane_init4(Variant &r_ret, const Variant **p_args) {

		r_ret = Plane(p_args[0]->operator Vector3(), p_args[1]->operator Vector3());
	}

	static void Quat_init1(Variant &r_ret, const Variant **p_args) {

		r_ret = Quat(*p_args[0], *p_args[1], *p_args[2], *p_args[3]);
	}

	static void Quat_init2(Variant &r_ret, const Variant **p_args) {

		r_ret = Quat(((Vector3)(*p_args[0])), ((float)(*p_args[1])));
	}

	static void Color_init1(Variant &r_ret, const Variant **p_args) {

		r_ret = Color(*p_args[0], *p_args[1], *p_args[2], *p_args[3]);
	}

	static void Color_init2(Variant &r_ret, const Variant **p_args) {

		r_ret = Color(*p_args[0], *p_args[1], *p_args[2]);
	}

	static void Color_init3(Variant &r_ret, const Variant **p_args) {

		r_ret = Color::html(*p_args[0]);
	}

	static void Color_init4(Variant &r_ret, const Variant **p_args) {

		r_ret = Color::hex(*p_args[0]);
	}

	static void Rect3_init1(Variant &r_ret, const Variant **p_args) {

		r_ret = Rect3(*p_args[0], *p_args[1]);
	}

	static void Basis_init1(Variant &r_ret, const Variant **p_args) {

		Basis m;
		m.set_axis(0, *p_args[0]);
		m.set_axis(1, *p_args[1]);
		m.set_axis(2, *p_args[2]);
		r_ret = m;
	}

	static void Basis_init2(Variant &r_ret, const Variant **p_args) {

		r_ret = Basis(p_args[0]->operator Vector3(), p_args[1]->operator real_t());
	}

	static void Basis_init3(Variant &r_ret, const Variant **p_args) {

		r_ret = Basis(p_args[0]->operator Vector3());
	}

	static void Transform_init1(Variant &r_ret, const Variant **p_args) {

		Transform t;
		t.basis.set_axis(0, *p_args[0]);
		t.basis.set_axis(1, *p_args[1]);
		t.basis.set_axis(2, *p_args[2]);
		t.origin = *p_args[3];
		r_ret = t;
	}

	static void Transform_init2(Variant &r_ret, const Variant **p_args) {

		r_ret = Transform(p_args[0]->operator Basis(), p_args[1]->operator Vector3());
	}

	static void Image_init1(Variant &r_ret, const Variant **p_args) {

		r_ret = Image(*p_args[0], *p_args[1], *p_args[2], Image::Format(p_args[3]->operator int()));
	}

	static void add_constructor(VariantConstructFunc p_func, const Variant::Type p_type,
			const String &p_name1 = "", const Variant::Type p_type1 = Variant::NIL,
			const String &p_name2 = "", const Variant::Type p_type2 = Variant::NIL,
			const String &p_name3 = "", const Variant::Type p_type3 = Variant::NIL,
			const String &p_name4 = "", const Variant::Type p_type4 = Variant::NIL) {

		ConstructData cd;
		cd.func = p_func;
		cd.arg_count = 0;

		if (p_name1 == "")
			goto end;
		cd.arg_count++;
		cd.arg_names.push_back(p_name1);
		cd.arg_types.push_back(p_type1);

		if (p_name2 == "")
			goto end;
		cd.arg_count++;
		cd.arg_names.push_back(p_name2);
		cd.arg_types.push_back(p_type2);

		if (p_name3 == "")
			goto end;
		cd.arg_count++;
		cd.arg_names.push_back(p_name3);
		cd.arg_types.push_back(p_type3);

		if (p_name4 == "")
			goto end;
		cd.arg_count++;
		cd.arg_names.push_back(p_name4);
		cd.arg_types.push_back(p_type4);

	end:

		construct_funcs[p_type].constructors.push_back(cd);
	}

	struct ConstantData {

		Map<StringName, int> value;
#ifdef DEBUG_ENABLED
		List<StringName> value_ordered;
#endif
	};

	static ConstantData *constant_data;

	static void add_constant(int p_type, StringName p_constant_name, int p_constant_value) {

		constant_data[p_type].value[p_constant_name] = p_constant_value;
#ifdef DEBUG_ENABLED
		constant_data[p_type].value_ordered.push_back(p_constant_name);
#endif
	}
};

_VariantCall::TypeFunc *_VariantCall::type_funcs = NULL;
_VariantCall::ConstructFunc *_VariantCall::construct_funcs = NULL;
_VariantCall::ConstantData *_VariantCall::constant_data = NULL;

Variant Variant::call(const StringName &p_method, const Variant **p_args, int p_argcount, CallError &r_error) {

	Variant ret;
	call_ptr(p_method, p_args, p_argcount, &ret, r_error);
	return ret;
}

void Variant::call_ptr(const StringName &p_method, const Variant **p_args, int p_argcount, Variant *r_ret, CallError &r_error) {
	Variant ret;

	if (type == Variant::OBJECT) {
		//call object
		Object *obj = _get_obj().obj;
		if (!obj) {
			r_error.error = CallError::CALL_ERROR_INSTANCE_IS_NULL;
			return;
		}
#ifdef DEBUG_ENABLED
		if (ScriptDebugger::get_singleton() && _get_obj().ref.is_null()) {
			//only if debugging!
			if (!ObjectDB::instance_validate(obj)) {
				r_error.error = CallError::CALL_ERROR_INSTANCE_IS_NULL;
				return;
			}
		}

#endif
		ret = _get_obj().obj->call(p_method, p_args, p_argcount, r_error);

		//else if (type==Variant::METHOD) {

	} else {

		r_error.error = Variant::CallError::CALL_OK;

		Map<StringName, _VariantCall::FuncData>::Element *E = _VariantCall::type_funcs[type].functions.find(p_method);
#ifdef DEBUG_ENABLED
		if (!E) {
			r_error.error = Variant::CallError::CALL_ERROR_INVALID_METHOD;
			return;
		}
#endif
		_VariantCall::FuncData &funcdata = E->get();
		funcdata.call(ret, *this, p_args, p_argcount, r_error);
	}

	if (r_error.error == Variant::CallError::CALL_OK && r_ret)
		*r_ret = ret;
}

#define VCALL(m_type, m_method) _VariantCall::_call_##m_type##_##m_method

Variant Variant::construct(const Variant::Type p_type, const Variant **p_args, int p_argcount, CallError &r_error, bool p_strict) {

	r_error.error = Variant::CallError::CALL_ERROR_INVALID_METHOD;
	ERR_FAIL_INDEX_V(p_type, VARIANT_MAX, Variant());

	r_error.error = Variant::CallError::CALL_OK;
	if (p_argcount == 0) { //generic construct

		switch (p_type) {
			case NIL:
				return Variant();

			// atomic types
			case BOOL: return Variant(false);
			case INT: return 0;
			case REAL: return 0.0f;
			case STRING:
				return String();

			// math types

			case VECTOR2:
				return Vector2(); // 5
			case RECT2: return Rect2();
			case VECTOR3: return Vector3();
			case TRANSFORM2D: return Transform2D();
			case PLANE: return Plane();
			case QUAT: return Quat();
			case RECT3:
				return Rect3(); // 10
			case BASIS: return Basis();
			case TRANSFORM:
				return Transform();

			// misc types
			case COLOR: return Color();
			case IMAGE: return Image();
			case NODE_PATH:
				return NodePath(); // 15
			case _RID: return RID();
			case OBJECT: return (Object *)NULL;
			case INPUT_EVENT: return InputEvent();
			case DICTIONARY: return Dictionary();
			case ARRAY:
				return Array(); // 20
			case POOL_BYTE_ARRAY: return PoolByteArray();
			case POOL_INT_ARRAY: return PoolIntArray();
			case POOL_REAL_ARRAY: return PoolRealArray();
			case POOL_STRING_ARRAY: return PoolStringArray();
			case POOL_VECTOR2_ARRAY:
				return PoolVector2Array(); // 25
			case POOL_VECTOR3_ARRAY: return PoolVector3Array();
			case POOL_COLOR_ARRAY: return PoolColorArray();
			default: return Variant();
		}

	} else if (p_argcount > 1) {

		_VariantCall::ConstructFunc &c = _VariantCall::construct_funcs[p_type];

		for (List<_VariantCall::ConstructData>::Element *E = c.constructors.front(); E; E = E->next()) {
			const _VariantCall::ConstructData &cd = E->get();

			if (cd.arg_count != p_argcount)
				continue;

			//validate parameters
			for (int i = 0; i < cd.arg_count; i++) {
				if (!Variant::can_convert(p_args[i]->type, cd.arg_types[i])) {
					r_error.error = Variant::CallError::CALL_ERROR_INVALID_ARGUMENT; //no such constructor
					r_error.argument = i;
					r_error.expected = cd.arg_types[i];
					return Variant();
				}
			}

			Variant v;
			cd.func(v, p_args);
			return v;
		}

	} else if (p_argcount == 1 && p_args[0]->type == p_type) {
		return *p_args[0]; //copy construct
	} else if (p_argcount == 1 && (!p_strict || Variant::can_convert(p_args[0]->type, p_type))) {
		//near match construct

		switch (p_type) {
			case NIL: {

				return Variant();
			} break;
			case BOOL: {
				return Variant(bool(*p_args[0]));
			}
			case INT: {
				return (int(*p_args[0]));
			}
			case REAL: {
				return real_t(*p_args[0]);
			}
			case STRING: {
				return String(*p_args[0]);
			}
			case VECTOR2: {
				return Vector2(*p_args[0]);
			}
			case RECT2: return (Rect2(*p_args[0]));
			case VECTOR3: return (Vector3(*p_args[0]));
			case PLANE: return (Plane(*p_args[0]));
			case QUAT: return (Quat(*p_args[0]));
			case RECT3:
				return (Rect3(*p_args[0])); // 10
			case BASIS: return (Basis(p_args[0]->operator Basis()));
			case TRANSFORM:
				return (Transform(p_args[0]->operator Transform()));

			// misc types
			case COLOR: return p_args[0]->type == Variant::STRING ? Color::html(*p_args[0]) : Color::hex(*p_args[0]);
			case IMAGE: return (Image(*p_args[0]));
			case NODE_PATH:
				return (NodePath(p_args[0]->operator NodePath())); // 15
			case _RID: return (RID(*p_args[0]));
			case OBJECT: return ((Object *)(p_args[0]->operator Object *()));
			case INPUT_EVENT: return (InputEvent(*p_args[0]));
			case DICTIONARY: return p_args[0]->operator Dictionary();
			case ARRAY:
				return p_args[0]->operator Array(); // 20

			// arrays
			case POOL_BYTE_ARRAY: return (PoolByteArray(*p_args[0]));
			case POOL_INT_ARRAY: return (PoolIntArray(*p_args[0]));
			case POOL_REAL_ARRAY: return (PoolRealArray(*p_args[0]));
			case POOL_STRING_ARRAY: return (PoolStringArray(*p_args[0]));
			case POOL_VECTOR2_ARRAY:
				return (PoolVector2Array(*p_args[0])); // 25
			case POOL_VECTOR3_ARRAY: return (PoolVector3Array(*p_args[0]));
			case POOL_COLOR_ARRAY: return (PoolColorArray(*p_args[0]));
			default: return Variant();
		}
	}
	r_error.error = Variant::CallError::CALL_ERROR_INVALID_METHOD; //no such constructor
	return Variant();
}

bool Variant::has_method(const StringName &p_method) const {

	if (type == OBJECT) {
		Object *obj = operator Object *();
		if (!obj)
			return false;
#ifdef DEBUG_ENABLED
		if (ScriptDebugger::get_singleton()) {
			if (ObjectDB::instance_validate(obj)) {
#endif
				return obj->has_method(p_method);
#ifdef DEBUG_ENABLED
			}
		}
#endif
	}

	const _VariantCall::TypeFunc &fd = _VariantCall::type_funcs[type];
	return fd.functions.has(p_method);
}

Vector<Variant::Type> Variant::get_method_argument_types(Variant::Type p_type, const StringName &p_method) {

	const _VariantCall::TypeFunc &fd = _VariantCall::type_funcs[p_type];

	const Map<StringName, _VariantCall::FuncData>::Element *E = fd.functions.find(p_method);
	if (!E)
		return Vector<Variant::Type>();

	return E->get().arg_types;
}

Vector<StringName> Variant::get_method_argument_names(Variant::Type p_type, const StringName &p_method) {

	const _VariantCall::TypeFunc &fd = _VariantCall::type_funcs[p_type];

	const Map<StringName, _VariantCall::FuncData>::Element *E = fd.functions.find(p_method);
	if (!E)
		return Vector<StringName>();

	return E->get().arg_names;
}

Variant::Type Variant::get_method_return_type(Variant::Type p_type, const StringName &p_method, bool *r_has_return) {

	const _VariantCall::TypeFunc &fd = _VariantCall::type_funcs[p_type];

	const Map<StringName, _VariantCall::FuncData>::Element *E = fd.functions.find(p_method);
	if (!E)
		return Variant::NIL;

	if (r_has_return)
		*r_has_return = E->get().return_type;

	return E->get().return_type;
}

Vector<Variant> Variant::get_method_default_arguments(Variant::Type p_type, const StringName &p_method) {

	const _VariantCall::TypeFunc &fd = _VariantCall::type_funcs[p_type];

	const Map<StringName, _VariantCall::FuncData>::Element *E = fd.functions.find(p_method);
	if (!E)
		return Vector<Variant>();

	return E->get().default_args;
}

void Variant::get_method_list(List<MethodInfo> *p_list) const {

	const _VariantCall::TypeFunc &fd = _VariantCall::type_funcs[type];

	for (const Map<StringName, _VariantCall::FuncData>::Element *E = fd.functions.front(); E; E = E->next()) {

		const _VariantCall::FuncData &fd = E->get();

		MethodInfo mi;
		mi.name = E->key();

		for (int i = 0; i < fd.arg_types.size(); i++) {

			PropertyInfo pi;
			pi.type = fd.arg_types[i];
#ifdef DEBUG_ENABLED
			pi.name = fd.arg_names[i];
#endif
			mi.arguments.push_back(pi);
		}

		mi.default_arguments = fd.default_args;
		PropertyInfo ret;
#ifdef DEBUG_ENABLED
		ret.type = fd.return_type;
		if (fd.returns)
			ret.name = "ret";
		mi.return_val = ret;
#endif

		p_list->push_back(mi);
	}
}

void Variant::get_constructor_list(Variant::Type p_type, List<MethodInfo> *p_list) {

	ERR_FAIL_INDEX(p_type, VARIANT_MAX);

	//custom constructors
	for (const List<_VariantCall::ConstructData>::Element *E = _VariantCall::construct_funcs[p_type].constructors.front(); E; E = E->next()) {

		const _VariantCall::ConstructData &cd = E->get();
		MethodInfo mi;
		mi.name = Variant::get_type_name(p_type);
		mi.return_val.type = p_type;
		for (int i = 0; i < cd.arg_count; i++) {

			PropertyInfo pi;
			pi.name = cd.arg_names[i];
			pi.type = cd.arg_types[i];
			mi.arguments.push_back(pi);
		}
		p_list->push_back(mi);
	}
	//default constructors
	for (int i = 0; i < VARIANT_MAX; i++) {
		if (i == p_type)
			continue;
		if (!Variant::can_convert(Variant::Type(i), p_type))
			continue;

		MethodInfo mi;
		mi.name = Variant::get_type_name(p_type);
		PropertyInfo pi;
		pi.name = "from";
		pi.type = Variant::Type(i);
		mi.arguments.push_back(pi);
		mi.return_val.type = p_type;
		p_list->push_back(mi);
	}
}

void Variant::get_numeric_constants_for_type(Variant::Type p_type, List<StringName> *p_constants) {

	ERR_FAIL_INDEX(p_type, Variant::VARIANT_MAX);

	_VariantCall::ConstantData &cd = _VariantCall::constant_data[p_type];

#ifdef DEBUG_ENABLED
	for (List<StringName>::Element *E = cd.value_ordered.front(); E; E = E->next()) {

		p_constants->push_back(E->get());
#else
	for (Map<StringName, int>::Element *E = cd.value.front(); E; E = E->next()) {

		p_constants->push_back(E->key());
#endif
	}
}

bool Variant::has_numeric_constant(Variant::Type p_type, const StringName &p_value) {

	ERR_FAIL_INDEX_V(p_type, Variant::VARIANT_MAX, false);
	_VariantCall::ConstantData &cd = _VariantCall::constant_data[p_type];
	return cd.value.has(p_value);
}

int Variant::get_numeric_constant_value(Variant::Type p_type, const StringName &p_value, bool *r_valid) {

	if (r_valid)
		*r_valid = false;

	ERR_FAIL_INDEX_V(p_type, Variant::VARIANT_MAX, 0);
	_VariantCall::ConstantData &cd = _VariantCall::constant_data[p_type];

	Map<StringName, int>::Element *E = cd.value.find(p_value);
	if (!E) {
		return -1;
	}
	if (r_valid)
		*r_valid = true;

	return E->get();
}

void register_variant_methods() {

	_VariantCall::type_funcs = memnew_arr(_VariantCall::TypeFunc, Variant::VARIANT_MAX);

	_VariantCall::construct_funcs = memnew_arr(_VariantCall::ConstructFunc, Variant::VARIANT_MAX);
	_VariantCall::constant_data = memnew_arr(_VariantCall::ConstantData, Variant::VARIANT_MAX);

#define ADDFUNC0(m_vtype, m_ret, m_class, m_method, m_defarg) \
	_VariantCall::addfunc(Variant::m_vtype, Variant::m_ret, _scs_create(#m_method), VCALL(m_class, m_method), m_defarg);
#define ADDFUNC1(m_vtype, m_ret, m_class, m_method, m_arg1, m_argname1, m_defarg) \
	_VariantCall::addfunc(Variant::m_vtype, Variant::m_ret, _scs_create(#m_method), VCALL(m_class, m_method), m_defarg, _VariantCall::Arg(Variant::m_arg1, _scs_create(m_argname1)));
#define ADDFUNC2(m_vtype, m_ret, m_class, m_method, m_arg1, m_argname1, m_arg2, m_argname2, m_defarg) \
	_VariantCall::addfunc(Variant::m_vtype, Variant::m_ret, _scs_create(#m_method), VCALL(m_class, m_method), m_defarg, _VariantCall::Arg(Variant::m_arg1, _scs_create(m_argname1)), _VariantCall::Arg(Variant::m_arg2, _scs_create(m_argname2)));
#define ADDFUNC3(m_vtype, m_ret, m_class, m_method, m_arg1, m_argname1, m_arg2, m_argname2, m_arg3, m_argname3, m_defarg) \
	_VariantCall::addfunc(Variant::m_vtype, Variant::m_ret, _scs_create(#m_method), VCALL(m_class, m_method), m_defarg, _VariantCall::Arg(Variant::m_arg1, _scs_create(m_argname1)), _VariantCall::Arg(Variant::m_arg2, _scs_create(m_argname2)), _VariantCall::Arg(Variant::m_arg3, _scs_create(m_argname3)));
#define ADDFUNC4(m_vtype, m_ret, m_class, m_method, m_arg1, m_argname1, m_arg2, m_argname2, m_arg3, m_argname3, m_arg4, m_argname4, m_defarg) \
	_VariantCall::addfunc(Variant::m_vtype, Variant::m_ret, _scs_create(#m_method), VCALL(m_class, m_method), m_defarg, _VariantCall::Arg(Variant::m_arg1, _scs_create(m_argname1)), _VariantCall::Arg(Variant::m_arg2, _scs_create(m_argname2)), _VariantCall::Arg(Variant::m_arg3, _scs_create(m_argname3)), _VariantCall::Arg(Variant::m_arg4, _scs_create(m_argname4)));

	/* STRING */
	ADDFUNC1(STRING, INT, String, casecmp_to, STRING, "to", varray());
	ADDFUNC1(STRING, INT, String, nocasecmp_to, STRING, "to", varray());
	ADDFUNC0(STRING, INT, String, length, varray());
	ADDFUNC2(STRING, STRING, String, substr, INT, "from", INT, "len", varray());

	ADDFUNC2(STRING, INT, String, find, STRING, "what", INT, "from", varray(0));

	ADDFUNC1(STRING, INT, String, find_last, STRING, "what", varray());
	ADDFUNC2(STRING, INT, String, findn, STRING, "what", INT, "from", varray(0));
	ADDFUNC2(STRING, INT, String, rfind, STRING, "what", INT, "from", varray(-1));
	ADDFUNC2(STRING, INT, String, rfindn, STRING, "what", INT, "from", varray(-1));
	ADDFUNC1(STRING, BOOL, String, match, STRING, "expr", varray());
	ADDFUNC1(STRING, BOOL, String, matchn, STRING, "expr", varray());
	ADDFUNC1(STRING, BOOL, String, begins_with, STRING, "text", varray());
	ADDFUNC1(STRING, BOOL, String, ends_with, STRING, "text", varray());
	ADDFUNC1(STRING, BOOL, String, is_subsequence_of, STRING, "text", varray());
	ADDFUNC1(STRING, BOOL, String, is_subsequence_ofi, STRING, "text", varray());
	ADDFUNC0(STRING, POOL_STRING_ARRAY, String, bigrams, varray());
	ADDFUNC1(STRING, REAL, String, similarity, STRING, "text", varray());

	ADDFUNC2(STRING, STRING, String, format, NIL, "values", STRING, "placeholder", varray("{_}"));
	ADDFUNC2(STRING, STRING, String, replace, STRING, "what", STRING, "forwhat", varray());
	ADDFUNC2(STRING, STRING, String, replacen, STRING, "what", STRING, "forwhat", varray());
	ADDFUNC2(STRING, STRING, String, insert, INT, "pos", STRING, "what", varray());
	ADDFUNC0(STRING, STRING, String, capitalize, varray());
	ADDFUNC2(STRING, POOL_STRING_ARRAY, String, split, STRING, "divisor", BOOL, "allow_empty", varray(true));
	ADDFUNC2(STRING, POOL_REAL_ARRAY, String, split_floats, STRING, "divisor", BOOL, "allow_empty", varray(true));

	ADDFUNC0(STRING, STRING, String, to_upper, varray());
	ADDFUNC0(STRING, STRING, String, to_lower, varray());

	ADDFUNC1(STRING, STRING, String, left, INT, "pos", varray());
	ADDFUNC1(STRING, STRING, String, right, INT, "pos", varray());
	ADDFUNC2(STRING, STRING, String, strip_edges, BOOL, "left", BOOL, "right", varray(true, true));
	ADDFUNC0(STRING, STRING, String, get_extension, varray());
	ADDFUNC0(STRING, STRING, String, get_basename, varray());
	ADDFUNC1(STRING, STRING, String, plus_file, STRING, "file", varray());
	ADDFUNC1(STRING, INT, String, ord_at, INT, "at", varray());
	ADDFUNC2(STRING, NIL, String, erase, INT, "pos", INT, "chars", varray());
	ADDFUNC0(STRING, INT, String, hash, varray());
	ADDFUNC0(STRING, STRING, String, md5_text, varray());
	ADDFUNC0(STRING, STRING, String, sha256_text, varray());
	ADDFUNC0(STRING, POOL_BYTE_ARRAY, String, md5_buffer, varray());
	ADDFUNC0(STRING, POOL_BYTE_ARRAY, String, sha256_buffer, varray());
	ADDFUNC0(STRING, BOOL, String, empty, varray());
	ADDFUNC0(STRING, BOOL, String, is_abs_path, varray());
	ADDFUNC0(STRING, BOOL, String, is_rel_path, varray());
	ADDFUNC0(STRING, STRING, String, get_base_dir, varray());
	ADDFUNC0(STRING, STRING, String, get_file, varray());
	ADDFUNC0(STRING, STRING, String, xml_escape, varray());
	ADDFUNC0(STRING, STRING, String, xml_unescape, varray());
	ADDFUNC0(STRING, STRING, String, c_escape, varray());
	ADDFUNC0(STRING, STRING, String, c_unescape, varray());
	ADDFUNC0(STRING, STRING, String, json_escape, varray());
	ADDFUNC0(STRING, STRING, String, percent_encode, varray());
	ADDFUNC0(STRING, STRING, String, percent_decode, varray());
	ADDFUNC0(STRING, BOOL, String, is_valid_identifier, varray());
	ADDFUNC0(STRING, BOOL, String, is_valid_integer, varray());
	ADDFUNC0(STRING, BOOL, String, is_valid_float, varray());
	ADDFUNC0(STRING, BOOL, String, is_valid_html_color, varray());
	ADDFUNC0(STRING, BOOL, String, is_valid_ip_address, varray());
	ADDFUNC0(STRING, INT, String, to_int, varray());
	ADDFUNC0(STRING, REAL, String, to_float, varray());
	ADDFUNC0(STRING, INT, String, hex_to_int, varray());
	ADDFUNC1(STRING, STRING, String, pad_decimals, INT, "digits", varray());
	ADDFUNC1(STRING, STRING, String, pad_zeros, INT, "digits", varray());

	ADDFUNC0(STRING, POOL_BYTE_ARRAY, String, to_ascii, varray());
	ADDFUNC0(STRING, POOL_BYTE_ARRAY, String, to_utf8, varray());

	ADDFUNC0(VECTOR2, VECTOR2, Vector2, normalized, varray());
	ADDFUNC0(VECTOR2, REAL, Vector2, length, varray());
	ADDFUNC0(VECTOR2, REAL, Vector2, angle, varray());
	ADDFUNC0(VECTOR2, REAL, Vector2, length_squared, varray());
	ADDFUNC0(VECTOR2, BOOL, Vector2, is_normalized, varray());
	ADDFUNC1(VECTOR2, REAL, Vector2, distance_to, VECTOR2, "to", varray());
	ADDFUNC1(VECTOR2, REAL, Vector2, distance_squared_to, VECTOR2, "to", varray());
	ADDFUNC1(VECTOR2, REAL, Vector2, angle_to, VECTOR2, "to", varray());
	ADDFUNC1(VECTOR2, REAL, Vector2, angle_to_point, VECTOR2, "to", varray());
	ADDFUNC2(VECTOR2, VECTOR2, Vector2, linear_interpolate, VECTOR2, "b", REAL, "t", varray());
	ADDFUNC4(VECTOR2, VECTOR2, Vector2, cubic_interpolate, VECTOR2, "b", VECTOR2, "pre_a", VECTOR2, "post_b", REAL, "t", varray());
	ADDFUNC1(VECTOR2, VECTOR2, Vector2, rotated, REAL, "phi", varray());
	ADDFUNC0(VECTOR2, VECTOR2, Vector2, tangent, varray());
	ADDFUNC0(VECTOR2, VECTOR2, Vector2, floor, varray());
	ADDFUNC1(VECTOR2, VECTOR2, Vector2, snapped, VECTOR2, "by", varray());
	ADDFUNC0(VECTOR2, REAL, Vector2, aspect, varray());
	ADDFUNC1(VECTOR2, REAL, Vector2, dot, VECTOR2, "with", varray());
	ADDFUNC1(VECTOR2, VECTOR2, Vector2, slide, VECTOR2, "n", varray());
	ADDFUNC1(VECTOR2, VECTOR2, Vector2, bounce, VECTOR2, "n", varray());
	ADDFUNC1(VECTOR2, VECTOR2, Vector2, reflect, VECTOR2, "n", varray());
	//ADDFUNC1(VECTOR2,REAL,Vector2,cross,VECTOR2,"with",varray());
	ADDFUNC0(VECTOR2, VECTOR2, Vector2, abs, varray());
	ADDFUNC1(VECTOR2, VECTOR2, Vector2, clamped, REAL, "length", varray());

	ADDFUNC0(RECT2, REAL, Rect2, get_area, varray());
	ADDFUNC1(RECT2, BOOL, Rect2, intersects, RECT2, "b", varray());
	ADDFUNC1(RECT2, BOOL, Rect2, encloses, RECT2, "b", varray());
	ADDFUNC0(RECT2, BOOL, Rect2, has_no_area, varray());
	ADDFUNC1(RECT2, RECT2, Rect2, clip, RECT2, "b", varray());
	ADDFUNC1(RECT2, RECT2, Rect2, merge, RECT2, "b", varray());
	ADDFUNC1(RECT2, BOOL, Rect2, has_point, VECTOR2, "point", varray());
	ADDFUNC1(RECT2, RECT2, Rect2, grow, REAL, "by", varray());
	ADDFUNC1(RECT2, RECT2, Rect2, expand, VECTOR2, "to", varray());

	ADDFUNC0(VECTOR3, INT, Vector3, min_axis, varray());
	ADDFUNC0(VECTOR3, INT, Vector3, max_axis, varray());
	ADDFUNC0(VECTOR3, REAL, Vector3, length, varray());
	ADDFUNC0(VECTOR3, REAL, Vector3, length_squared, varray());
	ADDFUNC0(VECTOR3, BOOL, Vector3, is_normalized, varray());
	ADDFUNC0(VECTOR3, VECTOR3, Vector3, normalized, varray());
	ADDFUNC0(VECTOR3, VECTOR3, Vector3, inverse, varray());
	ADDFUNC1(VECTOR3, VECTOR3, Vector3, snapped, REAL, "by", varray());
	ADDFUNC2(VECTOR3, VECTOR3, Vector3, rotated, VECTOR3, "axis", REAL, "phi", varray());
	ADDFUNC2(VECTOR3, VECTOR3, Vector3, linear_interpolate, VECTOR3, "b", REAL, "t", varray());
	ADDFUNC4(VECTOR3, VECTOR3, Vector3, cubic_interpolate, VECTOR3, "b", VECTOR3, "pre_a", VECTOR3, "post_b", REAL, "t", varray());
	ADDFUNC1(VECTOR3, REAL, Vector3, dot, VECTOR3, "b", varray());
	ADDFUNC1(VECTOR3, VECTOR3, Vector3, cross, VECTOR3, "b", varray());
	ADDFUNC1(VECTOR3, BASIS, Vector3, outer, VECTOR3, "b", varray());
	ADDFUNC0(VECTOR3, BASIS, Vector3, to_diagonal_matrix, varray());
	ADDFUNC0(VECTOR3, VECTOR3, Vector3, abs, varray());
	ADDFUNC0(VECTOR3, VECTOR3, Vector3, abs, varray());
	ADDFUNC0(VECTOR3, VECTOR3, Vector3, floor, varray());
	ADDFUNC0(VECTOR3, VECTOR3, Vector3, ceil, varray());
	ADDFUNC1(VECTOR3, REAL, Vector3, distance_to, VECTOR3, "b", varray());
	ADDFUNC1(VECTOR3, REAL, Vector3, distance_squared_to, VECTOR3, "b", varray());
	ADDFUNC1(VECTOR3, REAL, Vector3, angle_to, VECTOR3, "to", varray());
	ADDFUNC1(VECTOR3, VECTOR3, Vector3, slide, VECTOR3, "n", varray());
	ADDFUNC1(VECTOR3, VECTOR3, Vector3, bounce, VECTOR3, "n", varray());
	ADDFUNC1(VECTOR3, VECTOR3, Vector3, reflect, VECTOR3, "n", varray());

	ADDFUNC0(PLANE, PLANE, Plane, normalized, varray());
	ADDFUNC0(PLANE, VECTOR3, Plane, center, varray());
	ADDFUNC0(PLANE, VECTOR3, Plane, get_any_point, varray());
	ADDFUNC1(PLANE, BOOL, Plane, is_point_over, VECTOR3, "point", varray());
	ADDFUNC1(PLANE, REAL, Plane, distance_to, VECTOR3, "point", varray());
	ADDFUNC2(PLANE, BOOL, Plane, has_point, VECTOR3, "point", REAL, "epsilon", varray(CMP_EPSILON));
	ADDFUNC1(PLANE, VECTOR3, Plane, project, VECTOR3, "point", varray());
	ADDFUNC2(PLANE, VECTOR3, Plane, intersect_3, PLANE, "b", PLANE, "c", varray());
	ADDFUNC2(PLANE, VECTOR3, Plane, intersects_ray, VECTOR3, "from", VECTOR3, "dir", varray());
	ADDFUNC2(PLANE, VECTOR3, Plane, intersects_segment, VECTOR3, "begin", VECTOR3, "end", varray());

	ADDFUNC0(QUAT, REAL, Quat, length, varray());
	ADDFUNC0(QUAT, REAL, Quat, length_squared, varray());
	ADDFUNC0(QUAT, QUAT, Quat, normalized, varray());
	ADDFUNC0(QUAT, BOOL, Quat, is_normalized, varray());
	ADDFUNC0(QUAT, QUAT, Quat, inverse, varray());
	ADDFUNC1(QUAT, REAL, Quat, dot, QUAT, "b", varray());
	ADDFUNC1(QUAT, VECTOR3, Quat, xform, VECTOR3, "v", varray());
	ADDFUNC2(QUAT, QUAT, Quat, slerp, QUAT, "b", REAL, "t", varray());
	ADDFUNC2(QUAT, QUAT, Quat, slerpni, QUAT, "b", REAL, "t", varray());
	ADDFUNC4(QUAT, QUAT, Quat, cubic_slerp, QUAT, "b", QUAT, "pre_a", QUAT, "post_b", REAL, "t", varray());

	ADDFUNC0(COLOR, INT, Color, to_32, varray());
	ADDFUNC0(COLOR, INT, Color, to_ARGB32, varray());
	ADDFUNC0(COLOR, REAL, Color, gray, varray());
	ADDFUNC0(COLOR, COLOR, Color, inverted, varray());
	ADDFUNC0(COLOR, COLOR, Color, contrasted, varray());
	ADDFUNC2(COLOR, COLOR, Color, linear_interpolate, COLOR, "b", REAL, "t", varray());
	ADDFUNC1(COLOR, COLOR, Color, blend, COLOR, "over", varray());
	ADDFUNC1(COLOR, STRING, Color, to_html, BOOL, "with_alpha", varray(true));

	ADDFUNC0(IMAGE, INT, Image, get_format, varray());
	ADDFUNC0(IMAGE, INT, Image, get_width, varray());
	ADDFUNC0(IMAGE, INT, Image, get_height, varray());
	ADDFUNC0(IMAGE, BOOL, Image, empty, varray());
	ADDFUNC1(IMAGE, INT, Image, load, STRING, "path", varray(0));
	ADDFUNC1(IMAGE, INT, Image, save_png, STRING, "path", varray(0));
	ADDFUNC0(IMAGE, RECT2, Image, get_used_rect, varray(0));
	ADDFUNC1(IMAGE, IMAGE, Image, get_rect, RECT2, "area", varray(0));
	ADDFUNC1(IMAGE, IMAGE, Image, compressed, INT, "format", varray(0));
	ADDFUNC0(IMAGE, IMAGE, Image, decompressed, varray(0));
	ADDFUNC3(IMAGE, IMAGE, Image, resized, INT, "x", INT, "y", INT, "interpolation", varray(((int)Image::INTERPOLATE_BILINEAR)));
	ADDFUNC0(IMAGE, POOL_BYTE_ARRAY, Image, get_data, varray());
	ADDFUNC3(IMAGE, NIL, Image, blit_rect, IMAGE, "src", RECT2, "src_rect", VECTOR2, "dest", varray(0));
	ADDFUNC1(IMAGE, IMAGE, Image, converted, INT, "format", varray(0));
	ADDFUNC0(IMAGE, NIL, Image, fix_alpha_edges, varray());

	ADDFUNC0(_RID, INT, RID, get_id, varray());

	ADDFUNC0(NODE_PATH, BOOL, NodePath, is_absolute, varray());
	ADDFUNC0(NODE_PATH, INT, NodePath, get_name_count, varray());
	ADDFUNC1(NODE_PATH, STRING, NodePath, get_name, INT, "idx", varray());
	ADDFUNC0(NODE_PATH, INT, NodePath, get_subname_count, varray());
	ADDFUNC1(NODE_PATH, STRING, NodePath, get_subname, INT, "idx", varray());
	ADDFUNC0(NODE_PATH, STRING, NodePath, get_property, varray());
	ADDFUNC0(NODE_PATH, BOOL, NodePath, is_empty, varray());

	ADDFUNC0(DICTIONARY, INT, Dictionary, size, varray());
	ADDFUNC0(DICTIONARY, BOOL, Dictionary, empty, varray());
	ADDFUNC0(DICTIONARY, NIL, Dictionary, clear, varray());
	ADDFUNC1(DICTIONARY, BOOL, Dictionary, has, NIL, "key", varray());
	ADDFUNC1(DICTIONARY, BOOL, Dictionary, has_all, ARRAY, "keys", varray());
	ADDFUNC1(DICTIONARY, NIL, Dictionary, erase, NIL, "key", varray());
	ADDFUNC0(DICTIONARY, INT, Dictionary, hash, varray());
	ADDFUNC0(DICTIONARY, ARRAY, Dictionary, keys, varray());
	ADDFUNC0(DICTIONARY, ARRAY, Dictionary, values, varray());

	ADDFUNC0(ARRAY, INT, Array, size, varray());
	ADDFUNC0(ARRAY, BOOL, Array, empty, varray());
	ADDFUNC0(ARRAY, NIL, Array, clear, varray());
	ADDFUNC0(ARRAY, INT, Array, hash, varray());
	ADDFUNC1(ARRAY, NIL, Array, push_back, NIL, "value", varray());
	ADDFUNC1(ARRAY, NIL, Array, push_front, NIL, "value", varray());
	ADDFUNC1(ARRAY, NIL, Array, append, NIL, "value", varray());
	ADDFUNC1(ARRAY, NIL, Array, resize, INT, "pos", varray());
	ADDFUNC2(ARRAY, NIL, Array, insert, INT, "pos", NIL, "value", varray());
	ADDFUNC1(ARRAY, NIL, Array, remove, INT, "pos", varray());
	ADDFUNC1(ARRAY, NIL, Array, erase, NIL, "value", varray());
	ADDFUNC0(ARRAY, NIL, Array, front, varray());
	ADDFUNC0(ARRAY, NIL, Array, back, varray());
	ADDFUNC2(ARRAY, INT, Array, find, NIL, "what", INT, "from", varray(0));
	ADDFUNC2(ARRAY, INT, Array, rfind, NIL, "what", INT, "from", varray(-1));
	ADDFUNC1(ARRAY, INT, Array, find_last, NIL, "value", varray());
	ADDFUNC1(ARRAY, INT, Array, count, NIL, "value", varray());
	ADDFUNC1(ARRAY, BOOL, Array, has, NIL, "value", varray());
	ADDFUNC0(ARRAY, NIL, Array, pop_back, varray());
	ADDFUNC0(ARRAY, NIL, Array, pop_front, varray());
	ADDFUNC0(ARRAY, NIL, Array, sort, varray());
	ADDFUNC2(ARRAY, NIL, Array, sort_custom, OBJECT, "obj", STRING, "func", varray());
	ADDFUNC0(ARRAY, NIL, Array, invert, varray());

	ADDFUNC0(POOL_BYTE_ARRAY, INT, PoolByteArray, size, varray());
	ADDFUNC2(POOL_BYTE_ARRAY, NIL, PoolByteArray, set, INT, "idx", INT, "byte", varray());
	ADDFUNC1(POOL_BYTE_ARRAY, NIL, PoolByteArray, push_back, INT, "byte", varray());
	ADDFUNC1(POOL_BYTE_ARRAY, NIL, PoolByteArray, append, INT, "byte", varray());
	ADDFUNC1(POOL_BYTE_ARRAY, NIL, PoolByteArray, append_array, POOL_BYTE_ARRAY, "array", varray());
	ADDFUNC1(POOL_BYTE_ARRAY, NIL, PoolByteArray, remove, INT, "idx", varray());
	ADDFUNC2(POOL_BYTE_ARRAY, INT, PoolByteArray, insert, INT, "idx", INT, "byte", varray());
	ADDFUNC1(POOL_BYTE_ARRAY, NIL, PoolByteArray, resize, INT, "idx", varray());
	ADDFUNC0(POOL_BYTE_ARRAY, NIL, PoolByteArray, invert, varray());
	ADDFUNC2(POOL_BYTE_ARRAY, POOL_BYTE_ARRAY, PoolByteArray, subarray, INT, "from", INT, "to", varray());

	ADDFUNC0(POOL_BYTE_ARRAY, STRING, PoolByteArray, get_string_from_ascii, varray());
	ADDFUNC0(POOL_BYTE_ARRAY, STRING, PoolByteArray, get_string_from_utf8, varray());

	ADDFUNC0(POOL_INT_ARRAY, INT, PoolIntArray, size, varray());
	ADDFUNC2(POOL_INT_ARRAY, NIL, PoolIntArray, set, INT, "idx", INT, "integer", varray());
	ADDFUNC1(POOL_INT_ARRAY, NIL, PoolIntArray, push_back, INT, "integer", varray());
	ADDFUNC1(POOL_INT_ARRAY, NIL, PoolIntArray, append, INT, "integer", varray());
	ADDFUNC1(POOL_INT_ARRAY, NIL, PoolIntArray, append_array, POOL_INT_ARRAY, "array", varray());
	ADDFUNC1(POOL_INT_ARRAY, NIL, PoolIntArray, remove, INT, "idx", varray());
	ADDFUNC2(POOL_INT_ARRAY, INT, PoolIntArray, insert, INT, "idx", INT, "integer", varray());
	ADDFUNC1(POOL_INT_ARRAY, NIL, PoolIntArray, resize, INT, "idx", varray());
	ADDFUNC0(POOL_INT_ARRAY, NIL, PoolIntArray, invert, varray());

	ADDFUNC0(POOL_REAL_ARRAY, INT, PoolRealArray, size, varray());
	ADDFUNC2(POOL_REAL_ARRAY, NIL, PoolRealArray, set, INT, "idx", REAL, "value", varray());
	ADDFUNC1(POOL_REAL_ARRAY, NIL, PoolRealArray, push_back, REAL, "value", varray());
	ADDFUNC1(POOL_REAL_ARRAY, NIL, PoolRealArray, append, REAL, "value", varray());
	ADDFUNC1(POOL_REAL_ARRAY, NIL, PoolRealArray, append_array, POOL_REAL_ARRAY, "array", varray());
	ADDFUNC1(POOL_REAL_ARRAY, NIL, PoolRealArray, remove, INT, "idx", varray());
	ADDFUNC2(POOL_REAL_ARRAY, INT, PoolRealArray, insert, INT, "idx", REAL, "value", varray());
	ADDFUNC1(POOL_REAL_ARRAY, NIL, PoolRealArray, resize, INT, "idx", varray());
	ADDFUNC0(POOL_REAL_ARRAY, NIL, PoolRealArray, invert, varray());

	ADDFUNC0(POOL_STRING_ARRAY, INT, PoolStringArray, size, varray());
	ADDFUNC2(POOL_STRING_ARRAY, NIL, PoolStringArray, set, INT, "idx", STRING, "string", varray());
	ADDFUNC1(POOL_STRING_ARRAY, NIL, PoolStringArray, push_back, STRING, "string", varray());
	ADDFUNC1(POOL_STRING_ARRAY, NIL, PoolStringArray, append, STRING, "string", varray());
	ADDFUNC1(POOL_STRING_ARRAY, NIL, PoolStringArray, append_array, POOL_STRING_ARRAY, "array", varray());
	ADDFUNC1(POOL_STRING_ARRAY, NIL, PoolStringArray, remove, INT, "idx", varray());
	ADDFUNC2(POOL_STRING_ARRAY, INT, PoolStringArray, insert, INT, "idx", STRING, "string", varray());
	ADDFUNC1(POOL_STRING_ARRAY, NIL, PoolStringArray, resize, INT, "idx", varray());
	ADDFUNC0(POOL_STRING_ARRAY, NIL, PoolStringArray, invert, varray());
	ADDFUNC1(POOL_STRING_ARRAY, STRING, PoolStringArray, join, STRING, "string", varray());

	ADDFUNC0(POOL_VECTOR2_ARRAY, INT, PoolVector2Array, size, varray());
	ADDFUNC2(POOL_VECTOR2_ARRAY, NIL, PoolVector2Array, set, INT, "idx", VECTOR2, "vector2", varray());
	ADDFUNC1(POOL_VECTOR2_ARRAY, NIL, PoolVector2Array, push_back, VECTOR2, "vector2", varray());
	ADDFUNC1(POOL_VECTOR2_ARRAY, NIL, PoolVector2Array, append, VECTOR2, "vector2", varray());
	ADDFUNC1(POOL_VECTOR2_ARRAY, NIL, PoolVector2Array, append_array, POOL_VECTOR2_ARRAY, "array", varray());
	ADDFUNC1(POOL_VECTOR2_ARRAY, NIL, PoolVector2Array, remove, INT, "idx", varray());
	ADDFUNC2(POOL_VECTOR2_ARRAY, INT, PoolVector2Array, insert, INT, "idx", VECTOR2, "vector2", varray());
	ADDFUNC1(POOL_VECTOR2_ARRAY, NIL, PoolVector2Array, resize, INT, "idx", varray());
	ADDFUNC0(POOL_VECTOR2_ARRAY, NIL, PoolVector2Array, invert, varray());

	ADDFUNC0(POOL_VECTOR3_ARRAY, INT, PoolVector3Array, size, varray());
	ADDFUNC2(POOL_VECTOR3_ARRAY, NIL, PoolVector3Array, set, INT, "idx", VECTOR3, "vector3", varray());
	ADDFUNC1(POOL_VECTOR3_ARRAY, NIL, PoolVector3Array, push_back, VECTOR3, "vector3", varray());
	ADDFUNC1(POOL_VECTOR3_ARRAY, NIL, PoolVector3Array, append, VECTOR3, "vector3", varray());
	ADDFUNC1(POOL_VECTOR3_ARRAY, NIL, PoolVector3Array, append_array, POOL_VECTOR3_ARRAY, "array", varray());
	ADDFUNC1(POOL_VECTOR3_ARRAY, NIL, PoolVector3Array, remove, INT, "idx", varray());
	ADDFUNC2(POOL_VECTOR3_ARRAY, INT, PoolVector3Array, insert, INT, "idx", VECTOR3, "vector3", varray());
	ADDFUNC1(POOL_VECTOR3_ARRAY, NIL, PoolVector3Array, resize, INT, "idx", varray());
	ADDFUNC0(POOL_VECTOR3_ARRAY, NIL, PoolVector3Array, invert, varray());

	ADDFUNC0(POOL_COLOR_ARRAY, INT, PoolColorArray, size, varray());
	ADDFUNC2(POOL_COLOR_ARRAY, NIL, PoolColorArray, set, INT, "idx", COLOR, "color", varray());
	ADDFUNC1(POOL_COLOR_ARRAY, NIL, PoolColorArray, push_back, COLOR, "color", varray());
	ADDFUNC1(POOL_COLOR_ARRAY, NIL, PoolColorArray, append, COLOR, "color", varray());
	ADDFUNC1(POOL_COLOR_ARRAY, NIL, PoolColorArray, append_array, POOL_COLOR_ARRAY, "array", varray());
	ADDFUNC1(POOL_COLOR_ARRAY, NIL, PoolColorArray, remove, INT, "idx", varray());
	ADDFUNC2(POOL_COLOR_ARRAY, INT, PoolColorArray, insert, INT, "idx", COLOR, "color", varray());
	ADDFUNC1(POOL_COLOR_ARRAY, NIL, PoolColorArray, resize, INT, "idx", varray());
	ADDFUNC0(POOL_COLOR_ARRAY, NIL, PoolColorArray, invert, varray());

	//pointerbased

	ADDFUNC0(RECT3, REAL, Rect3, get_area, varray());
	ADDFUNC0(RECT3, BOOL, Rect3, has_no_area, varray());
	ADDFUNC0(RECT3, BOOL, Rect3, has_no_surface, varray());
	ADDFUNC1(RECT3, BOOL, Rect3, intersects, RECT3, "with", varray());
	ADDFUNC1(RECT3, BOOL, Rect3, encloses, RECT3, "with", varray());
	ADDFUNC1(RECT3, RECT3, Rect3, merge, RECT3, "with", varray());
	ADDFUNC1(RECT3, RECT3, Rect3, intersection, RECT3, "with", varray());
	ADDFUNC1(RECT3, BOOL, Rect3, intersects_plane, PLANE, "plane", varray());
	ADDFUNC2(RECT3, BOOL, Rect3, intersects_segment, VECTOR3, "from", VECTOR3, "to", varray());
	ADDFUNC1(RECT3, BOOL, Rect3, has_point, VECTOR3, "point", varray());
	ADDFUNC1(RECT3, VECTOR3, Rect3, get_support, VECTOR3, "dir", varray());
	ADDFUNC0(RECT3, VECTOR3, Rect3, get_longest_axis, varray());
	ADDFUNC0(RECT3, INT, Rect3, get_longest_axis_index, varray());
	ADDFUNC0(RECT3, REAL, Rect3, get_longest_axis_size, varray());
	ADDFUNC0(RECT3, VECTOR3, Rect3, get_shortest_axis, varray());
	ADDFUNC0(RECT3, INT, Rect3, get_shortest_axis_index, varray());
	ADDFUNC0(RECT3, REAL, Rect3, get_shortest_axis_size, varray());
	ADDFUNC1(RECT3, RECT3, Rect3, expand, VECTOR3, "to_point", varray());
	ADDFUNC1(RECT3, RECT3, Rect3, grow, REAL, "by", varray());
	ADDFUNC1(RECT3, VECTOR3, Rect3, get_endpoint, INT, "idx", varray());

	ADDFUNC0(TRANSFORM2D, TRANSFORM2D, Transform2D, inverse, varray());
	ADDFUNC0(TRANSFORM2D, TRANSFORM2D, Transform2D, affine_inverse, varray());
	ADDFUNC0(TRANSFORM2D, REAL, Transform2D, get_rotation, varray());
	ADDFUNC0(TRANSFORM2D, VECTOR2, Transform2D, get_origin, varray());
	ADDFUNC0(TRANSFORM2D, VECTOR2, Transform2D, get_scale, varray());
	ADDFUNC0(TRANSFORM2D, TRANSFORM2D, Transform2D, orthonormalized, varray());
	ADDFUNC1(TRANSFORM2D, TRANSFORM2D, Transform2D, rotated, REAL, "phi", varray());
	ADDFUNC1(TRANSFORM2D, TRANSFORM2D, Transform2D, scaled, VECTOR2, "scale", varray());
	ADDFUNC1(TRANSFORM2D, TRANSFORM2D, Transform2D, translated, VECTOR2, "offset", varray());
	ADDFUNC1(TRANSFORM2D, TRANSFORM2D, Transform2D, xform, NIL, "v", varray());
	ADDFUNC1(TRANSFORM2D, TRANSFORM2D, Transform2D, xform_inv, NIL, "v", varray());
	ADDFUNC1(TRANSFORM2D, TRANSFORM2D, Transform2D, basis_xform, NIL, "v", varray());
	ADDFUNC1(TRANSFORM2D, TRANSFORM2D, Transform2D, basis_xform_inv, NIL, "v", varray());
	ADDFUNC2(TRANSFORM2D, TRANSFORM2D, Transform2D, interpolate_with, TRANSFORM2D, "m", REAL, "c", varray());

	ADDFUNC0(BASIS, BASIS, Basis, inverse, varray());
	ADDFUNC0(BASIS, BASIS, Basis, transposed, varray());
	ADDFUNC0(BASIS, BASIS, Basis, orthonormalized, varray());
	ADDFUNC0(BASIS, REAL, Basis, determinant, varray());
	ADDFUNC2(BASIS, BASIS, Basis, rotated, VECTOR3, "axis", REAL, "phi", varray());
	ADDFUNC1(BASIS, BASIS, Basis, scaled, VECTOR3, "scale", varray());
	ADDFUNC1(BASIS, NIL, Basis, set_scale, VECTOR3, "scale", varray());
	ADDFUNC1(BASIS, NIL, Basis, set_rotation_euler, VECTOR3, "euler", varray());
	ADDFUNC2(BASIS, NIL, Basis, set_rotation_axis_angle, VECTOR3, "axis", REAL, "angle", varray());
	ADDFUNC0(BASIS, VECTOR3, Basis, get_scale, varray());
	ADDFUNC0(BASIS, VECTOR3, Basis, get_euler, varray());
	ADDFUNC1(BASIS, REAL, Basis, tdotx, VECTOR3, "with", varray());
	ADDFUNC1(BASIS, REAL, Basis, tdoty, VECTOR3, "with", varray());
	ADDFUNC1(BASIS, REAL, Basis, tdotz, VECTOR3, "with", varray());
	ADDFUNC1(BASIS, VECTOR3, Basis, xform, VECTOR3, "v", varray());
	ADDFUNC1(BASIS, VECTOR3, Basis, xform_inv, VECTOR3, "v", varray());
	ADDFUNC0(BASIS, INT, Basis, get_orthogonal_index, varray());

	ADDFUNC0(TRANSFORM, TRANSFORM, Transform, inverse, varray());
	ADDFUNC0(TRANSFORM, TRANSFORM, Transform, affine_inverse, varray());
	ADDFUNC0(TRANSFORM, TRANSFORM, Transform, orthonormalized, varray());
	ADDFUNC2(TRANSFORM, TRANSFORM, Transform, rotated, VECTOR3, "axis", REAL, "phi", varray());
	ADDFUNC1(TRANSFORM, TRANSFORM, Transform, scaled, VECTOR3, "scale", varray());
	ADDFUNC1(TRANSFORM, TRANSFORM, Transform, translated, VECTOR3, "ofs", varray());
	ADDFUNC2(TRANSFORM, TRANSFORM, Transform, looking_at, VECTOR3, "target", VECTOR3, "up", varray());
	ADDFUNC1(TRANSFORM, NIL, Transform, xform, NIL, "v", varray());
	ADDFUNC1(TRANSFORM, NIL, Transform, xform_inv, NIL, "v", varray());

#ifdef DEBUG_ENABLED
	_VariantCall::type_funcs[Variant::TRANSFORM].functions["xform"].returns = true;
	_VariantCall::type_funcs[Variant::TRANSFORM].functions["xform_inv"].returns = true;
#endif

	ADDFUNC0(INPUT_EVENT, BOOL, InputEvent, is_pressed, varray());
	ADDFUNC1(INPUT_EVENT, BOOL, InputEvent, is_action, STRING, "action", varray());
	ADDFUNC1(INPUT_EVENT, BOOL, InputEvent, is_action_pressed, STRING, "action", varray());
	ADDFUNC1(INPUT_EVENT, BOOL, InputEvent, is_action_released, STRING, "action", varray());
	ADDFUNC0(INPUT_EVENT, BOOL, InputEvent, is_echo, varray());
	ADDFUNC2(INPUT_EVENT, NIL, InputEvent, set_as_action, STRING, "action", BOOL, "pressed", varray());

	/* REGISTER CONSTRUCTORS */

	_VariantCall::add_constructor(_VariantCall::Vector2_init1, Variant::VECTOR2, "x", Variant::REAL, "y", Variant::REAL);

	_VariantCall::add_constructor(_VariantCall::Rect2_init1, Variant::RECT2, "pos", Variant::VECTOR2, "size", Variant::VECTOR2);
	_VariantCall::add_constructor(_VariantCall::Rect2_init2, Variant::RECT2, "x", Variant::REAL, "y", Variant::REAL, "width", Variant::REAL, "height", Variant::REAL);

	_VariantCall::add_constructor(_VariantCall::Transform2D_init2, Variant::TRANSFORM2D, "rot", Variant::REAL, "pos", Variant::VECTOR2);
	_VariantCall::add_constructor(_VariantCall::Transform2D_init3, Variant::TRANSFORM2D, "x_axis", Variant::VECTOR2, "y_axis", Variant::VECTOR2, "origin", Variant::VECTOR2);

	_VariantCall::add_constructor(_VariantCall::Vector3_init1, Variant::VECTOR3, "x", Variant::REAL, "y", Variant::REAL, "z", Variant::REAL);

	_VariantCall::add_constructor(_VariantCall::Plane_init1, Variant::PLANE, "a", Variant::REAL, "b", Variant::REAL, "c", Variant::REAL, "d", Variant::REAL);
	_VariantCall::add_constructor(_VariantCall::Plane_init2, Variant::PLANE, "v1", Variant::VECTOR3, "v2", Variant::VECTOR3, "v3", Variant::VECTOR3);
	_VariantCall::add_constructor(_VariantCall::Plane_init3, Variant::PLANE, "normal", Variant::VECTOR3, "d", Variant::REAL);

	_VariantCall::add_constructor(_VariantCall::Quat_init1, Variant::QUAT, "x", Variant::REAL, "y", Variant::REAL, "z", Variant::REAL, "w", Variant::REAL);
	_VariantCall::add_constructor(_VariantCall::Quat_init2, Variant::QUAT, "axis", Variant::VECTOR3, "angle", Variant::REAL);

	_VariantCall::add_constructor(_VariantCall::Color_init1, Variant::COLOR, "r", Variant::REAL, "g", Variant::REAL, "b", Variant::REAL, "a", Variant::REAL);
	_VariantCall::add_constructor(_VariantCall::Color_init2, Variant::COLOR, "r", Variant::REAL, "g", Variant::REAL, "b", Variant::REAL);

	_VariantCall::add_constructor(_VariantCall::Rect3_init1, Variant::RECT3, "pos", Variant::VECTOR3, "size", Variant::VECTOR3);

	_VariantCall::add_constructor(_VariantCall::Basis_init1, Variant::BASIS, "x_axis", Variant::VECTOR3, "y_axis", Variant::VECTOR3, "z_axis", Variant::VECTOR3);
	_VariantCall::add_constructor(_VariantCall::Basis_init2, Variant::BASIS, "axis", Variant::VECTOR3, "phi", Variant::REAL);
	_VariantCall::add_constructor(_VariantCall::Basis_init3, Variant::BASIS, "euler", Variant::VECTOR3);

	_VariantCall::add_constructor(_VariantCall::Transform_init1, Variant::TRANSFORM, "x_axis", Variant::VECTOR3, "y_axis", Variant::VECTOR3, "z_axis", Variant::VECTOR3, "origin", Variant::VECTOR3);
	_VariantCall::add_constructor(_VariantCall::Transform_init2, Variant::TRANSFORM, "basis", Variant::BASIS, "origin", Variant::VECTOR3);

	_VariantCall::add_constructor(_VariantCall::Image_init1, Variant::IMAGE, "width", Variant::INT, "height", Variant::INT, "mipmaps", Variant::BOOL, "format", Variant::INT);

	/* REGISTER CONSTANTS */

	_VariantCall::add_constant(Variant::VECTOR3, "AXIS_X", Vector3::AXIS_X);
	_VariantCall::add_constant(Variant::VECTOR3, "AXIS_Y", Vector3::AXIS_Y);
	_VariantCall::add_constant(Variant::VECTOR3, "AXIS_Z", Vector3::AXIS_Z);

	_VariantCall::add_constant(Variant::INPUT_EVENT, "NONE", InputEvent::NONE);
	_VariantCall::add_constant(Variant::INPUT_EVENT, "KEY", InputEvent::KEY);
	_VariantCall::add_constant(Variant::INPUT_EVENT, "MOUSE_MOTION", InputEvent::MOUSE_MOTION);
	_VariantCall::add_constant(Variant::INPUT_EVENT, "MOUSE_BUTTON", InputEvent::MOUSE_BUTTON);
	_VariantCall::add_constant(Variant::INPUT_EVENT, "JOYPAD_MOTION", InputEvent::JOYPAD_MOTION);
	_VariantCall::add_constant(Variant::INPUT_EVENT, "JOYPAD_BUTTON", InputEvent::JOYPAD_BUTTON);
	_VariantCall::add_constant(Variant::INPUT_EVENT, "SCREEN_TOUCH", InputEvent::SCREEN_TOUCH);
	_VariantCall::add_constant(Variant::INPUT_EVENT, "SCREEN_DRAG", InputEvent::SCREEN_DRAG);
	_VariantCall::add_constant(Variant::INPUT_EVENT, "ACTION", InputEvent::ACTION);

	_VariantCall::add_constant(Variant::IMAGE, "COMPRESS_16BIT", Image::COMPRESS_16BIT);
	_VariantCall::add_constant(Variant::IMAGE, "COMPRESS_S3TC", Image::COMPRESS_S3TC);
	_VariantCall::add_constant(Variant::IMAGE, "COMPRESS_PVRTC2", Image::COMPRESS_PVRTC2);
	_VariantCall::add_constant(Variant::IMAGE, "COMPRESS_PVRTC4", Image::COMPRESS_PVRTC4);
	_VariantCall::add_constant(Variant::IMAGE, "COMPRESS_ETC", Image::COMPRESS_ETC);
	_VariantCall::add_constant(Variant::IMAGE, "COMPRESS_ETC2", Image::COMPRESS_ETC2);

	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_L8", Image::FORMAT_L8);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_LA8", Image::FORMAT_LA8);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_R8", Image::FORMAT_R8);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RG8", Image::FORMAT_RG8);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RGB8", Image::FORMAT_RGB8);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RGBA8", Image::FORMAT_RGBA8);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RGB565", Image::FORMAT_RGB565);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RGBA4444", Image::FORMAT_RGBA4444);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RGBA5551", Image::FORMAT_DXT1);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RF", Image::FORMAT_RF);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RGF", Image::FORMAT_RGF);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RGBF", Image::FORMAT_RGBF);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RGBAF", Image::FORMAT_RGBAF);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RH", Image::FORMAT_RH);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RGH", Image::FORMAT_RGH);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RGBH", Image::FORMAT_RGBH);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_RGBAH", Image::FORMAT_RGBAH);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_DXT1", Image::FORMAT_DXT1);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_DXT3", Image::FORMAT_DXT3);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_DXT5", Image::FORMAT_DXT5);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_ATI1", Image::FORMAT_ATI1);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_ATI2", Image::FORMAT_ATI2);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_BPTC_RGBA", Image::FORMAT_BPTC_RGBA);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_BPTC_RGBF", Image::FORMAT_BPTC_RGBF);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_BPTC_RGBFU", Image::FORMAT_BPTC_RGBFU);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_PVRTC2", Image::FORMAT_PVRTC2);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_PVRTC2A", Image::FORMAT_PVRTC2A);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_PVRTC4", Image::FORMAT_PVRTC4);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_PVRTC4A", Image::FORMAT_PVRTC4A);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_ETC", Image::FORMAT_ETC);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_ETC2_R11", Image::FORMAT_ETC2_R11);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_ETC2_R11S", Image::FORMAT_ETC2_R11S);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_ETC2_RG11", Image::FORMAT_ETC2_RG11);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_ETC2_RG11S", Image::FORMAT_ETC2_RG11S);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_ETC2_RGB8", Image::FORMAT_ETC2_RGB8);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_ETC2_RGBA8", Image::FORMAT_ETC2_RGBA8);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_ETC2_RGB8A1", Image::FORMAT_ETC2_RGB8A1);
	_VariantCall::add_constant(Variant::IMAGE, "FORMAT_MAX", Image::FORMAT_MAX);

	_VariantCall::add_constant(Variant::IMAGE, "INTERPOLATE_NEAREST", Image::INTERPOLATE_NEAREST);
	_VariantCall::add_constant(Variant::IMAGE, "INTERPOLATE_BILINEAR", Image::INTERPOLATE_BILINEAR);
	_VariantCall::add_constant(Variant::IMAGE, "INTERPOLATE_CUBIC", Image::INTERPOLATE_CUBIC);
}

void unregister_variant_methods() {

	memdelete_arr(_VariantCall::type_funcs);
	memdelete_arr(_VariantCall::construct_funcs);
	memdelete_arr(_VariantCall::constant_data);
}
