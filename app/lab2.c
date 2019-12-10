#include "includes.h"

#define F_CPu    16000000uL    // CPu frequency = 16 Mhz

#include <avr/io.h>
#include <util/delay.h>
//#include <avr/interrupt.h>

#define  TASK_STK_SIZE  OS_TASK_DEF_STK_SIZE
#define CDS_VALUE 871 // 10 LUX의 밝기 값
#define ON 1
#define OFF 0

OS_STK ControlTaskStk[TASK_STK_SIZE];
OS_STK LedPlusTaskStk[TASK_STK_SIZE];
OS_STK LedMinusTaskStk[TASK_STK_SIZE];
OS_STK FndPlusTaskStk[TASK_STK_SIZE];
OS_STK FndMinusTaskStk[TASK_STK_SIZE];
OS_STK ShowFndTaskStk[TASK_STK_SIZE];

void ControlTask(void* data);
void LedPlusTask(void* data);
void LedMinusTask(void* data);
void FndPlusTask(void* data);
void FndMinusTask(void* data);
void ShowFndTask(void* data);

OS_FLAG_GRP* flag;

OS_EVENT* sem_buz;
OS_EVENT* sem_plant;
OS_EVENT* sem_random;


unsigned char SEL[4] = { 0x08, 0x04, 0x02, 0x01 };
unsigned char Plant[4] = { 0x4f, 0x63,0x5c, 0x46 };
unsigned char WIN[4] = { 0x3e, 0x3e, 0x06, 0x37 };
unsigned char LOSE[4] = { 0x38, 0x3f, 0x6d, 0x79 };
unsigned int Sound[] = { 17,97,17,66,97,137,114,105,97,87}; // default & 도 & 솔

volatile int time = 1;
volatile int state = OFF;
volatile int soundMode = -1;
volatile int plant_cnt = 0;

ISR(TIMER2_OVF_vect) {
	int err;

	if (state == ON) {
		PORTB = 0x00;
		state = OFF;
	}
	else {
		if (soundMode < 0) PORTB = 0x00;
		else PORTB = 0x10;
		state = ON;
	}
	TCNT2 = Sound[soundMode];
}

// INT4번 벡터 사용(스위치)
// 스위치가 눌리면 LED를 한 칸 올리도록 신호를 보냄(0x08)
ISR(INT4_vect) {
	int err;
	OSFlagPost(flag, 0x08, OS_FLAG_SET, &err);
}

// ---------------------------- ADC 사용 ----------------------------
void init_adc() {
	ADMUX = 0x00;
	// REFS(1:0) AREF (+5V 기준전압 사용),
	// ADLAR = 0 (값 오른쪽으로 정렬) ,
	// MUX(4:0) = 00000 (ADC0 사용, 단극 입력)
	ADCSRA = 0x87;
	// ADEN =1 (ADC 사용),
	// ADFR = 0 (single conversion 모드),
	// ADPS(2:0) = 111 프리스케일러 128분주
}

unsigned short read_adc() {
	unsigned char adc_low, adc_high;
	unsigned short value;
	ADCSRA |= 0x40; // ADC start conversion Setting 신호 보냄
	while ((ADCSRA & 0x10) != 0x10); // ADC 변환 완료 검사
	adc_low = ADCL; // 변환된 값 읽어오기
	adc_high = ADCH; // 변환된 값 읽어오기
	value = (adc_high << 8) | adc_low; // high 값은 <<8하여 원래 값으로 변환
	return value;
}
// ------------------------------------------------------------------

// ------------------------ RESET 함수 -------------------------------
void Reset() {
	int err;
	// Timer reset
	time = 1;
	plant_cnt = 0;

	// LED
	DDRA = 0xff;
	PORTA = 0x0f;

	// FND
	DDRC = 0xff;
	DDRG = 0x0f;
}
// ------------------------------------------------------------------

