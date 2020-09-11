#include "VulkanRenderer.h"

/***********************************************************
** Public Functions.
***********************************************************/
VulkanRenderer::VulkanRenderer()
{
}

VulkanRenderer::~VulkanRenderer()
{
}

int32_t VulkanRenderer::Init(GLFWwindow* newWindow)
{
   m_vkWindow = newWindow;
   try
   {
      CreateInstance();
      CreateSurface();
      GetPhysicalDevice();
      CreateLogicalDevice();
      CreateSwapchain();
      CreateRenderPass();
      CreateDescriptorSetLayout();
      CreatePushConstantRange();
      CreateGraphicsPipeline();
      CreateFrameBuffers();
      CreateCommandPool();

      m_uboViewProjection.projection = glm::perspective(glm::radians(45.0f), (float)m_vkSwapchainExtent.width / (float) m_vkSwapchainExtent.height, 0.0f, 100.0f);
      m_uboViewProjection.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

      m_uboViewProjection.projection[1][1] *= -1;

      // Create a mesh.
      // Vertex Data.
      std::vector<Vertex> meshVertices = {
         { { -0.4,   0.4,   0.0}, { 1.0f, 0.0f, 0.0f} },
         { { -0.4,  -0.4,   0.0}, { 0.0f, 1.0f, 0.0f} },
         { {  0.4,  -0.4,   0.0}, { 0.0f, 0.0f, 1.0f} },
         { {  0.4,   0.4,   0.0}, { 0.0f, 1.0f, 0.0f} }
      };
      std::vector<Vertex> meshVertices2 = {
         { { -0.25,  0.6,   0.0}, { 0.0f, 0.0f, 1.0f} },
         { { -0.25, -0.6,   0.0}, { 1.0f, 0.0f, 0.0f} },
         { {  0.25, -0.6,   0.0}, { 0.0f, 1.0f, 0.0f} },
         { {  0.25,  0.6,   0.0}, { 1.0f, 0.0f, 0.0f} }
      };

      // Index Data.
      std::vector<uint32_t> meshIndices = {
         0, 1, 2,
         2, 3, 0
      };

      Mesh firstMesh = Mesh(m_vkMainDevice.physicalDevice, m_vkMainDevice.logicalDevice,
         m_vkGraphicsQueue, m_vkGraphicsCommandPool, &meshVertices, &meshIndices);
      Mesh secMesh = Mesh(m_vkMainDevice.physicalDevice, m_vkMainDevice.logicalDevice,
         m_vkGraphicsQueue, m_vkGraphicsCommandPool, &meshVertices2, &meshIndices);

      m_vecMesh.push_back(firstMesh);
      m_vecMesh.push_back(secMesh);

      CreateCommandBuffers();
      //AllocateDynamicBufferTransferSpace();
      CreateUniformBuffers();
      CreateDescriptorPool();
      CreateDescriptorSets();
      CreateSynchronization();
   }
   catch (const std::runtime_error& e)
   {
      printf("ERROR: %s\n", e.what());
      return EXIT_FAILURE;
   }

   return 0;
}

void VulkanRenderer::Deinit()
{
   // Keep at top - waiting for idle so a proper cleanup can occur.
   vkDeviceWaitIdle(m_vkMainDevice.logicalDevice);

   //_aligned_free(m_uboModelTransferSpace);

   for (size_t i = 0; i < MAX_FRAME_DRAWS; i++)
   {
      vkDestroyFence(m_vkMainDevice.logicalDevice, m_vecDrawFences[i], nullptr);
      vkDestroySemaphore(m_vkMainDevice.logicalDevice, m_vecSemRenderFinished[i], nullptr);
      vkDestroySemaphore(m_vkMainDevice.logicalDevice, m_vecSemImageAvailable[i], nullptr);
   }
   vkDestroyCommandPool(m_vkMainDevice.logicalDevice, m_vkGraphicsCommandPool, nullptr);
   for (size_t i = 0; i < m_vecMesh.size(); i++)
   {
      m_vecMesh[i].Deinit();
   }
   for (auto framebuffer : m_vecSwapchainFramebuffers)
   {
      vkDestroyFramebuffer(m_vkMainDevice.logicalDevice, framebuffer, nullptr);
   }
   vkDestroyDescriptorPool(m_vkMainDevice.logicalDevice, m_vkDescriptorPool, nullptr);
   for (size_t i = 0; i < m_vecSwapchainImages.size(); i++)
   {
      vkDestroyBuffer(m_vkMainDevice.logicalDevice, m_vecVpUniformBuffer[i], nullptr);
      vkFreeMemory(m_vkMainDevice.logicalDevice, m_vecVpUniformBufferMemory[i], nullptr);
      //vkDestroyBuffer(m_vkMainDevice.logicalDevice, m_vecModelDUniformBuffer[i], nullptr);
      //vkFreeMemory(m_vkMainDevice.logicalDevice, m_vecModelDUniformBufferMemory[i], nullptr);
   }
   vkDestroyDescriptorSetLayout(m_vkMainDevice.logicalDevice, m_vkDescriptorSetLayout, nullptr);
   vkDestroyPipeline(m_vkMainDevice.logicalDevice, m_vkGraphicsPipeline, nullptr);
   vkDestroyPipelineLayout(m_vkMainDevice.logicalDevice, m_vkPipelineLayout, nullptr);
   vkDestroyRenderPass(m_vkMainDevice.logicalDevice, m_vkRenderPass, nullptr);
   for (auto image : m_vecSwapchainImages)
   {
      vkDestroyImageView(m_vkMainDevice.logicalDevice, image.imageView, nullptr);
   }
   vkDestroySwapchainKHR(m_vkMainDevice.logicalDevice, m_vkSwapchain, nullptr);
   vkDestroySurfaceKHR(m_vkInstance, m_vkSurface, nullptr);
   vkDestroyDevice(m_vkMainDevice.logicalDevice, nullptr);
   vkDestroyInstance(m_vkInstance, nullptr);
}

void VulkanRenderer::UpdateModel(uint32_t modelId, glm::mat4 newModel)
{
   if (modelId >= m_vecMesh.size())
   {
      throw std::runtime_error("Failed to update model! Probably need to update max model count!");
      return;
   }

   m_vecMesh[modelId].SetModel(newModel);
}

