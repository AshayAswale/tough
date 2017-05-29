#include <iostream>
#include <time.h>
#include <numeric>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/foreach.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/date_time.hpp>
#include <ros/ros.h>
#include <sensor_msgs/LaserScan.h>
#include <geometry_msgs/Twist.h>
#include <val_task2/val_task2.h>
#include <srcsim/StartTask.h>
#include "val_task_common/val_task_common_utils.h"
#include "val_task2/val_task2_utils.h"
#include <queue>

using namespace std;

#define foreach BOOST_FOREACH

valTask2 *valTask2::currentObject = nullptr;

valTask2* valTask2::getValTask2(ros::NodeHandle nh){
    if(currentObject == nullptr){
        currentObject = new valTask2(nh);
        return currentObject;
    }

    ROS_ERROR("valTask2::getValTask2 : Object already exists");
    assert(false && "valTask2::getValTask2 : Object already exists");
}


// constructor and destrcutor
valTask2::valTask2(ros::NodeHandle nh):
    nh_(nh)
{
    // object for the valkyrie walker
    walker_             = new ValkyrieWalker(nh_, 0.7, 0.7, 0, 0.18);
    pelvis_controller_  = new pelvisTrajectory(nh_);
    walk_track_         = new walkTracking(nh_);
    chest_controller_   = new chestTrajectory(nh_);
    arm_controller_     = new armTrajectory(nh_);



    //initialize all detection pointers
    rover_detector_        = nullptr;
    rover_detector_fine_   = nullptr;
    solar_panel_detector_  = nullptr;
    solar_array_detector_  = nullptr;
    rover_in_map_blocker_  = nullptr;
    panel_grabber_         = nullptr;

    //utils
    task2_utils_    = new task2Utils(nh_);
    robot_state_    = RobotStateInformer::getRobotStateInformer(nh_);

    // Variables
    map_update_count_ = 0;
//    panel_grasping_hand_ = armSide::LEFT;
    // Subscribers
    occupancy_grid_sub_ = nh_.subscribe("/map",10, &valTask2::occupancy_grid_cb, this);
}

// destructor
valTask2::~valTask2(){
    delete walker_;
    delete pelvis_controller_;
    delete walk_track_;
    delete chest_controller_;
    delete arm_controller_;
}

void valTask2::occupancy_grid_cb(const nav_msgs::OccupancyGrid::Ptr msg){
    // Count the number of times map is updated
    ++map_update_count_;
}

bool valTask2::preemptiveWait(double ms, decision_making::EventQueue& queue) {
    // wait for an external trigger
    for (int i = 0; i < 100 && !queue.isTerminated(); i++)
        boost::this_thread::sleep(boost::posix_time::milliseconds(ms / 100.0));
    return queue.isTerminated();
}

// This functions are called based on the remaping in the main.
// when ever a action is published one of these functions will be called
decision_making::TaskResult valTask2::initTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{

    ROS_INFO_STREAM("valTask2::initTask : executing " << name);
    static int retry_count = 0;

    // reset mapcount when retrying the state for the first time
    if(retry_count == 0){
        map_update_count_ = 0;
        taskCommonUtils::moveToInitPose(nh_);
    }

    // the state transition can happen from an event externally or can be geenerated here
    ROS_INFO("valTask2::initTask : Occupancy Grid has been updated %d times, tried %d times", map_update_count_, retry_count);
    if (map_update_count_ > 1) {
        // move to a configuration that is robust while walking
        retry_count = 0;
        pelvis_controller_->controlPelvisHeight(0.9);
        ros::Duration(1.0f).sleep();

        // start the task
        ros::ServiceClient  client = nh_.serviceClient<srcsim::StartTask>("/srcsim/finals/start_task");
        srcsim::StartTask   srv;
        srv.request.checkpoint_id = 1;
        srv.request.task_id       = 2;
        if(client.call(srv)) {
            //what do we do if this call fails or succeeds?
        }
        // generate the event
        eventQueue.riseEvent("/INIT_SUCESSFUL");

    }
    else if (map_update_count_ < 2 && retry_count++ < 40) {
        ROS_INFO("valTask2::initTask : Wait for occupancy grid to be updated with atleast 2 messages");
        ros::Duration(2.0).sleep();
        eventQueue.riseEvent("/INIT_RETRY");
    }
    else {
        retry_count = 0;
        ROS_INFO("valTask2::initTask : Failed to initialize");
        eventQueue.riseEvent("/INIT_FAILED");
    }
    return TaskResult::SUCCESS();

}

