﻿///FCC_seiral_nod For IROS ADR 2017
/// OVERALL COMMON SIGN : X -> roll, aileron   Y -> pitch, elevator   Z-> heave
/// ARRAY SEQUENCE: ENUM { X Y Z }
///
#define LOG_SPECIFY
#include "DefineList.h"

#define Kp_x                         (        70)
#define Kp_v                         (     0.024)  //0.025
#define Kd_x                         (       0.8) //0.25   0.16
#define Kp_y                         (       0.4)
#define Kd_y                         (       0.1)
#define Kp_z                         (       0.6)
#define Kd_z                         (      0.05)
#define Kp_psi                       (      0.35)

#define eps                          ( 0.0000001)
#define D2R                 (float ) (3.141592 / 180.0)
#define SECOND                       (        20)
#define FAIL                         (         0)
#define MATCH                        (         1)
#define VEL_FWD             (float)  (       0.6)
#define _GATE_THRES         (float)  (       0.9)
#define _MIN_ALT            (float)  (      0.05)
#define _ON                          (         1)
#define _OFF                         (         0)

#define _HOVER              (float)  (       5.0)
#define _FORWARD            (float)  (      13.0)
#define _MARGIN             (float)  (       2.0)


int 	FdPort1                 ;
// Serial Communication initialization
float   Y_cmd             = 2.0 ;
float   pos_error_x_m     = 0.0 ;
float   pos_error_y_m     = 0.0 ;
float   cmd_pos_z         = 0.0 ;
float   cmd_pos_psi       = 0.0 ;
float   psi_cmd           = 0.0 ;
float   del_psi           = 0.0 ;
float 	h                       ;
float   r_data                  ;
float   a_param, b_param        ;
char    filename[50]            ;
float height_m = 0.0;

//Flags
bool     flag_gate_num_counter  = 0 ;
bool     flag_gate_check_on     = 0 ;
bool     time_flag              = 0 ;
bool     flag_FM                = 0 ;
bool     flag_gate_init         = 0 ;


// Mission control
float WP_z[10] = {0,0,0,0,0,0,0,0.0,0};
float WP_psi[10] = {0,0,0,0,0,0,0,0.0,0};


// custom variables
int     count_mission_start = 0     ;
int     count_ros           = 0     ;
float   t_capt              = 0.0   ;
float   t_rel               = 0.0   ;
float   t_cur               = 0.0   ;
int     gate_num            = 0     ;
int     count_adhoc         = 0     ;
float   prev_pos_x	    = 0.0   ;
float   prev_pos_y	    = 0.0   ;
int     Mode_FlightMode     = 0     ;
float   FLAG_START          = 1.0   ;


//For FCC COM.
int OpenSerial(char *device_name);
int CloseSerial(int fd);
void updatedata(void);
void serialsend(int fd);
void *serialreceive(void *fd);
int DS_ParsingMainFuncArgs(int Arg_argc, char ** Arg_argv);
void DS_OnboardLog(void);


//Structures from DefineList.h
struct senddata     	     tx;         
struct struct_t_MessageBody  tx_data;
struct struct_t_RXttyO       StrRXttyO;
struct struct_t_MainFuncArgs StrMainFuncArgs;
struct Odometry_zed          Odom_zed;
struct OpticalFlow_zed       opt_flow;
struct Image_error     	     img;
struct velocity_command      cmd;

// setup the initial name
using namespace ros;
using namespace std;

// for publishing the data
std_msgs::Float32MultiArray receive_data;

float YawAngleMod(float ArgYawAngle_deg)
{
    float Ret = fmodf(ArgYawAngle_deg, 360.0);

    if(Ret > 180.0)
    {

        Ret -= 360.0;
    }
    return Ret;
}

float sat(float data, float max)
{

    float res;

    if(abs(data) > max)
        res = (data + eps)/abs(data + eps)*max;
    else
        res = data;

    return res;
}

void zed_OpticalFlow(const std_msgs::Float32MultiArray& zed_optflow_msg)
{
    opt_flow.x = zed_optflow_msg.data[0]; //lateral
    opt_flow.y = zed_optflow_msg.data[1]; //lateral
}

