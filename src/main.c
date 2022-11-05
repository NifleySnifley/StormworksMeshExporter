#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <stdint.h>
#include <libgen.h>
#include <windows.h>

const char* OUT_EXTS[4] = {
    ".ply",
    ".obj",
    ".ply",
    ".export.mesh"
};

enum OUTPUT_MODE {
    OUTPUT_PLY,
    OUTPUT_OBJ,
    OUTPUT_MULTI_PLY,
    OUTPUT_STORMWORKS,
    OUTPUT_STDOUT,
    OUTPUT_NONE
};

const char* IN_EXTS[1] = {
    ".mesh",
    // ".obj"
};

enum INPUT_MODE {
    INPUT_MESH,
    // INPUT_OBJ
};

const char* SIGNATURE = "mesh";
const char* SHADER_TYPES[4] = {
    "opaque",
    "glass",
    "emissive",
    "unknown"
};

typedef struct vertex {
    float x, y, z;
    uint8_t r, g, b, a;
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

typedef struct mesh {
    char* name;
    int n_vertices;
    vertex* vertices;
    int n_triangles;
    triangle* triangles;
    int n_submeshes;
    submesh* submeshes;
} mesh;

void freemesh(mesh* m) {
    free(m->vertices);
    m->vertices = NULL;
    free(m->triangles);
    m->triangles = NULL;
    for (int i = 0; i < m->n_submeshes; ++i) freesubmesh(&m->submeshes[i]);
    free(m->submeshes);
    m->submeshes = NULL;
}

int chgfname(char* name, int mode) {
    memcpy(strstr(name, ".mesh"), OUT_EXTS[mode], strlen(OUT_EXTS[mode]) + 1);
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

// TODO: Assimp OBJ file reading
mesh readobj(char* fbytes) {

}

// Writes `m`m to `destfd` in wavefront OBJ format
// TODO: OBJ Submesh export
int writeobj(mesh m, FILE* destfd) {
    FILE* obj = destfd;

    fprintf(obj, "# Created by StormworksMeshExporter v0.1\no\n");

    // Write OBJ-formatted mesh

    // Vertices
    for (int v = 0; v < m.n_vertices; ++v) {
        fprintf(obj, "v %f %f %f %f %f %f\n",
            m.vertices[v].x, m.vertices[v].y, m.vertices[v].z,
            (float)m.vertices[v].r / 255.0f, (float)m.vertices[v].g / 255.0f, (float)m.vertices[v].b / 255.0f
        );
    }

    // Normals
    for (int v = 0; v < m.n_vertices; ++v) {
        fprintf(obj, "vn %f %f %f\n", m.vertices[v].nx, m.vertices[v].ny, m.vertices[v].nz);
    }

    // Triangles
    for (int t = 0; t < m.n_triangles; ++t) {
        fprintf(obj, "f %d//%d %d//%d %d//%d\n",
            m.triangles[t].b + 1, m.triangles[t].c + 1,
            m.triangles[t].a + 1, m.triangles[t].b + 1,
            m.triangles[t].c + 1, m.triangles[t].a + 1
        );
    }

    return 0;
}

// Writes `m` to `destfd` in stanford PLY format with vertex colors
int writeply(mesh m, FILE* destfd) {
    FILE* ply = destfd;

    fprintf(ply, "ply\nformat ascii 1.0\ncomment Created by StormworksMeshExporter v0.1\n");
    fprintf(ply, "element vertex %d\nproperty float32 x\nproperty float32 y\nproperty float32 z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\n", m.n_vertices);
    fprintf(ply, "element face %d\nproperty list uint8 int32 vertex_indices\n", m.n_vertices);
    fprintf(ply, "end_header\n");
    // Write PLY-formatted mesh

    // Vertices
    for (int v = 0; v < m.n_vertices; ++v) {
        fprintf(ply, "%f %f %f %d %d %d\n", m.vertices[v].x, m.vertices[v].z, m.vertices[v].y, m.vertices[v].r, m.vertices[v].g, m.vertices[v].b);
    }

    // Triangles
    for (int t = 0; t < m.n_triangles; ++t) {
        fprintf(ply, "%d %d %d %d\n",
            m.triangles[t].a,
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
mesh loadphys(char* fbytes) {

}

// TODO: Phys file export
int writephys(mesh m, FILE* destfd) {

}

// Loads the mesh data stored in `fbytes` (encoded in Stormworks .mesh format) 
mesh loadmesh(char* fbytes) {
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

    // TODO: Submeshes
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

    mesh m;
    m.n_vertices = vtxcount;
    m.vertices = verts;
    m.n_triangles = tricount;
    m.triangles = tris;
    m.n_submeshes = submeshcount;
    m.submeshes = submeshes;

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

    fwrite("\x00\x00", 2, 1, destfd);

    fflush(destfd);

    return 0;
}

int cvtmesh(char* srcfile, char* destfile, int input_mode, int output_mode) {
    char* meshbytes;
    int err = 0;
    mesh m;
    printf("Converting %s to %s\n", srcfile, destfile);

    size_t len = 0;
    meshbytes = readbytes(srcfile, &len);
    if (meshbytes == NULL) {
        err = 1;
        goto exit;
    }

    if (input_mode == INPUT_MESH)
        m = loadmesh(meshbytes);
    // else if (input_mode == INPUT_OBJ)
    //     m = readobj(meshbytes);

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

int main(int argc, char** argv) {
    char* out_filename_buf = malloc(1024 * sizeof(char));
    int res = 0;

    int output_mode = OUTPUT_PLY, input_mode = INPUT_MESH;

    if (argc == 1) {
        printf("Error, input filename must be specified\n");
        res = 3;
        goto exit;
    }

    // Parse flags
    int argidx = 1;
    for (;argv[argidx][0] == '-'; argidx++) {
        // printf("Flag %s\n", argv[argidx]);
        if (!strcmp(argv[argidx], "--obj")) {
            output_mode = OUTPUT_OBJ;
        } else if (!strcmp(argv[argidx], "--ply")) {
            output_mode = OUTPUT_PLY;
        } else if (!strcmp(argv[argidx], "--mesh") || !strcmp(argv[argidx], "--stormworks")) {
            output_mode = OUTPUT_STORMWORKS;
        } else if (!strcmp(argv[argidx], "--plys") || !strcmp(argv[argidx], "--multiply")) {
            output_mode = OUTPUT_MULTI_PLY;
        } else if (!strcmp(argv[argidx], "--stdout")) {
            output_mode = OUTPUT_STDOUT;
        } else if (!strcmp(argv[argidx], "--dryrun")) {
            output_mode = OUTPUT_PLY;
        }

        if (argidx == (argc - 1)) {
            printf("Error, input filename must be specified\n");
            res = 2;
            goto exit;
        }
    }

    // TODO: Allow custom output filenames with <infile> -o <outfile>
    for (int i = argidx; i < argc; ++i) {
        memcpy(out_filename_buf, argv[i], strlen(argv[i]));
        chgfname(out_filename_buf, output_mode);
        res = cvtmesh(argv[i], out_filename_buf, input_mode, output_mode);
        if (res != 0) {
            printf("Error %d converting file #%d\n", res, i - 1);
            break;
        }
    }

exit:
    free(out_filename_buf);
    if (res != 0) {
        return res;
    } else {
        printf("converted %d file(s) successfully\n", argc - argidx);
        return 0;
    }
}