decision_making::TaskResult valTask2::detectRoverTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("valTask2::detectRoverTask : executing " << name);

    if(rover_detector_ == nullptr){
        rover_detector_ = new RoverDetector(nh_);
    }

    static int retry_count = 0;
    static int fail_count = 0;
    std::vector<std::vector<geometry_msgs::Pose> > roverPoseWaypoints;
    rover_detector_->getDetections(roverPoseWaypoints);


    ROS_INFO("valTask2::detectRoverTask : Size of poses : %d", (int)roverPoseWaypoints.size());

    //    consider the rover detected if there are more than 1 detections
    if (roverPoseWaypoints.size() > 1 ) {

        // Detections are 3D but we need 2D pose.So convert it here
        std::vector<geometry_msgs::Pose2D> detectedPoses2D;
        int length = roverPoseWaypoints.back().size();
        int Idx = roverPoseWaypoints.size() -1 ;
        for (int i = 0; i < length ; ++i){
            geometry_msgs::Pose2D pose2D;
            // get the last detected pose
            pose2D.x = roverPoseWaypoints[Idx][i].position.x;
            pose2D.y = roverPoseWaypoints[Idx][i].position.y;

            std::cout << "x " << pose2D.x << " y " << pose2D.y << std::endl;

            // get the theta
            pose2D.theta = tf::getYaw(roverPoseWaypoints[Idx][i].orientation);
            detectedPoses2D.push_back(pose2D);
        }
        setRoverWalkGoal(detectedPoses2D);


        ROVER_SIDE roverSide;
        if(rover_detector_->getRoverSide(roverSide)){
            setRoverSide(roverSide == ROVER_SIDE::RIGHT);
            // block rover in /map
//            rover_in_map_blocker_ = new SolarArrayDetector(nh_, detectedPoses2D.back(),is_rover_on_right_);
            //wait for the map to update. This is required to ensure the footsteps dont collide with rover
            //        ros::Duration(0.5).sleep();
            taskCommonUtils::moveToWalkSafePose(nh_);

            retry_count = 0;
            fail_count = 0;
            // update the plane coeffecients
            eventQueue.riseEvent("/DETECTED_ROVER");
            ROS_INFO("detected rover");
            if(rover_detector_ != nullptr) delete rover_detector_;
            rover_detector_ = nullptr;
        }
        else{
            eventQueue.riseEvent("/DETECT_ROVER_RETRY");
        }
    }
    else if(retry_count < 5) {
        ROS_INFO("sleep for 3 seconds for panel detection");
        ++retry_count;
        ros::Duration(2).sleep();
        eventQueue.riseEvent("/DETECT_ROVER_RETRY");
    }   else if (fail_count < 5)    {
        // increment the fail count
        fail_count++;
        task2_utils_->clearPointCloud();
        eventQueue.riseEvent("/DETECT_ROVER_RETRY");
    }

    // if failed for more than 5 times, go to error state
    else {
        // reset the fail count
        fail_count = 0;
        retry_count = 0;
        ROS_INFO("Rover detection failed");
        eventQueue.riseEvent("/DETECT_ROVER_FAILED");
        if(rover_detector_ != nullptr) delete rover_detector_;
        rover_detector_ = nullptr;
    }

    while(!preemptiveWait(1000, eventQueue)){
        ROS_INFO("waiting for transition");
    }

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::walkToRoverTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM_ONCE("executing " << name);

    // walk to the goal location
    // the goal can be updated on the run time
    static bool executeOnce = true;
    static geometry_msgs::Pose2D goal , pose_prev;

    static int fail_count = 0;
    static std::queue<geometry_msgs::Pose2D> goal_waypoints;
    // Run this block once during every visit of the state from either a failed state or previous state
    if(executeOnce){
        for (auto pose : rover_walk_goal_waypoints_){
            ROS_INFO("X: %.2f  Y:%.2f  Theta:%.2f", pose.x, pose.y, pose.theta);
            goal_waypoints.push(pose);
        }
        goal = goal_waypoints.front();
        goal_waypoints.pop();
        fail_count = 0;
        executeOnce = false;
        geometry_msgs::Pose2D  temp;
        pose_prev = temp;
    }

    geometry_msgs::Pose current_pelvis_pose;
    robot_state_->getCurrentPose(VAL_COMMON_NAMES::PELVIS_TF,current_pelvis_pose);
    // Robot will walk to coarse goal first. once that is reached, goal is changed to fine goal and the flag variable is updated
    if ( taskCommonUtils::isGoalReached(current_pelvis_pose, goal) ) {
        ROS_INFO("Local goal reached. Number of waypoints remaining %d", goal_waypoints.size());
        // if coarse goal was already reached before, then the current state is fine goal reached, else it is coarse goal reached
        if(goal_waypoints.empty()){
            ROS_INFO("reached The rover");
            ros::Duration(3).sleep(); // This is required for steps to complete
            ROS_INFO("Final few steps before we reach rover");
            eventQueue.riseEvent("/REACHED_ROVER");
            executeOnce = true;
            ros::Duration(1).sleep();
        }
        else
        {
            ROS_INFO("%d more waypoints to go", goal_waypoints.size());
            goal= goal_waypoints.front();
            goal_waypoints.pop();
            ros::Duration(3).sleep();
            eventQueue.riseEvent("/WALK_EXECUTING");
        }
    }
    // check if the pose is changed
    else if (taskCommonUtils::isPoseChanged(pose_prev, goal)) {
        ROS_INFO_STREAM("pose chaned to "<<goal);
        walker_->walkToGoal(goal, false);
        // sleep so that the walk starts
        ROS_INFO("Footsteps should be generated now");
        ros::Duration(4).sleep();
        // update the previous pose
        pose_prev = goal;
        eventQueue.riseEvent("/WALK_EXECUTING");
    }
    // if walking stay in the same state
    else if (walk_track_->isWalking())
    {
        // no state change
        ROS_INFO_THROTTLE(2, "walking");
        eventQueue.riseEvent("/WALK_EXECUTING");
    }
    // if walk finished
    // if failed for more than 5 times, go to error state
    else if (fail_count > 5)
    {
        // reset all the static variables in next run
        executeOnce = true;
        ROS_INFO("walk failed");
        eventQueue.riseEvent("/WALK_FAILED");
    }
    // if failed retry detecting the panel and then walk
    else
    {
        // increment the fail count
        fail_count++;
        ROS_INFO("walk retry");
        eventQueue.riseEvent("/WALK_RETRY");
    }

    // wait infinetly until an external even occurs
    while(!preemptiveWait(1000, eventQueue)){
        ROS_INFO("waiting for transition");
    }

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::detectPanelTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("executing " << name);
    static bool isFirstRun = true;
    if(solar_panel_detector_ == nullptr) {
        if (!isFirstRun){
            ROS_INFO("Clearing pointcloud");
            task2_utils_->clearPointCloud();
        }
        task2_utils_->resumePointCloud();
        isFirstRun = false;
        solar_panel_detector_ = new SolarPanelDetect(nh_, rover_walk_goal_waypoints_.back(), is_rover_on_right_);
        chest_controller_->controlChest(0, 0, 0);
        arm_controller_->moveToDefaultPose(armSide::RIGHT);
        arm_controller_->moveToDefaultPose(armSide::LEFT);
        ros::Duration(0.2).sleep();
    }

    static int fail_count = 0;
    static int retry_count = 0;

    // detect solar panel
    std::vector<geometry_msgs::Pose> poses;
    solar_panel_detector_->getDetections(poses);

    // if we get atleast two detections
    if (poses.size() > 1)
    {
        size_t idx = poses.size()-1;
        setSolarPanelHandlePose(poses[idx]);

        ROS_INFO_STREAM("Position " << poses[idx].position.x<< " " <<poses[idx].position.y <<" "<<poses[idx].position.z);
        ROS_INFO_STREAM("quat " << poses[idx].orientation.x << " " <<poses[idx].orientation.y <<" "<<poses[idx].orientation.z <<" "<<poses[idx].orientation.w);
        fail_count = 0;
        retry_count = 0;
        eventQueue.riseEvent("/DETECTED_PANEL");
        if(solar_panel_detector_ != nullptr) delete solar_panel_detector_;
        solar_panel_detector_ = nullptr;
    }

    else if(retry_count < 5) {
        ROS_INFO("sleep for 3 seconds for panel detection");
        ++retry_count;
        eventQueue.riseEvent("/DETECT_PANEL_RETRY");
        ros::Duration(3).sleep();
    }
    // if failed for more than 5 times, go to error state
    else if (fail_count > 5)
    {
        // reset the fail count
        fail_count = 0;
        retry_count = 0;
        eventQueue.riseEvent("/DETECT_PANEL_FAILED");
        if(solar_panel_detector_ != nullptr) delete solar_panel_detector_;
        solar_panel_detector_ = nullptr;
    }
    // if failed retry detecting the panel
    else
    {
        // increment the fail count
        fail_count++;
        eventQueue.riseEvent("/DETECT_PANEL_RETRY");
    }

    while(!preemptiveWait(1000, eventQueue)){
        ROS_INFO("waiting for transition");
    }

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::graspPanelTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("executing " << name);
    // we reached here means we don't need the map blocker anymore.
    if (rover_in_map_blocker_ != nullptr){
        delete rover_in_map_blocker_;
        rover_in_map_blocker_  = nullptr;
    }

    /*
     * Executing -> when grasp_handles is called
     * Retry -> grasp handles is called but it failed
     * Failed -> retry failed 5 times
     * Success -> grasp is successful
     */
    static bool executing = false;
    static int retry_count = 0;
    static armSide side;
    if(panel_grabber_ == nullptr){
        panel_grabber_ = new solar_panel_handle_grabber(nh_,BUTTON_LOCATION::RIGHT);
    }

    if(!executing){
        ROS_INFO("Executing the grasp panel handle command");
        executing = true;
        // grasp the handle
        geometry_msgs::Pose poseInPelvisFrame;
        robot_state_->transformPose(solar_panel_handle_pose_, poseInPelvisFrame, VAL_COMMON_NAMES::WORLD_TF, VAL_COMMON_NAMES::PELVIS_TF);
        float yaw = tf::getYaw(poseInPelvisFrame.orientation);
        ROS_INFO("Yaw Value : %f",yaw);
        // if the vector is pointing outwards, reorient it
        if (yaw > M_PI_2 || yaw < -M_PI_2){
            geometry_msgs::Pose tempYaw(solar_panel_handle_pose_);
            SolarPanelDetect::invertYaw(tempYaw);
            setSolarPanelHandlePose(tempYaw);

            robot_state_->transformPose(solar_panel_handle_pose_, poseInPelvisFrame, VAL_COMMON_NAMES::WORLD_TF, VAL_COMMON_NAMES::PELVIS_TF);
            yaw = tf::getYaw(poseInPelvisFrame.orientation);
        }

        side = yaw < 0 ? armSide::LEFT : armSide::RIGHT;
        setPanelGraspingHand(side);
        //Pause the pointcloud to avoid obstacles on map due to panel
        task2_utils_->pausePointCloud();
        if (panel_grabber_->grasp_handles(side, solar_panel_handle_pose_)) {
            ros::Duration(0.2).sleep(); //wait till grasp is complete
            ROS_INFO("Grasp is successful");
            eventQueue.riseEvent("/GRASPED_PANEL");
        }
        else{
            ++retry_count;
            eventQueue.riseEvent("/GRASP_RETRY");
            executing = false;
        }
    } else if (retry_count < 5 ){
        ROS_INFO("Grasp Failed, retrying");
        executing = false;
        ++retry_count;
        eventQueue.riseEvent("/GRASP_RETRY");
    }
    else {
        ROS_INFO("Failed all conditions. state error");
        executing = false;
        retry_count = 0;
        eventQueue.riseEvent("/GRASP_PANEL_FAILED");
    }

    // generate the event
    while(!preemptiveWait(1000, eventQueue)){
        ROS_INFO("waiting for transition");
    }

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::pickPanelTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("valTask2::pickPanelTask : executing " << name);

    static bool pickPanel= false;
    static bool retry = 0;
    if (!pickPanel){
        ROS_INFO("valTask2::pickPanelTask : Moving panel close to body");
        task2_utils_->afterPanelGraspPose(panel_grasping_hand_);
        pickPanel = true;
        eventQueue.riseEvent("/PICK_PANEL_RETRY");
    } else if (task2_utils_->isPanelPicked(panel_grasping_hand_)){

        ROS_INFO("valTask2::pickPanelTask : Walking 1 step back");
        ros::Duration(0.5).sleep();
        std::vector<float> x_offset={-0.3,-0.3};
        std::vector<float> y_offset={0.0,0.1};
        walker_->walkLocalPreComputedSteps(x_offset,y_offset,RIGHT);
        ros::Duration(3).sleep();

        /// @todo The following code should be a new state
        // reorient the robot.
        ROS_INFO("valTask2::pickPanelTask : Reorienting");
        armSide startingSide;
        geometry_msgs::Pose pose;
        pose.position.x = 0.0;
        pose.orientation.w = 1.0;

        if (is_rover_on_right_){
            pose.position.y = 0.5;
            startingSide = armSide::LEFT;
        } else{
            pose.position.y = -0.5;
            startingSide = armSide::RIGHT;
        }
        robot_state_->transformPose(pose, pose, VAL_COMMON_NAMES::PELVIS_TF, VAL_COMMON_NAMES::WORLD_TF);
        geometry_msgs::Pose2D pose2D;
        pose2D.x = pose.position.x;
        pose2D.y = pose.position.y;
        pose2D.theta = rover_walk_goal_waypoints_.front().theta;
        ROS_INFO("Walking to x:%f y:%f theta:%f", pose2D.x,pose2D.y,pose2D.theta);
        walker_->walkToGoal(pose2D);

        ros::Duration(1).sleep();
        task2_utils_->movePanelToWalkSafePose(panel_grasping_hand_);
        eventQueue.riseEvent("/PICKED_PANEL");
    }
    else if (retry < 5){
        ROS_INFO("Panel is not in hand. retrying detection of panel");
        ++retry;
        pickPanel=false;
        //grasp is done, but panel is not in hand
        eventQueue.riseEvent("/PICK_PANEL_RETRY_GRASP");
    }
    else{
        ROS_INFO("All attempts of picking up the panel failed");
        retry = 0;
        pickPanel=false;
        eventQueue.riseEvent("/PICKUP_FAILED");
    }

    // generate the event
    while(!preemptiveWait(1000, eventQueue)){
        ROS_INFO("waiting for transition");
    }

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::detectSolarArrayTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("valTask2::detectSolarArrayTask : executing " << name);

    if(solar_array_detector_ == nullptr) {
        solar_array_detector_ = new SolarArrayDetector(nh_, rover_walk_goal_waypoints_.back(), is_rover_on_right_);
        ros::Duration(0.2).sleep();
    }

    static int fail_count = 0;
    static int retry_count = 0;

    // detect solar array
    std::vector<geometry_msgs::Pose> poses;
    solar_array_detector_->getDetections(poses);

    // if we get atleast two detections
    if (poses.size() > 1) {
        // get the last detected pose
        int idx = poses.size() -1 ;

        geometry_msgs::Pose2D pose2D;
        pose2D.x = poses[idx].position.x;
        pose2D.y = poses[idx].position.y;
        // get the theta
        pose2D.theta = tf::getYaw(poses[idx].orientation);

        setSolarArrayWalkGoal(pose2D);

        ROS_INFO("valTask2::detectSolarArrayTask : Position x:%f y:%f z:%f",poses[idx].position.x,poses[idx].position.y,poses[idx].position.z);
        ROS_INFO("valTask2::detectSolarArrayTask : yaw:%f",pose2D.theta);
        retry_count = 0;
        eventQueue.riseEvent("/DETECTED_ARRAY");
    }

    else if(retry_count < 5) {
        ROS_INFO("valTask2::detectSolarArrayTask : sleep 3 seconds for panel detection");
        ++retry_count;
        eventQueue.riseEvent("/DETECT_ARRAY_RETRY");
        ros::Duration(3).sleep();
    }
    // if failed for more than 5 times, go to error state
    else if (fail_count > 5)
    {
        // reset the fail count
        ROS_INFO("valTask2::detectSolarArrayTask : Failed 5 times. transitioning to error state");
        fail_count = 0;
        eventQueue.riseEvent("/DETECT_ARRAY_FAILED");
        if(solar_array_detector_ != nullptr) delete solar_array_detector_;
        solar_array_detector_ = nullptr;
    }
    // if failed retry detecting the panel
    else
    {
        ROS_INFO("valTask2::detectSolarArrayTask : Failed attempt. trying again");
        // increment the fail count
        fail_count++;
        eventQueue.riseEvent("/DETECT_ARRAY_RETRY");
    }

    while(!preemptiveWait(1000, eventQueue)){
        ROS_INFO("valTask2::detectSolarArrayTask : waiting for transition");
    }

    return TaskResult::SUCCESS();


}

