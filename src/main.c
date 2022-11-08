#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <stdint.h>
#include <libgen.h>
#include <windows.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <stdbool.h>
#include <memory.h>

#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include "tinyobjloader-c/tinyobj_loader_c.h"

#include <librply/src/lib/rply.h>

#define VERSION_STR "0.1.5"

const char* HELPSTR = "StormworksMeshExporter v" VERSION_STR " made by Nifley <https://github.com/NifleySnifley>\n"
"Usage:\t swmeshexp.exe [options] <input> [-o output] ...\n"
"\nOptions:\n"
"\t-I <MODE>\tselects the input file format, <MODE> can be OBJ, MESH (stormworks), or PLY\n"
"\t-O <MODE>\tselects the output file format, <MODE> can be OBJ, MESH (stormworks), \n\t\t\tPLY, TEXT (human-readable), or MULTIPLY (directory output with one PLY file per submesh)"
"\t-h\t\tshows this help dialog\n"
"\t-C\t\tredirects mesh output to STDOUT (useful for interop)\n"
"\t-S <idx> submesh shader override (sets the shader of all imported submeshes to <idx>)\n\n"
"Limitations & technical information:\n"
"\tPLY import: due to the PLY format's limitations, only one submesh \n\t(encompassing all triangles/vertices) is created and the shader is set by default to opaque\n\n"
"\tOBJ import: due to the OBJ format's lack of formal support for vertex colors \n\t(and tinyOBJ's lack of support for extended RGB vertex attributes), all vertices in each submesh \n\tare colored based on the name of the submesh if it matches a specific format \n\tsee https://github.com/Lewinator56/swMesh2XML_repo/blob/master/swMesh2XML\%20User\%20Guide.pdf \n\tfor more information\n\n"
"\tOBJ export: shader types are appended to submesh IDs in parentheses, \n\tvertex colors are exported using informal XYZRGBA vertex attributes\n\tsee http://paulbourke.net/dataformats/obj/colour.html for more info\n\n"
"\tPLY export: all submeshes are merged into one and shader types are not preserved.\n\n"
"Human-readable mesh format:\n"
"\tthis tool also supports mesh output in a human-readable format using the `-O TEXT` flag\n"
"\tthis feature is designed for debugging, easy extensibility, and integration into other applications\n"
"\t\"--BEGIN MESH OUTPUT--\" is used to denote the start of the human-readable mesh data output\n"
"\teach list of data (vertex positions, normals, faces, etc.) is prefaced by <#> <DATA TYPE>S\n"
"\teach data element (vertex, face, submesh, etc.) is a simple comma-separated list of values\n"
"\tcurrently the data types exported are: VERTICES, NORMALS, COLORS, TRIANGLES, SUBMESHES (in order)\n"
"\tvertices and normals are X,Y,Z; colors are R,G,B,A; triangles are A,B,C (indices, starting at 0)\n"
"\tsubmeshes are formatted as ID,start,count,<cullmin xyz>,<cullmax xyz>,shaderID\n";

char tmp_buf[1024];

const char* OUT_EXTS[6] = {
    ".ply",
    ".obj",
    ".ply",
    ".mesh",
    ".txt",
    ""
};

enum OUTPUT_MODE {
    OUTPUT_PLY,
    OUTPUT_OBJ,
    OUTPUT_MULTI_PLY,
    OUTPUT_STORMWORKS,
    OUTPUT_TEXT,
    OUTPUT_NONE
};

const char* IN_EXTS[3] = {
    ".mesh",
    ".obj",
    ".ply"
};

enum INPUT_MODE {
    INPUT_MESH,
    INPUT_OBJ,
    INPUT_PLY,
};

const char* SIGNATURE = "mesh";
const char* SHADER_TYPES[4] = {
    "opaque",
    "glass",
    "emissive",
    "unknown"
};

typedef struct vertex {
    union {
        struct { float x, y, z; };
        float pos[3];
    };
    union {
        struct { uint8_t r, g, b, a; };
        uint8_t col[4];
    };
    union {
        struct { float nx, ny, nz; };
        float norm[3];
    };
} vertex;

typedef union triangle {
    struct {
        uint16_t a, b, c;
    };
    uint16_t i[3];
} triangle;

typedef struct submesh {
    uint32_t start_index;
    uint32_t vertex_count;
    uint16_t shadertype;
    float cullmin[3];
    float cullmax[3];
    char* id;
} submesh;

void freesubmesh(submesh* sm) {
    free(sm->id);
    sm->id = NULL;
}

submesh copysubmesh(submesh sm) {
    submesh s2 = sm;
    s2.id = malloc(strlen(sm.id) + 1);
    s2.id[strlen(sm.id)] = '\0';
    strcpy(s2.id, sm.id);
    return s2;
}

typedef struct mesh {
    // char* name;
    int n_vertices;
    vertex* vertices;
    int n_triangles;
    triangle* triangles;
    int n_submeshes;
    submesh* submeshes;
} mesh;

