#ifndef MAP_GENERATOR_H
#define MAP_GENERATOR_H

#define MAP_RESOLUTION 0.05
#define MAP_HEIGHT     50/MAP_RESOLUTION
#define MAP_WIDTH      50/MAP_RESOLUTION

#include <ros/ros.h>
#include <nav_msgs/OccupancyGrid.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud2_iterator.h>

class MapGenerator{

public:
    MapGenerator(ros::NodeHandle &n);
    ~MapGenerator();

private:

    void convertToOccupancyGrid(const sensor_msgs::PointCloud2Ptr msg);
    void convertToMapIndices(float &x, float &y);
    ros::NodeHandle nh_;
    ros::Subscriber pointcloudSub_;
    ros::Publisher  mapPub_;
    nav_msgs::OccupancyGrid occGrid_;

};

#endif // MAP_GENERATOR_H


