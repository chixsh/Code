/*****************************************************************************
* NAME      : datauseage.h
*
* FUNCTION  : Declare the DataUseage class and provide interfaces for data 
*             application logic processing.
*
* DATE(ORG) : 
*
* PROJECT   : 
* AUTHER    : xieliang
*
******************************************************************************/
#ifndef  _DATAUSAGE_H_
#define _DATAUSAGE_H_

#include "globalmacros.h"
#include "globalfuction.h"
#include "BinaryTree.h"
#include "ThreadManager.h"
#include "ConfigFile.h"

#include "dbo.h"    // windows 下编译时 打开

#include <stdio.h>
#include <string>
using std::string;
#include <vector>
using std::vector;
#include <map>



/* The infomation of the call bill  number */
struct  NumberInfo
{	                                      //////////////////////////////////////////////////////////////
	int callType;                         //// 1: 副号码做主叫               2: 真实号码呼叫副号码
	char majorNumber[MAX_NUMB_LEN];	      //// 主叫副号码的主号码            被叫副号码的主号码    
	char minorNumber[MAX_NUMB_LEN];       //// 主叫副号码                    被叫副号码
	char originalNumber[MAX_NUMB_LEN];    //// 被叫完整号码(不含接入号和序号)                  主叫号码(不含接入号和序号) 
	char beCalled[MAX_NUMB_LEN];          //// 去掉95096+序号的被叫号码      被叫副号码
	char callerNumberMsg[MAX_NUMB_LEN];   //// (北京)被叫是副号码时,转换后的主叫号 95096 + 序号 + 主叫号码 
};	                                      //////////////////////////////////////////////////////////////

/* The struct of the call bill */
struct CallBill
{
	NumberInfo numberInfo; /* The infomation of the phone number */
	time_t startTime; /* The start time of the call is connected */
	int callContinueTime; /* The time of the call */
	char device_ID[12]; /* The coding of the signal */
	int callInType ;        /* 入局呼叫拨打类型 IVR,CALL_OUT,SECOND_DIAL,CALL_CENTER,VRITUAL_SWITCH,OUT_SERVIC*/
    int result ;            /* 描述此次call是否呼叫成功 -1:失败 ;0:成功*/
	int releaseValue ;      /* 通话连通释放原因*/
};

struct MinorNumberAttr
{
	char minorNumber[MAX_NUMB_LEN]; /* minot number */
	int  sequenceNo;                /* The sequence of the minor number int the minor number group*/
	int  state;                     /* The state of the minor number*/
};

struct BakMinorNumberInfo
{
	string minorNumber; /* 副号码 */
	string sequenceNo;                /* 副号码序号*/
	string type;
	string regTime;
	string logoutTime;
	string logoutChannel;
};

enum ResultDataUsage
{
	E_OPERATOR_SUCCED = 0, /* The operation is sucess */
	E_DATABASE_ERROR , /* The operation or the connecting for the database error */
	E_MAJORNUMBER_NOT_EXESIT, /* The major number is not registered*/
	E_MINORNUMBER_NOT_EXESIT, /* The minor number is not registered */
	E_MINORINDEX_NUMBER_NOT_EXESIT, /* The sequence of the minor number is not exist. */
	E_NUMBER_LENGTH_ERROR , /* The length of the number is not match with the limit in config file. */
	E_IN_LIMITED_TIME_ERROR , /* The call is in the limit time.  */
	E_IN_LIMITEIED_LIST_ERROR, /* The number is in the limited list */
	E_NOT_IN_VOICEBODYLIST , /* The number does not call forward to voice box */
	E_SET_MINORNUMBER_SEQUE_ERROR, /* The sequence is error of the minor number */
	E_MINORNUMBER_ALREADY_REGISTER, /* The minor number has been registered */
	E_IN_FORBID_NUMBER_LIST, /* The number which the user want to call is forbidden. */


	/* The logic state of the minor number */
	STATE_ACTIVE , /* active on */
	STATE_OFFLINE, /* shut down or off line */
	STATE_LOGOUT, /* log out */
	STATE_ARREARAGE, /* The minor number is arrearage*/

	/* Voice message box */
	VOICEBOXSTATE_FALSE, /* There is no setting about the voice box */
	VOICEBOXSTATE_TRUE,  /* The calling of the limit number will call forward to the voice box. */

	VOICEBOXTYPE_ALL ,   /* All the caller for the user will call forward to the voice box. */
	VOICEBOXTYPE_PART,    /* Only the callers limited by the user will call forward to the voice box. */

	E_NOT_HAVEVOICEMSG_ERROR,

	/* The limit of the white list and black list */
	TYPE_NONE ,
	TYPE_BLACK ,
	TYPE_WHITE ,

	E_ADD_RELEASE_ERROR, /* The operation of adding is fail or the operation of release is fail */

	E_UNKNOWN_ERROR, /* Unknow erroe */

    E_CALLER_NOT_EXIST_IN_AREACODE,   /*The caller number is not exist in the area code table*/

	E_MINORNUMBER_SMS_LIMITED, /*The SMS of the minor number is limited*/

	E_DATABASE_NO_RECORD,

	E_BAD_CALL_LIMIT ,/*The called number is not allow all the caller's minor numbers call itself.*/
	//.s Add by whc for database synchronized  --add by whc 20100118
	E_NO_DATA_UNSYNCHRONIZED,
	E_NO_DATA_MATCHED,
	E_NO_FILE_FOUND,
	E_READ_FILE_FAIL,

	E_NUMBER_NOT_EXIST,  /* Checking input number is not exist in the number pool*/
	E_NUMBER_NOT_ACTIVE, /*The input number could not be choose*/
	E_HAS_ENOUGH_NUMBER,  /*The input number could not be choose*/


	//.e Add by whc for database synchronized  --add by whc 20100118
	E_MINOR_SEQ_IS_6,

	// .s added by wj 20120927 
#if 1
	E_VIRTUAL_MINOR_NOT_EXESIT,
#endif

#if 0
	E_OPERATOR_SUCCED_VIRTUAL_MINOR
#endif
	// .e added by wj 20120927 
};

