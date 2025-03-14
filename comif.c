#include "com.h"
#include "aiomap.h"
#include "com_sym.h"
#include "plc_sym.h"
#include "hwif_sym.h"
//#include "sz.h"
#include "rtdr.h"
#include "tmmsr.h" //時間量測物件
#include "com_ioctl.h"
#include <asm/div64.h>
#include <asm/neon.h>
#include <linux/interrupt.h>

//The global third party function pointer.
static void (*gThirdPartyFuncPtr)(void) = NULL;
void DLL_PREFIX COM_MountThirdPartyISRFuncPtr(void(*proc_func)(void))
{
    gThirdPartyFuncPtr = proc_func;
}

void DLL_PREFIX COM_DemountThirdPartyISRFuncPtr(void)
{
    gThirdPartyFuncPtr = NULL;
}

//The function pointer which can be assigned between input reading & plc run.
static void (*gFuncPtrBetweenInputNPLCRun)(void) = NULL;
void DLL_PREFIX COM_MountFuncPtrBetweenInputNPLCRun(void(*proc_func)(void))
{
    gFuncPtrBetweenInputNPLCRun = proc_func;
}

void DLL_PREFIX COM_DemountFuncPtrBetweenInputNPLCRun(void)
{
    gFuncPtrBetweenInputNPLCRun = NULL;
}

//The global Product_SYS function pointer.
static void (*gProductSYSFuncPtr)(void) = NULL;
void DLL_PREFIX COM_MountProductSysISRFuncPtr(void(*proc_func)(void))
{
    gProductSYSFuncPtr = proc_func;
}

void DLL_PREFIX COM_DemountProductSysISRFuncPtr(void)
{
    gProductSYSFuncPtr = NULL;
}

//The global Product_DEV function pointer.
static void (*gProductDEVFuncPtr)(void) = NULL;
void DLL_PREFIX COM_MountProductDevISRFuncPtr(void(*proc_func)(void))
{
    gProductDEVFuncPtr = proc_func;
}

void DLL_PREFIX COM_DemountProductDevISRFuncPtr(void)
{
    gProductDEVFuncPtr = NULL;
}

//The global Product_USER function pointer.
static void (*gProductUSERFuncPtr)(void) = NULL;
void DLL_PREFIX COM_MountProductUserISRFuncPtr(void(*proc_func)(void))
{
    gProductUSERFuncPtr = proc_func;
}

void DLL_PREFIX COM_DemountProductUserISRFuncPtr(void)
{
    gProductUSERFuncPtr = NULL;
}

//static int FObjCount = 0;            // 所建立的物件數量(含繼承出去的)
static unsigned long iISRCount;
static char  bPLCCodeInit[MLC_MAX_NUM+IEC_MAX_NUM];
short iomap_update[5];
AIOMAP AIMap;
AIOMAP AOMap;
AIOMAP TCIMap;
stNetPeriphList gNetPeriphList;         // The list for peripherals needed connection monitoring.
// 以下為記錄使用設定的EtherCAT站號，當相對Map更新時檢查該裝置是否存在
stNetPeriphSetConfig gNetPeriphSetConfig_DI;
stNetPeriphSetConfig gNetPeriphSetConfig_DO;
stNetPeriphSetConfig gNetPeriphSetConfig_AI;
stNetPeriphSetConfig gNetPeriphSetConfig_AO;
stNetPeriphSetConfig gNetPeriphSetConfig_TCI;
stComISRIntervalCtrldModuleSet gComISRIntervalCtrldModuleSet={0}; //Used to control the run interval of modules in ISR.
static short boot_Delay_Cnt=0;    //Used to delay the excution of the contents of COM_ExecuteLevel1 to wait the ready of net IO.

int divide(int dividend, int divisor){	  /* dividend: 被除数   divisor: 除数   返回商  */
    long long div=divisor, did=dividend;  /* 除数，被除数  都转换为64位数据，避免溢出 */
    long long quo=0;                      /* 商 */
    int  sign=0;
    int i=0;
 
    /* 正负号处理，全部转换为无符号数进行移位运算 */
    if( did<0 ){ sign = !sign; did = -did; }
    if( div<0 ){ sign = !sign; div = -div; }
 
    /* 除法主体 */
    for( i=31; (i>=0) ;  i--){        /* 按移位次数从大到小试探 */ 
        if( did>=(div<<i) ){              /* 最接近被除数且比被除数小的移位次数 */
            quo += ((long long)1<<i);     /* 更新结果 */ //将计算出来的一个子项加到商上
            did -= (div<<i);              /* 更新被除数 */ 
        }
    }
 
    quo = (sign==1) ? -quo : quo;         /* 获得最终结果 */
    return  (quo<-21474836478 || quo>2147483647) ? 2147483647 : quo; /* 结果溢出判断 */
}


// -----------------------------------------------------------------------
//  Private function
// -----------------------------------------------------------------------
void DLL_PREFIX COM_AllocObj( void )
{
    aiomap_Init( &AIMap);
    aiomap_Init( &AOMap);
    aiomap_Init( &TCIMap);

    tmmsr_Init();
}

void DLL_PREFIX COM_FreeObj( void )
{
//    szapp_Exit();
}

//! @brief     初始化HWIF
//! @param     no 資訊號碼
//!                 0  : 中斷時間(單位：us)
//!                 1  : 主要中斷計數次數
//!             100~105: EPCIO軸卡0~5之ERROR COUNTER溢位計數值
//!             106~111: EPCIO軸卡0~5之LOCAL輸入計數值
//!             112~117: EPCIO軸卡0~5之DDA中斷計數值
//!             118~123: EPCIO軸卡0~5之Index中斷計數值
//!             124~129: EPCIO軸卡0~5之RIO中斷計數值
//!             130~135: EPCIO軸卡0~5之Timer中斷計數值
//!             136~141: EPCIO軸卡0~5之非預期中斷計數值
//!             142    : 熱電耦模組目前掃瞄的頻道
//! @retval    對應的資訊數值
//INT32 hwif_GetInfo( INT16 no )

static long GetTimeBase( void )
{
    static long FOldTimeBase=0,FNewTimeBase=0; //用於計算中斷時間
    static long FOldRemTime=0;  //用於記錄上次殘餘的時間,單位us
    long        abc=0;
    unsigned long RetValue;

    //modify by ted for 記錄殘時間
//    printk("bf------hwif_GetInfo(1)--\n");
    FNewTimeBase = hwif_GetInfo(1);
//    printk("bf------hwif_GetInfo(0)--\n");
   abc = hwif_GetInfo(0);    //wait edan modify
   printk("af------hwif_GetInfo(0)--%lu\n",abc);
   RetValue = ((FNewTimeBase -  FOldTimeBase) * abc + FOldRemTime);
   RetValue = ((FNewTimeBase -  FOldTimeBase) * hwif_GetInfo(0) + FOldRemTime);
//    printk("af------hwif_GetInfo(0)--\n");
//    FOldRemTime = RetValue % 1000;
//    RetValue = RetValue / 1000;//us->ms
    RetValue = divide(RetValue,1000);//us->ms

    FOldTimeBase = FNewTimeBase;
    return RetValue;
}



static long GetTimeBase(void)
{
    static long FOldTimeBase = 0, FNewTimeBase = 0;  // 用於計算中斷時間
    static long FOldRemTime = 0;  // 用於記錄上次殘餘的時間，單位 us
    long abc = 0;
    unsigned long RetValue;

    printk("2025_PLC_GetTimeBase 100 [Before] FOldTimeBase: %ld, FOldRemTime: %ld\n", FOldTimeBase, FOldRemTime);

    // 獲取新的時間基準
    FNewTimeBase = hwif_GetInfo(1);
    printk("2025_PLC_GetTimeBase 110 FNewTimeBase (hwif_GetInfo(1)): %ld\n", FNewTimeBase);

    abc = hwif_GetInfo(0); // 等待 Edan 修改
    printk("2025_PLC_GetTimeBase 120 abc (hwif_GetInfo(0)): %ld\n", abc);

    // 計算 RetValue
    RetValue = ((FNewTimeBase - FOldTimeBase) * abc + FOldRemTime); // (27 - 26 )* 4000+ 0 = 4000

    printk("2025_PLC_GetTimeBase 130 RetValue (before division): %lu\n", RetValue);

    RetValue = ((FNewTimeBase - FOldTimeBase) * hwif_GetInfo(0) + FOldRemTime);
    printk("2025_PLC_GetTimeBase 140 RetValue (after hwif_GetInfo(0) recompute): %lu\n", RetValue);

    FOldRemTime = RetValue % 1000; // 0
    printk("2025_PLC_GetTimeBase 150 FOldRemTime (after modulo 1000): %ld\n", FOldRemTime);

    // RetValue = RetValue / 1000;  // us -> ms  //4
    // printk("2025_PLC_GetTimeBase 160 RetValue (after division by 1000): %lu\n", RetValue);

    RetValue = divide(RetValue, 1000);  // us -> ms //0
    printk("2025_PLC_GetTimeBase 170 RetValue (after divide function): %lu\n", RetValue);

    FOldTimeBase = FNewTimeBase;
    printk("2025_PLC_GetTimeBase 180 [After] Updated FOldTimeBase: %ld\n", FOldTimeBase);

    return RetValue;
}



static void _check_IO_status(void)
{
    static short cnt = 10;
    short rio0,rio1,cio,st;

    //每百次中斷更新一次
    if( cnt-- <0 )
    {
        cnt=100;

        //斷線檢查與警報
        rio0 = hwif_DIOInfo(0,0,0);
        rio1 = hwif_DIOInfo(0,1,0);
        cio  = hwif_DIOInfo(3,9,0);

        COM_WriteR_Bit(IO_STATE_REG, 0, rio0 ); //RIO1是否斷線
        COM_WriteR_Bit(IO_STATE_REG, 1, rio1 ); //RIO2是否斷線
        COM_WriteR_Bit(IO_STATE_REG, 2, cio  ); //CIO斷線

	     COM_WriteR_Bit(COM_ALARM_BASE, COM_ALARM_RIO1_ERR, rio0); //RIO1斷線
        COM_WriteR_Bit(COM_ALARM_BASE, COM_ALARM_RIO2_ERR, rio1); //RIO2斷線
        //COM_WriteR_Bit(COM_ALARM_BASE, COM_ALARM_CIO_ERR , cio );  //CIO斷線

        //針對CIO錯誤站號發出警報
        if( cio )
        {
            st = hwif_DIOInfo(3,1,-1);
            if( st >= 0 && st <= 7)
                COM_WriteR_Bit( COM_CIO_ALARM, st, 1);
            //0:NoError,1:TimeOutError,2:RX Reg Error,3:TX Reg Error, 4:CRC Error,  10:No Station Define
            switch(hwif_DIOInfo(3,10,-1))
            {
                case 1: //TimeOut
                    COM_WriteR_Bit( COM_CIO_ALARM, 16, 1);
                    break;
                case 2: //Rx Error
                    COM_WriteR_Bit( COM_CIO_ALARM, 17, 1);
                    break;
                case 3: //Tx Error
                    COM_WriteR_Bit( COM_CIO_ALARM, 18, 1);
                    break;
                case 4: //CRC Error
                    COM_WriteR_Bit( COM_CIO_ALARM, 19, 1);
                    break;
            }
        }
        else
            COM_WriteR( COM_CIO_ALARM, 0);


    	//CIO Reset
    	if( COM_ReadR_Bit( IO_RESET_REG,2) )
    	{
    	    hwif_DIOReset(3);
    	    COM_WriteR_Bit( IO_RESET_REG,2, 0);
    	}
    }
}

