#include "bsp_sys.h"

//////////////////////////////////////////////////////////////////////////////////	 
//鏈▼搴忓彧渚涘涔犱娇鐢紝鏈粡浣滆€呰鍙紝涓嶅緱鐢ㄤ簬鍏跺畠浠讳綍鐢ㄩ€?
//ALIENTEK Mini STM32寮€鍙戞澘
//绯荤粺涓柇鍒嗙粍璁剧疆鍖?	   
//姝ｇ偣鍘熷瓙@ALIENTEK
//鎶€鏈鍧?www.openedv.com
//淇敼鏃ユ湡:2012/9/10
//鐗堟湰锛歏1.4
//鐗堟潈鎵€鏈夛紝鐩楃増蹇呯┒銆?
//Copyright(C) 姝ｇ偣鍘熷瓙 2009-2019
//All rights reserved
//********************************************************************************  
//THUMB鎸囦护涓嶆敮鎸佹眹缂栧唴鑱?
//閲囩敤濡備笅鏂规硶瀹炵幇鎵ц姹囩紪鎸囦护WFI  
void WFI_SET(void)
{
	__ASM volatile("wfi");		  
}
//鍏抽棴鎵€鏈変腑鏂?
void INTX_DISABLE(void)
{		  
	__ASM volatile("cpsid i");
}
//寮€鍚墍鏈変腑鏂?
void INTX_ENABLE(void)
{
	__ASM volatile("cpsie i");		  
}
//璁剧疆鏍堥《鍦板潃
//addr:鏍堥《鍦板潃
__asm void MSR_MSP(u32 addr) 
{
    MSR MSP, r0 			//set Main Stack value
    BX r14
}
