#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <stdint.h>

const char* OUT_EXTS[2] = {
    ".ply",
    ".obj",
};
const char* SIGNATURE = "mesh";

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

enum OUTPUT_MODE {
    OUTPUT_PLY,
    OUTPUT_OBJ,
    OUTPUT_STDOUT,
    OUTPUT_NONE
};

void chgfname(char* name, int mode) {
    memcpy(strstr(name, ".mesh"), OUT_EXTS[mode], strlen(OUT_EXTS[mode]) + 1);
}

char* readbytes(char* filename, size_t* len) {
    struct stat st;
    stat(filename, &st);
    *len = st.st_size;

    char* buf = malloc(*len * sizeof(char));

    FILE* fhandle = fopen(filename, "rb");
    fread(buf, *len, 1, fhandle);
    fclose(fhandle);

    return buf;
}

int writeobj(vertex* verts, int vtxcount, triangle* tris, int tricount, FILE* destfd) {
    FILE* obj = destfd;

    fprintf(obj, "# Created by StormworksMeshExporter v0.1\no\n");

    // Write OBJ-formatted mesh

    // Vertices
    for (int v = 0; v < vtxcount; ++v) {
        fprintf(obj, "v %f %f %f\n", verts[v].x, verts[v].y, verts[v].z);
    }

    // Normals
    for (int v = 0; v < vtxcount; ++v) {
        fprintf(obj, "vn %f %f %f\n", verts[v].nx, verts[v].ny, verts[v].nz);
    }

    // Triangles
    for (int t = 0; t < tricount; ++t) {
        fprintf(obj, "f %d//%d %d//%d %d//%d\n",
            tris[t].b + 1, tris[t].c + 1,
            tris[t].a + 1, tris[t].b + 1,
            tris[t].c + 1, tris[t].a + 1
        );
    }

    return 0;
}

int writeply(vertex* verts, int vtxcount, triangle* tris, int tricount, FILE* destfd) {
    FILE* ply = destfd;

    fprintf(ply, "ply\nformat ascii 1.0\ncomment Created by StormworksMeshExporter v0.1\n");
    fprintf(ply, "element vertex %d\nproperty float32 x\nproperty float32 y\nproperty float32 z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\n", vtxcount);
    fprintf(ply, "element face %d\nproperty list uint8 int32 vertex_indices\n", tricount);
    fprintf(ply, "end_header\n");
    // Write PLY-formatted mesh

    // Vertices
    for (int v = 0; v < vtxcount; ++v) {
        fprintf(ply, "%f %f %f %d %d %d\n", verts[v].x, verts[v].z, verts[v].y, verts[v].r, verts[v].g, verts[v].b);
    }

    // Triangles
    for (int t = 0; t < tricount; ++t) {
        fprintf(ply, "%d %d %d %d\n",
            tris[t].a,
            tris[t].b,
            tris[t].c,
            tris[t].a
        );
    }

    fclose(ply);

    return 0;
}

// Write the basic data, in a human-readable format to STDOUT
int writestdout(vertex* verts, int vtxcount, triangle* tris, int tricount) {
    printf("--BEGIN MESH OUTPUT--\n");

    printf("%d VERTICES\n", vtxcount);
    for (int v = 0; v < vtxcount; ++v) {
        printf("%f, %f, %f\n", verts[v].x, verts[v].y, verts[v].z);
    }

    printf("%d NORMALS\n", vtxcount);
    for (int v = 0; v < vtxcount; ++v) {
        printf("%f, %f, %f\n", verts[v].nx, verts[v].ny, verts[v].nz);
    }

    printf("%d COLORS\n", vtxcount);
    for (int v = 0; v < vtxcount; ++v) {
        printf("%d, %d, %d\n", verts[v].r, verts[v].g, verts[v].b);
    }

    printf("%d TRIANGLES\n", tricount);
    for (int t = 0; t < tricount; ++t) {
        printf("%d, %d, %d\n",
            tris[t].b,
            tris[t].a,
            tris[t].c
        );
    }
    printf("--END MESH OUTPUT--\n");

    return 0;
}

int cvtfile(char* srcfile, char* destfile, int output_mode) {
    printf("Converting %s to %s\n", srcfile, destfile);

    size_t len = 0;
    char* meshbytes = readbytes(srcfile, &len);
    printf("Read %d bytes\n", len);

    printf("%s\n", meshbytes);
    int cursor = 8; // skip the first 8 bytes "mesh" + 4 header

    // Vertex count
    uint16_t vtxcount = *((uint16_t*)&meshbytes[cursor]);
    cursor += 2;
    printf("%d vertices\n", vtxcount);

    // Unknown
    cursor += 4;

    // Vertices
    vertex* verts = (vertex*)&meshbytes[cursor];
    cursor += vtxcount * sizeof(vertex);

    // Triangle (Face) count
    uint32_t tricount = *((uint32_t*)&meshbytes[cursor]) / 3;
    cursor += 4;
    printf("%d triangles\n", tricount);

    // Edges (Triangles)
    triangle* tris = (triangle*)&meshbytes[cursor];
    cursor += tricount * sizeof(triangle);

    // Submesh count
    uint16_t submeshcount = *((uint16_t*)&meshbytes[cursor]);
    cursor += 2;

    // TODO: Submeshes

    int err = 0;
    if (output_mode == OUTPUT_STDOUT) {
        err = writestdout(verts, (int)vtxcount, tris, (int)tricount);
    } else {
        FILE* outfile = fopen(destfile, "w");
        if (outfile == NULL) {
            err = 1;
            printf("Error, could not open output file \"%s\"\n", destfile);
            goto exit;
        }
        if (output_mode == OUTPUT_PLY)
            err = writeply(verts, (int)vtxcount, tris, (int)tricount, outfile);
        else if (output_mode == OUTPUT_OBJ)
            err = writeobj(verts, (int)vtxcount, tris, (int)tricount, outfile);
        fclose(outfile);
    }

exit:
    free(meshbytes);
    return err;
}

int main(int argc, char** argv) {
    char* out_filename_buf = malloc(1024 * sizeof(char));
    int res = 0;

    int output_mode = OUTPUT_PLY;

    // Parse flags
    int argidx = 1;
    for (;argv[argidx][0] == '-'; argidx++) {
        // printf("Flag %s\n", argv[argidx]);
        if (!strcmp(argv[argidx], "--obj")) {
            output_mode = OUTPUT_OBJ;
        } else if (!strcmp(argv[argidx], "--ply")) {
            output_mode = OUTPUT_PLY;
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
        res = cvtfile(argv[i], out_filename_buf, output_mode);
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
        printf("converted %d files successfully\n", argc - 1);
        return 0;
    }
}