/*!
    \brief Generate Net periph alarm.

    errDev is the order of the ith device with connection error. Currently only report maximum 4 devices with errors at one time.
*/

static void _gen_NetPeriph_alarm(short station,short errIdx,short errDev)
{
    /* Write the dev station information to register. */
    COM_WriteR(COM_NET_PERIPH_STATION_REG_START+errDev,station);
    /* Trigger the corresponding alarm. */
    switch(errIdx)
    {
    case -2:
        COM_WriteR_Bit(COM_NET_PERIPH_ALARM,COM_ALARM_NET_PERIPH_DISCONNETED+errDev,1);
        break;
    case -3:
        COM_WriteR_Bit(COM_NET_PERIPH_ALARM,COM_ALARM_NET_PERIPH_STATION_SET_OVERRANGE+errDev,1);
        break;
    case -5:
        COM_WriteR_Bit(COM_NET_PERIPH_ALARM,COM_ALARM_NET_PERIPH_DUPLICATED_STATION+errDev,1);
        break;
    case -6:
        COM_WriteR_Bit(COM_NET_PERIPH_ALARM,COM_ALARM_NET_PERIPH_MEM_ALLOC_FAIL+errDev,1);
        break;
    case -7:
        COM_WriteR_Bit(COM_NET_PERIPH_ALARM,COM_ALARM_NET_PERIPH_UNSUPPORTED_ETHERCAT_DEV+errDev,1);
        break;
    case -9:
        COM_WriteR_Bit(COM_NET_PERIPH_ALARM,COM_ALARM_NET_PERIPH_PACKET_OVERSIZE+errDev,1);
        break;
    case -11:
        COM_WriteR_Bit(COM_NET_PERIPH_ALARM,COM_ALARM_NET_PERIPH_DEV_MALFUNCTION+errDev,1);
        break;
    default:
        COM_WriteR_Bit(COM_NET_PERIPH_ALARM,COM_ALARM_NET_PERIPH_UNKNOWN_ERR+errDev,1);
        break;
    }
}

/*!
    \brief Monitoring net type peripherals (etherCAT for now)

    The connection status would only be correctly reported for station #, slot 0 which this would be included in the device list query.\n

*/
static void _check_NetPeriph_status(void)
{
    static long chkCnt=0;
    static short cnt = 1;
    /* If the list is not valid, do the check every interrupt. */
    if(0 == gNetPeriphList.isValid)
    {
        HWIF_DEVICE_LIST hwifList;
        short devNum=0;
        short ret=-1;
        ret=hwif_GetDeviceListV2(&devNum,&hwifList);
        /* The device list is valid. */
        if(0 == ret)
        {
            short i=0;
            /* Put the devices we care into our list. */
            short comMonDevsNum=0;
            for(i=0;i<devNum;i++)
            {
                printk("[COM]devNum=%d Listed net %d devs:%d\n",devNum,i,hwifList.type[i]);
                /* Found one peripheral that com will monitor. */
                if((999<hwifList.type[i]) && (2000 > hwifList.type[i]))
                {   //  slot: aabb 0101~ station,slot
                    short tmpVal=hwifList.st_slot[i];
                    short station=tmpVal/100;
                    short slot=tmpVal%100;
                    /* station starts from 1, so minus 10 slots. */
                    int uRegIdxForType = R_NET_PERIPH_TYPE_START+i;
                    gNetPeriphList.devList[comMonDevsNum].devID=hwifList.type[i];
                    gNetPeriphList.devList[comMonDevsNum].station=station;
                    gNetPeriphList.devList[comMonDevsNum].slot=slot;
                    comMonDevsNum++;
                    printk("[COM]Found dev for monitoring,id=%d station=%d slot=%d\n",hwifList.type[i],station,slot);
                    /* Write the type into the corresponding registers. */
                    if(0 < station)
                    {
                        COM_WriteUR(uRegIdxForType,hwifList.type[i]);
                    }
                }
            }//for(i=0;i<devNum;i++)
            /* Record the monitored dev number and set the list vaild. */
            gNetPeriphList.devNum=comMonDevsNum;
            gNetPeriphList.isValid=1;
        }//if(0 == ret)
        else
        {
            printk("[COM]hwifList is not valid, rtn of hwif_GetDeviceListV2=%d\n",ret);
        }
    }//if(0 == gNetPeriphList.isValid)
    else
    {
        /* If the list is valid, don't check the list again but do the monitoring every set period. */
        /* Do the monitor per 99 interrupt (don't put all the working with the same period.) */
        if( cnt-- <0 )
        {
            short i=0;
            short errDevNum=0;
            char isTCLAlarmIssued=0;
            cnt=99;
            /* Monitor the devices that com is in charge of. */
            for(i=0;i<gNetPeriphList.devNum;i++)
            {
                /* The connection status would only be correctly reported for station #, slot 0, which is the station itself. */
                if(0 == gNetPeriphList.devList[i].slot)
                {
                    short ret=0;
                    ret=hwif_SlotIOStatus(gNetPeriphList.devList[i].station,gNetPeriphList.devList[i].slot);
                    /* Exceed the number that can be reported at this time. */
                    if(0 > ret)
                    {
                        //printk("[COM]chkcnt=%d Net peripheral connection error. station:%d slot:%d err idx:%d\n",chkCnt,gNetPeriphList.devList[i].station,gNetPeriphList.devList[i].slot,ret);
                        if(4 >= errDevNum)
                        {
                            _gen_NetPeriph_alarm(gNetPeriphList.devList[i].station,ret,errDevNum);
                        }
                        errDevNum++;
                        if(0 == isTCLAlarmIssued)
                        {
                            /* Check if this disconnected station has a TCL slot. */
                            int j=0;
                            for(j=0;j<gNetPeriphList.devNum;j++)
                            {
                                /* If the disconnect device includes TCL, issue alarm. */
                                if(
                                    (gNetPeriphList.devList[i].station == gNetPeriphList.devList[j].station) &&
                                    (gNetPeriphList.devList[j].devID == 1016)
                                )
                                {
                                    /* Issue TCL alarm. */
                                    COM_WriteR_Bit(COM_MISC_ALARM1,COM_ALARM_NET_PERIPH_TCL_DISCONNECTED,1);
                                    isTCLAlarmIssued=1;
                                    break;
                                }

                            }//for(j=0;j<NetPeriphList.devNum;j++)

                        }//if(0 == isTCLAlarmIssued)
                    }
                    else
                    {
                        //printk("[COM]chkcnt=%d Net peripheral connection success. station:%d slot:%d rtn value:%d\n",chkCnt,gNetPeriphList.devList[i].station,gNetPeriphList.devList[i].slot,ret);
                    }

                }//if(0 == gNetPeriphList.devList[i].slot)
            }//for(i=0;i<gNetPeriphList.devNum;i++)
            chkCnt++;

        }//if( cnt-- <0 )

    }//else of if(0 == gNetPeriphList.isValid)

}

static void _plc_check_runtime_err(void)
{
    short rtnVal=0;
    /* Check PLCMacro runtime error. */
    rtnVal = PLC_IsPLCMacroRuntimeErr();
    /* This is to let reloading plc can erase the error. C3000 seems not clearing this area? */
    COM_WriteR_Bit(COM_PLC_RUNTIME_ERR_ALARM,COM_ALARM_PLCMACRO_RUNTIME_ERR,0);
    if(0 != rtnVal)
    {
//        COM_WriteR_Bit(COM_PLC_RUNTIME_ERR_ALARM,COM_ALARM_PLCMACRO_RUNTIME_ERR,1);
    }
    /* Check IEC runtime error. */
    rtnVal = PLC_IsIECRuntimeErr();
    /* This is to let reloading plc can erase the error. C3000 seems not clearing this area? */
    COM_WriteR_Bit(COM_PLC_RUNTIME_ERR_ALARM,COM_ALARM_IEC_PLC_RUNTIME_ERR,0);
    if(0 != rtnVal)
    {
//        COM_WriteR_Bit(COM_PLC_RUNTIME_ERR_ALARM,COM_ALARM_IEC_PLC_RUNTIME_ERR,1);
    }
}

