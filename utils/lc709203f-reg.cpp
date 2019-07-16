/*
NNS @ 2018
lc709203f-reg
Write LC709203F Register
*/
const char programversion[]="0.1a"; //program version

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <limits.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

int debug=2;		//debug level, 0:disable, 1:minimal, 2:full
#define debug_print(fmt, ...) do { if (debug) fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

int i2c_bus=-1;									//i2c bus id
char i2c_path[PATH_MAX];				//path to i2c bus
int i2c_addr=0x0B;							//i2c device adress
char i2c_register16_raw[1024];	//i2c custom register char array
int i2c_register16_count=-1;		//i2c custom register : counter
int i2c_register16_reg[32];		//i2c custom register : register array
int i2c_register16_value[32];		//i2c custom register : value array


bool i2c_addr_valid=false;			//i2c device adress is valid
int i2c_handle;									//i2c handle io
char i2c_buffer[10]={0};				//i2c data buffer

FILE *temp_filehandle;			//file handle to get cpu temp/usage



//https://github.com/RIOT-OS/RIOT/blob/master/drivers/lc709203f/lc709203f.c
static unsigned char get_crc(unsigned char *rec_values, unsigned char len){
	unsigned char crc = 0x00;
	unsigned char current_byte;
	unsigned char bit;
	for(current_byte = 0; current_byte < len; current_byte++){
		crc ^= (rec_values[current_byte]);
		for(bit = 8; bit > 0; bit--){
			if(crc & 0x80){crc = (crc << 1) ^ 0x07;}else{crc = (crc << 1);}
		}
	}
	return crc;
}


int LC709203F_write_reg(int addr,char reg, int data){
	unsigned char crc_array[5]; //array used to compute crc

	crc_array[0] = 0x16; //i2c adress
	crc_array[1] = reg; //register
	crc_array[2] = data&0x0FF; //low byte
	crc_array[3] = (data>>8)&0x0FF; //high byte
	crc_array[4] = get_crc(crc_array,4); //crc

	i2c_buffer[0]=reg; i2c_buffer[1]=crc_array[2]; i2c_buffer[2]=crc_array[3]; i2c_buffer[3]=crc_array[4];

	if(debug==2){
		debug_print("LC709203F_write_reg\n");
		debug_print("\tregister : 0x%02x\n",reg);
		debug_print("\tdata : 0x%02x 0x%02x\n",crc_array[2],crc_array[3]);
		debug_print("\tcrc : 0x%02x\n",crc_array[4]);
	}

	return write(i2c_handle,i2c_buffer,4); //write
}





void show_usage(void){
	printf(
"Version: %s\n"
"Usage: ./lc709203f-reg\n"
"Options:\n"
"\t-debug, Enable debugging features, 1 for minimal, 2 for full [Default: 2]\n"
"\t-i2cbus, I2C bus id, scan thru all available if not set\n"
"\t-i2caddr, I2C device adress, found via 'i2cdetect' [Default: 0x0B]\n"
"\t-register16, Custom I2C registers with 16bits value [Example : 0x12.0x1,0x08.0x0BA6,0x0B.0x2D,...]\n"
,programversion);
}



int main(int argc, char *argv[]){ //main
	
	for(int i=1;i<argc;++i){ //argument to variable
		if(strcmp(argv[i],"-help")==0){show_usage();return 1;
		}else if(strcmp(argv[i],"-debug")==0){debug=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-i2cbus")==0){i2c_bus=atoi(argv[i+1]);if(strstr(argv[i+1],"-")){i2c_bus=-i2c_bus;}
		}else if(strcmp(argv[i],"-i2caddr")==0){sscanf(argv[i+1], "%x", &i2c_addr);
		}else if(strcmp(argv[i],"-register16")==0){strcpy(i2c_register16_raw,argv[i+1]);i2c_register16_count=0;}
		++i;
	}
	
	if(i2c_register16_count==0){ //custom 16bit registers
		char* chr_ptr=i2c_register16_raw; //pointer to -register16 arguments
		char* chr_tmp_ptr; //temporary pointer
		char* chr_dot_ptr; //dot split pointer
		
		while((chr_tmp_ptr = strtok_r(chr_ptr,",",&chr_ptr))){ //0x1.0x1234,0x02.0x4567
			chr_dot_ptr=strtok(chr_tmp_ptr,"."); //split
			sscanf(chr_dot_ptr,"%x",&i2c_register16_reg[i2c_register16_count]); //extract register
			chr_dot_ptr=strtok(NULL,"."); //split
			if(chr_dot_ptr!=NULL){
				sscanf(chr_dot_ptr,"%x",&i2c_register16_value[i2c_register16_count]); //extract value
				debug_print("User custom 16bits Register : 0x%02x -> 0x%04x\n",i2c_register16_reg[i2c_register16_count],i2c_register16_value[i2c_register16_count]);
				i2c_register16_count++;
			}
		}
	}else{
		printf("At least 1 register need to be set\n\n");
		show_usage();return 1;
	}
	
	for(int i=(i2c_bus<0)?0:i2c_bus;i<10;i++){ //detect i2c bus
		sprintf(i2c_path,"/dev/i2c-%d",i); //generate i2c bus full path
		if((i2c_handle=open(i2c_path,O_RDWR))>=0){ //open i2c handle
			if(ioctl(i2c_handle,I2C_SLAVE,i2c_addr)>=0){ //i2c adress detected
				if(read(i2c_handle,i2c_buffer,2)==2){ //success read
					i2c_bus=i; i2c_addr_valid=true;
					break; //exit loop
				}
			}
			close(i2c_handle); //close i2c handle
		}
	}

	if(i2c_addr_valid){debug_print("Bus %d : 0x%02x detected\n",i2c_bus,i2c_addr); //bus detected
	}else{debug_print("Failed, 0x%02x not detected on any bus, Exiting\n",i2c_addr);return(1);} //failed
		
	if(i2c_addr==0x0B){debug_print("LC709203F detected\n");
	}else{debug_print("Failed, I2C address out of range, Exiting\n"); return(1);} //failed
	
	if(debug==2){debug_print("Start update loop\n");}
	
	if((i2c_handle=open(i2c_path,O_RDWR))<0){
		debug_print("Failed to open the I2C bus : %s\n",i2c_path); return(1);
	}else{
		if(ioctl(i2c_handle,I2C_SLAVE,i2c_addr)<0){ //access i2c device, allow retry if failed
			debug_print("Failed to access I2C device : %02x\n",i2c_addr); close(i2c_handle); return(1);
		}else{ //success
			if(ioctl(i2c_handle,I2C_PEC,1)<0){ //enable pec
				debug_print("Failed to enable PEC for I2C device : %02x\n",i2c_addr); close(i2c_handle); return(1);
			}else{
				for(int i=0;i<i2c_register16_count;i++){LC709203F_write_reg(i2c_addr,i2c_register16_reg[i],i2c_register16_value[i]);} //custom 16bits register
			}
		}
		close(i2c_handle);
	}
	
	return(0);
}