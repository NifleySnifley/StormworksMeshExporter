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

#define VERSION_STR "0.2"

const char* HELPSTR = "StormworksMeshExporter v" VERSION_STR " made by Nifley <https://github.com/NifleySnifley>\n"
"Usage:\t swmeshexp.exe [options] <input> [output]\n";

char tmp_buf[1024];

const char* OUT_EXTS[6] = {
    ".ply",
    ".obj",
    ".ply",
    ".export.mesh",
    ".txt",
    ""
};

enum OUTPUT_MODE {
    OUTPUT_PLY,
    OUTPUT_OBJ,
    OUTPUT_MULTI_PLY,
    OUTPUT_STORMWORKS,
    OUTPUT_STDOUT,
    OUTPUT_NONE
};

const char* IN_EXTS[2] = {
    ".mesh",
    ".obj"
};

enum INPUT_MODE {
    INPUT_MESH,
    INPUT_OBJ
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
    float nx, ny, nz;
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

void chgfname(char* name, int modein, int modeout) {
    memcpy(strstr(name, IN_EXTS[modein]), OUT_EXTS[modeout], strlen(OUT_EXTS[modeout]) + 1);
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
    *buffer = readbytes(filename, len);
}

// Set the color with the object name (like lew's SWMesh2XML)
bool extract_color(char* obj_name, vertex* vtx) {
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

        printf("%d\n", sm.vertex_count);
        vertex col_vtx;
        if (extract_color(sm.id, &col_vtx)) {
            for (int i = 0; i < sm.vertex_count; ++i) {
                m.vertices[i + sm.start_index].r = col_vtx.r;
                m.vertices[i + sm.start_index].g = col_vtx.g;
                m.vertices[i + sm.start_index].b = col_vtx.b;
            }
        }

        // NOTE: TinyOBJ doesn't appear to have (working) support for MTL files yet?
        // TODO: Proper MTL-based vertex coloring

        // for (int i = 0; i < sm.vertex_count * 3; ++i) {
        // int matidx = obj_attrs.material_ids[sm.start_index / 3];
        // printf("face %d mtl %d\n", (i + sm.start_index) / 3, matidx);
        // if (matidx < 0) continue;
        // tinyobj_material_t mtl = obj_mtls[matidx];
        // m.vertices[i + sm.start_index].r = (uint8_t)(mtl.diffuse[0] * 255.0f);
        // m.vertices[i + sm.start_index].g = (uint8_t)(mtl.diffuse[1] * 255.0f);
        // m.vertices[i + sm.start_index].b = (uint8_t)(mtl.diffuse[2] * 255.0f);
        // }

        m.submeshes[s] = sm;
    }

    recalculate_submesh_bounds(&m);

exit:
    tinyobj_attrib_free(&obj_attrs);
    if (obj_mtls) tinyobj_materials_free(obj_mtls, obj_n_mtls);
    if (obj_shapes) tinyobj_shapes_free(obj_shapes, obj_n_shapes);
    return m;
}

