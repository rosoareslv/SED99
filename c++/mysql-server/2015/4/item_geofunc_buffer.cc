/* Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */


/**
  @file

  @brief
  This file defines ST_Buffer function.
*/
#include "my_config.h"
#include "item_geofunc.h"

#include "sql_class.h"    // THD
#include "current_thd.h"

#include <boost/variant.hpp>                    // Boost.Variant
#define BOOST_VARAINT_MAX_MULTIVIZITOR_PARAMS 5
#include <boost/variant/multivisitors.hpp>
#include "item_geofunc_internal.h"
#include "gis_bg_traits.h"


static const char *const buffer_strategy_names []=
{
  "invalid_strategy",
  "end_round",
  "end_flat",
  "join_round",
  "join_miter",
  "point_circle",
  "point_square"
};

template <typename Char_type>
inline int char_icmp(const Char_type a, const Char_type b)
{
  const int a1= std::tolower(a);
  const int b1= std::tolower(b);
  return a1 > b1 ? 1 : (a1 < b1 ? -1 : 0);
}

/**
  Case insensitive comparison of two ascii strings.
  @param a '\0' ended string.
  @param b '\0' ended string.
 */
template <typename Char_type>
int str_icmp(const Char_type *a, const Char_type *b)
{
  int ret= 0, i;

  for (i= 0; a[i] != 0 && b[i] != 0; i++)
    if ((ret= char_icmp(a[i], b[i])))
      return ret;
  if (a[i] == 0 && b[i] != 0)
    return -1;
  if (a[i] != 0 && b[i] == 0)
    return 1;
  return 0;
}

/*
  Convert strategies stored in String objects into Strategy_setting objects.
*/
void Item_func_buffer::set_strategies()
{
  for (int i= 0; i < num_strats; i++)
  {
    String *pstr= strategies[i];
    const uchar *pstrat= pointer_cast<const uchar *>(pstr->ptr());

    uint32 snum= 0;

    if (pstr->length() != 12 ||
        !((snum= uint4korr(pstrat)) > invalid_strategy && snum <= max_strategy))
    {
      my_error(ER_WRONG_ARGUMENTS, MYF(0), "st_buffer");
      null_value= true;
      return;
    }

    const enum_buffer_strategies strat= (enum_buffer_strategies)snum;
    double value;
    float8get(&value, pstrat + 4);
    enum_buffer_strategy_types strategy_type= invalid_strategy_type;

    switch (strat)
    {
    case end_round:
    case end_flat:
      strategy_type= end_strategy;
      break;
    case join_round:
    case join_miter:
      strategy_type= join_strategy;
      break;
    case point_circle:
    case point_square:
      strategy_type= point_strategy;
      break;
    default:
      my_error(ER_WRONG_ARGUMENTS, MYF(0), "st_buffer");
      null_value= true;
      return;
      break;
    }

    // Each strategy option can be set no more than once for every ST_Buffer()
    // call.
    if (settings[strategy_type].strategy != invalid_strategy)
    {
      my_error(ER_WRONG_ARGUMENTS, MYF(0), "st_buffer");
      null_value= true;
      return;
    }
    else
    {
      settings[strategy_type].strategy= (enum_buffer_strategies)snum;
      settings[strategy_type].value= value;
    }
  }
}


Item_func_buffer_strategy::
Item_func_buffer_strategy(const POS &pos, PT_item_list *ilist)
  :Item_str_func(pos, ilist)
{
  // Here we want to use the String::set(const char*, ..) version.
  const char *pbuf= tmp_buffer;
  tmp_value.set(pbuf, 0, NULL);
}


void Item_func_buffer_strategy::fix_length_and_dec()
{
  collation.set(&my_charset_bin);
  decimals=0;
  max_length= 16;
  maybe_null= 1;
}

