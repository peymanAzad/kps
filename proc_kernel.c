#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#define BUFFER_SIZE 128
#define PROC_NAME "hello"

static pid_t target_pid = 0;
static DEFINE_MUTEX(lock);
ssize_t pread(struct file *file, char __user *usr_buf, size_t count,
              loff_t *pos);
ssize_t pwrite(struct file *, const char *, size_t, loff_t *);

static struct proc_ops ops = {
    .proc_read = pread,
    .proc_write = pwrite,
};

int proc_init(void) {
    proc_create(PROC_NAME, 0666, NULL, &ops);

    return 0;
}

void proc_exit(void) { remove_proc_entry(PROC_NAME, NULL); }

ssize_t pread(struct file *file, char __user *usr_buf, size_t count,
              loff_t *pos) {
    mutex_lock(&lock);
    pid_t pid = target_pid;
    mutex_unlock(&lock);

    char buffer[BUFFER_SIZE];
    struct task_struct *task;
    rcu_read_lock();
    if (pid == 0)
        task = current;
    else
        task = pid_task(find_vpid(pid), PIDTYPE_PID);

    if (task == NULL) {
        rcu_read_unlock();
        return -ESRCH;
    }

    char task_name[TASK_COMM_LEN];
    get_task_comm(task_name, task);
    char task_stat_char = task_state_to_char(task);
    rcu_read_unlock();
    int rv = sprintf(buffer, "pid: %d, name: %s, state: %c\n", task->pid,
                     task_name, task_stat_char);

    return simple_read_from_buffer(usr_buf, count, pos, buffer, rv);

    return rv;
}

ssize_t pwrite(struct file *file, const char __user *usr_buf, size_t count,
               loff_t *pos) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    // don't read more than our buffer can hold
    size_t to_copy = min(count, (size_t)BUFFER_SIZE - 1);

    if (copy_from_user(buffer, usr_buf, to_copy))
        return -EFAULT; // copy_from_user returns 0 on success, nonzero on fail

    // strip trailing newline that echo adds
    buffer[strcspn(buffer, "\n")] = '\0';

    long pid;
    if (kstrtol(buffer, 10, &pid) < 0)
        return -EINVAL;

    printk(KERN_INFO "inspector: received PID %ld\n", pid);
    mutex_lock(&lock);
    target_pid = (pid_t)pid;
    mutex_unlock(&lock);

    return count;
}

module_init(proc_init);
module_exit(proc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("hello proc");
MODULE_AUTHOR("P");
