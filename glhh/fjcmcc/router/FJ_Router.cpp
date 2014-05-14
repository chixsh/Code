#include "datausage.h"
#include "globalfuction.h"
#include "LogFileManager.h"
#include "ConfigFile.h"
#include "Router.h"

#define FLOW_17

ResultMakeDialRoute Router::makeDial(const string& dialInCaller, const string& dialInCalled,
										string& dialOutCaller, string& dialOutCalled,NumberInfo& numberInfo)
{

	LogFileManager::getInstance()->write(Debug,"INFO: dialInCaller--> %s, dialInCalled--> %s ",dialInCaller.c_str(), dialInCalled.c_str());

	dialOutCaller.clear();
	dialOutCalled.clear();
	std::string callerNumber = dialInCaller;
	std::string calledNumber = dialInCalled;
	std::string tmpCalledNumber;

	////cut area before access code 
	if(calledNumber[0]=='0')
	{
		int len=0;
		len = routetable_.findAreaCode(calledNumber.c_str(), areacode_);
		if(len==0)
		{
			char tmp[256] = { 0 };
			sprintf(tmp,"INFO: makeDial() findAreaCode len == 0, FILE=%s,Line=%d", __FILE__, __LINE__);
			LogFileManager::getInstance()->write(Debug, "%s",tmp);
			return RESULT_NUMBER_ERROR;
		}
		calledNumber.erase(0, len);
	}
	//check called number 
	if (!VaildDialNumber(calledNumber))
	{
		LogFileManager::getInstance()->write(Brief,"ERROR INFO: +VaildDialNumber.");
		return RESULT_NUMBER_ERROR;
	}

	if (calledNumber.length() < accessCode_.length() || calledNumber.length() > mobileCodeLength_+accessCode_.length()+4)
	{
		LogFileManager::getInstance()->write(Brief,"ERROR INFO: Called number length is error");
		return RESULT_NUMBER_ERROR;
	}

	// .s modified by wj 20130314
	if (calledNumber == secondDial_ || calledNumber == virtualSecondDial_ )
	{
		// 二次呼叫 例如 1258399
		return toSecondDial(callerNumber, calledNumber, dialInCalled, dialOutCaller, dialOutCalled, numberInfo);
	}

	if (calledNumber == callCenterNumber_ || calledNumber == virtualAccessCode_ )
	{
		// 转接到呼叫中心 12583
		return toCallCenter(callerNumber, calledNumber, dialInCalled, dialOutCaller, dialOutCalled, numberInfo);
	}

	if (calledNumber == ivrCode_ || calledNumber == virtualIvrCode_)		
	{
		//转接到IVR 1258300 
		return toIVR(callerNumber, calledNumber, dialInCalled, dialOutCaller, dialOutCalled, numberInfo);
	}
    // .e modified by wj 20130314

    //To check the whether there is access code.12583 or 95096
	if (strncmp(calledNumber.c_str(),accessCode_.c_str(),accessCode_.length())!=0)
	{
		// 被叫没有接入码
		// 真实号码呼叫真实副号码 
		//137xxxxxxxxx -> 135xxxxxxxx 021-12345678
		LogFileManager::getInstance()->write(Brief,"DEBUG INFO:Start makeDial majorToTrueMinor .");
		return majorToTrueMinor(callerNumber, calledNumber, dialInCalled, dialOutCaller, dialOutCalled, numberInfo);
	}
	else
	{
		// 被叫有接入码
		calledNumber = calledNumber.substr(accessCode_.length());
			
		if(calledNumber.size()==0)
		{
				//There is no number info.
			return RESULT_NUMBER_ERROR;
		}

		const char* p = calledNumber.c_str();
		//对接入的号码做限制
		string strTempCalledNumber=calledNumber ;
		if ( p[0] == '1'  || p[0] == '2' || p[0] == '3' || p[0] == '6')
		{
			strTempCalledNumber = strTempCalledNumber.substr(1);
			for ( int i = 0 ;i < m_vecLimitedCalled.size() ; i++)	
			{
				if (strncmp(strTempCalledNumber.c_str(),m_vecLimitedCalled[i].c_str(),m_vecLimitedCalled[i].length()) ==0)
				{
					LogFileManager::getInstance()->write(Brief,"LIMIT_CALLED:=%s.",strTempCalledNumber.c_str());
					return 	RESULT_NUMBER_ERROR ;		
				}
			}
		}
		

		vector<MinorNumberAttr>  minorNumbersCaller;
		string strMinorNum="";
		ResultDataUsage ret = DataUsage::instance()->getAllMinorNumber(callerNumber, minorNumbersCaller);
		ResultDataUsage retDataResult = DataUsage::instance()->getMinorNumber(callerNumber,6,strMinorNum);
		if(E_OPERATOR_SUCCED != ret || minorNumbersCaller.size()==0  )
		{
			// 真实号码呼叫虚拟副号码 
			return majorToDummyMinor(callerNumber, calledNumber, dialInCalled, dialOutCaller, dialOutCalled, numberInfo);
		}
		else if( (p[0] == '6') && (retDataResult != E_OPERATOR_SUCCED)) //注册了
		{
			// 真实号码呼叫虚拟副号码 
			return majorToDummyMinor(callerNumber, calledNumber, dialInCalled, dialOutCaller, dialOutCalled, numberInfo);
		}
		else
		{

			//.1 判断被叫号码是否合法		
		
			if (p[0]>'9'|| p[0]<'0')
			{
				//The code number formate is error.
				return  RESULT_NUMBER_ERROR;
			}	
			//这里需要判断主叫和被叫是否都已经注册一卡多号业务

	       	 LogFileManager::getInstance()->write(Brief,"DEBUG INFO:Start makeDial minorToNumber.");
			return minorToNumber(callerNumber, calledNumber, dialInCalled, dialOutCaller, dialOutCalled, numberInfo);
		}

	}
	return RESULT_OK;
}