void VulkanRenderer::Draw()
{
   // -- GET NEXT IMAGE --
   // Wait for given fence to signal from last draw before continuing.
   vkWaitForFences(m_vkMainDevice.logicalDevice, 1, &m_vecDrawFences[m_iCurrentFrame], VK_TRUE, std::numeric_limits<uint64_t>::max());
   // Manually close fences.
   vkResetFences(m_vkMainDevice.logicalDevice, 1, &m_vecDrawFences[m_iCurrentFrame]);

   // Get index to next image to be drawn. Signal semaphore when ready to be drawn to.
   uint32_t imageIndex;
   vkAcquireNextImageKHR(m_vkMainDevice.logicalDevice, m_vkSwapchain, std::numeric_limits<uint64_t>::max(),
      m_vecSemImageAvailable[m_iCurrentFrame], VK_NULL_HANDLE, &imageIndex);

   RecordCommands(imageIndex);
   UpdateUniformBuffers(imageIndex);

   // -- SUBMIT COMMAND BUFFER TO RENDER --
   VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
   };
   VkSubmitInfo submitInfo = {};
   submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submitInfo.waitSemaphoreCount = 1;                             // Number of semaphores to wait on.
   submitInfo.pWaitSemaphores = &m_vecSemImageAvailable[m_iCurrentFrame];   // List of semaphores to wait on.
   submitInfo.pWaitDstStageMask = waitStages;                     // Stages to check semaphores at.
   submitInfo.commandBufferCount = 1;                             // Number of command buffers to submit.
   submitInfo.pCommandBuffers = &m_vecCommandBuffers[imageIndex];  // Command buffer to submit.
   submitInfo.signalSemaphoreCount = 1;                           // Number of semaphores to signal.
   submitInfo.pSignalSemaphores = &m_vecSemRenderFinished[m_iCurrentFrame]; // Semaphores to signal when command buffer finishes.

   // Submit command buffer to queue.
   CREATION_SUCCEEDED(vkQueueSubmit(m_vkGraphicsQueue, 1, &submitInfo, m_vecDrawFences[m_iCurrentFrame]), "Failed to submit queue!")

   // -- PRESENT RENDERED IMAGE TO SCREEN --
   VkPresentInfoKHR presentInfo = {};
   presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
   presentInfo.waitSemaphoreCount = 1;                            // Number of semaphores to wait on.
   presentInfo.pWaitSemaphores = &m_vecSemRenderFinished[m_iCurrentFrame];  // Semaphore(s) to wait on.
   presentInfo.swapchainCount = 1;                                // Number of swapchains to present to.
   presentInfo.pSwapchains = &m_vkSwapchain;                      // Swapchain(s) to present images to.
   presentInfo.pImageIndices = &imageIndex;                       // Index of images in swaphchain(s) to present.

   // Present image.
   CREATION_SUCCEEDED(vkQueuePresentKHR(m_vkPresentationQueue, &presentInfo), "Failed to present Image!")

   // Keep at bottom - incrementing draw frame.
   m_iCurrentFrame = (m_iCurrentFrame + 1) % MAX_FRAME_DRAWS;
}

/***********************************************************
** Private Functions.
***********************************************************/
void VulkanRenderer::CreateInstance()
{
   // Information about the application itself. (mostly just info for devs)
   VkApplicationInfo appInfo = {};
   appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
   appInfo.pApplicationName = "Vulkan App";                 // Custom app name.
   appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);   // Custom version of the application.
   appInfo.pEngineName = "Rootz";                           // Custom engine name.
   appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);        // Custom engine version.
   appInfo.apiVersion = VK_API_VERSION_1_1;                 // Vulkan version.

   // Creation information for a VkInstance (Vulkan Instance).
   VkInstanceCreateInfo createInfo = {};
   createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
   createInfo.pApplicationInfo = &appInfo;

#ifdef VK_DEBUG
   if (!checkValidationLayerSupport())
   {
      throw std::runtime_error("Validation layers requested, but not available!");
   }

   createInfo.enabledLayerCount = static_cast<uint32_t>(this->validationLayers.size());
   createInfo.ppEnabledLayerNames = this->validationLayers.data();

#else
   // TODO: Setup validation layers (the right way) that instance will use.
   createInfo.enabledLayerCount = 0;
   createInfo.ppEnabledLayerNames = nullptr;
#endif

   // Create list to hold instance extensions.
   std::vector<const char*> instanceExtensions = std::vector<const char*>();

   // Set up extensions the Instance will use.
   uint32_t glfwExtensionCount = 0;                         // GLFW may require multiple extensions.
   const char** glfwExtensions;                             // Extensions passed as array of cstrings, so need pointer (the array) to pointer (the cstring).

   glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

   // Add GLFW extensions to list of extensions.
   for (size_t i = 0; i < glfwExtensionCount; i++)
   {
      instanceExtensions.push_back(glfwExtensions[i]);
   }

   // Check Instance Extensions are supported.
   if (!CheckInstanceExtensionSupport(&instanceExtensions))
   {
      throw std::runtime_error("VkInstance does not support instance extensions!");
   }

   createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
   createInfo.ppEnabledExtensionNames = instanceExtensions.data();

   // Create instace.
   CREATION_SUCCEEDED(vkCreateInstance(&createInfo, nullptr, &m_vkInstance), "Failed to create a Vulkan Instance!")
}

void VulkanRenderer::CreateLogicalDevice()
{
   // Get the queue family indices for the chose physical device.
   QueueFamilyIndices indices = GetQueueFamilies(m_vkMainDevice.physicalDevice);

   // Vector for queue creation information, and set for family indices.
   std::vector<VkDeviceQueueCreateInfo> queueCreateinfos;
   std::set<int> queuefamilyIndices = { indices.graphicsFamily, indices.presentationFamily };

   // Queues the logical device needs to create and info to do so.
   float_t priority = 1.0f;
   for (int queueFamilyIndex : queuefamilyIndices)
   {
      VkDeviceQueueCreateInfo queueCreateInfo = {};
      queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queueCreateInfo.queueFamilyIndex = queueFamilyIndex;                                   // The index of the family to create a queue from.
      queueCreateInfo.queueCount = 1;                                                        // Number of queues to create;
      queueCreateInfo.pQueuePriorities = &priority;                                          // Vulkan needs to know how to handle multiple queues, so decide priority. (1 = highest)

      queueCreateinfos.push_back(queueCreateInfo);
   }

   // Information to create logical device. (sometimes called "device")
   VkDeviceCreateInfo deviceCreateInfo = {};
   deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
   deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateinfos.size());   // Numver of queue create infos.
   deviceCreateInfo.pQueueCreateInfos = queueCreateinfos.data();                             // List of queue create infos so the device can create required queues.
   deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());  // Number of enabled logical device extensions.
   deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();                       // List of enabled logical device extensions.

   VkPhysicalDeviceFeatures deviceFeatures = {};
   deviceCreateInfo.pEnabledFeatures = &deviceFeatures;                                      // Physical device features that the logical device will use.

   // Create the logical device for the given physical device.
   CREATION_SUCCEEDED(vkCreateDevice(m_vkMainDevice.physicalDevice, &deviceCreateInfo, nullptr, &m_vkMainDevice.logicalDevice), "Failed to create a logical device!")

   // Queues are created at the same time as the device. Store handle.
   vkGetDeviceQueue(m_vkMainDevice.logicalDevice, indices.graphicsFamily, 0, &m_vkGraphicsQueue);
   vkGetDeviceQueue(m_vkMainDevice.logicalDevice, indices.presentationFamily, 0, &m_vkPresentationQueue);
}

void VulkanRenderer::CreateSurface()
{
   // Create Surface (creating a surface create info struct, runs the create surface function, 
   if (glfwCreateWindowSurface(m_vkInstance, m_vkWindow, nullptr, &m_vkSurface) != VK_SUCCESS)
   {
      throw std::runtime_error("Failed to create a surface!");
   }
}

