// info:
// - tau_J_d è zero se il robot è bloccato ed è la coppia mandata al franka senza la gravità

#include <pluginlib/class_list_macros.h>
#include <panda_controllers/CT_mod_OS.h> //library of the computed torque 
#include <ros/package.h>
#include <signal.h>

using namespace std;
namespace panda_controllers{

	bool CTModOS::init(hardware_interface::RobotHW* robot_hw, ros::NodeHandle& node_handle)
	{
		this->cvc_nh = node_handle; 
		
		// Initialize Ctrl-C
		// signal(SIGINT, this->signal_callback_handler);

		std::string arm_id; //checking up the arm id of the robot     
		if (!node_handle.getParam("arm_id", arm_id)) {
			ROS_ERROR("Computed Torque: Could not get parameter arm_id!");
			return false;
		}

		/* Inizializing the Kp and Kv gains */
		double kp1, kp2, kp3, kp4, kp5, kp6, kv1, kv2, kv3, kv4, kv5, kv6, kn1, kn2, kn3;
		update_param_flag = false;

		/* Chek paramaters acquisition*/
		if (!node_handle.getParam("kp1", kp1) || 
			!node_handle.getParam("kp2", kp2) ||
			!node_handle.getParam("kp3", kp3) || 
			!node_handle.getParam("kp4", kp4) ||
			!node_handle.getParam("kp5", kp5) ||
			!node_handle.getParam("kp6", kp6) ||
			!node_handle.getParam("kv1", kv1) || 
			!node_handle.getParam("kv2", kv2) ||
			!node_handle.getParam("kv3", kv3) || 
			!node_handle.getParam("kv4", kv4) ||
			!node_handle.getParam("kv5", kv5) ||
			!node_handle.getParam("kv6", kv6)) {
			ROS_ERROR("Computed Torque: Could not get parameter kpi or kvi");
			return false;
		}

		/* Assign Kp values*/
		Kp = Eigen::MatrixXd::Identity(DOF, DOF);
		Kp(0,0) = kp1; 
		Kp(1,1) = kp2; 
		Kp(2,2) = kp3; 
		Kp(3,3) = kp4; 
		Kp(4,4) = kp5; 
		Kp(5,5) = kp6; 

		/* Assign Kv values*/
		Kv = Eigen::MatrixXd::Identity(DOF, DOF);
		Kv(0,0) = kv1; 
		Kv(1,1) = kv2; 
		Kv(2,2) = kv3; 
		Kv(3,3) = kv4; 
		Kv(4,4) = kv5; 
		Kv(5,5) = kv6;

		if (!node_handle.getParam("kn1", kn1) || 
		!node_handle.getParam("kn2", kn2) ||
		!node_handle.getParam("kn3", kn3)) {
			ROS_ERROR("Computed Torque: Could not get parameter kpi or kv!");
			return false;
		}
		
		/* Assign Kn values*/
		Kn = Eigen::MatrixXd::Identity(NJ, NJ);
		Kn(0,0) = kn1; 
		Kn(1,1) = kn1; 
		Kn(2,2) = kn1; 
		Kn(3,3) = kn1; 
		Kn(4,4) = kn2; 
		Kn(5,5) = kn2;
		Kn(6,6) = kn3; 

		/* Assigning the time */
		if (!node_handle.getParam("dt", dt)) {
			ROS_ERROR("Computed Torque: Could not get parameter dt!");
			return false;
		}

		/* Chek joint names acquisition*/
		std::vector<std::string> joint_names;
		if (!node_handle.getParam("joint_names", joint_names) || joint_names.size() != 7) {
			ROS_ERROR("Computed Torque: Error in parsing joints name!");
			return false;
		}

		/* check model_interface error */
		franka_hw::FrankaModelInterface* model_interface = robot_hw->get<franka_hw::FrankaModelInterface>();
		if (model_interface == nullptr) {
			ROS_ERROR_STREAM("Computed Torque: Error getting model interface from hardware!");
			return false;
		}   
		/* Chek model_handle error acquisition*/
		try {
			model_handle_.reset(new franka_hw::FrankaModelHandle(model_interface->getHandle(arm_id + "_model")));
		} catch (hardware_interface::HardwareInterfaceException& ex) {
			ROS_ERROR_STREAM("Computed Torque: Exception getting model handle from interface: " << ex.what());
			return false;
		}
		
		/* Same check */
		franka_hw::FrankaStateInterface* state_interface = robot_hw->get<franka_hw::FrankaStateInterface>();
		if (state_interface == nullptr) {
			ROS_ERROR_STREAM("Computed Torque: Error getting state interface from hardware");
			return false;
		}
		try {
			state_handle_.reset(new franka_hw::FrankaStateHandle(state_interface->getHandle(arm_id + "_robot")));
		} catch (hardware_interface::HardwareInterfaceException& ex) {
			ROS_ERROR_STREAM("Computed Torque: Exception getting state handle from interface: " << ex.what());
			return false;
		}

		
		hardware_interface::EffortJointInterface* effort_joint_interface = robot_hw->get<hardware_interface::EffortJointInterface>();
		if (effort_joint_interface == nullptr) {
			ROS_ERROR_STREAM("Computed Torque: Error getting effort joint interface from hardware!");
			return false;
		}

		for (size_t i = 0; i < 7; ++i) {
			try {
				joint_handles_.push_back(effort_joint_interface->getHandle(joint_names[i]));

			} catch (const hardware_interface::HardwareInterfaceException& ex) {
				ROS_ERROR_STREAM("Computed Torque: Exception getting joint handles: " << ex.what());
				return false;
			}
		}
	 
		//  /* Initial parameters acquisition */
		// for(int i=0; i<NJ; i++){
		// 	double mass, cmx, cmy, cmz, xx, xy, xz, yy, yz, zz, d1, d2;
		// 	if (!node_handle.getParam("link"+std::to_string(i+1)+"/mass", mass) ||
		// 		!node_handle.getParam("link"+std::to_string(i+1)+"/m_CoM_x", cmx) ||
		// 		!node_handle.getParam("link"+std::to_string(i+1)+"/m_CoM_y", cmy) ||
		// 		!node_handle.getParam("link"+std::to_string(i+1)+"/m_CoM_z", cmz) ||
		// 		!node_handle.getParam("link"+std::to_string(i+1)+"/Ixx", xx) ||
		// 		!node_handle.getParam("link"+std::to_string(i+1)+"/Ixy", xy) ||
		// 		!node_handle.getParam("link"+std::to_string(i+1)+"/Ixz", xz) ||
		// 		!node_handle.getParam("link"+std::to_string(i+1)+"/Iyy", yy) ||
		// 		!node_handle.getParam("link"+std::to_string(i+1)+"/Iyz", yz) ||
		// 		!node_handle.getParam("link"+std::to_string(i+1)+"/Izz", zz) ||
		// 		!node_handle.getParam("link"+std::to_string(i+1)+"/d1", d1)||
		// 		!node_handle.getParam("link"+std::to_string(i+1)+"/d2", d2)){
		// 		ROS_ERROR("Computed_torque: Error in parsing inertial parameters!");
		// 		return 1;
		// 	}
		// 	param.segment(PARAM*i, PARAM) << mass,cmx,cmy,cmz,xx,xy,xz,yy,yz,zz;
		// 	param_frict.segment((FRICTION)*i, FRICTION) << d1, d2;
		// }
		// // param_real << 0.73552200000000001, 0.0077354848739999999, -0.0031274395439999996, -0.033394905365999997, 0.01645526761273585, -0.00039510871831575201, -0.00084478578026577801, 0.011624582982752355, -0.00088299513761623202, 0.0049096519673609458;

		// /* Param_dyn initial calculation*/
		// frankaRobot.set_par_REG(param);

		/* Inizializing the R gains to update parameters*/
		std::vector<double> gainRlinks(NJ), gainRparam(3), gainKd(7);

		// - thunder init - //
		// get absolute path to franka_conf.yaml file
		std::string package_path = ros::package::getPath("panda_controllers");
		std::string path_conf = package_path + "/config/thunder/franka.yaml";
		std::string path_par_REG = package_path + "/config/thunder/franka_par_REG.yaml";
		frankaRobot.load_conf(path_conf);
		frankaRobot.load_par_REG(path_par_REG);
		param = frankaRobot.get_par_REG();
		param_frict = frankaRobot.get_par_Dl();
		// cout << "param_frict: " << param_frict;
		Eigen::Matrix<double,PARAM,PARAM> Rlink;
		Eigen::Matrix<double,FRICTION,FRICTION> Rlink_fric;
		if (!node_handle.getParam("gainRlinks", gainRlinks) ||
			!node_handle.getParam("gainRparam", gainRparam) ||
			!node_handle.getParam("gainKd", gainKd)  ||
			!node_handle.getParam("update_param", update_param_flag)) {
	
			ROS_ERROR("Computed_torque Could not get gain parameter for R, Kd!");
			return false;
		}
		Kd.setIdentity();
		for(int i=0;i<NJ;i++){
			Kd(i,i) = gainKd[i];
		}
		/* setting R weight */
		Rlink.setZero();
		Rlink(0,0) = gainRparam[0];
		Rlink(1,1) = gainRparam[1];
		Rlink(2,2) = Rlink(1,1);
		Rlink(3,3) = Rlink(1,1);
		Rlink(4,4) = gainRparam[2];
		Rlink(5,5) = gainRparam[2];
		Rlink(6,6) = Rlink(5,5);
		Rlink(7,7) = Rlink(4,4);
		Rlink(8,8) = Rlink(5,5);
		Rlink(9,9) = Rlink(4,4);

		Rlink_fric.setZero();
		Rlink_fric(0,0) = gainRparam[3];
		Rlink_fric(1,1) = gainRparam[4];

		/* Inverse matrix R calculation */
		Rinv.setZero();
		Rinv_fric.setZero();
		// Rinv_tot.setZero();
		for (int i = 0; i<NJ; i++){	
			Rinv.block(i*PARAM, i*PARAM, PARAM, PARAM) = gainRlinks[i]*Rlink; // block permette di fare le operazioni blocco per blocco
			Rinv_fric.block(i*(FRICTION), i*(FRICTION), FRICTION, FRICTION) = Rlink_fric; 
		}
		// Rinv_tot.block(0,0,NJ*PARAM,NJ*PARAM) = Rinv;
		// Rinv_tot.block(NJ*PARAM,NJ*PARAM, NJ*FRICTION, NJ*FRICTION) = Rinv_fric;

		/* Initialize joint (torque,velocity) limits */
		tau_limit << 87, 87, 87, 87, 12, 12, 12;
		
		/*Start command subscriber and publisher */
		this->sub_command_ = node_handle.subscribe<panda_controllers::desTrajEE> ("/controller/command_cartesian", 1, &CTModOS::setCommandCB, this);   //it verify with the callback(setCommandCB) that the command joint has been received
		this->sub_flag_update_ = node_handle.subscribe<panda_controllers::flag> ("/controller/adaptiveFlag", 1, &CTModOS::setFlagUpdate, this); // Set adaptive_flag to true  
		// this->sub_command_j_ = node_handle.subscribe<sensor_msgs::JointState> ("/controller/command_joints", 1, &CTModOS::setCommandCBJ, this);
		// this->sub_flag_opt_ = node_handle.subscribe<panda_controllers::flag>("/controller/optFlag", 1, &CTModOS::setFlagJoints, this);
		// this->sub_joints =  node_handle.subscribe<sensor_msgs::JointState>("/franka_state_controller/joint_states", 1, &CTModOS::jointsCallbackT, this);
		this->sub_impedance_gains_ = node_handle.subscribe<panda_controllers::impedanceGain>("/controller/impedanceGains", 1, &CTModOS::setGains, this);
		this->sub_command_rpy_ = node_handle.subscribe<panda_controllers::rpy>("/controller/command_rpy", 1, &CTModOS::setRPYcmd, this);
		this->sub_flag_resetAdp = node_handle.subscribe<panda_controllers::flag>("/controller/resetFlag", 1,&CTModOS::setResetFlag, this);
		/*Topic for Fest*/
		this->sub_Fext_ = node_handle.subscribe<geometry_msgs::WrenchStamped>("/franka_state_controller/F_ext", 1, &CTModOS::callbackFext, this);

		this->pub_err_ = node_handle.advertise<panda_controllers::log_adaptive_cartesian> ("/controller/logging", 1); //Public error variables and tau
		this->pub_config_ = node_handle.advertise<panda_controllers::point>("/controller/current_config", 1); //Public Xi,dot_XI,ddot_XI
		
		/*Initialize Stack Procedure*/
		// l = 0;
		// count = 0;
		// epsilon = 0.1;
		// update_opt_flag = false;
		reset_adp_flag = false;
		// lambda_min = 0;
	   
		// S.setZero(10);
		// H.setZero(10,70);
		// E.setZero(70); 
		/* Friction case stack*/
		// H.setZero(14,98);
		// E.setZero(98); 

        // Y_stack_sum.setZero();
        // redY_stack_sum.setZero();
        // H_vec.resize(700);

		ros::Rate(10).sleep();

		return true;
	}

   
	void CTModOS::starting(const ros::Time& time){
	
		/* Getting Robot State in order to get q_curr and dot_q_curr */
		robot_state = state_handle_->getRobotState();
		T0EE = Eigen::Matrix4d::Map(robot_state.O_T_EE.data());

		/* Mapping actual joints position, actual joints velocity, Mass matrix and Coriolis vector onto Matrix form  */
		q_curr = Eigen::Map<Eigen::Matrix<double, NJ, 1>>(robot_state.q.data());
		dot_q_curr = Eigen::Map<Eigen::Matrix<double, NJ, 1>>(robot_state.dq.data());
		dot_q_curr_old = dot_q_curr;
		ddot_q_curr.setZero();

		dot_error_q.setZero(); 
		dot_error_Nq0.setZero();

		/* Secure Initialization  */
	   	ee_pos_cmd = T0EE.translation();
		ee_rot_cmd = T0EE.linear();
		// ROS_INFO_STREAM(ee_rot_cmd.eulerAngles(0,1,2));
		ee_vel_cmd.setZero();
		ee_acc_cmd.setZero();
		ee_ang_vel_cmd.setZero();
		ee_ang_acc_cmd.setZero();
		
		/* Compute error */
		error.setZero();
		dot_error.setZero();

		/* Compute reference (Position Control) */
		qr = q_curr; 
		dot_qr.setZero();
		ddot_qr.setZero();

		// ddq_opt.setZero();
		// dq_opt.setZero();
		// q_opt.setZero();

		// Vector center of limit's joints
		// q_c << 0.0, 0.0, 0.0, -1.5708, 0.0, 1.8675, 0.0;

		/*Limiti del franka*/
		q_max_limit << 2.8973, 1.7628, 2.8973, -0.0698, 2.8973, 3.7525, 2.8973;
		q_min_limit << -2.8973, -1.7628, -2.8973, -3.0718, -2.8973, -0.0175, -2.8973;
		q_c << (q_min_limit + q_max_limit)/2;
		// std::cout << "q_c: " << q_c << std::endl;
		dq_limit << 2.1, 2.1, 2.1, 2.1, 2.6, 2.6, 2.6;
		// ddq_limit << 15, 7.5, 10, 12.5, 15, 20, 20;
		ddq_limit << 1.0, 1.0, 1.0, 1.0, 1.3, 1.3, 1.3;
		
		/* Update regressor */
		dot_param.setZero();
		dot_param_frict.setZero();

		// frankaRobot.setInertialParams(param_dyn); // To compute Extimate Matrix M,G,C
		// frankaRobot.set_par_REG(param); 
		frankaRobot.setArguments(q_curr, dot_q_curr, dot_q_curr, ddot_q_curr); // To compute jacobian and regressor

		// G = Eigen::Map<Eigen::Matrix<double, NJ, 1>> (model_handle_->getGravity().data());
		// Gest = frankaRobot.get_G(); // Estimate Gravity Matrix
		// cout<<"erroregravità: "<<Gest-G<<endl;

	}


