cmake_minimum_required(VERSION 3.10)
project(multi_lx_camera)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The SDK is dlopen()ed at runtime (see dynamic_link.cpp), never linked.
add_definitions(-DLX_DYNAMIC)

option(LX_NATIVE_ARCH "Optimize for the building machine's CPU (non-portable binaries)" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
if(LX_NATIVE_ARCH)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -mtune=native")
endif()

find_package(OpenMP REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(image_transport REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(pcl_conversions REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(PCL REQUIRED COMPONENTS common io)
find_package(OpenCV REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/ObstacleBox.msg"
  "msg/FrameRate.msg"
  "msg/Obstacle.msg"
  "msg/Pallet.msg"
  "msg/Result.msg"
  "srv/LxString.srv"
  "srv/LxFloat.srv"
  "srv/LxBool.srv"
  "srv/LxCmd.srv"
  "srv/LxInt.srv"
  DEPENDENCIES builtin_interfaces std_msgs geometry_msgs sensor_msgs
)
ament_export_dependencies(rosidl_default_runtime)

# SDK
set(LX_SDK_ROOT /opt/MRDVS CACHE PATH "MRDVS camera SDK install prefix")
set(LX_LIB ${LX_SDK_ROOT}/lib/libLxCameraApi.so)
if(NOT EXISTS ${LX_LIB})
  message(WARNING "MRDVS SDK not found at ${LX_LIB}; the node will fail at runtime")
endif()

add_executable(lx_camera_node
  src/lx_camera.cpp
  src/lx_camera_node.cpp
  src/dynamic_link.cpp)
target_compile_definitions(lx_camera_node PRIVATE LX_LIB="${LX_LIB}")
target_include_directories(lx_camera_node PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${LX_SDK_ROOT}/include
  ${PCL_INCLUDE_DIRS}
  ${OpenCV_INCLUDE_DIRS})
ament_target_dependencies(lx_camera_node
  rclcpp std_msgs sensor_msgs geometry_msgs image_transport cv_bridge
  pcl_conversions tf2 tf2_ros)
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} rosidl_typesupport_cpp)
target_link_libraries(lx_camera_node
  ${PCL_LIBRARIES} ${OpenCV_LIBS} ${CMAKE_DL_LIBS} "${cpp_typesupport_target}")

install(TARGETS lx_camera_node DESTINATION lib/${PROJECT_NAME})
install(PROGRAMS scripts/check_multi.py DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch rviz DESTINATION share/${PROJECT_NAME})

ament_package()