void Lidar(const std_msgs::Float64 &Lidar_Height_msg)
{
    height_m = Lidar_Height_msg.data;
}


void Flag_start(const std_msgs::Float64 &Flag_start_msg)
{
    FLAG_START = Flag_start_msg.data;
}

void callback_serial_comm(const std_msgs::Float32MultiArray &msg)
{
    img.pos_error_pixel[0] 		 = msg.data[0]; //lateral axis error
    img.pos_error_pixel[1] 		 = msg.data[1]; //heave axis error
    img.pos_error_pixel[2] 		 = msg.data[2]; //distance to gate
    img.pos_error_pixel[3] 		 = msg.data[3]; //gate position error

    float div_x = -12.34*pow(img.pos_error_pixel[2],2) + 121.6*img.pos_error_pixel[2] -336.1;
    pos_error_x_m = -img.pos_error_pixel[0] / div_x;
}

int main(int argc, char** argv)
{
    static FILE* pFile;
    char OutFileName[12] = {" "};
    if(DS_ParsingMainFuncArgs(argc, argv) == 1)
	{
		printf("[DONE] Parsing main function arguments\n");
	}
	else
	{
		printf("[ERROR] 'DS_ParsingMainFuncArgs()'\n");
        return -1;
    }

    // node name initialization
    init(argc, argv, "FCC_serial");

    // assign node handler
    ros::NodeHandle nh_;

    // for debugging
    printf("Initiate: FCC_Serial_node\n");

    // subscribing the image processing results (x_pos, y_pos)
    Subscriber zed_optflow_sub_ = nh_.subscribe("/zedcamera/opt_flow", 1, &zed_OpticalFlow);
    Subscriber msg_data_input  = nh_.subscribe("/gate/pos_info", 4, callback_serial_comm);
    Subscriber Lidar_sub_ = nh_.subscribe("/mavros/global_position/rel_alt", 1, &Lidar);
    Subscriber flag_sub_ = nh_.subscribe("/flag/start", 1, &Flag_start);
    Publisher  fcc_info_pub = nh_.advertise<std_msgs::Int8MultiArray>("/fcc_info", 20);
    Publisher  fcc_cmd_pub = nh_.advertise<mavros_msgs::OverrideRCIn>("/mavros/rc/override", 20);

    receive_data.data.resize(10);

    // setup the loop speed, [Hz], synchronizing the hector slam loop
    ros::Rate loop_rate(20);

    float fdt = (float)(1/20);

    //===== Open Serial =====//
    FdPort1 = OpenSerial(PORT1);

    //===== pthread create =====//
    pthread_t p_thread;
    int thread_rx;

    thread_rx = pthread_create(&p_thread, NULL, serialreceive, (void *)FdPort1);

    if(thread_rx < 0)
    {
            perror("thread create error : ");
            exit(0);
    }

#ifdef LOG_SPECIFY
        sprintf(OutFileName,"/home/ubuntu/catkin_ws/src/fcc_serial/src/%s", "test");
        pFile = fopen(strcat(OutFileName, ".out"), "w+t");
#endif

    WP_psi[0] = ( 0.0 + 90.0 ) ;
    WP_psi[1] = ( WP_psi[0] + 179.0 ) ;
    WP_psi[2] = ( WP_psi[0] + 179.0 ) ;
    WP_psi[3] = ( WP_psi[0] + 179.0 ) ;
    WP_psi[4] = ( WP_psi[0] + 179.0 ) ;
    WP_psi[5] = ( WP_psi[0] + 90.0 ) ;
    WP_psi[6] = ( WP_psi[0] + 135.0 ) ;
    WP_psi[7] = ( WP_psi[0] + 179.0 ) ;
    WP_psi[8] = ( WP_psi[0] - 90.0 ) ;
    WP_psi[9] = ( WP_psi[0] - 90.0 ) ;

    WP_z[0] = 2.65          ;
    WP_z[1] = 2.65          ;
    WP_z[2] = 2.25          ;
    WP_z[3] = 2.25          ;
    WP_z[4] = 2.25          ;
    WP_z[5] = 2.25          ;
    WP_z[6] = 2.75          ;
    WP_z[7] = 3.25          ;
    WP_z[8] = 2.35          ;
    WP_z[9] = 1.65          ;


    while( ok() )
    {
        std_msgs::Int8MultiArray fcc_info_msg;
        mavros_msgs::OverrideRCIn fcc_cmd_msg;

        t_cur = count_ros / 20.0;

        //cout << opt_flow.x << " cur_vel_y: " << opt_flow.y << "\n";
        cout << FLAG_START<<"\n";

        fcc_info_msg.data.clear();
        fcc_info_msg.data.resize(4);
        fcc_info_msg.data[0] = StrRXttyO.Mode_FlightMode;
        fcc_info_msg.data[1] = gate_num;
        fcc_info_msg.data[2] = height_m;


        if (FLAG_START == 1.0)
        {
            cout << "\n3. Mission Mode" << " Gate_num: " << gate_num << " count: " << count_adhoc << " Quality: " << StrRXttyO.FlowQuality << "\n";
            count_mission_start = count_mission_start + 1;
            pos_error_x_m = sat(pos_error_x_m, 0.8);

            cmd_pos_psi = WP_psi[gate_num];
            cmd_pos_z   = WP_z[gate_num]  ;

            float psi_error     = YawAngleMod(cmd_pos_psi - StrRXttyO.Cur_Att_deg[2] );
            float posZ_error    = cmd_pos_z - height_m*cos(fabs(StrRXttyO.Cur_Att_deg[1])*D2R);

            float psi_LOS = Kp_x*pos_error_x_m - StrRXttyO.FlowXY_mps[0]*Kd_x;

            psi_cmd = cmd_pos_psi + psi_LOS;
            del_psi = cmd_pos_psi + psi_LOS - StrRXttyO.Cur_Att_deg[2];


            if( (count_mission_start > (_HOVER + _FORWARD + _MARGIN)*SECOND) && (height_m > _MIN_ALT) && (height_m < _GATE_THRES) && (flag_gate_check_on == _ON))
            {
                flag_gate_num_counter   = _ON;
                flag_gate_check_on      = _OFF;                
            }

            if( flag_gate_num_counter == _ON )
            {
                gate_num              = gate_num+1;
                time_flag             = _ON;                
                flag_gate_num_counter = _OFF;	
		count_adhoc = 0;
		flag_gate_init = 0;	
            }

            if(time_flag == _ON)
            {
                t_capt      = t_cur;
                time_flag   = _OFF;
            }
            t_rel = t_cur - t_capt;

            if(t_rel > 1.5)
            {
                flag_gate_check_on  = _ON   ;
                t_capt              = 0.0   ;
                t_rel               = 0.0   ;
            }
/// ---------------------------------------Straight Section----------------------------------------------------------
            if (gate_num == 0 )
            {
                count_adhoc = count_adhoc + 1;

                if(count_adhoc < _HOVER*SECOND)
                {
                    cmd.X_out = 0.0;
                    cmd.Y_out = 0.0;
                    cmd.Z_out = -1.0*Kp_z*(0.8 - height_m*cos(fabs(StrRXttyO.Cur_Att_deg[1])*D2R));
                    cmd.PSI_out = 0.0;
                    flag_gate_init = 1;
                }

                if(count_adhoc < (_HOVER + _FORWARD)*SECOND)
                {
                    cmd.X_out = 0.0;
                    cmd.Y_out = 0.3;
                    cmd.Z_out = -1.0*Kp_z*(1.5 - height_m*cos(fabs(StrRXttyO.Cur_Att_deg[1])*D2R));
                    cmd.PSI_out = 0.0;
                    flag_gate_init = 1;
                }

                else if(count_adhoc > (_HOVER + _FORWARD)*SECOND && count_adhoc < (_HOVER + _FORWARD + 1.3)*SECOND)
                {
                    cmd.X_out =  0.2;
                    cmd.Y_out =  0.2;
                    cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                    cmd.PSI_out = Kp_psi*(psi_error);
                    flag_gate_init = 1;
                }

                else flag_gate_init = 0;
            }
/// ---------------------------------------Curve Section--------------------------------------------------------------
            if (gate_num == 1 )
            {            
                count_adhoc = count_adhoc + 1;
    
                if(count_adhoc < 1.0*SECOND)
		{
                    cmd.X_out = -0.3;
                    cmd.Y_out = 0.4;
                    cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                    cmd.PSI_out = Kp_psi*(psi_error);
                    flag_gate_init = 1;
                }

                else flag_gate_init = 0;
            }

            if (gate_num == 2 )
            {
                count_adhoc = count_adhoc + 1;

                if(count_adhoc < 2.0*SECOND)
                {
                    cmd.X_out = 0.6;
                    cmd.Y_out = -0.3;
                    cmd.Z_out = -1.0*posZ_error*Kp_z;
                    cmd.PSI_out = Kp_psi*(psi_error+ psi_LOS);
                    flag_gate_init = 1;
                }

                else flag_gate_init = 0;
            }

            if (gate_num == 3 )
            {
                count_adhoc = count_adhoc + 1;

                if(count_adhoc < 3.0*SECOND)
                {
                    cmd.X_out = -0.5;
                    cmd.Y_out = -0.3;
                    cmd.Z_out = -1.0*posZ_error*Kp_z;
                    cmd.PSI_out = Kp_psi*(psi_error+ psi_LOS);
                    flag_gate_init = 1;
                }

                else flag_gate_init = 0;
            }

            if (gate_num == 4 )
            {
                count_adhoc = count_adhoc + 1;

                if(count_adhoc < 2.3*SECOND)
                {
                    cmd.X_out =  0.5;
                    cmd.Y_out = -0.3;
                    cmd.Z_out = -1.0*posZ_error*Kp_z;
                    cmd.PSI_out = Kp_psi*(psi_error+ psi_LOS);
                    flag_gate_init = 1;
                }

                else flag_gate_init = 0;
            }

            if (gate_num == 5 )
            {
                if (fabs(psi_cmd - StrRXttyO.Cur_Att_deg[2]) > 15)
                {
                    cmd.X_out = -0.1;
                    cmd.Y_out = 0.0;
                    cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                    cmd.PSI_out = Kp_psi*(psi_error+ psi_LOS);
                    flag_gate_init = 1;
                }

                else
                {
                    cmd.X_out = 0.0;//Kp_x*pos_error_x_m - StrRXttyO.FlowXY_mps[0]*Kd_x ; //lateral
                    cmd.Y_out = 0.35;          //longitudinal
                    cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                    cmd.PSI_out = Kp_psi*(psi_error+ psi_LOS);
                }
            }
/// ---------------------------------------Sharp Curve Section--------------------------------------------------------
            if (gate_num == 6 )
            {
                if (fabs(psi_error) > 15)
                {
                    cmd.X_out = 0.0;
                    cmd.Y_out = 0.0;
                    cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                    cmd.PSI_out = Kp_psi*(psi_error);
                    flag_gate_init = 1;
                }

                else
                {
                    cmd.X_out = 0.0;//Kp_x*pos_error_x_m - StrRXttyO.FlowXY_mps[0]*Kd_x ; //lateral
                    cmd.Y_out = 0.35;          //longitudinal
                    cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                    cmd.PSI_out = Kp_psi*(psi_error);
                }
            }

            if (gate_num == 7 )
            {
                if (fabs(psi_error) > 15)
                {
                    cmd.X_out = 0.0;
                    cmd.Y_out = 0.0;
                    cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                    cmd.PSI_out = Kp_psi*(psi_error);
                    flag_gate_init = 1;
                }

                else
                {
                    cmd.X_out = 0.0;//Kp_x*pos_error_x_m - StrRXttyO.FlowXY_mps[0]*Kd_x ; //lateral
                    cmd.Y_out = 0.35;          //longitudinal
                    cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                    cmd.PSI_out = Kp_psi*(psi_error);
                }
            }


            if (gate_num == 8 )
            {
                if (fabs(psi_error) > 15)
                {
                    cmd.X_out = 0.0;
                    cmd.Y_out = 0.0;
                    cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                    cmd.PSI_out = Kp_psi*(psi_error);
                    flag_gate_init = 1;
                }

                else
                {
                    cmd.X_out = 0.0;//Kp_x*pos_error_x_m - StrRXttyO.FlowXY_mps[0]*Kd_x ; //lateral
                    cmd.Y_out = 0.35;          //longitudinal
                    cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                    cmd.PSI_out = Kp_psi*(psi_error);
                }
            }
/// ---------------------------------------Dynamic Obstacle Section--------------------------------------------------
            if (gate_num == 9)
            {
                count_adhoc = count_adhoc + 1;

                if(count_adhoc < 1.0*SECOND)
                {
                    cmd.X_out = -0.3;
                    cmd.Y_out = 0.4;
                    cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                    cmd.PSI_out = Kp_psi*(psi_error);
                    flag_gate_init = 1;
                }

                else flag_gate_init = 0;
            }

            if (gate_num > 9)
            {
                cmd.X_out = 0.0;
                cmd.Y_out = 0.3;
                cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                cmd.PSI_out = Kp_psi*(psi_error);
            }
/// ---------------------------------------Guidance Command----------------------------------------------------------
            if (flag_gate_init == 0)
            {
                cmd.X_out = Kp_v*(psi_LOS);//Kp_x*pos_error_x_m - StrRXttyO.FlowXY_mps[0]*Kd_x ; //lateral
                cmd.Y_out = sqrt( pow(VEL_FWD, 2) - pow( sat(cmd.X_out,VEL_FWD), 2) ) ;          //longitudinal
                cmd.Z_out = -1.0*Kp_z*posZ_error;                                                //heave
                cmd.PSI_out = Kp_psi*(psi_error+ psi_LOS);
            }
            ///PWM range: 1000~2000,, Nominal Value:1500
            fcc_cmd_msg.channels[0] = 0.0;//(500.0/4.0)*cmd.X_out + 1500.0;   ///0~6.0
            fcc_cmd_msg.channels[1] = 1550;//-(500.0/4.0)*cmd.Y_out + 1500.0;   ///0~6.0
            fcc_cmd_msg.channels[2] = (500.0/4.0)*(-cmd.Z_out) + 1500.0;
            fcc_cmd_msg.channels[3] = 0.0;//(500.0/4.0)*cmd.PSI_out + 1500.0;
            fcc_cmd_msg.channels[4] = 0.0;
            fcc_cmd_msg.channels[5] = 0.0;
            fcc_cmd_msg.channels[6] = 0.0;
            fcc_cmd_msg.channels[7] = 0.0;
         }


        if(FLAG_START==0.0)
        {
            cout << "Emergency Landing" << "\n";
            fcc_cmd_msg.channels[0] = 0.0;
            fcc_cmd_msg.channels[1] = 0.0;
            fcc_cmd_msg.channels[2] = 0.0;
            fcc_cmd_msg.channels[3] = 0.0;
            fcc_cmd_msg.channels[4] = 0.0;
            fcc_cmd_msg.channels[5] = 0.0;
            fcc_cmd_msg.channels[6] = 0.0;
            fcc_cmd_msg.channels[7] = 0.0;
        }

#ifdef LOG_SPECIFY        
        fprintf(pFile, "%.4f %d %d %.4f %.4f %.4f %.4f %.4f %.4f\n", t_cur, StrRXttyO.Mode_FlightMode, gate_num, psi_cmd, StrRXttyO.Cur_Att_deg[2], del_psi, pos_error_x_m, cmd.X_out, StrRXttyO.FlowXY_mps[0]);
#endif

        updatedata();
        //===== Serial TX part=====//
        serialsend(FdPort1);

        if(StrMainFuncArgs.Flag_Args[0] == 1) // Case of Activating Onboard Logging
        {
                //printf("Debug OnboadLog\n");
                DS_OnboardLog();
        }

        fcc_info_pub.publish(fcc_info_msg);
        fcc_cmd_pub.publish(fcc_cmd_msg);
	prev_pos_x = Odom_zed.x;
        prev_pos_y = Odom_zed.y;

        count_ros = count_ros + 1;

        // loop rate [Hz]
        loop_rate.sleep();
        // loop sampling, ros
        spinOnce();
    }
    // for debugging
    printf("Terminate: FCC_Serial_node\n");
    return 0;
}