String *Item_func_buffer_strategy::val_str(String * /* str_arg */)
{
  String str;
  String *strat_name= args[0]->val_str_ascii(&str);
  if (args[0]->null_value)
    return error_str();

  // Get the NULL-terminated ascii string.
  const char *pstrat_name= strat_name->c_ptr_safe();

  bool found= false;

  tmp_value.set_charset(&my_charset_bin);
  // The tmp_value is supposed to always stores a {uint32,double} pair,
  // and it uses a char tmp_buffer[16] array data member.
  uchar *result_buf= const_cast<uchar *>(pointer_cast<const uchar *>
                                         (tmp_value.ptr()));

  // Although the result of this item node is never persisted, we still have to
  // use portable endianess access otherwise unaligned access will crash
  // on sparc CPUs.
  for (uint32 i= 0; i <= Item_func_buffer::max_strategy; i++)
  {
    // The above var_str_ascii() call makes the strat_name an ascii string so
    // we can do below comparison.
    if (str_icmp(pstrat_name, buffer_strategy_names[i]) != 0)
      continue;

    int4store(result_buf, i);
    result_buf+= 4;
    Item_func_buffer::enum_buffer_strategies istrat=
      static_cast<Item_func_buffer::enum_buffer_strategies>(i);

    /*
      The end_flat and point_square strategies must have no more arguments;
      The rest strategies must have 2nd parameter which must be a positive
      numeric value, and we will store it as a double.
      We use float8store to ensure that the value is independent of endianness.
    */
    if (istrat != Item_func_buffer::end_flat &&
        istrat != Item_func_buffer::point_square)
    {
      if (arg_count != 2)
      {
        my_error(ER_WRONG_ARGUMENTS, MYF(0), func_name());
        return error_str();
      }

      double val= args[1]->val_real();
      if (args[1]->null_value)
        return error_str();
      if (val <= 0)
      {
        my_error(ER_WRONG_ARGUMENTS, MYF(0), func_name());
        return error_str();
      }

      if (istrat != Item_func_buffer::join_miter &&
          val > current_thd->variables.max_points_in_geometry)
      {
        my_error(ER_GIS_MAX_POINTS_IN_GEOMETRY_OVERFLOWED, MYF(0),
                 "points_per_circle",
                 current_thd->variables.max_points_in_geometry,
                 func_name());
        return error_str();
      }

      float8store(result_buf, val);
    }
    else if (arg_count != 1)
    {
      my_error(ER_WRONG_ARGUMENTS, MYF(0), func_name());
      return error_str();
    }
    else
      float8store(result_buf, 0.0);

    found= true;

    break;
  }

  // Unrecognized strategy names, report error.
  if (!found)
  {
    my_error(ER_WRONG_ARGUMENTS, MYF(0), func_name());
    return error_str();
  }
  tmp_value.length(12);

  return &tmp_value;
}


// Define Variant types holding various strategies.
typedef boost::variant
  <
    boost::geometry::strategy::buffer::join_round,
    boost::geometry::strategy::buffer::join_miter
  > buffer_join_strategy;

typedef boost::variant
  <
    boost::geometry::strategy::buffer::end_round,
    boost::geometry::strategy::buffer::end_flat
  > buffer_end_strategy;

typedef boost::variant
  <
    boost::geometry::strategy::buffer::distance_symmetric<double>,
    boost::geometry::strategy::buffer::distance_asymmetric<double>
  > buffer_distance_strategy;

typedef boost::variant
  <
    boost::geometry::strategy::buffer::point_circle,
    boost::geometry::strategy::buffer::point_square
  > buffer_point_strategy;

typedef boost::variant
  <
    boost::geometry::strategy::buffer::side_straight
  > buffer_side_strategy;


// Define strategies multi-visitor
template <typename GeometryIn, typename MultiPolygon>
struct buffer_static_visitor
  : public boost::static_visitor<>
{
  buffer_static_visitor(GeometryIn const &geom_in, MultiPolygon &geom_out)
    : geometry_in(geom_in), geometry_out(geom_out)
  {}

  template <typename DistanceStrategy,
            typename SideStrategy,
            typename JoinStrategy,
            typename EndStrategy,
            typename PointStrategy>
  void operator()(DistanceStrategy const &distance_strategy,
                  SideStrategy const &side_strategy,
                  JoinStrategy const &join_strategy,
                  EndStrategy const &end_strategy,
                  PointStrategy const &point_strategy)
  {
      boost::geometry::buffer(geometry_in,
                              geometry_out,
                              distance_strategy,
                              side_strategy,
                              join_strategy,
                              end_strategy,
                              point_strategy);
  }

  GeometryIn const &geometry_in;
  MultiPolygon &geometry_out;
};


