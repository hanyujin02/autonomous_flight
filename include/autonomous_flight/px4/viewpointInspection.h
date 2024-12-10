
#ifndef AUTOFLIGHT_VIEWPOINT_INSPECTION_H
#define AUTOFLIGHT_VIEWPOINT_INSPECTION_H

#include <autonomous_flight/px4/flightBase.h>
#include <map_manager/dynamicMap.h>
#include <onboard_detector/fakeDetector.h>
#include <dynamic_predictor/dynamicPredictor.h>
#include <global_planner/rrtOccMap.h>
#include <global_planner/viewpointGenerator.h>
#include <trajectory_planner/polyTrajOccMap.h>
#include <trajectory_planner/piecewiseLinearTraj.h>
#include <trajectory_planner/bsplineTraj.h>
#include <trajectory_planner/mpcPlanner.h>

namespace AutoFlight{

	enum PLANNER {BSPLINE, MPC, MIXED};

	class viewpointInspection : public flightBase{
	private:
		std::shared_ptr<mapManager::dynamicMap> map_;
		std::shared_ptr<onboardDetector::fakeDetector> detector_;
		std::shared_ptr<dynamicPredictor::predictor> predictor_;
		std::shared_ptr<globalPlanner::rrtOccMap<3>> rrtPlanner_;
        std::shared_ptr<globalPlanner::vpPlanner> vpPlanner_;
		std::shared_ptr<trajPlanner::polyTrajOccMap> polyTraj_;
		std::shared_ptr<trajPlanner::pwlTraj> pwlTraj_;
		std::shared_ptr<trajPlanner::bsplineTraj> bsplineTraj_;
		std::shared_ptr<trajPlanner::mpcPlanner> mpc_;

		ros::Timer mpcTimer_;
		ros::Timer plannerTimer_;
		ros::Timer replanCheckTimer_;
        ros::Timer goalCheckTimer_;
		ros::Timer trajExeTimer_;
		ros::Timer visTimer_;
		ros::Timer freeMapTimer_;

		ros::Publisher rrtPathPub_;
		ros::Publisher polyTrajPub_;
		ros::Publisher pwlTrajPub_;
		ros::Publisher bsplineTrajPub_;
		ros::Publisher mpcTrajPub_;
		ros::Publisher inputTrajPub_;
		ros::Publisher goalPub_;
		ros::Publisher raycastVisPub_;

		std::thread mpcWorker_;

		// parameters
		bool useFakeDetector_;
		bool usePredictor_;
		bool useGlobalPlanner_;
		bool useBsplinePlanner_;
		bool useMPCPlanner_;
		bool noYawTurning_;
		bool useYawControl_;
		// bool usePredefinedGoal_;
		double desiredVel_;
		double desiredAcc_;
		double desiredAngularVel_;
		double replanTimeForDynamicObstacle_;
		std::string trajSavePath_;
		// nav_msgs::Path predefinedGoal_;
        std::vector<std::vector<Eigen::Vector4d>> viewpoints_;
		std::vector<Eigen::Vector2i> vpIdx_;
		int goalIdx_ = -1;
		int repeatPathNum_;
		PLANNER plannerType_;

		// navigation data
		bool bsplineReplan_ = false;
		bool mpcReplan_ = false;
        bool goalReplan_ = false;
		bool replanning_ = false;
		bool needGlobalPlan_ = false;
		bool globalPlanReady_ = false;
		bool refTrajReady_ = false;
		bool mpcFirstTime_ = false;
		nav_msgs::Path rrtPathMsg_;
		nav_msgs::Path polyTrajMsg_;
		nav_msgs::Path pwlTrajMsg_;
		nav_msgs::Path bsplineTrajMsg_;
		nav_msgs::Path mpcTrajMsg_;
		nav_msgs::Path inputTrajMsg_;
		bool mpcTrajectoryReady_ = false;
		bool bsplineTrajectoryReady_ = false;
		ros::Time trajStartTime_;
		ros::Time trackingStartTime_;
		double trajTime_; // current trajectory time
		double prevInputTrajTime_ = 0.0;
		trajPlanner::bspline trajectory_; // trajectory data for tracking
		double facingYaw_;
		double viewAngle_;
		bool firstTimeSave_ = false;
		bool lastDynamicObstacle_ = false;
		ros::Time lastDynamicObstacleTime_;
		Eigen::Vector3d startPos_;
		std::vector<std::vector<Eigen::Vector3d>> hitPoints_;
		
	public:
		viewpointInspection(const ros::NodeHandle& nh);
		void initParam();
		void initModules();
		void registerPub();
		void registerCallback();
		void initViewpoints();

		void mpcCB();
		void plannerCB(const ros::TimerEvent&);
		void staticPlannerCB(const ros::TimerEvent&);
		void replanCheckCB(const ros::TimerEvent&);
        void goalCheckCB(const ros::TimerEvent&);
		void trajExeCB(const ros::TimerEvent&);
		void visCB(const ros::TimerEvent&);
		void freeMapCB(const ros::TimerEvent&); // using fake detector

		void run();	
		double getViewAngle();
		void getInaccessibleView(std::vector<Eigen::Vector2i> &inaccessibleIdx);
		void getStartEndConditions(std::vector<Eigen::Vector3d>& startEndConditions);	
		bool goalHasCollision();
        bool vpHasCollision(const Eigen::Vector4d &vp);
		bool mpcHasCollision();
		bool bsplineHasCollision();
		bool hasCollision();
		bool hasDynamicCollision();
		double estimateExecutionTime();
		double computeExecutionDistance();
		bool replanForDynamicObstacle();
		nav_msgs::Path getCurrentTraj(double dt);
		nav_msgs::Path getRestGlobalPath();
		void getDynamicObstacles(std::vector<Eigen::Vector3d>& obstaclesPos, std::vector<Eigen::Vector3d>& obstaclesVel, std::vector<Eigen::Vector3d>& obstaclesSize, const Eigen::Vector3d &robotSize = Eigen::Vector3d(0.0, 0.0, 0.0));
		void publishGoal();
		void publishRayCast();
	};
}

#endif