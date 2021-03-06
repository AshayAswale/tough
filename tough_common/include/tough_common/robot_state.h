#ifndef TOUGH_ROBOT_STATE_INFORMER_H
#define TOUGH_ROBOT_STATE_INFORMER_H

#include <ros/ros.h>
#include <sensor_msgs/JointState.h>
#include <map>
#include <tf/transform_listener.h>
#include <mutex>
#include <geometry_msgs/Pose2D.h>
#include <geometry_msgs/Polygon.h>
#include "tough_common/robot_description.h"
#include <sensor_msgs/Imu.h>
#include <ihmc_msgs/Point2dRosMessage.h>
#include <ihmc_msgs/SupportPolygonRosMessage.h>
#include <geometry_msgs/WrenchStamped.h>
#include <std_msgs/Bool.h>
#include <pcl-1.7/pcl/surface/convex_hull.h>

struct RobotState
{
  std::string name;
  float position;
  float velocity;
  float effort;
};

class RobotStateInformer
{
private:
  // private constructor to disable user from creating objects
  RobotStateInformer(ros::NodeHandle nh);
  static RobotStateInformer* currentObject_;
  RobotDescription* rd_;

  ros::NodeHandle nh_;
  tf::TransformListener listener_;
  std::string robotName_;

  ros::Subscriber jointStateSub_;
  void jointStateCB(const sensor_msgs::JointState::Ptr msg);
  sensor_msgs::JointState::Ptr currentStatePtr_;
  std::map<std::string, RobotState> currentState_;
  std::vector<std::string> jointNames_;
  std::mutex currentStateMutex_;

  ros::Subscriber pelvisIMUSub_;
  void pelvisImuCB(const sensor_msgs::Imu::Ptr msg);
  sensor_msgs::Imu::Ptr pelvisImuValue_;

  ros::Subscriber centerOfMassSub_;
  void centerOfMassCB(const geometry_msgs::Point32::Ptr msg);
  geometry_msgs::Point32::Ptr centerOfMassValue_;

  ros::Subscriber capturePointSub_;
  void capturPointCB(const ihmc_msgs::Point2dRosMessage::Ptr msg);
  ihmc_msgs::Point2dRosMessage::Ptr capturePointValue_;

  ros::Subscriber isInDoubleSupportSub_;
  void doubleSupportStatusCB(const std_msgs::Bool& msg);
  bool doubleSupportStatus_;

  ros::Subscriber leftFootForceSensorSub_;
  ros::Subscriber rightFootForceSensorSub_;
  void leftFootForceSensorCB(const geometry_msgs::WrenchStamped::Ptr msg);
  void rightFootForceSensorCB(const geometry_msgs::WrenchStamped::Ptr msg);
  std::map<RobotSide, geometry_msgs::WrenchStamped::Ptr> footWrenches_;

  ros::Subscriber leftWristForceSensorSub_;
  ros::Subscriber rightWristForceSensorSub_;
  void leftWristForceSensorCB(const geometry_msgs::WrenchStamped::Ptr msg);
  void rightWristForceSensorCB(const geometry_msgs::WrenchStamped::Ptr msg);
  std::map<RobotSide, geometry_msgs::WrenchStamped::Ptr> wristWrenches_;

  void populateStateMap();
  void initializeClassMembers();

  ros::Subscriber leftSupportPolygonSub_;
  ros::Subscriber rightSupportPolygonSub_;
  void leftSupportPolygonCB(const ihmc_msgs::SupportPolygonRosMessage msg);
  void rightSupportPolygonCB(const ihmc_msgs::SupportPolygonRosMessage msg);
  ihmc_msgs::SupportPolygonRosMessage leftSupportPolygonIHMCMsg_;
  ihmc_msgs::SupportPolygonRosMessage rightSupportPolygonIHMCMsg_;
  void getConvexHull(const geometry_msgs::Polygon& left_support_polygon, const geometry_msgs::Polygon& right_support_polygon, geometry_msgs::Polygon& final_support_polygon);
  void convertIHMCPolygonMsgToGeomMsg(const ihmc_msgs::SupportPolygonRosMessage& ihmc_msg, geometry_msgs::Polygon& geometry_msg);
  void convertPCLPointCloudToGeomMsgPoint(pcl::PointCloud<pcl::PointXYZ>& convex_polygon_point_cloud,
                                          geometry_msgs::Polygon& final_support_polygon);
  
  void inline geomMsgPointToGeomMsgPoint32(const geometry_msgs::Point& point, geometry_msgs::Point32& point_32)
  {
    point_32.x = point.x;
    point_32.y = point.y;
    point_32.z = point.z;    
  }

  void inline parseParameter(const std::string& paramName, std::string& parameter)
  {
    if (paramName == "left_arm_joint_names" || paramName == "left_arm")
    {
      parameter.assign(TOUGH_COMMON_NAMES::TOPIC_PREFIX + robotName_ + "/" +
                       TOUGH_COMMON_NAMES::LEFT_ARM_JOINT_NAMES_PARAM);
    }
    else if (paramName == "right_arm_joint_names" || paramName == "right_arm")
    {
      parameter.assign(TOUGH_COMMON_NAMES::TOPIC_PREFIX + robotName_ + "/" +
                       TOUGH_COMMON_NAMES::RIGHT_ARM_JOINT_NAMES_PARAM);
    }
    else
    {
      parameter.assign(paramName);
    }
  }

public:
  /**
   * @brief Get the Robot State Informer object. Use this function to access a pointer to the object of
   * RobotStateInformer. Only one object of this class is created and it is shared with all the classes that use
   * RobotStateInformer.
   *
   * @param nh ros Nodehandle
   * @return RobotStateInformer*
   */
  static RobotStateInformer* getRobotStateInformer(ros::NodeHandle nh);
  ~RobotStateInformer();