	void CTModOS::update(const ros::Time&, const ros::Duration& period){
		Eigen::Matrix<double,DOF,DOF> tmp_conversion0, tmp_conversion1, tmp_conversion2;
		Eigen::VectorXd ee_vel_cmd_tot(DOF), ee_acc_cmd_tot(DOF);
		Eigen::VectorXd tmp_position(DOF), tmp_velocity(DOF);

		/*Dynamics matrix compute by libfranka based on true model of envaroment*/
		robot_state = state_handle_->getRobotState();
		M = Eigen::Map<Eigen::Matrix<double, NJ, NJ>> (model_handle_->getMass().data());
		C = Eigen::Map<Eigen::Matrix<double, NJ, 1>> (model_handle_->getCoriolis().data());
		G = Eigen::Map<Eigen::Matrix<double, NJ, 1>> (model_handle_->getGravity().data());
		T0EE = Eigen::Matrix4d::Map(robot_state.O_T_EE.data());

		/* Actual position, velocity and acceleration of the joints */
		q_curr = Eigen::Map<Eigen::Matrix<double, NJ, 1>>(robot_state.q.data());
		dot_q_curr = Eigen::Map<Eigen::Matrix<double, NJ, 1>>(robot_state.dq.data());

		/* Application of FIR to velocity and acceleration(velocity and torque filter no needed for true robot)*/
		addValue(buffer_dq, dot_q_curr, WIN_LEN_ACC);
		dq_est = obtainMean(buffer_dq);
		// dot_q_curr = dq_est;
		ddot_q_curr = (dq_est - dot_q_curr_old) / dt;

		// addValue(buffer_ddq, ddot_q_curr, WIN_LEN_ACC);
		// ddot_q_curr = obtainMean(buffer_ddq);

		dot_q_curr_old = dq_est; 
		ddot_q_curr_old = ddot_q_curr;
	
		/*Test computeT0EE fun*/
		// ROS_INFO_STREAM(T0EE.translation()-computeT0EE(q_curr).translation());
		// ROS_INFO_STREAM(T0EE.linear()-computeT0EE(q_curr).linear());

		 /* Update regressor */
        // frankaRobot.setInertialParam(param_dyn); // setta i parametri dinamici dell'oggetto frankaRobot e calcola una stima del regressore di M,C e G (che può differire da quella riportata dal franka)
        // frankaRobot.set_par_REG(param);
        // frankaRobot.setArguments(q_curr, dot_q_curr, dot_q_curr, ddot_qr); // setta i valori delle variabili di giunto di interresse e calcola il regressore Y attuale (oltre a calcolare jacobiani e simili e in maniera ridondante M,C,G)
		// auto M = frankaRobot.get_M();
		// auto C = frankaRobot.get_C();
		// auto G = frankaRobot.get_G();
		// auto Y = frankaRobot.get_Yr();
	    // std::array<double, 49> mass_array = model_handle_->getMass();
	    // std::array<double, 7> coriolis_array = model_handle_->getCoriolis();
    	// Eigen::MatrixXd M_franka = Eigen::Map<Eigen::Matrix<double, 7, 7>>(mass_array.data());
	    // Eigen::MatrixXd C_franka = Eigen::Map<Eigen::Matrix<double, 7, 1>>(coriolis_array.data());
	    // Eigen::MatrixXd G_franka = Eigen::Map<Eigen::Matrix<double, 7, 1>> (model_handle_->getGravity().data());
		// auto err_model = Y*param - (M*ddot_qr + C*dot_q_curr + G);
		// auto err_franka = Y*param - (M_franka*ddot_qr + C_franka + G_franka);
		// std::cout << "model error: " << err_model.transpose() << std::endl<<std::endl;
		// std::cout << "franka error: " << err_franka.transpose() << std::endl<<std::endl;
		// std::cout << "Y*param: " << Y*param << std::endl<<std::endl;
		// std::cout << "dot_qr: " << dot_qr << std::endl<<std::endl;
		// std::cout << "ddot_qr: " << ddot_qr << std::endl<<std::endl;
		// std::cout << "M_franka: " << M_franka << std::endl<<std::endl;
		// std::cout << "C_franka: " << C_franka << std::endl<<std::endl;
		// std::cout << "G_franka: " << G_franka << std::endl<<std::endl;

		/* tau_J_d is the desired link-side joint torque sensor signals "without gravity" */
		tau_J_d = Eigen::Map<Eigen::Matrix<double, NJ, 1>>(robot_state.tau_J_d.data());
		Eigen::VectorXd tau_J_curr = Eigen::Map<Eigen::Matrix<double, NJ, 1>>(robot_state.tau_J.data()); // best in simulation
		tau_J = tau_J_d+G;
		// tau_J = tau_cmd;
		// tau_J = Eigen::Map<Eigen::Matrix<double, NJ, 1>>(robot_state.tau_J.data()); // best in simulation
		F_cont = F_ext; // contact force
		// addValue(buffer_tau, tau_J,WIN_LEN);
		// tau_J = obtainMean(buffer_tau);
	
		/* Update pseudo-inverse of J and its derivative */
		frankaRobot.setArguments(q_curr, dot_q_curr, dot_q_curr, ddot_q_curr);

		/* Compute pseudo-inverse of J and its derivative */ 
		// J = Eigen::Map<Eigen::Matrix<double, DOF, NJ>>(model_handle_->getZeroJacobian(franka::Frame::kEndEffector).data());
		J = frankaRobot.get_J_ee();
		// ROS_INFO_STREAM(J -frankaRobot.getJac()); // Test thunderpanda for kinematics matrix
		Eigen::MatrixXd J_dot = frankaRobot.get_J_ee_dot();
		Eigen::MatrixXd J_pinv = frankaRobot.get_J_ee_pinv();
		// J_pinv = J.transpose()*((J*J.transpose()).inverse()); // Right pseudo-inverse of J
		//J_T_pinv = J_pinv.transpose(); // Left pseudo-inverse of J'
		J_T_pinv = ((J*J.transpose()).inverse())*J; // Left pseudo-inverse of J'
		Eigen::MatrixXd J_dot_pinv = - J_pinv * J_dot * J_pinv;

		/* NullSpace Calculation*/
		N1 = (I7.setIdentity() - J_pinv*J);
		// N1 = (I7.setIdentity() - J.transpose()*J_pinv.transpose());
				
		/* Compute error translation */
		ee_position = T0EE.translation();
		ee_velocity = J.topRows(3)*dot_q_curr;

		// if (update_opt_flag == false){
		error.head(3) = ee_pos_cmd - ee_position;
		dot_error.head(3) = ee_vel_cmd - ee_velocity;
		  
		// } else{
		// 	qr = q_opt;
		// 	dot_qr = dq_opt;
		// 	ddot_qr = ddq_opt;
			
		// 	/*Switch to joints control*/
		// 	error_q = qr - q_curr;
		// 	// dot_error_q = dot_qr - dot_q_curr;

		// 	error.head(3) = computeT0EE(qr).translation() - ee_position;
		// 	dot_error.head(3) = J.topRows(3)*dot_qr - ee_velocity;
		// 	ee_rot_cmd = computeT0EE(qr).linear();
		// 	ee_ang_vel_cmd = J.bottomRows(3)*dot_qr;
		// }
	
		/* Compute error orientation */
  		ee_rot = T0EE.linear();
		ee_omega = J.bottomRows(3)*dot_q_curr;

		/* Axis-Angle parametrization*/
		Rs_tilde = ee_rot_cmd*ee_rot.transpose();
		Eigen::Matrix<double,3,3> L_tmp, L_dot_tmp;
		L_tmp = createL(ee_rot_cmd, ee_rot); 
		L_dot_tmp = createDotL(ee_rot_cmd, ee_rot, ee_ang_vel_cmd, ee_omega);
		L.setIdentity();
		L.block(3, 3, 3, 3) = L_tmp;
		L_dot.setZero();
		L_dot.block(3, 3, 3, 3) = L_dot_tmp;
		
		/*Error orientation*/
		error.tail(3) = vect(Rs_tilde);
		dot_error.tail(3) = L_tmp.transpose()*ee_ang_vel_cmd-L_tmp*ee_omega;
		
		/* Inverse-Kinematics */
		ee_vel_cmd_tot << ee_vel_cmd, L_tmp.transpose()*ee_ang_vel_cmd;
		ee_acc_cmd_tot << ee_acc_cmd, L_dot_tmp.transpose()*ee_ang_vel_cmd + L_tmp.transpose()*ee_ang_acc_cmd;
		
		tmp_conversion0.setIdentity();
		tmp_conversion0.block(3, 3, 3, 3) = L_tmp;
		tmp_conversion1.setIdentity();
		tmp_conversion1.block(3, 3, 3, 3) = L_tmp.inverse();
		tmp_conversion2.setZero();
		tmp_conversion2.block(3, 3, 3, 3) = -L_tmp.inverse() * L_dot_tmp *L_tmp.inverse();

		dot_qr = J_pinv*tmp_conversion1*ee_vel_cmd_tot; 
		ddot_qr = J_pinv*tmp_conversion1*ee_acc_cmd_tot + J_pinv*tmp_conversion2*ee_vel_cmd_tot +J_dot_pinv*tmp_conversion1*ee_vel_cmd_tot;

		/* Error definition in Null Space*/
		dot_error_q = dot_qr - dot_q_curr;
		error_Nq0 = q_c - q_curr;
		// dot_error_Nq0 = N1*(error_Nq0-dot_q_curr);

		/* Update and Compute Regressor */
		frankaRobot.setArguments(q_curr, dot_q_curr, dot_qr, ddot_qr);
		Y_mod = frankaRobot.get_Yr();
		frankaRobot.setArguments(q_curr, dot_q_curr, dot_q_curr, ddot_q_curr);
		Y_norm = frankaRobot.get_Yr();

		err_param = Y_norm*param;
		// addValue(buffer_tau_d, err_param,WIN_LEN);
		// err_param = obtainMean(buffer_tau_d);

		// tau_est = Y_norm.block(0,0,NJ,(NJ-1)*PARAM)*param.segment(0,(NJ-1)*PARAM);
		// addValue(buffer_q, tau_est, WIN_LEN);
		// tau_est = obtainMean(buffer_q);

		// ROS_INFO_STREAM(tau_J - Y_norm*param);

		/* Friction matrix online creation*/
		Dest.setZero();
		for(int i = 0; i < 7; ++i){
			Dest(i,i) = param_frict(FRICTION*i,0)*dot_q_curr(i) + param_frict((FRICTION*i)+1,0)*deltaCompute(dot_q_curr(i));
		}

		/*Friction Regressor*/       
		Y_D.setZero();
		Y_D_norm.setZero();
		for (int i = 0; i < 7; ++i) {
			Y_D(i, i * 2) = dot_qr(i); 
			Y_D(i, i * 2 + 1) = deltaCompute(dot_q_curr(i)); 
			Y_D_norm(i, i * 2) = dot_q_curr(i); 
			Y_D_norm(i, i * 2 + 1) = deltaCompute(dot_q_curr(i));   
		}

		// Y_mod_tot.block(0,0,NJ,NJ*PARAM) = Y_norm;
		// Y_mod_tot.block(0,NJ*PARAM,NJ,NJ*FRICTION) = Y_D;

		/*Current parameters link 7*/
		// param7 = param.segment((NJ-1)*PARAM, PARAM);
		
		/* Update parameters law*/
		// err_param = tau_cmd - Y_norm*param - Y_D_norm*param_frict;
		err_param = tau_J - Y_norm*param - Y_D_norm*param_frict;
		if (update_param_flag){
			dot_param = 0.01*Rinv*(Y_mod.transpose()*dot_error_q + 0.3*Y_norm.transpose()*(err_param));
			param = param + dt*dot_param; 
			dot_param_frict = Rinv_fric*(Y_D.transpose()*dot_error_q + 0.3*Y_D_norm.transpose()*(err_param));
			param_frict = param_frict + dt*dot_param_frict;
		}

		/*Reshape parameters vector*/
		for(int i = 0; i < 7; ++i){
			param_tot.segment(i*(PARAM+FRICTION),PARAM) = param.segment(i*(PARAM),PARAM);
			param_tot.segment(i*(PARAM+FRICTION) + PARAM,FRICTION) = param_frict.segment(i*FRICTION,FRICTION);
		}

		/* update dynamic for control law(no filter action) */
		frankaRobot.setArguments(q_curr, dot_q_curr_old, dot_q_curr_old, ddot_q_curr_old);
		frankaRobot.set_par_REG(param); 

		Mest = frankaRobot.get_M(); // Estimate Mass Matrix
		Cest = frankaRobot.get_C(); // Estimate Coriollis Matrix
		Gest = frankaRobot.get_G(); // Estimate Gravity Matrix
			   
		/*Matrici nello spazio operativo*/
		// MestXi = J_T_pinv*Mest*J_pinv;
		// CestXi = (J_T_pinv*Cest - MestXi*J_dot)*J_pinv;
		// GestXi = J_T_pinv*Gest;
	  
		/* command torque to joint */
		tau_cmd_old = tau_cmd;
		// tau_cmd = Mest*ddot_qr + Cest*dot_qr + Dest + Gest + J.transpose()*Kp*error + J.transpose()*Kv*dot_error + N1*Kn*(error_Nq0-dot_q_curr);
		tau_cmd = Mest*ddot_qr + Cest*dot_qr + Dest + Gest + Kd*dot_error_q + J.transpose()*Kp*error + N1*Kn*(error_Nq0-dot_q_curr);

		/*For testing without Adp*/
		// tau_cmd = M*ddot_qr + C + G + J.transpose()*Kp*error + J.transpose()*Kv*dot_error + Kn*dot_error_Nq0;

		/* Verify the tau_cmd not exceed the desired joint torque value tau_J_d */
		// tau_cmd = saturateTorqueRate(tau_cmd, tau_J_curr); // works very bad, too much noise in the joint sensors
		tau_cmd = saturateTorqueRate(tau_cmd, tau_J);

		/* Set the command for each joint */
		for (size_t i = 0; i < 7; i++) {
		 	joint_handles_[i].setCommand(tau_cmd[i]-G(i));
		}

		/* Publish messages */
		time_now = ros::Time::now();
		msg_log.header.stamp = time_now;

		fillMsg(msg_log.error_pos_EE, error);
		fillMsg(msg_log.Fext, F_cont);
		fillMsg(msg_log.dot_error_pos_EE, dot_error);
		fillMsgLink(msg_log.link1, param_tot.segment(0, PARAM+FRICTION));
		fillMsgLink(msg_log.link2, param_tot.segment(12, PARAM+FRICTION));
		fillMsgLink(msg_log.link3, param_tot.segment(24, PARAM+FRICTION));
		fillMsgLink(msg_log.link4, param_tot.segment(36, PARAM+FRICTION));
		fillMsgLink(msg_log.link5, param_tot.segment(48, PARAM+FRICTION));
		fillMsgLink(msg_log.link6, param_tot.segment(60, PARAM+FRICTION));
		fillMsgLink(msg_log.link7, param_tot.segment(72, PARAM+FRICTION));
		fillMsg(msg_log.tau_cmd, tau_J);
		fillMsg(msg_log.tau_tilde, err_param);
		fillMsg(msg_log.dot_qr, dot_q_curr);
		fillMsg(msg_log.ddot_qr, ddot_q_curr);
		msg_log.cond = lambda_min;
		// msg_log.cond = l;

		msg_config.header.stamp  = time_now; 
		msg_config.xyz.x = T0EE.translation()(0); 
		msg_config.xyz.y = T0EE.translation()(1);
		msg_config.xyz.z = T0EE.translation()(2);

		/*Fill msg opt*/
		// fillMsg(msg_opt.q_cur, q_curr);
		// fillMsg(msg_opt.dot_q_curr, dot_q_curr);
		// fillMsg(msg_opt.ddot_q_curr, ddot_q_curr);
		// fillMsg(msg_opt.ee_velocity, ee_velocity);
		// msg_opt.l = l;
		// msg_opt.count = count;
		// fillMsg(msg_opt.H_stack, H_vec); // only this is needed

		// this->pub_opt_.publish(msg_opt);
		this->pub_err_.publish(msg_log); 
		this->pub_config_.publish(msg_config); 

		// cout<<"tau_cmd: "<<tau_cmd<<endl;
		// cout<<"tau_J_d: "<<tau_J_d<<endl;
		// cout<<"tau_J_curr: "<<tau_J_curr<<endl;
		// cout<<"tau_J: "<<tau_J<<endl;
		// cout<<"dqr: "<<dot_qr<<endl;
		// cout<<"ddqr: "<<ddot_qr<<endl;
		// cout<<"error: "<<error<<endl;
		// cout<<"dot_error: "<<dot_error<<endl;
		// cout<<"dot_error_Nq0: "<<dot_error_Nq0<<endl;
		// cout<<"kp_xi: "<<Kp<<endl;
		// cout<<"kv_xi: "<<Kv<<endl;
		// cout<<"kn: "<<Kn<<endl;
		// cout<<"Mest: "<<Mest<<endl;
		// cout<<"Cest: "<<Cest<<endl;
		// cout<<"Gest: "<<Gest<<endl;

	}