decision_making::TaskResult valTask2::walkSolarArrayTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("valTask2::walkSolarArrayTask : executing " << name);
    static int fail_count = 0;

    // walk to the goal location
    // the goal can be updated on the run time
    static geometry_msgs::Pose2D pose_prev;

    geometry_msgs::Pose current_pelvis_pose;
    robot_state_->getCurrentPose(VAL_COMMON_NAMES::PELVIS_TF,current_pelvis_pose);

    if ( taskCommonUtils::isGoalReached(current_pelvis_pose, solar_array_walk_goal_) ) {
        ROS_INFO("valTask2::walkSolarArrayTask : reached solar array");
        task2_utils_->resumePointCloud();
        ros::Duration(1).sleep();
        // TODO: check if robot rechead the panel
        eventQueue.riseEvent("/REACHED_ARRAY");
    }
    // check if the pose is changed
    else if (taskCommonUtils::isPoseChanged(pose_prev, solar_array_walk_goal_)) {
        ROS_INFO_STREAM("valTask2::walkSolarArrayTask : pose chaned to "<<solar_array_walk_goal_);
        walker_->walkToGoal(solar_array_walk_goal_, false);
        // sleep so that the walk starts
        ROS_INFO("valTask2::walkSolarArrayTask : Footsteps should be generated now");
        ros::Duration(4).sleep();
        // update the previous pose
        pose_prev = solar_array_walk_goal_;
        eventQueue.riseEvent("/WALK_TO_ARRAY_EXECUTING");
    }
    // if walking stay in the same state
    else if (walk_track_->isWalking())
    {
        // no state change
        ROS_INFO_THROTTLE(2, "valTask2::walkSolarArrayTask : walking");
        eventQueue.riseEvent("/WALK_TO_ARRAY_EXECUTING");
    }
    // if walk finished
    // if failed for more than 5 times, go to error state
    else if (fail_count > 5)
    {
        // reset the fail count
        fail_count = 0;
        ROS_INFO("valTask2::walkSolarArrayTask : walk failed");
        eventQueue.riseEvent("/WALK_TO_ARRAY_FAILED");
    }
    // if failed retry detecting the array and then walk
    else
    {
        // increment the fail count
        fail_count++;
        ROS_INFO("valTask2::walkSolarArrayTask : walk retry");
        eventQueue.riseEvent("/WALK_TO_ARRAY_RETRY");
    }

    // wait infinetly until an external even occurs
    while(!preemptiveWait(1000, eventQueue)){
        ROS_INFO("valTask2::walkSolarArrayTask : waiting for transition");
    }

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::placePanelTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("valTask2::placePanelTask : executing " << name);
    static bool handsPulledOff = false;

    /************************************
     *  Get into panel placement pose
     *  move down till effort reduces
     *  open grippers
     *  pull hand back
     ************************************
     */
    if (task2_utils_->isPanelPicked(panel_grasping_hand_)){
     ROS_INFO("valTask2::placePanelTask : Placing the panel on table");
        task2_utils_->moveToPlacePanelPose(panel_grasping_hand_, false);
        ros::Duration(1).sleep();
        eventQueue.riseEvent("/PLACE_ON_GROUND_RETRY");
    }
    else if (!handsPulledOff){

        geometry_msgs::Pose currentPalmPose;
        std::string palmFrame = panel_grasping_hand_ == armSide ::LEFT ? VAL_COMMON_NAMES::L_PALM_TF : VAL_COMMON_NAMES::R_PALM_TF;

        robot_state_->getCurrentPose(palmFrame, currentPalmPose, VAL_COMMON_NAMES::PELVIS_TF);
        currentPalmPose.position.x -= 0.2;
        robot_state_->transformPose(currentPalmPose, currentPalmPose, VAL_COMMON_NAMES::PELVIS_TF, VAL_COMMON_NAMES::WORLD_TF);

        arm_controller_->moveArmInTaskSpace(panel_grasping_hand_, currentPalmPose, 1.0f);
        ros::Duration(1).sleep();

        arm_controller_->moveToZeroPose(panel_grasping_hand_);
        ros::Duration(0.5).sleep();
        arm_controller_->moveToZeroPose((armSide)!panel_grasping_hand_);
        ros::Duration(0.5).sleep();
        handsPulledOff = true;
        eventQueue.riseEvent("/PLACE_ON_GROUND_RETRY");
    }
    else if (task2_utils_->getCurrentCheckpoint() > 2){
        ROS_INFO("valTask2::placePanelTask : Placed the panel successfully");
         eventQueue.riseEvent("/PLACED_ON_GROUND");
    }
    else {
        ROS_INFO("valTask2::placePanelTask : Panel placement failed");
         eventQueue.riseEvent("/PLACED_ON_GROUND_FAILED");
    }

    // generate the event
    eventQueue.riseEvent("/REACHED_ROVER");

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::detectButtonTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("executing " << name);

    if(button_detector_ == nullptr) {
        button_detector_ = new ButtonDetector(nh_);
    }

    static int retry_count = 0;

    if (retry_count == 0){
        //tilt head downwards to see the panel
        head_controller_->moveHead(0.0f, 20.0f, 0.0f, 2.0f);
        //wait for head to be in position
        ros::Duration(3).sleep();
    }

    //detect button
    if( button_detector_->findButtons(button_coordinates_)){

        ROS_INFO_STREAM("Button detected at "<<button_coordinates_.x<< " , "<<button_coordinates_.y<<" , "<<button_coordinates_.z);

        // generate the event
        eventQueue.riseEvent("/BUTTON_DETECTED");
    }
    else if( retry_count++ < 10){
        ROS_INFO("Did not detect button, retrying");
        eventQueue.riseEvent("/DETECT_BUTTON_RETRY");
    }
    else{
        ROS_INFO("Did not detect handle, failed");
        eventQueue.riseEvent("/BUTTON_DETECTION_FAILED");
        retry_count = 0;
    }

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::deployPanelTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("executing " << name);

    // generate the event
    eventQueue.riseEvent("/REACHED_ROVER");

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::detectCableTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("executing " << name);

    if(cable_detector_ == nullptr) {
        cable_detector_ = new CableDetector(nh_);
    }

    static int retry_count = 0;

    if (retry_count == 0){
        //tilt head downwards to see the panel
        head_controller_->moveHead(0.0f, 40.0f, 0.0f, 2.0f);
        //wait for head to be in position
        ros::Duration(3).sleep();
    }

    //detect cable
    if( cable_detector_->findCable(cable_coordinates_)){

        ROS_INFO_STREAM("Cable detected at "<<cable_coordinates_.x<< " , "<<cable_coordinates_.y<<" , "<<cable_coordinates_.z);

        // generate the event
        eventQueue.riseEvent("/DETECTED_CABLE");
    }
    else if( retry_count++ < 10){
        ROS_INFO("Did not detect cable, retrying");
        eventQueue.riseEvent("/DETECT_CABLE_RETRY");
    }
    else{
        ROS_INFO("Did not detect cable, failed");
        eventQueue.riseEvent("/DETECT_CABLE_FAILED");
        retry_count = 0;
    }

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::pickCableTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("executing " << name);

    // generate the event
    eventQueue.riseEvent("/REACHED_ROVER");

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::plugCableTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("executing " << name);

    // generate the event
    eventQueue.riseEvent("/REACHED_ROVER");

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::detectfinishBoxTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("executing " << name);

    // generate the event
    //eventQueue.riseEvent("/INIT_SUCESSUFL");

    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::walkToFinishTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("executing " << name);

    eventQueue.riseEvent("/WALK_TO_END");
    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::endTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("executing " << name);

    eventQueue.riseEvent("/STOP_TIMEOUT");
    return TaskResult::SUCCESS();
}

