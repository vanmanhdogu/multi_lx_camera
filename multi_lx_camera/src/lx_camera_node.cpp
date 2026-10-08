#include "multi_lx_camera/lx_camera.hpp"

int main(int argc, char **argv)
{
  DcLib lib;
  if (!DynamicLink(&lib)) {
    return 1;
  }
  rclcpp::init(argc, argv);
  int ret = 0;
  {
    auto node = std::make_shared<LxCamera>(&lib);
    if (node->IsOpen()) {
      rclcpp::spin(node);
    } else {
      ret = 1;
    }
    // node is destroyed here, which stops the stream and closes the device
    // before the library is unloaded below.
  }
  rclcpp::shutdown();
  DisDynamicLink(&lib);
  return ret;
}
