#include <pbr/opaque_batch_program.h>
#include <vulkan_utils.h>
#include <vertex_info.h>


namespace pbr {

constexpr static const char* VERTEX_SHADER = "shaders/common-opaque-batch-vs.spv";
constexpr static const char* FRAGMENT_SHADER = "shaders/opaque-ps.spv";

constexpr static const size_t COLOR_RENDER_TARGET_COUNT = 4U;
constexpr static const size_t STAGE_COUNT = 2U;
constexpr static const size_t VERTEX_ATTRIBUTE_COUNT = 5U;

//----------------------------------------------------------------------------------------------------------------------

OpaqueBatchProgram::OpaqueBatchProgram ():
    Program ( "OpaqueBatchProgram" )
{
    // NOTHING
}

bool OpaqueBatchProgram::Init ( android_vulkan::Renderer &renderer,
    VkRenderPass renderPass,
    VkExtent2D const &viewport
)
{
    VkPipelineInputAssemblyStateCreateInfo assemblyInfo;
    VkPipelineColorBlendAttachmentState attachmentInfo[ COLOR_RENDER_TARGET_COUNT ];
    VkVertexInputAttributeDescription attributeDescriptions[ VERTEX_ATTRIBUTE_COUNT ];
    VkVertexInputBindingDescription bindingDescription;
    VkPipelineColorBlendStateCreateInfo blendInfo;
    VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
    VkPipelineMultisampleStateCreateInfo multisampleInfo;
    VkPipelineRasterizationStateCreateInfo rasterizationInfo;
    VkRect2D scissorDescription;
    VkPipelineShaderStageCreateInfo stageInfo[ STAGE_COUNT ];
    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    VkViewport viewportDescription;
    VkPipelineViewportStateCreateInfo viewportInfo;

    VkGraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = nullptr;
    pipelineInfo.flags = 0U;
    pipelineInfo.stageCount = static_cast<uint32_t> ( std::size ( stageInfo ) );

    if ( !InitShaderInfo ( pipelineInfo.pStages, stageInfo, renderer ) )
    {
        Destroy ( renderer );
        return false;
    }

    pipelineInfo.pVertexInputState = InitVertexInputInfo ( vertexInputInfo,
        attributeDescriptions,
        &bindingDescription
    );

    pipelineInfo.pInputAssemblyState = InitInputAssemblyInfo ( assemblyInfo );
    pipelineInfo.pTessellationState = nullptr;
    pipelineInfo.pViewportState = InitViewportInfo ( viewportInfo, scissorDescription, viewportDescription, viewport );
    pipelineInfo.pRasterizationState = InitRasterizationInfo ( rasterizationInfo );
    pipelineInfo.pMultisampleState = InitMultisampleInfo ( multisampleInfo );
    pipelineInfo.pDepthStencilState = InitDepthStencilInfo ( depthStencilInfo );
    pipelineInfo.pColorBlendState = InitColorBlendInfo ( blendInfo, attachmentInfo );
    pipelineInfo.pDynamicState = nullptr;

    if ( !InitLayout ( pipelineInfo.layout, renderer ) )
    {
        Destroy ( renderer );
        return false;
    }

    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0U;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = 0;

    const bool result = renderer.CheckVkResult (
        vkCreateGraphicsPipelines ( renderer.GetDevice (), VK_NULL_HANDLE, 1U, &pipelineInfo, nullptr, &_pipeline ),
        "OpaqueBatchProgram::Init",
        "Can't create pipeline"
    );

    if ( !result )
    {
        Destroy ( renderer );
        return false;
    }

    AV_REGISTER_PIPELINE ( "OpaqueBatchProgram::_pipeline" )
    return true;
}

void OpaqueBatchProgram::Destroy ( android_vulkan::Renderer &renderer )
{
    VkDevice device = renderer.GetDevice ();

    if ( _pipeline != VK_NULL_HANDLE )
    {
        vkDestroyPipeline ( device, _pipeline, nullptr );
        _pipeline = VK_NULL_HANDLE;
        AV_UNREGISTER_PIPELINE ( "OpaqueBatchProgram::_pipeline" )
    }

    if ( _fragmentShader != VK_NULL_HANDLE )
    {
        vkDestroyShaderModule ( device, _fragmentShader, nullptr );
        _fragmentShader = VK_NULL_HANDLE;
        AV_UNREGISTER_SHADER_MODULE ( "OpaqueBatchProgram::_fragmentShader" )
    }

    if ( _vertexShader == VK_NULL_HANDLE )
        return;

    vkDestroyShaderModule ( device, _vertexShader, nullptr );
    _vertexShader = VK_NULL_HANDLE;
    AV_UNREGISTER_SHADER_MODULE ( "OpaqueBatchProgram::_vertexShader" )
}

std::vector<ProgramResource> const& OpaqueBatchProgram::GetResourceInfo () const
{
    // TODO
    static std::vector<ProgramResource> resources;
    return resources;
}

VkPipelineColorBlendStateCreateInfo const* OpaqueBatchProgram::InitColorBlendInfo (
    VkPipelineColorBlendStateCreateInfo &info,
    VkPipelineColorBlendAttachmentState* attachments
) const
{
    VkPipelineColorBlendAttachmentState& albedo = attachments[ 0U ];
    albedo.blendEnable = VK_FALSE;
    albedo.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    albedo.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    albedo.colorBlendOp = VK_BLEND_OP_ADD;
    albedo.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    albedo.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    albedo.alphaBlendOp = VK_BLEND_OP_ADD;

    albedo.colorWriteMask =
        AV_VK_FLAG ( VK_COLOR_COMPONENT_R_BIT ) |
        AV_VK_FLAG ( VK_COLOR_COMPONENT_G_BIT ) |
        AV_VK_FLAG ( VK_COLOR_COMPONENT_B_BIT ) |
        AV_VK_FLAG ( VK_COLOR_COMPONENT_A_BIT );

    constexpr const auto limit = static_cast<const ptrdiff_t> ( COLOR_RENDER_TARGET_COUNT );

    for ( ptrdiff_t i = 1; i < limit; ++i )
        memcpy ( attachments + i, &albedo, sizeof ( albedo ) );

    info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0U;
    info.logicOpEnable = VK_FALSE;
    info.logicOp = VK_LOGIC_OP_NO_OP;
    info.attachmentCount = static_cast<uint32_t> ( COLOR_RENDER_TARGET_COUNT );
    info.pAttachments = attachments;
    memset ( info.blendConstants, 0, sizeof ( info.blendConstants ) );

    return &info;
}

VkPipelineDepthStencilStateCreateInfo const* OpaqueBatchProgram::InitDepthStencilInfo (
    VkPipelineDepthStencilStateCreateInfo &info
) const
{
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0U;
    info.depthTestEnable = VK_TRUE;
    info.depthWriteEnable = VK_TRUE;
    info.depthCompareOp = VK_COMPARE_OP_LESS;
    info.depthBoundsTestEnable = VK_FALSE;
    info.stencilTestEnable = VK_FALSE;

    info.front.failOp = VK_STENCIL_OP_KEEP;
    info.front.passOp = VK_STENCIL_OP_KEEP;
    info.front.depthFailOp = VK_STENCIL_OP_KEEP;
    info.front.compareOp = VK_COMPARE_OP_ALWAYS;
    info.front.compareMask = UINT32_MAX;
    info.front.writeMask = 0x00U;
    info.front.reference = UINT32_MAX;
    memcpy ( &info.back, &info.front, sizeof ( info.back ) );

    info.minDepthBounds = 0.0F;
    info.maxDepthBounds = 1.0F;

    return &info;
}

VkPipelineInputAssemblyStateCreateInfo const* OpaqueBatchProgram::InitInputAssemblyInfo (
    VkPipelineInputAssemblyStateCreateInfo &info
) const
{
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0U;
    info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    info.primitiveRestartEnable = VK_FALSE;

    return &info;
}

bool OpaqueBatchProgram::InitLayout ( VkPipelineLayout &/*layout*/, android_vulkan::Renderer &/*renderer*/ )
{
    // TODO
    return false;
}

VkPipelineMultisampleStateCreateInfo const* OpaqueBatchProgram::InitMultisampleInfo (
    VkPipelineMultisampleStateCreateInfo &info
) const
{
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0U;
    info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    info.sampleShadingEnable = VK_FALSE;
    info.minSampleShading = 0.0F;
    info.pSampleMask = nullptr;
    info.alphaToCoverageEnable = VK_FALSE;
    info.alphaToOneEnable = VK_FALSE;

    return &info;
}

VkPipelineRasterizationStateCreateInfo const* OpaqueBatchProgram::InitRasterizationInfo (
    VkPipelineRasterizationStateCreateInfo &info
) const
{
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0U;
    info.depthClampEnable = VK_FALSE;
    info.rasterizerDiscardEnable = VK_FALSE;
    info.polygonMode = VK_POLYGON_MODE_FILL;
    info.cullMode = VK_CULL_MODE_BACK_BIT;
    info.frontFace = VK_FRONT_FACE_CLOCKWISE;
    info.depthBiasEnable = VK_FALSE;
    info.depthBiasConstantFactor = 0.0F;
    info.depthBiasClamp = 0.0F;
    info.depthBiasSlopeFactor = 0.0F;
    info.lineWidth = 1.0F;

    return &info;
}

bool OpaqueBatchProgram::InitShaderInfo ( VkPipelineShaderStageCreateInfo const* &targetInfo,
    VkPipelineShaderStageCreateInfo* sourceInfo,
    android_vulkan::Renderer &renderer
)
{
    bool result = renderer.CreateShader ( _vertexShader,
        VERTEX_SHADER,
        "Can't create vertex shader (pbr::OpaqueBatchProgram)"
    );

    if ( !result )
        return false;

    AV_REGISTER_SHADER_MODULE ( "OpaqueBatchProgram::_vertexShader" )

    result = renderer.CreateShader ( _fragmentShader,
        FRAGMENT_SHADER,
        "Can't create fragment shader (pbr::OpaqueBatchProgram)"
    );

    if ( !result )
        return false;

    AV_REGISTER_SHADER_MODULE ( "OpaqueBatchProgram::_fragmentShader" )

    VkPipelineShaderStageCreateInfo& vertexStage = sourceInfo[ 0U ];
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.pNext = nullptr;
    vertexStage.flags = 0U;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = _vertexShader;
    vertexStage.pName = VERTEX_SHADER_ENTRY_POINT;
    vertexStage.pSpecializationInfo = nullptr;

    VkPipelineShaderStageCreateInfo& fragmentStage = sourceInfo[ 1U ];
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.pNext = nullptr;
    fragmentStage.flags = 0U;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = _fragmentShader;
    fragmentStage.pName = FRAGMENT_SHADER_ENTRY_POINT;
    fragmentStage.pSpecializationInfo = nullptr;

    targetInfo = sourceInfo;
    return true;
}

VkPipelineViewportStateCreateInfo const* OpaqueBatchProgram::InitViewportInfo (
    VkPipelineViewportStateCreateInfo &info,
    VkRect2D &scissorInfo,
    VkViewport &viewportInfo,
    VkExtent2D const &viewport
) const
{
    viewportInfo.x = 0.0F;
    viewportInfo.y = 0.0F;
    viewportInfo.width = static_cast<float> ( viewport.width );
    viewportInfo.height = static_cast<float> ( viewport.height );
    viewportInfo.minDepth = 0.0F;
    viewportInfo.maxDepth = 1.0F;

    scissorInfo.offset.x = 0;
    scissorInfo.offset.y = 0;
    scissorInfo.extent = viewport;

    info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0U;
    info.viewportCount = 1U;
    info.pViewports = &viewportInfo;
    info.scissorCount = 1U;
    info.pScissors = &scissorInfo;

    return &info;
}

VkPipelineVertexInputStateCreateInfo const* OpaqueBatchProgram::InitVertexInputInfo (
    VkPipelineVertexInputStateCreateInfo &info,
    VkVertexInputAttributeDescription* attributes,
    VkVertexInputBindingDescription* binds
) const
{
    binds->binding = 0U;
    binds->stride = static_cast<uint32_t> ( sizeof ( android_vulkan::VertexInfo ) );
    binds->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription& vertex = attributes[ 0U ];
    vertex.location = 0U;
    vertex.binding = 0U;
    vertex.format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex.offset = static_cast<uint32_t> ( offsetof ( android_vulkan::VertexInfo, _vertex ) );

    VkVertexInputAttributeDescription& uv = attributes[ 1U ];
    uv.location = 1U;
    uv.binding = 0U;
    uv.format = VK_FORMAT_R32G32_SFLOAT;
    uv.offset = static_cast<uint32_t> ( offsetof ( android_vulkan::VertexInfo, _uv ) );

    VkVertexInputAttributeDescription& normal = attributes[ 2U ];
    normal.location = 2U;
    normal.binding = 0U;
    normal.format = VK_FORMAT_R32G32B32_SFLOAT;
    normal.offset = static_cast<uint32_t> ( offsetof ( android_vulkan::VertexInfo, _normal ) );

    VkVertexInputAttributeDescription& tangent = attributes[ 3U ];
    tangent.location = 3U;
    tangent.binding = 0U;
    tangent.format = VK_FORMAT_R32G32B32_SFLOAT;
    tangent.offset = static_cast<uint32_t> ( offsetof ( android_vulkan::VertexInfo, _tangent ) );

    VkVertexInputAttributeDescription& bitangent = attributes[ 4U ];
    bitangent.location = 4U;
    bitangent.binding = 0U;
    bitangent.format = VK_FORMAT_R32G32B32_SFLOAT;
    bitangent.offset = static_cast<uint32_t> ( offsetof ( android_vulkan::VertexInfo, _bitangent ) );

    info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0U;
    info.vertexBindingDescriptionCount = 1U;
    info.pVertexBindingDescriptions = binds;
    info.vertexAttributeDescriptionCount = static_cast<uint32_t> ( VERTEX_ATTRIBUTE_COUNT );
    info.pVertexAttributeDescriptions = attributes;

    return &info;
}

} // namespace pbr