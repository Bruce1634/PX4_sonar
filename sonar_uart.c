// ���ڳ�����������
/* ����ϵͳ�豸�봮��ӳ���ϵ
	NuttX UART	Pixhawk UART
	/dev/ttyS0	IO DEBUG(RX ONLY)
	/dev/ttyS1	TELEM1(USART2)
	/dev/ttyS2	TELEM2(USART3)
	/dev/ttyS3	GPS(UART4)
	/dev/ttyS4	N/A(UART5, IO link)
	/dev/ttyS5	SERIAL5(UART7,NSH Console Only)
	/dev/ttyS6	SERIAL4(UART8)
*/

#include <px4_config.h>
#include <px4_defines.h>
#include <px4_tasks.h>
#include <px4_posix.h>
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <math.h>
#include <float.h>

#include <arch/board/board.h>
#include <uORB/uORB.h>
#include <uORB/topics/sensor_combined.h>
#include <uORB/topics/manual_control_setpoint.h>


// ���������������嵽TELEM2
#define SONAR_DEV "/dev/ttyS2"
#define SONAR_ERR 0xFFFF
#define AGAINST	150.0f
#define constrain(val, min, max) (val < min) ? min : ((val > max) ? max : val)

__EXPORT int sonar_uart_main(int argc, char *argv[]);

// ��ʼ�����ڣ�dev: �豸�ţ�baud: ���ڲ�����
static int sonar_uart_init(char *dev, unsigned int baud)
{
	int uart = open(dev, O_RDWR | O_NONBLOCK | O_NOCTTY); //

	if (uart < 0) {
		printf("ERROR opening %s, aborting..\n", dev);
		return uart;
	}

	struct termios uart_config;

	int termios_state = 0;

	int ret;

	// ��ȡ���ڵ�����
	if ((termios_state = tcgetattr(uart, &uart_config)) < 0) {
		printf("ERROR getting termios config for UART: %d\n", termios_state);
		ret = termios_state;
		goto cleanup;
	}

	// �޸�uart_config�ṹ��
	if (cfsetispeed(&uart_config, baud) < 0 || cfsetospeed(&uart_config, baud) < 0) {
		printf("ERROR setting termios config for UART: %d\n", termios_state);
		ret = ERROR;
		goto cleanup;
	}
	// ʹ��uart_config���ò�����
	if ((termios_state = tcsetattr(uart, TCSANOW, &uart_config)) < 0) {
		printf("ERROR setting termios config for UART\n");
		ret = termios_state;
		goto cleanup;
	} 

	printf("%s opened, baud rate: %d\n", dev, baud);

	return uart;
cleanup:
	close(uart);
	return ret;

}
static uint16_t dist;

// ����һ���̺߳������涨��ʽΪ void *fun(void *)
static void *sonar_read_loop(void *arg)
{
	int sonar_dev = *((int *)arg);
	uint8_t start = 0x55;
	uint8_t data[2];
	int i;

	// ÿдһ��0x55����������һ�ξ���
	for(i=0; i<100; i++){
		write(sonar_dev, &start, 1);
		usleep(50000);
		read(sonar_dev, data, 2);
		dist = data[0]<<8 | data[1];
		if(dist < 20 || dist > 4500){	// �����˷�Χ��ʾ����
			dist = SONAR_ERR;
		}
		//printf("distance: %d\n", dist);
		usleep(50000);
	}
	
	close(sonar_dev);

	return NULL;
}




// ʹ�ô��ڷ������ݣ�ע�⣬�����һ��ģ��Ļ�����ں���������Ϊ: �ļ���_main
int sonar_uart_main(int argc, char *argv[])
{
	// ��������󣬺������Ѿ����
	int sonar_dev = sonar_uart_init(SONAR_DEV, B9600);
	struct manual_control_setpoint_s manual = {};

	// ��ʼ��ȡ������
	pthread_t receive_thread;
	pthread_create(&receive_thread, NULL, sonar_read_loop, &sonar_dev);

	printf("thread start\n");
	//�ȴ��߳���ֹ
	//pthread_join(receive_thread, NULL);

	// �����ֶ������趨����Ϣ������Ƶ��5Hz
	int man_sp_sub = orb_subscribe(ORB_ID(manual_control_setpoint));
	orb_set_interval(man_sp_sub, 100);
	px4_pollfd_struct_t fd = { 
		.fd = man_sp_sub,
		.events = POLLIN
	};
	
	for(int i=0; i<100; i++){
		// �ȴ���Ϣ����
		int poll_ret = px4_poll(&fd, 1, 1000);
		if (poll_ret == 0) {
			/* this means none of our providers is giving us data */
			PX4_ERR("Got no data within a second");
		}else if(poll_ret > 0){
			orb_copy(ORB_ID(manual_control_setpoint), man_sp_sub, &manual);
			// ���ݲ����ľ����ң������������ݽ��д���
			// ����û�п��Ƿɻ����ٶȣ�ֻ���ڻ����������������Ч
			if(dist == SONAR_ERR){
				continue;
			}
			manual.x -= AGAINST/dist;
			manual.x = constrain(manual.x, -1.f, 1.f);
		}
		
		PX4_INFO("\t%dmm\t%8.4f\t%8.4f\t%8.4f\t%8.4f",
					 dist, (double)manual.x, (double)manual.y, 
					 (double)manual.z, (double)manual.r);
	}

	printf("thread terminate\n");

	return 0;
}




