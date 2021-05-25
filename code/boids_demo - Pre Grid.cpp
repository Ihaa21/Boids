
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

inline u32 GridAddObject(grid* Grid, v3 Position, v3 Size)
{
    
}

inline u32 GridUpdateObject()
{
    
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
}

DEMO_INIT(Init)
{
    // NOTE: Init Memory
    {
        linear_arena Arena = LinearArenaCreate(ProgramMemory, ProgramMemorySize);
        DemoAllocGlobals(&Arena);
        *DemoState = {};
        *RenderState = {};
        DemoState->Arena = Arena;
        DemoState->TempArena = LinearSubArena(&DemoState->Arena, MegaBytes(10));
    }

    // NOTE: Init Vulkan
    {
        {
            const char* DeviceExtensions[] =
            {
                "VK_EXT_shader_viewport_index_layer",
            };
            
            render_init_params InitParams = {};
            InitParams.ValidationEnabled = true;
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

        DemoState->BoidRadiusSq = Square(0.224f);
        DemoState->AvoidRadiusSq=  Square(0.1418f);
        DemoState->TerrainAvoidRadius = 0.42;

        DemoState->TerrainRadius = 2.55f;
        
        DemoState->AvoidTerrainWeight = 0.14117f;
        DemoState->AvoidBirdWeight = 0.17941f;
        DemoState->AlignFlockWeight = 0.09117f;
        DemoState->MoveToFlockWeight = 0.22352f;
                
        DemoState->NumBirds = 5000;
        DemoState->Birds = 
        DemoState->BoidVelocities = PushArray(&DemoState->Arena, v3, DemoState->NumBoids);

        DemoState->BirdRadius = ;

        for (u32 BoidId = 0; BoidId < DemoState->NumBoids; ++BoidId)
        {
            f32 RandVel = Lerp(DemoState->MinSpeed, DemoState->MaxSpeed, RandFloat());
            DemoState->BoidPositions[BoidId] = 2.0f * V3(RandFloat(), RandFloat(), 0.0f) - V3(1, 1, 0);
            DemoState->BoidVelocities[BoidId] = 0.5f * RandVel * Normalize(2.0f * V3(RandFloat(), RandFloat(), 0.0f) - V3(1, 1, 0));
            
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
            UiPanelText(&Panel, "Max Steer Speed:");
            UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->MaxSteerSpeed);
            UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->MaxSteerSpeed);
            UiPanelNextRow(&Panel);
            
            UiPanelNextRowIndent(&Panel);
            UiPanelText(&Panel, "Boid Radius Sq:");
            UiPanelHorizontalSlider(&Panel, 0.0f, 1.0f, &DemoState->BoidRadiusSq);
            UiPanelNumberBox(&Panel, 0.0f, 1.0f, &DemoState->BoidRadiusSq);
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
            UiPanelHorizontalSlider(&Panel, 0.0f, 5.0f, &DemoState->TerrainRadius);
            UiPanelNumberBox(&Panel, 0.0f, 5.0f, &DemoState->TerrainRadius);
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
                // NOTE: Terrain
                SceneOpaqueInstanceAdd(Scene, DemoState->Quad, M4Pos(V3(0.0f, 0.0f, 0.0f)) * M4Scale(V3(2.0f*DemoState->TerrainRadius)));

                // NOTE: Update boids
                for (u32 BoidId = 0; BoidId < DemoState->NumBoids; ++BoidId)
                {
                    v3 OldBoidPosition = DemoState->BoidPositions[BoidId];
                    v3 OldBoidVelocity = DemoState->BoidVelocities[BoidId];
                    v3 NewBoidPosition = OldBoidPosition;
                    v3 NewBoidVelocity = OldBoidVelocity;

                    u32 NumBoidsInRadius = 0;
                    v3 AvgFlockDir = {};
                    v3 AvgFlockPos = {};
                    v3 AvgFlockAvoidance = {};
                    for (u32 NearbyBoidId = 0; NearbyBoidId < DemoState->NumBoids; ++NearbyBoidId)
                    {
                        if (NearbyBoidId != BoidId)
                        {
                            v3 DistanceVec = DemoState->BoidPositions[NearbyBoidId] - NewBoidPosition;
                            f32 DistanceSq = LengthSquared(DistanceVec);

                            // TODO: Add a proper FOV
                            if (DistanceSq < DemoState->BoidRadiusSq)
                            {
                                NumBoidsInRadius += 1;
                                
                                // NOTE: Velocity Matching
                                AvgFlockDir += DemoState->BoidVelocities[NearbyBoidId];

                                // NOTE: Boid Flocking
                                AvgFlockPos += DemoState->BoidPositions[NearbyBoidId];
                            }
                            if (DistanceSq < DemoState->AvoidRadiusSq)
                            {
                                // NOTE: Avoidance
                                AvgFlockAvoidance += OldBoidPosition - DemoState->BoidPositions[NearbyBoidId];
                            }
                        }
                    }

                    // NOTE: Only the flock pos has to be averaged
                    if (NumBoidsInRadius)
                    {
                        AvgFlockDir /= f32(NumBoidsInRadius);
                        AvgFlockPos /= f32(NumBoidsInRadius);
                    }
                    
                    // NOTE: Avoid Wall Vel
                    b32 CloseToWall = false;
                    v3 AvoidWallDir = {};
                    {
                        if (NewBoidPosition.x - DemoState->TerrainAvoidRadius <= -DemoState->TerrainRadius)
                        {
                            CloseToWall = true;
                            AvoidWallDir.x = 1.0f;
                        }
                        if (NewBoidPosition.x + DemoState->TerrainAvoidRadius >= DemoState->TerrainRadius)
                        {
                            CloseToWall = true;
                            AvoidWallDir.x = -1.0f;
                        }
                        if (NewBoidPosition.y - DemoState->TerrainAvoidRadius <= -DemoState->TerrainRadius)
                        {
                            CloseToWall = true;
                            AvoidWallDir.y = 1.0f;
                        }
                        if (NewBoidPosition.y + DemoState->TerrainAvoidRadius >= DemoState->TerrainRadius)
                        {
                            CloseToWall = true;
                            AvoidWallDir.y = -1.0f;
                        }
                        if (NewBoidPosition.z - DemoState->TerrainAvoidRadius <= -DemoState->TerrainRadius)
                        {
                            CloseToWall = true;
                            AvoidWallDir.z = 1.0f;
                        }
                        if (NewBoidPosition.z + DemoState->TerrainAvoidRadius >= DemoState->TerrainRadius)
                        {
                            CloseToWall = true;
                            AvoidWallDir.z = -1.0f;
                        }
                    }
                    
                    // NOTE: Apply rules
                    {
                        // NOTE: Fly towards center
                        if (NumBoidsInRadius > 0)
                        {
                            NewBoidVelocity += DemoState->MoveToFlockWeight * (AvgFlockPos - NewBoidPosition);
                        }
                        
                        // NOTE: Avoid Others
                        NewBoidVelocity += DemoState->AvoidBirdWeight * AvgFlockAvoidance;

                        // NOTE: Align Velocities
                        if (NumBoidsInRadius > 0)
                        {
                            v3 Test = (AvgFlockDir - NewBoidVelocity);
                            Test = Test * DemoState->AlignFlockWeight;
                            NewBoidVelocity += DemoState->AlignFlockWeight * (AvgFlockDir - NewBoidVelocity);
                        }
                        
                        // NOTE: Clamp Velocity
                        f32 BoidSpeed = Clamp(Length(NewBoidVelocity), DemoState->MinSpeed, DemoState->MaxSpeed);
                        NewBoidVelocity = BoidSpeed * Normalize(NewBoidVelocity);

                        // NOTE: Avoid Terrain
                        NewBoidVelocity += DemoState->AvoidTerrainWeight * AvoidWallDir;
                    }
                    
                    NewBoidPosition += NewBoidVelocity * ModifiedFrameTime;
                    
                    DemoState->BoidVelocities[BoidId] = NewBoidVelocity;
                    DemoState->BoidPositions[BoidId] = NewBoidPosition;

                    m4 Transform = M4Pos(NewBoidPosition) * M4Scale(V3(0.05f));
                    SceneOpaqueInstanceAdd(Scene, DemoState->Sphere, Transform);
                }
            }

            {
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
        
        for (u32 InstanceId = 0; InstanceId < Scene->NumOpaqueInstances; ++InstanceId)
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
            vkCmdDrawIndexed(Commands->Buffer, CurrMesh->NumIndices, 1, 0, 0, InstanceId);
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
