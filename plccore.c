#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include "plccore.h"
#include "plc_sym.h"
//#include <linux/filter.h>
#include <linux/set_memory.h>
//#include <asm-generic/set_memory.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/moduleloader.h>
//#include <asm/cacheflush.h>
#define VERSION_REG             47630
/* Used to store the loaded plc codes' oneshotbit area. */
static unsigned char   devotedOneShotBitsArea[MLC_MAX_NUM][MAX_IOCSA_NO];
/* PLC Macro data */
char *gRingBuf;
double *gNumIndexedGlobalVarBuf=NULL;
double *gNumIndexedLocalVarBuf=NULL;
long *gNumIndexedGlobalVarIntBuf=NULL;
long *gNumIndexedLocalVarIntBuf=NULL;
/* Sys user PLC Macro data. */
double *gSysNumIndexedGlobalVarBuf=NULL;
double *gSysNumIndexedLocalVarBuf=NULL;
long *gSysNumIndexedGlobalVarIntBuf=NULL;
long *gSysNumIndexedLocalVarIntBuf=NULL;
/* Maker user PLC Macro data. */
double *gMakerNumIndexedGlobalVarBuf=NULL;
double *gMakerNumIndexedLocalVarBuf=NULL;
long *gMakerNumIndexedGlobalVarIntBuf=NULL;
long *gMakerNumIndexedLocalVarIntBuf=NULL;
extern void *__kernel_plc_mem;

/* Used to let the int-oriented compiled code know when to initialize globalVar/localVar to 0. */
unsigned char gIntOrientFirstRun;

PLC_CORE *Rbase_getPLCbase(void);

PLC_CORE *tPlc = NULL;

/* The interface function for plcmacro generated bin to use. Fix the calling convention to fastcall. */
//!@brief  �?????D/I�????????表�????????
//!@param  No  �??�?????D/I�?????編�??
//!@return ??��?????D/I�????????,�???????��??0=Off???\n
//!        �???????��??�?????0??????,表示??��????�On
static char PLCMACROIF_ReadI(long No)
{
    return plccore_ReadI(tPlc,No);
}

//! @brief  �?????D/O�????????表�????????
//! @param   No  �??�?????D/O�?????編�??
//! @return  ??��?????D/I�????????,�???????��??0=Off???\n
//!            �???????��??�?????0??????,表示??��????�On
static char PLCMACROIF_ReadO(long No)
{
    return plccore_ReadO(tPlc,No);
}


//! @brief  �?????C�????????表�????????
//! @param   No  �??�?????C�?????編�??
//! @return  ??��?????D/I�????????,�???????��??0=Off???\n
//!            �???????��??�?????0??????,表示??��????�On
static char PLCMACROIF_ReadC(long No)
{
    return plccore_ReadC(tPlc,No);
}


//! @brief  �?????S�????????表�????????
//! @param   No  �??�?????S�?????編�??
//! @return  ??��?????D/I�????????,�???????��??0=Off???\n
//!            �???????��??�?????0??????,表示??��????�On
static char PLCMACROIF_ReadS(long No)
{
    return plccore_ReadS(tPlc,No);
}


//! @brief  �?????A�????????表�????????
//! @param   No  �??�?????A�?????編�??
//! @return  ??��?????D/I�????????,�???????��??0=Off???\n
//!            �???????��??�?????0??????,表示??��????�On
static char PLCMACROIF_ReadA(long No)
{
    return plccore_ReadA(tPlc,No);
}


//! @brief  �?????R Register??????
//! @param   No  �??�?????R???編�??
//! @return  ??��??R Register??????
long PLCMACROIF_ReadR(long No)
{
    //printk("%s %d\n",__func__,No);
    if (No < MAX_REGISTER_NO &&  No >= 0)
    {
        return plccore_ReadR(tPlc,No);
    }
    else
    {
        No-=USR_REG_ADDR_START;
        return plccore_ReadUR(tPlc,No);

    }
}


//! @brief  �?????R Register???bit???
//! @param   No  �??�?????R???編�??
//!            bit �?????
//! @return  ??��??R Register??????
static char PLCMACROIF_ReadR_Bit(long No,char bit)
{
   if (No < MAX_REGISTER_NO &&  No >= 0)
    {
        return plccore_ReadR_Bit(tPlc,No,bit);
    }
    else
    {
        No-=USR_REG_ADDR_START;
        return plccore_ReadUR_Bit(tPlc,No,bit);

    }
}

static void PLCMACROIF_WriteI(long No,char State)
{
    plccore_WriteI(tPlc,No,State);
}

//! @brief  �??�??輸�?��??D/O�??寫�?�D/O??????表裡
//! @param   No      �??寫�?��??D/O�??編�??
//! @param   State   �??寫�?��????????,�?????�??設�?�Off???,寫�??0,??��??寫�?��??0???
static void PLCMACROIF_WriteO(long No,char State)
{
    plccore_WriteO(tPlc, No,State);
}


//! @brief  寫�?��?�C bit
//! @param   No      �??寫�?��??c�??編�??
//! @param   State   �??寫�?��????????,�?????�??設�?�Off???,寫�??0,??��??寫�?��??0???
static void PLCMACROIF_WriteC(long No,char State)
{
    plccore_WriteC(tPlc, No,State);
}


//! @brief  寫�?��?�s bit
//! @param   No      �??寫�?��??S�??編�??
//! @param   State   �??寫�?��????????,�?????�??設�?�Off???,寫�??0,??��??寫�?��??0???
static void PLCMACROIF_WriteS(long No,char State)
{
    plccore_WriteS(tPlc, No,State);
}


//! @brief  寫�?��?�A bit
//! @param   No      �??寫�?��??A�??編�??
//! @param   State   �??寫�?��????????,�?????�??設�?�Off???,寫�??0,??��??寫�?��??0???
static void PLCMACROIF_WriteA(long No,char State)
{
    plccore_WriteA(tPlc, No,State);
}



//! @brief  寫�?��?�R Register
//! @param   No      �??寫�?��??R編�??
//! @param   State   �??寫�?��????��??
static void PLCMACROIF_WriteR(long No,long State)
{
   if (No < MAX_REGISTER_NO &&  No >= 0)
    {
        plccore_WriteR(tPlc,No,State);
    }
    else
    {
        No-=USR_REG_ADDR_START;
        plccore_WriteUR(tPlc,No,State);

    }
}


//! @brief  寫bit??��?�R Register
//! @param   No      �??寫�?��??R編�??
//! @param   State   �??寫�?��????��??
static void PLCMACROIF_WriteR_Bit(long No,char bit,char State)
{
   if (No < MAX_REGISTER_NO &&  No >= 0)
    {
        plccore_WriteR_Bit(tPlc,No,bit,State);
    }
    else
    {
        No-=USR_REG_ADDR_START;
        plccore_WriteUR_Bit(tPlc,No,bit,State);
    }
}


static int PLCMACROIF_snprintf(char *buf, unsigned int size, const char *fmt, ...)
{
    int copyNum=0;
    va_list ap;
    va_start(ap, fmt);
    copyNum=vsnprintf(buf,size,fmt,ap);
    va_end(ap);
    if(0 < copyNum)
    {
        return(copyNum);
    }
    else
    {
        return(0);
    }
}

/*!
    \brief Enable IEC execution to access printk.
*/
static int PLCMACROIF_printk(const char *fmt, ...)
{
    int rtn=0;
    va_list ap;
    va_start(ap,fmt);
    rtn=vprintk(fmt,ap);
    va_end(ap);
    return(rtn);
}

/*!
    \brief Enable IEC execution to access strlen.
*/
static int PLCMACROIF_strlen(char *s)
{
    return(strlen(s));
}

/*!
    \brief Enable IEC execution to access strcat.
*/

static char * PLCMACROIF_strncat(char *to,char *from,int size)
{
    return(strncat(to,from,size));
}

/*!
    \brief Enable IEC execution to access strncmp.
*/
static int PLCMACROIF_strncmp(char *s1,char *s2,char len)
{
    return(strncmp(s1,s2,len));
}

static void *PLCMACROIF_memcpy(void * destination, const void * source, unsigned int num)
{
    return(memcpy(destination,source,num));
}

static void *PLCMACROIF_memmove(void * destination, const void * source, unsigned int num)
{
    return(memmove(destination,source,num));
}

static void *PLCMACROIF_memset(void * destination, int ch, unsigned int num)
{
    return(memset(destination,ch,num));
}

