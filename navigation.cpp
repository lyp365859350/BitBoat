/*
 * navigation.c
 *
 *  Created on: 2016年5月10日
 *      Author: wangbo
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "radio.h"
#include "global.h"
#include "navigation.h"
#include "boatlink.h"
#include "gps.h"
#include "control.h"
#include "location.h"
//#include "utilityfunctions.h"
#include "utility.h"
#include "pid.h"

struct T_NAVIGATION auto_navigation;

static unsigned int get_next_wp_num(struct WAY_POINT *ptr_wp_data,\
                                    struct T_LOCATION *current_loc,\
		                            unsigned int current_target_wp_num,\
							        unsigned int total_wp_num,\
							        unsigned int arrive_radius);

void navigation_init(void)
{
	/*
	 * 结构体成员是指针，该成员在使用之前必须初始化
	 * 结构体内有不是例如char int等基本结构时，需要分配空间
	 * char int等基本类型，在声明时已经分配了空间，但是自己定义的结构是没有分配空间的
	 */
    auto_navigation.current_loc=(struct T_LOCATION *)malloc(sizeof (struct T_LOCATION));
	auto_navigation.current_target_loc=(struct T_LOCATION *)malloc(sizeof (struct T_LOCATION));
	auto_navigation.previous_target_loc=(struct T_LOCATION *)malloc(sizeof (struct T_LOCATION));
}