	void CTModOS::stopping(const ros::Time&)
	{
	//TO DO
	}


	void CTModOS::addValue(std::vector<Eigen::Matrix<double,NJ, 1>>& buffer_, const Eigen::Matrix<double,NJ, 1>& dato_, int win_len) {
		buffer_.push_back(dato_);
		if (buffer_.size() > win_len) {
			buffer_.erase(buffer_.begin());
		}
	}

	// Funzione per il calcolo della mean
	Eigen::Matrix<double,NJ, 1> CTModOS::obtainMean(const std::vector<Eigen::Matrix<double,NJ, 1>>& buffer_) {
		Eigen::Matrix<double,NJ, 1> mean = Eigen::Matrix<double,NJ, 1>::Zero();
		for (const auto& vector : buffer_) {
			mean += vector;
		}
		mean /= buffer_.size();
		return mean;
	}

	double CTModOS::deltaCompute(double a){
		double sgn;
		
		if (fabs(a) < 0.001){
			sgn = 0.0; 
		}else{
			sgn = a/(fabs(a)+0.01);
			// sgn = a;
		}
		return sgn;
	}

	// Define the function to be called when ctrl-c (SIGINT) is sent to process
	void CTModOS::signal_callback_handler(int signum){
		cout << "Caught signal " << signum << endl;
		// Terminate program
		exit(signum);
	}
	
