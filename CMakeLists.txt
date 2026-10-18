   cmake_minimum_required(VERSION 3.16)
   project(doom-but-shit CXX)
   set(CMAKE_CXX_STANDARD 17)

   find_package(OpenGL REQUIRED)
   find_package(glfw3 REQUIRED)
   find_package(GLEW REQUIRED)
   find_package(Threads REQUIRED)

   add_executable(doom main.cpp miniaudio_impl.cpp)
   target_compile_definitions(doom PRIVATE MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_ENGINE)
   target_link_libraries(doom PRIVATE OpenGL::GL glfw GLEW::GLEW Threads::Threads ${CMAKE_DL_LIBS} m)