ResultMakeDialRoute Router::majorToTrueMinor(const string& callerNumber, const string& calledNumber, const string& dialInCalled, 
												string& dialOutCaller, string& dialOutCalled, NumberInfo& numberInfo)
{
	//called number length error
	if (calledNumber.length()!=mobileCodeLength_)
	{
		LogFileManager::getInstance()->write(Brief,"ERROR INFO: majorToTrueMinor Called number length is wrong!");
		return RESULT_NUMBER_ERROR;
	}

	std::string majorNumber;
	int sequenceNo = 0;
	ResultDataUsage result = DataUsage::instance()->getMinorIndex(calledNumber,majorNumber,sequenceNo);
	if(DataUsage::instance()->getMinorIndex(calledNumber,majorNumber,sequenceNo)!= E_OPERATOR_SUCCED)
	{
		LogFileManager::getInstance()->write(Brief,"ERROR INFO: minor number is not exist...");
		return RESULT_MINOR_NOT_REG;
	}

	string scrCallerNumber = callerNumber;
#ifdef FLOW_17
    int len=0;
	char localAreaCode[5];
	memset(localAreaCode,'\0',5);
	if(scrCallerNumber.find_first_of("0",0)==0)
	{
		len = routetable_.findAreaCode(scrCallerNumber.c_str(), localAreaCode);
		if(len==4&&scrCallerNumber.size()>11)
		{
			scrCallerNumber.erase(0,1);
		}
	}
#endif

	//////////////////set dialOutCaller
	char seqNoStr[4] = {0}; 
	memset(seqNoStr,'\0',4);
	sprintf(seqNoStr, "%d",sequenceNo);

	////set dialOutCaller
    if(cutType_ == 1)
	{
		if(customCallerDisplay_ == "yes")
		{		
			dialOutCaller = accessCode_;
			dialOutCaller += seqNoStr;
			dialOutCaller += scrCallerNumber;
		}
		else
		{
			if(DataUsage::instance()->isCustomCallerDisplay(majorNumber) == true)
			{		
				dialOutCaller = accessCode_;
                dialOutCaller += seqNoStr;
				dialOutCaller += scrCallerNumber;
			}
			else
			{
				dialOutCaller = scrCallerNumber;
			}
		}
		sprintf(numberInfo.callerNumberMsg, "%s%d%s",accessCode_.c_str(),sequenceNo,callerNumber.c_str());
	}
	else  if(cutType_ == 0)
	{
		dialOutCaller = accessCode_;
		dialOutCaller += seqNoStr;
		dialOutCaller += scrCallerNumber;
		if (strlen(dialOutCaller.c_str()) > 17)
        {
            dialOutCaller[17] = '\0';	
        }
	}
	////set dialOutCalled
	dialOutCalled = majorNumber;

	if (callerNumber == majorNumber)
	{
		LogFileManager::getInstance()->write(Brief,"ERROR INFO: CALL SELF NUMBER...");
		return RESULT_NUMBER_ERROR;
	}

	////set NumberInfo	
	numberInfo.callType=2;
	strcpy(numberInfo.majorNumber,majorNumber.c_str());
	strcpy(numberInfo.minorNumber,dialInCalled.c_str());
	strcpy(numberInfo.originalNumber,callerNumber.c_str());
	strcpy(numberInfo.beCalled,calledNumber.c_str());

	/////////////////////////////////////
	////////////// check rules //////////
	//号码是否合法
	if (calledNumber.length()!=mobileCodeLength_)
	{
		return RESULT_NUMBER_ERROR;
	}
	//被叫是否在主叫的范围内
	if (DataUsage::instance()->isInCallerLimitedList(calledNumber) != E_OPERATOR_SUCCED)
	{
		return RESULT_CALLOUT_LIMIT;
	}

#if 0
	//被叫是否注册
	ResultDataUsage retState = DataUsage::instance()->getMinorNumberState(calledNumber);
	switch(retState)
	{
	case STATE_ARREARAGE:
		return RESULT_MINOR_OUT_SERVICE; //被叫欠费
	case  STATE_LOGOUT:
		return RESULT_MINOR_NOT_REG; //被叫没有注册
	case E_MINORNUMBER_NOT_EXESIT:
		return RESULT_MINOR_NOT_REG; //被叫没有注册
	}
#endif

	// .s modified by wj 20120821
#if 1	
	//被叫是否注册
	ResultDataUsage retState = DataUsage::instance()->getMinorNumberState(calledNumber);
	switch(retState)
	{
	case STATE_ARREARAGE:	// 欠费(4)
		return RESULT_MINOR_OUT_SERVICE; //被叫欠费
	case  STATE_LOGOUT:		// 暂停(3)
		return RESULT_CALLED_MINOR_STATE_CANCEL; //被叫副号码暂停
	case E_MINORNUMBER_NOT_EXESIT:
		return RESULT_MINOR_NOT_REG; //被叫没有注册
	}
#endif	
	// .e modified by wj 20120821


	//主叫是否在被叫的限制名单
	ResultDataUsage ret = DataUsage::instance()->isInCalledList(callerNumber,calledNumber);
	if (ret != E_OPERATOR_SUCCED)
	{
		return doReturn(ret);
	}
	//主叫是否转入被叫的语音信箱
	if (DataUsage::instance()->getVoiceBoxState(calledNumber) == VOICEBOXSTATE_TRUE)
	{
		//语音信箱的类型
		if (DataUsage::instance()->getVoiceBoxType(calledNumber)== VOICEBOXTYPE_ALL)
		{
			return RESULT_DIAL_TO_RECORD;
		}
		if (DataUsage::instance()->getVoiceBoxType(calledNumber)== VOICEBOXTYPE_PART)
		{
			if(DataUsage::instance()->isInVoiceList(callerNumber,calledNumber) == E_IN_LIMITEIED_LIST_ERROR)
			{
				return RESULT_DIAL_TO_RECORD;
			}
		}
	}
	//被叫是否关机
	if (retState == STATE_OFFLINE)
	{
		return RESULT_MINOR_OFF;
	}
	//是否在被叫限制时间内
	ret = DataUsage::instance()->isInLimitedTime(calledNumber);
	if (ret != E_OPERATOR_SUCCED)
	{
		return doReturn(ret);
	}
	return RESULT_OK;

}

