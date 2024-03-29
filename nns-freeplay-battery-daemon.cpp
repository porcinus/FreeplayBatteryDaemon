/*
NNS @ 2018
nns-freeplay-battery-daemon
Battery monitoring daemon Compatible with MCP3021A, LC709203F, MAX17048
*/
const char programversion[]="0.1d"; //program version

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

int debug=0;		//debug level, 0:disable, 1:minimal, 2:full
#define debug_print(fmt, ...) do { if (debug) fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0) //Flavor: print advanced debug to stderr

int i2c_bus=-1;									//i2c bus id
char i2c_path[PATH_MAX];				//path to i2c bus
int i2c_addr=0x0B;							//i2c device adress
char i2c_register16_raw[1024];	//i2c custom register char array
int i2c_register16_count=-1;		//i2c custom register : counter
int i2c_register16_reg[32];			//i2c custom register : register array
int i2c_register16_value[32];		//i2c custom register : value array


bool i2c_addr_valid=false;			//i2c device adress is valid
int i2c_handle;									//i2c handle io
char i2c_buffer[10]={0};				//i2c data buffer
int i2c_retry=0;								//reading retry if failure

char vbat_output_path[PATH_MAX];					//path where output final data
char vbat_filename[PATH_MAX];							//battery output filename
char vbat_stats_filename[PATH_MAX];				//battery stats output filename
char vbat_alarm_filename[PATH_MAX];				//battery alarm output filename
bool vbat_logging=true;										//enable battery stats output
float vbat_offset=0.;											//in volt, voltage error offset
float vbat_value=.0;											//battery voltage, used as backup if read fail
int vbat_percent_value=0;									//battery percentage
int vbat_warn_lsoc=4;											//warning value for low SOC
float vbat_warn_lv=3.1;										//warning value for low voltage

float temperature_k=-1;										//battery temperature in kelvin
int vbat_alarm=0;													//IC alarm state
int rsoc_extend[5]={100,0,100,0};					//rsoc extend limits
char rsoc_extend_raw[1024];								//rsoc extend limits char array
int rsoc_extend_count=-1;									//rsoc extend limits count
int update_duration=15;										//update duration

int adc_resolution=4096;									//256:8bits, 1024:10bits, 4096:12bits (default), 65535:16bits
int adc_divider_r1=0, adc_divider_r2=0;		//resistor divider value in ohm or kohm, check show_usage(void) for infomations
float adc_vref=4.5;												//in volt, vdd of the adc chip
int adc_raw_value=0;											//adc step value

bool LC709203F_detected=false;		//chip detected bool
bool LC709203F_init=false;				//used to init registers once
bool MAX17048_detected=false;		//chip detected bool
bool MAX17048_init=false;				//used to init registers once
bool MCP3021A_detected=false;			//chip detected bool




FILE *temp_filehandle;			//file handle to get cpu temp/usage
int uptime_value=0;					//uptime value


int vbat_smooth_value[5];				//array to store last smoothed data
bool vbat_smooth_init=false;		//array initialized?



int nns_map_int(int x,int in_min,int in_max,int out_min,int out_max){
  if(x<in_min){return out_min;}
  if(x>in_max){return out_max;}
  return (x-in_min)*(out_max-out_min)/(in_max-in_min)+out_min;
}



int nns_get_battery_percentage(int vbat){ //used if chip don't provide rsoc, try to predict rsoc plus smoothing
	int i;
	if(!vbat_smooth_init){vbat_smooth_value[0]=vbat_smooth_value[1]=vbat_smooth_value[2]=vbat_smooth_value[3]=vbat;vbat_smooth_init=true;} //initialize array if not already done
	vbat=(vbat+vbat_smooth_value[3]+vbat_smooth_value[2]+vbat_smooth_value[1]+vbat_smooth_value[0])/5; //smoothed value
	for(i=0;i<3;i++){vbat_smooth_value[i]=vbat_smooth_value[i+1];} vbat_smooth_value[3]=vbat; //shift array
	if(vbat<battery_percentage[0]){return 0;} //lower than min value, 0%
	if(vbat>=battery_percentage[100]){return 100;} //higher than max value, 100%
	for(i=0;i<100;i++){if(vbat>=battery_percentage[i]&&vbat<battery_percentage[i+1]){return i;}} //return the value
	return -1; //oups
}



