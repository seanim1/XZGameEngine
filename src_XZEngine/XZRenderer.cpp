#include "XZRenderer.hpp"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#ifdef __APPLE__
    #define VK_ENABLE_BETA_EXTENSIONS
    #include <vulkan/vulkan_metal.h>
    #include <vulkan/vulkan_beta.h>
#endif

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>

namespace XZRenderer {

// ============================================================
//  Logging
// ============================================================
static bool g_log = false;
static void xz_log(const char* msg) { if (g_log) SDL_Log("XZR: %s", msg); }
static void xz_logf(const char* fmt, ...) {
    if (!g_log) return;
    char buf[512]; va_list a; va_start(a,fmt);
    vsnprintf(buf,sizeof(buf),fmt,a); va_end(a);
    SDL_Log("XZR: %s", buf);
}

static const VkFormat DEPTH_FORMAT      = VK_FORMAT_D32_SFLOAT;
static const VkFormat COLOR_TARGET_FMT  = VK_FORMAT_R8G8B8A8_UNORM; // offscreen color

// ============================================================
//  GPU structs
// ============================================================
struct GpuBuffer {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};
struct GpuTexture {
    VkImage        image   = VK_NULL_HANDLE;
    VkDeviceMemory memory  = VK_NULL_HANDLE;
    VkImageView    view    = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;
};
struct FrameUBO {
    GpuBuffer       buffer;
    void*           mapped         = nullptr;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};
struct alignas(16) UniformData {
    glm::mat4 mvp;
    glm::mat4 model;
    glm::vec4 light_pos;
    glm::vec4 cam_pos;
};
// Post-process uniform — must match postprocess.frag layout
struct alignas(16) PostProcessData {
    int   chromatic_aberration_enabled;
    float chromatic_aberration_strength;
    int   radial_blur_enabled;
    float radial_blur_strength;
    int   radial_blur_samples;
    float pad0;
    float pad1;
    float pad2;
};
struct GpuSubMesh {
    GpuBuffer vertex_buffer;
    GpuBuffer index_buffer;
    uint32_t  index_count = 0;
};

// ============================================================
//  MeshObjectImpl
// ============================================================
struct MeshObjectImpl {
    RendererImpl* renderer = nullptr;
    std::vector<GpuSubMesh> sub_meshes;
    bool      is_gltf         = false;
    GpuBuffer mesh_vertex;
    GpuBuffer mesh_index;
    uint32_t  raw_index_count = 0;
    GpuTexture texture;
    std::vector<FrameUBO> frames;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
};

// ============================================================
//  CustomShaderQuadImpl
// ============================================================
struct CustomShaderQuadImpl {
    RendererImpl* renderer = nullptr;
    GpuBuffer vertex_buffer;
    GpuBuffer index_buffer;
    uint32_t  index_count = 0;
    VkPipeline            pipeline        = VK_NULL_HANDLE;
    VkPipelineLayout      pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout     = VK_NULL_HANDLE;
    VkDescriptorPool      descriptor_pool = VK_NULL_HANDLE;
    std::vector<FrameUBO> frames;
};

// ============================================================
//  CustomShaderPoints3dImpl
// ============================================================
struct CustomShaderPoints3dImpl {
    RendererImpl* renderer = nullptr;
    GpuBuffer vertex_buffer;
    uint32_t  max_count   = 500;
    uint32_t  alive_count = 0;
    VkPipeline            pipeline        = VK_NULL_HANDLE;
    VkPipelineLayout      pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout     = VK_NULL_HANDLE;
    VkDescriptorPool      descriptor_pool = VK_NULL_HANDLE;
    std::vector<FrameUBO> frames;
};

// ============================================================
//  RendererImpl
// ============================================================
struct RendererImpl {
    SDL_Window* window       = nullptr;
    bool        should_close = false;

    VkInstance       instance        = VK_NULL_HANDLE;
    VkSurfaceKHR     surface         = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice         device          = VK_NULL_HANDLE;
    VkQueue          graphics_queue  = VK_NULL_HANDLE;
    uint32_t         graphics_family = 0;

    VkSwapchainKHR           swapchain        = VK_NULL_HANDLE;
    std::vector<VkImage>     images;
    std::vector<VkImageView> image_views;
    VkExtent2D               swapchain_extent = {};
    VkSurfaceFormatKHR       surface_format   = {};
    uint32_t                 image_count      = 0;

    GpuTexture depth;

    // Offscreen color target — scene renders here
    GpuTexture color_target;

    VkCommandPool                command_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> scene_cmds;
    std::vector<VkCommandBuffer> imgui_cmds;

    std::vector<VkSemaphore> image_available_sems;
    std::vector<VkSemaphore> render_finished_sems;
    VkFence                  in_flight_fence = VK_NULL_HANDLE;
    uint32_t                 current_frame   = 0;

    // Main opaque pipeline
    VkPipeline            main_pipeline        = VK_NULL_HANDLE;
    VkPipelineLayout      main_pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout main_desc_layout     = VK_NULL_HANDLE;

    // Post-process pipeline — fullscreen quad reads color_target
    VkPipeline            post_pipeline        = VK_NULL_HANDLE;
    VkPipelineLayout      post_pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout post_desc_layout     = VK_NULL_HANDLE;
    VkDescriptorPool      post_descriptor_pool = VK_NULL_HANDLE;
    std::vector<FrameUBO> post_frames;          // per-swapchain-image UBO

    VkDescriptorPool imgui_pool = VK_NULL_HANDLE;

    float     clear_color[4] = {0.306f, 0.643f, 0.761f, 1};
    glm::vec3 camera_pos     = {0, 0, -6};
    glm::vec3 camera_target  = {0, 0, 0};
    uint32_t  current_image  = 0;
    float     last_time      = 0;