// Recalculates the bounding box of each submesh in `m` based on the vertices it contains
void recalculate_submesh_bounds(mesh* m) {
    submesh* sm;
    for (int i = 0; i < m->n_submeshes; ++i) {
        sm = &m->submeshes[i];
        sm->cullmin[0] = m->vertices[sm->start_index].x;
        sm->cullmin[1] = m->vertices[sm->start_index].y;
        sm->cullmin[2] = m->vertices[sm->start_index].z;
        sm->cullmax[0] = m->vertices[sm->start_index].x;
        sm->cullmax[1] = m->vertices[sm->start_index].y;
        sm->cullmax[2] = m->vertices[sm->start_index].z;

        for (int j = 0; j < sm->vertex_count; ++j) {
            int o = sm->start_index + j;
            sm->cullmax[0] = max(m->vertices[o].x, sm->cullmax[0]);
            sm->cullmax[1] = max(m->vertices[o].y, sm->cullmax[1]);
            sm->cullmax[2] = max(m->vertices[o].z, sm->cullmax[2]);

            sm->cullmin[0] = min(m->vertices[o].x, sm->cullmin[0]);
            sm->cullmin[1] = min(m->vertices[o].y, sm->cullmin[1]);
            sm->cullmin[2] = min(m->vertices[o].z, sm->cullmin[2]);
        }
    }
}

void freemesh(mesh* m) {
    free(m->vertices);
    m->vertices = NULL;
    free(m->triangles);
    m->triangles = NULL;
    for (int i = 0; i < m->n_submeshes; ++i) freesubmesh(&m->submeshes[i]);
    free(m->submeshes);
    m->submeshes = NULL;
}

// Appends the entire contents of the second mesh to the other.
// (All vertices, faces, and submeshes of `src` are added to an enlargened `dest`)
// The second mesh is neither modified not deallocated.
void concat_meshes(mesh* dest, mesh src) {
    // Allocate enough space for merging
    realloc(dest->vertices, sizeof(vertex) * (dest->n_vertices + src.n_vertices));
    realloc(dest->triangles, sizeof(triangle) * (dest->n_triangles + src.n_triangles));
    realloc(dest->submeshes, sizeof(submesh) * (dest->n_submeshes + src.n_submeshes));

    // Copy over vertices
    memcpy(&dest->vertices[dest->n_vertices], src.vertices, src.n_vertices * sizeof(vertex));

    // Triangles must be modified
    for (int i = 0; i < src.n_triangles; ++i) {
        dest->triangles[dest->n_triangles + i] = (triangle){
            .a = src.triangles[i].a + dest->n_triangles,
            .b = src.triangles[i].b + dest->n_triangles,
            .c = src.triangles[i].c + dest->n_triangles
        };
    }

    for (int s = 0; s < src.n_submeshes; ++s) {
        submesh sm = src.submeshes[s];
        sm.start_index += dest->n_vertices;
        dest->submeshes[dest->n_submeshes + s] = copysubmesh(sm); // Need to newly-allocate the ID
    }

    dest->n_submeshes += src.n_submeshes;
    dest->n_triangles += src.n_triangles;
    dest->n_vertices += src.n_vertices;
}

void chgfname(char* name, int modeout) {
    memcpy(strrchr(name, '.'), OUT_EXTS[modeout], strlen(OUT_EXTS[modeout]) + 1);
}

int replacechar(char* str, char orig, char rep) {
    char* ix = str;
    int n = 0;
    while ((ix = strchr(ix, orig)) != NULL) {
        *ix++ = rep;
        n++;
    }
    return n;
}

char* readbytes(char* filename, size_t* len) {
    struct stat st;
    stat(filename, &st);
    *len = st.st_size;

    char* buf = malloc(*len * sizeof(char));

    FILE* fhandle = fopen(filename, "rb");
    if (fhandle == NULL) return NULL;
    fread(buf, *len, 1, fhandle);
    fclose(fhandle);

    return buf;
}

void tinyOBJ_loadFile(void* ctx, const char* filename, const int is_mtl, const char* obj_filename, char** buffer, size_t* len) {
    *buffer = readbytes((char*)filename, len);
}

// Set the color with the object name (like lew's SWMesh2XML)
bool extract_color(char* obj_name, vertex* vtx, int* shadertype_ext) {
    char* objn_cpy = malloc(strlen(obj_name) + 2);
    strcpy(objn_cpy, obj_name);
    char* slash = strstr(obj_name, "/");
    if (slash == NULL) goto exit;

    objn_cpy[(int)(slash - obj_name)] = '\0';

    int dash_idx = 0, i = 0, start = 0;
    for (i = 0; i < 4; ++i) {
        if (dash_idx < 0) goto exit;
        dash_idx = strstr(&obj_name[dash_idx + 1], "-") - obj_name;
        if (dash_idx > 0) objn_cpy[dash_idx] = '\0';

        vtx->col[i] = atoi(&objn_cpy[start]);

        start = dash_idx + 1;
    }

    if ((shadertype_ext != NULL) && strlen(slash) > 1) {
        if (!strcasecmp(&slash[1], SHADER_TYPES[0])) {
            *shadertype_ext = 0;
        } else if (!strcasecmp(&slash[1], SHADER_TYPES[1]) || !strcasecmp(&slash[1], "transparent")) {
            *shadertype_ext = 1;
        } else if (!strcasecmp(&slash[1], SHADER_TYPES[2])) {
            *shadertype_ext = 2;
        }
    }

exit:
    free(objn_cpy);
    return i == 4;
}