static void _plc_run( void )
{
    static int iPLCIsr[13] = {0,0,0,0,0,0,0,0,0,0,0,0,0};
    short i;
printk("20250224 _plc_run 412\n");
    //更新IOMap時，不執行PLC
    //if( comobj_IsIOMapUpdating() )
    //    return;
//     for(i=0;i<5;i++)
//     {

//         if( iomap_update[i] )
//             return;
//     }
    /* Check PLCMacro/IEC PLC runtime error. */

    _plc_check_runtime_err();
printk("20250224 _plc_run after _plc_check_runtime_err() \n");
    //若DIO未Ready，則不執行PLC
    //if( !hwif_DIOIsReady() )
    //    return;

	//更新時間
	PLC_UpdateTimer(GetTimeBase());
printk("20250224 _plc_run after PLC_UpdateTimer \n");

    if ((COM_ReadR(PLC_ORDER_REG) == PLC_RUN) || (COM_ReadR(PLC_ORDER_REG) == PLC_SINGLE))
	{
		if (iPLCIsr[0] <= 1)
		{
			if (bPLCCodeInit[0])
		    {
		        //printk("[PLCD] run %hd start\n",0);
				PLC_RunLevel1N(0);
				//printk("[PLCD] run %hd end\n",0);
			}
			iPLCIsr[0] = COM_ReadR(PLC_RUN_ISR_COUNT+0);
		}
		else
			iPLCIsr[0]--;
		if (COM_ReadR(PLC_ORDER_REG) == PLC_SINGLE)
			COM_WriteR(PLC_ORDER_REG,PLC_STOP);
	}


	if (COM_ReadR(PLC_STATE_REG) != COM_ReadR(PLC_ORDER_REG))
		COM_WriteR(PLC_STATE_REG,COM_ReadR(PLC_ORDER_REG));


	for (i=1;i<MLC_MAX_NUM;i++)
	{
		if ((COM_ReadR(PLC_ORDER_REG2+i) == PLC_RUN) || (COM_ReadR(PLC_ORDER_REG2+i) == PLC_SINGLE))
	    {
	    	if (iPLCIsr[i] <= 1)
			{
				if (bPLCCodeInit[i])
				{
				    //printk("[PLCD] run %hd start\n",i);
					PLC_RunLevel1N(i);
				    //printk("[PLCD] run %hd end\n",i);
				}
				iPLCIsr[i] = COM_ReadR(PLC_RUN_ISR_COUNT+i);
			}
			else
				iPLCIsr[i]--;
			if (COM_ReadR(PLC_ORDER_REG2+i) == PLC_SINGLE)
			    COM_WriteR(PLC_ORDER_REG2+i,PLC_STOP);
	    }
	    if (COM_ReadR(PLC_STATE_REG2+i) != COM_ReadR(PLC_ORDER_REG2+i))
		    COM_WriteR(PLC_STATE_REG2+i,COM_ReadR(PLC_ORDER_REG2+i));
    }
    /* Run IEC code... Don't consider R value now.*/
	for (i=0;i<IEC_MAX_NUM;i++)
	{
        /* lock with plc0 for experimental stage. */
        int plc0Order=COM_ReadR(PLC_ORDER_REG);
        COM_WriteR(PLC_ORDER_REG3+i,plc0Order);
		if ((COM_ReadR(PLC_ORDER_REG3+i) == PLC_RUN) || (COM_ReadR(PLC_ORDER_REG3+i) == PLC_SINGLE))
	    {
	    	if (iPLCIsr[MLC_MAX_NUM+i] <= 1)
			{
				if (bPLCCodeInit[MLC_MAX_NUM+i])
				{
				    //printk("[PLCD] run %hd start\n",i);
					PLC_RunLevel1N(MLC_MAX_NUM+i);
				    //printk("[PLCD] run %hd end\n",i);
				}
				iPLCIsr[MLC_MAX_NUM+i] = COM_ReadR(PLC_RUN_ISR_COUNT2+i);
			}
			else
				iPLCIsr[MLC_MAX_NUM+i]--;
			if (COM_ReadR(PLC_ORDER_REG3+i) == PLC_SINGLE)
			    COM_WriteR(PLC_ORDER_REG3+i,PLC_STOP);
	    }
	    if (COM_ReadR(PLC_STATE_REG3+i) != COM_ReadR(PLC_ORDER_REG3+i))
		    COM_WriteR(PLC_STATE_REG3+i,COM_ReadR(PLC_ORDER_REG3+i));
    }
}


void _write_uio_info_to_reg(void)
{
    static short cnt=0;
    short st;
    unsigned long ltmp;
    unsigned long baudrateIdx=0;

    //每百次中斷更新一次
    if( cnt-- <0 )
    {
        cnt=100;

        ltmp = 0;
        for(st=7;st>=0;st--)
        {
            ltmp = ltmp << 1;
            ltmp |= (hwif_DIOInfo(3,2,st)?1:0);
        }
        COM_WriteR( R_UIOINFO_ST_ONLINE, ltmp);

        for(st=0;st<8;st++)
            COM_WriteR( R_UIOINFO_ST_TYPE + st, hwif_DIOInfo(3,3,st) );

        for(st=0;st<8;st++)
            COM_WriteR( R_UIOINFO_ERR_CNT+ st, hwif_DIOInfo(3,4,st)+hwif_DIOInfo(3,5,st)+hwif_DIOInfo(3,6,st) );
        /* Ask for baudrate. */
        baudrateIdx=COM_ReadR(R_UIOINFO_BAUDRATE_IDX);
        /* Stop reading if there is a valid idx */
        if( 0 == baudrateIdx)
        {
            baudrateIdx=hwif_DIOInfo(3,11,0);
            COM_WriteR(R_UIOINFO_BAUDRATE_IDX,baudrateIdx);
        }
    }

}


void _write_uartop_to_reg(void)
{
    static short cnt=0;

    //每297次中斷更新一次(避免為100次的周期)
    if( cnt-- <0 )
    {
        cnt=202;
        //COM_WriteR( R_UARTOP_INFO_EXIST      , hwif_DIOInfo(2,1,0) );    //裝置是否存在
        COM_WriteR( R_UARTOP_INFO_STEP       , hwif_DIOInfo(2,2,0) );    //取得目前階段
        COM_WriteR( R_UARTOP_INFO_TOUT_CNT   , hwif_DIOInfo(2,3,0) );    //各站封包逾時記數
        COM_WriteR( R_UARTOP_INFO_TXERR_CNT  , hwif_DIOInfo(2,4,0) );    //各站傳送暫存器錯誤計數
        COM_WriteR( R_UARTOP_INFO_RXERR_CNT  , hwif_DIOInfo(2,5,0) );    //各站接收暫存器錯誤計數
        COM_WriteR( R_UARTOP_INFO_CRCERR_CNT , hwif_DIOInfo(2,6,0) );    //各站產生CRC錯誤的計數
        COM_WriteR( R_UARTOP_INFO_OK_CNT     , hwif_DIOInfo(2,7,0) );    //正確封包次數
    }
}


void _write_trg_info_to_reg(void)
{
    static short first=1;
    //unsigned long Value=0;

//    printk("bf------hwif_GetInfo(13)--\n");
    if(first)
    {
        first = 0;
        COM_WriteR( R_FPGA_FIRMWARE, hwif_GetInfo(13)); //FPGA firmware版本(for PCC1360S1)
    }

    COM_WriteR( R_TRG_POSCMP_CNT, hwif_GetInfo(HWIF_IF_TRG_INFO_POS_CMP_TRG_CNT));              // Pos Cmp Trigger訊號觸發次數  , R47560
    COM_WriteR( R_TRG_INTVCMP_CNT, hwif_GetInfo(HWIF_IF_TRG_INFO_INTV_CMP_TRG_CNT));            // Intv CMP Trigger訊號觸發次數 , R47561
    COM_WriteR( R_TRG_POSCMP_INBUF, hwif_GetInfo(HWIF_IF_TRG_INFO_POSCMP_IN_BUF));              // Pos Cmp於Buffer中的的命令數量, R47564
    COM_WriteR( R_TRG_INTVCMP_INBUF, hwif_GetInfo(HWIF_IF_TRG_INFO_INTVCMP_IN_BUF));            // Intv Cmp於Buffer中的命令數量 , R47565
    COM_WriteR( R_TRG_POSCMP_BUF_IN_FPGA, hwif_GetInfo(HWIF_IF_TRG_INFO_POSCMP_BUF_IN_FPGA));   // Pos Cmp於FPGA中的可以再寫入命令數量 , R47567
    COM_WriteR( R_TRG_INTVCMP_BUF_IN_FPGA, hwif_GetInfo(HWIF_IF_TRG_INFO_INTVCMP_BUF_IN_FPGA)); // Intv Cmp於FPGA中的可以再寫入命令數量, R47568
//    printk("af------hwif_GetInfo(13)--\n");


    //SoftTrg ( !!!後續修改 )
/*  2019-11-28 dt.wu提出不由com處理這個功能
    Value = COM_ReadR( R_TRG_SOFTTRG );
    if( Value )
    {
        if( Value & 0x1 )
            hwif_SoftTrg(0);
        if( Value & 0x2 )
           hwif_SoftTrg(1);
        COM_WriteR( R_TRG_SOFTTRG, 0 );
    }
*/
}

long *vibSensorInfoPtr=NULL;
void _other_info_to_reg(void)
{
    //static char first_call=0;
    //static short old_onoff=0;
    short onoff=0;
    short vibSensorRtnVal=-1;

    //static long cnt=0;    //Used for debugging
    //主機後面紅色按鈕//
    //P3 version
//    printk("bf------hwif_GetInfo(MCU_VER)--\n");
    COM_WriteR(R_P3_VERSION,hwif_GetInfo(HWIF_IF_MCU_VER));
    /* The definition of 12 has been changed. Need modification if someone commplained but keep it as it is now. 2017/06/28. */
    /* Hwif is not supported now, disable it. 2017/12/15 */
    /* This info is valid from 03.01.04.00.16. 2018/04/03. */
    onoff = hwif_GetInfo( HWIF_IF_RED_BUTTON);
    COM_WriteR_Bit( R_DEFAULT_BTN, 0, onoff);
    //螢幕休眠狀態 (1 == rtn則為開 0==rtn 為未知命令)
    onoff = hwif_GetInfo(HWIF_IF_SCREEN_SAVER);
//    printk("af------hwif_GetInfo(MCU_VER)--\n");
    /* Prevent the rtn would be changed in the future by not using 1==onoff. */
    #if 0
    if(0 == first_call)
    {
        printk("[COM]first monitor saver status:%d\n",onoff);
        first_call=1;

    }
    if( onoff != old_onoff)
    {

        printk("[COM]monitor saver changed from %d to %d\n",old_onoff,onoff);
    }
    #endif
    if(0 != onoff)
    {
        COM_WriteR(R_MONITOR_SAVER_STATUS,1);
    }
    else
    {
        COM_WriteR(R_MONITOR_SAVER_STATUS,0);
    }
    //old_onoff=onoff;
    /* Read the vibration sensor status and values. */
    vibSensorRtnVal=hwif_SVIStatus(&vibSensorInfoPtr);
    #if 0
    if(0 == (cnt % 250))
    {
        if(NULL == vibSensorInfoPtr)
        {
            printk("[COM]vibSensorInfoPtr is NULL\n");
        }
        printk("[COM]vibSensorRtnVal=%d\n",vibSensorRtnVal);
    }
    #endif
    if(0 == vibSensorRtnVal)
    {
        short i=0;
        for(i=0;i<36;i+=4)
        {
            long status=-1;
            if(NULL == vibSensorInfoPtr)
            {
                /* Write the status only to indicate the error. */
                COM_WriteR(R_VIBRATION_SENSOR_INFO_START,0);
                COM_WriteR(R_VIBRATION_SENSOR_INFO_START+4,0);
                COM_WriteR(R_VIBRATION_SENSOR_INFO_START+8,0);
                COM_WriteR(R_VIBRATION_SENSOR_INFO_START+12,0);
                COM_WriteR(R_VIBRATION_SENSOR_INFO_START+16,0);
                COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II,0);
                COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II+4,0);
                COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II+8,0);
                COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II+12,0);
                break;
            }
            status=*(vibSensorInfoPtr+i);
            #if 0
            if(0 == (cnt % 250))
            {
                printk("[COM]i=%d status=%ld\n",i,status);
            }
            #endif
            /* The connection is OK. */
            if(1 == status)
            {
                if(20 > i)
                {
                    /* SV 1000 and the first 4 sensors of ETS 1000. */
                    /* Write the status. */
                    COM_WriteR(R_VIBRATION_SENSOR_INFO_START+i,1);
                    /* Write the reading on x,y,z axis */
                    COM_WriteR(R_VIBRATION_SENSOR_INFO_START+i+1,*(vibSensorInfoPtr+i+1));
                    COM_WriteR(R_VIBRATION_SENSOR_INFO_START+i+2,*(vibSensorInfoPtr+i+2));
                    COM_WriteR(R_VIBRATION_SENSOR_INFO_START+i+3,*(vibSensorInfoPtr+i+3));
                    #if 0
                    if(4 == i && 0==(cnt % 5000))
                    {
                        printk("[COM]ETS sensor 1 x=%ld y=%ld z=%ld\n",*(vibSensorInfoPtr+i+1),*(vibSensorInfoPtr+i+2),*(vibSensorInfoPtr+i+3));
                    }
                    #endif
                }
                else
                {
                    /* The last 4 sensors of ETS 1000. */
                    short offset = i-20;
                    /* Write the status. */
                    COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II+offset,1);
                    /* Write the reading on x,y,z axis */
                    COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II+offset+1,*(vibSensorInfoPtr+i+1));
                    COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II+offset+2,*(vibSensorInfoPtr+i+2));
                    COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II+offset+3,*(vibSensorInfoPtr+i+3));
                }
            }
            else
            {
                if(20 > i)
                {
                    /* Write the status only to indicate the error. */
                    COM_WriteR(R_VIBRATION_SENSOR_INFO_START+i,*(vibSensorInfoPtr+i));
                }
                else
                {
                    /* The last 4 sensors of ETS 1000. */
                    short offset = i-20;
                    /* Write the status only to indicate the error. */
                    COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II+offset,*(vibSensorInfoPtr+i));
                }
            }

        }//for(i=0;i<19;i+=4)

    }//if(0 == vibSensorRtnVal)
    else
    {
        /* Write the status only to indicate the error. */
        COM_WriteR(R_VIBRATION_SENSOR_INFO_START,0);
        COM_WriteR(R_VIBRATION_SENSOR_INFO_START+4,0);
        COM_WriteR(R_VIBRATION_SENSOR_INFO_START+8,0);
        COM_WriteR(R_VIBRATION_SENSOR_INFO_START+12,0);
        COM_WriteR(R_VIBRATION_SENSOR_INFO_START+16,0);
        COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II,0);
        COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II+4,0);
        COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II+8,0);
        COM_WriteR(R_VIBRATION_SENSOR_INFO_START_II+12,0);
    }
    //cnt++;

}

