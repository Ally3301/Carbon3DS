#include "world_stream.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Sections are selected by their recovered local origin.  The small resident
 * set is deliberate: the city must never become a second, permanent scene. */
int world_stream_init(WorldStream *world, const char *manifest_path)
{
    if (!world || !manifest_path) return -1;
    memset(world, 0, sizeof(*world));
    world->draw_distance = 420.0f; /* collision + prefetch cache */
    world->render_distance = 230.0f; /* GPU visibility circle */
    world->collision_pin = WORLD_STREAM_MAX_SECTIONS;
    FILE *f = fopen(manifest_path, "r");
    if (!f) return -1;
    char line[160];
    while (world->count < WORLD_STREAM_MAX_SECTIONS && fgets(line, sizeof(line), f)) {
        WorldStreamSection *s = &world->sections[world->count];
        if (sscanf(line, "%15s %79s %f %f %f %f %f %f %f", s->id, s->path,
                   &s->x, &s->y, &s->z, &s->min_x, &s->min_z, &s->max_x, &s->max_z) == 9
            && s->min_x <= s->max_x && s->min_z <= s->max_z)
            world->count++;
    }
    fclose(f);
    /* NRN1 is generated offline from the original Zeebo container.  A missing
       cache never prevents the city from loading; it only disables route-aware
       prefetch and the live road map. */
    (void)road_network_load(&world->roads, "romfs:/world/palmont/roadnetwork.nrn");
    return world->count ? 0 : -1;
}

static int section_contains(const WorldStreamSection *s, float x, float z)
{ return x>=s->min_x && x<=s->max_x && z>=s->min_z && z<=s->max_z; }

static void section_build_walls(WorldStreamSection *cell)
{
    free(cell->walls);
    cell->walls = NULL;
    cell->wall_count = 0;
    const SceneSection *s = cell->asset.section;
    if (!s) return;
    cell->walls = malloc(sizeof(*cell->walls) * WORLD_STREAM_MAX_WALLS);
    if (!cell->walls) return;
    unsigned candidates = 0;
    for (uint32_t i = 0; i + 2 < s->index_count; i += 3) {
        const SceneVertex *v[3] = {&s->vertices[s->indices[i]],
                                   &s->vertices[s->indices[i+1]],
                                   &s->vertices[s->indices[i+2]]};
        float ax=v[0]->pos[0], az=v[0]->pos[2], ay=v[0]->pos[1];
        float bx=v[1]->pos[0], bz=v[1]->pos[2], by=v[1]->pos[1];
        float cx=v[2]->pos[0], cz=v[2]->pos[2], cy=v[2]->pos[1];
        float nx=(by-ay)*(cz-az)-(bz-az)*(cy-ay);
        float ny=(bz-az)*(cx-ax)-(bx-ax)*(cz-az);
        float nz=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
        float normal_len=sqrtf(nx*nx+ny*ny+nz*nz);
        if (normal_len < 0.001f || fabsf(ny)/normal_len > 0.45f) continue;
        float ex[3]={ax,bx,cx}, ez[3]={az,bz,cz};
        int edge=0; float longest=0.0f;
        for (int e=0;e<3;++e) { int n=(e+1)%3; float dx=ex[n]-ex[e], dz=ez[n]-ez[e];
            float l=dx*dx+dz*dz; if (l>longest) { longest=l; edge=e; } }
        if (longest < 0.16f) continue;
        WorldWallSegment candidate = {ex[edge]+cell->x, ez[edge]+cell->z,
                                      ex[(edge+1)%3]+cell->x, ez[(edge+1)%3]+cell->z,
                                      fminf(ay,fminf(by,cy))+cell->y,
                                      fmaxf(ay,fmaxf(by,cy))+cell->y};
        /* Reservoir sampling keeps a representative collision proxy without
           retaining the visual mesh's full triangle count in CPU memory. */
        if (cell->wall_count < WORLD_STREAM_MAX_WALLS)
            cell->walls[cell->wall_count++] = candidate;
        else {
            unsigned slot = (candidates * 1103515245u + 12345u) % (candidates + 1u);
            if (slot < WORLD_STREAM_MAX_WALLS) cell->walls[slot] = candidate;
        }
        candidates++;
    }
}

