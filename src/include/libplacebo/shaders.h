/*
 * This file is part of libplacebo.
 *
 * libplacebo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libplacebo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libplacebo.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBPLACEBO_SHADERS_H_
#define LIBPLACEBO_SHADERS_H_

// This function defines the "direct" interface to libplacebo's GLSL shaders,
// suitable for use in contexts where the user controls GLSL shader compilation
// but wishes to include functions generated by libplacebo as part of their
// own rendering process. This API is normally not used for operation with
// libplacebo's higher-level constructs such as `pl_dispatch` or `pl_renderer`.

#include <libplacebo/gpu.h>

PL_API_BEGIN

// Thread-safety: Unsafe
typedef PL_STRUCT(pl_shader) *pl_shader;

struct pl_shader_params {
    // The `id` represents an abstract identifier for the shader, to avoid
    // collisions with other shaders being used as part of the same larger,
    // overarching shader. This is relevant for users which want to combine
    // multiple `pl_shader` objects together, in which case all `pl_shader`
    // objects should have a unique `id`.
    uint8_t id;

    // If `gpu` is non-NULL, then this `gpu` will be used to create objects
    // such as textures and buffers, or check for required capabilities, for
    // operations which depend on either of those. This is fully optional, i.e.
    // these GLSL primitives are designed to be used without a dependency on
    // `gpu` wherever possible - however, some features may not work, and will
    // be disabled even if requested.
    pl_gpu gpu;

    // The `index` represents an abstract frame index, which shaders may use
    // internally to do things like temporal dithering or seeding PRNGs. If the
    // user does not care about temporal dithering/debanding, or wants
    // deterministic rendering, this may safely be left as 0. Otherwise, it
    // should be incremented by 1 on successive frames.
    uint8_t index;

    // If `glsl.version` is nonzero, then this structure will be used to
    // determine the effective GLSL mode and capabilities. If `gpu` is also
    // set, then this overrides `gpu->glsl`.
    struct pl_glsl_version glsl;

    // If this is true, all constants in the shader will be replaced by
    // dynaminic variables. This is mainly useful to avoid recompilation for
    // shaders which expect to have their values change constantly.
    bool dynamic_constants;
};

#define pl_shader_params(...) (&(struct pl_shader_params) { __VA_ARGS__ })

// Creates a new, blank, mutable pl_shader object.
//
// Note: Rather than allocating and destroying many shaders, users are
// encouraged to reuse them (using `pl_shader_reset`) for efficiency.
pl_shader pl_shader_alloc(pl_log log, const struct pl_shader_params *params);

// Frees a pl_shader and all resources associated with it.
void pl_shader_free(pl_shader *sh);

// Resets a pl_shader to a blank slate, without releasing internal memory.
// If you're going to be re-generating shaders often, this function will let
// you skip the re-allocation overhead.
void pl_shader_reset(pl_shader sh, const struct pl_shader_params *params);

// Returns whether or not a shader is in a "failed" state. Trying to modify a
// shader in illegal ways (e.g. signature mismatch) will result in the shader
// being marked as "failed". Since most pl_shader_ operations have a void
// return type, the user can use this function to figure out whether a specific
// shader operation has failed or not. This function is somewhat redundant
// since `pl_shader_finalize` will also return NULL in this case.
bool pl_shader_is_failed(const pl_shader sh);

// Returns whether or not a pl_shader needs to be run as a compute shader. This
// will never be the case unless the `pl_glsl_version` this `pl_shader` was
// created using has `compute` support enabled.
bool pl_shader_is_compute(const pl_shader sh);

// Returns whether or not the shader has any particular output size
// requirements. Some shaders, in particular those that sample from other
// textures, have specific output size requirements which need to be respected
// by the caller. If this is false, then the shader is compatible with every
// output size. If true, the size requirements are stored into *w and *h.
bool pl_shader_output_size(const pl_shader sh, int *w, int *h);

// Indicates the type of signature that is associated with a shader result.
// Every shader result defines a function that may be called by the user, and
// this enum indicates the type of value that this function takes and/or
// returns.
//
// Which signature a shader ends up with depends on the type of operation being
// performed by a shader fragment, as determined by the user's calls. See below
// for more information.
enum pl_shader_sig {
    PL_SHADER_SIG_NONE = 0, // no input / void output
    PL_SHADER_SIG_COLOR,    // vec4 color (normalized so that 1.0 is the ref white)

    // The following are only valid as input signatures:
    PL_SHADER_SIG_SAMPLER, // (gsampler* src_tex, vecN tex_coord) pair,
                           // specifics depend on how the shader was generated
};

// Represents a finalized shader fragment. This is not a complete shader, but a
// collection of raw shader text together with description of the input
// attributes, variables and vertices it expects to be available.
struct pl_shader_res {
    // A copy of the parameters used to create the shader.
    struct pl_shader_params params;

    // A list of friendly names for the semantic operations being performed by
    // this shader, e.g. "color decoding" or "debanding".
    const char **steps;
    int num_steps;

    // As a convenience, this contains a pretty-printed version of the
    // above list, with entries tallied and separated by commas
    const char *description;

    // The shader text, as literal GLSL. This will always be a function
    // definition, such that the the function with the indicated name and
    // signature may be called by the user.
    const char *glsl;
    const char *name;
    enum pl_shader_sig input;  // what the function expects
    enum pl_shader_sig output; // what the function returns

    // For compute shaders (pl_shader_is_compute), this indicates the requested
    // work group size. Otherwise, both fields are 0. The interpretation of
    // these work groups is that they're tiled across the output image.
    int compute_group_size[2];

    // If this pass is a compute shader, this field indicates the shared memory
    // size requirements for this shader pass.
    size_t compute_shmem;

    // A set of input vertex attributes needed by this shader fragment.
    const struct pl_shader_va *vertex_attribs;
    int num_vertex_attribs;

    // A set of input variables needed by this shader fragment.
    const struct pl_shader_var *variables;
    int num_variables;

    // A list of input descriptors needed by this shader fragment,
    const struct pl_shader_desc *descriptors;
    int num_descriptors;

    // A list of compile-time constants used by this shader fragment.
    const struct pl_shader_const *constants;
    int num_constants;
};

// Represents a vertex attribute. The four values will be bound to the four
// corner vertices respectively, in row-wise order starting from the top left:
//   data[0] data[1]
//   data[2] data[3]
struct pl_shader_va {
    struct pl_vertex_attrib attr; // VA type, excluding `offset` and `location`
    const void *data[4];
};

// Represents a bound shared variable / descriptor
struct pl_shader_var {
    struct pl_var var;  // the underlying variable description
    const void *data;   // the raw data (as per `pl_var_host_layout`)
    bool dynamic;       // if true, the value is expected to change frequently
};

struct pl_buffer_var {
    struct pl_var var;
    struct pl_var_layout layout;
};

typedef uint16_t pl_memory_qualifiers;
enum {
    PL_MEMORY_COHERENT  = 1 << 0, // supports synchronization across shader invocations
    PL_MEMORY_VOLATILE  = 1 << 1, // all writes are synchronized automatically

    // Note: All descriptors are also implicitly assumed to have the 'restrict'
    // memory qualifier. There is currently no way to override this behavior.
};

struct pl_shader_desc {
    struct pl_desc desc; // descriptor type, excluding `int binding`
    struct pl_desc_binding binding; // contents of the descriptor binding

    // For PL_DESC_BUF_UNIFORM/STORAGE, this specifies the layout of the
    // variables contained by a buffer. Ignored for the other descriptor types
    struct pl_buffer_var *buffer_vars;
    int num_buffer_vars;

    // For storage images and buffers, this specifies additional memory
    // qualifiers on the descriptor. It's highly recommended to always use
    // at least PL_MEMORY_RESTRICT. Ignored for other descriptor types.
    pl_memory_qualifiers memory;
};

// Represents a compile-time constant. This can be lowered to a specialization
// constant to support cheaper recompilations.
struct pl_shader_const {
    enum pl_var_type type;
    const char *name;
    const void *data;

    // If true, this constant *must* be a compile-time constant, which
    // basically just overrides `pl_shader_params.dynamic_constants`. Useful
    // for constants which will serve as inputs to e.g. array sizes.
    bool compile_time;
};

// Finalize a pl_shader. It is no longer mutable at this point, and any further
// attempts to modify it result in an error. (Functions which take a `const
// pl_shader` argument do not modify the shader and may be freely
// called on an already-finalized shader)
//
// The returned pl_shader_res is bound to the lifetime of the pl_shader - and
// will only remain valid until the pl_shader is freed or reset. This function
// may be called multiple times, and will produce the same result each time.
//
// This function will return NULL if the shader is considered to be in a
// "failed" state (see pl_shader_is_failed).
const struct pl_shader_res *pl_shader_finalize(pl_shader sh);

// Shader objects represent abstract resources that shaders need to manage in
// order to ensure their operation. This could include shader storage buffers,
// generated lookup textures, or other sorts of configured state. The body
// of a shader object is fully opaque; but the user is in charge of cleaning up
// after them and passing them to the right shader passes.
//
// Note: pl_shader_obj objects must be initialized to NULL by the caller.
typedef PL_STRUCT(pl_shader_obj) *pl_shader_obj;

void pl_shader_obj_destroy(pl_shader_obj *obj);

PL_API_END

#endif // LIBPLACEBO_SHADERS_H_
