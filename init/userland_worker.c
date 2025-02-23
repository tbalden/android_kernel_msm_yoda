// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Vlad Adumitroaie <celtare21@gmail.com>.
 * Copyright (C) 2020 Pal Illes @tbalden at github - built in binaries and refactors
 */

#define pr_fmt(fmt) "userland_worker: " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/security.h>
#include <linux/namei.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/userland.h>

#include <linux/kernel.h>
#include <linux/sched/signal.h>

//file operation+
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/vmalloc.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/mm.h>
//file operation-

#include <linux/uci/uci.h>

#define LEN(arr) ((int) (sizeof (arr) / sizeof (arr)[0]))
#define INITIAL_SIZE 4
#define MAX_CHAR 128
#define SHORT_DELAY 10
#define DELAY 500
#define LONG_DELAY 10000

// don't use permissive, uncomment if you need for testing
//#define USE_PERMISSIVE

#define USE_ENCRYPTED
//#define RUN_ENCRYPTED_LATE
//#define USE_DECRYPTED

// use resetprops part, to set properties for safetynet and other things
//#define USE_RESET_PROPS
//#define RUN_RESET_PROPS_BEFORE_DECRYPT
//#define RUN_RESET_PROPS_AFTER_DECRYPT

#define USE_PACKED_HOSTS
// define this if you can use scripts .sh files
#define USE_SCRIPTS

//#define USE_SYSTOOLS_CALL

#define BIN_SH "/system/bin/sh"
#define BIN_CHMOD "/system/bin/chmod"
#define BIN_SETPROP "/system/bin/setprop"

// path differences =========================================
#ifdef CONFIG_USERLAND_WORKER_DATA_LOCAL

#define BIN_RESETPROP "/data/local/tmp/resetprop_static"
#define BIN_RESETPROPS_SH "/data/local/tmp/resetprops.sh"
#define BIN_OVERLAY_SH "/data/local/tmp/overlay.sh"
#define BIN_KERNELLOG_SH "/data/local/tmp/kernellog.sh"
#define BIN_SYSTOOLS_SH "/data/local/tmp/systools.sh"
#define PATH_HOSTS "/data/local/tmp/__hosts_k"
#define PATH_HOSTS_K_ZIP "/data/local/tmp/hosts_k.zip"
#define PATH_SYSHOSTS "/data/local/tmp/sys_hosts"
#define PATH_UCI_DMESG "/data/local/tmp/uci-cs-dmesg.txt"
#define PATH_UCI_RAMOOPS "/data/local/tmp/console-ramoops-0.txt"
#define UNZIP_PATH "-d /data/local/tmp/ -o"

#else

#define BIN_RESETPROP "/dev/resetprop_static"
#define BIN_RESETPROPS_SH "/dev/resetprops.sh"
#define BIN_OVERLAY_SH "/dev/overlay.sh"
#define BIN_KERNELLOG_SH "/dev/kernellog.sh"
#define BIN_SYSTOOLS_SH "/dev/systools.sh"
#define PATH_HOSTS "/dev/__hosts_k"
#define PATH_HOSTS_K_ZIP "/dev/hosts_k.zip"
#define PATH_SYSHOSTS "/dev/sys_hosts"
#define PATH_UCI_DMESG "/dev/uci-cs-dmesg.txt"
#define PATH_UCI_RAMOOPS "/dev/console-ramoops-0.txt"
#define UNZIP_PATH "-d /dev/ -o"

#endif
// ==========================================================

#define SDCARD_HOSTS "/storage/emulated/0/__hosts_k"

#ifdef USE_PACKED_HOSTS
// packed hosts_k.zip
#define HOSTS_K_ZIP_FILE                      "../binaries/hosts_k_zip.i"
u8 hosts_k_zip_file[] = {
#include HOSTS_K_ZIP_FILE
};
#endif


// binary file to byte array
#define RESETPROP_FILE                      "../binaries/resetprop_static.i"
u8 resetprop_file[] = {
#include RESETPROP_FILE
};

// binary file to byte array
#ifdef CONFIG_USERLAND_WORKER_DATA_LOCAL
#define RESETPROPS_SH_FILE                      "../binaries/resetprops_sh.i"
#else
#define RESETPROPS_SH_FILE                      "../binaries/resetprops_data_sh.i"
#endif
u8 resetprops_sh_file[] = {
#include RESETPROPS_SH_FILE
};

