
#include "boids_demo.h"

/*

  NOTE:

  - http://www.cs.toronto.edu/~dt/siggraph97-course/cwr87/
  - https://github.com/BogdanCodreanu/ECS-Boids-Murmuration_Unity_2019.1
  - https://github.com/SebLague/Boids
  - https://eater.net/boids
  
 */

inline f32 RandFloat()
{
    f32 Result = f32(rand()) / f32(RAND_MAX);
    return Result;
}

//
// NOTE: Spatial Partition
//

inline grid GridCreate(linear_arena* Arena, platform_block_arena* BlockArena, aabb2 WorldBounds, u32 NumCellsX, u32 NumCellsY)
{
    grid Result = {};
    Result.WorldBounds = WorldBounds;
    Result.NumCellsX = NumCellsX;
    Result.NumCellsY = NumCellsY;
    Result.Cells = PushArray(Arena, grid_cell, NumCellsX * NumCellsY);
    
    for (u32 CellId = 0; CellId < NumCellsX * NumCellsY; ++CellId)
    {
        grid_cell* CurrCell = Result.Cells + CellId;
        *CurrCell = {};
        CurrCell->IndexArena = BlockArenaCreate(BlockArena);
    }
    
    Result.MaxNumIndicesPerBlock = u32(BlockArenaGetBlockSize(&Result.Cells[0].IndexArena) / sizeof(u32));

    return Result;
}

inline u32 GridAddEntity(grid* Grid, v2 Position, u32 EntityId)
{
    u32 Result = 0;

    v2 ReMappedPos = (Position - Grid->WorldBounds.Min) / AabbGetDim(Grid->WorldBounds);
    i32 GridCellX = FloorU32(ReMappedPos.x * u32(Grid->NumCellsX));
    i32 GridCellY = FloorU32(ReMappedPos.y * u32(Grid->NumCellsY));

    Assert(GridCellX >= 0 && GridCellX < i32(Grid->NumCellsX));
    Assert(GridCellY >= 0 && GridCellY < i32(Grid->NumCellsY));

    grid_cell* GridCell = Grid->Cells + GridCellY * Grid->NumCellsX + GridCellX;
    u32* StoredIndex = PushStruct(&GridCell->IndexArena, u32);
    *StoredIndex = EntityId;

    Result = GridCell->NumIndices++;
    
    return Result;
}

inline grid_range GridGetRange(grid* Grid, v2 Pos, f32 Radius)
{
    grid_range Result = {};

    v2 Min = Pos - V2(Radius);
    v2 Max = Pos + V2(Radius);

    v2 ReMappedMin = (Min - Grid->WorldBounds.Min) / AabbGetDim(Grid->WorldBounds);
    v2 ReMappedMax = (Max - Grid->WorldBounds.Min) / AabbGetDim(Grid->WorldBounds);
    
    Result.StartX = Clamp((u32)FloorI32(ReMappedMin.x * Grid->NumCellsX), 0u, Grid->NumCellsX);
    Result.StartY = Clamp((u32)FloorI32(ReMappedMin.y * Grid->NumCellsY), 0u, Grid->NumCellsY);
    Result.EndX = Clamp((u32)FloorI32(ReMappedMax.x * Grid->NumCellsX), 0u, Grid->NumCellsX);
    Result.EndY = Clamp((u32)FloorI32(ReMappedMax.y * Grid->NumCellsY), 0u, Grid->NumCellsY);

    return Result;
}

inline void GridClear(grid* Grid)
{
    for (u32 CellId = 0; CellId < Grid->NumCellsX * Grid->NumCellsY; ++CellId)
    {
        grid_cell* Cell = Grid->Cells + CellId;
        Cell->NumIndices = 0;
        ArenaClear(&Cell->IndexArena);
    }
}

//
// NOTE: Asset Storage System
//