void updatedata(void)
{
    /// tx_data update
    tx_data.FlagA        =0; // Flag for auto takeoff/landing/motor-cut, etc...
    tx_data.Flag_Sensing =1; // [1] = flow from high-level computer (TX1, 2, etc...)
    tx_data.FlagC        =0; // N/A
    tx_data.FlagD        =0; // N/A

    tx_data.CmdVelAil = sat(cmd.X_out, 2.0);
    tx_data.CmdVelEle = sat(cmd.Y_out, 2.0);
    tx_data.CmdVelDown = sat(cmd.Z_out, 4.0);
    tx_data.CmdR_dps = sat(cmd.PSI_out, 30);

    tx_data.Cur_FlowAilEle_mps[0] = opt_flow.x;
    tx_data.Cur_FlowAilEle_mps[1] = opt_flow.y;


    unsigned char *data = (unsigned char *)&tx_data;
    memcpy((void *)(tx.Data),(void *)(data),sizeofdata);
}

void *serialreceive(void *fdt)
{
	int fd = *((int*) &fdt);
	int datasize;
	unsigned char RXRawData[sizeof(StrRXttyO)];

	printf("pthread RX process start!\n");
	while(1)
	{
		int ParsingMode   = 1;
        int ContinueWhile = 1;
		while(ContinueWhile)
		{
			switch(ParsingMode)
			{
			case 1:
				if(read((int)fd, &RXRawData[0], 1) == 1)
				{
					if(RXRawData[0] == 0x12)
					{
						ParsingMode = 2;
					}
					else
					{
						ParsingMode = 1;
					}
				}
				break;

			case 2:
				if(read((int)fd, &RXRawData[1], 1) == 1)
				{
					if(RXRawData[1] == 0x34)
					{
						ParsingMode = 3;
					}
					else
					{
						ParsingMode = 1;
					}
				}
				break;

			case 3:
				if(read((int)fd, &RXRawData[2], 1) == 1)
				{
					if(RXRawData[2] == 0x56)
					{
						ParsingMode = 4;
					}
					else
					{
						ParsingMode = 1;
					}
				}
				break;

			case 4:
				if(read((int)fd, &RXRawData[3], 1) == 1)
				{
					if(RXRawData[3] == 0x78)
					{
						ParsingMode = 5;
					}
					else
					{
						ParsingMode = 1;
					}
				}
				break;

			case 5:
				if(read((int)fd,&RXRawData[4],(sizeof(StrRXttyO)-4)/2)==(sizeof(StrRXttyO)-4)/2)
				{
					if(read((int)fd,&RXRawData[4]+(sizeof(StrRXttyO)-4)/2,(sizeof(StrRXttyO)-4)/2)==(sizeof(StrRXttyO)-4)/2)
					{
						// Calculate Checksum
						unsigned char CalChecksumA = 0;
                        unsigned char CalChecksumB = 0;

						int Ind;

                        for(Ind = 0; Ind<(sizeof(StrRXttyO)-2); Ind++)
						{
							CalChecksumA += RXRawData[Ind];
							CalChecksumB += CalChecksumA;
						}

						if((CalChecksumA == RXRawData[sizeof(StrRXttyO)-2])&&(CalChecksumB == RXRawData[sizeof(StrRXttyO)-1]))
						{
							memcpy((void *)(&StrRXttyO), (void *)(RXRawData), sizeof(StrRXttyO));
							ContinueWhile = 0;
						}
						else
						{
							ParsingMode = 1;
						}
					}
					else
					{
						ParsingMode = 1;
					}
				}
				else
				{
					ParsingMode = 1;
				}
				break;

			default:
				ParsingMode = 1;
				break;
			}
		}
	}

}