// overlay sh to byte array
#ifdef CONFIG_USERLAND_WORKER_DATA_LOCAL
#define OVERLAY_SH_FILE                      "../binaries/overlay_data_sh.i"
#else
#define OVERLAY_SH_FILE                      "../binaries/overlay_sh.i"
#endif
u8 overlay_sh_file[] = {
#include OVERLAY_SH_FILE
};

// kernellog sh to byte array
#ifdef CONFIG_USERLAND_WORKER_DATA_LOCAL
#define KERNELLOG_SH_FILE                      "../binaries/kernellog_data_sh.i"
#else
#define KERNELLOG_SH_FILE                      "../binaries/kernellog_sh.i"
#endif
u8 kernellog_sh_file[] = {
#include KERNELLOG_SH_FILE
};

// systools sh to byte array
#ifdef CONFIG_USERLAND_WORKER_DATA_LOCAL
#define SYSTOOLS_SH_FILE                      "../binaries/systools_data_sh.i"
#else
#define SYSTOOLS_SH_FILE                      "../binaries/systools_sh.i"
#endif
u8 systools_sh_file[] = {
#include SYSTOOLS_SH_FILE
};



extern void set_kernel_permissive(bool on);
extern void set_full_permissive_kernel_suppressed(bool on);
extern void set_kernel_pemissive_user_mount_access(bool on);

// file operations
static int uci_fwrite(struct file* file, loff_t pos, unsigned char* data, unsigned int size) {
    int ret;
    ret = kernel_write(file, data, size, &pos);
    return ret;
}

static int uci_read(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
    int ret;
    ret = kernel_read(file, data, size, &offset);
    return ret;
}


static void uci_fclose(struct file* file) {
    fput(file);
}

static struct file* uci_fopen(const char* path, int flags, int rights) {
    struct file* filp = NULL;
    int err = 0;

    filp = filp_open(path, flags, rights);

    if(IS_ERR(filp)) {
        err = PTR_ERR(filp);
        pr_err("[uci]File Open Error:%s %d\n",path, err);
        return NULL;
    }
    if(!filp->f_op){
        pr_err("[uci]File Operation Method Error!!\n");
        return NULL;
    }

    return filp;
}


static int write_file(char *filename, unsigned char* data, int length, int rights) {
        struct file*fp = NULL;
        int rc = 0;
        loff_t pos = 0;

	fp=uci_fopen (filename, O_RDWR | O_CREAT | O_TRUNC, rights);

        if (fp) {
		while (true) {
	                rc = uci_fwrite(fp,pos,data,length);
			if (rc<0) break; // error
			if (rc==0) break; // all done
			pos+=rc; // increase file pos with written bytes number...
			data+=rc; // step in source data array pointer with written bytes number...
			length-=rc; // decrease to be written length
		}
                if (rc) pr_info("%s [CLEANSLATE] uci error file kernel out %s...%d\n",__func__,filename,rc);
                vfs_fsync(fp,1);
                uci_fclose(fp);
                pr_info("%s [CLEANSLATE] uci closed file kernel out... %s\n",__func__,filename);
		return 0;
        }
	return -EINVAL;
}
static int write_files(void) {
	int rc = 0;
#ifdef USE_RESET_PROPS
	// pixel4 stuff
	rc = write_file(BIN_RESETPROP,resetprop_file,sizeof(resetprop_file),0755);
	if (rc) goto exit;
	rc = write_file(BIN_RESETPROPS_SH,resetprops_sh_file,sizeof(resetprops_sh_file),0755);
	if (rc) goto exit;
#endif
	rc = write_file(BIN_OVERLAY_SH,overlay_sh_file,sizeof(overlay_sh_file),0755);
	if (rc) goto exit;
	rc = write_file(BIN_KERNELLOG_SH,kernellog_sh_file,sizeof(kernellog_sh_file),0755);
	if (rc) goto exit;
	rc = write_file(BIN_SYSTOOLS_SH,systools_sh_file,sizeof(systools_sh_file),0755);
#ifdef USE_PACKED_HOSTS
	if (rc) goto exit;
	rc = write_file(PATH_HOSTS_K_ZIP,hosts_k_zip_file,sizeof(hosts_k_zip_file),0644);
#endif
exit:
	return rc;
}

