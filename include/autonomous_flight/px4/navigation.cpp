/*
	FILE: navigation.cpp
	------------------------
	navigation implementation file in real world
*/
#include <autonomous_flight/px4/navigation.h>

namespace AutoFlight{
	navigation::navigation(const ros::NodeHandle& nh) : flightBase(nh){
		this->initParam();
		this->initModules();
		this->registerPub();
	}

	void navigation::initParam(){
    	// parameters    
		// Use global Planner
		if (not this->nh_.getParam("autonomous_flight/use_global_planner", this->useGlobalPlanner_)){
			this->useGlobalPlanner_ = false;
			cout << "[AutoFlight]: No use global planner param found. Use default: false." << endl;
		}
		else{
			cout << "[AutoFlight]: Use global planner is set to: " << this->useGlobalPlanner_ << "." << endl;
		}	

		// No turning of yaw
		if (not this->nh_.getParam("autonomous_flight/no_yaw_turning", this->noYawTurning_)){
			this->noYawTurning_ = false;
			cout << "[AutoFlight]: No yaw turning param found. Use default: false." << endl;
		}
		else{
			cout << "[AutoFlight]: Yaw turning use is set to: " << this->noYawTurning_ << "." << endl;
		}	
		
		// full state control (yaw)
		if (not this->nh_.getParam("autonomous_flight/use_yaw_control", this->useYawControl_)){
			this->useYawControl_ = false;
			cout << "[AutoFlight]: No yaw control param found. Use default: false." << endl;
		}
		else{
			cout << "[AutoFlight]: Yaw control use is set to: " << this->useYawControl_ << "." << endl;
		}		

    	// desired linear velocity    	
		if (not this->nh_.getParam("autonomous_flight/desired_velocity", this->desiredVel_)){
			this->desiredVel_ = 1.0;
			cout << "[AutoFlight]: No desired velocity param found. Use default: 1.0 m/s." << endl;
		}
		else{
			cout << "[AutoFlight]: Desired velocity is set to: " << this->desiredVel_ << "m/s." << endl;
		}

		// desired acceleration
		if (not this->nh_.getParam("autonomous_flight/desired_acceleration", this->desiredAcc_)){
			this->desiredAcc_ = 1.0;
			cout << "[AutoFlight]: No desired acceleration param found. Use default: 1.0 m/s^2." << endl;
		}
		else{
			cout << "[AutoFlight]: Desired acceleration is set to: " << this->desiredAcc_ << "m/s^2." << endl;
		}


    	// desired angular velocity    	
		if (not this->nh_.getParam("autonomous_flight/desired_angular_velocity", this->desiredAngularVel_)){
			this->desiredAngularVel_ = 1.0;
			cout << "[AutoFlight]: No desired angular velocity param found. Use default: 0.5 rad/s." << endl;
		}
		else{
			cout << "[AutoFlight]: Desired angular velocity is set to: " << this->desiredAngularVel_ << "rad/s." << endl;
		}	

    	// trajectory data save path   	
		if (not this->nh_.getParam("autonomous_flight/trajectory_info_save_path", this->trajSavePath_)){
			this->trajSavePath_ = "No";
			cout << "[AutoFlight]: No trajectory info save path param found. Use current directory." << endl;
		}
		else{
			cout << "[AutoFlight]: Trajectory info save path is set to: " << this->trajSavePath_ << "." << endl;
		}			
	}

	void navigation::initModules(){
		// initialize map
		this->map_.reset(new mapManager::occMap (this->nh_));

		// initialize rrt planner
		this->rrtPlanner_.reset(new globalPlanner::rrtOccMap<3> (this->nh_));
		this->rrtPlanner_->setMap(this->map_);

		// initialize polynomial trajectory planner
		this->polyTraj_.reset(new trajPlanner::polyTrajOccMap (this->nh_));
		this->polyTraj_->setMap(this->map_);
		this->polyTraj_->updateDesiredVel(this->desiredVel_);
		this->polyTraj_->updateDesiredAcc(this->desiredVel_);

		// initialize piecewise linear trajectory planner
		this->pwlTraj_.reset(new trajPlanner::pwlTraj (this->nh_));

		// initialize bspline trajectory planner
		this->bsplineTraj_.reset(new trajPlanner::bsplineTraj (this->nh_));
		this->bsplineTraj_->setMap(this->map_);
		this->bsplineTraj_->updateMaxVel(this->desiredVel_);
		this->bsplineTraj_->updateMaxAcc(this->desiredAcc_);
	}

