#ifdef ANDROID_VULKAN_DEBUG


#include <vulkan_utils.h>

GX_DISABLE_COMMON_WARNINGS

#include <set>
#include <shared_mutex>

GX_RESTORE_WARNING_STATE

#include "logger.h"


namespace android_vulkan {

constexpr static const char* INDENT = "    ";

//----------------------------------------------------------------------------------------------------------------------

Half::Half ():
    data ( 0U )
{
    // NOTHING
}

Half::Half ( float value )
{
    // see https://en.wikipedia.org/wiki/Single-precision_floating-point_format
    // see https://en.wikipedia.org/wiki/Half-precision_floating-point_format
    // see https://en.wikipedia.org/wiki/NaN
    // see https://en.wikipedia.org/wiki/IEEE_754-1985#Positive_and_negative_infinity

    const uint32_t from = *reinterpret_cast<uint32_t*> ( &value );

    const uint32_t mantissa = from & 0x007FFFFFU;
    const uint32_t sign = from & 0x80000000U;
    const uint32_t exponent = from & 0x7F800000U;

    // checking special cases: zeros, NaNs and INFs

    if ( mantissa == 0 && exponent == 0 )
    {
        // positive|negative zero branch
        data = static_cast<uint16_t> ( sign >> 16U );
        return;
    }

    if ( exponent == 0xFF )
    {
        if ( mantissa == 0 )
        {
            // INF branch
            data = static_cast<uint16_t> ( ( sign >> 16U ) | 0x00007C00U );
            return;
        }

        // NaN branches

        if ( mantissa & 0x400000U )
        {
            // singnaling NaN
            data = static_cast<uint16_t> ( ( sign >> 16U ) | 0x00007C00U );
            return;
        }

        // quet NaN
        data = static_cast<uint16_t> ( ( sign >> 16U ) | 0x00007E00U );
        return;
    }

    const auto exponentRaw = static_cast<uint8_t> ( exponent >> 23U );

    // removing exponent bias (substract 127)
    // see https://en.wikipedia.org/wiki/Single-precision_floating-point_format
    auto restoredExponent = static_cast<int16_t> ( exponentRaw - 0x7F );

    if ( restoredExponent >= 0 )
    {
        // positive exponent

        if ( restoredExponent > 0x000F )
        {
            // exponent is bigger than float16 can represent -> INF
            data = static_cast<uint16_t> ( ( sign >> 16U ) | 0x0000FC00U );
            return;
        }
    }
    else
    {
        // negative exponent

        if ( restoredExponent < -0x000E )
        {
            // exponent is less than float16 can represent -> zero
            data = static_cast<uint16_t> ( sign >> 16U );
            return;
        }
    }

    // biasing exponent (add 15).
    // see https://en.wikipedia.org/wiki/Half-precision_floating-point_format
    restoredExponent += 0x000F;

    // input number is normalized by design. reassemble it
    data = static_cast<uint16_t> (
        ( sign >> 16U ) |
        ( static_cast<uint32_t> ( restoredExponent ) << 10U ) |
        ( mantissa >> 13U )
    );
}

//----------------------------------------------------------------------------------------------------------------------

class VulkanItem final
{
    private:
        size_t          _instances;
        std::string     _where;

    public:
        VulkanItem ();
        VulkanItem ( const VulkanItem &other ) = default;
        explicit VulkanItem ( std::string &&where );

        VulkanItem& operator = ( const VulkanItem &other ) = delete;

        void IncrementInstanceCount ();
        void DecrementInstanceCount ();
        void GetInfo ( std::string &info ) const;
        [[nodiscard]] bool IsLastInstance () const;

