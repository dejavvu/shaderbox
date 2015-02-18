#include "main_pre.h"
#define SCREEN_WIDTH 100
#define SCREEN_HEIGHT 100
/// GLSL begin //////////////////////////////////////////////////////////////////
#ifdef __cplusplus
#define _in(T) const T &
#define _inout(T) T &
#define _out(T) T &
#define _begin {
#define _end }
#else
#define _in(T) const in T
#define _inout(T) inout T
#define _out(T) out T
#define _begin (
#define _end )
#endif

struct ray_t {
	vec3 origin;
	vec3 direction;
};

struct material_t {
	vec3 base_color;
	float metallic;
	float roughness;
	float ior; // index of refraction
	float reflectivity;
	float translucency;
};

struct sphere_t {
	vec3 origin;
	float radius;
	int material;
};

struct plane_t {
	vec3 direction;
	float distance;
	int material;
};

struct point_light_t {
	vec3 origin;
	vec3 color;
};

struct hit_t {
	float t;
	int material_id;
	float material_param; // used by dynamic materials to store temp data
	vec3 normal;
	vec3 origin;
};

#define max_dist 999.0
hit_t no_hit = hit_t _begin
	(max_dist + 1.0), -1, 1., vec3 (0), vec3 (0)
_end;

#define num_planes 6
plane_t planes[num_planes];

#define num_spheres 3
sphere_t spheres[num_spheres];

#define num_lights 1
point_light_t lights[num_lights];
vec3 ambient_light = vec3 (.01, .01, .01);

#define num_materials 10
#define mat_invalid -1
#define mat_debug 0
material_t materials[num_materials];

vec3 eye;

#define BIAS 1e-4 // small offset to add to ray when retracing to avoid self-intersection
#define PI 3.14159265359

// the API
void intersect_sphere (_in(ray_t) ray, _in(sphere_t) sphere, _inout(hit_t) hit);
void intersect_plane (_in(ray_t) ray, _in(plane_t) p, _inout(hit_t) hit);
float fresnel_factor (_in(float) n1, _in(float) n2, _in(float) VdotH);
mat3 rotate_around_y (_in(float) angle_degrees);
mat3 rotate_around_x (_in(float) angle_degrees);
vec3 corect_gamma (_in(vec3) color);
material_t get_material (_in(int) index);

// GLSL/HLSL utilities ported to C++
float saturate(_in(float) value) { return clamp(value, 0., 1.); }
#ifdef __cplusplus
vec3 faceforward(_in(vec3) N, _in(vec3) I, _in(vec3) Nref);
vec3 reflect(_in(vec3) incident, _in(vec3) normal);
vec3 refract(_in(vec3) incident, _in(vec3) normal, _in(float) n);
#endif

