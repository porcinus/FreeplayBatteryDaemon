/*
NNS @ 2018
nns-freeplay-battery-daemon
Battery monitoring daemon Compatible with MCP3021A, LC709203F
*/
const char programversion[]="0.1b"; //program version

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <limits.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include "battery_cm3.h"		//battery data for the Freeplay CM3 platform

int debug=1;		//debug level, 0:disable, 1:minimal, 2:full
#define debug_print(fmt, ...) do { if (debug) fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

int i2c_bus=-1;									//i2c bus id
char i2c_path[PATH_MAX];				//path to i2c bus
char i2c_addr=0x0B;							//i2c device adress
char i2c_register16_raw[1024];	//i2c custom register char array
char i2c_register16_count=-1;		//i2c custom register : counter
char i2c_register16_reg[32];		//i2c custom register : register array
int i2c_register16_value[32];		//i2c custom register : value array


bool i2c_addr_valid=false;			//i2c device adress is valid
int i2c_handle;									//i2c handle io
char i2c_buffer[10]={0};				//i2c data buffer
int i2c_retry=0;								//reading retry if failure

char vbat_output_path[PATH_MAX];					//path where output final data
char vbat_filename[PATH_MAX];							//battery output filename
char vbat_stats_filename[PATH_MAX];				//battery stats output filename
bool vbat_logging=true;										//enable battery stats output
float vbat_offset=0.;											//in volt, voltage error offset
float vbat_value=.0;											//battery voltage, used as backup if read fail
int vbat_percent_value=0;									//battery percentage
int update_duration=15;										//update duration

int adc_resolution=4096;									//256:8bits, 1024:10bits, 4096:12bits (default), 65535:16bits
int adc_divider_r1=0, adc_divider_r2=0;		//resistor divider value in ohm or kohm, check show_usage(void) for infomations
float adc_vref=4.5;												//in volt, vdd of the adc chip
int adc_raw_value=0;											//adc step value

bool LC709203F_detected=false;
bool MCP3021A_detected=false;

FILE *temp_filehandle;			//file handle to get cpu temp/usage
int uptime_value=0;					//uptime value

int vbat_smooth_value[5];				//array to store last smoothed data
bool vbat_smooth_init=false;		//array initialized?





int nns_get_battery_percentage(int vbat){ //used if chip don't provide rsoc
	int i;
	if(!vbat_smooth_init){vbat_smooth_value[0]=vbat_smooth_value[1]=vbat_smooth_value[2]=vbat_smooth_value[3]=vbat;vbat_smooth_init=true;} //initialize array if not already done
	vbat=(vbat+vbat_smooth_value[3]+vbat_smooth_value[2]+vbat_smooth_value[1]+vbat_smooth_value[0])/5; //smoothed value
	for(i=0;i<3;i++){vbat_smooth_value[i]=vbat_smooth_value[i+1];} vbat_smooth_value[3]=vbat; //shift array
	if(vbat<battery_percentage[0]){return 0;} //lower than min value, 0%
	if(vbat>=battery_percentage[100]){return 100;} //higher than max value, 100%
	for(i=0;i<100;i++){if(vbat>=battery_percentage[i]&&vbat<battery_percentage[i+1]){return i;}} //return the value
	return -1; //oups
}




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

	if(debug==2){debug_print("\nLC709203F_write_reg\n");}
	if(debug==2){debug_print("register : 0x%02x\n",reg);}
	if(debug==2){debug_print("data : 0x%02x 0x%02x\n",crc_array[2],crc_array[3]);}
	if(debug==2){debug_print("crc : 0x%02x\n",crc_array[4]);}

	return write(i2c_handle,i2c_buffer,4); //write
}



int LC709203F_read_reg(int addr,char reg){
	unsigned char read_crc_array[5]; //array used to compute crc
	unsigned char returned_crc = 0; //readed crc
	unsigned char computed_crc = 0; //computed crc
	unsigned long i2c_read; //i2c read return code
	int value; //read value
	
	i2c_read = i2c_smbus_read_i2c_block_data(i2c_handle, reg, 3, (unsigned char *)i2c_buffer); //write register and read it
	
	if(debug==2){debug_print("\nLC709203F_read_reg\n");}
	if(debug==2){debug_print("register : 0x%02x\n",reg);}
	if(debug==2){debug_print("i2c_smbus_read_i2c_block_data returned 0x%04X 0x%02x 0x%02x 0x%02x\n", i2c_read, i2c_buffer[0],i2c_buffer[1],i2c_buffer[2]);}

	if(i2c_read<0){ //oups
		debug_print("Failed to read register : %02x\n",reg);
	}else{ //success
		read_crc_array[0] = 0x16; //i2c adress
		read_crc_array[1] = reg; //register
		read_crc_array[2] = 0x17; //addr|0x01
		read_crc_array[3] = i2c_buffer[0]; //low byte
		read_crc_array[4] = i2c_buffer[1]; //high byte

		returned_crc = i2c_buffer[2]; //returned crc
		computed_crc = get_crc(read_crc_array,5); //computed crc
		value=(i2c_buffer[1]<<8) | (i2c_buffer[0]); //merge into int

		if(debug==2){debug_print("read buffer : 0x%02x 0x%02x 0x%02x\n",i2c_buffer[0],i2c_buffer[1],i2c_buffer[2]);}
		if(debug==2){debug_print("read value : %d\n",value);}
		if(debug==2){debug_print("computed crc : 0x%02x\n",computed_crc);}
		if(debug==2){debug_print("read crc : 0x%02x\n",i2c_buffer[2]);}

		if(computed_crc == returned_crc){return value; //read crc match
		}else{
			debug_print("LC709203F_read_reg: CRC Mismatch\n");
			return -2;
		}
	}

	return -1;
}