// Function for convenient use of Visitors instead of specific strategies
template <typename GeometryIn, typename MultiPolygon>
static inline void call_bg_buffer(GeometryIn const &geometry_in,
                                  MultiPolygon &geometry_out,
                                  buffer_distance_strategy const
                                  &distance_strategy,
                                  buffer_side_strategy const &side_strategy,
                                  buffer_join_strategy const &join_strategy,
                                  buffer_end_strategy const &end_strategy,
                                  buffer_point_strategy const &point_strategy)
{
    buffer_static_visitor<GeometryIn, MultiPolygon>
      visitor(geometry_in, geometry_out);
    boost::apply_visitor(visitor, distance_strategy, side_strategy,
                         join_strategy, end_strategy, point_strategy);
}


/**
  Compute buffer by calling boost::geometry::buffer using specified strategies.
  @param geom the geometry to compute buffer for.
  @param [out] result takes back the result buffer.
  @return true if got error; false if no error.
*/
static bool bg_buffer(Geometry *geom,
                      BG_models<double, bgcs::cartesian>::Multipolygon &result,
                      buffer_distance_strategy const &dist_strategy,
                      buffer_side_strategy const &side_strategy,
                      buffer_join_strategy const &join_strategy,
                      buffer_end_strategy const &end_strategy,
                      buffer_point_strategy const &point_strategy)
{
  switch (geom->get_type())
  {
  case Geometry::wkb_point:
  {
    BG_models<double, bgcs::cartesian>::Point
      bg(geom->get_data_ptr(), geom->get_data_size(),
         geom->get_flags(), geom->get_srid());
    call_bg_buffer(bg, result, dist_strategy, side_strategy, join_strategy,
                   end_strategy, point_strategy);
    break;
  }
  case Geometry::wkb_multipoint:
  {
    BG_models<double, bgcs::cartesian>::Multipoint
      bg(geom->get_data_ptr(), geom->get_data_size(),
         geom->get_flags(), geom->get_srid());
    call_bg_buffer(bg, result, dist_strategy, side_strategy, join_strategy,
                   end_strategy, point_strategy);
    break;
  }
  case Geometry::wkb_linestring:
  {
    BG_models<double, bgcs::cartesian>::Linestring
      bg(geom->get_data_ptr(), geom->get_data_size(),
         geom->get_flags(), geom->get_srid());
    call_bg_buffer(bg, result, dist_strategy, side_strategy, join_strategy,
                   end_strategy, point_strategy);
    break;
  }
  case Geometry::wkb_multilinestring:
  {
    BG_models<double, bgcs::cartesian>::Multilinestring
      bg(geom->get_data_ptr(), geom->get_data_size(),
         geom->get_flags(), geom->get_srid());
    call_bg_buffer(bg, result, dist_strategy, side_strategy, join_strategy,
                   end_strategy, point_strategy);
    break;
  }
  case Geometry::wkb_polygon:
  {
    const void *data_ptr= geom->normalize_ring_order();
    if (data_ptr == NULL)
    {
      my_error(ER_GIS_INVALID_DATA, MYF(0), "st_buffer");
      return true;
    }
    BG_models<double, bgcs::cartesian>::Polygon
      bg(data_ptr, geom->get_data_size(),
         geom->get_flags(), geom->get_srid());
    call_bg_buffer(bg, result, dist_strategy, side_strategy, join_strategy,
                   end_strategy, point_strategy);
    break;
  }
  case Geometry::wkb_multipolygon:
  {
    const void *data_ptr= geom->normalize_ring_order();
    if (data_ptr == NULL)
    {
      my_error(ER_GIS_INVALID_DATA, MYF(0), "st_buffer");
      return true;
    }
    BG_models<double, bgcs::cartesian>::Multipolygon
      bg(data_ptr, geom->get_data_size(),
         geom->get_flags(), geom->get_srid());
    call_bg_buffer(bg, result, dist_strategy, side_strategy, join_strategy,
                   end_strategy, point_strategy);
    break;
  }
  default:
    DBUG_ASSERT(false);
    break;
  }

  return false;
}


Item_func_buffer::Item_func_buffer(const POS &pos, PT_item_list *ilist)
  :Item_geometry_func(pos, ilist)
{
  num_strats= 0;
  memset(settings, 0, sizeof(settings));
  memset(strategies, 0, sizeof(strategies));
}


namespace bgst= boost::geometry::strategy::buffer;