void serialsend(int fd)
{
	//===== initial header =====//
	tx.header[0] = header1;
	tx.header[1] = header2;

	tx.IDs[0] = IDs1;
	tx.IDs[1] = IDs2;

	tx.checksum[0] = 0;
	tx.checksum[1] = 0;

	unsigned char *data = (unsigned char *)&tx;

	for(int ind=0; ind<sizeof(senddata)-2;ind++)
	{
		tx.checksum[0] += data[ind];
		tx.checksum[1] += tx.checksum[0];
	}
	//printf("ckA : %d ckB : %d\n",tx.checksum[0],tx.checksum[1]);

	write(fd,&tx,sizeof(senddata));
}

int OpenSerial(char *device_name)
{
	int fd;
	struct termios newtio;

	fd = open(device_name, O_RDWR | O_NOCTTY);

	if(fd < 0)
	{
		printf("Serial Port Open Fail.\n");
		return -1;
	}

	memset(&newtio, 0, sizeof(newtio));
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;
	newtio.c_cflag = CS8|CLOCAL|CREAD;

	switch(BAUDRATE)
	{
		case 921600 : newtio.c_cflag |= B921600;
		break;
		case 115200 : newtio.c_cflag |= B115200;
		break;
		case 57600  : newtio.c_cflag |= B57600;
		break;
	}

	newtio.c_lflag 		= 0;
	newtio.c_cc[VTIME] 	= 0;
	newtio.c_cc[VMIN] 	= sizeof(StrRXttyO)/2;

	tcflush(fd,TCIFLUSH);
	tcsetattr(fd,TCSANOW, &newtio);

	return fd;
}

