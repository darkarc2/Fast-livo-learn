cmake_minimum_required(VERSION 2.8.3)
project(fast_livo)

SET(CMAKE_BUILD_TYPE "Debug")

ADD_COMPILE_OPTIONS(-std=c++14 )
ADD_COMPILE_OPTIONS(-std=c++14 )
set( CMAKE_CXX_FLAGS "-std=c++14 -O3" ) 

add_definitions(-DROOT_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/\")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fexceptions" )
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -pthread -std=c++0x -std=c++14 -fexceptions")

message("Current CPU archtecture: ${CMAKE_SYSTEM_PROCESSOR}")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)" )
  include(ProcessorCount)
  ProcessorCount(N)
  message("Processer number:  ${N}")
  if(N GREATER 5)
    add_definitions(-DMP_EN)
    add_definitions(-DMP_PROC_NUM=4)
    message("core for MP:  4")
  elseif(N GREATER 3)
    math(EXPR PROC_NUM "${N} - 2")
    add_definitions(-DMP_EN)
    add_definitions(-DMP_PROC_NUM="${PROC_NUM}")
    message("core for MP:  ${PROC_NUM}")
  else()
    add_definitions(-DMP_PROC_NUM=1)
  endif()
else()
  add_definitions(-DMP_PROC_NUM=1)
endif()

find_package(OpenMP QUIET)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}   ${OpenMP_C_FLAGS}")

find_package(PythonLibs REQUIRED)
find_path(MATPLOTLIB_CPP_INCLUDE_DIRS "matplotlibcpp.h")

find_package(catkin REQUIRED COMPONENTS
  geometry_msgs
  nav_msgs
  sensor_msgs
  roscpp
  rospy
  std_msgs
  pcl_ros
  tf
  livox_ros_driver
  message_generation
  eigen_conversions
  vikit_common  # 寻找vikit功能包，找到之后会自动包括其头文件的路径
  vikit_ros
  cv_bridge
  image_transport
)

find_package(Eigen3 REQUIRED)
find_package(PCL REQUIRED)
FIND_PACKAGE(OpenCV REQUIRED)
FIND_PACKAGE(Boost REQUIRED COMPONENTS thread)
# FIND_PACKAGE(Sophus REQUIRED)
# set(Sophus_LIBRARIES libSophus.so)
include_directories("/usr/local/include/sophus_no_template/include")
set(Sophus_LIBRARIES "/usr/local/include/sophus_no_template/lib/libSophus.so")

message(Eigen: ${EIGEN3_INCLUDE_DIR})

include_directories(
	${catkin_INCLUDE_DIRS} 
  ${EIGEN3_INCLUDE_DIR}
  ${PCL_INCLUDE_DIRS}
  ${PYTHON_INCLUDE_DIRS}
  ${OpenCV_INCLUDE_DIRS}
# ${Sophus_INCLUDE_DIRS}
  include)

add_message_files(
  FILES
  Pose6D.msg
  States.msg
)

generate_messages(
 DEPENDENCIES
 geometry_msgs
)

catkin_package(
  CATKIN_DEPENDS geometry_msgs nav_msgs roscpp rospy std_msgs message_runtime cv_bridge image_transport vikit_common vikit_ros
  DEPENDS EIGEN3 PCL OpenCV Sophus
  INCLUDE_DIRS include
)

# add_executable(loam_feat_extract src/feature_extract.cpp)
# target_link_libraries(loam_feat_extract ${catkin_LIBRARIES} ${PCL_LIBRARIES})

#; ikdtree部分，最后我们应该不用
add_library(ikdtree include/ikd-Tree/ikd_Tree.cpp
                    # include/ikd-Forest/ikd_Forest.cpp 
                    include/FOV_Checker/FOV_Checker.cpp
                    )

#; VIO部分
add_library(vio src/lidar_selection.cpp
                src/frame.cpp
                src/point.cpp
                src/map.cpp
                )

add_executable(fastlivo_mapping src/laserMapping.cpp 
                                src/IMU_Processing.cpp
                                src/preprocess.cpp   # 这个地方是处理点云特征提取的
                                )
target_link_libraries(fastlivo_mapping ${catkin_LIBRARIES} ${PCL_LIBRARIES} ${PYTHON_LIBRARIES} vio ikdtree)
target_include_directories(fastlivo_mapping PRIVATE ${PYTHON_INCLUDE_DIRS})

# add_executable(kd_tree_test include/ikd-Tree/ikd_Tree.cpp src/kd_tree_test.cpp)
# target_link_libraries(kd_tree_test ${PCL_LIBRARIES})

# add_executable(ikd_Forest_Test include/ikd-Forest/ikd_Forest.cpp test/ikd_Forest_Test.cpp)
# target_link_libraries(ikd_Forest_Test ${PCL_LIBRARIES})

# add_executable(fov_test src/fov_test.cpp include/FOV_Checker/FOV_Checker.cpp)


