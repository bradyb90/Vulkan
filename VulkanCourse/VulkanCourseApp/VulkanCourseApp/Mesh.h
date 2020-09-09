#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

#include "Utilities.h"

struct UboModel {
   glm::mat4 model;
};

class Mesh
{
public:
   Mesh();
   Mesh(VkPhysicalDevice newPhysicalDevice, VkDevice newDevice, VkQueue transferQueue,
        VkCommandPool transferCommandPool, std::vector<Vertex>* vertices, std::vector<uint32_t>* indices);
   ~Mesh();

   void Deinit();

   void SetModel(glm::mat4 newModel);
   UboModel GetModel();

   uint32_t GetVertexCount();
   VkBuffer GetVertexBuffer();

   uint32_t GetIndexCount();
   VkBuffer GetIndexBuffer();

private:
   void CreateVertexBuffer(VkQueue transferQueue, VkCommandPool transferCommandPool, std::vector<Vertex>* vertices);
   void CreateIndexBuffer(VkQueue transferQueue, VkCommandPool transferCommandPool, std::vector<uint32_t>* indices);

   UboModel m_uboModel;

   uint32_t m_iVertexCount;
   VkBuffer m_vkVertexBuffer;
   VkDeviceMemory m_vkVertexBufferMemory;

   uint32_t m_iIndexCount;
   VkBuffer m_vkIndexBuffer;
   VkDeviceMemory m_vkIndexBufferMemory;

   VkPhysicalDevice m_vkPhysicalDevice;
   VkDevice m_vkLogicalDevice;
};

