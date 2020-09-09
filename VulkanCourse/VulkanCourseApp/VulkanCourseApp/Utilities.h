#pragma once

#include <fstream>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>

const int MAX_FRAME_DRAWS = 2;
const int MAX_OBJECTS = 2;

const std::vector<const char*> deviceExtensions = {
   VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef VK_DEBUG
#define CREATION_SUCCEEDED(func, string) \
if (func != VK_SUCCESS) \
{ \
   throw std::runtime_error(string); \
}
#else
#define VK_SUCCEEDED(func, string) \
func;
#endif //VK_DEBUG

// Vertex data representation.
struct Vertex
{
   glm::vec3 pos; // Vertex Position. (x, y, z)
   glm::vec3 col; // Vertex Color. (r, g, b)
};

// Indices (locations) of Queue Families. (if they exist at all)
struct QueueFamilyIndices
{
   int32_t graphicsFamily = -1;      // Location of Graphics Queue Family.
   int32_t presentationFamily = -1;  // Location of Presentation Queue Family.

   // Check if queue families are valid.
   bool isValid()
   {
      return graphicsFamily >= 0 && presentationFamily >= 0;
   }
};

struct SwapchainDetails
{
   VkSurfaceCapabilitiesKHR surfaceCapabilities;      // Surface properties, e.g. image size/extent.
   std::vector<VkSurfaceFormatKHR> formats;           // Surface image formats, e.g. RGBA and size of each color.
   std::vector<VkPresentModeKHR> presentationModes;   // How images should be presented to the screen.
};

struct SwapchainImage
{
   VkImage image;
   VkImageView imageView;
};

static std::vector<char> readFile(const std::string& filename)
{
   // Open stream from given file.
   // binary tell stream to read file as binary. ate tells stream to start reading from end of file.
   std::ifstream file(filename, std::ios::binary | std::ios::ate);

   // Check if file stream successfully opened.
   if (!file.is_open())
   {
      throw std::runtime_error("Failed to open a file!");
   }

   // Get current read position and size.
   size_t fileSize = (size_t)file.tellg();
   std::vector<char> fileBuffer(fileSize);

   // Move read position to the start of the file.
   file.seekg(0);

   // Read the file data into the buffer. (stream "fileSize" in total)
   file.read(fileBuffer.data(), fileSize);

   // Close stream.
   file.close();

   return fileBuffer;
}

static uint32_t FindMemoryTypeIndex(VkPhysicalDevice physicalDevice, uint32_t allowedTypes, VkMemoryPropertyFlags properties)
{
   // Get properties of phsyical device memory.
   VkPhysicalDeviceMemoryProperties memoryProperties;
   vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

   for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
   {
      if ((allowedTypes & (1 << i)) &&
         (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
      {
         // This memory type is valid, so return its index.
         return i;
      }
   }

   throw std::runtime_error("Failed to find a suitable device!");

   return 1;
}

static void CreateBuffer(VkPhysicalDevice physicalDevice, VkDevice logicalDevice, VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsage,
   VkMemoryPropertyFlags bufferProperties, VkBuffer * buffer, VkDeviceMemory * bufferMemory)
{
   // -- CREATE VERTEX BUFFER --
   // Information to create a buffer. (doesn't include assigning memory)
   VkBufferCreateInfo bufferInfo = {};
   bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   bufferInfo.size = bufferSize;                                                          // Size of buffer. (size of 1 vertex * number of vertices)
   bufferInfo.usage = bufferUsage;                                                        // Multiple types of buffers possible.
   bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;                                    // Similar to swap chain images, can share vertex buffers.

   CREATION_SUCCEEDED(vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, buffer), "Failed to create a buffer!")

   // -- GET BUFFER MEMORY REQUIREMENTS --
   VkMemoryRequirements memRequirements;
   vkGetBufferMemoryRequirements(logicalDevice, *buffer, &memRequirements);

   // -- ALLOCATE MEMORY TO BUFFER --
   VkMemoryAllocateInfo memoryAllocInfo = {};
   memoryAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   memoryAllocInfo.allocationSize = memRequirements.size;
   memoryAllocInfo.memoryTypeIndex = FindMemoryTypeIndex(physicalDevice, memRequirements.memoryTypeBits, // Index of memory type on physical device that has required bit flags.
      bufferProperties);      // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : CPU can interact with memory.
                              // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : Allows placement of data straight into buffer after mapping. (otherwise would have to specify manually)
   // Allocate memory to VkDeviceMemory.
   CREATION_SUCCEEDED(vkAllocateMemory(logicalDevice, &memoryAllocInfo, nullptr, bufferMemory), "Failed to allocate vertex buffer memory!")

   // Allocate memory to given vertex buffer.
   CREATION_SUCCEEDED(vkBindBufferMemory(logicalDevice, *buffer, *bufferMemory, 0), "Failed to bind buffer memory!")
}

// TODO: refactor later for optimization with several meshes.
static void CopyBuffer(VkDevice logicalDevice, VkQueue transferQueue, VkCommandPool transferCommandPool,
   VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize bufferSize)
{
   // Command buffer to hold transfer commands.
   VkCommandBuffer transferCommandBuffer;

   VkCommandBufferAllocateInfo cbAllocInfo = {};
   cbAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   cbAllocInfo.commandPool = transferCommandPool;
   cbAllocInfo.commandBufferCount = 1;
   cbAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;                          // VK_COMMAND_BUFFER_LEVEL_PRIMARY : Buffer that submits directly to queue. Cannot be called by other buffers.
                                                                                 // VK_COMMAND_BUFFER_LEVEL_SECONDARY : Buffer can't be submitted to queue. Can be called from other buffers via "vkCommandExecuteCommands" on primary buffers.

   // Allocate command buffer from pool.
   CREATION_SUCCEEDED(vkAllocateCommandBuffers(logicalDevice, &cbAllocInfo, &transferCommandBuffer), "Failed to allocate one-shot command buffer!")

   // Information to begin the command buffer record.
   VkCommandBufferBeginInfo beginInfo = {};
   beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;                // We're only using the command buffer once, so set up for one use only.

   // Begin recording transfer commands.
   CREATION_SUCCEEDED(vkBeginCommandBuffer(transferCommandBuffer, &beginInfo), "Failed to record one-shot command buffer!")

   // Region of data to copy from and to.
   VkBufferCopy bufferCopyRegion = {};
   bufferCopyRegion.srcOffset = 0;
   bufferCopyRegion.dstOffset = 0;
   bufferCopyRegion.size = bufferSize;

   // Command to copy src buffer to dst buffer.
   vkCmdCopyBuffer(transferCommandBuffer, srcBuffer, dstBuffer, 1, &bufferCopyRegion);

   // End commands.
   CREATION_SUCCEEDED(vkEndCommandBuffer(transferCommandBuffer), "Failed to end command buffer!")

   // Queue submission information.
   VkSubmitInfo submitInfo = {};
   submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submitInfo.commandBufferCount = 1;
   submitInfo.pCommandBuffers = &transferCommandBuffer;

   // Submit transfer command to transfer queue and wait until it finishes.
   CREATION_SUCCEEDED(vkQueueSubmit(transferQueue, 1, &submitInfo, VK_NULL_HANDLE),"Failed to submit one-shot command buffer to queue!")
   CREATION_SUCCEEDED(vkQueueWaitIdle(transferQueue), "Failed to wait for one-shot command buffer to execute!")

   // Free temporary command buffer back to pool.
   vkFreeCommandBuffers(logicalDevice, transferCommandPool, 1, &transferCommandBuffer);
}