int main(void)
{
	int err;

	OSInit();
	OS_ENTER_CRITICAL();

	TCCR0 = 0x07;
	TIMSK = _BV(TOIE0) | _BV(TOIE2);
	TCNT0 = 256 - (CPU_CLOCK_HZ / OS_TICKS_PER_SEC / 1024);

	Reset();

	// 버저
	DDRB = 0x10; // 버저 출력 설정 PB4
	TCCR2 = 0x03; // 32분주
 //    TIMSK = 0x01; // timer 0  overflow interrupt 설정
	TCNT2 = 17; // 도

	// 스위치
	DDRE = 0xef; //0b11101111 4,5번 인터럽트 하강엣지 트리거 설정
	EICRB = 0x02;  // INT 4 하강 엣지 사용.
	EIMSK = 0x10;  // 4번 인터럽트 사용
	SREG |= 1 << 7; //전역 인터럽트 설정

	// 광센서
	init_adc();
	OS_EXIT_CRITICAL();

	sem_buz = OSSemCreate(1);
	sem_plant = OSSemCreate(1);
	sem_random = OSSemCreate(1);

	srand(0);

	OSTaskCreate(LedPlusTask, (void*)0, (void*)& LedPlusTaskStk[TASK_STK_SIZE - 1], 0);
	OSTaskCreate(LedMinusTask, (void*)0, (void*)& LedMinusTaskStk[TASK_STK_SIZE - 1], 1);
	OSTaskCreate(FndPlusTask, (void*)0, (void*)& FndPlusTaskStk[TASK_STK_SIZE - 1], 2);
	OSTaskCreate(FndMinusTask, (void*)0, (void*)& FndMinusTaskStk[TASK_STK_SIZE - 1], 3);
	OSTaskCreate(ControlTask, (void*)0, (void*)& ControlTaskStk[TASK_STK_SIZE - 1], 4);
	OSTaskCreate(ShowFndTask, (void*)0, (void*)& ShowFndTaskStk[TASK_STK_SIZE - 1], 6);

	// 플래그 초기화
	flag = OSFlagCreate(0x00, &err);

	OSStart();



	return 0;
}

// 타이머를 제어(1초)
// 일정 시간이 될때마다 LED와 FND에게 플래그로 전달
// 0x01 FLAG : 식물 한칸 증가
// 0x02 FLAG : LED 한칸 증가
// 0x04 FLAG : 식물 한칸 감소
void ControlTask(void* data) {
	int err, i, light, cnt;

	while (1) {
		// 1초 타이머
		OSTimeDlyHMSM(0, 0, 1, 0);

		// 어두우면 light값 증가
		 // 어두우면 fnd가 깜빡거림
		if (read_adc() < CDS_VALUE)
			light = 5;
		else
			light = 0;

		// 10초마다 식물이 자라는 신호 보냄(0x01)
		// 어두우면 20초마다 한 번씩 신호를 보냄

		if (time % (5 + light) == 0) {
			// 물이 다 떨어진 상태에서는 안보냄
			if (PORTA != 0x00 || PORTA != 0xFF)
				OSFlagPost(flag, 0x01, OS_FLAG_SET, &err);
		}

		// 5초마다
		// 물의 양을 감소시키는 신호를 보냄(0x02)
		// 물이 다 떨어지거나 꽉차면 되면 식물이 줄어듬(0x04)
		OSSemPend(sem_plant, 0, &err);
		cnt = plant_cnt;
		OSSemPost(sem_plant);
		if (time % 2 == 0) {
			if (PORTA == 0x00 && cnt > 0)
				OSFlagPost(flag, 0x04, OS_FLAG_SET, &err);
			else if (PORTA == 0xFF && cnt > 0) {
				OSFlagPost(flag, 0x04, OS_FLAG_SET, &err);
				OSFlagPost(flag, 0x02, OS_FLAG_SET, &err);
			}
			else {
				OSFlagPost(flag, 0x02, OS_FLAG_SET, &err);
			}
		}

		// 식물이 모두 자라거나
		// 식물과 물이 모두 떨어지게 되면
		// Game End
		if (cnt >= 4 || (cnt <= 0 && PORTA == 0x00) || (cnt <= 0 && PORTA == 0xFF)) {
			OSFlagPost(flag, 0x10, OS_FLAG_SET, &err);
			if (cnt <= 0) {
				for (i = 6; i <= 9; i++) {
					OSSemPend(sem_buz, 0, &err);
					soundMode = i;
					OSSemPost(sem_buz);
					OSTimeDlyHMSM(0, 0, 0, 200);
				}
				OSSemPend(sem_buz, 0, &err);
				soundMode = -1;
				OSSemPost(sem_buz);
			}
			else if(cnt >= 4)
			{
				for (i = 2; i <= 5; i++) {
					OSSemPend(sem_buz, 0, &err);
					soundMode = i;
					OSSemPost(sem_buz);
					OSTimeDlyHMSM(0, 0, 0, 200);
				}
				OSSemPend(sem_buz, 0, &err);
				soundMode = -1;
				OSSemPost(sem_buz);
			}
			OSTimeDlyHMSM(0, 0, 3, 0);
			// 3초간 문구 표시후 flag clear
			OSFlagPost(flag, 0xff, OS_FLAG_CLR, &err);

			// 설정을 리셋
			Reset();
		}

		time = time + 1;
	}
}