enum TimeLimitedStrategy
{
	TIMELIMITSTRATEGY_SELF =0, /* Self define time limit  */
	TIMELIMITSTRATEGY_ONE ,   /* policy 0ne */
	TIMELIMITSTRATEGY_TWO,   /*  */
	TIMELIMITSTRATEGY_THERE,
	TIMELIMITSTRATEGY_FOUR,
	TIMELIMITSTRATEGY_FIVE,
	TIMELIMITSTRATEGY_SIX,
	TIMELIMITSTRATEGY_SEVEN,
	TIMELIMITSTRATEGY_EIGHT,
	TIMELIMITSTRATEGY_NINE,
	TIMELIMITSTRATEGY_101 = 101,/*The time limit policy about SMS*/
	TIMELIMITSTRATEGY_102,
	TIMELIMITSTRATEGY_103

};
//.s add  for different the IVR and sms time limited type by whc.
enum TimeLimitedType
{
	IVR_LIMITED = 0,  //IVR type
	SMS_LIMITED  
};
//.e add  for different the IVR and sms time limited type by whc.

enum LogicState
{
	LOGIC_ACTIVE = 1, 
	LOGIC_OFFLINE, 
	LOGIC_LOGOUT, 
	LOGIC_ARREARAGE
};

enum OperatorChannelType
{
	OPCH_BUSINESSHALL = 1,
	OPCH_WEB,
	OPCH_10086,
	OPCH_IVR,
	OPCH_SMS
};

enum OperatorActionType
{
	OPAC_LOGIN = 1,
	OPAC_LOGOUT,
	OPAC_OFFLINE,
	OPAC_ACTIVE,
	OPAC_CHANGEMINOR,
	OPAC_RECORD,
	OPAC_TIMELIMIT,
	OPAC_FORBIDCALLIN,
	OPAC_FORBIDCALLOUT
};

//add the paramter to keep the call record  result when through the makeDialRouter process.
enum  CR_Result
{
	CR_CALL_IN=0,       /*Default value For the CR result befor make dial rout process */
	CR_IVR,             /* IVR call*/
	CR_CALL_OUT,        /*out dial */
	CR_SECOND_DIAL,     /* second  dial  process*/
	CR_CALL_CENTER,     /* call custom server center*/
	CR_VRITUAL_SWITCH,  /* virtual switch test*/
	CR_OUT_SERVICE      /* The service is no permmit. Include operation as follow: RESULT_MINOR_IN_LIMIT_TIME_FRAME,
				           RESULT_MINOR_IN_LIMIT_TIME_FRAME_AND_HAS_WORD,RESULT_DIAL_TO_RECORD:*/
};
//.s Add by whc for database synchronized  --add by whc 20100118
/*************************************************************
 *
 *The data struct type is used for database 
 *
 *************************************************************/

//同步标志位
enum IsSynchronized
{
	UNSYNCHRONIZED = 0, //未同步
	SYNCHRONIZING,      //同步中
	SYNCHRONIZED        //已同步
};

//同步类型
enum SynchronDataType
{
	REGISTERUSER = 0, //用户主号码
	MINORNUMBER,      //副号码
	REGISTERUSER_BAK, //注销用户主号码
	MINORNUMBER_BAK   //注销副号码
};

//黑名单信息
struct SyncBlackListInfo
{
	string forbidNumber;
	string customTime;
};

//语音信箱规则信息
struct SyncRecordRuleInfo
{
	string allowNumber;
	string customTime;
};

//时间策略信息
struct SyncTimeRuleInfo
{
	string startTime;
	string endTime;
	int week;
	int strategyID;
	string customTime;  
};

//白名单信息
struct SyncWhiteListInfo
{
	string allowNumber;
	string customTime;
};

//短信通知
struct SyncSMSNotifyInfo
{
	int isAccepted;
	int deliverCount;
	string updateTime;  
};

//来电显示
struct SyncServiceCustomInfo
{
	int isChangeCallerDisplay;
	string customTime;
};

//用户主号码信息
struct SyncUserNumberInfo
{
	string number;
	string IMSI;
	string zoneCode;
	string registeredTime;
	SyncSMSNotifyInfo SMSNotify;
	SyncServiceCustomInfo serviceCustom;
};

//副号码信息
struct SyncMinorNumberInfo
{
	string userNumber;
	string number;
	int type;
	int sequenceNo;
	string registerTime;
	int registerType;
	int stateID;
	string IMSI;
	int recordRule;
	string zoneCode;

	vector<SyncBlackListInfo>  blackList;
	vector<SyncRecordRuleInfo> recordRuleList;  
	vector<SyncTimeRuleInfo> timeRule;
	vector<SyncWhiteListInfo> whiteList;
};

//同步数据结构
struct  SynchronData
{
	SynchronDataType dataType;
	SyncUserNumberInfo userInfo;
	SyncMinorNumberInfo minorInfo;
};
//User register table;
struct UserRegisterInfo
{
	string userNumber;
	string number;
	string IMSI;
	string userName;
	string pwd;
	string zoneCode;
	string registeredTime;
	int synFlag;
	string syncTime;
	int opeChanelId;

};
//The type is used for minor number choosed.
enum MinorNumberState
{
	NUMBER_ABLE = 0,
	NUMBER_USED,
	NUMBER_FREEZE,
	NUMBER_RESERVED,
	NUMBER_TMPUSED
};
//.e  Add by whc for database synchronized  --add by whc 20100118

//CallForward list number struct
struct CallForwardInfo
{
	string calledNumber;
    short int order;
};

//.s  Add by whc for user operation collect  --add by whc 20100530
enum OperateAction
{
  BLACKLIST_SET = 1,       //1:设置黑名单；
  IVRTIMESTRATEGY_SET,   //2:来电时段策略设置；
  ACTIVE_SET,            //3:副号码开机设置；
  OFFLINE_SET,           //4:副号码关机设置；
  VOICEMESSAGEBOX_SET,   //5:呼叫转语音信箱设置；
  MINORNUMBERSEQ_SET ,   //6:副号码序号修改；
  RINGBACKTONE_SET,       //7:回铃音设置；
  SMSTIMESTRATEGY_SET,   //8:短信时段策略设置；
  SMSFILTER_SET,   //9:短信时段策略设置；
  WHITELIST_SET,
  CALLFORWARD_SET
};

enum OperateChannel
{
    CHANNEL_UNKNOW = 0,   //0:默认初始值，通道未定
    SIMM_IVR = 1,         //1:simm ivr
    PLATFORM_SMS,         //2:平台短信
    INTERNAL_WEBSITE,     //3:自服务网站
    COUNTER_SERVICE,      //4:营业厅通道
    CUSTOMSERVICE_SMS,    //5:10086短信
    TEL_OPERATOR,         //6:客服座席
	CUSTOMSERVICE_IVR,    //7: 10086IVR自动台
    MAINTAIN_TOOLS,       //8: 维护工具
    ACCOUNT_REQUIRE,      //9: 帐务请求
    BUSINESS_SYSTEM_EXCHANGE,  //10:业务系统间交互
    FIRST_LEVEL_BOSS,          //11:一级BOSS
    CITY_SPECIAL_INTERFACE,    //12:地市接口(各地市专用)
    SELF_BUSINESS,             //13:自助营业
    EXPEND_CHANEL =20             //20:其他通道
};
//.e  Add by whc for user operation collect  --add by whc 20100530