static void _CheckPeriphSetConfigWithRealConfig(void)
{
    short i=0;
    short rtnVal=0;
    short errNum=0;
    char errSt[4]={0};
    char testSt=0;
    /* Chec DI. */
    for(i=0;i<gNetPeriphSetConfig_DI.presentedStationNum;i++)
    {
        testSt=gNetPeriphSetConfig_DI.presentedStation[i];
        /* Check if the station has been reported. */
        if(
            (testSt == errSt[0]) |
            (testSt == errSt[1]) |
            (testSt == errSt[2]) |
            (testSt == errSt[3]) |
            (0 == testSt)               //Don't check station #=0 because this is a illegal station but may be used during testing
        )
        {
            continue;
        }
        rtnVal=hwif_SlotIOStatus((short)testSt,0);
        //printk("[COM]DI station %d rtnVal=%d\n",(short)testSt,rtnVal);
        /* Issue the alarm if the alarm number is not exceeded. */
        if((-1 == rtnVal) | (-8 == rtnVal))
        {
            if(4 > errNum)
            {
                /* Write the dev station information to register. */
                COM_WriteR(COM_NET_PERIPH_SET_CONFIG_ERR_REG_START+errNum,(short)testSt);
                COM_WriteR_Bit(COM_MISC_ALARM1,COM_ALARM_NET_PERIPH_SET_CONFIG_NOT_PRESENT+errNum,1);
                errSt[errNum]=testSt;
                errNum++;

            }
            else
            {
                break;
            }
        }
    }//for loop
    if(4 <= errNum)
    {
        return;
    }
    /* Chec DO. */
    for(i=0;i<gNetPeriphSetConfig_DO.presentedStationNum;i++)
    {
        testSt=gNetPeriphSetConfig_DO.presentedStation[i];
        /* Check if the station has been reported. */
        if(
            (testSt == errSt[0]) |
            (testSt == errSt[1]) |
            (testSt == errSt[2]) |
            (testSt == errSt[3]) |
            (0 == testSt)               //Don't check station #=0 because this is a illegal station but may be used during testing

        )
        {
            continue;
        }

        rtnVal=hwif_SlotIOStatus((short)testSt,0);
        //printk("[COM]DO station %d rtnVal=%d\n",(short)testSt,rtnVal);
        /* Issue the alarm if the alarm number is not exceeded. */
        if((-1 == rtnVal) | (-8 == rtnVal))
        {
            if(4 > errNum)
            {
                /* Write the dev station information to register. */
                COM_WriteR(COM_NET_PERIPH_SET_CONFIG_ERR_REG_START+errNum,(short)testSt);
                COM_WriteR_Bit(COM_MISC_ALARM1,COM_ALARM_NET_PERIPH_SET_CONFIG_NOT_PRESENT+errNum,1);
                errSt[errNum]=testSt;
                errNum++;
            }
            else
            {
                break;
            }
        }
    }//for loop
    if(4 <= errNum)
    {
        return;
    }
    /* Chec AI. */
    for(i=0;i<gNetPeriphSetConfig_AI.presentedStationNum;i++)
    {
        testSt=gNetPeriphSetConfig_AI.presentedStation[i];
        /* Check if the station has been reported. */
        if(
            (testSt == errSt[0]) |
            (testSt == errSt[1]) |
            (testSt == errSt[2]) |
            (testSt == errSt[3]) |
            (0 == testSt)               //Don't check station #=0 because this is a illegal station but may be used during testing
        )
        {
            continue;
        }

        rtnVal=hwif_SlotIOStatus((short)testSt,0);
        //printk("[COM]AI station %d rtnVal=%d\n",(short)testSt,rtnVal);
        /* Issue the alarm if the alarm number is not exceeded. */
        if((-1 == rtnVal) | (-8 == rtnVal))
        {
            if(4 > errNum)
            {
                /* Write the dev station information to register. */
                COM_WriteR(COM_NET_PERIPH_SET_CONFIG_ERR_REG_START+errNum,(short)testSt);
                COM_WriteR_Bit(COM_MISC_ALARM1,COM_ALARM_NET_PERIPH_SET_CONFIG_NOT_PRESENT+errNum,1);
                errSt[errNum]=testSt;
                errNum++;
            }
            else
            {
                break;
            }
        }
    }//for loop
    if(4 <= errNum)
    {
        return;
    }
    /* Chec AO. */
    for(i=0;i<gNetPeriphSetConfig_AO.presentedStationNum;i++)
    {
        testSt=gNetPeriphSetConfig_AO.presentedStation[i];
        /* Check if the station has been reported. */
        if(
            (testSt == errSt[0]) |
            (testSt == errSt[1]) |
            (testSt == errSt[2]) |
            (testSt == errSt[3]) |
            (0 == testSt)               //Don't check station #=0 because this is a illegal station but may be used during testing
        )
        {
            continue;
        }
        rtnVal=hwif_SlotIOStatus((short)testSt,0);
        //printk("[COM]AO station %d rtnVal=%d\n",(short)testSt,rtnVal);
        /* Issue the alarm if the alarm number is not exceeded. */
        if((-1 == rtnVal) | (-8 == rtnVal))
        {
            if(4 > errNum)
            {
                /* Write the dev station information to register. */
                COM_WriteR(COM_NET_PERIPH_SET_CONFIG_ERR_REG_START+errNum,(short)testSt);
                COM_WriteR_Bit(COM_MISC_ALARM1,COM_ALARM_NET_PERIPH_SET_CONFIG_NOT_PRESENT+errNum,1);
                errSt[errNum]=testSt;
                errNum++;
            }
            else
            {
                break;
            }
        }
    }//for loop
    if(4 <= errNum)
    {
        return;
    }
    /* Chec TCI. */
    for(i=0;i<gNetPeriphSetConfig_TCI.presentedStationNum;i++)
    {
        testSt=gNetPeriphSetConfig_TCI.presentedStation[i];
        /* Check if the station has been reported. */
        if(
            (testSt == errSt[0]) |
            (testSt == errSt[1]) |
            (testSt == errSt[2]) |
            (testSt == errSt[3]) |
            (0 == testSt)               //Don't check station #=0 because this is a illegal station but may be used during testing
        )
        {
            continue;
        }
        rtnVal=hwif_SlotIOStatus((short)testSt,0);
        //printk("[COM]TCI station %d rtnVal=%d\n",(short)testSt,rtnVal);
        /* Issue the alarm if the alarm number is not exceeded. */
        if((-1 == rtnVal) | (-8 == rtnVal))
        {
            if(4 > errNum)
            {
                /* Write the dev station information to register. */
                COM_WriteR(COM_NET_PERIPH_SET_CONFIG_ERR_REG_START+errNum,(short)testSt);
                COM_WriteR_Bit(COM_MISC_ALARM1,COM_ALARM_NET_PERIPH_SET_CONFIG_NOT_PRESENT+errNum,1);
                errSt[errNum]=testSt;
                errNum++;
            }
            else
            {
                break;
            }
        }
    }//for loop

}


short _UpdateDIMap(short init )
{
    static int LNum = 0;
    static short state = 0;
    short finish = 0;
    short station=0;
    long setNum=0;
    long temp;

    switch (state)
    {
        case 0:
            if( iomap_update[DI_MAP] || init )
                state = 1;
            break;
        case 1:
            /* Initialize the set config. */
            memset(&gNetPeriphSetConfig_DI,0,sizeof(stNetPeriphSetConfig));
            PLC_ClearDIMap();
            LNum = 0;
            state = 2;
            break;
        case 2:
            while(1)
            {
                setNum=PLC_ReadR(R_DIMAP_ST+LNum);
                /* Check if this is an EhterCAT periph. */
                if(100000 <= setNum)
                {
                    /* This is an EtherCAT device. */
                    temp = setNum % 100000;
                    station = (short)(temp / 1000);
                    if(0 == gNetPeriphSetConfig_DI.presentedStationMask[station])
                    {
                        /* Put into the presented arrary. */
                        /* Mark as being presented. */
                        gNetPeriphSetConfig_DI.presentedStationMask[station]=1;
                        gNetPeriphSetConfig_DI.presentedStation[gNetPeriphSetConfig_DI.presentedStationNum]=station;
                        gNetPeriphSetConfig_DI.presentedStationNum++;
                        //printk("[COM]DI set one station:%d number:%d\n",station,gNetPeriphSetConfig_DI.presentedStationNum);
                    }
                }

                PLC_AddDIMap(LNum, setNum , PLC_ReadR(R_DIMAP_ST+4096+LNum) );
                LNum++;
                if( LNum >= MAX_DIO_QTY )
                {
                    state = 3;
                    break;
                }
                if( 0 ==  (LNum % DIOMAP_UPDATE_INTV) )
                    break;
            }
            break;
        case 3:
            PLC_WriteR_Bit(IOMAP_UPDATE_KERNEL, DI_MAP, 0 );
            state = 0;
            finish = 1;
            /* Check if the set config is consist with real net configuration .*/
            _CheckPeriphSetConfigWithRealConfig();
            break;
    }
    return finish;
}