mesh readobj(char* filename, int* err) {
    mesh m;
    m.n_submeshes = 0;
    m.n_vertices = 0;
    m.n_triangles = 0;

    tinyobj_attrib_t obj_attrs;
    tinyobj_attrib_init(&obj_attrs);

    size_t obj_n_shapes = 0;
    tinyobj_shape_t* obj_shapes = NULL;
    size_t obj_n_mtls = 0;
    tinyobj_material_t* obj_mtls = NULL;

    unsigned int flags = TINYOBJ_FLAG_TRIANGULATE;
    *err = tinyobj_parse_obj(&obj_attrs, &obj_shapes, &obj_n_shapes, &obj_mtls, &obj_n_mtls, filename, tinyOBJ_loadFile, NULL, flags);
    if (*err != TINYOBJ_SUCCESS) {
        printf("tinyOBJ: Error parsing file %d\n", *err);
        return m;
    }

    // TODO: Deal with non-triangulated (quad) meshes
    // At least throw an intelligible error

    printf("tinyOBJ: Successfully loaded OBJ file with %d materials and %d shapes\n", obj_n_mtls, obj_n_shapes);

    // Allocate space in the mesh
    m.n_triangles = obj_attrs.num_face_num_verts;
    m.triangles = calloc(m.n_triangles, sizeof(triangle));
    m.n_vertices = m.n_triangles * 3; // Vertices will be created during the processing of triangles
    m.vertices = calloc(m.n_vertices, sizeof(vertex));
    m.n_submeshes = obj_n_shapes;
    m.submeshes = calloc(m.n_submeshes, sizeof(submesh));

    for (int t = 0; t < m.n_triangles; ++t) {
        for (int v = 0; v < 3; ++v) {
            tinyobj_vertex_index_t face_vert = obj_attrs.faces[t * 3 + v];
            m.vertices[t * 3 + v] = (vertex){
                .x = obj_attrs.vertices[face_vert.v_idx * 3],
                .y = obj_attrs.vertices[face_vert.v_idx * 3 + 1],
                .z = obj_attrs.vertices[face_vert.v_idx * 3 + 2],
                .nx = obj_attrs.normals[face_vert.vn_idx * 3],
                .ny = obj_attrs.normals[face_vert.vn_idx * 3 + 1],
                .nz = obj_attrs.normals[face_vert.vn_idx * 3 + 2],
                .r = 0,
                .g = 0,
                .b = 0,
                .a = 0
            };
        }

        // Next three vertices
        m.triangles[t] = (triangle){ .a = t * 3, .b = t * 3 + 1,.c = t * 3 + 2 };
    }

    // NOTE: If no materials are loaded, present, or match the OBJ file, try to parse color from the object name
    for (int s = 0; s < obj_n_shapes; ++s) {
        tinyobj_shape_t shape = obj_shapes[s];
        // printf("OBJ shape %d \"%s\" starting at face %d with %d faces\n", s, shape.name, shape.face_offset, shape.length);
        submesh sm;

        sm.id = malloc(strlen(shape.name) + 1);
        strcpy(sm.id, shape.name);
        replacechar(sm.id, '\r', '\0');

        sm.start_index = shape.face_offset * 3;
        sm.vertex_count = shape.length * 3;
        sm.shadertype = 0;

        // Attempt to get color (and shader) from the submesh name
        vertex col_vtx;
        int sty = 0;
        if (extract_color(sm.id, &col_vtx, &sty)) {
            printf("Extracted color from object name.\n");
            for (int i = 0; i < sm.vertex_count; ++i) {
                m.vertices[i + sm.start_index].r = col_vtx.r;
                m.vertices[i + sm.start_index].g = col_vtx.g;
                m.vertices[i + sm.start_index].b = col_vtx.b;
            }
            sm.shadertype = sty;
        }

        // NOTE: TinyOBJ doesn't appear to have (working) support for MTL files yet?
        // TODO: Proper MTL-based vertex coloring
        // for (int i = 0; i < sm.vertex_count * 3; ++i) {
        //     int matidx = obj_attrs.material_ids[(sm.start_index + i) / 3];
        //     if (matidx < 0) continue;
        //     printf("%d\n", matidx);
        //     tinyobj_material_t mtl = obj_mtls[matidx];
        //     m.vertices[i + sm.start_index].r = (uint8_t)(mtl.diffuse[0] * 255.0f);
        //     m.vertices[i + sm.start_index].g = (uint8_t)(mtl.diffuse[1] * 255.0f);
        //     m.vertices[i + sm.start_index].b = (uint8_t)(mtl.diffuse[2] * 255.0f);
        // }

        m.submeshes[s] = sm;
    }

    recalculate_submesh_bounds(&m);

    // exit:
    tinyobj_attrib_free(&obj_attrs);
    if (obj_mtls) tinyobj_materials_free(obj_mtls, obj_n_mtls);
    if (obj_shapes) tinyobj_shapes_free(obj_shapes, obj_n_shapes);
    return m;
}

