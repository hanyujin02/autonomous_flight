/*
	FILE: viewpoint_inspection_node.cpp
	-----------------------------
	dynamic inspection ROS node
*/

#include <autonomous_flight/px4/viewpointInspection.h>

int main(int argc, char** argv){
	ros::init(argc, argv, "viewpoint_inspection_node");
	ros::NodeHandle nh;
	AutoFlight::viewpointInspection inspector (nh);
	inspector.run();

	ros::spin();

	return 0;
}