// be aware that writing to sdcardfs needs a file creation from userspace app,.
// ...otherwise encrpytion key for file cannot be added. Make sure to touch files from app!
#define CP_BLOCK_SIZE 10000
#define MAX_COPY_SIZE 4000000
static int copy_files(char *src_file, char *dst_file, int max_len,  bool only_trunc){
	int rc =0;
	int drc = 0;

        struct file* dfp = NULL;
        loff_t dpos = 0;

	dfp=uci_fopen (dst_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
	pr_info("%s opening dest file.\n",__func__);
	if (!only_trunc && dfp) {
		struct file* sfp = NULL;
		off_t fsize;
		char *buf;
		sfp=uci_fopen (src_file, O_RDONLY, 0);
		pr_info("%s opening src file.\n",__func__);
		if (sfp) {
			unsigned long long offset = 0;

			fsize=sfp->f_inode->i_size;
			if (fsize>max_len) {
				// copy only the last "max_len" bytes of the file, too big otherwise
				offset = fsize - max_len;
			}
			pr_info("%s src file size: %d , copyting from offset: %d \n",__func__,fsize, offset);
			buf=(char *) kmalloc(CP_BLOCK_SIZE, GFP_KERNEL);

			while(true) {
				rc = uci_read(sfp, offset, buf, CP_BLOCK_SIZE);
				//pr_info("%s src file read... %d \n",__func__,rc);
				if (rc<0) break; // error
				if (rc==0) break; // all done
				offset+=rc; // increase file pos with read bytes number...
				//length-=rc; // decrease to be read length
				drc = uci_fwrite(dfp, dpos, buf, rc);
				dpos+=drc; // increase file pos with written bytes number...
				//pr_info("%s dst file written... %d \n",__func__,drc);
				if (drc<0) break;
				if (drc==0) break;
			}
			kfree(buf);
		}
                if (!sfp || rc || drc<0) pr_info("%s [CLEANSLATE] uci error file copy %s %s...%d\n",__func__,src_file,dst_file,rc);
		if (sfp) {
			vfs_fsync(sfp,1);
	                uci_fclose(sfp);
		}
		vfs_fsync(dfp,1);
                uci_fclose(dfp);
                pr_info("%s [CLEANSLATE] uci closed file kernel copy... %s %s\n",__func__,src_file,dst_file);

		return rc;
	}
	if (only_trunc && dfp) {
		vfs_fsync(dfp,1);
                uci_fclose(dfp);
                pr_info("%s [CLEANSLATE] uci truncated file... %s %s\n",__func__,src_file,dst_file);
		return 0;
	}

        pr_info("%s [CLEANSLATE] uci error file copy %s %s...%d\n",__func__,src_file,dst_file,rc);
	return -1;
}

static struct delayed_work userland_work;

static const struct file_operations proc_file_fops = {
	.owner = THIS_MODULE,
};

static void free_memory(char** argv, int size)
{
	int i;

	for (i = 0; i < size; i++)
		kfree(argv[i]);
	kfree(argv);
}

static char** alloc_memory(int size)
{
	char** argv;
	int i;

	argv = kmalloc(size * sizeof(char*), GFP_KERNEL);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return NULL;
	}

	for (i = 0; i < size; i++) {
		argv[i] = kmalloc(MAX_CHAR * sizeof(char), GFP_KERNEL);
		if (!argv[i]) {
			pr_err("Couldn't allocate memory!");
			kfree(argv);
			return NULL;
		}
	}

	return argv;
}

static int use_userspace(char** argv)
{
        static char *envp[] = {
		"SHELL=/bin/sh",
                "HOME=/",
		"USER=shell",
                "TERM=linux",
		"PATH=/bin:/sbin:/product/bin:/apex/com.android.runtime/bin:/apex/com.android.art/bin:/system_ext/bin:/system/bin:/system/xbin:/odm/bin:/vendor/bin:/vendor/xbin",
		"DISPLAY=:0",
                NULL
        };
        struct subprocess_info *info;
        info = call_usermodehelper_setup(argv[0], argv, envp, GFP_KERNEL,
                                         NULL, NULL, NULL);
        if (!info) {
		pr_err("%s cannot call usermodehelper setup - info NULL\n",__func__);
		return -EINVAL;
	}
	// in case of CONFIG_STATIC_USERMODEHELPER=y, we must override the empty path that usually is set, and calls won't do anything
	info->path = argv[0];
        return call_usermodehelper_exec(info, UMH_WAIT_EXEC | UMH_KILLABLE);
}

static int call_userspace(char *binary, char *param0, char *param1, char *message_text) {
	char** argv;
	int ret;
	argv = alloc_memory(INITIAL_SIZE);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return -ENOMEM;
	}
	strcpy(argv[0], binary);
	strcpy(argv[1], param0);
	strcpy(argv[2], param1);
	argv[3] = NULL;
	ret = use_userspace(argv);
	if (!ret) msleep(5);

	free_memory(argv, INITIAL_SIZE);

	if (!ret) {
		pr_info("%s call succeeded '%s' . rc = %u\n",__func__,message_text,ret);
	} else {
		pr_err("%s call error '%s' . rc = %u\n",__func__,message_text,ret);
	}
	return ret;
}

