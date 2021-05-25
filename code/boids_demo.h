#pragma once

#define VALIDATION 1

#include "framework_vulkan\framework_vulkan.h"

#define CPU_PROFILING
#define WIN32_PROFILING
#define X86_PROFILING
#include "profiling\profiling.h"

//
// NOTE: Sim handling
//

struct grid_range
{
    i32 StartX;
    i32 StartY;
    i32 EndX;
    i32 EndY;
};

struct grid_cell
{
    u32 NumIndices;
    block_arena IndexArena;
};

struct grid
{
    aabb2 WorldBounds;
    u32 NumCellsX;
    u32 NumCellsY;
    u32 MaxNumIndicesPerBlock;
    grid_cell* Cells;
};

struct bird
{
    // TODO: Handle multiple grid cells for one entity
    v2 Position;
    v2 Velocity;
};

//
// NOTE: Render Data
//

struct directional_light
{
    v3 Color;
    u32 Pad0;
    v3 Dir;
    u32 Pad1;
    v3 AmbientColor;
    u32 Pad2;
};

struct point_light
{
    v3 Color;
    u32 Pad0;
    v3 Pos;
    f32 MaxDistance;
};

struct scene_globals
{
    v3 CameraPos;
    u32 NumPointLights;
};

struct instance_entry
{
    u32 MeshId;
    m4 WVTransform;
    m4 WVPTransform;
};

struct gpu_instance_entry
{
    m4 WVTransform;
    m4 WVPTransform;
};

struct render_mesh
{
    vk_image Color;
    vk_image Normal;
    VkDescriptorSet MaterialDescriptor;
    
    VkBuffer VertexBuffer;
    VkBuffer IndexBuffer;
    u32 NumIndices;
};

struct render_scene
{
    // NOTE: General Render Data
    camera Camera;
    VkDescriptorSetLayout MaterialDescLayout;
    VkDescriptorSetLayout SceneDescLayout;
    VkBuffer SceneBuffer;
    VkDescriptorSet SceneDescriptor;

    // NOTE: Scene Lights
    u32 MaxNumPointLights;
    u32 NumPointLights;
    point_light* PointLights;
    VkBuffer PointLightBuffer;
    VkBuffer PointLightTransforms;
    
    directional_light DirectionalLight;
    VkBuffer DirectionalLightBuffer;

    // NOTE: Scene Meshes
    u32 MaxNumRenderMeshes;
    u32 NumRenderMeshes;
    render_mesh* RenderMeshes;
    
    // NOTE: Opaque Instances
    u32 MaxNumOpaqueInstances;
    u32 NumOpaqueInstances;
    instance_entry* OpaqueInstances;
    VkBuffer OpaqueInstanceBuffer;
};

struct demo_state
{
    platform_block_arena PlatformBlockArena;
    linear_arena Arena;
    linear_arena TempArena;

    // NOTE: Samplers
    VkSampler PointSampler;
    VkSampler LinearSampler;
    VkSampler AnisoSampler;

    // NOTE: Rendering Data
    vk_linear_arena RenderTargetArena;
    render_target_entry SwapChainEntry;
    VkImage DepthImage;
    render_target_entry DepthEntry;
    render_target RenderTarget;
    vk_pipeline* RenderPipeline;

    render_scene Scene;

    ui_state UiState;
    
    // NOTE: Saved model ids
    u32 Quad;
    u32 Cube;
    u32 Sphere;

    // NOTE: Boid Globals
    f32 MinSpeed;
    f32 MaxSpeed;
    f32 BirdRadiusSq;
    f32 AvoidRadiusSq;
    f32 TerrainAvoidRadius;
    f32 TerrainRadius;

    f32 AvoidTerrainWeight;
    f32 AvoidBirdWeight;
    f32 AlignFlockWeight;
    f32 MoveToFlockWeight;
    
    // NOTE: Bird Data
    v3 BirdRadius;
    u32 NumBirds;
    bird* CurrBirds;
    bird* PrevBirds;

    grid Grid;
};

global demo_state* DemoState;
