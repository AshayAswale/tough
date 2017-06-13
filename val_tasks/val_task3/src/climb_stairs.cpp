#include <val_task3/climb_stairs.h>

climbStairs::climbStairs(ros::NodeHandle nh): nh_(nh)
{
  walker_      = new ValkyrieWalker(nh_, 0.8f, 0.8f, 0, STEP_HEIGHT);
  chest_       = new chestTrajectory(nh_);
  pelvis_      = new pelvisTrajectory(nh_);
  arm_         = new armTrajectory(nh_);

  approach_ = APPROACH;
}

climbStairs::~climbStairs()
{
  if (walker_ != nullptr) delete walker_;
  if (chest_ != nullptr) delete chest_;
  if (pelvis_ != nullptr) delete pelvis_;
  if (arm_ != nullptr) delete arm_;
}

void climbStairs::climb_stairs()
{

  if (approach_ == 1)
  {
    approach_1();
  }
  else if (approach_ == 2)
  {
    approach_2();
  }
}

void climbStairs::approach_1 (void)
{
  // set the chest orientation
  chest_->controlChest(0,20,0);
  ros::Duration(1).sleep();

  // set the arms orientation
  arm_->moveArmJoint(RIGHT, 1, 0.7);
  ros::Duration(1).sleep();
  arm_->moveArmJoint(LEFT, 1, -0.7);
  ros::Duration(1).sleep();

  // first step
  walker_->walkNStepsWRTPelvis(1, FIRSTSTEP_OFFSET, 0.0, true, RIGHT);
  ros::Duration(0.5).sleep();
  pelvis_->controlPelvisHeight(1.0);
  ros::Duration(0.5).sleep();
  walker_->load_eff(armSide::RIGHT, EE_LOADING::LOAD);
  walker_->walkNStepsWRTPelvis(1, FIRSTSTEP_OFFSET, 0.0, true, LEFT);

  // next 8 steps
  for (int i=0; i<8; i++)
  {
    ros::Duration(1.0).sleep();
    pelvis_->controlPelvisHeight(1.0);
    ros::Duration(1.0).sleep();
    walker_->load_eff(armSide::LEFT, EE_LOADING::LOAD);
    walker_->walkNStepsWRTPelvis(1, STEP_DEPTH, 0.0, true, RIGHT);
    ros::Duration(1.0).sleep();
    pelvis_->controlPelvisHeight(1.0);
    ros::Duration(1.0).sleep();
    walker_->load_eff(armSide::RIGHT, EE_LOADING::LOAD);
    walker_->walkNStepsWRTPelvis(1, STEP_DEPTH, 0.0, true, LEFT);
  }

  // last step required to finish the check point
  walker_->setSwing_height(DEFAULT_SWINGHEIGHT);
  walker_->walkNStepsWRTPelvis(2, 0.3, 0.0);
  ros::Duration(0.1).sleep();

}