    // Post-process parameters (updated by Renderer::setXxx calls)
    PostProcessData post_data = {0, 0.005f, 0, 0.02f, 10, 0, 0, 0};
};

// ============================================================
//  Vulkan helpers
// ============================================================
static void check_vk(VkResult r, const char* op) {
    if (r != VK_SUCCESS) { SDL_Log("XZR Vulkan error [%s]: %d", op, r); exit(1); }
}
static uint32_t find_memory_type(VkPhysicalDevice pd, uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((filter & (1<<i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    SDL_Log("XZR: no suitable memory type"); exit(1);
}
static GpuBuffer make_buffer(VkDevice dev, VkPhysicalDevice pd,
    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props)
{
    GpuBuffer out;
    VkBufferCreateInfo bi = { .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size=size,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE };
    check_vk(vkCreateBuffer(dev,&bi,nullptr,&out.buffer),"vkCreateBuffer");
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev,out.buffer,&mr);
    VkMemoryAllocateInfo ai = { .sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=mr.size,.memoryTypeIndex=find_memory_type(pd,mr.memoryTypeBits,props) };
    check_vk(vkAllocateMemory(dev,&ai,nullptr,&out.memory),"vkAllocateMemory");
    vkBindBufferMemory(dev,out.buffer,out.memory,0);
    return out;
}
static void upload(VkDevice dev, GpuBuffer& b, const void* data, VkDeviceSize sz) {
    void* m; vkMapMemory(dev,b.memory,0,sz,0,&m); memcpy(m,data,sz); vkUnmapMemory(dev,b.memory);
}
static void destroy_buffer(VkDevice dev, GpuBuffer& b) {
    if (b.buffer!=VK_NULL_HANDLE){vkDestroyBuffer(dev,b.buffer,nullptr);b.buffer=VK_NULL_HANDLE;}
    if (b.memory!=VK_NULL_HANDLE){vkFreeMemory(dev,b.memory,nullptr);   b.memory=VK_NULL_HANDLE;}
}
static void destroy_texture(VkDevice dev, GpuTexture& t) {
    if (t.sampler!=VK_NULL_HANDLE){vkDestroySampler(dev,t.sampler,nullptr);   t.sampler=VK_NULL_HANDLE;}
    if (t.view   !=VK_NULL_HANDLE){vkDestroyImageView(dev,t.view,nullptr);    t.view   =VK_NULL_HANDLE;}
    if (t.image  !=VK_NULL_HANDLE){vkDestroyImage(dev,t.image,nullptr);       t.image  =VK_NULL_HANDLE;}
    if (t.memory !=VK_NULL_HANDLE){vkFreeMemory(dev,t.memory,nullptr);        t.memory =VK_NULL_HANDLE;}
}
static VkShaderModule load_shader(VkDevice dev, const char* path) {
    FILE* f = fopen(path,"rb");
    if (!f){SDL_Log("XZR: shader not found: %s",path);exit(1);}
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    uint32_t* spv=(uint32_t*)malloc(sz); fread(spv,1,sz,f); fclose(f);
    VkShaderModuleCreateInfo info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=(size_t)sz,.pCode=spv};
    VkShaderModule mod;
    check_vk(vkCreateShaderModule(dev,&info,nullptr,&mod),"vkCreateShaderModule");
    free(spv); return mod;
}

// Submit a one-time command buffer
static VkCommandBuffer begin_one_time(RendererImpl* r) {
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=r->command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cmd; vkAllocateCommandBuffers(r->device,&ca,&cmd);
    VkCommandBufferBeginInfo cb={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cmd,&cb);
    return cmd;
}
static void end_one_time(RendererImpl* r, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
    vkQueueSubmit(r->graphics_queue,1,&si,VK_NULL_HANDLE);
    vkQueueWaitIdle(r->graphics_queue);
    vkFreeCommandBuffers(r->device,r->command_pool,1,&cmd);
}

static GpuTexture make_texture(RendererImpl* r, stbi_uc* px, int w, int h) {
    GpuTexture out;
    VkDeviceSize sz=(VkDeviceSize)(w*h*4);
    GpuBuffer stg=make_buffer(r->device,r->physical_device,sz,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    upload(r->device,stg,px,sz);
    VkImageCreateInfo ii={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=VK_FORMAT_R8G8B8A8_SRGB,.extent={(uint32_t)w,(uint32_t)h,1},
        .mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    check_vk(vkCreateImage(r->device,&ii,nullptr,&out.image),"img");
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(r->device,out.image,&mr);
    VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,
        .memoryTypeIndex=find_memory_type(r->physical_device,mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    check_vk(vkAllocateMemory(r->device,&ma,nullptr,&out.memory),"img mem");
    vkBindImageMemory(r->device,out.image,out.memory,0);
    VkCommandBuffer cmd=begin_one_time(r);
    VkImageMemoryBarrier bar={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=0,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=out.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,0,nullptr,0,nullptr,1,&bar);
    VkBufferImageCopy cp={.bufferOffset=0,.bufferRowLength=0,.bufferImageHeight=0,
        .imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
        .imageOffset={0,0,0},.imageExtent={(uint32_t)w,(uint32_t)h,1}};
    vkCmdCopyBufferToImage(cmd,stg.buffer,out.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&cp);
    bar.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; bar.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    bar.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bar.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,0,nullptr,0,nullptr,1,&bar);
    end_one_time(r,cmd);
    destroy_buffer(r->device,stg);
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=out.image,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_SRGB,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    check_vk(vkCreateImageView(r->device,&vi,nullptr,&out.view),"img view");
    VkSamplerCreateInfo smi={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter=VK_FILTER_LINEAR,.minFilter=VK_FILTER_LINEAR,
        .mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .minLod=0,.maxLod=0};
    check_vk(vkCreateSampler(r->device,&smi,nullptr,&out.sampler),"sampler");
    return out;
}
static GpuTexture load_texture(RendererImpl* r, const std::string& path) {
    int w,h,ch;
    stbi_uc* px=stbi_load(path.c_str(),&w,&h,&ch,STBI_rgb_alpha);
    if (!px){SDL_Log("XZR: texture load failed: %s",path.c_str());exit(1);}
    GpuTexture t=make_texture(r,px,w,h); stbi_image_free(px); return t;
}

// ============================================================
//  Create the offscreen color target
// ============================================================
static void create_color_target(RendererImpl* r) {
    uint32_t W = r->swapchain_extent.width;
    uint32_t H = r->swapchain_extent.height;

    VkImageCreateInfo ii={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=COLOR_TARGET_FMT,.extent={W,H,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    check_vk(vkCreateImage(r->device,&ii,nullptr,&r->color_target.image),"color target img");
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(r->device,r->color_target.image,&mr);
    VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,
        .memoryTypeIndex=find_memory_type(r->physical_device,mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    check_vk(vkAllocateMemory(r->device,&ma,nullptr,&r->color_target.memory),"color target mem");
    vkBindImageMemory(r->device,r->color_target.image,r->color_target.memory,0);

    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image=r->color_target.image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=COLOR_TARGET_FMT,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    check_vk(vkCreateImageView(r->device,&vi,nullptr,&r->color_target.view),"color target view");

    VkSamplerCreateInfo smi={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter=VK_FILTER_LINEAR,.minFilter=VK_FILTER_LINEAR,
        .mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .minLod=0,.maxLod=0};
    check_vk(vkCreateSampler(r->device,&smi,nullptr,&r->color_target.sampler),"color target sampler");

    // Transition to shader read optimal so it's valid before first frame
    VkCommandBuffer cmd=begin_one_time(r);
    VkImageMemoryBarrier bar={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=0,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
        .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=r->color_target.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,0,nullptr,0,nullptr,1,&bar);
    end_one_time(r,cmd);

    xz_log("Color target created");
}

// ============================================================
//  Descriptor set helpers
// ============================================================
static void write_ds_ubo_sampler(VkDevice dev, VkDescriptorSet ds,
    VkBuffer ubo, VkImageView view, VkSampler sampler)
{
    VkDescriptorBufferInfo bi={.buffer=ubo,.offset=0,.range=sizeof(UniformData)};
    VkDescriptorImageInfo  ii={.sampler=sampler,.imageView=view,
        .imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr[2]={
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=ds,.dstBinding=0,
         .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&bi},
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=ds,.dstBinding=1,
         .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&ii}};
    vkUpdateDescriptorSets(dev,2,wr,0,nullptr);
}

static void alloc_mesh_frames(RendererImpl* r, MeshObjectImpl* m) {
    uint32_t n=r->image_count; m->frames.resize(n);
    VkDescriptorPoolSize ps[]={
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,n},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,n}};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=n,.poolSizeCount=2,.pPoolSizes=ps};
    check_vk(vkCreateDescriptorPool(r->device,&pi,nullptr,&m->descriptor_pool),"mesh pool");
    std::vector<VkDescriptorSetLayout> layouts(n,r->main_desc_layout);
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=m->descriptor_pool,.descriptorSetCount=n,.pSetLayouts=layouts.data()};
    std::vector<VkDescriptorSet> sets(n);
    check_vk(vkAllocateDescriptorSets(r->device,&ai,sets.data()),"mesh sets");
    for (uint32_t i=0;i<n;i++){
        m->frames[i].buffer=make_buffer(r->device,r->physical_device,sizeof(UniformData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkMapMemory(r->device,m->frames[i].buffer.memory,0,sizeof(UniformData),0,&m->frames[i].mapped);
        m->frames[i].descriptor_set=sets[i];
        write_ds_ubo_sampler(r->device,sets[i],m->frames[i].buffer.buffer,m->texture.view,m->texture.sampler);
    }
}

static VkDescriptorSetLayout make_ubo_desc_layout(RendererImpl* r, VkShaderStageFlags stages) {
    VkDescriptorSetLayoutBinding b{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount=1,.stageFlags=stages};
    VkDescriptorSetLayoutCreateInfo li{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=1,.pBindings=&b};
    VkDescriptorSetLayout layout;
    check_vk(vkCreateDescriptorSetLayout(r->device,&li,nullptr,&layout),"ubo layout");
    return layout;
}

static void alloc_ubo_frames(RendererImpl* r, VkDescriptorSetLayout desc_layout,
    VkDescriptorPool& out_pool, std::vector<FrameUBO>& out_frames)
{
    uint32_t n=r->image_count; out_frames.resize(n);
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,n};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=n,.poolSizeCount=1,.pPoolSizes=&ps};
    check_vk(vkCreateDescriptorPool(r->device,&pi,nullptr,&out_pool),"ubo pool");
    std::vector<VkDescriptorSetLayout> layouts(n,desc_layout);
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=out_pool,.descriptorSetCount=n,.pSetLayouts=layouts.data()};
    std::vector<VkDescriptorSet> sets(n);
    check_vk(vkAllocateDescriptorSets(r->device,&ai,sets.data()),"ubo sets");
    for (uint32_t i=0;i<n;i++){
        out_frames[i].buffer=make_buffer(r->device,r->physical_device,sizeof(UniformData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkMapMemory(r->device,out_frames[i].buffer.memory,0,sizeof(UniformData),0,&out_frames[i].mapped);
        out_frames[i].descriptor_set=sets[i];
        VkDescriptorBufferInfo bi2{.buffer=out_frames[i].buffer.buffer,.offset=0,.range=sizeof(UniformData)};
        VkWriteDescriptorSet wr{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet=sets[i],.dstBinding=0,.descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&bi2};
        vkUpdateDescriptorSets(r->device,1,&wr,0,nullptr);
    }
}

// ============================================================
//  Post-process descriptor layout and pipeline
//  Binding 0 = sampler2D colorTarget
//  Binding 1 = UBO PostProcessData
// ============================================================
static void build_post_pipeline(RendererImpl* r) {
    uint32_t n = r->image_count;

    // Descriptor layout: binding 0 = sampler, binding 1 = UBO
    VkDescriptorSetLayoutBinding bindings[2] = {
        {.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT},
    };
    VkDescriptorSetLayoutCreateInfo li{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=2,.pBindings=bindings};
    check_vk(vkCreateDescriptorSetLayout(r->device,&li,nullptr,&r->post_desc_layout),"post layout");

    // Descriptor pool
    VkDescriptorPoolSize ps[2]={
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,n},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,n},
    };
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=n,.poolSizeCount=2,.pPoolSizes=ps};
    check_vk(vkCreateDescriptorPool(r->device,&pi,nullptr,&r->post_descriptor_pool),"post pool");

