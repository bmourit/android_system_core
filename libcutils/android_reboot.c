/*
 * Copyright 2011, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unistd.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <cutils/android_reboot.h>

/* Check to see if /proc/mounts contains any writeable filesystems
 * backed by a block device.
 * Return true if none found, else return false.
 */
static int remount_ro_done(void)
{
    FILE *f;
    char mount_dev[256];
    char mount_dir[256];
    char mount_type[256];
    char mount_opts[256];
    int mount_freq;
    int mount_passno;
    int match;
    int found_rw_fs = 0;

    f = fopen("/proc/mounts", "r");
    if (! f) {
        /* If we can't read /proc/mounts, just give up */
        return 1;
    }

    do {
        match = fscanf(f, "%255s %255s %255s %255s %d %d\n",
                       mount_dev, mount_dir, mount_type,
                       mount_opts, &mount_freq, &mount_passno);
        mount_dev[255] = 0;
        mount_dir[255] = 0;
        mount_type[255] = 0;
        mount_opts[255] = 0;
        if ((match == 6) && !strncmp(mount_dev, "/dev/block", 10) && strstr(mount_opts, "rw")) {
            found_rw_fs = 1;
            break;
        }
    } while (match != EOF);

    fclose(f);

    return !found_rw_fs;
}

/* Remounting filesystems read-only is difficult when there are files
 * opened for writing or pending deletes on the filesystem.  There is
 * no way to force the remount with the mount(2) syscall.  The magic sysrq
 * 'u' command does an emergency remount read-only on all writable filesystems
 * that have a block device (i.e. not tmpfs filesystems) by calling
 * emergency_remount(), which knows how to force the remount to read-only.
 * Unfortunately, that is asynchronous, and just schedules the work and
 * returns.  The best way to determine if it is done is to read /proc/mounts
 * repeatedly until there are no more writable filesystems mounted on
 * block devices.
 */
static void remount_ro(void)
{
    int fd, cnt = 0;

    /* Trigger the remount of the filesystems as read-only,
     * which also marks them clean.
     */
    fd = open("/proc/sysrq-trigger", O_WRONLY);
    if (fd < 0) {
        return;
    }
    write(fd, "u", 1);
    close(fd);


    /* Now poll /proc/mounts till it's done */
    while (!remount_ro_done() && (cnt < 50)) {
        usleep(100000);
        cnt++;
    }

    return;
}

#define READ_BUF_SIZE 256

int find_pid_byname(char* pidName)
{
    DIR *dir;
    struct dirent *next;
    FILE *status;
    char filename[READ_BUF_SIZE];
    char buffer[READ_BUF_SIZE];
    char name[READ_BUF_SIZE];
    int i = 0;
    
    dir = opendir("/proc");
    if(!dir)
    {
        //__android_log_print(ANDROID_LOG_ERROR, "libcutils", "Cannot open /proc");
        return -1;
    }
    
    while((next = readdir(dir)) != NULL)
    {
        /*   Must skip ".." since that is outside /proc*/ 
        if(strcmp(next->d_name, "..") == 0) 
            continue; 
        
        /*   If it isn't a number, we don't want it*/ 
        if(!isdigit(*next->d_name))
            continue;
        
        sprintf(filename, "/proc/%s/status", next->d_name);
        if(!(status = fopen(filename, "r"))) 
            continue;

        /*Read first line in /proc/pid/status*/ 
        if(fgets(buffer, READ_BUF_SIZE-1, status) == NULL)
        {
            fclose(status);
            continue;
        }
        fclose(status);
       
        /*Buffer should contain a string like "Name:binary_name"*/
        sscanf(buffer, "%*s %s", name);
        if(strcmp(name, pidName) == 0)
        {
            //__android_log_print(ANDROID_LOG_ERROR, "libcutils", "kill [%s]:[%d]\n", name, strtol(next->d_name, NULL, 0));
            return strtol(next->d_name, NULL, 0);
        }
    }
    return -1;
} 
static int kill_by_name(char *process_name)
{
    pid_t pid;
    
    pid = find_pid_byname(process_name);
    if(pid >= 0)
        kill(pid, SIGTERM);
    return 0;
}