struct CallRecordInfo
{
	char  calltype[2];
	char  msisdn[32];
	char  submsisdn[32];
	char  startdate[11];
	char  call_duration[12] ;
	char  count[8] ;
	
};


struct SmsRecordInfo
{
	char  calltype[2];
	char  msisdn[32];
	char  submsisdn[32];
	char  startdate[11];
	char  count[8] ;
	
};


//for fujian report 
struct stDHTRegUserInfo
{
		char  msisdn[32];
		char  submsisdn[32];
		char  seq[8];
		char  regtime[32];
		char  zonecode[8];
};

struct stDHTUnRegUserInfo
{
		char  msisdn[32];
		char  submsisdn[32];
		char  seq[8];
		char  regtime[32];
		char  unregtime[32];
		char  zonecode[8];
};

struct stDHTCallRecordInfo
{
		char  seqid[32];
		char  calltype[2];
		char  msisdn[32];
		char  submsisdn[32];
		char  startdate[11];
		char  call_duration[12] ;
		char  count[8] ;
		char  zonecode[8];
		
};

struct stDHTSmsRecordInfo
{
		char  seqid[32];
		char  calltype[2];
		char  msisdn[32];
		char  submsisdn[32];
		char  startdate[11];
		char  count[8] ;
		char  zonecode[8];
		
};

//add for 0594
struct stDHTCallRecordInfo0594
{
		char  seqid[32];
		char  calltype[2];
		char  msisdn[32];
		char  submsisdn[32];
		char  otherpart[32];
		char  startdate[11];
		char  starttime[9];
		char  call_duration[12] ;
		char  zonecode[8];
		
};

struct stDHTSmsRecordInfo0594
{
		char  seqid[32];
		char  calltype[2];
		char  msisdn[32];
		char  submsisdn[32];
		char  otherpart[32];
		char  startdate[11];
		char  starttime[9];
		char  zonecode[8];
		
};

//////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////
class DATALOGIC_EXPROT DataUsage : public ThreadManager
{
public:
	static DataUsage* instance();
	static int initialize(std::string configfile_ ="config.txt");
	static void finish();


	/************************************************************************/
	/*                       Caller logical interface
	/************************************************************************/
    /*Get the area code from the caller number*/
	ResultDataUsage getCallerAreaCode(const string & caller, string & areaCode);

	/************************************************************************/
	/*                       Minor number process interface                 */
	/************************************************************************/
	//Get all the minor numbers according the major number , order by sequence.
	ResultDataUsage getAllMinorNumber(const string & majorNumber, vector<string> & minorNumbers);
	ResultDataUsage getAllMinorNumber(const string & majorNumber, vector<MinorNumberAttr> & minorNumbers);
	ResultDataUsage getAllBacMinorNumber(const string & majorNumber, vector<BakMinorNumberInfo> & minorNumbers);
	// Using the major number and the sequence of the minor number to get the minor number.
	ResultDataUsage getMinorNumber(const string& majorNumber, int index,string& minorNumber);

	//Using the minor number to get the major number and the sequence of the minor number.
	ResultDataUsage getMinorIndex(const string & minorNumber, string & majorNumber,int& index);

	//
	ResultDataUsage getMinorRegInfo(const string& minorNumber, string& type, string& registerChannel, string& registerTime);

	//Check whether the major number has registered
	ResultDataUsage isMajorRegistered (const string & majorNumber);

	// .s added by wj 20120927
	//Check whether the major number has registered whth true minor service.
	ResultDataUsage isTrueMinorRegistered (const string & majorNumber);
	//Check whether the major number has registered whth virtual minor service.
	ResultDataUsage isVirtualMinorRegistered (const string & majorNumber);
	// .e added by wj 20120927

	//Get the default minor number's index according the major number.
	int getDefaultMinorIndex(const string & majorNumber);

	//Get the default minor number according the major number.
	ResultDataUsage getDefaultMinorNumber(const string & majorNumber, string & minorNumber);

	//IMSI TO MinorNumber
	ResultDataUsage GetMinorNumberFromIMSI(const string& IMSI, string & minorNumber,
		                                   int &Seq,
		                                   string& MajorNumber);

	/************************************************************************/
	/*            Minor number logic state interface(active,shutdown)       */
	/************************************************************************/

	//Get the state of the minor number .(active,shutdown and so on?) 
	ResultDataUsage getMinorNumberState(const string& minorNumber);

	//Set the minor number to logic active or logic shutdown state.
	ResultDataUsage setMinorNumberState(const string& minorNumber,ResultDataUsage minorNumberState,OperateChannel inChanelID=CHANNEL_UNKNOW);

	//Check the minor number whether has been registered.
	ResultDataUsage isMinorNumberRegistered(const string& minorNumber);

	//Set the sequence of the minor number.
	ResultDataUsage setMinorIndex(const string& majorNumber,const string& minorIndexStr,OperateChannel inChanelID=CHANNEL_UNKNOW);
	
	/************************************************************************/
	/*                       Time limit policy interface                    */
	/************************************************************************/

	//Check the minor number called whether is limited by the time limit policy. 
	ResultDataUsage isInLimitedTime(const string& minorNumber);

	//Check the SMS of the minor number whether is limit by the time limit policy.
	ResultDataUsage isSMSInLimitedTime(const string& minorNumber);

	//Set the time limit policy
	ResultDataUsage setStrategyID(const string& minorNumber,int strategyID,OperateChannel inChanelID=CHANNEL_UNKNOW);
	//取消时间限制策略
	ResultDataUsage cancelStrategyID(const string& minorNumber,OperateChannel inChanelID=CHANNEL_UNKNOW);	
	//Set the time limit policy of the SMS
	ResultDataUsage setSmTimeStrategy(const string& minorNumber,
		const string& startTime,const string& endTime,int strategyID,OperateChannel inChanelID=CHANNEL_UNKNOW);

	//Get the time limit policy
	ResultDataUsage getStrategyID(const string& minorNumber,TimeLimitedStrategy& timeStrategy,TimeLimitedType serviceType);