// Writes `m`m to `destfd` in wavefront OBJ format
int writeobj(mesh m, FILE* destfd) {
    FILE* obj = destfd;

    fprintf(obj, "# Created by StormworksMeshExporter v" VERSION_STR "\n");

    // Write OBJ-formatted mesh

    // Vertices
    for (int v = 0; v < m.n_vertices; ++v) {
        fprintf(obj, "v %f %f %f %f %f %f %f\n",
            m.vertices[v].x, m.vertices[v].y, m.vertices[v].z,
            (float)m.vertices[v].r / 255.0f, (float)m.vertices[v].g / 255.0f, (float)m.vertices[v].b / 255.0f, (float)m.vertices[v].a / 255.0f
        );
    }

    // Normals
    for (int v = 0; v < m.n_vertices; ++v) {
        fprintf(obj, "vn %f %f %f\n", m.vertices[v].nx, m.vertices[v].ny, m.vertices[v].nz);
    }

    for (int v = 0; v < m.n_vertices; ++v) {
        fprintf(obj, "vt 0.0 0.0\n");
    }

    // Triangles
    // for (int t = 0; t < m.n_triangles; ++t) {
    //     fprintf(obj, "f %d/%d/%d %d/%d/%d %d/%d/%d\n",
    //         m.triangles[t].b + 1, m.triangles[t].b + 1, m.triangles[t].b + 1,
    //         m.triangles[t].a + 1, m.triangles[t].a + 1, m.triangles[t].a + 1,
    //         m.triangles[t].c + 1, m.triangles[t].c + 1, m.triangles[t].c + 1
    //     );
    // }

    // Objects
    for (int i = 0; i < m.n_submeshes; ++i) {
        submesh sm = m.submeshes[i];

        fprintf(obj, "o %s (%d)\n", sm.id, sm.shadertype);
        for (int t = sm.start_index / 3; t < (sm.vertex_count + sm.start_index) / 3; ++t) {
            fprintf(obj, "f %d/%d/%d %d/%d/%d %d/%d/%d\n",
                m.triangles[t].b + 1, m.triangles[t].b + 1, m.triangles[t].b + 1,
                m.triangles[t].a + 1, m.triangles[t].a + 1, m.triangles[t].a + 1,
                m.triangles[t].c + 1, m.triangles[t].c + 1, m.triangles[t].c + 1
            );
        }
    }

    return 0;
}

static int plyreader_vertex_cb(p_ply_argument argument) {
    mesh* m; // Mesh to add vertex information to
    long idx; // Index of the attribute on the vertex
    ply_get_argument_user_data(argument, (void*)&m, &idx);

    // Index of the current vertex being read
    long vertex_idx;
    ply_get_argument_element(argument, NULL, &vertex_idx);
    // printf("%ld,%ld: %g\n", vertex_idx, idx, ply_get_argument_value(argument));

    switch (idx) {
        case 0: // Position
        case 1:
        case 2:
            m->vertices[vertex_idx].pos[idx] = (float)ply_get_argument_value(argument);
            break;

        case 3: // Normal
        case 4:
        case 5:
            m->vertices[vertex_idx].norm[idx - 3] = (float)ply_get_argument_value(argument);
            break;

        case 6: // Color
        case 7:
        case 8:
        case 9:
            m->vertices[vertex_idx].col[idx - 6] = (uint8_t)ply_get_argument_value(argument);
            break;

        default:
            break;
    }

    return 1;
}

static int plyreader_face_cb(p_ply_argument argument) {
    mesh* m; // Mesh to add triangle information to
    ply_get_argument_user_data(argument, (void*)&m, NULL);

    long length, value_index;
    ply_get_argument_property(argument, NULL, &length, &value_index);

    long face_idx;
    ply_get_argument_element(argument, NULL, &face_idx);

    if (value_index >= 0)
        m->triangles[face_idx].i[2 - value_index] = (uint16_t)ply_get_argument_value(argument);

    return 1;
}