void setup_scene ()
{
	materials [mat_debug] = material_t _begin vec3 (1., 1., 1.), 0., 0., 1., 0., 0. _end;	

//
// Cornell box
//
#define cb_mat_white 1
#define cb_mat_red 2
#define cb_mat_blue 3
#define cb_mat_reflect 4
#define cb_mat_refract 5
	materials[cb_mat_white] = material_t _begin
		vec3(0.7913), .0, .5, 1., 0., 0.
	_end;
	materials[cb_mat_red] = material_t _begin
		vec3(0.6795, 0.0612, 0.0529),
		0., .5, 1., 0., 0.
	_end;
	materials[cb_mat_blue] = material_t _begin
		vec3(0.1878, 0.1274, 0.4287),
		0., .5, 1., 0., 0.
	_end;
	materials[cb_mat_reflect] = material_t _begin
		vec3(0.95, 0.64, 0.54),
		1., .1, 1.0, 1., 0.
	_end;
	materials[cb_mat_refract] = material_t _begin
		vec3(1., 0.77, 0.345),
		1., .05, 1.1, 0., 0.
	_end;

#define cb_plane_ground 0
#define cb_plane_behind 1
#define cb_plane_front 2
#define cb_plane_ceiling 3
#define cb_plane_left 4
#define cb_plane_right 5
#define cb_plane_dist 2.
	planes[cb_plane_ground] = plane_t _begin vec3(0, -1, 0), 0., cb_mat_white _end;
	planes[cb_plane_ceiling] = plane_t _begin vec3(0, 1, 0), 2. * cb_plane_dist, cb_mat_white _end;
	planes[cb_plane_behind] = plane_t _begin vec3(0, 0, -1), -cb_plane_dist, cb_mat_white _end;
	planes[cb_plane_front] = plane_t _begin vec3(0, 0, 1), cb_plane_dist, cb_mat_white _end;
	planes[cb_plane_left] = plane_t _begin vec3(1, 0, 0), cb_plane_dist, cb_mat_red _end;
	planes[cb_plane_right] = plane_t _begin vec3(-1, 0, 0), -cb_plane_dist, cb_mat_blue _end;

#define cb_sphere_light 0
#define cb_sphere_left 1
#define cb_sphere_right 2
	spheres[cb_sphere_light] = sphere_t _begin vec3(0, 2.5 * cb_plane_dist + 0.4, 0), 1.5, mat_debug _end;
	spheres[cb_sphere_left] = sphere_t _begin vec3(0.75, 1, -0.75), 1., cb_mat_reflect _end;
	spheres[cb_sphere_right] = sphere_t _begin vec3(-0.75, 0.75, 0.75), 0.75, cb_mat_refract _end;

	lights[0] = point_light_t _begin vec3(0, 2. * cb_plane_dist - 0.2, 2), vec3 (1., 1., 1.) _end;
	
#if 1
	float _sin = sin (iGlobalTime);
	float _cos = cos (iGlobalTime);
	spheres[cb_sphere_left].origin += vec3 (_sin - 0.5, abs (_sin), _cos);
//	spheres[cb_sphere_right].origin += vec3 (_sin + 0.5, abs (_cos), _cos);
#endif
}

vec3 background(_in(ray_t) ray)
{
#if 0
	vec3 sun_dir = normalize(vec3(0, 0.5 * sin (iGlobalTime/2.), -1));
	vec3 sun_color = vec3 (2, 2, 0);
	float sun_grad = max(0., dot(ray.direction, sun_dir));
	float sky_grad = dot(ray.direction, vec3 (0, 1, 0));
	return
		pow(sun_grad, 250.) * sun_color +
		pow (sky_grad, 2.) * vec3 (.3, .3, 3.);
#else
	return vec3 (.1, .1, .7);
#endif
}

//     R       V    N    H      L         L dir to light       
//      ^      ^    ^    ^     ^          V dir to eye
//        .     \   |   /    .            N normal
//          .    \  |  /   .              H half between L and V
//            .   \ | /  .                R reflected
//  n1          .  \|/ .                  O hit point             
// -----------------O----------------     T refracted
//  n2             .                      n1 index of refraction of outgoing medium
//                .                       n2 index of refraction of incoming medium
//               .
//              .
//             .
//           \/_ T
//
vec3 illum_point_light_blinn_phong(
    _in(vec3) V, _in(point_light_t) light, _in(hit_t) hit, _in(material_t) mat)
{
	vec3 L = normalize (light.origin - hit.origin);

	vec3 diffuse = max(0., dot (L, hit.normal)) * (mat.base_color * hit.material_param) * light.color;

    float spec_factor = 50.;
#if 0 // Blinn specular
	vec3 H = normalize(L + V);
	vec3 specular = pow (max (0., dot (H, hit.normal)), spec_factor) * light.color; // * specular color
#else // Phong specular
	vec3 R = reflect(-L, hit.normal);
	vec3 specular = pow (max (0., dot (R, V)), spec_factor) * light.color; // * specular color
#endif

	return diffuse + specular;
}

vec3 illum_point_light_cook_torrance(
	_in(vec3) V, _in(point_light_t) light, _in(hit_t) hit, _in(material_t) mat)
{
	vec3 L = normalize(light.origin - hit.origin);
	vec3 H = normalize(L + V);
	float NdotL = dot (hit.normal, L);
	float NdotH = dot (hit.normal, H);
	float NdotV = dot (hit.normal, V);
	float VdotH = dot (V, H);

	// geometric term
	float geo_a = (2. * NdotH * NdotV) / VdotH;
	float geo_b = (2. * NdotH * NdotL) / VdotH;
	float geo_term = min(1., min(geo_a, geo_b));

	// roughness term -using Beckmann Distribution
	float rough_sq = mat.roughness * mat.roughness;
	float rough_a = 1. / (rough_sq * NdotH * NdotH * NdotH * NdotH);
	float rough_exp = (NdotH * NdotH - 1.) / (rough_sq * NdotH * NdotH);
	float rough_term = rough_a * exp(rough_exp);

	// Fresnel term
	float fresnel_term = fresnel_factor (1., mat.ior, VdotH);

	float specular = (geo_term * rough_term * fresnel_term) / (PI * NdotV * NdotL);
	return max(0., NdotL) * (specular + (mat.base_color * hit.material_param));
}

