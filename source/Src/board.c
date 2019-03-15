#include "board.h"
#include "math.h"
#include "usart.h"
#include "adc.h"
#include "as5047.h"

#define LOW_CLOSE       IO_Low
#define LOW_OPEN        (IO_State)!LOW_CLOSE

typedef enum{
  FORWARD=0x01,
  BACKWARD=-0x01
}MAG_Direction;

Phase_State AB={A,B,0};
Phase_State AC={A,C,1};
Phase_State BC={B,C,2};
Phase_State BA={B,A,3};
Phase_State CA={C,A,4};
Phase_State CB={C,B,5};

Mode Board_Mode=TEST;
MAG_Direction Direction=BACKWARD;

int Now_Phase_Index=0;

//这两个，选择一个使用，具体使用哪一个，与ABC三条线的接法有关
//当对Phase_Const中，下标从0-5转一圈，如果位置增大，那么就使用Phase_Const
//否则就应该使用Phase_Const_Reverse
Phase_State * Phase_Const[6]={&AB,&AC,&BC,&BA,&CA,&CB};      
Phase_State * Phase_Const_Reverse[6]={&CA,&BA,&BC,&AC,&AB,&CB};  
Phase_State ** Phase_Table_Using=Phase_Const; //当前使用的换向表

int Test_Table_Cnt=0;

float Motor_Duty=TEST_TABLE_SPEED;       //电机占空比

uint16_t Start_Position=9713;       //未修正的起点位置。修正方式为Start_Position_Raw +/-  0.5*MIN_ANGLE

uint16_t Mag_Position=0;
int Phase_Change_Cnt=0;//换向计数，仅用于磁编码器的无刷电机

int Phase_Open_Cnt=0;// 相开启时间计数，防止某一相导通太长时间导致电流过大
                     // 1ms 增加一次，在systick中断中增加

void Set_Motor_Duty(float duty){ //设置电机占空比
  if(duty<0){
    Direction=FORWARD;
  }else{
    Direction=BACKWARD;
  }

  Motor_Duty=fabs(duty);
}

void Phase_Change(Phase_State *target,float speed){
  Close_Phases();
  Phase_Open_Cnt=0;
  Set_Phase_Low_State(target->Low,LOW_OPEN);
  Set_Phase_High_Speed(target->High,speed);     //开启目标高低桥  
  //Mointor_Change(target);
  Now_Phase_Index=target->index;
}

void Set_Phase_Low_State(Phase phase,IO_State state){
  switch(phase){
  case A:
    Set_AL_State(state);
    break;
  case B:
    Set_BL_State(state);
    break;
  case C:
    Set_CL_State(state);
    break;
  }
}


void Set_Phase_High_Speed(Phase phase,float speed){
  speed=speed<0?-speed:speed;
  speed=speed>95?95:speed;
  switch(phase){
  case A:
    Set_AH_Speed(speed);
    break;
  case B:
    Set_BH_Speed(speed);
    break;
  case C:
    Set_CH_Speed(speed);
    break;
  }
}

void Close_Phases(){
  Set_Phase_Low_State(A,IO_Low);
  Set_Phase_Low_State(B,IO_Low);
  Set_Phase_Low_State(C,IO_Low);
  Set_Phase_High_Speed(A,0);
  Set_Phase_High_Speed(B,0);
  Set_Phase_High_Speed(C,0);
  
  //uprintf("ok,close all phases\r\n");
}
       



void Set_To_CB_Positon(){  
  //将转子定位CB位置，为什么是CB?因为他是换向表最后一个位置，如果换向表不同
  //则要定位到其他位置（也是换向表的最后一个）
  for(int i=0;i<=10;++i){
    Phase_Change(Phase_Const[5],TEST_TABLE_SPEED);
    HAL_Delay(5);
    Close_Phases();
    HAL_Delay(5);
  }
}

void Get_Start_Position(){
  Set_To_CB_Positon();
  HAL_Delay(1000);
  uprintf("start_position=%d\r\n",Get_Position());
}
void Rotate_Test(){
  //测试板子，以选择正确的换向表
  //换向表错误的话，板子会来回振动，无法正常运转
  uint16_t last_position=0;
  uint16_t position=0;
  int larger_cnt=0;

  Set_To_CB_Positon();
  
  HAL_Delay(1000);
  last_position=Get_Position();
  for(int j=0;j<3;++j){
    for(int i=0;i<6;++i){
      Phase_Change(Phase_Const[i],TEST_TABLE_SPEED);
      HAL_Delay(5);
      Close_Phases();
      position=Get_Position();
      if(position-last_position>0){
        larger_cnt++;
      }else{
        larger_cnt--;
      }
      last_position=position;
    }
  }
  
  if(larger_cnt>0){
    Phase_Table_Using=Phase_Const;
  }else{
    Phase_Table_Using=Phase_Const_Reverse;
  }
  
  uprintf("larger_cnt:%d\r\n",larger_cnt);
}

void Mag_Brushless_Mointor(uint16_t mag_position){
  uint32_t fixed_start_position=0;
  uint32_t position=(uint32_t)mag_position;
  
  int temp=0;
  fixed_start_position=Start_Position+Direction*HALF_MIN_ANGLE;
  if(position<fixed_start_position){
    position+=MAG_ENCODER_LINES;
  }
  if(Direction==FORWARD){
    temp=(int)((float)(position-(fixed_start_position))/MIN_ANGLE)+1;
  }else{
    temp=(int)((float)(position-(fixed_start_position))/MIN_ANGLE)-3;
  }
  // 假设电机运转到换向点，实际上有两个方向，往前一格，和往后一格
  // 比如运行顺序为 AB AC BC
  // 假设电机运行到AC，此时它可以往AB，也可以往BC，两种选择对应正转和反转
  // 如果不加一个Direction呢？那么电机会往起始点(start_position)收敛，之后再也不动，画图易证
  if(temp<0){
    temp+=6;
  }
  temp%=6;
  
  if(Board_Mode!=NORMAL){
    return ;
  }
  
  if(temp!=Phase_Change_Cnt){
    Phase_Change_Cnt=temp;   //判断是否到达下一相，如果是，换相
    Phase_Change(Phase_Table_Using[Phase_Change_Cnt],Motor_Duty);
    uprintf("%d\r\n",Phase_Change_Cnt);
  }
}