//https://stackoverflow.com/questions/3769405/determining-cpu-utilization
int get_cpu_load(){
	long double a[4], b[4];
	temp_filehandle=fopen("/proc/stat","r");
  fscanf(temp_filehandle,"%*s %Lf %Lf %Lf %Lf",&a[0],&a[1],&a[2],&a[3]);
  fclose(temp_filehandle);
  sleep(1);
  temp_filehandle=fopen("/proc/stat","r");
  fscanf(temp_filehandle,"%*s %Lf %Lf %Lf %Lf",&b[0],&b[1],&b[2],&b[3]);
  fclose(temp_filehandle);
  return (int)(((b[0]+b[1]+b[2]) - (a[0]+a[1]+a[2])) / ((b[0]+b[1]+b[2]+b[3]) - (a[0]+a[1]+a[2]+a[3])))*100;
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

	if(debug==2){
		debug_print("LC709203F_write_reg\n");
		debug_print("\tregister : 0x%02x\n",reg);
		debug_print("\tdata : 0x%02x 0x%02x\n",crc_array[2],crc_array[3]);
		debug_print("\tcrc : 0x%02x\n",crc_array[4]);
	}

	return write(i2c_handle,i2c_buffer,4); //write
}



int LC709203F_read_reg(int addr,char reg){
	unsigned char read_crc_array[5]; //array used to compute crc
	unsigned char returned_crc = 0; //readed crc
	unsigned char computed_crc = 0; //computed crc
	unsigned long i2c_read; //i2c read return code
	int value; //read value
	
	i2c_read = i2c_smbus_read_i2c_block_data(i2c_handle, reg, 3, (unsigned char *)i2c_buffer); //write register and read it
	
	if(debug==2){
		debug_print("LC709203F_read_reg\n");
		debug_print("\tregister : 0x%02x\n",reg);
	}

	if(i2c_read<0){ //oups
		debug_print("Failed to read register, returned 0x%04X\n",i2c_read);
	}else{ //success
		read_crc_array[0] = 0x16; //i2c adress
		read_crc_array[1] = reg; //register
		read_crc_array[2] = 0x17; //addr|0x01
		read_crc_array[3] = i2c_buffer[0]; //low byte
		read_crc_array[4] = i2c_buffer[1]; //high byte

		returned_crc = i2c_buffer[2]; //returned crc
		computed_crc = get_crc(read_crc_array,5); //computed crc
		value=(i2c_buffer[1]<<8) | (i2c_buffer[0]); //merge into int

		if(debug==2){
			debug_print("\tread buffer : 0x%02x 0x%02x 0x%02x\n",i2c_buffer[0],i2c_buffer[1],i2c_buffer[2]);
			debug_print("\tread value : %d\n",value);
			debug_print("\tcomputed crc : 0x%02x\n",computed_crc);
			debug_print("\tread crc : 0x%02x\n",i2c_buffer[2]);
		}

		if(computed_crc == returned_crc){return value; //read crc match
		}else{
			debug_print("LC709203F_read_reg: CRC Mismatch\n");
			return -2;
		}
	}

	return -1;
}



int write_reg16(char reg, int data, bool reverse){
	i2c_buffer[0]=reg;

	if(reverse){ //split int, reverse order 
		i2c_buffer[1]=data&0x0FF; //low byte
		i2c_buffer[2]=(data>>8)&0x0FF; //high byte
	}else{ //split int
		i2c_buffer[1]=(data>>8)&0x0FF; //low byte
		i2c_buffer[2]=data&0x0FF; //high byte
	}

	if(debug==2){
		debug_print("write_reg16\n");
		debug_print("\tregister : 0x%02x\n",reg);
		debug_print("\tdata : 0x%02x 0x%02x\n",i2c_buffer[1],i2c_buffer[2]);
	}

	return write(i2c_handle,i2c_buffer,3); //write
}