	void navigation::registerPub(){
		this->rrtPathPub_ = this->nh_.advertise<nav_msgs::Path>("navigation/rrt_path", 10);
		this->polyTrajPub_ = this->nh_.advertise<nav_msgs::Path>("navigation/poly_traj", 10);
		this->pwlTrajPub_ = this->nh_.advertise<nav_msgs::Path>("navigation/pwl_trajectory", 10);
		this->bsplineTrajPub_ = this->nh_.advertise<nav_msgs::Path>("navigation/bspline_trajectory", 10);
		this->inputTrajPub_ = this->nh_.advertise<nav_msgs::Path>("navigation/input_trajectory", 10);
	}

	void navigation::registerCallback(){
		// planner callback
		this->plannerTimer_ = this->nh_.createTimer(ros::Duration(0.1), &navigation::plannerCB, this);
		
		// collision check callback
		this->replanCheckTimer_ = this->nh_.createTimer(ros::Duration(0.01), &navigation::replanCheckCB, this);

		// trajectory execution callback
		this->trajExeTimer_ = this->nh_.createTimer(ros::Duration(0.01), &navigation::trajExeCB, this);

		// state update callback (velocity and acceleration)
		this->stateUpdateTimer_ = this->nh_.createTimer(ros::Duration(0.033), &navigation::stateUpdateCB, this);

		// visualization callback
		this->visTimer_ = this->nh_.createTimer(ros::Duration(0.033), &navigation::visCB, this);
	}

