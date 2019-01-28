//
//  stack_logging.c
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/19.
//

#include <stdio.h>

static void
create_and_insert_lite_zone_while_locked()//create log zone
{
	malloc_zone_t *zone0 = malloc_zones[0];
	
	malloc_zone_t *stack_logging_lite_zone = create_stack_logging_lite_zone(0, zone0, malloc_debug_flags);
	malloc_zone_register_while_locked(stack_logging_lite_zone);
	malloc_set_zone_name(stack_logging_lite_zone, MALLOC_STOCK_LOGGING_LITE_ZONE_NAME);
	lite_zone = stack_logging_lite_zone;
}


boolean_t
turn_on_stack_logging(stack_logging_mode_type mode)
{
	boolean_t ret = false;
	
	MALLOC_LOCK();
	
	if (!stack_logging_enable_logging) {
		if (__uniquing_table_memory_was_deleted()) {
			// It would great to be able re-enable even if the uniquing table has been deleted
			// <rdar://problem/25014005> malloc stack logging should be able to recreate the uniquing table if needed
		} else {
			switch (mode) {
				case stack_logging_mode_all:
					__prepare_to_log_stacks(false);
					malloc_logger = __disk_stack_logging_log_stack;
					__syscall_logger = __disk_stack_logging_log_stack;
					stack_logging_mode = mode;
					stack_logging_enable_logging = 1;
					ret = true;
					
					malloc_printf("recording malloc and VM allocation stacks to disk using standard recorder\n");
					break;
					
				case stack_logging_mode_malloc:
					__prepare_to_log_stacks(false);
					malloc_logger = __disk_stack_logging_log_stack;
					stack_logging_mode = mode;
					stack_logging_enable_logging = 1;
					ret = true;
					
					malloc_printf("recording malloc (but not VM allocation) stacks to disk using standard recorder\n");
					break;
					
				case stack_logging_mode_vm:
					__prepare_to_log_stacks(false);
					__syscall_logger = __disk_stack_logging_log_stack;
					stack_logging_mode = mode;
					stack_logging_enable_logging = 1;
					ret = true;
					
					malloc_printf("recording VM allocation (but not malloc) stacks to disk using standard recorder\n");
					break;
					
				case stack_logging_mode_lite:
					if (!has_default_zone0()) {
						malloc_printf("zone[0] is not the normal default zone so can't turn on lite mode.\n", mode);
						ret = false;
					} else {
						malloc_printf("recording malloc (and VM allocation) stacks using lite mode\n");
						
						if (lite_zone) {
							enable_stack_logging_lite();
						} else {
							if (__prepare_to_log_stacks(true)) {
								__syscall_logger = __disk_stack_logging_log_stack;
								stack_logging_mode = stack_logging_mode_lite;
								stack_logging_enable_logging = 1;
								__prepare_to_log_stacks_stage2();
								create_and_insert_lite_zone_while_locked();
								enable_stack_logging_lite();
							}
						}
						ret = true;
					}
					break;
					
				default:
					malloc_printf("invalid mode %d passed to turn_on_stack_logging\n", mode);
					break;
			}
		}
	} else {
		malloc_printf("malloc stack logging already enabled.\n");
	}
	
	MALLOC_UNLOCK();
	
	return ret;
}

void
turn_off_stack_logging()
{
	MALLOC_LOCK();
	
	if (stack_logging_enable_logging) {
		switch (stack_logging_mode) {
			case stack_logging_mode_all:
				malloc_logger = NULL;
				__syscall_logger = NULL;
				stack_logging_enable_logging = 0;
				malloc_printf("turning off recording malloc and VM allocation stacks to disk using standard recorder\n");
				break;
				
			case stack_logging_mode_malloc:
				malloc_logger = NULL;
				stack_logging_enable_logging = 0;
				malloc_printf("turnning off recording malloc (but not VM allocation) stacks to disk using standard recorder\n");
				break;
				
			case stack_logging_mode_vm:
				__syscall_logger = NULL;
				stack_logging_enable_logging = 0;
				malloc_printf("turning off recording VM allocation (but not malloc) stacks to disk using standard recorder\n");
				break;
				
			case stack_logging_mode_lite:
				malloc_printf("turning off recording malloc (but not VM allocation) stacks using lite mode\n");
				
				disable_stack_logging_lite();
				stack_logging_enable_logging = 0;
				break;
				
			default:
				malloc_printf("invalid stack_logging_mode %d in turn_off_stack_logging\n", stack_logging_mode);
				break;
		}
	} else {
		malloc_printf("malloc stack logging not enabled.\n");
	}
	
	MALLOC_UNLOCK();
}

