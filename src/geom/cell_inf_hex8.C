// The libMesh Finite Element Library.
// Copyright (C) 2002-2017 Benjamin S. Kirk, John W. Peterson, Roy H. Stogner

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// Local includes
#include "libmesh/libmesh_config.h"

#ifdef LIBMESH_ENABLE_INFINITE_ELEMENTS

// C++ includes

// Local includes cont'd
#include "libmesh/cell_inf_hex8.h"
#include "libmesh/edge_edge2.h"
#include "libmesh/edge_inf_edge2.h"
#include "libmesh/face_quad4.h"
#include "libmesh/face_inf_quad4.h"
#include "libmesh/fe_interface.h"
#include "libmesh/fe_type.h"
#include "libmesh/side.h"

namespace libMesh
{


// ------------------------------------------------------------
// InfHex8 class member functions
const unsigned int InfHex8::side_nodes_map[5][4] =
  {
    { 0, 1, 2, 3}, // Side 0
    { 0, 1, 4, 5}, // Side 1
    { 1, 2, 5, 6}, // Side 2
    { 2, 3, 6, 7}, // Side 3
    { 3, 0, 7, 4}  // Side 4
  };

const unsigned int InfHex8::edge_nodes_map[8][2] =
  {
    {0, 1}, // Edge 0
    {1, 2}, // Edge 1
    {2, 3}, // Edge 2
    {0, 3}, // Edge 3
    {0, 4}, // Edge 4
    {1, 5}, // Edge 5
    {2, 6}, // Edge 6
    {3, 7}  // Edge 7
  };


// ------------------------------------------------------------
// InfHex8 class member functions

bool InfHex8::is_vertex(const unsigned int i) const
{
  if (i < 4)
    return true;
  return false;
}

bool InfHex8::is_edge(const unsigned int i) const
{
  if (i < 4)
    return false;
  return true;
}

bool InfHex8::is_face(const unsigned int) const
{
  return false;
}

bool InfHex8::is_node_on_side(const unsigned int n,
                              const unsigned int s) const
{
  libmesh_assert_less (s, n_sides());
  for (unsigned int i = 0; i != 4; ++i)
    if (side_nodes_map[s][i] == n)
      return true;
  return false;
}

bool InfHex8::is_node_on_edge(const unsigned int n,
                              const unsigned int e) const
{
  libmesh_assert_less (e, n_edges());
  for (unsigned int i = 0; i != 2; ++i)
    if (edge_nodes_map[e][i] == n)
      return true;
  return false;
}

UniquePtr<Elem> InfHex8::build_side_ptr (const unsigned int i,
                                         bool proxy)
{
  libmesh_assert_less (i, this->n_sides());

  if (proxy)
    {
      switch (i)
        {
          // base
        case 0:
          return UniquePtr<Elem>(new Side<Quad4,InfHex8>(this,i));

          // ifem sides
        case 1:
        case 2:
        case 3:
        case 4:
          return UniquePtr<Elem>(new Side<InfQuad4,InfHex8>(this,i));

        default:
          libmesh_error_msg("Invalid side i = " << i);
        }
    }

  else
    {
      // Create NULL pointer to be initialized, returned later.
      Elem * face = libmesh_nullptr;

      // Think of a unit cube: (-1,1) x (-1,1) x (1,1)
      switch (i)
        {
        case 0: // the base face
          {
            face = new Quad4;
            break;
          }

          // connecting to another infinite element
        case 1:
        case 2:
        case 3:
        case 4:
          {
            face = new InfQuad4;
            break;
          }

        default:
          libmesh_error_msg("Invalid side i = " << i);
        }

      face->subdomain_id() = this->subdomain_id();

      // Set the nodes
      for (unsigned n=0; n<face->n_nodes(); ++n)
        face->set_node(n) = this->node_ptr(InfHex8::side_nodes_map[i][n]);

      return UniquePtr<Elem>(face);
    }

  libmesh_error_msg("We'll never get here!");
  return UniquePtr<Elem>();
}


UniquePtr<Elem> InfHex8::build_edge_ptr (const unsigned int i)
{
  libmesh_assert_less (i, this->n_edges());

  if (i < 4) // base edges
    return UniquePtr<Elem>(new SideEdge<Edge2,InfHex8>(this,i));
  // infinite edges
  return UniquePtr<Elem>(new SideEdge<InfEdge2,InfHex8>(this,i));
}

bool InfHex8::contains_point (const Point & p, Real tol) const
{
  /*
   * For infinite elements with linear base interpolation:
   *
   * make use of the fact that infinite elements do not
   * live inside the envelope.  Use a fast scheme to
   * check whether point \p p is inside or outside
   * our relevant part of the envelope.  Note that
   * this is not exclusive: only when the distance is less,
   * we are safe.  Otherwise, we cannot say anything. The
   * envelope may be non-spherical, the physical point may lie
   * inside the envelope, outside the envelope, or even inside
   * this infinite element.  Therefore if this fails,
   * fall back to the FEInterface::inverse_map()
   */
  const Point my_origin (this->origin());

  /*
   * determine the minimal distance of the base from the origin
   * Use norm_sq() instead of norm(), it is faster
   */
  const Real min_distance_sq = std::min((Point(this->point(0)-my_origin)).norm_sq(),
                                        std::min((Point(this->point(1)-my_origin)).norm_sq(),
                                                 std::min((Point(this->point(2)-my_origin)).norm_sq(),
                                                          (Point(this->point(3)-my_origin)).norm_sq())));

  /*
   * work with 1% allowable deviation.  We can still fall
   * back to the InfFE::inverse_map()
   */
  const Real conservative_p_dist_sq = 1.01 * (Point(p-my_origin).norm_sq());



  if (conservative_p_dist_sq < min_distance_sq)
    {
      /*
       * the physical point is definitely not contained in the element
       */
      return false;
    }
  else
    {
      /*
       * Declare a basic FEType.  Will use default in the base,
       * and something else (not important) in radial direction.
       */
      FEType fe_type(default_order());

      const Point mapped_point = FEInterface::inverse_map(dim(),
                                                          fe_type,
                                                          this,
                                                          p,
                                                          tol,
                                                          false);

      return FEInterface::on_reference_element(mapped_point, this->type(), tol);
    }
}


void InfHex8::connectivity(const unsigned int libmesh_dbg_var(sc),
                           const IOPackage iop,
                           std::vector<dof_id_type> & conn) const
{
  libmesh_assert(_nodes);
  libmesh_assert_less (sc, this->n_sub_elem());
  libmesh_assert_not_equal_to (iop, INVALID_IO_PACKAGE);

  switch (iop)
    {
    case TECPLOT:
      {
        conn.resize(8);
        conn[0] = this->node_id(0)+1;
        conn[1] = this->node_id(1)+1;
        conn[2] = this->node_id(2)+1;
        conn[3] = this->node_id(3)+1;
        conn[4] = this->node_id(4)+1;
        conn[5] = this->node_id(5)+1;
        conn[6] = this->node_id(6)+1;
        conn[7] = this->node_id(7)+1;
        return;
      }

    default:
      libmesh_error_msg("Unsupported IO package " << iop);
    }
}



#ifdef LIBMESH_ENABLE_AMR

const float InfHex8::_embedding_matrix[4][8][8] =
  {
    // embedding matrix for child 0
    {
      //     0      1      2      3      4      5      6      7 th parent N.(ode)
      {    1.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0}, // 0th child N.
      {    0.5,   0.5,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0}, // 1
      {   0.25,  0.25,  0.25,  0.25,   0.0,   0.0,   0.0,   0.0}, // 2
      {    0.5,   0.0,   0.0,   0.5,   0.0,   0.0,   0.0,   0.0}, // 3
      {    0.0,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   0.0}, // 4
      {    0.0,   0.0,   0.0,   0.0,   0.5,   0.5,   0.0,   0.0}, // 5
      {    0.0,   0.0,   0.0,   0.0,  0.25,  0.25,  0.25,  0.25}, // 6
      {    0.0,   0.0,   0.0,   0.0,   0.5,   0.0,   0.0,   0.5}  // 7
    },

    // embedding matrix for child 1
    {
      //     0      1      2      3      4      5      6      7 th parent N.(ode)
      {    0.5,   0.5,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0}, // 0th child N.
      {    0.0,   1.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0}, // 1
      {    0.0,   0.5,   0.5,   0.0,   0.0,   0.0,   0.0,   0.0}, // 2
      {   0.25,  0.25,  0.25,  0.25,   0.0,   0.0,   0.0,   0.0}, // 3
      {    0.0,   0.0,   0.0,   0.0,   0.5,   0.5,   0.0,   0.0}, // 4
      {    0.0,   0.0,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0}, // 5
      {    0.0,   0.0,   0.0,   0.0,   0.0,   0.5,   0.5,   0.0}, // 6
      {    0.0,   0.0,   0.0,   0.0,  0.25,  0.25,  0.25,  0.25}  // 7
    },

    // embedding matrix for child 2
    {
      //     0      1      2      3      4      5      6      7 th parent N.(ode)
      {    0.5,   0.0,   0.0,   0.5,   0.0,   0.0,   0.0,   0.0}, // 0th child N.
      {   0.25,  0.25,  0.25,  0.25,   0.0,   0.0,   0.0,   0.0}, // 1
      {    0.0,   0.0,   0.5,   0.5,   0.0,   0.0,   0.0,   0.0}, // 2
      {    0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   0.0,   0.0}, // 3
      {    0.0,   0.0,   0.0,   0.0,   0.5,   0.0,   0.0,   0.5}, // 4
      {    0.0,   0.0,   0.0,   0.0,  0.25,  0.25,  0.25,  0.25}, // 5
      {    0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.5,   0.5}, // 6
      {    0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   1.0}  // 7
    },

    // embedding matrix for child 3
    {
      //     0      1      2      3      4      5      6      7 th parent N.(ode)
      {   0.25,  0.25,  0.25,  0.25,   0.0,   0.0,   0.0,   0.0}, // 0th child N.
      {    0.0,   0.5,   0.5,   0.0,   0.0,   0.0,   0.0,   0.0}, // 1
      {    0.0,   0.0,   1.0,   0.0,   0.0,   0.0,   0.0,   0.0}, // 2
      {    0.0,   0.0,   0.5,   0.5,   0.0,   0.0,   0.0,   0.0}, // 3
      {    0.0,   0.0,   0.0,   0.0,  0.25,  0.25,  0.25,  0.25}, // 4
      {    0.0,   0.0,   0.0,   0.0,   0.0,   0.5,   0.5,   0.0}, // 5
      {    0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   1.0,   0.0}, // 6
      {    0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.5,   0.5}  // 7
    }
  };



#endif

} // namespace libMesh

#endif // ifdef LIBMESH_ENABLE_INFINITE_ELEMENTS
