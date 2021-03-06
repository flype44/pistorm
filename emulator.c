#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <dirent.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <sched.h>
#include "m68k.h"
#include "main.h"
#include<pthread.h>
#include "Gayle.h"
#include "ide.h"

//#define BCM2708_PERI_BASE        0x20000000  //pi0-1
//#define BCM2708_PERI_BASE	0xFE000000     //pi4
#define BCM2708_PERI_BASE       0x3F000000     //pi3
#define BCM2708_PERI_SIZE       0x01000000
#define GPIO_BASE               (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */
#define GPCLK_BASE              (BCM2708_PERI_BASE + 0x101000)
#define GPIO_ADDR                0x200000 /* GPIO controller */
#define GPCLK_ADDR               0x101000
#define CLK_PASSWD      0x5a000000
#define CLK_GP0_CTL     0x070
#define CLK_GP0_DIV     0x074

#define SA0 5
#define SA1 3
#define SA2 2

#define STATUSREGADDR    GPIO_CLR = 1<<SA0;GPIO_CLR = 1<<SA1;GPIO_SET = 1<<SA2;
#define W16              GPIO_CLR = 1<<SA0;GPIO_CLR = 1<<SA1;GPIO_CLR = 1<<SA2;
#define R16              GPIO_SET = 1<<SA0;GPIO_CLR = 1<<SA1;GPIO_CLR = 1<<SA2;
#define W8               GPIO_CLR = 1<<SA0;GPIO_SET = 1<<SA1;GPIO_CLR = 1<<SA2;
#define R8               GPIO_SET = 1<<SA0;GPIO_SET = 1<<SA1;GPIO_CLR = 1<<SA2;

#define PAGE_SIZE (4*1024)
#define BLOCK_SIZE (4*1024)

#define GPIOSET(no, ishigh)           \
do {                                  \
        if (ishigh)                   \
                set |= (1 << (no));   \
        else                          \
                reset |= (1 << (no)); \
} while (0)


#define FASTBASE 0x07FFFFFF
//#define FASTSIZE 0xFFFFFF
#define FASTSIZE 0xFFFFFFF


#define GAYLEBASE 0xD80000 //D7FFFF
#define GAYLESIZE 0x6FFFF

int  mem_fd;
int  mem_fd_gpclk;
void *gpio_map;
void *gpclk_map;

// I/O access
volatile unsigned int *gpio;
volatile unsigned int *gpclk;
volatile unsigned int gpfsel0;
volatile unsigned int gpfsel1;
volatile unsigned int gpfsel2;
volatile unsigned int gpfsel0_o;
volatile unsigned int gpfsel1_o;
volatile unsigned int gpfsel2_o;

// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0

#define GET_GPIO(g) (*(gpio+13)&(1<<g)) // 0 if LOW, (1<<g) if HIGH

#define GPIO_PULL *(gpio+37) // Pull up/pull down
#define GPIO_PULLCLK0 *(gpio+38) // Pull up/pull down clock


void setup_io();

uint32_t read8(uint32_t address);
void write8(uint32_t address, uint32_t data);

uint32_t read16(uint32_t address);
void write16(uint32_t address, uint32_t data);

void write32(uint32_t address, uint32_t data);
uint32_t read32(uint32_t address);

uint16_t read_reg(void);
void write_reg(unsigned int value);

volatile uint16_t srdata;
volatile uint32_t srdata2;
volatile uint32_t srdata2_old;


unsigned char g_ram[FASTSIZE+1];                 /* RAM */
unsigned char toggle;

/* Signal Handler for SIGINT */
void sigint_handler(int sig_num)
{
    /* Reset handler to catch SIGINT next time.
       Refer http://en.cppreference.com/w/c/program/signal */
    printf("\n User provided signal handler for Ctrl+C \n");

    /* Do a graceful cleanup of the program like: free memory/resources/etc and exit */
    exit(0);
}


void* iplThread(void *args){ 

printf("thread!/n");
//srdata2_old = read_reg();
//toggle = 0;

while(42){
//printf("thread!/n");
  if (GET_GPIO(1) == 0){
                  srdata = read_reg();
                  if (srdata != srdata2_old){
                        srdata2 = ((srdata >> 13)&0xff);
                        //printf("STATUS: %d\n", srdata2);
                        srdata2_old = srdata;
                        m68k_set_irq(srdata2);
                        toggle = 1;
                        }
                } else {
                        if (toggle != 0){
                        /*
			srdata = read_reg();
                        srdata2 = ((srdata >> 13)&0xff);
                        srdata2_old = srdata;
                        m68k_set_irq(srdata2);
			*/
			m68k_set_irq(0);
                        //printf("STATUS: 0\n");
                        toggle = 0;
                        }
                }

	usleep(1);
	}
}