mesh readply(char* filename, int* err) {
    mesh m;
    long nvertices, ntriangles;

    p_ply ply = ply_open(filename, NULL, 0, NULL);
    if (!ply) { *err = 1; goto exit; };
    if (!ply_read_header(ply)) { *err = 2; goto exit; };

    // Set up vertex reading and get vertex count
    nvertices = ply_set_read_cb(ply, "vertex", "x", plyreader_vertex_cb, (void*)&m, 0);
    ply_set_read_cb(ply, "vertex", "y", plyreader_vertex_cb, (void*)&m, 1);
    ply_set_read_cb(ply, "vertex", "z", plyreader_vertex_cb, (void*)&m, 2);
    // Normals
    ply_set_read_cb(ply, "vertex", "nx", plyreader_vertex_cb, (void*)&m, 3);
    ply_set_read_cb(ply, "vertex", "ny", plyreader_vertex_cb, (void*)&m, 4);
    ply_set_read_cb(ply, "vertex", "nz", plyreader_vertex_cb, (void*)&m, 5);
    // Vertex colors
    ply_set_read_cb(ply, "vertex", "red", plyreader_vertex_cb, (void*)&m, 6);
    ply_set_read_cb(ply, "vertex", "green", plyreader_vertex_cb, (void*)&m, 7);
    ply_set_read_cb(ply, "vertex", "blue", plyreader_vertex_cb, (void*)&m, 8);
    ply_set_read_cb(ply, "vertex", "alpha", plyreader_vertex_cb, (void*)&m, 9);

    // Set up triangle reading adn get triangle count
    ntriangles = ply_set_read_cb(ply, "face", "vertex_indices", plyreader_face_cb, &m, 0);
    printf("PLY header parsed: %ld vertices and %ld triangles\n", nvertices, ntriangles);

    // Allocate space in the mesh
    m.n_vertices = (int)nvertices;
    m.n_triangles = (int)ntriangles;
    m.vertices = malloc(m.n_vertices * sizeof(vertex));
    m.triangles = malloc(m.n_triangles * sizeof(triangle));

    // Create a single submesh encompassing the entire mesh
    m.n_submeshes = 1;
    m.submeshes = malloc(m.n_submeshes * sizeof(submesh));
    submesh sm;
    sm.shadertype = 0;
    sm.start_index = 0;
    sm.vertex_count = m.n_triangles * 3;
    sm.id = basename(filename);
    m.submeshes[0] = sm;

    // Read all of the data from the file (using the registered callbacks)
    if (!ply_read(ply)) { *err = 3; goto exit; };

exit:
    ply_close(ply);
    return m;
}

// Writes `m` to `destfd` in stanford PLY format with vertex colors
int writeply(mesh m, FILE* destfd) {
    FILE* ply = destfd;

    fprintf(ply, "ply\nformat ascii 1.0\ncomment Created by StormworksMeshExporter v%s\n", VERSION_STR);
    fprintf(ply, "element vertex %d\nproperty float x\nproperty float y\nproperty float z\nproperty float nx\nproperty float ny\nproperty float nz\nproperty uchar red\nproperty uchar green\nproperty uchar blue\n", m.n_vertices);
    fprintf(ply, "element face %d\nproperty list uchar uint vertex_indices\n", m.n_triangles);
    fprintf(ply, "end_header\n");
    // Write PLY-formatted mesh

    // Vertices
    for (int v = 0; v < m.n_vertices; ++v) {
        fprintf(ply, "%f %f %f %f %f %f %d %d %d\n", m.vertices[v].x, m.vertices[v].y, m.vertices[v].z, m.vertices[v].nx, m.vertices[v].ny, m.vertices[v].nz, m.vertices[v].r, m.vertices[v].g, m.vertices[v].b);
    }

    // Triangles
    for (int t = 0; t < m.n_triangles; ++t) {
        fprintf(ply, "%d %d %d %d\n",
            3,
            m.triangles[t].b,
            m.triangles[t].c,
            m.triangles[t].a
        );
    }

    fclose(ply);

    return 0;
}

// Write basic data of `m`, in a human-readable format
int writedebug(mesh m, FILE* destfd) {
    fprintf(destfd, "--BEGIN MESH OUTPUT--\n");

    fprintf(destfd, "%d VERTICES\n", m.n_vertices);
    for (int v = 0; v < m.n_vertices; ++v) {
        fprintf(destfd, "%f, %f, %f\n", m.vertices[v].x, m.vertices[v].y, m.vertices[v].z);
    }

    fprintf(destfd, "%d NORMALS\n", m.n_vertices);
    for (int v = 0; v < m.n_vertices; ++v) {
        fprintf(destfd, "%f, %f, %f\n", m.vertices[v].nx, m.vertices[v].ny, m.vertices[v].nz);
    }

    fprintf(destfd, "%d COLORS\n", m.n_vertices);
    for (int v = 0; v < m.n_vertices; ++v) {
        fprintf(destfd, "%d, %d, %d, %d\n", m.vertices[v].r, m.vertices[v].g, m.vertices[v].b, m.vertices[v].a);
    }

    fprintf(destfd, "%d TRIANGLES\n", m.n_triangles);
    for (int t = 0; t < m.n_triangles; ++t) {
        fprintf(destfd, "%d, %d, %d\n",
            m.triangles[t].b,
            m.triangles[t].a,
            m.triangles[t].c
        );
    }

    fprintf(destfd, "%d SUBMESHES\n", m.n_submeshes);
    for (int s = 0; s < m.n_submeshes; ++s) {
        submesh sm = m.submeshes[s];
        fprintf(destfd, "\"%s\", %d, %d, %f, %f, %f, %f, %f, %f, %d\n",
            sm.id,
            sm.start_index / 3,
            sm.vertex_count / 3,
            sm.cullmin[0], sm.cullmin[1], sm.cullmin[2],
            sm.cullmax[0], sm.cullmax[1], sm.cullmax[2],
            sm.shadertype
        );
    }
    fprintf(destfd, "--END MESH OUTPUT--\n");

    return 0;
}