void VulkanRenderer::CreateSwapchain()
{
   // Get swap chain details so we can pick the best settings.
   SwapchainDetails swapchainDetails = GetSwapchainDetails(m_vkMainDevice.physicalDevice);

   // Find optimal surface values for our swap chain.
   VkSurfaceFormatKHR surfaceFormat = ChooseBestSurfaceFormat(swapchainDetails.formats);
   VkPresentModeKHR presentMode = ChooseBestPresentationMode(swapchainDetails.presentationModes);
   VkExtent2D extent = ChooseSwapExtent(swapchainDetails.surfaceCapabilities);

   // How many images are in the swap chain? Get 1 more than the minimum to allow triple buffering.
   uint32_t imageCount = swapchainDetails.surfaceCapabilities.minImageCount + 1;

   // Clamp min value to max.
   if (swapchainDetails.surfaceCapabilities.maxImageCount > 0 && // Limitless when 0
      swapchainDetails.surfaceCapabilities.maxImageCount < imageCount)
   {
      imageCount = swapchainDetails.surfaceCapabilities.maxImageCount;
   }

   // Creation information for swap chain.
   VkSwapchainCreateInfoKHR swapchainCreateInfo = {};
   swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
   swapchainCreateInfo.surface = m_vkSurface;                                                // Swapchain Surface.
   swapchainCreateInfo.minImageCount = imageCount;                                           // Minimum images in swapchain.
   swapchainCreateInfo.imageFormat = surfaceFormat.format;                                   // Swapchain format.
   swapchainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;                           // Swapchain color space.
   swapchainCreateInfo.presentMode = presentMode;                                            // Swapchain presentation mode.
   swapchainCreateInfo.imageExtent = extent;                                                 // Swapchain image extents.
   swapchainCreateInfo.imageArrayLayers = 1;                                                 // Number of layers for each image in chain.
   swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;                     // What attachment images will be used as.
   swapchainCreateInfo.preTransform = swapchainDetails.surfaceCapabilities.currentTransform; // Transform to perform on swap chain images.
   swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;                   // How to handle blending images with external graphics.
   swapchainCreateInfo.clipped = VK_TRUE;                                                    // Whether to clip parts of image not in view. (e.g. behind another window, off screen, etc)

   // Get Queue family indices.
   QueueFamilyIndices indices = GetQueueFamilies(m_vkMainDevice.physicalDevice);

   // If Graphics and Presentation families are different, then swapchain must let images be shared between families.
   if (indices.graphicsFamily != indices.presentationFamily)
   {
      uint32_t queueFamilyIndices[] =
      {
         (uint32_t)indices.graphicsFamily,
         (uint32_t)indices.presentationFamily
      };

      swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;   // Image share handling.
      swapchainCreateInfo.queueFamilyIndexCount = 2;                       // Number of queues to share images between.
      swapchainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;        // Array of queues to share between.
   }
   else
   {
      swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      swapchainCreateInfo.queueFamilyIndexCount = 0;
      swapchainCreateInfo.pQueueFamilyIndices = nullptr;
   }

   // If old swap chain has been destroyed and this one replaces it, then link the old one to quickly hand over responsibilities.
   swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

   // Create Swapchain
   CREATION_SUCCEEDED(vkCreateSwapchainKHR(m_vkMainDevice.logicalDevice, &swapchainCreateInfo, nullptr, &m_vkSwapchain), "Failed to create a Swapchain!")

   // Store for later reference.
   m_vkSwapchainImageFormat = surfaceFormat.format;
   m_vkSwapchainExtent = extent;

   // Get swap chain images.
   uint32_t swapchainImageCount;
   vkGetSwapchainImagesKHR(m_vkMainDevice.logicalDevice, m_vkSwapchain, &swapchainImageCount, nullptr);

   std::vector<VkImage> images(swapchainImageCount);
   vkGetSwapchainImagesKHR(m_vkMainDevice.logicalDevice, m_vkSwapchain, &swapchainImageCount, images.data());

   for (VkImage image : images)
   {
      // Store image handle.
      SwapchainImage swapchainImage = {};
      swapchainImage.image = image;
      swapchainImage.imageView = CreateImageView(image, m_vkSwapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);

      // Add to swapchain image list.
      m_vecSwapchainImages.push_back(swapchainImage);
   }
}

void VulkanRenderer::CreateRenderPass()
{
   // Color attachement of render pass.
   VkAttachmentDescription colorAttachment = {};
   colorAttachment.format = m_vkSwapchainImageFormat;                                  // Format to use for attachment.
   colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;                                    // Number of samples to write for multisampling.
   colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;                               // Describes what to do with attachment before rendering.
   colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;                             // Describes what to do with attachment after rendering.
   colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;                    // Describes what to do with stencil before rendering.
   colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;                  // Describes what to do with stencil after rendering.

   // Framebuffer data will be stored as an image, but images can be given different data layouts to give optimal use for certain operations.
   colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;                          // Image data layout before render pass starts.
   colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;                      // Image data layout after render pass. (to change to)

   // Attachement reference uses an attachment index that refers to index in the attachment list passed to renderPassCreateInfo.
   VkAttachmentReference colorAttachmentReference = {};
   colorAttachmentReference.attachment = 0;
   colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   // Information about a particular subpass the render pass is using.
   VkSubpassDescription subpass = {};
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;                        // Pipeline type subpass is to be bound to.
   subpass.colorAttachmentCount = 1;
   subpass.pColorAttachments = &colorAttachmentReference;

   // Need to determine when layout transitions occur using subpass dependencies.
   std::array<VkSubpassDependency, 2> subpassDependencies;


   // Conversion from VK_IMAGE_LAYOUT_UNDERINED to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL.
   // Transition must happen after...
   subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;                            // Supbass index (VK_SUBPASS_EXTERNAL = special value meaning outsider of renderpass)
   subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;         // Pipeline stage.
   subpassDependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;                   // Stage access mask. (memory access)
   // But must happen before..
   subpassDependencies[0].dstSubpass = 0;
   subpassDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
   subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   subpassDependencies[0].dependencyFlags = 0;


   // Conversion from VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.
   // Transition must happen after...
   subpassDependencies[1].srcSubpass = 0;
   subpassDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
   subpassDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   // But must happen before..
   subpassDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
   subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
   subpassDependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
   subpassDependencies[1].dependencyFlags = 0;

   // Create info for render pass.
   VkRenderPassCreateInfo renderPassCreateInfo = {};
   renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   renderPassCreateInfo.attachmentCount = 1;
   renderPassCreateInfo.pAttachments = &colorAttachment;
   renderPassCreateInfo.subpassCount = 1;
   renderPassCreateInfo.pSubpasses = &subpass;
   renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(subpassDependencies.size());
   renderPassCreateInfo.pDependencies = subpassDependencies.data();

   CREATION_SUCCEEDED(vkCreateRenderPass(m_vkMainDevice.logicalDevice, &renderPassCreateInfo, nullptr, &m_vkRenderPass), "Failed to create a Render Pass!")
}

void VulkanRenderer::CreateDescriptorSetLayout()
{
   // UboViewProjection binding info.
   VkDescriptorSetLayoutBinding vpLayoutBinding = {};
   vpLayoutBinding.binding = 0;                                                       // Binding point in shader. (designated by binding number in shader file)
   vpLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;                // Type of descriptor. (uniform, dynamic uniform, image sampler, etc)
   vpLayoutBinding.descriptorCount = 1;                                               // Number of descriptors for binding.
   vpLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;                           // Shader stage to bind to.
   vpLayoutBinding.pImmutableSamplers = nullptr;                                      // For texture: Can make (only) sampler data unchangeable (immutable) by specifying in layout.

   //// Model binding info.
   //VkDescriptorSetLayoutBinding modelLayoutBinding = {};
   //modelLayoutBinding.binding = 1;
   //modelLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
   //modelLayoutBinding.descriptorCount = 1;
   //modelLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
   //modelLayoutBinding.pImmutableSamplers = nullptr;

   std::vector<VkDescriptorSetLayoutBinding> layoutBindings = { vpLayoutBinding };//{ vpLayoutBinding, modelLayoutBinding };

   // Create descriptor set layout with given bindings.
   VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {};
   layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   layoutCreateInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());       // Number of binding infos.
   layoutCreateInfo.pBindings = layoutBindings.data();                                 // Array of binding infos.

   // Create descriptor set layout.
   CREATION_SUCCEEDED(vkCreateDescriptorSetLayout(m_vkMainDevice.logicalDevice, &layoutCreateInfo, nullptr, &m_vkDescriptorSetLayout), "Failed to create a descriptor set layout!")
}