    // Allocate descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(n, r->post_desc_layout);
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=r->post_descriptor_pool,.descriptorSetCount=n,.pSetLayouts=layouts.data()};
    std::vector<VkDescriptorSet> sets(n);
    check_vk(vkAllocateDescriptorSets(r->device,&ai,sets.data()),"post sets");

    // Create per-frame UBO buffers and write descriptor sets
    r->post_frames.resize(n);
    for (uint32_t i=0;i<n;i++){
        r->post_frames[i].buffer=make_buffer(r->device,r->physical_device,sizeof(PostProcessData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkMapMemory(r->device,r->post_frames[i].buffer.memory,0,sizeof(PostProcessData),
            0,&r->post_frames[i].mapped);
        r->post_frames[i].descriptor_set=sets[i];

        // Write sampler (color_target) and UBO
        VkDescriptorImageInfo img_info={.sampler=r->color_target.sampler,
            .imageView=r->color_target.view,
            .imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo buf_info={.buffer=r->post_frames[i].buffer.buffer,
            .offset=0,.range=sizeof(PostProcessData)};
        VkWriteDescriptorSet wr[2]={
            {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[i],.dstBinding=0,
             .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .pImageInfo=&img_info},
            {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[i],.dstBinding=1,
             .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
             .pBufferInfo=&buf_info},
        };
        vkUpdateDescriptorSets(r->device,2,wr,0,nullptr);
    }

    // Pipeline layout
    VkPipelineLayoutCreateInfo pli{.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&r->post_desc_layout};
    check_vk(vkCreatePipelineLayout(r->device,&pli,nullptr,&r->post_pipeline_layout),"post pl");

    // Shaders
    VkShaderModule vm=load_shader(r->device,SHADER_OUTPUT_DIR "postprocess.spv");
    VkShaderModule fm=load_shader(r->device,SHADER_OUTPUT_DIR "postprocessfrag.spv");

    // No vertex input — fullscreen triangle generated in vertex shader
    VkPipelineVertexInputStateCreateInfo vi{
        .sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=0,.vertexAttributeDescriptionCount=0};
    VkPipelineInputAssemblyStateCreateInfo ia{
        .sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport vp{.x=0,.y=0,.width=(float)r->swapchain_extent.width,
        .height=(float)r->swapchain_extent.height,.minDepth=0,.maxDepth=1};
    VkRect2D sc{{0,0},r->swapchain_extent};
    VkPipelineViewportStateCreateInfo vs{
        .sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&sc};
    VkPipelineRasterizationStateCreateInfo rs{
        .sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,
        .frontFace=VK_FRONT_FACE_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms{
        .sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    // No depth test for fullscreen post-process pass
    VkPipelineDepthStencilStateCreateInfo ds{
        .sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_FALSE,.depthWriteEnable=VK_FALSE};
    VkPipelineColorBlendAttachmentState cba{
        .colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                        VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cbs{
        .sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&cba};
    // Post pass writes to swapchain format
    VkPipelineRenderingCreateInfo pri{
        .sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount=1,.pColorAttachmentFormats=&r->surface_format.format};
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vm,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fm,.pName="main"}};
    VkGraphicsPipelineCreateInfo gpi{
        .sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.pNext=&pri,
        .stageCount=2,.pStages=stages,
        .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vs,
        .pRasterizationState=&rs,.pMultisampleState=&ms,.pDepthStencilState=&ds,
        .pColorBlendState=&cbs,.layout=r->post_pipeline_layout};
    check_vk(vkCreateGraphicsPipelines(r->device,VK_NULL_HANDLE,1,&gpi,nullptr,&r->post_pipeline),"post pipeline");
    vkDestroyShaderModule(r->device,vm,nullptr);
    vkDestroyShaderModule(r->device,fm,nullptr);
    xz_log("Post-process pipeline created");
}

// ============================================================
//  Vulkan init
// ============================================================
static void build_main_pipeline(RendererImpl* r);

static void vulkan_init(RendererImpl* r, uint32_t W, uint32_t H,
                        const std::string& title, const float cc[4])
{
    if (!SDL_Init(SDL_INIT_VIDEO)){SDL_Log("SDL_Init: %s",SDL_GetError());exit(1);}
    r->window=SDL_CreateWindow(title.c_str(),(int)W,(int)H,SDL_WINDOW_VULKAN);
    if (!r->window){SDL_Log("Window: %s",SDL_GetError());exit(1);}

    uint32_t api=0; vkEnumerateInstanceVersion(&api);
    xz_logf("Vulkan %d.%d.%d",VK_VERSION_MAJOR(api),VK_VERSION_MINOR(api),VK_VERSION_PATCH(api));

    std::vector<const char*> iexts={VK_KHR_SURFACE_EXTENSION_NAME};
#ifdef VK_USE_PLATFORM_WIN32_KHR
    iexts.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    iexts.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#elif defined(__APPLE__)
    iexts.push_back("VK_EXT_metal_surface");
    iexts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
    uint32_t ec=0; vkEnumerateInstanceExtensionProperties(nullptr,&ec,nullptr);
    std::vector<VkExtensionProperties> se(ec);
    vkEnumerateInstanceExtensionProperties(nullptr,&ec,se.data());
    std::vector<const char*> eiexts;
    for (const char* e:iexts) for (auto& s:se) if(!strcmp(e,s.extensionName)){eiexts.push_back(e);break;}

    uint32_t lc=0; vkEnumerateInstanceLayerProperties(&lc,nullptr);
    std::vector<VkLayerProperties> al(lc); vkEnumerateInstanceLayerProperties(&lc,al.data());
    std::vector<const char*> layers;
    for (auto& l:al) if(!strcmp(l.layerName,"VK_LAYER_KHRONOS_validation")){
        layers.push_back("VK_LAYER_KHRONOS_validation"); break;}

    VkApplicationInfo appInfo{.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName=title.c_str(),.applicationVersion=VK_MAKE_VERSION(1,0,0),
        .pEngineName="XZRenderer",.engineVersion=VK_MAKE_VERSION(1,0,0),.apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici{.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&appInfo,
        .enabledLayerCount=(uint32_t)layers.size(),.ppEnabledLayerNames=layers.data(),
        .enabledExtensionCount=(uint32_t)eiexts.size(),.ppEnabledExtensionNames=eiexts.data()};
#ifdef __APPLE__
    ici.flags=VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    check_vk(vkCreateInstance(&ici,nullptr,&r->instance),"instance");
    if (!SDL_Vulkan_CreateSurface(r->window,r->instance,nullptr,&r->surface))
    {SDL_Log("Surface: %s",SDL_GetError());exit(1);}

    uint32_t dc=0; vkEnumeratePhysicalDevices(r->instance,&dc,nullptr);
    std::vector<VkPhysicalDevice> devs(dc); vkEnumeratePhysicalDevices(r->instance,&dc,devs.data());
    r->physical_device=devs[0];
    uint32_t qc=0; vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device,&qc,nullptr);
    std::vector<VkQueueFamilyProperties> qf(qc);
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device,&qc,qf.data());
    for (uint32_t i=0;i<qc;i++) if(qf[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){r->graphics_family=i;break;}

    VkSurfaceCapabilitiesKHR sc;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r->physical_device,r->surface,&sc);
    r->swapchain_extent=sc.currentExtent;
    r->image_count=sc.minImageCount;
    if (sc.maxImageCount>0&&r->image_count>sc.maxImageCount) r->image_count=sc.maxImageCount;

    uint32_t fc=0; vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device,r->surface,&fc,nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fc);
    vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device,r->surface,&fc,fmts.data());
    r->surface_format=fmts[0];
    for (auto pf:{VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM})
        for (auto& f:fmts) if(f.format==pf){r->surface_format=f;goto fmt_done;}
    fmt_done:;

    float prio=1.0f;
    VkDeviceQueueCreateInfo qi{.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=r->graphics_family,.queueCount=1,.pQueuePriorities=&prio};
    std::vector<const char*> dexts={VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#ifdef __APPLE__
    dexts.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif
    VkPhysicalDeviceDynamicRenderingFeatures drf{
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,.dynamicRendering=VK_TRUE};
    VkDeviceCreateInfo di{.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&drf,
        .queueCreateInfoCount=1,.pQueueCreateInfos=&qi,
        .enabledExtensionCount=(uint32_t)dexts.size(),.ppEnabledExtensionNames=dexts.data()};
    check_vk(vkCreateDevice(r->physical_device,&di,nullptr,&r->device),"device");
    vkGetDeviceQueue(r->device,r->graphics_family,0,&r->graphics_queue);

    VkCompositeAlphaFlagBitsKHR alpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if(sc.supportedCompositeAlpha&VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
        alpha=VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    VkImageUsageFlags iu=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if(sc.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_SRC_BIT) iu|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if(sc.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT) iu|=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkSwapchainCreateInfoKHR sci{.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface=r->surface,.minImageCount=r->image_count,
        .imageFormat=r->surface_format.format,.imageColorSpace=r->surface_format.colorSpace,
        .imageExtent=r->swapchain_extent,.imageArrayLayers=1,.imageUsage=iu,
        .imageSharingMode=VK_SHARING_MODE_EXCLUSIVE,.preTransform=sc.currentTransform,
        .compositeAlpha=alpha,.presentMode=VK_PRESENT_MODE_FIFO_KHR,.clipped=VK_TRUE};
    check_vk(vkCreateSwapchainKHR(r->device,&sci,nullptr,&r->swapchain),"swapchain");
    vkGetSwapchainImagesKHR(r->device,r->swapchain,&r->image_count,nullptr);
    r->images.resize(r->image_count); r->image_views.resize(r->image_count);
    vkGetSwapchainImagesKHR(r->device,r->swapchain,&r->image_count,r->images.data());
    for (uint32_t i=0;i<r->image_count;i++){
        VkImageViewCreateInfo vi{.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=r->images[i],
            .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=r->surface_format.format,
            .components={VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        check_vk(vkCreateImageView(r->device,&vi,nullptr,&r->image_views[i]),"image view");
    }

    VkCommandPoolCreateInfo cpi{.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=r->graphics_family};
    check_vk(vkCreateCommandPool(r->device,&cpi,nullptr,&r->command_pool),"cmd pool");

    // Depth buffer
    {
        VkImageCreateInfo ii{.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
            .format=DEPTH_FORMAT,.extent={r->swapchain_extent.width,r->swapchain_extent.height,1},
            .mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
            .usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
        check_vk(vkCreateImage(r->device,&ii,nullptr,&r->depth.image),"depth img");
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(r->device,r->depth.image,&mr);
        VkMemoryAllocateInfo ma{.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,
            .memoryTypeIndex=find_memory_type(r->physical_device,mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        check_vk(vkAllocateMemory(r->device,&ma,nullptr,&r->depth.memory),"depth mem");
        vkBindImageMemory(r->device,r->depth.image,r->depth.memory,0);
        VkImageViewCreateInfo vi{.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=r->depth.image,
            .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=DEPTH_FORMAT,
            .subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
        check_vk(vkCreateImageView(r->device,&vi,nullptr,&r->depth.view),"depth view");
        VkCommandBuffer cmd=begin_one_time(r);
        VkImageMemoryBarrier bar={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask=0,.dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=r->depth.image,.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,0,0,nullptr,0,nullptr,1,&bar);
        end_one_time(r,cmd);
    }

    // Main descriptor set layout
    {
        VkDescriptorSetLayoutBinding b[]={
            {.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,
             .stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT},
            {.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,
             .stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT}};
        VkDescriptorSetLayoutCreateInfo li{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount=2,.pBindings=b};
        check_vk(vkCreateDescriptorSetLayout(r->device,&li,nullptr,&r->main_desc_layout),"main layout");
    }

    VkSemaphoreCreateInfo semi{.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,.flags=VK_FENCE_CREATE_SIGNALED_BIT};
    r->image_available_sems.resize(r->image_count);
    r->render_finished_sems.resize(r->image_count);
    for (uint32_t i=0;i<r->image_count;i++){
        check_vk(vkCreateSemaphore(r->device,&semi,nullptr,&r->image_available_sems[i]),"sem");
        check_vk(vkCreateSemaphore(r->device,&semi,nullptr,&r->render_finished_sems[i]),"sem");
    }
    check_vk(vkCreateFence(r->device,&fi,nullptr,&r->in_flight_fence),"fence");

    r->scene_cmds.resize(r->image_count); r->imgui_cmds.resize(r->image_count);
    VkCommandBufferAllocateInfo cba{.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=r->command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=r->image_count};
    check_vk(vkAllocateCommandBuffers(r->device,&cba,r->scene_cmds.data()),"scene cmds");
    check_vk(vkAllocateCommandBuffers(r->device,&cba,r->imgui_cmds.data()),"imgui cmds");

    for (int i=0;i<4;i++) r->clear_color[i]=cc[i];

    // Create color target before pipelines (post pipeline needs it)
    create_color_target(r);

    build_main_pipeline(r);
    build_post_pipeline(r);

    xz_logf("Swapchain %ux%u count=%d",r->swapchain_extent.width,r->swapchain_extent.height,r->image_count);
}

static void build_main_pipeline(RendererImpl* r) {
    VkShaderModule vm=load_shader(r->device,SHADER_OUTPUT_DIR "vert.spv");
    VkShaderModule fm=load_shader(r->device,SHADER_OUTPUT_DIR "frag.spv");
    VkPipelineLayoutCreateInfo pli{.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&r->main_desc_layout};
    check_vk(vkCreatePipelineLayout(r->device,&pli,nullptr,&r->main_pipeline_layout),"main pl");
    VkVertexInputBindingDescription vbd{.binding=0,.stride=sizeof(Vertex),.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vad[3]={
        {.location=0,.binding=0,.format=VK_FORMAT_R32G32B32_SFLOAT,.offset=offsetof(Vertex,position)},
        {.location=1,.binding=0,.format=VK_FORMAT_R32G32_SFLOAT,   .offset=offsetof(Vertex,uv)},
        {.location=2,.binding=0,.format=VK_FORMAT_R32G32B32_SFLOAT,.offset=offsetof(Vertex,normal)}};
    VkPipelineVertexInputStateCreateInfo vi{.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&vbd,
        .vertexAttributeDescriptionCount=3,.pVertexAttributeDescriptions=vad};
    VkPipelineInputAssemblyStateCreateInfo ia{.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport vp{.x=0,.y=0,.width=(float)r->swapchain_extent.width,
        .height=(float)r->swapchain_extent.height,.minDepth=0,.maxDepth=1};
    VkRect2D sc{{0,0},r->swapchain_extent};
    VkPipelineViewportStateCreateInfo vs{.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&sc};
    VkPipelineRasterizationStateCreateInfo rs{.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_BACK_BIT,
        .frontFace=VK_FRONT_FACE_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms{.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo ds{.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,.depthCompareOp=VK_COMPARE_OP_LESS};
    VkPipelineColorBlendAttachmentState cba{.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
        VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cbs{.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&cba};
    // Scene renders to color_target format
    VkPipelineRenderingCreateInfo pri{.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount=1,.pColorAttachmentFormats=&COLOR_TARGET_FMT,
        .depthAttachmentFormat=DEPTH_FORMAT};
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vm,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fm,.pName="main"}};
    VkGraphicsPipelineCreateInfo gpi{.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext=&pri,.stageCount=2,.pStages=stages,
        .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vs,
        .pRasterizationState=&rs,.pMultisampleState=&ms,.pDepthStencilState=&ds,
        .pColorBlendState=&cbs,.layout=r->main_pipeline_layout};
    check_vk(vkCreateGraphicsPipelines(r->device,VK_NULL_HANDLE,1,&gpi,nullptr,&r->main_pipeline),"main pipeline");
    vkDestroyShaderModule(r->device,vm,nullptr);
    vkDestroyShaderModule(r->device,fm,nullptr);
}

static void build_quad_pipeline(RendererImpl* r, CustomShaderQuadImpl* m, const std::string& frag_path) {
    m->desc_layout=make_ubo_desc_layout(r,VK_SHADER_STAGE_VERTEX_BIT);
    alloc_ubo_frames(r,m->desc_layout,m->descriptor_pool,m->frames);
    m->renderer=r;
    VkPipelineLayoutCreateInfo pli{.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&m->desc_layout};
    check_vk(vkCreatePipelineLayout(r->device,&pli,nullptr,&m->pipeline_layout),"quad pl");
    VkShaderModule vm=load_shader(r->device,SHADER_OUTPUT_DIR "quad.spv");
    VkShaderModule fm=load_shader(r->device,frag_path.c_str());
    struct QV{glm::vec3 p;glm::vec2 u;};
    VkVertexInputBindingDescription qbd{.binding=0,.stride=sizeof(QV),.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription qad[2]={
        {.location=0,.binding=0,.format=VK_FORMAT_R32G32B32_SFLOAT,.offset=offsetof(QV,p)},
        {.location=1,.binding=0,.format=VK_FORMAT_R32G32_SFLOAT,   .offset=offsetof(QV,u)}};
    VkPipelineVertexInputStateCreateInfo vi{.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&qbd,
        .vertexAttributeDescriptionCount=2,.pVertexAttributeDescriptions=qad};
    VkPipelineInputAssemblyStateCreateInfo ia{.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport vp{.x=0,.y=0,.width=(float)r->swapchain_extent.width,
        .height=(float)r->swapchain_extent.height,.minDepth=0,.maxDepth=1};
    VkRect2D sc{{0,0},r->swapchain_extent};
    VkPipelineViewportStateCreateInfo vs{.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&sc};
    VkPipelineRasterizationStateCreateInfo rs{.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,
        .frontFace=VK_FRONT_FACE_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms{.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo ds{.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_TRUE,.depthWriteEnable=VK_FALSE,.depthCompareOp=VK_COMPARE_OP_LESS};
    VkPipelineColorBlendAttachmentState cba{.blendEnable=VK_TRUE,
        .srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA,.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp=VK_BLEND_OP_ADD,.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO,.alphaBlendOp=VK_BLEND_OP_ADD,
        .colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cbs{.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&cba};
    // Quad also renders to color_target
    VkPipelineRenderingCreateInfo pri{.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount=1,.pColorAttachmentFormats=&COLOR_TARGET_FMT,.depthAttachmentFormat=DEPTH_FORMAT};
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vm,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fm,.pName="main"}};
    VkGraphicsPipelineCreateInfo gpi{.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext=&pri,.stageCount=2,.pStages=stages,
        .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vs,
        .pRasterizationState=&rs,.pMultisampleState=&ms,.pDepthStencilState=&ds,
        .pColorBlendState=&cbs,.layout=m->pipeline_layout};
    check_vk(vkCreateGraphicsPipelines(r->device,VK_NULL_HANDLE,1,&gpi,nullptr,&m->pipeline),"quad pipeline");
    vkDestroyShaderModule(r->device,vm,nullptr);
    vkDestroyShaderModule(r->device,fm,nullptr);
}

static void build_points_pipeline(RendererImpl* r, CustomShaderPoints3dImpl* m, const std::string& frag_path) {
    m->desc_layout=make_ubo_desc_layout(r,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT);
    alloc_ubo_frames(r,m->desc_layout,m->descriptor_pool,m->frames);
    m->renderer=r;
    VkPipelineLayoutCreateInfo pli{.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&m->desc_layout};
    check_vk(vkCreatePipelineLayout(r->device,&pli,nullptr,&m->pipeline_layout),"points pl");
    VkShaderModule vm=load_shader(r->device,SHADER_OUTPUT_DIR "particles3d.spv");
    VkShaderModule fm=load_shader(r->device,frag_path.c_str());
    VkVertexInputBindingDescription vbd{.binding=0,.stride=sizeof(glm::vec3),.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vad{.location=0,.binding=0,.format=VK_FORMAT_R32G32B32_SFLOAT,.offset=0};
    VkPipelineVertexInputStateCreateInfo vi{.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&vbd,
        .vertexAttributeDescriptionCount=1,.pVertexAttributeDescriptions=&vad};
    VkPipelineInputAssemblyStateCreateInfo ia{.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST};
    VkViewport vp{.x=0,.y=0,.width=(float)r->swapchain_extent.width,
        .height=(float)r->swapchain_extent.height,.minDepth=0,.maxDepth=1};
    VkRect2D sc{{0,0},r->swapchain_extent};
    VkPipelineViewportStateCreateInfo vs{.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&sc};
    VkPipelineRasterizationStateCreateInfo rs{.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,
        .frontFace=VK_FRONT_FACE_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms{.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo ds{.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_TRUE,.depthWriteEnable=VK_FALSE,.depthCompareOp=VK_COMPARE_OP_LESS};
    VkPipelineColorBlendAttachmentState cba{.blendEnable=VK_TRUE,
        .srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA,.dstColorBlendFactor=VK_BLEND_FACTOR_ONE,
        .colorBlendOp=VK_BLEND_OP_ADD,.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO,.alphaBlendOp=VK_BLEND_OP_ADD,
        .colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cbs{.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&cba};
    // Points also render to color_target
    VkPipelineRenderingCreateInfo pri{.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount=1,.pColorAttachmentFormats=&COLOR_TARGET_FMT,.depthAttachmentFormat=DEPTH_FORMAT};
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vm,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fm,.pName="main"}};
    VkGraphicsPipelineCreateInfo gpi{.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext=&pri,.stageCount=2,.pStages=stages,
        .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vs,
        .pRasterizationState=&rs,.pMultisampleState=&ms,.pDepthStencilState=&ds,
        .pColorBlendState=&cbs,.layout=m->pipeline_layout};
    check_vk(vkCreateGraphicsPipelines(r->device,VK_NULL_HANDLE,1,&gpi,nullptr,&m->pipeline),"points pipeline");
    vkDestroyShaderModule(r->device,vm,nullptr);
    vkDestroyShaderModule(r->device,fm,nullptr);
}

// ============================================================
//  record_scene  — renders to color_target (offscreen)
// ============================================================
static glm::mat4 compute_model(const glm::vec3& p, const glm::vec3& r, const glm::vec3& s) {
    glm::mat4 m=glm::translate(glm::mat4(1),p);
    m=glm::rotate(m,glm::radians(r.x),glm::vec3(1,0,0));
    m=glm::rotate(m,glm::radians(r.y),glm::vec3(0,1,0));
    m=glm::rotate(m,glm::radians(r.z),glm::vec3(0,0,1));
    return glm::scale(m,s);
}

static void record_scene(RendererImpl* r,
    const std::vector<MeshObject*>&            meshes,
    const std::vector<CustomShaderQuad*>&      quads,
    const std::vector<CustomShaderPoints3d*>&  points_clusters)
{
    for (uint32_t i=0;i<r->image_count;i++){
        VkCommandBufferBeginInfo bi{.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        check_vk(vkBeginCommandBuffer(r->scene_cmds[i],&bi),"begin scene");

        // Transition color_target: SHADER_READ → COLOR_ATTACHMENT so we can write to it
        VkImageMemoryBarrier to_write={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask=VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=r->color_target.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        vkCmdPipelineBarrier(r->scene_cmds[i],
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,0,nullptr,0,nullptr,1,&to_write);

        VkClearValue cv_col{}; memcpy(cv_col.color.float32,r->clear_color,sizeof(float)*4);
        VkClearValue cv_dep{}; cv_dep.depthStencil={1.0f,0};

        // Render to color_target, not swapchain image
        VkRenderingAttachmentInfo ca{.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView=r->color_target.view,
            .imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.clearValue=cv_col};
        VkRenderingAttachmentInfo da{.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView=r->depth.view,.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.clearValue=cv_dep};
        VkRenderingInfo ri{.sType=VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea={{0,0},r->swapchain_extent},.layerCount=1,
            .colorAttachmentCount=1,.pColorAttachments=&ca,.pDepthAttachment=&da};
        vkCmdBeginRendering(r->scene_cmds[i],&ri);

        VkDeviceSize offsets[]={0};

        // Opaque meshes
        vkCmdBindPipeline(r->scene_cmds[i],VK_PIPELINE_BIND_POINT_GRAPHICS,r->main_pipeline);
        for (MeshObject* obj:meshes){
            auto* m=obj->impl(); if (!m) continue;
            vkCmdBindDescriptorSets(r->scene_cmds[i],VK_PIPELINE_BIND_POINT_GRAPHICS,
                r->main_pipeline_layout,0,1,&m->frames[i].descriptor_set,0,nullptr);
            if (m->is_gltf){
                for (auto& sm:m->sub_meshes){
                    vkCmdBindVertexBuffers(r->scene_cmds[i],0,1,&sm.vertex_buffer.buffer,offsets);
                    vkCmdBindIndexBuffer(r->scene_cmds[i],sm.index_buffer.buffer,0,VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(r->scene_cmds[i],sm.index_count,1,0,0,0);
                }
            } else {
                VkBuffer vb=m->mesh_vertex.buffer, ib=m->mesh_index.buffer;
                if (vb==VK_NULL_HANDLE) continue;
                vkCmdBindVertexBuffers(r->scene_cmds[i],0,1,&vb,offsets);
                vkCmdBindIndexBuffer(r->scene_cmds[i],ib,0,VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(r->scene_cmds[i],m->raw_index_count,1,0,0,0);
            }
        }

        // Alpha-blended quads
        for (CustomShaderQuad* q:quads){
            auto* m=q->impl(); if (!m||m->index_count==0) continue;
            vkCmdBindPipeline(r->scene_cmds[i],VK_PIPELINE_BIND_POINT_GRAPHICS,m->pipeline);
            vkCmdBindVertexBuffers(r->scene_cmds[i],0,1,&m->vertex_buffer.buffer,offsets);
            vkCmdBindIndexBuffer(r->scene_cmds[i],m->index_buffer.buffer,0,VK_INDEX_TYPE_UINT32);
            vkCmdBindDescriptorSets(r->scene_cmds[i],VK_PIPELINE_BIND_POINT_GRAPHICS,
                m->pipeline_layout,0,1,&m->frames[i].descriptor_set,0,nullptr);
            vkCmdDrawIndexed(r->scene_cmds[i],m->index_count,1,0,0,0);
        }

        // Additive points
        for (CustomShaderPoints3d* pc:points_clusters){
            if (!pc->isVisible()) continue;
            auto* m=pc->impl(); if (!m||m->alive_count==0) continue;
            vkCmdBindPipeline(r->scene_cmds[i],VK_PIPELINE_BIND_POINT_GRAPHICS,m->pipeline);
            vkCmdBindVertexBuffers(r->scene_cmds[i],0,1,&m->vertex_buffer.buffer,offsets);
            vkCmdBindDescriptorSets(r->scene_cmds[i],VK_PIPELINE_BIND_POINT_GRAPHICS,
                m->pipeline_layout,0,1,&m->frames[i].descriptor_set,0,nullptr);
            vkCmdDraw(r->scene_cmds[i],m->alive_count,1,0,0);
        }

        vkCmdEndRendering(r->scene_cmds[i]);

        // Transition color_target: COLOR_ATTACHMENT → SHADER_READ so post pass can sample it
        VkImageMemoryBarrier to_read={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
            .oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=r->color_target.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        vkCmdPipelineBarrier(r->scene_cmds[i],
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,0,nullptr,0,nullptr,1,&to_read);

        // ---- Post-process pass — reads color_target, writes to swapchain image ----
        VkImageMemoryBarrier swapchain_write={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask=0,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=r->images[i],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        vkCmdPipelineBarrier(r->scene_cmds[i],
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,0,nullptr,0,nullptr,1,&swapchain_write);

        VkRenderingAttachmentInfo post_ca{.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView=r->image_views[i],.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_STORE};
        VkRenderingInfo post_ri{.sType=VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea={{0,0},r->swapchain_extent},.layerCount=1,
            .colorAttachmentCount=1,.pColorAttachments=&post_ca};
        vkCmdBeginRendering(r->scene_cmds[i],&post_ri);
        vkCmdBindPipeline(r->scene_cmds[i],VK_PIPELINE_BIND_POINT_GRAPHICS,r->post_pipeline);
        vkCmdBindDescriptorSets(r->scene_cmds[i],VK_PIPELINE_BIND_POINT_GRAPHICS,
            r->post_pipeline_layout,0,1,&r->post_frames[i].descriptor_set,0,nullptr);
        vkCmdDraw(r->scene_cmds[i],3,1,0,0);  // fullscreen triangle — no vertex buffer
        vkCmdEndRendering(r->scene_cmds[i]);

        check_vk(vkEndCommandBuffer(r->scene_cmds[i]),"end scene");
    }
}

// ============================================================
//  PointLight
// ============================================================
void PointLight::setPosition(float x,float y,float z){position_={x,y,z};}
void PointLight::setPosition(const glm::vec3& p){position_=p;}

// ============================================================
//  MeshObject
// ============================================================
MeshObject::MeshObject(RendererImpl* renderer_impl,
    const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
    const std::string& texture_path)
{
    impl_=std::make_unique<MeshObjectImpl>();
    impl_->renderer=renderer_impl;
    loadFromVertices(vertices,indices,texture_path);
}
MeshObject::MeshObject(RendererImpl* renderer_impl,
    const std::string& mesh_path, const std::string& texture_path)
{
    impl_=std::make_unique<MeshObjectImpl>();
    impl_->renderer=renderer_impl;
    loadFromGLTF(mesh_path,texture_path);
}
MeshObject::~MeshObject(){
    if (!impl_) return;
    RendererImpl* ri=impl_->renderer; if (!ri) return;
    vkDeviceWaitIdle(ri->device);
    destroy_buffer(ri->device,impl_->mesh_vertex);
    destroy_buffer(ri->device,impl_->mesh_index);
    for (auto& sm:impl_->sub_meshes){destroy_buffer(ri->device,sm.vertex_buffer);destroy_buffer(ri->device,sm.index_buffer);}
    impl_->sub_meshes.clear();
    destroy_texture(ri->device,impl_->texture);
    for (auto& f:impl_->frames) destroy_buffer(ri->device,f.buffer);
    impl_->frames.clear();
    if (impl_->descriptor_pool!=VK_NULL_HANDLE){
        vkDestroyDescriptorPool(ri->device,impl_->descriptor_pool,nullptr);
        impl_->descriptor_pool=VK_NULL_HANDLE;}
}
void MeshObject::setPosition(float x,float y,float z){position_={x,y,z};}
void MeshObject::setPosition(const glm::vec3& p){position_=p;}
void MeshObject::setRotation(float x,float y,float z){rotation_={x,y,z};}
void MeshObject::setRotation(const glm::vec3& r){rotation_=r;}
void MeshObject::setScale(float u){scale_={u,u,u};}
void MeshObject::setScale(float x,float y,float z){scale_={x,y,z};}
void MeshObject::setScale(const glm::vec3& s){scale_=s;}
const glm::vec3 MeshObject::getForward() const {
    glm::mat4 m=glm::mat4(1);
    m=glm::rotate(m,glm::radians(rotation_.y),glm::vec3(0,1,0));
    m=glm::rotate(m,glm::radians(rotation_.x),glm::vec3(1,0,0));
    m=glm::rotate(m,glm::radians(rotation_.z),glm::vec3(0,0,1));
    return -glm::normalize(glm::vec3(m[2]));
}
const glm::vec3 MeshObject::getRight() const {
    glm::mat4 m=glm::mat4(1);
    m=glm::rotate(m,glm::radians(rotation_.y),glm::vec3(0,1,0));
    m=glm::rotate(m,glm::radians(rotation_.x),glm::vec3(1,0,0));
    m=glm::rotate(m,glm::radians(rotation_.z),glm::vec3(0,0,1));
    return glm::normalize(glm::vec3(m[0]));
}
void MeshObject::loadFromGLTF(const std::string& mesh_path, const std::string& texture_path){
    auto* m=impl_.get(); RendererImpl* r=m->renderer;
    if (!r){SDL_Log("XZR: call loadFromGLTF after init()");exit(1);}
    m->is_gltf=true; m->texture=load_texture(r,texture_path);
    Assimp::Importer imp;
    const aiScene* scene=imp.ReadFile(mesh_path.c_str(),
        aiProcess_Triangulate|aiProcess_GenNormals|aiProcess_FlipUVs|
        aiProcess_CalcTangentSpace|aiProcess_FlipWindingOrder);
    if (!scene||scene->mFlags&AI_SCENE_FLAGS_INCOMPLETE||!scene->mRootNode){
        SDL_Log("XZR Assimp: %s",imp.GetErrorString());exit(1);}
    for (uint32_t mi=0;mi<scene->mNumMeshes;mi++){
        const aiMesh* am=scene->mMeshes[mi];
        std::vector<Vertex> verts; verts.reserve(am->mNumVertices);
        for (uint32_t vi=0;vi<am->mNumVertices;vi++){
            Vertex v;
            v.position={am->mVertices[vi].x,am->mVertices[vi].y,am->mVertices[vi].z};
            v.normal=am->HasNormals()?glm::vec3{am->mNormals[vi].x,am->mNormals[vi].y,am->mNormals[vi].z}:glm::vec3{0,1,0};
            v.uv=am->HasTextureCoords(0)?glm::vec2{am->mTextureCoords[0][vi].x,am->mTextureCoords[0][vi].y}:glm::vec2{0,0};
            verts.push_back(v);}
        std::vector<uint32_t> idxs; idxs.reserve(am->mNumFaces*3);
        for (uint32_t fi=0;fi<am->mNumFaces;fi++)
            for (uint32_t ii=0;ii<am->mFaces[fi].mNumIndices;ii++)
                idxs.push_back(am->mFaces[fi].mIndices[ii]);
        GpuSubMesh sm; sm.index_count=(uint32_t)idxs.size();
        VkDeviceSize vsz=verts.size()*sizeof(Vertex),isz=idxs.size()*sizeof(uint32_t);
        sm.vertex_buffer=make_buffer(r->device,r->physical_device,vsz,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        upload(r->device,sm.vertex_buffer,verts.data(),vsz);
        sm.index_buffer=make_buffer(r->device,r->physical_device,isz,VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        upload(r->device,sm.index_buffer,idxs.data(),isz);
        m->sub_meshes.push_back(sm);}
    alloc_mesh_frames(r,m);
}
void MeshObject::loadFromVertices(const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices, const std::string& texture_path)
{
    MeshObjectImpl* mi=impl_.get(); RendererImpl* ri=mi->renderer;
    if (!ri){SDL_Log("XZR: call loadFromVertices after init()");exit(1);}
    mi->is_gltf=false; mi->raw_index_count=(uint32_t)indices.size();
    mi->texture=load_texture(ri,texture_path);
    VkDeviceSize vsz=vertices.size()*sizeof(Vertex),isz=indices.size()*sizeof(uint32_t);
    mi->mesh_vertex=make_buffer(ri->device,ri->physical_device,vsz,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    upload(ri->device,mi->mesh_vertex,vertices.data(),vsz);
    mi->mesh_index=make_buffer(ri->device,ri->physical_device,isz,VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    upload(ri->device,mi->mesh_index,indices.data(),isz);
    alloc_mesh_frames(ri,mi);
}

// ============================================================
//  CustomShaderQuad
// ============================================================
CustomShaderQuad::CustomShaderQuad(const std::string& p):frag_spv_path_(p){}
void CustomShaderQuad::setVertices(const std::vector<glm::vec3>& positions,
    const std::vector<glm::vec2>& uvs, const std::vector<uint32_t>& indices)
{
    auto* m=impl_.get(); RendererImpl* r=m->renderer;
    if (!r){SDL_Log("XZR: call setVertices after init()");exit(1);}
    struct QV{glm::vec3 p;glm::vec2 u;};
    std::vector<QV> verts; verts.reserve(positions.size());
    for (size_t i=0;i<positions.size();i++) verts.push_back({positions[i],uvs[i]});
    m->index_count=(uint32_t)indices.size();
    VkDeviceSize vsz=verts.size()*sizeof(QV),isz=indices.size()*sizeof(uint32_t);
    m->vertex_buffer=make_buffer(r->device,r->physical_device,vsz,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    upload(r->device,m->vertex_buffer,verts.data(),vsz);
    m->index_buffer=make_buffer(r->device,r->physical_device,isz,VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    upload(r->device,m->index_buffer,indices.data(),isz);
}
void CustomShaderQuad::setPosition(float x,float y,float z){position_={x,y,z};}
void CustomShaderQuad::setPosition(const glm::vec3& p){position_=p;}
void CustomShaderQuad::setRotation(float x,float y,float z){rotation_={x,y,z};}
void CustomShaderQuad::setRotation(const glm::vec3& r){rotation_=r;}
void CustomShaderQuad::setScale(float u){scale_={u,u,1.0f};}
void CustomShaderQuad::setScale(float x,float y,float z){scale_={x,y,z};}
void CustomShaderQuad::setScale(const glm::vec3& s){scale_=s;}

// ============================================================
//  CustomShaderPoints3d
// ============================================================
CustomShaderPoints3d::CustomShaderPoints3d(const std::string& p):frag_spv_path_(p){}
void CustomShaderPoints3d::setPositions(const std::vector<glm::vec3>& positions){
    CustomShaderPoints3dImpl* m=impl_.get(); if (!m) return;
    uint32_t count=(uint32_t)std::min(positions.size(),(size_t)m->max_count);
    m->alive_count=count; positions_=positions;
    if (count==0) return;
    VkDeviceSize upload_size=count*sizeof(glm::vec3);
    void* mapped;
    vkMapMemory(m->renderer->device,m->vertex_buffer.memory,0,upload_size,0,&mapped);
    memcpy(mapped,positions.data(),upload_size);
    vkUnmapMemory(m->renderer->device,m->vertex_buffer.memory);
}

// ============================================================
//  ImGuiLayer
// ============================================================
void ImGuiLayer::exposeClearColor(const std::string& label){
    if (renderer_impl_) ImGui::ColorEdit4(label.c_str(),renderer_impl_->clear_color);}
void ImGuiLayer::exposeTransformation(MeshObject& o, const std::string& label){
    if (ImGui::CollapsingHeader(label.c_str())){
        glm::vec3 p=o.getPosition(),rt=o.getRotation(),s=o.getScale();
        if(ImGui::SliderFloat3((label+" Pos").c_str(),&p.x,-10,10))   o.setPosition(p);
        if(ImGui::SliderFloat3((label+" Rot").c_str(),&rt.x,-180,180)) o.setRotation(rt);
        if(ImGui::SliderFloat3((label+" Scl").c_str(),&s.x,0.01f,5))  o.setScale(s);}}
void ImGuiLayer::exposeTransformation(CustomShaderQuad& q, const std::string& label){
    if (ImGui::CollapsingHeader(label.c_str())){
        glm::vec3 p=q.getPosition(),rt=q.getRotation(),s=q.getScale();
        if(ImGui::SliderFloat3((label+" Pos").c_str(),&p.x,-10,10))   q.setPosition(p);
        if(ImGui::SliderFloat3((label+" Rot").c_str(),&rt.x,-180,180)) q.setRotation(rt);
        if(ImGui::SliderFloat3((label+" Scl").c_str(),&s.x,0.01f,5))  q.setScale(s);}}
void ImGuiLayer::exposeLight(PointLight& l, const std::string& label){
    if (ImGui::CollapsingHeader(label.c_str())){
        glm::vec3 p=l.getPosition();
        if(ImGui::SliderFloat3((label+" Pos").c_str(),&p.x,-10,10)) l.setPosition(p);}}
void ImGuiLayer::exposeCamera(const std::string& label){
    if (ImGui::CollapsingHeader(label.c_str())){
        glm::vec3 p=renderer_impl_->camera_pos,t=renderer_impl_->camera_target;
        if(ImGui::SliderFloat3((label+" Pos").c_str(),&p.x,-20,20)) renderer_impl_->camera_pos=p;
        if(ImGui::SliderFloat3((label+" Target").c_str(),&t.x,-20,20)) renderer_impl_->camera_target=t;}}
bool ImGuiLayer::exposeButton(const std::string& label){return ImGui::Button(label.c_str());}
void ImGuiLayer::beginWindow(const std::string& t){ImGui::Begin(t.c_str(),nullptr,ImGuiWindowFlags_AlwaysAutoResize);}
void ImGuiLayer::endWindow(){ImGui::End();}
void ImGuiLayer::separator(){ImGui::Separator();}
void ImGuiLayer::text(const std::string& s){ImGui::Text("%s",s.c_str());}
void ImGuiLayer::showFPS(){ImGui::Text("%.1f FPS",ImGui::GetIO().Framerate);}

// ============================================================
//  Renderer
// ============================================================
Renderer::Renderer(uint32_t width, uint32_t height, const std::string& title)
    : m_impl(std::make_unique<RendererImpl>())
    , m_gui(std::make_unique<ImGuiLayer>())
{
    m_window_width=width; m_window_height=height; m_window_title=title;
}

Renderer::~Renderer(){
    if (!m_impl||m_impl->device==VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_impl->device);

    for (auto& obj:m_mesh_objects){
        auto* m=obj->impl(); if (!m) continue;
        for (auto& f:m->frames) destroy_buffer(m_impl->device,f.buffer);
        if (m->descriptor_pool!=VK_NULL_HANDLE) vkDestroyDescriptorPool(m_impl->device,m->descriptor_pool,nullptr);
        destroy_texture(m_impl->device,m->texture);
        destroy_buffer(m_impl->device,m->mesh_vertex); destroy_buffer(m_impl->device,m->mesh_index);
        for (auto& sm:m->sub_meshes){destroy_buffer(m_impl->device,sm.vertex_buffer);destroy_buffer(m_impl->device,sm.index_buffer);}
    }
    for (auto& q:m_quads){
        auto* m=q->impl(); if (!m) continue;
        for (auto& f:m->frames) destroy_buffer(m_impl->device,f.buffer);
        if (m->descriptor_pool!=VK_NULL_HANDLE) vkDestroyDescriptorPool(m_impl->device,m->descriptor_pool,nullptr);
        if (m->desc_layout    !=VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_impl->device,m->desc_layout,nullptr);
        if (m->pipeline       !=VK_NULL_HANDLE) vkDestroyPipeline(m_impl->device,m->pipeline,nullptr);
        if (m->pipeline_layout!=VK_NULL_HANDLE) vkDestroyPipelineLayout(m_impl->device,m->pipeline_layout,nullptr);
        destroy_buffer(m_impl->device,m->vertex_buffer); destroy_buffer(m_impl->device,m->index_buffer);
    }
    for (auto& pc:m_points_clusters){
        auto* m=pc->impl(); if (!m) continue;
        for (auto& f:m->frames) destroy_buffer(m_impl->device,f.buffer);
        if (m->descriptor_pool!=VK_NULL_HANDLE) vkDestroyDescriptorPool(m_impl->device,m->descriptor_pool,nullptr);
        if (m->desc_layout    !=VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_impl->device,m->desc_layout,nullptr);
        if (m->pipeline       !=VK_NULL_HANDLE) vkDestroyPipeline(m_impl->device,m->pipeline,nullptr);
        if (m->pipeline_layout!=VK_NULL_HANDLE) vkDestroyPipelineLayout(m_impl->device,m->pipeline_layout,nullptr);
        destroy_buffer(m_impl->device,m->vertex_buffer);
    }

    // Post-process cleanup
    for (auto& f:m_impl->post_frames) destroy_buffer(m_impl->device,f.buffer);
    if (m_impl->post_descriptor_pool!=VK_NULL_HANDLE) vkDestroyDescriptorPool(m_impl->device,m_impl->post_descriptor_pool,nullptr);
    if (m_impl->post_desc_layout    !=VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_impl->device,m_impl->post_desc_layout,nullptr);
    if (m_impl->post_pipeline       !=VK_NULL_HANDLE) vkDestroyPipeline(m_impl->device,m_impl->post_pipeline,nullptr);
    if (m_impl->post_pipeline_layout!=VK_NULL_HANDLE) vkDestroyPipelineLayout(m_impl->device,m_impl->post_pipeline_layout,nullptr);
    destroy_texture(m_impl->device,m_impl->color_target);

    ImGui_ImplVulkan_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
    if (m_impl->imgui_pool!=VK_NULL_HANDLE) vkDestroyDescriptorPool(m_impl->device,m_impl->imgui_pool,nullptr);
    destroy_texture(m_impl->device,m_impl->depth);
    if (m_impl->main_desc_layout    !=VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_impl->device,m_impl->main_desc_layout,nullptr);
    if (m_impl->main_pipeline       !=VK_NULL_HANDLE) vkDestroyPipeline(m_impl->device,m_impl->main_pipeline,nullptr);
    if (m_impl->main_pipeline_layout!=VK_NULL_HANDLE) vkDestroyPipelineLayout(m_impl->device,m_impl->main_pipeline_layout,nullptr);
    for (auto& s:m_impl->image_available_sems) vkDestroySemaphore(m_impl->device,s,nullptr);
    for (auto& s:m_impl->render_finished_sems) vkDestroySemaphore(m_impl->device,s,nullptr);
    if (m_impl->in_flight_fence !=VK_NULL_HANDLE) vkDestroyFence(m_impl->device,m_impl->in_flight_fence,nullptr);
    if (m_impl->command_pool    !=VK_NULL_HANDLE) vkDestroyCommandPool(m_impl->device,m_impl->command_pool,nullptr);
    for (auto& iv:m_impl->image_views) vkDestroyImageView(m_impl->device,iv,nullptr);
    if (m_impl->swapchain!=VK_NULL_HANDLE) vkDestroySwapchainKHR(m_impl->device,m_impl->swapchain,nullptr);
    if (m_impl->device   !=VK_NULL_HANDLE) vkDestroyDevice(m_impl->device,nullptr);
    if (m_impl->surface  !=VK_NULL_HANDLE) vkDestroySurfaceKHR(m_impl->instance,m_impl->surface,nullptr);
    if (m_impl->instance !=VK_NULL_HANDLE) vkDestroyInstance(m_impl->instance,nullptr);
    if (m_impl->window){SDL_DestroyWindow(m_impl->window);SDL_Quit();}
}

void Renderer::setClearColor(float r,float g,float b,float a){
    m_impl->clear_color[0]=r;m_impl->clear_color[1]=g;
    m_impl->clear_color[2]=b;m_impl->clear_color[3]=a;}
void Renderer::setCameraPosition(glm::vec3 pos){m_impl->camera_pos=pos;}
void Renderer::setCameraTarget(glm::vec3 target){m_impl->camera_target=target;}
void Renderer::enableLogging(bool e){m_logging_enabled=e;g_log=e;}

void Renderer::setChromaticAberration(bool enabled, float strength){
    m_impl->post_data.chromatic_aberration_enabled  = enabled ? 1 : 0;
    m_impl->post_data.chromatic_aberration_strength = strength;
}
void Renderer::setRadialBlur(bool enabled, float strength, int samples){
    m_impl->post_data.radial_blur_enabled  = enabled ? 1 : 0;
    m_impl->post_data.radial_blur_strength = strength;
    m_impl->post_data.radial_blur_samples  = samples;
}

void Renderer::init(float guiSize){
    g_log=m_logging_enabled;
    vulkan_init(m_impl.get(),m_window_width,m_window_height,m_window_title,m_impl->clear_color);

    VkDescriptorPoolSize ps[]={
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE},
        {VK_DESCRIPTOR_TYPE_SAMPLER,      IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE}};
    VkDescriptorPoolCreateInfo dp{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets=IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE+IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE,
        .poolSizeCount=2,.pPoolSizes=ps};
    check_vk(vkCreateDescriptorPool(m_impl->device,&dp,nullptr,&m_impl->imgui_pool),"imgui pool");
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui::GetIO().FontGlobalScale=guiSize;
    ImGui::GetStyle().ScaleAllSizes(guiSize);
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForVulkan(m_impl->window);
    ImGui_ImplVulkan_InitInfo iv{};
    iv.ApiVersion=VK_API_VERSION_1_3;
    iv.Instance=m_impl->instance;iv.PhysicalDevice=m_impl->physical_device;
    iv.Device=m_impl->device;iv.QueueFamily=m_impl->graphics_family;
    iv.Queue=m_impl->graphics_queue;iv.DescriptorPool=m_impl->imgui_pool;
    iv.MinImageCount=m_impl->image_count;iv.ImageCount=m_impl->image_count;
    iv.UseDynamicRendering=true;
    iv.PipelineInfoMain.MSAASamples=VK_SAMPLE_COUNT_1_BIT;
    iv.PipelineInfoMain.PipelineRenderingCreateInfo={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount=1,.pColorAttachmentFormats=&m_impl->surface_format.format};
    ImGui_ImplVulkan_Init(&iv);
    m_gui->renderer_impl_=m_impl.get();
}

MeshObject& Renderer::createMeshObject(const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices, const std::string& texture_path)
{
    std::unique_ptr<MeshObject> o=std::make_unique<MeshObject>(m_impl.get(),vertices,indices,texture_path);
    MeshObject& ref=*o; m_mesh_objects.push_back(std::move(o)); return ref;
}
MeshObject& Renderer::createMeshObject(const std::string& mesh_path, const std::string& texture_path){
    std::unique_ptr<MeshObject> o=std::make_unique<MeshObject>(m_impl.get(),mesh_path,texture_path);
    MeshObject& ref=*o; m_mesh_objects.push_back(std::move(o)); return ref;
}
CustomShaderQuad& Renderer::createCustomShaderQuad(const std::string& frag_spv_path){
    const std::vector<glm::vec3> positions={{-0.5f,-0.5f,0},{0.5f,-0.5f,0},{0.5f,0.5f,0},{-0.5f,0.5f,0}};
    const std::vector<glm::vec2> uvs={{0,0},{1,0},{1,1},{0,1}};
    const std::vector<uint32_t>  indices={0,1,2,0,2,3};
    std::unique_ptr<CustomShaderQuad> q=std::make_unique<CustomShaderQuad>(frag_spv_path);
    q->impl_=std::make_unique<CustomShaderQuadImpl>();
    q->impl_->renderer=m_impl.get();
    build_quad_pipeline(m_impl.get(),q->impl_.get(),frag_spv_path);
    q->setVertices(positions,uvs,indices);
    CustomShaderQuad& ref=*q; m_quads.push_back(std::move(q)); return ref;
}
CustomShaderPoints3d& Renderer::createCustomShaderPoints3d(const std::string& frag_spv_path, uint32_t max_points){
    std::unique_ptr<CustomShaderPoints3d> pc=std::make_unique<CustomShaderPoints3d>(frag_spv_path);
    pc->impl_=std::make_unique<CustomShaderPoints3dImpl>();
    pc->impl_->renderer=m_impl.get();
    pc->impl_->max_count=max_points;
    pc->impl_->vertex_buffer=make_buffer(m_impl->device,m_impl->physical_device,
        max_points*sizeof(glm::vec3),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    build_points_pipeline(m_impl.get(),pc->impl_.get(),frag_spv_path);
    CustomShaderPoints3d& ref=*pc; m_points_clusters.push_back(std::move(pc)); return ref;
}
PointLight& Renderer::createPointLight(){
    m_lights.push_back(std::make_unique<PointLight>()); return *m_lights.back();}
ImGuiLayer& Renderer::getGui(){return *m_gui;}
bool Renderer::handleEvent(void* sdl_event){
    SDL_Event* e=static_cast<SDL_Event*>(sdl_event);
    ImGui_ImplSDL3_ProcessEvent(e);
    if (e->type==SDL_EVENT_QUIT){m_impl->should_close=true;return true;}
    return false;
}

bool Renderer::beginFrame(){
    if (m_impl->should_close) return false;
    vkWaitForFences(m_impl->device,1,&m_impl->in_flight_fence,VK_TRUE,UINT64_MAX);
    vkResetFences(m_impl->device,1,&m_impl->in_flight_fence);
    check_vk(vkAcquireNextImageKHR(m_impl->device,m_impl->swapchain,UINT64_MAX,
        m_impl->image_available_sems[m_impl->current_frame],VK_NULL_HANDLE,&m_impl->current_image),"acquire");

    uint32_t idx=m_impl->current_image;
    glm::mat4 view=glm::lookAt(m_impl->camera_pos,m_impl->camera_target,glm::vec3(0,1,0));
    glm::mat4 proj=glm::perspective(glm::radians(45.0f),
        (float)m_impl->swapchain_extent.width/(float)m_impl->swapchain_extent.height,0.1f,100.0f);
    proj[1][1]*=-1;
    glm::vec4 lp={2,2,-2,1};
    if (!m_lights.empty()) lp=glm::vec4(m_lights[0]->getPosition(),1.0f);
    glm::vec4 cp=glm::vec4(m_impl->camera_pos,1.0f);

    for (auto& obj:m_mesh_objects){
        auto* m=obj->impl(); if (!m||m->frames.empty()) continue;
        glm::mat4 mdl=compute_model(obj->getPosition(),obj->getRotation(),obj->getScale());
        UniformData u{}; u.mvp=proj*view*mdl;u.model=mdl;u.light_pos=lp;u.cam_pos=cp;
        memcpy(m->frames[idx].mapped,&u,sizeof(u));}
    for (auto& q:m_quads){
        auto* m=q->impl(); if (!m||m->frames.empty()) continue;
        glm::mat4 mdl=compute_model(q->getPosition(),q->getRotation(),q->getScale());
        UniformData u{}; u.mvp=proj*view*mdl;u.model=mdl;u.light_pos=lp;u.cam_pos=cp;
        memcpy(m->frames[idx].mapped,&u,sizeof(u));}
    for (auto& pc:m_points_clusters){
        auto* m=pc->impl(); if (!m||m->frames.empty()) continue;
        UniformData u{}; u.mvp=proj*view;u.model=glm::mat4(1);u.light_pos=lp;u.cam_pos=cp;
        memcpy(m->frames[idx].mapped,&u,sizeof(u));}

    // Update post-process UBO
    if (!m_impl->post_frames.empty())
        memcpy(m_impl->post_frames[idx].mapped,&m_impl->post_data,sizeof(PostProcessData));

    std::vector<MeshObject*>           mptrs; for(auto& o:m_mesh_objects)    mptrs.push_back(o.get());
    std::vector<CustomShaderQuad*>     qptrs; for(auto& q:m_quads)           qptrs.push_back(q.get());
    std::vector<CustomShaderPoints3d*> pptrs; for(auto& p:m_points_clusters) pptrs.push_back(p.get());
    record_scene(m_impl.get(),mptrs,qptrs,pptrs);

    ImGui_ImplVulkan_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
    return true;
}

void Renderer::submitFrame(){
    ImGui::Render();
    uint32_t idx=m_impl->current_image;
    VkCommandBufferBeginInfo bi{.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT};
    check_vk(vkBeginCommandBuffer(m_impl->imgui_cmds[idx],&bi),"imgui begin");
    VkRenderingAttachmentInfo ca{.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView=m_impl->image_views[idx],.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.storeOp=VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfo ri{.sType=VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea={{0,0},m_impl->swapchain_extent},.layerCount=1,
        .colorAttachmentCount=1,.pColorAttachments=&ca};
    vkCmdBeginRendering(m_impl->imgui_cmds[idx],&ri);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),m_impl->imgui_cmds[idx]);
    vkCmdEndRendering(m_impl->imgui_cmds[idx]);
    VkImageMemoryBarrier bar{.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=0,
        .oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=m_impl->images[idx],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(m_impl->imgui_cmds[idx],
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,0,nullptr,0,nullptr,1,&bar);
    check_vk(vkEndCommandBuffer(m_impl->imgui_cmds[idx]),"imgui end");

    VkCommandBuffer cmds[]={m_impl->scene_cmds[idx],m_impl->imgui_cmds[idx]};
    VkPipelineStageFlags wst=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount=1,.pWaitSemaphores=&m_impl->image_available_sems[m_impl->current_frame],
        .pWaitDstStageMask=&wst,.commandBufferCount=2,.pCommandBuffers=cmds,
        .signalSemaphoreCount=1,.pSignalSemaphores=&m_impl->render_finished_sems[m_impl->current_frame]};
    check_vk(vkQueueSubmit(m_impl->graphics_queue,1,&si,m_impl->in_flight_fence),"submit");
    VkPresentInfoKHR pi{.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount=1,.pWaitSemaphores=&m_impl->render_finished_sems[m_impl->current_frame],
        .swapchainCount=1,.pSwapchains=&m_impl->swapchain,.pImageIndices=&m_impl->current_image};
    vkQueuePresentKHR(m_impl->graphics_queue,&pi);
    m_impl->current_frame=(m_impl->current_frame+1)%m_impl->image_count;
}

MeshObject& Renderer::createMeshObject(){return createMeshObject({},{},"");}

} // namespace XZRenderer