//process the called number has the sequence number
ResultMakeDialRoute Router::minorToNumber(const string& callerNumber, string& calledNumber, const string& dialInCalled,
											string& dialOutCaller, string& dialOutCalled, NumberInfo& numberInfo)
{
    char tmp[256] = { 0 };

    sprintf(tmp,"INFO: callerNumber = %s, calledNumber = %s, dialInCalled = %s, dialOutCaller = %s,dialOutCalled = %s, FILE = %s,Line = %d", callerNumber.c_str(), calledNumber.c_str(), dialInCalled.c_str(), dialOutCaller.c_str(), dialOutCalled.c_str(), __FILE__, __LINE__);

	 LogFileManager::getInstance()->write(Debug, "%s",tmp);


    int callerType = 0;
	if( calledNumber.size() == 0 )
	{
		 sprintf(tmp,"DEBUG: calledNumber.size() == 0,  FILE = %s, LINE = %d ", __FILE__, __LINE__);

	 LogFileManager::getInstance()->write(Debug, "%s",tmp);

		return RESULT_NUMBER_ERROR;
	}
	
	const char* p = calledNumber.c_str();
	if (p[0]>'9'||p[0]<'0')
	{
		 sprintf(tmp,"DEBUG: calledNumber number error,  FILE = %s, __LINE__ = %d ", __FILE__, __LINE__);

		LogFileManager::getInstance()->write(Debug, "%s",tmp);

		return  RESULT_NUMBER_ERROR;
	}

	std::string minorNumber;
	ResultDataUsage retDataResult;
	int majorSequenceNo = -1;

	/* 真实号码呼叫虚拟副号码 */
	//if (p[0] == '0' || p[0]>'3')
	if( p[0] == '0' || (p[0]>'3' && p[0]!='6'))
	{
		if (calledNumber.length() < 3)
		{
			sprintf(tmp,"DEBUG: calledNumber.length() < 3,  FILE = %s, __LINE__ = %d", __FILE__, __LINE__);
			LogFileManager::getInstance()->write(Debug, "%s",tmp);
			return RESULT_NUMBER_ERROR;
		}

		int virtualAsCaller_ = 0;
		for(int loopI = 0; loopI <virtualNumberLength_.size(); loopI++)
		{
			if( calledNumber.length() == virtualNumberLength_[loopI] )//虚拟副号码z作主叫
			{
				virtualAsCaller_ = 1;
				break;
			}
		}

		if(virtualAsCaller_ == 0)
		{
			sprintf(tmp,"DEBUG: virtualAsCaller_ == 0, FILE = %s, LINE = %d", __FILE__, __LINE__);

			LogFileManager::getInstance()->write(Debug, "%s",tmp);
			return	RESULT_NUMBER_ERROR;
		}
		else
		{
			return majorToDummyMinor(callerNumber, calledNumber, dialInCalled, dialOutCaller, dialOutCalled, numberInfo);
		}
	}
	else    // 副号码做主叫
	{
		//To filter the sequence and get the number.
		majorSequenceNo = atoi((calledNumber.substr(0,1)).c_str());
		calledNumber = calledNumber.substr(1);
		if (calledNumber == callerNumber || calledNumber.length() < 3)
		{
			sprintf(tmp,"DEBUG: calledNumber == callerNumber || calledNumber.length() < 3,  FILE = %s, LINE = %d ", __FILE__, __LINE__);

			LogFileManager::getInstance()->write(Debug, "%s",tmp);

			return RESULT_NUMBER_ERROR;
		}
   		 //Check the caller number 		
		retDataResult = DataUsage::instance()->getMinorNumber(callerNumber,majorSequenceNo,minorNumber);
		if(retDataResult == E_MINORINDEX_NUMBER_NOT_EXESIT)
		{
			sprintf(tmp,"DEBUG: retDataResult == E_MINORINDEX_NUMBER_NOT_EXESIT, FILE = %s, LINE = %d ", __FILE__, __LINE__);

			LogFileManager::getInstance()->write(Debug, "%s",tmp);

			return RESULT_SEQUENCE_NO_NOT_EXIST;
		}
		else if( retDataResult != E_OPERATOR_SUCCED)
		{
			std::string callerAreaCode;
			ResultDataUsage callerAreaRet = DataUsage::instance()->getCallerAreaCode(callerNumber, callerAreaCode);
	
			if(callerAreaRet == E_OPERATOR_SUCCED && callerAreaCode != localAreaCode_)
			{
					sprintf(tmp,"DEBUG: callerAreaRet == E_OPERATOR_SUCCED && callerAreaCode != localAreaCode_, FILE = %s, LINE = %d ", __FILE__, __LINE__);

			LogFileManager::getInstance()->write(Debug, "%s",tmp);

				return RESULT_CALLER_NOT_LOCAL_REG;
			}
			else//(callerAreaRet == E_CALLER_NOT_EXIST_IN_AREACODE)  is not ChinaMobile user
			{
				sprintf(tmp,"DEBUG: callerAreaRet == E_OPERATOR_SUCCED && callerAreaCode != localAreaCode_ else, FILE = %s, LINE = %d ", __FILE__, __LINE__);

			LogFileManager::getInstance()->write(Debug, "%s",tmp);

				return RESULT_CALLER_NOT_REG_SIMM;
			}
		}
		//Check the called number and get the called major number .
		std::string majorNumber;
		int sequenceNo = 0;
		retDataResult = DataUsage::instance()->getMinorIndex(calledNumber,majorNumber,sequenceNo);

		string srcCalledNumber = calledNumber;

#ifdef FLOW_17
        if(sequenceNo==0)
		{
			int len=0;
			char localAreaCode[5];
			memset(localAreaCode,'\0',5);
			
			if(srcCalledNumber.length()>5 && srcCalledNumber.find_first_of("0",0)!=0)
			{
				string tmpStr = "0";
				tmpStr = tmpStr+srcCalledNumber;
				len = routetable_.findAreaCode(tmpStr.c_str(), localAreaCode);
				if(len!=0)
				{
					srcCalledNumber.clear();
					srcCalledNumber = tmpStr;
					calledNumber = srcCalledNumber;
				}
			}
		}
#endif

		/////////set NumberInfo
		numberInfo.callType=1;
		strcpy(numberInfo.majorNumber,callerNumber.c_str());
		strcpy(numberInfo.minorNumber,minorNumber.c_str());
		strcpy(numberInfo.originalNumber,srcCalledNumber.c_str());
		strcpy(numberInfo.beCalled,srcCalledNumber.c_str());

		LogFileManager::getInstance()->write(Brief,"INFO: callerNumber = %s,.",callerNumber.c_str());
		LogFileManager::getInstance()->write(Brief,"INFO: .majorSequenceNo =%d.",majorSequenceNo);
		if (sequenceNo == 0)
		{
			//被叫是真实号码
			return minorToMajor( calledNumber, minorNumber, dialOutCaller, dialOutCalled);
		}
		else
		{
			//被叫是副号码
			return minorToMinor( calledNumber, majorNumber, minorNumber, sequenceNo, dialOutCaller, dialOutCalled, numberInfo);
		}
	}
	return RESULT_OK;
}