void VulkanRenderer::CreatePushConstantRange()
{
   // Define push constant values. (no create needed)
   m_vkPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;                      // Shader stage push constant will go to.
   m_vkPushConstantRange.offset = 0;                                                   // Offset into given data to pass to push constant.
   m_vkPushConstantRange.size = sizeof(Model);                                         // Size of data being passed.
}

void VulkanRenderer::CreateGraphicsPipeline()
{
   // Read in SPIR-V code of shaders.
   auto vertexShaderCode = readFile("Shaders/vert.spv");
   auto fragmentShaderCode = readFile("Shaders/frag.spv");

   // Build Shader Modules to link to Graphics Pipeline.
   VkShaderModule vertexShaderModule = CreateShaderModule(vertexShaderCode);
   VkShaderModule fragmentShaderModule = CreateShaderModule(fragmentShaderCode);


   // -- SHADER STAGE CREATION INFORMATION --
   // Vertex stage creation info.
   VkPipelineShaderStageCreateInfo vertexShaderCreateInfo = {};
   vertexShaderCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   vertexShaderCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;                          // Shader stage name.
   vertexShaderCreateInfo.module = vertexShaderModule;                                 // Shader module to be used by stage.
   vertexShaderCreateInfo.pName = "main";                                              // Entry point in to shader.

   // Fragment stage creation info.
   VkPipelineShaderStageCreateInfo fragmentShaderCreateInfo = {};
   fragmentShaderCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   fragmentShaderCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;                      // Shader stage name.
   fragmentShaderCreateInfo.module = fragmentShaderModule;                             // Shader module to be used by stage.
   fragmentShaderCreateInfo.pName = "main";                                            // Entry point in to shader.

   // Put shader stage creation info into array.
   // Graphics pipeline creation info requires array of shader stage creates.
   VkPipelineShaderStageCreateInfo shaderStages[] = { vertexShaderCreateInfo, fragmentShaderCreateInfo };

   // How the data for a single vertex (including info such as position, color, texture coords, normals, etc) is as a whole.
   VkVertexInputBindingDescription bindingDescription = {};
   bindingDescription.binding = 0;                                                     // Can bind multiple streams of data, this defines which one.
   bindingDescription.stride = sizeof(Vertex);                                         // Size of a single vertex object.
   bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;                         // How to move between data after each vertex. VK_VERTEX_INPUT_RATE_VERTEX : move on to the next vertex. VK_VERTEX_INPUT_RATE_INSTANCE : move to a vertex for the next instance.

   // How the data for an attribute is defined within a vertex.
   std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions;

   // Position Attribute.
   attributeDescriptions[0].binding = 0;                                               // Which binding the data is at (should be same as above).
   attributeDescriptions[0].location = 0;                                              // Location in shader where data will be read from.
   attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;                       // Format the data will take. (also helps define the size of the data).
   attributeDescriptions[0].offset = offsetof(Vertex, pos);                            // Where this attribute is defined in the data for a signle vertex.

   // Color Attribute.
   attributeDescriptions[1].binding = 0;
   attributeDescriptions[1].location = 1;
   attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
   attributeDescriptions[1].offset = offsetof(Vertex, col);

   // Color attribute. TODO:

   // -- VERTEX INPUT --
   VkPipelineVertexInputStateCreateInfo vertexInputCreateInfo = {};
   vertexInputCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
   vertexInputCreateInfo.vertexBindingDescriptionCount = 1;
   vertexInputCreateInfo.pVertexBindingDescriptions = &bindingDescription;             // List of vertex binding descriptions. (e.g. data spacing/stride information.)
   vertexInputCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
   vertexInputCreateInfo.pVertexAttributeDescriptions = attributeDescriptions.data();  // List of vertex attribute sdescriptions. (e.g. data format and where to bind to or from.)


   // -- INPUT ASSEMBLY --
   VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
   inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
   inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;                       // Primitive type to assemble vertices as.
   inputAssembly.primitiveRestartEnable = VK_FALSE;                                    // Allow overriding of "strip" topology to start new primitives.


   // --VIEWPORT & SCISSOR --
   // Create a viewport info struct.
   VkViewport viewport = {};
   viewport.x = 0.0f;                                                                  // x start coordinate.
   viewport.y = 0.0f;                                                                  // y start coordinate.
   viewport.width = (float)m_vkSwapchainExtent.width;                                  // width of viewport.
   viewport.height = (float)m_vkSwapchainExtent.height;                                // height of viewport.
   viewport.minDepth = 0.0f;                                                           // min framebuffer depth.
   viewport.maxDepth = 1.0f;                                                           // max framebuffer depth.

   // Create a scissor info struct.
   VkRect2D scissor = {};
   scissor.offset = { 0,0 };                                                           // Offset to use region from.
   scissor.extent = m_vkSwapchainExtent;                                               // Extent to describe region to use, starting at offset.

   VkPipelineViewportStateCreateInfo viewportStateCreateInfo = {};
   viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
   viewportStateCreateInfo.viewportCount = 1;
   viewportStateCreateInfo.pViewports = &viewport;
   viewportStateCreateInfo.scissorCount = 1;
   viewportStateCreateInfo.pScissors = &scissor;


   //// -- DYNAMIC STATES --
   //// Dynamic states to enable.
   //std::vector<VkDynamicState> dynamicStateEnables;
   //dynamicStateEnables.push_back(VK_DYNAMIC_STATE_VIEWPORT);                           // Dynamic viewport: Can resize in command buffer with vkCmdSetViewport(commandbuffer, 0, 1, &viewport);
   //dynamicStateEnables.push_back(VK_DYNAMIC_STATE_SCISSOR);                            // Dynamic scissor: Can resize in command buffer with vkCmdSetScissor(commandbuffer, 0, 1, &scissor);

   //// Dynamic state creation info.
   //VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {};
   //dynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
   //dynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
   //dynamicStateCreateInfo.pDynamicStates = dynamicStateEnables.data();


   // -- RASTERIZER --
   VkPipelineRasterizationStateCreateInfo rasterizerCreateInfo = {};
   rasterizerCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
   rasterizerCreateInfo.depthClampEnable = VK_FALSE;                                      // Change if fragments beyond near/far planes are clipped (default) or clamped to plane.
   rasterizerCreateInfo.rasterizerDiscardEnable = VK_FALSE;                               // Whether to discard data and skip rasterizer. Never creates fragments, only suitable for pipeline without framebuffer output.
   rasterizerCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;                               // How to handle filling points between vertices.
   rasterizerCreateInfo.lineWidth = 1.0f;                                                 // How thick lines should be when drawn.
   rasterizerCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;                                 // Which face of a triangle to cull.
   rasterizerCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;                      // Winding to determine which side is front.
   rasterizerCreateInfo.depthBiasEnable = VK_FALSE;                                       // Whether to add depth bias to fragments. (good for stopping "shadow acne" in shadow mapping.)


   // -- MULTISAMPLING --
   VkPipelineMultisampleStateCreateInfo multipsamplingCreateInfo = {};
   multipsamplingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
   multipsamplingCreateInfo.sampleShadingEnable = VK_FALSE;                               // Enable multisample shading or not.
   multipsamplingCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;                 // Number of samples to use per fragment.


   // -- BLENDING --
   // Blending decides how to blend a new color being written to a fragment, with the old value.

   // Blend attachment state (how blending is handled)
   VkPipelineColorBlendAttachmentState colorStateAttachment = {};
   colorStateAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |      // Colors to apply blending to.
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
   colorStateAttachment.blendEnable = VK_TRUE;                                                      // Enable blending.

   // Blending uses equation: (srcColorBlendFactor * new color) colorBlenOp (dstColorBlendFactor * old color)
   colorStateAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
   colorStateAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
   colorStateAttachment.colorBlendOp = VK_BLEND_OP_ADD;

   // Summarized: (VK_BLEND_FACTOR_SRC_ALPHA * new color) + VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA * old color)

   colorStateAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
   colorStateAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
   colorStateAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

   VkPipelineColorBlendStateCreateInfo colorBlendingCreateInfo = {};
   colorBlendingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
   colorBlendingCreateInfo.logicOpEnable = VK_FALSE;                             // Alternative to calculations is to use logical operations.
   colorBlendingCreateInfo.attachmentCount = 1;
   colorBlendingCreateInfo.pAttachments = &colorStateAttachment;


   // -- PIPELINE LAYOUT --
   VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
   pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipelineLayoutCreateInfo.setLayoutCount = 1;
   pipelineLayoutCreateInfo.pSetLayouts = &m_vkDescriptorSetLayout;
   pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
   pipelineLayoutCreateInfo.pPushConstantRanges = &m_vkPushConstantRange;

   // Create pipeline layout.
   CREATION_SUCCEEDED(vkCreatePipelineLayout(m_vkMainDevice.logicalDevice, &pipelineLayoutCreateInfo, nullptr, &m_vkPipelineLayout), "Failed to create Pipline Layout!")


   // -- DEPTH STENCIL TESTING --
   // TODO: set up depth stencil testing.


   // -- GRAPHICS PIPELINE CREATION --
   VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
   pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
   pipelineCreateInfo.stageCount = 2;                                            // Number of shader stages.
   pipelineCreateInfo.pStages = shaderStages;                                    // List of shader stages.
   pipelineCreateInfo.pVertexInputState = &vertexInputCreateInfo;                // All the fixed function pipeline states.
   pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
   pipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
   pipelineCreateInfo.pDynamicState = nullptr;
   pipelineCreateInfo.pRasterizationState = &rasterizerCreateInfo;
   pipelineCreateInfo.pMultisampleState = &multipsamplingCreateInfo;
   pipelineCreateInfo.pColorBlendState = &colorBlendingCreateInfo;
   pipelineCreateInfo.pDepthStencilState = nullptr;
   pipelineCreateInfo.layout = m_vkPipelineLayout;                               // Pipeline layout pipelin should use.
   pipelineCreateInfo.renderPass = m_vkRenderPass;                               // Render pass description the pipeline is compatible with.
   pipelineCreateInfo.subpass = 0;                                               // Subpass of render pass to use with pipeline.

   // Pipeline derivatives: Can create multiple pipelines that derive from one another for optimization.
   pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;                       // Existing pipeline to derive from.
   pipelineCreateInfo.basePipelineIndex = -1;                                    // Index of pipeline being created to derive from. (incase creating multiple at once)

   CREATION_SUCCEEDED(vkCreateGraphicsPipelines(m_vkMainDevice.logicalDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_vkGraphicsPipeline), "Failed to a create Pipeline!")

   // Clean up after pipeline created.
   vkDestroyShaderModule(m_vkMainDevice.logicalDevice, fragmentShaderModule, nullptr);
   vkDestroyShaderModule(m_vkMainDevice.logicalDevice, vertexShaderModule, nullptr);
}

