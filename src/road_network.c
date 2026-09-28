#include "road_network.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define ROAD_NETWORK_VERSION 2u

typedef struct {
    char magic[4];
    uint32_t version;
    uint32_t source_node_count;
    uint32_t source_edge_count;
    uint32_t node_count;
    uint32_t link_count;
    float min_x, min_z, max_x, max_z;
} RoadNetworkHeader;

_Static_assert(sizeof(RoadNetworkNode) == 36, "road network node disk layout");
_Static_assert(sizeof(RoadNetworkLink) == 4, "road network link disk layout");
_Static_assert(sizeof(RoadNetworkHeader) == 40, "road network header disk layout");

void road_network_reset(RoadNetwork *network)
{
    if (!network) return;
    memset(network, 0, sizeof(*network));
    network->nearest_node = -1;
}

int road_network_load(RoadNetwork *network, const char *path)
{
    RoadNetworkHeader header;
    FILE *file;
    if (!network || !path) return -1;
    road_network_reset(network);
    file = fopen(path, "rb");
    if (!file) return -1;
    if (fread(&header, sizeof(header), 1, file) != 1 ||
        memcmp(header.magic, "NRN1", 4) != 0 || header.version != ROAD_NETWORK_VERSION ||
        header.node_count == 0 || header.node_count > ROAD_NETWORK_MAX_NODES ||
        header.link_count > ROAD_NETWORK_MAX_LINKS ||
        !isfinite(header.min_x) || !isfinite(header.min_z) ||
        !isfinite(header.max_x) || !isfinite(header.max_z) ||
        header.min_x >= header.max_x || header.min_z >= header.max_z) {
        fclose(file);
        return -1;
    }
    if (fread(network->nodes, sizeof(RoadNetworkNode), header.node_count, file) != header.node_count ||
        fread(network->links, sizeof(RoadNetworkLink), header.link_count, file) != header.link_count) {
        fclose(file);
        road_network_reset(network);
        return -1;
    }
    fclose(file);
    for (uint32_t i = 0; i < header.node_count; ++i) {
        const RoadNetworkNode *node = &network->nodes[i];
        if (!isfinite(node->x) || !isfinite(node->z) || !isfinite(node->y) ||
            node->edge_count > ROAD_NETWORK_MAX_EDGES_PER_NODE) {
            road_network_reset(network);
            return -1;
        }
    }
    for (uint32_t i = 0; i < header.link_count; ++i) {
        if (network->links[i].a >= header.node_count || network->links[i].b >= header.node_count ||
            network->links[i].a == network->links[i].b) {
            road_network_reset(network);
            return -1;
        }
    }
    network->source_node_count = header.source_node_count;
    network->source_edge_count = header.source_edge_count;
    network->node_count = header.node_count;
    network->link_count = header.link_count;
    network->min_x = header.min_x; network->min_z = header.min_z;
    network->max_x = header.max_x; network->max_z = header.max_z;
    return 0;
}

int road_network_ready(const RoadNetwork *network)
{ return network && network->node_count > 0; }

int road_network_nearest(const RoadNetwork *network, float x, float z, float *out_x, float *out_z)
{
    int best = -1;
    float best_distance = 1e30f;
    if (!road_network_ready(network)) return -1;
    for (uint32_t i = 0; i < network->node_count; ++i) {
        float dx = network->nodes[i].x - x, dz = network->nodes[i].z - z;
        float distance = dx * dx + dz * dz;
        if (distance < best_distance) { best_distance = distance; best = (int)i; }
    }
    if (best < 0) return -1;
    if (out_x) *out_x = network->nodes[best].x;
    if (out_z) *out_z = network->nodes[best].z;
    return best;
}

int road_network_nearest_point(const RoadNetwork *network, float x, float z,
                               float *out_x, float *out_z)
{
    float best_distance=1e30f, best_x=0.0f, best_z=0.0f;
    int best=-1;
    if (!road_network_ready(network)) return -1;
    for (uint32_t i=0; i<network->link_count; ++i) {
        RoadNetworkLink link=network->links[i];
        const RoadNetworkNode *a=&network->nodes[link.a], *b=&network->nodes[link.b];
        float dx=b->x-a->x, dz=b->z-a->z;
        float length2=dx*dx+dz*dz;
        float t=length2>1e-6f ? ((x-a->x)*dx+(z-a->z)*dz)/length2 : 0.0f;
        if (t<0.0f) t=0.0f; else if (t>1.0f) t=1.0f;
        float px=a->x+t*dx, pz=a->z+t*dz;
        float ex=px-x, ez=pz-z, distance=ex*ex+ez*ez;
        if (distance<best_distance) {
            best_distance=distance; best_x=px; best_z=pz; best=(int)i;
        }
    }
    if (best<0) return -1;
    if (out_x) *out_x=best_x;
    if (out_z) *out_z=best_z;
    return best;
}