static int PLCMACROIF_memcmp(const void * buf1, const void *buf2, unsigned int num)
{
    return(memcmp(buf1,buf2,num));
}


//! @brief ???�??IEC generated code�??????????��????��?��?��??�??�??�????��????��????��????��????��?��?��?????�????��??IEC compiler??��?��??code??��?��?????plc module??????crash(??��?????�????�NULL)???�?????plcmacro??�以??��?�plcmdata.h???主�????��????????此�?�護???
//! ?????��?��?��?��????????�??inputPtr/outputPtr空�??�????��?��?��??被�?��?��?��??該�??輸�?��????��??輸�?��????��????????�?????�??�??�????��?��?��??�??inputPtr/outputPtrSize�??檢�?��?��??
//! @param cmd: ???�????��??�??�????�令
//! @param inputPtrSize: ??��?��?��??�?????inputPtr空�??大�??�??�?????被�?��?��?��?????�?????大�??大�??輸�?��????��????�誤???\n
//! @param inputPtr:??��?��?��??�?????input?????�置空�?????\n
//! @param outputPtrSize: ??��?��?��??�?????outputPtr空�??大�??�??�?????被�?��?��?��?��??�?????大�??大�??輸�?��????��????�誤???\n
//! @param inputPtr:??��?��?��??�?????output?????�置空�?????\n
//! @rtn 0: normal exit <0 ??��?�誤�?????
//!
static int PLCMACROIF_Generic_KFunc_Dispatcher(char cmd,long inputPtrSize,char *inputPtr,long outputPtrSize,char *outputPtr)
{
    switch(cmd)
    {
        case GENERIC_KFUNC_CMD_SYS_DT: //Get system date and time and return in IEC DT format
        {
            //struct tm dt;
            struct rtc_time dt;
            struct timeval time;
            unsigned long local_time;
            do_gettimeofday(&time);
            local_time = (u32)(time.tv_sec - (sys_tz.tz_minuteswest * 60));
            rtc_time_to_tm(local_time, &dt);
            if(sizeof(long)*6 > outputPtrSize)
            {
                return(RUNTIME_ERR_INCORRECT_GENERIC_KFUNC_OUTPUT);
            }
            long *longPtr=(long *)outputPtr;
            *(longPtr)=dt.tm_year+1900;
            *(longPtr+1)=dt.tm_mon+1;
            *(longPtr+2)=dt.tm_mday;
            *(longPtr+3)=dt.tm_hour;
            *(longPtr+4)=dt.tm_min;
            *(longPtr+5)=dt.tm_sec;
            //printk("[PLC]sys dt:year=%ld mon=%ld day=%ld hour=%ld min=%ld sec=%ld\n",dt.tm_year+1900,dt.tm_mon+1,dt.tm_mday,dt.tm_hour,dt.tm_min,dt.tm_sec);
        }
        break;
        default:
            return(RUNTIME_ERR_UNKNOWN_GENERIC_KFUNC_CMD);
        break;
    }
    return(0);
}

/* For int-oriented compiling. */

//! @brief �????��?��???????��?��????��??�??確�????��????��?��??�??�??global/local var???�?????0
static unsigned char PLCMACROIF_IntOrientGetFirstRun(void)
{
    return gIntOrientFirstRun;
}

//! @brief �????��?��???????��?��????��??�??�??global/local var�?????0�???????��??次�?��??�????��????��??�?????
static void PLCMACROIF_IntOrientSetFirstRun(void)
{
    gIntOrientFirstRun=1;
}

//! @brief �????��?��???????��?��????��??�????��?��??global/local var�?????0(?????�default??��????�DBL_MAX)
//! @date 2016/09/09 �??int-oriented compiling???VACANT??��?�INT_MAX???
static void PLCMACROIF_IntOrientClearVarsToVacant(void)
{
    if(NULL != gNumIndexedGlobalVarIntBuf)
    {
        long i=0;
        for(i=0;i<GLOBAL_VAR_SIZE;i++)
        {
            gNumIndexedGlobalVarIntBuf[i]=INT_MAX;
            gSysNumIndexedGlobalVarIntBuf[i]=INT_MAX;
            gMakerNumIndexedGlobalVarIntBuf[i]=INT_MAX;
        }
    }
    if(NULL != gNumIndexedLocalVarIntBuf)
    {
        long i=0;
        for(i=0;i<LOCAL_VAR_SIZE;i++)
        {
            gNumIndexedLocalVarIntBuf[i]=INT_MAX;
            gSysNumIndexedLocalVarIntBuf[i]=INT_MAX;
            gMakerNumIndexedLocalVarIntBuf[i]=INT_MAX;
        }
    }
}

//static char      FPLCCode[MLC_MAX_NUM][PLC_CODE_SIZE];

// -----------------------------------------------------------------------
//  ?????�說???:??��?????�????????段�??�??�??�??
//  ???    ???:Code=�??�??段�??�?????(�????????int)
//           Data=�?????段�??�??�??(�????????int)
//  ??? ??? ???:
// -----------------------------------------------------------------------
//__attribute__((optimize("O0")))  //以�????��?????�?????佳�??//
//??��?��??code�?????使�?�__cdecl calling convention�??caller �??責�?�callee??��??�??�??�????�stack??��???????��??
//2016/05/31 �????????影�??eax??��????��????��??int-oriented編譯???空�??�??(??��??RET)?????��?��??
static void _RunCodeC( long CodePtr,long DataPtr,short iNo)
{
    printk("This is a PLC test 372 20241129\n");
#if 0
    #ifdef ATT_VER
	printk("This is a PLC test 20241129\n");
	/*
    __asm__  __volatile__ (
        //"pusha\n\t"

        //"xorl  %%esi,%%esi\n\t"
        //"movl  %0,%%edx\n\t"
        //"movl  %1,%%ebx\n\t"
        //"movl  %%edx,589820(%%ebx)\n\t"     //(0x90000-4) �????��??�??�?????R??��?????
        "push  %1\n\t"
        "call  %0\n\t"
        "addl $4,%%esp\n\t"

        //"popa  \n\t"
        :
        :"g" (CodePtr), "g" (DataPtr)
        :"eax"
    );
	*/
    #endif
#endif
}
//20241229
//int i_20241229=0;
int i_20241229=0;
static void _RunCode( long CodePtr,long DataPtr,short iNo){

//printk("20241229 _RunCode 451 start\n");
void *pc_value;
unsigned long unDataPtr = DataPtr;
//void* __kernel_plc_mem_code_start = (void*)((char*)__kernel_plc_mem + 0x0);
 printk("20241229 _RunCode 412 Start1 __kernel_plc_mem address= 0x%lx iNo=%d \n", __kernel_plc_mem,iNo);
//printk("20241229 _RunCode 413 Start1 __kernel_plc_mem_code_start= 0x%lx\n", __kernel_plc_mem_code_start);
//uintptr_t data_value_2 = 0x12345678;

   __asm__ volatile (
    "mov %0, pc"  // �? PC ?????��?��?��?? pc_value
    : "=r" (pc_value)  // 输出：�??结�?��?��?��?? pc_value
    :
    : "memory"
);
printk("20241229 _RunCode 468 pc=0x%lx\n",pc_value);
asm volatile(
        "stmdb sp!, {r4-r11}\n"   // �? R4-R11 ?��??��????? (保�??)
        :
        :
        : "memory"
    );
 printk("20241229 _RunCode 417 Start1 Executing machine code at address 0x%lx\n", __kernel_plc_mem);

 __asm__ volatile (
			"mov r6, %[data_value]       \n"  // �? data_value ?��???存�?? r6 �?存器
        //"mov r9, %[data_value_2]    \n"  // �? data_value_2 ?��???存�?? r9 �?存器
        "adr lr, 1f\n"         // �? label 1 ????��?????�载??? LR
        "bx %[func1]\n"              // 跳转??�目???�????
        "1:\n"                 // label 1，�?��?��?��?��?��??�????
        : 
        : [func1] "r" (__kernel_plc_mem),  // ?��標函?��?��???
          [data_value] "r" (unDataPtr)  // ??��?? data_value
          //[data_value_2] "r" (data_value_2)  // ??��?��?�移?��???
        : "r6", "r7", "r8", "r9"  // 說�?�使?���??��些暫存器
        
    );
printk("20241229 _RunCode 417 Start2 Executing machine code at address 0x%lx\n", __kernel_plc_mem);
    
    asm volatile(
        "ldmia sp!, {r4-r11}\n"   // 從�????��???�� R4-R11 (??�復)
        :
        :
        : "memory"
    );
    
    
		printk("20241229 _TestRunMem Run End!!\n");
//	}
}

