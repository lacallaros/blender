/**
 * Library to create hairs dynamically from control points.
 * This is less bandwidth intensive than fetching the vertex attributes
 * but does more ALU work per vertex. This also reduces the amount
 * of data the CPU has to precompute and transfer for each update.
 */

/**
 * hairStrandsRes: Number of points per hair strand.
 * 2 - no subdivision
 * 3+ - 1 or more interpolated points per hair.
 */
uniform int hairStrandsRes = 8;

#ifndef M_PI
#define M_PI 3.14159265358979323846     /* pi */
#endif
#ifndef M_2PI
#define M_2PI 6.28318530717958647692    /* 2*pi */
#endif

/**
 * hairThicknessRes : Subdiv around the hair.
 * 1 - Wire Hair: Only one pixel thick, independent of view distance.
 * 2 - Polystrip Hair: Correct width, flat if camera is parallel.
 * 3+ - Cylinder Hair: Massive calculation but potentially perfect. Still need proper support.
 */
uniform int hairThicknessRes = 1;

/* Hair thickness shape. */
uniform float hairRadRoot = 0.01;
uniform float hairRadTip = 0.0;
uniform float hairRadShape = 0.5;
uniform bool hairCloseTip = true;

uniform vec4 hairDupliMatrix[4];

/* -- Per control points -- */
uniform samplerBuffer hairPointBuffer; /* RGBA32F */
#define point_position xyz
#define point_time w /* Position along the hair length */

/* -- Per strands data -- */
uniform usamplerBuffer hairStrandBuffer;    /* R32UI */
uniform usamplerBuffer hairStrandSegBuffer; /* R16UI */

/* Not used, use one buffer per uv layer */
// uniform samplerBuffer hairUVBuffer; /* RG32F */
// uniform samplerBuffer hairColBuffer; /* RGBA16 linear color */

/* -- Subdivision stage -- */
/**
 * We use a transform feedback to preprocess the strands and add more subdivision to it.
 * For the moment these are simple smooth interpolation but one could hope to see the full
 * children particle modifiers being evaluated at this stage.
 *
 * If no more subdivision is needed, we can skip this step.
 */

#ifdef HAIR_PHASE_SUBDIV
int hair_get_base_id(float local_time, int strand_segments, out float interp_time)
{
  float time_per_strand_seg = 1.0 / float(strand_segments);

  float ratio = local_time / time_per_strand_seg;
  interp_time = fract(ratio);

  return int(ratio);
}

void hair_get_interp_attrs(
    out vec4 data0, out vec4 data1, out vec4 data2, out vec4 data3, out float interp_time)
{
  float local_time = float(gl_VertexID % hairStrandsRes) / float(hairStrandsRes - 1);

  int hair_id = gl_VertexID / hairStrandsRes;
  int strand_offset = int(texelFetch(hairStrandBuffer, hair_id).x);
  int strand_segments = int(texelFetch(hairStrandSegBuffer, hair_id).x);

  int id = hair_get_base_id(local_time, strand_segments, interp_time);

  int ofs_id = id + strand_offset;

  data0 = texelFetch(hairPointBuffer, ofs_id - 1);
  data1 = texelFetch(hairPointBuffer, ofs_id);
  data2 = texelFetch(hairPointBuffer, ofs_id + 1);
  data3 = texelFetch(hairPointBuffer, ofs_id + 2);

  if (id <= 0) {
    /* root points. Need to reconstruct previous data. */
    data0 = data1 * 2.0 - data2;
  }
  if (id + 1 >= strand_segments) {
    /* tip points. Need to reconstruct next data. */
    data3 = data2 * 2.0 - data1;
  }
}
#endif

/* -- Drawing stage -- */
/**
 * For final drawing, the vertex index and the number of vertex per segment
 */

#if !defined(HAIR_PHASE_SUBDIV) && defined(GPU_VERTEX_SHADER)
int hair_get_strand_id(void)
{
  return gl_VertexID / (hairStrandsRes * hairThicknessRes);
}

int hair_get_base_id(void)
{
  return gl_VertexID / hairThicknessRes;
}

/* Copied from cycles. */
float hair_shaperadius(float shape, float root, float tip, float time)
{
  float radius = 1.0 - time;

  if (shape < 0.0) {
    radius = pow(radius, 1.0 + shape);
  }
  else {
    radius = pow(radius, 1.0 / (1.0 - shape));
  }

  if (hairCloseTip && (time > 0.99)) {
    return 0.0;
  }

  return (radius * (root - tip)) + tip;
}

#  ifdef OS_MAC
in float dummy;
#  endif