int main() {


int g;


const struct sched_param priority = {99};

    sched_setscheduler(0, SCHED_RR , &priority);
    printf("YES locked in memory\n");
    mlockall(MCL_CURRENT); // lock in memory to keep us from paging out


 InitGayle();

/*
   int fd;
   ide0 = ide_allocate("cf");
   fd = open("hd0.img", O_RDWR);
   if (fd == -1){
   	printf("HDD Image hd0.image failed open\n");
   }else{
        ide_attach(ide0, 0, fd);
	ide_reset_begin(ide0);
	printf("HDD Image hd0.image attached\n");
   }
*/
  signal(SIGINT, sigint_handler);
  setup_io();

  //Enable 200MHz CLK output on GPIO4, adjust divider and pll source depending on pi model
  printf("Enable GPCLK0 on GPIO4\n");

        *(gpclk+ (CLK_GP0_CTL/4)) =  CLK_PASSWD | (1 << 5);
        usleep(10);
        while ( (*(gpclk+(CLK_GP0_CTL/4))) & (1 << 7));
        usleep(100);
        *(gpclk+(CLK_GP0_DIV/4)) =  CLK_PASSWD | (6 << 12); //divider , 6=200MHz on pi3
        usleep(10);
        *(gpclk+(CLK_GP0_CTL/4)) =   CLK_PASSWD | 5 | (1 << 4); //pll? 6=plld, 5=pllc
        usleep(10);
        while (((*(gpclk+(CLK_GP0_CTL/4))) & (1 << 7))== 0);
        usleep(100);

    SET_GPIO_ALT(4,0); //gpclk0

   //set SA to output
    INP_GPIO(2);
    OUT_GPIO(2);
    INP_GPIO(3);
    OUT_GPIO(3);
    INP_GPIO(5);
    OUT_GPIO(5);

  //set gpio0 (aux0) and gpio1 (aux1) to input
  INP_GPIO(0);
  INP_GPIO(1);

  // Set GPIO pins 6,7 and 8-23 to output
  for (g=6; g<=23; g++)
  {
    INP_GPIO(g);
    OUT_GPIO(g);
  }
     printf ("Precalculate GPIO8-23 aus Output\n");
     gpfsel0_o =*(gpio); //store gpio ddr
     printf ("gpfsel0: %#x\n", gpfsel0_o);
     gpfsel1_o =*(gpio+1); //store gpio ddr
     printf ("gpfsel1: %#x\n", gpfsel1_o);
     gpfsel2_o =*(gpio+2); //store gpio ddr
     printf ("gpfsel2: %#x\n", gpfsel2_o);

  // Set GPIO pins 8-23 to input
  for (g=8; g<=23; g++)
  {
    INP_GPIO(g);
  }
     printf ("Precalculate GPIO8-23 as Input\n");
     gpfsel0 =*(gpio); //store gpio ddr
     printf ("gpfsel0: %#x\n", gpfsel0);
     gpfsel1 =*(gpio+1); //store gpio ddr
     printf ("gpfsel1: %#x\n", gpfsel1);
     gpfsel2 =*(gpio+2); //store gpio ddr
     printf ("gpfsel2: %#x\n", gpfsel2);

 GPIO_CLR = 1<<2;
 GPIO_CLR = 1<<3;
 GPIO_SET = 1<<5;

 GPIO_SET = 1<<6;
 GPIO_SET = 1<<7;

 //reset cpld statemachine first

 write_reg(0x01);
 usleep(100);
 usleep(1500);
 write_reg(0x00);
 usleep(100);


 write8(0xbfe201,0x0001); //AMIGA OVL
 write8(0xbfe001,0x0001); //AMIGA OVL high (ROM@0x0)

 usleep(1500);

	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68EC030);
	m68k_pulse_reset();
	srdata2_old = read_reg();
	printf("STATUS: %d\n", srdata2_old);
	toggle = 0;