void show_usage(void){
	printf(
"Version: %s\n"
"Usage: ./nns-freeplay-battery-daemon\n"
"Options:\n"
"\t-debug, Enable debugging features, 1 for minimal, 2 for full [Default: 1]\n"
"\t-i2cbus, I2C bus id, scan thru all available if not set\n"
"\t-i2caddr, I2C device adress, found via 'i2cdetect' [Default: 0x0B]\n"
"\t-register16, Custom I2C registers with 16bits value [Example : 0x12.0x1,0x08.0x0BA6,0x0B.0x2D,...]\n"
"\t-updateduration, Time between each update, in sec [Default: 15]\n"
"\t-outputpath, Path where data will be saved [Default: /dev/shm]\n"
"\t-vbatfilename, File that will contain current battery voltage and RSOC, format 'vbat;percent' [Default: vbat.log]\n"
"\t-vbatstatsfilename, File that will contain battery data from start of the system, format 'uptime;vbat;percent' [Default: vbat-start.log]\n"
"\t-vbatlogging, Set to 0 to disable battery stats data [Default: 1]\n"
"\t-vbatoffset, Battery value offset, in volt, can be positive or negative [Default: 0.00]\n"
"\t-adcvref, VRef of the ADC chip in volt [Default: 4.5]\n"
"\t-adcres, ADC resolution: 256=8bits, 1024=10bits, 4096=12bits, 65535=16bits [Default: 4096]\n"
"\t-r1value, R1 resistor value, in ohm, disable if 0\n"
"\t-r2value, R2 resistor value, in ohm, disable if 0\n"
"\tResistor divider diagram: [Vin]---[R1]---[Vout (ADC)]---[R2]---[Gnd]\n"
,programversion);
}



