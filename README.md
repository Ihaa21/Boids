# Boids

This is a small demo I wrote, I haven't tested it on other hardware so there might be bugs when you run it, but it should serve as a reference to my blog post here: https://ihorszlachtycz.blogspot.com/2021/07/optimizing-grid-simulations-with-simd.html

Requirements:
- Vulkan capable GPU for displaying
- Vulkan SDK for building
- Visual Studio for compiling
- All SSE extensions that are pre AVX supported by your CPU

Steps to Pull:
- git clone with --recurse-submodules set

Steps to Build:
- Open either visual studio cmd prompt or a regular cmd prompt which had vcvarsall.bat ran in it (to initialize visual studio compiler)
- Set the code directory as the active directory
- If you have a different Vulkan SDK version than 1.2.135.0, then go to build.bat and set the VulkanBin/Include Dirs to match your installation location
- Run build.bat
- You should have a binary in the build_win32 folder

Steps to Debug:
- Open the visual studio project in the build directory
- In the project settings, set the working/and exe directories to match where the project was cloned to on your computer (note the working directory points to the data folder)
- You should be able to run the demo from your visual studio project and debug it (you need to open files manually to set break points)

Controls:
- W/S for zoom in/out
- A/D to move left and right
