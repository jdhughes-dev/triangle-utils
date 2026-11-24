/* Tests that the C API header compiles as C and the library links from C. */

#include <stdio.h>
#include <string.h>

#include "triangle_utils.h"

int main(void)
{
    const double vertex_xy[] = {0.0, 0.0, 1.0, 0.0, 0.0, 1.0};
    const int triangle_corners[] = {0, 1, 2};
    TriangleUtilsMesh * mesh = NULL;
    int vertex_count = 0;
    int triangle_count = 0;

    if (strcmp(triangle_utils_version(), TRIANGLE_UTILS_VERSION) != 0)
    {
        printf("the library version is not the header version\n");
        return 1;
    }
    if (triangle_utils_mesh_create(3, vertex_xy, 1, triangle_corners, &mesh) != TRIANGLE_UTILS_OK ||
        triangle_utils_mesh_counts(mesh, &vertex_count, &triangle_count) != TRIANGLE_UTILS_OK ||
        vertex_count != 3 || triangle_count != 1)
    {
        printf("creating a mesh failed: %s\n", triangle_utils_last_error());
        triangle_utils_mesh_free(mesh);
        return 1;
    }
    triangle_utils_mesh_free(mesh);
    return 0;
}