static inline void __set_selinux(int value)
{
	pr_info("%s Setting selinux state: %d", __func__, value);
	set_selinux(value);
}

static bool on_boot_selinux_mode_read = false;
static bool on_boot_selinux_mode = false;
DEFINE_MUTEX(enforce_mutex);

static void set_selinux_enforcing_2(bool enforcing, bool full_permissive, bool dont_supress_full_permissive) {
#ifdef USE_PERMISSIVE
	full_permissive = true;
#endif
	if (!full_permissive) {
		set_kernel_permissive(!enforcing);
		// enable fs accesses in /fs driver parts
		set_kernel_pemissive_user_mount_access(!enforcing);
		msleep(40);
	} else {
		bool is_enforcing = false;

		set_kernel_permissive(!enforcing);

		mutex_lock(&enforce_mutex);
		while (get_extern_state()==NULL) {
			msleep(10);
		}

		is_enforcing = get_enforce_value();

		if (!on_boot_selinux_mode_read) {
			on_boot_selinux_mode_read = true;
			on_boot_selinux_mode = is_enforcing;
		}

#ifndef USE_PERMISSIVE
		if (dont_supress_full_permissive) {
			set_full_permissive_kernel_suppressed(false);
			// enable fs accesses in /fs driver parts (full permissive suppression would block these as file access is in-kernel blocked)
			set_kernel_pemissive_user_mount_access(!enforcing);
		} else
		if (on_boot_selinux_mode) { // system is by default SELinux enforced...
			// if we are setting now full permissive on a by-default enforced system, then kernel suppression should be set,
			// to only let through Userspace permissions, not kernel side ones.
			pr_info("%s [userland] kernel permissive : setting full permissive kernel suppressed: %u\n",!enforcing);
			set_full_permissive_kernel_suppressed(!enforcing);
			// enable fs accesses in /fs driver parts (full permissive suppression would block these as file access is in-kernel blocked)
			set_kernel_pemissive_user_mount_access(!enforcing);
		}
#endif

		// nothing to do?
		if (enforcing == is_enforcing) goto exit;

		// change to permissive?
		if (is_enforcing && !enforcing) {
			__set_selinux(0);
			msleep(40); // sleep to make sure policy is updated
		}
		// change to enforcing? only if on-boot it was enforcing
		if (!is_enforcing && enforcing && on_boot_selinux_mode)
			__set_selinux(1);
exit:
		mutex_unlock(&enforce_mutex);
	}
}

static void set_selinux_enforcing(bool enforcing, bool full_permissive) {
	set_selinux_enforcing_2(enforcing, full_permissive, false);
}


static void sync_fs(void) {
	int ret = 0;
	ret = call_userspace(BIN_SH,
		"-c", "/system/bin/sync", "userland sync");
	// Wait for RCU grace period to end for the files to sync
	rcu_barrier();
	msleep(10);
}

static int overlay_system_etc(void) {
	int ret, retries = 0;

        do {
		ret = call_userspace("/system/bin/cp",
			"/system/etc/hosts", PATH_SYSHOSTS, "cp sys_hosts");
		if (ret) {
		    pr_info("%s can't copy system hosts yet. sleep...\n",__func__);
		    msleep(DELAY);
		}
	} while (ret && retries++ < 20);
        if (!ret)
                pr_info("%s userland: 0",__func__);
        else {
                pr_err("%s userland: COULDN'T access system/etc/hosts, exiting",__func__);
		return ret;
	}

	sync_fs();
	if (!ret) {
		ret = call_userspace(BIN_SH,
			"-c", BIN_OVERLAY_SH, "sh overlay 9.1");
	}
	sync_fs();
	return ret;
}

DEFINE_MUTEX(kernellog_mutex);

static void kernellog_call(void) {
		int ret;
		ret = call_userspace(BIN_SH,
			"-c", BIN_KERNELLOG_SH, "sh kernellog");
		msleep(3000);
		ret = copy_files(PATH_UCI_DMESG,"/storage/emulated/0/__uci-cs-dmesg.txt",MAX_COPY_SIZE,false);
	        if (!ret)
	                pr_info("%s copy cs dmesg: 0\n",__func__);
	        else {
	                pr_err("%s userland: COULDN'T copy dmesg %u\n",__func__,ret);
		}
		ret = copy_files(PATH_UCI_RAMOOPS,"/storage/emulated/0/__console-ramoops-0.txt",MAX_COPY_SIZE,false);
	        if (!ret)
	                pr_info("%s copy cs ramoops: 0\n",__func__);
	        else {
	                pr_err("%s userland: COULDN'T copy ramoops %u\n",__func__,ret);
		}

		msleep(100);
}