static void _RunCode1( long CodePtr,long DataPtr,short iNo)
{
//20241229 test1
 void *code_ptr;
    unsigned long code_size = 1024; // ???設�??械碼大�?��?? 1024 位�??�?

    // 使用 vmalloc ?????��?��?��??
    code_ptr = vmalloc(code_size);
    if (!code_ptr) {
        printk("Failed to allocate memory\n");
        return -ENOMEM;
    }
// 檢查??�擬位�???��?��??��??
    if (!virt_addr_valid(code_ptr)) {
        printk("kmalloc Invalid virtual address =0x%lx\n",code_ptr);
        dump_stack();
        vfree(code_ptr);
        return -EFAULT;
    }

//20241229
	unsigned long unCodePtr=CodePtr;
	unsigned long unDataPtr=DataPtr;
	void (*func)(void);
	func = (void (*)(void))unCodePtr;
	printk("20241229 _RunCode 404 unCodePtr=0x%lx\n unDataPtr=0x%lx\n",unCodePtr,unDataPtr);
  __asm__ volatile (
        "ADD R0, %0, #0x10000\n"    // �? unDataPtr ??�U?? 0x10000，放??? R0
        "MOV R1, #0x33\n"           // �? 0x78 ?��??? R1
        "STRB R1, [R0]\n"           // �? R1 ?????? (0x78) 寫�?? R0 ?????��??記�?��??

        "MOV R1, #0x55\n"           // �? 0x56 ?��??? R1
        "STRB R1, [R0, #1]\n"       // �? R1 ?????? (0x56) 寫�?? R0+1 ?????��??記�?��??

        "MOV R1, #0x11\n"           // �? 0x34 ?��??? R1
        "STRB R1, [R0, #2]\n"       // �? R1 ?????? (0x34) 寫�?? R0+2 ?????��??記�?��??

        "MOV R1, #0x22\n"           // �? 0x12 ?��??? R1
        "STRB R1, [R0, #3]\n"       // �? R1 ?????? (0x12) 寫�?? R0+3 ?????��??記�?��??
        :
        : "r" (unDataPtr)           // 輸�?��?��?�數：unDataPtr 對�?��?? %0
        : "r0", "r1"                // ??�知編譯?��使用�? R0 ??? R1
    );
/*	unsigned char *ptr = (unsigned char *)unDataPtr; // �? DataPtr 轉�?��?��??�?
ptr+=0x10000;
    *ptr = 0x78; 
*(ptr+1) = 0x56; 
*(ptr+2) = 0x34; 
*(ptr+3) = 0x12; 
*(ptr+4) = 33; 
*(ptr+5) = 0x00; 
*(ptr+6) = 0x00; 
*(ptr+7) = 0x00; */
 	//unsigned char machine_code[] = "\xE3\xA0\x00\x01\xE1\x2F\xFF\x1E"; // mov r0, #1; bx lr
	
    //unsigned long size = PAGE_SIZE;  // ?????��?��?�大�?
    //void *code_mem = vmalloc(size);
    

    // �?算�??�?設置?��?���?屬�?��??????��
    //unsigned long size_in_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    // 設置??��?��????�可?���?
    //unsigned long aligned_addr = (unsigned long)code_mem & PAGE_MASK;
    //set_memory_x(aligned_addr, size_in_pages);
		//memcpy(code_mem, machine_code, sizeof(machine_code));

    // ?��?��?????�執行�??械碼
    //void (*func)(void) = (void (*)(void))code_mem;
printk("20241229 _RunCode 425 before func\n");
/*
asm volatile (
        "mov r6, %0\n"               // r6 = DataPtr
        "mov r8, #0x10000\n"         // r8 = 0x10000 (??�移???)

        "mov r7, #0x12\n"            // r7 = 0x12
        "strb r7, [r6, r8]\n"        // *(DataPtr + R8) = 0x12

        "add r8, r8, #1\n"           // r8 = r8 + 1
        "mov r7, #0x34\n"            // r7 = 0x34
        "strb r7, [r6, r8]\n"        // *(DataPtr + R8) = 0x34

        "add r8, r8, #1\n"           // r8 = r8 + 1
        "mov r7, #0x56\n"            // r7 = 0x56
        "strb r7, [r6, r8]\n"        // *(DataPtr + R8) = 0x56

        "add r8, r8, #1\n"           // r8 = r8 + 1
        "mov r7, #0x78\n"            // r7 = 0x78
        "strb r7, [r6, r8]\n"        // *(DataPtr + R8) = 0x78
        :
        : "r" (unDataPtr)              // Input: %0 = DataPtr
        : "r6", "r7", "r8", "memory" // Clobbered registers and memory
    );
*/
    //func();
/*
   asm volatile (
        "mov lr, #0\n"      // �?空�?��?�地???�?存器
        "bx %0\n"           // 跳�?��?��??定地????��行�??械碼
        :
        : "r" (__kernel_plc_mem)        // 輸�?��???���?�? __kernel_plc_mem ??��?��??存器
        : "lr"              // ??�訴編譯?��，`lr` �?存器�?被修?��
    );
*/
printk("20241229 _RunCode 425 after func\n");
    //vfree(code_mem); // ??�放??��??





/*

	//set_memory_x((unsigned long)CodePtr, 1);
	func = (void (*)(void))unCodePtr;

    printk("20241229 _RunCode 407 Start Executing machine code at address 0x%lx\n", unCodePtr);

    // 使用??��???��編�?��?? R4-R11
    asm volatile(
        "stmdb sp!, {r4-r11}\n"   // �? R4-R11 ?��??��????? (保�??)
        :
        :
        : "memory"
    );
printk("20241229 _RunCode 417 Start1 Executing machine code at address 0x%lx\n", unCodePtr);

    // ?��行�??械碼
    func();
printk("20241229 _RunCode 417 Start2 Executing machine code at address 0x%lx\n", unCodePtr);
    //使用??��???��編�?�復 R4-R11
    asm volatile(
        "ldmia sp!, {r4-r11}\n"   // 從�????��???�� R4-R11 (??�復)
        :
        :
        : "memory"
    );

     printk("20241229 _RunCode 428 End Executing machine code at address 0x%lx\n", unDataPtr);



	


#if 0
    #ifdef ATT_VER
	
	printk("This is a PLC test 20241129\n");

    __asm__  __volatile__ (
        "pusha\n\t"

        "xorl  %%esi,%%esi\n\t"  //設�??esi=0

        "movl  %0,%%edx\n\t"
        "movl  %1,%%ebx\n\t"
        "movl  %%edx,257532(%%ebx)\n\t"     //Code??????�????��?��?? R47999
        "call  %0\n\t"

        "popa  \n\t"
        :
        :"g" (CodePtr), "g" (DataPtr)    // CodePtr??��?????,�????????%0; DataPtr??��?????,�????????%1
        :"ebx","esi","edx"               //?????�compiler, ebx,esi???被使???,�??�??護�??
    );

    #else

    asm {
        XOR  ESI,ESI                        // subroutine stack
        push edx
        mov  edx,CodePtr
        MOV  EBX,DataPtr                    // ??????I/O/C/S/A�????????
        mov  [ebx + 0x8FFFC],edx             // �??�??�??段�???????��??置�???????�Data???????????�尾?????��?�給PLC使�??
        pop  edx
        CALL DS:[&CodePtr]                  // Call�??�?????�??
    }
    #endif
#endif
*/
}