void VulkanRenderer::CreateFrameBuffers()
{
   // Resize framebuffer count to equal swap chain image count.
   m_vecSwapchainFramebuffers.resize(m_vecSwapchainImages.size());

   // Create a framebuffer for each swap chain image.
   for (size_t i = 0; i < m_vecSwapchainFramebuffers.size(); i++)
   {
      std::array<VkImageView, 1> attachments = {
         m_vecSwapchainImages[i].imageView
      };

      VkFramebufferCreateInfo framebufferCreateInfo = {};
      framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebufferCreateInfo.renderPass = m_vkRenderPass;                         // Render pass layout the framebuffer will be used with.
      framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
      framebufferCreateInfo.pAttachments = attachments.data();                   // List of attachments. (1:1 with render pass)
      framebufferCreateInfo.width = m_vkSwapchainExtent.width;                   // Framebuffer width.
      framebufferCreateInfo.height = m_vkSwapchainExtent.height;                 // Framebuffer height.
      framebufferCreateInfo.layers = 1;                                          // Framebuffer layers.

      CREATION_SUCCEEDED(vkCreateFramebuffer(m_vkMainDevice.logicalDevice, &framebufferCreateInfo, nullptr, &m_vecSwapchainFramebuffers[i]), "Failed to create a Framebuffer!")
   }
}

void VulkanRenderer::CreateCommandPool()
{
   // Get indices of queue families from device.
   QueueFamilyIndices queueFammilyIndices = GetQueueFamilies(m_vkMainDevice.physicalDevice);

   VkCommandPoolCreateInfo poolInfo = {};
   poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
   poolInfo.queueFamilyIndex = queueFammilyIndices.graphicsFamily;               // Queue family type that buffers from this command pool will use.

   // Create a Graphics Queue family command pool.
   CREATION_SUCCEEDED(vkCreateCommandPool(m_vkMainDevice.logicalDevice, &poolInfo, nullptr, &m_vkGraphicsCommandPool), "Failed to create a command pool!")
}

void VulkanRenderer::CreateCommandBuffers()
{
   // Resize command buffer count to have one for each framebuffer.
   m_vecCommandBuffers.resize(m_vecSwapchainFramebuffers.size());

   VkCommandBufferAllocateInfo cbAllocInfo = {};
   cbAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   cbAllocInfo.commandPool = m_vkGraphicsCommandPool;
   cbAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;                          // VK_COMMAND_BUFFER_LEVEL_PRIMARY : Buffer that submits directly to queue. Cannot be called by other buffers.
                                                                                 // VK_COMMAND_BUFFER_LEVEL_SECONDARY : Buffer can't be submitted to queue. Can be called from other buffers via "vkCommandExecuteCommands" on primary buffers.
   cbAllocInfo.commandBufferCount = static_cast<uint32_t>(m_vecCommandBuffers.size());

   CREATION_SUCCEEDED(vkAllocateCommandBuffers(m_vkMainDevice.logicalDevice, &cbAllocInfo, m_vecCommandBuffers.data()), "Failed to allocate command buffers!")
}