static void kernellog_call_work_func(struct work_struct * kernellog_call_work)
{
	if (mutex_trylock(&kernellog_mutex)) {
		set_selinux_enforcing(false,false);
		sync_fs();
		kernellog_call();
		sync_fs();
		set_selinux_enforcing(true,false);
		mutex_unlock(&kernellog_mutex);
	}
}
static DECLARE_WORK(kernellog_call_work, kernellog_call_work_func);

DEFINE_MUTEX(systools_mutex);


char *current_ssid = NULL;

static bool userland_init_done = false;
static bool initial_output_wifi_name_done = false;

static void output_wifi_name_work_func(struct work_struct * output_wifi_name_work) {
	if (current_ssid!=NULL)
	{
		initial_output_wifi_name_done = true;
		pr_info("%s wifi systools current ssid = %s size %d len %d\n",__func__,current_ssid, sizeof(current_ssid), strlen(current_ssid));
		set_selinux_enforcing(false,false); // needs full permissive for dumpsys
		sync_fs();
		write_file("/storage/emulated/0/__cs-systools.txt",current_ssid, strlen(current_ssid),0644);
		sync_fs();
		set_selinux_enforcing(true,false);
	}
}
static DECLARE_WORK(output_wifi_name_work, output_wifi_name_work_func);


void uci_set_current_ssid(const char *name) {
	if (!current_ssid) {
		current_ssid = kmalloc(33 * sizeof(char*), GFP_KERNEL);
	}
	strcpy(current_ssid,name);
	if (userland_init_done) {
		schedule_work(&output_wifi_name_work);
	}
}
EXPORT_SYMBOL(uci_set_current_ssid);

#ifdef USE_SYSTOOLS_CALL
static void systools_call(char *command) {
	if (mutex_trylock(&systools_mutex)) {
#if 1
	if (!initial_output_wifi_name_done && userland_init_done) {
		schedule_work(&output_wifi_name_work);
	}
#else
		int ret;
		ret = call_userspace(BIN_SH,
			"-c", BIN_SYSTOOLS_SH, "sh systools");
                ret = copy_files("/data/local/tmp/cs-systools.txt","/storage/emulated/0/__cs-systools.txt",MAX_COPY_SIZE,false);
                if (!ret)
                        pr_info("%s copy cs systools: 0\n",__func__);
                else {
                        pr_err("%s userland: COULDN'T copy systools %u\n",__func__,ret);
                }
		msleep(3000);
		sync_fs();
#endif
		mutex_unlock(&systools_mutex);
	}
}
#endif

#ifdef USE_RESET_PROPS
static void run_resetprops(void) {
	int ret, retries = 0;
	bool data_mount_ready = false;
	// this part needs full permission, resetprop/setprop doesn't work with Kernel permissive for now
	set_selinux_enforcing(false,true); // full permissive!
	msleep(100);
	// set product name to avid HW TEE in safetynet check
	// chmod for resetprop
	ret = call_userspace(BIN_CHMOD,
			"755", BIN_RESETPROP, "chmod resetprop");
	if (!ret) {
		data_mount_ready = true;
	}

	// set product name to avid HW TEE in safetynet check
	retries = 0;
	if (data_mount_ready) {
	        do {
			ret = call_userspace(BIN_RESETPROP, "ro.boot.flash.locked", "1", "resetprop verifiedbootstate");
			if (ret) {
			    pr_info("%s can't set resetprop yet. sleep...\n",__func__);
			    msleep(DELAY);
			}
		} while (ret && retries++ < 10);

		if (!ret) {
			pr_info("Device props set succesfully!");
		} else {
			pr_err("Couldn't set device props! %d", ret);
		}
		ret = call_userspace(BIN_RESETPROP, "ro.boot.vbmeta.device_state", "locked", "resetprop verifiedbootstate");
		ret = call_userspace(BIN_RESETPROP, "ro.boot.verifiedbootstate", "green", "resetprop verifiedbootstate");
		ret = call_userspace(BIN_RESETPROP, "ro.boot.veritymode", "enforcing", "resetprop verifiedbootstate");
		ret = call_userspace(BIN_RESETPROP, "ro.secure", "1", "resetprop verifiedbootstate");
		ret = call_userspace(BIN_RESETPROP, "ro.boot.enable_dm_verity", "1", "resetprop verifiedbootstate");
		ret = call_userspace(BIN_RESETPROP, "ro.boot.secboot", "enabled", "resetprop verifiedbootstate");
#if 0
// you can't use scripts for resetprops, doesn't work at all...
		ret = call_userspace(BIN_SH,
			"-c", BIN_RESETPROPS_SH, "sh resetprops safetynet");
#endif

	} else pr_err("Skipping resetprops, fs not ready!\n");

#if 0
	// pixel4 stuff
	// allow soli any region
	ret = call_userspace(BIN_SETPROP,
		"pixel.oslo.allowed_override", "1", "setprop oslo");

	// allow multisim
	ret = call_userspace(BIN_SETPROP,
		"persist.vendor.radio.multisim_switch_support", "true", "setprop miltisim");
#endif

	msleep(1500);
	set_selinux_enforcing(true,true); // set enforcing
	set_selinux_enforcing(true,false); // set back kernel permissive
}
#endif