// TODO: Phys file import
mesh readphys(char* fbytes) {
    mesh m;

    return m;
}

// TODO: Phys file export
int writephys(mesh m, FILE* destfd) {
    return 0;
}

// Loads the mesh data stored in `fbytes` (encoded in Stormworks .mesh format) 
mesh readmesh(char* filename, int* err) {
    mesh m;
    size_t len = 0;

    char* fbytes = readbytes(filename, &len);
    if (fbytes == NULL) {
        *err = 1;
        goto exit;
    }

    fbytes[4] = '\0';
    // Incorrect signature
    if (strcmp(fbytes, "mesh")) {
        *err = 2;
    }

    int cursor = 8; // skip the first 8 bytes "mesh" + 4 header

    // Vertex count
    uint16_t vtxcount = *((uint16_t*)&fbytes[cursor]);
    cursor += 2;

    // Unknown
    cursor += 4;

    // Vertices
    vertex* verts_ptr = (vertex*)&fbytes[cursor];
    vertex* verts = malloc(vtxcount * sizeof(vertex));
    memcpy(verts, verts_ptr, vtxcount * sizeof(vertex));
    cursor += vtxcount * sizeof(vertex);

    // Triangle (Face) count
    uint32_t tricount = *((uint32_t*)&fbytes[cursor]) / 3;
    cursor += 4;

    // Edges (Triangles)
    triangle* tris_ptr = (triangle*)&fbytes[cursor];
    triangle* tris = malloc(tricount * sizeof(triangle));
    memcpy(tris, tris_ptr, tricount * sizeof(triangle));
    cursor += tricount * sizeof(triangle);

    // Submesh count
    uint16_t submeshcount = *((uint16_t*)&fbytes[cursor]);
    cursor += 2;
    submesh* submeshes = malloc(submeshcount * sizeof(submesh));

    for (int s = 0; s < submeshcount; ++s) {
        submesh sm;
        sm.start_index = *((uint32_t*)&fbytes[cursor]);
        cursor += 4;
        sm.vertex_count = *((uint32_t*)&fbytes[cursor]);
        cursor += 4;

        cursor += 2; // Unknown 1

        sm.shadertype = *((uint16_t*)&fbytes[cursor]);
        cursor += 2;

        memcpy(sm.cullmin, &fbytes[cursor], 3 * sizeof(float));
        cursor += 3 * sizeof(float);

        memcpy(sm.cullmax, &fbytes[cursor], 3 * sizeof(float));
        cursor += 3 * sizeof(float);

        cursor += 2; // Unknown 2

        int idlen = (int)*((uint16_t*)&fbytes[cursor]);
        cursor += 2;

        sm.id = malloc(idlen + 1);
        memcpy(sm.id, &fbytes[cursor], idlen);
        sm.id[idlen] = '\0';
        cursor += idlen;

        cursor += 12; // Padding

        submeshes[s] = sm;
    }

    m.n_vertices = vtxcount;
    m.vertices = verts;
    m.n_triangles = tricount;
    m.triangles = tris;
    m.n_submeshes = submeshcount;
    m.submeshes = submeshes;

exit:
    if (fbytes != NULL) free(fbytes);
    return m;
}

// Writes `m` to destfd in Stormworks .mesh format
int writemesh(mesh m, FILE* destfd) {
    // Header
    fwrite("mesh\x07\x00\x01\x00", 8, 1, destfd);

    // Vertex count (2b)
    uint16_t vertn = m.n_vertices;
    fwrite(&vertn, 1, sizeof(uint16_t), destfd);

    // Unknown (4b)
    fwrite("\x13\x00\x00\x00", 4, 1, destfd);

    // Vertices
    fwrite(m.vertices, m.n_vertices, sizeof(vertex), destfd);

    // Triangle count (4b) (n_triangles * 3)
    uint32_t trin = m.n_triangles * 3;
    fwrite(&trin, 1, sizeof(uint32_t), destfd);

    // "Edge buffer" (triangles)
    fwrite(m.triangles, m.n_triangles, sizeof(triangle), destfd);

    // Submesh count
    uint16_t smct = m.n_submeshes;
    fwrite(&smct, 1, sizeof(uint16_t), destfd);

    for (int s = 0; s < m.n_submeshes; ++s) {
        submesh sm = m.submeshes[s];

        // Vertices start (4b)
        fwrite(&sm.start_index, 1, sizeof(uint32_t), destfd);

        // Vertices count (4b)
        // uint32_t vct = sm.vertex_count * 3;
        fwrite(&sm.vertex_count, 1, sizeof(uint32_t), destfd);

        // Unknown (2b)
        fwrite("\x00\x00", 2, 1, destfd);

        // Shader type (2b)
        fwrite(&sm.shadertype, 1, sizeof(uint16_t), destfd);

        // Cullmin (3x4b)
        fwrite(sm.cullmin, 3, sizeof(float), destfd);

        // Cullmax (3x4b)
        fwrite(sm.cullmax, 3, sizeof(float), destfd);

        // Unknown (2b)
        fwrite("\x00\x00", 2, 1, destfd);

        uint16_t len = strlen(sm.id);
        // Submesh ID (2b+data)
        fwrite(&len, 1, sizeof(uint16_t), destfd);
        fprintf(destfd, "%s", sm.id);

        // 3 unknown floats (3x4b)
        fwrite("\x00\x00\x80\x3F\x00\x00\x80\x3F\x00\x00\x80\x3F", 3, 4, destfd);
    }

    fwrite("\x00\x00 created with StormworksMeshExporter v" VERSION_STR, 2, 1, destfd);

    fflush(destfd);

    return 0;
}