String *Item_func_buffer::val_str(String *str_value_arg)
{
  DBUG_ENTER("Item_func_buffer::val_str");
  DBUG_ASSERT(fixed == 1);
  String strat_bufs[side_strategy + 1];
  String *obj= args[0]->val_str(&tmp_value);
  double dist= args[1]->val_real();
  Geometry_buffer buffer;
  Geometry *geom;
  String *str_result= str_value_arg;

  null_value= false;
  bg_resbuf_mgr.free_result_buffer();

  if (!obj || args[0]->null_value || args[1]->null_value)
    DBUG_RETURN(error_str());

  // Reset the two arrays, set_strategies() requires the settings array to
  // be brand new on every ST_Buffer() call.
  memset(settings, 0, sizeof(settings));
  memset(strategies, 0, sizeof(strategies));

  // Strategies options start from 3rd argument, the 1st two arguments are
  // never strategies: the 1st is input geometry, and the 2nd is distance.
  num_strats= arg_count - 2;
  for (uint i= 2; i < arg_count; i++)
  {
    strategies[i - 2]= args[i]->val_str(&strat_bufs[i]);
    if (strategies[i - 2] == NULL || args[i]->null_value)
      DBUG_RETURN(error_str());
  }

  /*
    Do this before simplify_multi_geometry() in order to exclude invalid
    WKB/WKT data.
   */
  if (!(geom= Geometry::construct(&buffer, obj)))
  {
    my_error(ER_GIS_INVALID_DATA, MYF(0), func_name());
    DBUG_RETURN(error_str());
  }

  /*
    If the input geometry is a multi-geometry or geometry collection that has
    only one component, extract that component as input argument.
  */
  Geometry::wkbType geom_type= geom->get_type();
  if (geom_type == Geometry::wkb_multipoint ||
      geom_type == Geometry::wkb_multipolygon ||
      geom_type == Geometry::wkb_multilinestring ||
      geom_type == Geometry::wkb_geometrycollection)
  {
    simplify_multi_geometry(obj);

    if (!(geom= Geometry::construct(&buffer, obj)))
    {
      my_error(ER_GIS_INVALID_DATA, MYF(0), func_name());
      DBUG_RETURN(error_str());
    }
  }

  if (geom->get_srid() != 0)
  {
    my_error(ER_WRONG_ARGUMENTS, MYF(0), func_name());
    DBUG_RETURN(error_str());
  }

  /*
    If distance passed to ST_Buffer is too small, then we return the
    original geometry as its buffer. This is needed to avoid division
    overflow in buffer calculation, as well as for performance purposes.
  */
  if (std::abs(dist) <= GIS_ZERO || is_empty_geocollection(geom))
  {
    null_value= 0;
    str_result= obj;
    DBUG_RETURN(str_result);
  }

  Geometry::wkbType gtype= geom->get_type();
  if (dist < 0 && gtype != Geometry::wkb_polygon &&
      gtype != Geometry::wkb_multipolygon &&
      gtype != Geometry::wkb_geometrycollection)
  {
    my_error(ER_WRONG_ARGUMENTS, MYF(0), func_name());
    DBUG_RETURN(error_str());
  }

  set_strategies();
  if (null_value)
    DBUG_RETURN(error_str());

  /*
    str_result will refer to BG object's memory directly if any, here we remove
    last call's remainings so that if this call doesn't produce any result,
    this call won't note down last address(already freed above) and
    next call won't free already free'd memory.
  */
  str_result->set(NullS, 0, &my_charset_bin);
  bool had_except= false;

  try
  {
    /*
      Create BG strategy objects from user settings and default values.
    */
    buffer_join_strategy join_strat;
    buffer_point_strategy point_strat;
    buffer_end_strategy end_strat;
    buffer_distance_strategy dist_strat= bgst::distance_symmetric<double>(dist);
    buffer_side_strategy side_strat= bgst::side_straight();

    Strategy_setting ss1= settings[end_strategy];
    Strategy_setting ss2= settings[join_strategy];
    Strategy_setting ss3= settings[point_strategy];

    if (ss1.strategy == end_flat)
      end_strat= bgst::end_flat();
    else if (ss1.strategy == end_round)
      end_strat= bgst::end_round(ss1.value);
    else
      DBUG_ASSERT(ss1.strategy == invalid_strategy);

    if (ss2.strategy == join_round)
      join_strat= bgst::join_round(ss2.value);
    else if (ss2.strategy == join_miter)
      join_strat= bgst::join_miter(ss2.value);
    else
      DBUG_ASSERT(ss2.strategy == invalid_strategy);

    if (ss3.strategy == point_circle)
      point_strat= bgst::point_circle(ss3.value);
    else if (ss3.strategy == point_square)
      point_strat= bgst::point_square();
    else
      DBUG_ASSERT(ss3.strategy == invalid_strategy);

    bool is_pts= (gtype == Geometry::wkb_point ||
                  gtype == Geometry::wkb_multipoint);

    bool is_plygn= (gtype == Geometry::wkb_polygon ||
                    gtype == Geometry::wkb_multipolygon);
    bool is_ls= (gtype == Geometry::wkb_linestring ||
                 gtype == Geometry::wkb_multilinestring);

    /*
      Some strategies can be applied to only part of the geometry types and
      coordinate systems. For now we only have cartesian coordinate system
      so no check for them.
    */
    if ((is_pts && (ss1.strategy != invalid_strategy ||
                    ss2.strategy != invalid_strategy)) ||
        (is_plygn && (ss1.strategy != invalid_strategy ||
                      ss3.strategy != invalid_strategy)) ||
        (is_ls && ss3.strategy != invalid_strategy))
    {
      my_error(ER_WRONG_ARGUMENTS, MYF(0), func_name());
      DBUG_RETURN(error_str());
    }

    // Assign default strategies if necessary. 32 points per circle is adopted
    // by PostGIS and 3DSMAX, it seems to be a de-facto standard, so we adopt
    // this value here too.
    if (is_pts && ss3.strategy == invalid_strategy)
      point_strat= bgst::point_circle(32);
    if (!is_pts && ss2.strategy == invalid_strategy)
      join_strat= bgst::join_round(32);
    if (is_ls && ss1.strategy == invalid_strategy)
      end_strat= bgst::end_round(32);

    BG_models<double, bgcs::cartesian>::Multipolygon result;
    result.set_srid(geom->get_srid());
    if (geom->get_type() != Geometry::wkb_geometrycollection)
    {
      if (bg_buffer(geom, result, dist_strat, side_strat,
                    join_strat, end_strat, point_strat))
        DBUG_RETURN(error_str());

      if (result.size() == 0)
      {
        str_result->reserve(GEOM_HEADER_SIZE + 4);
        write_geometry_header(str_result, geom->get_srid(),
                              Geometry::wkb_geometrycollection, 0);
        DBUG_RETURN(str_result);
      }
      else if (post_fix_result(&bg_resbuf_mgr, result, str_result))
        DBUG_RETURN(error_str());
      bg_resbuf_mgr.set_result_buffer(const_cast<char *>(str_result->ptr()));
    }
    else
    {
      // Compute buffer for a geometry collection(GC). We first compute buffer
      // for each component of the GC, and put the buffer polygons into another
      // collection, finally merge components of the collection.
      BG_geometry_collection bggc, bggc2;
      bggc.fill(geom);

      for (BG_geometry_collection::Geometry_list::iterator
           i= bggc.get_geometries().begin();
           i != bggc.get_geometries().end(); ++i)
      {

        BG_models<double, bgcs::cartesian>::Multipolygon res;
        String temp_result;

        res.set_srid((*i)->get_srid());
        Geometry::wkbType gtype= (*i)->get_type();
        if (dist < 0 && gtype != Geometry::wkb_multipolygon &&
            gtype != Geometry::wkb_polygon)
        {
          my_error(ER_WRONG_ARGUMENTS, MYF(0), func_name());
          DBUG_RETURN(error_str());
        }

        if (bg_buffer(*i, res, dist_strat, side_strat,
                      join_strat, end_strat, point_strat))
          DBUG_RETURN(error_str());
        if (res.size() == 0)
          continue;
        if (post_fix_result(&bg_resbuf_mgr, res, &temp_result))
          DBUG_RETURN(error_str());

        // A single component's buffer is computed above and stored here.
        bggc2.fill(&res);
      }

      // Merge the accumulated polygons because they may overlap.
      bggc2.merge_components<double, bgcs::cartesian>(&null_value);
      Gis_geometry_collection *gc= bggc2.as_geometry_collection(str_result);
      delete gc;
    }

    /*
      If the result geometry is a multi-geometry or geometry collection that has
      only one component, extract that component as result.
    */
    simplify_multi_geometry(str_result);
  }
  CATCH_ALL("st_buffer", had_except= true)

  if (had_except)
    DBUG_RETURN(error_str());
  DBUG_RETURN(str_result);
}
