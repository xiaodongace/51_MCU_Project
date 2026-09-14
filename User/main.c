#include "Config.h"

void sys_init(void) {
    
}

// 这里函数名可随意, 建议不要使用start, 会和I2C.h里的Start冲突
void main_start() _task_ 0 {
	sys_init();
	// 创建任务 1
	os_create_task(1);
	// 结束任务 0
    os_create_task(2);
	os_delete_task(0);
}