static int links_connected(const RoadNetwork *network, int a, int b)
{
    if (a < 0 || b < 0 || (uint32_t)a >= network->link_count || (uint32_t)b >= network->link_count)
        return 0;
    RoadNetworkLink x=network->links[a], y=network->links[b];
    return x.a==y.a || x.a==y.b || x.b==y.a || x.b==y.b;
}

int road_network_match_point(const RoadNetwork *network, float x, float z, float y,
                             float forward_x, float forward_z, int previous_link,
                             float *out_x, float *out_z,
                             float *out_forward_x, float *out_forward_z)
{
    int global=-1, connected=-1;
    float global_score=1e30f, connected_score=1e30f;
    float global_x=0, global_z=0, global_dx=0, global_dz=1;
    float connected_x=0, connected_z=0, connected_dx=0, connected_dz=1;
    if (!road_network_ready(network)) return -1;
    for (uint32_t i=0; i<network->link_count; ++i) {
        RoadNetworkLink link=network->links[i];
        const RoadNetworkNode *a=&network->nodes[link.a], *b=&network->nodes[link.b];
        float dx=b->x-a->x, dz=b->z-a->z, length2=dx*dx+dz*dz;
        /* Keep bridges and tunnels out of the current street layer. */
        float min_y=fminf(a->y,b->y)-14.0f, max_y=fmaxf(a->y,b->y)+14.0f;
        if (y < min_y || y > max_y || length2 <= 1e-6f) continue;
        float t=((x-a->x)*dx+(z-a->z)*dz)/length2;
        if (t<0.0f) t=0.0f; else if (t>1.0f) t=1.0f;
        float px=a->x+t*dx, pz=a->z+t*dz;
        float ex=px-x, ez=pz-z, distance2=ex*ex+ez*ez;
        float length=sqrtf(length2), nx=dx/length, nz=dz/length;
        float heading=fabsf(nx*forward_x+nz*forward_z);
        /* Prefer the carriageway that agrees with travel direction when two
           roads overlap, but never overpower genuine spatial proximity. */
        float score=distance2+(1.0f-heading)*36.0f;
        if (score<global_score) {
            global_score=score; global=(int)i; global_x=px; global_z=pz; global_dx=nx; global_dz=nz;
        }
        if (links_connected(network, previous_link, (int)i) && score<connected_score) {
            connected_score=score; connected=(int)i; connected_x=px; connected_z=pz; connected_dx=nx; connected_dz=nz;
        }
    }
    /* Stay on the current road family while it is still plausibly nearby;
       reset only after a respawn/teleport or a true departure. */
    int best=global;
    float px=global_x, pz=global_z, dx=global_dx, dz=global_dz;
    if (connected>=0 && connected_score < 120.0f*120.0f) {
        best=connected; px=connected_x; pz=connected_z; dx=connected_dx; dz=connected_dz;
    }
    if (best<0) return -1;
    if (dx*forward_x+dz*forward_z < 0.0f) { dx=-dx; dz=-dz; }
    if (out_x) *out_x=px;
    if (out_z) *out_z=pz;
    if (out_forward_x) *out_forward_x=dx;
    if (out_forward_z) *out_forward_z=dz;
    return best;
}

void road_network_update(RoadNetwork *network, float x, float z, float forward_x, float forward_z)
{
    int best = -1;
    float best_score = 1e30f;
    if (!road_network_ready(network)) return;
    if (network->refresh_counter++ % 10u && network->nearest_node >= 0) return;
    network->nearest_node = road_network_nearest(network, x, z, &network->nearest_x, &network->nearest_z);
    network->prefetch_x = network->nearest_x;
    network->prefetch_z = network->nearest_z;
    /* Pick a real navigation point ahead of the player. This remains useful
       before traffic is implemented and becomes its spawn/look-ahead anchor. */
    for (uint32_t i = 0; i < network->node_count; ++i) {
        float dx = network->nodes[i].x - x, dz = network->nodes[i].z - z;
        float forward = dx * forward_x + dz * forward_z;
        float distance = dx * dx + dz * dz;
        if (forward < 20.0f || distance > 300.0f * 300.0f) continue;
        float score = distance - forward * 35.0f;
        if (score < best_score) { best_score = score; best = (int)i; }
    }
    if (best >= 0) {
        network->prefetch_x = network->nodes[best].x;
        network->prefetch_z = network->nodes[best].z;
    }
}

int road_network_local_links(const RoadNetwork *network, float x, float z, float radius,
                             RoadNetworkLink *out, int max_links)
{
    int count = 0;
    float radius2 = radius * radius;
    if (!road_network_ready(network) || !out || max_links < 1) return 0;
    for (uint32_t i = 0; i < network->link_count && count < max_links; ++i) {
        RoadNetworkLink link = network->links[i];
        const RoadNetworkNode *a = &network->nodes[link.a];
        const RoadNetworkNode *b = &network->nodes[link.b];
        float ax = a->x - x, az = a->z - z;
        float bx = b->x - x, bz = b->z - z;
        if (ax * ax + az * az > radius2 && bx * bx + bz * bz > radius2) continue;
        out[count++] = link;
    }
    return count;
}