	//
	ResultDataUsage getLimitTime(const string& minorNumber, TimeLimitedType serviceType, string& startTime, string& endTime);

	//Define time limit policy only when the limit policy is zero.
	ResultDataUsage setTimeBySelf(const string& minorNumber,const string& startTime,const string& endTime);



	/************************************************************************/
	/*                       White list and black list  interface           */
	/************************************************************************/
	//Get the limit list type of the minor number.
    ResultDataUsage getListType(const string& minorNumber,string& minorNumberId);

	//Check the caller number whether is limited by the white or black list.
	ResultDataUsage isInCalledList(const string& caller,const string& minorNumber);

	////
	ResultDataUsage getBlackList(const string& minorNumber, vector<string>& blackList);
	ResultDataUsage getWhiteList(const string& minorNumber, vector<string>& whiteList);

	//Add new number into black list.
	ResultDataUsage addNumberToBlackList(const string& minorNumber, const string& preAddNumber,OperateChannel inChanelID=CHANNEL_UNKNOW);

	//Add new number into white list .
	ResultDataUsage addNumberToWhiteList(const string& minorNumber, const string& preAddNumber,OperateChannel inChanelID=CHANNEL_UNKNOW);

	//Remove all the number from the black list .
	ResultDataUsage releaseALLBlackList(const string& minorNumber,OperateChannel inChanelID=CHANNEL_UNKNOW);

	//Remove all the number from the white list.
	ResultDataUsage releaseALLWhiteList(const string& minorNumber,OperateChannel inChanelID=CHANNEL_UNKNOW);
	
	//Remove the single pointed number from the black list .
	ResultDataUsage releaseNumberFromBlackList(const string& minorNumber,const string& preReleaseNumber,OperateChannel inChanelID=CHANNEL_UNKNOW);

	//Remove the single pointed number from the white list .
	ResultDataUsage releaseNumberFromWhiteList(const string& minorNumber,const string& preReleaseNumber,OperateChannel inChanelID=CHANNEL_UNKNOW);

	/************************************************************************/
	/*                       Voice message box setting interface            */
	/************************************************************************/
	//
	ResultDataUsage getVoiceList(const std::string& minorNumber, vector<string>& voiceList);

    //Get the state of the voice message box. 
	ResultDataUsage getVoiceBoxState(const std::string& minorNumber);

	//Set the state of the voice message box .
	ResultDataUsage setVoiceBoxState(const std::string& minorNumber,ResultDataUsage voiceBoxState);

	//Get the type of the voice box  [VOICEBOXTYPE_ALL or VOICEBOXTYPE_PART]
	ResultDataUsage getVoiceBoxType(const std::string& minorNumber);

	//Set the type of the voice box when the voice box state is true. [VOICEBOXTYPE_ALL or VOICEBOXTYPE_PART]
	ResultDataUsage setVoiceBoxType(const std::string& minorNumber,ResultDataUsage voiceBoxType,OperateChannel inChanelID=CHANNEL_UNKNOW);

	//Check the caller whether is in the called's voice list.
	ResultDataUsage isInVoiceList(const std::string& caller ,const std::string& called);

	//Add the new number into the called's voice list,so when the caller call the called ,
	//the caller will call forward to the called's voice box.
	ResultDataUsage addNumberToVoiceList(const std::string& minorNumber ,const std::string& number,OperateChannel inChanelID=CHANNEL_UNKNOW);

	//Remove all the limit number from the pointed number's voice list.
	ResultDataUsage releaseAllVoiceNumber(const std::string& minorNumber,OperateChannel inChanelID=CHANNEL_UNKNOW);

	//Remove singale limit number from the pointed number's voice list.
	ResultDataUsage releaseVoiceNumber(const std::string& minorNumber,const std::string& preReleaseNumber,OperateChannel inChanelID=CHANNEL_UNKNOW);

	/************************************************************************/
	/*                      Play the voice message                          */
	/************************************************************************/

	//According the minor number,get the time,the voice message name ,and the auther of it's voice message.
	ResultDataUsage getVoiceMsg(const std::string& minorNumber, std::string& voiceFileName, 
		            std::string& timeVoice,std::string& doVoiceNumber,int isRead = 0);

	//Set the voice message into the called's voice box.
	ResultDataUsage setVoiceMsg(const std::string& minorNumber, const std::string& voiceFileName, 
		            const std::string& doVoiceNumber,int isRead = 0);

	//Set the state of the voice message file to read over.
	//minorNumber：minor number
	//voiceFileName：The voice message file name of the minor number.
	ResultDataUsage setVoiceHasRead(const std::string& minorNumber, const std::string& voiceFileName);
	
	//To get the count of all the voice message and the count of these voices message have not read.
	ResultDataUsage getVoiceCount(const string& minotnumber,int& allVoiceCount,int& hasNotReadVoiceCount);

	ResultDataUsage deleteVoiceFile(const string& minorNumber,const string& voiceFileName);

	//Chake the caller number whether is in the scope of the call limit list .
	ResultDataUsage isInCallerLimitedList(const string& called);
	
	
	/************************************************************************/
	/*     Call bill interface                                              */
	/************************************************************************/
	//Save the call bill.
	ResultDataUsage saveCallRecord(CallBill & record);

	/************************************************************************/
	/* Location infomation update module                                    */
	/************************************************************************/
	/* Load the location information of the minor number*/
	ResultDataUsage loadLocationInfo(std::map<std::string,std::string>&);
	/* Get the minor number updated time*/
	ResultDataUsage GetMinorUpdateTime(std::string&,std::string&);

	/* Set updated time about the minor number */
	ResultDataUsage setUpdateTime(/*std::map<std::string,std::string>&*/std::string& minorNumber);

	ResultDataUsage GetShouldUpdateItem(std::map<std::string,std::string>&,/* MinorNumber AND IMSI */
		                int interVailueTime /* Time  */);

	/************************************************************************/
	/* SMS Platform Interface                                               */
	/************************************************************************/
	/* Add the new minor number */
	ResultDataUsage addNewMinorNumber(const std::string& majorNumber,const std::string& minorNumber,
					const std::string & numIMSI,const std::string &numberType ,OperateChannel inChanelID=CHANNEL_UNKNOW);

	/* Cancel the register state of the minor number */
	ResultDataUsage deleteMinorNumber(const std::string& majorNumber,const std::string& minorNumber);