vec3 illuminate (_in(hit_t) hit)
{
	material_t mat = get_material(hit.material_id);

	// special case for debug stuff - just solid paint it
	if (hit.material_id == mat_debug) {
		return materials [mat_debug].base_color;
	}

	vec3 accum = ambient_light; // really cheap equivalent for indirect light 
	vec3 V = normalize (eye - hit.origin); // view direction

	for (int i = 0; i < num_lights; ++i) {
#if 0
		accum += illum_point_light_blinn_phong (V, lights [i], hit, mat);
#else
		accum += illum_point_light_cook_torrance (V, lights [i], hit, mat);
#endif
	}

	return accum;
}

hit_t raytrace_iteration (_in(ray_t) ray, _in(int) mat_ignored)
{
	hit_t hit = no_hit;

	for (int i = 0; i < num_planes; ++i) {
		intersect_plane(ray, planes [i], hit);
	}

	for (int i = 0; i < num_spheres; ++i) {
		if (spheres [i].material != mat_invalid
		&& spheres [i].material != mat_ignored) {
			intersect_sphere(ray, spheres [i], hit);
		}
	}

	return hit;
}

vec3 raytrace_render(_in(ray_t) ray)
{
	hit_t hit = raytrace_iteration(ray, mat_invalid);

	if (hit.t >= max_dist) {
		return background(ray);
	}
	else {
		return illuminate(hit);
	}
}

vec3 raytrace_all (_in(ray_t) ray)
{
	hit_t hit = raytrace_iteration (ray, mat_invalid);

	if (hit.t >= max_dist) {
		return background (ray);
	}

#if 1
	// 
	//             _.-""""-._                 R0 primary ray
	//           .'          `h2---R2-->h3    R1 inside ray
	//          /     R1 ....> \              R2 outside ray
	//         |     ....       |             h1 enter hit point 
	//  -R0--->h1....           |             h2 exit hit point
	//         |                |             h3 shade point (will also be at h1)
	//          \              /              n1 ior of outside medium
	//       n1  `._ n2     _.'               n2 ior of inside medium
	//              `-....-'
	//                 
	material_t mat = get_material(hit.material_id);

	if (mat.reflectivity > 0. || mat.translucency > 0.) {
		vec3 color = vec3(0);

		//TODO: proper fresnel
		float kr = 1.;
		float kt = 1.;

		// reflection with 1 depth
		if (kr * mat.reflectivity > 0.) {
			vec3 refl_dir = reflect(ray.direction, hit.normal);
			ray_t refl_ray = ray_t _begin
				hit.origin + refl_dir * BIAS,
				refl_dir
			_end;
			color += kr * mat.reflectivity * raytrace_render(refl_ray);
		}

		// refraction (or transmission) with 1 depth
		if (kt * mat.translucency > 0.) {
			float eta = 1./*air*/ / mat.ior;
			vec3 inside_dir = normalize(refract(ray.direction, hit.normal, eta));
			ray_t inside_ray = ray_t _begin
				hit.origin + inside_dir * BIAS,
				inside_dir
			_end;
			hit_t inside_hit = raytrace_iteration(inside_ray, mat_invalid);

			eta = mat.ior / 1./*air*/;
			vec3 outgoing_dir = normalize(refract(inside_dir, -inside_hit.normal, eta));
			ray_t outgoing_ray = ray_t _begin
				inside_hit.origin + outgoing_dir * BIAS,
				outgoing_dir
			_end;

			color += kt * mat.translucency * raytrace_render(outgoing_ray);
		}

		return color;
	}
#endif

	vec3 color = illuminate (hit);

#if 1 // shadow ray
	vec3 sh_line = lights [0].origin - hit.origin;
	vec3 sh_dir = normalize (sh_line);
	ray_t sh_trace = ray_t _begin
		hit.origin + sh_dir * BIAS,
		sh_dir
	_end;
	hit_t sh_hit = raytrace_iteration (sh_trace, mat_debug);
	if (sh_hit.t < length (sh_line)) {
		color *= 0.1;
	}
#endif

	return color;
}