int read_reg16(char reg, bool reverse){
	unsigned long i2c_read; //i2c read return code
	int value; //read value
	
	i2c_read = i2c_smbus_read_i2c_block_data(i2c_handle, reg, 2, (unsigned char *)i2c_buffer); //write register and read it
	
	if(debug==2){
		debug_print("read_reg16\n");
		debug_print("\tregister : 0x%02x\n",reg);
	}

	if(i2c_read<0){ //oups
		debug_print("Failed to read register, returned 0x%04X\n",i2c_read);
	}else{ //success
		if(reverse){value=(i2c_buffer[1]<<8) | (i2c_buffer[0]);} //merge into int, reverse order 
		else{value=(i2c_buffer[0]<<8) | (i2c_buffer[1]);} //merge into int
		
		if(debug==2){
			debug_print("\tread buffer : 0x%02x 0x%02x\n",i2c_buffer[0],i2c_buffer[1]);
			debug_print("\tread value : %d\n",value);
		}

		return value;
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
"\t-vbatalarmfilename, File that will contain alarm warning from fuel gauge IC, can contain : LV (low voltage), HV (over voltage), LSOC (low battery), NONE (fine) [Default: vbat-alarm.log]\n"
"\t-vbatlogging, Set to 0 to disable battery stats data [Default: 1]\n"
"\t-vbatlowvoltage, Set low voltage warning, in volt [Default: 3.1]\n"
"\t-vbatlowsoc, Set low SOC warning, in percent [Default: 4]\n"
"\t-vbatoffset, Battery value offset, in volt, can be positive or negative [Default: 0.00]\n"
"\t-rsocextend, Extend RSOC value [Default: 100,0,100,0] [Example : FROM_MAX,FROM_MIN,TO_MAX,TO_MIN]\n"
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
	strcpy(vbat_alarm_filename,"vbat-alarm.log"); //init
	
	
	for(int i=1;i<argc;++i){ //argument to variable
		if(strcmp(argv[i],"-help")==0){show_usage();return 1;
		}else if(strcmp(argv[i],"-debug")==0){debug=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-i2cbus")==0){i2c_bus=atoi(argv[i+1]);if(strstr(argv[i+1],"-")){i2c_bus=-i2c_bus;}
		}else if(strcmp(argv[i],"-i2caddr")==0){sscanf(argv[i+1], "%x", &i2c_addr);
		}else if(strcmp(argv[i],"-register16")==0){strcpy(i2c_register16_raw,argv[i+1]);i2c_register16_count=0;
		
		}else if(strcmp(argv[i],"-updateduration")==0){update_duration=atoi(argv[i+1]);
		
		}else if(strcmp(argv[i],"-outputpath")==0){strcpy(vbat_output_path,argv[i+1]);if(access(vbat_output_path,W_OK)!=0){debug_print("Failed, %s not writable, Exiting\n",vbat_output_path);return 1;}
		}else if(strcmp(argv[i],"-vbatfilename")==0){strcpy(vbat_filename,argv[i+1]);
		}else if(strcmp(argv[i],"-vbatalarmfilename")==0){strcpy(vbat_alarm_filename,argv[i+1]);
		}else if(strcmp(argv[i],"-vbatstatsfilename")==0){strcpy(vbat_stats_filename,argv[i+1]);
		}else if(strcmp(argv[i],"-vbatlogging")==0){if(atoi(argv[i+1])<1){vbat_logging=false;}else{vbat_logging=true;}
		}else if(strcmp(argv[i],"-vbatlowvoltage")==0){vbat_warn_lv=atof(argv[i+1]);
		}else if(strcmp(argv[i],"-vbatlowsoc")==0){vbat_warn_lsoc=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-vbatoffset")==0){vbat_offset=atof(argv[i+1]);if(strstr(argv[i+1],"-")){vbat_offset=vbat_offset*-1;}
		}else if(strcmp(argv[i],"-rsocextend")==0){strcpy(rsoc_extend_raw,argv[i+1]);rsoc_extend_count=1;
		
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
	
	if(rsoc_extend_count==1){ //custom rsoc limits
		if(sscanf(rsoc_extend_raw,"%d,%d,%d,%d",&rsoc_extend[0],&rsoc_extend[1],&rsoc_extend[2],&rsoc_extend[3])==4){ //extract value, success
			debug_print("valid RSOC Extend value : FROM_MAX:%d ,FROM_MIN:%d ,TO_MAX:%d ,TO_MIN:%d\n",rsoc_extend[0],rsoc_extend[1],rsoc_extend[2],rsoc_extend[3]);
		}else{ //fail
			rsoc_extend[0]=100;rsoc_extend[1]=0;rsoc_extend[2]=100;rsoc_extend[3]=0;rsoc_extend_count=-1; //reset
			debug_print("Invalid RSOC Extend value\n");
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
	}else if(i2c_addr==0x36){MAX17048_detected=true; debug_print("MAX17048 detected\n");
	}else if(i2c_addr>=0x48&&i2c_addr<=0x4F){MCP3021A_detected=true; debug_print("MCP3021A detected\n");
	}else{debug_print("Failed, I2C address out of range, Exiting\n"); return(1);} //failed
	
	while(true){
		chdir(vbat_output_path); //change default dir
		i2c_retry=0; //reset retry counter
		if(debug==2){debug_print("Start update loop\n");}
		
		while(i2c_retry<3){
			adc_raw_value=-1; vbat_value=-1; vbat_percent_value=-1; temperature_k=-1;
			if((i2c_handle=open(i2c_path,O_RDWR))<0){
				debug_print("Failed to open the I2C bus : %s, retry in %dsec\n",i2c_path,update_duration);
				i2c_retry=3; //no need to retry since failed to open I2C bus itself
			}else{
				if(ioctl(i2c_handle,I2C_SLAVE,i2c_addr)<0){ //access i2c device, allow retry if failed
					debug_print("Failed to access I2C device : %02x, retry in 1sec\n",i2c_addr);
				}else{ //success
					if(LC709203F_detected){
						if(ioctl(i2c_handle,I2C_PEC,1)<0){ //enable pec
							debug_print("Failed to enable PEC for I2C device : %02x\n",i2c_addr);
						}else{
							if(!LC709203F_init){ //not init
								while(get_cpu_load()>2){usleep(500000);} //if CPU load over 2%, sleep 500ms
								LC709203F_write_reg(i2c_addr,0x15,0x0001); //IC Power Mode
								LC709203F_write_reg(i2c_addr,0x0B,0x002D); //APA
								LC709203F_write_reg(i2c_addr,0x12,0x0001); //Select battery profile
								LC709203F_write_reg(i2c_addr,0x16,0x0000); //Temperature via I2C
								LC709203F_write_reg(i2c_addr,0x08,0x0BA6); //Temperature at 25�C
								LC709203F_write_reg(i2c_addr,0x13,vbat_warn_lsoc); //Alarm Low RSOC
								LC709203F_write_reg(i2c_addr,0x14,(int)(vbat_warn_lv*1000)); //Alarm Low Cell Voltage
								for(int i=0;i<i2c_register16_count;i++){LC709203F_write_reg(i2c_addr,i2c_register16_reg[i],i2c_register16_value[i]);} //custom 16bits register
								//LC709203F_write_reg(i2c_addr,0x07,0xAA55); //Init RSOC
								//LC709203F_write_reg(i2c_addr,0x04,0xAA55); //Before RSOC
								LC709203F_init=true; //Init done
								debug_print("LC709203F initialized\n");
								sleep(1);
							}
							
							vbat_value=LC709203F_read_reg(i2c_addr,0x09)/1000.; //Cell Voltage register
							vbat_percent_value=LC709203F_read_reg(i2c_addr,0x0D); //RSOC register
							temperature_k=(float)LC709203F_read_reg(i2c_addr,0x08)/10; //Cell Temperature in Kelvin
						}
					}else if(MAX17048_detected){
						if(!MAX17048_init){ //not init
							write_reg16(0x0A,0x0000,false); //HIBRT, disable
							//write_reg16(0x14,0x9BDC,false); //VALRT, low: 3.1v hi:4.4v
							write_reg16(0x14,((int)(1+vbat_warn_lv/0.02)<<8)|0xDC,false); //VALRT, low: vbat_warn_lv hi:4.4v
							write_reg16(0x0C,0x9700|(32-vbat_warn_lsoc),false); //CONFIG, low soc
							write_reg16(0x1A,0x00FF,false); //STATUS, reset alarm
							for(int i=0;i<i2c_register16_count;i++){write_reg16(i2c_register16_reg[i],i2c_register16_value[i],false);} //custom 16bits register
							MAX17048_init=true; //Init done
							debug_print("MAX17048 initialized\n");
							sleep(1);
						}
						
						vbat_value=read_reg16(0x02,false)*(double)0.000078125; //Cell Voltage register
						vbat_percent_value=read_reg16(0x04,false)/256; //SOC register
						vbat_alarm=read_reg16(0x1A,false); //STATUS register
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
						
						temp_filehandle=fopen(vbat_alarm_filename,"wb"); //open file handle
						if(MAX17048_detected&&vbat_alarm!=255){ //alarm MAX17048
							debug_print("STATUS register : 0x%04X\n",vbat_alarm);
							if((0x0400&vbat_alarm)==0x0400){debug_print("Warning : Low voltage\n"); fputs("LV\n",temp_filehandle);}
							if((0x0200&vbat_alarm)==0x0200){debug_print("Warning : Over voltage\n"); fputs("HV\n",temp_filehandle);}
							if((0x1000&vbat_alarm)==0x1000){debug_print("Warning : SOC %d%\n",vbat_warn_lsoc); fputs("LSOC\n",temp_filehandle);}
							if(vbat_percent_value>vbat_warn_lsoc){debug_print("Reset STATUS register\n");write_reg16(0x1A,0x00FF,false);} //STATUS, reset alarm
						}else if(vbat_percent_value<=vbat_warn_lsoc){debug_print("Warning : SOC %d%,%d\n",vbat_warn_lsoc,vbat_percent_value); fputs("LSOC\n",temp_filehandle); //low soc alarm
						}else if(vbat_value<=vbat_warn_lv){debug_print("Warning : Low voltage\n"); fputs("LV\n",temp_filehandle); //low voltage alarm
						}else{fputs("NONE\n",temp_filehandle);} //nothing wrong
						fclose(temp_filehandle); //close handle
						
						if(rsoc_extend_count==1){ //custom rsoc limits
							vbat_percent_value=nns_map_int(vbat_percent_value,rsoc_extend[1],rsoc_extend[0],rsoc_extend[3],rsoc_extend[2]);
							debug_print("Extended RSOC : %d%%\n",vbat_percent_value);
						}
						
						vbat_value+=vbat_offset; //add adc chip error offset
						temp_filehandle=fopen(vbat_filename,"wb"); fprintf(temp_filehandle,"%.3f;%d",vbat_value,vbat_percent_value); fclose(temp_filehandle); //write log file
						if(vbat_logging){ //cumulative cumulative log file
							temp_filehandle=fopen("/proc/uptime","r"); fscanf(temp_filehandle,"%u",&uptime_value); fclose(temp_filehandle); //get system uptime
							temp_filehandle=fopen(vbat_stats_filename,"a+"); fprintf(temp_filehandle,"%u;%.3f;%d\n",uptime_value,vbat_value,vbat_percent_value); fclose(temp_filehandle); //write cumulative log file
						}
					}
					
					if(temperature_k<0&&LC709203F_detected){debug_print("Warning, Cell Temperature probing failed\n");
					}else if(LC709203F_detected){debug_print("Cell Temperature : %.1fK, %.1fC, %.1fF\n",temperature_k,temperature_k-273.15,temperature_k*9/5-459.67);}
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