	/* Check for the effort commanded */
	Eigen::Matrix<double, NJ, 1> CTModOS::saturateTorqueRate(
	const Eigen::Matrix<double, NJ, 1>& tau_d_calculated,
	const Eigen::Matrix<double, NJ, 1>& tau_J_d)
	{
		Eigen::Matrix<double, NJ, 1> tau_d_saturated;
		for (size_t i = 0; i < 7; i++) {

			double difference = tau_d_calculated[i] - tau_J_d[i];
			tau_d_saturated[i] = tau_J_d[i] + std::max(std::min(difference, kDeltaTauMax), -kDeltaTauMax);

		}
		return tau_d_saturated;
	}

	void CTModOS::setCommandCB(const panda_controllers::desTrajEE::ConstPtr& msg)
	{
		ee_pos_cmd << msg->position.x, msg->position.y, msg->position.z;
		ee_vel_cmd << msg->velocity.x, msg->velocity.y, msg->velocity.z;
		ee_acc_cmd << msg->acceleration.x, msg->acceleration.y, msg->acceleration.z;
	}  

	// void CTModOS::setCommandCBJ(const sensor_msgs::JointStateConstPtr& msg)
	// {
	// 	q_opt = Eigen::Map<const Eigen::Matrix<double, 7, 1>>((msg->position).data());
	// 	dq_opt = Eigen::Map<const Eigen::Matrix<double, 7, 1>>((msg->velocity).data());
	// 	ddq_opt = Eigen::Map<const Eigen::Matrix<double, 7, 1>>((msg->effort).data());
	// }