PLC_CORE *plccore_Init(void)
{
	short       i;
    PLC_CORE    *obj = NULL;
    
    obj = Rbase_getPLCbase();
    printk("[PLC] plccore_Init tPlc 0x%x\n",obj);

    printk("TPLCData: 0x%x\n",obj->FPLCData);
    printk("FTimeRef: 0x%x sizeof: %d\n",&obj->FPLCData->FTimeRef,sizeof(TTimerRef));
    printk("WriteROp: 0x%x sizeof: %d\n",&obj->FPLCData->FWriteROption,sizeof(TWriteROption));
    printk("NoUse   : 0x%x  sizeof: %d 0x%x\n",obj->FPLCData->NoUse,sizeof(obj->FPLCData->NoUse),sizeof(obj->FPLCData->NoUse));
    printk("R0      : 0x%x\n",&obj->FPLCData->FRegisterBuffer[0]);


    obj->FPLCMData->r_reg = PLCMACROIF_ReadR;
    obj->FPLCMData->r_mlc_i = PLCMACROIF_ReadI;
    obj->FPLCMData->r_mlc_o = PLCMACROIF_ReadO;
    obj->FPLCMData->r_mlc_c = PLCMACROIF_ReadC;
    obj->FPLCMData->r_mlc_s = PLCMACROIF_ReadS;
    obj->FPLCMData->r_mlc_a = PLCMACROIF_ReadA;
    obj->FPLCMData->r_reg_bit = PLCMACROIF_ReadR_Bit;
    obj->FPLCMData->w_reg = PLCMACROIF_WriteR;
    obj->FPLCMData->w_mlc_i = PLCMACROIF_WriteI;
    obj->FPLCMData->w_mlc_o = PLCMACROIF_WriteO;
    obj->FPLCMData->w_mlc_c = PLCMACROIF_WriteC;
    obj->FPLCMData->w_mlc_s = PLCMACROIF_WriteS;
    obj->FPLCMData->w_mlc_a = PLCMACROIF_WriteA;
    obj->FPLCMData->w_reg_bit = PLCMACROIF_WriteR_Bit;
    obj->FPLCMData->snprintf = PLCMACROIF_snprintf;
    obj->FPLCMData->memcpy = PLCMACROIF_memcpy;
    /* For int-oriented compiling. */
    obj->FPLCMData->IntOrientGetFirstRun = PLCMACROIF_IntOrientGetFirstRun;
    obj->FPLCMData->IntOrientSetFirstRun = PLCMACROIF_IntOrientSetFirstRun;
    obj->FPLCMData->IntOrientClearVarsToVacant = PLCMACROIF_IntOrientClearVarsToVacant;
    obj->FPLCMData->memPtrType = USER_VAR_MEM;


    plccore_WriteR( obj, VERSION_REG, PLC_VERSION);

    //2.IOMap
    printk("bf iomap\n");
    iomap_Init( &obj->tIOMap, obj->FIBits, obj->FOBits );

    return obj;
}

short plccore_Release(void)
{
    short i;

    //iec ??�份
    for(i=0;i<IEC_MAX_NUM;i++)
    {
#ifdef _LINUX
        //rtai_kfree(nam2num(PLC_CODE_SM_NAME)+MLC_MAX_NUM+i);
        kfree(tPlc->caIECCode[i]);
#else
        free(tPlc->caIECCode[i]);
#endif
    }
    //1.Data??�份
    if( tPlc->FPLCData )
    {
#ifdef _LINUX
        //rtai_kfree( nam2num(PLC_DATA_SM_NAME) );
        //rtai_kfree( nam2num(PLC_MDATA_SM_NAME));
        //rtai_kfree( nam2num(PLC_IECDATA_SM_NAME));
        kfree(tPlc->FPLCMData);
        kfree(tPlc->FPLCData );
        kfree(tPlc->FPLCData );

        //kfree(tPlc->FPLCMData);
        //kfree( tPlc->FPLCData );
        //vfree( tPlc->FPLCData );
#else
        free( tPlc->FPLCData );
        free( tPlc->FPLCMData );
        free( tPlc->pIECData);
#endif
        kfree(gNumIndexedGlobalVarBuf);
        kfree(gNumIndexedLocalVarBuf);
        kfree(gRingBuf);
    }

    //2.IOMap
    iomap_Release(&tPlc->tIOMap);

    //3.Code??�份
    for(i=0;i<MLC_MAX_NUM;i++)
    {
 //       tPlc->saCodeAssigned[i] = 0;
        if( tPlc->caCode[i] )
        {
#ifdef _LINUX
            //rtai_kfree( nam2num(PLC_CODE_SM_NAME)+i );
            kfree( (void *)(tPlc->caCode[i]) );
            //vfree( (void *)(tPlc->caCode[i]) );
#else
            free( (void *)(tPlc->caCode[i]) );
#endif
        }
    }
    for(i=0;i<IEC_MAX_NUM;i++)
    {
        if( tPlc->caIECCode[i] )
        {
#ifdef _LINUX
            //rtai_kfree( nam2num(PLC_CODE_SM_NAME)+MLC_MAX_NUM+i );
            kfree( (void *)(tPlc->caIECCode[i]) );
#else
            free( (void *)(tPlc->caIECCode[i]) );
#endif
        }
    }

    return 0;
}

void plccore_RunLevel1( PLC_CORE *obj, short iNo)
{
    //printk("RunPLCMacro\n");
    //RunPLCMacro(obj->FPLCMData);
    //printk("This is a PLC test 561 20241129\n");
#if 0    
    static int i=0;
#endif    
	if ((iNo >= (MLC_MAX_NUM+IEC_MAX_NUM)) || (iNo < 0))  return;
#if 0
    if(i < 13)
    {
        //printk("[PLC]internal run slot=%d\n",(int)iNo);
    }
    if(1000 == i)
    {
        i=0;
    }
    i++;
#endif 
    /* iec code */
    if((iNo >= MLC_MAX_NUM) && (iNo <(MLC_MAX_NUM+IEC_MAX_NUM)))
    {
        short iecSlotNo=iNo-MLC_MAX_NUM;
        if(obj->saIECCodeAssigned[iecSlotNo])
        {
            //printk("[PLC]enter running iec code.\n");
            obj->pIECData->exeTypePtr=&(obj->iecExeType[iecSlotNo]);
            *(obj->pIECData->exeTypePtr)=DO_RUN;
            _RunCodeC( (long)(obj->caIECCode[iecSlotNo]),(long)(obj->pIECData), iNo);
            obj->level1RunCnt++ ;
        }
        return;
    }//if((iNo >= MLC_MAX_NUM) && (iNo <(MLC_MAX_NUM+IEC_MAX_NUM)))

    if( obj->saCodeAssigned[iNo] )
    {
        if (1==obj->saCodeType[iNo])
        {
            /* Reset the plcmacro var area to the default user area to prevent there is any sys/maker switching at other slots. */
            //obj->FPLCMData->dpNLocalVarAddrStart=gNumIndexedLocalVarBuf;
            //obj->FPLCMData->dpNGlobalVarAddrStart=gNumIndexedGlobalVarBuf;
            //obj->FPLCMData->intpNGlobalVarAddrStart=gNumIndexedGlobalVarIntBuf;
            //obj->FPLCMData->intpNLocalVarAddrStart=gNumIndexedLocalVarIntBuf;
            obj->FPLCMData->memPtrType=USER_VAR_MEM;
              _RunCodeC( (long)(obj->caCode[iNo]),(long)(obj->FPLCMData), iNo);
        }
        else
        {
            //printk(">> [%d] code=%p, date=%p\n", iNo,obj->caCode[iNo], obj->FPLCData );
            /* Copy the corresponding one shot bit area for this loaded code. */
            memcpy(obj->FPLCData->FOneShotBits,&devotedOneShotBitsArea[iNo][0],MAX_IOCSA_NO);
//20241229
				obj->FPLCData->FIBits[1]=26;
				obj->FPLCData->FRegisterBuffer[0]=36;
				
				printk("20241229 obj->FRegisterBuffer[1]=%ld",obj->FPLCData->FRegisterBuffer[1]);
				printk("20241229 obj->FPLCData[1]=%d",obj->FPLCData->FIBits[1]);
				
				unsigned long uaddress1 = (unsigned long)obj->FPLCData;
				unsigned long uaddress2 = (unsigned long)obj->FPLCData->FRegisterBuffer;
				printk("20241229 FPLCData address l =%ld u=%lu h=0x%lx",(long)(obj->FPLCData),uaddress1,uaddress1);
				printk("20241229 FRegisterBuffer l =%ld u=%lu h=0x%lx",(long)(obj->FPLCData->FRegisterBuffer),uaddress2,uaddress2);
				long address3=&(obj->FPLCData->FRegisterBuffer[1]);
				unsigned long uaddress3=&(obj->FPLCData->FRegisterBuffer[1]);

				printk("20241229 FRegisterBuffer[1] l =%ld u=%lu h=0x%lx",address3,uaddress3,uaddress3);
            _RunCode( (long)(obj->caCode[iNo]),(long)(obj->FPLCData), iNo );

            /* Copy back the oneshotbit status to the each loaded codes' devoted area. */
            memcpy(&devotedOneShotBitsArea[iNo][0],obj->FPLCData->FOneShotBits,MAX_IOCSA_NO);
        }

        obj->level1RunCnt++ ;
        /* After at least one run cycle, reset the firstLoadedNum for the next reload. */
        if( (MLC_MAX_NUM+IEC_MAX_NUM) < obj->level1RunCnt)
        {
            obj->firstLoadedNum = 0;
        }
    }
}