static float section_priority(const WorldStreamSection *s, float x, float z,
                              float forward_x, float forward_z, float hint_x, float hint_z, float limit2)
{
    float dx = x < s->min_x ? s->min_x-x : x > s->max_x ? x-s->max_x : 0.0f;
    float dz = z < s->min_z ? s->min_z-z : z > s->max_z ? z-s->max_z : 0.0f;
    float score=dx*dx+dz*dz;
    float cx=(s->min_x+s->max_x)*0.5f-x, cz=(s->min_z+s->max_z)*0.5f-z;
    /* Behind cells remain available for a short turn-around, but never block
       the road ahead from being loaded. */
    if(cx*forward_x+cz*forward_z < -25.0f) score += limit2*1.5f;
    float hx = hint_x < s->min_x ? s->min_x-hint_x : hint_x > s->max_x ? hint_x-s->max_x : 0.0f;
    float hz = hint_z < s->min_z ? s->min_z-hint_z : hint_z > s->max_z ? hint_z-s->max_z : 0.0f;
    float hint_score = hx*hx + hz*hz;
    if (hint_score < limit2) score = fminf(score, hint_score * 0.55f);
    return score;
}

void world_stream_update(WorldStream *world, float x, float z, float forward_x, float forward_z)
{
    if (!world) return;
    /* Navigation must stay current while an asset load is being throttled;
       otherwise prefetch points and the map lag by up to one second. */
    road_network_update(&world->roads, x, z, forward_x, forward_z);
    if (world->load_cooldown) { world->load_cooldown--; return; }
    float hint_x = road_network_ready(&world->roads) ? world->roads.prefetch_x : x;
    float hint_z = road_network_ready(&world->roads) ? world->roads.prefetch_z : z;
    float limit2=world->draw_distance*world->draw_distance;
    int best=-1, worst=-1; float best_score=1e30f, worst_score=-1.0f;
    unsigned wanted=0;
    for(unsigned i=0;i<world->count;i++) {
        WorldStreamSection *cell=&world->sections[i];
        float score=section_priority(cell,x,z,forward_x,forward_z,hint_x,hint_z,limit2);
        if(score<=limit2) wanted++;
        if(cell->loaded) {
            /* Purge genuinely distant cells first. */
            float dx=x<cell->min_x?cell->min_x-x:x>cell->max_x?x-cell->max_x:0;
            float dz=z<cell->min_z?cell->min_z-z:z>cell->max_z?z-cell->max_z:0;
            if(i!=world->collision_pin && dx*dx+dz*dz>limit2*1.44f) {
                C3D_FrameSync(); scene_asset_free(&cell->asset); free(cell->walls); cell->walls=NULL; cell->wall_count=0; cell->loaded=0; world->loaded--;
                continue;
            }
            /* Never evict a collision cell directly under the car. */
            if(i!=world->collision_pin && !section_contains(cell,x,z) && score>worst_score) { worst_score=score; worst=(int)i; }
        } else if(score<best_score && score<=limit2) { best_score=score; best=(int)i; }
    }
    world->pending=wanted>world->loaded?wanted-world->loaded:0;
    if(best<0) return;
    /* Replace a worse resident cell before it leaves the radius. */
    if(world->loaded>=WORLD_STREAM_MAX_LOADED) {
        /* Hysteresis prevents swapping cells back and forth at a boundary. */
        if(worst<0 || best_score+limit2*0.12f>=worst_score) return;
        C3D_FrameSync(); scene_asset_free(&world->sections[worst].asset);
        free(world->sections[worst].walls); world->sections[worst].walls=NULL; world->sections[worst].wall_count=0;
        world->sections[worst].loaded=0; world->loaded--;
    }
#ifdef __3DS__
    /* Geometry uses linear memory; keep room for vehicle swaps, audio and UI. */
    if(linearSpaceFree()<2u*1024u*1024u) return;
#endif
    WorldStreamSection *cell=&world->sections[best]; char path[112];
    snprintf(path,sizeof(path),"romfs:/world/palmont/%s",cell->path);
    if(scene_asset_load(&cell->asset,path)==0) { section_build_walls(cell); cell->loaded=1; world->loaded++; world->load_cooldown=60; if(world->pending) world->pending--; }
}