int CloseSerial(int fd)
{
	close(fd);
}

int DS_ParsingMainFuncArgs(int Arg_argc, char ** Arg_argv)
{
	// Initialization to set to 0
	memset(&StrMainFuncArgs, 0, sizeof(StrMainFuncArgs));


	// Assign Main Function Arguments
	StrMainFuncArgs.PtrArr_Args[0]  = DF_MAIN_FUNC_ARG_00;
	StrMainFuncArgs.PtrArr_Args[1]  = DF_MAIN_FUNC_ARG_01;
	StrMainFuncArgs.PtrArr_Args[2]  = DF_MAIN_FUNC_ARG_02;
	// ...
	// ...
	// ...
	// ...
	// ...


	// Set Flags
    if(Arg_argc > 2)
	{
		int TempIndA;
        int TempIndB;

		for(TempIndA = 2; TempIndA < Arg_argc; TempIndA++)
		{
			for(TempIndB = 0; TempIndB < DF_MAIN_FUNC_ARGS_MAX; TempIndB++)
			{
				if(strcmp(Arg_argv[TempIndA],StrMainFuncArgs.PtrArr_Args[TempIndB]) == 0)
				{
					StrMainFuncArgs.Flag_Args[TempIndB] = 1;
					break;
				}
			}
		}
	}


	// Get LogFileName
    if(StrMainFuncArgs.Flag_Args[0] == 1) // Case of Activating Onboard Logging
	{
		printf("Type onboard log file name : ");
		scanf("%1023s",&StrMainFuncArgs.OnboardLogFileName[0]);
        sprintf(filename,"/home/ubuntu/%s",StrMainFuncArgs.OnboardLogFileName);
    }
	return 1;
}

void DS_OnboardLog(void)
{
	static int    Flag_Initialized = 0;
	static FILE * FD_ONBOARD_LOG;

	if(Flag_Initialized==0) // Not Initialized
	{
		FD_ONBOARD_LOG = fopen(filename,"wb"); // File Opening
		if(FD_ONBOARD_LOG == NULL) // Open Error
		{
			printf("[ERROR] 'DS_OnboardLog()'\n");
			exit(-1); // Terminate Program
		}
		else // Opening Success
		{
			fclose(FD_ONBOARD_LOG);
			printf("[DONE] Creating onboard log file\n");
			Flag_Initialized = 1; // Initialized
		}
	}

	if(Flag_Initialized==1) // After Initializing
	{
		// Copy Data to Log File
        FD_ONBOARD_LOG = fopen(filename,"ab"); // File Opening with Update Mode
		fwrite(&StrRXttyO, sizeof(StrRXttyO), 1, FD_ONBOARD_LOG);        
		fclose(FD_ONBOARD_LOG);
        cout << StrRXttyO.Cur_Time_sec << "\n";
	}
}




