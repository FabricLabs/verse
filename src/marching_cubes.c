#include "marching_cubes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

// Edge table for marching cubes (256 cases)
static const int edge_table[256] = {
    0x0, 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c, 0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
    0x190, 0x99, 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c, 0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
    0x230, 0x339, 0x33, 0x13a, 0x636, 0x73f, 0x435, 0x53c, 0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
    0x3a0, 0x2a9, 0x1a3, 0xaa, 0x7a6, 0x6af, 0x5a5, 0x4ac, 0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
    0x460, 0x569, 0x663, 0x76a, 0x66, 0x16f, 0x265, 0x36c, 0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
    0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff, 0x3f5, 0x2fc, 0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
    0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55, 0x15c, 0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
    0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc, 0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
    0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc, 0xcc, 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
    0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c, 0x15c, 0x55, 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
    0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc, 0x2fc, 0x3f5, 0xff, 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
    0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c, 0x36c, 0x265, 0x16f, 0x66, 0x76a, 0x663, 0x569, 0x460,
    0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac, 0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa, 0x1a3, 0x2a9, 0x3a0,
    0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c, 0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33, 0x339, 0x230,
    0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c, 0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99, 0x190,
    0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c, 0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0
};

// Triangle table for marching cubes (256 cases)
static const int tri_table[256][16] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1, -1},
    {3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1, -1},
    {3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1},
    {3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1},
    {9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
    {2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1, -1},
    {8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1, -1},
    {3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1, -1},
    {1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1, -1},
    {4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1, -1},
    {4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1, -1},
    {2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1, -1},
    {9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1, -1},
    {10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1, -1},
    {5, 4, 0, 5, 0, 10, 5, 10, 11, 10, 0, 3, -1, -1, -1, -1},
    {5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1},
    {9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1},
    {1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1, -1},
    {10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1, -1},
    {8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1, -1},
    {2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1},
    {7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1, -1},
    {2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1, -1},
    {11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1, -1},
    {5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1},
    {11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1},
    {11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1, -1},
    {9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1, -1, -1, -1},
    {2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1, -1},
    {6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1, -1},
    {3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1, -1},
    {6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
    {10, 6, 5, 7, 1, 9, 7, 3, 1, 7, 4, 3, -1, -1, -1, -1},
    {6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1, -1},
    {8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1},
    {7, 4, 9, 7, 9, 3, 3, 9, 5, 3, 5, 6, 3, 6, 2, -1},
    {3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1, -1},
    {0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1},
    {9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1},
    {8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1, -1},
    {5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1},
    {0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1},
    {6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1, -1},
    {10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1},
    {10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1, -1},
    {8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1, -1, -1, -1},
    {1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1, -1},
    {0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1, -1},
    {3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1, -1},
    {6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1},
    {9, 10, 4, 9, 4, 1, 1, 4, 6, 1, 6, 3, 1, 3, 11, -1},
    {8, 11, 4, 8, 4, 9, 11, 2, 4, 10, 4, 6, 2, 6, 4, -1},
    {3, 11, 2, 3, 2, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1},
    {6, 4, 8, 6, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1, -1},
    {10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1, -1},
    {10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1, -1},
    {2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1},
    {7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1, -1},
    {7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1, -1},
    {2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1},
    {1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1},
    {11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1, -1},
    {8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1},
    {0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1, -1},
    {7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};

// Edge vertices for interpolation
static const int edge_vertices[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

// Edge directions for interpolation (unused in current implementation)
// static const float edge_directions[12][3] = {
//     {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
//     {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
//     {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
//     {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}
// };

// Helper function to get voxel density at a given position
static float get_voxel_density(const SimpleWorld *world, int x, int y, int z)
{
  if (x < 0 || y < 0 || z < 0 ||
      x >= (int)world->width || y >= (int)world->height || z >= (int)world->depth)
  {
    return 0.0f; // Air outside world bounds
  }

  const SimpleVoxel *voxel = simple_world_get_voxel((SimpleWorld *)world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
  if (!voxel || voxel->type == VOXEL_AIR)
  {
    return 0.0f; // Air
  }

  // Return density based on voxel type (1.0 for solid, 0.0 for air)
  return 1.0f;
}

// Interpolate vertex position along an edge
static void interpolate_vertex(float *out_pos, uint8_t *out_color,
                               const SimpleWorld *world, int x1, int y1, int z1,
                               int x2, int y2, int z2, float isolevel) {
    float d1 = get_voxel_density(world, x1, y1, z1);
    float d2 = get_voxel_density(world, x2, y2, z2);

    // Ensure we have a valid density range for interpolation
    if (d1 == d2) {
        // Densities are equal, use midpoint
        out_pos[0] = (x1 + x2) * 0.5f;
        out_pos[1] = (y1 + y2) * 0.5f;
        out_pos[2] = (z1 + z2) * 0.5f;
    } else {
        // Clamp the interpolation to avoid extreme values
        float t = (isolevel - d1) / (d2 - d1);
        t = fmaxf(0.0f, fminf(1.0f, t)); // Clamp to [0,1]

        out_pos[0] = x1 + t * (x2 - x1);
        out_pos[1] = y1 + t * (y2 - y1);
        out_pos[2] = z1 + t * (z2 - z1);
    }

    // Interpolate color
    if (out_color) {
        uint8_t r1, g1, b1, r2, g2, b2;
        const SimpleVoxel *v1 = simple_world_get_voxel((SimpleWorld*)world, x1, y1, z1);
        const SimpleVoxel *v2 = simple_world_get_voxel((SimpleWorld*)world, x2, y2, z2);

        if (v1 && v1->type != VOXEL_AIR) {
            simple_world_voxel_type_color(v1->type, &r1, &g1, &b1);
        } else {
            r1 = g1 = b1 = 0;
        }

        if (v2 && v2->type != VOXEL_AIR) {
            simple_world_voxel_type_color(v2->type, &r2, &g2, &b2);
        } else {
            r2 = g2 = b2 = 0;
        }

        float t = (isolevel - d1) / (d2 - d1);
        t = fmaxf(0.0f, fminf(1.0f, t)); // Clamp to [0,1]

        out_color[0] = (uint8_t)(r1 + t * (r2 - r1));
        out_color[1] = (uint8_t)(g1 + t * (g2 - g1));
        out_color[2] = (uint8_t)(b1 + t * (b2 - b1));
        out_color[3] = 255;
    }
}

// Calculate normal for a triangle
static void calculate_triangle_normal(const MarchingCubesVertex *v1,
                                      const MarchingCubesVertex *v2,
                                      const MarchingCubesVertex *v3,
                                      float *nx, float *ny, float *nz)
{
  float ux = v2->x - v1->x;
  float uy = v2->y - v1->y;
  float uz = v2->z - v1->z;

  float vx = v3->x - v1->x;
  float vy = v3->y - v1->y;
  float vz = v3->z - v1->z;

  *nx = uy * vz - uz * vy;
  *ny = uz * vx - ux * vz;
  *nz = ux * vy - uy * vx;

  // Normalize
  float length = sqrtf(*nx * *nx + *ny * *ny + *nz * *nz);
  if (length > FLT_EPSILON)
  {
    *nx /= length;
    *ny /= length;
    *nz /= length;
  }
}

// Ensure proper triangle winding order for outward-facing normals
static void fix_triangle_winding_order(MarchingCubesMesh *mesh, const SimpleWorld *world, float isolevel_unused)
{
    if (!mesh || !world) return;

    for (uint32_t i = 0; i < mesh->triangle_count; i++) {
        MarchingCubesTriangle *triangle = &mesh->triangles[i];
        MarchingCubesVertex *v1 = &mesh->vertices[triangle->vertices[0]];
        MarchingCubesVertex *v2 = &mesh->vertices[triangle->vertices[1]];
        MarchingCubesVertex *v3 = &mesh->vertices[triangle->vertices[2]];

        // Calculate triangle center
        float center_x = (v1->x + v2->x + v3->x) / 3.0f;
        float center_y = (v1->y + v2->y + v3->y) / 3.0f;
        float center_z = (v1->z + v2->z + v3->z) / 3.0f;

        // Calculate current normal
        float nx, ny, nz;
        calculate_triangle_normal(v1, v2, v3, &nx, &ny, &nz);

        // Sample density at triangle center and slightly outside
        float center_density = get_voxel_density(world,
            (int)(center_x / 1.0f), (int)(center_y / 1.0f), (int)(center_z / 1.0f));

        // Sample density slightly outside the triangle (in normal direction)
        float outside_x = center_x + nx * 0.5f;
        float outside_y = center_y + ny * 0.5f;
        float outside_z = center_z + nz * 0.5f;
        float outside_density = get_voxel_density(world,
            (int)outside_x, (int)outside_y, (int)outside_z);

        // If the normal points toward higher density (inside the solid), flip the triangle
        if (outside_density > center_density) {
            // Swap vertices 1 and 2 to flip the normal
            uint32_t temp = triangle->vertices[1];
            triangle->vertices[1] = triangle->vertices[2];
            triangle->vertices[2] = temp;
        }
    }
}

// Fill internal cavities to create a more solid object
// static void fill_internal_cavities(SimpleWorld *world) {
//     if (!world) return;
//
//     // Simple flood fill approach: mark all air voxels that are completely surrounded
//     // This is a basic approach - more sophisticated methods could be used
//
//     for (uint32_t x = 1; x < world->width - 1; x++) {
//         for (uint32_t y = 1; y < world->height - 1; y++) {
//             for (uint32_t z = 1; z < world->depth - 1; z++) {
//                 const SimpleVoxel *voxel = simple_world_get_voxel(world, x, y, z);
//                 if (voxel && voxel->type == VOXEL_AIR) {
//                     // Check if this air voxel is completely surrounded by solid voxels
//                     bool surrounded = true;
//                     for (int dx = -1; dx <= 1 && surrounded; dx++) {
//                         for (int dy = -1; dy <= 1 && surrounded; dy++) {
//                             for (int dz = -1; dz <= 1 && surrounded; dz++) {
//                                 if (dx == 0 && dy == 0 && dz == 0) continue; // Skip self
//
//                                 const SimpleVoxel *neighbor = simple_world_get_voxel(world, x + dx, y + dy, z + dz);
//                                 if (!neighbor || neighbor->type == VOXEL_AIR) {
//                                     surrounded = false;
//                                 }
//                             }
//                         }
//                     }
//
//                     // If completely surrounded, fill with stone
//                     if (surrounded) {
//                         simple_world_set_voxel(world, x, y, z, VOXEL_STONE);
//                     }
//                 }
//             }
//         }
//     }
// }

// Create a new marching cubes mesh
MarchingCubesMesh *marching_cubes_create_mesh(void)
{
  MarchingCubesMesh *mesh = malloc(sizeof(MarchingCubesMesh));
  if (!mesh)
  {
    return NULL;
  }

  mesh->vertices = NULL;
  mesh->triangles = NULL;
  mesh->vertex_count = 0;
  mesh->triangle_count = 0;
  mesh->capacity = 0;

  return mesh;
}

// Destroy a marching cubes mesh
void marching_cubes_destroy_mesh(MarchingCubesMesh *mesh)
{
  if (!mesh)
    return;

  if (mesh->vertices)
  {
    free(mesh->vertices);
  }
  if (mesh->triangles)
  {
    free(mesh->triangles);
  }
  free(mesh);
}

// Reserve capacity for vertices and triangles
bool marching_cubes_reserve_capacity(MarchingCubesMesh *mesh, uint32_t vertex_capacity, uint32_t triangle_capacity) {
    if (!mesh) return false;

    // Reserve vertex capacity
    if (vertex_capacity > mesh->capacity) {
        MarchingCubesVertex *new_vertices = realloc(mesh->vertices, vertex_capacity * sizeof(MarchingCubesVertex));
        if (!new_vertices) return false;
        mesh->vertices = new_vertices;
        mesh->capacity = vertex_capacity;
    }

    // Reserve triangle capacity (separate from vertex capacity)
    if (triangle_capacity > 0) {
        MarchingCubesTriangle *new_triangles = realloc(mesh->triangles, triangle_capacity * sizeof(MarchingCubesTriangle));
        if (!new_triangles) return false;
        mesh->triangles = new_triangles;
    }

    return true;
}

// Main marching cubes generation function
bool marching_cubes_generate_from_world(const SimpleWorld *world,
                                       const MarchingCubesConfig *config,
                                       MarchingCubesMesh *mesh) {
    if (!world || !config || !mesh) return false;



    // Use default config if none provided
    MarchingCubesConfig default_config = MARCHING_CUBES_DEFAULT_CONFIG;
    if (!config) config = &default_config;

    // Clear existing mesh data
    mesh->vertex_count = 0;
    mesh->triangle_count = 0;

    // Estimate capacity needed (worst case: every cube generates triangles)
    uint32_t max_vertices = (world->width - 1) * (world->height - 1) * (world->depth - 1) * 12;
    uint32_t max_triangles = (world->width - 1) * (world->height - 1) * (world->depth - 1) * 5;

    if (!marching_cubes_reserve_capacity(mesh, max_vertices, max_triangles)) {
        return false;
    }

    printf("Debug: Processing %ux%ux%u world with isolevel %.3f\n",
           world->width, world->height, world->depth, config->isolevel);

    uint32_t cubes_processed = 0;
    uint32_t cubes_with_triangles = 0;
    uint32_t total_edges = 0;

    // Process each cube in the grid
    for (uint32_t x = 0; x < world->width - 1; x++) {
        for (uint32_t y = 0; y < world->height - 1; y++) {
            for (uint32_t z = 0; z < world->depth - 1; z++) {
                cubes_processed++;

                // Get the 8 vertices of this cube
                float cube_vertices[8][3];
                float cube_densities[8];
                uint8_t cube_colors[8][4];

                // Sample the 8 corners of the cube
                for (int i = 0; i < 8; i++) {
                    int dx = i & 1;
                    int dy = (i >> 1) & 1;
                    int dz = (i >> 2) & 1;

                    cube_vertices[i][0] = (float)(x + dx);
                    cube_vertices[i][1] = (float)(y + dy);
                    cube_vertices[i][2] = (float)(z + dz);

                    cube_densities[i] = get_voxel_density(world, x + dx, y + dy, z + dz);

                    if (config->generate_colors) {
                        const SimpleVoxel *voxel = simple_world_get_voxel((SimpleWorld*)world, x + dx, y + dy, z + dz);
                        if (voxel && voxel->type != VOXEL_AIR) {
                            simple_world_voxel_type_color(voxel->type, &cube_colors[i][0], &cube_colors[i][1], &cube_colors[i][2]);
                            cube_colors[i][3] = 255;
                        } else {
                            cube_colors[i][0] = cube_colors[i][1] = cube_colors[i][2] = cube_colors[i][3] = 0;
                        }
                    }
                }

                // Determine cube index based on which vertices are inside/outside the surface
                int cube_index = 0;
                for (int i = 0; i < 8; i++) {
                    if (cube_densities[i] > config->isolevel) {
                        cube_index |= (1 << i);
                    }
                }

                // Debug output for first few cubes
                if (cubes_processed <= 5) {
                    printf("Debug: Cube at (%u,%u,%u): densities=[", x, y, z);
                    for (int i = 0; i < 8; i++) {
                        printf("%.2f%s", cube_densities[i], i < 7 ? "," : "");
                    }
                    printf("], index=%d\n", cube_index);
                }

                // Process cubes that have surface transitions (not completely solid or air)
                // This includes cubes where some vertices are above isolevel and others below
                if (cube_index == 0 || cube_index == 255) continue;

                cubes_with_triangles++;

                // Debug output for cubes that generate triangles
                if (cubes_with_triangles <= 10) {
                    printf("Debug: Surface cube at (%u,%u,%u): densities=[", x, y, z);
                    for (int i = 0; i < 8; i++) {
                        printf("%.2f%s", cube_densities[i], i < 7 ? "," : "");
                    }
                    printf("], index=%d\n", cube_index);
                }

                // Get the edge table for this cube configuration
                int edges = edge_table[cube_index];
                if (edges == 0) continue;

                // Get the triangle table for this cube configuration
                const int *triangles = tri_table[cube_index];

                // Special case for cube index 7 (3 vertices inside) - create a proper connected surface
                if (cube_index == 7) {
                    // For cube index 7, we have 3 solid vertices at the bottom corners
                    // Create a proper triangulation that forms a connected surface
                    // We'll create triangles that ensure all vertices are shared

                    // First, let's process the edges we need
                    int needed_edges = 0;
                    needed_edges |= (1 << 2);  // Edge 2: between vertices 2 and 3
                    needed_edges |= (1 << 3);  // Edge 3: between vertices 3 and 0
                    needed_edges |= (1 << 8);  // Edge 8: between vertices 0 and 4
                    needed_edges |= (1 << 9);  // Edge 9: between vertices 1 and 5
                    needed_edges |= (1 << 10); // Edge 10: between vertices 2 and 6

                    // Override the edge table for this case
                    edges = needed_edges;

                    printf("Debug: Overriding edge table for cube index 7: 0x%x\n", edges);



                    // Create a custom triangle table that forms a PROPER CLOSED SURFACE
                    // This creates a surface that actually encloses the solid volume
                    // We need to create a surface that follows the natural boundary of the solid
                    static const int custom_triangles[12] = {
                        2, 8, 3,    // Triangle 1: connects edges 2, 8, 3 - forms the main surface
                        2, 10, 8,   // Triangle 2: connects edges 2, 10, 8 - extends the surface
                        10, 9, 8,   // Triangle 3: connects edges 10, 9, 8 - completes the surface
                        -1, -1, -1  // End marker - only 3 triangles for a proper surface
                    };
                    triangles = custom_triangles;

                    printf("Debug: Using proper closed surface triangulation for cube index 7\n");
                }

                // Debug output for edge and triangle data
                if (cubes_processed <= 5) {
                    printf("Debug:   edges=0x%x, triangles=[", edges);
                    for (int i = 0; i < 16; i++) {
                        if (triangles[i] == -1) break;
                        printf("%d%s", triangles[i], i < 15 && triangles[i+1] != -1 ? "," : "");
                    }
                    printf("]\n");
                }

                // Debug output for surface cubes
                if (cubes_with_triangles <= 10) {
                    printf("Debug:   edges=0x%x, triangles=[", edges);
                    for (int i = 0; i < 16; i++) {
                        if (triangles[i] == -1) break;
                        printf("%d%s", triangles[i], i < 15 && triangles[i+1] != -1 ? "," : "");
                    }
                    printf("]\n");

                    // Additional debug for triangle table access
                    printf("Debug:   cube_index=%d, tri_table[%d]=%p\n", cube_index, cube_index, (void*)triangles);
                    printf("Debug:   tri_table[%d][0]=%d, tri_table[%d][1]=%d, tri_table[%d][2]=%d\n",
                           cube_index, tri_table[cube_index][0],
                           cube_index, tri_table[cube_index][1],
                           cube_index, tri_table[cube_index][2]);
                }

                // Generate vertices for each edge that intersects the surface
                float edge_positions[12][3];
                uint8_t edge_colors[12][4];
                bool edge_valid[12] = {false};

                for (int i = 0; i < 12; i++) {
                    if (edges & (1 << i)) {
                        int v1 = edge_vertices[i][0];
                        int v2 = edge_vertices[i][1];

                        printf("Debug: Processing edge %d: vertices (%d,%d) -> (%d,%d,%d) to (%d,%d,%d)\n",
                               i, v1, v2,
                               (int)cube_vertices[v1][0], (int)cube_vertices[v1][1], (int)cube_vertices[v1][2],
                               (int)cube_vertices[v2][0], (int)cube_vertices[v2][1], (int)cube_vertices[v2][2]);

                        // Interpolate vertex position along the edge
                        interpolate_vertex(edge_positions[i],
                                        config->generate_colors ? edge_colors[i] : NULL,
                                        world,
                                        (int)cube_vertices[v1][0], (int)cube_vertices[v1][1], (int)cube_vertices[v1][2],
                                        (int)cube_vertices[v2][0], (int)cube_vertices[v2][1], (int)cube_vertices[v2][2],
                                        config->isolevel);

                        printf("Debug: Edge %d interpolated to position (%.3f, %.3f, %.3f)\n",
                               i, edge_positions[i][0], edge_positions[i][1], edge_positions[i][2]);

                        edge_valid[i] = true;
                        total_edges++;
                    }
                }

                // Generate triangles with proper surface connectivity
                for (int i = 0; i < 16; i += 3) {
                    if (triangles[i] == -1) break;

                    printf("Debug: Processing triangle %d: edges [%d, %d, %d]\n", i/3, triangles[i], triangles[i+1], triangles[i+2]);

                    // Check if all three edges are valid
                    if (!edge_valid[triangles[i]] || !edge_valid[triangles[i+1]] || !edge_valid[triangles[i+2]]) {
                        printf("Debug: Triangle %d skipped - invalid edges\n", i/3);
                        continue;
                    }

                    // Add vertices with proper deduplication
                    uint32_t vertex_indices[3];
                    for (int j = 0; j < 3; j++) {
                        int edge_idx = triangles[i + j];

                        printf("Debug: Processing vertex %d for edge %d\n", j, edge_idx);

                        // Check if we need to add a new vertex
                        bool vertex_exists = false;
                        float tolerance = 0.01f; // Increased tolerance for better vertex sharing
                        for (uint32_t k = 0; k < mesh->vertex_count; k++) {
                            if (fabsf(mesh->vertices[k].x - edge_positions[edge_idx][0] * config->voxel_size) < tolerance &&
                                fabsf(mesh->vertices[k].y - edge_positions[edge_idx][1] * config->voxel_size) < tolerance &&
                                fabsf(mesh->vertices[k].z - edge_positions[edge_idx][2] * config->voxel_size) < tolerance) {
                                vertex_indices[j] = k;
                                vertex_exists = true;
                                printf("Debug: Vertex %d reused existing vertex %d\n", j, k);
                                break;
                            }
                        }

                        if (!vertex_exists) {
                            if (mesh->vertex_count >= max_vertices) {
                                // Reallocate if needed
                                uint32_t new_capacity = max_vertices * 2;
                                if (!marching_cubes_reserve_capacity(mesh, new_capacity, max_triangles)) {
                                    return false;
                                }
                                max_vertices = new_capacity;
                            }

                            vertex_indices[j] = mesh->vertex_count;

                            MarchingCubesVertex *vertex = &mesh->vertices[mesh->vertex_count];
                            vertex->x = edge_positions[edge_idx][0] * config->voxel_size;
                            vertex->y = edge_positions[edge_idx][1] * config->voxel_size;
                            vertex->z = edge_positions[edge_idx][2] * config->voxel_size;

                            printf("Debug: Created new vertex %d at (%.3f, %.3f, %.3f)\n",
                                   mesh->vertex_count, vertex->x, vertex->y, vertex->z);

                            if (config->generate_colors) {
                                vertex->r = edge_colors[edge_idx][0];
                                vertex->g = edge_colors[edge_idx][1];
                                vertex->b = edge_colors[edge_idx][2];
                                vertex->a = edge_colors[edge_idx][3];
                            } else {
                                vertex->r = vertex->g = vertex->b = 128;
                                vertex->a = 255;
                            }

                            // Initialize normals to zero
                            vertex->nx = vertex->ny = vertex->nz = 0.0f;

                            mesh->vertex_count++;
                        }
                    }

                    // Add triangle if all vertices are valid and different
                    if (vertex_indices[0] != vertex_indices[1] &&
                        vertex_indices[1] != vertex_indices[2] &&
                        vertex_indices[0] != vertex_indices[2]) {

                        if (mesh->triangle_count >= max_triangles) {
                            // Reallocate if needed
                            uint32_t new_capacity = max_triangles * 2;
                            if (!marching_cubes_reserve_capacity(mesh, max_vertices, new_capacity)) {
                                return false;
                            }
                            max_triangles = new_capacity;
                        }

                        MarchingCubesTriangle *triangle = &mesh->triangles[mesh->triangle_count];

                        // Ensure consistent winding order: vertices should be ordered counter-clockwise
                        // when viewed from outside the surface (density decreases outward)
                        triangle->vertices[0] = vertex_indices[0];
                        triangle->vertices[1] = vertex_indices[1];
                        triangle->vertices[2] = vertex_indices[2];

                        printf("Debug: Created triangle %d with vertices [%d, %d, %d]\n",
                               mesh->triangle_count, vertex_indices[0], vertex_indices[1], vertex_indices[2]);

                        mesh->triangle_count++;
                    } else {
                        printf("Debug: Triangle %d skipped - duplicate vertices [%d, %d, %d]\n",
                               i/3, vertex_indices[0], vertex_indices[1], vertex_indices[2]);
                    }
                }
            }
        }
    }

    printf("Debug: Processed %u cubes, %u generated triangles, %u total edges\n",
           cubes_processed, cubes_with_triangles, total_edges);
    printf("Debug: Final mesh: %u vertices, %u triangles\n",
           mesh->vertex_count, mesh->triangle_count);

    // After generating all triangles, ensure surface connectivity
    if (mesh->triangle_count > 0) {
        printf("Debug: Checking surface connectivity...\n");

        // Count how many triangles each vertex is part of
        int *vertex_triangle_count = calloc(mesh->vertex_count, sizeof(int));
        if (vertex_triangle_count) {
            for (uint32_t i = 0; i < mesh->triangle_count; i++) {
                const MarchingCubesTriangle *triangle = &mesh->triangles[i];
                vertex_triangle_count[triangle->vertices[0]]++;
                vertex_triangle_count[triangle->vertices[1]]++;
                vertex_triangle_count[triangle->vertices[2]]++;
            }

            // Print vertex connectivity info
            for (uint32_t i = 0; i < mesh->vertex_count; i++) {
                printf("Debug: Vertex %d is part of %d triangles\n", i, vertex_triangle_count[i]);
            }

            free(vertex_triangle_count);
        }
    }

    // Generate normals if requested
    if (config->generate_normals) {
        // Calculate normals for each triangle with proper winding order
        for (uint32_t i = 0; i < mesh->triangle_count; i++) {
            const MarchingCubesTriangle *triangle = &mesh->triangles[i];
            MarchingCubesVertex *v1 = &mesh->vertices[triangle->vertices[0]];
            MarchingCubesVertex *v2 = &mesh->vertices[triangle->vertices[1]];
            MarchingCubesVertex *v3 = &mesh->vertices[triangle->vertices[2]];

            float nx, ny, nz;
            calculate_triangle_normal(v1, v2, v3, &nx, &ny, &nz);

            // Add normal to each vertex (for smooth normals)
            v1->nx += nx; v1->ny += ny; v1->nz += nz;
            v2->nx += nx; v2->ny += ny; v2->nz += nz;
            v3->nx += nx; v3->ny += ny; v3->nz += nz;
        }

        // Normalize accumulated normals
        for (uint32_t i = 0; i < mesh->vertex_count; i++) {
            MarchingCubesVertex *vertex = &mesh->vertices[i];
            float length = sqrtf(vertex->nx * vertex->nx + vertex->ny * vertex->ny + vertex->nz * vertex->nz);
            if (length > FLT_EPSILON) {
                vertex->nx /= length;
                vertex->ny /= length;
                vertex->nz /= length;
            } else {
                // Fallback normal if no triangles contribute
                vertex->nx = 0.0f;
                vertex->ny = 1.0f;
                vertex->nz = 0.0f;
            }
        }
    }

    // Fix triangle winding order for outward-facing normals
    fix_triangle_winding_order(mesh, world, config->isolevel);

    return true;
}

// Export mesh to OBJ format
bool marching_cubes_export_obj(const MarchingCubesMesh *mesh, const char *filename)
{
  if (!mesh || !filename)
    return false;

  FILE *file = fopen(filename, "w");
  if (!file)
    return false;

  // Write header
  fprintf(file, "# Marching Cubes Generated Mesh\n");
  fprintf(file, "# Vertices: %u\n", mesh->vertex_count);
  fprintf(file, "# Triangles: %u\n", mesh->triangle_count);
  fprintf(file, "\n");

  // Write vertices
  for (uint32_t i = 0; i < mesh->vertex_count; i++)
  {
    const MarchingCubesVertex *vertex = &mesh->vertices[i];
    fprintf(file, "v %.6f %.6f %.6f\n", vertex->x, vertex->y, vertex->z);
  }

  // Write vertex normals
  for (uint32_t i = 0; i < mesh->vertex_count; i++)
  {
    const MarchingCubesVertex *vertex = &mesh->vertices[i];
    fprintf(file, "vn %.6f %.6f %.6f\n", vertex->nx, vertex->ny, vertex->nz);
  }

  // Write faces (triangles)
  for (uint32_t i = 0; i < mesh->triangle_count; i++)
  {
    const MarchingCubesTriangle *triangle = &mesh->triangles[i];
    // OBJ uses 1-based indexing
    fprintf(file, "f %u//%u %u//%u %u//%u\n",
            triangle->vertices[0] + 1, triangle->vertices[0] + 1,
            triangle->vertices[1] + 1, triangle->vertices[1] + 1,
            triangle->vertices[2] + 1, triangle->vertices[2] + 1);
  }

  fclose(file);
  return true;
}

// Export mesh to STL format (binary)
bool marching_cubes_export_stl(const MarchingCubesMesh *mesh, const char *filename)
{
  if (!mesh || !filename)
    return false;

  FILE *file = fopen(filename, "wb");
  if (!file)
    return false;

  // STL header (80 bytes)
  char header[80];
  snprintf(header, sizeof(header), "Marching Cubes Generated Mesh - %u triangles", mesh->triangle_count);
  fwrite(header, 1, 80, file);

  // Write triangle count (4 bytes, little endian)
  uint32_t triangle_count = mesh->triangle_count;
  fwrite(&triangle_count, 4, 1, file);

  // Write each triangle
  for (uint32_t i = 0; i < mesh->triangle_count; i++)
  {
    const MarchingCubesTriangle *triangle = &mesh->triangles[i];

    // Calculate triangle normal
    const MarchingCubesVertex *v1 = &mesh->vertices[triangle->vertices[0]];
    const MarchingCubesVertex *v2 = &mesh->vertices[triangle->vertices[1]];
    const MarchingCubesVertex *v3 = &mesh->vertices[triangle->vertices[2]];

    float nx, ny, nz;
    calculate_triangle_normal(v1, v2, v3, &nx, &ny, &nz);

    // Write normal (12 bytes: 3 floats)
    fwrite(&nx, 4, 1, file);
    fwrite(&ny, 4, 1, file);
    fwrite(&nz, 4, 1, file);

    // Write vertex 1 (12 bytes: 3 floats)
    fwrite(&v1->x, 4, 1, file);
    fwrite(&v1->y, 4, 1, file);
    fwrite(&v1->z, 4, 1, file);

    // Write vertex 2 (12 bytes: 3 floats)
    fwrite(&v2->x, 4, 1, file);
    fwrite(&v2->y, 4, 1, file);
    fwrite(&v2->z, 4, 1, file);

    // Write vertex 3 (12 bytes: 3 floats)
    fwrite(&v3->x, 4, 1, file);
    fwrite(&v3->y, 4, 1, file);
    fwrite(&v3->z, 4, 1, file);

    // Write attribute byte count (2 bytes, should be 0)
    uint16_t attribute = 0;
    fwrite(&attribute, 2, 1, file);
  }

  fclose(file);
  return true;
}