short _UpdateDOMap( short init)
{
    static int LNum = 0;
    static short state = 0;
    short finish = 0;
    short station=0;
    long setNum=0;
    long temp;

    switch (state)
    {
        case 0:
            //if( PLC_ReadR_Bit(IOMAP_UPDATE_REG, 1 ) )
            if( iomap_update[DO_MAP] || init)
                state = 1;
            break;
        case 1:
            /* Initialize the set config. */
            memset(&gNetPeriphSetConfig_DI,0,sizeof(stNetPeriphSetConfig));
            PLC_ClearDOMap();
            LNum = 0;
            state = 2;
            break;
        case 2:
            while(1)
            {
                setNum=PLC_ReadR(R_DOMAP_ST+LNum);
                /* Check if this is an EhterCAT periph. */
                if(100000 <= setNum)
                {
                    /* This is an EtherCAT device. */
                    temp = setNum % 100000;
                    station = (short)(temp / 1000);
                    if(0 == gNetPeriphSetConfig_DO.presentedStationMask[station])
                    {
                        /* Put into the presented arrary. */
                        /* Mark as being presented. */
                        gNetPeriphSetConfig_DO.presentedStationMask[station]=1;
                        gNetPeriphSetConfig_DO.presentedStation[gNetPeriphSetConfig_DO.presentedStationNum]=station;
                        gNetPeriphSetConfig_DO.presentedStationNum++;
                        //printk("[COM]DO set one station:%d number:%d\n",station,gNetPeriphSetConfig_DO.presentedStationNum);
                    }
                }
                PLC_AddDOMap(LNum, setNum , PLC_ReadR(R_DOMAP_ST+4096+LNum));
                LNum++;
                if( LNum >= MAX_DIO_QTY )
                {
                    state = 3;
                    break;
                }
                if( 0 ==  (LNum % DIOMAP_UPDATE_INTV) )
                    break;
            }
            break;
        case 3:
            PLC_WriteR_Bit(IOMAP_UPDATE_KERNEL, DO_MAP, 0 );
            state = 0;
            finish = 1;
            /* Check if the set config is consist with real net configuration .*/
            _CheckPeriphSetConfigWithRealConfig();
            break;
    }
    return finish;
}

short _UpdateAIMap(short init)
{
    int LNum = 0;
    static short state = 0;
    short finish=0;
    short station=0;
    long setNum=0;
    long temp;

    switch (state)
    {
        case 0:
            if( iomap_update[AI_MAP] || init)
            {
                state = 1;
            }
            break;
        case 1:
            /* Initialize the set config. */
            memset(&gNetPeriphSetConfig_AI,0,sizeof(stNetPeriphSetConfig));
            for (LNum =0; LNum < MAX_AIO_QTY; LNum++)
            {
                setNum=PLC_ReadR(R_AIMAP_ST+LNum);
                /* Check if this is an EhterCAT periph. */
                if(100000 <= setNum)
                {
                    /* This is an EtherCAT device. */
                    temp = setNum % 100000;
                    station = (short)(temp / 1000);
                    if(0 == gNetPeriphSetConfig_AI.presentedStationMask[station])
                    {
                        /* Put into the presented arrary. */
                        /* Mark as being presented. */
                        gNetPeriphSetConfig_AI.presentedStationMask[station]=1;
                        gNetPeriphSetConfig_AI.presentedStation[gNetPeriphSetConfig_AI.presentedStationNum]=station;
                        gNetPeriphSetConfig_AI.presentedStationNum++;
                        //printk("[COM]AI set one station:%d number:%d\n",station,gNetPeriphSetConfig_AI.presentedStationNum);
                    }
                }
                aiomap_Set( &AIMap, LNum, setNum );
            }
            state = 2;
            break;
        case 2:
            PLC_WriteR_Bit(IOMAP_UPDATE_KERNEL, AI_MAP, 0 );
            state = 0;
            finish =1;
            /* Check if the set config is consist with real net configuration .*/
            _CheckPeriphSetConfigWithRealConfig();
            break;
    }
    return finish;
}

short _UpdateAOMap(short init)
{
    int LNum = 0;
    static short state = 0;
    short finish = 0;
    short station=0;
    long setNum=0;
    long temp;

    switch (state)
    {
        case 0:
            //if( PLC_ReadR_Bit(IOMAP_UPDATE_REG, 3 ) )
            if( iomap_update[AO_MAP] || init )
            {
                state = 1;
            }
            break;
        case 1:
            /* Initialize the set config. */
            memset(&gNetPeriphSetConfig_AO,0,sizeof(stNetPeriphSetConfig));
            for (LNum =0; LNum < MAX_AIO_QTY; LNum++)
            {
                setNum=PLC_ReadR(R_AOMAP_ST+LNum);
                /* Check if this is an EhterCAT periph. */
                if(100000 <= setNum)
                {
                    /* This is an EtherCAT device. */
                    temp = setNum % 100000;
                    station = (short)(temp / 1000);
                    if(0 == gNetPeriphSetConfig_AO.presentedStationMask[station])
                    {
                        /* Put into the presented arrary. */
                        /* Mark as being presented. */
                        gNetPeriphSetConfig_AO.presentedStationMask[station]=1;
                        gNetPeriphSetConfig_AO.presentedStation[gNetPeriphSetConfig_AO.presentedStationNum]=station;
                        gNetPeriphSetConfig_AO.presentedStationNum++;
                        //printk("[COM]AO set one station:%d number:%d\n",station,gNetPeriphSetConfig_AO.presentedStationNum);
                    }
                }
                aiomap_Set( &AOMap, LNum, setNum );
            }
            state = 2;
            break;
        case 2:
            PLC_WriteR_Bit(IOMAP_UPDATE_KERNEL, AO_MAP, 0 );
            state = 0;
            finish = 1;
            /* Check if the set config is consist with real net configuration .*/
            _CheckPeriphSetConfigWithRealConfig();
            break;
    }
    return finish;
}

short _UpdateTCIMap(short init)
{
    int LNum = 0;
    static short state = 0;
    short finish = 0;
    short station=0;
    long setNum=0;
    long temp;

    switch (state)
    {
        case 0:
            //if( PLC_ReadR_Bit(IOMAP_UPDATE_REG, 4 ) )
            if( iomap_update[TCI_MAP] || init )
            {
                state = 1;
            }
            break;
        case 1:
            /* Initialize the set config. */
            memset(&gNetPeriphSetConfig_TCI,0,sizeof(stNetPeriphSetConfig));
            for (LNum =0; LNum < MAX_TCI_QTY; LNum++)
            {
                setNum=PLC_ReadR(R_TCIMAP_ST+LNum);
                /* Check if this is an EhterCAT periph. */
                if(100000 <= setNum)
                {
                    /* This is an EtherCAT device. */
                    temp = setNum % 100000;
                    station = (short)(temp / 1000);
                    if(0 == gNetPeriphSetConfig_TCI.presentedStationMask[station])
                    {
                        /* Put into the presented arrary. */
                        /* Mark as being presented. */
                        gNetPeriphSetConfig_TCI.presentedStationMask[station]=1;
                        gNetPeriphSetConfig_TCI.presentedStation[gNetPeriphSetConfig_TCI.presentedStationNum]=station;
                        gNetPeriphSetConfig_TCI.presentedStationNum++;
                        //printk("[COM]TCI set one station:%d number:%d\n",station,gNetPeriphSetConfig_TCI.presentedStationNum);
                    }
                }
                aiomap_Set( &TCIMap, LNum, setNum );
            }
            state = 2;
            break;
        case 2:
            PLC_WriteR_Bit(IOMAP_UPDATE_KERNEL, TCI_MAP, 0 );
            state = 0;
            finish = 1;
            /* Check if the set config is consist with real net configuration .*/
            _CheckPeriphSetConfigWithRealConfig();
            break;
    }
    return finish;
}


void _InitUpdateAIO_DIOMap(void)
{
    //static long t1,t2;
    //t1 = osal_measr_GetTick();
    while( !_UpdateDIMap(1) );
    while( !_UpdateDOMap(1) );
    while( !_UpdateAIMap(1) );
    while( !_UpdateAOMap(1) );
    while( !_UpdateTCIMap(1) );
    //t2 = osal_measr_GetTick();
    //printk("[COM] AIODIOMAP Update all = %ld(us)\n",(long)osal_TickToUs(t2-t1)); //P2主機, 約3ms
}

//硬體輸入監視功能(Real Input Monitor Function)(R204,790~R204,791; R204,800~R204,999)
//R204,790	啟用Real Input Monitor功能(0:關閉, 1:啟用)
//R204,791
//R204,800
//~
//R204,999
//            共200個Register，可記錄200個Real Intpu變化。
//            單筆記錄：
//            Bit31	       |   Bit30~0
//            -------------|-----------------
//            0:Low?蚵i    |   Real Input ID
//            1:Hi?蚯ow	  |
#define R_DIMNT_SW              204790  //啟用Real Input Monitor功能(0:關閉, 1:啟用)//
#define R_DIMNT_EVT_CNT         204791  //填入到Input Event Queue的Envent數量。關閉此功能後，變成0//
#define R_DIMNT_EVT_TBL_ST      204800  //Input Event Queue點//
#define R_DIMNT_EVT_MAX         200     //Input Event Queue buffer size//

void _ReadDIMinitor(void)
{
    static short sw = 0;
    unsigned long event;
    long cnt;

    //ON->OFF, OFF->ON
    if( 1 == PLC_ReadR(R_DIMNT_SW) )
    {
        if( 0 == sw )
        {
            PLC_InD_EnDis(1);
            PLC_WriteR( R_DIMNT_EVT_CNT, 0);
            sw = 1;
        }
    }
    else
    {
        if( 1 == sw )
        {
            PLC_InD_EnDis(0);
            sw = 0;
        }
    }

    if( sw )
    {
        //檢查是否有事件產生，並填入event queue//
        if( 0 == PLC_InD_GetEvent( &event ) )
        {
            cnt = PLC_ReadR( R_DIMNT_EVT_CNT);
            PLC_WriteR( R_DIMNT_EVT_TBL_ST + (cnt % R_DIMNT_EVT_MAX), event );
            PLC_WriteR( R_DIMNT_EVT_CNT, cnt+1 );
        }

        //監控主程式
        PLC_InD_Exec();
    }
}