void LedPlusTask(void* data) {
	int err,i,max;

	while (1) {
		// 스위치를 누르면
		OSFlagPend(flag, 0x08, OS_FLAG_WAIT_SET_ANY + OS_FLAG_CONSUME, 0, &err);
		max = rand();
		// LED를 왼쪽으로 한 비트씩 증가
		if (read_adc() < CDS_VALUE)
		{
			for (i = 0; i < max % 7 + 1; i++)
				if(PORTA!=0xFF) PORTA = (PORTA << 1) + 1;
		}
		else
		{
			for (i = 0; i < max %5 + 1; i++)
				if (PORTA != 0xFF) PORTA = (PORTA << 1) + 1;
		}
	}
}

void LedMinusTask(void* data) {
	int err;

	while (1) {
		// ControlTask가 플래그를 설정해 주면
		OSFlagPend(flag, 0x02, OS_FLAG_WAIT_SET_ANY + OS_FLAG_CONSUME, 0, &err);

		// LED가 한 칸씩 줄어듬
		PORTA = PORTA >> 1;
	}
}


void FndPlusTask(void* data) {
	int err, i;

	while (1) {
		// ControlTask가 플래그를 설정해 주면
		OSFlagPend(flag, 0x01, OS_FLAG_WAIT_SET_ANY + OS_FLAG_CONSUME, 0, &err);

		OSSemPend(sem_plant, 0, &err);
		if (plant_cnt < 4)
			plant_cnt = plant_cnt + 1;
		OSSemPost(sem_plant);
		// 식물이 한 칸 성장

		// 식물이 모두 성장하면 높은 음의 소리 (솔)
		OSSemPend(sem_buz, 0, &err);
		soundMode = 1;
		OSTimeDlyHMSM(0, 0, 0, 100);
		soundMode = -1;
		OSSemPost(sem_buz);
	}
}

// Led가 꽉 찼을때 일정 시간이 지날 때 마다 식물이 한 칸씩 줄어듬
void FndMinusTask(void* data) {
	int err, i;

	while (1) {
		OSFlagPend(flag, 0x04, OS_FLAG_WAIT_SET_ANY + OS_FLAG_CONSUME, 0, &err);
		OSSemPend(sem_plant, 0, &err);
		if (plant_cnt > 0)
			plant_cnt = plant_cnt - 1;
		OSSemPost(sem_plant);
		// 식물이 한 칸 줄어들면 낮은 음의 소리 (도)
		OSSemPend(sem_buz, 0, &err);
		soundMode = 0;
		OSTimeDlyHMSM(0, 0, 0, 100);
		soundMode = -1;
		OSSemPost(sem_buz);
	}
}

// FND display
void ShowFndTask(void* data) {
	int i, err, cnt;

	while (1) {
		// cnt는 빈칸의 개수
		OSSemPend(sem_plant, 0, &err);
		cnt = 4 - plant_cnt;
		OSSemPost(sem_plant);

		// Game End 플래그를 받은 경우
		if (OSFlagAccept(flag, 0x10, OS_FLAG_WAIT_SET_ANY, &err) > 0) {
			// fnd Max
			// WIN
			if (cnt == 0) {
				for (i = 0; i < 4; i++) {
					PORTC = WIN[i];
					PORTG = SEL[i];
					OSTimeDlyHMSM(0, 0, 0, 3);
				}
			}
			// fnd Min
			// LOSE
			else if (cnt == 4) {
				for (i = 0; i < 4; i++) {
					PORTC = LOSE[i];
					PORTG = SEL[i];
					OSTimeDlyHMSM(0, 0, 0, 3);
				}
			}
		}
		else {
			// 식물이 있을 경우
			for (i = 0; i < 4; i++) {
				if (cnt > 0)
					PORTC = 0x00;
				else
					PORTC = Plant[i];
				PORTG = SEL[i];
				cnt = cnt - 1;

				// 어두우면 깜빡거림
				if (read_adc() < CDS_VALUE)
					OSTimeDlyHMSM(0, 0, 0, 200);
				else
					OSTimeDlyHMSM(0, 0, 0, 3);
			}
		}
	}
}