	/* Set the minor number sequence */
	ResultDataUsage setMinorNumberSequence(const std::string& minorNumber,int numberSequence,OperateChannel inChanelID=CHANNEL_UNKNOW);
	
	/** 
	*  Add the major number,If the number has exist or the operation is sucess return the state of E_OPERATOR_SUCCED .
	*/
	ResultDataUsage addMajorNumber(const std::string& majorNumber,const std::string& user,
		                           const std::string& passwd,const std::string& IMSI,OperateChannel inChanelID=CHANNEL_UNKNOW);

	/*
	 Custom Caller Display
	*/
	ResultDataUsage  customCallerDisplay(const std::string & userNumber, int opt);
	bool isCustomCallerDisplay(const std::string & userNumber);

	//save minor number operator record
	ResultDataUsage saveMinorOperatorRecord(const std::string& minorNumber, OperatorChannelType chtype, OperatorActionType actype);

	//bad call out limit
	ResultDataUsage setBadCallLimit(const std::string& targetNumber, const std::string& minorNumber);
	ResultDataUsage checkBadCallLimit(const std::string& targetNumber, const std::string& minorNumber);

    /************************************************************************/
	/*                        Data Synchronized                             */
	/************************************************************************/
    /************************************************************************************
     *
     * NAME       : getAnUnSynchronizedData 
     *
     * FUNCTION   : To get one olderdata that has no been synchronized from database order by the time. 
     *
     * INPUT      : None
     *
     * OUTPUT     : synData   SynchronData& : The data which has no synchronized.
     *
     * RETURN     : E_OPERATOR_SUCCED: Get the data sucess.
	 *              other result           : The operation is fail .
     *
     ************************************************************************************/ 
     ResultDataUsage getAnUnSynchronizedData(SynchronData& synData);

	/************************************************************************************
     *
     * NAME       : setASynchronizedDataToCS 
     *
     * FUNCTION   : To update one synchronized data for the central database server . 
     *
     * INPUT      : synData   SynchronData& : The data which need to be synchronized.
     *
     * OUTPUT     : None
     *
     * RETURN     : E_OPERATOR_SUCCED: It is sucess to set the data to be synchronized.
	 *              other result     : The operation is fail .
     *
     ************************************************************************************/ 
     ResultDataUsage setASynchronizedDataToCS(const SynchronData& synData);

	/************************************************************************************
     *
     * NAME       : deleteUserFromCS 
     *
     * FUNCTION   : The function is used to delete the inputed major number from the T_RegisterUser
	 *              table of the center database server .At the same time,it is necessary to delete 
	 *              the relation record from the relation tables with the input number record.
	 *
     * INPUT      : number  const string&  : The major number inputed needs to be deleted.
     *
     * OUTPUT     : None
     *
     * RETURN     : E_OPERATOR_SUCCED: It is sucess to set the data to be synchronized.
	 *              other result     : The operation is fail .
     *
     ************************************************************************************/ 
     ResultDataUsage deleteUserFromCS(const string& number);

	/************************************************************************************
     *
     * NAME       : deleteMinorNumberFromCS 
     *
     * FUNCTION   : The function is used to delete the inputed minor number from the T_MinorNumber
	 *              table of the center database server.At the same time ,it is necessary to delete 
	 *              the relation record from the relation tables with the input number record.
     *
     * INPUT      : number  const string&  : The minor number inputed needs to be deleted.
     *
     * OUTPUT     : None
     *
     * RETURN     : E_OPERATOR_SUCCED: It is sucess to set the data to be synchronized.
	 *              other result     : The operation is fail .
     *
     ************************************************************************************/ 
     ResultDataUsage deleteMinorNumberFromCS (const string& number);

	/************************************************************************************
     *
     * NAME       : getIsSynchronized 
     *
     * FUNCTION   : To check the data's status whether has been synchronized.And  output
	 *              the flag value.
     *
     * INPUT      : dataType   SynchronDataType : The type of the data.
	 *              number    const string& : The data is that want to be checked.
     *
     * OUTPUT     :  isSynchronized IsSynchronized&:The flag marks the status whether has
	 *                                             been synchronized
	 *
     * RETURN     : E_OPERATOR_SUCCED: Get the data synchronized flag sucessfully.
	 *              other result     : The operation is fail .
     *
     ************************************************************************************/ 
     ResultDataUsage getIsSynchronized(SynchronDataType dataType, const string& number, IsSynchronized& isSynchronized);

	/************************************************************************************
     *
     * NAME       : setIsSynchronized 
     *
     * FUNCTION   : The function is used to set the synchronized status of the data
	 *              which has set into the database.
     *
     * INPUT      : dataType   SynchronDataType : The type of the data.
	 *              number    const string& : The data is that want to be checked.
     *              isSynchronized IsSynchronized&:The flag marks the status whether has
	 *                                             been synchronized
     * OUTPUT     : None
	 *
     * RETURN     : E_OPERATOR_SUCCED: Get the data synchronized flag sucessfully.
	 *              other result     : The operation is fail .
     *
     ************************************************************************************/ 
     ResultDataUsage setIsSynchronized(SynchronDataType dataType, const string& number, IsSynchronized isSynchronized);

	/************************************************************************************
     *
     * NAME       : backupCSSynchronData 
     *
     * FUNCTION   : The function is to backup the relation synchronized data of the center database table.
     *
     * INPUT      : backupFileName  const string& : The file is used to storage the DB data.
	 *
     * OUTPUT     : None
	 *
     * RETURN     : E_OPERATOR_SUCCED: Get the data synchronized flag sucessfully.
	 *              other result     : The operation is fail .
     *
     ************************************************************************************/ 
     ResultDataUsage backupCSSynchronData(const string& backupFileName);

	/************************************************************************************
     *
     * NAME       : updateFromCSSynchronData 
     *
     * FUNCTION   : The function is to compare relation table datas of center server with local DB.
	 *              and update the local DB according to the center server if there is difference.
     *
     * INPUT      : backupFileName  const string& : The file is used to storage the DB data.
     *
     * OUTPUT     : None
	 *
     * RETURN     : E_OPERATOR_SUCCED: Get the data synchronized flag sucessfully.
	 *              other result     : The operation is fail .
     *
     ************************************************************************************/ 
     ResultDataUsage updateFromCSSynchronData(const string& backupFileName);