// Step 1: Import a mesh
int importfile(char* input_filename, int input_mode, mesh* m) {
    int err = 0;
    switch (input_mode) {
        case INPUT_MESH:
            *m = readmesh(input_filename, &err);
            break;
        case INPUT_OBJ:
            *m = readobj(input_filename, &err);
            break;
        case INPUT_PLY:
            *m = readply(input_filename, &err);
            break;
    }

    if (err) {
        printf("Error %d importing file \"%s\"\n", err, input_filename);
    } else {
        printf("Successfully imported \"%s\" with %d vertices and %d faces\n", input_filename, m->n_vertices, m->n_triangles);

    }

    return err;
}

// Step 3: Export the mesh after processing
int exportfile(char* input_filename, char* output_filename, mesh m, int output_mode, bool cout) {
    int err = 0;

    if (output_filename == NULL) {
        memcpy(tmp_buf, input_filename, strlen(input_filename));
        chgfname(tmp_buf, output_mode);
        output_filename = tmp_buf;
    }

    // Print general information about the mesh to be exported
    printf("Mesh processed with %d vertices and %d faces\n", m.n_vertices, m.n_triangles);
    for (int i = 0; i < m.n_submeshes; ++i) {
        printf(
            "Submesh \"%s\" starting at face %d containing %d faces using shader #%d (%s)\n",
            m.submeshes[i].id,
            m.submeshes[i].start_index / 3,
            m.submeshes[i].vertex_count / 3,
            m.submeshes[i].shadertype,
            SHADER_TYPES[m.submeshes[i].shadertype]
        );
    }

    if (output_mode == OUTPUT_MULTI_PLY) {
        char* outdir = malloc(128);
        memcpy(outdir, output_filename, strlen(output_filename) + 1);
        outdir[strlen(outdir) - 4] = '\0';

        CreateDirectory(outdir, NULL);

        char* buf = malloc(128);

        for (int s = 0; s < m.n_submeshes; ++s) {
            submesh sm = m.submeshes[s];
            strcpy(buf, outdir);
            snprintf(buf, 128, "%s/%s-%s.ply", outdir, sm.id, SHADER_TYPES[sm.shadertype]);

            mesh sub;

            sub.vertices = m.vertices;
            sub.n_vertices = m.n_vertices;

            sub.triangles = &m.triangles[sm.start_index / 3];
            sub.n_triangles = sm.vertex_count / 3;

            FILE* outfile = fopen(buf, "w");
            err = writeply(sub, outfile);
            if (err) {
                free(buf);
                free(outdir);
                goto exit;
            }
            fclose(outfile);
        }

        free(buf);
        free(outdir);
    } else if (output_mode == OUTPUT_NONE) {
        // Do nothing!
    } else {
        FILE* outfile;
        if (cout) {
            outfile = stdout;
        } else {
            outfile = fopen(output_filename, output_mode == OUTPUT_STORMWORKS ? "wb" : "w");
            if (outfile == NULL) {
                err = 2;
                goto exit;
            }
        }

        switch (output_mode) {
            case OUTPUT_PLY:
                err = writeply(m, outfile);
                break;
            case OUTPUT_OBJ:
                err = writeobj(m, outfile);
                break;
            case OUTPUT_STORMWORKS:
                err = writemesh(m, outfile);
                break;
            case OUTPUT_TEXT:
                err = writedebug(m, outfile);
                break;
            default:
                break;
        }

        fflush(outfile);
        fclose(outfile);
    }

exit:
    if (err) printf("Error #%d exporting mesh %s\n", err, output_filename);
    // freemesh(&m);
    return err;
}