/*
    pthread_t id;
    int err;

        //err = pthread_create(&id, NULL, &iplThread, NULL);
        if (err != 0)
            printf("\ncan't create IPL thread :[%s]", strerror(err));
        else
            printf("\n IPL Thread created successfully\n");
*/
	m68k_pulse_reset();
	while(42) {

		m68k_execute(150);
		//usleep(1);

		//printf("IRQ:0x%06x\n",CheckIrq());
/*
		if (CheckIrq() == 1)
		   m68k_set_irq(2);
		else
		   m68k_set_irq(0);
*/

		if (GET_GPIO(1) == 0 || CheckIrq() == 1){
		  srdata = read_reg();
		//  if (CheckIrq() == 1) srdata |= (1 << 14);
		  if (srdata != srdata2_old){
                        srdata2 = ((srdata >> 13)&0xff);
                        //printf("STATUS: %d\n", srdata2);
                        srdata2_old = srdata;
                        m68k_set_irq(srdata2);
			toggle = 1;
                        }
		} else {

			if (toggle != 0){
			srdata = read_reg();
			srdata2 = ((srdata >> 13)&0xff);
			srdata2_old = srdata;
			m68k_set_irq(srdata2);
			 //printf("STATUS: 0\n");
			toggle = 0;
			}
		}

	}

	return 0;
}



void cpu_pulse_reset(void){

        usleep(10000);
}




int cpu_irq_ack(int level)
{
    printf("cpu irq ack\n");
    return level;
}



unsigned int  m68k_read_memory_8(unsigned int address){

	if(address>GAYLEBASE && address<GAYLEBASE + GAYLESIZE){
        return readGayleB(address);
    	}

        if(address>FASTBASE){
        return g_ram[address- FASTBASE];
        }

        return read8((uint32_t)address);
}

unsigned int  m68k_read_memory_16(unsigned int address){

	if(address>GAYLEBASE && address<GAYLEBASE + GAYLESIZE){
        return readGayle(address);
    	}

        if(address>FASTBASE){
        uint16_t value = *(uint16_t*)&g_ram[address- FASTBASE];
        value = (value << 8) | (value >> 8);
	return value;
        }
        return (unsigned int)read16((uint32_t)address);
}

unsigned int  m68k_read_memory_32(unsigned int address){

	if(address>GAYLEBASE && address<GAYLEBASE + GAYLESIZE){
        return readGayleL(address);
    	}

 	if(address>FASTBASE){
	uint32_t value = *(uint32_t*)&g_ram[address- FASTBASE];
        value = ((value << 8) & 0xFF00FF00 ) | ((value >> 8) & 0xFF00FF );
        return value << 16 | value >> 16;
        }

        uint16_t a = read16(address);
        uint16_t b = read16(address+2);
	return (a << 16) | b;
}

void m68k_write_memory_8(unsigned int address, unsigned int value){


  	if(address>GAYLEBASE && address<GAYLEBASE + GAYLESIZE){
        writeGayleB(address, value);
        return;
    	}


      if(address>FASTBASE){
	g_ram[address- FASTBASE] = value;
        return;
        }

	write8((uint32_t)address,value);
	return;
}

void m68k_write_memory_16(unsigned int address, unsigned int value){
//        if (address==0xdff030) printf("%c", value);

 	if(address>GAYLEBASE && address<GAYLEBASE + GAYLESIZE){
        writeGayle(address,value);
        return;
    	}


      if(address>FASTBASE){
	uint16_t* dest = (uint16_t*)&g_ram[address- FASTBASE];
    	value = (value << 8) | (value >> 8);
    	*dest = value;
        return;
        }

        write16((uint32_t)address,value);
	return;
}

void m68k_write_memory_32(unsigned int address, unsigned int value){


	if(address>GAYLEBASE && address<GAYLEBASE + GAYLESIZE){
        writeGayleL(address, value);
    	}

        if(address>FASTBASE){
	   uint32_t* dest = (uint32_t*)&g_ram[address- FASTBASE];
           value = ((value << 8) & 0xFF00FF00 ) | ((value >> 8) & 0xFF00FF );
           value = value << 16 | value >> 16;
           *dest = value;
        return;
        }

	write16(address , value >> 16);
	write16(address+2 , value );
	return;
}

/*
void write32(uint32_t address, uint32_t data){
        write16(address+2 , data);
        write16(address , data >>16 );
}

uint32_t read32(uint32_t address){
        uint16_t a = read16(address+2);
        uint16_t b = read16(address);
        return (a>>16)|b;
}
*/