	/************************************************************************************
     *
     * NAME       : setSyncFlagToUNSynced
     *
     * FUNCTION   : 
	 *              
     *
     * INPUT      : 
     *
     * OUTPUT     : None
	 *
     * RETURN     : E_OPERATOR_SUCCED: Get the data synchronized flag sucessfully.
	 *              other result     : The operation is fail .
     *
     ************************************************************************************/ 
     ResultDataUsage setSyncFlagToUNSynced(IsSynchronized  sourceFlag,IsSynchronized  targetFlag );

    //.s New add the interface for the minor number process by IVR  whc 20100412
	/************************************************************************************************
     *
     * NAME       : getAreaCode 
     *
     * FUNCTION   : The function is used to get the area code according with the major number from the
     *              database.
     *
     * INPUT      : minorNumber   const string& : The major number.
     *           
     * OUTPUT     : areaCode  string&
     *
     * RETURN     : E_OPERATOR_SUCCED: Get the number areaCode sucess.
     *              E_DATABASE_ERROR : Fail.
     *
     ***********************************************************************************************/
    ResultDataUsage getAreaCode(const string& majorNumber,string& areaCode);
    /************************************************************************************************
     *
     * NAME       : cancelMinorNumberByIVR 
     *
     * FUNCTION   : The function is used to delete the minor number and modify the relation table record by IVR.
     *
     * INPUT      : minorNumber   const string& : The minor number of the user.
     *           
     * OUTPUT     : None
     *
     * RETURN     : E_OPERATOR_SUCCED: Cancel number operation is sucess.
     *              E_DATABASE_ERROR : Fail.
     *
     ***********************************************************************************************/
    ResultDataUsage cancelMinorNumberByIVR(const string& minorNumber);
   /************************************************************************************************
    *
    * NAME       : getRandMinorNumber 
    *
    * FUNCTION   : The function is used to rand to get one minor number from the minor number pool.
    *
    * INPUT      : majorNumber.const string& :The major number that need to add the minor number.
    *           
    * OUTPUT     : minorNumber   string& : The type of the data.
    *
    * RETURN     : E_OPERATOR_SUCCED: Get one rand number fron the number pool
    *              E_DATABASE_ERROR : The database operation is fail.
    *
    ***********************************************************************************************/
    ResultDataUsage getRandMinorNumber(const string& majorNumber,string& minorNumber);
    /************************************************************************************************
    *
    * NAME       : commitRandMinorNumber 
    *
    * FUNCTION   : The function is used to add one number by get one rand number.
    *
    * INPUT      : majorNumber   conststring& : The number of the major number.
    *              minorNumber   const string& : The minor number that want to add.
    *              
    * OUTPUT     : numberIndex   unsigned int& : The index of the minor number which has been add in.
    *
    * RETURN     : E_OPERATOR_SUCCED: Add the rand minor number is sucessfully.
    *              E_DATABASE_ERROR : The database operation is fail.
    *
    ***********************************************************************************************/
    ResultDataUsage commitRandMinorNumber(const string& majorNumber,const string& minorNumber,unsigned int& numberIndex);
    /************************************************************************************************
     *
     * NAME       : addManualMinorNumber 
     *
     * FUNCTION   : The function is used to add one minor number by manual.
     *
     * INPUT      : majorNumber   const string& : The number of the major number.
     *              minorNumber   const string& : The minor number that want to add.
     *              
     * OUTPUT     : numberIndex   unsigned int& : The index of the minor number which has been add in.
     *
     * RETURN     : E_OPERATOR_SUCCED: Add the manual minor number is sucessfully..
     *              E_DATABASE_ERROR : The database operation is fail.
     *
     ***********************************************************************************************/
	 ResultDataUsage addManualMinorNumber(const string& majorNumber,const string& minorNumber,unsigned int& numberIndex);

    /************************************************************************************************
     *
     * NAME       : getMinorNumberOpeCount 
     *
     * FUNCTION   : The function is used to get the total number of user takes action for delete and add minor number
	 *              during one month.
     *
     * INPUT      : majorNumber   const string& : The number of the major number.
     *              
     * OUTPUT     : countNumber   unsigned int& : The count number of the operation fro del and add.
     *
     * RETURN     : E_OPERATOR_SUCCED: Add the manual minor number is sucessfully..
     *              E_DATABASE_ERROR : The database operation is fail.
     *
     ***********************************************************************************************/
	 ResultDataUsage getMinorNumberOpeCount(const string& majorNumber,int& numberIndex);

   /************************************************************************************************
     *
     * NAME       : getNumberFieldByZone 
     *
     * FUNCTION   : The function is used to get 
     *
     * INPUT      : majorNumber   const string& : The number of the major number.
     *              
     * OUTPUT     : countNumber   unsigned int& : The count number of the operation fro del and add.
     *
     * RETURN     : E_OPERATOR_SUCCED: Add the manual minor number is sucessfully..
     *              E_DATABASE_ERROR : The database operation is fail.
     *
     ***********************************************************************************************/
	 ResultDataUsage getNumberFieldByZone(const string& zoneCode, string& firstNum,string& lastNum);

	 ResultDataUsage setRingBackTone(const std::string& minorNumber,const int& ringBackId,OperateChannel inChanelID=CHANNEL_UNKNOW);
	 ResultDataUsage getRingBackTone(const std::string& minorNumber,int& ringBackId);
	 ResultDataUsage cancelRingBackTone(const std::string& minorNumber,OperateChannel inChanelID=CHANNEL_UNKNOW);
//.e New add the interface for the minor number process by IVR  whc 20100412
	 ResultDataUsage getSMSCount(const std::string& majorNumber,unsigned int &number);
	 ResultDataUsage getSMSCount(const std::string& majorNumber,const std::string& msgInfo,unsigned int &number);
	 ResultDataUsage getSMSCount(const std::string& majorNumber,const std::string& calledNumber, const std::string& msgInfo,unsigned int &number);
	 ResultDataUsage saveSMSSummary(const std::string& majorNumber,const std::string& minorNumber,const std::string& calledNumber, 
		                            int & callType,const std::string& msgInfo);
	 ResultDataUsage setSMSStatus(const std::string& majorNumber,const std::string& minorNumber, int & state,int & count,const std::string& reason);
	 ResultDataUsage getSMSStatus(const std::string& majorNumber,const std::string& minorNumber,int & state,int &smscount);
	 ResultDataUsage addFilterDictionary(vector<std::string>& dictionary);
	 ResultDataUsage getFilterDictionary(vector<std::string>& dictionary);

     ResultDataUsage setMinorNumberOpeRecord(const std::string& numberID,OperateAction opeActionID,OperateChannel opeChannelID,
	                                     const std::string& operatorNote,short int& opeResult);
	 void setOpeChannelID(OperateChannel opeChannelID);

