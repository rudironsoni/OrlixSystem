// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <asm/ptrace.h>
#include <asm/processor.h>

#define ORLIX_MAX_STACK_FRAMES 64

struct orlix_stack_frame {
	unsigned long fp;
	unsigned long lr;
};

static unsigned long orlix_current_frame_pointer(void)
{
	unsigned long fp;

	asm volatile("mov %0, x29" : "=r" (fp));
	return fp;
}

static int orlix_stack_frame_valid(const struct task_struct *task,
				   const struct orlix_stack_frame *frame)
{
	unsigned long stack_low;
	unsigned long stack_high;
	unsigned long frame_addr = (unsigned long)frame;

	if (!task || !frame)
		return 0;

	stack_low = (unsigned long)task_stack_page(task);
	stack_high = stack_low + THREAD_SIZE;

	return !(frame_addr & 0xf) &&
	       frame_addr >= stack_low &&
	       frame_addr + sizeof(*frame) <= stack_high;
}

void show_regs(struct pt_regs *regs)
{
	int i;

	if (!regs) {
		pr_info("Orlix pt_regs unavailable\n");
		return;
	}

	pr_info("pc: %016llx sp: %016llx pstate: %016llx\n",
		(unsigned long long)regs->pc,
		(unsigned long long)regs->sp,
		(unsigned long long)regs->pstate);
	for (i = 0; i < 31; i += 2) {
		if (i == 30) {
			pr_info("x%-2d: %016llx\n", i,
				(unsigned long long)regs->regs[i]);
			break;
		}
		pr_info("x%-2d: %016llx x%-2d: %016llx\n",
			i, (unsigned long long)regs->regs[i],
			i + 1, (unsigned long long)regs->regs[i + 1]);
	}
}

void show_stack(struct task_struct *task, unsigned long *sp, const char *loglvl)
{
	const char *level = loglvl ? loglvl : KERN_DEFAULT;
	struct orlix_stack_frame *frame;
	unsigned int depth;
	unsigned int printed = 0;

	(void)sp;

	if (!task)
		task = current;

	if (task == current)
		frame = (struct orlix_stack_frame *)orlix_current_frame_pointer();
	else
		frame = (struct orlix_stack_frame *)task->thread.cpu_context.fp;

	printk("%sCall Trace:\n", level);
	for (depth = 0; depth < ORLIX_MAX_STACK_FRAMES; depth++) {
		struct orlix_stack_frame *next;
		unsigned long lr;

		if (!orlix_stack_frame_valid(task, frame))
			break;

		lr = frame->lr;
		if (__kernel_text_address(lr)) {
			printk("%s[<%016lx>]\n", level, lr);
			printed++;
		}

		next = (struct orlix_stack_frame *)frame->fp;
		if (next <= frame)
			break;
		frame = next;
	}

	if (!printed)
		printk("%s  <no reliable frame records>\n", level);
}