inline u32 SceneMeshAdd(render_scene* Scene, vk_image Color, vk_image Normal, VkBuffer VertexBuffer, VkBuffer IndexBuffer, u32 NumIndices)
{
    Assert(Scene->NumRenderMeshes < Scene->MaxNumRenderMeshes);
    
    u32 MeshId = Scene->NumRenderMeshes++;
    render_mesh* Mesh = Scene->RenderMeshes + MeshId;
    Mesh->Color = Color;
    Mesh->Normal = Normal;
    Mesh->VertexBuffer = VertexBuffer;
    Mesh->IndexBuffer = IndexBuffer;
    Mesh->NumIndices = NumIndices;
    Mesh->MaterialDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Scene->MaterialDescLayout);
    VkDescriptorImageWrite(&RenderState->DescriptorManager, Mesh->MaterialDescriptor, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           Color.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkDescriptorImageWrite(&RenderState->DescriptorManager, Mesh->MaterialDescriptor, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           Normal.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return MeshId;
}

inline u32 SceneMeshAdd(render_scene* Scene, vk_image Color, vk_image Normal, procedural_mesh Mesh)
{
    u32 Result = SceneMeshAdd(Scene, Color, Normal, Mesh.Vertices, Mesh.Indices, Mesh.NumIndices);
    return Result;
}

inline void SceneOpaqueInstanceAdd(render_scene* Scene, u32 MeshId, m4 WTransform)
{
    Assert(Scene->NumOpaqueInstances < Scene->MaxNumOpaqueInstances);

    instance_entry* Instance = Scene->OpaqueInstances + Scene->NumOpaqueInstances++;
    Instance->MeshId = MeshId;
    Instance->WVTransform = CameraGetV(&Scene->Camera)*WTransform;
    Instance->WVPTransform = CameraGetP(&Scene->Camera)*Instance->WVTransform;
}

inline void ScenePointLightAdd(render_scene* Scene, v3 Pos, v3 Color, f32 MaxDistance)
{
    Assert(Scene->NumPointLights < Scene->MaxNumPointLights);

    // TODO: Specify strength or a sphere so that we can visualize nicely too?
    point_light* PointLight = Scene->PointLights + Scene->NumPointLights++;
    PointLight->Pos = Pos;
    PointLight->Color = Color;
    PointLight->MaxDistance = MaxDistance;
}

inline void SceneDirectionalLightSet(render_scene* Scene, v3 LightDir, v3 Color, v3 AmbientColor)
{
    Scene->DirectionalLight.Dir = LightDir;
    Scene->DirectionalLight.Color = Color;
    Scene->DirectionalLight.AmbientColor = AmbientColor;
}

//
// NOTE: Demo Code
//

inline void DemoSwapChainChange(u32 Width, u32 Height)
{
    b32 ReCreate = DemoState->RenderTargetArena.Used != 0;
    VkArenaClear(&DemoState->RenderTargetArena);

    // NOTE: Render Target Data
    RenderTargetEntryReCreate(&DemoState->RenderTargetArena, Width, Height, VK_FORMAT_D32_SFLOAT,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
                              &DemoState->DepthImage, &DemoState->DepthEntry);
}

inline void DemoAllocGlobals(linear_arena* Arena)
{
    // IMPORTANT: These are always the top of the program memory
    DemoState = PushStruct(Arena, demo_state);
    RenderState = PushStruct(Arena, render_state);
    ProfilerState = PushStruct(Arena, profiler_state);
}

DEMO_INIT(Init)
{
    // NOTE: Init Memory
    {
        linear_arena Arena = LinearArenaCreate(ProgramMemory, ProgramMemorySize);
        DemoAllocGlobals(&Arena);
        *DemoState = {};
        *RenderState = {};
        *ProfilerState = {};
        DemoState->Arena = Arena;
        DemoState->TempArena = LinearSubArena(&DemoState->Arena, MegaBytes(10));
    }

    ProfilerStateCreate(ProfilerFlag_OutputCsv | ProfilerFlag_AutoSetEndOfFrame);

    // NOTE: Init Vulkan
    {
        {
            const char* DeviceExtensions[] =
            {
                "VK_EXT_shader_viewport_index_layer",
            };
            
            render_init_params InitParams = {};
            InitParams.ValidationEnabled = false;
            InitParams.WindowWidth = WindowWidth;
            InitParams.WindowHeight = WindowHeight;
            InitParams.GpuLocalSize = MegaBytes(10);
            InitParams.DeviceExtensionCount = ArrayCount(DeviceExtensions);
            InitParams.DeviceExtensions = DeviceExtensions;
            VkInit(VulkanLib, hInstance, WindowHandle, &DemoState->Arena, &DemoState->TempArena, InitParams);
        }
    }
    
    // NOTE: Create samplers
    DemoState->PointSampler = VkSamplerCreate(RenderState->Device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
    DemoState->LinearSampler = VkSamplerCreate(RenderState->Device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
    DemoState->AnisoSampler = VkSamplerMipMapCreate(RenderState->Device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 16.0f,
                                                    VK_SAMPLER_MIPMAP_MODE_LINEAR, 0, 0, 5);    
        
    // NOTE: Init render target entries
    DemoState->SwapChainEntry = RenderTargetSwapChainEntryCreate(RenderState->WindowWidth, RenderState->WindowHeight,
                                                                 RenderState->SwapChainFormat);

    // NOTE: Init scene system
    {
        render_scene* Scene = &DemoState->Scene;

        Scene->Camera = CameraFpsCreate(V3(0, 0, -5), V3(0, 0, 1), true, 1.0f, 0.05f);
        CameraSetPersp(&Scene->Camera, f32(RenderState->WindowWidth / RenderState->WindowHeight), 90.0f, 0.01f, 1000.0f);

        Scene->SceneBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            sizeof(scene_globals));
        
        Scene->MaxNumPointLights = 1000;
        Scene->PointLights = PushArray(&DemoState->Arena, point_light, Scene->MaxNumPointLights);
        Scene->PointLightBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                 sizeof(point_light)*Scene->MaxNumPointLights);
        Scene->PointLightTransforms = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     sizeof(m4)*Scene->MaxNumPointLights);

        Scene->DirectionalLightBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                       sizeof(directional_light));
        
        Scene->MaxNumRenderMeshes = 1000;
        Scene->RenderMeshes = PushArray(&DemoState->Arena, render_mesh, Scene->MaxNumRenderMeshes);

        Scene->MaxNumOpaqueInstances = 50000;
        Scene->OpaqueInstances = PushArray(&DemoState->Arena, instance_entry, Scene->MaxNumOpaqueInstances);
        Scene->OpaqueInstanceBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     sizeof(gpu_instance_entry)*Scene->MaxNumOpaqueInstances);

        // NOTE: Create general descriptor set layouts
        {
            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Scene->MaterialDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }

            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Scene->SceneDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }
        }

        // NOTE: Populate descriptors
        Scene->SceneDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Scene->SceneDescLayout);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Scene->SceneBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->OpaqueInstanceBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->PointLightBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->DirectionalLightBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->PointLightTransforms);
    }

    // NOTE: Create render data
    {
        u32 Width = RenderState->WindowWidth;
        u32 Height = RenderState->WindowHeight;
        
        DemoState->RenderTargetArena = VkLinearArenaCreate(RenderState->Device, RenderState->LocalMemoryId, MegaBytes(100));
        DemoSwapChainChange(Width, Height);

        // NOTE: Forward Pass
        {
            render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, Width, Height);
            RenderTargetAddTarget(&Builder, &DemoState->SwapChainEntry, VkClearColorCreate(0, 0, 0, 1));
            RenderTargetAddTarget(&Builder, &DemoState->DepthEntry, VkClearDepthStencilCreate(0, 0));
            
            vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);
            u32 ColorId = VkRenderPassAttachmentAdd(&RpBuilder, RenderState->SwapChainFormat, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                    VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            u32 DepthId = VkRenderPassAttachmentAdd(&RpBuilder, DemoState->DepthEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                    VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
            VkRenderPassColorRefAdd(&RpBuilder, ColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            VkRenderPassDepthRefAdd(&RpBuilder, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            VkRenderPassSubPassEnd(&RpBuilder);

            DemoState->RenderTarget = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
        }
                
        // NOTE: Create PSO
        {
            vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

            // NOTE: Shaders
            VkPipelineShaderAdd(&Builder, "shader_forward_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
            VkPipelineShaderAdd(&Builder, "shader_forward_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);
                
            // NOTE: Specify input vertex data format
            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32_SFLOAT, sizeof(v2));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
            VkPipelineDepthStateAdd(&Builder, VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER);
            
            // NOTE: Set the blending state
            VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                         VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

            VkDescriptorSetLayout DescriptorLayouts[] =
                {
                    DemoState->Scene.MaterialDescLayout,
                    DemoState->Scene.SceneDescLayout,
                };
            
            DemoState->RenderPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                             DemoState->RenderTarget.RenderPass, 0, DescriptorLayouts,
                                                             ArrayCount(DescriptorLayouts));
        }
    }
    
    // NOTE: Init Boids
    {
        DemoState->MinSpeed = 0.5f;
        DemoState->MaxSpeed = 2.2f;

        DemoState->BirdRadiusSq = Square(0.224f);
        DemoState->AvoidRadiusSq=  Square(0.1418f);
        DemoState->TerrainAvoidRadius = 0.42;

        DemoState->TerrainRadius = 10.55f;
        DemoState->PlatformBlockArena = PlatformBlockArenaCreate(KiloBytes(64), 64);
        u32 CellCountForAxis = 64;
        f32 CellSize = DemoState->TerrainRadius * 2.0f / f32(CellCountForAxis);
        DemoState->Grid = GridCreate(&DemoState->Arena, &DemoState->PlatformBlockArena, AabbCenterRadius(V2(0), V2(DemoState->TerrainRadius)),
                                     CellCountForAxis, CellCountForAxis);
        
        DemoState->AvoidTerrainWeight = 0.14117f;
        DemoState->AvoidBirdWeight = 0.17941f;
        DemoState->AlignFlockWeight = 0.09117f;
        DemoState->MoveToFlockWeight = 0.22352f;
                
        DemoState->BirdRadius = V3(0.05f);
        DemoState->NumBirds = 40000;
        DemoState->CurrBirds = PushArray(&DemoState->Arena, bird, DemoState->NumBirds);
        DemoState->PrevBirds = PushArray(&DemoState->Arena, bird, DemoState->NumBirds);

        for (u32 BirdId = 0; BirdId < DemoState->NumBirds; ++BirdId)
        {
            bird* CurrBird = DemoState->PrevBirds + BirdId;

            f32 RandVel = Lerp(DemoState->MinSpeed, DemoState->MaxSpeed, RandFloat());
            CurrBird->Position = 2.0f * V2(RandFloat(), RandFloat()) - V2(1);
            CurrBird->Position *= 0.9f * DemoState->TerrainRadius;
            CurrBird->Velocity = 0.5f * RandVel * Normalize(2.0f * V2(RandFloat(), RandFloat()) - V2(1));
        }
    }
    
    // NOTE: Upload assets
    vk_commands* Commands = &RenderState->Commands;
    VkCommandsBegin(Commands, RenderState->Device);
    {
        render_scene* Scene = &DemoState->Scene;
        
        // NOTE: Push textures
        vk_image WhiteTexture = {};
        {
            u32 Texels[] =
            {
                0xFFFFFFFF, 
            };
            u32 Dim = 1;

#if 0
            u32 Texels[] =
            {
                0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 
                0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF,
                0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 
                0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF,
                0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 
                0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF,
                0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 
                0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF,
            };
            u32 Dim = 8;
#endif
            
            u32 ImageSize = Dim*Dim*sizeof(u32);
            WhiteTexture = VkImageCreate(RenderState->Device, &RenderState->GpuArena, Dim, Dim, VK_FORMAT_R8G8B8A8_UNORM,
                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

            u8* GpuMemory = VkCommandsPushWriteImage(Commands, WhiteTexture.Image, Dim, Dim, ImageSize,
                                                     VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                     BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                     BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

            Copy(Texels, GpuMemory, ImageSize);
        }
                        
        // NOTE: Push meshes
        DemoState->Quad = SceneMeshAdd(Scene, WhiteTexture, WhiteTexture, AssetsPushQuad());
        DemoState->Cube = SceneMeshAdd(Scene, WhiteTexture, WhiteTexture, AssetsPushCube());
        DemoState->Sphere = SceneMeshAdd(Scene, WhiteTexture, WhiteTexture, AssetsPushSphere(64, 64));

        UiStateCreate(RenderState->Device, &DemoState->Arena, &DemoState->TempArena, RenderState->LocalMemoryId,
                      &RenderState->DescriptorManager, &RenderState->PipelineManager, &RenderState->Commands,
                      RenderState->SwapChainFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &DemoState->UiState);
    }

    VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);
    VkCommandsSubmit(Commands, RenderState->Device, RenderState->GraphicsQueue);
}

DEMO_DESTROY(Destroy)
{
    // TODO: Remove if we can verify that this is auto destroyed (check recompiling if it calls the destructor)
    ProfilerStateDestroy();
}

DEMO_SWAPCHAIN_CHANGE(SwapChainChange)
{
    VkCheckResult(vkDeviceWaitIdle(RenderState->Device));
    VkSwapChainReCreate(&DemoState->TempArena, WindowWidth, WindowHeight, RenderState->PresentMode);
    
    DemoState->SwapChainEntry.Width = RenderState->WindowWidth;
    DemoState->SwapChainEntry.Height = RenderState->WindowHeight;
    DemoState->Scene.Camera.PerspAspectRatio = f32(RenderState->WindowWidth / RenderState->WindowHeight);
    DemoSwapChainChange(RenderState->WindowWidth, RenderState->WindowHeight);
}

DEMO_CODE_RELOAD(CodeReload)
{
    linear_arena Arena = LinearArenaCreate(ProgramMemory, ProgramMemorySize);
    // IMPORTANT: We are relying on the memory being the same here since we have the same base ptr with the VirtualAlloc so we just need
    // to patch our global pointers here
    DemoAllocGlobals(&Arena);

    VkGetGlobalFunctionPointers(VulkanLib);
    VkGetInstanceFunctionPointers();
    VkGetDeviceFunctionPointers();
}

DEMO_MAIN_LOOP(MainLoop)
{
    {
        CPU_TIMED_BLOCK("MainLoop");
    
        u32 ImageIndex;
        VkCheckResult(vkAcquireNextImageKHR(RenderState->Device, RenderState->SwapChain, UINT64_MAX, RenderState->ImageAvailableSemaphore,
                                            VK_NULL_HANDLE, &ImageIndex));
        DemoState->SwapChainEntry.View = RenderState->SwapChainViews[ImageIndex];

        vk_commands* Commands = &RenderState->Commands;
        VkCommandsBegin(Commands, RenderState->Device);

        // NOTE: Update pipelines
        VkPipelineUpdateShaders(RenderState->Device, &RenderState->CpuArena, &RenderState->PipelineManager);

        RenderTargetUpdateEntries(&DemoState->TempArena, &DemoState->RenderTarget);
    
        // NOTE: Update Ui State
        local_global f32 ModifiedFrameTime = 1.0f / 60.0f;
        {
            ui_state* UiState = &DemoState->UiState;
        
            ui_frame_input UiCurrInput = {};
            UiCurrInput.MouseDown = CurrInput->MouseDown;
            UiCurrInput.MousePixelPos = V2(CurrInput->MousePixelPos);
            UiCurrInput.MouseScroll = CurrInput->MouseScroll;
            Copy(CurrInput->KeysDown, UiCurrInput.KeysDown, sizeof(UiCurrInput.KeysDown));
            UiStateBegin(UiState, FrameTime, RenderState->WindowWidth, RenderState->WindowHeight, UiCurrInput);
            local_global v2 PanelPos = V2(100, 800);
            ui_panel Panel = UiPanelBegin(UiState, &PanelPos, "Shadow Panel");

            {
                UiPanelText(&Panel, "Boid Data:");

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "FrameTime:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 0.03f, &ModifiedFrameTime);
                UiPanelNumberBox(&Panel, 0.0f, 0.03f, &ModifiedFrameTime);
                UiPanelNextRow(&Panel);
            
                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Min Speed:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->MinSpeed);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->MinSpeed);
                UiPanelNextRow(&Panel);
            
                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Max Speed:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->MaxSpeed);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->MaxSpeed);
                UiPanelNextRow(&Panel);
            
                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Bird Radius Sq:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 1.0f, &DemoState->BirdRadiusSq);
                UiPanelNumberBox(&Panel, 0.0f, 1.0f, &DemoState->BirdRadiusSq);
                UiPanelNextRow(&Panel);
            
                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Avoid Radius Sq:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 1.0f, &DemoState->AvoidRadiusSq);
                UiPanelNumberBox(&Panel, 0.0f, 1.0f, &DemoState->AvoidRadiusSq);
                UiPanelNextRow(&Panel);
            
                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Terrain Avoid Radius:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 1.0f, &DemoState->TerrainAvoidRadius);
                UiPanelNumberBox(&Panel, 0.0f, 1.0f, &DemoState->TerrainAvoidRadius);
                UiPanelNextRow(&Panel);
            
                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Terrain Radius:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 50.0f, &DemoState->TerrainRadius);
                UiPanelNumberBox(&Panel, 0.0f, 50.0f, &DemoState->TerrainRadius);
                UiPanelNextRow(&Panel);
            
                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Avoid Terrain Weight:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 1.0f, &DemoState->AvoidTerrainWeight);
                UiPanelNumberBox(&Panel, 0.0f, 1.0f, &DemoState->AvoidTerrainWeight);
                UiPanelNextRow(&Panel);
            
                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Avoid Bird Weight:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 1.0f, &DemoState->AvoidBirdWeight);
                UiPanelNumberBox(&Panel, 0.0f, 1.0f, &DemoState->AvoidBirdWeight);
                UiPanelNextRow(&Panel);
            
                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Align Flock Weight:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 1.0f, &DemoState->AlignFlockWeight);
                UiPanelNumberBox(&Panel, 0.0f, 1.0f, &DemoState->AlignFlockWeight);
                UiPanelNextRow(&Panel);
            
                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Move To Flock Weight:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 1.0f, &DemoState->MoveToFlockWeight);
                UiPanelNumberBox(&Panel, 0.0f, 1.0f, &DemoState->MoveToFlockWeight);
                UiPanelNextRow(&Panel);
            
            }

            UiPanelEnd(&Panel);

            UiStateEnd(UiState, &RenderState->DescriptorManager);
        }

        // NOTE: Upload scene data
        {
            render_scene* Scene = &DemoState->Scene;
            Scene->NumOpaqueInstances = 0;
            Scene->NumPointLights = 0;
            if (!(DemoState->UiState.MouseTouchingUi || DemoState->UiState.ProcessedInteraction))
            {
                CameraUpdate(&Scene->Camera, CurrInput, PrevInput);
            }
                
            // NOTE: Populate scene
            {
                // NOTE: Add Instances
                {
                    CPU_TIMED_BLOCK("Add Instances");
                
                    grid* Grid = &DemoState->Grid;
                    temp_mem TempMem = BeginTempMem(&DemoState->TempArena);
                
                    // NOTE: Terrain
                    SceneOpaqueInstanceAdd(Scene, DemoState->Quad, M4Pos(V3(0.0f, 0.0f, 0.0f)) * M4Scale(V3(2.0f*DemoState->TerrainRadius)));

                    local_global b32 EvenFrame = true;

                    bird* PrevBirdArray = EvenFrame ? DemoState->PrevBirds : DemoState->CurrBirds;
                    bird* CurrBirdArray = EvenFrame ? DemoState->CurrBirds : DemoState->PrevBirds;
                    EvenFrame = !EvenFrame;
                
                    // NOTE: Add all birds to grid data structure
                    u32* BirdGridIds = PushArray(&DemoState->TempArena, u32, DemoState->NumBirds);
                    {
                        CPU_TIMED_BLOCK("Generate Grid");
                        for (u32 BirdId = 0; BirdId < DemoState->NumBirds; ++BirdId)
                        {
                            bird* CurrBird = PrevBirdArray + BirdId;
                            BirdGridIds[BirdId] = GridAddEntity(&DemoState->Grid, CurrBird->Position, BirdId);
                        }
                    }
                
                    // NOTE: Update birds
                    {
                        CPU_TIMED_BLOCK("Update Birds");
                    
                        for (u32 BirdId = 0; BirdId < DemoState->NumBirds; ++BirdId)
                        {
                            bird CurrBird = PrevBirdArray[BirdId];
                    
                            v2 NewBirdPosition = CurrBird.Position;
                            v2 NewBirdVelocity = CurrBird.Velocity;

                            u32 NumBirdsInRadius = 0;
                            v2 AvgFlockDir = {};
                            v2 AvgFlockPos = {};
                            v2 AvgFlockAvoidance = {};

                            {
                                CPU_TIMED_BLOCK("Generate avg data");
                                grid_range Range = GridGetRange(&DemoState->Grid, NewBirdPosition, Max(DemoState->BirdRadiusSq, DemoState->AvoidRadiusSq));
                                for (i32 GridY = Range.StartY; GridY <= Range.EndY; ++GridY)
                                {
                                    for (i32 GridX = Range.StartX; GridX <= Range.EndX; ++GridX)
                                    {
                                        grid_cell* CurrCell = Grid->Cells + GridY * Grid->NumCellsX + GridX;

                                        // NOTE: This will only help for larger avoid/boid radii
#if 0
                                        v2 CellDim = AabbGetDim(Grid->WorldBounds) / V2(Grid->NumCellsX, Grid->NumCellsY);
                                        v2 CellMin = Grid->WorldBounds.Min + V2(GridX, GridY) * CellDim;
                                        v2 CellMax = CellMin + CellDim;

                                        v2 MaxCellDistanceVec = Max(Abs(CellMin - NewBirdPosition), Abs(CellMax - NewBirdPosition));
                                        f32 MaxCellDistance = LengthSquared(MaxCellDistanceVec);

                                        if (MaxCellDistance < DemoState->BirdRadiusSq && MaxCellDistance < DemoState->AvoidRadiusSq)
                                        {
                                            // NOTE: Fast path, we don't have to compute distances for each bird
                                            u32 GlobalIndexId = 0;
                                            for (block* CurrBlock = CurrCell->IndexArena.Next; CurrBlock; CurrBlock = CurrBlock->Next)
                                            {
                                                u32* BlockIndices = BlockGetData(CurrBlock, u32);
                                                u32 NumIndicesInBlock = Min(CurrCell->NumIndices - GlobalIndexId, Grid->MaxNumIndicesPerBlock);
                                                for (u32 IndexId = 0; IndexId < NumIndicesInBlock; ++IndexId, ++GlobalIndexId)
                                                {
                                                    u32 NearbyBirdId = BlockIndices[IndexId];
                                    
                                                    if (NearbyBirdId != BirdId)
                                                    {
                                                        bird* NearbyBird = PrevBirdArray + NearbyBirdId;
                                                        v2 DistanceVec = NearbyBird->Position - NewBirdPosition;

                                                        // TODO: Add a proper FOV
                                                        NumBirdsInRadius += 1;
                                
                                                        // NOTE: Velocity Matching
                                                        AvgFlockDir += NearbyBird->Velocity;
                                                        // NOTE: Bird Flocking
                                                        AvgFlockPos += NearbyBird->Position;
                                                        // NOTE: Avoidance
                                                        AvgFlockAvoidance += -DistanceVec;
                                                    }
                                                }
                                            }
                                        }
                                        else
#endif
                                        
                                        {
                                            // NOTE: Loop over all birds in the grid
                                            u32 GlobalIndexId = 0;
                                            for (block* CurrBlock = CurrCell->IndexArena.Next; CurrBlock; CurrBlock = CurrBlock->Next)
                                            {
                                                u32* BlockIndices = BlockGetData(CurrBlock, u32);
                                                u32 NumIndicesInBlock = Min(CurrCell->NumIndices - GlobalIndexId, Grid->MaxNumIndicesPerBlock);
                                                for (u32 IndexId = 0; IndexId < NumIndicesInBlock; ++IndexId, ++GlobalIndexId)
                                                {
                                                    u32 NearbyBirdId = BlockIndices[IndexId];
                                    
                                                    if (NearbyBirdId != BirdId)
                                                    {
                                                        bird* NearbyBird = PrevBirdArray + NearbyBirdId;
                                                        v2 DistanceVec = NearbyBird->Position - NewBirdPosition;
                                                        f32 DistanceSq = LengthSquared(DistanceVec);

                                                        // TODO: Add a proper FOV
                                                        if (DistanceSq < DemoState->BirdRadiusSq)
                                                        {
                                                            NumBirdsInRadius += 1;
                                
                                                            // NOTE: Velocity Matching
                                                            AvgFlockDir += NearbyBird->Velocity;

                                                            // NOTE: Bird Flocking
                                                            AvgFlockPos += NearbyBird->Position;
                                                        }
                                                        if (DistanceSq < DemoState->AvoidRadiusSq)
                                                        {
                                                            // NOTE: Avoidance
                                                            AvgFlockAvoidance += -DistanceVec;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    
                                    }
                                }
                            }
                    
                            // NOTE: Only the flock pos has to be averaged
                            if (NumBirdsInRadius)
                            {
                                AvgFlockDir /= f32(NumBirdsInRadius);
                                AvgFlockPos /= f32(NumBirdsInRadius);
                            }
                    
                            // NOTE: Avoid Wall Vel
                            v2 AvoidWallDir = {};
                            {
                                if (NewBirdPosition.x - DemoState->TerrainAvoidRadius <= -DemoState->TerrainRadius)
                                {
                                    AvoidWallDir.x = 1.0f;
                                }
                                if (NewBirdPosition.x + DemoState->TerrainAvoidRadius >= DemoState->TerrainRadius)
                                {
                                    AvoidWallDir.x = -1.0f;
                                }
                                if (NewBirdPosition.y - DemoState->TerrainAvoidRadius <= -DemoState->TerrainRadius)
                                {
                                    AvoidWallDir.y = 1.0f;
                                }
                                if (NewBirdPosition.y + DemoState->TerrainAvoidRadius >= DemoState->TerrainRadius)
                                {
                                    AvoidWallDir.y = -1.0f;
                                }
                            }
                    
                            // NOTE: Apply rules
                            {
                                //CPU_TIMED_BLOCK("Apply Rules and Gen Opaque");
                            
                                {
                                    // NOTE: Fly towards center
                                    if (NumBirdsInRadius > 0)
                                    {
                                        NewBirdVelocity += DemoState->MoveToFlockWeight * (AvgFlockPos - NewBirdPosition);
                                    }
                        
                                    // NOTE: Avoid Others
                                    NewBirdVelocity += DemoState->AvoidBirdWeight * AvgFlockAvoidance;

                                    // NOTE: Align Velocities
                                    if (NumBirdsInRadius > 0)
                                    {
                                        NewBirdVelocity += DemoState->AlignFlockWeight * (AvgFlockDir - NewBirdVelocity);
                                    }
                        
                                    // NOTE: Clamp Velocity
                                    f32 BirdSpeed = Clamp(Length(NewBirdVelocity), DemoState->MinSpeed, DemoState->MaxSpeed);
                                    NewBirdVelocity = BirdSpeed * Normalize(NewBirdVelocity);

                                    // NOTE: Avoid Terrain
                                    NewBirdVelocity += DemoState->AvoidTerrainWeight * AvoidWallDir;
                                }
                    
                                NewBirdPosition += NewBirdVelocity * ModifiedFrameTime;

                                // NOTE: Write into next bird array
                                CurrBirdArray[BirdId].Velocity = NewBirdVelocity;
                                CurrBirdArray[BirdId].Position = NewBirdPosition;

                                m4 Transform = M4Pos(V3(NewBirdPosition, 0)) * M4Scale(V3(0.05f));
                                SceneOpaqueInstanceAdd(Scene, DemoState->Cube, Transform);
                            }
                        }
                    }
                
                    EndTempMem(TempMem);

                    // NOTE: Clear the grid
                    {
                        CPU_TIMED_BLOCK("Clear Grid");
                        GridClear(Grid);
                    }
                }

                {
                    CPU_TIMED_BLOCK("Upload instances to GPU");
                    gpu_instance_entry* GpuData = VkCommandsPushWriteArray(Commands, Scene->OpaqueInstanceBuffer, gpu_instance_entry, Scene->NumOpaqueInstances,
                                                                           BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                           BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));

                    for (u32 InstanceId = 0; InstanceId < Scene->NumOpaqueInstances; ++InstanceId)
                    {
                        GpuData[InstanceId].WVTransform = Scene->OpaqueInstances[InstanceId].WVTransform;
                        GpuData[InstanceId].WVPTransform = Scene->OpaqueInstances[InstanceId].WVPTransform;
                    }
                }
            
                // NOTE: Add point lights
                ScenePointLightAdd(Scene, V3(0.0f, 0.0f, -1.0f), V3(1.0f, 0.0f, 0.0f), 1);
                ScenePointLightAdd(Scene, V3(-1.0f, 0.0f, 0.0f), V3(1.0f, 1.0f, 0.0f), 1);
                ScenePointLightAdd(Scene, V3(0.0f, 1.0f, 1.0f), V3(1.0f, 0.0f, 1.0f), 1);
                ScenePointLightAdd(Scene, V3(0.0f, -1.0f, 1.0f), V3(0.0f, 1.0f, 1.0f), 1);
                ScenePointLightAdd(Scene, V3(-1.0f, 0.0f, -1.0f), V3(0.0f, 0.0f, 1.0f), 1);
            
                SceneDirectionalLightSet(Scene, Normalize(V3(-1.0f, -1.0f, 0.0f)), 0.3f*V3(1.0f, 1.0f, 1.0f), V3(0.4f, 0.4f, 0.4f));
            }        
        
            // NOTE: Push Point Lights
            {
                point_light* PointLights = VkCommandsPushWriteArray(Commands, Scene->PointLightBuffer, point_light, Scene->NumPointLights,
                                                                    BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                    BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));
                m4* Transforms = VkCommandsPushWriteArray(Commands, Scene->PointLightTransforms, m4, Scene->NumPointLights,
                                                          BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                          BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));

                for (u32 LightId = 0; LightId < Scene->NumPointLights; ++LightId)
                {
                    point_light* CurrLight = Scene->PointLights + LightId;
                    PointLights[LightId] = *CurrLight;
                    // NOTE: Convert to view space
                    PointLights[LightId].Pos = (CameraGetV(&Scene->Camera) * V4(CurrLight->Pos, 1.0f)).xyz;
                    Transforms[LightId] = CameraGetVP(&Scene->Camera) * M4Pos(CurrLight->Pos) * M4Scale(V3(CurrLight->MaxDistance));
                }
            }

            // NOTE: Push Directional Lights
            {
                directional_light* GpuData = VkCommandsPushWriteStruct(Commands, Scene->DirectionalLightBuffer, directional_light,
                                                                       BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                       BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));
                Copy(&Scene->DirectionalLight, GpuData, sizeof(directional_light));
            }
        
            {
                scene_globals* Data = VkCommandsPushWriteStruct(Commands, Scene->SceneBuffer, scene_globals,
                                                                BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                                BarrierMask(VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
                *Data = {};
                Data->CameraPos = Scene->Camera.Pos;
                Data->NumPointLights = Scene->NumPointLights;
            }

            VkCommandsTransferFlush(Commands, RenderState->Device);
        }

        // NOTE: Render Scene
        RenderTargetPassBegin(&DemoState->RenderTarget, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
        {
            CPU_TIMED_BLOCK("Render Forward");
            render_scene* Scene = &DemoState->Scene;
        
            vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->RenderPipeline->Handle);
            {
                VkDescriptorSet DescriptorSets[] =
                    {
                        Scene->SceneDescriptor,
                    };
                vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->RenderPipeline->Layout, 1,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            }

            u32 InstanceId = 0;
            for (; InstanceId < Scene->NumOpaqueInstances; )
            {
                instance_entry* CurrInstance = Scene->OpaqueInstances + InstanceId;
                render_mesh* CurrMesh = Scene->RenderMeshes + CurrInstance->MeshId;

                {
                    VkDescriptorSet DescriptorSets[] =
                        {
                            CurrMesh->MaterialDescriptor,
                        };
                    vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->RenderPipeline->Layout, 0,
                                            ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
                }
            
                VkDeviceSize Offset = 0;
                vkCmdBindVertexBuffers(Commands->Buffer, 0, 1, &CurrMesh->VertexBuffer, &Offset);
                vkCmdBindIndexBuffer(Commands->Buffer, CurrMesh->IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

                // NOTE: Check how many instances share the same mesh
                u32 NextInstanceId = 1;
                while (NextInstanceId < Scene->NumOpaqueInstances)
                {
                    instance_entry* NextInstance = Scene->OpaqueInstances + NextInstanceId;
                    render_mesh* NextMesh = Scene->RenderMeshes + NextInstance->MeshId;

                    if (NextMesh != CurrMesh)
                    {
                        break;
                    }
                    NextInstanceId += 1;
                }
                vkCmdDrawIndexed(Commands->Buffer, CurrMesh->NumIndices, NextInstanceId - InstanceId, 0, 0, InstanceId);

                InstanceId = NextInstanceId;
            }
        }
        RenderTargetPassEnd(Commands);        
        UiStateRender(&DemoState->UiState, RenderState->Device, Commands, DemoState->SwapChainEntry.View);

        VkCommandsEnd(Commands, RenderState->Device);
    
        // NOTE: Render to our window surface
        // NOTE: Tell queue where we render to surface to wait
        VkPipelineStageFlags WaitDstMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo SubmitInfo = {};
        SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        SubmitInfo.waitSemaphoreCount = 1;
        SubmitInfo.pWaitSemaphores = &RenderState->ImageAvailableSemaphore;
        SubmitInfo.pWaitDstStageMask = &WaitDstMask;
        SubmitInfo.commandBufferCount = 1;
        SubmitInfo.pCommandBuffers = &Commands->Buffer;
        SubmitInfo.signalSemaphoreCount = 1;
        SubmitInfo.pSignalSemaphores = &RenderState->FinishedRenderingSemaphore;
        VkCheckResult(vkQueueSubmit(RenderState->GraphicsQueue, 1, &SubmitInfo, Commands->Fence));
    
        VkPresentInfoKHR PresentInfo = {};
        PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        PresentInfo.waitSemaphoreCount = 1;
        PresentInfo.pWaitSemaphores = &RenderState->FinishedRenderingSemaphore;
        PresentInfo.swapchainCount = 1;
        PresentInfo.pSwapchains = &RenderState->SwapChain;
        PresentInfo.pImageIndices = &ImageIndex;
        VkResult Result = vkQueuePresentKHR(RenderState->PresentQueue, &PresentInfo);

        switch (Result)
        {
            case VK_SUCCESS:
            {
            } break;

            case VK_ERROR_OUT_OF_DATE_KHR:
            case VK_SUBOPTIMAL_KHR:
            {
                // NOTE: Window size changed
                InvalidCodePath;
            } break;

            default:
            {
                InvalidCodePath;
            } break;
        }
    }

    ProfilerProcessData();
    // TODO: FIX BUG
    ProfilerPrintTimeStamps();
}