void climbStairs::approach_2 (void)
{
  // set the chest orientation
  chest_->controlChest(0,20,0);
  ros::Duration(1).sleep();

  // set the arms orientation
  arm_->moveArmJoint(RIGHT, 1, 0.7);
  ros::Duration(1).sleep();
  arm_->moveArmJoint(LEFT, 1, -0.7);

  pelvis_->controlPelvisHeight(1.0);

  // first step
  walker_->walkNStepsWRTPelvis(1, 0.3, 0.0, true, RIGHT);
  ros::Duration(1).sleep();
  pelvis_->controlPelvisHeight(1.0);
  ros::Duration(1).sleep();
  walker_->load_eff(armSide::RIGHT, EE_LOADING::LOAD);
  walker_->walkNStepsWRTPelvis(1, 0.3, 0.0, true, LEFT);

  // starting step to keep track of the x co-ordinate
  ihmc_msgs::FootstepDataRosMessage l_foot, r_foot;

  // get the current foot steps
  walker_->getCurrentStep(LEFT, l_foot);
  walker_->getCurrentStep(RIGHT, r_foot);
  geometry_msgs::Point currentWorldLocation,currentPelvisLocation;
  // start x values
  float l_x, r_x;
  currentWorldLocation.x = r_foot.location.x;
  currentWorldLocation.y = r_foot.location.y;
  currentWorldLocation.z = r_foot.location.z;
  // transform the step to pelvis
  current_state_->transformPoint(currentWorldLocation,currentPelvisLocation,VAL_COMMON_NAMES::WORLD_TF,VAL_COMMON_NAMES::PELVIS_TF);
  r_x = currentPelvisLocation.x;

  currentWorldLocation.x = l_foot.location.x;
  currentWorldLocation.y = l_foot.location.y;
  currentWorldLocation.z = l_foot.location.z;
  // transform the step to pelvis
  current_state_->transformPoint(currentWorldLocation,currentPelvisLocation,VAL_COMMON_NAMES::WORLD_TF,VAL_COMMON_NAMES::PELVIS_TF);
  l_x = currentPelvisLocation.x;

  ihmc_msgs::FootstepDataListRosMessage step_list;
  step_list.default_swing_time = 0.8;
  step_list.default_transfer_time = 0.8;

  // next 8 steps
  for (int i=1; i<=8; i++)
  {

    // right foot step
    ros::Duration(1).sleep();
    pelvis_->controlPelvisHeight(1.0);
    ros::Duration(1).sleep();
    walker_->load_eff(armSide::LEFT, EE_LOADING::LOAD);
    walker_->getCurrentStep(RIGHT, r_foot);
    currentWorldLocation.x = r_foot.location.x;
    currentWorldLocation.y = r_foot.location.y;
    currentWorldLocation.z = r_foot.location.z;
    // transform the step to pelvis
    current_state_->transformPoint(currentWorldLocation,currentPelvisLocation,VAL_COMMON_NAMES::WORLD_TF,VAL_COMMON_NAMES::PELVIS_TF);
    // add the offsets wrt to pelvis
    ROS_INFO("right foot %f", (r_x + (0.2389*i) - currentPelvisLocation.x));
    currentPelvisLocation.x+= (r_x + (0.2389*i) - currentPelvisLocation.x) ;

    //currentPelvisLocation.y+=y;
    // tranform back the point to world
    current_state_->transformPoint(currentPelvisLocation,currentWorldLocation,VAL_COMMON_NAMES::PELVIS_TF,VAL_COMMON_NAMES::WORLD_TF);

    // update the new location
    r_foot.location.x=currentWorldLocation.x;
    r_foot.location.y=currentWorldLocation.y;
    r_foot.location.z=currentWorldLocation.z;
    r_foot.swing_height = 0.3;
    step_list.footstep_data_list.clear();
    step_list.footstep_data_list.push_back(r_foot);
    step_list.unique_id = i+1;
    walker_->walkGivenSteps(step_list);

    // left foot step
    ros::Duration(1).sleep();
    pelvis_->controlPelvisHeight(1.0);
    ros::Duration(1).sleep();
    walker_->load_eff(armSide::RIGHT, EE_LOADING::LOAD);
    walker_->getCurrentStep(LEFT, l_foot);
    currentWorldLocation.x = l_foot.location.x;
    currentWorldLocation.y = l_foot.location.y;
    currentWorldLocation.z = l_foot.location.z;
    // transform the step to pelvis
    current_state_->transformPoint(currentWorldLocation,currentPelvisLocation,VAL_COMMON_NAMES::WORLD_TF,VAL_COMMON_NAMES::PELVIS_TF);
    // add the offsets wrt to pelvis
    ROS_INFO("left foot %f", (l_x + (0.2389*i) - currentPelvisLocation.x));
    currentPelvisLocation.x+= (l_x + (0.2389*i) - currentPelvisLocation.x) ;
    //currentPelvisLocation.y+=y
    // tranform back the point to world
    current_state_->transformPoint(currentPelvisLocation,currentWorldLocation,VAL_COMMON_NAMES::PELVIS_TF,VAL_COMMON_NAMES::WORLD_TF);

    // update the new location
    l_foot.location.x=currentWorldLocation.x;
    l_foot.location.y=currentWorldLocation.y;
    l_foot.location.z=currentWorldLocation.z;
    l_foot.swing_height = 0.3;
    step_list.footstep_data_list.clear();
    step_list.footstep_data_list.push_back(l_foot);
    step_list.unique_id = i+2;
    walker_->walkGivenSteps(step_list);
  }
}