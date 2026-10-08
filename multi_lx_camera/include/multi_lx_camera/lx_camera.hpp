#ifndef MULTI_LX_CAMERA_LX_CAMERA_HPP
#define MULTI_LX_CAMERA_LX_CAMERA_HPP

#include "multi_lx_camera/dynamic_link.hpp"
#include <pcl/common/common_headers.h>

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "image_transport/image_transport.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/imu.hpp"
#include "sensor_msgs/msg/point_cloud.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/string.hpp"
#include "tf2_ros/static_transform_broadcaster.hpp"

#include "multi_lx_camera/msg/frame_rate.hpp"
#include "multi_lx_camera/msg/obstacle.hpp"
#include "multi_lx_camera/msg/pallet.hpp"
#include "multi_lx_camera/msg/result.hpp"

#include "multi_lx_camera/srv/lx_bool.hpp"
#include "multi_lx_camera/srv/lx_cmd.hpp"
#include "multi_lx_camera/srv/lx_float.hpp"
#include "multi_lx_camera/srv/lx_int.hpp"
#include "multi_lx_camera/srv/lx_string.hpp"

#include <Eigen/Core>
#include <Eigen/Dense>

#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

// 自定义包含时间戳的点云类型
struct PointXYZIT
{
    PCL_ADD_POINT4D
    uint32_t intensity;
    double timestamp;
    uint16_t row_pos;
    uint16_t col_pos;
    PointXYZIT() : intensity(0), timestamp(0.0), row_pos(0), col_pos(0) {}
    PointXYZIT(float x, float y, float z, uint32_t i, double t) 
        : intensity(i), timestamp(t), row_pos(0), col_pos(0) {
        this->x = x;
        this->y = y;
        this->z = z;
    }
    PointXYZIT(float x, float y, float z, uint32_t i, double t, uint16_t r, uint16_t c)
        : intensity(i), timestamp(t), row_pos(r), col_pos(c) {
        this->x = x;
        this->y = y;
        this->z = z;
    }
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
} EIGEN_ALIGN16;

// 注册自定义点云类型
POINT_CLOUD_REGISTER_POINT_STRUCT(PointXYZIT,
    (float, x, x)
    (float, y, y)
    (float, z, z)
    (std::uint32_t, intensity, intensity)
    (double, timestamp, timestamp)
    (std::uint16_t, row_pos, row_pos)
    (std::uint16_t, col_pos, col_pos)
)

typedef PointXYZIT PointType;


// One camera per node, one node per process. Every frame id is derived from
// the camera_name parameter, so several instances can share one TF tree.
class LxCamera : public rclcpp::Node {
public:
  LxCamera(DcLib *dynamic_lib,
           const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
  ~LxCamera();
  // False when the device could not be opened (e.g. shutdown requested while
  // still searching); main() then exits without spinning.
  bool IsOpen() const { return is_open_; }
  int Start();
  int Stop();

private:
  bool SearchAndOpenDevice();
  void PrepareTransforms();
  void GrabOnce();
  int Check(std::string command, int state);
  static void ImuDataCallback(LxImuData *data_ptr, void *usr_data);
  bool LxString(const multi_lx_camera::srv::LxString::Request::SharedPtr req,
                const multi_lx_camera::srv::LxString::Response::SharedPtr res);
  bool LxFloat(const multi_lx_camera::srv::LxFloat::Request::SharedPtr req,
               const multi_lx_camera::srv::LxFloat::Response::SharedPtr res);
  bool LxBool(const multi_lx_camera::srv::LxBool::Request::SharedPtr req,
              const multi_lx_camera::srv::LxBool::Response::SharedPtr res);
  bool LxCmd(const multi_lx_camera::srv::LxCmd::Request::SharedPtr req,
             const multi_lx_camera::srv::LxCmd::Response::SharedPtr res);
  bool LxInt(const multi_lx_camera::srv::LxInt::Request::SharedPtr req,
             const multi_lx_camera::srv::LxInt::Response::SharedPtr res);

private:
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_error_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_rgb_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_depth_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_amp_;
  rclcpp::Publisher<geometry_msgs::msg::TransformStamped>::SharedPtr pub_tf_;
  rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr pub_tof_info_;
  rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr pub_rgb_info_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr pub_cloud_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr pub_lidarCloud_;
  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr pub_imu_;
  rclcpp::Publisher<multi_lx_camera::msg::FrameRate>::SharedPtr pub_temper_;
  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr pub_location_;
  rclcpp::Publisher<multi_lx_camera::msg::Obstacle>::SharedPtr pub_obstacle_;
  rclcpp::Publisher<multi_lx_camera::msg::Pallet>::SharedPtr pub_pallet_;

  // Services must outlive the constructor, so they are members.
  rclcpp::Service<multi_lx_camera::srv::LxCmd>::SharedPtr srv_cmd_;
  rclcpp::Service<multi_lx_camera::srv::LxInt>::SharedPtr srv_int_;
  rclcpp::Service<multi_lx_camera::srv::LxBool>::SharedPtr srv_bool_;
  rclcpp::Service<multi_lx_camera::srv::LxFloat>::SharedPtr srv_float_;
  rclcpp::Service<multi_lx_camera::srv::LxString>::SharedPtr srv_string_;

  std::shared_ptr<tf2_ros::StaticTransformBroadcaster> tf_broadcaster_;
  rclcpp::TimerBase::SharedPtr grab_timer_;

  geometry_msgs::msg::TransformStamped tf_;
  sensor_msgs::msg::CameraInfo tof_info_;
  sensor_msgs::msg::CameraInfo rgb_info_;

  DcHandle handle_ = 0;
  bool is_open_ = false;
  bool is_start_ = false;

  std::string camera_name_;
  std::string parent_frame_;
  std::string frame_tof_, frame_rgb_, frame_imu_;
  std::string ip_;
  std::string sn_;

  int is_depth_ = 0;
  int is_amp_ = 0;
  int is_rgb_ = 0;
  int is_xyz_ = 1;
  int rgb_type_ = 0;
  int inside_app_ = 0;
  int rgb_channel_ = 0;
  int lx_rgbd_align = 0;
  // Mount pose of <camera_name>_tof in parent_frame: metres and degrees.
  float install_x_ = 0.0, install_y_ = 0.0, install_z_ = 0.0,
        install_yaw_ = 0.0, install_roll_ = 0.0, install_pitch_ = 0.0;
  bool publish_tf_ = true;
};

#endif // MULTI_LX_CAMERA_LX_CAMERA_HPP
