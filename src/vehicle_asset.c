#include "vehicle_asset.h"
#include "resources.h"
#include "vehicle_material_policy.h"
#include "gpu_color.h"

#include <3ds.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <limits.h>
#include <math.h>

static int slot_from_name(const char *name)
{
    static const char *const names[VEHICLE_SLOT_COUNT] = {
        "body", "base", "hood", "spoiler", "crewtag_sides", "crewtag_hood"
    };
    for (int i = 0; i < VEHICLE_SLOT_COUNT; ++i)
        if (!strcmp(name, names[i]))
            return i;
    return -1;
}

const char *vehicle_asset_slot_name(VehiclePartSlot slot)
{
    static const char *const names[VEHICLE_SLOT_COUNT] = {
        "Body kit", "Base", "Hood", "Spoiler", "Crew tag sides", "Crew tag hood"
    };
    if (slot >= VEHICLE_SLOT_COUNT)
        return "?";
    return names[slot];
}

void vehicle_asset_init(VehicleAsset *asset)
{
    if (asset) {
        memset(asset, 0, sizeof(*asset));
        /* PAINT (material 1500) is a flat modulation source in the Zeebo
           assets. The original game supplies the selected paint dynamically. */
        asset->paint_details_material = 0xFFFF;
        asset->paint_rgba = gpu_rgba8(0xB7, 0x43, 0x32, 0xFF);
    }
}

static void free_loaded_parts(VehicleAsset *asset)
{
    for (int i = 0; i < VEHICLE_SLOT_COUNT; ++i) {
        n3p_free(asset->slots[i].loaded);
        asset->slots[i].loaded = NULL;
    }
}

void vehicle_asset_free(VehicleAsset *asset)
{
    if (!asset)
        return;

    free_loaded_parts(asset);
    for (int i = 0; i < asset->texture_count; ++i) {
        if (asset->textures[i].loaded)
            C3D_TexDelete(&asset->textures[i].texture);
        asset->textures[i].loaded = 0;
    }
    if (asset->wheel_texture_loaded)
        C3D_TexDelete(&asset->wheel_texture);
    memset(asset, 0, sizeof(*asset));
}

static void update_bounds(VehicleAsset *asset)
{
    for (int k = 0; k < 3; ++k) {
        asset->bounds_min[k] = FLT_MAX;
        asset->bounds_max[k] = -FLT_MAX;
    }
    const VehiclePartSlot shape_slots[] = {
        VEHICLE_SLOT_BODY, VEHICLE_SLOT_BASE, VEHICLE_SLOT_HOOD
    };
    for (unsigned s = 0; s < sizeof(shape_slots)/sizeof(shape_slots[0]); ++s) {
        const N3PPart *part = asset->slots[shape_slots[s]].loaded;
        if (!part)
            continue;
        for (uint32_t i = 0; i < part->draw_vertex_count; ++i)
            for (int k = 0; k < 3; ++k) {
                float v = part->draw_verts[i].pos[k];
                if (v < asset->bounds_min[k]) asset->bounds_min[k] = v;
                if (v > asset->bounds_max[k]) asset->bounds_max[k] = v;
            }
    }
    if (asset->bounds_min[0] == FLT_MAX) {
        asset->bounds_min[0] = -2.0f; asset->bounds_max[0] = 2.0f;
        asset->bounds_min[1] = 0.0f;  asset->bounds_max[1] = 1.2f;
        asset->bounds_min[2] = -1.0f; asset->bounds_max[2] = 1.0f;
    }
}

static const char *wheel_style_name(const VehicleAsset *asset, int style,
                                    char *scratch, size_t scratch_size)
{
    static const char *const custom[] = {
        NULL, "BBS_STYLE02_CHROME", "ENKEI_STYLE01_BLACK"
    };
    if (style > 0 && style < VEHICLE_WHEEL_STYLE_COUNT)
        return custom[style];
    if (snprintf(scratch, scratch_size, "%s_TIRE_STYLE00", asset->id)
        >= (int)scratch_size)
        return NULL;
    return scratch;
}