  /**
   * @brief disable copy constructor. This is required for singleton pattern
   *
   */
  RobotStateInformer(RobotStateInformer const&) = delete;
  /**
   * @brief disable assignment operator. This is required for singleton pattern
   *
   */
  void operator=(RobotStateInformer const&) = delete;

  /**
   * @brief Get the current joint state message
   *
   * @param jointState [output]
   */
  void getJointStateMessage(sensor_msgs::JointState& jointState);

  int getJointNumber(std::string jointName);

  /**
   * @brief Get the current positions of all joints. Ordering is based on
   * joint names.
   *
   * @param positions [output]
   */
  void getJointPositions(std::vector<double>& positions);
  /**
   * @brief Get the current positions of joint names present in the parameter
   *
   * @param paramName - Parameter on ros param server that has an array of joint names
   * @param positions [output]
   * @return true when successful
   * @return false
   */
  bool getJointPositions(const std::string& paramName, std::vector<double>& positions);

  /**
   * @brief Get the current velocities of joints
   *
   * @param velocities [output]
   */
  void getJointVelocities(std::vector<double>& velocities);

  bool getJointVelocities(const std::string& paramName, std::vector<double>& velocities);

  void getJointEfforts(std::vector<double>& efforts);
  bool getJointEfforts(const std::string& paramName, std::vector<double>& efforts);

  double getJointPosition(const std::string& jointName);
  double getJointPosition(const int jointNumber);

  double getJointVelocity(const std::string& jointName);
  double getJointVelocity(const int jointNumber);

  double getJointEffort(const std::string& jointName);
  double getJointEffort(const int jointNumber);

  void getJointNames(std::vector<std::string>& jointNames);

  bool getCurrentPose(const std::string& frameName, geometry_msgs::Pose& pose,
                      const std::string& baseFrame = TOUGH_COMMON_NAMES::WORLD_TF);

  bool getTransform(const std::string& frameName, tf::StampedTransform& transform,
                    const std::string& baseFrame = TOUGH_COMMON_NAMES::WORLD_TF);

  bool transformQuaternion(const geometry_msgs::QuaternionStamped& qt_in, geometry_msgs::QuaternionStamped& qt_out,
                           const std::string target_frame = TOUGH_COMMON_NAMES::WORLD_TF);
  bool transformQuaternion(const geometry_msgs::Quaternion& qt_in, geometry_msgs::Quaternion& qt_out,
                           const std::string& from_frame, const std::string& to_frame = TOUGH_COMMON_NAMES::WORLD_TF);

  bool transformPoint(const geometry_msgs::PointStamped& pt_in, geometry_msgs::PointStamped& pt_out,
                      const std::string target_frame = TOUGH_COMMON_NAMES::WORLD_TF);
  bool transformPoint(const geometry_msgs::Point& pt_in, geometry_msgs::Point& pt_out, const std::string& from_frame,
                      const std::string& to_frame = TOUGH_COMMON_NAMES::WORLD_TF);

  bool transformPose(const geometry_msgs::PoseStamped& pose_in, geometry_msgs::PoseStamped& pose_out,
                     const std::string& to_frame = TOUGH_COMMON_NAMES::WORLD_TF);
  bool transformPose(const geometry_msgs::Pose& pose_in, geometry_msgs::Pose& pose_out, const std::string& from_frame,
                     const std::string& to_frame = TOUGH_COMMON_NAMES::WORLD_TF);
  bool transformPose(const geometry_msgs::Pose2D& pose_in, geometry_msgs::Pose2D& pose_out,
                     const std::string& from_frame, const std::string& to_frame = TOUGH_COMMON_NAMES::WORLD_TF);

  bool transformVector(const geometry_msgs::Vector3& vec_in, geometry_msgs::Vector3& vec_out,
                       const std::string& from_frame, const std::string& to_frame = TOUGH_COMMON_NAMES::WORLD_TF);
  bool transformVector(const geometry_msgs::Vector3Stamped& vec_in, geometry_msgs::Vector3Stamped& vec_out,
                       const std::string target_frame = TOUGH_COMMON_NAMES::WORLD_TF);

  void getFootWrenches(std::map<RobotSide, geometry_msgs::Wrench>& wrenches);
  void getWristWrenches(std::map<RobotSide, geometry_msgs::Wrench>& wrenches);

  void getFootWrench(const RobotSide side, geometry_msgs::Wrench& wrench);
  void getWristWrench(const RobotSide side, geometry_msgs::Wrench& wrench);

  void getFootForce(const RobotSide side, geometry_msgs::Vector3& force);
  void getFootTorque(const RobotSide side, geometry_msgs::Vector3& torque);

  void getWristForce(const RobotSide side, geometry_msgs::Vector3& force);
  void getWristTorque(const RobotSide side, geometry_msgs::Vector3& torque);

  bool isRobotInDoubleSupport();

  void getCapturePoint(geometry_msgs::Point& point);

  void getCenterOfMass(geometry_msgs::Point& point);

  void getPelvisIMUReading(sensor_msgs::Imu& msg);

  void getSupportPolygon(geometry_msgs::Polygon& supportPolygon);

  bool isPointInSupportPolygon(const geometry_msgs::Point& point);
};

#endif  // TOUGH_ROBOT_STATE_INFORMER_H