//process the called number has the sequence number

ResultMakeDialRoute Router::minorToMajor(const string& calledNumber, const string& minorNumber,  
											string& dialOutCaller, string& dialOutCalled)
{
	LogFileManager::getInstance()->write(Brief,"DEBUG INFO:Start makeDial minorToMajor.");

	////////set dialOutCalled
	dialOutCalled = calledNumber;

	////////set dialOutCaller
	int virtualAsCalled = 0;
	for(int loopI = 0; loopI <virtualNumberLength_.size(); loopI++)
	{
		if (minorNumber.length() == virtualNumberLength_[loopI])//虚拟副号码z作主叫
		{
			virtualAsCalled = 1;
			break;
		}
	}
	if (virtualAsCalled == 1)//虚拟副号码z作主叫
	{
		dialOutCaller = accessCode_;
		//beigin modify  by pengjh for virtual number 2013.03.25
		dialOutCaller +="6";
		//end modify  by pengjh for virtual number 2013.03.25
		dialOutCaller += minorNumber;
	}
	else
	{
		dialOutCaller = minorNumber;
	}
	////////check rules //////

	// .s modified by wj 20120724
#if 1
	//主叫副号码 是否注册
	ResultDataUsage retState = DataUsage::instance()->getMinorNumberState(minorNumber);
	switch(retState)
	{
	case  STATE_ARREARAGE:		//欠费
	case  STATE_LOGOUT:			//已注销
		return RESULT_CALLER_MINOR_STATE_CANCEL;		
	}
#endif	
	// .e modified by wj 20120724

	//被叫是否在主叫的范围内
	if (DataUsage::instance()->isInCallerLimitedList(calledNumber)!= E_OPERATOR_SUCCED)
	{
		return RESULT_CALLOUT_LIMIT;
	}
	//被叫禁止副号码呼入
	if( DataUsage::instance()->checkBadCallLimit(calledNumber, minorNumber) != E_OPERATOR_SUCCED)
	{
		return RESULT_CALLED_FORBID_MINOR;
	}

	return RESULT_OK;
}


