#pragma once

#define VALIDATION 1

#include "framework_vulkan\framework_vulkan.h"

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
    f32 MaxSteerSpeed;
    f32 BoidRadiusSq;
    f32 AvoidRadiusSq;
    f32 TerrainAvoidRadius;
    f32 TerrainRadius;

    f32 AvoidTerrainWeight;
    f32 AvoidBirdWeight;
    f32 AlignFlockWeight;
    f32 MoveToFlockWeight;
    
    // NOTE: Boid Data
    u32 NumBoids;
    v3* BoidPositions;
    v3* BoidVelocities;    
};

global demo_state* DemoState;