// Writes `m`m to `destfd` in wavefront OBJ format
// TODO: OBJ Submesh export
int writeobj(mesh m, FILE* destfd) {
    FILE* obj = destfd;

    fprintf(obj, "# Created by StormworksMeshExporter v%s\no\n", VERSION_STR);

    // Write OBJ-formatted mesh

    // Vertices
    for (int v = 0; v < m.n_vertices; ++v) {
        fprintf(obj, "v %f %f %f\n",
            m.vertices[v].x, m.vertices[v].y, m.vertices[v].z
            // (float)m.vertices[v].r / 255.0f, (float)m.vertices[v].g / 255.0f, (float)m.vertices[v].b / 255.0f
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
    for (int t = 0; t < m.n_triangles; ++t) {
        fprintf(obj, "f %d/%d/%d %d/%d/%d %d/%d/%d\n",
            m.triangles[t].b + 1, m.triangles[t].b + 1, m.triangles[t].b + 1,
            m.triangles[t].a + 1, m.triangles[t].a + 1, m.triangles[t].a + 1,
            m.triangles[t].c + 1, m.triangles[t].c + 1, m.triangles[t].c + 1
        );
    }

    return 0;
}

// Writes `m` to `destfd` in stanford PLY format with vertex colors
int writeply(mesh m, FILE* destfd) {
    FILE* ply = destfd;

    fprintf(ply, "ply\nformat ascii 1.0\ncomment Created by StormworksMeshExporter v%s\n", VERSION_STR);
    fprintf(ply, "element vertex %d\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\n", m.n_vertices);
    fprintf(ply, "element face %d\nproperty list uchar uint vertex_indices\n", m.n_triangles);
    fprintf(ply, "end_header\n");
    // Write PLY-formatted mesh

    // Vertices
    for (int v = 0; v < m.n_vertices; ++v) {
        fprintf(ply, "%f %f %f %d %d %d\n", m.vertices[v].x, m.vertices[v].z, m.vertices[v].y, m.vertices[v].r, m.vertices[v].g, m.vertices[v].b);
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

// Write basic data of `m`, in a human-readable format to STDOUT
int writestdout(mesh m) {
    printf("--BEGIN MESH OUTPUT--\n");

    printf("%d VERTICES\n", m.n_vertices);
    for (int v = 0; v < m.n_vertices; ++v) {
        printf("%f, %f, %f\n", m.vertices[v].x, m.vertices[v].y, m.vertices[v].z);
    }

    printf("%d NORMALS\n", m.n_vertices);
    for (int v = 0; v < m.n_vertices; ++v) {
        printf("%f, %f, %f\n", m.vertices[v].nx, m.vertices[v].ny, m.vertices[v].nz);
    }

    printf("%d COLORS\n", m.n_vertices);
    for (int v = 0; v < m.n_vertices; ++v) {
        printf("%d, %d, %d\n", m.vertices[v].r, m.vertices[v].g, m.vertices[v].b);
    }

    printf("%d TRIANGLES\n", m.n_triangles);
    for (int t = 0; t < m.n_triangles; ++t) {
        printf("%d, %d, %d\n",
            m.triangles[t].b,
            m.triangles[t].a,
            m.triangles[t].c
        );
    }

    printf("%d SUBMESHES\n", m.n_submeshes);
    for (int s = 0; s < m.n_submeshes; ++s) {
        submesh sm = m.submeshes[s];
        printf("%s: %d+%d, (%f, %f, %f)-(%f, %f, %f), %d\n",
            sm.id,
            sm.start_index,
            sm.vertex_count,
            sm.cullmin[0], sm.cullmin[1], sm.cullmin[2],
            sm.cullmax[0], sm.cullmax[1], sm.cullmax[2],
            sm.shadertype
        );
    }
    printf("--END MESH OUTPUT--\n");

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

int cvtmesh(char* srcfile, char* destfile, int input_mode, int output_mode) {
    int err = 0;
    mesh m;
    printf("Converting %s to %s\n", srcfile, destfile);

    if (input_mode == INPUT_MESH) {
        m = readmesh(srcfile, &err);
        if (err)
            return err;
    } else if (input_mode == INPUT_OBJ) {
        m = readobj(srcfile, &err);
        if (err)
            return err;
    }

    printf("Mesh loaded with %d vertices and %d faces\n", m.n_vertices, m.n_triangles);

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

    if (output_mode == OUTPUT_STDOUT) {
        err = writestdout(m);
    } else if (output_mode == OUTPUT_MULTI_PLY) {
        char* outdir = malloc(128);
        memcpy(outdir, destfile, strlen(destfile) + 1);
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
            writeply(sub, outfile);
            fclose(outfile);
        }

        free(buf);
        free(outdir);
    } else {
        FILE* outfile = fopen(destfile, output_mode == OUTPUT_STORMWORKS ? "wb" : "w");
        if (outfile == NULL) {
            err = 2;
            goto exit;
        }

        if (output_mode == OUTPUT_PLY)
            err = writeply(m, outfile);
        else if (output_mode == OUTPUT_OBJ)
            err = writeobj(m, outfile);
        else if (output_mode == OUTPUT_STORMWORKS)
            err = writemesh(m, outfile);
        fclose(outfile);
    }

exit:
    if (err) printf("Error #%d converting mesh %s\n", err, srcfile);
    freemesh(&m);
    return err;
}

int processfile(char* input_filename, char* output_filename, int input_mode, int output_mode) {
    // Auto generate output filename
    if (output_filename == NULL) {
        memcpy(tmp_buf, input_filename, strlen(input_filename));
        chgfname(tmp_buf, input_mode, output_mode);
        output_filename = tmp_buf;
    }

    clock_t start = clock();
    int res = cvtmesh(input_filename, output_filename, input_mode, output_mode);
    if (res != 0) {
        printf("Error %d converting file \"%s\"\n", res, input_filename);
        return res;
    }

    clock_t time = clock() - start;
    printf("Success! (%ld ms)\n", time * 1000 / CLOCKS_PER_SEC);
    return res;
}

int main(int argc, char** argv) {
    int res = 0;

    int output_mode = OUTPUT_PLY, input_mode = INPUT_MESH;
    char* inpfile = NULL;

    // v0.2: More inputs
    // DONE: Manual output file selection
    // DONE: Multiple input/output (<infile> -o <outfile>) pairs?
    // DONE: OBJ file reading (except vertex colors because tinyobj)
    // TODO: Document all options & capabilities after proper CLI is done in HELPSTR
    // TODO: Stormworks physics mesh IO

    // v0.3: CLI QOL features
    // TODO: Automatic input/output type detection if not manually set
    // TODO: Directory mode: Searches input directory for all files matching input type and converts them to selected output type. if an output option is provided, use it as a directory to store the output files. recursive option
    // TODO: Allow MISO (multiple-input-single-output) mesh converting with -i input file flags and specification of submesh data for each input (shader type, ID)

    int opt;
    while ((opt = getopt(argc, argv, "-:I:O:o:h")) != -1) {
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
                } else if (!strcasecmp(optarg, "stdout")) {
                    output_mode = OUTPUT_STDOUT;
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
                } else {
                    printf("Error, invalid input type \"%s\", see help (-h) for valid options.\n", optarg);
                    res = 5;
                    goto exit;
                }
                break;

            case 1:
                // If there is a file that hasn't been converted yet and no output has been given, convert it automatically.
                if (inpfile != NULL) {
                    processfile(inpfile, NULL, input_mode, output_mode);
                    inpfile = NULL;
                }
                inpfile = optarg;
                break;

            case 'o': // When output name is given, convert the previous file
                processfile(inpfile, optarg, input_mode, output_mode);
                inpfile = NULL;
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
    if (inpfile != NULL)
        processfile(inpfile, NULL, input_mode, output_mode);

exit:
    return res;
}