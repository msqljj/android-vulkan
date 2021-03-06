#include <rotating_mesh/texture2D.h>

GX_DISABLE_COMMON_WARNINGS

#include <array>
#include <cassert>
#include <set>
#include <thread>

#define STBI_NO_FAILURE_STRINGS
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_HDR
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

GX_RESTORE_WARNING_STATE

#include <file.h>
#include <logger.h>
#include <vulkan_utils.h>


namespace rotating_mesh {

constexpr static const size_t EXPANDER_THREADS = 4U;
constexpr static const size_t RGB_BYTES_PER_PIXEL = 3U;
constexpr static const size_t RGBA_BYTES_PER_PIXEL = 4U;

static const std::map<VkFormat, std::set<VkFormat>> g_CompatibleFormats =
{
    {
        VK_FORMAT_R8G8B8A8_SRGB,

        {
            VK_FORMAT_R8G8B8A8_UNORM
        }
    },

    {
        VK_FORMAT_R8G8B8A8_UNORM,

        {
            VK_FORMAT_R8G8B8A8_SRGB
        }
    }
};

//----------------------------------------------------------------------------------------------------------------------

Texture2D::Texture2D ():
    _format ( VK_FORMAT_UNDEFINED ),
    _image ( VK_NULL_HANDLE ),
    _imageDeviceMemory ( VK_NULL_HANDLE ),
    _imageView ( VK_NULL_HANDLE ),
    _mipLevels ( 0U ),
    _resolution { .width = 0U, .height = 0U },
    _transfer ( VK_NULL_HANDLE ),
    _transferDeviceMemory ( VK_NULL_HANDLE )
{
    // NOTHING
}

Texture2D::Texture2D ( std::string &fileName, VkFormat format, bool isGenerateMipmaps ):
    _format ( format ),
    _image ( VK_NULL_HANDLE ),
    _imageDeviceMemory ( VK_NULL_HANDLE ),
    _imageView ( VK_NULL_HANDLE ),
    _isGenerateMipmaps ( isGenerateMipmaps ),
    _resolution { .width = 0U, .height = 0U },
    _transfer ( VK_NULL_HANDLE ),
    _transferDeviceMemory ( VK_NULL_HANDLE ),
    _fileName ( fileName )
{
    // NOTHING
}

Texture2D::Texture2D ( std::string &&fileName, VkFormat format, bool isGenerateMipmaps ):
    _format ( format ),
    _image ( VK_NULL_HANDLE ),
    _imageDeviceMemory ( VK_NULL_HANDLE ),
    _imageView ( VK_NULL_HANDLE ),
    _isGenerateMipmaps ( isGenerateMipmaps ),
    _resolution { .width = 0U, .height = 0U },
    _transfer ( VK_NULL_HANDLE ),
    _transferDeviceMemory ( VK_NULL_HANDLE ),
    _fileName ( std::move ( fileName ) )
{
    // NOTHING
}

void Texture2D::FreeResources ( android_vulkan::Renderer &renderer )
{
    FreeTransferResources ( renderer );
    FreeResourceInternal ( renderer );

    _format = VK_FORMAT_UNDEFINED;
    memset ( &_resolution, 0, sizeof ( _resolution ) );
    _fileName.clear ();
}

void Texture2D::FreeTransferResources ( android_vulkan::Renderer &renderer )
{
    VkDevice device = renderer.GetDevice ();

    if ( _transferDeviceMemory != VK_NULL_HANDLE )
    {
        vkFreeMemory ( device, _transferDeviceMemory, nullptr );
        _transferDeviceMemory = VK_NULL_HANDLE;
        AV_UNREGISTER_DEVICE_MEMORY ( "Texture2D::_transferDeviceMemory" )
    }

    if ( _transfer == VK_NULL_HANDLE )
        return;

    vkDestroyBuffer ( device, _transfer, nullptr );
    _transfer = VK_NULL_HANDLE;
    AV_UNREGISTER_BUFFER ( "Texture2D::_transfer" )
}

VkImageView Texture2D::GetImageView () const
{
    return _imageView;
}

uint8_t Texture2D::GetMipLevelCount () const
{
    return _mipLevels;
}

bool Texture2D::UploadData ( android_vulkan::Renderer &renderer, VkCommandBuffer commandBuffer )
{
    if ( _imageView != VK_NULL_HANDLE )
    {
        android_vulkan::LogWarning ( "Texture2D::UploadData - Data is uploaded already. Skipping..." );
        return true;
    }

    if ( _fileName.empty () )
    {
        android_vulkan::LogError ( "Texture2D::UploadData - Can't upload data. Filename is empty." );
        return false;
    }

    std::vector<uint8_t> pixelData;

    int width = 0;
    int height = 0;
    int channels = 0;

    if ( !LoadImage ( pixelData, _fileName, width, height, channels ) )
        return false;

    const VkFormat actualFormat = PickupFormat ( channels );

    if ( !IsFormatCompatible ( _format, actualFormat, renderer ) )
        return false;

    return UploadDataInternal ( pixelData.data (),
        pixelData.size (),
        VkExtent2D { .width = static_cast<uint32_t> ( width ), .height = static_cast<uint32_t> ( height ) },
        _format,
        _isGenerateMipmaps,
        renderer,
        commandBuffer
    );
}

bool Texture2D::UploadData ( std::string &fileName,
    VkFormat format,
    bool isGenerateMipmaps,
    android_vulkan::Renderer &renderer,
    VkCommandBuffer commandBuffer
)
{
    if ( fileName.empty () )
    {
        android_vulkan::LogError ( "Texture2D::UploadData - Can't upload data. Filename is empty." );
        return false;
    }

    FreeResourceInternal ( renderer );
    std::vector<uint8_t> pixelData;

    int width = 0;
    int height = 0;
    int channels = 0;

    if ( !LoadImage ( pixelData, fileName, width, height, channels ) )
        return false;

    const VkFormat actualFormat = PickupFormat ( channels );

    if ( !IsFormatCompatible ( format, actualFormat, renderer ) )
        return false;

    const bool result = UploadDataInternal ( pixelData.data (),
        pixelData.size (),
        VkExtent2D { .width = static_cast<uint32_t> ( width ), .height = static_cast<uint32_t> ( height ) },
        format,
        isGenerateMipmaps,
        renderer,
        commandBuffer
    );

    if ( !result )
        return false;

    _fileName = fileName;
    return true;
}

bool Texture2D::UploadData ( std::string &&fileName,
    VkFormat format,
    bool isGenerateMipmaps,
    android_vulkan::Renderer &renderer,
    VkCommandBuffer commandBuffer
)
{
    if ( fileName.empty () )
    {
        android_vulkan::LogError ( "Texture2D::UploadData - Can't upload data. Filename is empty." );
        return false;
    }

    FreeResourceInternal ( renderer );
    std::vector<uint8_t> pixelData;

    int width = 0;
    int height = 0;
    int channels = 0;

    if ( !LoadImage ( pixelData, fileName, width, height, channels ) )
        return false;

    const VkFormat actualFormat = PickupFormat ( channels );

    if ( !IsFormatCompatible ( format, actualFormat, renderer ) )
        return false;

    const bool result = UploadDataInternal ( pixelData.data (),
        pixelData.size (),
        VkExtent2D { .width = static_cast<uint32_t> ( width ), .height = static_cast<uint32_t> ( height ) },
        format,
        isGenerateMipmaps,
        renderer,
        commandBuffer
    );

    if ( !result )
        return false;

    _fileName = std::move ( fileName );
    return true;
}

bool Texture2D::UploadData ( const uint8_t* data,
    size_t size,
    const VkExtent2D &resolution,
    VkFormat format,
    bool isGenerateMipmaps,
    android_vulkan::Renderer &renderer,
    VkCommandBuffer commandBuffer
)
{
    FreeResources ( renderer );
    return UploadDataInternal ( data, size, resolution, format, isGenerateMipmaps, renderer, commandBuffer );
}

uint32_t Texture2D::CountMipLevels ( const VkExtent2D &resolution ) const
{
    uint32_t pivot = std::max ( resolution.width, resolution.height );
    uint32_t mipCount = 1U;

    while ( pivot != 1U )
    {
        ++mipCount;
        pivot >>= 1U;
    }

    return mipCount;
}

void Texture2D::FreeResourceInternal ( android_vulkan::Renderer &renderer )
{
    _mipLevels = 0U;
    VkDevice device = renderer.GetDevice ();

    if ( _imageView != VK_NULL_HANDLE )
    {
        vkDestroyImageView ( device, _imageView, nullptr );
        _imageView = VK_NULL_HANDLE;
        AV_UNREGISTER_IMAGE_VIEW ( "Texture2D::_imageView" )
    }

    if ( _imageDeviceMemory != VK_NULL_HANDLE )
    {
        vkFreeMemory ( device, _imageDeviceMemory, nullptr );
        _imageDeviceMemory = VK_NULL_HANDLE;
        AV_UNREGISTER_DEVICE_MEMORY ( "Texture2D::_imageDeviceMemory" )
    }

    if ( _image == VK_NULL_HANDLE )
        return;

    vkDestroyImage ( device, _image, nullptr );
    _image = VK_NULL_HANDLE;
    AV_UNREGISTER_IMAGE ( "Texture2D::_image" )
}

bool Texture2D::IsFormatCompatible ( VkFormat target, VkFormat candidate, android_vulkan::Renderer &renderer ) const
{
    if ( target == candidate )
        return true;

    const auto t = g_CompatibleFormats.find ( target );

    if ( t == g_CompatibleFormats.cend () )
    {
        android_vulkan::LogError ( "Texture2D::IsFormatCompatible - Unexpected format %s (%i)",
            renderer.ResolveVkFormat ( target ),
            static_cast<int> ( target )
        );

        return false;
    }

    const auto& options = t->second;

    if ( options.count ( candidate ) == 1U )
        return true;

    android_vulkan::LogError (
        "Texture2D::IsFormatCompatible - Candidate format %s (%i) is not compatible with target format %s (%i).",
        renderer.ResolveVkFormat ( candidate ),
        static_cast<int> ( candidate ),
        renderer.ResolveVkFormat ( candidate ),
        static_cast<int> ( candidate )
    );

    return false;
}

VkFormat Texture2D::PickupFormat ( int channels ) const
{
    switch ( channels )
    {
        case 1:
        return VK_FORMAT_R8_SRGB;

        case 2:
        return VK_FORMAT_R8G8_SRGB;

        case 3:
            android_vulkan::LogError ( "Texture2D::PickupFormat - Three channel formats are not supported!" );
        return VK_FORMAT_UNDEFINED;

        case 4:
        return VK_FORMAT_R8G8B8A8_SRGB;

        default:
            android_vulkan::LogError (
                "Texture2D::PickupFormat - Unexpected channel count: %i! Supported channel count: 1, 2 or 4."
            );
        return VK_FORMAT_UNDEFINED;
    }
}

bool Texture2D::UploadDataInternal ( const uint8_t* data,
    size_t size,
    const VkExtent2D &resolution,
    VkFormat format,
    bool isGenerateMipmaps,
    android_vulkan::Renderer &renderer,
    VkCommandBuffer commandBuffer
)
{
    _format = format;
    _resolution = resolution;

    VkImageCreateInfo imageInfo;
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = nullptr;
    imageInfo.flags = 0U;
    imageInfo.format = _format;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    imageInfo.usage =
        AV_VK_FLAG ( VK_IMAGE_USAGE_SAMPLED_BIT ) |
        AV_VK_FLAG ( VK_IMAGE_USAGE_TRANSFER_DST_BIT ) |
        AV_VK_FLAG ( VK_IMAGE_USAGE_TRANSFER_SRC_BIT );

    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.extent.width = _resolution.width;
    imageInfo.extent.height = _resolution.height;
    imageInfo.extent.depth = 1U;
    imageInfo.mipLevels = isGenerateMipmaps ? CountMipLevels ( _resolution ) : 1U;
    imageInfo.arrayLayers = 1U;
    imageInfo.queueFamilyIndexCount = 0U;
    imageInfo.pQueueFamilyIndices = nullptr;

    VkDevice device = renderer.GetDevice ();

    bool result = renderer.CheckVkResult ( vkCreateImage ( device, &imageInfo, nullptr, &_image ),
        "Texture2D::UploadDataInternal",
        "Can't create image"
    );

    if ( !result )
        return false;

    AV_REGISTER_IMAGE ( "Texture2D::_image" )

    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements ( device, _image, &memoryRequirements );

    result = renderer.TryAllocateMemory ( _imageDeviceMemory,
        memoryRequirements,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        "Can't allocate image memory (Texture2D::UploadDataInternal)"
    );

    if ( !result )
    {
        FreeResources ( renderer );
        return false;
    }

    AV_REGISTER_DEVICE_MEMORY ( "Texture2D::_imageDeviceMemory" )

    result = renderer.CheckVkResult ( vkBindImageMemory ( device, _image, _imageDeviceMemory, 0U ),
        "Texture2D::UploadDataInternal",
        "Can't bind image memory"
    );

    if ( !result )
    {
        FreeResources ( renderer );
        return false;
    }

    VkImageViewCreateInfo viewInfo;
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.pNext = nullptr;
    viewInfo.flags = 0U;
    viewInfo.image = _image;
    viewInfo.format = _format;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.baseMipLevel = viewInfo.subresourceRange.baseArrayLayer = 0U;
    viewInfo.subresourceRange.layerCount = 1U;
    viewInfo.subresourceRange.levelCount = imageInfo.mipLevels;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    result = renderer.CheckVkResult ( vkCreateImageView ( device, &viewInfo, nullptr, &_imageView ),
        "Texture2D::UploadDataInternal",
        "Can't create image view"
    );

    if ( !result )
    {
        FreeResources ( renderer );
        return false;
    }

    AV_REGISTER_IMAGE_VIEW ( "Texture2D::_imageView" )

    VkBufferCreateInfo bufferInfo;
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.flags = 0U;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.queueFamilyIndexCount = 0U;
    bufferInfo.pQueueFamilyIndices = nullptr;
    bufferInfo.size = static_cast<VkDeviceSize> ( size );

    result = renderer.CheckVkResult ( vkCreateBuffer ( device, &bufferInfo, nullptr, &_transfer ),
        "Texture2D::UploadDataInternal",
        "Can't create transfer buffer"
    );

    if ( !result )
    {
        FreeResources ( renderer );
        return false;
    }

    AV_REGISTER_BUFFER ( "Texture2D::_transfer" )

    vkGetBufferMemoryRequirements ( device, _transfer, &memoryRequirements );

    result = renderer.TryAllocateMemory ( _transferDeviceMemory,
        memoryRequirements,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        "Can't allocate transfer device memory (Texture2D::UploadDataInternal)"
    );

    if ( !result )
    {
        FreeResources ( renderer );
        return false;
    }

    AV_REGISTER_DEVICE_MEMORY ( "Texture2D::_transferDeviceMemory" )

    result = renderer.CheckVkResult ( vkBindBufferMemory ( device, _transfer, _transferDeviceMemory, 0U ),
        "Texture2D::UploadDataInternal",
        "Can't bind transfer memory"
    );

    if ( !result )
    {
        FreeResources ( renderer );
        return false;
    }

    void* destination = nullptr;

    result = renderer.CheckVkResult (
        vkMapMemory ( device, _transferDeviceMemory, 0U, bufferInfo.size, 0U, &destination ),
        "Texture2D::UploadDataInternal",
        "Can't map transfer memory"
    );

    if ( !result )
    {
        FreeResources ( renderer );
        return false;
    }

    memcpy ( destination, data, static_cast<size_t> ( bufferInfo.size ) );
    vkUnmapMemory ( device, _transferDeviceMemory );

    VkCommandBufferBeginInfo beginInfo;
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.pNext = nullptr;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    result = renderer.CheckVkResult ( vkBeginCommandBuffer ( commandBuffer, &beginInfo ),
        "Texture2D::UploadDataInternal",
        "Can't begin command buffer"
    );

    if ( !result )
    {
        FreeResources ( renderer );
        return false;
    }

    VkImageMemoryBarrier barrierInfo;
    barrierInfo.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrierInfo.pNext = nullptr;
    barrierInfo.image = _image;
    barrierInfo.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrierInfo.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierInfo.srcAccessMask = 0U;
    barrierInfo.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrierInfo.srcQueueFamilyIndex = barrierInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrierInfo.subresourceRange.layerCount = 1U;
    barrierInfo.subresourceRange.levelCount = imageInfo.mipLevels;
    barrierInfo.subresourceRange.baseArrayLayer = 0U;
    barrierInfo.subresourceRange.baseMipLevel = 0U;

    vkCmdPipelineBarrier ( commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        0U,
        nullptr,
        0U,
        nullptr,
        1U,
        &barrierInfo
    );

    VkBufferImageCopy copyRegion;
    copyRegion.imageOffset.x = 0;
    copyRegion.imageOffset.y = 0;
    copyRegion.imageOffset.z = 0;
    copyRegion.imageExtent = imageInfo.extent;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1U;
    copyRegion.imageSubresource.baseArrayLayer = 0U;
    copyRegion.imageSubresource.mipLevel = 0U;
    copyRegion.bufferRowLength = imageInfo.extent.width;
    copyRegion.bufferImageHeight = imageInfo.extent.height;
    copyRegion.bufferOffset = 0U;

    vkCmdCopyBufferToImage ( commandBuffer, _transfer, _image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &copyRegion );

    if ( !isGenerateMipmaps )
    {
        barrierInfo.subresourceRange.levelCount = 1U;
        barrierInfo.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrierInfo.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrierInfo.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrierInfo.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier ( commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0U,
            0U,
            nullptr,
            0U,
            nullptr,
            1U,
            &barrierInfo
        );

        result = renderer.CheckVkResult ( vkEndCommandBuffer ( commandBuffer ),
            "Texture2D::UploadDataInternal",
            "Can't end command buffer"
        );

        if ( !result )
        {
            FreeResources ( renderer );
            return false;
        }

        VkSubmitInfo submitInfo;
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = nullptr;
        submitInfo.commandBufferCount = 1U;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.waitSemaphoreCount = 0U;
        submitInfo.pWaitSemaphores = nullptr;
        submitInfo.signalSemaphoreCount = 0U;
        submitInfo.pSignalSemaphores = nullptr;
        submitInfo.pWaitDstStageMask = nullptr;

        result = renderer.CheckVkResult ( vkQueueSubmit ( renderer.GetQueue (), 1U, &submitInfo, VK_NULL_HANDLE ),
            "Texture2D::UploadDataInternal",
            "Can't submit command"
        );

        if ( !result )
        {
            FreeResources ( renderer );
            return false;
        }

        _mipLevels = static_cast<uint8_t> ( imageInfo.mipLevels );
        return true;
    }

    barrierInfo.subresourceRange.levelCount = 1U;
    barrierInfo.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierInfo.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrierInfo.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrierInfo.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier ( commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        0U,
        nullptr,
        0U,
        nullptr,
        1U,
        &barrierInfo
    );

    VkImageBlit blitInfo;
    blitInfo.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitInfo.srcSubresource.layerCount = 1U;
    blitInfo.srcSubresource.baseArrayLayer = 0U;
    memset ( blitInfo.srcOffsets, 0, sizeof ( VkOffset3D ) );
    blitInfo.srcOffsets[ 1U ].z = 1;
    blitInfo.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitInfo.dstSubresource.layerCount = 1U;
    blitInfo.dstSubresource.baseArrayLayer = 0U;
    memset ( blitInfo.dstOffsets, 0, sizeof ( VkOffset3D ) );
    blitInfo.dstOffsets[ 1U ].z = 1;

    for ( uint32_t i = 1U; i < imageInfo.mipLevels; ++i )
    {
        const uint32_t previousMip = i - 1U;
        blitInfo.srcSubresource.mipLevel = previousMip;
        blitInfo.srcOffsets[ 1U ].x = static_cast<int32_t> ( std::max ( imageInfo.extent.width >> previousMip, 1U ) );
        blitInfo.srcOffsets[ 1U ].y = static_cast<int32_t> ( std::max ( imageInfo.extent.height >> previousMip, 1U ) );
        blitInfo.dstSubresource.mipLevel = i;
        blitInfo.dstOffsets[ 1U ].x = static_cast<int32_t> ( std::max ( imageInfo.extent.width >> i, 1U ) );
        blitInfo.dstOffsets[ 1U ].y = static_cast<int32_t> ( std::max ( imageInfo.extent.height >> i, 1U ) );

        vkCmdBlitImage ( commandBuffer,
            _image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            _image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1U,
            &blitInfo,
            VK_FILTER_LINEAR
        );

        barrierInfo.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrierInfo.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrierInfo.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrierInfo.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrierInfo.subresourceRange.baseMipLevel = previousMip;

        vkCmdPipelineBarrier ( commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0U,
            0U,
            nullptr,
            0U,
            nullptr,
            1U,
            &barrierInfo
        );

        if ( i + 1U >= imageInfo.mipLevels )
            continue;

        // There are more unprocessed mip maps. But now done with current mip map.

        barrierInfo.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrierInfo.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrierInfo.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrierInfo.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrierInfo.subresourceRange.baseMipLevel = i;

        vkCmdPipelineBarrier ( commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0U,
            0U,
            nullptr,
            0U,
            nullptr,
            1U,
            &barrierInfo
        );
    }

    // Note last mip must be translated to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL state.

    barrierInfo.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrierInfo.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrierInfo.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierInfo.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrierInfo.subresourceRange.baseMipLevel = imageInfo.mipLevels - 1U;

    vkCmdPipelineBarrier ( commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0U,
        0U,
        nullptr,
        0U,
        nullptr,
        1U,
        &barrierInfo
    );

    result = renderer.CheckVkResult ( vkEndCommandBuffer ( commandBuffer ),
        "Texture2D::UploadDataInternal",
        "Can't end command buffer"
    );

    if ( !result )
    {
        FreeResources ( renderer );
        return false;
    }

    VkSubmitInfo submitInfo;
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;
    submitInfo.commandBufferCount = 1U;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.waitSemaphoreCount = 0U;
    submitInfo.pWaitSemaphores = nullptr;
    submitInfo.signalSemaphoreCount = 0U;
    submitInfo.pSignalSemaphores = nullptr;
    submitInfo.pWaitDstStageMask = nullptr;

    result = renderer.CheckVkResult ( vkQueueSubmit ( renderer.GetQueue (), 1U, &submitInfo, VK_NULL_HANDLE ),
        "Texture2D::UploadDataInternal",
        "Can't submit command"
    );

    if ( !result )
    {
        FreeResources ( renderer );
        return false;
    }

    _mipLevels = static_cast<uint8_t> ( imageInfo.mipLevels );
    return true;
}

bool Texture2D::LoadImage ( std::vector<uint8_t> &pixelData,
    const std::string& fileName,
    int &width,
    int &height,
    int &channels
)
{
    android_vulkan::File file ( const_cast<std::string&> ( fileName ) );

    if ( !file.LoadContent () )
        return false;

    const std::vector<uint8_t>& imageContent = file.GetContent ();
    const stbi_uc* stbInData = imageContent.data ();
    const auto stbInSize = static_cast<const int> ( imageContent.size () );

    auto* imagePixels = stbi_load_from_memory ( stbInData, stbInSize, &width, &height, &channels, 0 );

    if ( channels != 3 )
    {
        const auto size = static_cast<const size_t> ( width * height * channels );
        pixelData.resize ( size );
        memcpy ( pixelData.data (), imagePixels, size );
        free ( imagePixels );

        return true;
    }

    // We don't support 24bit per pixel mode.
    // Expanding up to 32bit per pixel mode...

    const auto size = static_cast<const size_t> ( RGBA_BYTES_PER_PIXEL * width * height );
    pixelData.resize ( size );
    uint8_t* dst = pixelData.data ();

    auto expander = [] ( uint32_t* dst,
        size_t dstRowSkipPixels,
        const uint32_t* nonDstMemory,
        const uint8_t* src,
        size_t srcRowSkipPixels,
        size_t rowPixelCount,
        uint32_t oneAlphaMask
    ) {
        while ( dst < nonDstMemory )
        {
            for ( size_t i = 0U; i < rowPixelCount; ++i )
            {
                *dst = *reinterpret_cast<const uint32_t*> ( src );
                *dst |= oneAlphaMask;

                ++dst;
                src += RGB_BYTES_PER_PIXEL;
            }

            src += srcRowSkipPixels;
            dst += dstRowSkipPixels;
        }
    };

    constexpr const size_t SKIP_ROWS = EXPANDER_THREADS - 1U;

    const auto rowPixelCount = static_cast<const size_t> ( width );
    const auto srcRowSize = RGB_BYTES_PER_PIXEL * rowPixelCount;
    const auto srcRowSkipPixels = SKIP_ROWS * srcRowSize;
    const auto dstRowSize = RGBA_BYTES_PER_PIXEL * rowPixelCount;
    const auto dstRowSkipPixels = SKIP_ROWS * rowPixelCount;
    const auto* nonDstMemory = reinterpret_cast<uint32_t*> ( dst + size );

    constexpr const uint8_t oneAlphaRaw[ RGBA_BYTES_PER_PIXEL ] = { 0x00U, 0x00U, 0x00U, 0xFFU };
    const uint32_t oneAlphaMask = *reinterpret_cast<const uint32_t*> ( oneAlphaRaw );

    std::array<std::thread, EXPANDER_THREADS> expanders;

    for ( size_t i = 0U; i < EXPANDER_THREADS; ++i )
    {
        expanders[ i ] = std::thread ( expander,
            reinterpret_cast<uint32_t*> ( dst + i * dstRowSize ),
            dstRowSkipPixels,
            nonDstMemory,
            imagePixels + i * srcRowSize,
            srcRowSkipPixels,
            rowPixelCount,
            oneAlphaMask
        );
    }

    for ( size_t i = 0U; i < EXPANDER_THREADS; ++i )
        expanders[ i ].join ();

    free ( imagePixels );
    channels = static_cast<int> ( RGBA_BYTES_PER_PIXEL );

    return true;
}

} // namespace rotating_mesh