	void CTModOS::setGains(const panda_controllers::impedanceGain::ConstPtr& msg)
	{
		for(int i = 0; i<DOF; ++i){
			Kp(i,i) = msg->stiffness[i];
			Kv(i,i) = msg->damping[i];
			// ROS_INFO_STREAM(Kp);
		}    
	}

	void CTModOS::setRPYcmd(const panda_controllers::rpy::ConstPtr& msg){
		// ee_rot_cmd << msg->roll, msg->pitch, msg->yaw;
		// if(update_opt_flag == false){
			// ee_rot_cmd = 
		Eigen::AngleAxisd(msg->angle[0], Eigen::Vector3d::UnitX()) *
		Eigen::AngleAxisd(msg->angle[1], Eigen::Vector3d::UnitY()) *
		Eigen::AngleAxisd(msg->angle[2], Eigen::Vector3d::UnitZ());
		// }
		// ROS_INFO_STREAM(ee_rot_cmd);
	}

	void CTModOS::callbackFext(const geometry_msgs::WrenchStamped::ConstPtr& msg){
		F_ext(0) = msg->wrench.force.x;
		F_ext(1) = msg->wrench.force.y;
		F_ext(2) = msg->wrench.force.z;
		// ROS_INFO_STREAM(F_ext);
	}

