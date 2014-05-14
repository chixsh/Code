/* SIMMApp.cpp
*
*Copyright 2009-2010 Gelinhaohai Corporation. All Rights Reserved. You may not 
*reproduce this document in whole or in part without permission in writing from 
*Gelinhaohai Corporation at the address provided below.
*/

/* ------------------------- Include Files -----------------------*/
#include "SKC_API.h"         /* EXS SwitchKit include file */
#include "SIMM.h"            /* Function and Structure Declaration */
#include "GetArg.h"          /* Command line argument class */
#include "LogFileManager.h"  /* Log File management class*/
#include "Router.h"
#include "Common.h"
#include "SignalingParameters.h"

#include <time.h>
#include <signal.h>

#ifndef WIN32
#include <sys/stat.h>
#endif

#define ENABLE_CALLCENTER 

#ifdef ENABLE_CALLCENTER
#define CALLCENTER_GROUP "callcenter"
#endif

#ifdef SK_UNIX
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#include <process.h>
#include <winsock.h>
#endif

#include <string>
#include <vector>
using namespace std;

#include "ConfigFile.h"		 /* Config File management class*/
#include "datausage.h"
#include "globalmacros.h"
#include "DialNotifyLibWrapper.h"
#include "switcherclient.h"


/*---------------------------------------------------------------*/
const char SIMMApp::_sequenceTable[15][6] = { {"1\0"},  {"2\0"},  {"3\0"},  {"12\0"}, {"21\0"},
{"13\0"}, {"31\0"}, {"23\0"}, {"32\0"}, {"123\0"},
{"132\0"},{"213\0"},{"231\0"},{"312\0"},{"321\0"} };

const char SIMMApp::_testNumber[5][15] = { {"13717571381\0"},{"13910058921\0"},{"15911138375\0"},{"15011076956\0"},{"\0"} };

char SIMMApp::_callcenterNbr[32] = {"\0"};

// .s added by wj 20121113
char SIMMApp::_accessCode[32] = {"\0"};
// .e added by wj 20121113

int SIMMApp::_recordFileID = 0x300000;
int SIMMApp::_playFileID = 0x100001;
int SIMMApp::_UserTimerID = 0;
int SIMMApp::_callId = 0;

const int MAX_RETRY_COUNT = 3;
const int MAX_CRC_PLAY_COUNT = 2;
const float CRC_PLAY_INTERVAL = 10.0;
const float WAIT_SAM_INTERVAL = 2.0;
const int WAIT_REMOTE_INTERVAL = 30;
bool g_isPrintProcess = true ;

std::string g_strSwitcherIP = "127.0.0.1";
std::string g_strSwitcherPort = "9876";
int  g_iSwitcherID = 10000 ;
void *   g_pHand = NULL ;

int main(int argc, char *argv[])
{
	SIMMApp application;

	char *buffer;
	void *data;
	char param[10];
	int size;

	SKC_Message *msg;
	int ret;

	// Set the file name, then open up the log file.
#ifdef WIN32
	CreateDirectoryA("./log/", NULL);
#else
	mkdir("./log/", 0700);
#endif
	LogFileManager::initialize("./log/SIMMApp.log");

	// Process the command line arguments
	if( !( application.processCommandLine(argc, argv) ) )
	{
		application.printUsage();
		return(-1);
	}

	// Read configure from config file
	if( application.readConf() != true )
	{
		return (-1);
	}

	// Initialize connection to llc. 
	ret = sk_initializeConnection(getpid());
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief,
			"ERROR: sk_initializeConnection() returned %s",
			sk_statusText(ret));
		return (-1);
	}

	// Dynamic loading Business Library
	if( application.loadDataLoicLib() != true )
	{
		return -1 ;
	}

	//start Short message Notify module
	application.initDialNotifyEnv();

	//Configure Application Redundant
	int applicationID = sk_getConnectionName();
	memset(param, 0, sizeof(param));
	sprintf(param, "%d",  applicationID);
	sk_registerAsRedundantApp(applicationID, SIMM_RAPP_ID, 5, strlen(param), (const UBYTE *)param, 0, 0 );

	// Setup a signal handler to be run when user hits ctrl-c
	signal(SIGINT, SIMMApp::shutdown);

	LogFileManager::getInstance()->write(Verbose, "Waiting for a message ...");

	/* Continuously wait for asynchronous messages and dispatch to the
	* appropriate handler functions, or, if there are none, report the
	* unhandled message 
	*/
	while(1) 
	{
		/* Wait up to 600 seconds for a message.  If a message is received
		* that has handler function(s), rcvAndDispatch will
		* automatically call the handler function(s) and return the
		* result of the last handler function called.  If there are no
		* handler functions, then SK_NOT_HANDLED is returned, and the
		* message must be handled here.
		*/
		ret = sk_rcvAndDispatchAutoStorage(&buffer, &size, 600, &data);
		if (ret == SK_NOT_HANDLED) 
		{
			ret = skc_unpackMessage(buffer, size, &msg);
			if(ret != OK)
			{
				LogFileManager::getInstance()->write(Brief, 
					"skc_unpackMessage Failed!");				
				continue;
			}

			SKC_MSG_SWITCH(msg)
			{
				// First message sent to every SwitchKit application 
				CASEC_ConnectionStatusMsg(connectionStatus)
				{
					LogFileManager::getInstance()->write(Brief, 
						"Connection Status Message Received: IP Address %s",
						connectionStatus->getIPAddress());
					break;
				}
				CASEC_RedundantAppStatusMsg(redStatus)
				{
					//specified application has been removed from the redundant application.
					if( redStatus->getRedundancyStatus() == SK_RED_STATUS_REMOVED)
					{
						LogFileManager::getInstance()->write(Brief, 
							"Application (%d) has been removed from %s group",
							redStatus->getAppName(), redStatus->getRedundantAppPoolID());
					}
					//The specified application is the primary app for the redundant app group.
					else if( redStatus->getRedundancyStatus() == SK_RED_STATUS_PRIMARY )
					{
						LogFileManager::getInstance()->write(Brief, 
							"Application (%d) Setup,It is the primary of %s group",
							redStatus->getAppName(), redStatus->getRedundantAppPoolID());

						/* Specify inbound group of channels to wait for calls on */
						if( redStatus->getAppName() == getpid() )
						{
							application.setupChannelGroups();
						}
					}
					//specified application is a secondary application for the redundant application group.
					else if( redStatus->getRedundancyStatus() == SK_RED_STATUS_SECONDARY )
					{
						LogFileManager::getInstance()->write(Brief, 
							"Application (%d) Setup It is the secondary of %s group",
							redStatus->getAppName(), redStatus->getRedundantAppPoolID());

						if( redStatus->getAppName() == getpid() )
							application.ignoreChannelGroups();
						{
							/* change this app to primary */
#ifdef PRI_APP
							application.changeAppToPrimary();
#endif
						}
					}
					//There is no primary application for the redundant app group
					else if( redStatus->getRedundancyStatus() == SK_RED_STATUS_NO_PRIMARY )
					{
						LogFileManager::getInstance()->write(Brief, 
							"There is no primary in %s group",redStatus->getRedundantAppPoolID());
					}
					//The specified application is a monitoring application of the redundant application group.
					else if( redStatus->getRedundancyStatus() == SK_RED_STATUS_MONITOR )
					{
						LogFileManager::getInstance()->write(Brief, 
							"Application (%d) Setup It is the monitoring of %s group",redStatus->getRedundantAppPoolID());
					}

					break ;
				}
				CASEC_RegisterAsRedundantAppAck(rRegAck)
				{
					if( rRegAck->getStatus() != OK)
					{
						char logText[512];
						memset(logText, 0, sizeof(logText));
						sprintf(logText, "RegisterAsRedundantAppAck: Status=%x, AppName=%d, RedundantAppPoolID=%s, RedundantAppPriority=%d",
							rRegAck->getStatus(), rRegAck->getAppName(), rRegAck->getRedundantAppPoolID(),rRegAck->getRedundantAppPriority());

						LogFileManager::getInstance()->write(Brief, 
							"%s", logText);
					}
					break;
				}
				CASEC_ChannelReleasedWithData(crwd)
				{
					/* If a channel is releasedWithData, Print the Call details and
					* call the returnChannels().
					*/
					LogFileManager::getInstance()->write(Brief, 
						"INFO: ChannelReleasedWithData In Channel(0x%02x:0x%02x)", 
						crwd->getSpan(), crwd->getChannel());

					break;
				}

				//------------------------------------
				CASEC_RFSWithData(rfsd)
				{
					LogFileManager::getInstance()->write(Brief, 
						"INFO: RFSWithData In Channel(0x%02x:0x%02x)", 
						rfsd->getSpan(), rfsd->getChannel()); 

					application.processRFSWithData(rfsd);
					break;
				}
				//------------------------------------



				CASEC_default
				{
					LogFileManager::getInstance()->write(Brief, 
						"Received unhandled message, %s", msg->getMsgName());
				}
			}SKC_END_SWITCH;
		} /* (ret == SK_NOT_HANDLED) */
		else if (ret != OK)
		{
			LogFileManager::getInstance()->write (Brief, 
				"rcvAndDispatchAutoStorage returned %s",sk_errorText(ret));
		}
	}/* while (1) */

	return(0);

} /* main */

void SIMMApp::setupChannelGroups()
{
	int ret;

	// Specify inbound group of channels to wait for calls on 
	ret = sk_watchChannelGroup(getInboundChannelGroup());
	if ( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: watchChannelGroup returned %s\n",sk_errorText(ret));
	}

	/* Specify function to be called when a message is initially received on
	* a channel in this group 
	*/    
	ret = sk_setGroupHandler(getInboundChannelGroup(), (void *)this, inboundCallHandler);
	if ( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: setGroupHandler returned %s\n",
			sk_errorText(ret));
	}

#ifdef ENABLE_CALLCENTER	
	// watch callcenter group of channels to wait for calls on 
	ret = sk_watchChannelGroup(CALLCENTER_GROUP);
	if ( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: watchChannelGroup returned %s\n",sk_errorText(ret));
	}

	/* Specify function to be called when a message is initially received on
	* a channel in this group 
	*/    
	ret = sk_setGroupHandler(CALLCENTER_GROUP, (void *)this, inboundCallHandler);
	if ( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: setGroupHandler returned %s\n",
			sk_errorText(ret));
	}
#endif

}

void SIMMApp::ignoreChannelGroups()
{
	int ret;

	ret = sk_ignoreChannelGroup(getInboundChannelGroup());
	if ( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: sk_ignoreChannelGroup returned %s\n",sk_errorText(ret));
	}
}

int SIMMApp::changeAppToPrimary()
{
	int ret;
	SK_ReselectPrimaryApp rReselect;

	sk_initMsg( &rReselect, TAG_ReselectPrimaryApp );

	sprintf( rReselect.RedundantAppPoolID, SIMM_RAPP_ID );
	rReselect.Flag = SK_RED_RESELECT_PRIMARY;

	ret = sk_sendMsgStruct((MsgStruct *)&rReselect, 0, 0);
	if (ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: ReselectPrimaryApp returned %s\n",sk_errorText(ret));
	}

	return 0;
}

/////////////////////////////////

bool SIMMApp::processCommandLine(int argc, char *argv[])
{
	GetArg getArg;
	int argIndex = 1;

	while ( argIndex < argc )
	{
		argIndex = getArg.parseCmdLine(argc, argv, argIndex, "g:vbds");

		if (argIndex < 0 )
		{
			return FALSE;
		}

		switch (* (getArg.getOption()))
		{
		case 'h':
			// Returning false, will cause the usage data to be printed out.
			return FALSE;

		case 'g':
			// inbound channel group
			setInboundChannelGroup(getArg.getNextArg());
			break;

		case 'v':
			// verbose
			LogFileManager::getInstance()->setLogLevel(Verbose);
			break;

		case 'b':
			// brief (default)
			LogFileManager::getInstance()->setLogLevel(Brief);
			break;

		case 'd':
			// debug
			LogFileManager::getInstance()->setLogLevel(Debug);
			break;

		case 's':
			// turn off output to screen. Default is to send output to screen.
			LogFileManager::getInstance()->setOutputToScreen(FALSE);
			break;

		default:
			LogFileManager::getInstance()->write(Brief, "ERROR: Invalid Agument !!!" );
			return FALSE;
			break;
		} // end switch
	} // end while

	return TRUE;
}

void SIMMApp::printUsage()
{
	printf("SIMMApp:      Upon receipt of a Request for Service (RFS) message,\n");
	printf("              outseizes and connects  two channels based on,\n");
	printf("              a simple, hardcoded routing algorithm.  It \n");
	printf("              perform this function each time a new RFS is received.\n");
	printf("              It could be used in conjunction with callGen.  Both applications\n");
	printf("              will need tandem.cfg to be sent down to the switch \n");
	printf("              through SwitchMgr.\n");
	printf("-g inbound    - Inbound Channel Group (inbound is default)\n" );
	printf("-s            - Turn off output to screen. Default is on\n");
	printf("-h            - Print out this message and exit\n\n");
	printf("Output Levels: Should be mutually exclusive. Not enforced by application.\n");
	printf("-b            - Brief Output (default behavior)\n");
	printf("-v            - Verbose Output\n");
	printf("-d            - Debug Output\n");
}

bool SIMMApp::loadDataLoicLib()
{
	MY_HANDLE hInstLibrary = my_LoadLibrary( Routerdll);
	if(hInstLibrary == 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR: my_LoadLibrary %s failed!", Routerdll);
		return false;
	}
	_initRouter = (fpInitRouter)my_GetProcAddress(hInstLibrary, "initRouter");
	if(_initRouter == 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR: my_GetProcAddress initRouter failed!" );
		my_FreeLibrary(hInstLibrary);
		return false;  
	}
	_freeRouter = (fpFreeRouter)my_GetProcAddress(hInstLibrary, "freeRouter");
	if(_freeRouter == 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR: my_GetProcAddress freeRouter failed!" );
		my_FreeLibrary(hInstLibrary);
		return false;  
	}
	_makeDialRouter = (fpMakeDialRouter)my_GetProcAddress(hInstLibrary, "makeDialRouter");
	if(_makeDialRouter == 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR: my_GetProcAddress makeDialRouter failed!" );
		my_FreeLibrary(hInstLibrary);
		return false;  
	}
	_getResultDescription = (fpGetResultDescription)my_GetProcAddress(hInstLibrary, "getResultDescription");
	if(_getResultDescription == 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR: my_GetProcAddress getResultDescription failed!" );
		my_FreeLibrary(hInstLibrary);
		return false;  
	}

	if(_initRouter("config.txt") != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR: load config file failed!");
		my_FreeLibrary(hInstLibrary);
		return false;  
	}

	LogFileManager::getInstance()->write(Brief, "INFO: loadDataLoicLib Success !");

	return true ;
}

bool SIMMApp::readConf()
{
	string errorCode= "";
	ConfigFile conf;
	if( conf.SetConfigfile("./config.txt", errorCode) != true)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : %s",errorCode.c_str());
		return false;
	}

	string value;
	if(conf.get_string_value("SIMM_OPC", value) != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s","readConf [SIMM_OPC] failed !");
		return false ;
	}
	strncpy(_opc, value.c_str(), sizeof(_opc)-1);

	if(conf.get_string_value("SIMM_DPC", value) != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s","readConf [SIMM_DPC] failed !");
		return false ;
	}
	strncpy(_dpc, value.c_str(), sizeof(_dpc)-1);

	if(conf.get_string_value("SM_AGENT_IP", value) != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s","readConf [SM_AGENT_IP] failed !");
		return false ;
	}
	strncpy(_smAgentIp, value.c_str(), sizeof(_smAgentIp)-1);

	if(conf.get_integer_value("SM_AGENT_PORT", _smAgentPort) != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s","readConf [SM_AGENT_PORT] failed !");
		return false ;
	}

#ifdef ENABLE_CALLCENTER
	if( conf.get_string_value("CALLCENTER_NUM", value) != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s","readConf [CALLCENTER_NUM] failed !");
		return false ;
	}
	strncpy(_callcenterNbr, value.c_str(), sizeof(_callcenterNbr) - 1);
#endif

	if( conf.get_string_value("LOCAL_AREA_CODE", value) != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s","readConf [LOCAL_AREA_CODE] failed !");
		return false ;
	}
	strncpy(_localAreaCode, value.c_str(), sizeof(_localAreaCode) - 1);

	if( conf.get_integer_value("MOBILE_NUM_LEN",_mobileLen) != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s","readConf [MOBILE_NUM_LEN] failed !");
		return false ;
	}
	if( conf.get_string_value("ACCESS_CODE", value) != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s","readConf [ACCESS_CODE] failed !");
		return false ;
	}
	strncpy(_accessCode, value.c_str(), sizeof(_accessCode) - 1);	

	/*
	#虚拟副号码接入码
	VIRTUAL_ACCESS_CODE = 950966

	#虚拟副号码拨打IVR接入号码
	VIRTUAL_ACCESS_IVR = 95096600

	#虚拟副号码拨打二次拨号
	VIRTUAL_SECOND_DIAL = 95096699
	*/

	if( conf.get_integer_value("SEND_SM_FLAG",_sendSMFlag) != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s","readConf [SEND_SM_FLAG] failed ! default 0");
		_sendSMFlag = 0 ;
	}

	if( _sendSMFlag )
	{
		_sendSMFlag = 1;
	}

	//read switcher  
	string strSwitcherIP;
	string strSwitcherPort;
	int      iSwitcherID= 0 ;
	if( conf.get_string_value("SwitcherIP", strSwitcherIP) != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s","readConf [SwitcherIP] failed !");
		return false ;
	}
	else
	{
		g_strSwitcherIP = strSwitcherIP ;
	}

	if( conf.get_string_value("SwitcherPort", strSwitcherPort) != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s","readConf [SwitcherPort] failed !");
		return false ;
	}
	else
	{
		g_strSwitcherPort =  strSwitcherPort ;
	}
		
	if( conf.get_integer_value("SwitcherID",iSwitcherID) != 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s","readConf [SEND_SM_FLAG] failed ! default 0");
		_sendSMFlag = 0 ;
	}
	else
	{
		g_iSwitcherID = iSwitcherID ;
	}


	g_pHand = CreateHandle(1,g_strSwitcherIP.c_str(),g_strSwitcherPort.c_str(),g_iSwitcherID,"colorPrintSend");

	return true ;
}

int SIMMApp::inboundCallHandler(SK_Event *evt, void *tag) 
{
	SKC_Message *msg = evt->IncomingCMsg;

	/* The tag data field in this case is a pointer to the 'this'
	* pointer that represents the application.  It is a way to allow 
	* a static member function to be a message handler.
	*/

	LogFileManager::getInstance()->write(Debug, "INFO: Received SwitchKit message, %s", msg->getMsgName());

	SIMMApp * simm = (SIMMApp *)tag;

	SKC_MSG_SWITCH(msg) 
	{
		// If the msg is an RFSWithData msg, it will be cast to the rfsd variable
		CASEC_RFSWithData(rfsd)
		{
			/* The Router class obtains the digits contained in the RFSD
			* received from the switch, and determines 
			* the outbound Group to route the message to.
			* In this case, the router always "works". Even if the look up 
			* fails, some outbound route will be generated.  This isn't
			* realistic, but it works.
			*/

			SignalingParameters parameter(rfsd);
			CallRecord *cr = simm->getCallRecord(rfsd->getSpan(), rfsd->getChannel());
			if(cr == 0)
			{
				LogFileManager::getInstance()->write(Brief, "ERROR: get CallRecord Failed !");
				return SK_NOT_HANDLED;
			}
			cr->reset();
			cr->_callID = getCallID() ;
			sk_setDefaultHandler((void *)cr, simm->genericHandler);
			cr->_reference++ ;
			cr->signal_ = INCOMING_IAM;

			char textLog[512];
			memset(textLog, 0, sizeof(textLog));
			sprintf(textLog, "CallID(%d) Beginning New Call In Channel(0x%x, 0x%x), Caller = %s, Called = %s",
				cr->_callID, rfsd->getSpan(),  rfsd->getChannel(), 
				parameter.getCaller(),  parameter.getCalled());
			LogFileManager::getInstance()->write(Debug, "INFO: %s",textLog);

			// Set SIMMApp to point to the this pointer for the class.
			cr->_thisPtr = (void *)simm;

			// Set the receive time in the call record to the current time.
			cr->_rcvTime = time(NULL);

			cr->_inSpan = rfsd->getSpan();
			cr->_inChan = rfsd->getChannel();
			sk_setChannelHandler(cr->_inSpan, cr->_inChan, (void *)cr, genericHandler);

			/* Set the call out group in the Call Record */
			strncpy(cr->_outGroup, simm->getInboundChannelGroup(), sizeof(cr->_outGroup) - 1);

			/* Set the inbound caller and called in the Call Record */
			strncpy(cr->_inCaller, parameter.getCaller(), sizeof(cr->_inCaller) - 1);
			strncpy(cr->_inCalled, parameter.getCalled(), sizeof(cr->_inCalled) - 1);
			strncpy(cr->_inOriginalNbr, parameter.getOriginalNbr(), sizeof(cr->_inOriginalNbr) - 1);
			if( strlen(cr->_inOriginalNbr) != 0 )
			{
				memset(cr->_inCalled, 0, sizeof(cr->_inCalled));
				strncpy(cr->_inCalled, cr->_inOriginalNbr, MAX_NUMB_LEN - 1);
			}

			//may have SAM information
			char ac[20];
			memset(ac, 0, sizeof(ac));
			sprintf(ac, "%s%s", simm->_localAreaCode,simm->_accessCode);

			//zuosai Modify 20111209 
			int waitFlag = 0;
			int accesslen = strlen(simm->_accessCode);
			int calledlen = strlen(cr->_inCalled);

			if( cr->_inCaller[0] == '0' )
			{
				waitFlag = 1;
			}
			else if( !strncmp( cr->_inCalled, ac, strlen(ac) ) )
			{
				waitFlag = 1;
			}
			else if( !strncmp( cr->_inCalled, simm->_accessCode, accesslen ) )
			{
				if( calledlen <= 7 )
				{
					waitFlag = 0;
				}
				else
				{
					// 为下标为 1 的副号码
					if( cr->_inCalled[accesslen] == '1' )
					{
						// 检查被叫是否是手机号码
						if( cr->_inCalled[accesslen + 1] == '1' && calledlen == accesslen + 1 + 11 )
						{
							waitFlag = 0;
						}
						// 检查被叫是否是手机号码
						else if(calledlen == accesslen + 11)
						{
							waitFlag = 0;
						}
						else
						{
							waitFlag = 1;
						}
					}
					// 为下标为 2 或 3 的副号码
					else if( cr->_inCalled[accesslen] == '2' || cr->_inCalled[accesslen] == '3' )
					{
						if( cr->_inCalled[accesslen + 1] == '1' && calledlen == accesslen + 1 + 11 )
						{
							waitFlag = 0;
						}
						else
						{
							waitFlag = 1;
						}
					}
					// .s added by wj 20130216
					// 为下标为 6 的虚拟副号码
					else if( cr->_inCalled[accesslen] == '6' )
					{
						if(g_isPrintProcess)
						{
							char tmp[256] = { 0 };
							sprintf(tmp,"INFO: CallID(%d) Enter wj Modify InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s , FILE=%s,Line=%d", 
								cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

							LogFileManager::getInstance()->write(Debug, "%s",tmp);	
						}

						if( cr->_inCalled[accesslen + 1] == '1' && calledlen == accesslen + 1 + 11 )
						{
							waitFlag = 0;
						}
						else
						{
							waitFlag = 1;
						}
					}
					// .e added by wj 20130216
					else
					{
						waitFlag = 1;
					}
				}
			}

			if( waitFlag == 1 )
			{
				cr->_userTimerID = getUserTimerId();
				int ret = simm->startUserTimer(cr->_userTimerID, WAIT_SAM_INTERVAL, (void *)cr, genericHandler);
				LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Start Wait ISUP_SAM Msg UserTimer( TimerID = %d, Ret = 0x%x)", 
					cr->_callID,cr->_userTimerID, ret);

				return ret ;
			}
			else
			{
				// Send ACM to inComing 
				cr->signal_ = SENDOUT_ACM;
				return simm->sendPPLEvent(cr->_inSpan, cr->_inChan, ISUP_ACM, (void *)cr,genericHandler); 
			}

			/*
			if( (strlen(cr->_inCalled) > 7) && ((!strncmp(cr->_inCalled, simm->_accessCode, strlen(simm->_accessCode)))||
			(!strncmp(cr->_inCalled, ac, strlen(ac)))))
			{
			cr->_userTimerID = getUserTimerId();
			int ret = simm->startUserTimer(cr->_userTimerID, WAIT_SAM_INTERVAL, (void *)cr, genericHandler);
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Start Wait ISUP_SAM Msg UserTimer( TimerID = %d, Ret = 0x%x)", 
			cr->_callID,cr->_userTimerID, ret);
			return ret ;
			}

			// Send ACM to inComing 
			cr->signal_ = SENDOUT_ACM;
			return simm->sendPPLEvent(cr->_inSpan, cr->_inChan, ISUP_ACM, (void *)cr,genericHandler); 
			*/

			//zuosai Modify 20111209 end
		}
	} SKC_END_SWITCH;

	/* If message was not handled in SKC_MSG_SWITCH, then notify the caller */
	return(SK_NOT_HANDLED);
}

CallRecord * SIMMApp::getCallRecord(int span, int chan)
{
	if( (span < 0) || (chan < 0) )
	{
		return 0;
	}

	if( chan > 31 )
	{
		return 0;
	}

	int index = span * 32 + chan;
	if( index >= MAX_CHANNES_COUNT )
	{
		return 0;
	}

	LogFileManager::getInstance()->write(Brief, "INFO: Channel(0x%x, 0x%x) Get Index(%d) CallRecord",
		span, chan, index);
	return &_channes[index];
}

int SIMMApp::processRFSWithData(XLC_RFSWithData *rfsd)
{
	if(rfsd == 0)
		return SK_NOT_HANDLED ;

	SIMMApp * simm = (SIMMApp *)this;

	SignalingParameters parameter(rfsd);
	CallRecord *cr = simm->getCallRecord(rfsd->getSpan(), rfsd->getChannel());
	if(cr == 0)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR: get CallRecord Failed !");
		return SK_NOT_HANDLED;
	}

	cr->reset();
	cr->_callID = getCallID() ;
	sk_setDefaultHandler((void *)cr, simm->genericHandler);
	cr->_reference ++ ;
	cr->signal_ = INCOMING_IAM;

	char textLog[512];
	memset(textLog, 0, sizeof(textLog));
	sprintf(textLog, "CallID(%d) Beginning New Call In Channel(0x%x, 0x%x), Caller = %s, Called = %s",
		cr->_callID, rfsd->getSpan(),  rfsd->getChannel(), 
		parameter.getCaller(),  parameter.getCalled());
	LogFileManager::getInstance()->write(Debug, "INFO: %s",textLog);

	// Set SIMMApp to point to the this pointer for the class.
	cr->_thisPtr = (void *)simm;

	// Set the receive time in the call record to the current time.
	cr->_rcvTime = time(NULL);

	cr->_inSpan = rfsd->getSpan();
	cr->_inChan = rfsd->getChannel();
	sk_setChannelHandler(cr->_inSpan, cr->_inChan, (void *)cr, genericHandler);

	/* Set the call out group in the Call Record */
	strncpy(cr->_outGroup, simm->getInboundChannelGroup(), sizeof(cr->_outGroup)-1);

	/* Set the inbound caller and called in the Call Record */
	strncpy(cr->_inCaller, parameter.getCaller(), sizeof(cr->_inCaller)-1);
	strncpy(cr->_inCalled, parameter.getCalled(), sizeof(cr->_inCalled)-1);
	strncpy(cr->_inOriginalNbr, parameter.getOriginalNbr(), sizeof(cr->_inOriginalNbr)-1);
	if(strlen(cr->_inOriginalNbr) != 0)
	{
		memset(cr->_inCalled, 0, sizeof(cr->_inCalled));
		strncpy(cr->_inCalled, cr->_inOriginalNbr, MAX_NUMB_LEN-1);
	}

	//may have SAM information
	char ac[20];
	memset(ac, 0, sizeof(ac));
	sprintf(ac, "%s%s", simm->_localAreaCode,simm->_accessCode);
	//zuosai modify 20111209 
	int waitFlag=0;
	int accesslen=strlen(simm->_accessCode);
	int calledlen=strlen(cr->_inCalled);

	if(cr->_inCaller[0] == '0')
	{
		waitFlag=1;
	}
	else if(!strncmp(cr->_inCalled, ac, strlen(ac)))
	{
		waitFlag=1;
	}
	else if(!strncmp(cr->_inCalled, simm->_accessCode, accesslen))
	{
		if(calledlen <= 7 )
			waitFlag=0;
		else
		{
			if(cr->_inCalled[accesslen] == '1')
			{
				if(cr->_inCalled[accesslen+1]=='1' && calledlen==accesslen+1+11)
					waitFlag=0;
				else if(calledlen==accesslen+11)
					waitFlag=0;
				else
					waitFlag=1;
			}
			else if(cr->_inCalled[accesslen] == '2' || cr->_inCalled[accesslen] == '3')
			{
				if(cr->_inCalled[accesslen+1]=='1' && calledlen==accesslen+1+11)
					waitFlag=0;
				else
					waitFlag=1;
			}
			// .s added by wj 20130216
			// 为下标为 6 的虚拟副号码
			else if( cr->_inCalled[accesslen] == '6' )
			{
				if(g_isPrintProcess)
				{
					char tmp[256] = { 0 };
					sprintf(tmp,"INFO: CallID(%d) Enter wj Modify InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s , FILE=%s,Line=%d", 
						cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

					LogFileManager::getInstance()->write(Debug, "%s",tmp);	
				}

				if( cr->_inCalled[accesslen + 1] == '1' && calledlen == accesslen + 1 + 11 )
				{
					waitFlag = 0;
				}
				else
				{
					waitFlag = 1;
				}
			}
			// .e added by wj 20130216
			else
			{
				waitFlag=1;
			}
		}
	}

	if(waitFlag==1)
	{
		cr->_userTimerID = getUserTimerId();
		int ret = simm->startUserTimer(cr->_userTimerID, WAIT_SAM_INTERVAL, (void *)cr, genericHandler);
		LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Start Wait ISUP_SAM Msg UserTimer( TimerID = %d, Ret = 0x%x)", 
			cr->_callID,cr->_userTimerID, ret);
		return ret ;
	}
	else
	{
		// Send ACM to inComing 
		cr->signal_ = SENDOUT_ACM;
		return simm->sendPPLEvent(cr->_inSpan, cr->_inChan, ISUP_ACM, (void *)cr,genericHandler); 
	}
}

int SIMMApp::outseizeToNum(const CallRecord *cr)
{
	XL_OutseizeControl ocm;
	int ret = packOutseizeControlMsg(cr, ocm);
	if(ret < 0)
	{
		return ret ;
	}

	/* Request that the LLC choose a channel from the channel group indicated 
	* by outGroup field in the call record.  The LLC will automatically 
	* try 5 times to send the Outseize Control message passed in the 
	* ocm pointer. The static member function,  handleRequestChannelAck(), 
	* will handle the SK_RequestChannelAck message that is returned
	* from this call.
	*/
	ret = sk_requestOutseizedChannel((char *)cr->_outGroup, 
		5,   // Number of retries
		&ocm, 
		(void *)cr, 
		handleRequestChannelAck);

	if ( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: RequestOutseizedChannel returned %s\n",
			sk_errorText(ret));
	}

	return (ret);
}

int SIMMApp::processOutService( ResultMakeDialRoute result, CallRecord * cr)
{
	if(cr == 0)
	{
		return -1;
	}

	if(cr->_state)
	{
		delete cr->_state;
		cr->_state = 0;
	}

	OutServiceState * state = new OutServiceState(result);
	cr->_state = state;

	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, (void *)cr, outServiceHandler);
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));
	}

	if(cr->_isAnswer == 0)
	{
		/* Send ANM to inComing */
		cr->signal_ = SENDOUT_ANM;
		return sendPPLEvent(cr->_inSpan, cr->_inChan, ISUP_ANM, (void *)cr, outServiceHandler);
	}

	return (ret);
}

int SIMMApp::processSecondDial(CallRecord *cr)
{
	if(cr == 0)
	{
		return -1 ;
	}

	if(cr->_state)
	{
		delete cr->_state;
		cr->_state = 0;
	}

	SecondDialState * state = new SecondDialState();
	cr->_state = state;
	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp, "INFO: CallID(%d) Enter Function:processSecondDial() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);	
	}

	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, (void *)cr, secondDialHandler);
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));

		return ret;
	}


	int fileID[3] = {-1, -1, -1}; 
	if( strcmp( cr->_inCalled, "9509699") == 0 || strcmp( cr->_inCalled, "05919509699") == 0 )
	{

		ResultDataUsage result = DataUsage::instance()->isMajorRegistered(cr->_inCaller);
		DataUsage::instance()->getAllMinorNumber( cr->_inCaller, state->minorList_);
		if( ( result == E_MAJORNUMBER_NOT_EXESIT) || ( state->minorList_.size() == 0 ) )
		{
			//caller user not register simm app
			fileID[0] = 4308;
			fileID[1] = 4309;
			state->callerRegState_ = SecondDialState::STATE_NOT_REGISTER;
			state->sub_state_ = SecondDialState::STATE_SD_PLAY_INPUT_NUMBER_START;
		}
		else if(result == E_OPERATOR_SUCCED)
		{
			state->callerRegState_ = SecondDialState::STATE_ALREADY_REGISTER;
			if(cr->_isAnswer == 0)
			{
				/* Send ANM to inComing */
				cr->signal_ = SENDOUT_ANM;
				return sendPPLEvent(cr->_inSpan, cr->_inChan, ISUP_ANM, (void *)cr, secondDialHandler);
			}
		}
		else
		{
			//system failed
			fileID[0] = 4329;
			state->callerRegState_= SecondDialState::STATE_UNKNOWN;
			state->sub_state_ = SecondDialState::STATE_SD_PLAY_SYSTEM_BUSY_START;
		}
	}
	else if( strcmp( cr->_inCalled, "95096699") == 0 || strcmp( cr->_inCalled, "059195096699") == 0 )
	{

		string minorNumber="";
		ResultDataUsage ret_vmn = DataUsage::instance()->getMinorNumber(cr->_inCaller, 6, minorNumber);
		if( ret_vmn == E_OPERATOR_SUCCED)
		{
			state->callerRegState_ = SecondDialState::STATE_ALREADY_REGISTER;
			if(cr->_isAnswer == 0)
			{
				/* Send ANM to inComing */
				cr->signal_ = SENDOUT_ANM;
				return sendPPLEvent(cr->_inSpan, cr->_inChan, ISUP_ANM, (void *)cr, secondDialHandler);
			}
		}
		else if(  ret_vmn == E_MAJORNUMBER_NOT_EXESIT || ret_vmn == E_MINORINDEX_NUMBER_NOT_EXESIT)
		{
			//caller user not register simm app
			fileID[0] = 4445;
			fileID[1] = 4446;
			state->callerRegState_ = SecondDialState::STATE_NOT_REGISTER;
			state->sub_state_ = SecondDialState::STATE_SD_PLAY_INPUT_NUMBER_START;
		}
		else
		{
			fileID[0] = 4329;
			state->callerRegState_= SecondDialState::STATE_UNKNOWN;
			//state->sub_state_ = SecondDialState::STATE_SD_PLAY_SYSTEM_BUSY_START;
		}

	}



	return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, ( void *)cr, secondDialHandler);
}

int SIMMApp::processCallCenter(CallRecord *cr)
{
	if(cr == 0)
	{
		return -1;
	}

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp, "INFO: CallID(%d) Enter Function:processCallCenter() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);

	}

	/* If any unexpected events happen for this channel, 
	* they will be passed (eventually) to
	* genericHandler, which should take care of them 
	*/
	int flag = 1;
	memset(cr->_outGroup, 0, sizeof(cr->_outGroup));
	for(int i=0; i<5; i++)
	{
		if( strncmp(cr->_inCaller, (const char *)&_testNumber[i][0], strlen(cr->_inCaller)) == 0)
		{
			strcpy(cr->_outGroup, "callcenter");
			flag = 0;
			break ;
		}
	}

	/*0: callcenter test, 1:Operations*/
	if(flag == 1)
	{
		strcpy(cr->_outGroup, "callline");

		memset(cr->_outCalled, 0, sizeof(cr->_outCalled));
		strcpy(cr->_outCalled, "059112581130");
	}

	char textLog[512];
	sprintf(textLog, "CallID(%d) Beginning Connect to CallCenter: outGroup = %s, Caller = %s, Called = %s",
		cr->_callID, cr->_outGroup, cr->_outCaller, cr->_outCalled);

	LogFileManager::getInstance()->write(Debug, "INFO : %s", textLog);

	int stat = sk_setChannelHandler(cr->_inSpan, cr->_inChan, (void *)cr, genericHandler);
	if(stat != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x) Set Handler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(stat));
		return stat;
	}

	stat = outseizeToNum(cr);
	if(stat != OK ) 
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : outseizeToNum did not return properly\n");

		return stat;
	}

	return OK;
}

int SIMMApp::processIVR(CallRecord *cr)
{
	if(cr == 0)
	{
		return -1;
	}

	if(cr->_state)
	{
		delete cr->_state;
		cr->_state = 0;
	}

	cr->_state = new IvrRootState();

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp, "INFO: CallID(%d) Enter Function:processIVR() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);
	}

	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
		(void *)cr, IVR_rootHandler);
	if( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  sk_setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));

		return ret;
	}


	int fileID[2] = {-1, -1}; 
	if( strcmp( cr->_inCalled, "9509600") == 0 || strcmp( cr->_inCalled, "05919509600") == 0)
	{
		ResultDataUsage result = DataUsage::instance()->isMajorRegistered(cr->_inCaller); 
		if( ( result == E_MAJORNUMBER_NOT_EXESIT) )
		{
			//caller user not register simm app
			fileID[0] = 4301;
		}
		else if(result == E_OPERATOR_SUCCED)
		{
			if(cr->_isAnswer == 0)
			{
				/* Send ANM to inComing */
				cr->signal_ = SENDOUT_ANM;
				return sendPPLEvent(cr->_inSpan, cr->_inChan, ISUP_ANM, (void *)cr, IVR_rootHandler);
			}
			else
			{
				/*play ivr main menu */
				// 欢迎使用多号通，留言设置请按1，收听留言请按2，设置黑白名单请按3，设置免打扰时间请按4，查询副号码请按5，设置副号码顺序号请按6，
				// 设置副号码开关机请按7，呼叫转移设置请按8，资费及系统介绍请按9


				// 欢迎使用多号通，留言设置请按1，收听留言请按2，设置黑白名单请按3，设置免打扰时间请按4，查询副号码请按5，
				// 设置副号码开关机请按6，呼叫转移设置请按7，短信屏蔽请按8，资费及系统介绍请按9      // 以这为准  智山 20130218
				fileID[0] = 4328;
			}
		}
		else
		{
			//system failed
			fileID[0] = 4329;
		}
	}
	else if( strcmp( cr->_inCalled, "95096600") == 0 || strcmp( cr->_inCalled, "059195096600") == 0)
	{

		ResultDataUsage result = DataUsage::instance()->isVirtualMinorRegistered(cr->_inCaller);
		if( result == E_OPERATOR_SUCCED )
		{

			if(cr->_isAnswer == 0)
			{
				/* Send ANM to inComing */
				cr->signal_ = SENDOUT_ANM;
				return sendPPLEvent(cr->_inSpan, cr->_inChan, ISUP_ANM, (void *)cr, IVR_rootHandler);
			}
			else
			{
				/*play ivr main menu */
				// 欢迎使用多号通，留言设置请按1，收听留言请按2，设置黑白名单请按3，设置免打扰时间请按4，查询副号码请按5，设置副号码顺序号请按6，
				// 设置副号码开关机请按7，呼叫转移设置请按8，资费及系统介绍请按9


				// 欢迎使用多号通，留言设置请按1，收听留言请按2，设置黑白名单请按3，设置免打扰时间请按4，查询副号码请按5，
				// 设置副号码开关机请按6，呼叫转移设置请按7，短信屏蔽请按8，资费及系统介绍请按9      // 以这为准  智山 20130218
				fileID[0] = 4440;
			}

		}
		else if( result == E_DATABASE_ERROR)
		{
			//system failed
			fileID[0] = 4329;		// 当前系统忙， 请稍候再拨，谢谢

		}
		else           // modify  by pengjh for virtual number 2013.03.25
		{
			//对不起，您的号码没有注册多号通虚拟副号码业务。请咨询10086进行注册。
			fileID[0] = 4438;	

		}


	}


	return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, ( void *)cr, IVR_rootHandler);

}

int SIMMApp::processIvrSetRecord(CallRecord *cr)
{
	if(cr == 0)
	{
		return -1;
	}

	if(cr->_state)
	{
		delete cr->_state ;
		cr->_state = 0;
	}

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp, "INFO: CallID(%d) Enter Function:processIvrSetRecord() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);
		LogFileManager::getInstance()->write(Debug, "%s",tmp);

	}

	cr->_state = new IvrSetRecordState();
	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
		(void *)cr, IVR_setRecordHandler);
	if( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));
		return ret;
	}

	// .s added by wj 20121023
	// 虚拟副号码 IVR 呼叫
	if( strcmp( cr->_inCalled, "95096600") == 0 ||  strcmp( cr->_inCalled, "059195096600") == 0 )
	{
		// 判断一下 主叫是否为 虚拟副号码注册用户   
		if(g_isPrintProcess)
		{
			char tmp[256] = { 0 };
			sprintf(tmp,"INFO: CallID(%d) Enter wj Modify FILE=%s,Line=%d", cr->_callID,__FILE__,__LINE__);
			LogFileManager::getInstance()->write(Debug, "%s",tmp);	
		}

		int fileID[2] = {-1, -1};		
		ResultDataUsage result = DataUsage::instance()->isVirtualMinorRegistered(cr->_inCaller);
		if( result == E_OPERATOR_SUCCED )
		{
			IvrSetRecordState * state = reinterpret_cast<IvrSetRecordState *>(cr->_state);
			state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_START;
			state->minorNo_ = '6';

			string minorNumber="";
			ResultDataUsage ret= DataUsage::instance()->getMinorNumber(cr->_inCaller, 6, minorNumber);
			if( ret == E_OPERATOR_SUCCED)
			{
				strncpy( state->minor_, minorNumber.c_str() ,strlen(minorNumber.c_str()));	
				/*play 4015.wav */
				fileID[0] = 4015;			// 将所有来电转至语音信箱请按1, 添加单个号码请按2 ,解除所有来电转移到语音信箱请按3，解除除单个号码请按4，返回请按*号键
			}
			else
			{
				fileID[0]= 4438;	
				state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_SYSTEM_BUSY_START;
			}

			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setRecordHandler);
		}
		else	// 未注册就 拆线 挂机
		{
			IvrSetRecordState * state = reinterpret_cast<IvrSetRecordState *>(cr->_state);			
			state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_SYSTEM_BUSY_START;

			// 4301  // 对不起，您的号码没有注册多号通业务。请咨询10086进行注册。
			/*play 4438.wav */
			fileID[0] = 4438;			// 对不起，您的号码没有注册多号通虚拟副号码业务。请咨询10086进行注册。
			//int fileID[2] = {4301, -1};		// 对不起，您的号码没有注册多号通业务。请咨询10086进行注册。
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setRecordHandler);
		}
	}
	// .e added by wj 20121023


	/*play 4045.wav */
	int fileID[2] = {4045, -1};				    // 请选择您要设置的副号码，第一个；第二个；第三个；按1、2、3对应的顺序号进行选择。
	return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setRecordHandler);
}


int SIMMApp::processIvrListenRecord(CallRecord *cr)
{
	if(cr == 0)
		return -1;

	if(cr->_state)
	{
		delete cr->_state ;
		cr->_state = 0;
	}

	IvrListenRecordState *state = new IvrListenRecordState();
	cr->_state = state;

	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
		(void *)cr, IVR_listenRecordHandler);
	if( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));

		return ret;
	}

	vector<string> minorList ;
	int result = DataUsage::instance()->getAllMinorNumber(cr->_inCaller, minorList);
	if(result != E_OPERATOR_SUCCED)
	{
		/*play not record exist */
		int fileID[2] = { 4140, -1};
		state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_NOT_NEW_RECORD_START;
		return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
			(void *)cr, IVR_listenRecordHandler);
	}


	if( strcmp( cr->_inCalled, "95096600") == 0 ||  strcmp( cr->_inCalled, "059195096600") == 0)
	{
		string minorNumber="";
		ResultDataUsage ret = DataUsage::instance()->getMinorNumber( cr->_inCaller, 6, minorNumber );
		if( ret  != E_OPERATOR_SUCCED )
		{
			int fileID[2] = { 4329, -1};
			state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_SYSTEM_BUSY_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
				(void *)cr, IVR_listenRecordHandler);
		}
		else
		{

			state->minorNo_ = '6';
			strncpy(state->minorList_[0], minorNumber.c_str(), MAX_NUMB_LEN-1);
			int i,j,fileID[MAX_SEQUEL_PLAY_FILE], n=0;
			for( i=0; i< MAX_SEQUEL_PLAY_FILE; i++ )
			{
				fileID[i] = -1;
			}

			for( i=0, j=0; i< 1; i++ ) //virtual number only one number
			{
				int total, unpick;
				result = DataUsage::instance()->getVoiceCount(minorNumber, total, unpick);
				if(result == E_OPERATOR_SUCCED)
				{
					if(unpick==0) continue;

					fileID[j] = 4323 ;  j++;	
					fileID[j] = 1001+i; j++;			
					fileID[j] = 4324;   j++;
					fileID[j] = 1000 + unpick ; j++;
					fileID[j] = 4325 ;  j++ ;

					state->recordInfo_[n].recordMinorNo_ = '6';
					state->recordInfo_[n].newRecordCount_ = unpick;
					state->recordInfo_[n].allRecordCount_ = total;
					n++;
				}
				else 
				{
					int fileID2[2] = { 4329, -1};
					state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_SYSTEM_BUSY_START;
					return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID2, 
						(void *)cr, IVR_listenRecordHandler);
				}

			}

			if(j == 0) 
			{
				/* all minor not new record exist*/
				fileID[j] = 4140;
				state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_NOT_NEW_RECORD_START;

				return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
					(void *)cr, IVR_listenRecordHandler);
			}
			else
			{
				fileID[j] = 4439;			// 请输入6听取虚拟副号码留言，按*键返回主菜单	4439
				state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_HAS_RECORD_START;				
				return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
					(void *)cr, IVR_listenRecordHandler);
			}


		}


	}
	else  if( strcmp( cr->_inCalled, "9509600") == 0 ||  strcmp( cr->_inCalled, "05919509600") == 0)
	{
		state->minorCount_ = minorList.size();
		if (state->minorCount_ > 4) //minor count is greater than three
		{
			int fileID[2] = { 4329, -1};
			state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_SYSTEM_BUSY_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
				(void *)cr, IVR_listenRecordHandler);
		}

		int i,j,fileID[MAX_SEQUEL_PLAY_FILE], n=0;
		for( i=0; i< MAX_SEQUEL_PLAY_FILE; i++ )
		{
			fileID[i] = -1;
		}

		for( i =0; i< state->minorCount_; i++)
		{
			strncpy(state->minorList_[i], minorList[i].c_str(), MAX_NUMB_LEN-1);
		}

		for( i=0, j=0; i< state->minorCount_; i++ )
		{
			int total, unpick;
			result = DataUsage::instance()->getVoiceCount(state->minorList_[i], total, unpick);
			if(result == E_OPERATOR_SUCCED)
			{
				if(unpick==0) continue;

				fileID[j] = 4323 ;  j++;	
				fileID[j] = 1001+i; j++;			
				fileID[j] = 4324;   j++;
				fileID[j] = 1000 + unpick ; j++;
				fileID[j] = 4325 ;  j++ ;

				state->recordInfo_[n].recordMinorNo_ = '0'+(i+1);
				state->recordInfo_[n].newRecordCount_ = unpick;
				state->recordInfo_[n].allRecordCount_ = total;
				n++;
			}
			else 
			{
				int fileID2[2] = { 4329, -1};
				state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_SYSTEM_BUSY_START;
				return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID2, 
					(void *)cr, IVR_listenRecordHandler);
			}
		}

		if(j == 0) 
		{
			/* all minor not new record exist*/
			fileID[j] = 4140;
			state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_NOT_NEW_RECORD_START;

			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
				(void *)cr, IVR_listenRecordHandler);
		}
		else
		{
			/* minor has new record exist*/
			fileID[j] = 4326;
			state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_HAS_RECORD_START;

			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
				(void *)cr, IVR_listenRecordHandler);
		}

	}



	return OK;
}


int SIMMApp::processIvrSetAuthList(CallRecord *cr)
{
	if(cr == 0)
		return -1;

	if(cr->_state)
	{
		delete cr->_state ;
		cr->_state = 0;
	}

	cr->_state = new IvrSetAuthListState();

	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
		(void *)cr, IVR_setAuthListHandler);
	if( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));

		return ret;
	}


	if( strcmp( cr->_inCalled, "95096600") == 0 ||  strcmp( cr->_inCalled, "059195096600") == 0 )
	{

		if(g_isPrintProcess)
		{
			char tmp[256] = { 0 };
			sprintf(tmp, "INFO: CallID(%d) Enter wj Modify InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s , FILE=%s,Line=%d", 
				cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);
			LogFileManager::getInstance()->write(Debug, "%s",tmp);
		}	

		string minorNumber="";
		ResultDataUsage ret = DataUsage::instance()->getMinorNumber( cr->_inCaller, 6, minorNumber );
		if( ret == E_OPERATOR_SUCCED )
		{
			IvrSetAuthListState * state = reinterpret_cast<IvrSetAuthListState *>(cr->_state);
			//beigin modify by  pengjh for virtualMinir 2013.0315
			state->minorNo_ = '6';
			//end modify by pengjh for virtualMinir 2013.0315
			strncpy(state->minorNumber_,minorNumber.c_str(),strlen(minorNumber.c_str()));
			state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_START;

			/*请输入子菜单  play 4035.wav */
			int fileID[2] = {4035, -1};			// 设置黑名单请按1，解除黑名单限制请按2，设置白名单请按3， 解除白名单限制请按4，按*键返回主菜单
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setAuthListHandler);
		}
		else
		{
			IvrSetAuthListState * state = reinterpret_cast<IvrSetAuthListState *>(cr->_state);
			state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_SYSTEM_BUSY_START;

			/*请输入子菜单  play 4438.wav */
			int fileID[2] = {4438, -1};			// 对不起，您的号码没有注册多号通虚拟副号码业务。请咨询10086进行注册。	4438
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setAuthListHandler);
		}

	}

	/*play 4045.wav */
	int fileID[2] = {4045, -1}; 
	return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
		(void *)cr, IVR_setAuthListHandler);
}



int SIMMApp::processIvrSetAuthTime(CallRecord *cr)
{
	if(cr == 0)
		return -1;

	if(cr->_state)
	{
		delete cr->_state ;
		cr->_state = 0;
	}

	cr->_state = new IvrSetAuthTimeState();

	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
		(void *)cr, IVR_setAuthTimeHandler);
	if( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));

		return ret;
	}

	if( strcmp( cr->_inCalled, "95096600") == 0 ||  strcmp( cr->_inCalled, "059195096600") == 0 )
	{

		string minorNumber="";
		IvrSetAuthTimeState * state = reinterpret_cast<IvrSetAuthTimeState *>(cr->_state);
		ResultDataUsage ret = DataUsage::instance()->getMinorNumber( cr->_inCaller, 6, minorNumber );
		if( ret == E_OPERATOR_SUCCED )
		{
			/*
			40421.wav	策略1、所有的时间都接听电话
			40422.wav	策略2、只允许每天8点到21点之间的时间接听电话
			40423.wav	策略3、只?市碇芤坏街芪宓氖??浣犹缁?
			40424.wav	策略4、只允许周一到周五的8点到21点之间接听电话
			40425.wav	策略5、只允许在12点到14点和18点到21点之间进行接听
			40426.wav	策略6、只允许在周一到周五12点到14点和18点到21点之间进行接听
			40427.wav	策略7、只允许在周末接听电话
			40428.wav	策略8、只允许在周末12点到14点和18点到21点之间进行接听
			40429.wav	策略9、只允许在周末9点后进行接听
			404210.wav	请输入其中的任意一个策略序号进行设置
			*/
			/*play 40421.wav ~ 404210.wav */
			int fileID[11] = {40421,40422,40423, 40424, 40425, 40426, 40427, 40428, 40429, 404210, -1};
			//beigin modify by  pengjh for virtualMinir 2013.0315
			state->minorNo_ = '6';
			//end modify by pengjh for virtualMinir 2013.0315
			strncpy(state->minorNumber_ ,minorNumber.c_str(),strlen(minorNumber.c_str()));
			state->sub_state_ = IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setAuthTimeHandler);
		}
		else
		{
			int fileID[2] = {4329, -1}; 
			fileID[0] = 4329;		/*系统忙....*/
			state->sub_state_ = IvrSetAuthTimeState::STATE_ISAT_PLAY_SYSTEM_BUSY_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setAuthTimeHandler);
		}



	}

	/*play 4045.wav */
	int fileID[2] = {4045, -1}; 
	return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setAuthTimeHandler);

}

int SIMMApp::processIvrQueryMinor(CallRecord *cr)
{
	if(cr == 0)
		return -1;

	if(cr->_state)
	{
		delete cr->_state ;
		cr->_state = 0;
	}

	IvrQueryMinorState * state = new IvrQueryMinorState();
	cr->_state = state;

	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
		(void *)cr, IVR_queryMinorHandler);
	if( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));

		return ret;
	}


	if( strcmp( cr->_inCalled, "95096600") == 0 ||  strcmp( cr->_inCalled, "059195096600") == 0)
	{
		string minorNumber="";
		int virtualSeq = 6;
		ResultDataUsage retSearch = DataUsage::instance()->getMinorNumber( cr->_inCaller, 6, minorNumber );
		if ( retSearch == E_OPERATOR_SUCCED)
		{
			int fileID[MAX_SEQUEL_PLAY_FILE];
			int index = 0 ;
			for(int i=0; i<MAX_SEQUEL_PLAY_FILE; i++)
			{
				fileID[i] = -1 ;
			}

			fileID[index] = 4444; index++;		  /*序号为*/
			sprintf(state->minorMsg_, "序号为%d的虚拟副号码是:%s",virtualSeq, minorNumber.c_str());	
			int count = phonNum2voiceList(minorNumber.c_str(), &fileID[index]); index += count;
			fileID[index] = 4361; index += 1;		// 通过短信接收副号码信息请按1，继续收听请按2， 按*键返回主菜单
			fileID[index] = -1;

			if( index < 26 )
			{
				return  playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
					(void *)cr, IVR_queryMinorHandler);
			}
			else 
			{
				int firstFileId[25] = {-1};
				int i=0, j=0;
				for( ; i<24; i++)
				{
					firstFileId[i] = fileID[i];
				}
				firstFileId[24] = -1;

				//--------------------------------------------------------------
				int sequelElements = index-i ;
				if(sequelElements > MAX_SEQUEL_PLAY_FILE) /*文件数大于系统定义*/
				{
					return OK;
				}

				for(  ; i < index ; i++, j++)
				{
					state->data_.dataArray_[j] = fileID[i] ;
				}
				state->hasSequelData_ = 1; /*有后续的语音需要播放*/
				state->data_.elements_ = sequelElements ;

				//-------------------------------------------------------------

				return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, firstFileId, 
					(void *)cr, IVR_queryMinorHandler);
			}


		}
		else
		{
			int fileID[2] = { 4329, -1};
			state->sub_state_ = IvrQueryMinorState::STATE_IQM_PLAY_SYSTEM_BUSY_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_queryMinorHandler);
		}

	}
	else 
	{
		vector<MinorNumberAttr> minorList ;
		int result = DataUsage::instance()->getAllMinorNumber(cr->_inCaller, minorList);
		if( result == E_OPERATOR_SUCCED)
		{

			//add by pengjh for fujian 
			vector<MinorNumberAttr>::iterator  allIt= minorList.begin();
			while( allIt != minorList.end())
			{
				if((*allIt).sequenceNo > 3)
				{
					allIt=minorList.erase(allIt);		
				}
				else
				{
					++allIt;	
				}
				
			}
			
			int size = minorList.size();
			int fileID[MAX_SEQUEL_PLAY_FILE];
			for(int i=0; i<MAX_SEQUEL_PLAY_FILE; i++)
			{
				fileID[i] = -1 ;
			}
			int index = 0 ;
			if( (size > 0) && ( size < 4))
			{
				fileID[index] = 4346; index++;		  /*序号为*/
				if(size == 1)
				{  
					sprintf(state->minorMsg_, "序号为%d的副号码是:%s",
						minorList[0].sequenceNo, minorList[0].minorNumber);
					fileID[index] = 1000 + minorList[0].sequenceNo; index++;		
					fileID[index] = 4347; index++;	  /*的副号码是*/
					int count = phonNum2voiceList(minorList[0].minorNumber, &fileID[index]); index += count;
				}
				else if(size == 2)
				{
					sprintf(state->minorMsg_, "序号为%d的副号码是:%s, 序号为%d的副号码是:%s",
						minorList[0].sequenceNo,minorList[0].minorNumber,
						minorList[1].sequenceNo, minorList[1].minorNumber);

					fileID[index] = 1000 + minorList[0].sequenceNo; index++;		
					fileID[index] = 4347; index++;    /*的副号码是*/
					int count = phonNum2voiceList(minorList[0].minorNumber, &fileID[index]); index += count;

					fileID[index] = 4346; index++;	  /*序号为*/
					fileID[index] = 1000 + minorList[1].sequenceNo; index++;		
					fileID[index] = 4347; index++;	  /*的副号码是*/
					count = phonNum2voiceList(minorList[1].minorNumber, &fileID[index]); index += count;
				}
				else /*(size == 3)*/
				{
					sprintf(state->minorMsg_, "序号为%d的副号码是:%s, 序号为%d的副号码是:%s, 序号为%d的副号码是:%s",
						minorList[0].sequenceNo, minorList[0].minorNumber,
						minorList[1].sequenceNo, minorList[1].minorNumber,
						minorList[2].sequenceNo, minorList[2].minorNumber);

					fileID[index] = 1000 + minorList[0].sequenceNo; index++;
					fileID[index] = 4347; index++;	  /*的副号码是*/
					int count = phonNum2voiceList(minorList[0].minorNumber, &fileID[index]); index += count;
					fileID[index] = 4346; index++;	  /*序号为*/
					fileID[index] = 1000 + minorList[1].sequenceNo; index++;
					fileID[index] = 4347; index++;	  /*的副号码是*/
					count = phonNum2voiceList(minorList[1].minorNumber, &fileID[index]); index += count;
					fileID[index] = 4346; index++;	  /*序号为*/
					fileID[index] = 1000 + minorList[2].sequenceNo; index++;
					fileID[index] = 4347; index++;	  /*的副号码是*/
					count = phonNum2voiceList(minorList[2].minorNumber, &fileID[index]); index += count;
				}

				fileID[index] = 4361; index += 1; 
				fileID[index] = -1;

				if( index < 26 )
				{
					return ret = playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
						(void *)cr, IVR_queryMinorHandler);
				}
				else 
				{
					int firstFileId[25] = {-1};
					int i=0, j=0;
					for( ; i<24; i++)
					{
						firstFileId[i] = fileID[i];
					}
					firstFileId[24] = -1;

					//--------------------------------------------------------------
					int sequelElements = index-i ;
					if(sequelElements > MAX_SEQUEL_PLAY_FILE) /*文件数大于系统定义*/
					{
						return OK;
					}

					for(  ; i < index ; i++, j++)
					{
						state->data_.dataArray_[j] = fileID[i] ;
					}
					state->hasSequelData_ = 1; /*有后续的语音需要播放*/
					state->data_.elements_ = sequelElements ;

					//-------------------------------------------------------------

					return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, firstFileId, 
						(void *)cr, IVR_queryMinorHandler);
				}
			}
			else if( size == 0)
			{
				int fileID[2] = { 4301, -1};
				state->sub_state_ = IvrQueryMinorState::STATE_IQM_PLAY_NOT_MINOR_START;
				return  playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
					(void *)cr, IVR_queryMinorHandler);
			}
			else /*其他错误， 暂时不可用*/
			{
				int fileID[2] = { 4329, -1};
				state->sub_state_ = IvrQueryMinorState::STATE_IQM_PLAY_SYSTEM_BUSY_START;
				return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
					(void *)cr, IVR_queryMinorHandler);
			}
		}
		else /*其他错误， 暂时不可用*/
		{
			int fileID[2] = { 4329, -1};
			state->sub_state_ = IvrQueryMinorState::STATE_IQM_PLAY_SYSTEM_BUSY_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
				(void *)cr, IVR_queryMinorHandler);
		}
	}

}


int SIMMApp::processIvrSetMinorSequence(CallRecord *cr)
{
	if(cr == 0)
	{
		return -1;
	}

	if(cr->_state)
	{
		delete cr->_state ;
		cr->_state = 0;
	}

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp, "INFO: CallID(%d) Enter Function:processIvrSetMinorSequence() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);
	}

	IvrSetMinorSequenceState * state = new IvrSetMinorSequenceState();
	cr->_state = state;

	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
		(void *)cr, IVR_setMinorNoHandler);
	if( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));

		return ret;
	}

	// .s added by wj 20130216
	// 判断一下 是否为 真实副号码注册用户
	if( strcmp(cr->_inCalled, "9509600" ) == 0  || strcmp(cr->_inCalled, "05919509600" ) == 0 )
	{
		ResultDataUsage ret_t = DataUsage::instance()->isTrueMinorRegistered(cr->_inCaller);
		if( ret_t == E_OPERATOR_SUCCED )
		{
			// .e added by wj 20130216
			vector<MinorNumberAttr> minorList;
			int result = DataUsage::instance()->getAllMinorNumber(cr->_inCaller, minorList);
			if( result == E_OPERATOR_SUCCED)
			{
				int fileID[100];
				for(int n = 0; n < 100; n++)
				{
					fileID[n] = -1;
				}
				int index = 0, len = 0;
				state->minorCount_ = minorList.size();
				if( state->minorCount_ == 1 )	  //只有一个副号码
				{
					/*
					fileID[index] = 4305;
					state->sub_state_ = IvrSetMinorSequenceState::STATE_ISMS_PLAY_SINGLE_MINOR_START;
					*/
					fileID[index] = 4346; index++ ;
					fileID[index] = 1000 + minorList[0].sequenceNo; index++;
					fileID[index] = 4347; index++ ;
					len = phonNum2voiceList(minorList[0].minorNumber, &fileID[index]);
					index += len;

					fileID[index] = 4331; index++ ;
				}
				else if( state->minorCount_ == 2 ) //有二个副号码
				{
					fileID[index] = 4346; index++ ;
					fileID[index] = 1000 + minorList[0].sequenceNo; index++;
					fileID[index] = 4347; index++ ;
					len = phonNum2voiceList(minorList[0].minorNumber, &fileID[index]);
					index += len;

					fileID[index] = 4346; index++ ;
					fileID[index] = 1000 + minorList[1].sequenceNo; index++;
					fileID[index] = 4347; index++ ;
					len = phonNum2voiceList(minorList[1].minorNumber, &fileID[index]);
					index += len;

					fileID[index] = 4331; index++ ;
				}
				else if( state->minorCount_ == 3 ) //有3个副号码
				{
					fileID[index] = 4346; index++ ;
					fileID[index] = 1000 + minorList[0].sequenceNo; index++;
					fileID[index] = 4347; index++ ; 
					len = phonNum2voiceList(minorList[0].minorNumber, &fileID[index]);
					index += len;

					fileID[index] = 4346; index++ ;
					fileID[index] = 1000 + minorList[1].sequenceNo; index++;
					fileID[index] = 4347; index++ ; 
					len = phonNum2voiceList(minorList[1].minorNumber, &fileID[index]);
					index += len;

					fileID[index] = 4346; index++ ;
					fileID[index] = 1000 + minorList[2].sequenceNo; index++;
					fileID[index] = 4347; index++ ; 
					len = phonNum2voiceList(minorList[2].minorNumber, &fileID[index]);
					index += len;

					fileID[index] = 4331; index++ ;
				} 
				//begin Add by  pengjh  2013.03.13
				else if ( state->minorCount_ == 4) //虚拟副号码
				{ 
					fileID[index] = 4346; index++ ;
					fileID[index] = 1000 + minorList[0].sequenceNo; index++;
					fileID[index] = 4347; index++ ; 
					len = phonNum2voiceList(minorList[0].minorNumber, &fileID[index]);
					index += len;

					fileID[index] = 4346; index++ ;
					fileID[index] = 1000 + minorList[1].sequenceNo; index++;
					fileID[index] = 4347; index++ ; 
					len = phonNum2voiceList(minorList[1].minorNumber, &fileID[index]);
					index += len;

					fileID[index] = 4346; index++ ;
					fileID[index] = 1000 + minorList[2].sequenceNo; index++;
					fileID[index] = 4347; index++ ; 
					len = phonNum2voiceList(minorList[2].minorNumber, &fileID[index]);
					index += len;

					fileID[index] = 4346; index++ ;
					fileID[index] = 1000 + minorList[3].sequenceNo; index++;
					fileID[index] = 4347; index++ ; 
					len = phonNum2voiceList(minorList[3].minorNumber, &fileID[index]);
					index += len;

					fileID[index] = 4331; index++ ;

				}
				//end Add by  pengjh  2013.03.13
				else  /*副号码多于3个, 系统错误*/
				{
					fileID[index] = 4329; index++ ;	
					state->sub_state_ = IvrSetMinorSequenceState::STATE_ISMS_PLAY_SYSTEM_BUSY_START;
				}

				if(index < 26)
				{
					return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
						(void *)cr, IVR_setMinorNoHandler);
				}
				else
				{
					int firstFileId[25] = {-1};
					int i=0, j=0;
					for( ; i<24; i++)
					{
						firstFileId[i] = fileID[i];
					}
					firstFileId[24] = -1;

					//-------------------------------------------------
					int sequelElements = index-i ;
					if(sequelElements > MAX_SEQUEL_PLAY_FILE) /*文件数大于系统定义*/
					{
						return OK;
					}
					for(  ; i < index ; i++, j++)
					{
						state->data_.dataArray_[j] = fileID[i] ;
					}
					state->hasSequelData_ = 1; /*有后续的语音需要播放*/
					state->data_.elements_ = sequelElements ;
					//------------------------------------------------

					return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, firstFileId, 
						(void *)cr, IVR_setMinorNoHandler);
				}
			}
			else
			{
				int fileID[2] = { 4329, -1};
				state->sub_state_ = IvrSetMinorSequenceState::STATE_ISMS_PLAY_SYSTEM_BUSY_START;
				return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
					(void *)cr, IVR_setMinorNoHandler);
			}
			// .s added by wj 20130216
		}
	}
	// .e added by wj 20130216

	return OK;
}

int SIMMApp::processIvrSetMinorPower(CallRecord *cr)
{
	if(cr == 0)
	{
		return -1;
	}

	if(cr->_state)
	{
		delete cr->_state ;
		cr->_state = 0;
	}

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp, "INFO: CallID(%d) Enter Function:processIvrSetMinorPower() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);
	}

	cr->_state = new IvrSetMinorPowerState();

	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
		(void *)cr, IVR_setMinorPowerHandler);
	if( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));

		return ret;
	}


	// .s added by wj 20121024
	if( strcmp( cr->_inCalled, "95096600") == 0 ||  strcmp( cr->_inCalled, "059195096600") == 0 )
	{
		// 判断一下 主叫是否为 虚拟副号码注册用户     
		if(g_isPrintProcess)
		{

			char tmp[256] = { 0 };
			sprintf(tmp, "INFO: CallID(%d) Enter wj Modify InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s , FILE=%s,Line=%d", 
				cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);
			LogFileManager::getInstance()->write(Debug, "%s",tmp);

		}

		string minorNumber;		
		IvrSetMinorPowerState * state = reinterpret_cast<IvrSetMinorPowerState *>(cr->_state);
		ResultDataUsage result = DataUsage::instance()->getMinorNumber( cr->_inCaller, 6, minorNumber );
		if( result == E_OPERATOR_SUCCED )
		{	
			int fileID[3] = { -1, -1,-1 };
			state->minorNo_ = '6';
			strcpy( state->minorNumber_, minorNumber.c_str() );
			ResultDataUsage ret = DataUsage::instance()->getMinorNumberState( minorNumber );
			state->logicState_ = ret ;
			if( ret == STATE_ACTIVE )	  //STATE_ACTIVE
			{
				fileID[0] = 4441 ;		  // 副号码当前状态为开机			// 新语音 您的虚拟副号码当前状态为开机 4441
				fileID[1] = 4333 ;		  // 关机请按 3，*键返回
				state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_START;
			}
			else if( ret == STATE_OFFLINE ) //STATE_OFF
			{

				fileID[0] = 4442 ;		  // 副号码当前状态为关机			// 新语音 您的虚拟副号码当前状态为关机 4442
				fileID[1] = 4332 ;	      // 开机请按 2，*键返回
				state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_START;
			}
			else
			{
				fileID[0] = 4329 ;		  // 当前系统忙， 请稍候再拨，谢谢
				state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_SYSTEM_BUSY_START;
			}

			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setMinorPowerHandler);
		}
		else
		{
			int fileID[2] = { -1, -1 };
			fileID[0] = 4329 ;		  // 当前系统忙， 请稍候再拨，谢谢
			state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_SYSTEM_BUSY_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setMinorPowerHandler);
		}
	}
	else
	{
		/*play 4045.wav */
		int fileID[2] = {4045, -1};			  // 请选择您要设置的副号码，第一个；第二个；第三个；按1、2、3对应的顺序号进行选择。
		return  playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setMinorPowerHandler);
	}
	// .e added by wj 20121024
}

int SIMMApp::processIvrSMSAuthTime(CallRecord *cr)
{
	if(cr == 0)
	{
		return -1;
	}

	if(cr->_state)
	{
		delete cr->_state ;
		cr->_state = 0;
	}

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp, "INFO: CallID(%d) Enter Function:processIvrSMSAuthTime() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);
	}

	cr->_state = new IvrSetSMSAuthTimeState();

	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
		(void *)cr, IVR_setSMSAuthTimeHandler);
	if( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));

		return ret;
	}


	// .s added by wj 20121102
	if( strcmp( cr->_inCalled, "95096600") == 0 ||  strcmp( cr->_inCalled, "059195096600") == 0 )
	{
		if(g_isPrintProcess)
		{		
			char tmp[256] = { 0 };
			sprintf(tmp, "INFO: CallID(%d) Enter wj Modify InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s , FILE=%s,Line=%d", 
				cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);
			LogFileManager::getInstance()->write(Debug, "%s",tmp);
		}
		// 判断一下 主叫是否为 虚拟副号码注册用户     
		string minorNumber="";
		IvrSetSMSAuthTimeState * state =  reinterpret_cast<IvrSetSMSAuthTimeState *>(cr->_state);
		ResultDataUsage ret = DataUsage::instance()->getMinorNumber( cr->_inCaller, 6, minorNumber );
		if( ret == E_OPERATOR_SUCCED )
		{	
			/*play *.wav */
			int fileID[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
			int pos = 0;
			fileID[pos] = 4354 ; pos += 1; /* 输入 */
			fileID[pos] = 1001 ; pos += 1; /* 1 */
			fileID[pos] = 4351 ; pos += 1; /* 全天24小时不接收短信： */

			fileID[pos] = 4354 ; pos += 1; /* 输入 */
			fileID[pos] = 1002 ; pos += 1; /* 2 */
			fileID[pos] = 4352 ; pos += 1; /* 在您设定的时间段内不接收短信 */

			fileID[pos] = 4354 ; pos += 1; /* 输入 */
			fileID[pos] = 1003 ; pos += 1; /* 3 */
			fileID[pos] = 4353 ; pos += 1; /* 所有时间都接收短信 */


			//beigin modify by  pengjh for virtualMinir 2013.0315
			state->minorNo_ = '6';
			strncpy(state->minorNumber_,minorNumber.c_str(),strlen(minorNumber.c_str()));
			//end modify by pengjh for virtualMinir 2013.0315
			state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setSMSAuthTimeHandler);
		}
		else if( ret == E_MINORINDEX_NUMBER_NOT_EXESIT || ret ==  E_MAJORNUMBER_NOT_EXESIT )
		{
			int fileID[2] = { 4438, -1};			// 对不起，您的号码没有注册多号通虚拟副号码业务。请咨询10086进行注册。?
			state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_SYSTEM_BUSY_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setSMSAuthTimeHandler);
		}
		else
		{
			int fileID[2] = { 4329, -1};			// 当前系统忙， 请稍候再拨，谢谢
			state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_SYSTEM_BUSY_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setSMSAuthTimeHandler);
		}
	}
	// .e added by wj 20121102


	////zuosai Modify 20110124 
	IvrSetSMSAuthTimeState * state = reinterpret_cast<IvrSetSMSAuthTimeState *>(cr->_state);
	vector<MinorNumberAttr> minorList;
	int result = DataUsage::instance()->getAllMinorNumber(cr->_inCaller, minorList);
	if(result == E_OPERATOR_SUCCED)
	{
		if(minorList.size() == 1)
		{
			state->minorNo_ = minorList[0].sequenceNo + '0';
			strncpy(state->minorNumber_, minorList[0].minorNumber, MAX_NUMB_LEN - 1);

			/*play *.wav */
			int fileID[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
			int pos = 0;
			fileID[pos] = 4354 ; pos += 1; /* 输入 */
			fileID[pos] = 1001 ; pos += 1; /* 1 */
			fileID[pos] = 4351 ; pos += 1; /* 全天24小时不接收短信： */

			fileID[pos] = 4354 ; pos += 1; /* 输入 */
			fileID[pos] = 1002 ; pos += 1; /* 2 */
			fileID[pos] = 4352 ; pos += 1; /* 在您设定的时间段内不接收短信 */

			fileID[pos] = 4354 ; pos += 1; /* 输入 */
			fileID[pos] = 1003 ; pos += 1; /* 3 */
			fileID[pos] = 4353 ; pos += 1; /* 所有时间都接收短信 */

			state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setSMSAuthTimeHandler);
		}
		else if(minorList.size() > 1)
		{
			/*play 4045.wav */
			int fileID[2] = {4045, -1}; 
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setSMSAuthTimeHandler);

		}
	}

	int fileID[2] = { 4329, -1};			// 当前系统忙， 请稍候再拨，谢谢
	state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_SYSTEM_BUSY_START;
	return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setSMSAuthTimeHandler);
	////zuosai Modify 20110124 end
}

int SIMMApp::processIvrSetMultiNumber(CallRecord *cr)
{
	if(cr == 0)
	{
		return -1;
	}

	if(cr->_state)
	{
		delete cr->_state ;
		cr->_state = 0;
	}

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp, "INFO: CallID(%d) Enter Function:processIvrSetMultiNumber() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);
	}

	cr->_state = new IvrSetSetMultiNumberState();
	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
		(void *)cr, IVR_setMultiNumberHandler);
	if( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));

		return ret;
	}


	// .s added by wj 20121024
	if( strcmp( cr->_inCalled, "95096600") == 0 ||  strcmp( cr->_inCalled, "059195096600") == 0 )
	{
		if(g_isPrintProcess)
		{
			char tmp[256] = { 0 };
			sprintf(tmp, "INFO: CallID(%d) Enter wj Modify InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s , FILE=%s,Line=%d", 
				cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

			LogFileManager::getInstance()->write(Debug, "%s",tmp);	
		}
		// 判断一下 主叫是否为 虚拟副号码注册用户     
		string minorNumber="";
		IvrSetSetMultiNumberState * state = reinterpret_cast<IvrSetSetMultiNumberState *>(cr->_state);
		ResultDataUsage ret = DataUsage::instance()->getMinorNumber( cr->_inCaller, 6, minorNumber );
		if( ret  == E_OPERATOR_SUCCED )
		{	

			state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_START;

			//beigin modify by  pengjh for virtualMinir 2013.0315
			state->minorNo_ = '6';
			strncpy(state->minorNumber_,minorNumber.c_str(),strlen(minorNumber.c_str()));
			//end modify by pengjh for virtualMinir 2013.0315
			/*播放子菜单提示语  play 4420.wav */
			int fileID[2] = {4420, -1};			// 增加呼叫转移号码请按1,删除呼叫转移号码请按2,调整接听顺序请按3			
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setMultiNumberHandler);
		}
		else if( ret == E_MINORINDEX_NUMBER_NOT_EXESIT || ret ==  E_MAJORNUMBER_NOT_EXESIT )
		{
			int fileID[2] = { 4438, -1};			// 对不起，您的号码没有注册多号通虚拟副号码业务。请咨询10086进行注册。?
			state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_SYSTEM_BUSY_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setMultiNumberHandler);
		}
		else
		{
			int fileID[2] = { 4329, -1};			
			state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_SYSTEM_BUSY_START;
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setMultiNumberHandler);
		}
	}
	// .e added by wj 20121024


	/*play 4045.wav */
	int fileID[2] = {4045, -1};					// 请选择您要设置的副号码，第一个；第二个；第三个；按1、2、3对应的顺序号进行选择。
	return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_setMultiNumberHandler);
}

int SIMMApp::processIvrPlaySystemInfo(CallRecord *cr)
{
	if(cr == 0)
	{
		return -1;
	}

	if(cr->_state)
	{
		delete cr->_state ;
		cr->_state = 0;
	}

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp, "INFO: CallID(%d) Enter Function:processIvrPlaySystemInfo() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);	
	}

	cr->_state = new IvrPlaySystemInfoState();

	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
		(void *)cr, IVR_playSystemInfoHandler);
	if( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));

		return ret;
	}

	// .s added by wj 20121024
	if( strcmp( cr->_inCalled, "95096600") == 0 ||  strcmp( cr->_inCalled, "059195096600") == 0 )
	{
		if(g_isPrintProcess)
		{
			char tmp[256] = { 0 };
			sprintf(tmp, "INFO: CallID(%d) Enter wj Modify InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s , FILE=%s,Line=%d", 
				cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

			LogFileManager::getInstance()->write(Debug, "%s",tmp);
		}	
		// 判断一下 主叫是否为 虚拟副号码注册用户     
		ResultDataUsage result = DataUsage::instance()->isVirtualMinorRegistered(cr->_inCaller);
		if( result == E_OPERATOR_SUCCED )
		{	
			/*play 4327.wav */
			//int fileID[2] = {4327, -1};			// 是否需要新的语音  资费及系统介绍 ？？？
			/*
			多号通虚拟副号码业务是，在不换手机不换卡的情况下，在您原有手机号码的基础上申请一个新副号码，
			新申请的副号码开通后，您就可以使用它来接听来电。也可以通过拨950966+对方号码的方式拨号，在对方电话上显示您的副号码。
			多号通虚拟副号业务除了具备语音通话功能外，还有隐藏原有手机号码，保护您的隐私的作用，同时还具备来电筛选，语音信箱，
			指定时间段呼入，收发短信等功能，为您的工作和生活增添很多的方便和乐趣。精彩人生，多重角色，
			欢迎注册使用多号通虚拟副号码业务，详情10086客服热线。
			*/
			int fileID[2] = {4437, -1};			
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_playSystemInfoHandler);
		}
		else if ( result == E_VIRTUAL_MINOR_NOT_EXESIT)
		{
			int fileID[2] = {4438, -1};			
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_playSystemInfoHandler);
		}
		else
		{
			int fileID[2] = { 4329, -1};	
			return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_playSystemInfoHandler);

		}

	}
	// .e added by wj 20121024

	/*play 4327.wav */
	int fileID[2] = {4327, -1}; 
	return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, (void *)cr, IVR_playSystemInfoHandler);

}

int SIMMApp::IVR_rootHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
	{
		return -1 ;
	}

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 

	if(cr->_state == 0)
	{
		return -1;
	}

	if(cr->_state->state_ != State::STATE_IVR_ROOT)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error In Channel:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	IvrRootState * state = reinterpret_cast<IvrRootState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->sub_state_);

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp,  "INFO: CallID(%d) Enter Function:IVR_rootHandler() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);	
	}

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_PPLEventRequestAck(ppl)
		{
			if(ppl->getStatus() == 0x10)
			{
				//.s add the signal to ack the anm request by whc 20100127
				cr->signal_ = ANM_ACK;
				//.e add the signal to ack the anm request by whc 20100127
				cr->_isAnswer = 1 ; //应答主叫完成
				int fileID[2] = {-1, -1}; 

				//if called = 9509600
				if( strcmp( cr->_inCalled, "9509600") == 0 || strcmp( cr->_inCalled, "05919509600") == 0)
				{

					ResultDataUsage result = DataUsage::instance()->isTrueMinorRegistered(cr->_inCaller);				
					if(result == E_MAJORNUMBER_NOT_EXESIT)
					{
						//caller user not register simm app
						fileID[0] = 4301;		// 对不起，您的号码没有注册多号通业务。请咨询10086进行注册。
						state->sub_state_ = IvrRootState::STATE_IR_PLAY_MIJOR_UNREG_START;
					}
					else if( result == E_DATABASE_ERROR)
					{
						//system failed
						fileID[0] = 4329;		// 当前系统忙， 请稍候再拨，谢谢
						state->sub_state_ = IvrRootState::STATE_IR_PLAY_SYSTEM_BUSY_START;
					}
					else
					{

						/*play ivr welcome word*/
						fileID[0] = 4328;		// 欢迎使用多号通，留言设置请按1，收听留言请按2，设置黑白名单请按3，设置免打扰时间请按4，查询副号码请按5，
						// 设置副号码顺序号请按6，设置副号码开关机请按7，呼叫转移设置请按8，资费及系统介绍请按9

						// 欢迎使用多号通，留言设置请按1，收听留言请按2，设置黑白名单请按3，设置免打扰时间请按4，查询副号码请按5，
						// 设置副号码开关机请按6，呼叫转移设置请按7，短信屏蔽请按8，资费及系统介绍请按9      // 以这为准  智山 20130218

					}

				}
				else if( strcmp( cr->_inCalled, "95096600") == 0 || strcmp( cr->_inCalled, "059195096600") == 0 )
				{

					if(g_isPrintProcess)
					{
						char tmp[256] = { 0 };
						sprintf(tmp, "INFO: CallID(%d) Enter wj Modify IVR_rootHandler CASEC_PPLEventRequestAck(ppl) InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s , FILE=%s,Line=%d", 
							cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);
						LogFileManager::getInstance()->write(Debug, "%s",tmp);	
					}	

					ResultDataUsage result = DataUsage::instance()->isVirtualMinorRegistered(cr->_inCaller);
					if( result == E_OPERATOR_SUCCED )
					{

						/*play ivr welcome word*/
						fileID[0] = 4440;		// 欢迎使用多号通，留言设置请按1，收听留言请按2，设置黑白名单请按3，设置免打扰时间请按4，查询副号码请按5，
						// 设置副号码开关机请按7，呼叫转移设置请按8，资费及系统介绍请按9

						// 欢迎使用多号通，留言设置请按1，收听留言请按2，设置黑白名单请按3，设置免打扰时间请按4，查询副号码请按5，
						// 设置副号码开关机请按6，呼叫转移设置请按7，短信屏蔽请按8，资费及系统介绍请按9      // 以这为准  智山 20130218
						//state->sub_state_ = IvrRootState::;
					}
					else if( result == E_DATABASE_ERROR)
					{
						//system failed
						fileID[0] = 4329;		// 当前系统忙， 请稍候再拨，谢谢
						state->sub_state_ = IvrRootState::STATE_IR_PLAY_SYSTEM_BUSY_START;
					}
					else           // modify  by pengjh for virtual number 2013.03.25
					{
						//对不起，您的号码没有注册多号通虚拟副号码业务。请咨询10086进行注册。
						fileID[0] = 4438;	
						state->sub_state_ = IvrRootState::STATE_IR_PLAY_MIJOR_UNREG_START;
					}


				}

				//.s Modify for the simm call watching by whc 20100910
				cr->_callResult = 0;        /*call out sucess*/
				//.e Modify for the simm call watching by whc 20100910
				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, ( void *)cr, IVR_rootHandler);
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: PPLEventRequest could not be successfully processed, Ack State = 0x%x", 
					ppl->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if( rDspReq->getStatus() == 0x10)
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, tag, simm->IVR_rootHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
				switch( state->sub_state_)
				{
				case IvrRootState::STATE_IR_RECV_MENU_DTMF_CMPLT:
				case IvrRootState::STATE_IR_RECV_OTHER_DTMF_CMPLT:
					{
						return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, IVR_rootHandler);
					}
				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if( playAck->getStatus() == 0x10 )
			{
				if( state->sub_state_ == IvrRootState::STATE_IR_PLAY_MAIN_MENU_START )
				{
					state->sub_state_ = IvrRootState::STATE_IR_RECV_MENU_DTMF;
				}
				else if( state->sub_state_ == IvrRootState::STATE_IR_PLAY_MIJOR_UNREG_START)
				{
					state->sub_state_ = IvrRootState::STATE_IR_RECV_OTHER_DTMF;
				}
				else
				{
					return OK;
				}

				return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP + 5, (void *)cr, IVR_rootHandler);
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: PlayFileStart could not be successfully processed, Ack State = 0x%x", 
					playAck->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{

				if( state->sub_state_ == IvrRootState::STATE_IR_RECV_MENU_DTMF_CMPLT)
				{
					if( state->menuItem_ == '1')			/*留言设置*/
					{
						return simm->processIvrSetRecord(cr);
					}
					else if(state->menuItem_ == '2')		/*收听留言*/
					{
						return simm->processIvrListenRecord(cr);
					}
					else if(state->menuItem_ == '3')		/*设置黑白名单*/
					{
						return simm->processIvrSetAuthList(cr);
					}
					else if(state->menuItem_ == '4')		/*设置免打扰时间*/
					{
						return simm->processIvrSetAuthTime(cr);
					}
					else if(state->menuItem_ == '5')		/* 查询副号码 */				// （虚拟副号码时）查询副号码
						return simm->processIvrQueryMinor(cr);

					//else if(state->menuItem_ == '6')		/*设置副号码顺序号*/
					//	return simm->processIvrSetMinorSequence(cr);

					else if(state->menuItem_ == '6')		/*设置副号码开关机*/			// （虚拟副号码时）副号码开关机
					{
						return simm->processIvrSetMinorPower(cr);
					}
					else if(state->menuItem_ == '7')		/* 设置呼叫转移 */				// （虚拟副号码时）设置呼叫转移
					{
						return simm->processIvrSetMultiNumber(cr);
					}

					else if(state->menuItem_ == '8')		/* 短信屏蔽   副号码接收 */		/*设置副号码短信时间策略*/
					{			
						return simm->processIvrSMSAuthTime(cr);

					}
					//return simm->processIvrSMSAuthTime(cr);
					else if(state->menuItem_ == '9')		/* 资费及系统介绍 */		
					{			
						return simm->processIvrPlaySystemInfo(cr);							// （虚拟副号码时）是否需要修改语音
					}
					else if(state->menuItem_ == '*')		/*重复收听*/
					{
						return simm->processIVR(cr);
					}
					else									/*拆线,挂机*/
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
					}
				}
				else if(state->sub_state_ == IvrRootState:: STATE_IR_RECV_OTHER_DTMF_CMPLT)
				{
					if(state->menuItem_ == '*')
					{
						return simm->processIvrPlaySystemInfo(cr);
					}
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				}
			}
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			switch(rCpe->getEvent())
			{
			case RecvDtmf:
				{
					char buf[256];
					memset(buf, 0, sizeof(buf));
					simm->collectDigitResult( (SKC_Message *)rCpe, buf );

					if(strlen(buf) == 0)
						return OK;

					state->crcPlayCount_ = 0;
					if(state->userTimerId_ != 0)
					{
						simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_rootHandler) ;
						state->userTimerId_ = 0;
					}

					if(state->sub_state_ == IvrRootState::STATE_IR_RECV_MENU_DTMF)
					{
						state->menuItem_ = buf[0] ;
						state->sub_state_ = IvrRootState::STATE_IR_RECV_MENU_DTMF_CMPLT;
					}
					if(state->sub_state_ == IvrRootState::STATE_IR_RECV_OTHER_DTMF)
					{
						state->menuItem_ = buf[0] ;
						state->sub_state_ = IvrRootState::STATE_IR_RECV_OTHER_DTMF_CMPLT;
					}

					int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
					return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, (void *)cr, IVR_rootHandler);
				}
			case FilePlayOK:
				{
					LogFileManager::getInstance()->write(Debug, 
						"INFO: CallID(%d) In Channel(0x%x,0x%x), FilePlayOK ",
						cr->_callID,cr->_inSpan, cr->_inChan);

					if (state->sub_state_ == IvrRootState::STATE_IR_RECV_MENU_DTMF)
					{
						state->userTimerId_ = getUserTimerId();
						return simm->startUserTimer(state->userTimerId_, CRC_PLAY_INTERVAL, (void *)cr, IVR_rootHandler);
					}
					if( state->sub_state_ == IvrRootState::STATE_IR_PLAY_SYSTEM_BUSY_START)
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
					}

					return OK;
				}
			default:
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
			}
		}

		CASEC_UserTimerAck(varNm)
		{
			if(state->userTimerId_ !=0)
			{
				simm->cancelUserTimer(state->userTimerId_,(void *)cr,IVR_rootHandler);
				state->userTimerId_ = 0;
			}
			if (varNm->getStatus() == 0x10)
			{
				if (state->sub_state_ == IvrRootState::STATE_IR_RECV_MENU_DTMF)
				{
					if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

					int fileID[2] = {4328,-1};
					state->crcPlayCount_++;

					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, ( void *)cr, IVR_rootHandler);
				}
			}
			return OK;
		}
		CASEC_CancelUserTimerAck(varNm)
		{
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) In Channel(0x%x,0x%x), CancelUserTimer, status = %d",
				cr->_callID,cr->_inSpan, cr->_inChan, varNm->getStatus());

			return OK;
		}

		CASEC_default
		{
			return defaultHandler( evt, tag);
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;
}

int SIMMApp::IVR_setRecordHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if(cr->_state == 0)
		return -1;

	if(cr->_state->state_ != State::STATE_IVR_SET_RECORD)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error In Channel:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	IvrSetRecordState * state = reinterpret_cast<IvrSetRecordState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->sub_state_);

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if( rDspReq->getStatus() == 0x10)
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
					tag, 
					simm->IVR_setRecordHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetRecordState::STATE_ISR_PLAY_INPUT_MINOR_NO_START:
				case IvrSetRecordState::STATE_ISR_PLAY_MINOR_NO_ERROR_START:
				case IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_START:
				case IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_ERROR_START:
				case IvrSetRecordState::STATE_ISR_PLAY_ALL_OPT_SUCCESS_START:
				case IvrSetRecordState::STATE_ISR_PLAY_SINGLE_OPT_SUCCESS_START:
				case IvrSetRecordState::STATE_ISR_PLAY_INPUT_NUMBER_START:
				case IvrSetRecordState::STATE_ISR_PLAY_FAILED_START:
					{
						return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, IVR_setRecordHandler);
					}
				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if(playAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetRecordState::STATE_ISR_PLAY_INPUT_MINOR_NO_START:
				case IvrSetRecordState::STATE_ISR_PLAY_MINOR_NO_ERROR_START:
				case IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_START:
				case IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_ERROR_START:
				case IvrSetRecordState::STATE_ISR_PLAY_ALL_OPT_SUCCESS_START:
				case IvrSetRecordState::STATE_ISR_PLAY_SINGLE_OPT_SUCCESS_START:
					{
						return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
							(void *)cr, IVR_setRecordHandler);
					}
				case IvrSetRecordState::STATE_ISR_PLAY_INPUT_NUMBER_START:
				case IvrSetRecordState::STATE_ISR_PLAY_FAILED_START:
					{
						return simm->collectDigit( cr->_inSpan, cr->_inChan, MAX_COLLECT_DIGIT_LEN,
							rand()%5+100, "*#", (void *)cr, IVR_setRecordHandler);
					}
				default:
					return SK_NOT_HANDLED;
				}
			}
			else
				return SK_NOT_HANDLED;
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{	
				if(state->sub_state_ == IvrSetRecordState::STATE_ISR_PLAY_INPUT_MINOR_NO_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT )
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					if(( state->minorNo_ > '0') && ( state->minorNo_ < '6')) 
						state->retry_ = 0;

					else if( state->minorNo_ == '*')  /*返回主菜单*/
						return simm->processIVR(cr);

					else /*输入的副号码序号错误*/
					{
						/*play 4330.wav */
						int fileID[2] = {4330, -1};
						state->sub_state_ =IvrSetRecordState::STATE_ISR_PLAY_MINOR_NO_ERROR_START;
						state->retry_ ++;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setRecordHandler);
					}

					int fileID[2] = {-1, -1}; 
					vector<MinorNumberAttr> minorList;
					int result = DataUsage::instance()->getAllMinorNumber(cr->_inCaller, minorList);
					state->minorCount_ = minorList.size();
					if(result == E_MINORNUMBER_NOT_EXESIT)
					{
						/* caller no minor number exist , play 4301.wav */
						fileID[0] = 4301; 
						state->sub_state_ =IvrSetRecordState::STATE_ISR_PLAY_SYSTEM_BUSY_START;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setRecordHandler);
					}

					/*在副号码表中查找输入副号码序号*/
					int arrIndex = -1;
					for(int i=0; i< minorList.size(); i++)
					{
						if( minorList[i].sequenceNo == (state->minorNo_-'0'))
						{
							arrIndex = i ;
							break ;
						}
					}
					if(arrIndex == -1)
					{
						/*play 4313.wav */
						fileID[0] = 4313; 
						state->sub_state_ =IvrSetRecordState::STATE_ISR_PLAY_MINOR_NO_ERROR_START;
						state->retry_ ++;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setRecordHandler);
					}

					if((result != E_OPERATOR_SUCCED) || (state->minorCount_ > 7))  
					{
						/*play 4329.wav */
						fileID[0] = 4329; 
						state->sub_state_ =IvrSetRecordState::STATE_ISR_PLAY_SYSTEM_BUSY_START;
					}
					else
					{
						strncpy( state->minor_, minorList[arrIndex].minorNumber, MAX_NUMB_LEN-1);
						/*play 4015.wav */
						fileID[0] = 4015;
						state->sub_state_ =IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_START;
					}
					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, IVR_setRecordHandler);
				}
				else if(state->sub_state_ == IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_START)
				{
					if(state->actionKey_ == '*')
						return simm->processIvrSetRecord(cr);

					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					int fileID[3] = { -1, -1, -1};
					if(( state->minorNo_ < '1') || ( state->minorNo_ > '6')) 
					{
						/*play 4330.wav */
						fileID[0] = 4330; 
						state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_ERROR_START;
						state->retry_ ++ ;
					}
					if(state->actionKey_ == '1')
					{
						ResultDataUsage result = DataUsage::instance()->setVoiceBoxType(state->minor_,VOICEBOXTYPE_ALL);
						if(result == E_OPERATOR_SUCCED)
						{
							/*play 4017.wav + 4360.wav*/
							fileID[0] = 4017; 
							fileID[1] = 4360; 
							state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_ALL_OPT_SUCCESS_START;
						}
						else
						{
							/*play 4329.wav */
							fileID[0] = 4329; 
							state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_SYSTEM_BUSY_START;
						}
					}
					else if( (state->actionKey_ == '2') || (state->actionKey_ == '4'))
					{
						/*play 4013.wav or 4038.wav */
						fileID[0] = 4013;
						if(state->actionKey_ == '4')
							fileID[0] = 4038;
						state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_INPUT_NUMBER_START;
					}
					else if(state->actionKey_== '3')
					{
						ResultDataUsage result = DataUsage::instance()->releaseAllVoiceNumber(state->minor_);/* 解除 */
						if(result == E_OPERATOR_SUCCED)
						{
							/*play 4079.wav + 4360.wav */
							fileID[0] = 4079; 
							fileID[1] = 4360; 
							state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_ALL_OPT_SUCCESS_START;
						}
						else
						{
							/*play 4329.wav */
							fileID[0] = 4329; 
							state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_SYSTEM_BUSY_START;
						}
					}

					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, IVR_setRecordHandler);
				}
				else if(state->sub_state_ == IvrSetRecordState::STATE_ISR_PLAY_INPUT_NUMBER_START)
				{
					int len = strlen(state->number_);
					if(len <= 0)
						return OK;

					//zuosai add 20110210 for bug
					char endKey = 0;
					endKey = state->number_[len-1];
					if(endKey == '#')
						state->number_[len-1] = '\0';
					else if (endKey == '*')
						return simm->processIVR(cr);
					else
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					if(  len > MIN_NUMB_LEN+1 )
					{
						int result ;

						if(state->actionKey_ == '2')
						{
							result = DataUsage::instance()->addNumberToVoiceList(state->minor_,state->number_);
						}
						else
						{
							result = DataUsage::instance()->releaseVoiceNumber(state->minor_,state->number_);
						}
						if(result == E_OPERATOR_SUCCED)  /*操作成功*/
						{
							int fileID[2] = {-1, -1};
							if(state->actionKey_ == '2')
								fileID[0] = 4018;
							if(state->actionKey_ == '4')
								fileID[0] = 4315;

							state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_SINGLE_OPT_SUCCESS_START;

							return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
								(void *)cr, IVR_setRecordHandler);
						}
						else /*操作失败*/
						{
							int fileID[2] = {4016, -1};
							state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_FAILED_START;

							return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
								(void *)cr, IVR_setRecordHandler);
						}
					}
					else
					{
						int fileID[2] = {4016, -1};
						state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_FAILED_START;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setRecordHandler);
					}
					//zuosai add 20110210 for bug end
				}
				else if(state->sub_state_ == IvrSetRecordState::STATE_ISR_PLAY_SINGLE_OPT_SUCCESS_START
					|| state->sub_state_ == IvrSetRecordState::STATE_ISR_PLAY_ALL_OPT_SUCCESS_START)
				{
					if( ((state->actionKey_ == '2') || (state->actionKey_ == '4')) &&
						(state->sequelKey_ == '1') )
					{
						/*play 4013.wav or 4038.wav */
						int fileID[2] = {4013, -1};
						//.s del the actionKey_ value by whc 20100127
						//if(state->actionKey_ == '2')
						//.e del the actionKey_ value by whc 20100127						
						if(state->actionKey_ == '4')

							fileID[0] = 4038;

						state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_INPUT_NUMBER_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setRecordHandler);
					}
					if( ((state->actionKey_ == '2') || (state->actionKey_ == '4')) &&
						(state->sequelKey_ == '*') )
					{
						return simm->processIVR(cr);
					}
					if(((state->actionKey_ == '1') || (state->actionKey_ == '3')) &&
						(state->sequelKey_ == '1'))
					{
						return simm->processIvrSetRecord(cr);
					}
					if(((state->actionKey_ == '1') || (state->actionKey_ == '3')) &&
						(state->sequelKey_ == '*'))
					{
						return simm->processIVR(cr);
					}
					else
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
					}
				}
				else
					return OK;
			}
		}
		CASEC_CollectDigitStringAck(rCollect)
		{
			if(rCollect->getStatus() == 0x10)
			{
				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: CollectDigitString could not be successfully processed, Ack State = 0x%x", 
					rCollect->getStatus());

				return OK;
			}
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			if( rCpe->getEvent() == RecvDtmf )
			{
				char buf[256];
				memset(buf, 0, sizeof(buf));
				simm->collectDigitResult( (SKC_Message *)rCpe, buf );

				if(strlen(buf) == 0)
					return OK;

				state->crcPlayCount_ = 0;
				if(state->userTimerId_ != 0)
				{
					simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setRecordHandler) ;
					state->userTimerId_ = 0;
				}

				switch(state->sub_state_)
				{
				case IvrSetRecordState::STATE_ISR_PLAY_INPUT_MINOR_NO_START:
				case IvrSetRecordState::STATE_ISR_PLAY_MINOR_NO_ERROR_START:
					{
						state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_INPUT_MINOR_NO_START;
						state->minorNo_ = buf[0];
						break;
					}
				case IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_START:
				case IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_ERROR_START:
					{
						state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_START;
						state->actionKey_ = buf[0];
						break;
					}
				case IvrSetRecordState::STATE_ISR_PLAY_INPUT_NUMBER_START:
				case IvrSetRecordState::STATE_ISR_PLAY_FAILED_START:
					{
						state->sub_state_ = IvrSetRecordState::STATE_ISR_PLAY_INPUT_NUMBER_START;
						strncpy(state->number_, buf, MAX_NUMB_LEN-1);
						break;
					}
				case IvrSetRecordState::STATE_ISR_PLAY_ALL_OPT_SUCCESS_START:
				case IvrSetRecordState::STATE_ISR_PLAY_SINGLE_OPT_SUCCESS_START:
					{
						state->sequelKey_ = buf[0];
						break;
					}
				default:
					{
						break;
					}
				}

				int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
				return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, 
					(void *)cr, IVR_setRecordHandler);
			}
			else if (rCpe->getEvent() == FilePlayOK) /* 正常播放完毕 */
			{
				switch(state->sub_state_)
				{
				case IvrSetRecordState::STATE_ISR_PLAY_INPUT_MINOR_NO_START:
				case IvrSetRecordState::STATE_ISR_PLAY_MINOR_NO_ERROR_START:
				case IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_START:
				case IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_ERROR_START:
				case IvrSetRecordState::STATE_ISR_PLAY_INPUT_NUMBER_START:
				case IvrSetRecordState::STATE_ISR_PLAY_SINGLE_OPT_SUCCESS_START:
				case IvrSetRecordState::STATE_ISR_PLAY_FAILED_START:
				case IvrSetRecordState::STATE_ISR_PLAY_ALL_OPT_SUCCESS_START:
					{
						/* 发送定时器 */
						state->userTimerId_ = getUserTimerId();
						return simm->startUserTimer(state->userTimerId_,CRC_PLAY_INTERVAL, (void *)cr, IVR_setRecordHandler);
					}
				case IvrSetRecordState::STATE_ISR_PLAY_SYSTEM_BUSY_START:
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

				default:
					return OK;
				}
			}
			else 
				return  simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent()) ;
		}
		CASEC_UserTimerAck(varNm) /* 定时器返回消息 */
		{
			if(state->userTimerId_ !=0)
			{
				simm->cancelUserTimer(state->userTimerId_,(void *)cr,IVR_setRecordHandler);
				state->userTimerId_ = 0;
			}
			LogFileManager::getInstance()->write(Debug,"state->crcPlayCount_: %d",state->crcPlayCount_);
			if (varNm->getStatus() == 0x10)
			{
				int fileID[2] = {-1,-1};
				switch(state->sub_state_)
				{
				case IvrSetRecordState::STATE_ISR_PLAY_INPUT_MINOR_NO_START:
				case IvrSetRecordState::STATE_ISR_PLAY_MINOR_NO_ERROR_START:
					{
						fileID[0] = 4045;
						break;
					}
				case IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_START:
				case IvrSetRecordState::STATE_ISR_PLAY_ACTION_MENU_ERROR_START:
					{
						fileID[0] = 4015;
						break;
					}
					//zuosai modify 20110210 for bug		
				case IvrSetRecordState::STATE_ISR_PLAY_INPUT_NUMBER_START:
					{
						fileID[0] = 4013;
						if(state->actionKey_ == '4')
							fileID[0] = 4038;
						break;
					}
				case IvrSetRecordState::STATE_ISR_PLAY_SINGLE_OPT_SUCCESS_START:
					{
						fileID[0] = 4018;
						if(state->actionKey_ == '4')
							fileID[0] = 4315;
						break;
					}
				case IvrSetRecordState::STATE_ISR_PLAY_ALL_OPT_SUCCESS_START:
					{
						fileID[0] = 4360;
						break;
					}
				case IvrSetRecordState::STATE_ISR_PLAY_FAILED_START:
					{
						fileID[0] = 4016;
						break;
					}
					//zuosai modify 20110210 for bug end
				default:
					return OK;
				}
				if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

				state->crcPlayCount_++;
				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
					( void *)cr, IVR_setRecordHandler);
			}
			return OK;
		}
		CASEC_CancelUserTimerAck(varNm)
		{
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) In Channel(0x%x,0x%x), CancelUserTimer, status = %d",
				cr->_callID, cr->_inSpan, cr->_inChan, varNm->getStatus());

			return OK;
		}
		CASEC_default
		{
			return defaultHandler( evt, tag);
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;

}

int SIMMApp::IVR_listenRecordHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if(cr->_state == 0)
		return -1;

	if(cr->_state->state_ != State::STATE_IVR_LISTEN_RECORD)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error In Channel:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	IvrListenRecordState * state = reinterpret_cast<IvrListenRecordState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->sub_state_);

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if(rDspReq->getStatus())
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, tag, simm->IVR_listenRecordHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrListenRecordState::STATE_ILR_PLAY_HAS_RECORD_START:
				case IvrListenRecordState::STATE_ILR_PLAY_MINOR_NO_ERROR_START:
				case IvrListenRecordState::STATE_ILR_ONE_VOICE_COMPLETE:
					{
						return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, IVR_listenRecordHandler);
					}
				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if(playAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrListenRecordState::STATE_ILR_ONE_VOICE_COMPLETE:
					{
						//设置留言读取状态
						if( state->minorNo_ ==  '6' )
						{
								DataUsage::instance()->setVoiceHasRead(state->minorList_[0], state->filePath_);
						}
						else 
						{
								DataUsage::instance()->setVoiceHasRead(state->minorList_[state->minorNo_-'1'], state->filePath_);
						}
						
					}				
				case IvrListenRecordState::STATE_ILR_PLAY_HAS_RECORD_START:
				case IvrListenRecordState::STATE_ILR_PLAY_MINOR_NO_ERROR_START:
				case IvrListenRecordState::STATE_ILR_PLAY_LISTEN_RECORD_START:
					//case IvrListenRecordState::STATE_ILR_PLAY_NOT_NEW_RECORD_START:
					{
						return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
							(void *)cr, IVR_listenRecordHandler);
					}
				case IvrListenRecordState::STATE_ILR_PLAY_RECORD_CONTENT_START:
				case IvrListenRecordState::STATE_ILR_PLAY_RECORD_EXPLAIN_START:	
				case IvrListenRecordState::STATE_ILR_PLAY_NOT_NEW_RECORD_START:
					{

						return OK;
					}
				default:
					return SK_NOT_HANDLED;
				}
			}
			else
				return SK_NOT_HANDLED;
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{
				if( state->sub_state_ == IvrListenRecordState::STATE_ILR_PLAY_HAS_RECORD_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					if( strcmp( cr->_inCalled, "95096600") == 0 ||  strcmp( cr->_inCalled, "059195096600") == 0)
					{
						if(state->minorNo_ == '6')
							state->retry_ = 0;
						else if( state->minorNo_ == '*')  /*返回上级菜单*/
							return simm->processIVR(cr);
						else 
						{
							/*您输入的副号码序号错误 4313.wav*/
							int fileID[2] = {4313, -1};
							state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_MINOR_NO_ERROR_START;
							state->retry_ ++;

							return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
								(void *)cr, IVR_listenRecordHandler);
						}
					}
					else if( strcmp( cr->_inCalled, "9509600") == 0 ||  strcmp( cr->_inCalled, "05919509600") == 0 )
					{
						if(( state->minorNo_ > '0') && ( state->minorNo_ < '4'))
							state->retry_ = 0;
						else if( state->minorNo_ == '*')  /*返回上级菜单*/
							return simm->processIVR(cr);
						else 
						{
							/*您输入的副号码序号错误 4313.wav*/
							int fileID[2] = {4313, -1};
							state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_MINOR_NO_ERROR_START;
							state->retry_ ++;

							return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
								(void *)cr, IVR_listenRecordHandler);
						}
					}



					int flag = 0;
					for(int i=0; i < MAX_MINOR_COUNT; i++)
					{
						if(state->recordInfo_[i].recordMinorNo_ == state->minorNo_)
						{
							int fileID[24];
							for(int j=0; j<24; j++)
							{
								fileID[j] = -1;
							}
							int pos = 0;

							fileID[pos] = 4322;	pos += 1;	//此副号码有
							int ret = simm->digit2voiceList(state->recordInfo_[i].allRecordCount_ , &fileID[pos]);   
							pos += ret;
							fileID[pos] = 4142;  pos += 1;   //条留言,其中新留言
							ret = simm->digit2voiceList(state->recordInfo_[i].newRecordCount_, &fileID[pos]);
							pos += ret;
							fileID[pos] = 4143;  pos += 1;    //条.收听留言过程中,按任意键打断收听,按*号键停止收听
							/*
							int fileID[6] = { -1, -1, -1, -1, -1, -1};
							fileID[0] = 4322;   //此副号码有
							fileID[1] = 1000 + state->recordInfo_[i].allRecordCount_ ;
							fileID[2] = 4142;   //条留言,其中新留言
							fileID[3] = 1000 + state->recordInfo_[i].newRecordCount_ ;
							fileID[4] = 4143;   //条.收听留言过程中,按任意键打断收听,按*号键停止收听
							fileID[5] = -1;
							*/

							flag ++ ;
							state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_LISTEN_RECORD_START;

							return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
								(void *)cr, IVR_listenRecordHandler);
						}
					} /*for*/
					if(flag == 0) 
					{
						/*您输入的副号码序号不存在，请核对后重新输入*/
						int fileID[2] = {4313, -1};
						state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_MINOR_NO_ERROR_START;
						state->retry_ ++;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_listenRecordHandler);
					}
				}
				else if( state->sub_state_ == IvrListenRecordState::STATE_ILR_ONE_VOICE_COMPLETE)
				{
					/* 收听完一条留言后 */
					char keyBuff = state->actionKey_;
					switch(keyBuff)
					{
					case '1': /* 删除该条留言 */
						{
							ResultDataUsage ret;
							if (state->minorNo_ == '6')
							{			
 								ret = DataUsage::instance()->deleteVoiceFile(state->minorList_[0],state->filePath_);
							}
							else
							{
								 ret = DataUsage::instance()->deleteVoiceFile(state->minorList_[state->minorNo_-'1'],state->filePath_);
							}

							if (ret != E_OPERATOR_SUCCED)
							{
								return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
								/* 删除留言失败 */
							}
							else
							{

								/* 删除成功 */
								int fileID[2] = {4338,-1};
								state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_LISTEN_RECORD_START;
								return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
									(void *)cr, IVR_listenRecordHandler);
							}

							break;
						}
					case '2': /* 继续收听其他留言 */
						{
							state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_LISTEN_RECORD_START;
							simm->playListenRecord(cr);
							break;
						}
					case '*': /* 返回主菜单 */
						{
							return simm->processIVR(cr);
						}
					default:
						{
							state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_LISTEN_RECORD_START;
							simm->playListenRecord(cr);
							break;
						}
					}

				}
				else
					return OK;
			}
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			if( rCpe->getEvent() == RecvDtmf )
			{
				char buf[256];
				memset(buf, 0, sizeof(buf));
				simm->collectDigitResult( (SKC_Message *)rCpe, buf );

				if(strlen(buf) <= 0)
					return OK;

				state->crcPlayCount_ = 0;

				if(state->userTimerId_ != 0)
				{
					simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_listenRecordHandler) ;
					state->userTimerId_ = 0;
				}

				switch(state->sub_state_)
				{
				case IvrListenRecordState::STATE_ILR_PLAY_HAS_RECORD_START:
				case IvrListenRecordState::STATE_ILR_PLAY_MINOR_NO_ERROR_START:
					{
						state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_HAS_RECORD_START;
						state->minorNo_ = buf[0];
						break;
					}
					//case IvrListenRecordState::STATE_ILR_PLAY_LISTEN_RECORD_START:
				case IvrListenRecordState::STATE_ILR_ONE_VOICE_COMPLETE:
					{
						state->actionKey_ = buf[0];
						break;
					}
				default:
					break;
				}

				int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
				return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, 
					(void *)cr, IVR_listenRecordHandler);
			}
			else if(rCpe->getEvent() == FilePlayStart)
			{
				if( state->sub_state_ == IvrListenRecordState::STATE_ILR_PLAY_RECORD_EXPLAIN_START)
				{
					if( ( state->hasSequelData_ != 0) 
						&& (state->data_.elements_ > 0))
					{
						return simm->playFileQueue(cr->_inSpan, cr->_inChan, Dsp2Slot, state->data_.dataArray_,
							(void *)cr, IVR_listenRecordHandler);
					}
					else
						return OK;
				}
				else 
					return OK;
			}
			else if(rCpe->getEvent() == FilePlayCmpt)
			{
				return OK;
			}
			else if(rCpe->getEvent() == FilePlayOK)
			{
				if( state->sub_state_ == IvrListenRecordState::STATE_ILR_PLAY_NOT_NEW_RECORD_START)
				{
					return simm->processIVR(cr);
				}
				else if( state->sub_state_ == IvrListenRecordState::STATE_ILR_PLAY_LISTEN_RECORD_START)
				{
					return simm->playListenRecord(cr);
				}
				else if( state->sub_state_ == IvrListenRecordState::STATE_ILR_PLAY_RECORD_EXPLAIN_START)
				{
					state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_RECORD_CONTENT_START;
					return simm->playVoiceFile(cr->_inSpan, cr->_inChan, Dsp2Slot, getPlayFileId(), 
						state->filePath_, 0,(void *)cr, IVR_listenRecordHandler);
				}
				else if (state->sub_state_ == IvrListenRecordState::STATE_ILR_PLAY_RECORD_CONTENT_START)
				{
					state->sub_state_ = IvrListenRecordState::STATE_ILR_ONE_VOICE_COMPLETE;
					int fileID[2] ={4146,-1};
					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
						(void *)cr, IVR_listenRecordHandler);
				}

				else if ( state->sub_state_ == IvrListenRecordState::STATE_ILR_ONE_VOICE_COMPLETE ||
					state->sub_state_ == IvrListenRecordState::STATE_ILR_PLAY_HAS_RECORD_START || 
					state->sub_state_ == IvrListenRecordState::STATE_ILR_PLAY_MINOR_NO_ERROR_START)//zuosai modify 20110210 for bug
				{
					/* Timer */
					state->userTimerId_ = getUserTimerId();
					return simm->startUserTimer(state->userTimerId_, CRC_PLAY_INTERVAL,(void *)cr,IVR_listenRecordHandler);
				}
				else
					return OK ;
			}
			else
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
		}
		CASEC_UserTimerAck(varNm) /* 定时器返回消息 */
		{
			if(state->userTimerId_ != 0)
			{
				simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_listenRecordHandler) ;
				state->userTimerId_ = 0;
			}
			if (varNm->getStatus() == 0x10)
			{
				int fileID[2] = {-1,-1};
				if ( state->sub_state_ == IvrListenRecordState::STATE_ILR_ONE_VOICE_COMPLETE)
					fileID[0]=4146;
				else if (state->sub_state_ == IvrListenRecordState::STATE_ILR_PLAY_HAS_RECORD_START)
					fileID[0]=4326;
				else if(state->sub_state_ == IvrListenRecordState::STATE_ILR_PLAY_MINOR_NO_ERROR_START)
					fileID[0]=4313;//zuosai add 20110210 for bug
				else
					return OK;

				if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

				state->crcPlayCount_++;
				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
					( void *)cr, IVR_listenRecordHandler);
			}
			return OK;
		}
		CASEC_CancelUserTimerAck(varNm)
		{
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) In Channel(0x%x,0x%x), CancelUserTimer, status = %d",
				cr->_callID, cr->_inSpan, cr->_inChan, varNm->getStatus());
			return OK;
		}
		CASEC_default
		{
			return defaultHandler( evt, tag);
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;



}


int SIMMApp::IVR_setAuthListHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if(cr->_state == 0)
		return -1;

	if(cr->_state->state_ != State::STATE_IVR_SET_AUTH_LIST)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error In Channel:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	IvrSetAuthListState * state = reinterpret_cast<IvrSetAuthListState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->sub_state_);

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if(rDspReq->getStatus())
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
					tag, 
					simm->IVR_setAuthListHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_MINOR_NO_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_MINOR_NO_ERROR_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_ERROR_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_SUB_ACTION_MENU_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_SUB_ACTION_MENU_ERROR_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_SINGLE_OPT_SUCCESS_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_ALL_OPT_SUCCESS_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_SYSTEM_BUSY_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_CONFIRM_NUMBER_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_ERROR_START:
					return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, IVR_setAuthListHandler);

				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_CollectDigitStringAck(rCollect)
		{
			if(rCollect->getStatus() == 0x10)
			{
				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: CollectDigitString could not be successfully processed, Ack State = 0x%x", 
					rCollect->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if(playAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_MINOR_NO_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_MINOR_NO_ERROR_START:

				case IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_ERROR_START:

				case IvrSetAuthListState::STATE_ISAL_PLAY_SUB_ACTION_MENU_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_SUB_ACTION_MENU_ERROR_START:

				case IvrSetAuthListState::STATE_ISAL_PLAY_SINGLE_OPT_SUCCESS_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_ALL_OPT_SUCCESS_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_CONFIRM_NUMBER_START:
					{
						return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
							(void *)cr, IVR_setAuthListHandler);
					}
				case IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_ERROR_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_START:					
					{
						return simm->collectDigit( cr->_inSpan, cr->_inChan, MAX_COLLECT_DIGIT_LEN,
							rand()%5+1000, "*#", (void *)cr, IVR_setAuthListHandler);
					}
				case IvrSetAuthListState::STATE_ISAL_PLAY_SYSTEM_BUSY_START:
					{
						return OK ; 
					}
				default:
					return SK_NOT_HANDLED;
				}
			}
			else
				return SK_NOT_HANDLED;
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{
				if(state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_MINOR_NO_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);
					}

					int error = 0;
					if(( state->minorNo_ > '0') && ( state->minorNo_ < '7')) 
					{
						state->retry_ = 0;

						//查副号码序号对应的副号码;
						vector<MinorNumberAttr> minorList;
						int result = DataUsage::instance()->getAllMinorNumber(cr->_inCaller, minorList);
						if(result == E_OPERATOR_SUCCED)
						{
							int arrIndex = -1;
							for(int i=0; i<minorList.size(); i++)
							{
								if(minorList[i].sequenceNo == state->minorNo_-'0')
								{
									arrIndex = i;
									break ;
								}
							}
							if(arrIndex == -1)
								error = 1;
							else
								strncpy(state->minorNumber_, minorList[arrIndex].minorNumber, MAX_NUMB_LEN-1);
						}
						else
							error = 1;
					}
					else if( state->minorNo_ == '*')  /*返回上级菜单*/
						return simm->processIVR(cr);

					else
					{
						/*按键错误， 请重新输入 4330.wav*/
						int fileID[2] = {4330, -1};
						state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_MINOR_NO_ERROR_START;
						state->retry_ ++;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthListHandler);
					}

					if(error == 1)
					{
						/*输入的副号码序号不存在,play 4313.wav */
						int fileID[2] = {4313, -1};
						state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_MINOR_NO_ERROR_START;
						state->retry_++;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthListHandler);
					}

					/*请输入子菜单  play 4035.wav */
					int fileID[2] = {4035, -1};
					state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_START;
					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, IVR_setAuthListHandler);
				}
				else if( state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					if(( state->actionKey_ > '0') && ( state->actionKey_ < '5')) 
					{
						int fileID[2] = {-1, -1};
						if(( state->actionKey_ == '1') ||(state->actionKey_ == '3')) /*设置黑白名单*/
						{
							fileID[0] = 4013;
							state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_START;
						}
						else if((state->actionKey_ == '2') ||(state->actionKey_ == '4')) /*解除黑白名单*/
						{
							fileID[0] = 4040;
							if(state->actionKey_ == '4')
								fileID[0] = 4036;
							state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_SUB_ACTION_MENU_START;
						}

						state->retry_ = 0;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthListHandler);
					}
					else if( state->actionKey_ == '*')
						return simm->processIVR(cr);
					else
					{
						/*按键错误， 请重新输入 4330.wav*/
						int fileID[2] = {4330, -1};
						state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_ERROR_START;
						state->retry_++;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthListHandler);
					}
				}
				else if( state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_START)
				{
					//zuosai modify 20110210 for bug
					char endKey = 0;
					int len = strlen(state->targetNumber_);
					if(len == 0 )
						return OK;
					endKey = state->targetNumber_[len-1];

					if(endKey == '#') 
						state->targetNumber_[len-1] = '\0';
					else if (endKey == '*')
						return simm->processIVR(cr);
					else
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);
					//
					//zuosai modify 20110210 for bug end	
					if(simm->isValidNumber_black(state->targetNumber_) != 0 )
					{
						int result = 0;
						int preResult = 0;
						int fileID[24] ;
						for(int cnt = 0; cnt < 24; cnt ++)
							fileID[cnt] = -1;

						if(state->actionKey_ == '1') //添加黑名单
						{
							fileID[0] = 4343;
							int len = simm->phonNum2voiceList(state->targetNumber_, &fileID[1]);
							fileID[len+1] = 4345 ;
						}
						if(state->actionKey_ == '3') //添加白名单
						{
							fileID[0] = 4432;
							int len = simm->phonNum2voiceList(state->targetNumber_, &fileID[1]);
							fileID[len+1] = 4345 ;
						}
						else if(state->actionKey_ == '2') //解除黑名单
						{
							fileID[0] = 4344;
							int len =  simm->phonNum2voiceList(state->targetNumber_, &fileID[1]);
							fileID[len+1] = 4345 ;
						}
						else if(state->actionKey_ == '4') //解除白名单
						{
							fileID[0] = 4433;
							int len =  simm->phonNum2voiceList(state->targetNumber_, &fileID[1]);
							fileID[len+1] = 4345 ;
						}

						state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_CONFIRM_NUMBER_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthListHandler);

					}
					else /*添加的号码有误, 请核实后再拨*/
					{
						int fileID[2] = {4016, -1};
						state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_ERROR_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthListHandler);
					}
				}
				else if( state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_SUB_ACTION_MENU_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					if((state->subActionKey_ != '1') && (state->subActionKey_ != '2')) //键输入错误
					{
						/*按键错误， 请重新输入 4330.wav*/
						int fileID[2] = {4330, -1};
						state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_SUB_ACTION_MENU_ERROR_START;
						state->retry_++;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthListHandler);
					}

					int fileID[3] = {-1, -1, -1};
					state->retry_ = 0;
					if(( state ->actionKey_ == '2') ||(state ->actionKey_ == '4'))				/*解除黑名单限制*/
					{
						if( state->subActionKey_  == '1')		/*解除单个限制*/
						{
							fileID[0] = 4038;					/*请输入您要解除的号码按#号键结束，按*号键返回*/
							state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_START;
						}
						if( state->subActionKey_  == '2')		/*解除全部*/
						{
							int result = 0;
							if(state ->actionKey_ == '2')
								result = DataUsage::instance()->releaseALLBlackList(state->minorNumber_);
							else
								result = DataUsage::instance()->releaseALLWhiteList(state->minorNumber_);
							if( result != E_OPERATOR_SUCCED)
							{
								if (result == E_ADD_RELEASE_ERROR)
								{
									if(state ->actionKey_ == '2')
										fileID[0] = 4041;               //已解除所有黑名单的呼入限制
									else
										fileID[0] = 4039;               //已解除所有白名单的呼入限制

									fileID[1] = 4360;               //继续操按1，返回*
									state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_ALL_OPT_SUCCESS_START;
								}
								else
								{
									fileID[0] = 4329;               //系统忙..
									state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_SYSTEM_BUSY_START;
								}
							}
							else
							{
								if(state ->actionKey_ == '2')
									fileID[0] = 4041;               //已解除所有黑名单的呼入限制
								else
									fileID[0] = 4039;               //已解除所有白名单的呼入限制

								fileID[1] = 4360;               //继续操按1，返回*							
								state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_ALL_OPT_SUCCESS_START;
							}
						}
					}

					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, IVR_setAuthListHandler);
				}
				else if( state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_SINGLE_OPT_SUCCESS_START)
				{
					if(state->sequelKey_ == '*')
					{
						return simm->processIvrSetAuthList(cr);
					}
					else if(state->sequelKey_ == '1')
					{
						memset(state->targetNumber_ , 0, sizeof(state->targetNumber_)) ;

						int fileID[2] = {-1, -1};
						if(( state->actionKey_ == '1') || ( state->actionKey_ == '3')) /*设置黑白名单*/
							fileID[0] = 4013;
						if(( state->actionKey_ == '2')||(state->actionKey_ == '4')) /*解除黑白名单*/
							fileID[0] = 4038;

						state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthListHandler);
					}
					else
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
					}
				}
				//.s add by whc 20100127
				else if(state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_ALL_OPT_SUCCESS_START)
				{
					if(state->sequelKey_ == '*')
						return simm->processIVR(cr);
					else if(state->sequelKey_ == '1')
						return simm->processIvrSetAuthList(cr);
					else
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				}				
				//.e add by whc 20100127
				else if(state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_CONFIRM_NUMBER_START)
				{
					int fileID[2] = {-1, -1};
					int result = 0;
					int preResult = 0;

					//确认号码正确
					if(state->confirmKey_ == '1')
					{
						if(state->actionKey_ == '1')  //添加黑名单
						{
							result = DataUsage::instance()->addNumberToBlackList(state->minorNumber_, state->targetNumber_);
							if (result == E_ADD_RELEASE_ERROR)
							{
								preResult = DataUsage::instance()->releaseALLWhiteList(state->minorNumber_);
								result = DataUsage::instance()->addNumberToBlackList(state->minorNumber_, state->targetNumber_);
							}
							fileID[0] = 4018;
						}
						else if(state->actionKey_ == '3') //添加白名单
						{
							result = DataUsage::instance()->addNumberToWhiteList(state->minorNumber_, state->targetNumber_);
							if (result == E_ADD_RELEASE_ERROR)
							{
								preResult = DataUsage::instance()->releaseALLBlackList(state->minorNumber_);
								result = DataUsage::instance()->addNumberToWhiteList(state->minorNumber_, state->targetNumber_);
							}
							fileID[0] = 4018;
						}
						else if(state->actionKey_ == '2') //解除黑名单
						{
							result = DataUsage::instance()->releaseNumberFromBlackList(state->minorNumber_, state->targetNumber_);
							if (result == E_ADD_RELEASE_ERROR) 
							{
								result = E_OPERATOR_SUCCED;
							}
							fileID[0] = 4315;
						}
						else if(state->actionKey_ == '4') //解除白名单
						{
							result = DataUsage::instance()->releaseNumberFromWhiteList(state->minorNumber_, state->targetNumber_);
							if (result == E_ADD_RELEASE_ERROR) 
							{
								result = E_OPERATOR_SUCCED;
							}
							fileID[0] = 4315;
						}
					}
					//重新输入号码
					else if(state->confirmKey_ == '0')
					{
						state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_START;
						if((state->actionKey_ == '1') ||(state->actionKey_ == '3'))
							fileID[0] = 4013;
						if((state->actionKey_ == '2') ||(state->actionKey_ == '4'))
							fileID[0] = 4038;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthListHandler);							                            					
					}
					else if(state->confirmKey_ == '*')
						return simm->processIvrSetAuthList(cr);
					else
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

					if(result == E_OPERATOR_SUCCED && preResult == E_OPERATOR_SUCCED) 
					{
						state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_SINGLE_OPT_SUCCESS_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthListHandler);
					}
					else //系统忙， 请稍候拨打
					{
						int fileID[2] = {4016, -1};
						state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_SYSTEM_BUSY_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthListHandler);
					}
				}
			}
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			if( rCpe->getEvent() == RecvDtmf )
			{
				char buf[256];
				memset(buf, 0, sizeof(buf));
				simm->collectDigitResult( (SKC_Message *)rCpe, buf );

				if(strlen(buf) < 1)
					return OK;
				state->crcPlayCount_ = 0;

				if(state->userTimerId_ != 0)
				{
					simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setAuthListHandler) ;
					state->userTimerId_ = 0;
				}

				if( state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_MINOR_NO_START
					|| state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_MINOR_NO_ERROR_START)
				{
					state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_MINOR_NO_START;
					state->minorNo_ = buf[0];
				}
				else if( state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_START
					|| state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_ERROR_START)
				{
					state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_START;
					state->actionKey_ = buf[0];
				}
				else if(state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_START
					|| state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_ERROR_START)
				{
					state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_START;
					memset(state->targetNumber_, 0, MAX_NUMB_LEN);
					strncpy(state->targetNumber_, buf, MAX_NUMB_LEN-1);
				}
				else if(state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_SUB_ACTION_MENU_START
					|| state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_SUB_ACTION_MENU_ERROR_START)
				{
					state->sub_state_ = IvrSetAuthListState::STATE_ISAL_PLAY_SUB_ACTION_MENU_START;
					state->subActionKey_ = buf[0];
				}
				else if(state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_SINGLE_OPT_SUCCESS_START)
				{
					state->sequelKey_ = buf[0];
				}
				else if(state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_CONFIRM_NUMBER_START)
				{
					state->confirmKey_ = buf[0];
				}
				//.s add by whc fo the all opt 20100127
				else if(state->sub_state_ == IvrSetAuthListState::STATE_ISAL_PLAY_ALL_OPT_SUCCESS_START)
				{
					state->sequelKey_ = buf[0];
				}		
				//.e add by whc fo the all opt 20100127	
				else
					return OK;

				int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
				return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, (void *)cr, IVR_setAuthListHandler);
			}
			else if(rCpe->getEvent() == FilePlayOK)
			{
				switch(state->sub_state_)
				{
				case IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_MINOR_NO_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_MINOR_NO_ERROR_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_ERROR_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_SUB_ACTION_MENU_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_ERROR_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_START:
				case IvrSetAuthListState::STATE_ISAL_PLAY_SINGLE_OPT_SUCCESS_START:
					{
						/* Send Timer */
						state->userTimerId_ = getUserTimerId();
						return simm->startUserTimer(state->userTimerId_,CRC_PLAY_INTERVAL, (void *)cr, IVR_setAuthListHandler);
					}
				case IvrSetAuthListState::STATE_ISAL_PLAY_ALL_OPT_SUCCESS_START:
					return simm->processIVR(cr);					
				case IvrSetAuthListState::STATE_ISAL_PLAY_SYSTEM_BUSY_START:
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				default:
					return OK;
				}

				return OK;
			}
			else
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
		}
		CASEC_UserTimerAck(varNm) /* 定时器返回消息 */
		{
			if(state->userTimerId_ != 0)
			{
				simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setAuthListHandler) ;
				state->userTimerId_ = 0;
			}
			if (varNm->getStatus() == 0x10)
			{
				int fileID[2] = {-1,-1};
				switch(state->sub_state_)
				{
				case IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_MINOR_NO_START:
					{
						fileID[0] = 4045;
						break;
					}
					//zuosai modify 20110210 for bug
				case IvrSetAuthListState::STATE_ISAL_PLAY_MINOR_NO_ERROR_START:
					{
						fileID[0] = 4313;
						break;
					}
				case IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_ERROR_START:
					{
						fileID[0] = 4330;
						break;
					}
				case IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_ERROR_START:
					{
						fileID[0] = 4016;
						break;
					}
					//zuosai modify 20110210 for bug end

				case IvrSetAuthListState::STATE_ISAL_PLAY_ACTION_MENU_START:
					{
						fileID[0] = 4035;
						break;
					}
				case IvrSetAuthListState::STATE_ISAL_PLAY_SUB_ACTION_MENU_START:
					{
						fileID[0] = 4040;
						break;
					}

				case IvrSetAuthListState::STATE_ISAL_PLAY_INPUT_NUMBER_START:
					{
						fileID[0] = 4013;
						break;
					}
				case IvrSetAuthListState::STATE_ISAL_PLAY_SINGLE_OPT_SUCCESS_START:
					{

						if (state->actionKey_ == '1') /* 填加黑名单 */
						{
							fileID[0] = 4018;
						}
						else if (state->actionKey_ == '2') /* 解除黑名单 */
						{
							fileID[0] = 4315;
						}
						break;
					}
				default:
					break;
				}
				if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

				state->crcPlayCount_++;
				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
					( void *)cr, IVR_setAuthListHandler);
			}
			return OK;
		}
		CASEC_CancelUserTimerAck(varNm)
		{
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) In Channel(0x%x,0x%x), CancelUserTimer, status = %d",
				cr->_callID, cr->_inSpan, cr->_inChan, varNm->getStatus());
			return OK;
		}
		CASEC_default
		{
			return defaultHandler( evt, tag);
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;


}


int SIMMApp::IVR_setAuthTimeHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if(cr->_state == 0)
		return -1;

	if(cr->_state->state_ != State::STATE_IVR_SET_AUTH_TIME)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error in:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	IvrSetAuthTimeState * state = reinterpret_cast<IvrSetAuthTimeState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->sub_state_);

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if(rDspReq->getStatus())
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
					tag, 
					simm->IVR_setAuthTimeHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_INPUT_MINOR_NO_START:
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_MINOR_NO_ERROR_START:
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_START:
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_SET_RESULT_START:
					{
						return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, IVR_setAuthTimeHandler);
					}
				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{
				if(state->sub_state_ == IvrSetAuthTimeState::STATE_ISAT_PLAY_INPUT_MINOR_NO_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					int error = 0;
					if(( state->minorNo_ > '0') && ( state->minorNo_ < '4')) 
					{
						state->retry_ = 0;

						//查副号码序号对应的副号码;
						vector<MinorNumberAttr> minorList;
						int result = DataUsage::instance()->getAllMinorNumber(cr->_inCaller, minorList);
						if(result == E_OPERATOR_SUCCED)
						{
							int arrIndex = -1;
							for(int i=0; i<minorList.size(); i++)
							{
								if(minorList[i].sequenceNo == state->minorNo_-'0')
								{
									arrIndex = i;
									break ;
								}
							}
							if(arrIndex == -1) /*副号码序号有误*/
								error = 1;
							else
								strncpy(state->minorNumber_, minorList[arrIndex].minorNumber, MAX_NUMB_LEN-1);
						}
						else
							error = 1;
					}
					else if( state->minorNo_ == '*')  /*返回上级菜单*/
						return simm->processIVR(cr);
					else
					{
						/*按键错误， 请重新输入 4330.wav*/
						int fileID[2] = {4330, -1};
						state->sub_state_ = IvrSetAuthTimeState::STATE_ISAT_PLAY_MINOR_NO_ERROR_START;
						state->retry_ ++;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthTimeHandler);
					}

					if(error == 1)
					{
						/*输入的副号码序号不存在,play 4313.wav */
						int fileID[2] = {4313, -1};
						state->sub_state_ = IvrSetAuthTimeState::STATE_ISAT_PLAY_MINOR_NO_ERROR_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthTimeHandler);
					}

					/*play 40421.wav ~ 404210.wav */
					int fileID[11] = {40421,40422,40423, 40424, 40425, 40426, 40427, 40428, 40429, 404210, -1};
					state->sub_state_ = IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_START;
					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, IVR_setAuthTimeHandler);
				}
				else if(state->sub_state_ == IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					if((state->policyID_ >= '1') && (state->policyID_ <= '9'))
					{
						int fileID[4] = {-1, -1, -1, -1};
						state->retry_ = 0 ;
						int result = DataUsage::instance()->setStrategyID(state->minorNumber_, state->policyID_-'0');
						if(result == E_OPERATOR_SUCCED)
						{
							fileID[0] = 4043;		/*设置成功，您设置的呼入时间限制为*/
							fileID[1] = 40421 + (state->policyID_ - '1');
							fileID[2] = 4360;
							state->sub_state_ = IvrSetAuthTimeState::STATE_ISAT_PLAY_SET_RESULT_START;
						}
						else
						{
							fileID[0] = 4329;		/*系统忙....*/
							state->sub_state_ = IvrSetAuthTimeState::STATE_ISAT_PLAY_SYSTEM_BUSY_START;
						}

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthTimeHandler);
					}
					else if( state->policyID_ == '*')
					{
						return simm->processIVR(cr);
					}
					else
					{
						/*按键错误， 请重新输入 4330.wav*/
						int fileID[2] = {4330, -1};
						state->sub_state_ = IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_ERROR_START;
						state->retry_ ++ ;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setAuthTimeHandler);
					}
				}
				else if( state->sub_state_ == IvrSetAuthTimeState::STATE_ISAT_PLAY_SET_RESULT_START )
				{
					if(state->sequelKey_ == '1')
						return simm->processIvrSetAuthTime(cr);
					else if(state->sequelKey_ == '*')
						return simm->processIVR(cr);
					else 
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				}
				else 
					return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if(playAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_INPUT_MINOR_NO_START:
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_MINOR_NO_ERROR_START:
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_START:
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_ERROR_START:
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_SET_RESULT_START:
					return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
						(void *)cr, IVR_setAuthTimeHandler);
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_SYSTEM_BUSY_START:
					{
						return OK;
					}
				default:
					return SK_NOT_HANDLED;
				}
			}
			else
				return SK_NOT_HANDLED;
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			if( rCpe->getEvent() == RecvDtmf )
			{
				char buf[256];
				memset(buf, 0, sizeof(buf));
				simm->collectDigitResult( (SKC_Message *)rCpe, buf );

				if( strlen(buf) < 1)
					return OK;
				state->crcPlayCount_ = 0;

				if(state->userTimerId_ != 0)
				{
					simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setAuthTimeHandler) ;
					state->userTimerId_ = 0;
				}

				if(( state->sub_state_ == IvrSetAuthTimeState::STATE_ISAT_PLAY_INPUT_MINOR_NO_START)||
					(state->sub_state_ == IvrSetAuthTimeState::STATE_ISAT_PLAY_MINOR_NO_ERROR_START))
				{
					state->sub_state_ = IvrSetAuthTimeState::STATE_ISAT_PLAY_INPUT_MINOR_NO_START;
					state->minorNo_ = buf[0];
				}
				else if(( state->sub_state_ == IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_START) ||
					(state->sub_state_ == IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_ERROR_START))
				{
					state->sub_state_ = IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_START;
					state->policyID_ = buf[0];
				}
				else if(state->sub_state_ == IvrSetAuthTimeState::STATE_ISAT_PLAY_SET_RESULT_START)
				{
					state->sequelKey_ = buf[0];
				}
				else
				{
					return OK;
				}

				int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
				return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, (void *)cr, IVR_setAuthTimeHandler);
			}
			else if( rCpe->getEvent() == FilePlayOK )
			{
				switch(state->sub_state_)
				{
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_INPUT_MINOR_NO_START:
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_MINOR_NO_ERROR_START:
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_START:
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_ERROR_START:
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_SET_RESULT_START:
					{
						state->userTimerId_ = getUserTimerId();
						return simm->startUserTimer(state->userTimerId_,CRC_PLAY_INTERVAL, (void *)cr, IVR_setAuthTimeHandler);
					}
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_SYSTEM_BUSY_START:
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
					}
				default:
					return OK;
				}
			}
			else
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
		}
		CASEC_UserTimerAck(varNm) /* 定时器返回消息 */
		{

			if(state->userTimerId_ != 0)
			{
				simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setAuthTimeHandler) ;
				state->userTimerId_ = 0;
			}
			if (varNm->getStatus() == 0x10)
			{
				int fileID[2] = {-1,-1};
				switch(state->sub_state_)
				{
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_INPUT_MINOR_NO_START:
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_MINOR_NO_ERROR_START:
					{
						fileID[0] = 4045;
						break;
					}
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_START:
					{
						fileID[0] = 4042;
						break;
					}
				case IvrSetAuthTimeState::STATE_ISAT_PLAY_POLICY_MENU_ERROR_START:
					{
						fileID[0] = 4330;
						break;
					}
				default:
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				}

				if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

				state->crcPlayCount_++;
				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
					( void *)cr, IVR_setAuthTimeHandler);
			}

			return OK;
		}
		CASEC_CancelUserTimerAck(varNm)
		{
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) In Channel(0x%x,0x%x), CancelUserTimer, status = %d",
				cr->_callID, cr->_inSpan, cr->_inChan, varNm->getStatus());			
			return OK;
		}
		CASEC_default
		{
			return defaultHandler( evt, tag);
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;
}


int SIMMApp::IVR_queryMinorHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
	{
		return -1 ;
	}

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if( cr->_state == 0 )
	{
		return -1;
	}

	if(cr->_state->state_ != State::STATE_IVR_QUERY_MINOR)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error in:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	IvrQueryMinorState * state = reinterpret_cast<IvrQueryMinorState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->sub_state_);

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp,  "INFO: CallID(%d) Enter Function:IVR_queryMinorHandler() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);
	}	

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if(rDspReq->getStatus())
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
					tag, 
					simm->IVR_queryMinorHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrQueryMinorState::STATE_IQM_RECV_ACTION_DTMF:
					{
						return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, IVR_queryMinorHandler);
					}
				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if( playAck->getStatus() == 0x10 )
			{
				switch(state->sub_state_)
				{
				case IvrQueryMinorState::STATE_IQM_PLAY_NUMBER_START:
				case IvrQueryMinorState::STATE_IQM_PLAY_MINOR_NO_ERROR_START:
					{
						if( state->sub_state_ == IvrQueryMinorState::STATE_IQM_PLAY_NUMBER_START )
						{
							state->sub_state_ = IvrQueryMinorState::STATE_IQM_RECV_ACTION_DTMF;
						}
						return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP + 5, (void *)cr, IVR_queryMinorHandler);
					}
				case IvrQueryMinorState::STATE_IQM_PLAY_NOT_MINOR_START:
				case IvrQueryMinorState::STATE_IQM_PLAY_SYSTEM_BUSY_START:
					{
						return OK;
					}

					/*has Sequel Data want to play used playFileQueue*/
				case IvrQueryMinorState::STATE_IQM_RECV_ACTION_DTMF: 
					{
						return OK;
					}
				default:
					{
						return SK_NOT_HANDLED;
					}
				}
			}
			else
			{
				return SK_NOT_HANDLED;
			}
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{
				if( state->sub_state_ == IvrQueryMinorState::STATE_IQM_RECV_ACTION_DTMF )
				{
					if( state->retry_ >= MAX_RETRY_COUNT )
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);
					}

					state->retry_ = 0;

					if( state->actionKey_ == '*') 
					{
						return simm->processIVR(cr);
					}
					else if( state->actionKey_ == '2')  /*重复收听*/
					{
						return simm->processIvrQueryMinor(cr);
					}
					else if( state->actionKey_ == '1')  /*通过短信发送副号码信息*/
					{
						if( simm->_sendSMFlag )
						{							
							//int ret = DialNotifyLibWrapper::instance()->submitGeneralMsg("95096", cr->_inCaller, state->minorMsg_);
							int ret = DialNotifyLibWrapper::instance()->submitGeneralMsg( _accessCode, cr->_inCaller, state->minorMsg_ );
							if( ret == -1 )
							{
								LogFileManager::getInstance()->write(Brief, "ERROR : submitGeneralMsg Failed...");
							}
							else
							{
								LogFileManager::getInstance()->write(Debug, "INFO : CallID(%d) submitGeneralMsg Success...",cr->_callID);
							}
						}

						return simm->processIVR(cr);
					}
					else
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);
					}
				}
				else if( state->sub_state_ == IvrQueryMinorState::STATE_IQM_PLAY_MINOR_NO_ERROR_START )
				{
					if( state->retry_ >= MAX_RETRY_COUNT )
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);
					}

					state->retry_ = 0;

					if( state->actionKey_ == '*') 
					{
						return simm->processIVR(cr);
					}
					else if( state->actionKey_ == '1')  /*重复收听*/
					{
						return simm->processIvrQueryMinor(cr);
					}
					else 
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);
					}
				}
			}
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			if( rCpe->getEvent() == RecvDtmf )
			{
				char buf[256];
				memset(buf, 0, sizeof(buf));
				simm->collectDigitResult( (SKC_Message *)rCpe, buf );

				if( strlen(buf) < 1)
				{
					return OK;
				}

				if(state->userTimerId_ !=0)
				{
					simm->cancelUserTimer(state->userTimerId_,(void *)cr,IVR_queryMinorHandler);
					state->userTimerId_ = 0;
				}

				if( state->sub_state_ == IvrQueryMinorState::STATE_IQM_RECV_ACTION_DTMF)
				{
					state->actionKey_ = buf[0];

					int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
					return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, (void *)cr, IVR_queryMinorHandler);
				}

				return OK;
			}
			else if( rCpe->getEvent() == FilePlayOK )
			{
				if( state->sub_state_ == IvrQueryMinorState::STATE_IQM_RECV_ACTION_DTMF)
				{
					/* Send Timer */
					state->userTimerId_ = getUserTimerId();
					return simm->startUserTimer(state->userTimerId_, CRC_PLAY_INTERVAL, (void *)cr, IVR_queryMinorHandler);
				}
				else
				{
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				}

				return OK;
			}
			else if(rCpe->getEvent() == FilePlayStart)
			{
				if( ( state->hasSequelData_ != 0 ) && 
					( state->data_.elements_ > 0 ) )
				{
					int ret = simm->playFileQueue(cr->_inSpan, cr->_inChan, Dsp2Slot, state->data_.dataArray_, (void *)cr, IVR_queryMinorHandler);
					if(ret != OK)
					{
						LogFileManager::getInstance()->write(Brief, 
							"ERROR : In Channel(0x%x:0x%x)  playFileQueue Failed, %s\n",
							cr->_inSpan, cr->_inChan,sk_errorText(ret));
					}
					return ret ;
				}
				return OK;
			}
			else
			{
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
			}
		}
		CASEC_UserTimerAck(varNm) /* 定时器返回消息 */
		{
			if(state->userTimerId_ !=0)
			{
				simm->cancelUserTimer(state->userTimerId_,(void *)cr,IVR_queryMinorHandler);
				state->userTimerId_ = 0;
			}
			if (varNm->getStatus() == 0x10)
			{
				if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

				int fileID[2] = {-1,-1};
				if(state->pressKey_ == false)
					state->crcPlayCount_++;

				switch(state->sub_state_)
				{
				case IvrQueryMinorState::STATE_IQM_RECV_ACTION_DTMF:
					{
						fileID[0] = 4361;
						break;
					}
				default:
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				}

				if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

				state->crcPlayCount_++;
				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
					( void *)cr, IVR_queryMinorHandler);
			}
			return OK;
		}
		CASEC_CancelUserTimerAck(varNm)
		{
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) In Channel(0x%x,0x%x), CancelUserTimer, status = %d",
				cr->_callID, cr->_inSpan, cr->_inChan, varNm->getStatus());
			return OK;
		}
		CASEC_default
		{
			return defaultHandler( evt, tag);
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;
}

int SIMMApp::IVR_setMinorNoHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if(cr->_state == 0)
		return -1;

	if(cr->_state->state_ != State::STATE_IVR_SET_MINOR_SEQUENCE)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error in:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	IvrSetMinorSequenceState * state = reinterpret_cast<IvrSetMinorSequenceState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->sub_state_);

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp, "INFO: CallID(%d) Enter Function:IVR_setMinorNoHandler() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);
	}		

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if(rDspReq->getStatus())
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
					tag, 
					simm->IVR_setMinorNoHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
#if 0
				// 虚拟副号码时 直接返回 OK
				return OK;
				// 
#endif

				switch(state->sub_state_)
				{
				case IvrSetMinorSequenceState::STATE_ISMS_PLAY_MULIT_MINOR_START:
				case IvrSetMinorSequenceState::STATE_ISMS_PLAY_SEQUENCE_ERROR_START:
					{
						return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, IVR_setMinorNoHandler);
					}
				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_CollectDigitStringAck(rCollect)
		{
			if(rCollect->getStatus() == 0x10)
			{
				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: CollectDigitString could not be successfully processed, Ack State = 0x%x", 
					rCollect->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{

				if( state->sub_state_ == IvrSetMinorSequenceState::STATE_ISMS_PLAY_SEQUENCE_ERROR_START || 
					state->sub_state_ == IvrSetMinorSequenceState::STATE_ISMS_PLAY_MULIT_MINOR_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);
					}

					int len = strlen(state->sequenceNo_);
					if(len < 1 )
					{
						return (OK);
					}

					char endKey = state->sequenceNo_[len - 1];
					if( endKey == '*')
					{
						return simm->processIVR(cr);
					}
					else if( endKey == '#')
					{
						state->sequenceNo_[len - 1] = 0;
					}

					if( isValidSequence(state->sequenceNo_) == 0)
					{
						/*您输入的副号码序号错误*/
						int fileID[3] = { 4318, 4331, -1};
						state->sub_state_ = IvrSetMinorSequenceState::STATE_ISMS_PLAY_SEQUENCE_ERROR_START;
						state->retry_ ++ ;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
							(void *)cr, IVR_setMinorNoHandler);
					}
					else
					{
						//Modify:2009.04.30
						if (strlen(state->sequenceNo_) != state->minorCount_)
						{
							/*您输入的副号码序号错误*/
							int fileID[3] = { 4318, 4331, -1 };
							state->sub_state_ = IvrSetMinorSequenceState::STATE_ISMS_PLAY_SEQUENCE_ERROR_START;
							state->retry_ ++ ;

							return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
								(void *)cr, IVR_setMinorNoHandler);
						}

						int fileID[2] = { -1, -1 };
						int result = DataUsage::instance()->setMinorIndex(cr->_inCaller, state->sequenceNo_);
						if( result == E_OPERATOR_SUCCED)
						{
							LogFileManager::getInstance()->write(Debug,"CallID(%d) setMinorIndex Succeed...",cr->_callID);
							fileID[0] = 4047;
						}

						else
						{
							LogFileManager::getInstance()->write(Debug,"CallID(%d) setMinorIndex Failed...",cr->_callID);
							fileID[0] = 4317;
						}

						int ret =simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
							(void *)cr, IVR_setMinorNoHandler);
						if( ret != OK )
						{
							LogFileManager::getInstance()->write(Brief, 
								"ERROR : In Channel(0x%x:0x%x)  playVoiceForID Failed, %s\n",
								cr->_inSpan, cr->_inChan,sk_errorText(ret));
						}

						state->retry_ = 0 ;
						state->sub_state_ = IvrSetMinorSequenceState::STATE_ISMS_PLAY_RESULT_START;
						return ret ;
					}
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: PlayFileStop could not be successfully processed, Ack State = 0x%x", 
					rPfs->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if(playAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetMinorSequenceState::STATE_ISMS_PLAY_MULIT_MINOR_START:
				case IvrSetMinorSequenceState::STATE_ISMS_PLAY_SEQUENCE_ERROR_START:
					{
						int maxDigist = 4;
						return simm->collectDigit( cr->_inSpan, cr->_inChan, maxDigist,
							rand()%5+1000, "*#", (void *)cr, IVR_setMinorNoHandler);
					}
				case IvrSetMinorSequenceState::STATE_ISMS_PLAY_SINGLE_MINOR_START:
				case IvrSetMinorSequenceState::STATE_ISMS_PLAY_SYSTEM_BUSY_START:
				case IvrSetMinorSequenceState::STATE_ISMS_PLAY_RESULT_START:
					return OK;
				default:
					return SK_NOT_HANDLED;
				}
			}
			else
				return SK_NOT_HANDLED;
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			if( rCpe->getEvent() == RecvDtmf )
			{
				char buf[256];
				memset(buf, 0, sizeof(buf));
				simm->collectDigitResult( (SKC_Message *)rCpe, buf );

				int len = strlen(buf);
				if( ( len < 1) || (len > 4))
					return OK;
				state->crcPlayCount_ = 0;
				if(state->userTimerId_ != 0)
				{
					simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setMinorNoHandler) ;
					state->userTimerId_ = 0;
				}

				if( state->sub_state_ == IvrSetMinorSequenceState::STATE_ISMS_PLAY_MULIT_MINOR_START
					||  state->sub_state_ == IvrSetMinorSequenceState::STATE_ISMS_PLAY_SEQUENCE_ERROR_START)
				{
					memset(state->sequenceNo_, 0, sizeof(state->sequenceNo_));
					strncpy(state->sequenceNo_, buf, 4);
				}

				int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
				return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, (void *)cr, IVR_setMinorNoHandler);
			}
			else if(rCpe->getEvent() == FilePlayOK)
			{
				if( (state->sub_state_ == IvrSetMinorSequenceState::STATE_ISMS_PLAY_SYSTEM_BUSY_START))
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				else if( state->sub_state_ == IvrSetMinorSequenceState::STATE_ISMS_PLAY_RESULT_START
					|| (state->sub_state_ == IvrSetMinorSequenceState::STATE_ISMS_PLAY_SINGLE_MINOR_START ))
					return simm->processIVR(cr);
				else if (state->sub_state_ == IvrSetMinorSequenceState::STATE_ISMS_PLAY_MULIT_MINOR_START)
				{
					/* Send Timer */
					state->userTimerId_ = getUserTimerId();
					return simm->startUserTimer(state->userTimerId_,CRC_PLAY_INTERVAL, (void *)cr, IVR_setMinorNoHandler);
				}
			}
			else if(rCpe->getEvent() == FilePlayStart)
			{

				if((state->sub_state_ == IvrSetMinorSequenceState::STATE_ISMS_PLAY_RESULT_START) ||
					state->sub_state_ == IvrSetMinorSequenceState::STATE_ISMS_PLAY_SEQUENCE_ERROR_START)
					return OK;

				if( ( state->hasSequelData_ != 0) 
					&& (state->data_.elements_ > 0))
				{
					int ret = simm->playFileQueue(cr->_inSpan, cr->_inChan, Dsp2Slot, state->data_.dataArray_,
						(void *)cr, IVR_setMinorNoHandler);
					if(ret != OK)
					{
						LogFileManager::getInstance()->write(Brief, 
							"ERROR : In Channel(0x%x:0x%x)  playFileQueue Failed, %s\n",
							cr->_inSpan, cr->_inChan,sk_errorText(ret));
					}

					state->hasSequelData_ = 0;
					state->data_.elements_ = 0;

					return ret ;
				}
				else 
					return OK;
			}
			else
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
		}
		CASEC_UserTimerAck(varNm) /* 定时器返回消息 */
		{
			if(state->userTimerId_ != 0)
			{
				simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setMinorNoHandler) ;
				state->userTimerId_ = 0;
			}
			if (varNm->getStatus() == 0x10)
			{
				if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

				state->crcPlayCount_++;

				switch(state->sub_state_)
				{
				case IvrSetMinorSequenceState::STATE_ISMS_PLAY_MULIT_MINOR_START:
					{
						return simm->processIvrSetMinorSequence(cr);
					}
				default:
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
					}
				}

			}
			return OK;
		}
		CASEC_CancelUserTimerAck(varNm)
		{
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) In Channel(0x%x,0x%x), CancelUserTimer, status = %d",
				cr->_callID, cr->_inSpan, cr->_inChan, varNm->getStatus());

			return OK;
		}
		CASEC_default
		{
			return defaultHandler( evt, tag);
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;
}


int SIMMApp::IVR_setMinorPowerHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if(cr->_state == 0)
		return -1;

	if(cr->_state->state_ != State::STATE_IVR_SET_MINOR_POWER)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error in:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	IvrSetMinorPowerState * state = reinterpret_cast<IvrSetMinorPowerState *>(cr->_state);
	SKC_MSG_SWITCH(msg) 
	{
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if(rDspReq->getStatus())
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
					tag, 
					simm->IVR_setMinorPowerHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_INPUT_MINOR_NO_START:
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_MINOR_NO_ERROR_START:
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_START:
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_ERROR_START:
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_RESULT:
					return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, IVR_setMinorPowerHandler);
				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if(playAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_INPUT_MINOR_NO_START:
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_MINOR_NO_ERROR_START:
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_START:
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_ERROR_START:
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_RESULT:
					{
						return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
							(void *)cr, IVR_setMinorPowerHandler);
					}
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_SYSTEM_BUSY_START:
					return OK;
				default:
					return SK_NOT_HANDLED;
				}
			}
			else
				return SK_NOT_HANDLED;
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{
				if( state->sub_state_ == IvrSetMinorPowerState::STATE_ISMP_PLAY_INPUT_MINOR_NO_START
					|| state->sub_state_ == IvrSetMinorPowerState::STATE_ISMP_PLAY_MINOR_NO_ERROR_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					int error = 0;
					if(( state->minorNo_ > '0') && ( state->minorNo_ < '4')) 
					{
						//查副号码序号对应的副号码;
						vector<MinorNumberAttr> minorList;
						int result = DataUsage::instance()->getAllMinorNumber(cr->_inCaller, minorList);
						if(result == E_OPERATOR_SUCCED)
						{
							int isfind = 0;
							for(int i=0; i<minorList.size(); i++ )
							{
								if(minorList[i].sequenceNo == state->minorNo_-'0')
								{
									strncpy(state->minorNumber_, minorList[i].minorNumber, MAX_NUMB_LEN-1);
									isfind = 1;
									break;
								}
							}
							if(isfind == 0) /*副号码序号有误*/
								error = 1;
						}
						else
							error = 1;
					}
					else if( state->minorNo_ == '*')  /*返回上级菜单*/
						return simm->processIVR(cr);
					else
					{
						/*您输入的副号码序号错误 4016.wav*/
						int fileID[2] = {4016, -1};
						state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_MINOR_NO_ERROR_START;
						state->retry_ ++;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setMinorPowerHandler);
					}

					if(error == 1)
					{
						/*输入的副号码序号不存在,play 4313.wav */
						int fileID[2] = {4313, -1};
						state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_MINOR_NO_ERROR_START;
						state->retry_ ++;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setMinorPowerHandler);
					}

					//---------------------------------------------------------------
					state->retry_ = 0;
					int fileID[6] = {-1, -1, -1, -1, -1, -1};
					error = 0;
					int result = DataUsage::instance()->getMinorNumberState( state->minorNumber_);
					state->logicState_ = result ;
					if( result == STATE_ACTIVE)	  //STATE_ACTIVE
					{
						if( state->minorNo_ == '6' )
						{								
							fileID[0] = 4441 ;		//您的虚拟副号码当前状态为开机
							fileID[1] = 4333 ;		//关机请按 3，*键返回
						}
						else
						{
							fileID[0] = 4323 ;		  //你的第
							fileID[1] = 1000 + (state->minorNo_-'0');
							fileID[2] = 4213 ;
							fileID[3] = 4319 ;		  //副号码当前状态为开机
							fileID[4] = 4333 ;		  //关机请按 3，*键返回
						}
						state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_START;
					}
					else if(result == STATE_OFFLINE) //STATE_OFF
					{
						if( state->minorNo_ == '6' )
						{								
							fileID[0] = 4442 ; //您的虚拟副号码当前状态为关机
							fileID[1] = 4332 ; //开机请按 2，*键返回
						}
						else
						{
							fileID[0] = 4323 ;		//你的第
							fileID[1] = 1000 + (state->minorNo_-'0') ;
							fileID[2] = 4213 ;
							fileID[3] = 4320 ;	    //副号码当前状态为关机
							fileID[4] = 4332 ;      //开机请按 2，*键返回
						}
						state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_START;
					}
					else
					{
						fileID[0] = 4329 ; 
						state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_SYSTEM_BUSY_START;
					}

					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, IVR_setMinorPowerHandler);
				}
				else if((state->sub_state_ == IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_START)||
					(state->sub_state_ == IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_ERROR_START))
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					int error = 0;
					int fileID[7] = {-1, -1, -1, -1, -1, -1, -1};

					//modify 2009.11.4
					if( state->actionKey_ == '2')		/* 开机 */
					{
						state->retry_ = 0;
						ResultDataUsage result = DataUsage::instance()->setMinorNumberState( state->minorNumber_,
							STATE_ACTIVE);
						if( result == E_OPERATOR_SUCCED)
						{
							if( state->minorNo_ == '6' )
							{
								fileID[0] = 4334 ; //设置成功
								fileID[1] = 4441 ; //您的虚拟副号码当前状态为开机
								fileID[2] = 4360 ; //继续操作请按1，按*键返回主菜单
							}
							else
							{
								fileID[0] = 4334 ; //设置成功
								fileID[1] = 4323 ; //你的第
								fileID[2] = 1000 + (state->minorNo_-'0') ;
								fileID[3] = 4213 ;
								fileID[4] = 4319 ; //副号码当前状态为开机
								fileID[5] = 4360 ; //继续操作请按1，按*键返回主菜单
							}
							state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_RESULT;
						}
						else 
							error = 1;
					}
					else if(state->actionKey_ == '3')    /*关机*/
					{
						state->retry_ = 0;
						ResultDataUsage result = DataUsage::instance()->setMinorNumberState( state->minorNumber_, 
							STATE_OFFLINE);
						if( result == E_OPERATOR_SUCCED)
						{
							if( state->minorNo_ == '6' )
							{
								fileID[0] = 4334 ; //设置成功
								fileID[1] = 4442 ; //您的虚拟副号码当前状态为关机
								fileID[2] = 4360 ; //继续操作请按1，按*键返回主菜单
							}
							else
							{
								fileID[0] = 4334 ; //设置成功
								fileID[1] = 4323 ; //你的第
								fileID[2] = 1000 + (state->minorNo_-'0') ;
								fileID[3] = 4213 ;
								fileID[4] = 4320 ; //副号码当前状态为关机
								fileID[5] = 4360 ; //继续操作请按1，按*键返回主菜单
							}
							state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_RESULT;
						}
						else 
							error = 1;
					}
					else if( state->actionKey_ == '*')	/*返回*/
					{
						return simm->processIvrSetMinorPower(cr);
					}
					else								/*按键错误， 请重新输入*/							
					{
						fileID[0] = 4330 ; 
						state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_ERROR_START;
						state->retry_ ++;
					}
					if( error == 1)
					{
						fileID[0] = 4329 ; 
						state->sub_state_ = IvrSetMinorPowerState::STATE_ISMP_PLAY_SYSTEM_BUSY_START;
					}
					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, IVR_setMinorPowerHandler);
				}
				else if(state->sub_state_ == IvrSetMinorPowerState::STATE_ISMP_PLAY_RESULT)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					//modify 2009.11.4
					if( state->subActionKey_ == '1')		
						return simm->processIvrSetMinorPower(cr);
					else if( state->subActionKey_ == '*')		
						return simm->processIVR(cr);
					else 
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);
				}
				else 
					return SK_NOT_HANDLED; 
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rPfs->getStatus());

				return OK;
			}
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			if( rCpe->getEvent() == RecvDtmf )
			{
				char buf[256];
				memset(buf, 0, sizeof(buf));
				simm->collectDigitResult( (SKC_Message *)rCpe, buf );

				if(strlen(buf) < 1)
					return OK;
				state->crcPlayCount_ = 0;
				if(state->userTimerId_ != 0)
				{
					simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setMinorPowerHandler) ;
					state->userTimerId_ = 0;
				}

				if( state->sub_state_ == IvrSetMinorPowerState::STATE_ISMP_PLAY_INPUT_MINOR_NO_START
					|| state->sub_state_ == IvrSetMinorPowerState::STATE_ISMP_PLAY_MINOR_NO_ERROR_START)
				{
					state->minorNo_  = buf[0];
				}
				else if ( state->sub_state_ == IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_START
					|| state->sub_state_ == IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_ERROR_START)
				{
					state->actionKey_  = buf[0];
				}
				else if(state->sub_state_ == IvrSetMinorPowerState::STATE_ISMP_PLAY_RESULT)
				{
					state->subActionKey_  = buf[0];
				}

				int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
				return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, (void *)cr, IVR_setMinorPowerHandler);
			}
			else if( rCpe->getEvent() == FilePlayOK )
			{
				switch(state->sub_state_)
				{
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_INPUT_MINOR_NO_START:
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_MINOR_NO_ERROR_START:
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_START:
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_ERROR_START:
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_RESULT://zuosai add 20110210for bug
					{
						state->userTimerId_ = getUserTimerId();
						return simm->startUserTimer(state->userTimerId_,CRC_PLAY_INTERVAL, (void *)cr, IVR_setMinorPowerHandler);
					}
					/*zuosai delete 20110210 for bug
					case IvrSetMinorPowerState::STATE_ISMP_PLAY_RESULT:
					{
					return simm->processIVR(cr);
					}*/
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_SYSTEM_BUSY_START:
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
					}
				default:
					return OK;
				}
			}
			else
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
		}
		CASEC_UserTimerAck(varNm) /* 定时器返回消息 */
		{
			if(state->userTimerId_ != 0)
			{
				simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setMinorPowerHandler) ;
				state->userTimerId_ = 0;
			}
			if (varNm->getStatus() == 0x10)
			{
				int fileID[2] = {-1,-1};
				switch(state->sub_state_)
				{
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_INPUT_MINOR_NO_START:
					{
						fileID[0] = 4045;
						break;
					}
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_MINOR_NO_ERROR_START:
					{
						fileID[0] = 4313;
						break;
					}
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_START:
					{
						if(state->logicState_ == STATE_ACTIVE)
							fileID[0] = 4333;
						if(state->logicState_ == STATE_OFFLINE)
							fileID[0] = 4332;

						break;
					}
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_ACTION_ERROR_START:
					{
						fileID[0] = 4330;
						break ;
					}
					//zuosai add 20110210 for bug
				case IvrSetMinorPowerState::STATE_ISMP_PLAY_RESULT:
					{
						fileID[0] = 4360;
						break;
					}
					//zuosai add 20110210 for bug end
				default:
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				}

				if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

				state->crcPlayCount_++;
				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
					( void *)cr, IVR_setMinorPowerHandler);
			}
			return OK;
		}
		CASEC_CancelUserTimerAck(varNm)
		{
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) In Channel(0x%x,0x%x), CancelUserTimer, status = %d",
				cr->_callID, cr->_inSpan, cr->_inChan, varNm->getStatus());

			return OK;
		}
		CASEC_default
		{
			return defaultHandler( evt, tag);
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;
}
//


int SIMMApp::IVR_setSMSAuthTimeHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if(cr->_state == 0)
		return -1;

	if(cr->_state->state_ != State::STATE_IVR_SET_SMS_AUTH_TIME)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error in:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	IvrSetSMSAuthTimeState * state = reinterpret_cast<IvrSetSMSAuthTimeState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->sub_state_);

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if(rDspReq->getStatus())
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
					tag, 
					simm->IVR_setSMSAuthTimeHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_MINOR_NO_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_MINOR_NO_ERROR_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_START:
					{
						return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, IVR_setSMSAuthTimeHandler);
					}
				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{
				if(state->sub_state_ == IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_MINOR_NO_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);
					}

					int error = 0;
					if(( state->minorNo_ > '0') && ( state->minorNo_ < '4')) 
					{
						state->retry_ = 0;

						//查副号码序号对应的副号码;
						vector<MinorNumberAttr> minorList;
						int result = DataUsage::instance()->getAllMinorNumber(cr->_inCaller, minorList);
						if(result == E_OPERATOR_SUCCED)
						{
							int arrIndex = -1;
							for(int i=0; i<minorList.size(); i++)
							{
								if(minorList[i].sequenceNo == state->minorNo_-'0')
								{
									arrIndex = i;
									break ;
								}
							}
							if(arrIndex == -1) /*副号码序号有误*/
								error = 1;
							else
								strncpy(state->minorNumber_, minorList[arrIndex].minorNumber, MAX_NUMB_LEN-1);
						}
						else
							error = 1;
					}
					else if( state->minorNo_ == '*')  /*返回上级菜单*/
						return simm->processIVR(cr);
					else
					{
						/*您输入的号码错误 4016.wav*/
						int fileID[2] = {4016, -1};
						state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_MINOR_NO_ERROR_START;
						state->retry_ ++;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setSMSAuthTimeHandler);
					}

					if(error == 1)
					{
						/*输入的副号码序号不存在,play 4313.wav */
						int fileID[2] = {4313, -1};
						state->retry_ ++;
						state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_MINOR_NO_ERROR_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setSMSAuthTimeHandler);
					}

					/*play *.wav */
					int fileID[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
					int pos = 0;
					fileID[pos] = 4354 ; pos += 1; /* 输入 */
					fileID[pos] = 1001 ; pos += 1; /* 1 */
					fileID[pos] = 4351 ; pos += 1; /* 全天24小时不接收短信： */

					fileID[pos] = 4354 ; pos += 1; /* 输入 */
					fileID[pos] = 1002 ; pos += 1; /* 2 */
					fileID[pos] = 4352 ; pos += 1; /* 在您设定的时间段内不接收短信 */

					fileID[pos] = 4354 ; pos += 1; /* 输入 */
					fileID[pos] = 1003 ; pos += 1; /* 3 */
					fileID[pos] = 4353 ; pos += 1; /* 所有时间都接收短信 */

					state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_START;
					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, IVR_setSMSAuthTimeHandler);
				}
				else if(state->sub_state_ == IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					if((state->policyID_ >= '1') && (state->policyID_ <= '3'))
					{
						int fileID[4] = {-1, -1, -1, -1};
						state->retry_ = 0;
						int timeStrategyID = 0;
						int result = 0;

						if(state->policyID_ == '1') //All the time to receive text messages
						{
							timeStrategyID = 101;
						}
						else if(state->policyID_ == '3') //All the time not receive text messages
							timeStrategyID = 103;
						if((state->policyID_ == '1') || state->policyID_ == '3')
						{
							string startTime = "1970-01-01 00:00:00";
							string endTime= "1970-01-01 00:00:00";
							result = DataUsage::instance()->setSmTimeStrategy(state->minorNumber_, startTime, endTime, timeStrategyID);
							if(result == E_OPERATOR_SUCCED)
							{
								fileID[0] = 4334;		/* 设置成功 */
								fileID[1] = 4342;       /* 您的副号吗 */

								fileID[2] = 4351;       /* 全天24小时不接收短信 */
								if(timeStrategyID == 103)
									fileID[2] = 4353;       /* 所有时间都接收短信 */
								state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_SET_RESULT_START;
							}
							else
							{
								fileID[0] = 4329;		/*系统忙....*/
								state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_SYSTEM_BUSY_START;
							}
						}
						else if(state->policyID_ == '2')
						{
							fileID[0] = 4355;		    /*请按24小时制输入2位数字...*/
							state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_START;
						}

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setSMSAuthTimeHandler);
					}
					else if( state->policyID_ == '*')
					{
						return simm->processIVR(cr);
					}
					else
					{
						/*按键错误， 请重新输入 4330.wav*/
						int fileID[2] = {4330, -1};
						state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_ERROR_START;
						state->retry_ ++ ;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setSMSAuthTimeHandler);
					}
				}
				else if(state->sub_state_ == IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					state->timeField_[4] = 0;
					if(!simm->isValidTimeField(state->timeField_))
					{
						/* 时间段输入错误，4356.wav 4355.wav */
						int fileID[3] = {4356, 4355, -1};
						state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_ERROR_START;
						state->retry_ ++ ;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setSMSAuthTimeHandler);
					}

					state->retry_ = 0;
					int fileID[24];
					for(int i=0; i<24; i++)
					{
						fileID[i] = -1;
					}

					char tmp[8];
					char sTime[24], eTime[24];
					int startTime, endTime;

					memset(tmp, 0, sizeof(tmp));
					memset(sTime, 0, sizeof(sTime));
					memset(eTime, 0, sizeof(eTime));

					strncpy(tmp, state->timeField_, 2);
					startTime = atoi(tmp);
					sprintf(sTime, "1970-01-01 %s:00:00", tmp);

					memset(tmp, 0, sizeof(tmp));
					strncpy(tmp, state->timeField_+2, 2);
					endTime = atoi(tmp);
					sprintf(eTime, "1970-01-01 %s:00:00", tmp);

					string strStart = sTime;
					string strEnd= eTime;
					int timeStrategyID = 102; 
					int result = DataUsage::instance()->setSmTimeStrategy(state->minorNumber_, strStart, strEnd, timeStrategyID);
					if(result == E_OPERATOR_SUCCED)
					{
						int pos = 0;

						fileID[pos] = 4334;	pos += 1;	/* 设置成功 */
						fileID[pos] = 4342; pos += 1;   /* 您的副号码 */
						fileID[pos] = 4357; pos += 1;   /* 在每天 */
						int ret = simm->digit2voiceList(startTime, &fileID[pos]);   
						pos += ret;
						fileID[pos] = 1018;  pos += 1;   /* 时 */
						fileID[pos] = 4358;  pos += 1;   /* 到 */
						ret = simm->digit2voiceList(endTime, &fileID[pos]);
						pos += ret;
						fileID[pos] = 1018;  pos += 1;   /* 时 */
						fileID[pos] = 4359;       /* 不接收短信 */
						state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_SET_RESULT_START;
					}
					else
					{
						fileID[0] = 4329;		/*系统忙....*/
						state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_SYSTEM_BUSY_START;
					}

					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, IVR_setSMSAuthTimeHandler);
				}
				else 
					return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if(playAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_MINOR_NO_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_MINOR_NO_ERROR_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_ERROR_START:
					return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
						(void *)cr, IVR_setSMSAuthTimeHandler);
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_ERROR_START://zuosai add 20110210 for bug
					{
						int maxDigist = 4;
						return simm->collectDigit( cr->_inSpan, cr->_inChan, maxDigist,
							rand()%5+1000, "#", (void *)cr, IVR_setSMSAuthTimeHandler);
					}

				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_SET_RESULT_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_SYSTEM_BUSY_START:
					{
						return OK;
					}
				default:
					return SK_NOT_HANDLED;
				}
			}
			else
				return SK_NOT_HANDLED;
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			if( rCpe->getEvent() == RecvDtmf )
			{
				char buf[256];
				memset(buf, 0, sizeof(buf));
				simm->collectDigitResult( (SKC_Message *)rCpe, buf );

				if( strlen(buf) < 1)
					return OK;

				state->crcPlayCount_ = 0;

				if(state->userTimerId_ != 0)
				{
					simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setSMSAuthTimeHandler) ;
					state->userTimerId_ = 0;
				}

				if( state->sub_state_ == IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_MINOR_NO_START)
				{
					state->minorNo_ = buf[0];
				}
				else if( state->sub_state_ == IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_MINOR_NO_ERROR_START)
				{
					state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_MINOR_NO_START;
					state->minorNo_ = buf[0];
				}
				else if( state->sub_state_ == IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_START)
				{
					state->policyID_ = buf[0];
				}
				else if(state->sub_state_ == IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_START)
				{
					strncpy(state->timeField_, buf, sizeof(state->timeField_)-1);
				}
				else if( state->sub_state_ == IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_ERROR_START)
				{
					state->sub_state_ = IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_START;
					strncpy(state->timeField_, buf, sizeof(state->timeField_)-1);
				}
				else
				{
					return OK;
				}

				int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
				return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, (void *)cr, IVR_setSMSAuthTimeHandler);
			}
			else if( rCpe->getEvent() == FilePlayOK )
			{
				switch(state->sub_state_)
				{
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_MINOR_NO_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_MINOR_NO_ERROR_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_ERROR_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_START:
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_ERROR_START:
					{
						state->userTimerId_ = getUserTimerId();
						return simm->startUserTimer(state->userTimerId_,CRC_PLAY_INTERVAL, (void *)cr, IVR_setSMSAuthTimeHandler);
					}
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_SET_RESULT_START:
					{
						return simm->processIVR(cr);
					}
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_SYSTEM_BUSY_START:
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
					}
				default:
					return OK;
				}
			}
			else
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
		}
		CASEC_UserTimerAck(varNm) /* 定时器返回消息 */
		{
			if(state->userTimerId_ != 0)
			{
				simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setSMSAuthTimeHandler) ;
				state->userTimerId_ = 0;
			}
			if (varNm->getStatus() == 0x10)
			{
				int fileID[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
				switch(state->sub_state_)
				{
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_MINOR_NO_START:
					{
						fileID[0] = 4045;
						break;
					}
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_MINOR_NO_ERROR_START:
					{
						fileID[0] = 4313;
						break;
					}
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_START:
					{
						/*play *.wav */
						int pos = 0;
						fileID[pos] = 4354 ; pos += 1; /* 输入 */
						fileID[pos] = 1001 ; pos += 1; /* 1 */
						fileID[pos] = 4351 ; pos += 1; /* 全天24小时不接收短信： */
						fileID[pos] = 4354 ; pos += 1; /* 输入 */
						fileID[pos] = 1002 ; pos += 1; /* 2 */
						fileID[pos] = 4352 ; pos += 1; /* 在您设定的时间段内不接收短信 */

						fileID[pos] = 4354 ; pos += 1; /* 输入 */
						fileID[pos] = 1003 ; pos += 1; /* 3 */
						fileID[pos] = 4353 ; pos += 1; /* 所有时间都接收短信 */
						break;
					}
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_POLICY_MENU_ERROR_START:
					{
						fileID[0] = 4330;
						break;
					}
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_START:
					{
						fileID[0] = 4355;
						break;
					}
				case IvrSetSMSAuthTimeState::STATE_ISSAT_PLAY_INPUT_TIME_FIELD_ERROR_START:
					{
						fileID[0] = 4356;
						fileID[1] = 4355;
						break ;
					}
				default:
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
					}
				}
				if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

				state->crcPlayCount_++;
				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
					( void *)cr, IVR_setSMSAuthTimeHandler);
			}
			return OK;
		}
		CASEC_CancelUserTimerAck(varNm)
		{
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) In Channel(0x%x,0x%x), CancelUserTimer, status = %d",
				cr->_callID, cr->_inSpan, cr->_inChan, varNm->getStatus());

			return OK;
		}
		CASEC_default
		{
			return defaultHandler( evt, tag);
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;
}


int SIMMApp::IVR_setMultiNumberHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if(cr->_state == 0)
		return -1;

	if(cr->_state->state_ != State::STATE_IVR_SET_MULTI_NUMBER)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error in:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	IvrSetSetMultiNumberState * state = reinterpret_cast<IvrSetSetMultiNumberState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->sub_state_);

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if(rDspReq->getStatus())
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
					tag, 
					simm->IVR_setMultiNumberHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_INPUT_MINOR_NO_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_CONFIRM_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_DELETE_NUMBER_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ORDER_NUMBER_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_RESULT_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_DELETE_NUMBER_RESULT_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ORDER_NUMBER_RESULT_START:
					return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, IVR_setMultiNumberHandler);
				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_CollectDigitStringAck(rCollect)
		{
			if(rCollect->getStatus() == 0x10)
			{
				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: CollectDigitString could not be successfully processed, Ack State = 0x%x", 
					rCollect->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if(playAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_INPUT_MINOR_NO_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_CONFIRM_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_RESULT_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_DELETE_NUMBER_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ORDER_NUMBER_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_DELETE_NUMBER_RESULT_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ORDER_NUMBER_RESULT_START:
					return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,(void *)cr, IVR_setMultiNumberHandler);
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_START:
					return simm->collectDigit( cr->_inSpan, cr->_inChan, MAX_COLLECT_DIGIT_LEN, 
						rand()%5+100, "*#", (void *)cr, IVR_setMultiNumberHandler);
				case IvrSetAuthListState::STATE_ISAL_PLAY_SYSTEM_BUSY_START:
					return OK ; 
				default:
					return SK_NOT_HANDLED;
				}
			}
			else
				return SK_NOT_HANDLED;
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{
				if(state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_INPUT_MINOR_NO_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					int error = 0;
					if(( state->minorNo_ > '0') && ( state->minorNo_ < '4')) 
					{
						state->retry_ = 0;

						//查副号码序号对应的副号码;
						vector<MinorNumberAttr> minorList;
						int result = DataUsage::instance()->getAllMinorNumber(cr->_inCaller, minorList);
						if(result == E_OPERATOR_SUCCED)
						{
							int arrIndex = -1;
							for(int i=0; i<minorList.size(); i++)
							{
								if(minorList[i].sequenceNo == state->minorNo_-'0')
								{
									arrIndex = i;
									break ;
								}
							}
							if(arrIndex == -1)
								error = 1;
							else
								strncpy(state->minorNumber_, minorList[arrIndex].minorNumber, MAX_NUMB_LEN-1);
						}
						else
							error = 1;
					}
					else if( state->minorNo_ == '*')  /*返回上级菜单*/
						return simm->processIVR(cr);

					else
					{
						/*按键错误， 请重新输入 4330.wav*/
						int fileID[2] = {4330, -1};
						state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_MINOR_NO_ERROR_START;
						state->retry_ ++;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setMultiNumberHandler);
					}

					if(error == 1)
					{
						/*输入的副号码序号不存在,play 4313.wav */
						int fileID[2] = {4313, -1};
						state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_MINOR_NO_ERROR_START;
						state->retry_++;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setMultiNumberHandler);
					}

					/*播放子菜单提示语  play 4420.wav */
					int fileID[2] = {4420, -1};
					state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_START;
					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, IVR_setMultiNumberHandler);
				}
				else if( state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					if(( state->actionKey_ > '0') && ( state->actionKey_ < '4')) 
					{
						int fileID[2] = {-1, -1};
						if( state->actionKey_ == '1')	    /*增加呼叫转移号码*/
						{
							fileID[0] = 4421;
							state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_START;
						}
						else if(state->actionKey_ == '2')  /*删除呼叫转移号码*/
						{
							//获取副号码设定的前转号码列表;
							vector<CallForwardInfo> nbrList;
							int result = DataUsage::instance()->getCallForwardNumber(state->minorNumber_, nbrList);
							if(result != E_OPERATOR_SUCCED)
							{
								fileID[0] = 4329;         /*查询异常， 系统忙。。。*/
								state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_SYSTEM_BUSY_START;
							}
							else
							{
								if( nbrList.size() == 0)  /*没有设定转移号码。。。*/
								{
									fileID[0] = 4422;     
									state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_NO_DATA_START;
								}
								else                      /*确认删除*/
								{
									int tmpfileID[24];
									for(int i=0; i<24; i++)
										tmpfileID[i]= -1;
									tmpfileID[0] = 4423;
									int len = simm->phonNum2voiceList(nbrList[0].calledNumber.c_str(), &tmpfileID[1]);
									tmpfileID[len+1] = 4424 ;
									state->retry_ = 0;
									state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_DELETE_NUMBER_START;
									return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, tmpfileID,
										(void *)cr, IVR_setMultiNumberHandler);
								}
							}
						}
						else if(state->actionKey_ == '3')   /*调整接听顺序*/
						{
							//获取副号码设定的前转号码列表;
							vector<CallForwardInfo> nbrList;
							int result = DataUsage::instance()->getCallForwardNumber(state->minorNumber_, nbrList);
							if(result != E_OPERATOR_SUCCED)
							{
								fileID[0] = 4329;         /*查询异常， 系统忙。。。*/
								state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_SYSTEM_BUSY_START;
							}
							else
							{
								if( nbrList.size() == 0)  /*没有设定转移号码。。。*/
								{
									fileID[0] = 4422;     
									state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_NO_DATA_START;
								}
								else                      /*确认删除*/
								{
									strncpy(state->targetNumber_ ,nbrList[0].calledNumber.c_str(), sizeof(state->targetNumber_)-1);


									int tmpfileID[24];
									for(int i=0; i<24; i++)
										tmpfileID[i]= -1;
									tmpfileID[0] = 4423;
									int len = simm->phonNum2voiceList(nbrList[0].calledNumber.c_str(), &tmpfileID[1]);
									tmpfileID[len+1] = 4425 ;
									state->retry_ = 0;
									state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ORDER_NUMBER_START;
									return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, tmpfileID,
										(void *)cr, IVR_setMultiNumberHandler);
								}
							}
						}
						state->retry_ = 0;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setMultiNumberHandler);
					}
					else if( state->actionKey_ == '*')
						return simm->processIVR(cr);
					else
					{
						/*按键错误， 请重新输入 4330.wav*/
						int fileID[2] = {4330, -1};
						state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_ERROR_START;
						state->retry_++;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setMultiNumberHandler);
					}
				}
				else if( state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_START)
				{
					char endKey = 0;
					if(simm->isValidNumber(state->targetNumber_) != false )
					{
						int len = strlen(state->targetNumber_);
						endKey = state->targetNumber_[len-1];
						if((endKey == '#') /*|| (endKey == '*')*/)
							state->targetNumber_[len-1] = '\0';
						else if (endKey == '*')
							return simm->processIVR(cr);
						else
							return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

						int fileID[24] ;
						for(int cnt = 0; cnt < 24; cnt ++)
							fileID[cnt] = -1;

						fileID[0] = 4426;
						len = 0 ;
						len = simm->phonNum2voiceList(state->targetNumber_, &fileID[1]);
						fileID[len+1] = 4427 ;

						state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_CONFIRM_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setMultiNumberHandler);
					}
					else /*添加的号码有误, 请核实后再拨*/
					{
						int fileID[2] = {4016, -1};
						state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_ERROR_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setMultiNumberHandler);
					}
				}
				else if( state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_CONFIRM_START)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					int fileID[3] = {-1, -1, -1};
					if( state->subActionKey_  == '1')	/*确认请按1*/
					{
						CallForwardInfo nbrInfo;
						nbrInfo.calledNumber = state->targetNumber_ ;
						nbrInfo.order = -1;
						DataUsage::instance()->setCallForwardNumber(state->minorNumber_, nbrInfo);

						fileID[0] = 4428 ;
						fileID[1] = 4429 ;
						state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_RESULT_START;
					}
					else if( state->subActionKey_  == '0')
					{
						fileID[0] = 4421 ;
						state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_START;
					}
					else
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, IVR_setMultiNumberHandler);
				}
				else if(( state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_RESULT_START)|
					(state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_DELETE_NUMBER_RESULT_START) |
					(state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ORDER_NUMBER_RESULT_START))
				{
					if(state->confirmKey_ == '*')
						return simm->processIVR(cr);
					else if(state->confirmKey_ == '1')
						return simm->processIvrSetMultiNumber(cr);
					else
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				}
				else if(state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_DELETE_NUMBER_START)
				{
					int fileID[3] = {-1, -1, -1};
					int result = 0;

					//确认删除
					if(state->subActionKey_ == '1')
					{
						result = DataUsage::instance()->cancelCallForwardNumber(state->minorNumber_);
						fileID[0] = 4430;
						fileID[1] = 4429;
						state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_DELETE_NUMBER_RESULT_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setMultiNumberHandler);
					}
					else
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				}
				else if(state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ORDER_NUMBER_START)
				{
					int fileID[3] = {-1, -1, -1};
					//确认删除
					if( (state->subActionKey_ == '1') |(state->subActionKey_ == '2'))
					{
						CallForwardInfo nbrInfo;
						nbrInfo.calledNumber = state->targetNumber_ ;
						nbrInfo.order = -1;
						if(state->subActionKey_ == '2')
							nbrInfo.order = 0;
						DataUsage::instance()->setCallForwardNumber(state->minorNumber_, nbrInfo);
						fileID[0] = 4431;
						fileID[1] = 4429;
						state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ORDER_NUMBER_RESULT_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, IVR_setMultiNumberHandler);
					}
					else
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				}
			}
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			if( rCpe->getEvent() == RecvDtmf )
			{
				char buf[256];
				memset(buf, 0, sizeof(buf));
				simm->collectDigitResult( (SKC_Message *)rCpe, buf );

				if(strlen(buf) < 1)
					return OK;
				state->crcPlayCount_ = 0;

				if(state->userTimerId_ != 0)
				{
					simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setMultiNumberHandler) ;
					state->userTimerId_ = 0;
				}

				if( state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_INPUT_MINOR_NO_START
					|| state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_MINOR_NO_ERROR_START)
				{
					state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_INPUT_MINOR_NO_START;
					state->minorNo_ = buf[0];
				}
				else if( state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_START
					|| state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_ERROR_START)
				{
					state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_START;
					state->actionKey_ = buf[0];
				}
				else if(state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_START
					|| state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_ERROR_START)
				{
					state->sub_state_ = IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_START;
					memset(state->targetNumber_, 0, MAX_NUMB_LEN);
					strncpy(state->targetNumber_, buf, MAX_NUMB_LEN-1);
				}
				else if(state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_CONFIRM_START)
					state->subActionKey_ = buf[0];
				else if(state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_DELETE_NUMBER_START)
					state->subActionKey_ = buf[0];
				else if(state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ORDER_NUMBER_START)
					state->subActionKey_ = buf[0];
				else if(state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_RESULT_START)
					state->confirmKey_ = buf[0];
				else if(state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_DELETE_NUMBER_RESULT_START)
					state->confirmKey_ = buf[0];
				else if(state->sub_state_ == IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ORDER_NUMBER_RESULT_START)
					state->confirmKey_ = buf[0];
				else
					return OK;
				int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
				return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, (void *)cr, IVR_setMultiNumberHandler);
			}
			else if(rCpe->getEvent() == FilePlayOK)
			{
				switch(state->sub_state_)
				{
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_INPUT_MINOR_NO_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_MINOR_NO_ERROR_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_ERROR_START:
					{
						/* Send Timer */
						state->userTimerId_ = getUserTimerId();
						return simm->startUserTimer(state->userTimerId_,CRC_PLAY_INTERVAL, (void *)cr, IVR_setMultiNumberHandler);
					}				
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_SYSTEM_BUSY_START:
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_RESULT_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_DELETE_NUMBER_RESULT_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ORDER_NUMBER_RESULT_START:
					{
						state->userTimerId_ = getUserTimerId();
						return simm->startUserTimer(state->userTimerId_,CRC_PLAY_INTERVAL*2, (void *)cr, IVR_setMultiNumberHandler);
					}
				default:
					return OK;
				}
				return OK;
			}
			else
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
		}
		CASEC_UserTimerAck(varNm) /* 定时器返回消息 */
		{
			if(state->userTimerId_ != 0)
			{
				simm->cancelUserTimer( state->userTimerId_, (void *)cr, IVR_setMultiNumberHandler) ;
				state->userTimerId_ = 0;
			}
			if (varNm->getStatus() == 0x10)
			{
				int fileID[3] = {-1,-1,-11};
				switch(state->sub_state_)
				{
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_INPUT_MINOR_NO_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_MINOR_NO_ERROR_START:
					{
						fileID[0] = 4045;
						break;
					}
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ACTION_MENU_ERROR_START:
					{
						fileID[0] = 4420;
						break;
					}
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ADD_NUMBER_RESULT_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_DELETE_NUMBER_RESULT_START:
				case IvrSetSetMultiNumberState::STATE_ISSMN_PLAY_ORDER_NUMBER_RESULT_START:
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				default:
					return OK;
				}
				if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

				state->crcPlayCount_++;
				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
					( void *)cr, IVR_setMultiNumberHandler);
			}
			return OK;
		}
		CASEC_CancelUserTimerAck(varNm)
		{
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) In Channel(0x%x,0x%x), CancelUserTimer, status = %d",
				cr->_callID, cr->_inSpan, cr->_inChan, varNm->getStatus());

			return OK;
		}
		CASEC_default
		{
			return defaultHandler( evt, tag);
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;
}


int SIMMApp::IVR_playSystemInfoHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
	{
		return -1 ;
	}

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if(cr->_state == 0)
	{
		return -1;
	}

	if(cr->_state->state_ != State::STATE_IVR_PLAY_SYSTEM_INFO)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error in:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	IvrPlaySystemInfoState * state = reinterpret_cast<IvrPlaySystemInfoState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->sub_state_);

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp, "INFO: CallID(%d) Enter Function:IVR_playSystemInfoHandler() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);
	}	

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if(rDspReq->getStatus())
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
					tag, 
					simm->IVR_playSystemInfoHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrPlaySystemInfoState::STATE_IPSI_RECV_ACTION_DTMF_CMPLT:
					{
						return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, IVR_playSystemInfoHandler);
					}
				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if(playAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case IvrPlaySystemInfoState::STATE_IPSI_PLAY_INFO_START:
					{
						int ret = simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
							(void *)cr, IVR_playSystemInfoHandler);
						if(ret != OK)
						{
							LogFileManager::getInstance()->write(Brief,
								"ERROR: dspServiceRequest: %s\n", sk_errorText(ret));
						}

						state->sub_state_ =IvrPlaySystemInfoState::STATE_IPSI_RECV_ACTION_DTMF;
						return ret ;
					}
				default:
					return SK_NOT_HANDLED;
				}
			}
			else
			{
				return SK_NOT_HANDLED;
			}
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{
				if( state->sub_state_ == IvrPlaySystemInfoState::STATE_IPSI_RECV_ACTION_DTMF_CMPLT)
				{
					if(state->actionKey_ == '*')
					{
						return simm->processIVR(cr);
					}
					if(state->actionKey_ == '1')
					{
						return simm->processIvrPlaySystemInfo(cr);
					}
					else
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
					}
				}
			}
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			if( rCpe->getEvent() == RecvDtmf )
			{
				char buf[256];
				memset(buf, 0, sizeof(buf));
				simm->collectDigitResult( (SKC_Message *)rCpe, buf );

				if(strlen(buf) < 1)
				{
					return OK;
				}

				if( state->sub_state_ == IvrPlaySystemInfoState::STATE_IPSI_RECV_ACTION_DTMF)
				{
					state->actionKey_  = buf[0];
					state->sub_state_ = IvrPlaySystemInfoState::STATE_IPSI_RECV_ACTION_DTMF_CMPLT;
				}

				int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
				return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, (void *)cr, IVR_playSystemInfoHandler);
			}
			else if ( rCpe->getEvent() == FileRecOK )
			{
				if( state->sub_state_ == IvrPlaySystemInfoState::STATE_IPSI_PLAY_INFO_START)
				{
					simm->processIVR(cr);
				}
			}
			else
			{
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
			}
		}
		CASEC_default
		{
			return defaultHandler( evt, tag);
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;
}

int SIMMApp::processUnknown(const CallRecord * cr)
{
	return OK;
}

int SIMMApp::handleRequestChannelAck(SK_Event *evt, void *tag) 
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)(cr->_thisPtr);
	int stat;

	LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) CallOut, outgroup %s",
		cr->_callID, cr->_outGroup);

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_RequestChannelAck(rca) 
		{
			LogFileManager::getInstance()->write(Debug, 
				"INFO: CallID(%d) RequestChannelAck In Channel: (0x%x,0x%x)",
				cr->_callID,
				rca->getSpan(), 
				rca->getChannel());

			stat = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
				(void *)cr,
				simm->genericHandler);

			if (stat != OK)
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: in sk_setChannelHandler: %s\n", 
					sk_errorText(stat));
			}

			int ret = rca->getSKStatus();
			if ( ret == OK) 
			{
				//.s add by whc 20100127
				cr->_outSpan = rca->getSpan();
				cr->_outChan = rca->getChannel();
				//.e add by whc 20100127
				stat = sk_setChannelHandler(cr->_outSpan, 
					cr->_outChan, 
					(void *)cr,
					simm->genericHandler);

				if (stat != OK)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: in sk_setChannelHandler: %s\n", 
						sk_errorText(stat));
				}

				if(cr->_reference == 0)
					return simm->returnChannels(cr->_outSpan, cr->_outChan, (void *)cr);

				//----------------------------------------------------------------------------------
				//副号码作被叫时 cr->_nbrInfo.beCalled为副号码 cr->_outCalled是主号码
				//真实号码作被叫时 cr->_nbrInfo.beCalled 与 cr->_outCalled都是是该真实号码
				if(cr->_playingRingback==0 && strcmp(cr->_nbrInfo.beCalled, cr->_outCalled) != 0)
				{
					int ret=OK;
					ret = simm->processRingback(cr->_nbrInfo.beCalled,cr);
					if(ret == OK)
						return ret;

					LogFileManager::getInstance()->write(Debug, "ERROR: processRingback return error");
				}
				//------------------------------------------------------------------------------------

				XLC_Connect cnct;

				/* Send a connect message to the switch to connect the 
				* incoming channel to the outseized channel 
				*/
				cnct.setSpanA(cr->_inSpan); 
				cnct.setChannelA(cr->_inChan);
				cnct.setSpanB(cr->_outSpan = rca->getSpan());
				cnct.setChannelB(cr->_outChan = rca->getChannel());

				char textLog[512];
				memset(textLog, 0, sizeof(textLog));
				sprintf(textLog,"CallID(%d) Connecting ..(0x%x, 0x%x) And (0x%x, 0x%x)",
					cr->_callID,
					cnct.getSpanA(),
					cnct.getChannelA(),
					cnct.getSpanB(),
					cnct.getChannelB());
				LogFileManager::getInstance()->write(Debug, "INFO: %s", textLog);

				/* For each of the channels being connected, store the other 
				* channel's span and chan in the channel data structure as 
				* the "cnctdSpan" and "cnctdChan".  When one channel is released,
				* it's connecting channel can be released as well, this is done in 
				* returnChannels.  
				*/

				ChannelData *chandat1 = new ChannelData;
				ChannelData *chandat2 = new ChannelData;

				chandat1->cnctdSpan = cr->_outSpan;
				chandat1->cnctdChan = cr->_outChan;
				sk_setChannelData(cr->_inSpan,cr->_inChan,(void *)chandat1);

				chandat2->cnctdSpan = cr->_inSpan;
				chandat2->cnctdChan = cr->_inChan;
				sk_setChannelData(cr->_outSpan,cr->_outChan,(void *)chandat2);
				cr->_reference++ ;

				cnct.send((void *)cr, handleAcks);
			} 
			else 
			{
				LogFileManager::getInstance()->write(Brief,
					"INFO: CallID(%d) RequestChannelAck : Outseize was not successful, Releasing In Channel (0x%x,0x%x) error = %s",
					cr->_callID, cr->_inSpan, cr->_inChan,  sk_statusText(rca->getSKStatus()));

				// 设置正确的出通道
				//.s add by wj 20120305
				cr->_outSpan = rca->getSpan();
				cr->_outChan = rca->getChannel();

				stat = sk_setChannelHandler(cr->_outSpan, 
					cr->_outChan, 
					(void *)cr,
					simm->genericHandler);

				if (stat != OK)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: in sk_setChannelHandler: %s\n", 
						sk_errorText(stat));
				}
				//.e add by wj 20120305


				/*
				//.e delete the process of the second dial state process by whc

				/* Outseize was not successful - close out the CallRecord, and
				*  release and return the inbound channel 
				*/

				XLC_ReleaseChannel rc;

				rc.setSpanA(cr->_inSpan);
				rc.setChannelA(cr->_inChan);
				rc.setSpanB(cr->_inSpan);
				rc.setChannelB(cr->_inChan);

				stat = rc.send(	NULL,handleAcks);

			}
			return OK;
		} // end CASEC_RequestChannelAck
	} SKC_END_SWITCH;

	// If message was not handled in SKC_MSG_SWITCH, then notify the caller 
	return(SK_NOT_HANDLED);
}

int SIMMApp::genericHandler(SK_Event *evt, void *tag) 
{
	if( ( evt == 0 ) || ( tag == 0 ) )
	{
		return -1 ;
	}

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;
	CallRecord *cr = (CallRecord *)tag;

	// this pointer stored in call record.
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	char logText[1024];

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_PPLEventIndication(pplEvent)
		{
			if((pplEvent->getComponentID() == ISUP_CPC) && (pplEvent->getPPLEvent() == ISUP_IND_SAM ))
			{
				char tempNbr[16];
				memset(tempNbr, 0, sizeof(tempNbr));
				simm->getSAMNbr( pplEvent->getData(), tempNbr);

				LogFileManager::getInstance()->write(Brief, "INFO: CallID(%d) Received ISUP_IND_SAM,  Follow-up number(%s)",
					cr->_callID, tempNbr);

				strcat(cr->_inSAMNbr, tempNbr) ;

				//zuosai add 20111209 for SAM
				int accesslen = strlen(simm->_accessCode);
				int calledlen = strlen(cr->_inCalled);
				if(cr->_inCaller[0] != '0' && !strncmp(cr->_inCalled, simm->_accessCode, accesslen))
				{
					//if(cr->_inCalled[accesslen] == '1' || cr->_inCalled[accesslen] == '2' || cr->_inCalled[accesslen] == '3')
					if(cr->_inCalled[accesslen] == '1' || cr->_inCalled[accesslen] == '2' || cr->_inCalled[accesslen] == '3' || cr->_inCalled[accesslen] == '6' )  // .s added by wj 20130301
					{
						//95096113900000000 or 9509613900000000 or 95096213900000000 or 95096313900000000
						//will not waite anymore SAM
						if(cr->_userTimerID != -1)
						{
							simm->cancelUserTimer(cr->_userTimerID,(void *)cr,genericHandler);
							cr->_userTimerID = -1;
						}

						if( cr->signal_ == INCOMING_IAM)
						{
							/* Send ACM to inComing */
							cr->signal_ = SENDOUT_ACM;
							LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) Sent ISUP_ACM to Channel(Ox%x, 0x%x)",
								cr->_callID, cr->_inSpan, cr->_inChan);
							return simm->sendPPLEvent(cr->_inSpan, cr->_inChan, ISUP_ACM, (void *)cr,genericHandler); 
						}
					}
				}
				//zuosai add 20111209 for SAM end
				return OK;
			}
			else if((pplEvent->getComponentID() == L3P_CIC) && (pplEvent->getPPLEvent() == ISUP_IND_ACM ))
			{
				UBYTE * pData = pplEvent->getData();
				int icbCount = pplEvent->getICBCount();
				LogFileManager::getInstance()->write(Brief, "INFO: CallID(%d) Received ISUP_IND_ACM,  ICBs = %d", cr->_callID, icbCount);
				int ret = simm->forecastCalledStatus(cr, pData);
			}
			return OK;
		}
		CASEC_PPLEventRequestAck(ppl)
		{
			if( ( ppl->getStatus() == 0x10 ) && ( cr->signal_ == SENDOUT_ANM ) )
			{
				cr->signal_ = ANM_ACK;
				cr->_connTime = time(0);
				//.s Modify for the simm call watching by whc 201000910
				cr->_callResult = 0;        /*call out sucess*/				
				//.e Modify for the simm call watching by whc 201000910				
				/*Generate appropriate signaling to return answer to the incoming call*/
				return simm->generateCallProcessingEvent(cr->_inSpan, cr->_inChan, cr, genericHandler);
			}
			else if( ( ppl->getStatus() == 0x10 ) && ( cr->signal_ == SENDOUT_ACM ) )
			{
				/* Make Simm Application logic */
				cr->signal_ = ACM_ACK;

				//SAM numbers will be added to the called number
				if( strlen( cr->_inSAMNbr ) != 0 )
				{
					strncat( cr->_inCalled, cr->_inSAMNbr, MAX_NUMB_LEN - 1 );
				}

				//Caller to determine whether a fixed tele, and no prefix area code
				if( ( ( strlen(cr->_inCaller) == 7 ) || ( strlen(cr->_inCaller) == 8 )) && 
					( strncmp(cr->_inCaller, "00", strlen("00")) ) )
				{
					char tmpBuf[32];
					memset(tmpBuf, 0, sizeof(tmpBuf));
					sprintf(tmpBuf, "%s%s", simm->_localAreaCode, cr->_inCaller);
					strcpy(cr->_inCaller, tmpBuf);
				}

				//Whether the outbound call center
#ifdef ENABLE_CALLCENTER
				char tmpCaller[64];
				memset(tmpCaller, 0, sizeof(tmpCaller));
				strcpy(tmpCaller, simm->_localAreaCode);
				strcat(tmpCaller, _callcenterNbr);
				if( ( !strcmp(cr->_inCalled, _callcenterNbr) ) || 
					( !strcmp(cr->_inCalled, tmpCaller) ) )
				{
					strcpy(cr->_outCalled, "01095096");
					strcpy(cr->_outCaller, cr->_inCaller);

					//send for caiying
					char  szNum[32]={0};
					char *pSzTemp = szNum ;
					*(unsigned short *)pSzTemp = 32 ;
					pSzTemp += 2;
					strcpy(pSzTemp,cr->_inCaller);
					bool isOK = ClientSendMsg(g_pHand ,szNum ,32);
					if(!isOK)
					{
						LogFileManager::getInstance()->write(Debug,"Send caller Num to switcher failed . Caller = %s",cr->_inCaller);
					}
					LogFileManager::getInstance()->write(Debug,"Send caller Num to switcher  success. Caller = %s",cr->_inCaller);

					return simm->processCallout(cr) ;
				}
#endif
				ResultMakeDialRoute ret;
				if(strlen(cr->_inOriginalNbr) != 0) 
				{
					strncpy(cr->_inCalled, cr->_inOriginalNbr, MAX_NUMB_LEN-1);
					if(simm->_makeDialRouter != 0)
					{
						string outCaller, outCalled;
						LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) begin makeDialRouter()",cr->_callID);
						ret = simm->_makeDialRouter(cr->_inCaller, cr->_inCalled, outCaller, outCalled, cr->_nbrInfo);
						strncpy(cr->_outCaller, outCaller.c_str(), sizeof(cr->_outCaller)-1);
						strncpy(cr->_outCalled, outCalled.c_str(), sizeof(cr->_outCalled)-1);

						LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) makeDialRouter() ret = %s",cr->_callID,simm->_getResultDescription(ret).c_str());

						if(g_isPrintProcess)
						{			   
							char tmp[256] = { 0 };
							sprintf(tmp,"INFO: CallID(%d) makeDialRouter() ret = %s, FILE=%s,Line=%d", cr->_callID,simm->_getResultDescription(ret).c_str(), __FILE__, __LINE__);
							LogFileManager::getInstance()->write(Debug, "%s",tmp);
						}



					}
					else
					{
						LogFileManager::getInstance()->write(Brief,"ERROR: simm->_makeDialRouter == 0");
						return OK;
					}
				}
				else
				{
					if(simm->_makeDialRouter != 0)
					{
						string outCaller, outCalled;
						LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) begin makeDialRouter()",cr->_callID);
						ret = simm->_makeDialRouter(cr->_inCaller, cr->_inCalled, outCaller, outCalled, cr->_nbrInfo);
						strncpy(cr->_outCaller, outCaller.c_str(), sizeof(cr->_outCaller)-1);
						strncpy(cr->_outCalled, outCalled.c_str(), sizeof(cr->_outCalled)-1);
						LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) makeDialRouter() ret = %s",cr->_callID,simm->_getResultDescription(ret).c_str());

						if(g_isPrintProcess)
						{		
							char tmp[256] = { 0 };
							sprintf(tmp,"INFO: CallID(%d) makeDialRouter() ret = %s, FILE=%s,Line=%d", cr->_callID,simm->_getResultDescription(ret).c_str(), __FILE__, __LINE__);
							LogFileManager::getInstance()->write(Debug, "%s",tmp);
						}
					}
					else
					{
						LogFileManager::getInstance()->write(Brief,"ERROR: simm->_makeDialRouter == 0");
						return OK;
					}
				}

				switch(ret)
				{
				case RESULT_SECOND_DIAL:
					{
						//.s add by whc for watch user operation 20100910
						cr->_routerResult = CR_SECOND_DIAL;        /*second dial*/
						return simm->processSecondDial(cr);
					}
				case RESULT_IVR:
					{
						//.s add by whc for watch user operation 20100910
						cr->_routerResult = CR_IVR;        /*IVR*/
						cr->_connTime = time(NULL); 
						return simm->processIVR(cr);
					}
				case RESULT_CALL_CENTER:
					{
						//.s add by whc for watch user operation 20100910
						cr->_routerResult = CR_CALL_CENTER;        /*CALL_CENTER*/
						return simm->processCallCenter(cr);
					}
				case RESULT_SEQUENCE_NO_NOT_EXIST:
				case RESULT_CALLER_NOT_REG_SIMM:
				case RESULT_CALLER_NOT_LOCAL_REG:
				case RESULT_MINOR_NOT_REG:

#if 1
					// .s added by wj 20121023
				case RESULT_VIRTUAL_MINOR_NOT_REG:
					// .e added by wj 20121023
#endif

#if 1		
				case RESULT_CALLER_MINOR_STATE_CANCEL:
#endif
	
#if 1	
				case RESULT_CALLED_MINOR_STATE_CANCEL:
#endif

				case RESULT_NUMBER_ERROR:
				case RESULT_MINOR_OFF:
				case RESULT_MINOR_OUT_SERVICE:
				case RESULT_MINOR_FORBID_CALLER:
				case RESULT_CALLOUT_LIMIT:
				case RESULT_MINOR_IN_LIMIT_TIME_FRAME:
					{
#if 1
						LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) ret = ", cr->_callID, ret);						
#endif
						return simm->processException(ret ,cr);
					}

					//case RESULT_MINOR_IN_LIMIT_TIME_FRAME:
				case RESULT_MINOR_IN_LIMIT_TIME_FRAME_AND_HAS_WORD:
				case RESULT_DIAL_TO_RECORD:
					{
						//.s add by whc for watch user operation 20100910
						cr->_routerResult = CR_OUT_SERVICE;
						cr->_callResult = -1; //out service call out result is failure.
						return simm->processOutService( ret, cr);
					}

				case RESULT_OK:
					{
						//.s Modify for the simm call watching by whc 20100910
						if(cr->_routerResult == CR_CALL_IN)
						{
							//Check whether is directly call out, if not second dial. add by whc
							cr->_routerResult = CR_CALL_OUT; 
						}
						return simm->processCallout(cr) ;		
					}

				default:
					return simm->processUnknown(cr);
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: PPLEventRequest could not be successfully processed, Ack State = 0x%x", 
					ppl->getStatus());

				return OK;
			}
		}
		CASEC_GenerateCallProcessingEventAck(pEvent)
		{
			LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) GenerateCallProcessingEventAck",cr->_callID);

			if(cr->_playingRingback )
			{
				return simm->playFileStop(cr->_inSpan,cr->_inChan, tag, RingbackHandler);
			}

			return OK;
		}
		CASEC_DS0StatusChange(ds0) 
		{
			// Print info about any DS0 status change
			sprintf(logText, "DS0 Status Change: (0x%x,0x%x) Status: 0x%x Purge: 0x%x",
				ds0->getSpan(), 
				ds0->getChannel(),
				ds0->getChannelStatus(),
				ds0->getPurgeStatus());
			LogFileManager::getInstance()->write(Verbose,logText);
			return OK;
		}
		CASEC_CallProcessingEvent(cpe) 
		{
			if(cpe->getEvent() == Answer)
			{
				char textLog[512];
				memset(textLog, 0, sizeof(textLog));
				sprintf(textLog, "CallID(%d) outChannel(Ox%x, 0x%x) Recv ANSWER and Send ISUP_ANM to inChannel(Ox%x, 0x%x)",
					cr->_callID,cr->_outSpan, cr->_outChan, cr->_inSpan, cr->_inChan);
				LogFileManager::getInstance()->write(Brief, "INFO: %s",textLog);

				//zuosai add 20101227 for callforward
				if(cr->_userTimerID != -1)
				{
					simm->cancelUserTimer(cr->_userTimerID,(void *)cr,genericHandler);
					cr->_userTimerID = -1;
				}
				//zuosai add 20101227 for callforward end

				if(cr->_isAnswer == 0)
				{
					cr->signal_ = SENDOUT_ANM;
					return simm->sendPPLEvent(cr->_inSpan, cr->_inChan, ISUP_ANM, cr, simm->genericHandler);
				}
				else
				{
					//.s Modify for the simm call watching by whc 201000910
					cr->_callResult = 0;        /*call out sucess*/

					/*Generate appropriate signaling to return answer to the incoming call*/
					LogFileManager::getInstance()->write(Brief, "INFO: CallID(%d) Generate appropriate signaling to return answer to the incoming call",cr->_callID);

					simm->generateCallProcessingEvent(cr->_inSpan, cr->_inChan, cr, genericHandler);
					return OK ;
				}

			}
			else if ( cpe->getEvent() == FilePlayOK) 
			{
				if( cr->_playingRingback )
				{
					int fileID[2]={-1, -1};
					fileID[0] = 4400+cr->_ringBackId;/*标准回铃音 4401~4408*/
					fileID[1] = -1; 
					LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Ringback FilePlayOK in genericHandler, play again",cr->_callID);

					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,(void *)cr, RingbackHandler);
				}
				else 
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
			}
			else if( cpe->getEvent() == FilePlayCmpt)
			{
				if( cr->_playingRingback )
					return OK;
				else
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
			}
			else if( cpe->getEvent() == FilePlayStart)
			{
				return OK;
			}
			else if( cpe->getEvent() == FileRecStart)
			{
				LogFileManager::getInstance()->write(Debug, 
					"INFO: CallID(%d) Channel(Ox%x, 0x%x) RecordVox Start !", cr->_callID,cr->_inSpan, cr->_inChan);

				return OK ;
			}
			else if(cpe->getEvent() == FileRecOK) 
			{
				LogFileManager::getInstance()->write(Debug, 
					"INFO: CallID(%d) Channel(Ox%x, 0x%x) RecordVox Ok !", cr->_callID,cr->_inSpan, cr->_inChan);

				return OK ;
			}
			else if(cpe->getEvent() == FileRecCmpt)
			{
				LogFileManager::getInstance()->write(Debug, 
					"INFO: CallID(%d) Channel(Ox%x, 0x%x) RecordVox Cmpt !", cr->_callID,cr->_inSpan, cr->_inChan);

				return OK ;
			}
			else
				LogFileManager::getInstance()->write(Brief, 
				"Got a Call Processing Event message Event = %02x", cpe->getEvent());

			return OK;
		}
		CASEC_Connect(connect)
		{
			LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) Received Connect", cr->_callID);
			return OK;
		}
		CASEC_UserTimerAck(varNm)
		{
			if(cr->_userTimerID != -1)
			{
				simm->cancelUserTimer(cr->_userTimerID,(void *)cr,genericHandler);
				cr->_userTimerID = -1;
			}

			int ret = varNm->getStatus() ;
			LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) Received UserTimerAck, Status = 0x%x", cr->_callID, ret);

			if( (ret == 0x10) && (cr->signal_ == INCOMING_IAM))
			{
				/* Send ACM to inComing */
				cr->signal_ = SENDOUT_ACM;
				LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) Sent ISUP_ACM to Channel(Ox%x, 0x%x)",
					cr->_callID, cr->_inSpan, cr->_inChan);
				return simm->sendPPLEvent(cr->_inSpan, cr->_inChan, ISUP_ACM, (void *)cr,genericHandler); 
			}
			//zuosai add 20101227 for callforward
			else if((ret == 0x10) && (cr->signal_ == ACM_ACK))
			{
				cr->_doCallForward = 1;
				return simm->returnChannels(cr->_outSpan, cr->_outChan, tag);
			}
			//zuosai add 20101227 for callforward end

			return OK;
		}
		CASEC_CancelUserTimerAck(varNm)
		{
			int ret = varNm->getStatus() ;
			LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) Received CancelUserTimerAck, Status = 0x%x", 
				cr->_callID, ret);
			return OK;
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if(playAck->getStatus() == 0x10)
				return OK;
		}
		CASEC_RecordFileStartAck(rRecordStartAck)
		{
			LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) Received RecordFileStartAck, Status = 0x%x", 
				cr->_callID,rRecordStartAck->getStatus());

			return OK;
		}
		CASEC_RecordFileStopAck(rRecordStopAck)
		{
			LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) Received RecordFileStopAck, Status = 0x%x", 
				cr->_callID,rRecordStopAck->getStatus());

			return OK;
		}
		CASEC_default 
		{
			return defaultHandler( evt, tag);
		}
	} SKC_END_SWITCH;

	return(OK);
}



int SIMMApp::secondDialHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if(cr->_state == 0)
		return -1;

	if(cr->_state->state_ != State::STATE_SECOND_DIAL)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error In Channel:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	SecondDialState * state = reinterpret_cast<SecondDialState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->sub_state_);

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_PPLEventRequestAck(ppl)
		{
			if(ppl->getStatus() == 0x10)
			{
				cr->_isAnswer = 1 ; //应答主叫完成
				int fileID[2] = {-1, -1}; 
				if( strcmp( cr->_inCalled, "9509699") == 0 || strcmp( cr->_inCalled, "05919509699") == 0 )
				{
					ResultDataUsage result = DataUsage::instance()->isMajorRegistered(cr->_inCaller);
					if(result == E_MAJORNUMBER_NOT_EXESIT)
					{
						//caller user not register simm app
						fileID[0] = 4310;
						state->callerRegState_ = SecondDialState::STATE_NOT_REGISTER;
						state->sub_state_ = SecondDialState::STATE_SD_PLAY_INPUT_NUMBER_START;
					}
					else if( result == E_DATABASE_ERROR)
					{
						//system failed
						fileID[0] = 4329;
						state->callerRegState_= SecondDialState::STATE_UNKNOWN;
						state->sub_state_ = SecondDialState::STATE_SD_PLAY_SYSTEM_BUSY_START;
					}
					else
					{
						state->callerRegState_ = SecondDialState::STATE_ALREADY_REGISTER;

						/*play ivr welcome word*/
						result = DataUsage::instance()->getAllMinorNumber( cr->_inCaller, state->minorList_);
						if( result == E_OPERATOR_SUCCED)
						{
							if( state->minorList_.size() == 1) /*caller has one minor*/
							{
								fileID[0] = 4310;
								state->minorNo_ = '0' + state->minorList_[0].sequenceNo;

								strncpy(state->minorNumber_, state->minorList_[0].minorNumber, MAX_NUMB_LEN-1);
								state->sub_state_ = SecondDialState::STATE_SD_PLAY_INPUT_NUMBER_START;
							}
							else					   /*caller has multi minor*/
							{
								fileID[0] = 4311;	 
								state->sub_state_ = SecondDialState::STATE_SD_PLAY_INPUT_MINOR_NO_START;
							}
						}
						else						  /*system busy*/
						{
							fileID[0] = 4329;
							state->sub_state_ = SecondDialState::STATE_SD_PLAY_SYSTEM_BUSY_START;
						}
					}

				}
				else if( strcmp( cr->_inCalled, "95096699") == 0 || strcmp( cr->_inCalled, "059195096699") == 0)
				{
					if(g_isPrintProcess)
					{
						char tmp[256] = { 0 };
						sprintf(tmp ,"INFO: CallID(%d) Enter wj Modify InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s , FILE=%s,Line=%d", 
							cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);
						LogFileManager::getInstance()->write(Debug, "%s",tmp);
					}

					//begin  modify  by pengjh for virtual number 2013.03.25					
					//判断是否注册了虚拟副号码
					string minorNumber="";
					ResultDataUsage ret_vmn = DataUsage::instance()->getMinorNumber(cr->_inCaller, 6, minorNumber);
					if( ret_vmn == E_OPERATOR_SUCCED)
					{
						fileID[0] = 4310;
						state->minorNo_ = '6';
						strncpy( state->minorNumber_, minorNumber.c_str(), strlen(minorNumber.c_str()));
						state->callerRegState_ = SecondDialState::STATE_ALREADY_REGISTER;
						state->sub_state_ = SecondDialState::STATE_SD_PLAY_INPUT_NUMBER_START;
					}
					else if(  ret_vmn == E_MAJORNUMBER_NOT_EXESIT || ret_vmn == E_MINORINDEX_NUMBER_NOT_EXESIT)
					{
						//caller user not register simm app
						fileID[0] = 4446;
						state->callerRegState_ = SecondDialState::STATE_NOT_REGISTER;
						state->sub_state_ = SecondDialState::STATE_SD_PLAY_INPUT_NUMBER_START;
					}
					else
					{
						fileID[0] = 4329;
						state->callerRegState_= SecondDialState::STATE_UNKNOWN;
						state->sub_state_ = SecondDialState::STATE_SD_PLAY_SYSTEM_BUSY_START;
					}


				}

				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
					( void *)cr, secondDialHandler);
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: PPLEventRequest could not be successfully processed, Ack State = 0x%x", 
					ppl->getStatus());

				return OK;
			}
		}
		CASEC_CollectDigitStringAck(rCollect)
		{
			if(rCollect->getStatus() == 0x10)
			{
				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: CollectDigitString could not be successfully processed, Ack State = 0x%x", 
					rCollect->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if(rDspReq->getStatus())
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
					tag, 
					simm->secondDialHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case SecondDialState::STATE_SD_RECV_MINOR_NO_DTMF_CMPLT:
				case SecondDialState::STATE_SD_RECV_NUMBER_DTMF_CMPLT:
				case SecondDialState::STATE_SD_RECV_OUT_SERVICE_ACTION_DTMF_CMPLT:
				case SecondDialState::STATE_SD_RECV_RECORD_ACTION_DTMF_CMPLT:
					return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, secondDialHandler);
				case SecondDialState::STATE_SD_RECV_RECORD_STOP_DTMF_CMPLT:
					return simm->recordVoxStop(cr->_inSpan, cr->_inChan, (void *)cr, secondDialHandler);
				case SecondDialState::STATE_SD_RECV_RECORD_END:
					{
						int fileID[2] ={4306, -1}; /*留言完毕， 请挂机*/
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, secondDialHandler);
					}
				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if(playAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case SecondDialState::STATE_SD_PLAY_INPUT_NUMBER_START:
				case SecondDialState::STATE_SD_PLAY_NUMBER_ERROR_START:
					{
						state->sub_state_ = SecondDialState::STATE_SD_RECV_NUMBER_DTMF; 
						return simm->collectDigit( cr->_inSpan, cr->_inChan, MAX_COLLECT_DIGIT_LEN, rand()%5+500, "#" ,
							(void *)cr, simm->secondDialHandler);
					}
				case SecondDialState::STATE_SD_PLAY_INPUT_MINOR_NO_START:
				case SecondDialState::STATE_SD_PLAY_MINOR_NO_ERROR_START:
					{
						state->sub_state_ = SecondDialState::STATE_SD_RECV_MINOR_NO_DTMF; 
						return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
							(void *)cr, secondDialHandler);
					}
				case SecondDialState::STATE_SD_PLAY_RECORD_CUE_START:
					{
						state->sub_state_ = SecondDialState::STATE_SD_RECV_RECORD_STOP_DTMF; 
						return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
							(void *)cr, secondDialHandler);
					}
				case SecondDialState::STATE_SD_PLAY_CALLOUT_FAILED_START:
					{
						state->sub_state_ = SecondDialState::STATE_SD_RECV_RECORD_ACTION_DTMF; 
						return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
							(void *)cr, secondDialHandler);
					}
				case SecondDialState::STATE_SD_PLAY_OUT_SERVICE_CUE_START:
					{
						state->sub_state_ = SecondDialState::STATE_SD_RECV_RECORD_ACTION_DTMF; 
						return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
							(void *)cr, secondDialHandler);
					}
				case SecondDialState::STATE_SD_PLAY_CALLOUT_START:
				case SecondDialState::STATE_SD_PLAY_FORBID_CUE_START:
				case SecondDialState::STATE_SD_PLAY_SYSTEM_BUSY_START:
				case SecondDialState::STATE_SD_RECV_RECORD_END:
					return OK;
				default:
					return SK_NOT_HANDLED;
				}
			}
			else
			{
				return SK_NOT_HANDLED;
			}
			break;
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{
				if(state->sub_state_ == SecondDialState::STATE_SD_RECV_MINOR_NO_DTMF_CMPLT)
				{
					if(state->retry_ >= MAX_RETRY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, (void *)cr);

					int fileID[2] = {-1, -1};
					if(( state->minorNo_ > '0') && ( state->minorNo_ < '4')) 
					{
						int areIndex = -1;
						for(int i=0; i<state->minorList_.size(); i++)
						{
							if(state->minorList_[i].sequenceNo == (state->minorNo_-'0'))
							{
								areIndex = 1;
								break ;
							}
						}
						if(areIndex == 1)
						{
							state->retry_ = 0;
							fileID[0] = 4310; 
							state->sub_state_ =SecondDialState::STATE_SD_PLAY_INPUT_NUMBER_START;
						}
						else
						{
							fileID[0] = 4313; 
							state->sub_state_ =SecondDialState::STATE_SD_PLAY_MINOR_NO_ERROR_START;
						}
					}
					else /*输入的副号码序号错误*/
					{
						fileID[0] = 4330; 
						state->sub_state_ =SecondDialState::STATE_SD_PLAY_MINOR_NO_ERROR_START;
						state->retry_ ++ ;
					}

					int ret = simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, secondDialHandler);
					if( ret != OK )
					{
						LogFileManager::getInstance()->write(Brief, 
							"ERROR : In Channel(0x%x:0x%x)  playVoiceForID Failed, %s\n",
							cr->_inSpan, cr->_inChan,sk_errorText(ret));
					}
					return ret ;
				}
				if(state->sub_state_ == SecondDialState::STATE_SD_RECV_NUMBER_DTMF_CMPLT)
				{
					//zuosai add 20110210 for bug
					int len = strlen(state->targetNumber_);
					char endKey = state->targetNumber_[len-1];

					if(endKey != '#')
					{
						int fileID[2] = {4410, -1};
						state->sub_state_ = SecondDialState::STATE_SD_PLAY_NUMBER_ERROR_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,(void *)cr, secondDialHandler);
					}
					state->targetNumber_[len-1] = '\0';
					//zuosai add 20110210 for bug end

					int fileID[2] = {-1, -1};
					if( strcmp( cr->_inCalled, "9509699") == 0 || strcmp( cr->_inCalled, "05919509699") == 0  )
					{
			
						
						if(simm->isValidNumber(state->targetNumber_) != 0)
						{
							if((strlen(state->targetNumber_) == 8) && (strncmp(state->targetNumber_, "01", 2)))
							{
								int fileID[2] = {4349, -1}; /**/
								state->sub_state_ = SecondDialState::STATE_SD_PLAY_NUMBER_ERROR_START;
								return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,(void *)cr, secondDialHandler);
							}
							//zuosai add 20110210 for bug 
							if(strlen(state->targetNumber_) == 7)
							{
								int fileID[2] = {4349, -1}; 
								state->sub_state_ = SecondDialState::STATE_SD_PLAY_NUMBER_ERROR_START;
								return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,(void *)cr, secondDialHandler);
							}
							//zuosai add 20110210 for bug end

						char inCaller[MAX_NUMB_LEN], inCalled[MAX_NUMB_LEN];
						memset(inCaller, 0, sizeof(inCaller));
						memset(inCalled, 0, sizeof(inCalled));
						if( state->callerRegState_ == SecondDialState::STATE_ALREADY_REGISTER)
						{
							strncpy(inCaller, cr->_inCaller, MAX_NUMB_LEN-1);							
							sprintf(inCalled,"%s%c%s",simm->_accessCode,state->minorNo_, state->targetNumber_);
							LogFileManager::getInstance()->write(Brief,"inCalled:  %s",inCalled);
						}
						else if(state->callerRegState_ == SecondDialState::STATE_NOT_REGISTER)
						{
							strncpy(inCaller, cr->_inCaller, MAX_NUMB_LEN-1);
							strncpy(inCalled, state->targetNumber_, MAX_NUMB_LEN-1);
						}
						else
						{
							return OK;
						}

						if(simm->_makeDialRouter == 0)
						{
							LogFileManager::getInstance()->write(Brief,"simm->_makeDialRouter == 0");
							return OK;
						}

						string OutCaller, OutCalled;
						ResultMakeDialRoute result = simm->_makeDialRouter(inCaller, inCalled, OutCaller, OutCalled, cr->_nbrInfo);
						strncpy(cr->_outCaller, OutCaller.c_str(), sizeof(cr->_outCaller)-1);
						strncpy(cr->_outCalled, OutCalled.c_str(), sizeof(cr->_outCalled)-1);

						switch(result)
						{
						case RESULT_OK:
							{
								fileID[0] = 4312;
								state->sub_state_ = SecondDialState::STATE_SD_PLAY_CALLOUT_START;
							}
							break;
						case RESULT_NUMBER_ERROR:
							{
								fileID[0] = 4016; /**/
								state->sub_state_ = SecondDialState::STATE_SD_PLAY_NUMBER_ERROR_START;
							}
						case RESULT_MINOR_NOT_REG:
							{
								strncpy(cr->_outCaller, cr->_inCaller, MAX_NUMB_LEN-1);
								strncpy(cr->_outCalled, state->targetNumber_, MAX_NUMB_LEN-1);

								fileID[0] = 4312;
								state->sub_state_ = SecondDialState::STATE_SD_PLAY_CALLOUT_START;
							}
							break;
						case RESULT_MINOR_FORBID_CALLER:
							{
								fileID[0] = 4321; /*  */
								state->sub_state_ = SecondDialState::STATE_SD_PLAY_FORBID_CUE_START;
							}
							break;
						case RESULT_DIAL_TO_RECORD:
							{
								fileID[0] = 4122;
								state->sub_state_ = SecondDialState::STATE_SD_PLAY_RECORD_CUE_START;
							}
							break;
						case RESULT_MINOR_IN_LIMIT_TIME_FRAME:
						case RESULT_MINOR_OFF:
							{
								fileID[0] = 4335;
								state->sub_state_ = SecondDialState::STATE_SD_PLAY_OUT_SERVICE_CUE_START;
							}
							break;
						default:
							{
								fileID[0] = 4329; /**/
								state->sub_state_ = SecondDialState::STATE_SD_PLAY_SYSTEM_BUSY_START;
							}
							break;
						}

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, secondDialHandler);
					}
					else
					{
						fileID[0] = 4410; /**/
						state->sub_state_ = SecondDialState::STATE_SD_PLAY_NUMBER_ERROR_START;
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, secondDialHandler);
					}
				} /*SecondDialState::STATE_SD_RECV_NUMBER_DTMF_CMPLT*/
					else if ( strcmp( cr->_inCalled, "95096699") == 0 || strcmp( cr->_inCalled, "059195096699") == 0 )
					{

						LogFileManager::getInstance()->write(Brief,"SecDial state->targetNumber_ =:%s",state->targetNumber_);
						if(simm->isValidNumber_(state->targetNumber_) != 0)
						{
							char inCaller[24], inCalled[24];
							memset(inCaller, 0, sizeof(inCaller));
							memset(inCalled, 0, sizeof(inCalled));
							string  strCalled="";
							string  strTemp="";
							if( state->callerRegState_ == SecondDialState::STATE_ALREADY_REGISTER)
							{
								strncpy(inCaller, cr->_inCaller, 24-1);							
								//sprintf(inCalled,"%s%c%s",simm->_accessCode,state->minorNo_, state->targetNumber_);
								strCalled="950966";
								strTemp.append(state->targetNumber_);
								strCalled+=strTemp;
								strncpy(inCalled,strCalled.c_str(),24-1);
								LogFileManager::getInstance()->write(Brief,"STATE_ALREADY_REGISTER:strCalled:  %s",strCalled.c_str());
								LogFileManager::getInstance()->write(Brief,"STATE_ALREADY_REGISTER:inCalled:  %s",inCalled);
							}
							else if(state->callerRegState_ == SecondDialState::STATE_NOT_REGISTER)
							{
								strncpy(inCaller, cr->_inCaller, 24-1);
								//sprintf(inCalled,"%s6%s",simm->_accessCode,state->minorNo_, state->targetNumber_);
								strCalled="950966";
								strTemp.append(state->targetNumber_);
								strCalled+=strTemp;
								strncpy(inCalled,strCalled.c_str(),24-1);
								LogFileManager::getInstance()->write(Brief,"STATE_NOT_REGISTER:strCalled:  %s",strCalled.c_str());
								LogFileManager::getInstance()->write(Brief,"STATE_NOT_REGISTER:inCalled:  %s",inCalled);
							}
							else
							{
								return OK;
							}

							if(simm->_makeDialRouter == 0)
							{
								LogFileManager::getInstance()->write(Brief,"simm->_makeDialRouter == 0");
								return OK;
							}

							string OutCaller, OutCalled;
							ResultMakeDialRoute result = simm->_makeDialRouter(inCaller, inCalled, OutCaller, OutCalled, cr->_nbrInfo);
							strncpy(cr->_outCaller, OutCaller.c_str(), sizeof(cr->_outCaller)-1);
							strncpy(cr->_outCalled, OutCalled.c_str(), sizeof(cr->_outCalled)-1);
							LogFileManager::getInstance()->write(Brief,"SecDial _makeDialRouter:outCaller:%s",cr->_outCaller);
							LogFileManager::getInstance()->write(Brief,"SecDial _makeDialRouter:_outCalled:%s",cr->_outCalled);
							LogFileManager::getInstance()->write(Brief,"SecDial _makeDialRouter:Result =:%d",result);

							switch(result)
							{
							case RESULT_OK:
								{
									fileID[0] = 4312;
									state->sub_state_ = SecondDialState::STATE_SD_PLAY_CALLOUT_START;
								}
								break;
							case RESULT_NUMBER_ERROR:
								{
									fileID[0] = 4016; /**/
									state->sub_state_ = SecondDialState::STATE_SD_PLAY_NUMBER_ERROR_START;
								}
							case RESULT_MINOR_NOT_REG:
								{
									strncpy(cr->_outCaller, cr->_inCaller, MAX_NUMB_LEN-1);
									strncpy(cr->_outCalled, state->targetNumber_, MAX_NUMB_LEN-1);

									fileID[0] = 4312;
									state->sub_state_ = SecondDialState::STATE_SD_PLAY_CALLOUT_START;
								}
								break;
							case RESULT_MINOR_FORBID_CALLER:
								{
									fileID[0] = 4321; /*  */
									state->sub_state_ = SecondDialState::STATE_SD_PLAY_FORBID_CUE_START;
								}
								break;
							case RESULT_DIAL_TO_RECORD:
								{
									fileID[0] = 4122;
									state->sub_state_ = SecondDialState::STATE_SD_PLAY_RECORD_CUE_START;
								}
								break;
							case RESULT_MINOR_IN_LIMIT_TIME_FRAME:
							case RESULT_MINOR_OFF:
								{
									fileID[0] = 4335;
									state->sub_state_ = SecondDialState::STATE_SD_PLAY_OUT_SERVICE_CUE_START;
								}
								break;
							default:
								{
									fileID[0] = 4329; /**/
									state->sub_state_ = SecondDialState::STATE_SD_PLAY_SYSTEM_BUSY_START;
								}
								break;
							}

							return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
								(void *)cr, secondDialHandler);
						}
						else
						{
							fileID[0] = 4410; /**/
							state->sub_state_ = SecondDialState::STATE_SD_PLAY_NUMBER_ERROR_START;
							return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
								(void *)cr, secondDialHandler);
						}
						
					}
					


				} /*SecondDialState::STATE_SD_RECV_NUMBER_DTMF_CMPLT*/
				if( state->sub_state_ == SecondDialState::STATE_SD_RECV_RECORD_ACTION_DTMF_CMPLT)
				{
					if(state->actionKey_ == '1')
					{
						int fileID[2] = { 4335, -1}; 
						state->sub_state_ =SecondDialState::STATE_SD_PLAY_RECORD_CUE_START;

						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
							(void *)cr, secondDialHandler);
					}
					else if( state->actionKey_ == '*')
						return simm->processSecondDial(cr);
					else 
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				} /*SecondDialState::STATE_SD_RECV_RECORD_ACTION_DTMF_CMPLT*/
				else if ( state->sub_state_ == SecondDialState::STATE_SD_PLAY_SYSTEM_BUSY_START)
				{
					if( state->actionKey_ == '*')
					{
						return simm->processSecondDial(cr);
					}
					else
					{
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
						LogFileManager::getInstance()->write(Brief, "Call:  SecondDialState::STATE_SD_PLAY_SYSTEM_BUSY_START");
					}
				}
				else
					return SK_NOT_HANDLED;
			}
			else
				return (OK);
		}
		CASEC_RecordFileStartAck(rRecordStartAck)
		{
			if(rRecordStartAck->getStatus() == 0x10)
			{
				int ret = sk_setChannelHandler(cr->_inSpan, 
					cr->_inChan, (void *)cr, secondDialHandler);
			}
			break ;
		}
		CASEC_RecordFileStopAck(rRecordStopAck)
		{
			if(rRecordStopAck->getStatus() == 0x10)
			{
				if( state->sub_state_ == SecondDialState::STATE_SD_RECV_RECORD_STOP_DTMF_CMPLT)
				{
					int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
					simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, 
						(void *)cr, secondDialHandler);
				}

				state->sub_state_ = SecondDialState::STATE_SD_RECV_RECORD_END;
				//DataUsage::instance()->setVoiceMsg();
			}
			break ;
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			if( rCpe->getEvent() == RecvDtmf )
			{
				char buf[256];
				memset(buf, 0, sizeof(buf));
				simm->collectDigitResult( (SKC_Message *)rCpe, buf );

				if(strlen(buf) < 1)
					return OK;

				if(state->userTimerId_ != 0)
				{
					simm->cancelUserTimer(state->userTimerId_,(void *)cr,secondDialHandler); 
					state->userTimerId_ = 0;
				}

				if( state->sub_state_ == SecondDialState::STATE_SD_RECV_MINOR_NO_DTMF)
				{
					state->minorNo_ = buf[0];
					state->sub_state_ = SecondDialState::STATE_SD_RECV_MINOR_NO_DTMF_CMPLT;
				}
				if( state->sub_state_ == SecondDialState::STATE_SD_RECV_NUMBER_DTMF)
				{
					strncpy(state->targetNumber_, buf, MAX_NUMB_LEN-1);
					state->sub_state_ = SecondDialState::STATE_SD_RECV_NUMBER_DTMF_CMPLT;
				}
				if( state->sub_state_ == SecondDialState::STATE_SD_RECV_RECORD_ACTION_DTMF)
				{
					state->actionKey_ = buf[0];
					state->sub_state_ = SecondDialState::STATE_SD_RECV_RECORD_ACTION_DTMF_CMPLT;
				}
				if( state->sub_state_ == SecondDialState::STATE_SD_RECV_RECORD_STOP_DTMF)
				{
					state->actionKey_ = buf[0];
					state->sub_state_ = SecondDialState::STATE_SD_RECV_RECORD_STOP_DTMF_CMPLT;
				}

				int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
				return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, 
					(void *)cr, secondDialHandler);

			} /*RecvDtmf Event End*/
			else if ( (rCpe->getEvent() == FilePlayOK) ||
				(rCpe->getEvent() == FilePlayCmpt))
			{
				switch(state->sub_state_)
				{
				case SecondDialState::STATE_SD_PLAY_CALLOUT_START:
					cr->_connTime = time(NULL);
					return simm->processCallout(cr);
				case SecondDialState::STATE_SD_PLAY_FORBID_CUE_START:
				case SecondDialState::STATE_SD_PLAY_SYSTEM_BUSY_START:
				case SecondDialState::STATE_SD_RECV_RECORD_END:
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				case SecondDialState::STATE_SD_PLAY_RECORD_CUE_START:
					{
						cr->_connTime = time(NULL);
						char recordFileName[MAX_FILE_NAME];
						memset(recordFileName, 0, MAX_FILE_NAME);
						int ret = simm->makeRecordFileName(cr->_outCalled, recordFileName);
						if(ret != 0)
							return OK;
						ret = simm->recordVoxStart( cr->_inSpan, cr->_inChan, 
							getRecordFileId(), recordFileName,
							MAX_RECORD_TIME, tag, secondDialHandler);
						if(ret != OK)
						{
							LogFileManager::getInstance()->write(Brief, "ERROR: recordVoxStart: in:(0x%x,0x%x)",
								cr->_inSpan, cr->_inChan);
						}
						return ret;
					}
				case SecondDialState::STATE_SD_RECV_NUMBER_DTMF:
					{
						if(state->userTimerId_ != 0)
						{
							simm->cancelUserTimer(state->userTimerId_,(void *)cr,secondDialHandler); 
							state->userTimerId_ = 0;
							return OK;
						}
						else
						{
							return OK ;
						}
						/*
						if (rCpe->getEvent() == FilePlayOK)
						{
						state->userTimerId_ = getUserTimerId();
						return simm->startUserTimer(state->userTimerId_, CRC_PLAY_INTERVAL*2, (void *)cr, secondDialHandler);
						}
						*/
					}
				case SecondDialState::STATE_SD_RECV_MINOR_NO_DTMF:
					{
						if(rCpe->getEvent() == FilePlayOK)
						{
							state->userTimerId_ = getUserTimerId();
							return simm->startUserTimer(state->userTimerId_, CRC_PLAY_INTERVAL, (void *)cr, secondDialHandler);
						}
					}
				default:
					{
						LogFileManager::getInstance()->write(Brief, "INFO: FILE PLAY CMPT: in:(0x%x,0x%x) ",
							cr->_inSpan, cr->_inChan);

						return OK;
					}
				}
			}
			else if( rCpe->getEvent() == FileRecStart)
			{
				state->sub_state_ = SecondDialState::STATE_SD_RECV_RECORD_STOP_DTMF; 
				return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
					(void *)cr, secondDialHandler);
			}
			else if(rCpe->getEvent() == FileRecOK) 
			{
				if( state->sub_state_ == SecondDialState::STATE_SD_RECV_RECORD_STOP_DTMF)
				{
					int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
					simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, 
						(void *)cr, secondDialHandler);
				}

				state->sub_state_ = SecondDialState::STATE_SD_RECV_RECORD_END;

				return OK;	
			}
			else if(rCpe->getEvent() == FileRecCmpt)
			{
				return OK ;
			}
			else
			{
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
			}
			break ;
		}

		//mod by zhouqr 20100512
		CASEC_UserTimerAck(varNm)
		{
			if(state->userTimerId_ != 0)
			{
				simm->cancelUserTimer(state->userTimerId_,(void *)cr,secondDialHandler); 
				state->userTimerId_ = 0;
			}
			if (varNm->getStatus() == 0x10)
			{
				if (state->sub_state_ == SecondDialState::STATE_SD_RECV_NUMBER_DTMF)
				{
					if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

					int fileID[2] = {4310,-1};
					state->crcPlayCount_++;

					state->sub_state_ =SecondDialState::STATE_SD_PLAY_INPUT_NUMBER_START;
					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,( void *)cr, secondDialHandler);
				}
				else if(state->sub_state_ == SecondDialState::STATE_SD_RECV_MINOR_NO_DTMF)
				{
					if(state->crcPlayCount_ > MAX_CRC_PLAY_COUNT)
						return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);

					int fileID[2] = {4311,-1};
					state->crcPlayCount_++;

					state->sub_state_ =SecondDialState::STATE_SD_PLAY_INPUT_MINOR_NO_START;
					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,( void *)cr, secondDialHandler);
				}
			}
			return OK;
		}
		CASEC_CancelUserTimerAck(varNm)
		{
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) In Channel(0x%x,0x%x), CancelUserTimer, status = %d",
				cr->_callID,cr->_inSpan, cr->_inChan, varNm->getStatus());

			return OK;
		}

		CASEC_default 
		{
			return simm->defaultHandler(evt, tag);
		}
	}SKC_END_SWITCH;

	return (OK);
}


int SIMMApp::outServiceHandler( SK_Event *evt, void *tag )
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if(cr->_state == 0)
		return -1;

	if(cr->_state->state_ != State::STATE_OUT_SERVICE)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error In Channel:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	OutServiceState * state = reinterpret_cast<OutServiceState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->sub_state_);

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp, "INFO: CallID(%d) Enter Function:outServiceHandler() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);
	}		

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_PPLEventRequestAck(ppl)
		{
			if( ppl->getStatus() == 0x10)
			{
				cr->_isAnswer = 1 ; //应答主叫完成

				int fileID[2] = { -1, -1};

				if( state->result_ == RESULT_NUMBER_ERROR)
					fileID[0] = 4016;
				else if( state->result_ == RESULT_MINOR_OFF)
					fileID[0] = 4210;
				else if( state->result_ == RESULT_MINOR_FORBID_CALLER)
					fileID[0] = 4321;
				else if( state->result_ == RESULT_DIAL_TO_RECORD)
				{
					fileID[0] = 4122;
					state->sub_state_ = OutServiceState::STATE_OS_RECV_ACTION_DTMF; 
				}
				else
					fileID[0] = 4210;

				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
					(void *)cr, outServiceHandler);
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: PPLEventRequest could not be successfully processed, Ack State = 0x%x", 
					ppl->getStatus());

				return (OK);
			}
		}
		CASEC_DSPServiceRequestAck(rDspReq)
		{
			if(rDspReq->getStatus())
			{
				int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
					tag, 
					simm->outServiceHandler);
				if(ret != 0)
				{
					LogFileManager::getInstance()->write(Brief, 
						"ERROR: In Channel(0x%x,0x%x) sk_setChannelHandler Failed, %s\n",
						cr->_inSpan, cr->_inChan, sk_errorText(ret));
				}

				return OK;
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceRequest could not be successfully processed, Ack State = 0x%x", 
					rDspReq->getStatus());

				return OK;
			}
		}
		CASEC_DSPServiceCancelAck(rCancelAck)
		{
			if(rCancelAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case OutServiceState::STATE_OS_RECV_ACTION_DTMF_CMPLT:
					{
						if(state->active_play_end == 1)
						{
							int fileID[2] = {-1, -1};
							if( state->actionKey_ == '1') //record 
							{
								fileID[0] = 4335; 
								state->sub_state_ =OutServiceState::STATE_OS_PLAY_RECORD_START;
								return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
									(void *)cr, outServiceHandler);
							}
							else
								return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
						}
						else
							return simm->playFileStop(cr->_inSpan, cr->_inChan, (void *)cr, outServiceHandler);
					}
				case OutServiceState::STATE_OS_RECV_RECORD_STOP_DTMF_CMPLT:
					return simm->recordVoxStop(cr->_inSpan, cr->_inChan, (void *)cr, outServiceHandler);
				case OutServiceState::STATE_OS_RECV_RECORD_END:
					{
						/*
						int fileID[2] ={4306, -1}; //留言完毕， 请挂机
						return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, outServiceHandler);
						*/
						return OK;
					}
				default:
					return OK;
				}
			}
			else
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: DSPServiceCancel could not be successfully processed, Ack State = 0x%x", 
					rCancelAck->getStatus());

				return OK;
			}
		}
		CASEC_PlayFileStartAck(playAck)
		{
			if(playAck->getStatus() == 0x10)
			{
				switch(state->sub_state_)
				{
				case OutServiceState::STATE_OS_RECV_ACTION_DTMF:
					{
						return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
							(void *)cr, outServiceHandler);
					}
				case OutServiceState::STATE_OS_PLAY_EXPLAIN_START:
				case OutServiceState::STATE_OS_PLAY_RECORD_START:
				case OutServiceState::STATE_OS_RECV_RECORD_END:
					return OK;
				default:
					return SK_NOT_HANDLED;
				}
			}
			else
				return OK;

			break;
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{
				///zuosai Modify 20100819
				/*
				if( state->sub_state_ == OutServiceState::STATE_OS_RECV_ACTION_DTMF_CMPLT )
				{
				int fileID[2] = {-1, -1};
				if( state->actionKey_ == '1') //record 
				{
				fileID[0] = 4335; 
				state->sub_state_ =OutServiceState::STATE_OS_PLAY_RECORD_START;
				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
				(void *)cr, outServiceHandler);
				}
				else
				return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				}
				else
				return OK;
				*/
				///zuosai Modify 20100819 end
				
				//- add by pengjh for fj notify sms
				char logText[1024]={0};
				LogFileManager::getInstance()->write(Brief, "INFO : Now Begin VoiceMsg8817...,CallType = %d ",cr->_nbrInfo.callType);
				if(cr->_nbrInfo.callType == 1) 
				{
					if(DataUsage::instance()->setVoiceMsg(cr->_nbrInfo.originalNumber, state->filePath_, 
						 cr->_nbrInfo.minorNumber)!=E_OPERATOR_SUCCED)
						LogFileManager::getInstance()->write(Brief, "ERROR : LEAVE VoiceMsg FAILED...");
							
					//DialNotifyLibWrapper::instance()->submitRecordNoify(cr->_nbrInfo.minorNumber, cr->_outCalled, cr->_nbrInfo.originalNumber);
					sprintf(logText, "caller = %s, userNumber = %s, minorNumber = %s", cr->_nbrInfo.minorNumber, cr->_outCalled, cr->_nbrInfo.originalNumber);
					LogFileManager::getInstance()->write(Debug, "INFO : Send voice record Notify by short message, %s",logText);
					//////send short message ///
					string tmp;
					tmp = "副号码业务提醒:";
					tmp += cr->_nbrInfo.minorNumber;
					tmp += "给您的副号";
					tmp += cr->_nbrInfo.originalNumber;
					tmp += "留言了,请拨9509600按2键收听.";
					int ret = DialNotifyLibWrapper::instance()->submitGeneralMsg(_accessCode, cr->_outCalled, tmp.c_str());
					if(ret == -1)
						LogFileManager::getInstance()->write(Brief, "ERROR : submitGeneralMsg Failed...");
					else
						LogFileManager::getInstance()->write(Debug, "INFO : submitGeneralMsg Success...");	
				}
				else 
				{
					 	if(DataUsage::instance()->setVoiceMsg(cr->_nbrInfo.minorNumber, state->filePath_, 
						cr->_nbrInfo.originalNumber)!= E_OPERATOR_SUCCED)
						LogFileManager::getInstance()->write(Brief, "ERROR : LEAVE VoiceMsg FAILED...");


					//DialNotifyLibWrapper::instance()->submitRecordNoify(cr->_nbrInfo.originalNumber, cr->_outCalled, cr->_nbrInfo.minorNumber);
					sprintf(logText, "caller = %s, userNumber = %s, minorNumber = %s", 
								cr->_nbrInfo.originalNumber, cr->_outCalled, cr->_nbrInfo.minorNumber);
					LogFileManager::getInstance()->write(Debug, "INFO : Send voice record Notify by short message, %s",logText);
					//////send short message ///
					string tmp;
					tmp = "副号码业务提醒:";
					tmp += cr->_nbrInfo.originalNumber;
					tmp += "给您的副号";
					tmp += cr->_nbrInfo.minorNumber;
					tmp += "留言了,请拨9509600按2键收听.";
					int ret = DialNotifyLibWrapper::instance()->submitGeneralMsg(_accessCode, cr->_outCalled, tmp.c_str());
					if(ret == -1)
						LogFileManager::getInstance()->write(Brief, "ERROR : submitGeneralMsg Failed...");
					else
						LogFileManager::getInstance()->write(Debug, "INFO : submitGeneralMsg Success...");
				}

				
				return OK;
			}
			else
				return OK;
		}
		CASEC_RecordFileStartAck(rRecordStartAck)
		{
			if(rRecordStartAck->getStatus() == 0x10)
			{
				int ret = sk_setChannelHandler(cr->_inSpan, 
					cr->_inChan, (void *)cr, outServiceHandler);
			}
			break ;
		}
		CASEC_RecordFileStopAck(rRecordStopAck)
		{
			if(rRecordStopAck->getStatus() == 0x10)
			{
				/*
				if( state->sub_state_ == OutServiceState::STATE_OS_RECV_RECORD_STOP_DTMF_CMPLT)
				{
				int type = 0x01 ; 
				simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, 
				(void *)cr, outServiceHandler);
				}

				state->sub_state_ = OutServiceState::STATE_OS_RECV_RECORD_END;
				*/
				/* 呼入：主叫 和 被叫去掉接入码 */
				LogFileManager::getInstance()->write(Brief, 
					"INFO : Now Begin VoiceMsg8896...");
				char logText[512]={0};
				if(cr->_nbrInfo.callType == 1) 
				{
					if(DataUsage::instance()->setVoiceMsg(cr->_nbrInfo.originalNumber, state->filePath_, 
						cr->_nbrInfo.minorNumber)!=E_OPERATOR_SUCCED)
						LogFileManager::getInstance()->write(Brief, "ERROR : LEAVE VoiceMsg FAILED...");
					//DialNotifyLibWrapper::instance()->submitRecordNoify(cr->_nbrInfo.minorNumber, cr->_outCalled, cr->_nbrInfo.originalNumber);
					sprintf(logText, "caller = %s, userNumber = %s, minorNumber = %s", cr->_nbrInfo.minorNumber, cr->_outCalled, cr->_nbrInfo.originalNumber);
					LogFileManager::getInstance()->write(Debug, "INFO : Send voice record Notify by short message, %s",logText);
					//////send short message ///
					string tmp;
					tmp = "副号码业务提醒:";
					tmp += cr->_nbrInfo.minorNumber;
					tmp += "给您的副号";
					tmp += cr->_nbrInfo.originalNumber;
					tmp += "留言了,请拨9509600按2键收听.";
					int ret = DialNotifyLibWrapper::instance()->submitGeneralMsg(_accessCode, cr->_outCalled, tmp.c_str());
					if(ret == -1)
						LogFileManager::getInstance()->write(Brief, "ERROR : submitGeneralMsg Failed...");
					else
						LogFileManager::getInstance()->write(Debug, "INFO : submitGeneralMsg Success...");	
				}
				else
				{
					if(DataUsage::instance()->setVoiceMsg(cr->_nbrInfo.minorNumber, state->filePath_, 
						cr->_nbrInfo.originalNumber)!= E_OPERATOR_SUCCED)
						LogFileManager::getInstance()->write(Brief, 
						"ERROR : LEAVE VoiceMsg FAILED...");
					//DialNotifyLibWrapper::instance()->submitRecordNoify(cr->_nbrInfo.originalNumber, cr->_outCalled, cr->_nbrInfo.minorNumber);
					sprintf(logText, "caller = %s, userNumber = %s, minorNumber = %s", 
								cr->_nbrInfo.originalNumber, cr->_outCalled, cr->_nbrInfo.minorNumber);
					LogFileManager::getInstance()->write(Debug, "INFO : Send voice record Notify by short message, %s",logText);
					//////send short message ///
					string tmp;
					tmp = "副号码业务提醒:";
					tmp += cr->_nbrInfo.originalNumber;
					tmp += "给您的副号";
					tmp += cr->_nbrInfo.minorNumber;
					tmp += "留言了,请拨9509600按2键收听.";
					int ret = DialNotifyLibWrapper::instance()->submitGeneralMsg(_accessCode, cr->_outCalled, tmp.c_str());
					if(ret == -1)
						LogFileManager::getInstance()->write(Brief, "ERROR : submitGeneralMsg Failed...");
					else
						LogFileManager::getInstance()->write(Debug, "INFO : submitGeneralMsg Success...");
				}

				if(state->sub_state_ == OutServiceState::STATE_OS_RECV_RECORD_STOP_DTMF_CMPLT)
				{
					int fileID[2] ={4306, -1}; //留言完毕， 请挂机
					state->sub_state_ = OutServiceState::STATE_OS_RECV_RECORD_END;
					//has send notify at stop rec ack
					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, outServiceHandler);
				}	
				return OK;
			}
			break ;
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			LogFileManager::getInstance()->write(Debug, "INFO : CallID(%d) rCpe->getEvent() == 0x%x", cr->_callID,rCpe->getEvent());

			if( rCpe->getEvent() == RecvDtmf )
			{
				char buf[256];
				memset(buf, 0, sizeof(buf));
				simm->collectDigitResult( (SKC_Message *)rCpe, buf );

				if(strlen(buf) < 1)
					return OK;

				if( state->sub_state_ == OutServiceState::STATE_OS_RECV_ACTION_DTMF)
				{
					state->actionKey_ = buf[0];
					state->sub_state_ = OutServiceState::STATE_OS_RECV_ACTION_DTMF_CMPLT;
				}
				if( state->sub_state_ == OutServiceState::STATE_OS_RECV_RECORD_STOP_DTMF)
				{
					state->actionKey_ = buf[0];
					state->sub_state_ = OutServiceState::STATE_OS_RECV_RECORD_STOP_DTMF_CMPLT;
				}

				int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
				return simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, 
					(void *)cr, outServiceHandler);
			} /*RecvDtmf Event End*/
			else if ( (rCpe->getEvent() == FilePlayOK) ||
				(rCpe->getEvent() == FilePlayCmpt))
			{
				switch(state->sub_state_)
				{
				case OutServiceState::STATE_OS_RECV_RECORD_END:
					return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
				case OutServiceState::STATE_OS_RECV_ACTION_DTMF:
					{
						if(rCpe->getEvent() == FilePlayOK)
							state->active_play_end = 1;
						return OK;
					}
					///zuosai add 20100819
				case OutServiceState::STATE_OS_RECV_ACTION_DTMF_CMPLT:
					{
						int fileID[2] = {-1, -1};
						if( state->actionKey_ == '1') //record 
						{
							fileID[0] = 4335; 
							state->sub_state_ =OutServiceState::STATE_OS_PLAY_RECORD_START;
							return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
								(void *)cr, outServiceHandler);
						}
						else
							return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
					}
					///zuosai add 20100819 end
				case OutServiceState::STATE_OS_PLAY_RECORD_START:
					{
						char recordFileName[MAX_FILE_NAME];
						memset(recordFileName, 0, MAX_FILE_NAME);
						int ret = simm->makeRecordFileName(cr->_outCalled, recordFileName);
						if(ret != 0)
							return OK;
						ret = simm->recordVoxStart( cr->_inSpan, cr->_inChan, 
							getRecordFileId(), recordFileName,
							MAX_RECORD_TIME, tag, outServiceHandler);
						if(ret != OK)
						{
							LogFileManager::getInstance()->write(Brief, "ERROR: recordVoxStart: in:(0x%x,0x%x)",
								cr->_inSpan, cr->_inChan);
						}

						strncpy(state->filePath_, recordFileName, MAX_FILE_NAME-1);
						return ret;
					}
				default:
					return OK;
				}
			}
			else if( rCpe->getEvent() == FileRecStart)
			{
				state->sub_state_ = OutServiceState::STATE_OS_RECV_RECORD_STOP_DTMF; 
				return simm->dspServiceRequest( cr->_inSpan, cr->_inChan, WaitBeforeOutP+5,
					(void *)cr, outServiceHandler);
			}
			else if(rCpe->getEvent() == FileRecOK || rCpe->getEvent() == FileRecCmpt)  
			{
				int ret=OK;
				if( state->sub_state_ == OutServiceState::STATE_OS_RECV_RECORD_STOP_DTMF)
				{
					int type = 0x01 ; /* 1: receive DTMF  2: Energy Detect */
					state->sub_state_ = OutServiceState::STATE_OS_RECV_RECORD_END;
					ret = simm->dspServiceCancel(cr->_inSpan, cr->_inChan, type, 
						(void *)cr, outServiceHandler);
				}
				else if(state->sub_state_ == OutServiceState::STATE_OS_RECV_RECORD_STOP_DTMF_CMPLT)
				{
					int fileID[2] ={4306, -1}; //留言完毕， 请挂机
					state->sub_state_ = OutServiceState::STATE_OS_RECV_RECORD_END;
					//has send notify at stop rec ack
					return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,
						(void *)cr, outServiceHandler);
				}
				else
				{
					return OK;
				}

				LogFileManager::getInstance()->write(Brief, 
					"INFO : Now Begin VoiceMsg9064...");
				char logText[512]={0};
				if(cr->_nbrInfo.callType == 1) 
				{
					if(DataUsage::instance()->setVoiceMsg(cr->_nbrInfo.originalNumber, state->filePath_, 
						cr->_nbrInfo.minorNumber)!=E_OPERATOR_SUCCED)
						LogFileManager::getInstance()->write(Brief, "ERROR : LEAVE VoiceMsg FAILED...");
										//DialNotifyLibWrapper::instance()->submitRecordNoify(cr->_nbrInfo.minorNumber, cr->_outCalled, cr->_nbrInfo.originalNumber);
					sprintf(logText, "caller = %s, userNumber = %s, minorNumber = %s", cr->_nbrInfo.minorNumber, cr->_outCalled, cr->_nbrInfo.originalNumber);
					LogFileManager::getInstance()->write(Debug, "INFO : Send voice record Notify by short message, %s",logText);
					//////send short message ///
					string tmp;
					tmp = "副号码业务提醒:";
					tmp += cr->_nbrInfo.minorNumber;
					tmp += "给您的副号";
					tmp += cr->_nbrInfo.originalNumber;
					tmp += "留言了,请拨9509600按2键收听.";
					int ret = DialNotifyLibWrapper::instance()->submitGeneralMsg(_accessCode, cr->_outCalled, tmp.c_str());
					if(ret == -1)
						LogFileManager::getInstance()->write(Brief, "ERROR : submitGeneralMsg Failed...");
					else
						LogFileManager::getInstance()->write(Debug, "INFO : submitGeneralMsg Success...");	
				}
				else
				{
					if(DataUsage::instance()->setVoiceMsg(cr->_nbrInfo.minorNumber, state->filePath_, 
						cr->_nbrInfo.originalNumber)!= E_OPERATOR_SUCCED)
						LogFileManager::getInstance()->write(Brief, 
						"ERROR : LEAVE VoiceMsg FAILED...");
										//DialNotifyLibWrapper::instance()->submitRecordNoify(cr->_nbrInfo.originalNumber, cr->_outCalled, cr->_nbrInfo.minorNumber);
					sprintf(logText, "caller = %s, userNumber = %s, minorNumber = %s", 
								cr->_nbrInfo.originalNumber, cr->_outCalled, cr->_nbrInfo.minorNumber);
					LogFileManager::getInstance()->write(Debug, "INFO : Send voice record Notify by short message, %s",logText);
					//////send short message ///
					string tmp;
					tmp = "副号码业务提醒:";
					tmp += cr->_nbrInfo.originalNumber;
					tmp += "给您的副号";
					tmp += cr->_nbrInfo.minorNumber;
					tmp += "留言了,请拨9509600按2键收听.";
					int ret = DialNotifyLibWrapper::instance()->submitGeneralMsg(_accessCode, cr->_outCalled, tmp.c_str());
					if(ret == -1)
						LogFileManager::getInstance()->write(Brief, "ERROR : submitGeneralMsg Failed...");
					else
						LogFileManager::getInstance()->write(Debug, "INFO : submitGeneralMsg Success...");
				}

				return ret;	
			}
			else if(rCpe->getEvent() == FileRecCmpt)
			{
				return OK ;
			}
			else
			{
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
			}
			break ;
		}
		CASEC_default 
		{
			return simm->defaultHandler(evt, tag);
		}
	}SKC_END_SWITCH;

	return (OK);
}

int SIMMApp::returnChannels(int rel_span, int rel_chan, void *tag) 
{
	XLC_ReleaseChannel rel_channel;

	/* No distant end channel, perhaps it was already released.
	* Since memory was allocated in inboundCallHandler in order to start 
	* passing the call record around, delete the memory here.
	*/

	rel_channel.setSpanA(rel_span);
	rel_channel.setSpanB(rel_span);
	rel_channel.setChannelA(rel_chan);
	rel_channel.setChannelB(rel_chan);
	rel_channel.send(NULL, handleAcks);

	LogFileManager::getInstance()->write(Verbose, "INFO: returnChannels: (0x%x,0x%x)", rel_span, rel_chan);     

	return(OK);
}

int SIMMApp::processChannelReleasedWithData(int rel_span, int rel_chan, void *tag)
{
	int cnctd_chan, cnctd_span, stat;
	ChannelData *chandat;
	XLC_ReleaseChannel rel_channel;

	CallRecord *cr = (CallRecord *)tag;
	if(tag == 0)
		return 0;

	/* Return the channel to the channel manager */

	/* By using sk_setChannelData() in handleOutseizeAck, the connecting 
	* Channel / Span were attached as stored data to the channel that 
	* is about to be released.  At this point, simply use 
	* sk_getChannelData() to retrieve the data from the 
	* channel manager and release that channel as well.
	*/
	chandat = (ChannelData *)sk_getChannelData(rel_span,rel_chan);
	if (chandat) 
	{
		cnctd_span = chandat->cnctdSpan;
		cnctd_chan = chandat->cnctdChan;

		/* Having determined from stored data the channel connected to 
		* the one previously returned, now release it as well.
		*/
		rel_channel.setSpanA(cnctd_span);
		rel_channel.setSpanB(cnctd_span);
		rel_channel.setChannelA(cnctd_chan);
		rel_channel.setChannelB(cnctd_chan);
		rel_channel.send(tag, handleAcks);

		LogFileManager::getInstance()->write(Verbose, "INFO: CallID(%d) Released Connected Channel:(0x%x,0x%x)", 
			cr->_callID, cnctd_span,  cnctd_chan);               

		// Clear out the data associated with this channel
		sk_setChannelData(rel_span, rel_chan, NULL);

		/* Since memory was allocated in handleOutseizeAck in order to pass 
		* a pointer to sk_setChannelData(), delete the memory here.
		*/
		delete chandat;

		//Modify by zhouqr 2009-5-8
		sk_returnChannel(rel_span, rel_chan);

	}  // end if
	else
	{
		/* No distant end channel, perhaps it was already released.
		* Since memory was allocated in inboundCallHandler in order to start 
		* passing the call record around, delete the memory here.
		*/
		//Modify by zhouqr 2009-5-8
		sk_returnChannel(rel_span, rel_chan);

		rel_channel.setSpanA(rel_span);
		rel_channel.setSpanB(rel_span);
		rel_channel.setChannelA(rel_chan);
		rel_channel.setChannelB(rel_chan);
		rel_channel.send(tag, handleAcks);

		LogFileManager::getInstance()->write(Verbose, "INFO: CallID(%d) ChannelReleasedWithData:(0x%x,0x%x)", 
			cr->_callID, rel_span, rel_chan);   
	}

	if(tag == 0)
		return OK ;

	writeCallRecord((CallRecord *)tag, rel_span, rel_chan);

	return(OK);
}

int SIMMApp::handleAcks(SK_Event *evt, void *tag)
{
	SKC_Message *msg = evt->IncomingCMsg;

	if (!tag)
	{
		LogFileManager::getInstance()->write(Brief, 
			"INFO: In handleAcks and tag is NULL.");
		return(OK);
	}

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr;
	int ret;

	SKC_MSG_SWITCH(msg)
	{
		CASEC_ConnectAck(connectAck)
		{
			if(connectAck->getStatus() != 0x10)
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: Channel Connect Failed: State = %d",connectAck->getStatus());

				return OK;
			}

			char logText[128];
			memset(logText, 0, sizeof(logText));
			sprintf(logText, "CallID(%d) ConnectAck (0x%x, 0x%x) and (0x%x, 0x%x)",
				cr->_callID,
				cr->_inSpan,
				cr->_inChan,
				cr->_outSpan,
				cr->_outChan);
			LogFileManager::getInstance()->write(Verbose, "INFO: %s",logText);

			time_t connect_time = time(0);
			cr->_rcvTime = connect_time;
			//cr->_connTime = connect_time;

			ret = sk_setChannelHandler(cr->_outSpan, 
				cr->_outChan, 
				(void *)cr,
				simm->genericHandler);
			if( ret != OK)
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: in(0x%x, 0x%x) setChannelHandler: %s\n", 
					cr->_outSpan, cr->_outChan,sk_errorText(ret));
			}

			ret = sk_setChannelHandler(cr->_inSpan, 
				cr->_inChan, 
				(void *)cr,
				simm->genericHandler);
			if( ret != OK)
			{
				LogFileManager::getInstance()->write(Brief, 
					"ERROR: in(0x%x, 0x%x) setChannelHandler: %s\n", 
					cr->_inSpan, cr->_inChan,sk_errorText(ret));
			}

			//zuosai add 20101227 for callforward
			if(cr->_callForwardList.size())
			{
				cr->_userTimerID = getUserTimerId();
				int ret = simm->startUserTimer(cr->_userTimerID, WAIT_REMOTE_INTERVAL, (void *)cr, genericHandler);
				LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Start Wait remote pickup UserTimer( TimerID = %d, Ret = 0x%x)", 
					cr->_callID,cr->_userTimerID, ret);
			}
			//zuosai add 20101227 for callforward end

			break;
		}
		CASEC_ReleaseChannelAck(releaseAck)
		{
			if(cr->_reference == 0)
			{
				LogFileManager::getInstance()->write(Debug,"INFO: CallID(%d) ReleaseChannelAck And Release CallRecord",cr->_callID);

			}
			break;
		}
		CASEC_default
		{
			return(SK_NOT_HANDLED);
		}
	} SKC_END_SWITCH;

	return(OK);
}

void SIMMApp::shutdown(int) 
{
	LogFileManager::getInstance()->write(Brief,"Attempting graceful shutdown");

	LogFileManager::getInstance()->finish();

	exit(0);
}

///////////////////////////////
int SIMMApp::initOneAddrInfo( unsigned char *addr, int span, int chan )
{
	if( ( span < 0 ) || ( chan < 0 ) )
	{
		return -1;
	}
	else
	{
		addr[0] = 0;
		addr[1] = 1;
		addr[2] = 0x0d;
		addr[3] = 3;
		addr[4] = span >> 8;
		addr[5] = span & 0x00ff;
		addr[6] = chan;
	}

	return 0;
}

int SIMMApp::sendMessage(MsgStruct *msg, void *tag, HandlerFunc *func)
{
	int stat;

	stat = sk_sendMsgStruct(msg, tag, func);
	if( stat != OK )
	{
		LogFileManager::getInstance()->write(Verbose,"Error of %s\n",sk_errorText(stat));
	}

	return stat ;
}

int SIMMApp::sendPPLEvent( int span, int chan, int Signal, void *tag, HandlerFunc *func)
{
	XL_PPLEventRequest nPPL;

	sk_initMsg( &nPPL, TAG_PPLEventRequest );
	nPPL.ComponentID = L3P_CIC;

	initOneAddrInfo(nPPL.AddrInfo, span, chan );
	switch( Signal )
	{
	case ISUP_ACM:
		nPPL.PPLEvent = ISUP_REQ_ACM;
		break;

	case ISUP_ANM:
		nPPL.PPLEvent = ISUP_REQ_ANM;
		break;

	case ISUP_INR:
		nPPL.PPLEvent = ISUP_REQ_INR;
		break;

	default:
		return -1;
	}

	nPPL.ICBCount = 0x01;			// one ICB
	nPPL.Data[0] = 0x02;			// ICB type. 0x01:action  0x02:data
	nPPL.Data[1] = ISUP_CPC;		// ICB Subtype: SS7Format

	if( Signal == ISUP_ACM )
	{
		/*
		nPPL.Data[2] = 10;			// Data Length of ICB data
		nPPL.Data[3] = ISUP_ACM;	// ISUP Msg of ACM
		nPPL.Data[4] = 0x02;		// ICB Parameter count
		nPPL.Data[5] = 0x11;		// Type: back call indicator
		nPPL.Data[6] = 0x02;		// Length
		nPPL.Data[7] = 0x14;		// Data
		nPPL.Data[8] = 0x24;		// Data

		nPPL.Data[9] = 0x12;		// Type: reason
		nPPL.Data[10] = 0x02;		// Length
		nPPL.Data[11] = 0x80;		// Data
		nPPL.Data[12] = 0x94;		// Data
		*/

		nPPL.Data[2] = 0x06;		// Data Length of ICB data
		nPPL.Data[3] = ISUP_ACM;	// ISUP Msg of ACM
		nPPL.Data[4] = 0x01;		// ICB Parameter count
		nPPL.Data[5] = 0x11;		// Type: back call indicator
		nPPL.Data[6] = 0x02;		// Length
		nPPL.Data[7] = 0x14;		// Data
		nPPL.Data[8] = 0x24;		// Data
	}
	else if( Signal == ISUP_ANM )
	{
		nPPL.Data[2] = 0x06;		// Data Length of ICB data
		nPPL.Data[3] = ISUP_ANM;	// ISUP Msg of ANM
		nPPL.Data[4] = 0x01;		// ICB Parameter count
		nPPL.Data[5] = 0x11;		// Type: back call Ind
		nPPL.Data[6] = 0x02;		// Len
		nPPL.Data[7] = 0x16;
		nPPL.Data[8] = 0x04;		// Data
	}
	else if( Signal == ISUP_INR )
	{
		nPPL.Data[2] = 0x06;		// Data Length of ICB data
		nPPL.Data[3] = ISUP_INR;	// ISUP Msg of INF
		nPPL.Data[4] = 0x01;		// ICB Parameter count
		nPPL.Data[5] = 0x0e;		// Type: back call Ind
		nPPL.Data[6] = 0x02;		// Len
		nPPL.Data[7] = 0x0b;
		nPPL.Data[8] = 0x00;		// Data
	}
	else
	{
		return -1;
	}

	int ret = sendMessage((MsgStruct *)&nPPL, tag, func);
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: In Channel(0x%x:0x%x)  sendPPLEvent Failed, %s\n",
			span, chan,sk_errorText(ret));
	}

	return ret;
}
///////////////////////////////

int SIMMApp::playVoiceFile( int span, int chan, int dspSlot, int fileId, char *filename, int offset, 
						   void *tag, HandlerFunc *func)
{
	int  nLen;
	UBYTE Data[211];
	XL_PlayFileStart pfs;

	sk_initMsg( &pfs, TAG_PlayFileStart );

	pfs.AddrInfo[0] = 0x00;
	pfs.AddrInfo[1] = 0x02;
	pfs.AddrInfo[2] = 0x0d;
	pfs.AddrInfo[3] = 0x03;
	pfs.AddrInfo[4] = span >> 8;
	pfs.AddrInfo[5] = span & 0x00ff;
	pfs.AddrInfo[6] = chan;
	pfs.AddrInfo[7] = 0x01;
	pfs.AddrInfo[8] = 0x01;
	pfs.AddrInfo[9] = dspSlot;

	pfs.FileCount   = 1;
	pfs.DataType    = 0x00;		// TLV

	pfs.TLVCount    = 0;

	nLen = 0;
	nLen += setIntTLV( pfs.Data+nLen, 0x05e0, 4, fileId );
	pfs.TLVCount ++;

	/* 0x0f0000 < fileid < 0x100000: VIF Bypass */
	/* fileid > 0x100000:            filename */
	/* fileid < 0x0f0000:            ID */
	if( fileId >= 0x000f0000 )
	{
		if( strstr(filename, ".wav") != NULL || strstr(filename, ".WAV") != NULL )
			nLen += setIntTLV( pfs.Data+nLen, 0x05e1, 2, 0x0200 );	// File Format
		else if( strstr(filename, ".vox") != NULL || strstr(filename, ".VOX") != NULL )
			nLen += setIntTLV( pfs.Data+nLen, 0x05e1, 2, 0x0003 );	// File Format: 3 - 8K*4Bit  4 - 6K*4Bit
		else
			nLen += setIntTLV( pfs.Data+nLen, 0x05e1, 2, 0 );		// File Format
		pfs.TLVCount ++;

		// Filename
		Data[0] = 0x00;		// 1th server
		Data[1] = 0x01;
		Data[2] = 0x00;		// 2th server
		Data[3] = 0x02;
		strcpy( (char *)Data+4, filename );
		nLen += setUByteTLV( (pfs.Data)+nLen, 0x05e2, strlen(filename)+4, Data );
		pfs.TLVCount ++;

		if( offset > 0 )
		{
			// Offset and Length
			Data[0] = offset >> 24;		// Offset
			Data[1] = (offset & 0x00ffffff) >> 16;
			Data[2] = (offset & 0x0000ffff) >> 8;
			Data[3] = offset & 0x000000ff;
			Data[4] = 0x7f;		// Length
			Data[5] = 0xff;
			Data[6] = 0xff;
			Data[7] = 0xff;
			nLen += setUByteTLV( (pfs.Data)+nLen, 0x0614, 8, Data );
			pfs.TLVCount ++;
		}
		/*
		// Gain
		nLen += setIntTLV( pfs.Data+nLen, 0x05e3, 1, 0x00 );
		pfs.TLVCount ++;

		// Barge In
		nLen += setIntTLV( pfs.Data+nLen, 0x05e5, 1, 0 );
		pfs.TLVCount ++;

		// Play File Queue
		nLen += setIntTLV( pfs.Data+nLen, 0x05f2, 1, 0 );
		pfs.TLVCount ++;
		*/
	}

	nLen += setIntTLV( pfs.Data+nLen, 0x05e6, 1, 3 );		// File Event Desc
	pfs.TLVCount ++;
	/*
	// Playback Repeat
	Data[0] = 0x00;		// times to repeat
	Data[1] = 0x03;
	Data[2] = 0x00;		// Interval between repetitions: 500ms
	Data[3] = 0x05;
	Data[4] = 0x00;		// Total time file is played: 50s
	Data[5] = 0x00;
	nLen += setUByteTLV( (pfs.Data)+nLen, 0x05e7, 6, Data );
	pfs.TLVCount ++;
	*/
	int ret = sendMessage((MsgStruct *)&pfs, tag, func);
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: In Channel(0x%x:0x%x)  playVoiceFile Failed, %s\n",
			span, chan,sk_errorText(ret));
	}

	return ret;
}

int SIMMApp::playVoiceForID( int span, int chan, int dspSlot, int fileId[], void *tag, HandlerFunc *func )
{
	int  nLen, i;
	XL_PlayFileStart pfs;
	sk_initMsg( &pfs, TAG_PlayFileStart );

	pfs.AddrInfo[0] = 0x00;
	pfs.AddrInfo[1] = 0x02;
	pfs.AddrInfo[2] = 0x0d;
	pfs.AddrInfo[3] = 0x03;
	pfs.AddrInfo[4] = span >> 8;
	pfs.AddrInfo[5] = span & 0x00ff;
	pfs.AddrInfo[6] = chan;
	pfs.AddrInfo[7] = 0x01;
	pfs.AddrInfo[8] = 0x01;
	pfs.AddrInfo[9] = dspSlot;

	pfs.DataType    = 0x00;		// TLV

	pfs.TLVCount    = 0;

	nLen = 0;

	i = 0;
	while( fileId[i] >= 0 )
	{
		nLen += setIntTLV( pfs.Data+nLen, 0x05e0, 4, fileId[i] );
		pfs.TLVCount ++;
		i ++;
	}
	pfs.FileCount = i;

	nLen += setIntTLV( pfs.Data+nLen, 0x05e6, 1, 3 );		// File Event Desc
	pfs.TLVCount ++;

	// Playback Repeat
	//Data[0] = 0x00;		// times to repeat
	//Data[1] = 0x03;
	//Data[2] = 0x00;		// Interval between repetitions: 500ms
	//Data[3] = 0x05;
	//Data[4] = 0x00;		// Total time file is played: 50s
	//Data[5] = 0x00;

	//nLen += setUByteTLV( (pfs.Data)+nLen, 0x05e7, 6, Data );
	//pfs.TLVCount ++;

	int ret = sendMessage((MsgStruct *)&pfs, tag, func);
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: In Channel(0x%x:0x%x)  playVoiceForID Failed, %s\n",
			span, chan,sk_errorText(ret));
	}
	return ret;
}

int SIMMApp::playFileQueue( int span, int chan, int dspSlot, int fileId[], void *tag, HandlerFunc *func )
{
	int  nLen, i;
	XL_PlayFileStart pfs;

	sk_initMsg( &pfs, TAG_PlayFileStart );

	pfs.AddrInfo[0] = 0x00;
	pfs.AddrInfo[1] = 0x02;
	pfs.AddrInfo[2] = 0x0d;
	pfs.AddrInfo[3] = 0x03;
	pfs.AddrInfo[4] = span >> 8;
	pfs.AddrInfo[5] = span & 0x00ff;
	pfs.AddrInfo[6] = chan;
	pfs.AddrInfo[7] = 0x01;
	pfs.AddrInfo[8] = 0x01;
	pfs.AddrInfo[9] = dspSlot;

	pfs.DataType    = 0x00;		// TLV

	pfs.TLVCount    = 0;

	nLen = 0;

	i = 0;
	while( fileId[i] >= 0 )
	{
		nLen += setIntTLV( pfs.Data+nLen, 0x05e0, 4, fileId[i] );
		pfs.TLVCount ++;
		i++;
	}
	pfs.FileCount = i;

	nLen += setIntTLV( pfs.Data+nLen, 0x05e6, 1, 3 );		// File Event Desc
	pfs.TLVCount ++;

	nLen += setIntTLV( pfs.Data+nLen, 0x05f2, 1, 0x00);	    // Play File Queue
	pfs.TLVCount ++;

	/*
	// Playback Repeat
	Data[0] = 0x00;		// times to repeat
	Data[1] = 0x03;
	Data[2] = 0x00;		// Interval between repetitions: 500ms
	Data[3] = 0x05;
	Data[4] = 0x00;		// Total time file is played: 50s
	Data[5] = 0x00;
	nLen += setUByteTLV( (rPlayFile.Data)+nLen, 0x05e7, 6, Data );
	rPlayFile.TLVCount ++;
	*/
	int ret = sendMessage((MsgStruct *)&pfs, tag, func);
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: In Channel(0x%x:0x%x)  playFileQueue Failed, %s\n",
			span, chan,sk_errorText(ret));
	}
	return ret;
}

int SIMMApp::playFileStop( int span, int chan, void *tag , HandlerFunc *func)
{
	XL_PlayFileStop pfs;

	sk_initMsg (&pfs, TAG_PlayFileStop);

	initOneAddrInfo( pfs.AddrInfo, span, chan );

	pfs.DataType = 0x00;
	pfs.TLVCount = 0;

	int ret = sendMessage((MsgStruct *)&pfs, tag, func);
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: In Channel(0x%x:0x%x)  playFileStop Failed, %s\n",
			span, chan,sk_errorText(ret));
	}
	return ret;
}

/*
record voice to file or rtp 
*/
int SIMMApp::recordVoxStart(int span, int chan, int fileid, char *filename, int nMaxTime, 
							void *tag , HandlerFunc *func)
{
	int  nLen, nRecordFileId;
	UBYTE Data[211];

	XL_RecordFileStart rfs;

	sk_initMsg (&rfs, TAG_RecordFileStart);

	rfs.AddrInfo[0] = 0x00;
	rfs.AddrInfo[1] = 0x02;
	rfs.AddrInfo[2] = 0x0d;
	rfs.AddrInfo[3] = 0x03;
	rfs.AddrInfo[4] = span >> 8;
	rfs.AddrInfo[5] = span & 0x00ff;
	rfs.AddrInfo[6] = chan;
	rfs.AddrInfo[7] = 0x01;
	rfs.AddrInfo[8] = 0x01;
	rfs.AddrInfo[9] = 0xff;

	rfs.DataType = 0x00;
	rfs.TLVCount = 0;

	nLen = 0;
	nRecordFileId = fileid;

	// File ID
	nLen += setIntTLV( rfs.Data+nLen, 0x05e0, 4, nRecordFileId );
	rfs.TLVCount ++;

	// File Format
	if( strstr(filename, ".wav") != NULL || strstr(filename, ".WAV") != NULL )
		nLen += setIntTLV( rfs.Data+nLen, 0x05e1, 2, 0x0105 );	// File Format
	else if( strstr(filename, ".vox") != NULL || strstr(filename, ".VOX") != NULL )
		nLen += setIntTLV( rfs.Data+nLen, 0x05e1, 2, 0x0004 );	// File Format
	else
		nLen += setIntTLV( rfs.Data+nLen, 0x05e1, 2, 0 );		// File Format
	rfs.TLVCount ++;

	// Filename
	Data[0] = 0x00;		// 1th server
	Data[1] = 0x01;
	Data[2] = 0x00;		// 2th server
	Data[3] = 0x02;
	strcpy( (char *)Data+4, filename );
	nLen += setUByteTLV( (rfs.Data)+nLen, 0x05e2, strlen(filename)+4, Data );
	rfs.TLVCount ++;

	// File Event Descriptor
	nLen += setIntTLV( rfs.Data+nLen, 0x05e6, 1, 0x0c );
	rfs.TLVCount ++;

	// Beep Tone Parameters
	Data[0] = 0x00;		// Beep Enable Flag
	Data[1] = 0x03;
	Data[2] = 0xe8;		// Beep Frequency (Hz): 0x03e8
	Data[3] = 0x14;		// Beep Duration
	Data[4] = 0xf1;		// Beep Amplitude
	nLen += setUByteTLV( (rfs.Data)+nLen, 0x05e8, 5, Data );
	rfs.TLVCount ++;

	// Initial Silence Timer
	nLen += setIntTLV( rfs.Data+nLen, 0x05e9, 2, 0xffff );
	rfs.TLVCount ++;

	// Final Silence Timer
	nLen += setIntTLV( rfs.Data+nLen, 0x05ea, 2, 1000 );
	rfs.TLVCount ++;

	// Maximum Record Timer
	if( nMaxTime == 0xffff )
		nLen += setIntTLV( rfs.Data+nLen, 0x05eb, 2, 0xffff );
	else
		nLen += setIntTLV( rfs.Data+nLen, 0x05eb, 2, nMaxTime*100 );
	rfs.TLVCount ++;
	/*
	// DTMF Clamping
	nLen += setIntTLV( rRecordFile.Data+nLen, 0x0604, 2, 1 );
	rRecordFile.TLVCount ++;
	*/
	/*	// Dual Channel Record Option: 00: sum together  01: independent channels
	nLen += setIntTLV( rfs.Data+nLen, 0x05ed, 1, 0x01 );
	rfs.TLVCount ++;

	// Append or Replace
	nLen += setIntTLV( rRecordFile.Data+nLen, 0x0615, 2, 0x0101 );
	rRecordFile.TLVCount ++;
	*/
	int ret = sendMessage((MsgStruct *)&rfs, (void *)tag, func);
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: In Channel(0x%x:0x%x)  recordVoxStart Failed, %s\n",
			span, chan,sk_errorText(ret));
	}
	return ret;
}

int SIMMApp::recordVoxStop(int span, int chan, void *tag , HandlerFunc *func)
{
	XL_RecordFileStop rfs;

	sk_initMsg (&rfs, TAG_RecordFileStop);

	initOneAddrInfo( rfs.AddrInfo, span, chan );

	int ret = sendMessage((MsgStruct *)&rfs, tag, func);
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: In Channel(0x%x:0x%x)  recordVoxStop Failed, %s\n",
			span, chan,sk_errorText(ret));
	}
	return ret;
}

int SIMMApp::sendSAMtoChan( int span, int chan, char *sSAM, void *tag, HandlerFunc *func)
{
	int nSAMLen=0, i;
	XL_PPLEventRequest nPPL;

	nSAMLen = strlen(sSAM);

	sk_initMsg( &nPPL, TAG_PPLEventRequest );
	initOneAddrInfo(nPPL.AddrInfo, span, chan );

	nPPL.ComponentID = L3P_CIC;
	nPPL.PPLEvent = ISUP_REQ_SAM;

	nPPL.ICBCount = 0x01;				// one ICB
	nPPL.Data[0] = 0x02;				// ICB type. 0x01:action  0x02:data
	nPPL.Data[1] = ISUP_CPC;			// ICB Subtype: SS7Format

	nPPL.Data[2] = (nSAMLen+1)/2+1+4;	// Data Length of ICB data
	nPPL.Data[3] = ISUP_SAM;			// ISUP Msg of ACM
	nPPL.Data[4] = 0x01;				// ICB Parameter count
	nPPL.Data[5] = 0x05;				// Type: SAM Number
	nPPL.Data[6] = (nSAMLen+1)/2+1;		// Length
	if( nSAMLen%2 )
		nPPL.Data[7] = 0x80;			// nSAMLen: odd
	else
		nPPL.Data[7] = 0x00;			// nSAMLen: even

	for( i=0; i<((nSAMLen+1)>>1); i++ )
		nPPL.Data[8+i] = (sSAM[(i<<1)]-'0')+
		(((sSAM[(i<<1)+1]?sSAM[(i<<1)+1]:'0')-'0')<<4);

	int ret = sendMessage((MsgStruct *)&nPPL, tag, func);
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: In Channel(0x%x:0x%x)  sendSAMtoChan Failed, %s\n",
			span, chan,sk_errorText(ret));
	}
	return ret;
}

int SIMMApp::packOutseizeControlMsg( const CallRecord * cr, XL_OutseizeControl & ocm)
{
	int i, m=0, nLen=0;

	int calledLen = strlen( cr->_outCalled );
	if( calledLen == 0 )
		return SK_UNKNOWN_OUTBOUND_MSG;

	int callerLen = strlen( cr->_outCaller );

	sk_initMsg( &ocm, TAG_OutseizeControl );
	ocm.Span = cr->_outSpan;
	ocm.Channel = cr->_outChan;

	ocm.ICBCount = 0x04;			// IAM have four Parameter
	ocm.ICBData[0] = 0x01;			// ICB one, ICB action
	ocm.ICBData[1] = 0x0a;			// ICB subtype
	ocm.ICBData[2] = 0x00;			// ICB param number
	nLen += 3;

	ocm.ICBData[3] = 0x01;			// ICB two, ICB action
	ocm.ICBData[4] = 0x08;			// ICB subtype
	ocm.ICBData[5] = 0x00;			// ICB param number
	nLen += 3;

	ocm.ICBData[6] = 0x01;			// ICB three, ICB action
	ocm.ICBData[7] = 0x0f;			// ICB subtype
	ocm.ICBData[8] = 0x00;			// ICB param number
	nLen += 3;

	m = nLen;						// the pos of ICBtype=0x02

	ocm.ICBData[nLen+0] = 0x02;		// ICB type. 0x01:action  0x02:data
	ocm.ICBData[nLen+1] = ISUP_CPC;	// ICB Subtype: SS7Format
	ocm.ICBData[nLen+3] = ISUP_IAM;	// ISUP Msg ID: IAM

	ocm.ICBData[nLen+4] = 0x04;		// ICB Parameter count, IAM have four
	nLen += 5;

	ocm.ICBData[nLen+0] = 0x09;		// ICB param type: Calling Party Category
	ocm.ICBData[nLen+1] = 0x01;		// ICB Data length
	ocm.ICBData[nLen+2] = Excel_Category;	    // ICB Data
	nLen += 3;

	ocm.ICBData[nLen] = 0x04;					// Type: Called Party Address Data
	ocm.ICBData[nLen+1] = ((calledLen+1)>>1)+2;	// Length
	if( calledLen%2 )
		ocm.ICBData[nLen+2] = 0x83;				// Odd even and Nature
	else
		ocm.ICBData[nLen+2] = 0x03;

	ocm.ICBData[nLen+3] = 0x90;					// INN ind and Numbering plan ind
	nLen += 4;

	for( i=0; i<((calledLen+1)>>1); i++ )
		ocm.ICBData[nLen+i] = (cr->_outCalled[(i<<1)]-'0')+
		(((cr->_outCalled[(i<<1)+1]?cr->_outCalled[(i<<1)+1]:'0')-'0')<<4);
	nLen = nLen + i;

	ocm.ICBData[nLen+0] = 0x0a;					// Type: Calling line id
	ocm.ICBData[nLen+1] = ((callerLen+1)>>1)+2;	// Length
	if( callerLen%2 )
	{
		if( (!strncmp(cr->_outCalled, "133", 3)) || ((!strncmp(cr->_outCalled, "153", 3))))
			ocm.ICBData[nLen+2] = 0x83;         // Odd/even and Nature
		else
			ocm.ICBData[nLen+2] = 0x82;         // Odd/even and unknow(use Nature)
	}
	else
		ocm.ICBData[nLen+2] = 0x02;

	ocm.ICBData[nLen+3] = 0x11;					// NI/Numvering plan ind/Address pre 
	nLen += 4;

	for( i=0; i<((callerLen+1)>>1); i++ )
		ocm.ICBData[nLen+i] = (cr->_outCaller[i<<1]-'0')+
		(((cr->_outCaller[(i<<1)+1]?cr->_outCaller[(i<<1)+1]:'0')-'0')<<4);
	nLen = nLen + i;

	ocm.ICBData[nLen+0] = 0x06;					// Type:connection indicator
	ocm.ICBData[nLen+1] = 0x01;					// length 
	ocm.ICBData[nLen+2] = 0x10;					// length 
	nLen +=3; 

	ocm.ICBData[11] = (nLen-m-3);				// Data Length of ICB data

	return 0;
}


/* return length of total TLV */
int SIMMApp::setUByteTLV( UBYTE *sMsgData, int nTag, int nLength, UBYTE *Data )
{
	sMsgData[0] = nTag >> 8;
	sMsgData[1] = nTag & 0x00ff;
	sMsgData[2] = nLength >> 8;
	sMsgData[3] = nLength & 0x00ff;
	memcpy( sMsgData+4, Data, nLength );

	return nLength+4;
}

/* return length of total TLV */
int SIMMApp::setIntTLV( UBYTE *sMsgData, int nTag, int nLength, int Data )
{
	int i = 0;

	sMsgData[0] = nTag >> 8;
	sMsgData[1] = nTag & 0x00ff;
	sMsgData[2] = nLength >> 8;
	sMsgData[3] = nLength & 0x00ff;

	if( nLength == 4 || nLength == 3 || nLength == 2 )
	{
		if( nLength == 4)
		{
			sMsgData[4] = Data >> 24;
			sMsgData[5] = (Data & 0x00ffffff) >> 16;
			i = 2;
		}

		if( nLength == 3)
		{
			sMsgData[4] = Data >> 16;
			i = 1;
		}

		sMsgData[4 + i]   = (Data & 0x0000ffff) >> 8;
		sMsgData[4 + i + 1] = (Data & 0x000000ff);
	}
	else if( nLength == 1 )
	{
		sMsgData[4] = Data;
	}
	else
	{
		return 0;
	}

	return nLength+4;
}

void SIMMApp::itob(unsigned int value, char *string, int radix, int nStrLen)
{
	int  r, i, nLen;
	char sTmp[128];

	nLen = 0;
	if(value == 0)
	{
		sTmp[0] = '0';
		nLen = 1;
	}
	else
	{
		while(value>0)
		{
			r = value%radix;
			if (r<10)
				sTmp[nLen]=r+48;
			else
				sTmp[nLen]=r+55;
			value = value/radix;
			nLen++;
		}
	}

	if( nStrLen > 0 )
	{
		for (i=0; i<nStrLen-nLen; i++)
			string[i] = '0';
	}
	else
		i = 0;

	for( r=0; r<nLen; r++ )
		string[i+r] = sTmp[nLen-r-1];

	string[i+nLen] = '\0';
}

/*
* send CollectDigitString message to Excel. (GetDtmf)
*/
int SIMMApp::collectDigit( int span, int chan, int maxDigit, int maxTime, char *termChar, 
						  void *tag, HandlerFunc *func)
{
	XL_CollectDigitString rCollect;
	unsigned short nTmp, i;

	sk_initMsg( &rCollect, TAG_CollectDigitString );
	rCollect.Span = span;
	rCollect.Channel = chan;
	rCollect.Mode = 0x02;			/* 0x04: Fix number of Digits or Term Chars */
	rCollect.MaxDigits = maxDigit;
	rCollect.NumTermChars = strlen(termChar);
	rCollect.ConfigBits = 0x81;

	rCollect.TermChars = 0;
	for( i=0; i<strlen(termChar); i++ )
	{
		nTmp = 0;
		if( termChar[i] == '*' )
		{
			nTmp = 0x0e;
		}
		else if( termChar[i] == '#' )
		{
			nTmp = 0x0f;
		}
		else
		{
			if( termChar[i] >= 'A' )
			{
				nTmp = termChar[i] - 'A' + 0x0a;
			}
			else
			{
				nTmp = termChar[i] - '0';
			}
		}

		rCollect.TermChars += nTmp<<(12-i*4);
	}

	if( maxTime != 0xffff )
	{
		rCollect.InterDigitTimer = 500;				/* 5s */
	}
	else
	{
		rCollect.InterDigitTimer = 0xffff;
	}

	if( maxTime != 0xffff )
	{
		rCollect.FirstDigitTimer = maxTime*100;		/*  */
	}
	else
	{
		rCollect.FirstDigitTimer = 0xffff;
	}

	rCollect.CompletionTimer = maxTime*100;
	rCollect.MinReceiveDigitDuration = 3;
	rCollect.AddressSignallingType = 0x01;			/* DTMF */
	rCollect.NumDigitStrings = 0x01;				/* DTMF */
	rCollect.ResumeDigitCltnTimer = 5;

	int ret = sendMessage((MsgStruct *)&rCollect, tag, func);
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: collectDigit: In Channel(0x%x:0x%x)%s\n", span, chan, sk_errorText(ret));
	}

	return ret;
}

int SIMMApp::dspServiceRequest( int span, int chan, int maxTime, void *tag, HandlerFunc *func )
{
	XL_DSPServiceRequest  rDSPRequest;
	sk_initMsg( &rDSPRequest, TAG_DSPServiceRequest );

	rDSPRequest.Span = span;
	rDSPRequest.Channel = chan;
	rDSPRequest.ServiceType = 0x01;
	if( maxTime == 0xffff )
	{
		rDSPRequest.Data[0] = 0xff;
		rDSPRequest.Data[1] = 0xff;
	}
	else
	{
		rDSPRequest.Data[0] = (maxTime*100) >> 8;
		rDSPRequest.Data[1] = (maxTime*100) & 0x00ff;
	}
	rDSPRequest.Data[2] = 0x81;
	rDSPRequest.Data[3] = 0x00;
	rDSPRequest.Data[4] = 0x03;

	int ret = sendMessage((MsgStruct *)&rDSPRequest, tag, func);
	if(ret != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: dspServiceRequest: In Channel(0x%x:0x%x) %s\n", span, chan, sk_errorText(ret));
	}

	return ret;
}

/*
* stop collectDigitString or DSPServiceRequest.
*/
int SIMMApp::dspServiceCancel( int span, int chan, int type, void *tag, HandlerFunc *func)
{
	XL_DSPServiceCancel  rDSPCancel;
	sk_initMsg( &rDSPCancel, TAG_DSPServiceCancel );

	rDSPCancel.Span = span;
	rDSPCancel.Channel = chan;
	rDSPCancel.ServiceType = type;		/* 1: receive DTMF  2: Energy Detect */

	return sendMessage((MsgStruct *)&rDSPCancel, tag, func);
}

int SIMMApp::generateCallProcessingEvent( int span, int chan, void *tag, HandlerFunc *func )
{
	XL_GenerateCallProcessingEvent  callProcessingEvent;
	sk_initMsg( &callProcessingEvent, TAG_GenerateCallProcessingEvent );

	callProcessingEvent.Span = span;
	callProcessingEvent.Channel = chan;
	callProcessingEvent.Event = 0x01;  
	/*Answer Call (Generate appropriate signaling to return answer to the incoming call)
	This event might not be necessary if PPLevREQ(ANM) is sent*/

	return sendMessage((MsgStruct *)&callProcessingEvent, tag, func);
}

int SIMMApp::collectDigitResult( SKC_Message *rMsg, char *recvStr )
{
	if( (rMsg == 0) || (recvStr == 0) )
	{
		return -1;
	}

	int length, i, j;

	SKC_MSG_SWITCH(rMsg) 
	{
		CASEC_CallProcessingEvent(cpe)
		{
			if( cpe->getEvent() == 0x02 )
			{   
				j = 0;
				UBYTE * data = cpe->getData();
				length = (int)data[4];
				if( length > MAX_NUMB_LEN )
				{
					length = MAX_NUMB_LEN;
				}

				for( i = 0; i < ( length + 1 ) / 2; i++ )
				{
					recvStr[j] = ToChar( (data[ 5 + i ]) >> 4 );
					if( recvStr[j] == 'E' )
					{
						recvStr[j] = '*';
					}
					else if( recvStr[j] == 'F' )
					{
						recvStr[j] = '#';
					}
					j++;

					recvStr[j] = ToChar( (data[ 5 + i ]) & 0x0f );
					if( recvStr[j] == 'E' )
					{
						recvStr[j] = '*';
					}
					else if( recvStr[j] == 'F' )
					{
						recvStr[j] = '#';
					}
					j++;
				}

				recvStr[length] = 0x00;
			}
			break;
		}
		CASEC_default;

	} SKC_END_SWITCH;

	LogFileManager::getInstance()->write(Debug, "INFO: Received DTMF: ( dtmf = %s )", recvStr);

	return 0;
}

int SIMMApp::startUserTimer(int timerId, const float interval, void *tag, HandlerFunc *func)
{
	SKC_UserTimer UserTimer;

	UserTimer.setSeconds(interval);
	UserTimer.setGroupTag(timerId);
	int ret = UserTimer.send(tag ,func);
	if (ret != OK)
	{
		LogFileManager::getInstance()->write(Verbose,"ERROR: startUserTimer Error of %s\n",sk_errorText(ret));
	}

	return ret ;
}

int SIMMApp::cancelUserTimer(int timerId, void *tag, HandlerFunc *func)
{
	SKC_CancelUserTimer UserTimer;
	UserTimer.setGroupTag(timerId);
	int ret = UserTimer.send(tag ,func);
	if (ret != OK)
	{
		LogFileManager::getInstance()->write(Verbose,"ERROR: cancelUserTimer Error of %s\n",sk_errorText(ret));
	}

	return ret ;
}

bool SIMMApp::isValidNumber(char *number)
{
	if( ( strlen(number) > 13) || ( strlen(number) < 8))		// 虚拟副号码时 ?			8~13位
	{
		return false;
	}

	return true ;
}

bool SIMMApp::isValidNumber_(char *number)
{
	if( ( strlen(number) > 13) || ( strlen(number) < 7))		// 虚拟副号码时 ?			8~13位
	{
		return false;
	}

	return true ;
}

bool SIMMApp::isValidNumber_black(char *number)
{
	if( (strncmp(number,"95",2) == 0) ||
	    (strncmp(number,"96",2) == 0) ||
	    (strncmp(number,"11",2) == 0) ||
        (strncmp(number,"12",2) == 0)
	  )
	{
		return true;
	}

	if( ( strlen(number) > 13) || ( strlen(number) < 8))		// 虚拟副号码时 ?			8~13位
	{
		return false;
	}

	return true ;
}

bool SIMMApp::isValidSequence(char *sequence)
{
	if(sequence == 0)
		return 0;

	for(int i=0; i< 15; i++)
	{
		if( !strcmp(sequence, _sequenceTable[i]))
			return 1;
	}

	return 0;
}

bool SIMMApp::isValidTimeField(char *field)
{
	if( strlen(field) != 4 )
	{
		return false ;
	}
	for( int i = 0; i < strlen(field); i++ )
	{
		if( (field[i] < '0') || (field[i] > '9') )
		{
			return false;
		}
	}

	char tmp[4];
	memset(tmp, 0 , sizeof(tmp));
	strncpy(tmp,field, 2);
	// 判断开始时间，以小时为单位
	if(atoi(tmp) > 23)
	{
		return false;
	}

	memset(tmp, 0 , sizeof(tmp));
	strncpy(tmp,field + 2, 2);
	// 判断结束时间，以小时为单位
	if(atoi(tmp) > 23)
	{
		return false;
	}

	return true ;
}

//zuosai add 20101227 for callforward
int  SIMMApp::processCallForward( CallRecord * cr)
{
	vector<CallForwardInfo> callForwardNumber;

	//minor be caller		//副号码做主叫
	if( cr->_nbrInfo.callType == 1 )	
	{
		//if called is major, callForwardNumber will be empty
		//if called is minor, originalNumber is minor number, callForwardNumber may not empty.
		DataUsage::instance()->getCallForwardNumber(cr->_nbrInfo.originalNumber, callForwardNumber);
	}
	else//cr->_nbrInfo.callType==2//major be caller,minor be called
	{
		DataUsage::instance()->getCallForwardNumber(cr->_nbrInfo.minorNumber, callForwardNumber);
	}

	//add number to call forward list
	if(!callForwardNumber.empty())
	{
		int flag = 0;
		int i=0;
		list <string>::iterator itlist;
		for( i=0; i< callForwardNumber.size(); i++)
		{
			if(callForwardNumber[i].calledNumber.length() > sizeof(cr->_outCalled)-1)
				continue;
			cr->_callForwardList.push_back(callForwardNumber[i].calledNumber);
			if(flag==0 && callForwardNumber[i].order>=0 )
			{
				flag = 1;
				itlist = cr->_callForwardList.end();
				itlist--;
			}
		}
		if(flag == 0)
		{
			itlist = cr->_callForwardList.end();
		}
		if(itlist != cr->_callForwardList.begin())
		{
			if(itlist == cr->_callForwardList.end())
				cr->_callForwardList.push_back(cr->_outCalled);
			else
				cr->_callForwardList.insert(itlist, cr->_outCalled);
			strcpy(cr->_outCalled, cr->_callForwardList.front().c_str());
			cr->_callForwardList.pop_front();
		}
	}
	cr->_hasReadCallForward = 1;

	return 0;
}
//zuosai add 20101227 for callforward end

int SIMMApp::processCallout( CallRecord * cr)
{
	if( cr == 0 )
	{
		return -1;
	}

	//zuosai Modify 20110210 for bug
	int stat = sk_setChannelHandler(cr->_inSpan, cr->_inChan, (void *)cr, genericHandler);
	if(stat != OK)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x) Set Handler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(stat));
		return stat;
	}
	//zuosai Modify 20110210 for bug end

	//zuosai add 20101227 for callforward 
	if( cr->_hasReadCallForward == 0 )
	{
		processCallForward(cr);
	}
	//zuosai add 20101227 for callforward end

	char textLog[512];
	memset(textLog, 0, sizeof(textLog));
	sprintf(textLog, "CallID(%d) Beginning Callout: Caller = %s, Called = %s, OutGroup = %s",
		cr->_callID, cr->_outCaller, cr->_outCalled, cr->_outGroup);
	LogFileManager::getInstance()->write(Debug, "INFO: %s",textLog);

	/* If any unexpected events happen for this channel, 
	* they will be passed (eventually) to
	* genericHandler, which should take care of them 
	*/

	if(strlen(cr->_outCalled) < 3)
	{
		int fileID[2]={4016,-1};
		return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
			(void *)cr, genericHandler);
	}
	if(( strlen(cr->_outCalled) > 5) && (strlen(cr->_outCalled) < 9) && 
		( strncmp(cr->_outCalled, "01095096", strlen("01095096"))))
	{
		int fileID[2]={4349,-1};
		return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
			(void *)cr, genericHandler);
	}


	//2009-12-18 by wuhongchao
	//Delete the short message notify Called .-----start-----------------
#ifdef MSGNOTIFY
	/* through the short message notify Called*/

	if(strlen(cr->_outCalled) == _mobileLen)
	{
		char logText[512];
		memset(logText, 0, sizeof(logText));
		int flagCallerIsLocalUser = 0;

		string areaCode(_localAreaCode);
		if(cr->_nbrInfo.callType == 2)
		{
			ResultDataUsage result = DataUsage::instance()->getCallerAreaCode(cr->_nbrInfo.originalNumber, areaCode);
			if(result == E_OPERATOR_SUCCED)
				flagCallerIsLocalUser = 1;


			DialNotifyLibWrapper::instance()->submitNoify(flagCallerIsLocalUser, cr->_nbrInfo.originalNumber, cr->_outCalled, cr->_nbrInfo.callerNumberMsg, cr->_nbrInfo.minorNumber);
			sprintf(logText, "caller = %s, called = %s, concern number = %s, minorNumber = %s", 
				cr->_nbrInfo.originalNumber, cr->_outCalled, cr->_nbrInfo.callerNumberMsg, cr->_nbrInfo.minorNumber);

			LogFileManager::getInstance()->write(Debug, "INFO : CallID(%d) Send Dial Notify by short message, concern number = %s",cr->_callID,logText);

		}
		else if(cr->_nbrInfo.callType == 1)
		{
			ResultDataUsage result = DataUsage::instance()->getCallerAreaCode(cr->_nbrInfo.minorNumber, areaCode);
			if(result == E_OPERATOR_SUCCED)
				flagCallerIsLocalUser = 1;

			DialNotifyLibWrapper::instance()->submitNoify(flagCallerIsLocalUser, cr->_nbrInfo.minorNumber, cr->_outCalled, cr->_nbrInfo.callerNumberMsg, cr->_nbrInfo.originalNumber);
			sprintf(logText, "caller = %s, called = %s, concern number = %s, minorNumber = %s", 
				cr->_nbrInfo.originalNumber, cr->_outCalled, cr->_nbrInfo.callerNumberMsg, cr->_nbrInfo.minorNumber);

			LogFileManager::getInstance()->write(Debug, "INFO : CallID(%d) Send Dial Notify by short message, concern number = %s",cr->_callID,logText);

		}
	}
#endif

	//Delete the short message notify Called .-----end-----------------

	stat = outseizeToNum(cr);
	if(stat != OK ) 
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : outseizeToNum did not return properly\n");

		return stat;
	}

	return OK;
}

int SIMMApp::digit2voiceList(int digit, int* list) //num limit is 0~99
{
	int count = 0;
	if(digit < 0 || digit > 99)
	{
		return count;
	}

	int low = digit%10;
	int high = digit/10;

	if( high == 0 )// 0~9
	{
		list[count++] = low + 1000;
	}
	else if( high == 1 ) //10~19
	{
		list[count++] = 1010;// 	1010.WAV	十
		if(low > 0)
			list[count++] = low + 1000;
	}
	else if(high > 1) //20~99
	{
		list[count++] = high + 1000;
		list[count++] = 1010;// 	1010.WAV	十
		if(low > 0)
		{
			list[count++] = low + 1000;
		}
	}

	return count;
}

int SIMMApp::phonNum2voiceList(const char* phonNum, int* list)
{
	int len = strlen(phonNum);
	int i = 0;
	for(i = 0; i < len; i++)
	{
		if( phonNum[i] > '9' || phonNum[i] < '0' )
		{
			return -1;
		}
		list[i] = phonNum[i] - '0' + 1000;
	}

	return i;
}

int SIMMApp::time2voiceList(const char* timeStr, int* list)
{
	int count=0;

	/////////year
	char year[5]={0};
	strncpy(year, timeStr, 4);
	count += phonNum2voiceList(year,&list[count]);
	list[count++] = 1015; //	1015.WAV	年

	/////////month
	const char* p;
	p = strchr(timeStr, '-');
	p++;
	int month=0;
	month = atoi(p);
	count += digit2voiceList(month, &list[count]);
	list[count++] = 1016; //	1016.WAV	月

	/////////day
	p = strchr(p, '-');
	p++;
	int day=0;
	day = atoi(p);
	count += digit2voiceList(day, &list[count]);
	list[count++] = 1017; //	1017.WAV	日

	/////////hour
	p = strchr(p, ' ');//space
	p++;
	int hour=0;
	hour = atoi(p);
	count += digit2voiceList(hour, &list[count]);
	list[count++] = 1018; //	1018.WAV	时

	/////////minute
	p = strchr(p, ':');//space
	p++;
	int minute=0;
	minute = atoi(p);
	count += digit2voiceList(minute, &list[count]);
	list[count++] = 1019; //	1019.WAV	分

	/////////second
	p = strchr(p, ':');//space
	p++;
	int second=0;
	second = atoi(p);
	count += digit2voiceList(second, &list[count]);
	list[count++] = 1020; //	1020.WAV	秒

	return count;
}

/* get SAM number from data, such as:
* 0  1  2  3  4  5  6  7  8  9  10
* 02 12 08 02 01 05 04 80 98 10 32
*/
int SIMMApp::getSAMNbr( UBYTE *Data, char *TelNbr )
{
	int  i, j, nPos;
	int  nParamCount, nParamType, nParamLen;

	if( Data[0] != 0x02 )		/* not Data ICB */
		return -1;

	if( Data[1] == ISUP_CPC )	/* for ISUP */
	{
		if( Data[3] == ISUP_SAM )
		{
			nParamCount = Data[4];
			nPos = 5;
			for( i=0; i<nParamCount; i++ )
			{
				nParamType = Data[nPos];
				nParamLen  = Data[nPos+1];
				if( nParamType == 0x05 )	/* SAM param */
				{
					for( j=0; j<nParamLen-1; j++ )
					{
						TelNbr[j*2+1] = ToChar( Data[nPos+3+j]>>4 );
						TelNbr[j*2] =
							((Data[nPos+3+j])&0x0f)>9?0x00:ToChar((Data[nPos+3+j])&0x0f);
					}

					if( Data[nPos+2] & 0x80 )
						TelNbr[(nParamLen-1)*2-1] = 0x00;
					else
						TelNbr[(nParamLen-1)*2] = 0x00;
				}
				else
					nPos += nParamLen+2;
			}
		}
		else
			return -1;
	}

	return 0;
}

int SIMMApp::forecastCalledStatus(const CallRecord * cr, UBYTE * data)
{
	if( data[0] != 0x02 )		/* not Data ICB */
		return -1;

	if( data[1] == ISUP_CPC )	/* for ISUP */
	{
		int j = 0 ;
		int dataLength = data[2] ;
		char  sByte[sizeof(int)], sParamData[sizeof(int)*20];
		memset(sParamData, 0 , sizeof(sParamData));
		int nParamType, nParamLen ;

		if( data[3] == ISUP_ACM )
		{
			int paramCount =  data[4] ;
			int nPos = 5 ;
			for( j=0; j<paramCount; j++ )
			{
				nParamType = data[nPos];
				nParamLen  = data[nPos+1];
				if(nParamType == 0x11)  //Called party's status indicator
				{

					UBYTE uState = data[nPos+2];
					uState = uState >> 2 & 0x03 ; 
					switch( uState )
					{
					case 0:
						LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Channel(0x%x:0x%x) Called party's status indicator( no indication)", 
							cr->_callID, cr->_outSpan, cr->_outChan);
						break;
					case 1:
						LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Channel(0x%x:0x%x) Called party's status indicator( subscriber free)", 
							cr->_callID, cr->_outSpan, cr->_outChan);
						break;
					case 2:
						LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Channel(0x%x:0x%x) Called party's status indicator( connect when free (national use))", 
							cr->_callID, cr->_outSpan, cr->_outChan);
					case 3:
						LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Channel(0x%x:0x%x) Called party's status indicator( spare)", 
							cr->_callID, cr->_outSpan, cr->_outChan);
						break;
					default:
						LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Channel(0x%x:0x%x) Called party's status indicator. Value = %d", 
							cr->_callID, cr->_outSpan, cr->_outChan, uState);
						break;
					}
					nPos += nParamLen+2;
				}
				else
					nPos += nParamLen+2;
			}
		}
	}
	return 0;
}

int SIMMApp::handleDefaultCallProcessingEvent( const CallRecord * cr, UBYTE evt)
{
	if( cr == 0)
	{
		return -1;
	}

	switch(evt)
	{
	case Answer:
		{
			LogFileManager::getInstance()->write(Brief, 
				"INFO: CallID(%d) In Channel(0x%x,0x%x), Answer ",
				cr->_callID,cr->_inSpan, cr->_inChan);

			return OK;
		}
	case FilePlayStart:
		{
			LogFileManager::getInstance()->write(Brief, 
				"INFO: CallID(%d) In Channel(0x%x,0x%x), FilePlayStart ",
				cr->_callID,cr->_inSpan, cr->_inChan);

			return OK;
		}
	case FilePlayOK:
		{
			LogFileManager::getInstance()->write(Brief, 
				"INFO: CallID(%d) In Channel(0x%x,0x%x), FilePlayOK ",
				cr->_callID,cr->_inSpan, cr->_inChan);

			return OK;
		}
	case FileNoFound:
		{
			LogFileManager::getInstance()->write(Brief, 
				"INFO: CallID(%d) In Channel(0x%x,0x%x), FileNoFound ",
				cr->_callID,cr->_inSpan, cr->_inChan);

			return OK;
		}
	case FilePlayCmpt:
		{
			LogFileManager::getInstance()->write(Brief, 
				"INFO: CallID(%d) In Channel(0x%x,0x%x), FilePlayCmpt ",
				cr->_callID,cr->_inSpan, cr->_inChan);

			return OK;
		}
	case FileRecStart:
		{
			LogFileManager::getInstance()->write(Brief, 
				"INFO: CallID(%d) In Channel(0x%x,0x%x), FileRecStart ",
				cr->_callID,cr->_inSpan, cr->_inChan);

			return OK;
		}
	case FileRecOK:
		{
			LogFileManager::getInstance()->write(Brief, 
				"INFO: CallID(%d) In Channel(0x%x,0x%x), FileRecOK ",
				cr->_callID,cr->_inSpan, cr->_inChan);

			return OK;
		}
	case FileRecCmpt:
		{
			LogFileManager::getInstance()->write(Brief, 
				"INFO: CallID(%d) In Channel(0x%x,0x%x), FileRecCmpt ",
				cr->_callID,cr->_inSpan, cr->_inChan);

			return OK;
		}
	default:
		{
			LogFileManager::getInstance()->write(Brief, 
				"Got a Call Processing Event message Event = %d", evt);

			return SK_NOT_HANDLED;
		}
	}
}

int SIMMApp::defaultHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_ChannelReleased(released) 
		{
			/* If a channel is released, Print the Call details and
			* call the returnChannels().
			*/

			if ((released->getSpan() == cr->_inSpan) && 
				(released->getChannel() == cr->_inChan))
			{
				char crBuf[1024];
				cr->_endTime = time(0);
				// Print the callRecord Details 
				sprintf(crBuf, "INF0: CallID(%d) Completed, In:(0x%x,0x%x) out:(0x%x,0x%x) duration: %d seconds",
					cr->_callID,
					cr->_inSpan, 
					cr->_inChan, 
					cr->_outSpan, 
					cr->_outChan,(cr->_endTime) - (cr->_rcvTime));
				LogFileManager::getInstance()->write(Brief, crBuf);

				simm->returnChannels(released->getSpan(), 
					released->getChannel(), tag);
			}
			else 
			{
				LogFileManager::getInstance()->write(Verbose,
					"INFO: CallID(%d) Release OutBount Channel(0x%x, 0x%x)", 
					cr->_callID,
					released->getSpan(),
					released->getChannel());

				XLC_ReleaseChannel rel_channel;
				/* Return the channel to the channel manager */
				//Modify by zhouqr 2009-5-8
				//sk_returnChannel(released->getSpan(), released->getChannel());

				rel_channel.setSpanA(released->getSpan());
				rel_channel.setSpanB(released->getSpan());
				rel_channel.setChannelA(released->getChannel());
				rel_channel.setChannelB(released->getChannel());
				rel_channel.send(NULL, handleAcks);

			}
			return OK;
		}
		CASEC_ChannelReleasedWithData(crwd)
		{
			/* If a channel is releasedWithData, Print the Call details and
			* call the returnChannels().
			*/
			cr->_reference -- ;
			simm->getDataByChannelReleaseWD(crwd, cr);

			if ((crwd->getSpan() == cr->_inSpan) && 
				(crwd->getChannel() == cr->_inChan))
			{
				char logText[1024];
				cr->_endTime = time(0);

				// Print the callRecord Details 
				sprintf(logText, "INF0: CallID(%d) Completed, In:(0x%x,0x%x) out:(0x%x,0x%x) duration: %d seconds",
					cr->_callID,
					cr->_inSpan, 
					cr->_inChan, 
					cr->_outSpan, 
					cr->_outChan,(cr->_endTime) - (cr->_rcvTime));
				LogFileManager::getInstance()->write(Brief, logText);


				return simm->processChannelReleasedWithData(crwd->getSpan(), 
					crwd->getChannel(), tag);

			}
			else 
			{
				//.s add by wj 20120315
				ChannelData *chandat;

				chandat = (ChannelData *)sk_getChannelData(crwd->getSpan(),crwd->getChannel());
				if (chandat) 
				{
					// Clear out the data associated with this channel
					sk_setChannelData(crwd->getSpan(),crwd->getChannel(), NULL);

					/* Since memory was allocated in handleOutseizeAck in order to pass 
					* a pointer to sk_setChannelData(), delete the memory here.
					*/
					delete chandat;
				}
				//.e add by wj 20120315

				XLC_ReleaseChannel rel_channel;
				/* Return the channel to the channel manager */
				//Modify by zhouqr 2009-5-8
				sk_returnChannel(crwd->getSpan(),crwd->getChannel());

				rel_channel.setSpanA(crwd->getSpan());
				rel_channel.setSpanB(crwd->getSpan());
				rel_channel.setChannelA(crwd->getChannel());
				rel_channel.setChannelB(crwd->getChannel());
				rel_channel.send(NULL, handleAcks);

				LogFileManager::getInstance()->write(Verbose,
					"INFO: CallID(%d) Release OutBount Channel(0x%x, 0x%x)", 
					cr->_callID,
					crwd->getSpan(),
					crwd->getChannel());

				//zuosai add 20101227 for callforward
				if(cr->_reference==1 && cr->_doCallForward == 1)//inChan is not hangup
				{
					cr->_doCallForward = 0;
					if(cr->_userTimerID != -1)
					{
						simm->cancelUserTimer(cr->_userTimerID,(void *)cr,genericHandler);
						cr->_userTimerID = -1;
					}
					if(cr->_callForwardList.size())
					{
						string number;
						number = cr->_callForwardList.front();
						if(number.length() < sizeof(cr->_outCalled))
						{
							strcpy(cr->_outCalled, number.c_str());
							cr->_callForwardList.pop_front();
							return simm->processCallout(cr);		
						}
					}
				}
				//zuosai add 20101227 for callforward end

				return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
			}
		}
		CASEC_ReleaseChannelAck(rca)
		{
			LogFileManager::getInstance()->write(Debug,
				"INFO: ReleaseChannelAck");

			return OK;
		}
		CASEC_default 
		{
			return SK_NOT_HANDLED;
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;
}

int SIMMApp::getDataByChannelReleaseWD( XLC_ChannelReleasedWithData *rRel, void *tag)
{
	int   i, j, nPos;
	int   nMesgId;
	char  sByte[sizeof(int)], sParamData[sizeof(int)*20];
	UBYTE *sData;
	char s_sTmpBuff[512];

	if(tag == 0)
		return -1 ;

	CallRecord *cr = (CallRecord *)tag;

	if( rRel->getICBCount() > 1 )  // ICBCount should be 1
		return -1;

	memset(s_sTmpBuff, 0, sizeof(s_sTmpBuff));
	if( rRel->getICBType() == 0x02 && rRel->getICBSubtype() == ISUP_CPC ) // for ISUP
	{
		s_sTmpBuff[0] = 0x00;
		sData = rRel->getICBData();

		nMesgId = sData[0];
		switch( nMesgId )
		{
		case ISUP_REL:
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Channel(0x%x:0x%x) Recv ChannelReleasedWithData, Mesg: REL(0x%x)", 
				cr->_callID, rRel->getSpan(), rRel->getChannel(), nMesgId);
			break;
		case ISUP_RLC:
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Channel(0x%x:0x%x) Recv ChannelReleasedWithData, Mesg: RLC(0x%x)",
				cr->_callID, rRel->getSpan(), rRel->getChannel(), nMesgId);
			break;
		case ISUP_RSC:
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Channel(0x%x:0x%x) Recv ChannelReleasedWithData, Mesg: RSC(0x%x)", 
				cr->_callID, rRel->getSpan(), rRel->getChannel(), nMesgId);
			break;
		default:
			LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Channel(0x%x:0x%x) Recv ChannelReleasedWithData, Mesg: 0x%x", 
				cr->_callID, rRel->getSpan(), rRel->getChannel(), nMesgId);
			break;
		}

		nPos = 2;
		for( i=0; i<sData[1]; i++ )  // find all ISUP parameters from data.
		{
			sParamData[0] = 0x00;
			for( j=0; j<sData[nPos+1]; j++ )
			{
				sprintf( sByte, "%02x ", sData[nPos+2+j] );
				strcat( sParamData, sByte );
			}
			sprintf( s_sTmpBuff+strlen(s_sTmpBuff),"Param[%d].Type=0x%x, Param[%d].Value=\"%s\"",
				i, sData[nPos], i, sParamData );

			if(sData[nPos] == 0x12)
			{
				if(sData[nPos+1] >= 2)
				{
					int casevalue = sData[nPos+3];
					if(cr->_releaseValue == -1)
						cr->_releaseValue = (casevalue & 0x7f);
					LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Channel(0x%x:0x%x) ChannelReleasedWithData CaseValue:%s",
						cr->_callID, rRel->getSpan(), rRel->getChannel(),getRELCauseValuesDescription(casevalue & 0x7f));
				}
				else
				{
					LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Channel(0x%x:0x%x) ChannelReleasedWithData CaseValue:%s",
						cr->_callID, rRel->getSpan(), rRel->getChannel(),"Invalid data formats");
				}
			}

			nPos += (sData[nPos+1] + 2);
		}
		LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Channel(0x%x:0x%x) ChannelReleasedWithData parameters:%s", 
			cr->_callID, rRel->getSpan(), rRel->getChannel(), s_sTmpBuff);
	}

	return 0;
}

int SIMMApp::makeRecordFileName(const char * number, char * fileName)
{
	if(number == 0)
		return -1;

	if(fileName == 0)
		return -1;

	//get time
	time_t ltime;
	tm curtime;

	time(&ltime);
	curtime = *localtime(&ltime);

	char strfile[64]={0};
	sprintf(fileName, "./record/%s_%04d%02d%02d%02d%02d%02d.pcm", number, 
		curtime.tm_year+1900, curtime.tm_mon+1, curtime.tm_mday,
		curtime.tm_hour, curtime.tm_min,  curtime.tm_sec);

	return 0;
}

void SIMMApp::printChanneMsg( const CallRecord * cr , SKC_Message *msg, int state, int subState)
{
	if( (cr == 0) || (msg == 0))
	{
		return ;
	}

	char logText[512];
	memset(logText, 0, sizeof(logText));
	sprintf(logText, "State: (%d:%d), Received message: %s",state, subState, msg->getMsgName());

	LogFileManager::getInstance()->write(Debug, 
		"INFO: CallID(%d) In Channel:(0x%x,0x%x), %s",
		cr->_callID,cr->_inSpan, cr->_inChan, logText);
}

void SIMMApp::writeCallRecord( const CallRecord * cr, int rel_span, int rel_chan)
{
	//Write RecordCall to DataBase
	if(cr == 0)
		return ;

	CallBill bill;
	//.s add by whc for printf the call result 20100910
	LogFileManager::getInstance()->write(Brief, "Debug makeDial result(%d).",cr->_routerResult);
	if(cr->_connTime == 0)
	{
		//.s add by whc for printf the call result 20100910
		switch(cr->_routerResult)
		{
		case  CR_CALL_OUT:         /* Directly call out terminal*/
		case  CR_IVR:              /* IVR call*/
		case  CR_SECOND_DIAL:      /* second  dial  process*/
		case  CR_CALL_CENTER:      /* call custom server center*/
		case  CR_VRITUAL_SWITCH:   /* virtual switch test*/
		case  CR_OUT_SERVICE:      /* The service is no permmit*/

			bill.result = -1;
			bill.callInType = (int)cr->_routerResult;
			bill.callContinueTime = 0;
			bill.startTime = cr->_endTime;
			strncpy(bill.device_ID, _opc, sizeof(bill.device_ID)-1);
			memcpy(&bill.numberInfo, &cr->_nbrInfo, sizeof(NumberInfo));

			DataUsage::instance()->saveCallRecord(bill);

			break;
		}

		//bill.callContinueTime = cr->_endTime - cr->_rcvTime;
		//bill.startTime = cr->_rcvTime;

		LogFileManager::getInstance()->write(Verbose, "INFO: CallID(%d) Channel(0x%x,0x%x) NO CALLRECORD ,CONNECTED TIME == 0", 
			cr->_callID,rel_span, rel_chan);

		return;
	}
	else
	{
		bill.callContinueTime = cr->_endTime - cr->_connTime;
		bill.startTime = cr->_connTime;
	}

	//.s Modify for the simm call watching by whc 20100910.
	bill.result=cr->_callResult;
	bill.callInType = (int)cr->_routerResult;
	//.e Modify for the simm call watching by whc 20100910.

	strncpy(bill.device_ID, _opc, sizeof(bill.device_ID)-1);
	memcpy(&bill.numberInfo, &cr->_nbrInfo, sizeof(NumberInfo));

	bill.releaseValue = cr->_releaseValue;

	DataUsage::instance()->saveCallRecord(bill);


	LogFileManager::getInstance()->write(Verbose, "INFO: CallID(%d) Save Channel(0x%x,0x%x) CallRecord OK !", 
		cr->_callID,rel_span, rel_chan);     

}

int SIMMApp::processException(ResultMakeDialRoute result,CallRecord *cr)
{
	if(cr == 0)
	{
		return -1;
	}

	if(cr->_state)
	{
		delete cr->_state;
		cr->_state = 0;
	}

	ExceptionState* state = new ExceptionState(result);
	cr->_state = state;

	int ret = sk_setChannelHandler(cr->_inSpan, cr->_inChan, 
		(void *)cr, ExceptionHandler);
	if( ret != OK )
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR : In Channel(0x%x:0x%x)  setChannelHandler Failed, %s\n",
			cr->_inSpan, cr->_inChan,sk_errorText(ret));

		return ret;
	}
	int fileID[2] = {-1, -1}; 
	switch(state->result_)
	{
	case RESULT_SEQUENCE_NO_NOT_EXIST:
		{
			fileID[0] = 4313;
			break;
		}
	case RESULT_MINOR_NOT_REG:
		{
			fileID[0] = 4301;
			break;
		}
#if 1
	case RESULT_VIRTUAL_MINOR_NOT_REG:
		{
			fileID[0] = 4301;
			break;
		}
#endif

#if 1		
	case RESULT_CALLER_MINOR_STATE_CANCEL:
		{
			fileID[0] = 4434;
			break;
		}
#endif
		
#if 1		
	case RESULT_CALLED_MINOR_STATE_CANCEL:
		{
			fileID[0] = 4435;
			break;
		}
#endif

	case RESULT_NUMBER_ERROR:
		{
			fileID[0] = 4016;
			break;
		}
	case RESULT_MINOR_OFF:
		{
			fileID[0] = 4362;
			break;
		}
	case RESULT_MINOR_OUT_SERVICE:
	case RESULT_MINOR_FORBID_CALLER:
	case RESULT_CALLOUT_LIMIT:
	case RESULT_MINOR_IN_LIMIT_TIME_FRAME:
		{
			fileID[0] = 4350;
			break;
		}
	case RESULT_CALLER_NOT_REG_SIMM:
		{
			fileID[0] = 4301;
			break;
		}
	case RESULT_CALLER_NOT_LOCAL_REG:
		{
			fileID[0] = 4341;
			break;
		}
	default:
		{	
			fileID[0] = 4329;
			break;
		}
	}

	/*zuosai delete 090723
	if (cr->_isAnswer == 0)
	{
	cr->signal_ = SENDOUT_ANM;
	return sendPPLEvent(cr->_inSpan, cr->_inChan, ISUP_ANM, (void *)cr, ExceptionHandler);
	}
	*/	
	return  playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
		(void *)cr, ExceptionHandler);

}

int SIMMApp::ExceptionHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 
	if(cr->_state == 0)
		return -1;

	if(cr->_state->state_ != State::STATE_EXCEPTION)
	{
		LogFileManager::getInstance()->write(Brief, 
			"ERROR: state error In Channel:(0x%x,0x%x)	state = (%d)",
			cr->_inSpan, cr->_inChan, (int)cr->_state->state_);

		return -1;
	}

	ExceptionState * state = reinterpret_cast<ExceptionState *>(cr->_state);
	printChanneMsg(cr, msg, state->state_, state->result_);

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_PPLEventRequestAck(rDspReq)
			return OK;
		CASEC_DSPServiceRequestAck(rDspReq)
			return OK;
		CASEC_DSPServiceCancelAck(rCancelAck)
			return OK;
		CASEC_PlayFileStartAck(playAck)
			return OK;
		CASEC_PlayFileStopAck(rPfs)
			return OK;
		CASEC_CallProcessingEvent(rCpe)
		{
			if(rCpe->getEvent() == FilePlayCmpt)
			{
				/* 计算结束时间 */
				cr->_endTime = time(NULL);
				return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);;
			}
			else if(rCpe->getEvent() == FilePlayOK)
			{
				/* 计算结束时间 */
				cr->_endTime = time(NULL);
				return simm->returnChannels(cr->_inSpan, cr->_inChan, tag);
			}
			else
				return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
		}
		CASEC_default
		{
			return defaultHandler( evt, tag);
		}
	}SKC_END_SWITCH;

	return SK_NOT_HANDLED ;
}

int SIMMApp::playListenRecord(CallRecord *cr)
{

	if ( g_isPrintProcess)
	{
		char tmp[256] = { 0 };
		sprintf(tmp,  "INFO: CallID(%d) Enter Function:playListenRecord() , InCaller = %s ,InCalled = %s,OutCaller = %s ,OutCalled  = %s ,FILE=%s,Line=%d", 
			cr->_callID,cr->_inCaller, cr->_inCalled,cr->_outCaller, cr->_outCalled,__FILE__,__LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);	
	}
	
	IvrListenRecordState* state = reinterpret_cast<IvrListenRecordState *>(cr->_state);
	for( int i = 0; i < MAX_MINOR_COUNT; i++ )
	{
		if( state->recordInfo_[i].recordMinorNo_ == state->minorNo_ )
		{
			state->data_.resetData();
			state->hasSequelData_ = 0;

			string fileName = "";
			string recordTime = "";
			string recordNumber = "";

			int result = 0 ;
			if( state->minorNo_ == '6' )
			{
				LogFileManager::getInstance()->write(Debug, "Enter Function:playListenRecord():case Virtual  no");	
 				result = DataUsage::instance()->getVoiceMsg(state->minorList_[0], fileName, recordTime, recordNumber);
			}
			else
			{
				LogFileManager::getInstance()->write(Debug, "Enter Function:playListenRecord():case not Virtual no");
				 result = DataUsage::instance()->getVoiceMsg(state->minorList_[state->minorNo_-'1'], fileName, recordTime, recordNumber);
			}

			strncpy(state->filePath_, fileName.c_str(), sizeof(state->filePath_)-1);
			if( result != E_OPERATOR_SUCCED )
			{
				/*no voice*/
				/*
				int fileID[2] ={4326,-1};
				state->sub_state_ = IvrListenRecordState::;
				return simm->playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID, 
				(void *)cr, IVR_listenRecordHandler);
				*/
				return processIVR(cr);
			}
			else
			{
				int index = 0;
				int fileID[100];

				for( int i=0; i< 100; i++)
				{
					fileID[i] = -1;
				}

				fileID[index] = 4323; index++; //您的第
				fileID[index] = 1001+ state->playFileNo_ ; index++;
				state->playFileNo_++;
				fileID[index] = 4325; index++;//条新留言
				fileID[index] = 4145; index++;//来自
				int count = phonNum2voiceList(recordNumber.c_str(), &fileID[index]);
				index += count;
				fileID[index] = 4186; index++;//留言时间
				count = time2voiceList(recordTime.c_str(), &fileID[index]);
				index += count; /* #*/
				fileID[index] = 4189; index++;//内容为

				state->sub_state_ = IvrListenRecordState::STATE_ILR_PLAY_RECORD_EXPLAIN_START;
				LogFileManager::getInstance()->write(Debug, "Enter Function:playListenRecord():case play voice");
				if( index < 26 )
				{
					int firstFileId[26];

					for(int i = 0 ; i < 26; i++)
					{
						firstFileId[i] = -1;
					}

					for( int i = 0 ; i < 25; i++ )
					{
						firstFileId[i] = fileID[i];
					}

					return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, firstFileId, (void *)cr, IVR_listenRecordHandler);
				}
				else 
				{
					int firstFileId[25];
					for(int i = 0 ; i < 25; i++)
					{
						firstFileId[i] = -1;
					}

					int i = 0, j = 0;
					for( ; i < 24; i++)
					{
						firstFileId[i] = fileID[i];
					}
					firstFileId[24] = -1;

					//-------------------------------------------------
					int sequelElements = index - i ;
					if( sequelElements > MAX_SEQUEL_PLAY_FILE ) /*文件数大于系统定义*/
					{
						return OK;
					}

					for( ; i < index ; i++, j++)
					{
						state->data_.dataArray_[j] = fileID[i] ;
					}
					state->hasSequelData_ = 1; /*有后续的语音需要播放*/
					state->data_.elements_ = sequelElements ;
					//------------------------------------------------

					return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, firstFileId, (void *)cr, IVR_listenRecordHandler);
				}
			}
		}
	}

	return OK;
}

void SIMMApp::initDialNotifyEnv()
{
	DialNotifyLibWrapper * wrapper = DialNotifyLibWrapper::instance();

	wrapper->init(_smAgentIp, _smAgentPort);

	char logText[128];
	memset(logText, 0, sizeof(logText));
	sprintf(logText, "start Short message Notify module, SmAgent Addr(%s:%d)", _smAgentIp, _smAgentPort);
	LogFileManager::getInstance()->write(Debug, "INFO: %s",logText);
}

int SIMMApp::processRingback(const std::string& minorNumber, CallRecord *cr)
{
	if(cr == 0)
	{
		return -1;
	}

	int ringBackId = 0;

	ResultDataUsage ret = DataUsage::instance()->getRingBackTone(minorNumber,ringBackId);
	if(ret != E_OPERATOR_SUCCED)
	{
		LogFileManager::getInstance()->write(Debug, "ERROR: getRingBackTone return error");
		return -1 ;
	}

	LogFileManager::getInstance()->write(Debug, "INFO: getRingBackTone ringBackId = %d", ringBackId);

	if( ringBackId <= 0 || ringBackId>8)
	{
		return -1;
	}

	cr->_ringBackId = ringBackId;

	int fileID[2]={-1, -1};
	fileID[0] = 4400+ringBackId;/*标准回铃音 4401~4408*/

	LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) Start Play Ringback ", cr->_callID);
	return playVoiceForID(cr->_inSpan, cr->_inChan, Dsp2Slot, fileID,(void *)cr, RingbackHandler);
}

int SIMMApp::RingbackHandler(SK_Event *evt, void *tag)
{
	if((evt == 0) || (tag == 0))
		return -1 ;

	SKC_Message *msg = evt->IncomingCMsg;
	SKC_Message *ackedMsg = evt->AckedCMsg;

	CallRecord *cr = (CallRecord *)tag;
	SIMMApp *simm = (SIMMApp *)cr->_thisPtr; 

	SKC_MSG_SWITCH(msg) 
	{
		CASEC_PlayFileStartAck(rPfs)
		{
			if(rPfs->getStatus() == 0x10)
			{
				LogFileManager::getInstance()->write(Debug, "INFO: CallID(%d) In Channel(0x%x,0x%x), receive Ringback PlayFileStartAck ",
					cr->_callID, cr->_inSpan, cr->_inChan);
				cr->_playingRingback ++ ;
				if(cr->_playingRingback > 1)
					return OK;

				XLC_Connect cnct;

				/* Send a connect message to the switch to connect the 
				* incoming channel to the outseized channel 
				*/
				cnct.setSpanA(cr->_inSpan); 
				cnct.setChannelA(cr->_inChan);
				cnct.setSpanB(cr->_outSpan );
				cnct.setChannelB(cr->_outChan );

				char textLog[512];
				memset(textLog, 0, sizeof(textLog));
				sprintf(textLog,"CallID(%d) Connecting ..(0x%x, 0x%x) And (0x%x, 0x%x)",
					cr->_callID,
					cnct.getSpanA(),
					cnct.getChannelA(),
					cnct.getSpanB(),
					cnct.getChannelB());
				LogFileManager::getInstance()->write(Debug, "INFO: %s", textLog);


				/* For each of the channels being connected, store the other 
				* channel's span and chan in the channel data structure as 
				* the "cnctdSpan" and "cnctdChan".  When one channel is released,
				* it's connecting channel can be released as well, this is done in 
				* returnChannels.  
				*/

				ChannelData *chandat1 = new ChannelData;
				ChannelData *chandat2 = new ChannelData;

				chandat1->cnctdSpan = cr->_outSpan;
				chandat1->cnctdChan = cr->_outChan;
				sk_setChannelData(cr->_inSpan,cr->_inChan,(void *)chandat1);

				chandat2->cnctdSpan = cr->_inSpan;
				chandat2->cnctdChan = cr->_inChan;
				sk_setChannelData(cr->_outSpan,cr->_outChan,(void *)chandat2);

				cr->_reference++ ;

				return cnct.send((void *)cr, handleAcks);
			}
			else
				return (OK);
		}
		CASEC_PlayFileStopAck(rPfs)
		{
			LogFileManager::getInstance()->write(Debug, "INFO: In Channel(0x%x,0x%x), receive Ringback PlayFileStopAck ", cr->_inSpan, cr->_inChan);
			return OK;
		}
		CASEC_CallProcessingEvent(rCpe)
		{
			return simm->handleDefaultCallProcessingEvent(cr, rCpe->getEvent());
		}
		CASEC_default 
			return simm->defaultHandler(evt, tag);

	}SKC_END_SWITCH;

	return (OK);
}

char * SIMMApp::getRELCauseValuesDescription(int causeValue)
{
	switch(causeValue)
	{
	case 0x01: return "Unallocated (unassigned) number";
	case 0x02: return "No route to specified transit network";
	case 0x03: return "No route to destination";
	case 0x04: return "Send special information tone";
	case 0x05: return "Misdialed trunk prefix";
	case 0x06: return "Channel unacceptable";
	case 0x07: return "Call awarded and being delivered in an established channel";
	case 0x08: return "Preemption";
	case 0x09: return "Preemption—circuit reserved for reuse";
	case 0x0e: return "Query On Release (QOR)—ported number";
	case 0x10: return "Normal clearing";
	case 0x11: return "User busy";
	case 0x12: return "No user responding";
	case 0x13: return "No answer from user (user alerted)";
	case 0x14: return "Subscriber absent";
	case 0x15: return "Call rejected";
	case 0x16: return "Number changed";
	case 0x17: return "Redirection to new destination";
	case 0x18: return "Call rejected because of a feature at the destination";
	case 0x19: return "Exchange routing error";
	case 0x1a: return "Nonselected user clearing";
	case 0x1b: return "Destination out of order";
	case 0x1c: return "Invalid number format (address incomplete)";
	case 0x1d: return "Facility rejected";
	case 0x1e: return "Response to Status Enquiry";
	case 0x1f: return "Normal, unspecified";

		//Resource Unavailable Class 
	case 0x22: return "No circuit/channel available";
	case 0x26: return "Network out of order";
	case 0x27: return "Permanent frame mode connection out of service";
	case 0x28: return "Permanent frame mode connection operational";
	case 0x29: return "Temporary failure";
	case 0x2a: return "Switching equipment congestion";
	case 0x2b: return "Access information discarded";
	case 0x2c: return "Requested circuit/channel not available";
	case 0x2d: return "Precedence call blocked";
	case 0x2f: return "Resource unavailable, unspecified";

		//Service or Option Unavailable Class 
	case 0x31: return "Quality of service unavailable";
	case 0x32: return "Requested facility not subscribed";
	case 0x33: return "Outgoing calls barred within Closed User Group";
	case 0x37: return "Incoming calls barred within Closed User Group";
	case 0x39: return "Bearer capability not authorized";
	case 0x3a: return "Bearer capability not presently available";
	case 0x3b: return "Inconsistency in designated outgoing access information and subscriber class";
	case 0x3f: return "Service or option unavailable, unspecified";

		//Service or Option Not Implemented Class 
	case 0x41: return "Bearer capability not implemented";
	case 0x42: return "Channel type not implemented";
	case 0x43: return "Requested facility not implemented";
	case 0x44: return "Only restricted digital information bearer capability is available";
	case 0x45: return "Service or option not implemented, unspecified";

		//Invalid Message Class
	case 0x51:  return "Invalid call reference value";
	case 0x52:  return "Identified channel does not exist";
	case 0x53:  return "A suspended call exists but this call identity does not";
	case 0x54:  return "Call identity in use";
	case 0x55:  return "No call suspended";
	case 0x56:  return "Call that has the requested call identity has been cleared";
	case 0x57:  return "User not member of Closed User Group";
	case 0x58:  return "Incompatible destination";
	case 0x5a:  return "Nonexisting Closed User Group";
	case 0x5b:  return "Invalid transit network selection";
	case 0x5f:  return "Invalid message, unspecified";

		//Protocol Error Class
	case 0x60:  return "Mandatory information element is missing";
	case 0x61:  return "Message type nonexistent or not implemented";
	case 0x62:  return "Message not compatible with call state, or message type nonexistent or not implemented";
	case 0x63:  return "Information element/parameter nonexistent or not implemented";
	case 0x64:  return "Invalid information element contents";
	case 0x65:  return "Message not compatible with call state";
	case 0x66:  return "Recovery on timer expiry";
	case 0x67:  return "Parameter nonexistent or not implemented, passed on";
	case 0x68:  return "Protocol error, unspecified";

		//Interworking Class 
	case 0x7f:  return "Interworking, unspecified";
	default:return "unknow define";
	}
}

