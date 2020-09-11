#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLM/gtc/matrix_transform.hpp>

#include <stdexcept>
#include <vector>
#include <set>
#include <algorithm>
#include <array>

#include "Mesh.h"
#include "Utilities.h"

class VulkanRenderer
{
public:
   VulkanRenderer();
   ~VulkanRenderer();

   int32_t Init(GLFWwindow* newWindow);
   void Deinit();

   void UpdateModel(uint32_t modelId, glm::mat4 newModel);

   void Draw();

private:
   /***********************************************************
   ** Vulkan Functions.
   ***********************************************************/
   // - Create Functions.
   void CreateInstance();
   void CreateLogicalDevice();
   void CreateSurface();
   void CreateSwapchain();
   void CreateRenderPass();
   void CreateDescriptorSetLayout();
   void CreatePushConstantRange();
   void CreateGraphicsPipeline();
   void CreateFrameBuffers();
   void CreateCommandPool();
   void CreateCommandBuffers();
   void CreateSynchronization();

   void CreateUniformBuffers();
   void CreateDescriptorPool();
   void CreateDescriptorSets();

   void UpdateUniformBuffers(uint32_t imageIndex);

   // - Record Functions
   void RecordCommands(uint32_t currentImage);

   // - Get Functions.
   void GetPhysicalDevice();

   // - Allocate Functions.
   void AllocateDynamicBufferTransferSpace();

   // - Support Functions.
   // -- Checker Functions.
   bool CheckInstanceExtensionSupport(std::vector<const char*>* checkExtensions);
   bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
   bool CheckDeviceSuitable(VkPhysicalDevice device);

   // -- Getter Functions.
   QueueFamilyIndices GetQueueFamilies(VkPhysicalDevice device);
   SwapchainDetails GetSwapchainDetails(VkPhysicalDevice device);

   // -- Choose Functions.
   VkSurfaceFormatKHR ChooseBestSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
   VkPresentModeKHR ChooseBestPresentationMode(const std::vector<VkPresentModeKHR>& presentationModes);
   VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities);

   // -- Create Functions.
   VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
   VkShaderModule CreateShaderModule(const std::vector<char>& code);

   /***********************************************************
   ** Variable Declarations.
   ***********************************************************/
   GLFWwindow* m_vkWindow;

   uint32_t m_iCurrentFrame = 0;

   // Scene Objects.
   std::vector<Mesh> m_vecMesh;

   // Scene Settings.
   struct UboViewProjection {
      glm::mat4 projection;
      glm::mat4 view;
   } m_uboViewProjection;

   // Vulkan Components.
   // - Main.
   VkInstance m_vkInstance;
   struct{
      VkPhysicalDevice physicalDevice;
      VkDevice logicalDevice;
   } m_vkMainDevice;
   VkQueue m_vkGraphicsQueue;
   VkQueue m_vkPresentationQueue;
   VkSurfaceKHR m_vkSurface;
   VkSwapchainKHR m_vkSwapchain;

   std::vector<SwapchainImage> m_vecSwapchainImages;
   std::vector<VkFramebuffer> m_vecSwapchainFramebuffers;
   std::vector<VkCommandBuffer> m_vecCommandBuffers;

   // - Descriptors
   VkDescriptorSetLayout m_vkDescriptorSetLayout;
   VkPushConstantRange m_vkPushConstantRange;

   VkDescriptorPool m_vkDescriptorPool;
   std::vector<VkDescriptorSet> m_vecDescriptorSets;

   std::vector<VkBuffer> m_vecVpUniformBuffer;
   std::vector<VkDeviceMemory> m_vecVpUniformBufferMemory;

   std::vector<VkBuffer> m_vecModelDUniformBuffer;
   std::vector<VkDeviceMemory> m_vecModelDUniformBufferMemory;

   //VkDeviceSize m_vkMinUniformBufferOffset;
   //size_t m_vkModelUniformAlignment;
   //Model* m_uboModelTransferSpace;

   // - Pipeline.
   VkPipeline m_vkGraphicsPipeline;
   VkPipelineLayout m_vkPipelineLayout;
   VkRenderPass m_vkRenderPass;

   // - Pools.
   VkCommandPool m_vkGraphicsCommandPool;

   // - Utility.
   VkFormat m_vkSwapchainImageFormat;
   VkExtent2D m_vkSwapchainExtent;

   // - Synchronization.
   std::vector<VkSemaphore> m_vecSemImageAvailable;
   std::vector<VkSemaphore> m_vecSemRenderFinished;
   std::vector<VkFence> m_vecDrawFences;

#ifdef VK_DEBUG
   const std::vector<const char*> validationLayers = {
      "VK_LAYER_KHRONOS_validation"
   };
   bool checkValidationLayerSupport();
#endif
};

