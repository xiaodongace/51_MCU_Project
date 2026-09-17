#include "NIXIE.h"

void text_task() _task_ App_Nixie_Task_Id {
		
		while(1){
				Nixie_task();
		}
}