void hair_get_pos_tan_binor_time(bool is_persp,
                                 mat4 invmodel_mat,
                                 vec3 camera_pos,
                                 vec3 camera_z,
                                 out vec3 wpos,
                                 out vec3 wtan,
                                 out vec3 wbinor,
                                 out float time,
                                 out float thickness,
                                 out float thick_time)
{
  int id = hair_get_base_id();
  vec4 data = texelFetch(hairPointBuffer, id);
  wpos = data.point_position;
  time = data.point_time;

#  ifdef OS_MAC
  /* Generate a dummy read to avoid the driver bug with shaders having no
   * vertex reads on macOS (T60171) */
  wpos.y += dummy * 0.0;
#  endif

  if (time == 0.0) {
    /* Hair root */
    wtan = texelFetch(hairPointBuffer, id + 1).point_position - wpos;
  }
  else {
    wtan = wpos - texelFetch(hairPointBuffer, id - 1).point_position;
  }

  mat4 obmat = mat4(
      hairDupliMatrix[0], hairDupliMatrix[1], hairDupliMatrix[2], hairDupliMatrix[3]);

  wpos = (obmat * vec4(wpos, 1.0)).xyz;
  wtan = -normalize(mat3(obmat) * wtan);

  vec3 camera_vec = (is_persp) ? camera_pos - wpos : camera_z;
  wbinor = normalize(cross(camera_vec, wtan));

  thickness = hair_shaperadius(hairRadShape, hairRadRoot, hairRadTip, time);

  if (hairThicknessRes == 2) {
    thick_time = float(gl_VertexID % hairThicknessRes) / float(hairThicknessRes - 1);
    thick_time = thickness * (thick_time * 2.0 - 1.0);

    /* Take object scale into account.
     * NOTE: This only works fine with uniform scaling. */
    float scale = 1.0 / length(mat3(invmodel_mat) * wbinor);

    wpos += wbinor * thick_time * scale;
  } else if (hairThicknessRes > 2) { //cylinder
    vec4 data2;
    vec4 data3;
    vec3 wpos2;
    vec3 wtan2;

    int id2 = time > 0.0 ? id - 1 : id;
    data2 = texelFetch(hairPointBuffer, id2);

    int id3 = data2.point_time > 0.0 ? id2 - 1 : id2;
    data3 = texelFetch(hairPointBuffer, id3);

    wtan = data.point_position - data2.point_position;
    wtan2 = data2.point_position - data3.point_position;

    wtan = -normalize(mat3(obmat) * wtan);
    wtan2 = -normalize(mat3(obmat) * wtan2);

    wpos2 = data2.point_position;
    wpos2 = (obmat * vec4(wpos2, 1.0)).xyz;

    //as a triangle strip, we alternative between current and next ring every other vert
    if (gl_VertexID % 2 == 0) {
      thickness = hair_shaperadius(hairRadShape, hairRadRoot, hairRadTip, data2.point_time);
      wtan = wtan2;
      time = data2.point_time;
    }

    thick_time = float((gl_VertexID/2) % (hairThicknessRes/2)) / float((hairThicknessRes/2) - 1);
    thick_time *= M_2PI;

    //build reference frame

    //find compatible world axis
    vec3 axis;
    if (abs(wtan[0]) >= abs(wtan[1]) && abs(wtan[0]) >= abs(wtan[2])) {
      axis = vec3(0.0, 1.0, 0.0);
    } else if (abs(wtan[1]) >= abs(wtan[0]) && abs(wtan[1]) >= abs(wtan[2])) {
      axis = vec3(0.0, 0.0, 1.0);
    } else {
      axis = vec3(1.0, 0.0, 0.0);
    }

    //make frame
    vec3 dx = normalize(cross(axis, wtan));
    vec3 dy = normalize(cross(wtan, dx));

    float x = sin(thick_time);
    float y = cos(thick_time);

    wbinor = dx*x + dy*y;
    wbinor = normalize(mat3(obmat) * wbinor);
    

    /* Take object scale into account.
     * NOTE: This only works fine with uniform scaling. */
    float scale = 1.0 / length(mat3(invmodel_mat) * wbinor);

    x *= scale * thickness;
    y *= scale * thickness;

    if (gl_VertexID % 2 == 1) {
      wpos += dx*x + dy*y;
    } else {
      wpos = wpos2 + (dx*x + dy*y);
    }
  }
}

vec2 hair_get_customdata_vec2(const samplerBuffer cd_buf)
{
  int id = hair_get_strand_id();
  return texelFetch(cd_buf, id).rg;
}

vec3 hair_get_customdata_vec3(const samplerBuffer cd_buf)
{
  int id = hair_get_strand_id();
  return texelFetch(cd_buf, id).rgb;
}

vec4 hair_get_customdata_vec4(const samplerBuffer cd_buf)
{
  int id = hair_get_strand_id();
  return texelFetch(cd_buf, id).rgba;
}

vec3 hair_get_strand_pos(void)
{
  int id = hair_get_strand_id() * hairStrandsRes;
  return texelFetch(hairPointBuffer, id).point_position;
}

vec2 hair_get_barycentric(void)
{
  /* To match cycles without breaking into individual segment we encode if we need to invert
   * the first component into the second component. We invert if the barycentricTexCo.y
   * is NOT 0.0 or 1.0. */
  int id = hair_get_base_id();
  return vec2(float((id % 2) == 1), float(((id % 4) % 3) > 0));
}

#endif

/* To be fed the result of hair_get_barycentric from vertex shader. */
vec2 hair_resolve_barycentric(vec2 vert_barycentric)
{
  if (fract(vert_barycentric.y) != 0.0) {
    return vec2(vert_barycentric.x, 0.0);
  }
  else {
    return vec2(1.0 - vert_barycentric.x, 0.0);
  }
}