ResultMakeDialRoute Router::minorToMinor(const string& calledNumber, const string& majorNumber, const string& minorNumber, int sequenceNo,
											string& dialOutCaller, string& dialOutCalled, NumberInfo& numberInfo)
{
	////////set dialOutCalled
	LogFileManager::getInstance()->write(Brief,"DEBUG INFO:Start makeDial minorToMinor.");
	dialOutCalled = majorNumber;
	int numberType_ = -1;

	////////set dialOutCaller
	int virtualAsCalled = 0;
	char seqNum[2];
	memset(seqNum,'\0',2);
	sprintf(seqNum,"%d",sequenceNo);
	for(int loopI = 0; loopI <virtualNumberLength_.size(); loopI++)
	{
		if (calledNumber.length() == virtualNumberLength_[loopI])//虚拟副号码z作主叫
		{
			virtualAsCalled = 1;
			break;
		}
	}
	if (virtualAsCalled != 1)//虚拟副号码z作主叫
	{
		numberType_ = 0;
	}
	else
	{
	    numberType_ = 1;	
	}
	if (cutType_ == 1)
	{
		/* 被叫是虚拟副号码 */
		if (numberType_ == 1)
		{
		    dialOutCaller = accessCode_;
			dialOutCaller += seqNum;
			dialOutCaller += minorNumber;
		}
		else
		{
			/* 被叫是真实副号码 */
			if(customCallerDisplay_ == "yes")
			{
				dialOutCaller = accessCode_;
				dialOutCaller += seqNum;
				dialOutCaller += minorNumber;
			}
			else
			{
				dialOutCaller = minorNumber;
				if(DataUsage::instance()->isCustomCallerDisplay(majorNumber) == true)
				{
                    dialOutCaller = accessCode_;
					dialOutCaller += seqNum;
					dialOutCaller += minorNumber;
				}
			}
			sprintf(numberInfo.callerNumberMsg, "%s%d%s",accessCode_.c_str(),sequenceNo,minorNumber.c_str());
		}	
	}
	else if (cutType_ == 0)
	{
		/* 被叫是虚拟副号码 */
		if (numberType_ == 1)
		{
			dialOutCaller = accessCode_;
			dialOutCaller += seqNum;
			dialOutCaller += minorNumber;
			sprintf(numberInfo.callerNumberMsg, "%s%d%s",accessCode_.c_str(),sequenceNo,minorNumber.c_str());
		}
		else
		{
			/* 被叫是真实副号码 */
			dialOutCaller = minorNumber;
			sprintf(numberInfo.callerNumberMsg, "%s%d%s",accessCode_.c_str(),sequenceNo,minorNumber.c_str());
		}
		if (dialOutCaller.length() > 14)
		{
			dialOutCaller[14] = '\0';
		}		
	}

	///////////////////////////////
	////// check rules ///////////

	if( sequenceNo > 6)
	{
		 return RESULT_DIAL_TO_RECORD ;
	}

	//被叫是否在主叫的范围内
	if (DataUsage::instance()->isInCallerLimitedList(calledNumber)!= E_OPERATOR_SUCCED)
	{
		return RESULT_CALLOUT_LIMIT;
	}
	//被叫禁止副号码呼入
	if( DataUsage::instance()->checkBadCallLimit(calledNumber, minorNumber) != E_OPERATOR_SUCCED)
	{
		return RESULT_CALLED_FORBID_MINOR;
	}

	// .s modified by wj 20120724
#if 1
	//主叫副号码 是否注册
	ResultDataUsage retState = DataUsage::instance()->getMinorNumberState(minorNumber);
	switch(retState)
	{
	case  STATE_ARREARAGE:		//欠费
	case  STATE_LOGOUT:			//已注销
		return RESULT_CALLER_MINOR_STATE_CANCEL;		
	}
#endif	
	// .e modified by wj 20120724

	// .s modified by wj 20120821
#if 1	
	//被叫副号码是否注册
	//ResultDataUsage 
	retState = DataUsage::instance()->getMinorNumberState(calledNumber);
	switch(retState)
	{
	case STATE_ARREARAGE:	// 欠费(4)
		return RESULT_MINOR_OUT_SERVICE; //被叫欠费

	case  STATE_LOGOUT:			// 暂停(3)		//被叫副号码暂停(3)
		return RESULT_CALLED_MINOR_STATE_CANCEL; //被叫副号码暂停

	case E_MINORNUMBER_NOT_EXESIT:		//被叫副号码没有注册
		return RESULT_MINOR_NOT_REG; //被叫没有注册
	}
#endif	
	// .e modified by wj 20120821

#if 0
	//被叫是否注册
	ResultDataUsage retState = DataUsage::instance()->getMinorNumberState(calledNumber);
	switch(retState)
	{
	case  STATE_ARREARAGE:
		return  RESULT_MINOR_OUT_SERVICE; //被叫欠费
	case  STATE_LOGOUT:
		return  RESULT_MINOR_NOT_REG; //被叫没有注册
	case  E_MINORNUMBER_NOT_EXESIT:
		return  RESULT_MINOR_NOT_REG; //被叫没有注册
	}
#endif


	//主叫是否在被叫的限制名单
	ResultDataUsage ret = DataUsage::instance()->isInCalledList(minorNumber,calledNumber);
	if (ret !=  E_OPERATOR_SUCCED)
	{
		return doReturn(ret);
	}
	//主叫是否转入被叫的语音信箱
	if (DataUsage::instance()->getVoiceBoxState(calledNumber) ==  VOICEBOXSTATE_TRUE)
	{
		//语音信箱的类型
		if (DataUsage::instance()->getVoiceBoxType(calledNumber)==  VOICEBOXTYPE_ALL)
		{
			return  RESULT_DIAL_TO_RECORD;
		}
		if (DataUsage::instance()->getVoiceBoxType(calledNumber)==  VOICEBOXTYPE_PART)
		{
			if(DataUsage::instance()->isInVoiceList(minorNumber,calledNumber) ==  E_IN_LIMITEIED_LIST_ERROR)
			{
				return  RESULT_DIAL_TO_RECORD;
			}
		}
	}
	//被叫是否关机
	if (retState ==  STATE_OFFLINE)
	{
		return  RESULT_MINOR_OFF;
	}
	//是否在被叫限制时间内
	ret = DataUsage::instance()->isInLimitedTime(calledNumber);
	if (ret !=  E_OPERATOR_SUCCED)
	{
		return doReturn(ret);
	}

	return RESULT_OK;
}