int navigation_loop(struct T_NAVIGATION *ptr_auto_navigation,\
                    struct WAY_POINT *ptr_wp_data,\
                    nmea_msg *ptr_gps_data)
{
    unsigned int target_wp_num = 0;

    if(gcs2ap_radio_all.arrive_radius<MIN_ARRIVE_RADIUS)
    {
        gcs2ap_radio_all.arrive_radius = MIN_ARRIVE_RADIUS;
    }
    ptr_auto_navigation->arrive_radius=gcs2ap_radio_all.arrive_radius*10;

    //总航点数
    ptr_auto_navigation->total_wp_num=global_bool_boatpilot.wp_total_num;

    //当前位置
    ptr_auto_navigation->current_loc->lng = (float)(ptr_gps_data->longitude)*GPS_LOCATION_SCALE;
    ptr_auto_navigation->current_loc->lat = (float)(ptr_gps_data->latitude)*GPS_LOCATION_SCALE;

    switch(gcs2ap_radio_all.workmode)
    {
    case STOP_MODE:
        //推进器停止，方向舵停止
        break;
    case RC_MODE:
        break;
    case AUTO_MODE:
        switch(gcs2ap_radio_all.auto_work_mode)
        {
        case AUTO_MISSION_MODE:
            break;
        case AUTO_GUIDE_MODE:
            ptr_auto_navigation->current_target_wp_cnt = gcs2ap_radio_all.wp_guide_no;
            gcs2ap_radio_all.cte_p=0.0;
            gcs2ap_radio_all.cte_i=0.0;
            gcs2ap_radio_all.cte_d=0.0;
            break;
        case AUTO_LOITER_MODE:
            break;
        default:
            break;
        }

        if (ptr_auto_navigation->current_target_wp_cnt >= ptr_auto_navigation->total_wp_num)
        {
           ptr_auto_navigation->current_target_wp_cnt = 0;
        }

        /*1. 获取目标航点*/
        target_wp_num=get_next_wp_num(ptr_wp_data,\
                                     ptr_auto_navigation->current_loc,\
                                     ptr_auto_navigation->current_target_wp_cnt,\
                                     ptr_auto_navigation->total_wp_num,\
                                     ptr_auto_navigation->arrive_radius);

        /*2. 更新上一航点，当前航点的信息*/
        ptr_auto_navigation->current_target_wp_cnt = target_wp_num;
        ptr_auto_navigation->current_target_loc->lng = ((float)ptr_wp_data[target_wp_num].lng)*GPS_LOCATION_SCALE;
        ptr_auto_navigation->current_target_loc->lat = ((float)ptr_wp_data[target_wp_num].lat)*GPS_LOCATION_SCALE;

        if(target_wp_num>=1)
        {
           ptr_auto_navigation->previous_target_loc->lng=((float)ptr_wp_data[target_wp_num-1].lng)*GPS_LOCATION_SCALE;
           ptr_auto_navigation->previous_target_loc->lat=((float)ptr_wp_data[target_wp_num-1].lat)*GPS_LOCATION_SCALE;
        }
        else
        {
           ptr_auto_navigation->previous_target_loc->lng=((float)ptr_wp_data[ptr_auto_navigation->total_wp_num-1].lng)*GPS_LOCATION_SCALE;
           ptr_auto_navigation->previous_target_loc->lat=((float)ptr_wp_data[ptr_auto_navigation->total_wp_num-1].lat)*GPS_LOCATION_SCALE;
        }

        /*3. 获取 1期望course angle 2当前实际course angle 3期望heading angle 4当前实际heading angle */
        if ((ptr_gps_data->longitude != 0 )&& (ptr_gps_data->latitude != 0))
        {
           ptr_auto_navigation->command_course_angle_radian = get_command_heading_NED(ptr_auto_navigation->previous_target_loc,\
                                                                                      ptr_auto_navigation->current_loc,\
                                                                                      ptr_auto_navigation->current_target_loc);
           ptr_auto_navigation->gps_course_angle_radian=ptr_gps_data->course;

           ptr_auto_navigation->command_heading_angle_radian = get_command_heading_NED(ptr_auto_navigation->previous_target_loc,\
                                                                                       ptr_auto_navigation->current_loc,\
                                                                                       ptr_auto_navigation->current_target_loc);

           ptr_auto_navigation->gps_heading_angle_degree=((float)ptr_gps_data->yaw)*GPS_DIRECTION_INT_TO_REAL;
           ptr_auto_navigation->gps_heading_angle_radian=convert_degree_to_radian(ptr_auto_navigation->gps_heading_angle_degree);
        }

        global_bool_boatpilot.wp_next=ptr_auto_navigation->current_target_wp_cnt;
        break;
    case MIX_MODE:
        break;
    case RTL_MODE:
        ptr_auto_navigation->current_target_wp_cnt = 0;
        gcs2ap_radio_all.cte_p=0.0;
        gcs2ap_radio_all.cte_i=0.0;
        gcs2ap_radio_all.cte_d=0.0;

        if (ptr_auto_navigation->current_target_wp_cnt >= ptr_auto_navigation->total_wp_num)
        {
            ptr_auto_navigation->current_target_wp_cnt = 0;
        }

        /*1. 获取目标航点*/
        target_wp_num=get_next_wp_num(ptr_wp_data,\
                                      ptr_auto_navigation->current_loc,\
                                      ptr_auto_navigation->current_target_wp_cnt,\
                                      ptr_auto_navigation->total_wp_num,\
                                      ptr_auto_navigation->arrive_radius);

        /*2. 更新上一航点，当前航点的信息*/
        ptr_auto_navigation->current_target_wp_cnt = target_wp_num;
        ptr_auto_navigation->current_target_loc->lng = ((float)ptr_wp_data[target_wp_num].lng)*GPS_LOCATION_SCALE;
        ptr_auto_navigation->current_target_loc->lat = ((float)ptr_wp_data[target_wp_num].lat)*GPS_LOCATION_SCALE;

        if(target_wp_num>=1)
        {
            ptr_auto_navigation->previous_target_loc->lng=((float)ptr_wp_data[target_wp_num-1].lng)*GPS_LOCATION_SCALE;
            ptr_auto_navigation->previous_target_loc->lat=((float)ptr_wp_data[target_wp_num-1].lat)*GPS_LOCATION_SCALE;
        }
        else
        {
            ptr_auto_navigation->previous_target_loc->lng=((float)ptr_wp_data[ptr_auto_navigation->total_wp_num-1].lng)*GPS_LOCATION_SCALE;
            ptr_auto_navigation->previous_target_loc->lat=((float)ptr_wp_data[ptr_auto_navigation->total_wp_num-1].lat)*GPS_LOCATION_SCALE;
        }

        /*3. 获取 1期望course angle 2当前实际course angle 3期望heading angle 4当前实际heading angle */
        if ((ptr_gps_data->longitude != 0 )&& (ptr_gps_data->latitude != 0))
        {
            ptr_auto_navigation->command_course_angle_radian = get_command_heading_NED(ptr_auto_navigation->previous_target_loc,\
                                                                                       ptr_auto_navigation->current_loc,\
                                                                                       ptr_auto_navigation->current_target_loc);
            ptr_auto_navigation->gps_course_angle_radian=ptr_gps_data->course;

            ptr_auto_navigation->command_heading_angle_radian = get_command_heading_NED(ptr_auto_navigation->previous_target_loc,\
                                                                                        ptr_auto_navigation->current_loc,\
                                                                                        ptr_auto_navigation->current_target_loc);

            ptr_auto_navigation->gps_heading_angle_degree=((float)ptr_gps_data->yaw)*GPS_DIRECTION_INT_TO_REAL;
            ptr_auto_navigation->gps_heading_angle_radian=convert_degree_to_radian(ptr_auto_navigation->gps_heading_angle_degree);
        }
        break;
    default:
        break;
    }

    return 0;
}