        bool operator < ( const VulkanItem &other ) const;
};

VulkanItem::VulkanItem ():
    _instances ( 1U ),
    _where {}
{
    // NOTHING
}

VulkanItem::VulkanItem ( std::string &&where ):
    _instances ( 1U ),
    _where ( std::move ( where ) )
{
    // NOTHING
}

void VulkanItem::IncrementInstanceCount ()
{
    ++_instances;
}

void VulkanItem::DecrementInstanceCount ()
{
    --_instances;
}

void VulkanItem::GetInfo ( std::string &info ) const
{
    info = _where + " (instances: " + std::to_string ( static_cast<long long int> ( _instances ) ) + ")";
}

bool VulkanItem::IsLastInstance () const
{
    return _instances == 1U;
}

bool VulkanItem::operator < ( const VulkanItem &other ) const
{
    return _where < other._where;
}

//----------------------------------------------------------------------------------------------------------------------

static std::shared_timed_mutex      g_Lock;
static std::set<VulkanItem>         g_Buffers;
static std::set<VulkanItem>         g_CommandPools;
static std::set<VulkanItem>         g_DescriptorPools;
static std::set<VulkanItem>         g_DescriptorSetLayouts;
static std::set<VulkanItem>         g_Devices;
static std::set<VulkanItem>         g_DeviceMemory;
static std::set<VulkanItem>         g_Fences;
static std::set<VulkanItem>         g_Framebuffers;
static std::set<VulkanItem>         g_Images;
static std::set<VulkanItem>         g_ImageViews;
static std::set<VulkanItem>         g_Pipelines;
static std::set<VulkanItem>         g_PipelineLayouts;
static std::set<VulkanItem>         g_RenderPasses;
static std::set<VulkanItem>         g_Samplers;
static std::set<VulkanItem>         g_Semaphores;
static std::set<VulkanItem>         g_ShaderModules;
static std::set<VulkanItem>         g_Surfaces;
static std::set<VulkanItem>         g_Swapchains;

static void CheckNonDispatchableObjectLeaks ( const char* objectType, std::set<VulkanItem> &storage )
{
    if ( storage.empty () )
        return;

    LogError ( "AV_CHECK_VULKAN_LEAKS - %s objects were leaked: %zu", objectType, storage.size () );
    LogError ( ">>>" );
    std::string info;

    for ( auto const& leak : storage )
    {
        leak.GetInfo ( info );
        LogWarning ( "%s%s", INDENT, info.c_str () );
    }

    LogError ( "<<<" );

#ifdef ANDROID_VULKAN_STRICT_MODE

    assert ( !"CheckNonDispatchableObjectLeaks triggered!" );

#endif

}

static void RegisterNonDispatchableObject ( std::string &&where, std::set<VulkanItem> &storage )
{
    std::unique_lock<std::shared_timed_mutex> lock ( g_Lock );
    auto result = storage.insert ( VulkanItem ( std::move ( where ) ) );

    if ( result.second )
        return;

    auto& item = const_cast<VulkanItem&> ( *result.first );
    item.IncrementInstanceCount ();
}

static void UnregisterNonDispatchableObject ( const char* method,
    const char* objectType,
    std::string &&where,
    std::set<VulkanItem> &storage
)
{
    std::unique_lock<std::shared_timed_mutex> lock ( g_Lock );
    const char* str = where.c_str ();
    const auto findResult = storage.find ( VulkanItem ( std::move ( where ) ) );

    if ( findResult == storage.cend () )
    {
        LogError ( "%s - Can't find %s with ID: %s. Please check logic.",
            method,
            objectType,
            str
        );

#ifdef ANDROID_VULKAN_STRICT_MODE

        assert ( !"UnregisterNonDispatchableObject triggered!" );

#endif

    }

    if ( findResult->IsLastInstance () )
    {
        storage.erase ( findResult );
        return;
    }

    auto& item = const_cast<VulkanItem&> ( *findResult );
    item.DecrementInstanceCount ();
}

void CheckVulkanLeaks ()
{
    std::shared_lock<std::shared_timed_mutex> lock ( g_Lock );

    CheckNonDispatchableObjectLeaks ( "Buffer", g_Buffers );
    CheckNonDispatchableObjectLeaks ( "Command pool", g_CommandPools );
    CheckNonDispatchableObjectLeaks ( "Descriptor pool", g_DescriptorPools );
    CheckNonDispatchableObjectLeaks ( "Descriptor set layout", g_DescriptorSetLayouts );
    CheckNonDispatchableObjectLeaks ( "Device", g_Devices );
    CheckNonDispatchableObjectLeaks ( "Device memory", g_DeviceMemory );
    CheckNonDispatchableObjectLeaks ( "Fence", g_Fences );
    CheckNonDispatchableObjectLeaks ( "Framebuffer", g_Framebuffers );
    CheckNonDispatchableObjectLeaks ( "Image", g_Images );
    CheckNonDispatchableObjectLeaks ( "Image view", g_ImageViews );
    CheckNonDispatchableObjectLeaks ( "Pipeline", g_Pipelines );
    CheckNonDispatchableObjectLeaks ( "Pipeline layout", g_PipelineLayouts );
    CheckNonDispatchableObjectLeaks ( "Render pass", g_RenderPasses );
    CheckNonDispatchableObjectLeaks ( "Sampler", g_Samplers );
    CheckNonDispatchableObjectLeaks ( "Semaphore", g_Semaphores );
    CheckNonDispatchableObjectLeaks ( "Shader module", g_ShaderModules );
    CheckNonDispatchableObjectLeaks ( "Surface", g_Surfaces );
    CheckNonDispatchableObjectLeaks ( "Swapchain", g_Swapchains );
}

void RegisterBuffer ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_Buffers );
}

