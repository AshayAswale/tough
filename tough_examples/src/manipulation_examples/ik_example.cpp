#include <pluginlib/class_loader.h>
#include <ros/ros.h>
#include <ros/time.h>
#include <tf/tf.h>
#include <geometry_msgs/PoseStamped.h>

#include "tough_common/tough_common_names.h"
#include "tough_moveit_planners/taskspace_planner.h"
#include "tough_controller_interface/wholebody_control_interface.h"
#include <tough_kinematics/tough_kinematics.h>

int main(int argc, char** argv)
{
  ros::init(argc, argv, "motion_planning_tutorial");
  ros::AsyncSpinner spinner(1);
  spinner.start();
  ros::NodeHandle nh;

  WholebodyControlInterface wb_controller(nh);
  // ArmControlInterface arm_controller(nh);
  geometry_msgs::PoseStamped pose;
  pose.pose.orientation.w = 1.0;
  pose.header.frame_id = "pelvis";
  
  if (argc != 4)
  {
    pose.pose.position.x = 0.4;
    pose.pose.position.y = -0.8;
    pose.pose.position.z = 0.2;
  }
  else
  {
    pose.pose.position.x = std::atof(argv[1]);
    pose.pose.position.y = std::atof(argv[2]);
    pose.pose.position.z = std::atof(argv[3]);
  }
  std::vector<double> joint_angles;
  trajectory_msgs::JointTrajectory result_joint_angles;

  ToughKinematics tough_kinematics(nh);
  if(tough_kinematics.solve_ik(TOUGH_COMMON_NAMES::RIGHT_ARM_10DOF_GROUP, pose, result_joint_angles))
    wb_controller.executeTrajectory(result_joint_angles);
  else
    ROS_ERROR("COULD NOT PLAN FOR THE TRAJECTORY");

  // float solve_fraction = tough_kinematics.solve_ik(TOUGH_COMMON_NAMES::RIGHT_ARM_7DOF_GROUP, pose, joint_angles);
  // TaskspacePlanner man(nh);
  // man.solve_ik(TOUGH_COMMON_NAMES::RIGHT_ARM_7DOF_GROUP, pose, joint_angles);

  // std::vector<std::vector<double> > arm_poses;
  // arm_poses.push_back(joint_angles);
  // // wb_controller.executeTrajectory()
  // if(solve_fraction>0.9)
  // arm_controller.moveArmJoints(RobotSide::RIGHT, arm_poses, 1.0f);
  // // wb_controller.executeTrajectory()
  // else
  //   ROS_ERROR("COULD NOT PLAN FOR THE TRAJECTORY");
  ros::Duration(1).sleep();
  return 0;
}