decision_making::TaskResult valTask2::errorTask(string name, const FSMCallContext& context, EventQueue& eventQueue)
{
    ROS_INFO_STREAM("executing " << name);

    // generate the event
    //eventQueue.riseEvent("/INIT_SUCESSUFL");

    return TaskResult::SUCCESS();
}


// setter and getter methods
geometry_msgs::Pose2D valTask2::getPanelWalkGoal()
{
    return panel_walk_goal_;
}

void valTask2::setPanelWalkGoal(const geometry_msgs::Pose2D &panel_walk_goal)
{
    panel_walk_goal_ = panel_walk_goal;
}

void valTask2::setRoverWalkGoal(const std::vector<geometry_msgs::Pose2D> &rover_walk_goal)
{
    rover_walk_goal_waypoints_ = rover_walk_goal;

}

void valTask2::setRoverSide(const bool isRoverOnRight)
{
    is_rover_on_right_ = isRoverOnRight;
}

void valTask2::setSolarPanelHandlePose(const geometry_msgs::Pose &pose)
{
    solar_panel_handle_pose_ = pose;
}

void valTask2::setSolarArrayWalkGoal(const geometry_msgs::Pose2D &panel_walk_goal)
{
    solar_array_walk_goal_ = panel_walk_goal;
}

void valTask2::setSolarArraySide(const bool isSolarArrayOnRight)
{
    is_array_on_right_ = isSolarArrayOnRight;
}

void valTask2::setPanelGraspingHand(armSide side)
{
    panel_grasping_hand_ = side;
}