DEFINE_MUTEX(killprocess_mutex);

struct task_kill_info {
    struct task_struct *task;
    struct work_struct work;
};

static void proc_kill_task(struct work_struct *work)
{
    struct task_kill_info *kinfo = container_of(work, typeof(*kinfo), work);
    struct task_struct *task = kinfo->task;

    send_sig(SIGKILL, task, 0);
    put_task_struct(task);
    kfree(kinfo);
}

char buffer[256];
char * get_task_state(long state)
{
    switch (state) {
        case TASK_RUNNING:
            return "TASK_RUNNING";
        case TASK_INTERRUPTIBLE:
            return "TASK_INTERRUPTIBLE";
        case TASK_UNINTERRUPTIBLE:
            return "TASK_UNINTERRUPTIBLE";
        case __TASK_STOPPED:
            return "__TASK_STOPPED";
        case __TASK_TRACED:
            return "__TASK_TRACED";
        default:
        {
            sprintf(buffer, "Unknown Type:%ld\n", state);
            return buffer;
        }
    }
}

static int find_and_kill_task(char *filter)
{
    struct task_struct *task_list;
    struct task_struct *task_to_kill;
    unsigned int process_count = 0;
    int found_count = 0;

    pr_info("%s: In init\n", __func__);

    for_each_process(task_list) {
        //pr_info("Process: %s\t PID:[%d]\t (%s) - looking for: %s\n",
        //        task_list->comm, task_list->pid,
        //        get_task_state(task_list->state), filter);

	if (strlen(filter)>0 && strstr(filter,task_list->comm)) { // filter is longer, comm is substring possibly
		struct mm_struct *mm = NULL;
		char *pathname,*p;
		mm = task_list->mm;
		if (mm) {
		    down_read(&mm->mmap_sem);
		    if (mm->exe_file) {
	                    pathname = kmalloc(PATH_MAX, GFP_ATOMIC);
	                    if (pathname) {
		                p = d_path(&mm->exe_file->f_path, pathname, PATH_MAX);
				if (strlen(p)>0 && strstr(p,"/system/bin/app_process")) { // android executable only
		                    /*Now you have the path name of exe in p*/
				    found_count++;
				    if (found_count == 1) {
					task_to_kill = task_list;
				    }
				    pr_info("%s FOUND! Process: %s\t PID:[%d]\t State:%s Path: %s\n",__func__,
			                task_list->comm, task_list->pid,
			                get_task_state(task_list->state), p);
				}
	                    }
	            }
		    up_read(&mm->mmap_sem);
		}
	}
        process_count++;
    }
    //pr_info("Number of processes:%u\n", process_count);

    if (found_count==1) // only kill process if found exactly 1 of it. overlapping package names should find >1, block it
    {
	    struct task_kill_info *kinfo;

	    kinfo = kmalloc(sizeof(*kinfo), GFP_KERNEL);
	    if (kinfo) {
		pr_info("%s FOUND, trying to kill task.\n",__func__);
		get_task_struct(task_to_kill);
		kinfo->task = task_to_kill;
		INIT_WORK(&kinfo->work, proc_kill_task);
		schedule_work(&kinfo->work);
	    }
    }

    return 0;
}

static void killprocess_call(char *command) {
	if (mutex_trylock(&killprocess_mutex)) {
		pr_info("%s trying to kill process named: %s\n",__func__,command);
		find_and_kill_task(command);
		mutex_unlock(&killprocess_mutex);
	}
}