//R204,792	啟用硬體輸出測試功能 (0:關閉, 1:啟用)
//R205,000
//~
//R205,199	Testing Table。欄位1：硬體輸出編號，共200組
//          (0:No Use, 10000~1xxxx:Remote I/O, 20000~2xxxx:Local I/O, 30000~3xxxx:Reg I/O, 50000~5xxxx:SIOA I/O)
//R205,200
//~
//R205,399	Testing Table。欄位2：該硬體編號輸出狀態， 共200組。
//          0:No Use, 0xF:Force OFF, 0xFF: Force ON)

#define R_DOTEST_SW         204792
#define R_DOTEST_TBL_ST     205000
#define R_DOTEST_TBL_MAX    200

void _DOTesting(void)
{
    static short sw = 0;
    short idx;
    long real_do,state;

    //ON->OFF, OFF->ON
    if( 1 == PLC_ReadR( R_DOTEST_SW ) )
    {
        if( 0 == sw )
        {
            PLC_OutD_EnDis(1);
            sw = 1;

            for(idx=0;idx<R_DOTEST_TBL_MAX;idx++)
            {
                real_do = PLC_ReadR( R_DOTEST_TBL_ST );
                state = PLC_ReadR( R_DOTEST_TBL_ST+R_DOTEST_TBL_MAX );
                if( real_do > 0 && state != 0 )
                    PLC_OutD_SetForceOut( real_do, state );
            }
        }
    }
    else
    {
        if( 1 == sw )
        {
            PLC_OutD_EnDis(0);
            sw = 0;
        }
    }

}

void _ReadRealDIAITCI(void)
{
    int No;
    long Offset;

    //@執行IO Mapping動作(更新mapping時，不執行)
    if( 0 == iomap_update[DI_MAP] )
        PLC_RealReadI();

    if( 0 == iomap_update[AI_MAP] )
    {
        // Analog Input R29100~R29149
        for (No=0;No<MAX_AIO_QTY;No++)
        {
            if ( aiomap_Get( &AIMap, No, &Offset))
            {
                if ( Offset > 0)
                {
                    //!!! 注意單位變成 0.00mV
                    PLC_WriteR(RAI0+No, (long)hwif_AIReadV2(Offset) );
                }
            }
        }
    }
    if(  0 == iomap_update[TCI_MAP] )
    {
        // TCI Input R29200~R29263
        for (No=0;No<MAX_TCI_QTY;No++)
        {
            if ( aiomap_Get( &TCIMap, No, &Offset))
            {
                if ( Offset > 0)
                {
                    PLC_WriteR(RTCI0+No, hwif_GetTCIV2(Offset) );
                }
            }
        }
    }
}

/*!
    \brief 提供給rtdr2讀取TCI
    \param No 要讀取的TCI編號
*/
long comif_ReadTCI(int No)
{
    if(MAX_TCI_QTY> No)
    {
        long Offset;
        aiomap_Get(&TCIMap,No,&Offset);
        if(Offset > 0)
        {
            return(hwif_GetTCIV2(Offset));
        }
        else
        {
            return(0);
        }

    }
    else
    {
        return(0);
    }
}

/*!
    \brief 提供給rtdr2讀取AI
    \param No 要讀取的AI編號
*/
long comif_ReadAI(int No)
{
    if(MAX_AIO_QTY> No)
    {
        long Offset;
        aiomap_Get(&AIMap,No,&Offset);
        if(Offset > 0)
        {
            return(hwif_AIReadV2(Offset));
        }
        else
        {
            return(0);
        }

    }
    else
    {
        return(0);
    }
}


void _WriteRealDOAO(void)
{
    static long OldAOValue[MAX_AIO_QTY];
    static short InitFlag = 0; //force update when first run
    long AOValue = 0;
    int i;
    long Offset;

    if( 0 == iomap_update[DO_MAP] )
        PLC_RealWriteO();

    if( 0 == iomap_update[AO_MAP] )
    {
        for( i=0; i<MAX_AIO_QTY; i++)
        {
            AOValue = PLC_ReadR(RAO0+i);
            if (OldAOValue [i] != AOValue || InitFlag == 0)
            {
                if ( aiomap_Get( &AOMap, i, &Offset) )
                {
                    if ( Offset > 0)
                        hwif_AOWriteV2(Offset, AOValue);
                }
                OldAOValue[i] = AOValue;
            }
        }
        InitFlag = 1;
    }
}

void _GetUpdateEvent(void)
{
    long  update;
    short i;

    update = PLC_ReadR(IOMAP_UPDATE_KERNEL);

    for(i=0;i<5;i++)
    {
        iomap_update[i] = (update & 0x1l)?1:0;
        update = update >> 1;
    }
}

#define R_SIMU_DI   188192
#define R_FORCE_DO  200482
#define UPDATE_INTV  0x40  //需為 MAX_DIO_QTY 可以整除

// 模擬輸入與強制輸出
void _SimuDI_ForceDO(void)
{
    short num;
    static short seg = 0;
    long reg;

    for(num=0;num<UPDATE_INTV;num++)
    {
        reg = PLC_ReadR(R_SIMU_DI+seg*UPDATE_INTV+num);
        PLC_SetSimuDI(seg*UPDATE_INTV+num, (reg!=0), (reg==0xff)?1:0);
    }

    for(num=0;num<UPDATE_INTV;num++)
    {
        reg = PLC_ReadR(R_FORCE_DO+seg*UPDATE_INTV+num);
        PLC_SetForceDO(seg*UPDATE_INTV+num, (reg!=0), (reg==0xff)?1:0);
    }

    seg++;
    if( seg >= MAX_DIO_QTY/UPDATE_INTV )
        seg = 0;
}

static void _time_measure( short p, short type )
{
    if( 2 != COM_ReadR(HWIF_DETECTION_ISR) )
        return;
    tmmsr_T1T2Meas(p,type);
}

static void _time_measure_cal(void)
{
    double run_time[10]={0};
    static double exec_time[10]={0.};
    static short cnt = 0;
    short i;

    kernel_neon_begin();
    if( 2 != COM_ReadR(HWIF_DETECTION_ISR) )    //  R40005
    {
        for(i=0;i<10;i++)
            tmmsr_ClearStat(i);
        return;
    }

//    printk("bf------hwif_GetInfo(ISR_RUN)--\n");
    run_time[0] = hwif_GetInfo(HWIF_IF_ISR_RUN) ;
    run_time[1] = tmmsr_Result(TM_OP1     , TMMSR_INTVAL);
    run_time[2] = tmmsr_Result(TM_INMAP   , TMMSR_INTVAL);
    run_time[3] = tmmsr_Result(TM_PLC     , TMMSR_INTVAL);
    run_time[4] = tmmsr_Result(TM_OP_RUN  , TMMSR_INTVAL);
    run_time[5] = tmmsr_Result(TM_OUTMAP  , TMMSR_INTVAL);
    run_time[6] = tmmsr_Result(TM_IOMAP_UD, TMMSR_INTVAL);
    run_time[7] = tmmsr_Result(TM_OP2     , TMMSR_INTVAL);
    run_time[8] = tmmsr_Result(TM_DIOTEST , TMMSR_INTVAL);
    run_time[9] = tmmsr_Result(TM_RTDR    , TMMSR_INTVAL);

    //即時執行時間
    for(i=0;i<10;i++)
        PLC_WriteR(R_TM_START+i, (long)run_time[i] );   //  R47900


    //執行最大時間
    PLC_WriteR(R_TM_START+10, hwif_GetInfo(HWIF_IF_ISR_RUN_MAX) );
    for(i=1;i<10;i++)
        PLC_WriteR(R_TM_START+10+i, tmmsr_Result(i,TMMSR_STAT_MAX) );

    //CPU消耗(0.1%)每1000次中斷更新一次
    for(i=0;i<10;i++)
        exec_time[i] += run_time[i];
    if( ++cnt >= 1000 )
    {
        cnt = 0;
        for(i=0;i<10;i++)
        {
            PLC_WriteR(R_TM_START+20+i,  (long)(exec_time[i]/hwif_GetInfo(HWIF_IF_ISRTIME)) ); //CPU耗能%(0.1%)
            exec_time[i] = 0;
        }
    }

//    printk("af------hwif_GetInfo(ISR_RUN)--\n");
    kernel_neon_end();

}

//type 1:Read Process, 2:Write Process
void _SvSpecInfoRW( short type )
{
    long sw,value;
    short station;
    int i;
    int dataGroup;          //There are 4 groups.

    for(dataGroup=0; dataGroup < 3; dataGroup++)
    {
        sw = PLC_ReadR( R_SV_SPECRW_SW+dataGroup );
        if( !(sw&0x3) ) continue;

        station = (sw >> 8 ) & 0xff;

        switch( type )
        {
            case 1: //Read
                if( sw & 0x1 )
                {
                    for(i=0;i<100;i++)
                    {
                        hwif_SpecInfoReadLongV2( station, i, &value );
                        /* The 200 offset is to maintain the original station R/data R range mapping. */
                        PLC_WriteR( R_SV_SPECRW_RD+200+i+(dataGroup*200), value);
                    }
                }
                break;
            case 2: //Write
                if( sw & 0x2 )
                {
                    for(i=0;i<100;i++)
                    {
                        /* The 200 offset is to maintain the original station R/data R range mapping. */
                        value = PLC_ReadR( R_SV_SPECRW_WD+200+i+(dataGroup*200) );
                        hwif_SpecInfoWriteLongV2( station, i, value );
                    }
                }
                break;
        }//switch

    }//dataGroup loop
    /* Maintain the mapping of the original case when there is only one station. */
    sw = PLC_ReadR( R_SV_SPECRW_SW+dataGroup );
    if( !(sw&0x3) ) return;
    station = (sw >> 8 ) & 0xff;
    switch( type )
    {
        case 1: //Read
            if( sw & 0x1 )
            {
                for(i=0;i<100;i++)
                {
                    hwif_SpecInfoReadLongV2( station, i, &value );
                        PLC_WriteR( R_SV_SPECRW_RD+i, value);
                }
            }
            break;
        case 2: //Write
            if( sw & 0x2 )
            {
                for(i=0;i<100;i++)
                {
                    /* The 200 offset is to maintain the original station R/data R range mapping. */
                    value = PLC_ReadR( R_SV_SPECRW_WD+i );
                    hwif_SpecInfoWriteLongV2( station, i, value );
                }
            }
            break;
    }//switch

}



