//超声波测距传感器
#include "capture.h"

#include "led.h"
#include "timer.h"
#include "stepMotor.h"
#include "lcd.h"
#include "usart2.h"

void capture_init(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
	EXTI_InitTypeDef EXTI_InitStructure;
 	NVIC_InitTypeDef NVIC_InitStructure;
	//PB0 trig
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);	 //使能PA,PD端口时钟
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;				 //LED0-->PA.8 端口配置
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; 		 //推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;		 //IO口速度为50MHz
	GPIO_Init(GPIOB, &GPIO_InitStructure);					 //根据设定参数初始化GPIOA.8
	GPIO_ResetBits(GPIOB,GPIO_Pin_0);	
	
	
	//PB1 echo
	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_1;//PB1
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD; //PB1设置成输入，默认下拉	  
	GPIO_Init(GPIOB, &GPIO_InitStructure);//初始化GPIOA.0
	

	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO,ENABLE);//外部中断，需要使能AFIO时钟

	  
    //GPIOB.1 中断线以及中断初始化配置
  	GPIO_EXTILineConfig(GPIO_PortSourceGPIOB,GPIO_PinSource1);

  	EXTI_InitStructure.EXTI_Line=EXTI_Line1;
  	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;	
  	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;//上升沿，下降沿触发
  	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
  	EXTI_Init(&EXTI_InitStructure);	 	//根据EXTI_InitStruct中指定的参数初始化外设EXTI寄存器
	
	NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;			//使能按键所在的外部中断通道
  	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x02;	//抢占优先级2 
  	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x02;					//子优先级1
  	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;								//使能外部中断通道
  	NVIC_Init(&NVIC_InitStructure); //根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存�
	
	TIM3_Int_Init(0xFFFF,71);//初始化定时器 频率为1M，1us计数一次
}

int state_dis = 0;//没有触发
u16 time_cnt = 0;
extern int is_lock_open;
extern char self_num[2];

int have_car = 0; //判断车是否到来
int cal_ts = 0; //取几次计算距离的结果，求平均值
float cal_sum = 0, cal_average = 0;

int car_leave_confirm = 0, car_come_confirm = 0;
u16 get_dis(u16 ts)
{
	u16 dis = ts / 1000000.0 * 340 * 1000 / 2;
	return dis;
}

//开始进行一次测距，测量结果在外部中断中获取
void start_cal_distance(void) 
{
	trig = 1;
	delay_us(45);
	trig = 0;

	state_dis = 1;//触发
}

char show[30] = {0};
unsigned char state_af[4] = {0x5a,0,1,0x53}; //向协调器发送反馈状态

int dis_come = 500, dis_leave = 800;//判定车辆到来或者离开后的检测距离

//外部中断，计算echo引脚高电平的时间
void EXTI1_IRQHandler(void)
{
	if(echo == 1 && state_dis == 1) //捕获到上升沿
	{	  
		TIM3->CNT = 0;
		TIM_Cmd(TIM3, ENABLE);//开启定时器
		state_dis = 2;//准备捕获下降沿
	}
	if(echo == 0 && state_dis == 2)//捕获到下降沿
	{	  
		time_cnt = TIM3->CNT;//高电平的持续时间
		
		state_dis = 0;//恢复到初始化状态
		TIM_Cmd(TIM3, DISABLE);//关闭定时器
		
		//printf("dis：%.1f cm\r\n", get_dis(time_cnt) / 10.0);
		
		cal_ts++;
		cal_sum += get_dis(time_cnt);
		
		if (cal_ts == 10) //累计10次求平均值		 
		{
			cal_ts = 0;
			cal_average = cal_sum / 10.0;//10次
			cal_sum = 0;
			
			//printf("\r\ndis: %.1f cm\r\n", cal_average / 10.0);
			sprintf(show, "dis: %.1f cm  ", cal_average / 10.0);
			LCD_P8x16Str(0,2,show);
			
			//is_lock_open == 0 && have_car == 0 && 
			if (is_lock_open == 0 && have_car == 0 && cal_average < dis_come) // < 1m
			{
				car_come_confirm++;
				if (car_come_confirm == 3) //3次确认
				{
					car_come_confirm = 0;
					have_car = 1;//认为有车来
					//printf("\r\na car come in\r\n\r\n");
					
					LCD_P8x16Str(0,6,"car come  ");
				}
				
			} //is_lock_open == 0 && have_car == 1 && 
			else if (is_lock_open == 0 && have_car == 1 && cal_average > dis_leave) // > 1.2m
			{
				car_leave_confirm++;
				if (car_leave_confirm == 3) //3次确认
				{
					car_leave_confirm = 0;
					have_car = 0;//认为车离开了
					is_lock_open = 1;//车位自动打开
					//printf("\r\na car leave\r\n\r\n");
					
					LCD_P8x16Str(0,6,"car leave  ");
					LCD_P8x16Str(0,4,"lock open ");
					motor_run(0, 90);//车位锁打开
					
					//向协调器 汇报车辆离开 车位锁开启
					state_af[1] = self_num[0]; //更新车位编号
					state_af[2] = self_num[1];
					USART2_send_data(state_af, 4);
				}
				
			}
			
		}
	}
	EXTI_ClearITPendingBit(EXTI_Line1);  //清除EXTI0线路挂起位
}

void TIM3_IRQHandler(void)   //TIM3中断
{
	if(TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET) //检查指定的TIM中断发生与否:TIM 中断源 
	{
		TIM_ClearITPendingBit(TIM3, TIM_IT_Update );  //清除TIMx的中断待处理位:TIM 中断源 
		state_dis = 0;//恢复到初始化状态
		//printf("unkown\r\n");//长时间未收到反射波，长度无法测量
		LCD_P8x16Str(0,2,"dis:unkown");
		TIM_Cmd(TIM3, DISABLE);//关闭定时器
	}
}