	void navigation::plannerCB(const ros::TimerEvent&){
		if (not this->firstGoal_) return;

		if (this->replan_){
			// get start and end condition for trajectory generation (the end condition is the final zero condition)
			std::vector<Eigen::Vector3d> startEndCondition;
			this->getStartEndCondition(startEndCondition); 
			nav_msgs::Path inputTraj;
			// bspline trajectory generation
			double finalTime; // final time for bspline trajectory
			double initTs = this->bsplineTraj_->getInitTs();
			if (this->useGlobalPlanner_){
				if (this->needGlobalPlan_){
					this->rrtPlanner_->updateStart(this->odom_.pose.pose);
					this->rrtPlanner_->updateGoal(this->goal_.pose);
					nav_msgs::Path rrtPathMsgTemp;
					this->rrtPlanner_->makePlan(rrtPathMsgTemp);
					if (rrtPathMsgTemp.poses.size() >= 2){
						this->rrtPathMsg_ = rrtPathMsgTemp;
						this->globalPlanReady_ = true;
					}
					this->needGlobalPlan_ = false;
				}
				else{
					if (this->globalPlanReady_){
						// get rest of global plan
						nav_msgs::Path restPath = this->getRestGlobalPath();
						this->polyTraj_->updatePath(restPath, startEndCondition);
						this->polyTraj_->makePlan(this->polyTrajMsg_); // no corridor constraint		
						nav_msgs::Path adjustedInputPolyTraj;
						bool satisfyDistanceCheck = false;
						double dtTemp = initTs;
						double finalTimeTemp;
						ros::Time startTime = ros::Time::now();
						ros::Time currTime;
						while (ros::ok()){
							currTime = ros::Time::now();
							if ((currTime - startTime).toSec() >= 0.05){
								cout << "[AutoFlight]: Exceed path check time. Use the best." << endl;
								break;
							}
							nav_msgs::Path inputPolyTraj = this->polyTraj_->getTrajectory(dtTemp);
							satisfyDistanceCheck = this->bsplineTraj_->inputPathCheck(inputPolyTraj, adjustedInputPolyTraj, dtTemp, finalTimeTemp);
							if (satisfyDistanceCheck) break;
							dtTemp *= 0.8;
						}

						inputTraj = adjustedInputPolyTraj;
						finalTime = finalTimeTemp;
						startEndCondition[1] = this->polyTraj_->getVel(finalTime);
						startEndCondition[3] = this->polyTraj_->getAcc(finalTime);	

					}
					else{
						cout << "[AutoFlight]: Global planner fails. Check goal and map." << endl;
					}		
				}				
			}
			else{
				if (not this->trajectoryReady_){ // use polynomial trajectory as input
					nav_msgs::Path waypoints, polyTrajTemp;
					geometry_msgs::PoseStamped start, goal;
					start.pose = this->odom_.pose.pose; goal = this->goal_;
					waypoints.poses = std::vector<geometry_msgs::PoseStamped> {start, goal};					
					
					this->polyTraj_->updatePath(waypoints, startEndCondition);
					this->polyTraj_->makePlan(false); // no corridor constraint
					
					nav_msgs::Path adjustedInputPolyTraj;
					bool satisfyDistanceCheck = false;
					double dtTemp = initTs;
					double finalTimeTemp;
					ros::Time startTime = ros::Time::now();
					ros::Time currTime;
					while (ros::ok()){
						currTime = ros::Time::now();
						if ((currTime - startTime).toSec() >= 0.05){
							cout << "[AutoFlight]: Exceed path check time. Use the best." << endl;
							break;
						}
						nav_msgs::Path inputPolyTraj = this->polyTraj_->getTrajectory(dtTemp);
						satisfyDistanceCheck = this->bsplineTraj_->inputPathCheck(inputPolyTraj, adjustedInputPolyTraj, dtTemp, finalTimeTemp);
						if (satisfyDistanceCheck) break;
						
						dtTemp *= 0.8;
					}

					inputTraj = adjustedInputPolyTraj;
					finalTime = finalTimeTemp;
					startEndCondition[1] = this->polyTraj_->getVel(finalTime);
					startEndCondition[3] = this->polyTraj_->getAcc(finalTime);
				}
				else{
					Eigen::Vector3d bsplineLastPos = this->trajectory_.at(this->trajectory_.getDuration());
					geometry_msgs::PoseStamped lastPs; lastPs.pose.position.x = bsplineLastPos(0); lastPs.pose.position.y = bsplineLastPos(1); lastPs.pose.position.z = bsplineLastPos(2);
					Eigen::Vector3d goalPos (this->goal_.pose.position.x, this->goal_.pose.position.y, this->goal_.pose.position.z);
					// check the distance between last point and the goal position
					if ((bsplineLastPos - goalPos).norm() >= 0.2){ // use polynomial trajectory to make the rest of the trajectory
						nav_msgs::Path waypoints, polyTrajTemp;
						waypoints.poses = std::vector<geometry_msgs::PoseStamped>{lastPs, this->goal_};
						std::vector<Eigen::Vector3d> polyStartEndCondition;
						Eigen::Vector3d polyStartVel = this->trajectory_.getDerivative().at(this->trajectory_.getDuration());
						Eigen::Vector3d polyEndVel (0.0, 0.0, 0.0);
						Eigen::Vector3d polyStartAcc = this->trajectory_.getDerivative().getDerivative().at(this->trajectory_.getDuration());
						Eigen::Vector3d polyEndAcc (0.0, 0.0, 0.0);
						polyStartEndCondition.push_back(polyStartVel);
						polyStartEndCondition.push_back(polyEndVel);
						polyStartEndCondition.push_back(polyStartAcc);
						polyStartEndCondition.push_back(polyEndAcc);
						this->polyTraj_->updatePath(waypoints, polyStartEndCondition);
						this->polyTraj_->makePlan(false); // no corridor constraint
						
						nav_msgs::Path adjustedInputCombinedTraj;
						bool satisfyDistanceCheck = false;
						double dtTemp = initTs;
						double finalTimeTemp;
						ros::Time startTime = ros::Time::now();
						ros::Time currTime;
						while (ros::ok()){
							currTime = ros::Time::now();
							if ((currTime - startTime).toSec() >= 0.05){
								cout << "[AutoFlight]: Exceed path check time. Use the best." << endl;
								break;
							}							
							nav_msgs::Path inputRestTraj = this->getCurrentTraj(dtTemp);
							nav_msgs::Path inputPolyTraj = this->polyTraj_->getTrajectory(dtTemp);
							nav_msgs::Path inputCombinedTraj;
							inputCombinedTraj.poses = inputRestTraj.poses;
							for (size_t i=1; i<inputPolyTraj.poses.size(); ++i){
								inputCombinedTraj.poses.push_back(inputPolyTraj.poses[i]);
							}
							
							satisfyDistanceCheck = this->bsplineTraj_->inputPathCheck(inputCombinedTraj, adjustedInputCombinedTraj, dtTemp, finalTimeTemp);
							if (satisfyDistanceCheck) break;
							
							dtTemp *= 0.8; // magic number 0.8
						}
						inputTraj = adjustedInputCombinedTraj;
						finalTime = finalTimeTemp - this->trajectory_.getDuration(); // need to subtract prev time since it is combined trajectory
						startEndCondition[1] = this->polyTraj_->getVel(finalTime);
						startEndCondition[3] = this->polyTraj_->getAcc(finalTime);
					}
					else{
						nav_msgs::Path adjustedInputRestTraj;
						bool satisfyDistanceCheck = false;
						double dtTemp = initTs;
						double finalTimeTemp;
						ros::Time startTime = ros::Time::now();
						ros::Time currTime;
						while (ros::ok()){
							currTime = ros::Time::now();
							if ((currTime - startTime).toSec() >= 0.05){
								cout << "[AutoFlight]: Exceed path check time. Use the best." << endl;
								break;
							}
							nav_msgs::Path inputRestTraj = this->getCurrentTraj(dtTemp);
							satisfyDistanceCheck = this->bsplineTraj_->inputPathCheck(inputRestTraj, adjustedInputRestTraj, dtTemp, finalTimeTemp);
							if (satisfyDistanceCheck) break;
							
							dtTemp *= 0.8;
						}
						inputTraj = adjustedInputRestTraj;
					}
				}
			}
			

			this->inputTrajMsg_ = inputTraj;

			bool updateSuccess = this->bsplineTraj_->updatePath(inputTraj, startEndCondition);
			if (updateSuccess){
				nav_msgs::Path bsplineTrajMsgTemp;
				bool planSuccess = this->bsplineTraj_->makePlan(bsplineTrajMsgTemp);
				if (planSuccess){
					this->bsplineTrajMsg_ = bsplineTrajMsgTemp;
					this->trajStartTime_ = ros::Time::now();
					this->trajTime_ = 0.0; // reset trajectory time
					this->trajectory_ = this->bsplineTraj_->getTrajectory();
					this->trajectoryReady_ = true;
					this->replan_ = false;
					cout << "[AutoFlight]: Trajectory generated successfully." << endl;

					if (this->trajSavePath_ != "No" and this->firstTimeSave_){
						this->bsplineTraj_->writeCurrentTrajInfo(this->trajSavePath_, 0.05);
						this->firstTimeSave_ = false;
					}
				}
				else{
					// if the current trajectory is still valid, then just ignore this iteration
					// if the current trajectory/or new goal point is assigned is not valid, then just stop
					if (this->hasCollision()){
						this->trajectoryReady_ = false;
						this->stop();
						cout << "[AutoFlight]: Stop!!! Trajectory generation fails." << endl;
					}
					else{
						if (this->trajectoryReady_){
							cout << "[AutoFlight]: Trajectory fail. Use trajectory from previous iteration." << endl;
						}
						else{
							cout << "[AutoFlight]: Unable to generate a feasible trajectory." << endl;
						}
						this->replan_ = false;
					}
				}
			}
		}
	}