int main(int argc, char** argv) {
    int res = 0;

    int output_mode = OUTPUT_PLY, input_mode = INPUT_MESH;
    bool consoleout = false;

    char* input_filename = NULL;

    bool hasmesh = false;
    mesh m;

    // v0.2: More inputs (and way more other random things)
    // DONE: Manual output file selection
    // DONE: Multiple input/output (<infile> -o <outfile>) pairs?
    // DONE: OBJ file reading (except vertex colors because tinyobj)
    // DONE: PLY file reading
    // DONE: Document all options, limitations, & capabilities after proper CLI is done in HELPSTR
    // DONE: OBJ export submeshes and shader types in object names
    // NOTE: Refactored for a more "pipelined" import->process->export flow to allow more mesh operations in the future.
    // TODO: Stormworks physics mesh IO
    // TODO: Warning/error when exporting .mesh with too many vertices, too large of parameters, etc.

    // v0.3: CLI QOL features & advanced operations
    // TODO: .mesh export shader selection
    // TODO: Automatic input/output type detection if not manually set
    // TODO: Directory mode: Searches input directory for all files matching input type and converts them to selected output type. if an output option is provided, use it as a directory to store the output files. recursive option
    // TODO: Allow MISO (multiple-input-single-output) mesh converting with -i input file flags and specification of submesh data for each input (shader type, etc.)
    // TODO: ^ Multi-PLY folder input 
    // TODO: ^ add 'operations' more flags! (merge, select submesh [by ID or name], swap axes, set shader, offset?)

    int opt;
    while ((opt = getopt(argc, argv, "-:I:O:S:o:hC")) != -1) {
        switch (opt) {
            case 'O':
                if (!strcasecmp(optarg, "obj")) {
                    output_mode = OUTPUT_OBJ;
                } else if (!strcasecmp(optarg, "ply")) {
                    output_mode = OUTPUT_PLY;
                } else if (!strcasecmp(optarg, "mesh") || !strcasecmp(optarg, "stormworks")) {
                    output_mode = OUTPUT_STORMWORKS;
                } else if (!strcasecmp(optarg, "plys") || !strcasecmp(optarg, "multiply")) {
                    output_mode = OUTPUT_MULTI_PLY;
                } else if (!strcasecmp(optarg, "text")) {
                    output_mode = OUTPUT_TEXT;
                } else if (!strcasecmp(optarg, "dryrun")) {
                    output_mode = OUTPUT_NONE;
                } else {
                    printf("Error, invalid output type \"%s\", see help (-h) for valid options.\n", optarg);
                    res = 5;
                    goto exit;
                }
                break;
            case 'I':
                if (!strcasecmp(optarg, "obj")) {
                    input_mode = INPUT_OBJ;
                } else if (!strcasecmp(optarg, "mesh") || !strcasecmp(optarg, "stormworks")) {
                    input_mode = INPUT_MESH;
                } else if (!strcasecmp(optarg, "ply")) {
                    input_mode = INPUT_PLY;
                } else {
                    printf("Error, invalid input type \"%s\", see help (-h) for valid options.\n", optarg);
                    res = 5;
                    goto exit;
                }
                break;
            case 'C': // STDOUT redirection flag
                consoleout = true;
                break;

            case 'S': // Submesh shader override
                break;

            case 1:
                // If there is a file that hasn't been converted yet and no output has been given, convert it automatically.
                // if (inpfile != NULL) {
                //     processfile(inpfile, NULL, input_mode, output_mode, consoleout);
                //     inpfile = NULL;
                // }
                // inpfile = optarg;

                if (hasmesh) {
                    res = exportfile(input_filename, NULL, m, output_mode, consoleout);
                    if (res) goto exit;
                    freemesh(&m);
                    hasmesh = false;
                    input_filename = NULL;
                }
                input_filename = optarg;
                res = importfile(input_filename, input_mode, &m);
                hasmesh = true;
                if (res)
                    goto exit;

                break;

            case 'o': // When output name is given, convert the previous file
                // processfile(inpfile, optarg, input_mode, output_mode, consoleout);
                // inpfile = NULL;
                res = exportfile(input_filename, optarg, m, output_mode, consoleout);
                if (res) goto exit;
                freemesh(&m);
                hasmesh = false;
                input_filename = NULL;
                break;

            case 'h':
                printf("%s", HELPSTR);
                goto exit;
                break;
            case '?': // Unknown arg
                printf("Error, unknown argument \'%c\'\n", optopt);
                res = 6;
                goto exit;
                break;
            case ':': // No optarg
                switch (optopt) {
                    case 'O':
                        printf("Error, output type must be specified after -O\n");
                        break;
                }
                res = 7;
                goto exit;
                break;
        }
    }

    // Process the last filename
    // if (inpfile != NULL)
    //     processfile(inpfile, NULL, input_mode, output_mode, consoleout);

    if (hasmesh) {
        res = exportfile(input_filename, NULL, m, output_mode, consoleout);
        if (res) goto exit;
        freemesh(&m);
        hasmesh = false;
        input_filename = NULL;
    }


exit:
    if (hasmesh) {
        freemesh(&m);
    }
    return res;
}