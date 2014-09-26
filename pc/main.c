#include <sys/time.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

#define SERIALPORT "/dev/cuaU0"

//Buffers UART
unsigned char UART0_buffer_state = 0;
#define UART0_buffer_size (4*sizeof(int)+2)
unsigned char UART0_buffer[UART0_buffer_size];
unsigned char *UART0_buffer_pointer = UART0_buffer;

#define BUTTON_CMD_VOL_PLUS       0
#define BUTTON_CMD_VOL_MINUS      1
#define BUTTON_CMD_TOP_1          2
#define BUTTON_CMD_TOP_2          3
#define BUTTON_CMD_BOTTOM         4
#define BUTTON_CMD_WHEEL_DOWN     5
#define BUTTON_CMD_WHEEL_UP       6
#define BUTTON_CMD_VOL_PLUSMINUS  8

void handle_button_cmd(unsigned char button_cmd) {
  //printf("cmd %d\n", button_cmd);
  switch(button_cmd) {
    case BUTTON_CMD_VOL_PLUS:
      printf("BUTTON_CMD_VOL_PLUS\n");
      break;
    case BUTTON_CMD_VOL_MINUS:
      printf("BUTTON_CMD_VOL_MINUS\n");
      break;
    case BUTTON_CMD_TOP_1:
      printf("BUTTON_CMD_TOP_1\n");
      break;
    case BUTTON_CMD_TOP_2:
      system("/root/nuclearmuse/playpause.sh");
      printf("BUTTON_CMD_TOP_2\n");
      break;
    case BUTTON_CMD_BOTTOM:
      system("/usr/local/bin/mpc next");
      printf("BUTTON_CMD_BOTTOM\n");
      break;
    case BUTTON_CMD_WHEEL_DOWN:
      system("/usr/local/bin/mpc next");
      printf("BUTTON_CMD_WHEEL_DOWN\n");
      break;
    case BUTTON_CMD_WHEEL_UP:
      system("/usr/local/bin/mpc prev");
      printf("BUTTON_CMD_WHEEL_UP\n");
      break;
    case BUTTON_CMD_VOL_PLUSMINUS:
      printf("BUTTON_CMD_VOL_PLUSMINUS\n");
      break;
    default:
      printf("Unknown command: 0x%x\n", button_cmd);
      break;
  }
}

void handle_incoming_char(unsigned char c) {
  //printf("char: 0x%.2x state %d\n", c, UART0_buffer_state);
  switch(UART0_buffer_state) {
  case 1:
      //On reçoit les données
      *UART0_buffer_pointer = c;
      UART0_buffer_pointer++;
      if (UART0_buffer_pointer - UART0_buffer >= 1) {
          UART0_buffer_state = 0;
          unsigned char button_cmd = UART0_buffer[0];
          handle_button_cmd(button_cmd);
          UART0_buffer_pointer = UART0_buffer;
      }
      break;
  default:
      //On reçoit un int de start ou une commande simple
      if ((c == 0xAB) && (UART0_buffer_pointer - UART0_buffer == 1)) {
          //Pong
          UART0_buffer_pointer = UART0_buffer;
      }
      else if (c != 0xAA) {
          //Truc pas normal, on reset la stack
          UART0_buffer_pointer = UART0_buffer;
      }
      else {
          //Octet de start
          *UART0_buffer_pointer = c;
          UART0_buffer_pointer++;
          if (UART0_buffer_pointer - UART0_buffer >= 4) {
              //Passage en réception des données
              UART0_buffer_state = 1;
              UART0_buffer_pointer = UART0_buffer;
          }
      }
  }
}

int serial;

void *read_serial(void *threadid) {
  unsigned char rbuf[32];

  while(1) {
    int rs;
    int r;

    struct termios serialConfig;
    rs = tcgetattr(serial, &serialConfig);
    if (rs < 0) {
      printf("Could not read serial configuration. Might be missing!\n");
      exit(1);
    }

    r = read(serial, rbuf, 256);
    printf("read: %d\n", r);
    if (r<0) {
      printf("Reading error!\n");
      exit(1);
    }

    //printf("read: %d\n", r);
    unsigned int i;
    for (i=0; i<r; i++) {
      handle_incoming_char(rbuf[i]);
    }
  }

  pthread_exit(NULL);
}

int main(int argc, char **argv) {
  struct termios serialConfig;
  unsigned char wbuf[32];
  pthread_t read_thread;
  int rc;
  long t;

  serial = open(SERIALPORT, O_RDWR | O_NOCTTY); 
  tcgetattr(serial, &serialConfig);
  cfmakeraw(&serialConfig);
  cfsetispeed(&serialConfig, B115200);
  cfsetospeed(&serialConfig, B115200);
  serialConfig.c_cflag |= CS8;    // 8n1 (8bit,no parity,1 stopbit)
  serialConfig.c_cflag |= CLOCAL; //local connection, no modem contol
  serialConfig.c_cflag |= CREAD;  // enable receiving characters
  serialConfig.c_iflag = IGNPAR;  // ignore bytes with parity errors
  serialConfig.c_cc[VMIN] = 5;
  serialConfig.c_cc[VTIME] = 1;  // in deciseconds

  serialConfig.c_cflag &= ~CRTSCTS;
  tcsetattr(serial, TCSANOW, &serialConfig);

  tcflush(serial,TCIOFLUSH);

  rc = pthread_create(&read_thread, NULL, read_serial, (void *)t);
  if (rc){
     printf("ERROR; return code from pthread_create() is %d\n", rc);
     return(-1);
  }

  while(1) {
    //Ping every second
    wbuf[0] = wbuf[1] = wbuf[2] = 0xAA;
    wbuf[3] = 0xAB;
    write(serial, wbuf, 4);
    sleep(1);
  }

  pthread_exit(NULL);
  return (0);
}