void plccore_RunLevel2(  PLC_CORE *obj, short iNo)
{
    //if ((iNo >= MLC_MAX_NUM) || (iNo < 0)) return;
    //if (FCodeAssigned[iNo] && FPLCCode2[iNo] != NULL)
    //    RunCode((int)FPLCCode2[iNo],(int)FPLCData,iNo);
}

// void plccore_UpdateTimer( PLC_CORE *obj, long RealIntTime )
// {
//     obj->FNowTimerRef += RealIntTime;                            // double??????�??累�?????10ms就�??�??
//     obj->FPLCData->FTimeRef.FTimerRef1 = (char)obj->FNowTimerRef;     // char  ??????�??累�?????10ms就�??�??
//     obj->FPLCData->FTimeRef.FTimerBuffer0 += RealIntTime;        // �??次�?��?��?��?��??1ms�??timer
//     while (obj->FNowTimerRef >= 10)
//     {
//         obj->FNowTimerRef  -= 10;                                // �??DDA??��??累�??�??次�??累�?????10ms就�??�??
//         obj->FPLCData->FTimeRef.FTimerRef1 -= 10;                // �??DDA??��??累�??�??次�??累�?????10ms就�??�??
//         obj->FPLCData->FTimeRef.FTimerBuffer1 ++;                // �??10ms累�??�??次�?????�??累�??
//         obj->FPLCData->FTimeRef.FTimerRef2 ++;                    // �??10ms累�??�??�??

//         if(obj->FPLCData->FTimeRef.FTimerRef2 >= 10)
//         {
//             obj->FPLCData->FTimeRef.FTimerRef2 = 0;                // �??10ms累�??�??次�??累�?????100ms就�??�??
//             obj->FPLCData->FTimeRef.FTimerBuffer2 ++;             // �??100ms累�??�??次�?????�??累�??
//             obj->FPLCData->FTimeRef.FTimerRef3 ++;                // �??100ms累�??�??�??

//             if(obj->FPLCData->FTimeRef.FTimerRef3 >= 10)
//             {
//                 obj->FPLCData->FTimeRef.FTimerRef3 = 0;            // �??100ms累�??�??次�??累�?????1000ms就�??�??
//                 obj->FPLCData->FTimeRef.FTimerBuffer3 ++;        // �??100ms累�??�??次�?????�??累�??
//             }
//         }
//     }
// }

void plccore_UpdateTimer(PLC_CORE *obj, long RealIntTime)
{
    printk("20250224 _plc_run 100 RealIntTime: %ld\n", RealIntTime);

    obj->FNowTimerRef += RealIntTime;
    printk("20250224 _plc_run 110 FNowTimerRef: %ld\n", obj->FNowTimerRef);

    obj->FPLCData->FTimeRef.FTimerRef1 = (char)obj->FNowTimerRef;
    printk("20250224 _plc_run 120 FTimerRef1: %d\n", obj->FPLCData->FTimeRef.FTimerRef1);

    obj->FPLCData->FTimeRef.FTimerBuffer0 += RealIntTime;
    printk("20250224 _plc_run 130 FTimerBuffer0: %ld\n", obj->FPLCData->FTimeRef.FTimerBuffer0);

    while (obj->FNowTimerRef >= 10)
    {
        obj->FNowTimerRef -= 10;
        printk("20250224 _plc_run 200 FNowTimerRef after -=10: %ld\n", obj->FNowTimerRef);

        obj->FPLCData->FTimeRef.FTimerRef1 -= 10;
        printk("20250224 _plc_run 210 FTimerRef1 after -=10: %d\n", obj->FPLCData->FTimeRef.FTimerRef1);

        obj->FPLCData->FTimeRef.FTimerBuffer1++;
        printk("20250224 _plc_run 220 FTimerBuffer1: %ld\n", obj->FPLCData->FTimeRef.FTimerBuffer1);

        obj->FPLCData->FTimeRef.FTimerRef2++;
        printk("20250224 _plc_run 230 FTimerRef2: %d\n", obj->FPLCData->FTimeRef.FTimerRef2);

        if (obj->FPLCData->FTimeRef.FTimerRef2 >= 10)
        {
            obj->FPLCData->FTimeRef.FTimerRef2 = 0;
            printk("20250224 _plc_run 300 FTimerRef2 reset to 0\n");

            obj->FPLCData->FTimeRef.FTimerBuffer2++;
            printk("20250224 _plc_run 310 FTimerBuffer2: %ld\n", obj->FPLCData->FTimeRef.FTimerBuffer2);

            obj->FPLCData->FTimeRef.FTimerRef3++;
            printk("20250224 _plc_run 320 FTimerRef3: %d\n", obj->FPLCData->FTimeRef.FTimerRef3);

            if (obj->FPLCData->FTimeRef.FTimerRef3 >= 10)
            {
                obj->FPLCData->FTimeRef.FTimerRef3 = 0;
                printk("20250224 _plc_run 400 FTimerRef3 reset to 0\n");

                obj->FPLCData->FTimeRef.FTimerBuffer3++;
                printk("20250224 _plc_run 410 FTimerBuffer3: %ld\n", obj->FPLCData->FTimeRef.FTimerBuffer3);
            }
        }
    }
}


//�????�PLC??��??�?????
static void _ClearPLCStateVar( TPLCData *obj)
{
    //IOCS??��????��??�??�??�?????
    memset( obj->FABits, 0, sizeof(obj->FABits));

    memset( obj->FOneShotBits, 0, sizeof( obj->FOneShotBits));
    memset( obj->FStatusStoreBits, 0, sizeof( obj->FStatusStoreBits));
    memset( obj->FCounterReg, 0, sizeof(obj->FCounterReg));
    memset( obj->FCounterStatus, 0, sizeof(obj->FCounterStatus));
    memset( obj->FTimerReg, 0, sizeof(obj->FTimerReg));
    memset( obj->FTimerStatus, 0, sizeof(obj->FTimerStatus));
    memset( &(obj->FTimeRef), 0, sizeof(obj->FTimeRef));
    memset(devotedOneShotBitsArea,0,sizeof(devotedOneShotBitsArea));
}


//�????�PLCMACRO??��??�?????
//注�??�????��??�????��??�????�int-oriented compiling???�????��??�?????�???????��?��?��??binary?????�第�??次�?��?????�????�該???�?????(�????��?��?�PLCMACROIF_IntOrientClearVars)�??延�?��??�??�????��??�??????????��????????�????��??�???????��????��??�????��?�以修�?��?????
static void _ClearPLCMacroStateVar( stPLCMData *obj)
{
    short i=0;
    /* PLC Macro. */
PRINT("20241229 _ClearPLCMacroStateVar 681.\n");
    memset(gRingBuf,0,RING_BUF_SIZE);
    for(i=0;i<GLOBAL_VAR_SIZE;i++)
    {
        *(gNumIndexedGlobalVarBuf+i)=DBL_MAX;
        *(gSysNumIndexedGlobalVarBuf+i)=DBL_MAX;
        *(gMakerNumIndexedGlobalVarBuf+i)=DBL_MAX;
    }
PRINT("20241229 _ClearPLCMacroStateVar 689.\n");
    for(i=0;i<LOCAL_VAR_SIZE;i++)
    {
        *(gNumIndexedLocalVarBuf+i)=DBL_MAX;
        *(gSysNumIndexedLocalVarBuf+i)=DBL_MAX;
        *(gMakerNumIndexedLocalVarBuf+i)=DBL_MAX;
    }
PRINT("20241229 _ClearPLCMacroStateVar 696.\n");
    memset(obj->errFlags,0,sizeof(tPlc->FPLCMData->errFlags));
    memset(obj->errSrcArrary,0,sizeof(tPlc->FPLCMData->errSrcArrary));
    obj->rearIdxDbgRingBuf=0;
    obj->lNLocalVarAddrOffset=0;
    obj->memPtrType=USER_VAR_MEM;
    /* Reset the first run flag to let the generated code to reset vars to zero again. */
    gIntOrientFirstRun = 0;
PRINT("20241229 _ClearPLCMacroStateVar 704.\n");
}

//�????�IECDATA??��??�?????
static void _ClearIECStateVar( stIECData *obj)
{
    obj->runtimeErrNum=0;

}

