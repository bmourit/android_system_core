#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <errno.h>
#include <stdarg.h>
#include <mtd/mtd-user.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>   
#include <sys/select.h>   
#include <errno.h>   
#include <sys/inotify.h>  
#include <pthread.h>
#include <sys/sysinfo.h>
#include "property_service.h"
#include "log.h"

static int work_done=0;
static void do_some_hack(void)
{
	
	int ret;
	int fd;
	char *prefs="/data/data/com.google.android.inputmethod.latin/shared_prefs/com.google.android.inputmethod.latin_preferences.xml";
	char *prefs_dir1="/data/data/com.google.android.inputmethod.latin";
	char *prefs_dir2="/data/data/com.google.android.inputmethod.latin/shared_prefs";
	char *data= "<?xml version='1.0' encoding='utf-8' standalone='yes' ?> \n\
	<map> \n\
	<boolean name=\"enable_sound_on_keypress\" value=\"true\" /> \n\
    <int name=\"HAD_FIRST_RUN\" value=\"1\" />     \n\
    </map> \n";
	
	ret=mkdir(prefs_dir1, S_IRWXU|S_IRGRP|S_IWGRP|S_IRWXO);
	if(ret!=0){
		ERROR("failed to create %s \n", prefs_dir1);
	}
	mkdir(prefs_dir2, S_IRWXU|S_IRGRP|S_IWGRP|S_IRWXO);
	if(ret!=0){
		ERROR("failed to create %s \n", prefs_dir2);
	}
	fd=open(prefs,O_CREAT|O_RDWR|O_TRUNC,S_IRWXU|S_IRGRP|S_IWGRP|S_IRWXO);
	if(fd<0){
		ERROR("failed to open %s\n", prefs);
		return;
	}
	write(fd, data, strlen(data));
	close(fd);
}

static void firstwrite_event_handler(struct inotify_event *event){
         if(event&&(event->mask&IN_CREATE)){
        		if(!strcmp(event->name, "com.google.android.inputmethod.latin")){
        			 do_some_hack();
        			 work_done=1;
        		}
        }
}

static  void * monitor_first_write(void *p) {
	unsigned char buf[1024] = {0};   
  	struct inotify_event *event = NULL;               
	char *monitordir="/data/data";
	int testfd=open("/data", O_RDONLY);
	INFO("start monitor");
	int fd = inotify_init(); 
	//int wd = inotify_add_watch(fd, monitordir, IN_CREATE|IN_ISDIR|IN_ONLYDIR);
	int wd = inotify_add_watch(fd, monitordir, IN_CREATE);	
	do {   
		fd_set fds;   
		FD_ZERO(&fds);                
		FD_SET(fd, &fds);   
		if (select(fd + 1, &fds, NULL, NULL, NULL) > 0) {   
			int len, index = 0;   
			while (((len = read(fd, &buf, sizeof(buf))) < 0) && (errno == EINTR)) {
			}
			while (index < len) {   
				  event = (struct inotify_event *)(buf + index);                       
				  firstwrite_event_handler(event);
				  index += sizeof(struct inotify_event) + event->len;
			}   
		}   
  	}
  	while(work_done==0);   
    INFO("end of monitor\n");
    inotify_rm_watch(fd, wd);
	return 0; 
}  

static void start_monitor_thread(void) {
	pthread_t thread1;
	int err;
	err = pthread_create(&thread1,NULL,monitor_first_write,NULL);
	if(err != 0) {
		ERROR("can't create thread1: %s\n",strerror(err));
		return ;
	}
}

void do_hack_update_system_prop(void){
	struct sysinfo s_info;
	int error;
	int valueint;
	char value[1024];
	const char *valuestr;
	int sdram_cap=0;
	valuestr=property_get("system.ram.total");
	if(valuestr){
		sdram_cap=atoi(valuestr);
	}
	if(sdram_cap>100 &&sdram_cap<4096){
		//property define in product build, no need to detect
		if(sdram_cap<=512){
			property_set("ro.skia.min.font.cache", "262144");
			property_set("ro.skia.font.cache", "2097152");
		}
		return ;
	} 
	error = sysinfo(&s_info);
	valueint=s_info.totalram/(1024*1024);
	ERROR("total=%d", valueint);
	if(valueint>300 &&valueint<=512){
		valueint=512;
		property_set("ro.skia.min.font.cache", "262144");
		property_set("ro.skia.font.cache", "2097152");
	}else if (valueint>512 &&valueint<1024){
		valueint=1024;
	}
	sprintf(value, "%d",valueint );
	ERROR("value=%s", value);
	property_set("system.ram.total", value);

}

int do_hack(int argc , char **argv) {
	int fd;
	const char *dohack;
	char *entropy="/data/system/entropy.dat";
	do_hack_update_system_prop();
	//for first time: /data/system/entropy.dat not exist
	dohack = property_get("ro.im.keysounddefenable");
	if(!dohack){
		return 0;
	}
	if(strcmp(dohack, "true")){
		return 0;
	}
	
	fd = open(entropy, O_RDONLY);
	if(fd>=0){
		close(fd);
		ERROR("failed to open %s\n", entropy);
		return 0;
	}
	start_monitor_thread();
	return 0;

}

