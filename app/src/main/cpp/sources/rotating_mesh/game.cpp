#include <rotating_mesh/game.h>

AV_DISABLE_COMMON_WARNINGS

#include <cassert>

AV_RESTORE_WARNING_STATE

#include <vulkan_utils.h>
#include <rotating_mesh/vertex_info.h>


namespace rotating_mesh {

constexpr static const size_t TEXTURE_COMMAND_BUFFERS = 5U;
constexpr static const size_t BUFFER_COMMAND_BUFFERS = 1U;

constexpr static const char* VERTEX_SHADER = "shaders/static-mesh-vs.spv";
constexpr static const char* VERTEX_SHADER_ENTRY_POINT = "VS";

constexpr static const char* FRAGMENT_SHADER = "shaders/static-mesh-ps.spv";
constexpr static const char* FRAGMENT_SHADER_ENTRY_POINT = "PS";

constexpr static const char* MATERIAL_1_DIFFUSE = "textures/rotating_mesh/sonic-material-1-diffuse.png";
constexpr static const char* MATERIAL_2_DIFFUSE = "textures/rotating_mesh/sonic-material-2-diffuse.png";
constexpr static const char* MATERIAL_2_NORMAL = "textures/rotating_mesh/sonic-material-2-normal.png";
constexpr static const char* MATERIAL_3_DIFFUSE = "textures/rotating_mesh/sonic-material-3-diffuse.png";
constexpr static const char* MATERIAL_3_NORMAL = "textures/rotating_mesh/sonic-material-3-normal.png";

//----------------------------------------------------------------------------------------------------------------------

Game::Game ():
    _commandPool ( VK_NULL_HANDLE ),
//    _constantBuffer ( VK_NULL_HANDLE ),
//    _constantBufferDeviceMemory ( VK_NULL_HANDLE ),
    _descriptorPool ( VK_NULL_HANDLE ),
    _descriptorSet ( VK_NULL_HANDLE ),
    _descriptorSetLayout ( VK_NULL_HANDLE ),
    _pipeline ( VK_NULL_HANDLE ),
    _pipelineLayout ( VK_NULL_HANDLE ),
    _renderPass ( VK_NULL_HANDLE ),
    _renderPassEndSemaphore ( VK_NULL_HANDLE ),
    _renderTargetAcquiredSemaphore ( VK_NULL_HANDLE ),
    _sampler09Mips ( VK_NULL_HANDLE ),
    _sampler10Mips ( VK_NULL_HANDLE ),
    _sampler11Mips ( VK_NULL_HANDLE ),
    _vertexShaderModule ( VK_NULL_HANDLE ),
    _fragmentShaderModule ( VK_NULL_HANDLE )
{
    // NOTHING
}

bool Game::IsReady ()
{
    return _descriptorSet != VK_NULL_HANDLE;
}

bool Game::OnInit ( android_vulkan::Renderer &renderer )
{
    if ( !CreateRenderPass ( renderer ) )
    {
        OnDestroy ( renderer );
        return false;
    }

    if ( !CreateSyncPrimitives ( renderer ) )
    {
        OnDestroy ( renderer );
        return false;
    }

    if ( !CreateCommandPool ( renderer ) )
    {
        OnDestroy ( renderer );
        return false;
    }

    if ( !LoadGPUContent ( renderer ) )
    {
        OnDestroy ( renderer );
        return false;
    }

    if ( !CreateSamplers ( renderer ) )
    {
        OnDestroy ( renderer );
        return false;
    }

    if ( !CreateShaderModules ( renderer ) )
    {
        OnDestroy ( renderer );
        return false;
    }

    if ( !CreatePipelineLayout ( renderer ) )
    {
        OnDestroy ( renderer );
        return false;
    }

    if ( !CreatePipeline ( renderer ) )
    {
        OnDestroy ( renderer );
        return false;
    }

    if ( !CreateDescriptorSet ( renderer ) )
    {
        OnDestroy ( renderer );
        return false;
    }

    if ( !InitCommandBuffers ( renderer ) )
    {
        OnDestroy ( renderer );
        return false;
    }

    return true;
}

bool Game::OnFrame ( android_vulkan::Renderer &renderer, double /*deltaTime*/ )
{
    uint32_t presentationImageIndex = UINT32_MAX;

    if ( !BeginFrame ( presentationImageIndex, renderer ) )
        return false;

    constexpr const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

    VkSubmitInfo submitInfo;
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;
    submitInfo.commandBufferCount = 1U;
    submitInfo.pCommandBuffers = &_commandBuffers[ static_cast<uint32_t> ( presentationImageIndex ) ];
    submitInfo.waitSemaphoreCount = 1U;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.pWaitSemaphores = &_renderTargetAcquiredSemaphore;
    submitInfo.signalSemaphoreCount = 1U;
    submitInfo.pSignalSemaphores = &_renderPassEndSemaphore;

    const bool result = renderer.CheckVkResult (
        vkQueueSubmit ( renderer.GetQueue (), 1U, &submitInfo, VK_NULL_HANDLE ),
        "Game::OnFrame",
        "Can't submit command buffer"
    );

    if ( !result )
        return false;

    return EndFrame ( presentationImageIndex, renderer );
}

bool Game::OnDestroy ( android_vulkan::Renderer &renderer )
{
    DestroyDescriptorSet ( renderer );
    DestroyPipeline ( renderer );
    DestroyPipelineLayout ( renderer );
    DestroyShaderModules ( renderer );
    DestroySamplers ( renderer );
    DestroyMeshes ( renderer );
    DestroyTextures ( renderer );
    DestroyCommandPool ( renderer );
    DestroySyncPrimitives ( renderer );
    DestroyRenderPass ( renderer );

    return true;
}

bool Game::BeginFrame ( uint32_t &presentationImageIndex, android_vulkan::Renderer &renderer )
{
    return renderer.CheckVkResult (
        vkAcquireNextImageKHR ( renderer.GetDevice (),
            renderer.GetSwapchain (),
            UINT64_MAX,
            _renderTargetAcquiredSemaphore,
            VK_NULL_HANDLE,
            &presentationImageIndex
        ),

        "Game::BeginFrame",
        "Can't get presentation image index"
    );
}

bool Game::EndFrame ( uint32_t presentationImageIndex, android_vulkan::Renderer &renderer )
{
    VkResult presentResult = VK_ERROR_DEVICE_LOST;

    VkPresentInfoKHR presentInfo;
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.waitSemaphoreCount = 1U;
    presentInfo.pWaitSemaphores = &_renderPassEndSemaphore;
    presentInfo.pResults = &presentResult;
    presentInfo.swapchainCount = 1U;
    presentInfo.pSwapchains = &renderer.GetSwapchain ();
    presentInfo.pImageIndices = &presentationImageIndex;

    const VkResult mainResult = vkQueuePresentKHR ( renderer.GetQueue (), &presentInfo );

    // Note vkQueuePresentKHR may return VK_SUBOPTIMAL_KHR right before application is minimized.
    if ( mainResult == VK_SUBOPTIMAL_KHR )
        return true;

    if ( !renderer.CheckVkResult ( mainResult, "Game::EndFrame", "Can't present frame" ) )
        return false;

    return renderer.CheckVkResult ( presentResult, "Game::EndFrame", "Present queue has been failed" );
}

bool Game::CreateCommandPool ( android_vulkan::Renderer &renderer )
{
    VkCommandPoolCreateInfo poolInfo;
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.pNext = nullptr;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = renderer.GetQueueFamilyIndex ();

    const bool result = renderer.CheckVkResult (
        vkCreateCommandPool ( renderer.GetDevice (), &poolInfo, nullptr, &_commandPool ),
        "Game::CreateCommandPool",
        "Can't create command pool"
    );

    if ( !result )
        return false;

    AV_REGISTER_COMMAND_POOL ( "Game::_commandPool" )
    return true;
}

void Game::DestroyCommandPool ( android_vulkan::Renderer &renderer )
{
    if ( _commandPool == VK_NULL_HANDLE )
        return;

    vkDestroyCommandPool ( renderer.GetDevice (), _commandPool, nullptr );
    _commandPool = VK_NULL_HANDLE;
    AV_UNREGISTER_COMMAND_POOL ( "Game::_commandPool" )
}

bool Game::CreateConstantBuffer ( android_vulkan::Renderer& /*renderer*/ )
{
    assert ( !"Game::CreateConstantBuffer - Implement me!" );
    return false;
}

void Game::DestroyConstantBuffer ( android_vulkan::Renderer& /*renderer*/ )
{
    assert ( !"Game::DestroyConstantBuffer - Implement me!" );
}

bool Game::CreateDescriptorSet ( android_vulkan::Renderer &renderer )
{
    const VkDevice device = renderer.GetDevice ();

    VkDescriptorPoolSize samplerFeature;
    samplerFeature.descriptorCount = 1U;
    samplerFeature.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    VkDescriptorPoolCreateInfo poolInfo;
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.pNext = nullptr;
    poolInfo.flags = 0U;
    poolInfo.maxSets = 1U;
    poolInfo.poolSizeCount = 1U;
    poolInfo.pPoolSizes = &samplerFeature;

    bool result = renderer.CheckVkResult ( vkCreateDescriptorPool ( device, &poolInfo, nullptr, &_descriptorPool ),
        "Game::CreateDescriptorSet",
        "Can't create descriptor pool"
    );

    if ( !result )
        return false;

    AV_REGISTER_DESCRIPTOR_POOL ( "Game::_descriptorPool" )

    VkDescriptorSetAllocateInfo setAllocateInfo;
    setAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocateInfo.pNext = nullptr;
    setAllocateInfo.descriptorPool = _descriptorPool;
    setAllocateInfo.pSetLayouts = &_descriptorSetLayout;
    setAllocateInfo.descriptorSetCount = 1U;

    result = renderer.CheckVkResult ( vkAllocateDescriptorSets ( device, &setAllocateInfo, &_descriptorSet ),
        "Game::CreateDescriptorSet",
        "Can't allocate descriptor set"
    );

    if ( !result )
        return false;

    auto selector = [ this ] ( const Texture2D &texture ) -> VkSampler {
        const uint8_t mips = texture.GetMipLevelCount ();

        if ( mips == 9U )
            return _sampler09Mips;

        if ( mips == 10U )
            return _sampler10Mips;

        if ( mips == 11U )
            return _sampler11Mips;

        assert ( !"Game::CreateDescriptorSet::selector - Can't select sampler" );
        return VK_NULL_HANDLE;
    };

    const Texture2D& target = _material1Diffuse;

    VkDescriptorImageInfo imageInfo;
    imageInfo.sampler = selector ( target );
    imageInfo.imageView = target.GetImageView ();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writeSet;
    writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSet.pNext = nullptr;
    writeSet.dstSet = _descriptorSet;
    writeSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeSet.dstBinding = 0U;
    writeSet.dstArrayElement = 0U;
    writeSet.descriptorCount = 1U;
    writeSet.pImageInfo = &imageInfo;
    writeSet.pBufferInfo = nullptr;
    writeSet.pTexelBufferView = nullptr;

    vkUpdateDescriptorSets ( device, 1U, &writeSet, 0U, nullptr );
    return true;
}

void Game::DestroyDescriptorSet ( android_vulkan::Renderer &renderer )
{
    if ( _descriptorPool == VK_NULL_HANDLE )
        return;

    vkDestroyDescriptorPool ( renderer.GetDevice (), _descriptorPool, nullptr );
    _descriptorPool = VK_NULL_HANDLE;
    AV_UNREGISTER_DESCRIPTOR_POOL ( "Game::_descriptorPool" )

    _descriptorSet = VK_NULL_HANDLE;
}

bool Game::CreateMeshes ( android_vulkan::Renderer &renderer, VkCommandBuffer* commandBuffers )
{
    constexpr VertexInfo quad[ 4U ] =
    {
        VertexInfo ( -1.0F, -1.0F, 0.5f, 1.0F, 0.0F, 0.0F ),
        VertexInfo ( 1.0F, -1.0F, 0.5f, 1.0F, 1.0F, 0.0F ),
        VertexInfo ( -1.0F, 1.0F, 0.5f, 1.0F, 0.0F, 1.0F ),
        VertexInfo ( 1.0F, 1.0F, 0.5f, 1.0F, 1.0F, 1.0F )
    };

    return _mesh.LoadMesh ( reinterpret_cast<const uint8_t*> ( quad ),
        sizeof ( quad ),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        renderer,
        commandBuffers[ 0U ]
    );
}

void Game::DestroyMeshes ( android_vulkan::Renderer &renderer )
{
    _mesh.FreeResources ( renderer );
}

bool Game::CreatePipeline ( android_vulkan::Renderer &renderer )
{
    VkPipelineShaderStageCreateInfo stageInfo[ 2U ];
    VkPipelineShaderStageCreateInfo& vertexStage = stageInfo[ 0U ];
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.pNext = nullptr;
    vertexStage.flags = 0U;
    vertexStage.pSpecializationInfo = nullptr;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = _vertexShaderModule;
    vertexStage.pName = VERTEX_SHADER_ENTRY_POINT;

    VkPipelineShaderStageCreateInfo& fragmentStage = stageInfo[ 1U ];
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.pNext = nullptr;
    fragmentStage.flags = 0U;
    fragmentStage.pSpecializationInfo = nullptr;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = _fragmentShaderModule;
    fragmentStage.pName = FRAGMENT_SHADER_ENTRY_POINT;

    VkPipelineInputAssemblyStateCreateInfo assemblyInfo;
    assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assemblyInfo.pNext = nullptr;
    assemblyInfo.flags = 0U;
    assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    assemblyInfo.primitiveRestartEnable = VK_FALSE;

    VkVertexInputAttributeDescription attributeDescriptions[ 2U ];
    VkVertexInputAttributeDescription& vertexDescription = attributeDescriptions[ 0U ];
    vertexDescription.location = 0U;
    vertexDescription.binding = 0U;
    vertexDescription.offset = static_cast<uint32_t> ( offsetof ( VertexInfo, _vx ) );
    vertexDescription.format = VK_FORMAT_R32G32B32A32_SFLOAT;

    VkVertexInputAttributeDescription& uvDescription = attributeDescriptions[ 1U ];
    uvDescription.location = 1U;
    uvDescription.binding = 0U;
    uvDescription.offset = static_cast<uint32_t> ( offsetof ( VertexInfo, _tu ) );
    uvDescription.format = VK_FORMAT_R32G32_SFLOAT;

    VkVertexInputBindingDescription bindingDescription;
    bindingDescription.binding = 0U;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindingDescription.stride = sizeof ( VertexInfo );

    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.flags = 0U;
    vertexInputInfo.vertexAttributeDescriptionCount = 2U;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;
    vertexInputInfo.vertexBindingDescriptionCount = 1U;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;

    VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
    depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilInfo.pNext = nullptr;
    depthStencilInfo.flags = 0U;
    depthStencilInfo.stencilTestEnable = VK_FALSE;
    depthStencilInfo.depthTestEnable = VK_TRUE;
    depthStencilInfo.depthBoundsTestEnable = VK_TRUE;
    depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencilInfo.depthWriteEnable = VK_TRUE;
    depthStencilInfo.minDepthBounds = 0.0F;
    depthStencilInfo.maxDepthBounds = 1.0F;

    VkPipelineColorBlendAttachmentState attachmentInfo;
    attachmentInfo.blendEnable = VK_FALSE;

    attachmentInfo.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blendInfo;
    blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendInfo.pNext = nullptr;
    blendInfo.flags = 0U;
    blendInfo.attachmentCount = 1U;
    blendInfo.pAttachments = &attachmentInfo;
    blendInfo.logicOpEnable = VK_FALSE;
    memset ( blendInfo.blendConstants, 0, sizeof ( blendInfo.blendConstants ) );

    VkPipelineMultisampleStateCreateInfo multisampleInfo;
    multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleInfo.pNext = nullptr;
    multisampleInfo.flags = 0U;
    multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampleInfo.alphaToCoverageEnable = VK_FALSE;
    multisampleInfo.alphaToOneEnable = VK_FALSE;
    multisampleInfo.sampleShadingEnable = VK_FALSE;
    multisampleInfo.pSampleMask = nullptr;

    VkPipelineRasterizationStateCreateInfo rasterizationInfo;
    rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationInfo.pNext = nullptr;
    rasterizationInfo.flags = 0U;
    rasterizationInfo.depthBiasEnable = VK_FALSE;
    rasterizationInfo.depthClampEnable = VK_FALSE;
    rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
    rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    rasterizationInfo.lineWidth = 1.0F;

    const VkExtent2D& surfaceSize = renderer.GetSurfaceSize ();

    VkRect2D scissor;
    scissor.extent = surfaceSize;
    memset ( &scissor.offset, 0, sizeof ( scissor.offset ) );

    VkViewport viewport;
    viewport.x = viewport.y = 0.0F;
    viewport.width = static_cast<float> ( surfaceSize.width );
    viewport.height = static_cast<float> ( surfaceSize.height );
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;

    VkPipelineViewportStateCreateInfo viewportInfo;
    viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportInfo.pNext = nullptr;
    viewportInfo.flags = 0U;
    viewportInfo.viewportCount = 1U;
    viewportInfo.pViewports = &viewport;
    viewportInfo.scissorCount = 1U;
    viewportInfo.pScissors = &scissor;

    VkGraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = nullptr;
    pipelineInfo.flags = 0U;
    pipelineInfo.subpass = 0U;
    pipelineInfo.stageCount = 2U;
    pipelineInfo.pStages = stageInfo;
    pipelineInfo.renderPass = _renderPass;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = _pipelineLayout;
    pipelineInfo.basePipelineIndex = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.pInputAssemblyState = &assemblyInfo;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pTessellationState = nullptr;
    pipelineInfo.pDepthStencilState = &depthStencilInfo;
    pipelineInfo.pRasterizationState = &rasterizationInfo;
    pipelineInfo.pViewportState = &viewportInfo;
    pipelineInfo.pColorBlendState = &blendInfo;
    pipelineInfo.pMultisampleState = &multisampleInfo;

    const bool result = renderer.CheckVkResult (
        vkCreateGraphicsPipelines ( renderer.GetDevice (), VK_NULL_HANDLE, 1U, &pipelineInfo, nullptr, &_pipeline ),
        "Game::CreatePipeline",
        "Can't create pipeline"
    );

    if ( !result )
        return false;

    AV_REGISTER_PIPELINE ( "Game::_pipeline" )
    return true;
}

void Game::DestroyPipeline ( android_vulkan::Renderer &renderer )
{
    if ( _pipeline == VK_NULL_HANDLE )
        return;

    vkDestroyPipeline ( renderer.GetDevice (), _pipeline, nullptr );
    _pipeline = VK_NULL_HANDLE;
    AV_UNREGISTER_PIPELINE ( "Game::_pipeline" )
}

bool Game::CreatePipelineLayout ( android_vulkan::Renderer &renderer )
{
    VkDescriptorSetLayoutBinding bindingInfo;
    bindingInfo.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindingInfo.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindingInfo.descriptorCount = 1U;
    bindingInfo.binding = 0U;
    bindingInfo.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo descriptorSetInfo;
    descriptorSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetInfo.pNext = nullptr;
    descriptorSetInfo.flags = 0U;
    descriptorSetInfo.bindingCount = 1U;
    descriptorSetInfo.pBindings = &bindingInfo;

    const VkDevice device = renderer.GetDevice ();

    bool result = renderer.CheckVkResult (
        vkCreateDescriptorSetLayout ( device, &descriptorSetInfo, nullptr, &_descriptorSetLayout ),
        "Game::CreatePipelineLayout",
        "Can't create descriptor set layout"
    );

    if ( !result )
        return false;

    AV_REGISTER_DESCRIPTOR_SET_LAYOUT ( "Game::_descriptorSetLayout" )

    VkPipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pNext = nullptr;
    pipelineLayoutInfo.flags = 0U;
    pipelineLayoutInfo.pushConstantRangeCount = 0U;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;
    pipelineLayoutInfo.setLayoutCount = 1U;
    pipelineLayoutInfo.pSetLayouts = &_descriptorSetLayout;

    result = renderer.CheckVkResult ( vkCreatePipelineLayout ( device, &pipelineLayoutInfo, nullptr, &_pipelineLayout ),
        "Game::CreatePipelineLayout",
        "Can't create pipeline layout"
    );

    if ( !result )
        return false;

    AV_REGISTER_PIPELINE_LAYOUT ( "Game::_pipelineLayout" )
    return true;
}

void Game::DestroyPipelineLayout ( android_vulkan::Renderer &renderer )
{
    const VkDevice device = renderer.GetDevice ();

    if ( _descriptorSetLayout != VK_NULL_HANDLE )
    {
        vkDestroyDescriptorSetLayout ( device, _descriptorSetLayout, nullptr );
        _descriptorSetLayout = VK_NULL_HANDLE;
        AV_UNREGISTER_DESCRIPTOR_SET_LAYOUT ( "Game::_descriptorSetLayout" )
    }

    if ( _pipelineLayout == VK_NULL_HANDLE )
        return;

    vkDestroyPipelineLayout ( device, _pipelineLayout, nullptr );
    _pipelineLayout = VK_NULL_HANDLE;
    AV_UNREGISTER_PIPELINE_LAYOUT ( "Game::_pipelineLayout" )
}

bool Game::CreateRenderPass ( android_vulkan::Renderer &renderer )
{
    VkAttachmentDescription attachmentInfo[ 2U ];
    VkAttachmentDescription& colorAttachment = attachmentInfo[ 0U ];
    colorAttachment.format = renderer.GetSurfaceFormat ();
    colorAttachment.flags = 0U;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkAttachmentDescription& depthStencilAttachment = attachmentInfo[ 1U ];
    depthStencilAttachment.format = renderer.GetDefaultDepthStencilFormat ();
    depthStencilAttachment.flags = 0U;
    depthStencilAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthStencilAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthStencilAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthStencilAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthStencilAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    VkAttachmentReference colorReference;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorReference.attachment = 0U;

    VkAttachmentReference depthStencilReference;
    depthStencilReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthStencilReference.attachment = 1U;

    VkSubpassDescription subpassInfo;
    subpassInfo.flags = 0U;
    subpassInfo.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassInfo.colorAttachmentCount = 1U;
    subpassInfo.pColorAttachments = &colorReference;
    subpassInfo.pDepthStencilAttachment = &depthStencilReference;
    subpassInfo.preserveAttachmentCount = 0U;
    subpassInfo.pPreserveAttachments = nullptr;
    subpassInfo.inputAttachmentCount = 0U;
    subpassInfo.pInputAttachments = nullptr;
    subpassInfo.pResolveAttachments = nullptr;

    VkRenderPassCreateInfo renderPassInfo;
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.pNext = nullptr;
    renderPassInfo.flags = 0U;
    renderPassInfo.attachmentCount = 2U;
    renderPassInfo.pAttachments = attachmentInfo;
    renderPassInfo.dependencyCount = 0U;
    renderPassInfo.pDependencies = nullptr;
    renderPassInfo.subpassCount = 1U;
    renderPassInfo.pSubpasses = &subpassInfo;

    const bool result = renderer.CheckVkResult (
        vkCreateRenderPass ( renderer.GetDevice (), &renderPassInfo, nullptr, &_renderPass ),
        "Game::CreateRenderPass",
        "Can't create render pass"
    );

    if ( !result )
        return false;

    AV_REGISTER_RENDER_PASS ( "Game::_renderPass" )
    return true;
}

void Game::DestroyRenderPass ( android_vulkan::Renderer &renderer )
{
    if ( _renderPass == VK_NULL_HANDLE )
        return;

    vkDestroyRenderPass ( renderer.GetDevice (), _renderPass, nullptr );
    _renderPass = VK_NULL_HANDLE;
    AV_UNREGISTER_RENDER_PASS ( "Game::_renderPass" )
}

bool Game::CreateSamplers ( android_vulkan::Renderer &renderer )
{
    VkSamplerCreateInfo samplerInfo;
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.pNext = nullptr;
    samplerInfo.flags = 0U;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.mipLodBias = 0.0F;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0F;
    samplerInfo.maxLod = 8.0F;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0F;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    bool result = renderer.CheckVkResult (
        vkCreateSampler ( renderer.GetDevice (), &samplerInfo, nullptr, &_sampler09Mips ),
        "Game::CreateSamplers",
        "Can't create sampler with 9 mips"
    );

    if ( !result )
        return false;

    AV_REGISTER_SAMPLER ( "Game::_sampler09Mips" )

    samplerInfo.maxLod = 9.0F;

    result = renderer.CheckVkResult (
        vkCreateSampler ( renderer.GetDevice (), &samplerInfo, nullptr, &_sampler10Mips ),
        "Game::CreateSamplers",
        "Can't create sampler with 10 mips"
    );

    if ( !result )
        return false;

    AV_REGISTER_SAMPLER ( "Game::_sampler10Mips" )

    samplerInfo.maxLod = 10.0F;

    result = renderer.CheckVkResult (
        vkCreateSampler ( renderer.GetDevice (), &samplerInfo, nullptr, &_sampler11Mips ),
        "Game::CreateSamplers",
        "Can't create sampler with 11 mips"
    );

    if ( !result )
        return false;

    AV_REGISTER_SAMPLER ( "Game::_sampler11Mips" )
    return true;
}

void Game::DestroySamplers ( android_vulkan::Renderer &renderer )
{
    if ( _sampler11Mips != VK_NULL_HANDLE )
    {
        vkDestroySampler ( renderer.GetDevice (), _sampler11Mips, nullptr );
        _sampler11Mips = VK_NULL_HANDLE;
        AV_UNREGISTER_SAMPLER ( "Game::_sampler11Mips" )
    }

    if ( _sampler10Mips != VK_NULL_HANDLE )
    {
        vkDestroySampler ( renderer.GetDevice (), _sampler10Mips, nullptr );
        _sampler10Mips = VK_NULL_HANDLE;
        AV_UNREGISTER_SAMPLER ( "Game::_sampler10Mips" )
    }

    if ( _sampler09Mips == VK_NULL_HANDLE )
        return;

    vkDestroySampler ( renderer.GetDevice (), _sampler09Mips, nullptr );
    _sampler09Mips = VK_NULL_HANDLE;
    AV_UNREGISTER_SAMPLER ( "Game::_sampler09Mips" )
}

bool Game::CreateShaderModules ( android_vulkan::Renderer &renderer )
{
    bool result = renderer.CreateShader ( _vertexShaderModule,
        VERTEX_SHADER,
        "Can't create vertex shader (Game::CreateShaderModules)"
    );

    if ( !result )
        return false;

    AV_REGISTER_SHADER_MODULE ( "Game::_vertexShaderModule" )

    result = renderer.CreateShader ( _fragmentShaderModule,
        FRAGMENT_SHADER,
        "Can't create fragment shader (Game::CreateShaderModules)"
    );

    if ( !result )
        return false;

    AV_REGISTER_SHADER_MODULE ( "Game::_fragmentShaderModule" )
    return true;
}

void Game::DestroyShaderModules ( android_vulkan::Renderer &renderer )
{
    const VkDevice device = renderer.GetDevice ();

    if ( _fragmentShaderModule != VK_NULL_HANDLE )
    {
        vkDestroyShaderModule ( device, _fragmentShaderModule, nullptr );
        _fragmentShaderModule = VK_NULL_HANDLE;
        AV_UNREGISTER_SHADER_MODULE ( "Game::_fragmentShaderModule" )
    }

    if ( _vertexShaderModule == VK_NULL_HANDLE )
        return;

    vkDestroyShaderModule ( device, _vertexShaderModule, nullptr );
    _vertexShaderModule = VK_NULL_HANDLE;
    AV_UNREGISTER_SHADER_MODULE ( "Game::_vertexShaderModule" )
}

bool Game::CreateSyncPrimitives ( android_vulkan::Renderer &renderer )
{
    const VkDevice device = renderer.GetDevice ();

    VkSemaphoreCreateInfo semaphoreInfo;
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = nullptr;
    semaphoreInfo.flags = 0U;

    bool result = renderer.CheckVkResult ( vkCreateSemaphore ( device, &semaphoreInfo, nullptr, &_renderPassEndSemaphore ),
        "Game::CreateSyncPrimitives",
        "Can't create render pass end semaphore"
    );

    if ( !result )
        return false;

    AV_REGISTER_SEMAPHORE ( "Game::_renderPassEndSemaphore" )

    result = renderer.CheckVkResult (
        vkCreateSemaphore ( device, &semaphoreInfo, nullptr, &_renderTargetAcquiredSemaphore ),
        "Game::CreateSyncPrimitives",
        "Can't create render target acquired semaphore"
    );

    if ( !result )
        return false;

    AV_REGISTER_SEMAPHORE ( "Game::_renderTargetAcquiredSemaphore" )
    return true;
}

void Game::DestroySyncPrimitives ( android_vulkan::Renderer &renderer )
{
    const VkDevice device = renderer.GetDevice ();

    if ( _renderTargetAcquiredSemaphore != VK_NULL_HANDLE )
    {
        vkDestroySemaphore ( device, _renderTargetAcquiredSemaphore, nullptr );
        _renderTargetAcquiredSemaphore = VK_NULL_HANDLE;
        AV_UNREGISTER_SEMAPHORE ( "Game::_renderTargetAcquiredSemaphore" )
    }

    if ( _renderPassEndSemaphore == VK_NULL_HANDLE )
        return;

    vkDestroySemaphore ( device, _renderPassEndSemaphore, nullptr );
    _renderPassEndSemaphore = VK_NULL_HANDLE;
    AV_UNREGISTER_SEMAPHORE ( "Game::_renderPassEndSemaphore" )
}

bool Game::CreateTextures ( android_vulkan::Renderer &renderer, VkCommandBuffer* commandBuffers )
{
    bool result = _material1Diffuse.UploadData ( MATERIAL_1_DIFFUSE,
        VK_FORMAT_R8G8B8A8_SRGB,
        renderer,
        commandBuffers[ 0U ]
    );

    if ( !result )
        return false;

    result = _material2Diffuse.UploadData ( MATERIAL_2_DIFFUSE,
        VK_FORMAT_R8G8B8A8_SRGB,
        renderer,
        commandBuffers[ 1U ]
    );

    if ( !result )
        return false;

    result = _material2Normal.UploadData ( MATERIAL_2_NORMAL,
        VK_FORMAT_R8G8B8A8_SRGB,
        renderer,
        commandBuffers[ 2U ]
    );

    if ( !result )
        return false;

    result = _material3Diffuse.UploadData ( MATERIAL_3_DIFFUSE,
        VK_FORMAT_R8G8B8A8_SRGB,
        renderer,
        commandBuffers[ 3U ]
    );

    if ( !result )
        return false;

    return _material3Normal.UploadData ( MATERIAL_3_NORMAL,
        VK_FORMAT_R8G8B8A8_SRGB,
        renderer,
        commandBuffers[ 4U ]
    );
}

void Game::DestroyTextures ( android_vulkan::Renderer &renderer )
{
    renderer.CheckVkResult ( vkQueueWaitIdle ( renderer.GetQueue () ),
        "Game::DestroyTextures",
        "Can't wait queue idle"
    );

    _material3Normal.FreeResources ( renderer );
    _material3Diffuse.FreeResources ( renderer );
    _material2Normal.FreeResources ( renderer );
    _material2Diffuse.FreeResources ( renderer );
    _material1Diffuse.FreeResources ( renderer );
}

bool Game::InitCommandBuffers ( android_vulkan::Renderer &renderer )
{
    const size_t framebuffers = renderer.GetPresentFramebufferCount ();
    _commandBuffers.resize ( framebuffers );
    VkCommandBuffer* commandBuffers = _commandBuffers.data ();

    VkCommandBufferAllocateInfo allocateInfo;
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = static_cast<uint32_t> ( framebuffers );
    allocateInfo.commandPool = _commandPool;

    const VkDevice device = renderer.GetDevice ();

    bool result = renderer.CheckVkResult ( vkAllocateCommandBuffers ( device, &allocateInfo, commandBuffers ),
        "Game::InitCommandBuffers",
        "Can't allocate command buffer"
    );

    if ( !result )
        return false;

    VkCommandBufferBeginInfo bufferBeginInfo;
    bufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bufferBeginInfo.pNext = nullptr;
    bufferBeginInfo.flags = 0U;
    bufferBeginInfo.pInheritanceInfo = nullptr;

    VkClearValue clearValues[ 2U ];
    VkClearValue& colorTarget = clearValues[ 0U ];
    memset ( &colorTarget.color, 0, sizeof ( colorTarget.color ) );

    VkClearValue& depthStencilTarget = clearValues[ 1U ];
    depthStencilTarget.depthStencil.depth = 1.0F;
    depthStencilTarget.depthStencil.stencil = 0x00000000U;

    VkRenderPassBeginInfo renderPassBeginInfo;
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassBeginInfo.pNext = nullptr;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent = renderer.GetSurfaceSize ();
    renderPassBeginInfo.renderPass = _renderPass;
    renderPassBeginInfo.clearValueCount = 2U;
    renderPassBeginInfo.pClearValues = clearValues;

    for ( size_t i = 0U; i < framebuffers; ++i )
    {
        const VkCommandBuffer commandBuffer = commandBuffers[ i ];

        result = renderer.CheckVkResult ( vkBeginCommandBuffer ( commandBuffer, &bufferBeginInfo ),
            "Game::InitCommandBuffers",
            "Can't begin command buffer"
        );

        if ( !result )
            return false;

        renderPassBeginInfo.framebuffer = renderer.GetPresentFramebuffer ( static_cast<uint32_t> ( i ) );
        vkCmdBeginRenderPass ( commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

        vkCmdBindPipeline ( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline );

        vkCmdBindDescriptorSets ( commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            _pipelineLayout,
            0U,
            1U,
            &_descriptorSet,
            0U,
            nullptr
        );

        constexpr VkDeviceSize offset = 0U;
        vkCmdBindVertexBuffers ( commandBuffer, 0U, 1U, &_mesh.GetBuffer (), &offset );

        vkCmdDraw ( commandBuffer, 4U, 1U, 0U, 0U );
        vkCmdEndRenderPass ( commandBuffer );

        result = renderer.CheckVkResult ( vkEndCommandBuffer ( commandBuffer ),
            "Game::InitCommandBuffers",
            "Can't end command buffer"
        );

        if ( result )
            continue;

        return false;
    }

    return true;
}

bool Game::LoadGPUContent ( android_vulkan::Renderer &renderer )
{
    constexpr const size_t commandBufferCount = TEXTURE_COMMAND_BUFFERS + BUFFER_COMMAND_BUFFERS;
    VkCommandBuffer commandBuffers[ commandBufferCount ] = { VK_NULL_HANDLE };

    VkCommandBufferAllocateInfo allocateInfo;
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.commandPool = _commandPool;
    allocateInfo.commandBufferCount = static_cast<uint32_t> ( commandBufferCount );
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    const VkDevice device = renderer.GetDevice ();

    bool result = renderer.CheckVkResult (
        vkAllocateCommandBuffers ( renderer.GetDevice (), &allocateInfo, commandBuffers ),
        "Game::LoadGPUContent",
        "Can't allocate command buffers"
    );

    if ( !result )
        return false;

    if ( !CreateTextures ( renderer, commandBuffers ) )
        return false;

    if ( !CreateMeshes ( renderer, commandBuffers + TEXTURE_COMMAND_BUFFERS ) )
        return false;

    result = renderer.CheckVkResult ( vkQueueWaitIdle ( renderer.GetQueue () ),
        "Game::LoadGPUContent",
        "Can't run upload commands"
    );

    if ( !result )
        return false;

    _mesh.FreeTransferResources ( renderer );

    _material3Normal.FreeTransferResources ( renderer );
    _material3Diffuse.FreeTransferResources ( renderer );
    _material2Normal.FreeTransferResources ( renderer );
    _material2Diffuse.FreeTransferResources ( renderer );
    _material1Diffuse.FreeTransferResources ( renderer );

    vkFreeCommandBuffers ( device, _commandPool, allocateInfo.commandBufferCount, commandBuffers );
    return true;
}

} // namespace rotating_mesh