ray_t get_primary_ray (_in(vec3) cam_local_point, _in(vec3) cam_origin, _in(vec3) cam_look_at)
{
	vec3 fwd = normalize (cam_look_at - cam_origin);
	vec3 up = vec3 (0, 1, 0);
	vec3 right = cross (up, fwd);
	up = cross (fwd, right);

	return ray_t _begin
		cam_origin,
		normalize (
			fwd +
			up * cam_local_point.y +
			right * cam_local_point.x
		)
	_end;
}

float sdPlane(vec3 p)
{
	return p.y;
}

float sdSphere(vec3 p, float s)
{
	return length(p) - s;
}

float sdBox(vec3 p, vec3 b)
{
	vec3 d = abs(p) - b;
	return min(max(d.x, max(d.y, d.z)), 0.0) + length(max(d, 0.0));
}

float udRoundBox(vec3 p, vec3 b, float r)
{
	return length(max(abs(p) - b, 0.0)) - r;
}

float sdTorus(vec3 p, vec2 t)
{
	return length(vec2(length(p.xz) - t.x, p.y)) - t.y;
}

float opS(float d1, float d2)
{
	return max(-d2, d1);
}

vec2 opU(vec2 d1, vec2 d2)
{
	return (d1.x < d2.x) ? d1 : d2;
}

vec3 opRep(vec3 p, vec3 c)
{
	return mod(p, c) - 0.5*c;
}

float sdf (_in(vec3) p)
{
	return
		opS(
			sdBox (p, vec3 (1)),
			sdSphere (p, 1.25)
		)
	;
}

vec3 sdf_normal (_in(vec3) p)
{
	float dt = 0.05;
	vec3 x = vec3 (dt, 0, 0);
	vec3 y = vec3 (0, dt, 0);
	vec3 z = vec3 (0, 0, dt);
	return normalize (vec3 (
		sdf (p+x) - sdf (p-x),
		sdf (p+y) - sdf (p-y),
		sdf (p+z) - sdf (p-z)
	));
}

vec3 raymarch(_in(ray_t) ray)
{
	float t = 0.;

	for (int i = 0; i < 50; i++) {
		vec3 p = ray.origin + ray.direction * t;

		float d = sdf(p);
		if (t > 10.) break;

		if (d < BIAS) {
			vec3 n = sdf_normal(p);
			hit_t h = hit_t _begin
				t, cb_mat_red, 1., p, n
				_end;

			return
			//vec3 (float(i)/50.);
			illuminate(h);
		}

		t += d;
	}

	return background(ray);
}

