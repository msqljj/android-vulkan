#include <rotating_mesh/uniform_buffer.h>

GX_DISABLE_COMMON_WARNINGS

#include <cassert>

GX_RESTORE_WARNING_STATE

#include <vulkan_utils.h>


namespace rotating_mesh {

UniformBuffer::UniformBuffer ():
    _size ( 0U ),
    _buffer ( VK_NULL_HANDLE ),
    _bufferMemory ( VK_NULL_HANDLE ),
    _commandBuffer ( VK_NULL_HANDLE ),
    _commandPool  ( VK_NULL_HANDLE ),
    _renderer ( nullptr ),
    _targetStages ( VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT ),
    _transfer ( VK_NULL_HANDLE ),
    _transferMemory ( VK_NULL_HANDLE )
{
    // NOTHING
}

void UniformBuffer::FreeResources ()
{
    const VkDevice device = _renderer->GetDevice ();

    if ( _transferMemory != VK_NULL_HANDLE )
    {
        vkFreeMemory ( device, _transferMemory, nullptr );
        _transferMemory = VK_NULL_HANDLE;
        AV_UNREGISTER_DEVICE_MEMORY ( "UniformBuffer::_transferMemory" )
    }

    if ( _transfer != VK_NULL_HANDLE )
    {
        vkDestroyBuffer ( device, _transfer, nullptr );
        _transfer = VK_NULL_HANDLE;
        AV_UNREGISTER_BUFFER ( "UniformBuffer::_transfer" )
    }

    if ( _bufferMemory != VK_NULL_HANDLE )
    {
        vkFreeMemory ( device, _bufferMemory, nullptr );
        _bufferMemory = VK_NULL_HANDLE;
        AV_UNREGISTER_DEVICE_MEMORY ( "UniformBuffer::_bufferMemory" )
    }

    if ( _buffer != VK_NULL_HANDLE )
    {
        vkDestroyBuffer ( device, _buffer, nullptr );
        _buffer = VK_NULL_HANDLE;
        AV_UNREGISTER_BUFFER ( "UniformBuffer::_buffer" )
    }

    if ( _commandBuffer != VK_NULL_HANDLE )
    {
        vkFreeCommandBuffers ( device, _commandPool, 1U, &_commandBuffer );
        _commandBuffer = VK_NULL_HANDLE;
    }

    _size = 0U;
    _commandPool = VK_NULL_HANDLE;
    _targetStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    _renderer = nullptr;
}

VkBuffer UniformBuffer::GetBuffer () const
{
    return _buffer;
}

bool UniformBuffer::Init ( android_vulkan::Renderer &renderer,
    VkCommandPool commandPool,
    VkPipelineStageFlags targetStages
)
{
    if ( _commandPool != VK_NULL_HANDLE )
        return true;

    _commandPool = commandPool;
    _renderer = &renderer;
    _targetStages = targetStages;

    VkCommandBufferAllocateInfo allocateInfo;
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.commandBufferCount = 1U;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandPool = commandPool;

    return renderer.CheckVkResult (
        vkAllocateCommandBuffers ( renderer.GetDevice (), &allocateInfo, &_commandBuffer ),
        "UniformBuffer::Init",
        "Can't allocate command buffer"
    );
}

bool UniformBuffer::Update ( const uint8_t* data, size_t size )
{
    assert ( size );

    if ( !_size && !InitResources ( size ) )
        return false;

    if ( !data )
        return true;

    const VkDevice device = _renderer->GetDevice ();
    void* dst = nullptr;

    bool result = _renderer->CheckVkResult ( vkMapMemory ( device, _transferMemory, 0U, size, 0U, &dst ),
        "UniformBuffer::Update",
        "Can't map transfer memory"
    );

    if ( !result )
        return false;

    memcpy ( dst, data, size );
    vkUnmapMemory ( device, _transferMemory );

    VkSubmitInfo submitInfo;
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;
    submitInfo.commandBufferCount = 1U;
    submitInfo.pCommandBuffers = &_commandBuffer;
    submitInfo.waitSemaphoreCount = 0U;
    submitInfo.pWaitSemaphores = nullptr;
    submitInfo.pWaitDstStageMask = nullptr;
    submitInfo.signalSemaphoreCount = 0U;
    submitInfo.pSignalSemaphores = nullptr;

    return _renderer->CheckVkResult ( vkQueueSubmit ( _renderer->GetQueue (), 1U, &submitInfo, VK_NULL_HANDLE ),
        "UniformBuffer::Update",
        "Can't submit upload command"
    );
}

bool UniformBuffer::InitResources ( size_t size )
{
    const VkDevice device = _renderer->GetDevice ();

    VkBufferCreateInfo bufferInfo;
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.flags = 0U;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.queueFamilyIndexCount = 0U;
    bufferInfo.pQueueFamilyIndices = nullptr;

    bool result = _renderer->CheckVkResult ( vkCreateBuffer ( device, &bufferInfo, nullptr, &_buffer ),
        "UniformBuffer::InitResources",
        "Can't create buffer"
    );

    if ( !result )
        return false;

    AV_REGISTER_BUFFER ( "UniformBuffer::_buffer" )

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements ( device, _buffer, &requirements );

    result = _renderer->TryAllocateMemory ( _bufferMemory,
        requirements,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        "Can't allocate buffer memory (UniformBuffer::InitResources)"
    );

    if ( !result )
    {
        FreeResources ();
        return false;
    }

    AV_REGISTER_DEVICE_MEMORY ( "UniformBuffer::_bufferMemory" )

    result = _renderer->CheckVkResult ( vkBindBufferMemory ( device, _buffer, _bufferMemory, 0U ),
        "UniformBuffer::InitResources",
        "Can't bind buffer memory"
    );

    if ( !result )
    {
        FreeResources ();
        return false;
    }

    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    result = _renderer->CheckVkResult ( vkCreateBuffer ( device, &bufferInfo, nullptr, &_transfer ),
        "UniformBuffer::InitResources",
        "Can't create transfer buffer"
    );

    if ( !result )
        return false;

    AV_REGISTER_BUFFER ( "UniformBuffer::_transfer" )
    vkGetBufferMemoryRequirements ( device, _transfer, &requirements );

    result = _renderer->TryAllocateMemory ( _transferMemory,
        requirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        "Can't allocate transfer memory (UniformBuffer::InitResources)"
    );

    if ( !result )
    {
        FreeResources ();
        return false;
    }

    AV_REGISTER_DEVICE_MEMORY ( "UniformBuffer::_transferMemory" )

    result = _renderer->CheckVkResult ( vkBindBufferMemory ( device, _transfer, _transferMemory, 0U ),
        "UniformBuffer::InitResources",
        "Can't bind transfer memory"
    );

    if ( !result )
    {
        FreeResources ();
        return false;
    }

    VkCommandBufferBeginInfo beginInfo;
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.pNext = nullptr;
    beginInfo.flags = 0U;
    beginInfo.pInheritanceInfo = nullptr;

    result = _renderer->CheckVkResult ( vkBeginCommandBuffer ( _commandBuffer, &beginInfo ),
        "UniformBuffer::InitResources",
        "Can't begin command buffer"
    );

    if ( !result )
    {
        FreeResources ();
        return false;
    }

    VkBufferMemoryBarrier barrier;
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.buffer = _buffer;
    barrier.size = size;
    barrier.offset = 0U;
    barrier.srcAccessMask = 0U;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    vkCmdPipelineBarrier ( _commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        0U,
        nullptr,
        1U,
        &barrier,
        0U,
        nullptr
    );

    VkBufferCopy copyInfo;
    copyInfo.size = size;
    copyInfo.srcOffset = copyInfo.dstOffset = 0U;

    vkCmdCopyBuffer ( _commandBuffer, _transfer, _buffer, 1U, &copyInfo );

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;

    vkCmdPipelineBarrier ( _commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        _targetStages,
        0U,
        0U,
        nullptr,
        1U,
        &barrier,
        0U,
        nullptr
    );

    result = _renderer->CheckVkResult ( vkEndCommandBuffer ( _commandBuffer ),
        "UniformBuffer::InitResources",
        "Can't end command buffer"
    );

    if ( result )
    {
        _size = size;
        return true;
    }

    FreeResources ();
    return false;
}

} // namespace rotating_mesh