	// Eigen::Affine3d CTModOS::computeT0EE(const Eigen::VectorXd& q){
	   
	// 	Eigen::Matrix<double, NJ, 4> DH; // matrice D-H
	// 	Eigen::Affine3d T0i = Eigen::Affine3d::Identity();
	// 	Eigen::Affine3d T0n; 
	// 	Eigen::Matrix<double, 4, 4> T = Eigen::Matrix<double, 4, 4>::Identity();

	// 	// Riempio sezione distanza "a"
	// 	DH.block(0,0,NJ,1) << 0, 0, 0,0.0825, -0.0825, 0, 0.088;   
	// 	// Riempio sezione angolo "alpha"
	// 	DH.block(0,1,NJ,1) << 0, -M_PI_2, M_PI_2, M_PI_2, -M_PI_2, M_PI_2, M_PI_2;
	// 	// Riempio sezione distanza "d"
	// 	DH.block(0,2,NJ,1) << 0.3330, 0, 0.3160, 0, 0.384, 0, 0.107; // verificato che questi valori corrispondono a DH che usa il robot in simulazione
	// 	// Riempio sezione angolo giunto "theta"
	// 	DH.block(0,3,NJ,1) = q;     

	// 	for (int i = 0; i < NJ; ++i)
	// 	{
	// 		double a_i = DH(i,0);
	// 		double alpha_i = DH(i,1);
	// 		double d_i = DH(i,2);
	// 		double q_i = DH(i,3);

