#include "includes.h"

#define F_CPu    16000000uL    // CPu frequency = 16 Mhz

#include <avr/io.h>
#include <util/delay.h>
//#include <avr/interrupt.h>

#define  TASK_STK_SIZE  OS_TASK_DEF_STK_SIZE
#define CDS_VALUE 871 // 10 LUX�� ��� ��
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
unsigned int Sound[] = { 17,97,17,66,97,137,114,105,97,87}; // default & �� & ��

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

// INT4�� ���� ���(����ġ)
// ����ġ�� ������ LED�� �� ĭ �ø����� ��ȣ�� ����(0x08)
ISR(INT4_vect) {
	int err;
	OSFlagPost(flag, 0x08, OS_FLAG_SET, &err);
}

// ---------------------------- ADC ��� ----------------------------
void init_adc() {
	ADMUX = 0x00;
	// REFS(1:0) AREF (+5V �������� ���),
	// ADLAR = 0 (�� ���������� ����) ,
	// MUX(4:0) = 00000 (ADC0 ���, �ܱ� �Է�)
	ADCSRA = 0x87;
	// ADEN =1 (ADC ���),
	// ADFR = 0 (single conversion ���),
	// ADPS(2:0) = 111 ���������Ϸ� 128����
}

unsigned short read_adc() {
	unsigned char adc_low, adc_high;
	unsigned short value;
	ADCSRA |= 0x40; // ADC start conversion Setting ��ȣ ����
	while ((ADCSRA & 0x10) != 0x10); // ADC ��ȯ �Ϸ� �˻�
	adc_low = ADCL; // ��ȯ�� �� �о����
	adc_high = ADCH; // ��ȯ�� �� �о����
	value = (adc_high << 8) | adc_low; // high ���� <<8�Ͽ� ���� ������ ��ȯ
	return value;
}
// ------------------------------------------------------------------

// ------------------------ RESET �Լ� -------------------------------
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

	// ����
	DDRB = 0x10; // ���� ��� ���� PB4
	TCCR2 = 0x03; // 32����
 //    TIMSK = 0x01; // timer 0  overflow interrupt ����
	TCNT2 = 17; // ��

	// ����ġ
	DDRE = 0xef; //0b11101111 4,5�� ���ͷ�Ʈ �ϰ����� Ʈ���� ����
	EICRB = 0x02;  // INT 4 �ϰ� ���� ���.
	EIMSK = 0x10;  // 4�� ���ͷ�Ʈ ���
	SREG |= 1 << 7; //���� ���ͷ�Ʈ ����

	// ������
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

	// �÷��� �ʱ�ȭ
	flag = OSFlagCreate(0x00, &err);

	OSStart();



	return 0;
}

// Ÿ�̸Ӹ� ����(1��)
// ���� �ð��� �ɶ����� LED�� FND���� �÷��׷� ����
// 0x01 FLAG : �Ĺ� ��ĭ ����
// 0x02 FLAG : LED ��ĭ ����
// 0x04 FLAG : �Ĺ� ��ĭ ����
void ControlTask(void* data) {
	int err, i, light, cnt;

	while (1) {
		// 1�� Ÿ�̸�
		OSTimeDlyHMSM(0, 0, 1, 0);

		// ��ο�� light�� ����
		 // ��ο�� fnd�� �����Ÿ�
		if (read_adc() < CDS_VALUE)
			light = 5;
		else
			light = 0;

		// 10�ʸ��� �Ĺ��� �ڶ�� ��ȣ ����(0x01)
		// ��ο�� 20�ʸ��� �� ���� ��ȣ�� ����

		if (time % (5 + light) == 0) {
			// ���� �� ������ ���¿����� �Ⱥ���
			if (PORTA != 0x00 || PORTA != 0xFF)
				OSFlagPost(flag, 0x01, OS_FLAG_SET, &err);
		}

		// 5�ʸ���
		// ���� ���� ���ҽ�Ű�� ��ȣ�� ����(0x02)
		// ���� �� �������ų� ������ �Ǹ� �Ĺ��� �پ��(0x04)
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

		// �Ĺ��� ��� �ڶ�ų�
		// �Ĺ��� ���� ��� �������� �Ǹ�
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
			// 3�ʰ� ���� ǥ���� flag clear
			OSFlagPost(flag, 0xff, OS_FLAG_CLR, &err);

			// ������ ����
			Reset();
		}

		time = time + 1;
	}
}

void LedPlusTask(void* data) {
	int err,i,max;

	while (1) {
		// ����ġ�� ������
		OSFlagPend(flag, 0x08, OS_FLAG_WAIT_SET_ANY + OS_FLAG_CONSUME, 0, &err);
		max = rand();
		// LED�� �������� �� ��Ʈ�� ����
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
		// ControlTask�� �÷��׸� ������ �ָ�
		OSFlagPend(flag, 0x02, OS_FLAG_WAIT_SET_ANY + OS_FLAG_CONSUME, 0, &err);

		// LED�� �� ĭ�� �پ��
		PORTA = PORTA >> 1;
	}
}


void FndPlusTask(void* data) {
	int err, i;

	while (1) {
		// ControlTask�� �÷��׸� ������ �ָ�
		OSFlagPend(flag, 0x01, OS_FLAG_WAIT_SET_ANY + OS_FLAG_CONSUME, 0, &err);

		OSSemPend(sem_plant, 0, &err);
		if (plant_cnt < 4)
			plant_cnt = plant_cnt + 1;
		OSSemPost(sem_plant);
		// �Ĺ��� �� ĭ ����

		// �Ĺ��� ��� �����ϸ� ���� ���� �Ҹ� (��)
		OSSemPend(sem_buz, 0, &err);
		soundMode = 1;
		OSTimeDlyHMSM(0, 0, 0, 100);
		soundMode = -1;
		OSSemPost(sem_buz);
	}
}

// Led�� �� á���� ���� �ð��� ���� �� ���� �Ĺ��� �� ĭ�� �پ��
void FndMinusTask(void* data) {
	int err, i;

	while (1) {
		OSFlagPend(flag, 0x04, OS_FLAG_WAIT_SET_ANY + OS_FLAG_CONSUME, 0, &err);
		OSSemPend(sem_plant, 0, &err);
		if (plant_cnt > 0)
			plant_cnt = plant_cnt - 1;
		OSSemPost(sem_plant);
		// �Ĺ��� �� ĭ �پ��� ���� ���� �Ҹ� (��)
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
		// cnt�� ��ĭ�� ����
		OSSemPend(sem_plant, 0, &err);
		cnt = 4 - plant_cnt;
		OSSemPost(sem_plant);

		// Game End �÷��׸� ���� ���
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
			// �Ĺ��� ���� ���
			for (i = 0; i < 4; i++) {
				if (cnt > 0)
					PORTC = 0x00;
				else
					PORTC = Plant[i];
				PORTG = SEL[i];
				cnt = cnt - 1;

				// ��ο�� �����Ÿ�
				if (read_adc() < CDS_VALUE)
					OSTimeDlyHMSM(0, 0, 0, 200);
				else
					OSTimeDlyHMSM(0, 0, 0, 3);
			}
		}
	}
}