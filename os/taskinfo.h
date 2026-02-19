#ifndef TASKINFO_H
#define TASKINFO_H

#define MAX_SYSCALL_NUM 500

typedef enum {
    UnInit,
    Ready,
    Running,
    Exited,
} TaskStatus;

typedef struct {
	TaskStatus status;
	unsigned int syscall_times[MAX_SYSCALL_NUM];
	int time;
} TaskInfo;

#endif // TASKINFO_H