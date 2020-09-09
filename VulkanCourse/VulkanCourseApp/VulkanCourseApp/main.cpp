#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <vector>
#include <iostream>

#include "VulkanRenderer.h"

GLFWwindow* g_vkMainWindow;
VulkanRenderer g_vkRenderer;

void InitWindow(std::string wName = "Test Window", const int width = 800, const int height = 600)
{
   // Initialize GLFW.
   glfwInit();

   // Set GLFW to NOT work with OpenGL.
   glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
   glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

   g_vkMainWindow = glfwCreateWindow(width, height, wName.c_str(), nullptr, nullptr);
}

int main()
{
   // Create Window.
   InitWindow();

   // Create Vulkan Renderer Instance.
   if (g_vkRenderer.Init(g_vkMainWindow) == EXIT_FAILURE)
   {
      return EXIT_FAILURE;
   }

   float angle = 0.0f;
   float deltaTime = 0.0f;
   double lastTime = 0.0f;
   double now = 0.0f;

   // Loop until close.
   while (!glfwWindowShouldClose(g_vkMainWindow))
   {
      glfwPollEvents();

      now = glfwGetTime();
      deltaTime = (float)now - (float)lastTime;
      lastTime = now;

      angle += 10.0f * deltaTime;
      if (angle > 360.0f)
      {
         angle -= 360.0f;
      }
      //g_vkRenderer.UpdateModel(glm::rotate(glm::mat4(1.0f), glm::radians(angle), glm::vec3(0.0f, 0.0f, 1.0f)));
      g_vkRenderer.Draw();
   }

   // Clean up.
   g_vkRenderer.Deinit();

   glfwDestroyWindow(g_vkMainWindow);
   glfwTerminate();

   return EXIT_SUCCESS;
}