void write16(uint32_t address, uint32_t data)
{
 uint32_t addr_h_s = (address & 0x0000ffff) << 8;
 uint32_t addr_h_r = (~address & 0x0000ffff) << 8;
 uint32_t addr_l_s = (address >> 16) << 8;
 uint32_t addr_l_r = (~address >> 16) << 8;
 uint32_t data_s = (data & 0x0000ffff) << 8;
 uint32_t data_r = (~data & 0x0000ffff) << 8;

  //      asm volatile ("dmb" ::: "memory");
        W16
        *(gpio) = gpfsel0_o;
        *(gpio + 1) = gpfsel1_o;
        *(gpio + 2) = gpfsel2_o;

        *(gpio + 7) = addr_h_s;
        *(gpio + 10) = addr_h_r;
        GPIO_CLR = 1 << 7;
	GPIO_SET = 1 << 7;

	*(gpio + 7) = addr_l_s;
        *(gpio + 10) = addr_l_r;
        GPIO_CLR = 1 << 7;
	GPIO_SET = 1 << 7;

        //write phase
        *(gpio + 7) = data_s;
        *(gpio + 10) = data_r;
        GPIO_CLR = 1 << 7;
	GPIO_SET = 1 << 7;

        *(gpio) = gpfsel0;
        *(gpio + 1) = gpfsel1;
        *(gpio + 2) = gpfsel2;
        while ((GET_GPIO(0)));
   //     asm volatile ("dmb" ::: "memory");
}


void write8(uint32_t address, uint32_t data)
{

        if ((address & 1) == 0)
            data = data + (data << 8); //EVEN, A0=0,UDS
        else data = data & 0xff ; //ODD , A0=1,LDS
 uint32_t addr_h_s = (address & 0x0000ffff) << 8;
 uint32_t addr_h_r = (~address & 0x0000ffff) << 8;
 uint32_t addr_l_s = (address >> 16) << 8;
 uint32_t addr_l_r = (~address >> 16) << 8;
 uint32_t data_s = (data & 0x0000ffff) << 8;
 uint32_t data_r = (~data & 0x0000ffff) << 8;


     //   asm volatile ("dmb" ::: "memory");
        W8
        *(gpio) = gpfsel0_o;
        *(gpio + 1) = gpfsel1_o;
        *(gpio + 2) = gpfsel2_o;

        *(gpio + 7) = addr_h_s;
        *(gpio + 10) = addr_h_r;
        GPIO_CLR = 1 << 7;
        GPIO_SET = 1 << 7;

        *(gpio + 7) = addr_l_s;
        *(gpio + 10) = addr_l_r;
        GPIO_CLR = 1 << 7;
        GPIO_SET = 1 << 7;

        //write phase
        *(gpio + 7) = data_s;
        *(gpio + 10) = data_r;
        GPIO_CLR = 1 << 7;
        GPIO_SET = 1 << 7;

        *(gpio) = gpfsel0;
        *(gpio + 1) = gpfsel1;
        *(gpio + 2) = gpfsel2;
        while ((GET_GPIO(0)));
     //   asm volatile ("dmb" ::: "memory");
	GPIO_SET = 1 << 7;
}


uint32_t read16(uint32_t address)
{
        volatile int val;
 uint32_t addr_h_s = (address & 0x0000ffff) << 8;
 uint32_t addr_h_r = (~address & 0x0000ffff) << 8;
 uint32_t addr_l_s = (address >> 16) << 8;
 uint32_t addr_l_r = (~address >> 16) << 8;

     //   asm volatile ("dmb" ::: "memory");
        R16

        *(gpio) = gpfsel0_o;
        *(gpio + 1) = gpfsel1_o;
        *(gpio + 2) = gpfsel2_o;

        *(gpio + 7) = addr_h_s;
        *(gpio + 10) = addr_h_r;
        GPIO_CLR = 1 << 7;
        GPIO_SET = 1 << 7;

        *(gpio + 7) = addr_l_s;
        *(gpio + 10) = addr_l_r;
        GPIO_CLR = 1 << 7;
        GPIO_SET = 1 << 7;


        //read phase

        *(gpio) = gpfsel0;
        *(gpio + 1) = gpfsel1;
        *(gpio + 2) = gpfsel2;

        GPIO_CLR = 1 << 6;
        while (!(GET_GPIO(0)));
        GPIO_CLR = 1 << 6;
        asm volatile ("nop" ::);
	asm volatile ("nop" ::);
	asm volatile ("nop" ::);
	val = *(gpio + 13);
        GPIO_SET = 1 << 6;
    //    asm volatile ("dmb" ::: "memory");
        return (val >>8)&0xffff;
}