static int load_wheel_style(VehicleAsset *asset, int style)
{
    char stock[64], path[144];
    const char *name = wheel_style_name(asset, style, stock, sizeof(stock));
    if (!name || snprintf(path, sizeof(path), "romfs:/wheels/tex/%s.t3x", name)
        >= (int)sizeof(path))
        return -1;
    C3D_Tex next;
    TextureUV next_uv;
    memset(&next, 0, sizeof(next));
    texture_uv_identity(&next_uv);
    if (resource_load_tex(path, &next, &next_uv) != 0) {
        if (style != 0)
            return -1;
        name = "TRAFFICCAR_TIRE_STYLE00";
        snprintf(path, sizeof(path), "romfs:/wheels/tex/%s.t3x", name);
        if (resource_load_tex(path, &next, &next_uv) != 0)
            return -1;
    }
    C3D_TexSetWrap(&next, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    /* Rim atlases contain deliberate hard-edged spokes and alpha cut-outs.
       Linear filtering mixes their bright metal texels into transparent atlas
       neighbours, producing the white pixel halo visible on PSP wheels. */
    C3D_TexSetFilter(&next, GPU_NEAREST, GPU_NEAREST);
    C3D_FrameSync();
    if (asset->wheel_texture_loaded)
        C3D_TexDelete(&asset->wheel_texture);
    asset->wheel_texture = next;
    asset->wheel_uv = next_uv;
    asset->wheel_texture_loaded = 1;
    asset->wheel_style = style;
    snprintf(asset->wheel_texture_name, sizeof(asset->wheel_texture_name), "%s", name);
    return 0;
}

static int add_option(VehicleAsset *asset, int slot, int index, const char *relative)
{
    if (slot < 0 || slot >= VEHICLE_SLOT_COUNT || !relative)
        return -1;
    VehicleSlotState *state = &asset->slots[slot];
    if (state->option_count >= VEHICLE_MAX_OPTIONS_PER_SLOT)
        return -1;
    VehiclePartOption *option = &state->options[state->option_count++];
    option->index = index;
    if (snprintf(option->path, sizeof(option->path), "%s/%s",
                 asset->base_path, relative) >= (int)sizeof(option->path))
        return -1;
    return 0;
}

static int find_option_value(const VehicleSlotState *slot, int value)
{
    for (int i = 0; i < slot->option_count; ++i)
        if (slot->options[i].index == value)
            return i;
    return -1;
}

static int preferred_option(const VehicleSlotState *slot)
{
    if (!slot->option_count)
        return -1;
    int zero = find_option_value(slot, 0);
    return zero >= 0 ? zero : 0;
}

static int asset_has_texture(const VehicleAsset *asset, uint16_t material)
{
    if (!asset)
        return 0;
    for (int i = 0; i < asset->texture_count; ++i)
        if (asset->textures[i].loaded && asset->textures[i].material == material)
            return 1;
    return 0;
}

static float uv_clamp(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static float uv_repeat(float x)
{
    float y = x - floorf(x);
    /* Preserve exact 1.0 at a source edge instead of wrapping it to zero.
       This avoids creating a seam on geometry authored exactly on the border. */
    if (fabsf(x - 1.0f) < 0.00001f)
        return 1.0f;
    return y;
}

static void prepare_part_draw_uvs(const VehicleAsset *asset, N3PPart *part)
{
    if (!asset || !part || !part->draw_verts)
        return;

    for (uint32_t g = 0; g < part->group_count; ++g) {
        const N3PGroup *group = &part->groups[g];
        int has_texture = asset_has_texture(asset, group->material);
        VehicleMaterialClass cls =
            vehicle_material_classify(group->renderer, group->material,
                                      has_texture);

        VehicleUVMode uv_mode = vehicle_material_uv_mode(cls);
        if (uv_mode == VEHICLE_UV_NONE)
            continue;

        uint32_t end = group->index_start + group->index_count;
        if (end > part->draw_vertex_count)
            end = part->draw_vertex_count;
        for (uint32_t i = group->index_start; i < end; ++i) {
            float u = part->draw_verts[i].uv[0];
            float v = part->draw_verts[i].uv[1];
            part->draw_verts[i].uv[0] =
                uv_mode == VEHICLE_UV_REPEAT ? uv_repeat(u) : uv_clamp(u);
            part->draw_verts[i].uv[1] =
                uv_mode == VEHICLE_UV_REPEAT ? uv_repeat(v) : uv_clamp(v);
        }
    }

#ifdef __3DS__
    GSPGPU_FlushDataCache(part->draw_verts,
                          (size_t)part->draw_vertex_count * sizeof(*part->draw_verts));
#endif
}

static N3PPart *load_option(const VehicleAsset *asset,
                            const VehicleSlotState *slot, int selected)
{
    if (!slot || selected < 0 || selected >= slot->option_count)
        return NULL;
    N3PPart *part = n3p_load(slot->options[selected].path);
    if (part) {
        prepare_part_draw_uvs(asset, part);
        /* Draws use the expanded stream only.  Release source vertices and
           indices now so a garage swap does not retain three linear buffers
           per part. */
        linearFree(part->verts); part->verts = NULL;
        linearFree(part->indices); part->indices = NULL;
    }
    return part;
}

static int set_slot_selection(VehicleAsset *asset, VehiclePartSlot slot, int selected)
{
    if (!asset || slot >= VEHICLE_SLOT_COUNT)
        return -1;
    VehicleSlotState *state = &asset->slots[slot];
    if (selected < 0 || selected >= state->option_count)
        return -1;

    N3PPart *next = load_option(asset, state, selected);
    if (!next)
        return -1;

    C3D_FrameSync();
    n3p_free(state->loaded);
    state->loaded = next;
    state->selected = selected;
    return 0;
}

static int load_defaults(VehicleAsset *asset)
{
    /* Body/base must remain on the same upgrade number whenever possible. */
    VehicleSlotState *body = &asset->slots[VEHICLE_SLOT_BODY];
    VehicleSlotState *base = &asset->slots[VEHICLE_SLOT_BASE];

    int body_sel = preferred_option(body);
    int base_sel = preferred_option(base);
    if (body_sel < 0)
        return -1;

    int target = body->options[body_sel].index;
    int paired = find_option_value(base, target);
    if (paired >= 0)
        base_sel = paired;

    body->selected = body_sel;
    body->loaded = load_option(asset, body, body_sel);
    if (!body->loaded)
        return -1;

    if (base_sel >= 0) {
        base->selected = base_sel;
        base->loaded = load_option(asset, base, base_sel);
        if (!base->loaded)
            return -1;
    }

    for (int slot = VEHICLE_SLOT_HOOD; slot <= VEHICLE_SLOT_SPOILER; ++slot) {
        VehicleSlotState *state = &asset->slots[slot];
        int selected = preferred_option(state);
        if (selected < 0)
            continue;
        state->selected = selected;
        state->loaded = load_option(asset, state, selected);
        if (!state->loaded)
            return -1;
    }

    /* Crew-tag geometry is intentionally not enabled by default. It is kept in
       the manifest for the later vinyl/crew customization system. */
    return 0;
}

static int texture_index(const VehicleAsset *asset, uint16_t material)
{
    for (int i = 0; i < asset->texture_count; ++i)
        if (asset->textures[i].material == material)
            return i;
    return -1;
}

static int add_texture(VehicleAsset *asset, uint16_t material, const char *path)
{
    if (!asset || !path)
        return -1;
    if (texture_index(asset, material) >= 0)
        return 0;
    if (asset->texture_count >= VEHICLE_MAX_TEXTURES)
        return -1;

    VehicleTexture *tex = &asset->textures[asset->texture_count];
    memset(tex, 0, sizeof(*tex));
    tex->material = material;
    texture_uv_identity(&tex->uv);
    if (resource_load_tex(path, &tex->texture, &tex->uv) != 0)
        return -1;

    /* Vehicle atlases must never inherit the sampler's default repeat mode.
       The Zeebo meshes put many lamp/badge UVs exactly on texture borders;
       linear filtering + repeat samples the opposite edge and shows up on
       hardware as one-pixel "static" around headlights, taillights and
       customization seams.  Carbon is the only authored tiling material. */
    if (material == 1000)
        C3D_TexSetWrap(&tex->texture, GPU_REPEAT, GPU_REPEAT);
    else
        C3D_TexSetWrap(&tex->texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    tex->loaded = 1;
    asset->texture_count++;
    return 0;
}

static void load_global_materials(VehicleAsset *asset)
{
    /* These material ids come from race_car_common.msh and
       race_car_modable.msh.  1500/PAINT is deliberately absent: it is a
       dynamic tint class, not a sampled body texture at runtime. */
    static const uint16_t materials[] = {
        1000, /* CARBONFIBRE */
        1001, /* CarBottom */
        1002, /* DRIVER */
        1003, /* MESH */
        1004, /* Carbon_red_legend */
        1501, /* WINDOW */
        1995, /* TIRE_BACK: PSP wheel closing surface */
        1996, /* CALIPER: PSP wheel closing surface */
    };

    char path[96];
    for (unsigned i = 0; i < sizeof(materials) / sizeof(materials[0]); ++i) {
        uint16_t material = materials[i];
        if (texture_index(asset, material) >= 0)
            continue;
        if (snprintf(path, sizeof(path),
                     "romfs:/vehicle_global/tex/%04u.t3x", material)
            >= (int)sizeof(path))
            continue;
        /* Missing global textures should not make a vehicle unloadable. The
           renderer keeps conservative fallbacks for partial RomFS builds. */
        (void)add_texture(asset, material, path);
    }
}

int vehicle_asset_load(VehicleAsset *asset, const char *vehicle_id)
{
    if (!asset || !vehicle_id)
        return -1;

    VehicleAsset temp;
    vehicle_asset_init(&temp);
    if (strlen(vehicle_id) == 0 || strlen(vehicle_id) >= sizeof(temp.id))
        return -1;
    for (const char *p = vehicle_id; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '_')
            return -1;

    strcpy(temp.id, vehicle_id);
    if (snprintf(temp.base_path, sizeof(temp.base_path), "romfs:/vehicles/%s",
                 vehicle_id) >= (int)sizeof(temp.base_path))
        return -1;

    char config_path[128];
    if (snprintf(config_path, sizeof(config_path), "%s/vehicle.cfg",
                 temp.base_path) >= (int)sizeof(config_path))
        return -1;

    FILE *fp = fopen(config_path, "r");
    if (!fp)
        return -1;

    char line[256];
    int saw_header = 0;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#')
            continue;

        int version = 0;
        if (sscanf(line, "N3VCFG %d", &version) == 1) {
            if (version != 1) {
                fclose(fp);
                vehicle_asset_free(&temp);
                return -1;
            }
            saw_header = 1;
            continue;
        }

        char id[32];
        if (sscanf(line, "id %31s", id) == 1)
            continue;

        if (!strcmp(line, "psp_texture_alpha mask")) {
            temp.psp_texture_alpha_is_mask = 1;
            continue;
        }

        unsigned alpha_material = 0;
        if (sscanf(line, "texture_alpha coverage %u", &alpha_material) == 1) {
            if (alpha_material <= 0xFFFE && temp.psp_alpha_coverage_count < VEHICLE_MAX_TEXTURES)
                temp.psp_alpha_coverage[temp.psp_alpha_coverage_count++] = (uint16_t)alpha_material;
            continue;
        }

        unsigned material = 0;
        char relative[VEHICLE_PATH_MAX];

        float wf_front, wf_rear, wf_track, wf_radius, wf_center;
        if (sscanf(line, "wheel_fit %f %f %f %f %f",
                   &wf_front, &wf_rear, &wf_track, &wf_radius, &wf_center) == 5) {
            if (wf_radius > 0.20f && wf_radius < 0.70f && wf_track > 0.20f) {
                temp.wheel_fit_valid = 1;
                temp.wheel_front_x = wf_front;
                temp.wheel_rear_x = wf_rear;
                temp.wheel_half_track = wf_track;
                temp.wheel_radius = wf_radius;
                temp.wheel_center_y = wf_center;
            }
            continue;
        }

        float light_values[10];
        if (sscanf(line, "light_fit %f %f %f %f %f %f %f %f %f %f",
                   &light_values[0], &light_values[1], &light_values[2], &light_values[3], &light_values[4],
                   &light_values[5], &light_values[6], &light_values[7], &light_values[8], &light_values[9]) == 10) {
            temp.light_fit_valid = 1;
            temp.light_front_x=light_values[0]; temp.light_front_y=light_values[1]; temp.light_front_z=light_values[2];
            temp.light_front_h=light_values[3]; temp.light_front_w=light_values[4];
            temp.light_rear_x=light_values[5]; temp.light_rear_y=light_values[6]; temp.light_rear_z=light_values[7];
            temp.light_rear_h=light_values[8]; temp.light_rear_w=light_values[9];
            continue;
        }

        float front_radius, rear_radius, front_y, rear_y;
        if (sscanf(line, "wheel_fit_axles %f %f %f %f",
                   &front_radius, &rear_radius, &front_y, &rear_y) == 4) {
            if (front_radius > 0.18f && rear_radius > 0.18f) {
                temp.wheel_axles_valid = 1;
                temp.wheel_front_radius = front_radius;
                temp.wheel_rear_radius = rear_radius;
                temp.wheel_front_y = front_y;
                temp.wheel_rear_y = rear_y;
            }
            continue;
        }

        /* PAINT is a two-input material in the original renderer: the
           selected paint colour plus the vehicle-specific *_DETAILS texture.
           build_vehicle_assets.py records the original numeric DETAILS id. */
        if (sscanf(line, "paint_details %u", &material) == 1) {
            if (material <= 0xFFFE)
                temp.paint_details_material = (uint16_t)material;
            continue;
        }

        if (sscanf(line, "texture %u %95s", &material, relative) == 2) {
            if (material > 0xFFFE)
                continue;
            char path[160];
            if (snprintf(path, sizeof(path), "%s/%s", temp.base_path, relative)
                >= (int)sizeof(path))
                continue;
            /* A broken optional texture does not invalidate the geometry. */
            (void)add_texture(&temp, (uint16_t)material, path);
            continue;
        }

        char slot_name[24], model_name[80];
        int index = 0;
        if (sscanf(line, "part %23s %d %95s %79s",
                   slot_name, &index, relative, model_name) >= 3) {
            int slot = slot_from_name(slot_name);
            if (slot >= 0 && add_option(&temp, slot, index, relative) != 0) {
                fclose(fp);
                vehicle_asset_free(&temp);
                return -1;
            }
        }
    }
    fclose(fp);

    load_global_materials(&temp);

    if (!saw_header || !temp.slots[VEHICLE_SLOT_BODY].option_count
        || load_defaults(&temp) != 0) {
        vehicle_asset_free(&temp);
        return -1;
    }
    update_bounds(&temp);
    if (load_wheel_style(&temp, 0) != 0) {
        vehicle_asset_free(&temp);
        return -1;
    }

    vehicle_asset_free(asset);
    *asset = temp;
    return 0;
}

int vehicle_asset_load_preview(VehicleAsset *asset, const char *vehicle_id)
{
    if (!asset || !vehicle_id || !vehicle_id[0] || strlen(vehicle_id) >= 32)
        return -1;
    VehicleAsset temp;
    vehicle_asset_init(&temp);
    strcpy(temp.id, vehicle_id);
    if (snprintf(temp.base_path,sizeof(temp.base_path),"romfs:/vehicles/%s",vehicle_id)
        >= (int)sizeof(temp.base_path)) return -1;
    char cfg[128];
    if (snprintf(cfg,sizeof(cfg),"%s/vehicle.cfg",temp.base_path) >= (int)sizeof(cfg)) return -1;
    FILE *fp=fopen(cfg,"r");
    if (!fp) return -1;
    char line[256], rel[VEHICLE_PATH_MAX]={0}, slot[24], label[80];
    int index=0, best=INT_MAX;
    while (fgets(line,sizeof(line),fp)) {
        if (sscanf(line,"part %23s %d %95s %79s",slot,&index,rel,label) >= 3
            && !strcmp(slot,"BODY") && index < best) {
            best=index;
        }
    }
    fclose(fp);
    if (best == INT_MAX) { vehicle_asset_free(&temp); return -1; }
    /* Read once more to recover the matching relative path. */
    fp=fopen(cfg,"r");
    if (!fp) { vehicle_asset_free(&temp); return -1; }
    rel[0]=0;
    while (fgets(line,sizeof(line),fp))
        if (sscanf(line,"part %23s %d %95s %79s",slot,&index,rel,label) >= 3
            && !strcmp(slot,"BODY") && index == best) break;
    fclose(fp);
    char path[160];
    if (!rel[0] || snprintf(path,sizeof(path),"%s/%s",temp.base_path,rel) >= (int)sizeof(path)) {
        vehicle_asset_free(&temp); return -1;
    }
    temp.slots[VEHICLE_SLOT_BODY].loaded=n3p_load(path);
    if (!temp.slots[VEHICLE_SLOT_BODY].loaded) { vehicle_asset_free(&temp); return -1; }
    prepare_part_draw_uvs(&temp,temp.slots[VEHICLE_SLOT_BODY].loaded);
    linearFree(temp.slots[VEHICLE_SLOT_BODY].loaded->verts);
    linearFree(temp.slots[VEHICLE_SLOT_BODY].loaded->indices);
    temp.slots[VEHICLE_SLOT_BODY].loaded->verts=NULL;
    temp.slots[VEHICLE_SLOT_BODY].loaded->indices=NULL;
    temp.paint_rgba=gpu_rgba8(0x6C,0x6C,0x72,0xFF);
    update_bounds(&temp);
    vehicle_asset_free(asset);
    *asset=temp;
    return 0;
}

const N3PPart *vehicle_asset_part(const VehicleAsset *asset, VehiclePartSlot slot)
{
    if (!asset || slot >= VEHICLE_SLOT_COUNT)
        return NULL;
    return asset->slots[slot].loaded;
}

const C3D_Tex *vehicle_asset_texture(const VehicleAsset *asset, uint16_t material)
{
    if (!asset)
        return NULL;
    for (int i = 0; i < asset->texture_count; ++i)
        if (asset->textures[i].loaded && asset->textures[i].material == material)
            return &asset->textures[i].texture;
    return NULL;
}

const TextureUV *vehicle_asset_texture_uv(const VehicleAsset *asset, uint16_t material)
{
    if (!asset)
        return NULL;
    for (int i = 0; i < asset->texture_count; ++i)
        if (asset->textures[i].loaded && asset->textures[i].material == material)
            return &asset->textures[i].uv;
    return NULL;
}

const C3D_Tex *vehicle_asset_paint_details_texture(const VehicleAsset *asset)
{
    if (!asset || asset->paint_details_material == 0xFFFF)
        return NULL;
    return vehicle_asset_texture(asset, asset->paint_details_material);
}

const TextureUV *vehicle_asset_paint_details_uv(const VehicleAsset *asset)
{
    if (!asset || asset->paint_details_material == 0xFFFF)
        return NULL;
    return vehicle_asset_texture_uv(asset, asset->paint_details_material);
}

uint16_t vehicle_asset_paint_details_material(const VehicleAsset *asset)
{
    return asset ? asset->paint_details_material : 0xFFFF;
}

void vehicle_asset_set_paint(VehicleAsset *asset, uint32_t rgba)
{
    if (asset)
        asset->paint_rgba = rgba;
}

uint32_t vehicle_asset_paint(const VehicleAsset *asset)
{
    return asset ? asset->paint_rgba : gpu_rgba8(0xFF,0xFF,0xFF,0xFF);
}

int vehicle_asset_cycle_bodykit(VehicleAsset *asset, int direction)
{
    if (!asset || !direction)
        return -1;

    VehicleSlotState *body = &asset->slots[VEHICLE_SLOT_BODY];
    if (body->option_count < 2)
        return 0;

    int body_sel = (body->selected + (direction > 0 ? 1 : -1) + body->option_count)
        % body->option_count;
    return vehicle_asset_select_value(asset, VEHICLE_SLOT_BODY,
                                      body->options[body_sel].index);
}

int vehicle_asset_select_value(VehicleAsset *asset, VehiclePartSlot slot, int value)
{
    if (!asset || slot >= VEHICLE_SLOT_COUNT)
        return -1;
    if (slot != VEHICLE_SLOT_BODY && slot != VEHICLE_SLOT_BASE) {
        int selected = find_option_value(&asset->slots[slot], value);
        return selected >= 0 ? set_slot_selection(asset, slot, selected) : -1;
    }

    VehicleSlotState *body = &asset->slots[VEHICLE_SLOT_BODY];
    int body_sel = find_option_value(body, value);
    if (body_sel < 0)
        return -1;
    int target = body->options[body_sel].index;

    VehicleSlotState *base = &asset->slots[VEHICLE_SLOT_BASE];
    int base_sel = find_option_value(base, target);

    N3PPart *next_body = load_option(asset, body, body_sel);
    if (!next_body)
        return -1;
    N3PPart *next_base = NULL;
    if (base_sel >= 0) {
        next_base = load_option(asset, base, base_sel);
        if (!next_base) {
            n3p_free(next_body);
            return -1;
        }
    }

    C3D_FrameSync();
    n3p_free(body->loaded);
    body->loaded = next_body;
    body->selected = body_sel;

    if (base_sel >= 0) {
        n3p_free(base->loaded);
        base->loaded = next_base;
        base->selected = base_sel;
    }
    update_bounds(asset);
    return 0;
}

int vehicle_asset_cycle_part(VehicleAsset *asset, VehiclePartSlot slot, int direction)
{
    if (!asset || !direction || slot >= VEHICLE_SLOT_COUNT)
        return -1;
    if (slot == VEHICLE_SLOT_BODY || slot == VEHICLE_SLOT_BASE)
        return vehicle_asset_cycle_bodykit(asset, direction);

    VehicleSlotState *state = &asset->slots[slot];
    if (state->option_count < 2)
        return 0;
    int selected = (state->selected + (direction > 0 ? 1 : -1) + state->option_count)
        % state->option_count;
    return set_slot_selection(asset, slot, selected);
}

int vehicle_asset_cycle_wheel(VehicleAsset *asset, int direction)
{
    if (!asset || !direction)
        return -1;
    int next = (asset->wheel_style + (direction > 0 ? 1 : -1)
                + VEHICLE_WHEEL_STYLE_COUNT) % VEHICLE_WHEEL_STYLE_COUNT;
    return load_wheel_style(asset, next);
}

int vehicle_asset_select_wheel(VehicleAsset *asset, int style)
{
    if (!asset || style < 0 || style >= VEHICLE_WHEEL_STYLE_COUNT)
        return -1;
    return load_wheel_style(asset, style);
}

int vehicle_asset_wheel_style(const VehicleAsset *asset)
{
    return asset ? asset->wheel_style : -1;
}

const char *vehicle_asset_wheel_name(const VehicleAsset *asset)
{
    return asset && asset->wheel_texture_loaded ? asset->wheel_texture_name : "unavailable";
}

const C3D_Tex *vehicle_asset_wheel_texture(const VehicleAsset *asset)
{
    return asset && asset->wheel_texture_loaded ? &asset->wheel_texture : NULL;
}

const TextureUV *vehicle_asset_wheel_uv(const VehicleAsset *asset)
{
    return asset && asset->wheel_texture_loaded ? &asset->wheel_uv : NULL;
}

void vehicle_asset_wheel_placement(const VehicleAsset *asset,
                                   float *front_x, float *rear_x,
                                   float *half_track, float *radius,
                                   float *center_y)
{
    if (asset && asset->wheel_fit_valid) {
        *front_x = asset->wheel_front_x;
        *rear_x = asset->wheel_rear_x;
        *half_track = asset->wheel_half_track;
        *radius = asset->wheel_radius;
        *center_y = asset->wheel_center_y;
    } else {
        /* Conservative fallback for old configs without wheel_fit. */
    float length = asset->bounds_max[0] - asset->bounds_min[0];
    float width = asset->bounds_max[2] - asset->bounds_min[2];
    float r = length * 0.082f;
    if (r < 0.31f) r = 0.31f;
    if (r > 0.46f) r = 0.46f;
    *front_x = asset->bounds_max[0] - length * 0.205f;
    *rear_x = asset->bounds_min[0] + length * 0.205f;
    *half_track = width * 0.49f;
    *radius = r;
    *center_y = asset->bounds_min[1] + r * 0.94f;
    }

    /* Per-car collision envelope guard. Wheel arches are not collision
       meshes, but their source body bounds prevent an oversized procedural
       tyre from crossing a low roofline or rocker panel. */
    if (asset && asset->bounds_max[1] > asset->bounds_min[1]) {
        float body_height = asset->bounds_max[1] - asset->bounds_min[1];
        float allowed_radius = body_height * 0.42f;
        if (*radius > allowed_radius) {
            *radius = allowed_radius;
            *center_y = asset->bounds_min[1] + *radius * 0.94f;
        }
    }
}

void vehicle_asset_wheel_axles(const VehicleAsset *asset,
                               float *front_radius, float *rear_radius,
                               float *front_y, float *rear_y)
{
    float front, rear, track, radius, center;
    vehicle_asset_wheel_placement(asset, &front, &rear, &track, &radius, &center);
    *front_radius = radius; *rear_radius = radius;
    *front_y = center; *rear_y = center;
    if (asset && asset->wheel_axles_valid) {
        *front_radius = asset->wheel_front_radius;
        *rear_radius = asset->wheel_rear_radius;
        *front_y = asset->wheel_front_y;
        *rear_y = asset->wheel_rear_y;
        float cap = (asset->bounds_max[1] - asset->bounds_min[1]) * 0.42f;
        if (cap > 0.1f) {
            if (*front_radius > cap) *front_radius = cap;
            if (*rear_radius > cap) *rear_radius = cap;
        }
    }
}

int vehicle_asset_option_count(const VehicleAsset *asset, VehiclePartSlot slot)
{
    if (!asset || slot >= VEHICLE_SLOT_COUNT)
        return 0;
    return asset->slots[slot].option_count;
}

int vehicle_asset_selected_value(const VehicleAsset *asset, VehiclePartSlot slot)
{
    if (!asset || slot >= VEHICLE_SLOT_COUNT)
        return -1;
    const VehicleSlotState *state = &asset->slots[slot];
    if (!state->option_count || state->selected < 0 || state->selected >= state->option_count)
        return -1;
    return state->options[state->selected].index;
}

int vehicle_asset_selected_ordinal(const VehicleAsset *asset, VehiclePartSlot slot)
{
    if (!asset || slot >= VEHICLE_SLOT_COUNT) return -1;
    const VehicleSlotState *state=&asset->slots[slot];
    return state->selected >= 0 && state->selected < state->option_count ? state->selected : -1;
}

const char *vehicle_asset_selected_path(const VehicleAsset *asset, VehiclePartSlot slot)
{
    if (!asset || slot >= VEHICLE_SLOT_COUNT)
        return NULL;
    const VehicleSlotState *state = &asset->slots[slot];
    if (!state->option_count || state->selected < 0 || state->selected >= state->option_count)
        return NULL;
    return state->options[state->selected].path;
}

void vehicle_asset_part_counts(const VehicleAsset *asset, VehiclePartSlot slot,
                               uint32_t *vertices, uint32_t *indices,
                               uint32_t *groups)
{
    if (vertices) *vertices = 0;
    if (indices) *indices = 0;
    if (groups) *groups = 0;
    if (!asset || slot >= VEHICLE_SLOT_COUNT)
        return;

    const N3PPart *part = asset->slots[slot].loaded;
    if (!part)
        return;

    if (vertices) *vertices = part->vertex_count;
    if (indices) *indices = part->index_count;
    if (groups) *groups = part->group_count;
}

int vehicle_asset_psp_texture_uses_alpha(const VehicleAsset *asset, uint16_t material)
{
    if (!asset || !asset->psp_texture_alpha_is_mask)
        return 0;
    for (int i = 0; i < asset->psp_alpha_coverage_count; ++i)
        if (asset->psp_alpha_coverage[i] == material)
            return 1;
    return 0;
}