void VulkanRenderer::CreateSynchronization()
{
   m_vecSemImageAvailable.resize(MAX_FRAME_DRAWS);
   m_vecSemRenderFinished.resize(MAX_FRAME_DRAWS);
   m_vecDrawFences.resize(MAX_FRAME_DRAWS);

   // Semaphore creation.
   VkSemaphoreCreateInfo semaphoreCreateInfo = {};
   semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

   // Fence creation information.
   VkFenceCreateInfo fenceInfo = {};
   fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
   fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

   for (size_t i = 0; i < MAX_FRAME_DRAWS; i++)
   {
      CREATION_SUCCEEDED(vkCreateSemaphore(m_vkMainDevice.logicalDevice, &semaphoreCreateInfo, nullptr, &m_vecSemImageAvailable[i]), "Failed to create a imageAvailable semaphore!")
      CREATION_SUCCEEDED(vkCreateSemaphore(m_vkMainDevice.logicalDevice, &semaphoreCreateInfo, nullptr, &m_vecSemRenderFinished[i]), "Failed to create a RenderFinished semaphore!")
      CREATION_SUCCEEDED(vkCreateFence(m_vkMainDevice.logicalDevice, &fenceInfo, nullptr, &m_vecDrawFences[i]), "Failed to create a fence!")
   }
}

void VulkanRenderer::CreateUniformBuffers()
{
   // View projection buffer size.
   VkDeviceSize vpBufferSize = sizeof(UboViewProjection);
   
   // Model buffer size.
   //VkDeviceSize modelBufferSize = (uint64_t)m_vkModelUniformAlignment * (uint64_t)MAX_OBJECTS;

   // One uniform buffer for each image (and by extension, command buffer)
   m_vecVpUniformBuffer.resize(m_vecSwapchainImages.size());
   m_vecVpUniformBufferMemory.resize(m_vecSwapchainImages.size());
   //m_vecModelDUniformBuffer.resize(m_vecSwapchainImages.size());
   //m_vecModelDUniformBufferMemory.resize(m_vecSwapchainImages.size());

   //Create uniform buffers.
   for (size_t i = 0; i < m_vecSwapchainImages.size(); i++)
   {
      CreateBuffer(m_vkMainDevice.physicalDevice, m_vkMainDevice.logicalDevice, vpBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &m_vecVpUniformBuffer[i], &m_vecVpUniformBufferMemory[i]);

      /*CreateBuffer(m_vkMainDevice.physicalDevice, m_vkMainDevice.logicalDevice, modelBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &m_vecModelDUniformBuffer[i], &m_vecModelDUniformBufferMemory[i]);*/
   }
}

void VulkanRenderer::CreateDescriptorPool()
{
   // Type of descriptors and how many DESCRIPTORS, not descriptor sets. (combined tmakes the pool size)
   // Viewprojection Pool.
   VkDescriptorPoolSize vpPoolSize = {};
   vpPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
   vpPoolSize.descriptorCount = static_cast<uint32_t>(m_vecVpUniformBuffer.size());

   //// Model Pool (Dynamic)
   //VkDescriptorPoolSize modelPoolSize = {};
   //modelPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
   //modelPoolSize.descriptorCount = static_cast<uint32_t>(m_vecModelDUniformBuffer.size());

   // List of pool sizes.
   std::vector<VkDescriptorPoolSize> descriptorPoolSizes = { vpPoolSize };//{ vpPoolSize, modelPoolSize };

   // Data to create descriptor pool.
   VkDescriptorPoolCreateInfo poolCreateInfo = {};
   poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
   poolCreateInfo.maxSets = static_cast<uint32_t>(m_vecVpUniformBuffer.size());        // Maximum number of descriptor sets that can be created from pool.
   poolCreateInfo.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size());   // Amount of pool sizes being passed.
   poolCreateInfo.pPoolSizes = descriptorPoolSizes.data();                             // Pool sizes to create pool with.

   // Create descriptor pool.
   CREATION_SUCCEEDED(vkCreateDescriptorPool(m_vkMainDevice.logicalDevice, &poolCreateInfo, nullptr, &m_vkDescriptorPool), "Failed to create a Descriptor Pool!")
}

void VulkanRenderer::CreateDescriptorSets()
{
   // Resize descriptor set list so there's one for every buffer.
   m_vecDescriptorSets.resize(m_vecSwapchainImages.size());

   std::vector<VkDescriptorSetLayout> setLayouts(m_vecSwapchainImages.size(), m_vkDescriptorSetLayout);

   // Descriptor set allocation info.
   VkDescriptorSetAllocateInfo setAllocInfo = {};
   setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   setAllocInfo.descriptorPool = m_vkDescriptorPool;                                      // Pool to allocate descriptor set from.
   setAllocInfo.descriptorSetCount = static_cast<uint32_t>(m_vecSwapchainImages.size());  // Number of sets to allocate.
   setAllocInfo.pSetLayouts = setLayouts.data();                                          // Layouts to use to allocate sets. (1:1 relationship)

   // Allocate descriptor sets. (multiple)
   CREATION_SUCCEEDED(vkAllocateDescriptorSets(m_vkMainDevice.logicalDevice, &setAllocInfo, m_vecDescriptorSets.data()), "Failed to allocate descriptor set!")

   // Update all of descriptor set buffer bindings.
   for (size_t i = 0; i < m_vecSwapchainImages.size(); i++)
   {
      // VIEW PROJECTION DESCRIPTOR.
      // Buffer info and data offset info.
      VkDescriptorBufferInfo vpBufferInfo = {};
      vpBufferInfo.buffer = m_vecVpUniformBuffer[i];                                      // Buffer to get data from.
      vpBufferInfo.offset = 0;                                                            // Position of start of data.
      vpBufferInfo.range = sizeof(UboViewProjection);                                     // Size of data.

      // Data about connection between binding and buffer.
      VkWriteDescriptorSet vpSetWrite = {};
      vpSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      vpSetWrite.dstSet = m_vecDescriptorSets[i];                                         // Descriptor set to update.
      vpSetWrite.dstBinding = 0;                                                          // Binding to update. (matches with binding on layout/shader)
      vpSetWrite.dstArrayElement = 0;                                                     // Index in array to update.
      vpSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;                      // Type of descriptor.
      vpSetWrite.descriptorCount = 1;                                                     // Amount to update.
      vpSetWrite.pBufferInfo = &vpBufferInfo;                                             // Information about buffer data to bind.

      //// MODEL DESCRIPTOR.
      //// Model buffer binding info.
      //VkDescriptorBufferInfo modelBufferInfo = {};
      //modelBufferInfo.buffer = m_vecModelDUniformBuffer[i];
      //modelBufferInfo.offset = 0;
      //modelBufferInfo.range = m_vkModelUniformAlignment;

      //VkWriteDescriptorSet modelSetWrite = {};
      //modelSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      //modelSetWrite.dstSet = m_vecDescriptorSets[i];
      //modelSetWrite.dstBinding = 1;
      //modelSetWrite.dstArrayElement = 0;
      //modelSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      //modelSetWrite.descriptorCount = 1;
      //modelSetWrite.pBufferInfo = &modelBufferInfo;

      // List of Descriptor Set Writes.
      std::vector<VkWriteDescriptorSet> setWrites = { vpSetWrite }; //{ vpSetWrite, modelSetWrite };

      // Update the descriptor sets with new buffer/bindging info.
      vkUpdateDescriptorSets(m_vkMainDevice.logicalDevice, static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0, nullptr);
   }
}