//第�?????�????��??plc�????��?��??�?????
static void __ClearPLCStateVar(PLC_CORE *obj)
{
   PRINT("20241229 __ClearPLCStateVar 711 start.\n");
    memset(obj->saCodeAssigned,0,sizeof(obj->saCodeAssigned));
    /* iec code part. */
    memset(obj->saIECCodeAssigned,0,sizeof(obj->saIECCodeAssigned));
    /* Reset the run cnt for the judgement of one run cycle. */
    obj->level1RunCnt = 0;
    /* Only run the clearing state data once. */
    _ClearPLCStateVar(obj->FPLCData);
 PRINT("20241229 _ClearPLCStateVar 721 start.\n");
//20241229 comment marco and iec    
//_ClearPLCMacroStateVar(obj->FPLCMData);
PRINT("20241229 _ClearPLCMacroStateVar 723 start.\n");
    //_ClearIECStateVar(obj->pIECData);
PRINT("20241229 _ClearIECStateVar 725 start.\n");

}
//! @brief     寫�?�PLC binary code
//! @param     obj      ??�件???�??\n
//! @param     type     Code形�??�?? 0:machine code by MLC, 1:machine code by C
//! @param     *data    Code�????????�??\n
//! @param     load_size  �????��??Code大�??  \n
//! @param     offset_addr     寫�?��?????移�?????  \n
//! @retval    0        ??�誤//
//! @retval    1       OK
short plccore_AssignCode( PLC_CORE *obj, void *Code, long Size,long Offset,short bIsCCode,short iNo)
{
    printk("20241229 plccore_AssignCode 734 start\n");
	//obj->caCode[0]=__kernel_plc_mem;
printk("20241229 plccore_AssignCode 734 after change memory\n");


printk("20241229 plccore_AssignCode 734 after change memory\n");
    if( iNo >= MLC_MAX_NUM + IEC_MAX_NUM || iNo < 0 ) return 0;
    /* Load iec code. */
    if( (iNo >= MLC_MAX_NUM) && (iNo < (MLC_MAX_NUM+IEC_MAX_NUM)))
    {
        short iecSlotNo=iNo-MLC_MAX_NUM;
        PRINT("[PLC]enter plccore_AssignCode for iec code.\n");
		  printk("20241229 plccore_AssignCode 742 plccore_AssignCode for iec code.\n");
        if((Offset+Size ) >= IEC_CODE_SIZE)
            return(0);
        if(0 != Offset)
            return(0);
        /* The code should be ready by user space copying with shared memory. */
#if 0
        // 1.???�??空code�??
        memset(obj->caIECCode[iecSlotNo],0, IEC_CODE_SIZE);
        // 2. ??��????��?????�??
        memcpy( (void*)((obj->caIECCode[iecSlotNo])), Code, Size);
#endif
        // 3. 設�??iecdata???iniOpType???exeType pointer
        obj->pIECData->iniOpTypePtr=&(obj->iecIniOpType[iecSlotNo]);
        obj->pIECData->exeTypePtr=&(obj->iecExeType[iecSlotNo]);
        // 4. ??��??iec initialization�??initialization???該�?�legacy ???�????��??設�??好�??
        *(obj->pIECData->exeTypePtr)=DO_INI;
        PRINT("[PLC]enter plccore_AssignCode for iec code. Before run code\n");
        /* iNo is not used within _RunCodeC */
        /* Current architect cannot support RETAIN mechanism since the code area would be clear during reloading. */
        *(obj->pIECData->iniOpTypePtr) = INI_OP_ALL;
        _RunCodeC( (long)(obj->caIECCode[iecSlotNo]),(long)(obj->pIECData), iNo);
        PRINT("[PLC]enter plccore_AssignCode for iec code. After run code\n");
        //5. �??示�?�已�?????
        if(0 == obj->firstLoadedNum)
        {
            PRINT("[PLC](IEC)firstLoaded slot=%d\n",iNo);
            __ClearPLCStateVar(obj);
            /* This value would be cleared for the reload within runlevel1 routine after at least one run cycle. */
            obj->firstLoadedNum = iNo + 1;
 PRINT("20241229 plccore_AssignCode 776 \n",iNo);
        }
        *(obj->pIECData->exeTypePtr)=DO_RUN;
 PRINT("20241229 plccore_AssignCode 779 \n",iNo);
        obj->saIECCodeAssigned[iecSlotNo] = 1;

        PRINT("[PLC] install iec code %d, type=%d, offset=0x%lx, size=0x%lx saIECCodeAssigned=%d\n",iecSlotNo,bIsCCode,Offset,Size,obj->saIECCodeAssigned[iecSlotNo]);
        return(1);
    }//if( (iNo >= MLC_MAX_NUM) && (iNo < (MLC_MAX_NUM+IEC_MAX_NUM)))

    if( Offset+Size >= PLC_CODE_SIZE )
        return 0;
    if (Offset == 0)
    {
PRINT("20241229 plccore_AssignCode Offset == 0 \n",iNo);
        // 1.???�??空code�??
        memset(obj->caCode[iNo],0, PLC_CODE_SIZE);

        // 2.�??空�?��??�?????�??
        /* Turn off all the assigned flag to prevent erronous trigger if the system interrupt is not stopped when loading PLC fails. */
        if(0 == obj->firstLoadedNum)
        {
PRINT("20241229 plccore_AssignCode 0 == obj->firstLoadedNum \n",iNo);
            PRINT("[PLC](non-IEC)firstLoaded slot=%d\n",iNo);
            __ClearPLCStateVar(obj);
            /* This value would be cleared for the reload within runlevel1 routine after at least one run cycle. */
            obj->firstLoadedNum = iNo + 1;
        }
    }
	


if(iNo==0){
	//void *source = (void *)((char *)Code + 0x400);
   //size_t length_to_copy = Size - 0x400;

    // ?��行�??�?
    //memcpy(__kernel_plc_mem, source, length_to_copy);
	//unsigned char first_byte = *((unsigned char *)source);
    //memcpy( (void*)((obj->caCode[iNo])+Offset), Code, Size);
	//PRINT("20241229 plccore_AssignCode memcpy First=0x%x length_to_copy=%d iNo=%d \n",first_byte,length_to_copy,iNo);

	size_t start_offset = 0x400;
	unsigned char *machine_code_all = (unsigned char *)kmalloc(Size,GFP_KERNEL);
	//unsigned char machine_code_all[2048];
	size_t copy_size = Size - start_offset;
	memcpy(machine_code_all, Code, Size);
	PRINT("20241229 plccore_AssignCode machine_code_all First=0x%x Size=%d iNo=%d \n",machine_code_all[0],Size,iNo);
	kfree(machine_code_all);
	unsigned char *machine_code_exe = (unsigned char *)kmalloc(copy_size,GFP_KERNEL);
	//unsigned char machine_code_exe[40];
	 memcpy(machine_code_exe, machine_code_all + start_offset, copy_size);
	PRINT("20241229 plccore_AssignCode machine_code_exe First=0x%x length_to_copy=%d iNo=%d \n",machine_code_exe[0],copy_size,iNo);	
	memcpy( __kernel_plc_mem, machine_code_exe, copy_size);
	kfree(machine_code_exe);
	PRINT("20241229 plccore_AssignCode __kernel_plc_mem copy_size=%d iNo=%d \n",copy_size,iNo);
	obj->saCodeAssigned[iNo] = 1;
	obj->saCodeType[iNo] = bIsCCode;

/*
unsigned char machine_code_3[] = {
0xB8, 0x7B, 0x04, 0xE3, //    mov r7 , Index1
0x00, 0x70, 0x40, 0xE3, //  movt r7 , Index1
0x07, 0x50, 0x96, 0xE7, //   ldr r5,[r6,r7]
0xFF, 0x50, 0x05, 0xE2,//  and r5 , r5 , #0xff
0x00, 0x00, 0x55, 0xE3, //cmp r5 , #0
0x03, 0x00, 0x00, 0x0A,//beq . + 20
0x0A, 0x80, 0xA0, 0xE3, //mov r8 , #input_value
0x00, 0x70, 0xA0, 0xE3, //mov r7 , Index_Reg
0x01, 0x70, 0x40, 0xE3, //movt r7 , Index_Reg
0x07, 0x80, 0x86, 0xE7, //str r8 [r6,r7]
0x1E, 0xFF, 0x2F, 0xE1  //bx lr
};
*/
/*
unsigned char machine_code_3[4];
memcpy(machine_code_3, (unsigned char *)Code + Size - 4, 4);

	printk("20241229 AssignCode Begin!! %x %x %x %x \n",machine_code_3[0],machine_code_3[1],machine_code_3[2],machine_code_3[3]);	
		memcpy(__kernel_plc_mem, machine_code_3, sizeof(machine_code_3));
	printk("20241229 AssignCode 1099 after memcpy __kernel_plc_mem=0x%lx sizeof(machine_code_3)=%d\n",__kernel_plc_mem,sizeof(machine_code_3));
   	//memcpy( __kernel_plc_mem, Code, Size);
    obj->saCodeAssigned[iNo] = 1;
    obj->saCodeType[iNo] = bIsCCode;
    PRINT("[PLC] 20241229 AssignCode 863 install code %d, addr=%lx type=%d, offset=0x%lx, size=0x%lx saCodeAssigned=%d\n",iNo,__kernel_plc_mem,bIsCCode,Offset,Size,obj->saCodeAssigned[iNo]);
*/
}

    return 1;
}