void world_stream_free(WorldStream *world)
{
    if (!world) return;
    C3D_FrameSync();
    for (unsigned i = 0; i < world->count; ++i) {
        scene_asset_free(&world->sections[i].asset);
        free(world->sections[i].walls);
    }
    memset(world, 0, sizeof(*world));
}

int world_stream_ground_y(WorldStream *world, float x, float z, float ceiling, float *out_y)
{
    int found=0, best_cell=-1; float best=-1e30f;
    if (!world || !out_y) return 0;
    for (unsigned n=0;n<world->count;n++) {
        const WorldStreamSection *cell=&world->sections[n];
        if (!cell->loaded || x<cell->min_x || x>cell->max_x || z<cell->min_z || z>cell->max_z) continue;
        const SceneSection *s=cell->asset.section;
        for (uint32_t i=0;i<s->index_count;i+=3) {
            const SceneVertex *a=&s->vertices[s->indices[i]], *b=&s->vertices[s->indices[i+1]], *c=&s->vertices[s->indices[i+2]];
            float ax=a->pos[0]+cell->x, az=a->pos[2]+cell->z, ay=a->pos[1]+cell->y;
            float bx=b->pos[0]+cell->x, bz=b->pos[2]+cell->z, by=b->pos[1]+cell->y;
            float cx=c->pos[0]+cell->x, cz=c->pos[2]+cell->z, cy=c->pos[1]+cell->y;
            float d=(bz-cz)*(ax-cx)+(cx-bx)*(az-cz);
            if (fabsf(d)<1e-5f) continue;
            float u=((bz-cz)*(x-cx)+(cx-bx)*(z-cz))/d;
            float v=((cz-az)*(x-cx)+(ax-cx)*(z-cz))/d, w=1.0f-u-v;
            float y=u*ay+v*by+w*cy;
            if (u>=0 && v>=0 && w>=0 && y<=ceiling && y>best) { best=y;best_cell=(int)n;found=1; }
        }
    }
    if (found) { *out_y = best; world->collision_pin=(unsigned)best_cell; }
    return found;
}

int world_stream_wall_contact(const WorldStream *world, float x, float z, float y,
                              float radius, float *out_x, float *out_z, float *out_depth)
{
    if (!world) return 0;
    float best = radius * radius;
    float hit_x = 0.0f, hit_z = 0.0f;
    for (unsigned i=0; i<world->count; ++i) {
        const WorldStreamSection *cell=&world->sections[i];
        if (!cell->loaded) continue;
        for (unsigned w=0; w<cell->wall_count; ++w) {
            const WorldWallSegment *wall=&cell->walls[w];
            if (wall->min_y > y+0.70f || wall->max_y < y-0.45f) continue;
            float dx=wall->bx-wall->ax, dz=wall->bz-wall->az;
            float denom=dx*dx+dz*dz;
            if (denom < 0.0001f) continue;
            float t=((x-wall->ax)*dx+(z-wall->az)*dz)/denom;
            if(t<0)t=0; else if(t>1)t=1;
            float px=wall->ax+t*dx, pz=wall->az+t*dz;
            float nx=x-px, nz=z-pz, d2=nx*nx+nz*nz;
            if (d2 < best) { best=d2; hit_x=nx; hit_z=nz; }
        }
    }
    if (best >= radius*radius) return 0;
    float len=sqrtf(best);
    if (len < 0.001f) { hit_x=1.0f; hit_z=0.0f; }
    else { hit_x/=len; hit_z/=len; }
    if(out_x)*out_x=hit_x;
    if(out_z)*out_z=hit_z;
    if(out_depth)*out_depth=radius-len;
    return 1;
}