/***************************************************************************/
static short COM_ExecuteLevel1(long option)
{
    //static long debugCnt=0;
    static short old_plc_interval_setting=0;
    static short plc_run_interval=0;
    static short plc_boot_delayCnt=0;
    short plc_run_intervalKeep=0;       //for OP actions.
    short read_plc_interval_setting=0;
    short opTypeKeep=ISR_INTERVAL_CTRLD_MODULE_NONE;
    read_plc_interval_setting=gComISRIntervalCtrldModuleSet.interval;
    opTypeKeep=gComISRIntervalCtrldModuleSet.opType;
//    printk("COM_ExecuteLevel1 %d\n",option);
//    kernel_neon_begin();
#if 0
    if(0 == debugCnt)
    {
        printk("[COM]gComISRIntervalCtrldModuleSet.interval=%d opType=%d\n",gComISRIntervalCtrldModuleSet.interval,gComISRIntervalCtrldModuleSet.opType);
        debugCnt++;
    }
#endif
    if(old_plc_interval_setting != read_plc_interval_setting)
    {
        old_plc_interval_setting=read_plc_interval_setting;
        plc_run_interval=old_plc_interval_setting;
    }

	iISRCount++;
    PLC_WriteR( R_ISR_COUNT, iISRCount );
   //執行自訂ISR
   if(NULL != gThirdPartyFuncPtr)
   {
        gThirdPartyFuncPtr();
        /* Don't run lnc routines. */
        return 1;
    }

    //執行自訂Product_Sys
    if(NULL != gProductSYSFuncPtr)
    {
        gProductSYSFuncPtr();
    }
    //執行自訂Product_Dev
    if(NULL != gProductDEVFuncPtr)
    {
        gProductDEVFuncPtr();
    }
    //執行自訂Product_User
    if(NULL != gProductUSERFuncPtr)
    {
        gProductUSERFuncPtr();
    }

    //OP前置作業
_time_measure(TM_OP1,TMMSR_T1);
    /* Keep plc_run_interval for OP actions. */
    plc_run_intervalKeep=plc_run_interval;

    if(ISR_INTERVAL_CTRLD_MODULE_NONE != opTypeKeep)
    {
        /* OP is also controlled. */
        if(ISR_INTERVAL_CTRLD_MODULE_PLCNOP == opTypeKeep)
        {
            /* Run OP in accordance with the setting interval. */
            if(1 >= plc_run_intervalKeep)
            {
                OP_Start();
            }
        }
        else
        {
            /* Only PLC is controlled. */
            OP_Start();
        }

    }
    else
    {
        /* PLC&OP are not in run interval control. */
        OP_Start();
    }
    _time_measure(TM_OP1,TMMSR_T2);

    //※檢查RIO1與RIO2是否正常
    _check_IO_status();

    //Check net peripherals
    _check_NetPeriph_status();

	//產生Mapping Table Update Event
	//comobj_mtu_GenEvent();
	_GetUpdateEvent();

	//讀取所有的Input
_time_measure(TM_INMAP,TMMSR_T1);
    _ReadRealDIAITCI();
    if(NULL != gFuncPtrBetweenInputNPLCRun)
    {
        /* Run assigned function pointer. */
        gFuncPtrBetweenInputNPLCRun();
    }
_time_measure(TM_INMAP,TMMSR_T2);

	//執行Level 1 PLC
_time_measure(TM_PLC,TMMSR_T1);
    /* Op would be effected if the boot delay is adopted, changed to only apply on the plc. */
    if(boot_Delay_Cnt > plc_boot_delayCnt)
    {
        plc_boot_delayCnt++;
        //printk("[COM]plc_boot_delayCnt=%d\n",plc_boot_delayCnt);
        /* Skip the plc execution. */
        goto SKIP_PLC;
    }

    if(ISR_INTERVAL_CTRLD_MODULE_NONE != opTypeKeep)
    {
        if(1 >= plc_run_interval)
        {
            _plc_run();
            plc_run_interval=old_plc_interval_setting;
        }
        else
        {
            plc_run_interval--;
        }
    }
    else
    {
        _plc_run();
    }

SKIP_PLC:
    //_plc_run();
_time_measure(TM_PLC,TMMSR_T2);

	//執行OP
_time_measure(TM_OP_RUN,TMMSR_T1);
    if(ISR_INTERVAL_CTRLD_MODULE_NONE != opTypeKeep)
    {
        if(ISR_INTERVAL_CTRLD_MODULE_PLCNOP == opTypeKeep)
        {
            if(1 >= plc_run_intervalKeep)
            {
                OP_Run();
            }

        }
        else
        {
            OP_Run();
        }

    }
    else
    {
        OP_Run();
    }
    //OP_Run();
_time_measure(TM_OP_RUN,TMMSR_T2);

	//輸出所有的Output
_time_measure(TM_OUTMAP,TMMSR_T1);
	_WriteRealDOAO();
_time_measure(TM_OUTMAP,TMMSR_T2);

    //更新IOMAP/AIOMAP
_time_measure(TM_IOMAP_UD,TMMSR_T1);
    _UpdateDIMap(0);
    _UpdateDOMap(0);
	_UpdateAIMap(0);
	_UpdateAOMap(0);
	_UpdateTCIMap(0);
_time_measure(TM_IOMAP_UD,TMMSR_T2);

_time_measure(TM_DIOTEST,TMMSR_T1);
	//模擬輸入與強制輸出功能
	_SimuDI_ForceDO();

	//Read DI/DO debug功能
	_ReadDIMinitor();
	_DOTesting();
_time_measure(TM_DIOTEST,TMMSR_T2);

	//DataRecord功能
	//comobj_UpdataRecorder();

	//uart io資訊
    _write_uio_info_to_reg();

    //PCC1360S1 TRG功能資訊
    _write_trg_info_to_reg();

    //UART OP資訊更新
    _write_uartop_to_reg();

    //other
    _other_info_to_reg();

	//OP後置作業
_time_measure(TM_OP2,TMMSR_T1);
    if(ISR_INTERVAL_CTRLD_MODULE_NONE != opTypeKeep)
    {
        if(ISR_INTERVAL_CTRLD_MODULE_PLCNOP == opTypeKeep)
        {
            if(1 >= plc_run_intervalKeep)
            {
                OP_End();
            }

        }
        else
        {
            OP_End();
        }

    }
    else
    {
        OP_End();
    }

	//OP_End();
_time_measure(TM_OP2,TMMSR_T2);

    //Spec SvInfo Read/Write
    _SvSpecInfoRW(1);
    _SvSpecInfoRW(2);

	//執行RTDR
_time_measure(TM_RTDR,TMMSR_T1);
    rtdr_Exec();
    rtdr2_Exec();
_time_measure(TM_RTDR,TMMSR_T2);

    //計算時間量測
//    _time_measure_cal();
//    kernel_neon_end();

	return option;
}

char DLL_PREFIX COM_Initial( void )
{
    short i;
    printk("[COM] COM_Initial!\n");
	iISRCount = 0;

	memset(bPLCCodeInit,0,sizeof(bPLCCodeInit));

	COM_WriteR(VERSION_REG,VERSION);

    //初始化PLC執行狀態
	COM_WriteR(PLC_STATE_REG,PLC_STOP);
	for (i=1;i<MLC_MAX_NUM;i++)
	    COM_WriteR(PLC_STATE_REG2+i,PLC_STOP);

    return 1;
}

static long _get_hwif_param(long pr)
{
    return COM_ReadR(40000+pr); //Hwif參數起點為40000
}

static void _set_hwif_param(long pr, long val)
{
    COM_WriteR(40000+pr,val); //Hwif參數起點為40000
}


//為了HWIF能讀取參數值
char DLL_PREFIX COM_Initial2( void )
{
    long    hwifVer=0;
    printk("[COM] Prepare Init HWIF...\n");
	//Init HWIF Module
	if (0 != hwif_Init(_get_hwif_param,_set_hwif_param))
	{
		printk("[COM] HWIF Initial fail\n");
		return 0;
	}
//    printk("bf------hwif_GetInfo(2)--\n");
    hwifVer = hwif_GetInfo(2);
//    printk("af------hwif_GetInfo(2)--\n");
    COM_WriteR(HWIF_VERSION_REG,hwifVer);
    hwifVer = hwifVer%100000000;
    //if(hwifVer < 2070000)
    //    COM_WriteR_Bit(COM_ALARM_BASE, COM_ALARM_HWIF_VER_ERR, 1);
    printk("[COM] HWIF Initial okk\n");

	//Init OP Module
	if (!OP_Init())
	{
		printk("[COM] OP Initial fail\n");
		return 0;
	}
    printk("[COM] OP Initial ok\n");

    //SZ初始化
//    szapp_Init( 50. ); //每50ms執行一次

    // Intialize peripheral list
    memset(&gNetPeriphList,0,sizeof(gNetPeriphList));

	//安裝主要ISR程序
	hwif_MountProc( ISR_MAIN, COM_ExecuteLevel1 );
	return 1;
}

char DLL_PREFIX COM_Initial3( void )
{
    int iEnableISR;
    long intpo_Period=0;
    printk("[COM] COM_Initial3!\n");

	//開機先更新DIO/AIO Map一次
	_InitUpdateAIO_DIOMap();

    // Get the interpolation time.
    intpo_Period=COM_ReadR(HWIF_INTPO_PERIOD);
    // Calculate the int cnt for 100 ms delay.
    //boot_Delay_Cnt=(short)((long)100000/intpo_Period+1);
    boot_Delay_Cnt=(short)divide((long)100000,intpo_Period+1);
    //printk("[COM]boot_Delay=%d intpo_period=%ld\n",boot_Delay_Cnt,intpo_Period);

    //Do RTDR2 initialization.
    rtdr2_Init();
	COM_WriteR(ISR_CMD_REG,0);

	iEnableISR = hwif_IsrRun();
	//啟動DDA中斷
	if ( iEnableISR < 0 )
	{
		COM_WriteR(ISR_CMD_REG,iEnableISR);
		printk("[COM] hwif_IsrRun fail\n");
		return 0;
	}
    COM_ExecuteLevel1(0);
    return 1;
}

/***************************************************************************/
/*!
    @brief  讀取D/I點狀態表的狀態
    @param  No  要讀取D/I點的編號
    @return 目前的D/I點狀態,如果傳回0=Off時\n
            如果傳回不是0的值,表示目前為On
*/
/***************************************************************************/
char DLL_PREFIX COM_ReadI(int No)
{
    return PLC_ReadI(No);
}
/***************************************************************************/
/*!
    @brief  讀取D/O點狀態表的狀態
    @param  No  要讀取D/O點的編號
    @return 目前的D/I點狀態,如果傳回0=Off時\n
            如果傳回不是0的值,表示目前為On
*/
/***************************************************************************/
char DLL_PREFIX COM_ReadO(int No)
{
	//char Enable,State;

	//comobj_GetForceDO(No,&Enable,&State);
	//if (!Enable)
      return PLC_ReadO(No);
    //else
    //  return State;
}

/***************************************************************************/
/*!
    @brief  讀取C點狀態表的狀態
    @param  No  要讀取C點的編號
    @return 目前的D/I點狀態,如果傳回0=Off時\n
            如果傳回不是0的值,表示目前為On
*/
/***************************************************************************/
char DLL_PREFIX COM_ReadC(int No)
{
    return PLC_ReadC(No);
}

