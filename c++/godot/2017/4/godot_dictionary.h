/*************************************************************************/
/*  godot_dictionary.h                                                   */
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
#ifndef GODOT_DICTIONARY_H
#define GODOT_DICTIONARY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifndef GODOT_CORE_API_GODOT_DICITIONARY_TYPE_DEFINED
typedef struct godot_dictionary {
	uint8_t _dont_touch_that[8];
} godot_dictionary;
#endif

#include "godot_array.h"
#include "godot_variant.h"

void GDAPI godot_dictionary_new(godot_dictionary *p_dict);

void GDAPI godot_dictionary_clear(godot_dictionary *p_dict);

godot_bool GDAPI godot_dictionary_empty(const godot_dictionary *p_dict);

void GDAPI godot_dictionary_erase(godot_dictionary *p_dict, const godot_variant *p_key);

godot_bool GDAPI godot_dictionary_has(const godot_dictionary *p_dict, const godot_variant *p_key);

godot_bool GDAPI godot_dictionary_has_all(const godot_dictionary *p_dict, const godot_array *p_keys);

uint32_t GDAPI godot_dictionary_hash(const godot_dictionary *p_dict);

godot_array GDAPI godot_dictionary_keys(const godot_dictionary *p_dict);

godot_int GDAPI godot_dictionary_parse_json(godot_dictionary *p_dict, const godot_string *p_json);

godot_variant GDAPI *godot_dictionary_operator_index(godot_dictionary *p_dict, const godot_variant *p_key);

godot_int GDAPI godot_dictionary_size(const godot_dictionary *p_dict);

godot_string GDAPI godot_dictionary_to_json(const godot_dictionary *p_dict);

godot_array GDAPI godot_dictionary_values(const godot_dictionary *p_dict);

void GDAPI godot_dictionary_destroy(godot_dictionary *p_dict);

#ifdef __cplusplus
}
#endif

#endif // GODOT_DICTIONARY_H