char  plccore_ReadI( PLC_CORE *obj, long Pos)
{
    if (Pos < MAX_IOCSA_NO && Pos >= 0)
        return obj->FIBits[Pos] != 0;
    else return 0;
}

char  plccore_ReadO( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_IOCSA_NO && Pos >= 0)
        return obj->FOBits[Pos] != 0;
    else return 0;
}

char  plccore_ReadC( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_IOCSA_NO && Pos >= 0)
        return obj->FCBits[Pos] != 0;
    else return 0;
}

char  plccore_ReadS( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_IOCSA_NO && Pos >= 0)
        return obj->FSBits[Pos] != 0;
    else return 0;
}

char  plccore_ReadA( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_IOCSA_NO && Pos >= 0)
        return obj->FABits[Pos] != 0;
    else return 0;
}

long plccore_ReadR( PLC_CORE *obj,long Pos)
{
    //if( (Pos >270000) && (Pos<280000) )
    //if(Pos==270620)
    //printk("plccore_ReadR[%d] = %d\n",Pos,obj->FRegisterBuffer[Pos]);
    if (Pos < MAX_REGISTER_NO && Pos >= 0)
        return obj->FRegisterBuffer[Pos];
    else
        return 0;
}

short plccore_ReadR_Bit( PLC_CORE *obj,long Pos,short bit)
{
    if (Pos < MAX_REGISTER_NO && Pos >= 0 && bit < 32 && bit >= 0)
        return ((obj->FRegisterBuffer[Pos] >> bit) & 0x0001);
    else return 0;
}

long plccore_ReadUR( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_USER_REG_NO && Pos >= 0)
        return obj->FUserReg[Pos/SIZE_4M][Pos];
    else return 0;
}

short plccore_ReadUR_Bit( PLC_CORE *obj,long Pos,short bit)
{
    if (Pos < MAX_USER_REG_NO && Pos >= 0 && bit < 32 && bit >= 0)
        return ((obj->FUserReg[Pos/SIZE_4M][Pos] >> bit) & 0x0001);
    else return 0;
}

double plccore_ReadF( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_DOUBLE_REG_NO && Pos >= 0)
        return obj->FDoubleReg[Pos];
    else return 0;
}

void  plccore_WriteI(  PLC_CORE *obj,long Pos,char OnOff)
{
    if (Pos < MAX_IOCSA_NO && Pos >= 0)
        obj->FIBits[Pos] = (OnOff ? 0xFF : 0);
}

void  plccore_WriteO(  PLC_CORE *obj,long Pos,char OnOff)
{
    if (Pos < MAX_IOCSA_NO && Pos >= 0)
        obj->FOBits[Pos] = (OnOff ? 0xFF : 0);
}

void  plccore_WriteC(  PLC_CORE *obj,long Pos,char OnOff)
{
    if (Pos < MAX_IOCSA_NO && Pos >= 0)
        obj->FCBits[Pos] = (OnOff ? 0xFF : 0);
}

void  plccore_WriteS(  PLC_CORE *obj,long Pos,char OnOff)
{
    if (Pos < MAX_IOCSA_NO && Pos >= 0)
        obj->FSBits[Pos] = (OnOff ? 0xFF : 0);
}

void  plccore_WriteA(  PLC_CORE *obj,long Pos,char OnOff)
{
    if (Pos < MAX_IOCSA_NO && Pos >= 0)
        obj->FABits[Pos] = (OnOff ? 0xFF : 0);
}
// -----------------------------------------------------------------------
//  ?????�說???:�????�寫??�R??��?????
//  ???    ???:Pos=�??寫�?��??�?????
//           Value=�??寫�?��?????
//  ??? ??? ???:???
// -----------------------------------------------------------------------
void plccore_WriteR( PLC_CORE *obj,long Pos,long Value)
{
    //if( (Pos >270000) && (Pos<280000) )
    //if(Pos==270620)
    //printk("plccore_WriteR[%d] = %d\n",Pos,Value);
    //if( (Pos>242700) && (Pos<242800) )
    //printk("WriteR [0x%x] %ld = 0x%x\n",obj,Pos,Value);
    if (Pos < MAX_REGISTER_NO && Pos >= 0)
    {
        obj->FRegisterBuffer[Pos] = Value;
    }
}

// -----------------------------------------------------------------------
//  ?????�說???:�????�寫??�R??��????��??Bit
//  ???    ???:Pos=�??寫�?��??�?????
//           bit=�??寫�?��??bit
//           Value=�??寫�?��?????
//  ??? ??? ???:???
// -----------------------------------------------------------------------
void plccore_WriteR_Bit( PLC_CORE *obj,long Pos,short bit,short Value)
{
    long status;

    if (Pos < MAX_REGISTER_NO && Pos >= 0 && bit < 32 && bit >= 0)
    {
        status = (1 << bit);
        if (Value)
            obj->FRegisterBuffer[Pos] |= status;
        else
            obj->FRegisterBuffer[Pos] &= ~status;
    }
}

void plccore_WriteUR( PLC_CORE *obj,long Pos,long Value)
{
    if (Pos < MAX_USER_REG_NO && Pos >= 0)
        obj->FUserReg[Pos/SIZE_4M][Pos] = Value;
}

// -----------------------------------------------------------------------
//  ?????�說???:�????�寫??�R??��????��??Bit
//  ???    ???:Pos=�??寫�?��??�?????
//           bit=�??寫�?��??bit
//           Value=�??寫�?��?????
//  ??? ??? ???:???
// -----------------------------------------------------------------------
void plccore_WriteUR_Bit( PLC_CORE *obj,long Pos,short bit,short Value)
{
    long status;

    if (Pos < MAX_USER_REG_NO && Pos >= 0 && bit < 32 && bit >= 0)
    {
        status = (1 << bit);
        if (Value)
            obj->FUserReg[Pos/SIZE_4M][Pos] |= status;
        else
            obj->FUserReg[Pos/SIZE_4M][Pos] &= ~status;
    }
}


void plccore_WriteF( PLC_CORE *obj,long Pos,double Value)
{
    if (Pos < MAX_DOUBLE_REG_NO && Pos >= 0)
    {
        obj->FDoubleReg[Pos] = Value;
    }
}

// -----------------------------------------------------------------------
//  ?????�說???:設�??Level2???�??�??�?????(??��????��??�??Buffer??????)
//  ???    ???:Pos=�??�??�?????
//  ??? ??? ???:???
// -----------------------------------------------------------------------
void plccore_SetLevel2( PLC_CORE *obj,long Pos,short iNo)
{
    //if ((iNo >= MLC_MAX_NUM) || (iNo < 0)) return;
    //if (Pos < 0) return;
    //char *Ptr = (char*) (FPLCCode[iNo] + Pos);

    //if (*Ptr != 0)                              // �?????該�????????�??�??碼�?????
    //    obj->FPLCCode2[iNo] = Ptr;
    ;
}

// -----------------------------------------------------------------------
//  ?????�說???:設�??Level2???�??�??�?????(??��????��??�??Buffer??????)
//  ???    ???:Pos=�??�??�?????
//  ??? ??? ???:???
// -----------------------------------------------------------------------
void plccore_SetLevel3(  PLC_CORE *obj,long Pos,short iNo)
{
    //if ((iNo >= MLC_MAX_NUM) || (iNo < 0)) return;
    //if (Pos < 0) return;
    //char *Ptr = (char*) (FPLCCode[iNo] + Pos);

    //if (*Ptr != 0)                              // �?????該�????????�??�??碼�?????
    //    obj->FPLCCode3[iNo] = Ptr;
    ;
}