uint32_t read8(uint32_t address)
{
        int val;
 uint32_t addr_h_s = (address & 0x0000ffff) << 8;
 uint32_t addr_h_r = (~address & 0x0000ffff) << 8;
 uint32_t addr_l_s = (address >> 16) << 8;
 uint32_t addr_l_r = (~address >> 16) << 8;

    //    asm volatile ("dmb" ::: "memory");
        R8
        *(gpio) = gpfsel0_o;
        *(gpio + 1) = gpfsel1_o;
        *(gpio + 2) = gpfsel2_o;

        *(gpio + 7) = addr_h_s;
        *(gpio + 10) = addr_h_r;
        GPIO_CLR = 1 << 7;
        GPIO_SET = 1 << 7;

        *(gpio + 7) = addr_l_s;
        *(gpio + 10) = addr_l_r;
        GPIO_CLR = 1 << 7;
        GPIO_SET = 1 << 7;

        //read phase

        *(gpio) = gpfsel0;
        *(gpio + 1) = gpfsel1;
        *(gpio + 2) = gpfsel2;

        GPIO_CLR = 1 << 6;
        while (!(GET_GPIO(0)));
        GPIO_CLR = 1 << 6;
	asm volatile ("nop" ::);
	asm volatile ("nop" ::);
	asm volatile ("nop" ::);
        val = *(gpio + 13);
        GPIO_SET = 1 << 6;
    //    asm volatile ("dmb" ::: "memory");

	val = (val >>8)&0xffff;
        if ((address & 1) == 0)
            val = (val >> 8) & 0xff ; //EVEN, A0=0,UDS
        else
            val = val & 0xff ; //ODD , A0=1,LDS
	return val;
}



/******************************************************/

void write_reg(unsigned int value)
{
        asm volatile ("dmb" ::: "memory");
        STATUSREGADDR
        asm volatile ("nop" ::);
        asm volatile ("nop" ::);
        asm volatile ("nop" ::);
        //Write Status register
        GPIO_CLR = 1 << SA0;
        GPIO_CLR = 1 << SA1;
        GPIO_SET = 1 << SA2;

        *(gpio) = gpfsel0_o;
        *(gpio + 1) = gpfsel1_o;
        *(gpio + 2) = gpfsel2_o;
        *(gpio + 7) = (value & 0xffff) << 8;
        *(gpio + 10) = (~value & 0xffff) << 8;
        GPIO_CLR = 1 << 7;
        GPIO_CLR = 1 << 7; //delay
        GPIO_SET = 1 << 7;
	GPIO_SET = 1 << 7;
        //Bus HIGH-Z
        *(gpio) = gpfsel0;
        *(gpio + 1) = gpfsel1;
        *(gpio + 2) = gpfsel2;
        asm volatile ("dmb" ::: "memory");
}


uint16_t read_reg(void)
{
        uint32_t val;

        asm volatile ("dmb" ::: "memory");
        STATUSREGADDR
        asm volatile ("nop" ::);
        asm volatile ("nop" ::);
        asm volatile ("nop" ::);
        //Bus HIGH-Z
        *(gpio) = gpfsel0;
        *(gpio + 1) = gpfsel1;
        *(gpio + 2) = gpfsel2;

        GPIO_CLR = 1 << 6;
        GPIO_CLR = 1 << 6;      //delay
	GPIO_CLR = 1 << 6;
	GPIO_CLR = 1 << 6;
        val = *(gpio + 13);
        GPIO_SET = 1 << 6;
        asm volatile ("dmb" ::: "memory");

        return (uint16_t)(val >> 8);
}


//
// Set up a memory regions to access GPIO
//
void setup_io()
{
   /* open /dev/mem */
   if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
      printf("can't open /dev/mem \n");
      exit(-1);
   }

   /* mmap GPIO */
   gpio_map = mmap(
      NULL,             //Any adddress in our space will do
      BCM2708_PERI_SIZE,       //Map length
      PROT_READ|PROT_WRITE,// Enable reading & writting to mapped memory
      MAP_SHARED,       //Shared with other processes
      mem_fd,           //File to map
      BCM2708_PERI_BASE //Offset to GPIO peripheral
   );

   close(mem_fd); //No need to keep mem_fd open after mmap

   if (gpio_map == MAP_FAILED) {
      printf("gpio mmap error %d\n", (int)gpio_map);//errno also set!
      exit(-1);
   }

   gpio = ((volatile unsigned *)gpio_map) + GPIO_ADDR/4;
   gpclk = ((volatile unsigned *)gpio_map) + GPCLK_ADDR/4;


} // setup_io

