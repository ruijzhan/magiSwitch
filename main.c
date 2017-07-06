#include <errno.h>
#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <hiredis/hiredis.h>

int set_interface_attribs(int fd, int speed)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0) {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    tty.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         /* 8-bit characters */
    tty.c_cflag &= ~PARENB;     /* no parity bit */
    tty.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */

    /* setup for non-canonical mode */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    /* fetch bytes as they become available */
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int open_serial(char * portname, int speed)
{
    int fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        printf("Error opening %s: %s\n", portname, strerror(errno));
        return -1;
    }
    set_interface_attribs(fd, speed);
    return fd;

}


int main() {
    int wlen;
    redisContext *c;
    redisReply *reply;
    int fd = open_serial("/dev/ttyS0",B9600);
//    int fd = open_serial("/dev/ttyUSB0",B9600);

    struct timeval timeout = { 1, 500000 }; // 1.5 seconds
    c = redisConnectWithTimeout("127.0.0.1", 6379, timeout);

    if(fork() == 0) {
        char * f_fifo = "magiFIFO";
        //mkfifo(f_fifo, O_RDONLY);
        while(1) {
            int fd_cmd = open(f_fifo, O_RDONLY);
            if (fd_cmd < 0) {
                printf("Error opening FIFO %s: %s\n", "magiSwitch", strerror(errno));
                continue;
            }
            unsigned char cmd[16];
            int cmd_len = read(fd_cmd, cmd, sizeof(cmd) - 1);
            if(cmd_len >0) {
                printf("%d %s", cmd_len, cmd);
                cmd_len -= 1;
                wlen = write(fd, cmd, cmd_len);
                tcdrain(fd);    /* delay for output */
                if (wlen != cmd_len) {
                    printf("Error from write: %d, %s\n", wlen, strerror(errno));
                }
            }
            close(fd_cmd);
        }
    } else {
        unsigned char one_byte;      
        unsigned char buf[80];
        unsigned char *p;
        int rdlen = 0;
        do {
            p = buf;
            rdlen = 0;
           
            while (1){
                if(read(fd, &one_byte, 1) < 0) {
                    printf("Error from read: %d: %s\n", rdlen, strerror(errno));
                }
                *p = one_byte;
                p++;
                rdlen++;
                if (one_byte == 0x30 || one_byte == 0x31) {
                    break;
                }
            } 
            if (rdlen > 0) {
                //unsigned char *p;
                printf("Read %d bytes:", rdlen);
                for (p = buf; rdlen-- > 0; p++)
                    printf(" 0x%x", *p);
                printf("\n");
     
                char *key = (buf[1] == 0x42) ? "switch1" : "switch2";
               
                reply = redisCommand(c,"SET %b %b", key, (size_t) 7, buf, (size_t) 3);
                //printf("SET (binary API): %s\n", reply->str);
                freeReplyObject(reply);
    
            } else{
                printf("Error from read: %d: %s\n", rdlen, strerror(errno));
            }
    } while (1);
    }
}