	void navigation::replanCheckCB(const ros::TimerEvent&){
		/*
			Replan if
			1. collision detected
			2. new goal point assigned
			3. fixed distance
		*/
		if (this->goalReceived_){
			this->replan_ = false;
			this->trajectoryReady_ = false;
			if (not this->noYawTurning_ and not this->useYawControl_){
				double yaw = atan2(this->goal_.pose.position.y - this->odom_.pose.pose.position.y, this->goal_.pose.position.x - this->odom_.pose.pose.position.x);
				this->moveToOrientation(yaw, this->desiredAngularVel_);
			}
			this->firstTimeSave_ = true;
			this->replan_ = true;
			this->goalReceived_ = false;
			if (this->useGlobalPlanner_){
				cout << "[AutoFlight]: Start global planning." << endl;
				this->needGlobalPlan_ = true;
				this->globalPlanReady_ = false;
			}

			cout << "[AutoFlight]: Replan for new goal position." << endl; 
			return;
		}


		if (this->trajectoryReady_){
			if (this->hasCollision()){ // if trajectory not ready, do not replan
				this->replan_ = true;
				cout << "[AutoFlight]: Replan for collision." << endl;
				return;
			}


			if (this->computeExecutionDistance() >= 3.0){
				this->replan_ = true;
				cout << "[AutoFlight]: Regular replan." << endl;
				return;
			}
		}
	}