     ResultDataUsage getLastCalledRecord(const std::string& minorNumber,std::string& callTime);
	 
	 ResultDataUsage setSMSNotify(const std::string & userNumber, int opt);
	 //副号码作被叫, 短信提醒
	 ResultDataUsage isSMSNotify(const std::string & userNumber, int& deliverCount,int & notifyFlag);
	 ResultDataUsage deliverSMSCountPlus(const std::string & userNumber,const int & countNum);

	 //cancel action
	 ResultDataUsage setUndesirableNumberList(std::string majorNumber);
//.s Add the function for the call forward list setting by whc 20101116.
   /************************************************************************************************
     *
     * NAME       : getCallForwardList 
     *
     * FUNCTION   : The function is used to get call forward number list with the minor numberID.
     *
     * INPUT      : minorNumber   const string& : The number of the major number.
     *              
     * OUTPUT     : callForwardNumber   string& : The call forward number by the user for the minor number.
	 *                                 if its value is empty indicate that there is no set for calling forward.
     *
     * RETURN     : E_OPERATOR_SUCCED: Get call forward number is sucessfully..
     *              E_DATABASE_ERROR : The database operation is fail.
     *
     ***********************************************************************************************/
	 ResultDataUsage getCallForwardNumber(const std::string& minorNumber,vector<CallForwardInfo>& callForwardNumber);

   /************************************************************************************************
     *
     * NAME       : setCallForwardNumber 
     *
     * FUNCTION   : The function is used to set call forward number list with the minor numberID.
	 *              you can set one number as the call forward number when the first called is no answer.
     *
     * INPUT      : minorNumber   const string& : The number of the major number.
     *              
     * OUTPUT     : callForwardNumber   string& : The call forward number by the user for the minor number.
	 *                                 if its value is empty indicate that there is no set for calling forward.
     *
     * RETURN     : E_OPERATOR_SUCCED: Set call forward number is sucessfully..
     *              E_DATABASE_ERROR : The database operation is fail.
     *
     ***********************************************************************************************/
	 ResultDataUsage setCallForwardNumber(const std::string& minorNumber,CallForwardInfo & info,OperateChannel inChanelID=CHANNEL_UNKNOW);

   /************************************************************************************************
     *
     * NAME       : cancelCallForwardNumber 
     *
     * FUNCTION   : The function is used to cancel call forward number list with the minor numberID.
	 *              To delete the call forward number if the user has set.
     *
     * INPUT      : minorNumber   const string& : The number of the major number.
     *              
     * OUTPUT     : None
     *
     * RETURN     : E_OPERATOR_SUCCED: To cancel call forward number is sucessfully..
     *              E_DATABASE_ERROR : The database operation is fail.
     *
     ***********************************************************************************************/
	 ResultDataUsage cancelCallForwardNumber(const std::string& minorNumber,OperateChannel inChanelID=CHANNEL_UNKNOW);
//.e Add the function for the call forward list setting by whc 20101116.
	ResultDataUsage deleteCallForwardNumber(const std::string& minorNumber,const string & number,OperateChannel inChanelID=CHANNEL_UNKNOW);


	ResultDataUsage saveSMSRecord(const string& callType, const string& msIsdn, const string& subIsdn, const string& otherPart);

	ResultDataUsage saveUserInputInfo(const string& userNumber, const string& inputInfo, const string& result, OperateChannel inChanelID=CHANNEL_UNKNOW);




	//add for fujian 
	ResultDataUsage getOpenSMSminorIMSI(std::map<std::string,std::string> & mapInfo );
	ResultDataUsage getOpenSMSminor(std::vector<std::string> & vecMinorInfo );
	ResultDataUsage getMinorNumberZoneInfo(std::map<std::string,std::string> & mapInfo );
	ResultDataUsage getBakMinorNumberZoneInfo(std::map<std::string,std::string> & mapInfo );
	ResultDataUsage setMinorNumberZoneInfo2Table(std::map<std::string,std::string> & mapInfo);	
	int  InsertDialyCallRecord(std::vector<CallRecordInfo *> & vecCallRecord);
	int  getDialyCallRecord(const string & strDay , std::vector<CallRecordInfo *> & vecCallRecord);
	int  InsertDialySmsRecord(std::vector<SmsRecordInfo *> & vecSmsRecord);
	int  getDialySmsRecord(const string & strDay , std::vector<SmsRecordInfo *> & vecSmsRecord);
	int  getMinorAndBakInfoByDay(const string & strDay );
	
	//dor xiamen2fujian
	int  InsertXiamenUser2Fj(const string & strMajorNum,const string & strMinorNum,const string &strRegTime ,const string &strZone,const string &strState);
	std::map<std::string,std::string>			 m_mapMinorZoneInfo ;
		
	//for fujian report 
	int  getDHTAllRegUser(std::vector<stDHTRegUserInfo *> &vecRegUser);
	int  getDHTRegUserBakByDay(const string & strDay ,std::vector<stDHTUnRegUserInfo *> &vecUnRegUser);
	int  getDHTCallRecordByDay(const string & strDay ,std::vector<stDHTCallRecordInfo *> &vecCallRecord);
	int  getDHTSMSRecordByDay(const string & strDay  ,std::vector<stDHTSmsRecordInfo *> &vecSmsRecord);
	
	
	//add for zone 0594
	ResultDataUsage getMinorNumberZoneInfo0594(std::map<std::string,std::string> & mapInfo );
	ResultDataUsage getBakMinorNumberZoneInfo0594(std::map<std::string,std::string> & mapInfo );
		
	int  getDHTSMSRecordAfterDay0594(const string & strDay  ,std::vector<stDHTSmsRecordInfo0594 *> &vecSmsRecord);
	int  getDHTCallRecordAfterDay0594(const string & strDay ,std::vector<stDHTCallRecordInfo0594 *> &vecCallRecord);
	
	//thread fuction
	virtual int svc();

protected:
	typedef struct DomainEntry
	{
		std::string	areacode; 
	} ROMAIN_ENTRY;

	typedef  std::map<unsigned int , ROMAIN_ENTRY> NUMBER_DOMAIN_TABLE;

private:
	DataUsage();
	~DataUsage();
	DataUsage(const DataUsage&);
	DataUsage& operator=(const DataUsage&);

	bool init(std::string configfile_ ="config.txt");

