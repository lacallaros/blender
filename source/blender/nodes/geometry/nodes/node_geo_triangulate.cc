/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "node_geometry_util.hh"

extern "C" {
Mesh *triangulate_mesh(Mesh *mesh,
                       const int quad_method,
                       const int ngon_method,
                       const int min_vertices,
                       const int flag);
}

static bNodeSocketTemplate geo_node_triangulate_in[] = {
    {SOCK_GEOMETRY, N_("Geometry")},
    {SOCK_INT, N_("Minimum Vertices"), 4, 0, 0, 0, 4, 10000},
    {-1, ""},
};

static bNodeSocketTemplate geo_node_triangulate_out[] = {
    {SOCK_GEOMETRY, N_("Geometry")},
    {-1, ""},
};

namespace blender::nodes {
static void geo_triangulate_exec(bNode *UNUSED(node), GValueByName &inputs, GValueByName &outputs)
{
  GeometryPtr geometry_in = inputs.extract<GeometryPtr>("Geometry");
  const int min_vertices = std::max(inputs.extract<int>("Minimum Vertices"), 4);
  GeometryPtr geometry_out;
  if (geometry_in.has_value()) {
    Mesh *mesh_in = geometry_in->mesh_get_for_read();
    if (mesh_in != nullptr) {
      Mesh *mesh_out = triangulate_mesh(mesh_in, 3, 0, min_vertices, 0);
      geometry_out = GeometryPtr{new Geometry()};
      geometry_out->mesh_set_and_transfer_ownership(mesh_out);
    }
  }
  outputs.move_in("Geometry", std::move(geometry_out));
}
}  // namespace blender::nodes

void register_node_type_geo_triangulate()
{
  static bNodeType ntype;

  geo_node_type_base(&ntype, GEO_NODE_TRIANGULATE, "Triangulate", 0, 0);
  node_type_socket_templates(&ntype, geo_node_triangulate_in, geo_node_triangulate_out);
  ntype.geometry_node_execute = blender::nodes::geo_triangulate_exec;
  nodeRegisterType(&ntype);
}