#pragma once

#include <stdint.h>

#define ROAD_NETWORK_MAX_NODES 2168
#define ROAD_NETWORK_MAX_LINKS 8192
#define ROAD_NETWORK_MAX_EDGES_PER_NODE 7

typedef struct {
    float x, z, y;
    uint16_t source_index;
    uint16_t edge_count;
    uint16_t edges[ROAD_NETWORK_MAX_EDGES_PER_NODE];
    uint16_t reserved[3];
} RoadNetworkNode;

typedef struct { uint16_t a, b; } RoadNetworkLink;

typedef struct {
    uint32_t source_node_count;
    uint32_t source_edge_count;
    uint32_t node_count;
    uint32_t link_count;
    float min_x, min_z, max_x, max_z;
    RoadNetworkNode nodes[ROAD_NETWORK_MAX_NODES];
    RoadNetworkLink links[ROAD_NETWORK_MAX_LINKS];
    int nearest_node;
    float nearest_x, nearest_z;
    float prefetch_x, prefetch_z;
    unsigned refresh_counter;
} RoadNetwork;

int road_network_load(RoadNetwork *network, const char *path);
void road_network_reset(RoadNetwork *network);
void road_network_update(RoadNetwork *network, float x, float z, float forward_x, float forward_z);
int road_network_ready(const RoadNetwork *network);
int road_network_nearest(const RoadNetwork *network, float x, float z, float *out_x, float *out_z);
/* Projects a world point onto the closest navigable road segment. */
int road_network_nearest_point(const RoadNetwork *network, float x, float z,
                               float *out_x, float *out_z);
/* Stable nearest-road match. previous_link keeps a moving marker on its
   current segment or a directly connected segment at an intersection. */
int road_network_match_point(const RoadNetwork *network, float x, float z, float y,
                             float forward_x, float forward_z, int previous_link,
                             float *out_x, float *out_z,
                             float *out_forward_x, float *out_forward_z);
int road_network_local_links(const RoadNetwork *network, float x, float z, float radius,
                             RoadNetworkLink *out, int max_links);