void UnregisterBuffer ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_BUFFER",
        "buffer",
        std::move ( where ),
        g_Buffers
    );
}

void RegisterCommandPool ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_CommandPools );
}

void UnregisterCommandPool ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_COMMAND_POOL",
        "command pool",
        std::move ( where ),
        g_CommandPools
    );
}

void RegisterDescriptorPool ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_DescriptorPools );
}

void UnregisterDescriptorPool ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_DESCRIPTOR_POOL",
        "descriptor pool",
        std::move ( where ),
        g_DescriptorPools
    );
}

void RegisterDescriptorSetLayout ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_DescriptorSetLayouts );
}

void UnregisterDescriptorSetLayout ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_DESCRIPTOR_SET_LAYOUT",
        "descriptor set layout",
        std::move ( where ),
        g_DescriptorSetLayouts
    );
}

void RegisterDevice ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_Devices );
}

void UnregisterDevice ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_DEVICE",
        "device",
        std::move ( where ),
        g_Devices
    );
}

void RegisterDeviceMemory ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_DeviceMemory );
}

void UnregisterDeviceMemory ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_DEVICE_MEMORY",
        "device memory",
        std::move ( where ),
        g_DeviceMemory
    );
}

void RegisterFence ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_Fences );
}

void UnregisterFence ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_FENCE",
        "fence",
        std::move ( where ),
        g_Fences
    );
}

void RegisterFramebuffer ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_Framebuffers );
}

void UnregisterFramebuffer ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_FRAMEBUFFER",
        "framebuffer",
        std::move ( where ),
        g_Framebuffers
    );
}

void RegisterImage ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_Images );
}

void UnregisterImage ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_IMAGE",
        "image",
        std::move ( where ),
        g_Images
    );
}

void RegisterImageView ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_ImageViews );
}

void UnregisterImageView ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_IMAGE_VIEW",
        "image view",
        std::move ( where ),
        g_ImageViews
    );
}

void RegisterPipeline ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_Pipelines );
}

void UnregisterPipeline ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_PIPELINE",
        "pipeline",
        std::move ( where ),
        g_Pipelines
    );
}

void RegisterPipelineLayout ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_PipelineLayouts );
}

void UnregisterPipelineLayout ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_PIPELINE_LAYOUT",
        "pipeline layout",
        std::move ( where ),
        g_PipelineLayouts
    );
}

void RegisterRenderPass ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_RenderPasses );
}

void UnregisterRenderPass ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_RENDER_PASS",
        "render pass",
        std::move ( where ),
        g_RenderPasses
    );
}

void RegisterSampler ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_Samplers );
}

void UnregisterSampler ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_SAMPLER",
        "sampler",
        std::move ( where ),
        g_Samplers
    );
}

void RegisterSemaphore ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_Semaphores );
}

void UnregisterSemaphore ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_SEMAPHORE",
        "semaphore",
        std::move ( where ),
        g_Semaphores
    );
}

void RegisterShaderModule ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_ShaderModules );
}

void UnregisterShaderModule ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_SHADER_MODULE",
        "shader module",
        std::move ( where ),
        g_ShaderModules
    );
}

void RegisterSurface ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_Surfaces );
}

void UnregisterSurface ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_SURFACE",
        "surface",
        std::move ( where ),
        g_Surfaces
    );
}

void RegisterSwapchain ( std::string &&where )
{
    RegisterNonDispatchableObject ( std::move ( where ), g_Swapchains );
}

void UnregisterSwapchain ( std::string &&where )
{
    UnregisterNonDispatchableObject ( "AV_UNREGISTER_SWAPCHAIN",
        "swapchain",
        std::move ( where ),
        g_Swapchains
    );
}

} // namespace android_vulkan


#endif // ANDROID_VULKAN_DEBUG