	// 		T << cos(q_i), -sin(q_i), 0, a_i,
	// 			sin(q_i)*cos(alpha_i), cos(q_i)*cos(alpha_i), -sin(alpha_i), -sin(alpha_i)*d_i,
	// 			sin(q_i)*sin(alpha_i), cos(q_i)*sin(alpha_i), cos(alpha_i), cos(alpha_i)*d_i,
	// 			0, 0, 0, 1;

	// 		// Avanzamento perice i 
	// 		T0i.matrix() = T0i.matrix()*T;
	// 	}
	// 	T0n = T0i;
	// 	/*If EE system differs from frame n(Like frame hand true robot)*/
	// 	Eigen::Affine3d TnEE;
	// 	Eigen::Vector3d dnEE;
	// 	dnEE << 0.13, 0 , 0.035;
	// 	T0n.translation() = T0i.translation() + T0i.linear()*dnEE;    
	// 	return T0n;
	// }

	void CTModOS::setFlagUpdate(const panda_controllers::flag::ConstPtr& msg){
		update_param_flag = msg->flag;
	}
	
	// void CTModOS::setFlagJoints(const panda_controllers::flag::ConstPtr& msg){
	// 	update_opt_flag = msg->flag;
	// }

	void CTModOS::setResetFlag(const panda_controllers::flag::ConstPtr& msg){
		reset_adp_flag = msg->flag;
	}


	template <size_t N>
	void CTModOS::fillMsg(boost::array<double, N>& msg_, const Eigen::VectorXd& data_) {
		int dim = data_.size();
		for (int i = 0; i < dim; i++) {
			msg_[i] = data_[i];
		}
	}

	void CTModOS::fillMsgLink(panda_controllers::link_params &msg_, const Eigen::VectorXd& data_) {      
		msg_.mass = data_[0];
		msg_.m_CoM_x = data_[1];
		msg_.m_CoM_y = data_[2];
		msg_.m_CoM_z = data_[3];
		msg_.Ixx = data_[4];
		msg_.Ixy = data_[5];
		msg_.Ixz = data_[6];
		msg_.Iyy = data_[7];
		msg_.Iyz = data_[8];
		msg_.Izz = data_[9];
		msg_.d1 = data_[10];
		msg_.d2 = data_[11];
	}

}
PLUGINLIB_EXPORT_CLASS(panda_controllers::CTModOS, controller_interface::ControllerBase);