static void encrypted_work(void)
{
	int ret, retries = 0;
	bool data_mount_ready = false;

	// TEE part
        msleep(SHORT_DELAY * 2);
	// copy files from kernel arrays...
        do {
		ret = write_files();
		if (ret) {
		    pr_info("%s can't write resetprop yet. sleep...\n",__func__);
		    msleep(DELAY);
		}
	} while (ret && retries++ < 20);

#ifdef USE_PACKED_HOSTS
	// chmod for resetprop
	ret = call_userspace(BIN_CHMOD,
			"644", PATH_HOSTS_K_ZIP, "chmod hosts_k_zip");
	if (!ret) {
		data_mount_ready = true;
	}

// do this from overlay.sh instead, permission issue without SHELL user...
#ifndef USE_SCRIPTS
	// rm original hosts_k file to enable unzip to create new file (permission issue)
	ret = call_userspace("/system/bin/rm",
			"-f", PATH_HOSTS, "rm hosts");
	// unzip hosts_k file
	ret = call_userspace("/system/bin/unzip",
			PATH_HOSTS_K_ZIP, UNZIP_PATH, "unzip hosts");
	// ch context selinux for hosts file
	ret = call_userspace("/system/bin/chcon",
			"u:object_r:system_file:s0", PATH_HOSTS, "chcon u:object_r:system_file:s0 hosts");
	// chmod for hosts file
	ret = call_userspace("/system/bin/chmod",
			"644", PATH_HOSTS, "chmod /dev/__hosts_k");
#endif
#endif

	// chmod for overlay.sh
	ret = call_userspace(BIN_CHMOD,
			"755", BIN_OVERLAY_SH, "chmod overlay sh");

	// chmod for kernellog.sh
	ret = call_userspace(BIN_CHMOD,
			"755", BIN_KERNELLOG_SH, "chmod kernellog sh");

	// chmod for systools.sh
	ret = call_userspace(BIN_CHMOD,
			"755", BIN_RESETPROPS_SH, "chmod resetprops sh");

	// chmod for systools.sh
	ret = call_userspace(BIN_CHMOD,
			"755", BIN_SYSTOOLS_SH, "chmod systools sh");
	if (!ret) data_mount_ready = true;

#ifdef USE_RESET_PROPS
#ifdef RUN_RESET_PROPS_BEFORE_DECRYPT
	run_resetprops();
	set_selinux_enforcing(false,false); // set back kernel permissive
#endif
#endif

	if (data_mount_ready) {
#ifdef USE_SCRIPTS
		ret = overlay_system_etc();
#endif
		msleep(1000); // make sure unzip and all goes down in overlay sh, before enforcement is enforced again!
	}
}

#ifdef USE_DECRYPTED
static void decrypted_work(void)
{
	// no need for permissive yet...
	set_selinux_enforcing(true,false);
	if (!is_before_decryption) {
		pr_info("Waiting for key input for decrypt phase!");
		while (!is_before_decryption)
			msleep(1000);
	}
	sync_fs();
	pr_info("Fs key input for decrypt! Call encrypted_work now..");
	// set kernel permissive
	set_selinux_enforcing(false,false);
#ifdef USE_ENCRYPTED
#ifdef RUN_ENCRYPTED_LATE
	encrypted_work();
#endif
#endif
	// unset kernel permissive
	set_selinux_enforcing(true,false);

	if (!is_decrypted) {
		pr_info("Waiting for fs decryption!");
		while (!is_decrypted)
			msleep(1000);
		pr_info("fs decrypted!");
	}
	// Wait for RCU grace period to end for the files to sync
	sync_fs();
	msleep(100);

#ifdef USE_RESET_PROPS
#ifdef RUN_RESET_PROPS_AFTER_DECRYPT
	// run resetprops here, otherwise it's not set
	run_resetprops();
#endif
#endif

//	overlay_system_etc();
}
#endif

#ifndef USE_PACKED_HOSTS
static void setup_kadaway(bool on) {
	int ret;
	set_selinux_enforcing(false,false);
	if (!on) {
		ret = copy_files(SDCARD_HOSTS,PATH_HOSTS,MAX_COPY_SIZE,true);
#if 0
		ret = call_userspace("/system/bin/cp",
			"/dev/null", PATH_HOSTS, "devnull to hosts");
#endif
                if (!ret)
                        pr_info("%s userland: rm hosts file",__func__);
                else
                        pr_err("%s userland: COULDN'T rm hosts file",__func__);
		sync_fs();
		// chmod for hosts file
#if 0
		ret = call_userspace(BIN_CHMOD,
				"644", PATH_HOSTS, "chmod hosts");
#endif
	} else{
		ret = copy_files(SDCARD_HOSTS,PATH_HOSTS,MAX_COPY_SIZE,false);
#if 0
		ret = call_userspace("/system/bin/cp",
			SDCARD_HOSTS, PATH_HOSTS,"cp hosts");
#endif
                if (!ret)
                        pr_info("%s userland: cp hosts file",__func__);
                else
                        pr_err("%s userland: COULDN'T copy hosts file",__func__);
		sync_fs();
		// chmod for hosts file
#if 0
		ret = call_userspace(BIN_CHMOD,
				"644", PATH_HOSTS, "chmod hosts");
#endif
	}
	sync_fs();
	set_selinux_enforcing(true,false);
}
#endif