// -----------------------------------------------------------------------
//  ?????�說???:寫�?�Timer???
//  ???    ???:Pos=???�?????�??�??
//           Value=�??寫�?��?????
//  ??? ??? ???:???
// -----------------------------------------------------------------------
void plccore_WriteTimer( PLC_CORE *obj,long Pos,long Value)
{
    if (Pos < MAX_TM_CN_NO && Pos >= 0)
    {
        obj->FTimerReg[Pos].SetValue = Value;      // load timer register init value
        obj->FTimerReg[Pos].NowValue = 0;          // Clear Timer Update Buffer
    }
}

// -----------------------------------------------------------------------
//  ?????�說???:寫�?�Timer???
//  ???    ???:Pos=???�?????�??�??
//           IncValue=�??????????��??�??�????��??
//           Value=�??寫�?��?????
//  ??? ??? ???:???
// -----------------------------------------------------------------------
void plccore_WriteCounter( PLC_CORE *obj,long Pos,long IncValue,long Value)
{
    if (Pos < MAX_TM_CN_NO && Pos >= 0)
    {
        obj->FCounterReg[Pos].InitValue = Value;       // load counter register init value
        if ( IncValue == 1 )
            obj->FCounterReg[Pos].NowValue  = 0;       // Clear Count Inc Buffer
        else
            obj->FCounterReg[Pos].NowValue  = Value;   // Update Count Inc Buffer

        obj->FCounterReg[Pos].UpDown  = IncValue;      // Update Count Inc Buffer
    }
}

// -----------------------------------------------------------------------
//  ?????�說???:�???????��?????Timer�????????
//  ???    ???:???
//  ??? ??? ???:??��?????Timer�????????
// -----------------------------------------------------------------------
long plccore_ReadTimer( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_TM_CN_NO && Pos >= 0)
        return obj->FTimerReg[Pos].NowValue;
    else
        return 0;
}

// -----------------------------------------------------------------------
//  ?????�說???:�???????��?????Timer設�?????
//  ???    ???:???
//  ??? ??? ???:??��?????Timer設�?????
// -----------------------------------------------------------------------
long plccore_ReadTimerSet( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_TM_CN_NO && Pos >= 0)
        return obj->FTimerReg[Pos].SetValue;
    else
        return 0;
}

// -----------------------------------------------------------------------
//  ?????�說???:�???????��?????Timer?????????
//  ???    ???:???
//  ??? ??? ???:??��?????Timer?????????
// -----------------------------------------------------------------------
short plccore_ReadTimerStatus( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_TM_CN_NO && Pos >= 0)
        return obj->FPLCData->FTimerStatus[Pos].FTimerStatus;
    else
        return 0;
}

// -----------------------------------------------------------------------
//  ?????�說???:�???????��?????Timer?????????
//  ???    ???:???
//  ??? ??? ???:??��?????Timer?????????
// -----------------------------------------------------------------------
short plccore_ReadTimerOutput( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_TM_CN_NO && Pos >= 0)
        return obj->FPLCData->FTimerStatus[Pos].FTimerOneShotBits;
    else
        return 0;
}

// -----------------------------------------------------------------------
//  ?????�說???:�???????��?????Counter�????��??
//  ???    ???:???
//  ??? ??? ???:??��?????Counter�????��??
// -----------------------------------------------------------------------
long plccore_ReadCounter( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_TM_CN_NO && Pos >= 0)
        return obj->FPLCData->FCounterReg[Pos].NowValue; // Return Count Inc Buffer
    else
        return 0;
}
// -----------------------------------------------------------------------
//  ?????�說???:�???????��?????Counter設�?????
//  ???    ???:???
//  ??? ??? ???:??��?????Counter設�?????
// -----------------------------------------------------------------------
long plccore_ReadCounterSet( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_TM_CN_NO && Pos >= 0)
        return obj->FPLCData->FCounterReg[Pos].InitValue; // Return Count Inc Buffer
    else
        return 0;
}

// -----------------------------------------------------------------------
//  ?????�說???:�???????��?????Counter?????????
//  ???    ???:???
//  ??? ??? ???:??��?????Counter?????????
// -----------------------------------------------------------------------
short plccore_ReadCounterStatus( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_TM_CN_NO && Pos >= 0)
        return obj->FPLCData->FCounterStatus[Pos].FCounterStatus;
    else
        return 0;
}

// -----------------------------------------------------------------------
//  ?????�說???:�???????��?????Counter?????????
//  ???    ???:???
//  ??? ??? ???:??��?????Counter?????????
// -----------------------------------------------------------------------
short plccore_ReadCounterOutput( PLC_CORE *obj,long Pos)
{
    if (Pos < MAX_TM_CN_NO && Pos >= 0)
        return obj->FPLCData->FCounterStatus[Pos].FCounterOneShotBits;
    else
        return 0;
}



// -----------------------------------------------------------------------
//  ?????�說???:?????��??�?????plcmacro code??��?��?��??runtime error
//  ???    ???:???
//  ??? ??? ???:0 ??�runtime error�??�?????0 ???eruntime error ??��??�????�使??�plccore_AskRuntimeErrByIndex確�????????�??種runtime error ??��?????
// -----------------------------------------------------------------------
short plccore_IsPLCMacroRuntimeErr( PLC_CORE *obj)
{
    short i=0;
    short isRuntimeErr=0;
    for(i=0;i<32;i++)
    {

        if(0 != obj->FPLCMData->errFlags[i])
        {
            /* There is runtime error occuring. */
            isRuntimeErr=1;
            break;
        }
    }
    return(isRuntimeErr);

}


// -----------------------------------------------------------------------
//  ?????�說???:使�?�index詢�????��?��?????該index???runtime error?????��??
//  ???    ???:???
//  ??? ??? ???:0 ??�runtime error�??�?????0 ???eruntime error ??��??�??errSrcName?????��????�runtime error???source file�??errSrcLine?????��?��?????�????????
//  注�??�??caller??��??�??errSrcName???errSrcLine???�????��?��??plc module???�??實�??�??????????��??�????��??copy???
// -----------------------------------------------------------------------

short plccore_AskRuntimeErrByIndex(PLC_CORE *obj,short index,char *errSrcName,unsigned int *errSrcLine)
{
    short errByteIndex=index/8;
    short errBitIndex=index % 8;
    unsigned char isErr=(obj->FPLCMData->errFlags[errByteIndex] & (0x01 << errBitIndex));
    if(0 != isErr)
    {
        /* There is an error of this index. */
        errSrcName=obj->FPLCMData->errSrcArrary[index].fileName;
        errSrcLine=&(obj->FPLCMData->errSrcArrary[index].lineNum);
        return(1);
    }
    else
    {
        return(0);
    }

}


// -----------------------------------------------------------------------
//  ?????�說???:?????��??�?????iec code??��?��?��??runtime error�??詳細�?????以plccore_AskIECRuntimeErrByIndex穫�?????
//  ???    ???:???
//  ??? ??? ???:0 ??�runtime error�??�?????0 ???eruntime error ??��??�??�????�runtime error??��?????
// -----------------------------------------------------------------------
short plccore_IsIECRuntimeErr( PLC_CORE *obj)
{
    short isRuntimeErr=obj->pIECData->runtimeErrNum;
    return(isRuntimeErr);
}


// -----------------------------------------------------------------------
//  ?????�說???:使�?�index詢�??第index-th runtime error�??�??�????�plccore_IsIECRuntimeErr�????�runtime err??��??�?????以此�????��??詳細�??�??
//  ???    ???:???
//  ??? ??? ???:0 ??�runtime error�??�?????0 ???eruntime error ??��??�????�error �?????�????????
//  注�??�??caller??��??�??errSrcName???errSrcLine???�????��?��??plc module???�??實�??�??????????��??�????��??copy???
// -----------------------------------------------------------------------

short plccore_AskIECRuntimeErrByIndex(PLC_CORE *obj,short index,char *errSrcName,unsigned int *errSrcLine)
{
    if((RUNTIME_ERR_ARRAY_SIZE >= index) || (obj->pIECData->runtimeErrNum <= index))
    {
        errSrcName=NULL;
        errSrcLine=NULL;
        return(0);
    }
    errSrcName=obj->pIECData->errSrcArrary[index].fileName;
    errSrcLine=&obj->pIECData->errSrcArrary[index].lineNum;
    return(obj->pIECData->errSrcArrary[index].runtimeErrIndex);

}