	void navigation::trajExeCB(const ros::TimerEvent&){
		if (this->trajectoryReady_){
			ros::Time currTime = ros::Time::now();
			double trajTime = (currTime - this->trajStartTime_).toSec();
			this->trajTime_ = this->bsplineTraj_->getLinearReparamTime(trajTime);
			double linearReparamFactor = this->bsplineTraj_->getLinearFactor();
			Eigen::Vector3d pos = this->trajectory_.at(this->trajTime_);
			Eigen::Vector3d vel = this->trajectory_.getDerivative().at(this->trajTime_) * linearReparamFactor;
			Eigen::Vector3d acc = this->trajectory_.getDerivative().getDerivative().at(this->trajTime_) * pow(linearReparamFactor, 2);

			
			tracking_controller::Target target;
			if (this->noYawTurning_ or not this->useYawControl_){
				target.yaw = AutoFlight::rpy_from_quaternion(this->odom_.pose.pose.orientation);
			}
			else{
				target.yaw = atan2(vel(1), vel(0));
				// cout << "current vel: " << vel.transpose() << endl;
			}
			if (std::abs(this->trajTime_ - this->trajectory_.getDuration()) <= 0.3 or this->trajTime_ > this->trajectory_.getDuration()){ // zero vel and zero acc if close to
				vel *= 0;
				acc *= 0;
				target.yaw = AutoFlight::rpy_from_quaternion(this->odom_.pose.pose.orientation);
			}			
			target.position.x = pos(0);
			target.position.y = pos(1);
			target.position.z = pos(2);
			target.velocity.x = vel(0);
			target.velocity.y = vel(1);
			target.velocity.z = vel(2);
			target.acceleration.x = acc(0);
			target.acceleration.y = acc(1);
			target.acceleration.z = acc(2);
			this->updateTargetWithState(target);			
		}
	}

	void navigation::stateUpdateCB(const ros::TimerEvent&){
		Eigen::Vector3d currVelBody (this->odom_.twist.twist.linear.x, this->odom_.twist.twist.linear.y, this->odom_.twist.twist.linear.z);
		Eigen::Vector4d orientationQuat (this->odom_.pose.pose.orientation.w, this->odom_.pose.pose.orientation.x, this->odom_.pose.pose.orientation.y, this->odom_.pose.pose.orientation.z);
		Eigen::Matrix3d orientationRot = AutoFlight::quat2RotMatrix(orientationQuat);
		this->currVel_ = orientationRot * currVelBody;	
		ros::Time currTime = ros::Time::now();	
		if (this->stateUpdateFirstTime_){
			this->currAcc_ = Eigen::Vector3d (0.0, 0.0, 0.0);
			this->prevStateTime_ = currTime;
			this->stateUpdateFirstTime_ = false;
		}
		else{
			double dt = (currTime - this->prevStateTime_).toSec();
			this->currAcc_ = (this->currVel_ - this->prevVel_)/dt;
			this->prevVel_ = this->currVel_; 
			this->prevStateTime_ = currTime;
		}
	}

	void navigation::visCB(const ros::TimerEvent&){
		if (this->rrtPathMsg_.poses.size() != 0){
			this->rrtPathPub_.publish(this->rrtPathMsg_);
		}
		if (this->polyTrajMsg_.poses.size() != 0){
			this->polyTrajPub_.publish(this->polyTrajMsg_);
		}
		if (this->pwlTrajMsg_.poses.size() != 0){
			this->pwlTrajPub_.publish(this->pwlTrajMsg_);
		}
		if (this->bsplineTrajMsg_.poses.size() != 0){
			this->bsplineTrajPub_.publish(this->bsplineTrajMsg_);
		}
		if (this->inputTrajMsg_.poses.size() != 0){
			this->inputTrajPub_.publish(this->inputTrajMsg_);
		}
	}

	void navigation::run(){
		// take off the drone
		this->takeoff();

		// register timer callback
		this->registerCallback();
	}