static bool use_kill_app = false;
static bool kadaway = true;

static void uci_user_listener(void) {
	bool new_kadaway = !!uci_get_user_property_int_mm("kadaway", kadaway, 0, 1);
	if (new_kadaway!=kadaway) {
		pr_info("%s kadaway %u\n",__func__,new_kadaway);
		kadaway = new_kadaway;
#ifndef USE_PACKED_HOSTS
		setup_kadaway(kadaway);
#endif
	}
	use_kill_app = uci_get_user_property_int_mm("sweep2sleep_kill_app_mode",0,0,3)>0;
}

static bool kernellog = false;
static bool wifi = false;
static char* fg_process0 = NULL;
static char* fg_process1 = NULL;
static void uci_sys_listener(void) {
	bool new_kernellog = !!uci_get_sys_property_int_mm("kernel_log", kernellog, 0, 1);
	bool new_wifi = !!uci_get_sys_property_int_mm("wifi_connected", wifi, 0, 1);

	if (use_kill_app) {
	const char* new_fg_process0 = uci_get_sys_property_str("fg_process0","");
	const char* new_fg_process1 = uci_get_sys_property_str("fg_process1","");
	bool first_run = false;
	if (!fg_process0) {
		fg_process0 = kmalloc(255 * sizeof(char*), GFP_KERNEL);
		fg_process1 = kmalloc(255 * sizeof(char*), GFP_KERNEL);
		first_run = true;
	}
	if ( (first_run && new_fg_process0 && new_fg_process1) || (new_fg_process0 && strcmp(fg_process0,new_fg_process0)!=0) ) {
		pr_info("%s fg process kill change %s %s \n",__func__,new_fg_process0, new_fg_process1);
		strcpy(fg_process0,new_fg_process0);
		strcpy(fg_process1,new_fg_process1);
		if (strstr( fg_process0, "launcher") || strstr( fg_process0, "cleanslate.csservice") || strstr( fg_process0, "#####")) {
			pr_info("%s not killing launcher process!\n",__func__);
		} else {
			set_selinux_enforcing(false,false);
			sync_fs();
			killprocess_call(fg_process0);
			sync_fs();
			set_selinux_enforcing(true,false);
		}
	}
	}

	if (new_kernellog!=kernellog) {
		if (new_kernellog) {
			schedule_work(&kernellog_call_work);
		}
		kernellog = new_kernellog;
	}

	if (new_wifi!=wifi) {
		if (new_wifi) {
#ifdef USE_SYSTOOLS_CALL
			set_selinux_enforcing(false,false); // needs full permissive for dumpsys
			sync_fs();
			systools_call("wifi");
			sync_fs();
			set_selinux_enforcing(true,false);
#endif
		}
		wifi = new_wifi;
	}

}


static void userland_worker(struct work_struct *work)
{
	pr_info("%s worker...\n",__func__);
	while (extern_state==NULL) { // wait out first write to selinux / fs
		msleep(10);
	}
	pr_info("%s worker extern_state inited...\n",__func__);

#ifdef USE_ENCRYPTED
#ifndef RUN_ENCRYPTED_LATE
	// set permissive while setting up properties and stuff..
	set_selinux_enforcing(false,false);
	encrypted_work();
	set_selinux_enforcing(true,false);
#endif
#endif

#ifdef USE_DECRYPTED
	decrypted_work();
	// revert back to enforcing/switch off kernel permissive
	set_selinux_enforcing(true,false);
#endif
	uci_add_user_listener(uci_user_listener);
	uci_add_sys_listener(uci_sys_listener);
	userland_init_done = true;
}

static int __init userland_worker_entry(void)
{
	INIT_DELAYED_WORK(&userland_work, userland_worker);
	pr_info("%s boot init\n",__func__);
	queue_delayed_work(system_power_efficient_wq,
			&userland_work, DELAY);


	return 0;
}

module_init(userland_worker_entry);