static unsigned int get_next_wp_num(struct WAY_POINT *ptr_wp_data,\
                                    struct T_LOCATION *current_loc,\
                                    unsigned int current_target_wp_cnt,\
                                    unsigned int total_wp_num,\
                                    unsigned int arrive_radius)
{
	unsigned char bool_arrive_point_radius = 0;//利用是否到达该航点某半径内 判断是否到达该航点
	unsigned char bool_arrive_point = 0;//利用是否冲过 上一目标航点到当前目标航点连线的垂线 判断是否到达该航点

	struct T_LOCATION last_target;
	struct T_LOCATION specific_location;
	struct T_LOCATION *specific_loc=NULL;

	specific_loc = &specific_location;

	if(current_target_wp_cnt>=1)
	{
		last_target.lng=((float)ptr_wp_data[current_target_wp_cnt-1].lng)*GPS_LOCATION_SCALE;
		last_target.lat=((float)ptr_wp_data[current_target_wp_cnt-1].lat)*GPS_LOCATION_SCALE;
	}
	else
	{
		last_target.lng=((float)ptr_wp_data[total_wp_num-1].lng)*GPS_LOCATION_SCALE;
		last_target.lat=((float)ptr_wp_data[total_wp_num-1].lat)*GPS_LOCATION_SCALE;
	}

	specific_loc->lng = ((float)ptr_wp_data[current_target_wp_cnt].lng)*GPS_LOCATION_SCALE;
	specific_loc->lat = ((float)ptr_wp_data[current_target_wp_cnt].lat)*GPS_LOCATION_SCALE;

	bool_arrive_point_radius = arrive_specific_location_radius(current_loc, specific_loc,arrive_radius);

	//bool_arrive_point=arrive_specific_location_over_line(&last_target,current_loc,specific_loc);
	//20170325采用ned坐标系下的到达判断
	bool_arrive_point=arrive_specific_location_over_line_project_NED(&last_target,current_loc,specific_loc);

	//if (bool_arrive_point)
	//if (bool_arrive_point && bool_arrive_point_radius)
	if (bool_arrive_point || bool_arrive_point_radius)
	{
		if (current_target_wp_cnt >= (total_wp_num - 1))
		{
		    auto_navigation.previous_target_loc->lng = (float)(wp_data[current_target_wp_cnt].lng)*GPS_LOCATION_SCALE;
            auto_navigation.previous_target_loc->lat = (float)(wp_data[current_target_wp_cnt].lat)*GPS_LOCATION_SCALE;
            current_target_wp_cnt = 0;
		}
		else
		{
			auto_navigation.previous_target_loc->lng = (float)(wp_data[current_target_wp_cnt].lng)*GPS_LOCATION_SCALE;
			auto_navigation.previous_target_loc->lat = (float)(wp_data[current_target_wp_cnt].lat)*GPS_LOCATION_SCALE;
			current_target_wp_cnt++;
		}
	}

	return current_target_wp_cnt;
}

float get_command_heading_NED(struct T_LOCATION *previous_target_loc,  struct T_LOCATION *current_loc, struct T_LOCATION *target_loc)
{
    float current_to_target_radian = 0.0;//当前航点与目标航点方位角的弧度值
    float command_heading_radian = 0.0;//期望航向角heading或者说是yaw
    float cross_track_error_correct_radian = 0.0;

    current_to_target_radian = get_bearing_point_2_point_NED(current_loc, target_loc);
    current_to_target_radian=wrap_PI(current_to_target_radian);//从当前位置直接冲向目标航点，从小圈也就是小于180的方向转

    global_bool_boatpilot.current_to_target_radian=(short)current_to_target_radian*1000;
    global_bool_boatpilot.dir_target_degree=(short)(convert_radian_to_degree(current_to_target_radian)*100);/*把这个目标航向返回给实时数据*/

    cross_track_error_correct_radian = get_cross_track_error_correct_radian_NED(previous_target_loc, current_loc, target_loc);
    //printf("cross_track_error_correct_radian=%f\n",cross_track_error_correct_radian);//20170410已测试

    /*
     * 虽然经过偏航距离修正，但是还是要朝着当前位置到目标航点直接方位角的小于180度方向走
     * 例如从当前位置直接到目标航点的角度为175度
     * 如果经过偏航距离修正后，成为185度，wrap_PI函数会导致目标航向-175度，这不行，得还是朝着175的那个小圈走
     */
    //command_heading_radian = wrap_PI(command_heading_radian);/不需要这个wrap_PI函数，用了反而错误
    command_heading_radian = current_to_target_radian + cross_track_error_correct_radian;
    if(command_heading_radian>M_PI)
    {
        command_heading_radian=M_PI;
    }
    else if(command_heading_radian<-M_PI)
    {
        command_heading_radian=-M_PI;
    }

    global_bool_boatpilot.dir_nav_degree=(short)(convert_radian_to_degree(command_heading_radian)*100);
    global_bool_boatpilot.command_radian=(short)command_heading_radian*1000;

    return command_heading_radian;
}