	ResultDataUsage getNumberID(const std::string& minorNumber,std::string& numberID);
	ResultDataUsage getUserID(const std::string& majorNumber,std::string& userID);
	ResultDataUsage getUserIDbyNumberID(const std::string& minorNumberID, std::string& userID);
	ResultDataUsage getNumberIDbyUserID(const std::string& userID, vector<string> & minorNumberIDs);
	ResultDataUsage getBacUserID(const std::string& majorNumber,std::string& userID);

	inline OperateChannel confirmChannel(OperateChannel confirmID)
	{
		return confirmID==CHANNEL_UNKNOW ? channelID_ : confirmID;
	}

	//.s To add the define of the center database Handler  --add by whc 20100115
	ResultDataUsage getSyncMinorNumInfo(SyncMinorNumberInfo & minorInfo, IsSynchronized syncFlag, SynchronDataType  dataType,std::string & syncTime);
	ResultDataUsage getSyncMajorNumInfo(SyncUserNumberInfo & userInfo, IsSynchronized syncFlag, SynchronDataType  dataType,std::string & syncTime);
	ResultDataUsage getSMSNotifyInfo(SyncSMSNotifyInfo & smsNotifyInfo,std::string & userId);
	ResultDataUsage getCustomServiceInfo(SyncServiceCustomInfo & customServiceInfo,std::string & userId);
	ResultDataUsage getBlackListInfo(vector<SyncBlackListInfo>  & blackList,std::string & numberId);
	ResultDataUsage getWhiteListInfo(vector<SyncWhiteListInfo>  & whiteList,std::string & numberId);
	ResultDataUsage getRuleRecordInfo( vector<SyncRecordRuleInfo>  & ruleRecordInfo,std::string & numberId);
    ResultDataUsage getRuleTimeInfo( vector<SyncTimeRuleInfo>  & ruleTimeInfo,std::string & numberId);
	//To sync minor number infomation
	
	ResultDataUsage getCSDataInfo(FILE * fd);

	ResultDataUsage syncMajorNumToCS(const SyncUserNumberInfo userInfo,std::string & userID);
	ResultDataUsage updateMajorInfo(vector<string> &majorInfo,vector<string> &SMSInfo,vector<string>& customServiceInfo);

	ResultDataUsage updateSMSNotifyInfo(const SyncSMSNotifyInfo &SMSNotifyInfo,std::string & userID);
	ResultDataUsage updateCustomServiceInfo(const SyncServiceCustomInfo & customServiceInfo,std::string & userID);

	ResultDataUsage updateMinorInfo(SyncMinorNumberInfo minorInfo,std::string & userId);

	ResultDataUsage updateBlackListInfo(vector<SyncBlackListInfo> &blackListInfo, std::string & numberId);
	ResultDataUsage updateWhiteListInfo(vector<SyncWhiteListInfo> &whiteListInfo, std::string & numberId);

	ResultDataUsage updateRuleRecordInfo(vector<SyncRecordRuleInfo> &ruleRecordInfo, std::string & numberId);
    ResultDataUsage updateRuleTimeInfo(vector<SyncTimeRuleInfo> &ruleTimeInfoInfo, std::string & numberId);

	ResultDataUsage filterMajorNumInfo(DB_RECORDSET_ARRAY::iterator &it, std::map<std::string,std::string> & mapUserInfo,vector<std::string> &vecUserInfo );
	ResultDataUsage filterMinorNumInfo(DB_RECORDSET_ARRAY::iterator &it, std::map<std::string,std::string> & mapMinorInfo,vector<std::string> &vecMinorInfo);
	ResultDataUsage writeInfoToFile(FILE *fd,vector<std::string>& dataInfo);
	string  intToStr(int & valueInt);
    ResultDataUsage isNumberExist(std::string & sql);
	ResultDataUsage deleteNoCompData();
	ResultDataUsage deleteMinorNumberFromDB(const string& number);
	ResultDataUsage deleteUserFromDB(const string& number);
	ResultDataUsage setUnSynchronized(SynchronDataType dataType, const string& number);

    ResultDataUsage getDBFieldValue(string& sqlCotent,string& fieldName,string& fieldValue);
	ResultDataUsage setMinorNumPoolState( const string& poolID,MinorNumberState& state);
	ResultDataUsage saveChooseMinorNumber(  const string& majorNumber,const string& minorNumber);
	ResultDataUsage deleteChooseTmpMinorNumber(  const string& majorNumber,const string& minorNumber);
	ResultDataUsage addVirtualMinorNumber(const string& majorNumber,const string& minorNumber,unsigned int& numberIndex,string& areaCode,
		                                   OperateChannel inChanelID=CHANNEL_UNKNOW);
	ResultDataUsage getPoolIDByNumber(const std::string& minorNumber,std::string&  poolID);
	//.e To add the define of the center database Handler  --add by whc 20100115

	/*Load number domain table*/
	int LoadNumberDomain();


	//add for fujian mobile client
	//0表示符号状态正常,1表示注销中  -1表示出错
public:	
	int  getMinorDelayData(const string& majorNumber ,const string & minorNumber);
	int  getSMSLimit106Number(const string & calledNumber);
	
	

private:
	static DataUsage* datausage_;

	ConfigFile* configfile_;

	dbo_Mysql* dbmysql_;
	std::string dbname_;

	MyMutex myMutex_;
    
	struct mapValueInfo
	{
		std::string  strValue;
		unsigned int flag;
	};
	////////////////////////////
	//add for forbidden number
	dbo_Mysql dbMysql_fb_;

	std::map<int,BTree*> limitCalledNumberRecord_;

	//add for domain
	NUMBER_DOMAIN_TABLE  numberDomainTable_;
	string localZoneCode_;
	vector<string> subZoneCodeVec_;
	string zoneCodeStr_ ;
	//.s add for update database data time by whc 2010-02-08.
	vector<int> virtualNumLength_;
	string syncActionTime_;
	OperateChannel channelID_;
	//.e add for update database data time by whc 2010-02-08.
	//.s add for update database data time by whc 2010-05-25.
public:
	int maxSMSNumSend_ ;//The user send sms total number limited by day
	int maxSameSMSNumToSameTarget_;//The user send sms number to the single target with same number.
	int maxSameSMSNumToTarget_;
	int sendPeriodToTarget_ ; //The time wide of sms sending to multi target from now .
	int sendPeriodToSameTarget_ ; //The time wide of sms sending to single target from now. 
	//.e add for update database data time by whc 2010-05-25
};

#endif/* end of _DBMANAGER_H_ */