void main ()
{
// The pipeline transform
//
// 1. gl_FragCoord is in raster space [0..resolution]
// 2. convert to NDC [0..1] by dividing to the resolution
// 3. convert to camera space:
//  a. xy gets [-1, +1] by 2 * NDC - 1; z fixed at -1
//  c. apply aspect & fov
//  d. apply the look-at algoritm which will
//     produce the 3 camera axis:
//
//      R   ^ +Y                  ^ +Y             E eye/ray origin
//       .  |\                    |     . R        R primary ray
//         .| \                   |   .            @ fov angle
//   -Z     | .\   +Z             | .
//    ------0---E--->   +X -------0-------> -X
//          | @/                  |
//          | /                   |
//          |/                    | -Y
//           -Y
//
// NOTE: everything is expressed in this space, NOT world

	// assuming screen width is larger than height 
	vec2 aspect_ratio = vec2(iResolution.x / iResolution.y, 1);
	// field of view
	float fov = tan(radians(30.0));

	// antialising
#if 1
#define MSAA_PASSES 4
	float offset = 0.25;
	float ofst_x = offset * aspect_ratio.x;
	float ofst_y = offset;
	vec2 msaa[MSAA_PASSES];
	msaa[0] = vec2(-ofst_x, -ofst_y);
	msaa[1] = vec2(-ofst_x, +ofst_y);
	msaa[2] = vec2(+ofst_x, -ofst_y);
	msaa[3] = vec2(+ofst_x, +ofst_y);
#else
#define MSAA_PASSES 1
	vec2 msaa[MSAA_PASSES];
	msaa[0] = vec2(0.5);
#endif

#if 0
	// trackball
	vec2 mouse = iMouse.x < BIAS ? vec2(0) : 2. * (iResolution.xy / iMouse.xy) - 1.;
	mat3 rot_y = rotate_around_y(mouse.x * 30.);
	eye = rot_y * vec3(0, cb_plane_dist, 2.333 * cb_plane_dist);
	vec3 look_at = vec3(0, cb_plane_dist, 0);
#else
	float q = iGlobalTime * 24.;
	mat3 rot_y = rotate_around_y(q);
	mat3 rot_x = rotate_around_x(-q);
	vec3 eye = rot_x * rot_y * vec3(0, 0, 4);
	vec3 look_at = vec3(0);
#endif

	vec3 color = vec3(0);

	setup_scene();

	for (int i = 0; i < MSAA_PASSES; i++) {
		vec2 point_ndc = (gl_FragCoord.xy + msaa[i]) / iResolution.xy;
		vec3 point_cam = vec3((2.0 * point_ndc - 1.0) * aspect_ratio * fov, -1.0);

		ray_t ray = get_primary_ray(point_cam, eye, look_at);

		color +=
#if 0
			raytrace_all(ray)
#else
			raymarch(ray)
#endif
			/ float(MSAA_PASSES);
	}

	gl_FragColor = vec4 (corect_gamma (color), 1);
}

