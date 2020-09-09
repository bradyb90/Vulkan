#include "Mesh.h"

/***********************************************************
** Public Functions.
***********************************************************/
Mesh::Mesh()
{
}

Mesh::Mesh(VkPhysicalDevice newPhysicalDevice, VkDevice newDevice, VkQueue transferQueue,
           VkCommandPool transferCommandPool, std::vector<Vertex>* vertices, std::vector<uint32_t>* indices)
{
   m_iVertexCount = static_cast<uint32_t>(vertices->size());
   m_iIndexCount = static_cast<uint32_t>(indices->size());
   m_vkPhysicalDevice = newPhysicalDevice;
   m_vkLogicalDevice = newDevice;
   CreateVertexBuffer(transferQueue, transferCommandPool, vertices);
   CreateIndexBuffer(transferQueue, transferCommandPool, indices);

   m_uboModel.model = glm::mat4(1.0f);
}

Mesh::~Mesh()
{
}

void Mesh::Deinit()
{
   vkDestroyBuffer(m_vkLogicalDevice, m_vkVertexBuffer, nullptr);
   vkFreeMemory(m_vkLogicalDevice, m_vkVertexBufferMemory, nullptr);
   vkDestroyBuffer(m_vkLogicalDevice, m_vkIndexBuffer, nullptr);
   vkFreeMemory(m_vkLogicalDevice, m_vkIndexBufferMemory, nullptr);
}

void Mesh::SetModel(glm::mat4 newModel)
{
   m_uboModel.model = newModel;
}

UboModel Mesh::GetModel()
{
   return m_uboModel;
}

uint32_t Mesh::GetVertexCount()
{
   return m_iVertexCount;
}

VkBuffer Mesh::GetVertexBuffer()
{
   return m_vkVertexBuffer;
}

uint32_t Mesh::GetIndexCount()
{
   return m_iIndexCount;
}

VkBuffer Mesh::GetIndexBuffer()
{
   return m_vkIndexBuffer;
}

/***********************************************************
** Private Functions.
***********************************************************/

void Mesh::CreateVertexBuffer(VkQueue transferQueue, VkCommandPool transferCommandPool, std::vector<Vertex>* vertices)
{
   // Get size of buffer needed for vertices.
   VkDeviceSize bufferSize = static_cast<uint64_t>(sizeof(Vertex)) * static_cast<uint64_t>(vertices->size());

   // Temporary buffer to "stage" vertex data before transferring to GPU.
   VkBuffer stagingBuffer;
   VkDeviceMemory stagingBufferMemory;

   // Create staging buffer and allocate memory to it.
   CreateBuffer(m_vkPhysicalDevice, m_vkLogicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      &stagingBuffer, &stagingBufferMemory);

   // -- MAP MEMORY TO VERTEX BUFFER --
   void* data;                                                                         // 1. Create pointer to a point in normal memory.
   CREATION_SUCCEEDED(vkMapMemory(m_vkLogicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data),
      "Failed to map staging buffer memory!")                                 // 2. "Map" the vertex buffer memory to that point.
   memcpy(data, vertices->data(), static_cast<uint32_t>(bufferSize));                  // 3. Copy memory from vertices vector to the point.
   vkUnmapMemory(m_vkLogicalDevice, stagingBufferMemory);                              // 4. Unmap the vertex buffer memory.

   // Create buffer with TRANSFER_DST_BIT to mark as recipient of the transfer data.
   // Buffer memory is to be DEVICE_LOCAL_BIT meaning memory is on the GPU and only accessible by it and not CPU. (host)
   CreateBuffer(m_vkPhysicalDevice, m_vkLogicalDevice, bufferSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m_vkVertexBuffer, &m_vkVertexBufferMemory);

   // Copy staging buffer to vertex buffer on GPU.
   CopyBuffer(m_vkLogicalDevice, transferQueue, transferCommandPool, stagingBuffer, m_vkVertexBuffer, bufferSize);

   // Clean up staging buffer parts.
   vkDestroyBuffer(m_vkLogicalDevice, stagingBuffer, nullptr);
   vkFreeMemory(m_vkLogicalDevice, stagingBufferMemory, nullptr);
}

void Mesh::CreateIndexBuffer(VkQueue transferQueue, VkCommandPool transferCommandPool, std::vector<uint32_t>* indices)
{
   // Get size of buffer needed for indices.
   VkDeviceSize bufferSize = sizeof(uint32_t) * indices->size();

   // Temporary buffer to "stage" vertex data before transferring to GPU.
   VkBuffer stagingBuffer;
   VkDeviceMemory stagingBufferMemory;

   // Create staging buffer and allocate memory to it.
   CreateBuffer(m_vkPhysicalDevice, m_vkLogicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      &stagingBuffer, &stagingBufferMemory);

   // -- MAP MEMORY TO VERTEX BUFFER --
   void* data;
   CREATION_SUCCEEDED(vkMapMemory(m_vkLogicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data), "Failed to map staging buffer memory!")
   memcpy(data, indices->data(), static_cast<uint32_t>(bufferSize));
   vkUnmapMemory(m_vkLogicalDevice, stagingBufferMemory);

   // Create buffer for index data on gpu access only area.
   CreateBuffer(m_vkPhysicalDevice, m_vkLogicalDevice, bufferSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m_vkIndexBuffer, &m_vkIndexBufferMemory);

   // Copy from staging buffer to GPU access buffer.
   CopyBuffer(m_vkLogicalDevice, transferQueue, transferCommandPool, stagingBuffer, m_vkIndexBuffer, bufferSize);

   // Clean up staging buffer parts.
   vkDestroyBuffer(m_vkLogicalDevice, stagingBuffer, nullptr);
   vkFreeMemory(m_vkLogicalDevice, stagingBufferMemory, nullptr);
}