/*
 *真实号码呼叫虚拟副号码
 */
ResultMakeDialRoute Router::majorToDummyMinor(const string& callerNumber, const string& calledNumber, const string& dialInCalled, 
											string& dialOutCaller, string& dialOutCalled, NumberInfo& numberInfo)
{
	char szChar[256]={0};
	sprintf(szChar,"DEBUG INFO:Start makeDial majorToDummyMinor. customCallerDisplay_=%s,cutType_ = %d ",customCallerDisplay_.c_str(),cutType_);
	LogFileManager::getInstance()->write(Debug,"%s",szChar);
	char tmp[256] = { 0 };
  	sprintf(tmp,"INFO: callerNumber = %s, calledNumber = %s, dialInCalled = %s, dialOutCaller = %s,dialOutCalled = %s, FILE = %s,Line = %d", callerNumber.c_str(), calledNumber.c_str(), dialInCalled.c_str(), dialOutCaller.c_str(), dialOutCalled.c_str(), __FILE__, __LINE__);
	LogFileManager::getInstance()->write(Debug, "%s",tmp);
	string majorNumber;
	int sequenceNo = 0;

	//need remove the number 6
	const char* pFlag = calledNumber.c_str();
	string  calledNumber_=calledNumber;
	if(*pFlag == '6')
	{
		calledNumber_ = calledNumber.substr(1);
	}
	
	
	ResultDataUsage   iGetIndex =DataUsage::instance()->getMinorIndex(calledNumber_,majorNumber,sequenceNo);
	LogFileManager::getInstance()->write(Debug,"majorToDummyMinor:iGetIndex =%d ...",iGetIndex);
	sprintf(tmp,"INFO: calledNumber_ = %s, strlen = %d,length = %d,  CalledSeq=%d,FILE = %s,Line = %d", calledNumber_.c_str(), strlen(calledNumber_.c_str()), calledNumber_.length(), sequenceNo, __FILE__, __LINE__);
	LogFileManager::getInstance()->write(Debug, "%s",tmp);
	
	if (callerNumber == majorNumber)
	{
		LogFileManager::getInstance()->write(Debug,"DEBUG INFO: CALL SELF NUMBER...");
		return RESULT_NUMBER_ERROR;
	}

	char numSeqStr[12];
	memset(numSeqStr,'\0',2);
	sprintf(numSeqStr,"%d",sequenceNo);

	//////////////set NumberInfo
	numberInfo.callType=2;
	strcpy(numberInfo.majorNumber,majorNumber.c_str());
	strcpy(numberInfo.minorNumber,calledNumber_.c_str());
	strcpy(numberInfo.originalNumber,callerNumber.c_str());
	strcpy(numberInfo.beCalled,calledNumber_.c_str());

	// set dialOutCalled
	dialOutCalled = majorNumber;

	string srcCaller = callerNumber;
#ifdef FLOW_17
    int len=0;
	char localAreaCode[5];
	memset(localAreaCode,'\0',5);
	if(srcCaller.find_first_of("0",0)==0)
	{
		len = routetable_.findAreaCode(srcCaller.c_str(), localAreaCode);
		if(len==4&&srcCaller.size()>11)
		{
			srcCaller.erase(0,1);
		}
	}
#endif
	// set dialOutCaller
	if (cutType_ == 1)
	{
		if(customCallerDisplay_ == "yes")
		{
            		dialOutCaller = accessCode_;
			dialOutCaller += numSeqStr;
			dialOutCaller += srcCaller;
		}
		else
		{
			dialOutCaller = srcCaller;
			if(DataUsage::instance()->isCustomCallerDisplay(majorNumber) == true)
			{				
				dialOutCaller = accessCode_;
				dialOutCaller += numSeqStr;
				dialOutCaller += srcCaller;
			}
		}
		sprintf(numberInfo.callerNumberMsg, "%s%d%s",accessCode_.c_str(),sequenceNo,srcCaller.c_str());
	}
	else if (cutType_ == 0)
	{
		dialOutCaller = accessCode_;
		dialOutCaller += numSeqStr;
		dialOutCaller += srcCaller;

		if (dialOutCaller.length() > 14)
		{
			dialOutCaller[14] = '\0';	
		}
	}
	////////////////////////////
	////////// check rules ////

	//被叫是否在主叫的范围内
	if (DataUsage::instance()->isInCallerLimitedList(calledNumber_)!= E_OPERATOR_SUCCED)
	{
		return RESULT_CALLOUT_LIMIT;
	}

	std::string callerAreaCode;
	ResultDataUsage callerAreaRet = DataUsage::instance()->getCallerAreaCode(callerNumber, callerAreaCode);


	// .s modified by wj 20120821

	if(sequenceNo > 6)
	{
		return RESULT_DIAL_TO_RECORD; 
	}

	const string strCalled=calledNumber_ ;
	//被叫副号码是否注册
	ResultDataUsage retState = DataUsage::instance()->getMinorNumberState(strCalled);
	switch(retState)
	{
	case STATE_ARREARAGE:	// 欠费(4)
		return RESULT_MINOR_OUT_SERVICE; //被叫欠费

	case  STATE_LOGOUT:			// 暂停(3)		//被叫副号码暂停(3)
		return RESULT_CALLED_MINOR_STATE_CANCEL; //被叫副号码暂停

	case E_MINORNUMBER_NOT_EXESIT:		//被叫副号码没有注册
		if(callerAreaRet == E_OPERATOR_SUCCED && callerAreaCode != localAreaCode)
		{
			return RESULT_CALLER_NOT_LOCAL_REG;
		}
		else//(callerAreaRet == E_CALLER_NOT_EXIST_IN_AREACODE)  is not ChinaMobile user
		{
			return RESULT_MINOR_NOT_REG; //被叫没有注册
		}
	}

	// .e modified by wj 20120821


	//主叫是否在被叫的限制名单
	ResultDataUsage ret = DataUsage::instance()->isInCalledList(callerNumber,calledNumber_);
	if (ret !=  E_OPERATOR_SUCCED)
	{
		return doReturn(ret);
	}
	//主叫是否转入被叫的语音信箱
	if (DataUsage::instance()->getVoiceBoxState(calledNumber_) ==  VOICEBOXSTATE_TRUE)
	{
		//语音信箱的类型
		if (DataUsage::instance()->getVoiceBoxType(calledNumber_)==  VOICEBOXTYPE_ALL)
		{
			return  RESULT_DIAL_TO_RECORD;
		}
		if (DataUsage::instance()->getVoiceBoxType(calledNumber_)==  VOICEBOXTYPE_PART)
		{
			if(DataUsage::instance()->isInVoiceList(callerNumber,calledNumber_) ==  E_IN_LIMITEIED_LIST_ERROR)
			{
				return  RESULT_DIAL_TO_RECORD;
			}
		}
	}
	//被叫是否关机
	if (retState ==  STATE_OFFLINE)
	{
		return  RESULT_MINOR_OFF;
	}
	//是否在被叫限制时间内
	ret = DataUsage::instance()->isInLimitedTime(calledNumber_);
	if (ret !=  E_OPERATOR_SUCCED)
	{
		return doReturn(ret);
	}
	return RESULT_OK;
}