static int hibernate(void)
{
    int fd;

    kill_by_name("ndroid.launcher");
    
    sync();

    fd = open("/misc/boot.prt", O_RDONLY);
    if (fd >= 0) {
        close(fd);
    }
    else
    {
        fd = open("/proc/sys/kernel/printk", O_WRONLY);
        if (fd >= 0) {
            write(fd, "1", 1);
            close(fd);
        }
    }

    /* Trigger the remount of the filesystems as read-only,
     * which also marks them clean.
     */
    fd = open("/sys/power/state", O_WRONLY);
    if (fd < 0) {
        return -1;
    }
    write(fd, "disk", 4);
    close(fd);

    return 0;
}


static char governor[32];
static char *scaling_governor = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
static unsigned int cnt;

static int hibernate_perform(void)
{
    kill_by_name("mediaserver");

    return 0;
}

static int hibernate_prepare(void)
{
    int fd;
    
    cnt = 0;
    
    fd = open(scaling_governor, O_RDONLY);
    if (fd >= 0) {
        cnt = read(fd, governor, sizeof(governor));
        close(fd);

        if(cnt > 0 && cnt <= sizeof(governor)) {
            fd = open(scaling_governor, O_WRONLY);
            if (fd >= 0) {
                write(fd, "performance", 11);
                close(fd);
            }
        }
    }
    
    fd = open("/sys/power/state", O_WRONLY);
    if (fd >= 0) {
        write(fd, "prepare", 7);
        close(fd);
    }

    return 0;
}

static int hibernate_screenOnOff(char *onoff)
{
    int fd;

    if(strcmp(onoff, "on") == 0)
        fd = open("/sys/power/wake_unlock", O_WRONLY);
    else
        fd = open("/sys/power/wake_lock", O_WRONLY);
        
    if (fd >= 0) {
        write(fd, "hibernate", 9);
        close(fd);
    }

    fd = open("/sys/power/state", O_WRONLY);
    if (fd < 0) {
        return -1;
    }
    if(strcmp(onoff, "on") == 0)
        write(fd, "on", 2);
    else
        write(fd, "mem", 3);
    close(fd);
    
    return 0;
}

static int hibernate_resume()
{
    int fd;

    if(cnt > 0 && cnt <= sizeof(governor)) {
        fd = open(scaling_governor, O_WRONLY);
        if (fd >= 0) {
            write(fd, governor, cnt);
            close(fd);
        }
    }
    cnt = 0;
    
    fd = open("/sys/power/state", O_WRONLY);
    if (fd >= 0) {
        write(fd, "resume", 6);
        close(fd);
    }

    fd = open("/misc/boot.prt", O_RDONLY);
    if (fd >= 0) {
        close(fd);
    }
    else
    {
        fd = open("/proc/sys/kernel/printk", O_WRONLY);
        if (fd >= 0) {
            write(fd, "5", 1);
            close(fd);
        }
    }

    return 0;
}

static int hibernate_kill(char *arg)
{
    int pid = strtol(arg, NULL, 0);
    
    if(pid >= 0)
        kill(pid, SIGTERM);
        
    return 0;
}

int android_reboot(int cmd, int flags, char *arg)
{
    int ret;

    if(cmd == (int)ANDROID_RB_RESTART2) {
        if(strcmp(arg, "perform") == 0) {
            ret = hibernate_perform();
            return ret;
        }
        else if(strcmp(arg, "prepare") == 0) {
            ret = hibernate_prepare();
            return ret;
        }
        else if(strcmp(arg, "shutdown") == 0) {
            ret = hibernate();
            return ret;
        }
        else if(strcmp(arg, "screenOn") == 0) {
            ret = hibernate_screenOnOff("on");
            return ret;
        }
        else if(strcmp(arg, "screenOff") == 0) {
            ret = hibernate_screenOnOff("off");
            return ret;
        }
        else if(strcmp(arg, "resume") == 0) {
            ret = hibernate_resume();
            return ret;
        }
        else if(strncmp(arg, "kill ", 5) == 0) {
            ret = hibernate_kill(arg + 5);
            return ret;
        }
    }

//    hibernate_screenOnOff("off");
    
    if (!(flags & ANDROID_RB_FLAG_NO_SYNC))
        sync();

    if (!(flags & ANDROID_RB_FLAG_NO_REMOUNT_RO))
        remount_ro();

    switch (cmd) {
        case ANDROID_RB_RESTART:
            ret = reboot(RB_AUTOBOOT);
            break;

        case ANDROID_RB_POWEROFF:
            ret = reboot(RB_POWER_OFF);
            break;

        case ANDROID_RB_RESTART2:
            ret = __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                       LINUX_REBOOT_CMD_RESTART2, arg);
            break;

        default:
            ret = -1;
    }

    return ret;
}