/***************************************************************************/
/*!
    @brief  讀取S點狀態表的狀態
    @param  No  要讀取S點的編號
    @return 目前的D/I點狀態,如果傳回0=Off時\n
            如果傳回不是0的值,表示目前為On
*/
/***************************************************************************/
char DLL_PREFIX COM_ReadS(int No)
{
    return PLC_ReadS(No);
}

/***************************************************************************/
/*!
    @brief  讀取A點狀態表的狀態
    @param  No  要讀取A點的編號
    @return 目前的D/I點狀態,如果傳回0=Off時\n
            如果傳回不是0的值,表示目前為On
*/
/***************************************************************************/
char DLL_PREFIX COM_ReadA(int No)
{
    return PLC_ReadA(No);
}

/***************************************************************************/
/*!
    @brief  讀取R Register的值
    @param  No  要讀取R的編號
    @return 目前R Register的值
*/
/***************************************************************************/
int DLL_PREFIX COM_ReadR(int No)
{
    return PLC_ReadR(No);
}



/***************************************************************************/
/*!
    @brief  讀取R Register的值
    @param  No  要讀取R的編號
    @return 目前R Register的值
*/
/***************************************************************************/
char DLL_PREFIX COM_ReadR_Bit(int No,char bit)
{
    return PLC_ReadR_Bit(No,bit);
}


/***************************************************************************/
/*!
    @brief  讀取User R Register的值
    @param  No  要讀取R的編號
    @return 目前User R Register的值
*/
/***************************************************************************/
int DLL_PREFIX COM_ReadUR(int No)
{
    return PLC_ReadUR(No);
}



/***************************************************************************/
/*!
    @brief  讀取User R Register的值
    @param  No  要讀取R的編號
    @return 目前User R Register的值
*/
/***************************************************************************/
char DLL_PREFIX COM_ReadUR_Bit(int No,char bit)
{
    return PLC_ReadUR_Bit(No,bit);
}



/***************************************************************************/
/*!
    @brief  將要輸出的D/O點寫到D/O狀態表裡
    @param  No      要寫出的D/O點編號
    @param  State   要寫出的狀態,如果要設為Off時,寫入0,否則寫入非0值
*/
/***************************************************************************/
void DLL_PREFIX COM_WriteO(int No,char State)
{
    PLC_WriteO(No,State);
}

/***************************************************************************/
/*!
    @brief  寫值到C bit
    @param  No      要寫出的c點編號
    @param  State   要寫出的狀態,如果要設為Off時,寫入0,否則寫入非0值
*/
/***************************************************************************/
void DLL_PREFIX COM_WriteC(int No,char State)
{
    PLC_WriteC(No,State);
}

/***************************************************************************/
/*!
    @brief  寫值到s bit
    @param  No      要寫出的S點編號
    @param  State   要寫出的狀態,如果要設為Off時,寫入0,否則寫入非0值
*/
/***************************************************************************/
void DLL_PREFIX COM_WriteS(int No,char State)
{
    PLC_WriteS(No,State);
}

/***************************************************************************/
/*!
    @brief  寫值到A bit
    @param  No      要寫出的A點編號
    @param  State   要寫出的狀態,如果要設為Off時,寫入0,否則寫入非0值
*/
/***************************************************************************/
void DLL_PREFIX COM_WriteA(int No,char State)
{
    PLC_WriteA(No,State);
}


/***************************************************************************/
/*!
    @brief  寫值到R Register
    @param  No      要寫出的R編號
    @param  State   要寫出的數值
*/
/***************************************************************************/
void DLL_PREFIX COM_WriteR(int No,int State)
{
    PLC_WriteR(No,State);
    //COM_SetRegChange(No,1);  //PLC_WriteR原本就會寫入change旗標
}

//void DLL_PREFIX COM_WriteR_No_Save(int No,int State)
//{
//    PLC_WriteR(No,State);
//    //COM_SetRegChange(No,0); //將change旗標清除
//}

/***************************************************************************/
/*!
    @brief  寫值到User R Register
    @param  No      要寫出的R編號
    @param  State   要寫出的數值
*/
/***************************************************************************/
void DLL_PREFIX COM_WriteUR(int No,int State)
{
    PLC_WriteUR(No,State);
}


/***************************************************************************/
/*!
    @brief  寫值到R Register
    @param  No      要寫出的R編號
    @param  State   要寫出的數值
*/
/***************************************************************************/
void DLL_PREFIX COM_WriteR_Bit(int No,char bit,char State)
{
    PLC_WriteR_Bit(No,bit,State);
    //COM_SetRegChange(No,1);  //預設會設定change旗標
}

/***************************************************************************/
/*!
    @brief  寫值到User R Register
    @param  No      要寫出的R編號
    @param  State   要寫出的數值
*/
/***************************************************************************/
void DLL_PREFIX COM_WriteUR_Bit(int No,char bit,char State)
{
    PLC_WriteUR_Bit(No,bit,State);
}


/***************************************************************************/
/*!
    @brief  從Simu軸卡讀取DO點狀態
    @param  No  編號
    @return 0:Off, 1:On
*/
/***************************************************************************/
char DLL_PREFIX COM_Read_SimuO(int type, int no)
{

    //! @brief     讀取DO(每次單點)(For SimuCard)
    //! @param     type   0:RIO, 1:LIO,2:Uart OP,3:Uart I/O
    //! @param     no
    //! @rtval     1:ON,0:OFF
    return hwif_DORead( type, no );
}

void DLL_PREFIX COM_Write_SimuI(int type, int no,char on_off)
{
    //! @brief     設定DI(每次單點)(For SimuCard)
    //! @param     type   0:RIO, 1:LIO,2:Uart OP,3:Uart I/O
    //! @param     no
    //! @param     1:ON,0:OFF
    hwif_DIWrite( type, no, on_off );
}

/***************************************************************************/
/*!
    @brief  讀入PLC程式碼(機器碼)
    @param  Code    程式碼的位址
    @param  Size    程式碼的大小
    @return 1= 寫入成功
            0=寫入失敗
*/
/***************************************************************************/
char DLL_PREFIX COM_PLCAssignCode(void *Code,int Size,int Offset,char bIsCCode)
{
printk("20241229 COM_PLCAssignCode 2276\n");
	if (PLC_AssignCode(Code,Size,Offset,bIsCCode))
	{
		bPLCCodeInit[0] = 1;
		return 1;
    }
    else
        return 0;
}

char DLL_PREFIX COM_PLCAssignCodeN(void *Code,int Size,int Offset,char bIsCCode,int iNo)
{
	if (PLC_AssignCodeN(Code,Size,Offset,bIsCCode,iNo))
	{
        bPLCCodeInit[iNo] = 1;
        printk("[COM]iNo=%d bPLCCodeInit[iNo]=%d\n",iNo,bPLCCodeInit[iNo]);
		return 1;
    }
    else
        return 0;
}

/***************************************************************************/
/*!
    @brief  設定Level2的程式位址(相對於程式Buffer開頭)
    @param  Pos 程式位址
*/
/***************************************************************************/
void DLL_PREFIX COM_PLCSetLevel2(int Pos)
{
    PLC_SetLevel2(Pos);
}

void DLL_PREFIX COM_PLCSetLevel2N(int Pos,int iNo)
{
    PLC_SetLevel2N(Pos,iNo);
}

/***************************************************************************/
/*!
    @biref  設定Level3的程式位址(相對於程式Buffer開頭)
    @param  Pos 程式位址
*/
/***************************************************************************/
void DLL_PREFIX COM_PLCSetLevel3(int Pos)
{
    PLC_SetLevel3(Pos);
}

void DLL_PREFIX COM_PLCSetLevel3N(int Pos,int iNo)
{
    PLC_SetLevel3N(Pos,iNo);
}

/***************************************************************************/
/*!
    @biref  讀取Timer目前值
    @param  Pos 指定的Timer編號
    @return 目前的值
*/
/***************************************************************************/
int  DLL_PREFIX COM_PLCReadTimer(int Pos)
{
    return PLC_ReadTimer(Pos);
}

/***************************************************************************/
/*!
    @biref  讀取Timer狀態的設定值
    @param  Pos 指定的Timer編號
    @return 目前的值
*/
/***************************************************************************/
int  DLL_PREFIX COM_PLCReadTimerSet(int Pos)
{
    return PLC_ReadTimerSet(Pos);
}

/***************************************************************************/
/*!
    @biref  讀取Timer狀態值
    @param  Pos 指定的Timer編號
    @return 目前的值
*/
/***************************************************************************/
char DLL_PREFIX COM_PLCReadTimerStatus(int Pos)
{
    return PLC_ReadTimerStatus(Pos);
}

/***************************************************************************/
/*!
    @biref  讀取Timer狀態值
    @param  Pos 指定的Timer編號
    @return 目前的值
*/
/***************************************************************************/
char DLL_PREFIX COM_PLCReadTimerOutput(int Pos)
{
    return PLC_ReadTimerOutput(Pos);
}

/***************************************************************************/
/*!
    @biref  讀取Counterh的目前值
    @param  Pos 指定的Counter編號
    @return 目前的值
*/
/***************************************************************************/
int  DLL_PREFIX COM_PLCReadCounter(int Pos)
{
    return PLC_ReadCounter(Pos);
}

/***************************************************************************/
/*!
    @biref  讀取Counter狀態的設定值
    @param  Pos 指定的Counter編號
    @return 目前的值
*/
/***************************************************************************/
int  DLL_PREFIX COM_PLCReadCounterSet(int Pos)
{
    return PLC_ReadCounterSet(Pos);
}

/***************************************************************************/
/*!
    @biref  讀取Counter狀態值
    @param  Pos 指定的Counter編號
    @return 目前的值
*/
/***************************************************************************/
char DLL_PREFIX COM_PLCReadCounterStatus(int Pos)
{
    return PLC_ReadCounterStatus(Pos);
}

/***************************************************************************/
/*!
    @biref  讀取Counter狀態值
    @param  Pos 指定的Counter編號
    @return 目前的值
*/
/***************************************************************************/
char DLL_PREFIX COM_PLCReadCounterOutput(int Pos)
{
    return PLC_ReadCounterOutput(Pos);
}

/***************************************************************************/
/*!
    @biref  寫入Timer值
    @param  Pos     指定的位置
    @param  Value   要寫入的值
*/
/***************************************************************************/
void DLL_PREFIX COM_PLCWriteTimer(int Pos,int Value)
{
    PLC_WriteTimer(Pos,Value);
}


/***************************************************************************/
/*!
    @biref  寫入Counter值
    @param  Pos         指定的位置
    @param  IncValue    增量或是絕對計數值
    @param  Value       要寫入的值
*/
/***************************************************************************/
void DLL_PREFIX COM_PLCWriteCounter(int Pos,int IncValue,int Value)
{
    PLC_WriteCounter(Pos,IncValue,Value);
}