void VulkanRenderer::UpdateUniformBuffers(uint32_t imageIndex)
{
   // Copy vp data.
   void* data;
   vkMapMemory(m_vkMainDevice.logicalDevice, m_vecVpUniformBufferMemory[imageIndex], 0, sizeof(UboViewProjection), 0, &data);
   memcpy(data, &m_uboViewProjection, sizeof(UboViewProjection));
   vkUnmapMemory(m_vkMainDevice.logicalDevice, m_vecVpUniformBufferMemory[imageIndex]);

   // Copy model data.
   //for (size_t i = 0; i < m_vecMesh.size(); i++)
   //{
   //   Model* thisModel = (Model*)((uint64_t)m_uboModelTransferSpace + (i * m_vkModelUniformAlignment));
   //   *thisModel = m_vecMesh[i].GetModel();
   //}

   //// Map the list of model data.
   //vkMapMemory(m_vkMainDevice.logicalDevice, m_vecModelDUniformBufferMemory[imageIndex], 0, m_vkModelUniformAlignment * m_vecMesh.size(), 0, &data);
   //memcpy(data, m_uboModelTransferSpace, m_vkModelUniformAlignment * m_vecMesh.size());
   //vkUnmapMemory(m_vkMainDevice.logicalDevice, m_vecModelDUniformBufferMemory[imageIndex]);
}

void VulkanRenderer::RecordCommands(uint32_t currentImage)
{
   // Information about how to begin each command buffer.
   VkCommandBufferBeginInfo bufferBeginInfo = {};
   bufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   //bufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;         // Buffer can be resubmitted when it has already been submitted and is awaiting execution.

   // Information about how to begin a render pass. (only needed for graphical applications)
   VkClearValue clearValues[] = {
      {0.6f, 0.65f, 0.4f, 1.0f}
   };
   VkRenderPassBeginInfo renderpassBeginInfo = {};
   renderpassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   renderpassBeginInfo.renderPass = m_vkRenderPass;                              // Render pass to begin.
   renderpassBeginInfo.renderArea.offset = { 0, 0 };                             // Start point of render pass in pixels.
   renderpassBeginInfo.renderArea.extent = m_vkSwapchainExtent;                  // Size of region to run render pass on. (starting at offset)
   renderpassBeginInfo.pClearValues = clearValues;                               // List of clear values. (TODO: Depth attachment clear value)
   renderpassBeginInfo.clearValueCount = 1;

   renderpassBeginInfo.framebuffer = m_vecSwapchainFramebuffers[currentImage];

   // Start recording commands to command buffer.
   CREATION_SUCCEEDED(vkBeginCommandBuffer(m_vecCommandBuffers[currentImage], &bufferBeginInfo), "Failed to start recording a command buffer!")

      // Begin render pass.
      vkCmdBeginRenderPass(m_vecCommandBuffers[currentImage], &renderpassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

         // Bind pipeline to be used in render pass.
         vkCmdBindPipeline(m_vecCommandBuffers[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_vkGraphicsPipeline);

         // Bind vertex buffer.
         for (size_t j = 0; j < m_vecMesh.size(); j++)
         {
            VkBuffer vertexBuffers[] = { m_vecMesh[j].GetVertexBuffer() };                   // Buffers to bind.
            VkDeviceSize offsets[] = { 0 };                                                  // Offsets into buffers being bound.
            vkCmdBindVertexBuffers(m_vecCommandBuffers[currentImage], 0, 1, vertexBuffers, offsets);    // Command to bind vertex buffer before drawing with them.

            // Bind index buffer.
            vkCmdBindIndexBuffer(m_vecCommandBuffers[currentImage], m_vecMesh[j].GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            // Dynamic offset amount.
            //uint32_t dynamicOffset = static_cast<uint32_t>(m_vkModelUniformAlignment) * j;

            // Push constants to given shader stage directly. (no buffer)
            vkCmdPushConstants(m_vecCommandBuffers[currentImage], m_vkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Model), &m_vecMesh[j].GetModel());

            // Bind descriptor sets.
            vkCmdBindDescriptorSets(m_vecCommandBuffers[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_vkPipelineLayout,
               0, 1, &m_vecDescriptorSets[currentImage], 0, nullptr);

            // Execute pipeline.
            vkCmdDrawIndexed(m_vecCommandBuffers[currentImage], m_vecMesh[j].GetIndexCount(), 1, 0, 0, 0);
         }

      // End render pass.
      vkCmdEndRenderPass(m_vecCommandBuffers[currentImage]);

   // Strop recording to command buffer.
   CREATION_SUCCEEDED(vkEndCommandBuffer(m_vecCommandBuffers[currentImage]), "Failed to stop recording a command buffer!")
}

void VulkanRenderer::GetPhysicalDevice()
{
   // Enumerate physical device that the vkIOnstance can access.
   uint32_t deviceCount = 0;
   vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, nullptr);

   // If no devices available, then none support Vulkan!
   if (deviceCount == 0)
   {
      throw std::runtime_error("Can't find GPUs that support Vulkan Instance!");
   }

   // Get list of physical devices.
   std::vector<VkPhysicalDevice> deviceList(deviceCount);
   vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, deviceList.data());

   // TODO: Just picking first device currently.
   for (const auto& device : deviceList)
   {
      if (CheckDeviceSuitable(device))
      {
         m_vkMainDevice.physicalDevice = device;
         break;
      }
   }

   // Get properties of our new device.
   VkPhysicalDeviceProperties deviceProperties;
   vkGetPhysicalDeviceProperties(m_vkMainDevice.physicalDevice, &deviceProperties);

   //m_vkMinUniformBufferOffset = deviceProperties.limits.minUniformBufferOffsetAlignment;
}

void VulkanRenderer::AllocateDynamicBufferTransferSpace()
{
   //// Calculate alignment of model data.
   //m_vkModelUniformAlignment = (sizeof(Model) + (size_t)m_vkMinUniformBufferOffset -1) & ~(m_vkMinUniformBufferOffset - 1);

   //// Create space in memory to hold dynamic buffer that is aligned to our required alignment and holds MAX_OBJECTS.
   //m_uboModelTransferSpace = (Model *)_aligned_malloc(m_vkModelUniformAlignment * MAX_OBJECTS, m_vkModelUniformAlignment);
}

bool VulkanRenderer::CheckInstanceExtensionSupport(std::vector<const char*>* checkExtensions)
{
   // Need to get number of extensions to create array of correct size to hold extensions.
   uint32_t extensionCount = 0;
   vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

   if (extensionCount == 0)
   {
      return false;
   }

   // Create a list of VkExtensionProperties using count.
   std::vector<VkExtensionProperties> extensions(extensionCount);
   vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

   // Check if given extensions are in list of available extensions.
   for (const auto& checkExtension : *checkExtensions)
   {
      bool hasExtension = false;

      for (const auto& extension : extensions)
      {
         if (strcmp(checkExtension, extension.extensionName) == 0)
         {
            hasExtension = true;
            break;
         }
      }

      // Problem, extension doesn't exist.
      if (!hasExtension)
      {
         return false;
      }
   }

   return true;
}

bool VulkanRenderer::CheckDeviceExtensionSupport(VkPhysicalDevice device)
{
   // Get device extension count.
   uint32_t extensionCount = 0;
   vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

   if (extensionCount == 0)
   {
      return false;
   }

   // Populate list of extensions.
   std::vector<VkExtensionProperties> extensions(extensionCount);
   vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

   // Check for extension.
   for (const auto& deviceExtension : deviceExtensions)
   {
      bool hasExtension = false;
      for (const auto& extension : extensions)
      {
         if (strcmp(deviceExtension, extension.extensionName) == 0)
         {
            hasExtension = true;
            break;
         }
      }

      if (!hasExtension)
      {
         return false;
      }
   }

   return true;
}

bool VulkanRenderer::CheckDeviceSuitable(VkPhysicalDevice device)
{
   // Information about the device itself (ID, name, type, vendor, etc)
   VkPhysicalDeviceProperties deviceProperties;
   vkGetPhysicalDeviceProperties(device, &deviceProperties);

   // Information about what the device can do (geo shader, tess shader, wide lines, etc)
   VkPhysicalDeviceFeatures deviceFeatures;
   vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

   QueueFamilyIndices indices = GetQueueFamilies(device);

   bool extensionsSupported = CheckDeviceExtensionSupport(device);

   bool swapchainValid = false;
   if (extensionsSupported)
   {
      SwapchainDetails swapchainDetails = GetSwapchainDetails(device);
      swapchainValid = !swapchainDetails.presentationModes.empty() && !swapchainDetails.formats.empty();
   }

   return indices.isValid() && extensionsSupported && swapchainValid;
}

QueueFamilyIndices VulkanRenderer::GetQueueFamilies(VkPhysicalDevice device)
{
   QueueFamilyIndices indices;

   // Get all Queue Family Property info for the given device.
   uint32_t queueFamilyCount = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

   std::vector<VkQueueFamilyProperties> queueFamilyList(queueFamilyCount);
   vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilyList.data());

   // Go through each queue family and check if it has at least 1 of the required types of queue.
   uint32_t i = 0;
   for (const auto& queueFamily : queueFamilyList)
   {
      // First check if queue family has at least 1 queue in that family (could have no queues)
      // Queue can be multiple types defined through bitfield. Need to bitwise AND with VK_QUEUE_*_BIT to check if has required type.
      if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
      {
         indices.graphicsFamily = i;      // If queue family is valid, store index.
      }

      // Check if Queue Family supports presentation.
      VkBool32 presentationSupport = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_vkSurface, &presentationSupport);
      // Check if queue is presentation type (can be both graphics and presentation).
      if (queueFamily.queueCount > 0 && presentationSupport)
      {
         indices.presentationFamily = i;
      }

      // Check if queue family indices are in a valid state. Stop searching if so.
      if (indices.isValid())
      {
         break;
      }

      i++;
   }

   return indices;
}