	void navigation::getStartEndCondition(std::vector<Eigen::Vector3d>& startEndCondition){
		/*	
			1. start velocity
			2. start acceleration (set to zero)
			3. end velocity
			4. end acceleration (set to zero) 
		*/

		Eigen::Vector3d currVel = this->currVel_;
		Eigen::Vector3d currAcc = this->currAcc_;
		Eigen::Vector3d endVel (0.0, 0.0, 0.0);
		Eigen::Vector3d endAcc (0.0, 0.0, 0.0);

		// if (not this->trajectoryReady_){
		// 	double yaw = AutoFlight::rpy_from_quaternion(this->odom_.pose.pose.orientation);
		// 	Eigen::Vector3d direction (cos(yaw), sin(yaw), 0.0);
		// 	currVel = this->desiredVel_ * direction;
		// 	currAcc = this->desiredAcc_ * direction;
		// }
		startEndCondition.push_back(currVel);
		startEndCondition.push_back(endVel);
		startEndCondition.push_back(currAcc);
		startEndCondition.push_back(endAcc);
	}

	bool navigation::hasCollision(){
		if (this->trajectoryReady_){
			for (double t=this->trajTime_; t<=this->trajectory_.getDuration(); t+=0.1){
				Eigen::Vector3d p = this->trajectory_.at(t);
				bool hasCollision = this->map_->isInflatedOccupied(p);
				if (hasCollision){
					return true;
				}
			}
		}
		return false;
	}

	double navigation::computeExecutionDistance(){
		if (this->trajectoryReady_ and not this->replan_){
			Eigen::Vector3d prevP, currP;
			bool firstTime = true;
			double totalDistance = 0.0;
			for (double t=0.0; t<=this->trajTime_; t+=0.1){
				currP = this->trajectory_.at(t);
				if (firstTime){
					firstTime = false;
				}
				else{
					totalDistance += (currP - prevP).norm();
				}
				prevP = currP;
			}
			return totalDistance;
		}
		return -1.0;
	}

	nav_msgs::Path navigation::getCurrentTraj(double dt){
		nav_msgs::Path currentTraj;
		currentTraj.header.frame_id = "map";
		currentTraj.header.stamp = ros::Time::now();
	
		if (this->trajectoryReady_){
			// include the current pose
			// geometry_msgs::PoseStamped psCurr;
			// psCurr.pose = this->odom_.pose.pose;
			// currentTraj.poses.push_back(psCurr);
			for (double t=this->trajTime_; t<=this->trajectory_.getDuration(); t+=dt){
				Eigen::Vector3d pos = this->trajectory_.at(t);
				geometry_msgs::PoseStamped ps;
				ps.pose.position.x = pos(0);
				ps.pose.position.y = pos(1);
				ps.pose.position.z = pos(2);
				currentTraj.poses.push_back(ps);
			}		
		}
		return currentTraj;
	}

	nav_msgs::Path navigation::getRestGlobalPath(){
		nav_msgs::Path currPath;

		int nextIdx = this->rrtPathMsg_.poses.size()-1;
		Eigen::Vector3d pCurr (this->odom_.pose.pose.position.x, this->odom_.pose.pose.position.y, this->odom_.pose.pose.position.z);
		double minDist = std::numeric_limits<double>::infinity();
		for (size_t i=0; i<this->rrtPathMsg_.poses.size()-1; ++i){
			geometry_msgs::PoseStamped ps = this->rrtPathMsg_.poses[i];
			Eigen::Vector3d pEig (ps.pose.position.x, ps.pose.position.y, ps.pose.position.z);
			Eigen::Vector3d pDiff = pCurr - pEig;

			geometry_msgs::PoseStamped psNext = this->rrtPathMsg_.poses[i+1];
			Eigen::Vector3d pEigNext (psNext.pose.position.x, psNext.pose.position.y, psNext.pose.position.z);
			Eigen::Vector3d diffToNext = pEigNext - pEig;
			double dist = (pEig - pCurr).norm();
			if (trajPlanner::angleBetweenVectors(diffToNext, pDiff) > PI_const/2){
				if (dist < minDist){
					nextIdx = i;
					minDist = dist;
				}
			}
		}


		geometry_msgs::PoseStamped psCurr;
		psCurr.pose = this->odom_.pose.pose;
		currPath.poses.push_back(psCurr);
		for (size_t i=nextIdx; i<this->rrtPathMsg_.poses.size(); ++i){
			currPath.poses.push_back(this->rrtPathMsg_.poses[i]);
		}
		return currPath;		
	}
}