void intersect_sphere(_in(ray_t) ray, _in(sphere_t) sphere, _inout(hit_t) hit)
{
#if 1
// geometrical solution
// info: http://www.scratchapixel.com/old/lessons/3d-basic-lessons/lesson-7-intersecting-simple-shapes/ray-sphere-intersection/
	vec3 rc = sphere.origin - ray.origin;
	float radius2 = sphere.radius * sphere.radius;
	float tca = dot(rc, ray.direction);
	if (tca < 0.) return;
	float d2 = dot(rc, rc) - tca * tca;
	if (d2 > radius2) return;
	float thc = sqrt(radius2 - d2);
	float t0 = tca - thc;
	float t1 = tca + thc;

	if (t0 < 0.) t0 = t1;
	if (t0 < hit.t) {
#else // TODO: wrong for some reason... t gets weird values at intersection
// analytical solution
// based on combining the
// sphere eq: (P - C)^2 = R^2
// ray eq: P = O + t*D
// into a quadratic eq: ax^2 + bx + c = 0
// which can be solved by "completing the square" http://www.mathsisfun.com/algebra/completing-square.html
// NOTE: be careful about "catastrophic cancellation" http://en.wikipedia.org/wiki/Loss_of_significance
	vec3 rc = ray.origin - sphere.origin;
//	float a = D dot D -- which is 1 because D is normalised
	float b = 2.0 * dot(rc, ray.direction); // 2 * (O - C) dot D
	float c = dot(rc, rc) - sphere.radius * sphere.radius; // (O - C)^2 - R^2
	float discr = b * b - 4.0 * c;
	float t = -b - sqrt(abs(discr)); // use abs to avoid the check for < 0

	if (discr > 0.0 && t > 0.0 && t < hit.t) {
#endif
		vec3 impact = ray.origin + ray.direction * t0;

		hit.t = t0;
		hit.material_id = sphere.material;
		hit.material_param = 1.;
		hit.origin = impact;
		hit.normal = (impact - sphere.origin) / sphere.radius;
	}
}

// Plane is define by normal N and distance to origin P0 (which is on the plane itself)
// a plane eq is: (P - P0) dot N = 0
// which means that any line on the plane is perpendicular to the plane normal
// a ray eq: P = O + t*D
// substitution and solving for t gives:
// t = ((P0 - O) dot N) / (N dot D)
void intersect_plane(_in(ray_t) ray, _in(plane_t) p, _inout(hit_t) hit)
{
	float denom = dot(p.direction, ray.direction);
	if (denom > 1e-6)
	{
		float t = dot(vec3 (p.distance) - ray.origin, p.direction) / denom;
		if (t >= 0.0 && t < hit.t)
		{
			vec3 impact = ray.origin + ray.direction * t;

			// checkboard pattern			
			//vec2 pattern = floor (impact.xz * 0.5);
			//float cb = mod (pattern.x + pattern.y, 2.0);

			hit.t = t;
			hit.material_id = p.material;
			hit.material_param = 1.; // cb; // Disabled for now
			hit.origin = impact;
			hit.normal = faceforward(p.direction, ray.direction, p.direction);
		}
	}
}

// Fresnel effect says that reflection
// increases on a surface with the angle of view
// and the transmission acts the reverse way
// ex: soap bubble - the edges are highly reflected and colorful
// while the middle is transparent
// ex: water of a lake: close to shore you see thru,
// farther away you see only the reflections on the water
//
// how much reflection and how much transmission
// is given by Fresnel equations which can be
// approximated with the Schilck eq
//
// the direction of the refracted (aka transmitted)
// ray is given by the Snell eq given the
// indices of refraction of the mediums
//
// http://www.scratchapixel.com/old/lessons/3d-basic-lessons/lesson-14-interaction-light-matter/optics-reflection-and-refraction/
// http://graphics.stanford.edu/courses/cs148-10-summer/docs/2006--degreve--reflection_refraction.pdf
//    
// indices of refraction    
// gold ---- air ---- ice --- water --- beer --- alumim --- glass --- salt --- PET --- asphalt --- lead --- diamond ---- iron ---
// 0.470    1.000    1.309    1.333     1.345     1.390     1.500    1.516    1.575     1.645      2.010     2.420      2.950
//
float fresnel_factor(_in(float) n1, _in(float) n2, _in(float) VdotH)
{
// using Schlick’s approximation    
	float Rn = (n1 - n2) / (n1 + n2);
	float R0 = Rn * Rn; // reflection coefficient for light incoming parallel to the normal
	float F = 1. - VdotH;
	return R0 + (1. - R0) * (F * F * F * F * F);
}

#ifdef __cplusplus
vec3 faceforward(_in(vec3) N, _in(vec3) I, _in(vec3) Nref)
{
	return dot(Nref, I) < 0 ? N : -N;
}

vec3 reflect(_in(vec3) incident, _in(vec3) normal)
{
	return incident - 2. * dot(normal, incident) * normal;
}

vec3 refract (_in(vec3) incident, _in(vec3) normal, _in(float) n)
{
	float cosi = -dot ( normal, incident);
	float sint2 = n * n * (1. - cosi * cosi);
	if (sint2 > 1.) {
		return reflect (incident, normal); // Total Internal Reflection - TODO: is this ok?
	}
	return n * incident + (n * cosi - sqrt (1. - sint2)) * normal;
}
#endif

mat3 rotate_around_y(_in(float) angle_degrees)
{
	float angle = radians(angle_degrees);
	float _sin = sin(angle);
	float _cos = cos(angle);
	return mat3(_cos, 0, _sin, 0, 1, 0, -_sin, 0, _cos);
}

mat3 rotate_around_x(_in(float) angle_degrees)
{
	float angle = radians(angle_degrees);
	float _sin = sin(angle);
	float _cos = cos(angle);
	return mat3 (1, 0, 0, 0, _cos, -_sin, 0, _sin, _cos);
}

vec3 corect_gamma(_in(vec3) color)
{
	float gamma = 1.0 / 2.25;
	return vec3 (pow(color.r, gamma), pow(color.g, gamma),pow(color.b, gamma));
}

material_t get_material(_in(int) index)
{
	material_t mat;

	for (int i = 0; i < num_materials; ++i) {
		if (i == index) {
			mat = materials[i];
			break;
		}
	}

	return mat;
}
/// GLSL end //////////////////////////////////////////////////////////////////

	// be a dear a clean up
#pragma warning(pop)
#undef main
#undef in
#undef out
#undef inout
#undef uniform
}

// these headers, especially SDL.h & time.h set up names that are in conflict
// with sandbox'es;
// sandbox should be moved to a separate h/cpp pair, but out of laziness...
// including them
// *after* sandbox solves it too

#include <iostream>
#include <sstream>
#include <SDL.h>
#include <SDL_image.h>
#include <time.h>
#include <memory>
#include <functional>
#if OMP_ENABLED
#include <omp.h>
#endif

#include "main_post.h"