SwapchainDetails VulkanRenderer::GetSwapchainDetails(VkPhysicalDevice device)
{
   SwapchainDetails swapchainDetails;

   // Capabilities.
   vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_vkSurface, &swapchainDetails.surfaceCapabilities);

   // Formats.
   uint32_t formatCount = 0;
   vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_vkSurface, &formatCount, nullptr);

   if (formatCount != 0)
   {
      swapchainDetails.formats.resize(formatCount);
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_vkSurface, &formatCount, swapchainDetails.formats.data());
   }

   // Presentation modes.
   uint32_t presentationCount = 0;
   vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_vkSurface, &presentationCount, nullptr);

   if (presentationCount != 0)
   {
      swapchainDetails.presentationModes.resize(presentationCount);
      vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_vkSurface, &presentationCount, swapchainDetails.presentationModes.data());
   }

   return swapchainDetails;
}

VkSurfaceFormatKHR VulkanRenderer::ChooseBestSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
   // If only 1 format is available and undefined, then all formats are available.
   if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
   {
      return { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
   }

   // If restricted, search for optimal format.
   for (const auto& format : formats)
   {
      if ((format.format == VK_FORMAT_R8G8B8A8_UNORM || format.format == VK_FORMAT_B8G8R8A8_UNORM) &&
         format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
      {
         return format;
      }
   }

   return formats[0];
}

VkPresentModeKHR VulkanRenderer::ChooseBestPresentationMode(const std::vector<VkPresentModeKHR>& presentationModes)
{
   // Look for mailbox presentation mode.
   for (const auto& presentationMode : presentationModes)
   {
      if (presentationMode == VK_PRESENT_MODE_MAILBOX_KHR)
      {
         return presentationMode;
      }
   }

   // FIFO is forced in vulkan spec. Safe default.
   return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities)
{
   // Current extent is at numeric limit, then extent can vary. Otherwise it is the size of the window.
   if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
   {
      return surfaceCapabilities.currentExtent;
   }
   else
   {
      // Set size manually since it can vary.
      int32_t width, height;
      glfwGetFramebufferSize(m_vkWindow, &width, &height);

      // Create new extent using window size.
      VkExtent2D newExtent = {};
      newExtent.width = static_cast<uint32_t>(width);
      newExtent.height = static_cast<uint32_t>(height);

      // Surface also defines max and min, so make sure within boundaries by clamping value.
      newExtent.width = std::max(surfaceCapabilities.minImageExtent.width, std::min(surfaceCapabilities.maxImageExtent.width, newExtent.width));
      newExtent.height = std::max(surfaceCapabilities.minImageExtent.height, std::min(surfaceCapabilities.maxImageExtent.height, newExtent.height));

      return newExtent;
   }
}

VkImageView VulkanRenderer::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
{
   VkImageViewCreateInfo viewCreateInfo = {};
   viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   viewCreateInfo.image = image;                                  // Image to create view for.
   viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;               // Type of image. (1D, 2D, 3D, Cube, etc)
   viewCreateInfo.format = format;                                // Format of image data.
   viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;   // Allows remapping of rgba components to other rgba values.
   viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
   viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
   viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

   // Subresources allow the view to view only a part of an image.
   viewCreateInfo.subresourceRange.aspectMask = aspectFlags;      // Which aspect of image to view (e.g. COLOR_BIT for viewing color)
   viewCreateInfo.subresourceRange.baseMipLevel = 0;              // Start mipmap level to view from.
   viewCreateInfo.subresourceRange.levelCount = 1;                // Number of mipmap levels to view.
   viewCreateInfo.subresourceRange.baseArrayLayer = 0;            // Start array level to view from.
   viewCreateInfo.subresourceRange.layerCount = 1;                // Number of array levels to view.

   // Create image view and return it.
   VkImageView imageView;
   CREATION_SUCCEEDED(vkCreateImageView(m_vkMainDevice.logicalDevice, &viewCreateInfo, nullptr, &imageView), "Failed to create an Image View!")

   return imageView;
}

VkShaderModule VulkanRenderer::CreateShaderModule(const std::vector<char>& code)
{
   // Shader Module creation information.
   VkShaderModuleCreateInfo shaderModuleCreateInfo = {};
   shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   shaderModuleCreateInfo.codeSize = code.size();
   shaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

   VkShaderModule shaderModule;
   CREATION_SUCCEEDED(vkCreateShaderModule(m_vkMainDevice.logicalDevice, &shaderModuleCreateInfo, nullptr, &shaderModule), "Failed to create a shader module!")

   return shaderModule;
}

#ifdef VK_DEBUG
bool VulkanRenderer::checkValidationLayerSupport()
{
   uint32_t layerCount;
   vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

   std::vector<VkLayerProperties> availableLayers(layerCount);
   vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

   for (const char* layerName : this->validationLayers) {
      bool layerFound = false;

      for (const auto& layerProperties : availableLayers) {
         if (strcmp(layerName, layerProperties.layerName) == 0) {
            layerFound = true;
            break;
         }
      }

      if (!layerFound) {
         return false;
      }
   }

   return true;
}
#endif