int main(int argc, char *argv[]){ //main
	strcpy(vbat_output_path,"/dev/shm/"); //init
	strcpy(vbat_filename,"vbat.log"); //init
	strcpy(vbat_stats_filename,"vbat-start.log"); //init
	
	for(int i=1;i<argc;++i){ //argument to variable
		if(strcmp(argv[i],"-help")==0){show_usage();return 1;
		}else if(strcmp(argv[i],"-debug")==0){debug=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-i2cbus")==0){i2c_bus=atoi(argv[i+1]);if(strstr(argv[i+1],"-")){i2c_bus=-i2c_bus;}
		}else if(strcmp(argv[i],"-i2caddr")==0){sscanf(argv[i+1], "%x", &i2c_addr);
		}else if(strcmp(argv[i],"-register16")==0){strcpy(i2c_register16_raw,argv[i+1]);i2c_register16_count=0;
		
		}else if(strcmp(argv[i],"-updateduration")==0){update_duration=atoi(argv[i+1]);
		
		}else if(strcmp(argv[i],"-outputpath")==0){strcpy(vbat_output_path,argv[i+1]);if(access(vbat_output_path,W_OK)!=0){printf("Failed, %s not writable, Exiting\n",vbat_output_path);return 1;}
		}else if(strcmp(argv[i],"-vbatfilename")==0){strcpy(vbat_filename,argv[i+1]);
		}else if(strcmp(argv[i],"-vbatstatsfilename")==0){strcpy(vbat_stats_filename,argv[i+1]);
		}else if(strcmp(argv[i],"-vbatlogging")==0){if(atoi(argv[i+1])<1){vbat_logging=false;}else{vbat_logging=true;};
		}else if(strcmp(argv[i],"-vbatoffset")==0){vbat_offset=atof(argv[i+1]);if(strstr(argv[i+1],"-")){vbat_offset=vbat_offset*-1;}
		
		}else if(strcmp(argv[i],"-adcvref")==0){adc_vref=atof(argv[i+1]);
		}else if(strcmp(argv[i],"-adcres")==0){adc_resolution=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-r1value")==0){adc_divider_r1=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-r2value")==0){adc_divider_r2=atoi(argv[i+1]);}
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
		
	if(i2c_addr==0x0B){LC709203F_detected=true; debug_print("LC709203F detected\n");
	}else if(i2c_addr>=0x48&&i2c_addr<=0x4F){MCP3021A_detected=true; debug_print("MCP3021A detected\n");
	}else{debug_print("Failed, I2C address out of range, Exiting\n"); return(1);} //failed
	
	while(true){
		chdir(vbat_output_path); //change default dir
		i2c_retry=0; //reset retry counter
		if(debug==2){debug_print("\nStart update loop\n");}
		
		while(i2c_retry<3){
			if((i2c_handle=open(i2c_path,O_RDWR))<0){
				debug_print("Failed to open the I2C bus : %s, retry in %dsec\n",i2c_path,update_duration);
				i2c_retry=3; //no need to retry since failed to open I2C bus itself
				adc_raw_value=-1; vbat_value=-1; vbat_percent_value=-1;
			}else{
				if(ioctl(i2c_handle,I2C_SLAVE,i2c_addr)<0){ //access i2c device, allow retry if failed
					debug_print("Failed to access I2C device : %02x, retry in 1sec\n",i2c_addr);
				}else{ //success
					if(LC709203F_detected){
						if(ioctl(i2c_handle,I2C_PEC,1)<0){ //enable pec
							debug_print("Failed to enable PEC for I2C device : %02x\n",i2c_addr);
						}else{
							LC709203F_write_reg(i2c_addr,0x15,0x0001); //IC Power Mode
							LC709203F_write_reg(i2c_addr,0x0B,0x002D); //APA
							LC709203F_write_reg(i2c_addr,0x12,0x0001); //Select battery profile
							LC709203F_write_reg(i2c_addr,0x16,0x0000); //Temperature via I2C
							LC709203F_write_reg(i2c_addr,0x08,0x0BA6); //Temperature at 25°C
							for(int i=0;i<i2c_register16_count;i++){LC709203F_write_reg(i2c_addr,i2c_register16_reg[i],i2c_register16_value[i]);} //custom 16bits register
							LC709203F_write_reg(i2c_addr,0x07,0xAA55); //Init RSOC
							
							vbat_value=LC709203F_read_reg(i2c_addr,0x09)/1000.; //Cell Voltage register
							vbat_percent_value=LC709203F_read_reg(i2c_addr,0x0D); //RSOC register
						}
					}else if(MCP3021A_detected){
						if(read(i2c_handle,i2c_buffer,2)!=2){debug_print("Failed to read data from I2C device : %04x, retry in 1sec\n",i2c_addr);
						}else{ //success
							adc_raw_value=(i2c_buffer[0]<<8)|(i2c_buffer[1]&0xff); //combine buffer bytes into integer
							if(adc_divider_r1!=0&&adc_divider_r2!=0){vbat_value=adc_raw_value*(float)(adc_vref/adc_resolution)/(float)(adc_divider_r2/(float)(adc_divider_r1+adc_divider_r2)); //compute battery voltage with resistor divider
							}else{vbat_value=adc_raw_value*(float)(adc_vref/adc_resolution);} //compute battery voltage only with adc vref
							vbat_percent_value=nns_get_battery_percentage((int)(vbat_value*1000));
						}
					}
					
					if(vbat_value<0){debug_print("Warning, voltage under 0 volt, Probing failed\n");
					}else if(vbat_percent_value<0){debug_print("Warning, RSOC < 0%, Probing failed\n");
					}else{ //success
						debug_print("Voltage : %.3fv, RSOC : %d%%\n",vbat_value,vbat_percent_value);
						vbat_value+=vbat_offset; //add adc chip error offset
						temp_filehandle=fopen(vbat_filename,"wb"); fprintf(temp_filehandle,"%.3f;%d",vbat_value,vbat_percent_value); fclose(temp_filehandle); //write log file
						if(vbat_logging){ //cumulative cumulative log file
							temp_filehandle=fopen("/proc/uptime","r"); fscanf(temp_filehandle,"%u",&uptime_value); fclose(temp_filehandle); //get system uptime
							temp_filehandle=fopen(vbat_stats_filename,"a+"); fprintf(temp_filehandle,"%u;%.3f;%d\n",uptime_value,vbat_value,vbat_percent_value); fclose(temp_filehandle); //write cumulative log file
						}
					}
				}
				close(i2c_handle);
			}
			
			if(vbat_value<0||vbat_percent_value<0){
				i2c_retry++; //something failed at one point so retry
				if(i2c_retry>2){debug_print("Warning, voltage probe failed 3 times, skipping until next update\n");}else{sleep(1);}
			}else{i2c_retry=3;} //data read with success, no retry
		}
		
		sleep(update_duration);
	}
	
	return(0);
}