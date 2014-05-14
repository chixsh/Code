/*************************************************************************************
* NAME      : DataUsage.cpp
*
* FUNCTION  : Define the DataUseage class and provide interfaces for data 
*             application logic processing.
*
* DATE(ORG) : 
*
* PROJECT   : 
* AUTHER    : xieliang
*
**************************************************************************************/
#include "datausage.h"
#include "globalmacros.h"
#include "globalfuction.h"
#include "LogFileManager.h"
#include "MyMutex.h"
#include <assert.h>
#include <time.h>
#include <string>


#include "tinyxml.h"
#include "crypt.h"
#include <unistd.h>

//To declare the object and set the initial value to 0.
DataUsage* DataUsage::datausage_ = 0;

/************************************************************************************
*
* NAME       : instance 
*
* FUNCTION   : The function is used to initialize singleton object according with config file. 
*              To check the object whether has been initialized.
*              1:If the object is initialized ,return the object handler.
*              2:If not been initialized ,to initialize the object , 
*                get the info from config file and connect the database. 
*
* INPUT      : None
*
* OUTPUT     : None
*
* RETURN     : The singleton object of DataUsage.  type:DataUsage* 
*
************************************************************************************/ 
DataUsage* DataUsage::instance()
{
    if (datausage_ == 0)
    {
        initialize();

    }
    return datausage_;
}

/************************************************************************************
*
* NAME       : DataUsage 
*
* FUNCTION   : To contruct the object handler. 
*              Initialize the ConfigFile object handler and dbo_Mysql object.
*
* INPUT      : None
*
* OUTPUT     : None
*
* RETURN     : None
*
************************************************************************************/ 
DataUsage::DataUsage()
{
    configfile_ = new ConfigFile();
    assert(configfile_);
    
    dbmysql_ = new dbo_Mysql();
    channelID_ = CHANNEL_UNKNOW;         //initalize the chanelID
    assert(dbmysql_);    
}

/************************************************************************************
*
* NAME       : ~DataUsage 
*
* FUNCTION   : To deconstruct the object handler in the construct function, 
*              release mutex locker and the space of the limit call record. 
*
* INPUT      : None
*
* OUTPUT     : None
*
* RETURN     : None
*
***********************************************************************************/ 
DataUsage::~DataUsage()
{
    if(configfile_)
	{
		delete configfile_;
	}

    if(dbmysql_)
	{
		delete dbmysql_;
	}

    MyGuard myGuard(&(myMutex_));
    int recordSize = limitCalledNumberRecord_.size();
    BTree* pTree = NULL;
    for (int i=0;i<recordSize;i++ )
    {
        pTree = limitCalledNumberRecord_[i];
        if (pTree != NULL)
        {
            delete pTree;
        }    
    }
}

/************************************************************************************
*
* NAME       : initialize 
*
* FUNCTION   : This is a static function used to initial config by application . 
*              To initialize the object of DataUsage, 
*              get the info from config file and connect the database.
*
* INPUT      : confilename  std::string . The config file name is used for initial.
*
* OUTPUT     : None
*
* RETURN     : -1:  The initial operation fail.
*               0:  Operation sucess.
*
************************************************************************************/ 
int DataUsage::initialize(std::string confilename)
{
    datausage_ = new DataUsage();
    if( datausage_ == 0 )
	{
		return -1;
	}

    if( datausage_->init(confilename) == false )
	{
		return -1;
	}

    return 0;
}

/************************************************************************************
*
* NAME       : finish 
*
* FUNCTION   : To deconstruct DataUsage objects and set the object value to NULL.
*
* INPUT      : None.
*
* OUTPUT     : None
*
* RETURN     : None
*
************************************************************************************/ 
void DataUsage::finish()
{
    if(datausage_)
	{
		delete datausage_;
	}

    datausage_ = NULL;
}

/***********************************************************************************
*
* NAME       : init 
*
* FUNCTION   : To construct DataUsage objects. 
*              First read the infomation from config file ,and to connect the database
*              at last to start one thread to set the mutex locker.
*
* INPUT      : confilename  std::string . The config file name is used for initial.
*
* OUTPUT     : None
*
* RETURN     : false:  If read config file infomation fail ,return false.
*                      If connet the database fail,return false..
*                      If start the thread fail return false..
*              true :  Initial operation sucess.
*
************************************************************************************/ 
bool DataUsage::init(std::string confilename)
{
    std::string username = "";
    std::string dbname = "";
    std::string host = "";
    std::string password = "";
    
    std::string errorinfo_;
    //Formate the config file contents and copy it into the memory. 
    if( !configfile_->SetConfigfile(confilename, errorinfo_) )
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: %s",errorinfo_.c_str());
        return false;
    }
    
    //Start to read the filed value about config file from memory.
    if( -1 == configfile_->get_string_value("UserName", username) )
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: Config File UserName ERROR...");
        return false;
    }

    if( -1 == configfile_->get_string_value("DataBaseName", dbname) )
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: Config File DataBaseName ERROR...");
        return false;
    }
    dbname_ = dbname;
    
    if( -1 == configfile_->get_string_value("HostIP", host) )
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: Config File HostIP ERROR...");
        return false;
    }
    
    if( -1 == configfile_->get_string_value("PassWord", password) )
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: Config File PassWord ERROR...");
        return false;
    }

    //To set major database server infomation.
    dbmysql_->setDBInfo(username, password, host, dbname);
    dbMysql_fb_.setDBInfo(username, password, host, dbname);

    if( -1 == configfile_->get_string_value("UserNameSlave", username) )
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: Config File UserName ERROR...");
        return false;
    }
    
    if( -1 == configfile_->get_string_value("DataBaseNameSlave", dbname ) )
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: Config File DataBaseName ERROR...");
        return false;
    }
    
    if( -1 == configfile_->get_string_value("HostIPSlave", host) )
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: Config File HostIP ERROR...");
        return false;
    }
    
    if (-1 == configfile_->get_string_value("PassWordSlave",password))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: Config File PassWord ERROR...");
        return false;
    }

    //.s Add by whc for filter the sync data to the center database. 20100202
    if (-1 == configfile_->get_string_value("LOCAL_AREA_CODE",localZoneCode_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: Config File ZoneCode ERROR...");
        return false;
    }
    //To get the subAreaCode
    //First add the local code that the server belong to.
    subZoneCodeVec_.push_back(localZoneCode_);
    int subAreaCodeCounter;
    if(configfile_->get_integer_value("SUB_LOCAL_AREA_CODE_COUNT",subAreaCodeCounter))
    {
        LogFileManager::getInstance()->write(Brief,"DataUsage::init: read SUB_LOCAL_AREA_CODE_COUNT failed");
    }
    else
    {
        char tmpFieldBuff[32] = "";
        for(int loopI = 0;loopI<subAreaCodeCounter;loopI++)
        {
            string areaCodeValue;
            memset(tmpFieldBuff,'\0',32);
            sprintf(tmpFieldBuff,"SUB_AREA_CODE_%d",loopI);
            if(!configfile_->get_string_value(tmpFieldBuff,areaCodeValue))
            {
                subZoneCodeVec_.push_back(areaCodeValue);
            }
        }
        if(subZoneCodeVec_.size() == 0)
        {
            LogFileManager::getInstance()->write(Brief,"DataUsage::init: There is no subZoneCode");
        }
    }
    //Construct the Zone code string
    zoneCodeStr_ = "(";
    for(int loopIndex=0;loopIndex<subZoneCodeVec_.size();loopIndex++)
    {
        if(loopIndex == subZoneCodeVec_.size()-1)
        {
            zoneCodeStr_ += subZoneCodeVec_[loopIndex]+ ")";
        }
        else
        {
            zoneCodeStr_ += subZoneCodeVec_[loopIndex]+",";
        }
    }
    LogFileManager::getInstance()->write(Debug,"Debug INFO: zoneCodeStr_%s",zoneCodeStr_.c_str());
    //.e Add by whc for filter the sync data to the center database. 20100202

    LogFileManager::getInstance()->write(Debug,"SlaveUserName: %s",username.c_str());
    LogFileManager::getInstance()->write(Debug,"SlavePassWord: %s",password.c_str());
    LogFileManager::getInstance()->write(Debug,"SlaveDBName: %s",dbname.c_str());
    LogFileManager::getInstance()->write(Debug,"SlaveHostIP: %s",host.c_str());
    
    //Set slave database infomation.
    dbmysql_->setSlaveDBInfo(username,password,host,dbname);
    dbMysql_fb_.setSlaveDBInfo(username,password,host,dbname);

    if( -1 == dbmysql_->connect())
    {
        LogFileManager::getInstance()->write(Brief,"ERROR: DataBase Connection Failed, Please Check ...");
        return false;
    }

    //To connect slave database,the database handler is used to read the forbidden number.
    if( -1 == dbMysql_fb_.connect())
    {
        LogFileManager::getInstance()->write(Brief,"ERROR: DataBase Connection Failed, Please Check ...");
        return false;
    }

    //Start the thread to create the mutex locker for every call processing.
    if (this->BeginThread(0,NULL,0) == -1)
    {
        LogFileManager::getInstance()->write(Brief,"Create Thread Failed!!!");
        return false;
    }
//.s add by whc for the IVR adding minor number 20100414
    int virtualLengthNum;
    if(configfile_->get_integer_value("VIRTUAL_NUM_LEN_COUNT",virtualLengthNum))
    {
        printf("=====%d\n",virtualLengthNum);
        LogFileManager::getInstance()->write(Brief,"DataUsage::init: read VIRTUAL_NUM_LEN_COUNT failed");
    }
    else
    {
        char tmpFieldName[32] = "";
        for(int loopI = 0;loopI<virtualLengthNum;loopI++)
        {
            int virtualLength = 0;
            memset(tmpFieldName,'\0',32);
            sprintf(tmpFieldName,"VIRTUAL_NUM_LEN_%d",loopI);
            if(!configfile_->get_integer_value(tmpFieldName,virtualLength))
            {
                virtualNumLength_.push_back(virtualLength);
            }
        }
        if(virtualNumLength_.size() == 0)
        {
            printf("=====ss%d\n",virtualNumLength_[0]);
            LogFileManager::getInstance()->write(Brief,"DataUsage::init: read VIRTUAL_NUM_LEN failed");
        }
    }
//.e add by whc for the IVR adding minor number 20100414

//.s add by whc for the sms management 20100525
    if (-1 == configfile_->get_integer_value("MaxSMSNumSend",maxSMSNumSend_))
    {
        LogFileManager::getInstance()->write(Debug,"DEBUG INFO: Config File MaxSMSNumSend fail used default...");
        maxSMSNumSend_ = 200;
    }
    if (-1 == configfile_->get_integer_value("MaxSameSMSNumToSameTarget",maxSameSMSNumToSameTarget_))
    {
        LogFileManager::getInstance()->write(Debug,"DEBUG INFO: Config File MaxSameSMSNumToSameTarget fail used default...");
        maxSameSMSNumToSameTarget_ = 5;
        
    }
    if (-1 == configfile_->get_integer_value("MaxSameSMSNumToTarget",maxSameSMSNumToTarget_))
    {
        LogFileManager::getInstance()->write(Debug,"DEBUG INFO: Config File MaxSameSMSNumToTarget fail used default...");
        maxSameSMSNumToTarget_ = 10; 
    }
    if (-1 == configfile_->get_integer_value("SendPeriodToTarget",sendPeriodToTarget_))
    {
        LogFileManager::getInstance()->write(Debug,"DEBUG INFO: Config File SendPeriodToTarget fail used default...");
        sendPeriodToTarget_ = 1; //unit by one day
    }
    if (-1 == configfile_->get_integer_value("SendPeriodToSameTarget",sendPeriodToSameTarget_))
    {
        LogFileManager::getInstance()->write(Debug,"DEBUG INFO: Config File SendPeriodToSameTarget fail used default...");
        sendPeriodToSameTarget_ = 1;//unit by one day
    }
//.e add by whc for the sms management 20100525
//.s Add the character setting. by whc 20100119    
    std::string characterEncod = "set names 'gb2312';";
    dbmysql_->executenonquery(characterEncod);
    dbMysql_fb_.executenonquery(characterEncod);
//.e Add the character setting. by whc 20100119        
    return true;
}

/************************************************************************************
*
* NAME       : LoadNumberDomain 
*
* FUNCTION   : Load Number Domain Table from the file of NumberDomain.xml. 
*              (The function is not called by other module)
*
* INPUT      : None.
*
* OUTPUT     : None
*
* RETURN     : -1:  Load Number Domain Table Failed!
*                  
*              0 :  Sucess.
*
************************************************************************************/ 
int DataUsage::LoadNumberDomain()
{
    try
    {
        TiXmlDocument *myDocument = new TiXmlDocument("./NumberDomain.xml");
        myDocument->LoadFile();
        //get root element
        TiXmlElement *RootElement = myDocument->RootElement();

        //Traversal of all sub-items
        TiXmlElement *childEle = RootElement->FirstChildElement();
        while(childEle != 0)
        {
            ROMAIN_ENTRY entry_;

            TiXmlAttribute *IDAttribute = childEle->FirstAttribute();
            std::string ID = IDAttribute->Value();

            TiXmlElement *spcEle = childEle->FirstChildElement();
            entry_.areacode = spcEle->FirstChild()->Value();  
        
            int pos = ID.find("-");
            if( pos != std::string::npos)
            {
                std::string sField = ID.substr(0, pos);
                int start = atoi(sField.c_str());
                std::string eField = ID.substr(pos+1);
                int end = atoi(eField.c_str());
                for(int i=start; i<= end; i++)
                {
                    numberDomainTable_.insert(std::make_pair(i, entry_));
                }
            }
            else  /*Only one paragraph numbers */
            {
                int paragraph = atoi(ID.c_str());
                numberDomainTable_.insert(std::make_pair(paragraph, entry_));
            }

            childEle = childEle->NextSiblingElement();
        }
    }
    catch(...)
    {
        LogFileManager::getInstance()->write(Brief, "ERROR: Load Number Domain Table Failed! ");
        return -1;
    }

    LogFileManager::getInstance()->write(Brief, 
        "INFO: Load Number Domain Table Succesful, has %d number domain", numberDomainTable_.size());
    
    return 0;
}

/************************************************************************************
*
* NAME       : getCallerAreaCode 
*
* FUNCTION   : Get the area code of the caller number.
*
* INPUT      : caller  const string &: The caller number of the callsession. 
*
* OUTPUT     : areaCode  string &: The area code of the caller number.
*
* RETURN     : E_OPERATOR_SUCCED:  To get area code of the caller number sucess!
*                  
*              E_CALLER_NOT_EXIST_IN_AREACODE:  There is no area code about the caller number.
*
************************************************************************************/ 
ResultDataUsage DataUsage::getCallerAreaCode(const string & caller, string & areaCode)
{
    int mobileLen;
    configfile_->get_integer_value("MOBILE_NUM_LEN",mobileLen);

    if(caller.length() < mobileLen)
        return E_CALLER_NOT_EXIST_IN_AREACODE ;
    
    std::string section(caller.c_str(), 7);
    unsigned int id = atoi(section.c_str());
    NUMBER_DOMAIN_TABLE::iterator iter = numberDomainTable_.find(id);
    if(iter != numberDomainTable_.end())
    {
        ROMAIN_ENTRY & entry = iter->second;
        areaCode = entry.areacode ;
        return E_OPERATOR_SUCCED ;
    }
    
    return E_CALLER_NOT_EXIST_IN_AREACODE;
}

/***********************************************************************************
*
* NAME       : getAllMinorNumber 
*
* FUNCTION   :  Get all the minor phone numbers of the major number input. 
*               First to check the major phone number has been registered the service.
*               If it registered the service ,query the relation records about it's minor 
*               numbers from database. 
*
* INPUT      : majorNumber  const string & : The caller's major number .
*
* OUTPUT     : minorNumbers  vector<string> &: The minor numbers about the major number.
*
* RETURN     : E_MAJORNUMBER_NOT_EXESIT:The major number has not been registered the sercice!          
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_OPERATOR_SUCCED :  Sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::getAllMinorNumber(const string & majorNumber, vector<string> & minorNumbers)
{
    //Check the major phone number has been registered the service.
    std::string userID;
    if (this->getUserID(majorNumber,userID) == E_MAJORNUMBER_NOT_EXESIT)
    {
        return E_MAJORNUMBER_NOT_EXESIT;
    }
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "select MinorNumber from T_MinorNumber where UserID = '";
    sql += userID;
    sql += "'order by SequenceNo ASC";
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;
    
    //If no minor phone number.
    if (dbrecord_.empty())
    {
        return E_OPERATOR_SUCCED; 
    }
    //Filter the minor numbers's infomation about the major number.
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.begin();
        while(it_!=item_.end())
        {
            string minorvalue_ = (*it_).second;
            minorNumbers.push_back(minorvalue_);
            ++it_;
        }
        ++it;
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : getAllMinorNumber 
*
* FUNCTION   :  Get all the minor phone numbers of the major number input. 
*               First to check the major phone number has been registered the service.
*               If it registered the service ,query the relation records about it's minor 
*               numbers from database. 
*
* INPUT      : majorNumber const string & : The caller's major number .
*
* OUTPUT     : minorNumbers vector<MinorNumberAttr> &: The minor numbers's attribute about the major number.
*
* RETURN     : E_MAJORNUMBER_NOT_EXESIT:The major number has not been registered the sercice!          
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_OPERATOR_SUCCED :  Sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::getAllMinorNumber(const string & majorNumber, vector<MinorNumberAttr> & minorNumbers)
{
	int isVirtualMinor = 0;
    minorNumbers.clear();

    //Check the major phone number has been registered the service.
    std::string userID;
    if (this->getUserID(majorNumber,userID) == E_MAJORNUMBER_NOT_EXESIT)
    {
        return E_MAJORNUMBER_NOT_EXESIT;
    }
    //Construct the sql query content.
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "select MinorNumber, SequenceNo , StateID from T_MinorNumber where UserID = '";
    sql += userID;
    sql += "'order by SequenceNo ASC";
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;
    
    //No minor number about the major number.
    if (dbrecord_.empty())
    {
        return E_OPERATOR_SUCCED;			
    }

    //Filter the minor numbers's infomation about the major number.
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        std::string minorval = "";
        std::string sequenceval = "";
        std::string stateidval = "";
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("minornumber");
        if(it_ != item_.end())
            minorval = (*it_).second;
        
        it_ = item_.find("sequenceno");
        if(it_ != item_.end())
            sequenceval = (*it_).second;

        it_ = item_.find("stateid");
        if(it_ != item_.end())
            stateidval = (*it_).second;
        
        //Contruct the minor number attribute struct.
        MinorNumberAttr nbrInfo ;
        memset(&nbrInfo, 0, sizeof(nbrInfo));
        strncpy(nbrInfo.minorNumber, minorval.c_str(), sizeof(nbrInfo.minorNumber)-1);
        nbrInfo.sequenceNo = atoi(sequenceval.c_str());
        nbrInfo.state = atoi(stateidval.c_str());

        minorNumbers.push_back(nbrInfo);
		
#if 0      
		// .s added by wj 20121022
		if(sequenceval.compare("6") == 0)
		{
			isVirtualMinor = 1;
		}
		// .e added by wj 20121022
#endif

        ++it;
    }

#if 0
	// .s added by wj 20121022
	if(isVirtualMinor == 1)
	{
		return E_OPERATOR_SUCCED_VIRTUAL_MINOR;
	}
	// .e added by wj 20121022
#endif

    return E_OPERATOR_SUCCED;
}


ResultDataUsage DataUsage::getAllBacMinorNumber(const string & majorNumber, vector<BakMinorNumberInfo> & minorNumbers)
{
	minorNumbers.clear();

	//主号码是否注册
	std::string userID;
	if (getBacUserID(majorNumber,userID) == E_MAJORNUMBER_NOT_EXESIT)
	{
		return E_MAJORNUMBER_NOT_EXESIT;
	}
	DB_RECORDSET_ARRAY dbrecord_;
	std::string sql = "select MinorNumber,SequenceNo,Type,RegisterTime,BakTime,OperateChannelID from t_minornumber_bak where UserID = '";
	sql += userID;
	sql += "'order by SequenceNo ASC";
	dbmysql_->executequery(sql,dbrecord_);
	
	//没有副号码
	if (dbrecord_.empty())
	{
		return E_OPERATOR_SUCCED; 
	}
    //数据导入参数中
	DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
	while (it!=dbrecord_.end())
	{
		BakMinorNumberInfo nbrInfo;

		DB_RECORDSET_ITEM item_ = (*it);
		DB_RECORDSET_ITEM::iterator it_ = item_.find("minornumber");
		if(it_ != item_.end())
			nbrInfo.minorNumber = (*it_).second;
		
		it_ = item_.find("sequenceno");
		if(it_ != item_.end())
			nbrInfo.sequenceNo = (*it_).second;

		it_ = item_.find("type");
		if(it_ != item_.end())
			nbrInfo.type = (*it_).second;

		it_ = item_.find("registertime");
		if(it_ != item_.end())
			nbrInfo.regTime = (*it_).second;

		it_ = item_.find("baktime");
		if(it_ != item_.end())
			nbrInfo.logoutTime = (*it_).second;

		it_ = item_.find("operatechannelid");
		if(it_ != item_.end())
			nbrInfo.logoutChannel = (*it_).second;

		minorNumbers.push_back(nbrInfo);
		
		++it;
	}
	return E_OPERATOR_SUCCED;
}
/***********************************************************************************
*
* NAME       : getMinorNumber 
*
* FUNCTION   : Get the minor phone numbers according with the index and the major number. 
*              First to check the major phone number has been registered the service.
*              If it registered the service ,query the relation records about it's minor 
*              numbers from database. 
*
* INPUT      : majorNumber const string & : The caller's major number .
*              index int : The index is the sequence number of the minor number in the database.
*
* OUTPUT     : minorNumber string& : The minor number get from the database.
*
* RETURN     : E_MAJORNUMBER_NOT_EXESIT:The major number has not been registered the sercice!          
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_MINORINDEX_NUMBER_NOT_EXESIT: The major doesn't hava the minor number!  
*              E_OPERATOR_SUCCED :  Sucess.
*              
***********************************************************************************/ 
ResultDataUsage DataUsage::getMinorNumber(const string& majorNumber, int index,string& minorNumber)
{
    //Check the major phone number has been registered the service.
    std::string userID;
    if (this->getUserID(majorNumber,userID) == E_MAJORNUMBER_NOT_EXESIT)
    {
        return E_MAJORNUMBER_NOT_EXESIT;
    }

    //Get the minor phone number has been registered the service.
    char buff[30];
    memset(buff,0,sizeof(buff));
    sprintf(buff,"%d",index);
    std::string phoneorder_ = buff;
    std::string sql = "select MinorNumber,SequenceNo from T_MinorNumber where UserID = '";
    sql += userID;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;

    //Filter the infomation of the query result.
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        std::string key_ = "sequenceno";
        DB_RECORDSET_ITEM::iterator it_ = item_.find(key_);
        if (phoneorder_ == (*it_).second)
        {
            //Th minor number is exist.
            DB_RECORDSET_ITEM::iterator itmino_ = item_.find("minornumber");
            minorNumber = (*itmino_).second;
            return E_OPERATOR_SUCCED;
        }
        ++it;
    }
    return E_MINORINDEX_NUMBER_NOT_EXESIT; 
}

/***********************************************************************************
*
* NAME       : getMinorIndex 
*
* FUNCTION   : Get the major phone numbers and index according with the minor number. 
*              First to check the major phone number has been registered the service.
*              If it registered the service ,query the relation records about it's minor 
*              numbers from database. 
*
* INPUT      : majorNumber const string & : The caller's major number .
*
* OUTPUT     : minorNumbers vector<MinorNumberAttr> &: 
*                           The minor numbers's attribute about the major number.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT:The minor number is no record int the database!          
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_MAJORNUMBER_NOT_EXESIT : The major number which is used to register is not exist.
*              E_OPERATOR_SUCCED :  Sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::getMinorIndex(const string & minorNumber, string & majorNumber,int& index)
{
    //Check the minor number whether exist in the database.
    std::string sql = "select UserID,SequenceNo from T_MinorNumber where MinorNumber = '";
    sql += minorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;
    if (dbrecord_.empty())
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    int minorNumberIndex;
    std::string userID;
    //Execute the query content.
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    //CHeck and get the minor number index in the database record.
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        std::string phoneorder_ = (*(item_.find("sequenceno"))).second;
        minorNumberIndex = atoi(phoneorder_.c_str());
        userID = (*(item_.find("userid"))).second;
        ++it;
    }
    
    //Contruct the query content to get the major number of the current minor number.
    sql.clear();
    sql = "select UserNumber from T_RegisterUser where UserID = '";
    sql += userID;
    sql += "'";
    dbrecord_.clear();
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;
    if (dbrecord_.empty())
    {
        return E_MAJORNUMBER_NOT_EXESIT;
    }

    it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        majorNumber = (*(item_.find("usernumber"))).second;
        ++it;
    }
    index = minorNumberIndex;
    return E_OPERATOR_SUCCED;
}

ResultDataUsage DataUsage::getMinorRegInfo(const string& minorNumber, string& type, string& registerChannel, string& registerTime)
{
	std::string sql = "select Type,RegisterTime,OperateChannelID from T_MinorNumber where MinorNumber = '";
	sql += minorNumber;
	sql += "'";
	DB_RECORDSET_ARRAY dbrecord_;
	dbmysql_->executequery(sql,dbrecord_);
	if (dbrecord_.empty())
	{
		return E_MINORNUMBER_NOT_EXESIT;
	}
	int minorNumberIndex;
	std::string userID;
	//数据导入参数中
	DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
	while (it!=dbrecord_.end())
	{
		DB_RECORDSET_ITEM item_ = (*it);
		type = (*(item_.find("type"))).second;
        registerTime = (*(item_.find("registertime"))).second;
		registerChannel = (*(item_.find("operatechannelid"))).second;
		++it;
	}
	
	return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : isMajorRegistered 
*
* FUNCTION   : To check the input major number whether has been registered the service.
*
* INPUT      : majorNumber const string & : The caller's major number .
*
* OUTPUT     : none .
*
* RETURN     : E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_MAJORNUMBER_NOT_EXESIT : The major number which is used to register is not exist.
*              E_OPERATOR_SUCCED :  Sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::isMajorRegistered (const string & majorNumber)
{
    std::string sql = "select UserID from T_RegisterUser where UserNumber = '";
    sql += majorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    //Query the database fail.
    if(0 != dbmysql_->executequery(sql,dbrecord_))
	{
		return E_DATABASE_ERROR;
	}
    
    if (!dbrecord_.empty())
    {
        return E_OPERATOR_SUCCED;
    }

    return E_MAJORNUMBER_NOT_EXESIT;
}

// .s added by wj 20120927
/***********************************************************************************
*
* NAME       : isTrueMinorRegistered 
*
* FUNCTION   : To check the input major number whether has been registered the true minor service.
*
* INPUT      : majorNumber const string & : The caller's major number .
*
* OUTPUT     : none .
*
* RETURN     : false :  The majorNumber is not registered the virtual minor service.
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_VIRTUAL_MINOR_NOT_EXESIT : The major number which is used to register with virtual minor number.
*              E_OPERATOR_SUCCED :  The majorNumber is registered the virtual minor service..
*
***********************************************************************************/ 
ResultDataUsage DataUsage::isTrueMinorRegistered (const string & majorNumber)
{
	string minorNumber;
    int minorNumberIndex = 0;
	int isTrueMinorRegistered = 0;

    std::string sql = "select MinorNumber, SequenceNo from T_RegisterUser r,T_MinorNumber m where r.UserID=m.UserID AND UserNumber = '";
    sql += majorNumber;
    sql += "'";

    DB_RECORDSET_ARRAY dbrecord_;
    //Query the database fail.
    if( 0 != dbmysql_->executequery(sql,dbrecord_) )
	{
		return E_DATABASE_ERROR;
	}
    
    if( dbrecord_.empty() )
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    //To get the virtual mionr number according the major number
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it != dbrecord_.end())
    {        
        DB_RECORDSET_ITEM::iterator it_mn_ = (*it).find("minornumber");
        if (it_mn_ == (*it).end())
        {
            return E_VIRTUAL_MINOR_NOT_EXESIT;
        }

        minorNumber = (*it_mn_).second;

		DB_RECORDSET_ITEM::iterator it_sn_ = (*it).find("sequenceno");
        if (it_sn_ == (*it).end())
        {
            return E_VIRTUAL_MINOR_NOT_EXESIT;
        }

		std:string phoneorder_ = (*it_sn_).second;
		minorNumberIndex = atoi(phoneorder_.c_str());         
       
		if( minorNumberIndex != 6 )
		{
			isTrueMinorRegistered = 1;
		}

        ++it;
    }

	if( isTrueMinorRegistered == 1)
	{
		return E_OPERATOR_SUCCED;
	}

    return E_MINORNUMBER_NOT_EXESIT;
}

/***********************************************************************************
*
* NAME       : isVirtualMinorRegistered 
*
* FUNCTION   : To check the input major number whether has been registered the virtual minor service.
*
* INPUT      : majorNumber const string & : The caller's major number .
*
* OUTPUT     : none .
*
* RETURN     : false :  The majorNumber is not registered the virtual minor service.
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_VIRTUAL_MINOR_NOT_EXESIT : The major number which is used to register with virtual minor number.
*              E_OPERATOR_SUCCED :  The majorNumber is registered the virtual minor service..
*
***********************************************************************************/ 
ResultDataUsage DataUsage::isVirtualMinorRegistered (const string & majorNumber)
{
	string minorNumber;
    int minorNumberIndex = 0;
	int isVirtualMinorRegistered = 0;

    std::string sql = "select MinorNumber, SequenceNo from T_RegisterUser r,T_MinorNumber m where r.UserID=m.UserID AND UserNumber = '";
    sql += majorNumber;
    sql += "'";

    DB_RECORDSET_ARRAY dbrecord_;
    //Query the database fail.
    if( 0 != dbmysql_->executequery(sql,dbrecord_) )
	{
		return E_DATABASE_ERROR;
	}
    
    if( dbrecord_.empty() )
    {
        return E_VIRTUAL_MINOR_NOT_EXESIT;
    }

    //To get the virtual mionr number according the major number
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {        
        DB_RECORDSET_ITEM::iterator it_mn_ = (*it).find("minornumber");
        if (it_mn_ == (*it).end())
        {
            return E_VIRTUAL_MINOR_NOT_EXESIT;
        }

        minorNumber = (*it_mn_).second;

		DB_RECORDSET_ITEM::iterator it_sn_ = (*it).find("sequenceno");
        if (it_sn_ == (*it).end())
        {
            return E_VIRTUAL_MINOR_NOT_EXESIT;
        }

		std:string phoneorder_ = (*it_sn_).second;
		minorNumberIndex = atoi(phoneorder_.c_str());         
       
		if( minorNumberIndex == 6 )
		{
			isVirtualMinorRegistered = 1;
		}

        ++it;
    }

	if( isVirtualMinorRegistered == 1)
	{
		return E_OPERATOR_SUCCED;
	}

    return E_VIRTUAL_MINOR_NOT_EXESIT;
}
// .e added by wj 20120927


/***********************************************************************************
*
* NAME       : getDefaultMinorIndex 
*
* FUNCTION   : Get the default minor number index according with the majorNumber number. 
*
* INPUT      : majorNumber const string & : The caller's major number .
*
* OUTPUT     : none .
*
* RETURN     : E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_MAJORNUMBER_NOT_EXESIT : The major number which is used to register is not exist.
*              1 :  The default index of minor number is 1..
*
***********************************************************************************/ 
int DataUsage::getDefaultMinorIndex(const string & majorNumber)
{
    std::string sql ="select MinorNumber,SequenceNo from T_MinorNumber where UserID = (";
    sql += "select UserID from T_RegisterUser where UserNumber = '";
    sql += majorNumber;
    sql += "'";
    sql += ")";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;

    if (dbrecord_.empty())
    {
        return E_MAJORNUMBER_NOT_EXESIT; 
    }
    //The default index of minor number is 1
    return 1;  
}

/***********************************************************************************
*
* NAME       : getDefaultMinorIndex 
*
* FUNCTION   : Get the default minor phone number according with the majorNumber number. 
*
* INPUT      : majorNumber const string & : The caller's major number .
*
* OUTPUT     : minorNumber string & : The default minor phone number of the major phone number .
*
* RETURN     : E_DATABASE_ERROR         :  The operation of querying the database fail.
*              E_MAJORNUMBER_NOT_EXESIT : The major number which is used to register is not exist.
*              1 :  The default index of minor number is 1..
*
***********************************************************************************/ 
ResultDataUsage DataUsage::getDefaultMinorNumber(const string & majorNumber, string & minorNumber)
{
    //To check the major whether has been registered.If register the service to get the relation infomation.
    std::string sql = "select MinorNumber from T_MinorNumber where SequenceNo = '1' and UserID = (";
    sql += "select UserID from T_RegisterUser where UserNumber = '";
    sql += majorNumber;
    sql += "'";
    sql += ")";

    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;

    //The major number is no registered.
    if (dbrecord_.empty())
    {
        return E_MAJORNUMBER_NOT_EXESIT; 
    }

    //To get the default mionr number according the major number and default index.(1)
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        
        DB_RECORDSET_ITEM::iterator itorder_ = (*it).find("minornumber");
        if (itorder_ == (*it).end())
        {
            return E_UNKNOWN_ERROR;
        }
        minorNumber = (*itorder_).second;
        return E_OPERATOR_SUCCED;
        ++it;
    }

    return E_MAJORNUMBER_NOT_EXESIT;
}

/***********************************************************************************
*
* NAME       : getMinorNumberState 
*
* FUNCTION   : Get the state of the minor phone number. 
*
* INPUT      : minorNumber const string & : The minor number .
*
* OUTPUT     : None.
*
* RETURN     : STATE_ACTIVE   : The minor number is active
*              STATE_OFFLINE  : The minor number is logic shut down.
*              STATE_ARREARAGE: The number is arrearage.
*              STATE_LOGOUT   : The number is log out the service. 
*
***********************************************************************************/ 
ResultDataUsage DataUsage::getMinorNumberState(const string& minorNumber)
{
    //To check the minor number whether is exist in the database.
    std::string sql = "select StateID from T_MinorNumber where MinorNumber = '";
    sql += minorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
	{
		return E_DATABASE_ERROR;
	}

    //The minor number input is not exist.
    if (dbrecord_.empty())
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    int i;
    //Get the current state of the minor number.
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("stateid");
        if (it_ != item_.end())
        {
            std::string minorNumberState = (*it_).second;
            i = atoi(minorNumberState.c_str());
        }
        
        ++it;
    }
    //Filter the state of the minor number.
    if (LOGIC_ACTIVE == i)
    {
        return STATE_ACTIVE;
    }
    if (LOGIC_OFFLINE == i)
    {
        return STATE_OFFLINE;
    }
    if (LOGIC_LOGOUT == i)
    {
        return STATE_LOGOUT;
    }
    if (LOGIC_ARREARAGE == i)
    {
        return STATE_ARREARAGE;
    }// set MinorNumber is STATE_ACTIVE then Return
    else
    {
        char stateActivebuff[10];
        memset(stateActivebuff,0,sizeof(stateActivebuff));
        sprintf(stateActivebuff,"%d",LOGIC_ACTIVE);
        sql.clear();
        sql = "update T_MinorNumber set IsSynchronization = 0, StateID = '";
        sql += stateActivebuff;
        sql += "' where MinorNumber = '";
        sql += minorNumber;
        sql += "'";
        if( 0 != dbmysql_->executenonquery(sql))
		{
			return E_DATABASE_ERROR ;
		}
    }
    return STATE_ACTIVE;
}

/***********************************************************************************
*
* NAME       : setMinorNumberState 
*
* FUNCTION   : Set the state of the minor phone number according with the number state input.. 
*
* INPUT      : minorNumber const string & : The minor number .
*              minorNumberState  ResultDataUsage: The state used for update current state 
*                                of the minor number.
*
* OUTPUT     : None.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT:    The minor number is not exist.
*              E_DATABASE_ERROR:   The query operation is fail.
*              E_OPERATOR_SUCCED:  Sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::setMinorNumberState(const string& minorNumber,ResultDataUsage minorNumberState,OperateChannel inChanelID)
{
    //To check the minor whether is exist.
    std::string sql = "select NumberID,StateID from T_MinorNumber where MinorNumber = '";
    sql += minorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;

    if (dbrecord_.empty())
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    ResultDataUsage result = E_OPERATOR_SUCCED;
    std::string numberID;
    std::string operatorNote ;
    short int opeResult = 0;

    int numberState;
    switch(minorNumberState)
    {
    case STATE_ACTIVE:
        {
            operatorNote = "Set minumber active";
            numberState = LOGIC_ACTIVE;
        }
        break;
    case STATE_OFFLINE:
        {
            operatorNote = "Set minumber offline";
            numberState = LOGIC_OFFLINE;
        }
        break;
    case STATE_LOGOUT:
        {
            numberState = LOGIC_LOGOUT;
        }
        break;
    case STATE_ARREARAGE:
        {
            numberState = LOGIC_ARREARAGE;
        }
        break;
    }

    //Set the state of the minor number.
    char buff[10];
    memset(buff,0,sizeof(buff));
    sprintf(buff,"%d",numberState);
    sql.clear();
    //.s modify  by whc for sync the database when there is change. 2010-02-20
    //sql = "update T_MinorNumber set StateID = '";
    sql = "update T_MinorNumber set IsSynchronization = 0, StateID = '";
    //.e modify  by whc for sync the database when there is change. 2010-02-20
    sql += buff;
    sql += "' where MinorNumber = '";
    sql += minorNumber;
    sql += "'";

    if( 0 != dbmysql_->executenonquery(sql))
    {
        result = E_DATABASE_ERROR;
        opeResult = -1;
    }
    else
    {
        result = E_OPERATOR_SUCCED;
        opeResult = 0;
    }

    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("numberid");
        if (it_ != item_.end())
        {
            numberID = (*it_).second;
        }

        ++it;
    }

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    //user operation collect
    if(minorNumberState == STATE_ACTIVE)
    {
        this->setMinorNumberOpeRecord(numberID, ACTIVE_SET,tmpChanelID,operatorNote,opeResult);
    }
    else if(minorNumberState == STATE_OFFLINE)
    {
        this->setMinorNumberOpeRecord(numberID, OFFLINE_SET,tmpChanelID,operatorNote,opeResult);
    }

    return result;
}

/***********************************************************************************
*
* NAME       : isMinorNumberRegistered 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : To check the minor number whether has already registered. 
*
* INPUT      : minorNumber const string & : The minor number .
*
* OUTPUT     : None.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT:    The minor number is not exist.
*              E_DATABASE_ERROR:   The query operation is fail.
*              E_OPERATOR_SUCCED:  Sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::isMinorNumberRegistered(const string& minorNumber)
{
    //Query the database and check the mimor number whether exist.
    std::string sql = "select StateID from T_MinorNumber where MinorNumber = '";
    sql += minorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;

    //The minor number is not exist.
    if (dbrecord_.empty())
    {
        return E_MINORNUMBER_NOT_EXESIT; 
    }

    //To filter these state,if the state is LOGIC_ARREARAGE ,the minor number is no registered.
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("stateid");
        if (it_ != item_.end())
        {
            std::string minorNumberState = (*it_).second;
            int ret = atoi(minorNumberState.c_str());
            if (ret != LOGIC_ARREARAGE)
            {
                return E_OPERATOR_SUCCED;
            }
        }

        ++it;
    }
    return E_MINORNUMBER_NOT_EXESIT;

}
/***********************************************************************************
*
* NAME       : isInLimitedTime 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : Check the minor number called by caller is whether in the limited time policy. 
*              First to check minor number whether is exist.
*              If the  number is exist then to get the limited time policy from database,and 
*              get the current time.
*              At last to compare the policy and the current time.
*              If the time match with the policy return E_IN_LIMITED_TIME_ERROR else return 
*              E_OPERATOR_SUCCED
*
* INPUT      : minorNumber const string & : The minor number .
*
* OUTPUT     : None.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT:    The minor number is not exist.
*              E_DATABASE_ERROR  :   The query operation is fail.
*              E_OPERATOR_SUCCED:    Sucess.
*              E_IN_LIMITED_TIME_ERROR :The number is limited called in the time period.     
*
***********************************************************************************/ 
ResultDataUsage DataUsage::isInLimitedTime(const string& minorNumber)
{
    TimeLimitedStrategy timeStrategy;
    //To check the minor number and get it's time policy .
    ResultDataUsage ret = getStrategyID(minorNumber,timeStrategy,IVR_LIMITED);
    if (ret != E_OPERATOR_SUCCED)
    {
        return E_OPERATOR_SUCCED;
    }

    switch(timeStrategy)
    {
    case TIMELIMITSTRATEGY_SELF:
        {
#if 0		// old
   std::string numberID;
            if(getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
            {
                return E_MINORNUMBER_NOT_EXESIT;
            }
            //To get the policy keep in the database about the pointed minor number.
            //Modify the StrategyID  for IVR and SMS different by whc 2010-0312.
            std::string sql = "select StartTime,EndTime,Week from T_RuleTime where StrategyID = 0 and NumberID = '";
            sql += numberID;
            sql += "'";
            DB_RECORDSET_ARRAY dbrecord_;
            if(0 != dbmysql_->executequery(sql,dbrecord_))
                return E_DATABASE_ERROR;
            if (dbrecord_.empty())
            {
                return E_OPERATOR_SUCCED;
            }

            int week;
            std::string startTime;
            std::string endTime;
            DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
            while (it!=dbrecord_.end())
            {
                DB_RECORDSET_ITEM item_ = (*it);
                DB_RECORDSET_ITEM::iterator it_ = item_.find("starttime");
                if (it_ != item_.end())
                {
                    startTime = (*it_).second;
                }
                it_ = item_.find("endtime");
                if (it_ != item_.end())
                {
                    endTime = (*it_).second;
                }
                it_ = item_.find("week");
                if (it_ != item_.end())
                {
                    std::string ret = (*it_).second;
                    week = atoi(ret.c_str());
                }

                ++it;
            }
            //No limit week day
            if (week == 0) 
            {
                //To get the curren time.
                time_t nowTime = time(NULL);
                std::string nowTimeStr = LonglongtoStr(nowTime);
                //Filter the time string. %Y-%m-%d %H:%M:%S
                string startTimeStr, endTimeStr;
                int pos = startTime.find_first_of(" ");
                startTimeStr = startTime;
                if(pos != string::npos)
                    startTimeStr = startTime.substr(pos+1);

                pos = endTime.find_first_of(" ");
                endTimeStr = endTime;
                if(pos != string::npos)
                    endTimeStr = endTime.substr(pos+1);
                
                std::string substring  = nowTimeStr;
                pos = nowTimeStr.find_first_of(" ");
                if(pos != string::npos)
                    substring = nowTimeStr.substr(pos+1);

                char logText[1024];
                memset(logText, 0 , sizeof(logText));
                sprintf(logText, "substring = %s  startTimeStr = %s endTimeStr = %s", 
                    substring.c_str(), startTimeStr.c_str(), endTimeStr.c_str());
                LogFileManager::getInstance()->write(Brief,"INFO: %s", logText);
                //To check the callsession time is in the limited policy.
                if (substring > startTimeStr && substring < endTimeStr)
                {
                    return E_IN_LIMITED_TIME_ERROR;
                }
                return E_OPERATOR_SUCCED;
            }
            else //limit week day
            {
                //get the current week.
                time_t nowTime = time(NULL);
                tm *nowTimeStruct = localtime(&nowTime);
                if ((nowTimeStruct->tm_wday + 1) == week)
                {
                    return E_IN_LIMITED_TIME_ERROR;
                }
                else
                {
                    std::string nowTimeStr = LonglongtoStr(nowTime);
                    if (nowTimeStr > startTime && nowTimeStr < endTime)
                    {
                        return E_IN_LIMITED_TIME_ERROR;
                    }
                }
            }

#endif
			// new 
			// .s modified by wangjing 20120712
		    std::string sql = "select * from T_RuleTime T_R, T_MinorNumber T_M \
										where ( \
												  (     HOUR(T_R.StartTime) <= HOUR(now()) \
												    and HOUR(T_R.EndTime) > HOUR(now()) \
												    and HOUR(T_R.StartTime) < HOUR(T_R.EndTime) \
											      ) \
												  or  ( \
														  (    (HOUR(T_R.StartTime) <= HOUR(now()) and HOUR(now()) < 24 ) \
															or (HOUR(T_R.EndTime) > HOUR(now()) and HOUR(now()) > 0 ) \
														  ) \
													        and HOUR(T_R.StartTime) > HOUR(T_R.EndTime) \
												      ) \
											  ) \
											  and  T_R.NumberID = T_M.NumberID and StrategyID = 0 and T_M.MinorNumber = '";
			sql += minorNumber;
			sql += "'";

			DB_RECORDSET_ARRAY dbrecord_;

            if(0 != dbmysql_->executequery(sql,dbrecord_))
                return E_DATABASE_ERROR;
            
            if (dbrecord_.empty())
            {
                return E_OPERATOR_SUCCED;
            }
            else                
			{
				return E_IN_LIMITED_TIME_ERROR;
			}
			// .e modified by wangjing 20120712

        }
        return E_OPERATOR_SUCCED;
    case TIMELIMITSTRATEGY_ONE:
        return E_OPERATOR_SUCCED;
    case TIMELIMITSTRATEGY_TWO:
        {
            //Get current time.
            time_t nowTime = time(NULL);
            std::string nowTimeStr = LonglongtoStr(nowTime);
            //To cut the year and month of the current time. 
            int i = nowTimeStr.find_first_of(" ");
            std::string substring = nowTimeStr.substr(i+1);
            if (substring < "08:00:00" || substring > "21:00:00")
            {
                return E_IN_LIMITED_TIME_ERROR;
            }
        }
        return E_OPERATOR_SUCCED;
    case TIMELIMITSTRATEGY_THERE:
        {
            //The policy is from Mondy to Friday limited
            //To get current time and filter the week day.
            time_t nowTime = time(NULL);
            tm *nowTimeStruct = localtime(&nowTime);
            if (nowTimeStruct->tm_wday == 0 || nowTimeStruct->tm_wday == 6)
            {
                return E_IN_LIMITED_TIME_ERROR;
            }        
        }
        return E_OPERATOR_SUCCED;
    case TIMELIMITSTRATEGY_FOUR:
        {
            //The policy is limit special day time from Monday to Friday.
            //To get current time and filter the week day.
            time_t nowTime = time(NULL);
            tm *nowTimeStruct = localtime(&nowTime);
            if (nowTimeStruct->tm_wday == 0 || nowTimeStruct->tm_wday == 6)
            {
                return E_IN_LIMITED_TIME_ERROR;
            }
            std::string nowTimeStr = LonglongtoStr(nowTime);
            //cut the year and the month.
            int i = nowTimeStr.find_first_of(" ");
            std::string substring = nowTimeStr.substr(i+1);
            if (substring < "08:00:00" || substring > "21:00:00")
            {
                return E_IN_LIMITED_TIME_ERROR;
            }
        }
        return E_OPERATOR_SUCCED;
    case TIMELIMITSTRATEGY_FIVE:
        {
            time_t nowTime = time(NULL);
            std::string nowTimeStr = LonglongtoStr(nowTime);
            //Filter the month and year string
            int i = nowTimeStr.find_first_of(" ");
            std::string substring = nowTimeStr.substr(i+1);
            //To compare the time policy.
            if ((substring > "12:00:00" && substring < "14:00:00") || (substring > "18:00:00" && substring < "21:00:00"))
            {
                return E_OPERATOR_SUCCED;
            }
        }
        return E_IN_LIMITED_TIME_ERROR;
    case TIMELIMITSTRATEGY_SIX:
        {
            //This branch is only restricted to Saturdays and Sundays
            //To get the current time.
            time_t nowTime = time(NULL);
            tm *nowTimeStruct = localtime(&nowTime);
            if (nowTimeStruct->tm_wday == 0 || nowTimeStruct->tm_wday == 6)
            {
                return E_IN_LIMITED_TIME_ERROR;
            }
            std::string nowTimeStr = LonglongtoStr(nowTime);
            //去掉年月日
            int i = nowTimeStr.find_first_of(" ");
            std::string substring = nowTimeStr.substr(i+1);
            if ((substring > "12:00:00" && substring < "14:00:00") || (substring > "18:00:00" && substring < "21:00:00"))
            {
                return E_OPERATOR_SUCCED;
            }
        }
        return E_IN_LIMITED_TIME_ERROR;
    case TIMELIMITSTRATEGY_SEVEN:
        {
            time_t nowTime = time(NULL);
            tm *nowTimeStruct = localtime(&nowTime);
            if (nowTimeStruct->tm_wday == 0 || nowTimeStruct->tm_wday == 6)
            {
                return E_OPERATOR_SUCCED;
            }
        }
        return E_IN_LIMITED_TIME_ERROR;
    case TIMELIMITSTRATEGY_EIGHT:
        {
            time_t nowTime = time(NULL);
            tm *nowTimeStruct = localtime(&nowTime);
            if (nowTimeStruct->tm_wday == 0 || nowTimeStruct->tm_wday == 6)
            {
                std::string nowTimeStr = LonglongtoStr(nowTime);
                int i = nowTimeStr.find_first_of(" ");
                std::string substring = nowTimeStr.substr(i+1);
                if ((substring > "12:00:00" && substring < "14:00:00") || (substring > "18:00:00" && substring < "21:00:00"))
                {
                    return E_OPERATOR_SUCCED;
                }
            }
        }
        return E_IN_LIMITED_TIME_ERROR;
    case TIMELIMITSTRATEGY_NINE:
        {
            time_t nowTime = time(NULL);
            tm *nowTimeStruct = localtime(&nowTime);
            if (nowTimeStruct->tm_wday == 0 || nowTimeStruct->tm_wday == 6)
            {
                std::string nowTimeStr = LonglongtoStr(nowTime);
                int i = nowTimeStr.find_first_of(" ");
                std::string substring = nowTimeStr.substr(i+1);
                if (substring > "09:00:00")
                {
                    return E_OPERATOR_SUCCED;
                }
            }
        }
        return E_IN_LIMITED_TIME_ERROR;
    default:
        return E_OPERATOR_SUCCED;
    }
}

/***********************************************************************************
*
* NAME       : isSMSInLimitedTime 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to check the limit for short message receiving.
*              First to get the time  policy about the text message receiving.Then 
*              compare the policy and current time.
*              
* INPUT      : minorNumber  const string& :The called user 's minor number.
*
* OUTPUT     : None.
*
* RETURN     : E_DATABASE_ERROR :The database opeartion is failed;
*              E_MINORNUMBER_NOT_EXESIT:The minior number is not exist.
*              E_IN_LIMITEIED_LIST_ERROR: The caller is limited by the called using the white or black list.
*              E_OPERATOR_SUCCED: The call is no limit.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::isSMSInLimitedTime(const string& minorNumber)
{
    TimeLimitedStrategy timeStrategy;
    ResultDataUsage ret = this->getStrategyID(minorNumber,timeStrategy,SMS_LIMITED);
    if (ret != E_OPERATOR_SUCCED)
        return E_OPERATOR_SUCCED;

    LogFileManager::getInstance()->write(Brief,"INFO: Time Strategyid =%d", timeStrategy);
    switch(timeStrategy)
    {
    case TIMELIMITSTRATEGY_101://24 hours not to receive text messages
        return E_MINORNUMBER_SMS_LIMITED;
    case TIMELIMITSTRATEGY_102://Self define time Period
        {
		    std::string sql = "select * from T_RuleTime T_R, T_MinorNumber T_M \
										where ( \
												  (     HOUR(T_R.StartTime) <= HOUR(now()) \
												    and HOUR(T_R.EndTime) > HOUR(now()) \
												    and HOUR(T_R.StartTime) < HOUR(T_R.EndTime) \
											      ) \
												  or  ( \
														  (    (HOUR(T_R.StartTime) <= HOUR(now()) and HOUR(now()) < 24 ) \
															or (HOUR(T_R.EndTime) > HOUR(now()) and HOUR(now()) > 0 ) \
														  ) \
													        and HOUR(T_R.StartTime) > HOUR(T_R.EndTime) \
												      ) \
											  ) \
											  and  T_R.NumberID = T_M.NumberID and StrategyID = 102 and T_M.MinorNumber = '";
            sql += minorNumber;
            sql += "'";
            DB_RECORDSET_ARRAY dbrecord_;
            if(0 != dbmysql_->executequery(sql,dbrecord_))
                return E_DATABASE_ERROR;
            
            if (dbrecord_.empty())
            {
                return E_OPERATOR_SUCCED;
            }
            else
                return E_MINORNUMBER_SMS_LIMITED;
        }
        break ;
    case TIMELIMITSTRATEGY_103: //All the time to receive text messages 
        return E_OPERATOR_SUCCED;
    
    default:
        return E_OPERATOR_SUCCED;
    }
}


ResultDataUsage DataUsage::getBlackList(const string& minorNumber, vector<string>& blackList)
{
	string numberID;
	if (getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
	{
		return E_MINORNUMBER_NOT_EXESIT;
	}
	string sql = "select ForbidNumber from T_RuleBlackList where NumberID = '";
	sql += numberID;
	sql += "'";
	DB_RECORDSET_ARRAY dbrecord_;
	dbmysql_->executequery(sql,dbrecord_);
	if (dbrecord_.empty())
	{
		return E_OPERATOR_SUCCED;
	}

	DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
	while (it!=dbrecord_.end())
	{
		DB_RECORDSET_ITEM item_ = (*it);
		DB_RECORDSET_ITEM::iterator it_ = item_.find("forbidnumber");
		if (it_ != item_.end())
		{
			std::string callerStr = (*it_).second;
			blackList.push_back(callerStr);
		}

		++it;
	}
	return E_OPERATOR_SUCCED;
}

ResultDataUsage DataUsage::getWhiteList(const string& minorNumber, vector<string>& whiteList)
{
	string numberID;
	if (getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
	{
		return E_MINORNUMBER_NOT_EXESIT;
	}

	string sql =  "select AllowNumber from T_RuleWhiteList where NumberID = '";
	sql += numberID;
	sql += "'";
	DB_RECORDSET_ARRAY dbrecord_;
	dbmysql_->executequery(sql,dbrecord_);
	if (dbrecord_.empty())
	{
		return E_OPERATOR_SUCCED;
	}

	DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
	while (it!=dbrecord_.end())
	{
		DB_RECORDSET_ITEM item_ = (*it);
		DB_RECORDSET_ITEM::iterator it_ = item_.find("allownumber");
		if (it_ != item_.end())
		{
			string callerStr = (*it_).second;
			whiteList.push_back(callerStr);
		}
		++it;
	}
	return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : isInCalledList 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to check the caller number whether is limited by the 
*              called number by setting the black or white list. 
*              
* INPUT      : caller  const string&: The phone number which request the call.
*              minorNumber  const string& :The called user 's minor number.
*
* OUTPUT     : None.
*
* RETURN     : E_DATABASE_ERROR :The database opeartion is failed;
*              E_MINORNUMBER_NOT_EXESIT:The minior number is not exist.
*              E_IN_LIMITEIED_LIST_ERROR: The caller is limited by the called using the white or black list.
*              E_OPERATOR_SUCCED: The call is no limit.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::isInCalledList(const string& caller,const string& minorNumber)
{
	//To check the minor number whether has registered.
    std::string numberID;
    if (this->getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

	//First to check whether there is relation record with the numberID in the white list.
    DB_RECORDSET_ARRAY dbrecord_;
    std::string  sql = "select count(AllowNumber) wcounter from T_RuleWhiteList where NumberID = '";
    sql += numberID;
    sql += "'";

    if(0 != dbmysql_->executequery(sql,dbrecord_))
	{
        return E_DATABASE_ERROR;
	}

    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    if(it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("wcounter");
        if (it_ != item_.end())
        {
            int wNumber = atoi(it_->second.c_str());
            if (wNumber > 0)
            {
                sql.clear();
				dbrecord_.clear();
				//To check the call is in  the white list 
                sql = "select AllowNumber from T_RuleWhiteList where NumberID = '";
                sql += numberID;
                sql += "' and AllowNumber='";
                sql += caller;
                sql += "';";
                if(0 != dbmysql_->executequery(sql,dbrecord_))
	            {
                    return E_DATABASE_ERROR;
	            }
                else
                {
                    if(dbrecord_.empty())
                    {
						//No marked in white list forbid caller
                        return E_IN_LIMITEIED_LIST_ERROR;
                    }
                    else
                    {
                        return E_OPERATOR_SUCCED;            
                    }
                }
            }
        }
    }

	//Second to check whether there is black number relation record with the numberID.
    dbrecord_.clear();
	sql.clear();
    sql = "select count(ForbidNumber) bcounter from T_RuleBlackList where NumberID = '";
    sql += numberID;
    sql += "'";

    if(0 != dbmysql_->executequery(sql,dbrecord_))
	{
        return E_DATABASE_ERROR;
	}
	else
	{
	    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        if(it!=dbrecord_.end())
        {
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("bcounter");
            if (it_ != item_.end())
            {
                int bNumber = atoi(it_->second.c_str());
                if (bNumber > 0)
                {
					sql.clear();
					dbrecord_.clear();
                    sql = "select ForbidNumber from T_RuleBlackList where NumberID = '";
                    sql += numberID;
                    sql += "' and ForbidNumber='";
                    sql += caller;
                    sql += "';";
                    if(0 != dbmysql_->executequery(sql,dbrecord_))
	                {
                        return E_DATABASE_ERROR;
	                }
					else
					{
						 if(!dbrecord_.empty())
                         {
                             return E_IN_LIMITEIED_LIST_ERROR;
                         }
					}
                }
            }
        }
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : getListType 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to get the type of the limit list set by the 
*              pointed minor number. The type is such as follow:TYPE_NONE.TYPE_WHITE and TYPE_BLACK
*              
* INPUT      : minorNumber  const string&: The minor number.
*
* OUTPUT     : minorNumberId  string& :The number ID in the table record of the minor number.
*
* RETURN     : E_DATABASE_ERROR :The database opeartion is failed;
*              E_MINORNUMBER_NOT_EXESIT:The minior number is not exist.
*              TYPE_NONE: The user does not set any list to limit the caller.
*              TYPE_WHITE: The user has set the white list for the minor number.
*              TYPE_BLACK: The user has set the black list for the minor number.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::getListType(const string& minorNumber,string& minorNumberId)
{
    //Check the minor number whether is exist.
    std::string sql = "select NumberID from T_MinorNumber where MinorNumber = '";
    sql += minorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;

    if (dbrecord_.empty())
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    std::string numberID;
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("numberid");
        if (it_ != item_.end())
        {
            numberID = (*it_).second;
        }

        ++it;
    }
    //To get the black list of the minor number.
    minorNumberId = numberID;
    sql.clear();
    sql = "select ForbidNumber from T_RuleBlackList where NumberID = '";
    sql += numberID;
    sql += "'";
    dbrecord_.clear();
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;
    //If user does not set the black list for the monior number,check whether there is 
    //white list setting for it.
    if (dbrecord_.empty())
    {
        sql.clear();
        sql = "select AllowNumber from T_RuleWhiteList where NumberID = '";
        sql += numberID;
        sql += "'";
        dbrecord_.clear();
        if(0 != dbmysql_->executequery(sql,dbrecord_))
            return E_DATABASE_ERROR;
        if (dbrecord_.empty())//Uset does not set the balck list and the white list.
        {
            return TYPE_NONE;
        }
        else //The white list has been set for the minor number.
        {
            return TYPE_WHITE;
        }

    }

    //The user has set the black list for the monior number.So the list type is TYPE_BLACK.
    return TYPE_BLACK;
}

/***********************************************************************************
*
* NAME       : addNumberToBlackList 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to add member to the black list set by the pointed minor number. 
*              
* INPUT      : minorNumber  const std::string&: The unique ID of the minor number.
*              preAddNumber  const string& :The phone number need to be set into the black list.
*
* OUTPUT     : None.
*
* RETURN     : E_DATABASE_ERROR :The database opeartion is failed;
*              E_ADD_RELEASE_ERROR: The operation to add the limit is failed.
*              E_OPERATOR_SUCCED: The operation to add the limit is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::addNumberToBlackList(const string& minorNumber, const string& preAddNumber,OperateChannel inChanelID)
{
    //.s modify by whc for the single update by whc 2010-03-12
    string minorNumberId;
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "";
    //.e modify by whc for the single update by whc 2010-03-12
    std::string operatorNote = "Add ForbidNumber"+preAddNumber;
    short int opeResult = 0;

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    if (this->getListType(minorNumber,minorNumberId) == TYPE_BLACK || 
        this->getListType(minorNumber,minorNumberId) == TYPE_NONE)
    {
        //.s modify by whc for the single update by whc 2010-03-12
        if(this->getListType(minorNumber,minorNumberId) == TYPE_BLACK)
        {
            sql = "select ForbidNumber from T_RuleBlackList where NumberID = '";
            sql += minorNumberId + "' and ForbidNumber = '";
            sql += preAddNumber + "';";
            if(0 != dbmysql_->executequery(sql,dbrecord_))
            {
                return E_DATABASE_ERROR;
            }
            if(!dbrecord_.empty())
            {
                sql.clear();
                sql = "update T_RuleBlackList set CustomTime = now() where NumberID = '";
                sql += minorNumberId + "' and ForbidNumber = '";
                sql += preAddNumber + "';";    
                if(dbmysql_->executenonquery(sql) != 0)
                {
                    opeResult = -1;
                    this->setMinorNumberOpeRecord(minorNumberId, BLACKLIST_SET,tmpChanelID,operatorNote,opeResult);
                    return E_DATABASE_ERROR;
                }    
                else
                {
                    opeResult = 0;
                    this->setMinorNumberOpeRecord(minorNumberId, BLACKLIST_SET,tmpChanelID,operatorNote,opeResult);
                    return E_OPERATOR_SUCCED;
                }
            }
        }    
        sql.clear();
        //.e modify by whc for the single update by whc 2010-03-12
        //generate UUID
        std::string ruleID;
        generateUUID(ruleID);
        sql = "insert into T_RuleBlackList (RuleID,NumberID,ForbidNumber) values ('";
        sql += ruleID;
        sql += "','";
        sql += minorNumberId;
        sql += "','";
        sql += preAddNumber;
        sql += "')";
        if( 0 != dbmysql_->executenonquery(sql))
        {
            opeResult = -1;
            this->setMinorNumberOpeRecord(minorNumberId, BLACKLIST_SET,tmpChanelID,operatorNote,opeResult);
            return E_DATABASE_ERROR ;
        }
        else
        {
            opeResult = 0;
            this->setMinorNumberOpeRecord(minorNumberId, BLACKLIST_SET,tmpChanelID,operatorNote,opeResult);
        }
        //.s  modify  by whc for sync the database when there is change. 2010-02-20
        ResultDataUsage result = this->setUnSynchronized(MINORNUMBER, minorNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }
        //.e modify  by whc for sync the database when there is change. 2010-02-20        
        return E_OPERATOR_SUCCED;
    }
    return E_ADD_RELEASE_ERROR;
}

/***********************************************************************************
*
* NAME       : addNumberToWhiteList 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to add member to the white list set by the pointed minor number. 
*              
* INPUT      : minorNumber  const std::string&: The unique ID of the minor number.
*              preAddNumber  const string& :The phone number need to be set into the white list.
*
* OUTPUT     : None.
*
* RETURN     : E_DATABASE_ERROR :The database opeartion is failed;
*              E_ADD_RELEASE_ERROR: The operation to add the limit is failed.
*              E_OPERATOR_SUCCED: The operation to add the limit is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::addNumberToWhiteList(const string& minorNumber, const string& preAddNumber,OperateChannel inChanelID)
{

    string minorNumberId;
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "";

	std::string operatorNote = "Add AllowNumber"+preAddNumber;
	short int opeResult = 0;
    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    if (this->getListType(minorNumber,minorNumberId) == TYPE_WHITE || 
        this->getListType(minorNumber,minorNumberId) == TYPE_NONE)
    {
        //.s modify by whc for the single update by whc 2010-03-12
        if(this->getListType(minorNumber,minorNumberId) == TYPE_WHITE)
        {
            sql = "select AllowNumber from T_RuleWhiteList where NumberID = '";
            sql += minorNumberId + "' and AllowNumber = '";
            sql += preAddNumber + "';";
            if(0 != dbmysql_->executequery(sql,dbrecord_))
            {
                return E_DATABASE_ERROR;
            }
            if(!dbrecord_.empty())
            {
                sql.clear();
                sql = "update T_RuleWhiteList set CustomTime = now() where NumberID = '";
                sql += minorNumberId + "' and AllowNumber = '";
                sql += preAddNumber + "';";    
                if(dbmysql_->executenonquery(sql) != 0)
                {
					opeResult = -1;
					this->setMinorNumberOpeRecord(minorNumberId, WHITELIST_SET,tmpChanelID,operatorNote,opeResult);
					return E_DATABASE_ERROR;
                }        
				else
				{
					opeResult = 0;
					this->setMinorNumberOpeRecord(minorNumberId, WHITELIST_SET,tmpChanelID,operatorNote,opeResult);
					return E_OPERATOR_SUCCED;
				}
            }
        }    
        sql.clear();
        //.e modify by whc for the single update by whc 2010-03-12
        //generate UUID
        std::string ruleID;
        generateUUID(ruleID);
        sql = "insert into T_RuleWhiteList (RuleID,NumberID,AllowNumber) values ('";
        sql += ruleID;
        sql += "','";
        sql += minorNumberId;
        sql += "','";
        sql += preAddNumber;
        sql += "')";
        if( 0 != dbmysql_->executenonquery(sql))
		{
			opeResult = -1;
			this->setMinorNumberOpeRecord(minorNumberId, WHITELIST_SET,tmpChanelID,operatorNote,opeResult);
            return E_DATABASE_ERROR ;
		}
		else
		{
			opeResult = 0;
			this->setMinorNumberOpeRecord(minorNumberId, WHITELIST_SET,tmpChanelID,operatorNote,opeResult);
		}
        
        //.s  modify  by whc for sync the database when there is change. 2010-02-20
        ResultDataUsage result = this->setUnSynchronized(MINORNUMBER, minorNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }    
        //.e modify  by whc for sync the database when there is change. 2010-02-20            
        return E_OPERATOR_SUCCED;
    }
    return E_ADD_RELEASE_ERROR;
}

/***********************************************************************************
*
* NAME       : releaseALLBlackList 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to remove the black list set by the pointed minor number. 
*              
* INPUT      : minorNumber  const std::string&: The unique ID of the minor number.
*
* OUTPUT     : None.
*
* RETURN     : E_DATABASE_ERROR :The database opeartion is failed;
*              E_ADD_RELEASE_ERROR: The operation to release the limit is failed.
*              E_OPERATOR_SUCCED: The operation to release the limit is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::releaseALLBlackList(const string& minorNumber,OperateChannel inChanelID)
{
    string operatorNote = "Delete all forbidden number";
    short int opeResult = 0;
    string minorNumberId;

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    if (this->getListType(minorNumber,minorNumberId) == TYPE_BLACK)
    {
        std::string sql = "delete from T_RuleBlackList where NumberID = '";
        sql += minorNumberId;
        sql += "'";
        if( 0 != dbmysql_->executenonquery(sql))
        {
            opeResult = -1;
            this->setMinorNumberOpeRecord(minorNumberId, BLACKLIST_SET,tmpChanelID,operatorNote,opeResult);
            return E_DATABASE_ERROR ;
        }
        else
        {
            opeResult = 0;
            this->setMinorNumberOpeRecord(minorNumberId, BLACKLIST_SET,tmpChanelID,operatorNote,opeResult);
        }
        
        //.s  modify  by whc for sync the database when there is change. 2010-02-20
        ResultDataUsage result = this->setUnSynchronized(MINORNUMBER, minorNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }    
        //.e modify  by whc for sync the database when there is change. 2010-02-20        
        return E_OPERATOR_SUCCED;
    }
    return E_ADD_RELEASE_ERROR;
}

/***********************************************************************************
*
* NAME       : releaseALLWhiteList 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to remove the white list set by the pointed minor number. 
*              
* INPUT      : minorNumber  const std::string&: The unique ID of the minor number.
*
* OUTPUT     : None.
*
* RETURN     : E_DATABASE_ERROR :The database opeartion is failed;
*              E_ADD_RELEASE_ERROR: The operation to release the limit is failed.
*              E_OPERATOR_SUCCED: The operation to release the limit is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::releaseALLWhiteList(const string& minorNumber,OperateChannel inChanelID)
{
    string minorNumberId;
    short int opeResult = 0;
    string operatorNote = "Delete all allowed number";

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    if (this->getListType(minorNumber,minorNumberId) == TYPE_WHITE)
    {
        std::string sql = "delete from T_RuleWhiteList where NumberID = '";
        sql += minorNumberId;
        sql += "'";
        if( 0 != dbmysql_->executenonquery(sql))
            return E_DATABASE_ERROR ;

        //.s  modify  by whc for sync the database when there is change. 2010-02-20
        ResultDataUsage result = this->setUnSynchronized(MINORNUMBER, minorNumber);
        if(result == E_DATABASE_ERROR)
        {
            opeResult = -1;
            this->setMinorNumberOpeRecord(minorNumberId, WHITELIST_SET,tmpChanelID,operatorNote,opeResult);
            return E_DATABASE_ERROR ;
        }
        else
        {
            opeResult = 0;
            this->setMinorNumberOpeRecord(minorNumberId, WHITELIST_SET,tmpChanelID,operatorNote,opeResult);
        }

        //.e modify  by whc for sync the database when there is change. 2010-02-20        
        return E_OPERATOR_SUCCED;
    }
    return E_ADD_RELEASE_ERROR;
}

/***********************************************************************************
*
* NAME       : releaseNumberFromBlackList 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to remove the limit from the black list 
*              of the pointed minor number. 
*              
* INPUT      : minorNumber  const std::string&: The phone number which will remove 
*                           the member from its black list.
*              preReleaseNumber  const std::string&: The phone number that should  
*                           remove the black list limit for the call.
*
* OUTPUT     : None.
*
* RETURN     : E_DATABASE_ERROR :The database opeartion is failed;
*              E_ADD_RELEASE_ERROR: The operation to release the limit is failed.
*              E_OPERATOR_SUCCED: The operation to release the limit is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::releaseNumberFromBlackList(const string& minorNumber,const string& preReleaseNumber,OperateChannel inChanelID)
{
    string minorNumberId;
    string operatorNote = "Delete " +preReleaseNumber;
    short int opeResult = 0;

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    //Check the minor number whether has set the black list.
    if (this->getListType(minorNumber,minorNumberId) == TYPE_BLACK)
    {
        std::string sql = "delete from T_RuleBlackList where NumberID = '";
        sql += minorNumberId;
        sql += "' and ForbidNumber = '";
        sql += preReleaseNumber;
        sql += "'";

        if( 0 != dbmysql_->executenonquery(sql))
        {
            opeResult = -1;
            this->setMinorNumberOpeRecord(minorNumberId, BLACKLIST_SET,tmpChanelID,operatorNote,opeResult);        
            return E_DATABASE_ERROR ;
        }
        else
        {
            opeResult = 0;
            this->setMinorNumberOpeRecord(minorNumberId, BLACKLIST_SET,tmpChanelID,operatorNote,opeResult);
        }
        
        //.s  modify  by whc for sync the database when there is change. 2010-02-20
        ResultDataUsage result = this->setUnSynchronized(MINORNUMBER, minorNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }    
        //.e modify  by whc for sync the database when there is change. 2010-02-20        
        return E_OPERATOR_SUCCED;
    }
    return E_ADD_RELEASE_ERROR;
}

/***********************************************************************************
*
* NAME       : releaseNumberFromWhiteList 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to remove the limit from the white list 
*              of the pointed minor number. 
*              
* INPUT      : minorNumber  const std::string&: The phone number which will remove 
*                           the member from its white list.
*              preReleaseNumber  const std::string&: The phone number that should  
*                           remove the white list limit for the call.
*
* OUTPUT     : None.
*
* RETURN     : E_DATABASE_ERROR :The database opeartion is failed;
*              E_ADD_RELEASE_ERROR: The operation to release the limit is failed.
*              E_OPERATOR_SUCCED: The operation to release the limit is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::releaseNumberFromWhiteList(const string &minorNumber, const string &preReleaseNumber,OperateChannel inChanelID)
{
    string minorNumberId;

    string operatorNote = "Delete " +preReleaseNumber;
    short int opeResult = 0;

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    //Check the minor number whether has set the white list.
    if (this->getListType(minorNumber, minorNumberId) == TYPE_WHITE)
    {
        std::string sql = "delete from T_RuleWhiteList where NumberID = '";
        sql += minorNumberId;
        sql += "' and AllowNumber = '";
        sql += preReleaseNumber;
        sql += "'";

        if( 0 != dbmysql_->executenonquery(sql))
        {
            opeResult = -1;
            this->setMinorNumberOpeRecord(minorNumberId, WHITELIST_SET,tmpChanelID,operatorNote,opeResult);        
            return E_DATABASE_ERROR ;
        }
        else
        {
            opeResult = 0;
            this->setMinorNumberOpeRecord(minorNumberId, WHITELIST_SET,tmpChanelID,operatorNote,opeResult);
        }

        //.s  modify  by whc for sync the database when there is change. 2010-02-20
        ResultDataUsage result = this->setUnSynchronized(MINORNUMBER, minorNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }    
        //.e modify  by whc for sync the database when there is change. 2010-02-20        
        return E_OPERATOR_SUCCED;
    }
    return E_ADD_RELEASE_ERROR;
}

/***********************************************************************************
*
* NAME       : getVoiceCount 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to get the count of the voice file and the count 
*              of the files that has not read in the voice box of the pointed minor number. 
*              
* INPUT      : minorNumber  const std::string&: The phone number that to get its uinque number ID.
*
* OUTPUT     : allVoiceCount  int&: The count of all the voice file fo the pointed minor number.
*              hasNotReadVoiceCount  int&: The count of the voice file that has not read..
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              VOICEBOXSTATE_TRUE: There is number set calling forward for the minor number.
*              VOICEBOXSTATE_FALSE: Current voice box state is no number set to call 
*                          forward to voice box for the input minor number.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::getVoiceCount(const std::string &minorNumber,int& allVoiceCount,int& hasNotReadVoiceCount)
{
    std::string numberID;
    if( this->getNumberID(minorNumber, numberID) == E_MINORNUMBER_NOT_EXESIT )
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    std::string sql ="select HasRead from T_VoiceMsgBox where NumberID = '";
    sql += numberID;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if( 0 != dbmysql_->executequery(sql,dbrecord_) )
	{
		return E_DATABASE_ERROR;
	}
    if (dbrecord_.empty())
    {
        allVoiceCount = 0;
        hasNotReadVoiceCount = 0;
    }
    else
    {
        allVoiceCount = dbrecord_.size();
        int i = 0;
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        while( it != dbrecord_.end() )
        {
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("hasread");
            if (it_->second == "0")
            {
                i++;
            }

            ++it;
        }
        hasNotReadVoiceCount = i;

    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : getVoiceBoxState 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to get the voice box state about the pointed minor number
*              
* INPUT      : minorNumber  const std::string&: The phone number that to get its uinque number ID.
*
* OUTPUT     : None.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              VOICEBOXSTATE_TRUE: There is number set calling forward for the minor number.
*              VOICEBOXSTATE_FALSE: Current voice box state is no number set to call 
*                          forward to voice box for the input minor number.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::getVoiceBoxState(const std::string &minorNumber)
{
    std::string sql = "select RecordRule from T_MinorNumber where MinorNumber = '";
    sql += minorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;
    if (dbrecord_.empty())
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    std::string voiceBoxState;
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("recordrule");
        if (it_ != item_.end())
        {
            voiceBoxState = (*it_).second;
        }

        ++it;
    }
    //User set any numbers to call forward to voice box when called.
    int voiceBoxState_ = atoi(voiceBoxState.c_str());
    if (voiceBoxState_ == VOICEBOXSTATE_TRUE)
    {
        return VOICEBOXSTATE_TRUE;
    }
    else //no set the voice box state
    {
        return VOICEBOXSTATE_FALSE;
    }
}

/***********************************************************************************
*
* NAME       : setVoiceBoxState 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to set the message box state about the pointed minor number
*              The state is include two values such as true or false.
*              
* INPUT      : minorNumber  const std::string&: The phone number that to get its uinque number ID.
*              voiceBoxState ResultDataUsage: It is the value that need to set for the minor number.
*
* OUTPUT     : None.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::setVoiceBoxState(const std::string& minorNumber,ResultDataUsage voiceBoxState)
{
    std::string sql = "select RecordRule from T_MinorNumber where MinorNumber = '";
    sql += minorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;

    if (dbrecord_.empty())
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    char buff[10];
    memset(buff,0,10);
    sprintf(buff,"%d",voiceBoxState);
    sql.clear();

    //.s modify  by whc for sync the database when there is change. 2010-02-20
    //    sql = "update T_MinorNumber set RecordRule = '";
    sql = "update T_MinorNumber set IsSynchronization = 0, RecordRule = '";
    //.e modify  by whc for sync the database when there is change. 2010-02-20

    sql += buff;
    sql += "' where MinorNumber = '";
    sql += minorNumber;
    sql += "'";
    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR ;
    return E_OPERATOR_SUCCED;
}

ResultDataUsage DataUsage::getVoiceList(const std::string& minorNumber, vector<string>& voiceList)
{
	string numberId;
	if (getNumberID(minorNumber,numberId) == E_MINORNUMBER_NOT_EXESIT)
	{
		return E_MINORNUMBER_NOT_EXESIT;
	}
	string sql = "select AllowNumber from T_RuleRecord where NumberID = '";
	sql += numberId;
	sql += "'";
	DB_RECORDSET_ARRAY dbrecord;
	dbmysql_->executequery(sql,dbrecord);
	if (dbrecord.empty())
	{
		return E_OPERATOR_SUCCED;
	}
	else
	{
		DB_RECORDSET_ARRAY::iterator it = dbrecord.begin();
		while (it != dbrecord.end())
		{
			DB_RECORDSET_ITEM::iterator it_ = (*it).find("allownumber");
			voiceList.push_back(it_->second);
		
			++it;
		}
	}
	return E_OPERATOR_SUCCED;

}

/***********************************************************************************
*
* NAME       : getVoiceBoxType 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to get the message box type about the pointed 
*              minor number's setting.
*              
* INPUT      : minorNumber  const std::string&: The phone number.
*
* OUTPUT     : None.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              VOICEBOXTYPE_ALL :All the caller number's request call forward to voice box.
*              VOICEBOXTYPE_PART :The limited number request call forward to voice box.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::getVoiceBoxType(const std::string &minorNumber)
{
    /*
    if (VOICEBOXSTATE_FALSE == this->getVoiceBoxState(minorNumber))
    {
        return VOICEBOXSTATE_FALSE;
    }*/

    std::string sql = "select NumberID from T_MinorNumber where MinorNumber = '";
    sql += minorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;
    if (dbrecord_.empty())
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    std::string numberID;
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("numberid");
        if (it_ != item_.end())
        {
            numberID = (*it_).second;
        }

        ++it;
    }
    sql.clear();
    sql = "select AllowNumber from T_RuleRecord where NumberID = '";
    sql += numberID;
    sql += "'";
    dbrecord_.clear();
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;
    //If the voice box state is true ,when there is no number set means that
    //the voice box type is VOICEBOXTYPE_ALL same as all caller call forward to voice box.
    if (dbrecord_.empty())
    {
        return VOICEBOXTYPE_ALL;
    }
    else
    {
        //If there is number exist in the T_RuleRecord table ,the type is  VOICEBOXTYPE_PART
        return VOICEBOXTYPE_PART;
    }
    
}

/***********************************************************************************
*
* NAME       : setVoiceBoxType 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to set the message box type about the pointed minor number
*              If the type is the value of VOICEBOXTYPE_ALL,to set the voice box state to true state.
*              
* INPUT      : minorNumber  const std::string&: The phone number .
*              voiceBoxType ResultDataUsage: The type of the message box inlude the value of 
*                       OICEBOXTYPE_ALL and the value of VOICEBOXTYPE_ALL.
*
* OUTPUT     : None.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::setVoiceBoxType(const std::string& minorNumber,ResultDataUsage voiceBoxType,OperateChannel inChanelID)
{
    string numberID;
    if (this->getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    string operatorNote ;
    short int opeResult = 0;
    operatorNote = "Add all number CallForward";
    string sql;

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);
    //All the caller call forward to message box.

    if (voiceBoxType == VOICEBOXTYPE_ALL)
    {
        sql = "delete from T_RuleRecord where NumberID = '";
        sql += numberID;
        sql += "'";
        if( 0 != dbmysql_->executenonquery(sql))
        {
			opeResult = -1;
            this->setMinorNumberOpeRecord(numberID, VOICEMESSAGEBOX_SET,tmpChanelID,operatorNote,opeResult);
            return E_DATABASE_ERROR ;
        }

        if(this->setVoiceBoxState(minorNumber,VOICEBOXSTATE_TRUE)== E_DATABASE_ERROR)
        {
			opeResult = -1;
            this->setMinorNumberOpeRecord(numberID, VOICEMESSAGEBOX_SET,tmpChanelID,operatorNote,opeResult);
            return E_DATABASE_ERROR;
        }
        opeResult = 0;
        this->setMinorNumberOpeRecord(numberID, VOICEMESSAGEBOX_SET,tmpChanelID,operatorNote,opeResult);
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : getNumberID 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to get the minor number's ID according with the minor number.
*              
* INPUT      : minorNumber  const std::string&: The phone number that to get its uinque number ID..
*
* OUTPUT     : userID  const std::string &: The pointed major number's user identity number.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/
ResultDataUsage DataUsage::getNumberID(const std::string& minorNumber,std::string& numberID)
{
    std::string sql = "select NumberID from T_MinorNumber where MinorNumber = '";
    sql += minorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;

    if (dbrecord_.empty())
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("numberid");
        if (it_ != item_.end())
        {
            numberID = (*it_).second;
        }

        ++it;
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : getUserID 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to get the major number according with its minor number.
*              
* INPUT      : majorNumber  const std::string&: The phone number that to get its uinque user ID.
*
* OUTPUT     : userID  const std::string &: The pointed major number's user identity number.
*
* RETURN     : E_MAJORNUMBER_NOT_EXESIT: The major number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/
ResultDataUsage DataUsage::getUserID(const std::string& majorNumber,std::string& userID)
{
    //Check the major number whether is exist.
    std::string sql = "select UserID from T_RegisterUser where UserNumber = '";
    sql += majorNumber;
    sql += "'";

    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;

    if (dbrecord_.empty())
    {
        return E_MAJORNUMBER_NOT_EXESIT;
    }
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.begin();
        while(it_!=item_.end())
        {
            userID = (*it_).second;
            ++it_;
        }
        ++it;
    }
    return E_OPERATOR_SUCCED;
}


ResultDataUsage DataUsage::getBacUserID(const std::string& majorNumber,std::string& userID)
{
	//主号码是否存在
	std::string sql = "select UserID from t_registeruser_bak where UserNumber = '";
	sql += majorNumber;
	sql += "'";
	DB_RECORDSET_ARRAY dbrecord_;
	if(0 != dbmysql_->executequery(sql,dbrecord_))
		return E_DATABASE_ERROR;

	if (dbrecord_.empty())
	{
		return E_MAJORNUMBER_NOT_EXESIT;
	}
	DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
	while (it!=dbrecord_.end())
	{
		DB_RECORDSET_ITEM item_ = (*it);
		DB_RECORDSET_ITEM::iterator it_ = item_.begin();
		while(it_!=item_.end())
		{
			userID = (*it_).second;
			++it_;
		}
		++it;
	}
	return E_OPERATOR_SUCCED;
}
/***********************************************************************************
*
* NAME       : getUserIDbyNumberID 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to get the major number according with its minor number.
*              
* INPUT      : minorNumberID  const std::string&: The minor number is used for getting its
*                            major number.
*
* OUTPUT     : userID  const std::string &: It is the major number got by its minor number.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/
ResultDataUsage DataUsage::getUserIDbyNumberID(const std::string& minorNumberID, std::string& userID)
{
    std::string sql = "select UserID from T_MinorNumber where NumberID = '";
    sql += minorNumberID;
    sql += "'";

    DB_RECORDSET_ARRAY dbrecord_;
    if( 0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;
    
    if (dbrecord_.empty())
        return E_MINORNUMBER_NOT_EXESIT;

    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();

    DB_RECORDSET_ITEM::iterator it_ = (*it).find("userid");
    userID = it_->second;
        
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : getNumberIDbyUserID 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used for the pointed major number to get all the minor
*              number from database .
*              
* INPUT      : userID  const std::string &: The pointed major number's user identity number.
*
* OUTPUT     :  minorNumberIDs  vector<string> & : All the minor number's ID that belong to the
*                            the pointed major number.
*
* RETURN     : E_MAJORNUMBER_NOT_EXESIT: The major number has no minor number.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/
ResultDataUsage DataUsage::getNumberIDbyUserID(const std::string& userID, vector<string> & minorNumberIDs)
{
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "select NumberID from T_MinorNumber where UserID = '";
    sql += userID;
    sql += "'";
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;    
    //There is no any minor number about the user.
    if (dbrecord_.empty())
    {
        return E_MAJORNUMBER_NOT_EXESIT; 
    }
    //Filter the minor number of the major number.
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.begin();
        while(it_!=item_.end())
        {
            string numberID = (*it_).second;
            minorNumberIDs.push_back(numberID);
            ++it_;
        }
        ++it;
    }
        
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : addNumberToVoiceList 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used for the pointed phone number to add the number 
*              which be set calling forward to the voice box.
*              
* INPUT      : minorNumber  const std::string &: The phone number which has set calling forward 
*                           to the voice box .
*              number  std::string &: The phone number will be set into the pointed minor's 
*                           voice list.When the number calls this minor number ,the call will call
*                           forward to the voice box.
*
* OUTPUT     : None;
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/
ResultDataUsage DataUsage::addNumberToVoiceList(const std::string &minorNumber, const std::string &number,OperateChannel inChanelID)
{
    string numberID;
    if (this->getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    //First set the voice box state to true that means the pointed minor number has add the calling forward number.
    string operatorNote ;
    short int opeResult = 0;
    //generate UUID
    std::string ruleID;
    generateUUID(ruleID);
    std::string sql = "insert into T_RuleRecord (RuleID,NumberID,AllowNumber) values ('";
    sql += ruleID;
    sql += "','";
    sql += numberID;
    sql += "','";
    sql += number;
    sql += "')";
    if( 0 != dbmysql_->executenonquery(sql))
    {
        operatorNote = "Add "+number+" callForward";
        opeResult = -1;
        this->setMinorNumberOpeRecord(numberID, VOICEMESSAGEBOX_SET,tmpChanelID,operatorNote,opeResult);
        return E_DATABASE_ERROR ;
    }
    //设置流言状态为true
    //.s modify the IsSynchronization filed setting for the sync database by whc 2010-03-09
    if(this->setVoiceBoxState(minorNumber,VOICEBOXSTATE_TRUE)!=0)
    {
        return E_DATABASE_ERROR;
    }
    //.s modify the IsSynchronization filed setting for the sync database by whc 2010-03-09        
    operatorNote = "Add "+number+" callForward";
    opeResult = 0;
    this->setMinorNumberOpeRecord(numberID, VOICEMESSAGEBOX_SET,tmpChanelID,operatorNote,opeResult);
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : releaseAllVoiceNumber 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to release the setting that calling forward  
*              to the voice box for all the phone numbers which have set by the pointed 
*              minor phone number.  
*              
* INPUT      : minorNumber  const std::string &: The phone number which has set calling forward 
*                           to the voice box .
*
* OUTPUT     : None;
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::releaseAllVoiceNumber(const std::string& minorNumber,OperateChannel inChanelID)
{
    string numberID;
    if (this->getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);
    short int opeResult = 0;
    string operatorNote = "Delete all number callForward";    

    string sql = "delete from T_RuleRecord where NumberID = '";
    sql += numberID;
    sql += "'";
    if( 0 != dbmysql_->executenonquery(sql))
    {
        opeResult = -1;
        this->setMinorNumberOpeRecord(numberID, VOICEMESSAGEBOX_SET,tmpChanelID,operatorNote,opeResult);
        return E_DATABASE_ERROR ;
    }
    else
    {
        this->setMinorNumberOpeRecord(numberID, VOICEMESSAGEBOX_SET,tmpChanelID,operatorNote,opeResult);    
    }

    //The sate of the voice box setting shoud be change to VOICEBOXSTATE_FALSE that means the pointed number 
    //has not set the voice box.
    this->setVoiceBoxState(minorNumber,VOICEBOXSTATE_FALSE);
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : releaseVoiceNumber 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to release the setting that calling forward  
*              to the voice box for the pointed caller.   
*              
* INPUT      : minorNumber  const std::string & : The phone number which has set calling forward 
*                           to the voice box .
*              preReleaseNumber  const std::string & : It is the phone number which change the call 
*                                to the voice box when it calls the pointed minor number.. 
*
* OUTPUT     : None;
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::releaseVoiceNumber(const std::string &minorNumber,const std::string &preReleaseNumber,OperateChannel inChanelID)
{
    string numberID;
    if (this->getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    //When the caller calls the pointed number ,the call session will not be turn to the voice box.
    string sql = "delete from T_RuleRecord where NumberID = '";
    sql += numberID;
    sql += "'and AllowNumber = '";
    sql += preReleaseNumber;
    sql += "'";
    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR ;
    //To check whether there is other number that be set to the voice box. 
    sql.clear();
    sql += "select * from T_RuleRecord where NumberID = '";
    sql += numberID;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;

    //If there is no number that allow to turn to the voice box when calls the pointed phone number,the
    //sate of the voice box setting shoud be change to VOICEBOXSTATE_FALSE that means the pointed number 
    //has not set the voice box.
    if (dbrecord_.empty())
    {
        this->setVoiceBoxState(minorNumber,VOICEBOXSTATE_FALSE);
    }
    else
    {
        //.s  modify  by whc for sync the database when there is change. 2010-02-20
        ResultDataUsage result = this->setUnSynchronized(MINORNUMBER, minorNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }
        //.e modify  by whc for sync the database when there is change. 2010-02-20    
    }
    short int opeResult = 0;
    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    std::string operatorNote = "Delete "+preReleaseNumber+" callForward";
    this->setMinorNumberOpeRecord(numberID, VOICEMESSAGEBOX_SET,tmpChanelID,operatorNote,opeResult);    

    return E_OPERATOR_SUCCED;

}

/***********************************************************************************
*
* NAME       : getVoiceMsg 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to get the voice file which is create for the pointed phone number.
*              
* INPUT      : minorNumber  const std::string & : The phone number which receive the voice message.
*              voiceFileName  const std::string & : It is the voice file name which caller create. 
*              timeVoice    std::string & :It is the time when caller created the voice file.
*              doVoiceNumber  const std::string & :The phone number which set the voice message.
*              isRead  int :  The state of the voice message,the default value is 0 which means 
*                             to get the voice file that has not read.If you set the value to 1 means
*                             that get the voice message that has read.
*
* OUTPUT     : None;
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_NOT_HAVEVOICEMSG_ERROR: There is no voice message for the pointed phone number.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::getVoiceMsg(const std::string &minorNumber, std::string &voiceFileName, 
                           std::string &timeVoice, std::string &doVoiceNumber, int isRead)
{
    std::string numberID;
    if (this->getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    char buff[10];
    memset(buff,0,sizeof(buff));
    sprintf(buff,"%d",isRead);
    std::string sql = "select * from T_VoiceMsgBox where NumberID = '";
    sql += numberID;
    sql += "' and HasRead = '";
    sql += buff;
    sql += "'order by RecTime desc";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;
    if (dbrecord_.empty())
    {
        return E_NOT_HAVEVOICEMSG_ERROR;
    }
    //To get the first voice message with the same state order by time..
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    DB_RECORDSET_ITEM item_ = (*it);
    DB_RECORDSET_ITEM::iterator it_ = item_.find("recfilename");
    if (it_ != item_.end())
    {
        LogFileManager::getInstance()->write(Brief,"recfilename = %s", (*it_).second.c_str());
        //2009-8-25 by zhouqianren
        //Sensitive Field decrypt -----start---------------------
        unsigned char key[8];
        memset(key, 0 , sizeof(key));
        strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
        Crypt crypt;
        string decrypt_file = crypt.decrypt(key, (char *)(*it_).second.c_str()); 
        voiceFileName = decrypt_file;
        LogFileManager::getInstance()->write(Brief,"voiceFileName = %s", voiceFileName.c_str());
        //Sensitive Field decrypt -----end----------------------    
    }
    //Get the time of the voice message when create.
    it_ = item_.find("rectime");
    if (it_ != item_.end())
    {
        timeVoice = (*it_).second;
    }
    //Get the number of the voice message which create by.
    it_ = item_.find("recnumber");
    if (it_ != item_.end())
    {
        doVoiceNumber = (*it_).second;
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : setVoiceMsg 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to set the voice file for the pointed phone number.
*              
* INPUT      : minorNumber  const std::string & : The phone number which receive the voice message.
*              voiceFileName  const std::string & : It is the voice file name which caller create. 
*              doVoiceNumber  const std::string & :The phone number which set the voice message.
*              isRead  int :  The state of the voice message,the default value is 0 which means 
*                             the message has not read.
*
* OUTPUT     : None;
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::setVoiceMsg(const std::string &minorNumber,const std::string &voiceFileName, 
                           const std::string &doVoiceNumber, int isRead )
{
    std::string numberID;
    if (this->getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    char buff[10];
    memset(buff,0,sizeof(buff));
    sprintf(buff,"%d",isRead);
    //generate UUID
    std::string msgID;
    generateUUID(msgID);

    //2009-8-25 by zhouqianren
    //Sensitive Field encrypt -----start----------------------
    unsigned char key[8];
    memset(key, 0 , sizeof(key));
    strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
    
    Crypt crypt;
    LogFileManager::getInstance()->write(Brief,"key = %s, voiceFileName = %s",  (char *)key, voiceFileName.c_str());
    string encrypt_file = crypt.encrypt(key, (char *)voiceFileName.c_str()); 
    LogFileManager::getInstance()->write(Brief,"encrypt_file = %s", encrypt_file.c_str());
    //Sensitive Field encrypt -----end----------------------

    std::string sql = "insert into  T_VoiceMsgBox (MsgID,NumberID,RecFileName,RecTime,RecNumber,HasRead) values ( '"; 
    sql += msgID;
    sql += "','";
    sql += numberID;
    sql += "','";
    sql += encrypt_file;
    sql += "', now(), '";
    sql += doVoiceNumber;
    sql += "','";
    sql += buff;
    sql += "')";

    LogFileManager::getInstance()->write(Brief,"encrypt_file = %s", sql.c_str());
    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR ;
    return E_OPERATOR_SUCCED;

}

/***********************************************************************************
*
* NAME       : setVoiceHasRead 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to set the voice file state to read for the pointed 
*              phone number.
*              
* INPUT      : minorNumber  const std::string & : Pointer the minor number.
*              voiceFileName  const std::string & : It is the voice file name which need to set state.
*
* OUTPUT     : None;
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::setVoiceHasRead(const std::string &minorNumber,const std::string &voiceFileName)
{
    std::string numberID;
    if (this->getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    //2009-8-25 by zhouqianren
    //Sensitive Field encrypt -----start----------------------
    unsigned char key[8];
    memset(key, 0 , sizeof(key));
    strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
    
    Crypt crypt;
    string encrypt_file = crypt.encrypt(key, (char *)voiceFileName.c_str()); 
    //Sensitive Field encrypt -----end----------------------

    std::string sql = "update T_VoiceMsgBox set HasRead = '1' where NumberID = '";
    sql += numberID;
    sql += "' and RecFileName = '";
    sql += encrypt_file;
    sql += "'";
    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR ;
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : setStrategyID 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to set the time limit policy about voice calling for the pointed 
*              phone number..
*              
* INPUT      : minorNumber  const std::string & : Pointer the minor number.
*              strategyID   int                 : The time limit policy.[101,102,103]
*
* OUTPUT     : none
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::setStrategyID(const std::string &minorNumber, int strategyID,OperateChannel inChanelID)
{
    string numberID;
    ResultDataUsage result;
    if (this->getNumberID(minorNumber,numberID)  == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    string operatorNote ;
    short int opeResult = 0;

    char buff[10];
    memset(buff,0,sizeof(buff));
    sprintf(buff,"%d",strategyID);
    string strBuf = string(buff);
    //To check whether the rule time has been set.
    string sql ="select * from T_RuleTime where NumberID = '";
    sql +=numberID;
    sql +="' and  StrategyID >= 0 and StrategyID <= 9";
    DB_RECORDSET_ARRAY dbrecord;
    if(0 != dbmysql_->executequery(sql,dbrecord))
        return E_DATABASE_ERROR;

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    if (dbrecord.empty())
    {
        //generate UUID
        std::string ruleID;
        generateUUID(ruleID);
        sql.clear();
        sql = "insert into T_RuleTime (RuleID,NumberID,StrategyID) values ('";
        sql += ruleID;
        sql += "','";
        sql += numberID;
        sql += "','";
        sql += buff;
        sql += "')";

        operatorNote = "Add Strategy "+strBuf;
        if( 0 != dbmysql_->executenonquery(sql))
        {
            opeResult = -1;
            this->setMinorNumberOpeRecord(numberID, IVRTIMESTRATEGY_SET,tmpChanelID,operatorNote,opeResult);
            return E_DATABASE_ERROR ;
        }
        else
        {
            opeResult = 0;
            this->setMinorNumberOpeRecord(numberID, IVRTIMESTRATEGY_SET,tmpChanelID,operatorNote,opeResult);
        }
        
        //.s  modify  by whc for sync the database when there is change. 2010-02-20
        result = this->setUnSynchronized(MINORNUMBER, minorNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }
        //.e modify  by whc for sync the database when there is change. 2010-02-20        
        return E_OPERATOR_SUCCED;
    }
    //.s add for avoid same setting bug by whc 2010-03-11
    DB_RECORDSET_ARRAY::iterator it = dbrecord.begin();
    string dbStrategyId = "";
    if(it != dbrecord.end())
    {
        DB_RECORDSET_ITEM::iterator it_ = (*it).find("strategyid");
        if(it_!=(*it).end())
        {
            dbStrategyId = it_->second.c_str();
        }
        else
        {
            return E_DATABASE_ERROR;
        }
    }
    //.e add for avoid same setting bug by whc 2010-03-11    
    sql.clear();
    sql = "update T_RuleTime set StrategyID = '";
    sql += buff;
    sql += "' where NumberID = '";
    sql += numberID;
    sql += "' and StrategyID = '";
    sql += dbStrategyId+"';";

    operatorNote.clear();
    operatorNote = "Update Strategy "+strBuf;
    if( 0 != dbmysql_->executenonquery(sql))
    {
        opeResult = -1;
        this->setMinorNumberOpeRecord(numberID, IVRTIMESTRATEGY_SET,tmpChanelID,operatorNote,opeResult);
        return E_DATABASE_ERROR ;
    }
    else
    {
        opeResult = 0;
        this->setMinorNumberOpeRecord(numberID, IVRTIMESTRATEGY_SET,tmpChanelID,operatorNote,opeResult);
    }
    //.s  modify  by whc for sync the database when there is change. 2010-02-20
    result = this->setUnSynchronized(MINORNUMBER, minorNumber);
    if(result == E_DATABASE_ERROR)
    {
        return E_DATABASE_ERROR;
    }
    //.e modify  by whc for sync the database when there is change. 2010-02-20    
    return E_OPERATOR_SUCCED;

}

ResultDataUsage DataUsage::cancelStrategyID(const string& minorNumber,OperateChannel inChanelID)
{
	std::string numberID;

	ResultDataUsage ret = this->getNumberID(minorNumber,numberID) ;
	if ( ret != E_OPERATOR_SUCCED)
	{
		return ret;
	}

	OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

	std::string sql ="select StrategyID from T_RuleTime where NumberID = '";
	sql +=numberID;
	sql +="' and  StrategyID >= 0 and StrategyID <= 9";
	
	DB_RECORDSET_ARRAY dbrecord;
	dbmysql_->executequery(sql,dbrecord);
	if (dbrecord.empty())
	{
		return E_OPERATOR_SUCCED;
	}
	else
	{
		DB_RECORDSET_ARRAY::iterator it = dbrecord.begin();
		string dbStrategyId = "";
		if(it != dbrecord.end())
		{
			DB_RECORDSET_ITEM::iterator it_ = (*it).find("strategyid");
			if(it_!=(*it).end())
			{
				dbStrategyId = it_->second.c_str();
			}
			else
			{
				return E_DATABASE_ERROR;
			}
		}
		sql.clear();
		sql = "delete from T_RuleTime where StrategyID ='";
		sql += dbStrategyId;
		sql += "' and NumberID = '";
		sql += numberID;
		sql += "';";

		string operatorNote = "Delete Strategy "+dbStrategyId;

		short int opeResult = 0;

		if(dbmysql_->executenonquery(sql) != 0)
		{
			opeResult = -1;
			this->setMinorNumberOpeRecord(numberID, IVRTIMESTRATEGY_SET,tmpChanelID,operatorNote,opeResult);
			return E_DATABASE_ERROR;
		}
		else
		{
			opeResult = 0;
			this->setMinorNumberOpeRecord(numberID, IVRTIMESTRATEGY_SET,tmpChanelID,operatorNote,opeResult);
		}
	}
	return E_OPERATOR_SUCCED;
}
/***********************************************************************************
*
* NAME       : setSmTimeStrategy 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to set the time limit policy about the short message
*              for the pointed minor number .
*              
* INPUT      : minorNumber  const std::string & : Pointer the minor number.
*              startTime    const std::string & : The limit time when start.
*              endTime      const std::string & : The limit time when end.
*              strategyID   int                 : The time limit policy.[101,102,103]
*
* OUTPUT     : none
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::setSmTimeStrategy(const string& minorNumber,
                                             const string& startTime,const string& endTime,int strategyID,OperateChannel inChanelID)
{
    string numberID;
    ResultDataUsage result;
    if (this->getNumberID(minorNumber,numberID)  == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    char buff[10];
    memset(buff,0,sizeof(buff));
    sprintf(buff,"%d",strategyID);
    string strBuff = string(buff);

    string operatorNote ;
    short int opeResult = 0;

    operatorNote = "Add SMS Strategy "+strBuff;
    //To check whether the rule time has been set.
    std::string sql ="select * from T_RuleTime where NumberID = '";
    sql +=numberID;
    sql +="' and  StrategyID > 100 and StrategyID < 104";
    DB_RECORDSET_ARRAY dbrecord;
    if(0 != dbmysql_->executequery(sql,dbrecord))
        return E_DATABASE_ERROR;

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    if(dbrecord.empty())
    {
        //generate UUID
        std::string ruleID;
        generateUUID(ruleID);
        sql.clear();
        sql = "insert into T_RuleTime (RuleID,NumberID,StartTime,EndTime,StrategyID) values ('";
        sql += ruleID;
        sql += "','";
        sql += numberID;
        sql += "','";
        sql += startTime;
        sql += "','";
        sql += endTime;
        sql += "','";
        sql += buff;
        sql += "')";
        if( 0 != dbmysql_->executenonquery(sql))
        {
            opeResult = -1;
            this->setMinorNumberOpeRecord(numberID, SMSTIMESTRATEGY_SET,tmpChanelID,operatorNote,opeResult);
            return E_DATABASE_ERROR ;
        }
        else
        {
            opeResult = 0;
            this->setMinorNumberOpeRecord(numberID, SMSTIMESTRATEGY_SET,tmpChanelID,operatorNote,opeResult);
        }
        //.s  modify  by whc for sync the database when there is change. 2010-02-20
        result = this->setUnSynchronized(MINORNUMBER, minorNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }
        //.e modify  by whc for sync the database when there is change. 2010-02-20        
        return E_OPERATOR_SUCCED;
    }
    //.s add for avoid same setting bug by whc 2010-03-11
    DB_RECORDSET_ARRAY::iterator it = dbrecord.begin();
    string dbStrategyId = "";
    if(it != dbrecord.end())
    {
        DB_RECORDSET_ITEM::iterator it_ = (*it).find("strategyid");
        if(it_!=(*it).end())
        {
            dbStrategyId = it_->second.c_str();
        }
        else
        {
            return E_DATABASE_ERROR;
        }
    }
    //.e add for avoid same setting bug by whc 2010-03-11    
    operatorNote.clear();
    operatorNote = "Update SMS Strategy "+strBuff;

    sql.clear();
    sql = "update T_RuleTime set StrategyID = '";
    sql += buff;
    sql += "',";
    sql += "StartTime = '";
    sql += startTime;
    sql += "',";
    sql += "EndTime = '";
    sql += endTime;
    sql += "'";
    sql += "where NumberID = '";
    sql += numberID;
    sql += "' and StrategyID = '";
    sql += dbStrategyId+"';";

    if( 0 != dbmysql_->executenonquery(sql))
    {
        opeResult = -1;
        this->setMinorNumberOpeRecord(numberID, SMSTIMESTRATEGY_SET,tmpChanelID,operatorNote,opeResult);
        return E_DATABASE_ERROR ;
    }
    else
    {
        opeResult = 0;
        this->setMinorNumberOpeRecord(numberID, SMSTIMESTRATEGY_SET,tmpChanelID,operatorNote,opeResult);
    }
    //.s  modify  by whc for sync the database when there is change. 2010-02-20
    result = this->setUnSynchronized(MINORNUMBER, minorNumber);
    if(result == E_DATABASE_ERROR)
    {
        return E_DATABASE_ERROR;
    }
    //.e modify  by whc for sync the database when there is change. 2010-02-20    
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : getStrategyID 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to get the time linit policy for the pointed minor number .
*              
* INPUT      : minorNumber  const std::string & : Pointer the minor number.

*
* OUTPUT     : timeStrategy TimeLimitedStrategy&: limit time policy.The detail description refer to the 
*              define about TimeLimitedStrategy
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::getStrategyID(const std::string &minorNumber,TimeLimitedStrategy& timeStrategy,TimeLimitedType serviceType)
{
    std::string numberID;
    timeStrategy = TIMELIMITSTRATEGY_SELF;
    if (this->getNumberID(minorNumber,numberID)  == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    std::string sql = "";
    //.s add for different the sms and ivr time strategy by whc 2010-03-12.
    switch(serviceType)
    {
    case IVR_LIMITED:
        sql = "select StrategyID from T_RuleTime where  StrategyID >= 0 and StrategyID <= 9 and NumberID = '";
        break;
    case SMS_LIMITED:
        sql = "select StrategyID from T_RuleTime where  StrategyID >= 101 and StrategyID <= 103 and NumberID = '";
        break;
    default:
        return E_OPERATOR_SUCCED;            
    }
    //.e add for different the sms and ivr time strategy by whc 2010-03-12.
    sql += numberID;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
        return E_DATABASE_ERROR;
    if (dbrecord_.empty())
    {
		return E_DATABASE_NO_RECORD;
        //return E_OPERATOR_SUCCED;
    }
    std::string strategyid;
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("strategyid");
        if (it_ != item_.end())
        {
            strategyid = (*it_).second;
        }

        ++it;
    }
    timeStrategy = (TimeLimitedStrategy)atoi(strategyid.c_str());
    return E_OPERATOR_SUCCED;
}

ResultDataUsage DataUsage::getLimitTime(const string& minorNumber, TimeLimitedType serviceType, string& startTime, string& endTime)
{
	std::string numberID;
	if(getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
	{
		return E_MINORNUMBER_NOT_EXESIT;
	}
	std::string sql = "";
	switch(serviceType)
	{
	case IVR_LIMITED:
		sql = "select StartTime,EndTime from T_RuleTime where  StrategyID >= 0 and StrategyID <= 9 and NumberID = '";
		break;
	case SMS_LIMITED:
		sql = "select StartTime,EndTime from T_RuleTime where  StrategyID >= 101 and StrategyID <= 103 and NumberID = '";
		break;
	default:
		return E_OPERATOR_SUCCED;
	}
	sql += numberID;
	sql += "'";
	DB_RECORDSET_ARRAY dbrecord_;
	dbmysql_->executequery(sql,dbrecord_);
	if (dbrecord_.empty())
	{
		return E_OPERATOR_SUCCED;
	}

	DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
	while (it!=dbrecord_.end())
	{
		DB_RECORDSET_ITEM item_ = (*it);
		DB_RECORDSET_ITEM::iterator it_ = item_.find("starttime");
		if (it_ != item_.end())
		{
			startTime = (*it_).second;
		}
		it_ = item_.find("endtime");
		if (it_ != item_.end())
		{
			endTime = (*it_).second;
		}
		++it;
	}
}

/***********************************************************************************
*
* NAME       : setTimeBySelf 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to 
*              
* INPUT      : minorNumber  const std::string & : Pointer the minor number that need set time.
*              startTime    const std::string & : The limit time when start.
*              endTime      const std::string & : The limit time when end.
*
* OUTPUT     : None;
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The minor number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::setTimeBySelf(const std::string &minorNumber, const std::string &startTime, const std::string &endTime)
{
    std::string numberID;
    ResultDataUsage result;
    if (this->getNumberID(minorNumber,numberID)  == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    //To check whether the rule time has been set.
    std::string sql ="select * from T_RuleTime where NumberID = '";
    sql +=numberID;
    sql +="'";
    DB_RECORDSET_ARRAY dbrecord;
    if(0 != dbmysql_->executequery(sql,dbrecord))
        return E_DATABASE_ERROR;
    if (dbrecord.empty())
    {
        //generate UUID
        std::string ruleID;
        generateUUID(ruleID);
        sql.clear();
        sql = "insert into T_RuleTime (RuleID,NumberID,StartTime,EndTime) values ('";
        sql += ruleID;
        sql += "','";
        sql += numberID;
        sql += "','";
        sql += startTime;
        sql += "','";
        sql += endTime;
        sql += "')";
        if( 0 != dbmysql_->executenonquery(sql))
            return E_DATABASE_ERROR ;
        //.s  modify  by whc for sync the database when there is change. 2010-02-20
        result = this->setUnSynchronized(MINORNUMBER, minorNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }
        //.e modify  by whc for sync the database when there is change. 2010-02-20        
        return E_OPERATOR_SUCCED;
    }

    sql.clear();
    sql = "update T_RuleTime set StartTime = '";
    sql += startTime;
    sql += "',EndTime = '";
    sql += endTime;
    sql += "' where NumberID = '";
    sql += numberID;
    sql += "'";
    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR ;
    //.s  modify  by whc for sync the database when there is change. 2010-02-20
    result = this->setUnSynchronized(MINORNUMBER, minorNumber);
    if(result == E_DATABASE_ERROR)
    {
        return E_DATABASE_ERROR;
    }
    //.e modify  by whc for sync the database when there is change. 2010-02-20        
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : setMinorIndex 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to set the sequence about all the minor numbers which belong 
*              to the pointer major number.
*              
* INPUT      : majorNumber  const std::string &: The number is the major number which want to set the 
*                            minor numbers sequence..
*              minorIndexStr  const std::string & :The sequence string need to decode for setting.
*
* OUTPUT     : None;
*
* RETURN     : E_MAJORNUMBER_NOT_EXESIT: The major number is no exist.
*              E_DATABASE_ERROR :The database opeartion is failed;
*              E_SET_MINORNUMBER_SEQUE_ERROR: There is error when set the minor number sequence.
*              E_OPERATOR_SUCCED: The operation is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::setMinorIndex(const string& majorNumber,const std::string &minorIndexStr,OperateChannel inChanelID)
{
    std::string UserID;
    //Check the major number whether is exist.
    if (this->getUserID(majorNumber,UserID) == E_MAJORNUMBER_NOT_EXESIT)
    {
        return E_MAJORNUMBER_NOT_EXESIT;
    }
    std::string operatorNote ;
    short int opeResult = 0;
    std::string minorNumberId ="";

    //Get all  the minor numbers belong to the pointed major number.
    std::string sql = "select MinorNumber from T_MinorNumber where UserID = '";
    sql += UserID;
    sql += "' order by SequenceNo ASC";
    DB_RECORDSET_ARRAY dbrecord;
    if(0 != dbmysql_->executequery(sql,dbrecord))
        return E_DATABASE_ERROR;
    //The length is over the rule.
    int length = minorIndexStr.length();
    int count = dbrecord.size();
    if (length != count)
    {
        return E_SET_MINORNUMBER_SEQUE_ERROR;
    }
    //The value wide is over the rule. The rule wide :[1,3].
    const char* p = minorIndexStr.c_str();
    for (int i =0;i < length-1;i++)
    {
        if (p[i] > '3' || p[i] < '1')
        {
            return E_SET_MINORNUMBER_SEQUE_ERROR;
        }
    }

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    DB_RECORDSET_ARRAY::iterator it = dbrecord.begin();
    int i =0;
    while (it != dbrecord.end())
    {
        if (i >= length)
        {
            return E_OPERATOR_SUCCED;
        }
        DB_RECORDSET_ITEM::iterator it_ = (*it).begin();
        std::string minorNumber = it_->second;
        sql.clear();
        sql = "update T_MinorNumber set SequenceNo = '";
        //Decode the sequence string step by one.
        sql += minorIndexStr.substr(i,1);
        sql += "' where MinorNumber = '";
        sql += minorNumber;
        sql += "'";

        operatorNote = "Update seq "+minorIndexStr.substr(i,1);
        if( 0 != dbmysql_->executenonquery(sql))
        {
            opeResult = -1;
            this->setMinorNumberOpeRecord(minorNumberId, MINORNUMBERSEQ_SET,tmpChanelID,operatorNote,opeResult);
            return E_DATABASE_ERROR ;
        }
        else
        {
            opeResult = 0;    
        }
        minorNumberId.clear();
        this->getNumberID(minorNumber,minorNumberId);
        this->setMinorNumberOpeRecord(minorNumberId, MINORNUMBERSEQ_SET,tmpChanelID,operatorNote,opeResult);

        //.s  modify  by whc for sync the database when there is change. 2010-02-20
        ResultDataUsage result = this->setUnSynchronized(MINORNUMBER, minorNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }
        //.e modify  by whc for sync the database when there is change. 2010-02-20        
        ++i;
        ++it;
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : deleteVoiceFile 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used for user to delete the voice file that belong to the 
*              pointed minor number.
*              
* INPUT      : minorNumber  const std::string &: The number is pointed to delete it's voice file .
*              voiceFileName  const std::string & :The file name is pointed to be delete.
*
* OUTPUT     : None;
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT :The pointed number is no exist.
*              E_DATABASE_ERROR :        The database operation is fail.
*              E_OPERATOR_SUCCED:        Delete the voice file sucess. 
*
***********************************************************************************/ 
ResultDataUsage DataUsage::deleteVoiceFile(const std::string &minorNumber, const std::string &voiceFileName)
{
    string numberID;
    if (this->getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    //2009-8-25 by zhouqianren
    //Sensitive Field encrypt -----start----------------------
    unsigned char key[8];
    memset(key, 0 , sizeof(key));
    strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
    
    Crypt crypt;
    string encrypt_file = crypt.encrypt(key, (char *)voiceFileName.c_str()); 
    //Sensitive Field encrypt -----end----------------------

    string sql = "delete from T_VoiceMsgBox where NumberID = '";
    sql += numberID;
    sql += "'and RecFileName = '";
    sql +=encrypt_file;
    sql += "'";
    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR ;
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : isInVoiceList 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to check caller number whether is be turn to set voice message
*              when call the called number.If the caller is turn to ser voice box ,return 
*              E_IN_LIMITEIED_LIST_ERROR;
*              
* INPUT      : caller const std::string &. : The call session starter;
*              called const std::string &. : The number is called .
*
* OUTPUT     : None;
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT: The called number is no exist.
*              E_DATABASE_ERROR :        The database operation is fail
*              E_OPERATOR_SUCCED:        The caller could not be turn to the voice message box,caller 
*                                        could create normal call session.
*              E_IN_LIMITEIED_LIST_ERROR:Add the new user number infomation into table sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::isInVoiceList(const std::string &caller,const std::string &called)
{
    string numberId;
    if (this->getNumberID(called,numberId) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    string sql = "select AllowNumber from T_RuleRecord where NumberID = '";
    sql += numberId;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord;
    if(0 != dbmysql_->executequery(sql,dbrecord))
        return E_DATABASE_ERROR;
    if (dbrecord.empty())
    {
        return E_OPERATOR_SUCCED;
    }
    else
    {
        DB_RECORDSET_ARRAY::iterator it = dbrecord.begin();
        while (it != dbrecord.end())
        {
            DB_RECORDSET_ITEM::iterator it_ = (*it).find("allownumber");
            if (it_->second == caller)
            {
                return E_IN_LIMITEIED_LIST_ERROR;
            }
            
            ++it;
        }
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : saveCallRecord 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to save the call bill.
*              
* INPUT      : record   CallBill & : This is the call bill that need to save in database.
*
* OUTPUT     : None;
*
* RETURN     : E_DATABASE_ERROR  : The database operation is fail 
*              E_OPERATOR_SUCCED : It is sucessfull to save the call bill into the database.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::saveCallRecord(CallBill &record)
{
    //Formate the time recorded in call  bill.
    std::string startDate;
    std::string startTime;
    std::string dateTime = LonglongtoStr(record.startTime);

    startDate = dateTime.substr(0,dateTime.find_first_of(" "));
    startTime = dateTime.substr(dateTime.find_first_of(" ")+1);
    //To get the minor number sequence from the database. 
    std::string minorNumber;
    minorNumber = record.numberInfo.minorNumber;
    std::string majorNumber;
    int index = 0;
    this->getMinorIndex(minorNumber,majorNumber,index);
    //Filter the call record number by compare the call session time with the time rules.
    int callRecordNo;
    int calltime = record.callContinueTime;
    if (calltime <= 1800)
    {
        callRecordNo = 0;
    }
    if (calltime > 1800 && calltime <= 3600)
    {
        callRecordNo = 13;
    }
    if (calltime > 3600)
    {
        callRecordNo = 123;
    }
    char buff[20];
    char buff1[20];
    memset(buff,0,sizeof(buff));
    memset(buff1,0,sizeof(buff1));
    sprintf(buff,"%d",index);
    sprintf(buff1,"%d",callRecordNo);
    //Get the call type     
    // 0: Major number is as the caller ;1:others call the major number.
    char callType[10];
    memset(callType,0,sizeof(callType));
    sprintf(callType,"%d",record.numberInfo.callType);

    //
    char buffcontinueTime[100];
    memset(buffcontinueTime,0,sizeof(buffcontinueTime));
    sprintf(buffcontinueTime,"%d",record.callContinueTime);

    //.s Modify for the simm call watching by whc 20100716
    //convert the call result
    char buffCrResult[8];
    memset(buffCrResult,0,sizeof(buffCrResult));
    sprintf(buffCrResult,"%d",record.result);
    char buffCrType[8];
    memset(buffCrType,0,sizeof(buffCrType));
    sprintf(buffCrType,"%d",record.callInType);
    //.e Modify for the simm call watching by whc 20100716

    char buffCrReservel[32];
    memset(buffCrReservel,0,sizeof(buffCrReservel));
    sprintf(buffCrReservel,"%d",record.releaseValue);

    std::string device_ID = record.device_ID;
    //generate UUID
    std::string seqID;
    generateUUID(seqID);

    //2009-8-25 by zhouqianren
    //Sensitive Field encrypt -----start----------------------
    unsigned char key[8];
    memset(key, 0 , sizeof(key));
    strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
    Crypt crypt;

    string encrypt_major = crypt.encrypt(key, record.numberInfo.majorNumber); 
    strncpy(record.numberInfo.majorNumber, encrypt_major.c_str(),sizeof(record.numberInfo.majorNumber)-1);
    

    string encrypt_minor = crypt.encrypt(key, (char *)minorNumber.c_str()); 
    minorNumber.clear();
    minorNumber = encrypt_minor;

    string encrypt_original = crypt.encrypt(key, record.numberInfo.originalNumber);
    strncpy(record.numberInfo.originalNumber, encrypt_original.c_str(), sizeof(record.numberInfo.originalNumber)-1);
    //Sensitive Field encrypt -----end----------------------

    string sql = "insert into T_CallRecord (SEQID,CALL_TYPE,Msisdn,Submsisdn,Other_part,STARTDATE,STARTTIME,CALL_DURATION,DEVICE_ID,Sub_no,cdr_no,RouterType,Result,Reserve1) values ('";
    sql += seqID;
    sql += "', '";
    sql += callType;
    sql +="', '";
    sql += record.numberInfo.majorNumber;
    sql += "','";
    sql += minorNumber;
    sql += "','";
    sql += record.numberInfo.originalNumber;
    sql += "','";
    sql += startDate;
    sql += "','";
    sql += startTime;
    sql += "','";
    sql += buffcontinueTime;
    sql += "','";
    sql += device_ID;
    sql += "','";
    sql += buff;
    sql += "','";
    sql += buff1;
    //.s Modify for the simm call watching by whc 20100422
    sql += "','";
    sql += buffCrType;
    //.e Modify for the simm call watching by whc 20100422
    sql += "','";
    sql += buffCrResult;
    sql += "','";
    sql += buffCrReservel;
    sql += "')";

    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR ;
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : isInCallerLimitedList 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to check the phone number whether the caller number 
*              permit to create the call session.There is need to add the lock for access
*              the forbidden number list.
*              
* INPUT      : called  const string& :  The target number that caller want to call..
*
* OUTPUT     : None;
*
* RETURN     : E_IN_FORBID_NUMBER_LIST: The called number is forbidden to call by other.
*              E_OPERATOR_SUCCED:       The called number is no limit..
*
***********************************************************************************/ 
ResultDataUsage DataUsage::isInCallerLimitedList(const string& called)
{
    int length = called.length();
    BTree* bTree = NULL; 

    /* LOCK */
    MyGuard myGuard(&(myMutex_));
    bTree = limitCalledNumberRecord_[length];
    if (bTree != NULL)
    {
        if (bTree->FindNumber(called) == 0)
        {
            return E_IN_FORBID_NUMBER_LIST;
        }        
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : loadLocationInfo 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to get the mapping about the minor number 
*              and IMSI(user unique identity)
*
* INPUT      : None.
*
* OUTPUT     : locationInfo   std::map<std::string,std::string>&.
*                       The struct is used keep the relation minor number and IMSI.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT:The number formate is no available.
*              E_DATABASE_ERROR :       The database operation is fail
*              E_OPERATOR_SUCCED:       Add the new user number infomation into table sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::loadLocationInfo(std::map<std::string,std::string>& locationInfo)
{
    std::string sql = "select MinorNumber,IMSI from T_MinorNumber ORDER BY LastUpdateTime ASC limit 1";
    DB_RECORDSET_ARRAY dbRecord;
    if(0 != dbmysql_->executequery(sql,dbRecord))
        return E_DATABASE_ERROR;

    if (dbRecord.empty())
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
    while (it != dbRecord.end())
    {
        DB_RECORDSET_ITEM::iterator it_ = (*it).find("minornumber");
        std::string minorNumber = it_->second;
        it_ = (*it).find("imsi");
        std::string iMSI = it_->second;
        locationInfo[minorNumber] = iMSI;
        ++it;

    }
    LogFileManager::getInstance()->write(Debug,"E_OPERATOR_SUCCED....");
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : setUpdateTime 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to set the updating time about the pointer minor number.
*
* INPUT      : minorNumber  const std::string & : The minor number need to set the updating time.
*
* OUTPUT     : none.
*
* RETURN     : E_DATABASE_ERROR:    The database operation is fail.
*              E_OPERATOR_SUCCED:   Add the new user number infomation into table sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::setUpdateTime(/*std::map<std::string,std::string> &updateRecord*/
                                         std::string& minorNumber)
{
    std::string sql;
    sql = "update T_MinorNumber set IsSynchronization = 0, LastUpdateTime = now() where MinorNumber = '";
    sql += minorNumber;
    sql += "'";
    if (0 != dbmysql_->executenonquery(sql))
    {
        LogFileManager::getInstance()->write(Brief,"Data Base Error!");
        return E_DATABASE_ERROR;
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : addNewMinorNumber 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to add the new minor number into the minor group of the major number.
*              First check the minor number whether is available;
*              Second check the major number whether is registered;
*              If the major number exist,allocate the minor number to it.
*
* INPUT      : majorNumber  const std::string & : The number as the major number that could add new minor number.
*              minorNumber  const std::string & : The number is the new minor number should arrange to the major number.
*              numberType :
*
* OUTPUT     : none.
*
* RETURN     : E_NUMBER_LENGTH_ERROR:    The number formate is no available.
*              E_DATABASE_ERROR:         The database operation is fail
*              E_MINORNUMBER_ALREADY_REGISTER: The minor number belong another ,so could not
*                                        allocate to new major number as the same time. 
*              E_MAJORNUMBER_NOT_EXESIT: The major number is not registered the service.
*              E_OPERATOR_SUCCED:   Add the new user number infomation into table sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::addNewMinorNumber(const std::string &majorNumber, const std::string &minorNumber,
											 const std::string & numIMSI,const std::string & numberType,OperateChannel inChanelID)
{
    //First : To check the available of the number that want to be added as the minor number.
    bool isVaild = VaildDialNumber(minorNumber);
    if (!isVaild)
    {
        return E_NUMBER_LENGTH_ERROR;
    }

    //Second check the major number whether is exist.
    std::string sql;
    std::string userID;
    ResultDataUsage ret = this->getUserID(majorNumber,userID);
    //generate UUID
    std::string seqID;
    generateUUID(seqID);
    if (ret == E_OPERATOR_SUCCED)
    {
        //The major number is exist.
        sql.clear();
        sql = "select UserID from T_MinorNumber where MinorNumber = '";
        sql += minorNumber;
        sql += "'";
        //Check the minor number .
        DB_RECORDSET_ARRAY minorRecord;
        if(0 != dbmysql_->executequery(sql,minorRecord))
        {
            return E_DATABASE_ERROR;
        }

        if (!minorRecord.empty()) 
        {
            //The number want to be add has registerd.
            DB_RECORDSET_ARRAY::iterator it = minorRecord.begin();
            int checkRet = 0;
            while (it != minorRecord.end())
            {
                DB_RECORDSET_ITEM item_ = (*it);
                DB_RECORDSET_ITEM::iterator it_ = item_.find("userID");
                if(it_ != item_.end())
                {
                    string  minorvalue_ = (*it_).second;
                    if(minorvalue_ == userID)
                    {
                        checkRet = 1;
                        break;
                        return E_MINORNUMBER_ALREADY_REGISTER;
                    }
                    else
                    {
                        sql.clear();
                        sql = "select * from T_RegisterUser where UserID = '";
                        sql += minorvalue_;
                        sql += "'";
                        //Check the minor number .
                        DB_RECORDSET_ARRAY userIDCH;
                        if(0 != dbmysql_->executequery(sql,userIDCH))
                        {
                            return E_DATABASE_ERROR;
                        }
                        if(!userIDCH.empty())
                        {
                            checkRet = 1;
                            break;
                        }
                    }
                }
                ++it;
            }
            //The number has register as minor number for another major number. 
            if(checkRet == 1)
            {
                return E_MINORNUMBER_ALREADY_REGISTER; 
            }
        }

        //To get the minor number sequence.
        std::vector<MinorNumberAttr> allMinorNumber;
        this->getAllMinorNumber(majorNumber, allMinorNumber);

        int minorNumSize = allMinorNumber.size();
        int sequenceNo = 0;
        char buffSeq[10];
        memset(buffSeq,0,sizeof(buffSeq));

        if(minorNumSize != 0)
        {
            if(minorNumSize >=3 )
            {
                return E_DATABASE_ERROR;
            }
            int flag = 0;
            for(int i =1; i<= 3;i++)
            {
                flag = 0;
                for(int j=0;j<allMinorNumber.size();j++)
                {
                    if(allMinorNumber[j].sequenceNo == i)
                    {
                        flag = 1;
                        break;
                    }
                }
                if(flag == 0)
                {
                    sprintf(buffSeq,"%d",i);
                    break;
                }
            }      
        }
        else
        {
            sprintf(buffSeq,"%d",1);
        }

        std::string registerType;

        //The state of the minor number.
        int stateID = (int)STATE_ACTIVE;
        char buffStateID[10];
        memset(buffStateID,0,sizeof(buffStateID));
        sprintf(buffStateID,"%d",stateID);

        OperateChannel tmpChanelID = this->confirmChannel(inChanelID);
        char operateChinal[8];
        memset(operateChinal,0,sizeof(operateChinal));
        sprintf(operateChinal,"%d",tmpChanelID);


        //Construct the sql content of the minor number infomation .

        //.start add the filed of the table  by whc 20100208.
        sql = "insert into T_MinorNumber ( NumberID,MinorNumber,UserID,Type,SequenceNo,RegisterTime," ;
        sql +=  "StateID,IMSI,ZoneCode,LastUpdateTime,OperateChannelID,RecordRule) values('";
        //.end add the filed of the table  by whc 20100208.
        sql += seqID;
        sql += "','";
        sql += minorNumber;
        sql += "','";
        sql += userID;
        sql += "','";
        sql += numberType;
        sql += "','";
        sql += buffSeq;
        sql += "', now(),'";
        sql += buffStateID;
        sql += "','";
        sql += numIMSI;
        sql += "','";
        sql += localZoneCode_;
        sql += "',now(),'";
        sql += operateChinal;
        sql += "','0')";
        //Add the minor number infomation into the database.
        if( 0 != dbmysql_->executenonquery(sql))
            return E_DATABASE_ERROR ;
    }
    
    //The major number input is no exist.
    if (ret != E_OPERATOR_SUCCED)
    {
        return E_MAJORNUMBER_NOT_EXESIT; 
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : deleteMinorNumber 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to delete the pointed minor number from the table. 
*
* INPUT      : majorNumber  const std::string &: The number is the major number of the
*                           minor number that need to delete.
*              minorNumber  const std::string &: The number is need to delete..
*
* OUTPUT     : none.
*
* RETURN     : E_DATABASE_ERROR:    The database operation is fail.
*              E_MAJORNUMBER_NOT_EXESIT: The major number is not registered.
*              E_OPERATOR_SUCCED:   The operation that to delete the pointed minor number is sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::deleteMinorNumber(const std::string &majorNumber, const std::string &minorNumber)
{
    //To check the number whether is on the center database.
    std::string numberId = "";
    string sqlResult[12];
    string fieldNames[12] ={"numberid","minornumber","userid","type","sequenceno","registertime","registertype","stateid","imsi","operatechannelid","recordrule","zonecode"};
    std::string chkSql = "select NumberID,MinorNumber,UserID,Type,SequenceNo,RegisterTime,RegisterType,StateID,IMSI,OperateChannelID,RecordRule,ZoneCode from T_MinorNumber where MinorNumber = '";
    chkSql += minorNumber + "' ;";
    DB_RECORDSET_ARRAY dbrecord_;
    //To init the array
     for(int loopSR =0;loopSR <12;loopSR++)
    {
        sqlResult[loopSR]="";
    }
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumber] bak T_MinorNumber error!");        
        return E_DATABASE_ERROR;
    }    
    //If there is no unynchronized data on the local databace server.
    if (dbrecord_.empty())
    {
        return  E_OPERATOR_SUCCED; 
    }
    else
    {
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        while (it!=dbrecord_.end())
        {
            DB_RECORDSET_ITEM item_ = (*it);
            for(int loopI=0;loopI<12;loopI++)
            {
                DB_RECORDSET_ITEM::iterator itIn_ = item_.find(fieldNames[loopI].c_str());
                sqlResult[loopI]=(*itIn_).second;
            }
            ++it;
        }
    }
    numberId = sqlResult[0];
    //step2 To release all the white list number.
    std::string delSql = "";
    delSql = "delete from T_RuleWhiteList where NumberID = '";
    delSql += numberId;
    delSql += "'";
    if( 0 != dbmysql_->executenonquery(delSql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumber] T_RuleWhiteList error!");        
        return E_DATABASE_ERROR ;
    }
    //step3 To release all the Black list number.
    delSql = "delete from T_RuleBlackList where NumberID = '";
    delSql += numberId;
    delSql += "'";
    if( 0 != dbmysql_->executenonquery(delSql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumber] T_RuleBlackList error!");        
        return E_DATABASE_ERROR ;
    }
    //step3 To release the record rule from the record rule table with the minor number.
    delSql = "";
    delSql = "delete from T_RuleRecord where NumberID = '";
    delSql += numberId;
    delSql += "'";
    
    if( 0 != dbmysql_->executenonquery(delSql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumber] T_RuleRecord error!");        
        return E_DATABASE_ERROR ;
    }
    // step4 To release the record rule from the record rule table with the minor number.
    delSql = "";
    delSql = "delete from T_RuleTime where NumberID = '";
    delSql += numberId;
    delSql += "'";
    if(0 != dbmysql_->executequery(delSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumber] T_RuleTime error!");        
        return E_DATABASE_ERROR;
    }

    // step5 To release the user operation record about the minor number.
    delSql = "";
    delSql = "delete from T_MinorNumberOperateRecord where NumberID = '";
    delSql += numberId;
    delSql += "'";
    if(0 != dbmysql_->executequery(delSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumber] T_MinorNumberOperateRecord error!");        
        return E_DATABASE_ERROR;
    }
    // step6 To release the user operation record about the minor number.
    delSql = "";
    delSql = "delete from T_ProPayMinorNumber where NumberID = '";
    delSql += numberId;
    delSql += "'";
    if(0 != dbmysql_->executequery(delSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumber] T_ProPayMinorNumber error!");        
        return E_DATABASE_ERROR;
    }

    //.s add the user number info into the t_registeruser_bk table. by whc 20100428
    dbrecord_.clear();
    string sqlMajorResult[8];
    string fieldMajorNames[8] ={"userid","usernumber","imsi","username","pwd","zonecode","registeredtime","operatechannelid"};
    std::string chkMajorSql = "select UserID,UserNumber,IMSI,UserName,PWD,ZoneCode,RegisteredTime,OperateChannelID from T_RegisterUser where UserNumber = '";
    chkMajorSql += majorNumber;
    chkMajorSql += "';";
    //To initalize the array. 
    for(int loopSMR =0;loopSMR <8;loopSMR++)
    {
        sqlMajorResult[loopSMR]=" ";
    }
    if(0 != dbmysql_->executequery(chkMajorSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumber] T_RegisterUser select error!");        
        return E_DATABASE_ERROR;
    }    
    //If there is no unynchronized data on the local databace server.
    if (dbrecord_.empty())
    {
        LogFileManager::getInstance()->write(Debug,"Debug INFO: [deleteMinorNumber] T_RegisterUser select db error!");
        return  E_DATABASE_ERROR; 
    }
    else
    {

        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        while (it!=dbrecord_.end())
        {
            DB_RECORDSET_ITEM item_ = (*it);
            for(int loopI=0;loopI<8;loopI++)
            {
                DB_RECORDSET_ITEM::iterator itIn_ = item_.find(fieldMajorNames[loopI].c_str());
                sqlMajorResult[loopI]=(*itIn_).second;
            }
            ++it;
        }
    }
    //To check there has the recod about the major number
    int chkFlag = 0;
    dbrecord_.clear();
    chkMajorSql.clear();
    chkMajorSql = "select UserID from t_registeruser_bak where UserID='"+sqlMajorResult[0]+"';";
    if(0 != dbmysql_->executequery(chkMajorSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumber] t_registeruser_bak select error!");        
        return E_DATABASE_ERROR;
    }    
    if (!dbrecord_.empty())
    {
        DB_RECORDSET_ARRAY::iterator itMajor = dbrecord_.begin();
        if(itMajor!=dbrecord_.end())
        {
            DB_RECORDSET_ITEM item_ = (*itMajor);
            DB_RECORDSET_ITEM::iterator itMajorIn_ = item_.find("userid");
            if((*itMajorIn_).second == sqlMajorResult[0])
            {
                LogFileManager::getInstance()->write(Debug,"Debug INFO: [deleteMinorNumber] userID[%s] User register bak has exist!",sqlMajorResult[0].c_str());
                chkFlag =1;
            }
        }
    }
    if(chkFlag == 0)
    {
        //Step To inser the minor number into the minor number bak table.
        std::string  inserMajorBkSql="insert into t_registeruser_bak (UserID,UserNumber,IMSI,UserName,PWD,BakTime,ZoneCode,RegisteredTime,OperateChannelID) values('";
        inserMajorBkSql += sqlMajorResult[0];
        inserMajorBkSql += "','";
        inserMajorBkSql += sqlMajorResult[1];
        inserMajorBkSql += "','";
        inserMajorBkSql += sqlMajorResult[2];
        inserMajorBkSql += "','";
        inserMajorBkSql += sqlMajorResult[3];
        inserMajorBkSql += "','";
        inserMajorBkSql += sqlMajorResult[4];
        inserMajorBkSql += "',";
        inserMajorBkSql += "now(),'";
        inserMajorBkSql += sqlMajorResult[5];
        inserMajorBkSql += "','";
        inserMajorBkSql += sqlMajorResult[6];
        inserMajorBkSql += "','";
        inserMajorBkSql += sqlMajorResult[7];
        inserMajorBkSql += "');";
        if(0 != dbmysql_->executenonquery(inserMajorBkSql))
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumber] database inserMajorBkSql operation error!");        
            return E_DATABASE_ERROR;
        }
    }
    //.e add the user number info into the t_registeruser_bk table. by whc 20100428

    //Step7 To inser the minor number into the minor number bak table.
    std::string  inserSql="insert into t_minornumber_bak (NumberID,MinorNumber,UserID,Type,SequenceNo,RegisterTime,RegisterType,StateID,IMSI,OperateChannelID,RecordRule,BakTime,ZoneCode) values('";
    inserSql += sqlResult[0];
    inserSql += "','";
    inserSql += sqlResult[1];
    inserSql += "','";
    inserSql += sqlResult[2];
    inserSql += "','";
    inserSql += sqlResult[3];
    inserSql += "','";
    inserSql += sqlResult[4];
    inserSql += "','";
    inserSql += sqlResult[5];
    inserSql += "','";
    inserSql += sqlResult[6];
    inserSql += "','";
    inserSql += sqlResult[7];
    inserSql += "','";
    inserSql += sqlResult[8];
    inserSql += "','";
    inserSql += sqlResult[9];
    inserSql += "','";
    inserSql += sqlResult[10];
    inserSql += "',";
    inserSql += "now(),'";
    inserSql += sqlResult[11];
    inserSql += "');";
    if(0 != dbmysql_->executenonquery(inserSql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: %s",inserSql.c_str());
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumber] database operation error!");        
        return E_DATABASE_ERROR;
    }
    //Step6 To delete the minor number from the number table.
    delSql = "";
    delSql = "delete from T_MinorNumber where MinorNumber = '";
    delSql += minorNumber + "' ;";
    if(0 != dbmysql_->executenonquery(delSql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumber] database operation error!");        
        return E_DATABASE_ERROR;
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : setMinorNumberSequence 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to set the sequence of the pointed minor number in the 
*              minor number group.And the value set should bigger then 0 and less than 4;
*
* INPUT      : minorNumber  const std::string & : The number that need to set sequence .
*              numberSequence  int: The value want to be set.
*
* OUTPUT     : none.
*
* RETURN     : E_DATABASE_ERROR:    The database operation is fail.
*              E_MINORNUMBER_NOT_EXESIT:
*              E_SET_MINORNUMBER_SEQUE_ERROR:
*              E_OPERATOR_SUCCED:   Add the new user number infomation into table sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::setMinorNumberSequence(const std::string& minorNumber,int numberSequence,OperateChannel inChanelID)
{
    ResultDataUsage result = E_OPERATOR_SUCCED;
    if (numberSequence <1 || numberSequence >3)
    {
        return E_SET_MINORNUMBER_SEQUE_ERROR;
    }

    //To get the sequence of the input minor number and get the register major number.
    std::string majorNumber;
    int sequence;
    ResultDataUsage ret = this->getMinorIndex(minorNumber,majorNumber,sequence);
    if (ret != E_OPERATOR_SUCCED)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    
    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    std::string operatorNote ="Update SequenceNo " ;
    short int opeResult = 0;
    std::string minorNumberId ="";    

    //To get the minor number with the input sequence number and major number.
    std::string outMinorNumber;
    ret = this->getMinorNumber(majorNumber,numberSequence,outMinorNumber);
    
    char buff[10];
    memset(buff,0,sizeof(buff));
    sprintf(buff,"%d",numberSequence);
    operatorNote += buff;
    //Modify the sequence of the minor number.
    //If there is no item with the sequence in the database,update the minor sequence with the input sequence value.
    if (ret == E_MINORINDEX_NUMBER_NOT_EXESIT)
    {
        std::string sql;
        //.s modify  by whc for sync the database when there is change. 2010-02-20
        //sql = "update T_MinorNumber set SequenceNo = '";
        sql = "update T_MinorNumber set IsSynchronization = 0, SequenceNo = '";
        //.e modify  by whc for sync the database when there is change. 2010-02-20
        sql += buff;
        sql += "' where MinorNumber = '";
        sql += minorNumber;
        sql += "'";

        if( 0 != dbmysql_->executenonquery(sql))
        {
            opeResult = -1;
            result = E_DATABASE_ERROR ;
        }
        else
        {
            opeResult = 0;
        }
    }
    else
    {
        //If there is item with the sequence in the database,update the minor sequence with the input value.      

        char buffsequence[10];
        memset(buffsequence,0,sizeof(buffsequence));
        sprintf(buffsequence,"%d",sequence);
        std::string sql,sql1;
        //.s modify  by whc for sync the database when there is change. 2010-02-20
        sql = "update T_MinorNumber set IsSynchronization = 0, SequenceNo = '";
        //.e modify  by whc for sync the database when there is change. 2010-02-20        
        sql += buff;
        sql += "' where MinorNumber = '";
        sql += minorNumber;
        sql += "'";

        //And update another minor sequence with the original index of the input number. 
        //.s modify  by whc for sync the database when there is change. 2010-02-20
        sql1 = "update T_MinorNumber set IsSynchronization = 0, SequenceNo = '";
        //.e modify  by whc for sync the database when there is change. 2010-02-20    

        sql1 += buffsequence;
        sql1 += "' where MinorNumber = '";
        sql1 += outMinorNumber;
        sql1 += "'";

        if( 0 != dbmysql_->executenonquery(sql))
        {
            opeResult = -1;
            result = E_DATABASE_ERROR ;
        }
        else
        {
            if( 0 != dbmysql_->executenonquery(sql1))
            {
                opeResult = -1;
                result = E_DATABASE_ERROR ;
            }
            else
            {
                opeResult = 0;
            }
        }

    }
    this->getNumberID(minorNumber,minorNumberId);
    this->setMinorNumberOpeRecord(minorNumberId, MINORNUMBERSEQ_SET,tmpChanelID,operatorNote,opeResult);    
    return result;
}

/***********************************************************************************
*
* NAME       : addMajorNumber 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used for new user register.The user's phone number infomation
*              should be inserted into database.
*
* INPUT      : majorNumber  const std::string & : The user's phone number that is used 
*                           to register.
*              user  const std::string&  : The name of the user used to register.
*              passwd const std::string& : The password set when the user register.
*              IMSI  const std::string& : The unique identity of the User's phone number.
*
* OUTPUT     : none.
*
* RETURN     : E_DATABASE_ERROR:    The database operation is fail.
*              E_OPERATOR_SUCCED:   Add the new user number infomation into table sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::addMajorNumber(const std::string& majorNumber,const std::string& user,
                               const std::string& passwd,const std::string& IMSI,OperateChannel inChanelID)
{
    //Check the input number whether has register as major number.
    std::string sql = "select * from T_RegisterUser where UserNumber = '";
    sql += majorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY majorNumberRecord;
    if( 0 != dbmysql_->executequery(sql,majorNumberRecord))
        return E_DATABASE_ERROR ;

    if (!majorNumberRecord.empty())
    {
        return E_OPERATOR_SUCCED; 
    }

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);
    char operateChinal[8];
    memset(operateChinal,0,sizeof(operateChinal));
    sprintf(operateChinal,"%d",tmpChanelID);

    //generate UUID
    std::string seqID;
    generateUUID(seqID);

    sql.clear();
    sql = "insert into T_RegisterUser(UserID,UserNumber,UserName,PWD,IMSI,ZoneCode,OperateChannelID,RegisteredTime) values('";
    sql += seqID;
    sql += "','";
    sql += majorNumber;
    sql += "','";
    sql += user;
    sql += "','";
    sql += passwd;
    sql += "','";
    sql += IMSI;
    sql += "','";
    sql += localZoneCode_;
    sql += "','";
    sql += operateChinal;
    sql += "',now())";
    //Insert the record item into the database table.
    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR ;
    return E_OPERATOR_SUCCED; 
}

/***********************************************************************************
*
* NAME       : svc 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to get all the forbidden number and 
*
* INPUT      : minorNumber  const std::string & : The unique user identity.
*
* OUTPUT     : updateTime  const std::string &  : The time that the minor number be updated.
*
* RETURN     : E_DATABASE_ERROR:    The database operation is fail.
*              E_OPERATOR_SUCCED:  Sucess.
*
***********************************************************************************/ 
int DataUsage::svc()
{
    DB_RECORDSET_ARRAY dbRecord;
    std::string sql = "select ForbidNumber from T_ForbidNumber";
    if( 0 == dbMysql_fb_.executequery(sql,dbRecord))
    {
        /* LOCK */
        MyGuard myGuard(&(myMutex_));
        int recordSize = limitCalledNumberRecord_.size();
        BTree* pTree = NULL;
        for (int i=0;i<recordSize;i++ )
        {
            pTree = limitCalledNumberRecord_[i];
            if (pTree != NULL)
            {
                delete pTree;
            }    
        }
        limitCalledNumberRecord_.clear();
        for (int i =1; i<= 11;i++) //To cut the max length number.
        {
            BTree* treeTemp = new BTree();
            if (treeTemp != NULL)
            {
                limitCalledNumberRecord_[i]=treeTemp;
            }
        }

        DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
        BTree* bTree = NULL;
        while (it != dbRecord.end())
        {
            DB_RECORDSET_ITEM& item = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item.begin();
            bTree = limitCalledNumberRecord_[((*it_).second).length()];
            if (bTree == NULL)
            {
                ++it;
                continue;
            }

            bTree->PutInto((*it_).second);
            ++it;
        }
    }
    
    /* Wait for a Moment */
    ThreadSleep(3600);
    return 0;
}

/***********************************************************************************
*
* NAME       : GetMinorUpdateTime 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : The function is used to get the last time when user update the pointed 
*              minor number.        
*
* INPUT      : minorNumber  const std::string & : The unique user identity.
*
* OUTPUT     : updateTime  const std::string &  : The time that the minor number be updated.
*
* RETURN     : E_DATABASE_ERROR:    The database operation is fail.
*              E_OPERATOR_SUCCED:  Sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::GetMinorUpdateTime(std::string &minorNumber,std::string &updateTime)
{
    std::string sql = "select LastUpdateTime from T_MinorNumber where MinorNumber = '";
    sql += minorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbRecord;
    if( 0 != dbmysql_->executequery(sql,dbRecord))
        return E_DATABASE_ERROR;

    DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
    while (it != dbRecord.end())
    {
        DB_RECORDSET_ITEM& item = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item.begin();
        updateTime = it_->second;
        ++it;
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : GetMinorNumberFromIMSI 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : Get UserID,the minor number,and minor number sequence according with the IMSI from
*              database table. If the operation sucess,try to get the major number from 
*              T_RegisterUser table according with the UserID.
*              
*
* INPUT      : IMSI const std::string & : The unique user identity.
*
* OUTPUT     : minorNumber  string &  : The minor number relation to the IMSI
*              Seq  int&  :             The minor number's sequence.
*              MajorNumber std::string& :The minor number's user number which registered as the major number.
*
* RETURN     : E_DATABASE_ERROR:    The database operation is fail.
*              E_OPERATOR_SUCCED:  Sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::GetMinorNumberFromIMSI(const std::string &IMSI, string & minorNumber, 
                                                  int& Seq,
                                                  std::string& MajorNumber)
{
    std::string userID;
    std::string sql = "select UserID,MinorNumber,SequenceNo from T_MinorNumber where IMSI = '";
    sql += IMSI;
    sql += "'";
    DB_RECORDSET_ARRAY dbRecord;
    if( 0 != dbmysql_->executequery(sql,dbRecord))
        return E_DATABASE_ERROR;

    DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
    while (it != dbRecord.end())
    {
        DB_RECORDSET_ITEM& item = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item.find("userid") ;
        userID = it_->second;

        it_ = item.find("sequenceno");
        Seq = atoi((it_->second).c_str());

        it_ = item.find("minornumber");
        minorNumber = it_->second;

        ++it;
    }
    
    sql.clear();
    sql = "select UserNumber from T_RegisterUser where UserID = '";
    sql += userID;
    sql += "'";
    dbRecord.clear();
    if(0 != dbmysql_->executequery(sql,dbRecord))
        return E_DATABASE_ERROR;
    it = dbRecord.begin();
    while (it != dbRecord.end())
    {
        DB_RECORDSET_ITEM& item = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item.begin();
        MajorNumber = it_->second;
        ++it;
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : GetShouldUpdateItem 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : 
*
* INPUT      : locationInfo std::map<std::string,std::string>& : The maping between minor number 
*                                and the IMSI.
*              interVailueTime  int  :
*
* OUTPUT     : None.
*
* RETURN     : E_DATABASE_ERROR:    The database operation is fail.
*              E_OPERATOR_SUCCED:  Sucess.
*              other           : Other result of database operation.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::GetShouldUpdateItem(std::map<std::string,std::string>& locationInfo, int interVailueTime )
{
    string weekVailueTime = "604800"; //7*24*60*60
    char buff[100];
    memset(buff,0,sizeof(buff));
    sprintf(buff,"%d",interVailueTime);
    std::string sql = "select MinorNumber,IMSI from T_MinorNumber where (unix_timestamp(now()) - unix_timestamp(LastUpdateTime )) > '";
    sql += buff;
    sql += "'";
    sql += "and (unix_timestamp(now()) - unix_timestamp(RegisterTime)) < '";
    sql += weekVailueTime;
    sql += "'";
    sql += "and Type <> 3 and LENGTH(IMSI)=15 ORDER BY LastUpdateTime ASC limit 10";
    DB_RECORDSET_ARRAY dbRecord;
    if( 0 != dbmysql_->executequery(sql,dbRecord))
        return E_DATABASE_ERROR;

    if (dbRecord.empty())
    {
        //注册时间超7天的号码更新间隔周期为7天
        sql.clear();
        sql = "select MinorNumber,IMSI from T_MinorNumber where (unix_timestamp(now()) - unix_timestamp(LastUpdateTime )) > '";
        sql += weekVailueTime;
        sql += "'";
        sql += "and (unix_timestamp(now()) - unix_timestamp(RegisterTime)) >= '";
        sql += weekVailueTime;
        sql += "'";
        sql += "and Type <> 3 and LENGTH(IMSI)=15 ORDER BY LastUpdateTime ASC limit 10";
        if( 0 != dbmysql_->executequery(sql,dbRecord))
            return E_DATABASE_ERROR;
        
        if (dbRecord.empty())
        {
            return E_MINORNUMBER_NOT_EXESIT;
        }
    }

    DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
    while (it != dbRecord.end())
    {
        DB_RECORDSET_ITEM::iterator it_ = (*it).find("minornumber");
        std::string minorNumber = it_->second;
        it_ = (*it).find("imsi");
        std::string iMSI = it_->second;
        locationInfo[minorNumber] = iMSI;
        ++it;

    }
    return E_OPERATOR_SUCCED;
}


ResultDataUsage DataUsage::getOpenSMSminorIMSI(std::map<std::string,std::string> & mapInfo )
{
	string  strSql = "select T_MinorNumber.IMSI from T_MinorNumber,T_RuleTime where T_MinorNumber.NumberID = T_RuleTime.NumberID and T_RuleTime.StrategyID=103";
	DB_RECORDSET_ARRAY dbRecord;
	if( 0 != dbmysql_->executequery(strSql,dbRecord))
		return E_DATABASE_ERROR;

	
	 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
	 while (it != dbRecord.end())
	 {
		 DB_RECORDSET_ITEM::iterator it_ = (*it).find("imsi");
		 if ( it_ != it->end())
		 {
			std::string iMSI = it_->second;
			mapInfo[iMSI]=iMSI ;
		 }

		 ++it;
	 }

	  return E_OPERATOR_SUCCED;

}


ResultDataUsage DataUsage::getOpenSMSminor(std::vector<std::string> & vecMinorInfo )
{
		string  strSql = "select T_MinorNumber.MinorNumber from T_MinorNumber,T_RuleTime where T_MinorNumber.NumberID = T_RuleTime.NumberID and T_RuleTime.StrategyID=103";
	DB_RECORDSET_ARRAY dbRecord;
	if( 0 != dbmysql_->executequery(strSql,dbRecord))
		return E_DATABASE_ERROR;

	
	 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
	 while (it != dbRecord.end())
	 {
		 DB_RECORDSET_ITEM::iterator it_ = (*it).find("minornumber");
		 if ( it_ != it->end())
		 {
				std::string strMinor = it_->second;
				vecMinorInfo.push_back(strMinor);
		 }

		 ++it;
	 }

	  return E_OPERATOR_SUCCED;
}


ResultDataUsage DataUsage::getMinorNumberZoneInfo(std::map<std::string,std::string> & mapInfo )
{
	string  strSql = "select MinorNumber ,ZoneCode  from T_MinorNumber";
	DB_RECORDSET_ARRAY dbRecord;
	if( 0 != dbmysql_->executequery(strSql,dbRecord))
		return E_DATABASE_ERROR;
	
	 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
	 DB_RECORDSET_ITEM::iterator it_ ;
	 std::string strMinor="";
	 std::string strZone="";
	 while (it != dbRecord.end())
	 {
		 it_ = (*it).find("minornumber");
		 if ( it_ != it->end())
		 {
			 strMinor = it_->second;
		 }

		 it_ = (*it).find("zonecode");
		 if ( it_ != it->end())
		 {
			strZone = it_->second;
		 }

		 if( strMinor !="" && strZone != "")
		 {
			mapInfo[strMinor]=strZone;
		 }

		 ++it;
	 }	 


	return E_OPERATOR_SUCCED;


}



ResultDataUsage DataUsage::getBakMinorNumberZoneInfo(std::map<std::string,std::string> & mapInfo )
{

	string  strSql = "select MinorNumber ,ZoneCode  from t_minornumber_bak ";
	DB_RECORDSET_ARRAY dbRecord;
	if( 0 != dbmysql_->executequery(strSql,dbRecord))
		return E_DATABASE_ERROR;
	
	 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
	 DB_RECORDSET_ITEM::iterator it_ ;
	 std::string strMinor="";
	 std::string strZone="";
	 while (it != dbRecord.end())
	 {
		 it_ = (*it).find("minornumber");
		 if ( it_ != it->end())
		 {
			 strMinor = it_->second;
		 }

		 it_ = (*it).find("zonecode");
		 if ( it_ != it->end())
		 {
			strZone = it_->second;
		 }

		 if( strMinor !="" && strZone != "")
		 {
			mapInfo[strMinor]=strZone;
		 }

		 ++it;
	 }	 


	return E_OPERATOR_SUCCED;

}


ResultDataUsage DataUsage::setMinorNumberZoneInfo2Table(std::map<std::string,std::string> & mapInfo)
{

	std::string  strSql="insert into tmp_minor_pool_fj (MinorNumber,MinorNumberCrypt,ZoneCode) values ('";
	std::map<std::string,std::string>::iterator it ;
	unsigned char key[8];
    memset(key, 0 , sizeof(key));
    strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
    Crypt crypt;
    string encrypt_minor = "";
	int  i = 0 ;
	for(it = mapInfo.begin(); it!= mapInfo.end();it++)
	{
		i++;
		encrypt_minor= crypt.encrypt(key, (char *)(it->first).c_str()); 
		strSql="insert into tmp_minor_pool_fj (MinorNumber,MinorNumberCrypt,ZoneCode) values ('";
		strSql += it->first;
		strSql += "','";
		strSql += encrypt_minor;
		strSql += "','";	
		strSql += it->second;
		strSql += "')";

		if( 0 != dbmysql_->executenonquery(strSql))
		{
			printf("exec sql error");
		}

		if( i == 100)
		{
			i=0 ;
			usleep(1000*10); //10ms
		}

	}

	return E_OPERATOR_SUCCED;

}


/***********************************************************************************
*
* NAME       : customCallerDisplay 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : Check the function that display the caller number upon the user number 
*              terminal. 
*
* INPUT      : userNumber const std::string & : The user's number want to check whether 
*                               customer the display caller number function.
*              opt  int  : The operation type which user want to custom,1:custom; 0: cancel.
*
* OUTPUT     : None.
*
* RETURN     : E_DATABASE_ERROR:    The database operation is fail.
*              E_OPERATOR_SUCCED:  Sucess.
*              other           : Other result of database operation.
*
***********************************************************************************/ 
ResultDataUsage  DataUsage::customCallerDisplay(const std::string & userNumber, int opt)
{
    std::string  UserID;
    //Check the user number's available and get the userID filed value.
    ResultDataUsage result = getUserID(userNumber, UserID);
    if(result != E_OPERATOR_SUCCED)
        return result;

    //set caller display custom
    if(opt == 1)
    {
        std::string sql = "select isChangeCallerDisplay from T_ServiceCustom where UserID = '";
        sql += UserID;
        sql += "'";
        
        DB_RECORDSET_ARRAY dbRecord;
        if( 0 != dbmysql_->executequery(sql,dbRecord))
            return E_DATABASE_ERROR;
        
        if (dbRecord.empty())
        {
            //generate UUID
            std::string seqID;
            generateUUID(seqID);

            sql.clear();
            sql = "insert into T_ServiceCustom (ItemID,UserID,isChangeCallerDisplay) values ('";
            sql += seqID;
            sql += "','";
            sql += UserID;
            sql += "','";
            sql += "1";
            sql += "')";
            
            if( 0 != dbmysql_->executenonquery(sql))
                return E_DATABASE_ERROR;
            //.s  modify  by whc for sync the database when there is change. 2010-02-20
            result = this->setUnSynchronized(REGISTERUSER, userNumber);
            if(result == E_DATABASE_ERROR)
            {
                return E_DATABASE_ERROR;
            }
            //.e modify  by whc for sync the database when there is change. 2010-02-20                
            return E_OPERATOR_SUCCED;
        }
        else
        {
            DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
            bool isCustom = false ;
            while (it != dbRecord.end())
            {
                DB_RECORDSET_ITEM::iterator it_ = (*it).find("ischangecallerdisplay");
                std::string value = it_->second;
                if( value =="1")
                {
                    isCustom = true;
                    return E_OPERATOR_SUCCED;
                }
                ++it;
            }
            if(isCustom == false)
            sql.clear();

            sql = "update T_ServiceCustom set isChangeCallerDisplay ='1' where UserID = '";
            sql += UserID;
            sql += "'";
            
            if( 0 != dbmysql_->executenonquery(sql))
                return E_DATABASE_ERROR;
            
            //.s  modify  by whc for sync the database when there is change. 2010-02-20
            result = this->setUnSynchronized(REGISTERUSER, userNumber);
            if(result == E_DATABASE_ERROR)
            {
                return E_DATABASE_ERROR;
            }
            //.e modify  by whc for sync the database when there is change. 2010-02-20                
            return E_OPERATOR_SUCCED;
        }
    }
    //cancel caller display custom
    if(opt == 0)
    {
        std::string sql ;
        sql = "delete from T_ServiceCustom where UserID = '";
        sql += UserID;
        sql += "'";
        
        if( 0 != dbmysql_->executenonquery(sql))
            return E_DATABASE_ERROR;
        //.s  modify  by whc for sync the database when there is change. 2010-02-20
        result = this->setUnSynchronized(REGISTERUSER, userNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }
        //.e modify  by whc for sync the database when there is change. 2010-02-20            
        return E_OPERATOR_SUCCED;
    }

    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : isCustomCallerDisplay 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : Check the function that display the caller number upon the user number 
*              terminal. 
*
* INPUT      : userNumber const std::string & : The user's number want to check whether 
*                               customer the display caller number function.
*
* OUTPUT     : None.
*
* RETURN     : E_DATABASE_ERROR:    The database operation is fail.
*              false:   The checking operation fail.
*              true:  Sucess.
*
***********************************************************************************/ 
bool DataUsage::isCustomCallerDisplay(const std::string & userNumber)
{
    std::string  UserID;
    ResultDataUsage result = getUserID(userNumber, UserID);
    if(result != E_OPERATOR_SUCCED)
        return false;
    
    std::string sql = "select isChangeCallerDisplay from T_ServiceCustom where UserID = '";
    sql += UserID;
    sql += "'";
    
    DB_RECORDSET_ARRAY dbRecord;
    if( 0 != dbmysql_->executequery(sql,dbRecord))
        return true;
    
    DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
    while (it != dbRecord.end())
    {
        DB_RECORDSET_ITEM::iterator it_ = (*it).find("ischangecallerdisplay");
        std::string value = it_->second;
        if( value =="1")
        {
            return true;
        }
        ++it;
    }

    return false ;
}

/***********************************************************************************
*
* NAME       : saveMinorOperatorRecord 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : Save the minor number operator record. 
*
* INPUT      : minorNumber const std::string & : The minor number should take action with it.
*              chtype  OperatorChannelType : The action take on which operator channel type:    
*                                 OPCH_BUSINESSHALL = 1,OPCH_WEB,OPCH_10086,OPCH_IVR,OPCH_SMS
*              actype  OperatorActionType :OPAC_LOGIN,OPAC_LOGOUT,OPAC_ACTIVE so and on.
*
* OUTPUT     : None.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT:    The minor number is not exist.
*              E_DATABASE_ERROR:   The database operation is fail.
*              E_OPERATOR_SUCCED:  Sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::saveMinorOperatorRecord(const std::string& minorNumber, OperatorChannelType chtype, OperatorActionType actype)
{
    //generate UUID
    std::string RecordID;
    generateUUID(RecordID);

    std::string numberID;
    if(getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    char operateActionID[2]={0};
    sprintf(operateActionID, "%d", actype);

    char operateChannelID[2]={0};
    sprintf(operateChannelID, "%d", chtype);

    std::string operateName;

    string sql = "insert into T_MinorNumberOperateRecord (RecordID, NumberID,OperateActionID,OperateTime,OperateName,OperateChannelID) values ('";
    sql += RecordID;
    sql += "', '";
    sql += numberID;
    sql +="', '";
    sql += operateActionID;
    sql += "',now(),'";
    sql += operateName;
    sql += "','";
    sql += operateChannelID;
    sql += "')";

    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR;

    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : setBadCallLimit 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : Set the limited number group of the called number as the caller . 
*
* INPUT      : targetNumber const std::string & : The number will set the limited
*                            number call group.
*              minorNumber  const std::string&  : The minor number should be set in 
*                            the limit number group of the targetNumber.
*
* OUTPUT     : None.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT:    The minor number is not exist.
*              E_DATABASE_ERROR:   The database operation is fail.
*              E_OPERATOR_SUCCED:  Sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::setBadCallLimit(const std::string& targetNumber, const std::string& minorNumber)
{
    //generate UUID
    std::string RuleID;
    generateUUID(RuleID);
    //Check the minor number whether is in the database table.    
    std::string numberID;
    if(getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    
    std::string sql = "select NumberID from T_RuleBadCallLimit where RuleTargetNumber = '";
    sql += targetNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbRecord;
    if( 0 != dbmysql_->executequery(sql,dbRecord))
        return E_DATABASE_ERROR;
    DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
    while (it != dbRecord.end())
    {
        DB_RECORDSET_ITEM::iterator it_ = (*it).find("numberid");
        std::string value = it_->second;
        if(value == numberID)
            return E_OPERATOR_SUCCED;
        ++it;
    }
    //Set the relation with the minor number's numberID and the target number's ID.
    sql = "insert into T_RuleBadCallLimit (RuleID, RuleTargetNumber,NumberID,Reserve1) values ('";
    sql += RuleID;
    sql += "', '";
    sql += targetNumber;
    sql +="', '";
    sql += numberID;
    sql += "', '')";

    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR;

    return E_OPERATOR_SUCCED;
}

/***********************************************************************************
*
* NAME       : checkBadCallLimit 
*
* CLASS NAME : DataUsage
*
* FUNCTION   : Check the minor number whether is in the limited group of the called
*              number as the caller . 
*
* INPUT      : targetNumber const std::string & :The called number.
*              minorNumber  const std::string&  :Minor number as caller.
*
* OUTPUT     : None.
*
* RETURN     : E_MINORNUMBER_NOT_EXESIT:    The minor number is not exist.
*              E_DATABASE_ERROR:   The query operation is fail.
*              E_BAD_CALL_LIMIT :  The caller is in the limited group of the called.
*              E_OPERATOR_SUCCED:  Sucess.
*
***********************************************************************************/ 
ResultDataUsage DataUsage::checkBadCallLimit(const std::string& targetNumber, const std::string& minorNumber)
{
    std::string numberID;
    if(getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }

    std::string sql = "select NumberID from T_RuleBadCallLimit where RuleTargetNumber = '";
    sql += targetNumber;
    sql += "'";
    
    DB_RECORDSET_ARRAY dbRecord;
    if( 0 != dbmysql_->executequery(sql,dbRecord))
        return E_DATABASE_ERROR;
    
    DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
    if(it == dbRecord.end())
        return E_OPERATOR_SUCCED;
    //Filter the result and find the limited number.
    while (it != dbRecord.end())
    {
        DB_RECORDSET_ITEM::iterator it_ = (*it).find("numberid");
        std::string value = it_->second;
        std::string userID;
        if(getUserIDbyNumberID(value, userID) != E_OPERATOR_SUCCED)
            continue;
        vector<string> numberIDs;
        if( getNumberIDbyUserID(userID, numberIDs) != E_OPERATOR_SUCCED)
            continue;
        for(int i=0; i<numberIDs.size(); i++)
        {
            if(numberIDs[i] == numberID)
                return E_BAD_CALL_LIMIT;
        }

        ++it;
    }
    return E_OPERATOR_SUCCED;
}


/***********************************************************************************************/
/*                                  Data Synchronized                                          */
/***********************************************************************************************/
/***********************************************************************************************
 *
 * NAME       : setSyncFlagToUNSynced 
 *
 * FUNCTION   : This function is used for local zone number to change their sync flag from 
 *              source flag to target flag.
 *              
 * INPUT      : sourceFlag :  The current sync flag of these data in database is between 0 and 3. 
 *              targetFlag :  The new sync flag want to be set is 0 and 3..
 *
 * OUTPUT     : None
 *
 * RETURN     : E_OPERATOR_SUCCED: It is sucess to set the data to be synchronized.
 *                E_DATABASE_ERROR : The operation is fail .
 *
 ***********************************************************************************************/ 
ResultDataUsage DataUsage::setSyncFlagToUNSynced(IsSynchronized  sourceFlag,IsSynchronized  targetFlag)
{
    std::string sFlag ;
    std::string tFlag ;

    sFlag = intToStr((int&) sourceFlag);
    tFlag = intToStr((int&) targetFlag);
    
    //std::string tailSql =  "set IsSynchronization = '" +tFlag + "' where IsSynchronization = '" + sFlag +"' and ZoneCode = '"+localZoneCode_+"';";
    std::string tailSql =  "set IsSynchronization = '" +tFlag + "' where IsSynchronization = '" + sFlag +"' and ZoneCode in "+zoneCodeStr_+";";
    
    //Update 1: the T_MinorNumber table data.
    std::string headSql = "update T_MinorNumber "; 
    headSql += tailSql;
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(headSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [setSyncFlagToUNSynced] database operation error!");
        return E_DATABASE_ERROR;
    }
    //Update 2: the T_RegisterUser table data.
    headSql = "update T_RegisterUser ";
    headSql += tailSql;

    if(0 != dbmysql_->executequery(headSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [setSyncFlagToUNSynced] database operation error!");
        return E_DATABASE_ERROR;
    }
    //Update 3: the  t_minornumber_bak table data.
    headSql = "update t_minornumber_bak ";
    headSql += tailSql;
    if(0 != dbmysql_->executequery(headSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [setSyncFlagToUNSynced] database operation error!");
        return E_DATABASE_ERROR;
    }
    //Update 4: the  t_registeruser_bak table data.
    headSql = "update t_registeruser_bak ";
    headSql += tailSql;
    if(0 != dbmysql_->executequery(headSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [setSyncFlagToUNSynced] database operation error!");
        return E_DATABASE_ERROR;
    }
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************************
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
 *                E_DATABASE_ERROR : The operation is fail .
 *
 ***********************************************************************************************/ 
ResultDataUsage DataUsage::deleteUserFromCS(const string& number)
{
    ResultDataUsage result = this->deleteUserFromDB(number);
    return result;
}

/************************************************************************************************
 *
 * NAME     : deleteMinorNumberFromCS 
 *
 * FUNCTION : The function is used to delete the inputed minor number from the T_MinorNumber
 *            table of the center database server.At the same time ,it is necessary to delete 
 *            the relation record from the relation tables with the input number record.
 *
 * INPUT    : number  const string&  : The minor number inputed needs to be deleted.
 *
 * OUTPUT   : None
 *
 * RETURN   : E_OPERATOR_SUCCED: It is sucess to delete data if exist,or no data matched for delete..
 *            E_DATABASE_ERROR : The operation is fail .
 *
 ***********************************************************************************************/ 
ResultDataUsage DataUsage::deleteMinorNumberFromCS (const string& number)
{
    ResultDataUsage result = this->deleteMinorNumberFromDB(number);
    return result;
}

/***********************************************************************************************
 *
 * NAME       : getIsSynchronized 
 *
 * FUNCTION   : To check the data's status whether has been synchronized.And output the flag value.
 *
 * INPUT      : dataType   SynchronDataType : The type of the data.
 *              number    const string& : The data is that want to be checked.
 *
 * OUTPUT     :  isSynchronized IsSynchronized&:The flag marks the status whether has
 *                                             been synchronized
 *
 * RETURN     : E_OPERATOR_SUCCED: Get the data synchronized flag sucessfully.
 *              E_DATABASE_ERROR : The database operation is fail .
 *              E_NO_DATA_UNSYNCHRONIZED : There is no match data in the local databse server.
 *
 ***********************************************************************************************/ 
ResultDataUsage  DataUsage::getIsSynchronized(SynchronDataType dataType, const string& number, IsSynchronized& isSynchronized)
{
    int tmpSyncFlag = -1;
    std::string sql = "select IsSynchronization from ";
    switch(dataType)
    {
    case REGISTERUSER:
        sql += "T_RegisterUser where UserNumber = '";
        sql +=  number +"' ;";
        break;
    case MINORNUMBER:
        sql += "T_MinorNumber where MinorNumber = '";
        sql +=  number+"' ;";
        break;  
    case REGISTERUSER_BAK:
        sql += "t_registeruser_bak where UserNumber = '";
        sql +=  number+"' ;";
        break;
    case MINORNUMBER_BAK:
        sql += "t_minornumber_bak where MinorNumber= '";
        sql +=  number+"' ;";
        break;
    default:
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [getIsSynchronized] dataType is no match!");    
        return E_DATABASE_ERROR;
    }
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [getIsSynchronized] database operation error!");    
        return E_DATABASE_ERROR;
    }
    
    //If there is no unynchronized data ,return the result that there is no data match with the input number.
    if (dbrecord_.empty())
    {
        return  E_NO_DATA_UNSYNCHRONIZED; 
    }
    //Filter the sync flag value according with the query result.
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("issynchronization");
        if (it_ != item_.end())
        {
            tmpSyncFlag = atoi((*it_).second.c_str());
        }
        ++it;
    }
    isSynchronized = (IsSynchronized)tmpSyncFlag;
    return E_OPERATOR_SUCCED;
}

/************************************************************************************************
 *
 * NAME       : setIsSynchronized 
 *
 * FUNCTION   : The function is used to set the synchronized status of the data
 *              which has set into the database according with the data type.
 *
 * INPUT      : dataType   SynchronDataType : The type of the data.
 *              number    const string& : The data is that want to be checked.
 *              isSynchronized IsSynchronized&:The flag marks the status whether has
 *                                             been synchronized
 * OUTPUT     : None
 *
 * RETURN     : E_OPERATOR_SUCCED: Get the data synchronized flag sucessfully.
 *              E_DATABASE_ERROR : The database operation is fail.
 *              E_MINORNUMBER_NOT_EXESIT : Minor number is not exist.
 *              E_MAJORNUMBER_NOT_EXESIT : Major number is not exist.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::setIsSynchronized(SynchronDataType dataType, const string& number, IsSynchronized isSynchronized)
{
    ResultDataUsage result;
    std::string sql = "update ";
    std::string checkSql = "";
    char buff[2];
    memset(buff,'0',2);
    sprintf(buff,"%d",isSynchronized);
    //Filter the data type ,the different type has different process sql.
    switch(dataType)
    {
    case REGISTERUSER:
        result = this->isMajorRegistered (number);
        if(result == E_OPERATOR_SUCCED)
        {
            sql += "T_RegisterUser set IsSynchronization  = '" ; 
        }
        break;
    case MINORNUMBER:
        result = this->isMinorNumberRegistered(number);
        if(result == E_OPERATOR_SUCCED)
        {
            sql += "T_MinorNumber set IsSynchronization  = '" ; 
        }
        break;  
    case REGISTERUSER_BAK:
        checkSql = "select UserID  from t_registeruser_bak  where UserNumber = '";
        checkSql += number + "';";
        result = this->isNumberExist(checkSql);

        if(result == E_OPERATOR_SUCCED)
        {
            sql += "t_registeruser_bak set IsSynchronization  = '" ; 
        }
        break;
    case MINORNUMBER_BAK:
        checkSql = "select UserID  from t_minornumber_bak  where minornumber = '";
        checkSql += number + "';";
        result = this->isNumberExist(checkSql);
        if(result == E_OPERATOR_SUCCED)
        {
            sql += "t_minornumber_bak set IsSynchronization  = '" ; 
        }
        break;
    default:
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [setIsSynchronized] dataType is no match!");    
        result = E_DATABASE_ERROR;        
        break;
    }
    if(result != E_OPERATOR_SUCCED)
    {
        return result;
    }
    switch(dataType)
    {
    case REGISTERUSER:
    case REGISTERUSER_BAK:
        sql += buff;
        if(isSynchronized == SYNCHRONIZED)
        {
            sql += "', SynchronizationTime = now() where UserNumber = '";
        }
        else
        {
            sql += "' where UserNumber = '";
        }
        sql +=  number+ "' ;";
        break;

    case MINORNUMBER:
    case MINORNUMBER_BAK:
        sql +=  buff;
        if(isSynchronized == SYNCHRONIZED)
        {
            sql += "', SynchronizationTime = now() where MinorNumber = '";
        }
        else
        {
            sql += "' where MinorNumber = '";
        }
        sql +=  number +"' ;";
        break;
    }

    //Execute the setting process.
    if(0 != dbmysql_->executenonquery(sql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [setIsSynchronized] database operation error!");
        return E_DATABASE_ERROR;
    }
    return E_OPERATOR_SUCCED;
}
/************************************************************************************************
 *
 * NAME       : getAnUnSynchronizedData 
 *
 * FUNCTION   : To get one older data that has no been synchronized from database order by the time. 
 *              First to get the t_minornumber_bak table,when the data is no matched to get data from
 *              t_registeruser_bal table. If there is no data match in the two table ,start to filter the
 *              t_minornumber table and the t_registeruser table.
 *             
 * INPUT      : None
 *
 * OUTPUT     : synData   SynchronData& : The data which has no synchronized.
 *
 * RETURN     : E_OPERATOR_SUCCED: Get the data sucess.
 *              E_DATABASE_ERROR : The operation is fail.
 *              E_NO_DATA_MATCHED: There is no unsync data in the local database server.
 *
 ***********************************************************************************************/ 
ResultDataUsage DataUsage::getAnUnSynchronizedData(SynchronData& synData)
{
    int  IsMajorFlag = 0;
    std::string syncMinorTime ;
    std::string syncMajorTime ;
    SynchronDataType dataType_;
    ResultDataUsage result_;
    //To set the data type that want to get from the local database.
    IsSynchronized syncFlag = UNSYNCHRONIZED;
    
    //Step 1: To check the minor number whether has exist in the t_minornumber_bak  table .
    SyncMinorNumberInfo minorInfoObj;
    dataType_ = MINORNUMBER_BAK;

    result_ = getSyncMinorNumInfo(minorInfoObj,syncFlag, dataType_ , syncMinorTime);
    if(result_ != E_NO_DATA_MATCHED)
    {
        synData.dataType = dataType_;
        synData.minorInfo = minorInfoObj;
        return result_;
    }
    //Step 2: To check the major number whether has exist in  the t_registeruser_bak table..
    /*
    dataType_ = REGISTERUSER_BAK;
    result_ = getSyncMajorNumInfo(synData.userInfo,syncFlag, dataType_ ,syncMajorTime);
    if(result_ != E_NO_DATA_MATCHED)
    {        
        synData.dataType = dataType_;
        return result_;
    }
    */
    //Step 3: To check the minor number and major number order by the time.
    dataType_ = MINORNUMBER;
    SyncMinorNumberInfo minorInfo;
    ResultDataUsage resultMinor_ = getSyncMinorNumInfo(minorInfo,syncFlag, dataType_ ,syncMinorTime);
    if(resultMinor_ != E_NO_DATA_MATCHED && resultMinor_ != E_OPERATOR_SUCCED)
    {
        return resultMinor_;
    }
    dataType_ = REGISTERUSER ;
    ResultDataUsage resultMajor_  = getSyncMajorNumInfo(synData.userInfo,syncFlag, dataType_ ,syncMajorTime);
    //Step 4 To compare these data .order by the sequence as follow :minor number ->major number.
    //Choose the data has the smallest sync time.
    switch(resultMinor_)
    {
    case E_NO_DATA_MATCHED:
        if(resultMajor_ != E_NO_DATA_MATCHED)
        {
            synData.dataType = REGISTERUSER;
            return resultMajor_;
        }
        else
        {
            return E_NO_DATA_MATCHED;
        }
    case E_OPERATOR_SUCCED:
        if(resultMajor_ == E_NO_DATA_MATCHED)
        {
            synData.dataType = MINORNUMBER;
            synData.minorInfo = minorInfo;
        }
        else if(resultMajor_ == E_OPERATOR_SUCCED)
        {
            if(syncMajorTime <= syncMinorTime)
            {
                synData.dataType = REGISTERUSER;
            }
            else if(syncMinorTime < syncMajorTime)
            {
                   synData.dataType = MINORNUMBER;
                synData.minorInfo = minorInfo;              
            }
        }
        break;
    case E_DATABASE_ERROR:
        return E_DATABASE_ERROR;
        
    }
    return E_OPERATOR_SUCCED;
}

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
ResultDataUsage DataUsage::setASynchronizedDataToCS(const SynchronData& synData)
{
    DB_RECORDSET_ARRAY dbrecord_;
    ResultDataUsage result; 
    syncActionTime_ = "1970-01-01 00:00:00";
    switch(synData.dataType)
    {
    case REGISTERUSER :
    {
        std::string userID;
        result = syncMajorNumToCS((synData.userInfo), userID);
        result = updateSMSNotifyInfo(synData.userInfo.SMSNotify,userID);
        result = updateCustomServiceInfo(synData.userInfo.serviceCustom,userID);
        break;
    }
    case MINORNUMBER:
        {
            if(synData.minorInfo.userNumber.empty())        
            {
                return E_NO_DATA_UNSYNCHRONIZED;
            }
            
            std::string userId;
            //To get the NumberID userID from the register table.
            DB_RECORDSET_ARRAY dbrecordTmp_;
            std::string getUserIDSql = "select UserID from T_RegisterUser where  UserNumber = '";
            getUserIDSql += synData.minorInfo.userNumber + "';";
            if(0 != dbmysql_->executequery(getUserIDSql,dbrecordTmp_))
            {
               return E_DATABASE_ERROR;
            }
            if (dbrecordTmp_.empty())
            {
                //create a temp user info
                std::string userID_;
                generateUUID(userID_);
                std::string insertSql = "insert into T_RegisterUser(UserID,UserNumber,ZoneCode) values('";
                insertSql += userID_ + "','";
                insertSql += synData.minorInfo.userNumber + "','";
                insertSql += synData.minorInfo.zoneCode + "');";
                if(0 != dbmysql_->executenonquery(insertSql))
                {
                    LogFileManager::getInstance()->write(Brief,"ERROR INFO: [setASynchronizedDataToCS] database operation error!");    
                    return E_DATABASE_ERROR;
                }
                userId = userID_;
            }
            else
            {
                DB_RECORDSET_ARRAY::iterator it = dbrecordTmp_.begin();

                DB_RECORDSET_ITEM::iterator it_ = (*it).find("userid");
                userId = it_->second;
            }
            updateMinorInfo(synData.minorInfo,userId);
        }
        break;

    case REGISTERUSER_BAK:
        //.s delete by whc 0226
        //deleteUserFromCS(synData.userInfo.number);
        //.e delete by whc 0226
        break;

    case MINORNUMBER_BAK :
        if(synData.minorInfo.number.empty())        
        {
            return E_NO_DATA_UNSYNCHRONIZED;
        }
        deleteMinorNumberFromCS(synData.minorInfo.number);
        break;
    }
    return E_OPERATOR_SUCCED;
}

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
ResultDataUsage DataUsage::backupCSSynchronData(const string & backupFileName)
{
    //To construct the userRegister datas.
    size_t found;
    found=backupFileName.find_last_of("/\\");
    string tmpBackFile = "tmpBackFile.txt";    
    string tmpFileName = backupFileName.substr(0,found+1) +tmpBackFile;

    FILE *fd;
    if(tmpFileName.empty())
    {
        return E_NO_FILE_FOUND;
    }
    fd = fopen(tmpFileName.c_str(),"wb"); 
    if(fd==NULL)
    {
        return E_NO_FILE_FOUND;
    }
    ResultDataUsage result = getCSDataInfo(fd);
    fclose(fd);
    if(result == E_OPERATOR_SUCCED)
    {
        remove(backupFileName.c_str());
        if(rename ( tmpFileName.c_str() , backupFileName.c_str() ) == 0)
        {
            result = E_OPERATOR_SUCCED;
        }
        else
        {
            result = E_NO_FILE_FOUND;
        }
    }
    return result;
}

/************************************************************************************
 *
 * NAME       : updateFromCSSynchronData 
 *
 * FUNCTION   : The function is to compare relation table datas of center server with local DB.
 *              And update the local DB according to the center server if there is difference.
 *
 * INPUT      : backupFileName  const string& : The file is used to storage the DB data.
 *
 * OUTPUT     : None
 *
 * RETURN     : E_OPERATOR_SUCCED: Get the data synchronized flag sucessfully.
 *              other result     : The operation is fail .
 *
 ************************************************************************************/ 
ResultDataUsage DataUsage::updateFromCSSynchronData(const string& backupFileName)
{
    FILE *fd;   
    char bufStr[256] ;
    if(backupFileName.empty())
    {
        return E_NO_FILE_FOUND;
    }
    fd = fopen(backupFileName.c_str(),"rb"); 
    if(fd == NULL)
    {
        return E_READ_FILE_FAIL;
    }
    //To define the vector to contain the major info.
    vector<string> majorInfo;
    vector<string> SMSInfo;
    vector<string> customServiceInfo;
    vector<string> minorInfo;
    vector<SyncMinorNumberInfo> vecMinorInfo;

    int index = 0;
    int countNum = 0;
    int setCount = 0;
    int dataType = -1;
    int maxCount = -1;
    int fileStatus = 0;

    //To get the single same time for update local data base from center db.
    time_t nowTime = time(NULL);
    syncActionTime_ = LonglongtoStr(nowTime);

    while(!feof(fd))
    {    
        unsigned int dataSize;
        //Loop to read major infomation from file.
        majorInfo.clear();
        SMSInfo.clear();
        vecMinorInfo.clear();
        customServiceInfo.clear();

        int fEndFlag = 0;
        for(int loopMajor = 0;loopMajor<9;loopMajor++)
        {
            int result = fread(&dataSize,sizeof(int),1,fd);
            if(result != 1)
            {
                fEndFlag = 1;
                break;
            }
            memset(bufStr,'\0',256);
            unsigned int readSize = fread(bufStr,dataSize,1,fd);        
            majorInfo.push_back(bufStr);
        }
        if(fEndFlag == 1)
        {
            fileStatus = 1;
            break;
        }
        
        //Loop to read sms infomation from file.
        dataSize = 0;
        int smsCount = -1;
        fread(&smsCount,sizeof(int),1,fd);
        if(smsCount == 1)
        {
            for(int loopSMS = 0;loopSMS<3;loopSMS++)
            {
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);        
                SMSInfo.push_back(bufStr);
            }
        }
        //Loop to read cusService infomation from file.
        dataSize = 0;
        int cstomCount = -1;
        fread(&cstomCount,sizeof(int),1,fd);
        if(cstomCount == 1)
        {
            for(int loopI= 0;loopI<2;loopI++)
            {
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                unsigned int readSize = fread(bufStr,dataSize,1,fd);        
                customServiceInfo.push_back(bufStr);
            }
        } 
        //Loop to read the minor information
        dataSize = 0;
        int minorCount = -1;
        fread(&minorCount,sizeof(int),1,fd);
        //To compare the major number struct and update the difference
        ResultDataUsage result = updateMajorInfo(majorInfo,SMSInfo,customServiceInfo);

        if(result == E_DATABASE_ERROR)
        {
            fclose(fd);
            return result;
        }        
        else if(result == E_NO_DATA_MATCHED) 
        {
            //If the zone code is same as local code, do not to update this item and check the line end  sign.
            char tmp;
            while(!feof(fd))
            {
                fread(&tmp,1,1,fd);
                if(tmp==0x0d)
                {
                    fread(&tmp,1,1,fd);
                    if(tmp==0x0a)
                    {
                        break;
                    }
                }
            }
            continue;
        }
        //The whole line data has been read.
        for(int loopMinor = 0;loopMinor<minorCount ;loopMinor++)
        {
            minorInfo.clear();
            for(int loopM = 0;loopM<9;loopM++)
            {
                dataSize = 0;
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);
                minorInfo.push_back(bufStr);    
            }
            SyncMinorNumberInfo tmpVecInfo;
            tmpVecInfo.userNumber =  majorInfo[1];  //user number

            tmpVecInfo.number = minorInfo[0];
            tmpVecInfo.type = atoi(minorInfo[1].c_str());
            tmpVecInfo.sequenceNo = atoi(minorInfo[2].c_str());
            tmpVecInfo.registerTime = minorInfo[3];
            tmpVecInfo.registerType = atoi(minorInfo[4].c_str());
            tmpVecInfo.stateID = atoi(minorInfo[5].c_str());
            tmpVecInfo.IMSI = minorInfo[6];
            tmpVecInfo.recordRule = atoi(minorInfo[7].c_str());
            tmpVecInfo.zoneCode = minorInfo[8];
            
            //To check the black list and read its infomaiton from the file.

            dataSize = 0;
            int blCount = -1;
            fread(&blCount,sizeof(int),1,fd);

            for(int loop = 0;loop<blCount;loop++)
            {   
                SyncBlackListInfo tmpBlackInfo;
                dataSize = 0;
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);
                tmpBlackInfo.forbidNumber = bufStr;

                dataSize = 0;
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);
                tmpBlackInfo.customTime = bufStr;
                tmpVecInfo.blackList.push_back(tmpBlackInfo);
            }

            //To check the record rule list and read its infomaiton from the file.
            dataSize = 0;
            int rrCount = -1;
            fread(&rrCount,sizeof(int),1,fd);

            for(int loop = 0;loop<rrCount;loop++)
            {
                SyncRecordRuleInfo tmpRecordInfo;
                dataSize = 0;
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);
                tmpRecordInfo.allowNumber = bufStr;;

                dataSize = 0;
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);
                tmpRecordInfo.customTime = bufStr;
                tmpVecInfo.recordRuleList.push_back(tmpRecordInfo);
            }
            //To check the time rule list and read its infomaiton from the file.
            dataSize = 0;
            int rtCount = -1;
            fread(&rtCount,sizeof(int),1,fd);

            for(int loop = 0;loop<rtCount;loop++)
            {
                SyncTimeRuleInfo tmpRuleTime; 
                dataSize = 0;
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);
                tmpRuleTime.startTime = bufStr;
                dataSize = 0;
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);
                tmpRuleTime.endTime = bufStr;
                dataSize = 0;
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);
                tmpRuleTime.strategyID = atoi(bufStr);
                dataSize = 0;
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);
                tmpRuleTime.week = atoi(bufStr);
                dataSize = 0;
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);
                tmpRuleTime.customTime = bufStr;
                tmpVecInfo.timeRule.push_back(tmpRuleTime);
            }
            //To check the white list and read its infomaiton from the file.
            dataSize = 0;
            int wlCount = -1;
            fread(&wlCount,sizeof(int),1,fd);

            for(int loop = 0;loop<wlCount;loop++)
            {
                SyncWhiteListInfo tmpWhiteList;
                dataSize = 0;
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);
                tmpWhiteList.allowNumber = bufStr;

                dataSize = 0;
                fread(&dataSize,sizeof(int),1,fd);
                memset(bufStr,'\0',256);
                fread(bufStr,dataSize,1,fd);
                tmpWhiteList.customTime = bufStr;
                tmpVecInfo.whiteList.push_back(tmpWhiteList);
            }

            vecMinorInfo.push_back(tmpVecInfo);
            //.s 
            std::string userID = "";
            result = getUserID(vecMinorInfo[loopMinor].userNumber,userID);
            if(result == E_DATABASE_ERROR)
            {
                return result;
            }
            result = updateMinorInfo(vecMinorInfo[loopMinor],userID);
            //.e
            if(result == E_DATABASE_ERROR)
            {
                fclose(fd);
                return result;
            }    
        }
        fseek(fd,2,SEEK_CUR);
        
        ///
    }
    ResultDataUsage retResult = E_OPERATOR_SUCCED;
    fclose(fd);
    if(fileStatus != 1)
    {
        LogFileManager::getInstance()->write(Brief,"INFO: ERROR ---updateFromCSSynchronData file exception!");
        retResult = E_DATABASE_ERROR;
    }
    else
    {
        retResult = this->deleteNoCompData();
    }
    return retResult;
}

/**********************************************************************************************
 *
 * NAME       : updateMajorInfo 
 *
 * FUNCTION   : The function is to compare relation table datas of center server with local DB.
 *              And update the local DB according to the center server if there is difference.
 *
 * INPUT      : backupFileName  const string& : The file is used to storage the DB data.
 *
 * OUTPUT     : None
 *
 * RETURN     : E_OPERATOR_SUCCED: Get the data synchronized flag sucessfully.
 *              other result     : The operation is fail .
 *
 *********************************************************************************************/ 
ResultDataUsage DataUsage::updateMajorInfo(vector<string> &majorInfo,vector<string> &SMSInfo,vector<string>& customServiceInfo)
{
    int vectorSize = majorInfo.size();
    int updateFlag = 0;
    if(vectorSize == 0)  //no data need to be sync
    {
        return E_NO_DATA_MATCHED;
    }

    //Compare 1 : major infomation. To get local major infomation except the local zone code.
    for(int loopI =0;loopI<subZoneCodeVec_.size();loopI++)
    {
        if(subZoneCodeVec_[loopI].compare(majorInfo[5])==0)
        {
            return E_NO_DATA_MATCHED;
        }
    }

    std::string chkSql = "select * from T_RegisterUser where UserNumber = '";
    chkSql += majorInfo[1] + "' ;";
    std::string  userID = "";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"The check userNumber sql is -->Error");
        return E_DATABASE_ERROR;
    }

    //If there is no unynchronized data on the local databace server.
    if (dbrecord_.empty())
    {
        if(vectorSize == 0)  //no data need to be sync
        {
            return E_OPERATOR_SUCCED;
        }
        //insert T_RegisterUser
        generateUUID(userID);

        std::string insertSql = "insert into T_RegisterUser(UserID,UserNumber,IMSI,UserName,PWD,ZoneCode,RegisteredTime,IsSynchronization,SynchronizationTime) values('";
        insertSql += userID + "','";
        insertSql += majorInfo[1] + "','";
        insertSql += majorInfo[2] + "','";
        insertSql += majorInfo[3] + "','";
        insertSql += majorInfo[4] + "','";

        insertSql += majorInfo[5] + "','";
        insertSql += majorInfo[6] + "','";
        insertSql += majorInfo[7] + "','";
        insertSql += syncActionTime_ + "');";
        if(0 != dbmysql_->executenonquery(insertSql))
        {
            return E_DATABASE_ERROR;
        }    
    }
    else
    {
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        std::string cmpIMSI;
        std::string cmpZoneCode;
        std::string cmpRegistTime;
        while (it!=dbrecord_.end())
        {
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("userid");
            if (it_ != item_.end())
            {
                userID  = (*it_).second;
            }
            //.s add by whc 0226
            it_ = item_.find("imsi");
            if (it_ != item_.end())
            {
                cmpIMSI  = (*it_).second;
            }
            it_ = item_.find("zonecode");
            if (it_ != item_.end())
            {
                cmpZoneCode  = (*it_).second;
            }
            it_ = item_.find("registeredtime");
            if (it_ != item_.end())
            {
                cmpRegistTime  = (*it_).second;
            }
            //.e add by whc 0226
            ++it;
        }

        //.s Compare these major infomation by whc 0226
        if(cmpIMSI!=majorInfo[2])
        {
            updateFlag = 1;
        }
        else if(cmpZoneCode!=majorInfo[5])
        {
            updateFlag = 1;
        }
        else if( cmpRegistTime!=majorInfo[6])
        {
            updateFlag = 1;
        }

        if(updateFlag == 1)
        {
        //.e Compare these major infomation by whc 0226
            std::string updateSql = "update T_RegisterUser set UserNumber = '";
            updateSql += majorInfo[1] + "',IMSI = '";
            updateSql += majorInfo[2] + "',UserName = '";
            updateSql += majorInfo[3] + "',PWD = '";
            updateSql += majorInfo[4] + "',ZoneCode = '";
            updateSql += majorInfo[5] + "',RegisteredTime = '";
            updateSql += majorInfo[6] + "', IsSynchronization = '";
            updateSql += majorInfo[7] + "',SynchronizationTime ='";
            updateSql += syncActionTime_ + "' where UserID = '";
            updateSql += userID +"';";
            if(0 != dbmysql_->executenonquery(updateSql))
            {
                return E_DATABASE_ERROR;
            }
        }
    }
    ResultDataUsage tmpResult;
    SyncServiceCustomInfo upCustomSerInfo;
    SyncSMSNotifyInfo upSMSInfo;
    if(SMSInfo.size()==0)
    {
        upSMSInfo.isAccepted = -1;
        upSMSInfo.deliverCount = -1;
        upSMSInfo.updateTime = "0000-00-00 00:00:00";    
    }
    else
    {
        upSMSInfo.isAccepted = atoi(SMSInfo[0].c_str());
        upSMSInfo.deliverCount = atoi(SMSInfo[1].c_str());
        upSMSInfo.updateTime = SMSInfo[2].c_str();
    }
    if(customServiceInfo.size()==0)
    {
        upCustomSerInfo.isChangeCallerDisplay = -1;
        upCustomSerInfo.customTime = "0000-00-00 00:00:00";    
    }
    else
    {
        upCustomSerInfo.isChangeCallerDisplay = atoi(customServiceInfo[0].c_str());
        upCustomSerInfo.customTime = customServiceInfo[1].c_str();    
    }

    tmpResult =    updateSMSNotifyInfo(upSMSInfo, userID);
    if(tmpResult == E_DATABASE_ERROR)
    {
        return tmpResult;
    }
    tmpResult = updateCustomServiceInfo(upCustomSerInfo, userID);
    if(tmpResult == E_DATABASE_ERROR)
    {
        return tmpResult;
    }
    return  E_OPERATOR_SUCCED; 
}

/***********************************************************************************
*
* NAME       : updateSMSNotifyWithCS 
*
* FUNCTION   :  
*
* INPUT      : 
*
* OUTPUT     : 
*
* RETURN     :          
*              
*
***********************************************************************************/ 
ResultDataUsage DataUsage::updateSMSNotifyInfo(const SyncSMSNotifyInfo &SMSNotifyInfo,std::string & userID)
{
    int size = 1;
    if(SMSNotifyInfo.deliverCount == -1 && SMSNotifyInfo.isAccepted == -1 && SMSNotifyInfo.updateTime == "0000-00-00 00:00:00")
    {
        size = 0;
    }
    int updateFlag = 0;
    //Compare 1 : sms notify infomation.
    int isAccepted = -1;
    int  deliverCount = -1;
    std::string  updateTime = "";
    
    ResultDataUsage queryResult;
    SyncSMSNotifyInfo  smsNotifyInfo;
    SyncServiceCustomInfo  customServiceInfo;
    //To get the local database infomation.
    queryResult = getSMSNotifyInfo(smsNotifyInfo, userID);
    if(queryResult == E_DATABASE_ERROR)
    {
        return queryResult;
    }
    else if(queryResult == E_NO_DATA_MATCHED)
    {
        if(size == 0)
        {
            return  E_OPERATOR_SUCCED; 
        }
        //insert T_SMSNotify
        std::string ruleID;
        generateUUID(ruleID);

        std::string insertSql = "insert into T_SMSNotify(ItemID,UserID,IsAccepted,DeliverCount,UpdateTime) values('";
        insertSql += ruleID + "','";
        insertSql += userID + "','";
        insertSql += intToStr((int&)SMSNotifyInfo.isAccepted) + "','";
        insertSql += intToStr((int&)SMSNotifyInfo.deliverCount) + "','";
        insertSql += SMSNotifyInfo.updateTime + "');";
        if(0 != dbmysql_->executenonquery(insertSql))
        {
            return E_DATABASE_ERROR;
        }    
        return  E_OPERATOR_SUCCED; 
    }
    else if(queryResult == E_OPERATOR_SUCCED)
    {
        isAccepted = smsNotifyInfo.isAccepted;
        deliverCount = smsNotifyInfo.deliverCount;
        updateTime = smsNotifyInfo.updateTime;
        //To check there has data in the center database.        
        if(size == 0)
        {
            std::string delSql = "delete from T_SMSNotify where UserID = '";
            delSql += userID + "';";
            if(0 != dbmysql_->executenonquery(delSql))
            {
                return E_DATABASE_ERROR;
            }
            return  E_OPERATOR_SUCCED; 
        }
        //isAccepted
        while(1)
        {
            if(isAccepted != SMSNotifyInfo.isAccepted)
            {
                updateFlag = 1;
                break;
            }
            //delivercount:
            if(deliverCount != SMSNotifyInfo.deliverCount)
            {
                updateFlag = 1;
                break;
            }
            //updatetime ;
            if(updateTime != SMSNotifyInfo.updateTime)
            {
                updateFlag = 1;
                break;
            }
            break;
        }
    }

    //If there is no unynchronized data on the local databace server.
    if(updateFlag == 1)
    {
        std::string updateSql = "update T_SMSNotify set IsAccepted = '";
        updateSql += intToStr((int&)SMSNotifyInfo.isAccepted) + "',DeliverCount = '";
        updateSql += intToStr((int&)SMSNotifyInfo.deliverCount)+ "',UpdateTime = '";
        updateSql += SMSNotifyInfo.updateTime+ "' where UserID = '";
        updateSql += userID +"';";
        if(0 != dbmysql_->executenonquery(updateSql))
        {
            return E_DATABASE_ERROR;
        }
    }
    return  E_OPERATOR_SUCCED; 
}

/***********************************************************************************
*
* NAME       : updateCustomServiceWithCS 
*
* CLASS NAME : DataUsage
*
* FUNCTION   :  
*
* INPUT      : 
*
* OUTPUT     : 
*
* RETURN     :          
*              
*
***********************************************************************************/ 
ResultDataUsage DataUsage::updateCustomServiceInfo(const SyncServiceCustomInfo & customServiceInfo,std::string & userID)
{
    int vectorSize = 1;
    if(customServiceInfo.isChangeCallerDisplay == -1 &&  customServiceInfo.customTime == "0000-00-00 00:00:00")
    {
        vectorSize = 0;
    }
    int updateFlag = 0;
    //Compare 1 : To get the custom service settting
    SyncServiceCustomInfo  serviceInfoObj;
    vector<string> serviceInfo;
    ResultDataUsage result = getCustomServiceInfo(serviceInfoObj,userID);
    if(result == E_DATABASE_ERROR)
    {
        return result;
    }
    else if(result == E_NO_DATA_MATCHED)
    {
        if(vectorSize == 0)
        {
            return  E_OPERATOR_SUCCED; 
        }
        //insert T_ServiceCustom generate UUID
        std::string ruleID;
        generateUUID(ruleID);

        std::string insertSql = "insert into T_ServiceCustom(ItemID,UserID,isChangeCallerDisplay,CustomTime) values('";
        insertSql += ruleID + "','";
        insertSql += userID + "','";
        insertSql += intToStr((int&)customServiceInfo.isChangeCallerDisplay) + "','";
        insertSql += customServiceInfo.customTime + "');";
        if(0 != dbmysql_->executenonquery(insertSql))
        {
            return E_DATABASE_ERROR;
        }     
        return  E_OPERATOR_SUCCED; 
    }
      else if(result == E_OPERATOR_SUCCED)
    {
        //To check there has data in the center database.        
        if(vectorSize == 0)
        {
            std::string delSql = "delete from T_ServiceCustom where UserID = '";
            delSql += userID + "';";
            LogFileManager::getInstance()->write(Brief,"updateSMSNotifyWithCS delete sql is -->%s",delSql.c_str());
            if(0 != dbmysql_->executenonquery(delSql))
            {
                return E_DATABASE_ERROR;
            }
            return  E_OPERATOR_SUCCED; 
        }
        int isDisplay = -1;
        std::string  customTime = "";
        isDisplay = serviceInfoObj.isChangeCallerDisplay;
        serviceInfoObj.customTime;
        //There is data in the database
        while(1)
        {
             //ischangecallerdisplay:
            if(isDisplay != customServiceInfo.isChangeCallerDisplay)
            {
                updateFlag = 1;
                break;
            }
            //customtime:
            if(customTime != customServiceInfo.customTime)
            {
                updateFlag = 1;
                break;
            }
            break;
        }    
        if(updateFlag == 1)
        {
            std::string updateSql = "update T_ServiceCustom set isChangeCallerDisplay = '";
            updateSql += intToStr((int&)customServiceInfo.isChangeCallerDisplay) + "',CustomTime = '";
            updateSql += customServiceInfo.customTime + "' where UserID = '";
            updateSql += userID +"';";
            if(0 != dbmysql_->executenonquery(updateSql))
            {
                return E_DATABASE_ERROR;
            }
        }
    }
    return  E_OPERATOR_SUCCED; 
}

/***********************************************************************************
*
* NAME       : updateBlackListWithCS 
*
* CLASS NAME : DataUsage
*
* FUNCTION   :  
*
* INPUT      : 
*
* OUTPUT     : 
*
* RETURN     :          
*              
*
***********************************************************************************/ 
ResultDataUsage DataUsage::updateBlackListInfo(vector<SyncBlackListInfo> &blackListInfo, std::string & numberId)
{
    int vectorSize = blackListInfo.size();
    if(vectorSize == 0)  //no data need to be sync
    {
        return E_NO_DATA_MATCHED;
    }

    int updateFlag = 0;
    int insertFlag = 0;
    //Compare 1 : BlackList infomation.
    vector<SyncBlackListInfo>  allBlackListInfo;
    ResultDataUsage resultTmp = getBlackListInfo(allBlackListInfo,numberId);
    if(resultTmp == E_DATABASE_ERROR)
    {
          return resultTmp;
    }
    else if(resultTmp == E_NO_DATA_MATCHED)
    {
        if(vectorSize == 0)
        {
            return  E_OPERATOR_SUCCED; 
        }
        //insert T_RuleBlackList
        //generate UUID
        std::string ruleID;
        generateUUID(ruleID);
        std::string insertSql = "";
    
        for(int index = 0;index<vectorSize;index++)
        {
            generateUUID(ruleID);
            insertSql = "";
            insertSql = "insert into T_RuleBlackList(RuleID,NumberID,ForbidNumber,CustomTime) values('";
            insertSql += ruleID + "','";
            insertSql += numberId + "','";
            insertSql += blackListInfo[index].forbidNumber + "','";
            insertSql += blackListInfo[index].customTime + "');";
            if(0 != dbmysql_->executenonquery(insertSql))
            {
                LogFileManager::getInstance()->write(Brief,"ERROR INFO: [updateBlackListWithCS] E_DATABASE_ERROR");
                return E_DATABASE_ERROR;
            }    
        }
        return E_OPERATOR_SUCCED; 
    }
      else if(resultTmp == E_OPERATOR_SUCCED)
    {
        //To check there has data in the center database.        
        if(vectorSize == 0)
        {
            std::string delSql = "";
            delSql = "delete from T_RuleBlackList where NumberID = '";
            delSql += numberId + "';";
            if(0 != dbmysql_->executenonquery(delSql))
            {
                LogFileManager::getInstance()->write(Brief,"ERROR INFO: [updateBlackListWithCS] E_DATABASE_ERROR");
                return E_DATABASE_ERROR;
            }
            return  E_OPERATOR_SUCCED; 
        }
        //To get the record Rule infomation
        map<std::string, struct mapValueInfo> recordRuleMap;
        struct mapValueInfo tmpMapValueInfo;
        tmpMapValueInfo.flag = 0;
        tmpMapValueInfo.strValue = "";
        for(int loopi = 0;loopi<allBlackListInfo.size();loopi++)
        {  
            tmpMapValueInfo.strValue = allBlackListInfo[loopi].customTime;
            recordRuleMap.insert(map<string, struct mapValueInfo>::value_type (allBlackListInfo[loopi].forbidNumber, tmpMapValueInfo));
        }

        std::string  tmpNumber = "";
        std::string  customTime = "";
        for(int loopNum = 0;loopNum<blackListInfo.size();loopNum++)
        {
            updateFlag = 0;
            insertFlag = 0;

            map<string ,struct mapValueInfo >::iterator mapIt;;
            tmpNumber = "";
            customTime = "";
            tmpNumber = blackListInfo[loopNum].forbidNumber;
            customTime = blackListInfo[loopNum].customTime;
            mapIt = recordRuleMap.find(tmpNumber);
            if(mapIt == recordRuleMap.end())
            {            
                insertFlag = 1;
            }
            else
            {    
                if( mapIt->second.strValue != customTime)
                {
                    updateFlag = 1;
                }
                else
                {
                    (mapIt->second).flag = 1;
                }
            }
    
            if(updateFlag == 1)
            {
                std::string updateSql = "update T_RuleBlackList set CustomTime = '";
                updateSql += customTime + "' where NumberID = '";
                updateSql += numberId +"' and ForbidNumber = '";
                updateSql += tmpNumber +"';";
                if(0 != dbmysql_->executenonquery(updateSql))
                {    
                    LogFileManager::getInstance()->write(Brief,"ERROR INFO: [updateBlackListWithCS] E_DATABASE_ERROR");
                    return E_DATABASE_ERROR;
                }
                (mapIt->second).flag = 1;
             }
             if(insertFlag ==1)
             {
                 std::string insertSql = "";
                 std::string ruleID = "";
                 generateUUID(ruleID);
        
                 insertSql = "insert into T_RuleBlackList(RuleID,NumberID,ForbidNumber,CustomTime) values('";
                 insertSql += ruleID + "','";
                 insertSql += numberId + "','";
                 insertSql += tmpNumber + "','";
                 insertSql += customTime + "');";
                 if(0 != dbmysql_->executenonquery(insertSql))
                 {
                    return E_DATABASE_ERROR;
                 }
             }
         }
         //To delete these record that no be compared in the table with the bake file.
         map<string ,struct mapValueInfo >::iterator mapItLeft;
         for(mapItLeft = recordRuleMap.begin();mapItLeft!=recordRuleMap.end();mapItLeft++)
         {
             if(((mapItLeft->second).flag) != 1)
             {
                std::string delSql = "delete from T_RuleBlackList where NumberID = '";
                delSql += numberId + "' and ForbidNumber = '";
                delSql += mapItLeft->first + "';";
                if(0 != dbmysql_->executenonquery(delSql))
                {
                    return E_DATABASE_ERROR;
                }
            }            
         }
    }
    return  E_OPERATOR_SUCCED; 
}

/***********************************************************************************
*
* NAME       : updateWhiteListWithCS 
*
* CLASS NAME : DataUsage
*
* FUNCTION   :  
*
* INPUT      : 
*
* OUTPUT     : 
*
* RETURN     :          
*              
*
***********************************************************************************/ 
ResultDataUsage DataUsage::updateWhiteListInfo(vector<SyncWhiteListInfo> &whiteListInfo, std::string & numberId)
{
    int vectorSize = whiteListInfo.size();
    if(vectorSize == 0)  //no data need to be sync
    {
        return E_NO_DATA_MATCHED;
    }
    int updateFlag = 0;
    int insertFlag = 0;
    //Compare 1 : sms notify infomation.
    vector<SyncWhiteListInfo>  allWhiteListInfo;
    ResultDataUsage resultTmp = getWhiteListInfo(allWhiteListInfo,numberId);
    if(resultTmp == E_DATABASE_ERROR)
    {
          return resultTmp;
    }
    else if(resultTmp == E_NO_DATA_MATCHED)
    {
        if(vectorSize == 0)
        {
            return  E_OPERATOR_SUCCED; 
        }
        //insert T_RuleBlackList
        //generate UUID
        std::string ruleID;
        generateUUID(ruleID);
        std::string insertSql = "";
    
        for(int index = 0;index<vectorSize;index++)
        {
            generateUUID(ruleID);
            insertSql = "";
            insertSql = "insert into T_RuleWhiteList(RuleID,NumberID,AllowNumber,CustomTime) values('";
            insertSql += ruleID + "','";
            insertSql += numberId + "','";
            insertSql += whiteListInfo[index].allowNumber + "','";
            insertSql += whiteListInfo[index].customTime + "');";
            if(0 != dbmysql_->executenonquery(insertSql))
            {
                return E_DATABASE_ERROR;
            }    
        }
        return E_OPERATOR_SUCCED; 
    }
      else if(resultTmp == E_OPERATOR_SUCCED)
    {
        //To check there has data in the center database.        
        if(vectorSize == 0)
        {
            std::string delSql = "";
            delSql = "delete from T_RuleWhiteList where NumberID = '";
            delSql += numberId + "';";
            if(0 != dbmysql_->executenonquery(delSql))
            {
                return E_DATABASE_ERROR;
            }
            return  E_OPERATOR_SUCCED; 
        }

        //step 2 To construct the maping with these blackList datas.
        //If there is no unynchronized data on the local databace server.
        map<std::string, struct mapValueInfo> recordRuleMap;
        struct mapValueInfo tmpMapValueInfo;
        tmpMapValueInfo.flag = 0;
        tmpMapValueInfo.strValue = "";
        for(int loopi = 0;loopi<allWhiteListInfo.size();loopi++)
        {  
            tmpMapValueInfo.strValue = allWhiteListInfo[loopi].customTime;
            recordRuleMap.insert(map<string, struct mapValueInfo>::value_type (allWhiteListInfo[loopi].allowNumber, tmpMapValueInfo));
        }

        //There is data in the database
        std::string  tmpNumber = "";
        std::string  customTime = "";
        for(int loopNum = 0;loopNum<whiteListInfo.size();loopNum++)
        {
            updateFlag = 0;
            insertFlag = 0;

            map<string ,struct mapValueInfo >::iterator mapIt;;
            tmpNumber = "";
            customTime = "";
            tmpNumber = whiteListInfo[loopNum].allowNumber;
            customTime = whiteListInfo[loopNum].customTime;
            mapIt = recordRuleMap.find(tmpNumber);
            if(mapIt == recordRuleMap.end())
            {            
                insertFlag = 1;
            }
            else
            {    
                if( mapIt->second.strValue != customTime)
                {
                    updateFlag = 1;
                }
                else
                {
                    //recordRuleMap.erase(mapIt);
                    mapIt->second.flag = 1;
                }
            }
    
            if(updateFlag == 1)
            {
                std::string updateSql = "update T_RuleWhiteList set CustomTime  = '";
                updateSql += customTime + "' where NumberID = '";
                updateSql += numberId +"' and AllowNumber  = '";
                updateSql += tmpNumber + "';";
                if(0 != dbmysql_->executenonquery(updateSql))
                {    
                    return E_DATABASE_ERROR;
                }
                //recordRuleMap.erase(mapIt);
                mapIt->second.flag = 1;
             }
             if(insertFlag ==1)
             {
                 std::string insertSql = "";
                 std::string ruleID = "";
                 generateUUID(ruleID);
        
                 insertSql = "insert into T_RuleWhiteList(RuleID,NumberID,AllowNumber,CustomTime) values('";
                 insertSql += ruleID + "','";
                 insertSql += numberId + "','";
                 insertSql += tmpNumber + "','";
                 insertSql += customTime + "');";
                 if(0 != dbmysql_->executenonquery(insertSql))
                 {
                    return E_DATABASE_ERROR;
                 }
             }
         }
         map<string ,struct mapValueInfo >::iterator mapItLeft;;
         for(mapItLeft = recordRuleMap.begin();mapItLeft!=recordRuleMap.end();mapItLeft++)
         {
             if(mapItLeft->second.flag !=1)
             {
                 std::string delSql = "delete from T_RuleWhiteList where NumberID = '";
                 delSql += numberId + "' and AllowNumber = '";
                 delSql += mapItLeft->first + "';";
                 if(0 != dbmysql_->executenonquery(delSql))
                 {
                     return E_DATABASE_ERROR;
                 }
             }
         }
    }
    return  E_OPERATOR_SUCCED; 
}

/***********************************************************************************
*
* NAME       : updateRuleRecordWithCS 
*
* CLASS NAME : DataUsage
*
* FUNCTION   :  
*
* INPUT      : 
*
* OUTPUT     : 
*
* RETURN     :          
*              
*
***********************************************************************************/ 
ResultDataUsage DataUsage::updateRuleRecordInfo(vector<SyncRecordRuleInfo> &ruleRecordInfo, std::string & numberId)
{
    int vectorSize = ruleRecordInfo.size();
    if(vectorSize == 0)  //no data need to be sync
    {
        return E_NO_DATA_MATCHED;
    }
    int updateFlag = 0;
    int insertFlag = 0;

    //Compare 1 : To get RuleRecord infomation
    vector<string> recordRuleInfo;
    vector<SyncRecordRuleInfo>  allRecordRule;
    ResultDataUsage resultTmp = getRuleRecordInfo(allRecordRule,numberId);
    if(resultTmp == E_DATABASE_ERROR)
    {
          return resultTmp;
    }
    else if(resultTmp == E_NO_DATA_MATCHED)
    {
        if(vectorSize == 0)
        {
            return  E_OPERATOR_SUCCED; 
        }
        std::string insertSql = "";
        std::string ruleID = "";
        for(int index=0;index<vectorSize;index++)
        {
            insertSql = "";
            ruleID = "";
            //generate UUID
            generateUUID(ruleID);
            //insert T_ServiceCustom
            insertSql = "insert into T_RuleRecord (RuleID,NumberID,AllowNumber,CustomTime) values ('";
            insertSql += ruleID + "','";
            insertSql += numberId + "','";
            insertSql += ruleRecordInfo[index].allowNumber + "','";
            insertSql += ruleRecordInfo[index].customTime + "');";
            if(0 != dbmysql_->executenonquery(insertSql))
            {
                return E_DATABASE_ERROR;
            }
        }
        return  E_OPERATOR_SUCCED; 
    }
      else if(resultTmp == E_OPERATOR_SUCCED)
    {
        //To check there has data in the center database.    
        std::string delSql = "";
        if(vectorSize == 0)
        {
            for(int index=0;index<vectorSize;index++)
            {
                delSql = "";
                delSql = "delete from T_RuleRecord where NumberID = '";
                delSql += numberId + "' and AllowNumber = '";
                delSql += ruleRecordInfo[index].allowNumber + "';";
                if(0 != dbmysql_->executenonquery(delSql))
                {
                    return E_DATABASE_ERROR;
                }
            }
            return  E_OPERATOR_SUCCED; 
        }
        //step 2 To construct the maping with these rulerecord datas.
        //If there is no unynchronized data on the local databace server.
        map<std::string, struct mapValueInfo> recordRuleMap;
        struct mapValueInfo tmpMapValueInfo;
        tmpMapValueInfo.flag = 0;
        tmpMapValueInfo.strValue = "";

        for(int loopi = 0;loopi<vectorSize;loopi++)
        {
            tmpMapValueInfo.strValue = ruleRecordInfo[loopi].customTime;
            recordRuleMap.insert(map<string, struct mapValueInfo>::value_type (ruleRecordInfo[loopi].allowNumber,tmpMapValueInfo ));
        }

        //There is data in the database
        std::string  allowNumber = "";
        std::string  customTime = "";
        for(int loopNum = 0;loopNum<allRecordRule.size();loopNum++)
        {
            updateFlag = 0;
            insertFlag = 0;

            map<string ,struct mapValueInfo >::iterator mapIt;;
            allowNumber = "";
            customTime = "";
            allowNumber = allRecordRule[loopNum].allowNumber;
            customTime = allRecordRule[loopNum].customTime;
            mapIt = recordRuleMap.find(allowNumber);
            if(mapIt == recordRuleMap.end())
            {            
                insertFlag = 1;
            }
            else
            {    
                if( mapIt->second.strValue != customTime)
                {
                    updateFlag = 1;
                }
                else
                {
                    mapIt->second.flag = 1;
                }
            }
    
            if(updateFlag == 1)
            {
                std::string updateSql = "update T_RuleRecord set CustomTime = '";
                updateSql += customTime + "' where NumberID = '";
                updateSql += numberId +"' and AllowNumber  = '";
                updateSql += allowNumber +"';";
                if(0 != dbmysql_->executenonquery(updateSql))
                {    
                    return E_DATABASE_ERROR;
                }
                mapIt->second.flag = 1;
             }
             if(insertFlag ==1)
             {
                 std::string insertSql = "";
                 std::string ruleID = "";
                 generateUUID(ruleID);
        
                 insertSql = "insert into T_RuleRecord (RuleID,NumberID,AllowNumber,CustomTime) values ('";
                 insertSql += ruleID + "','";
                 insertSql += numberId + "','";
                 insertSql += allowNumber + "','";
                 insertSql += customTime + "');";
                 if(0 != dbmysql_->executenonquery(insertSql))
                 {
                    return E_DATABASE_ERROR;
                 }
             }
         }
         map<string ,struct mapValueInfo >::iterator mapItLeft;;
         for(mapItLeft = recordRuleMap.begin();mapItLeft!=recordRuleMap.end();mapItLeft++)
         {
             if(mapItLeft->second.flag != 1)
             {
                 std::string delSql = "delete from T_RuleRecord where NumberID = '";
                 delSql += numberId + "' and AllowNumber = '";
                 delSql += mapItLeft->first + "';";
                 if(0 != dbmysql_->executenonquery(delSql))
                 {
                     return E_DATABASE_ERROR;
                 }
             }
         }
    }
    return  E_OPERATOR_SUCCED; 
}

/***********************************************************************************
*
* NAME       : updateRuleTimeWithCS 
*
* CLASS NAME : DataUsage
*
* FUNCTION   :  
*
* INPUT      : 
*
* OUTPUT     : 
*
* RETURN     :          
*              
*
***********************************************************************************/ 
ResultDataUsage DataUsage::updateRuleTimeInfo(vector<SyncTimeRuleInfo> &ruleTimeInfo, std::string & numberId)
{
    int vectorSize = ruleTimeInfo.size();
    if(vectorSize == 0)  //no data need to be sync
    {
        return E_NO_DATA_MATCHED;
    }
    int updateFlag = 0;
    int insertFlag = 0;
    //Compare 1 : To get RuleTime infomation
    vector<string> ruleTimeInfoTmp;
    vector<SyncTimeRuleInfo>  allTimeRule;
    ResultDataUsage resultTmp = getRuleTimeInfo(allTimeRule,numberId);
    if(resultTmp == E_DATABASE_ERROR)
    {
          return resultTmp;
    }
    else if(resultTmp == E_NO_DATA_MATCHED)
    {
        if(vectorSize == 0)
        {
            return  E_OPERATOR_SUCCED; 
        }
        std::string insertSql = "";
        std::string ruleID = "";
        for(int index=0;index<vectorSize;index++)
        {
            insertSql = "";
            ruleID = "";
            //generate UUID
            generateUUID(ruleID);
            //insert T_ServiceCustom
            insertSql = "insert into T_RuleTime (RuleID,NumberID,StartTime,EndTime,Week,StrategyID,CustomTime) values('";
            insertSql += ruleID + "','";
            insertSql += numberId + "','";
            insertSql += ruleTimeInfo[index].startTime + "','";
            insertSql += ruleTimeInfo[index].endTime + "','";
            insertSql += intToStr(ruleTimeInfo[index].week) + "','";
            insertSql += intToStr(ruleTimeInfo[index].strategyID) + "','";
            insertSql += ruleTimeInfo[index].customTime + "');";
            if(0 != dbmysql_->executenonquery(insertSql))
            {
                return E_DATABASE_ERROR;
            }
        }
        return  E_OPERATOR_SUCCED; 
    }
      else if(resultTmp == E_OPERATOR_SUCCED)
    {
        //To check there has data in the center database.    
        std::string delSql = "";
        if(vectorSize == 0)
        {
            for(int index = 0;index < vectorSize;index++)
            {
                delSql = "";
                delSql = "delete from T_RuleTime where NumberID = '";
                delSql += numberId + "' and StrategyID = '";
                delSql += ruleTimeInfo[index].strategyID + "';";
                if(0 != dbmysql_->executenonquery(delSql))
                {
                    return E_DATABASE_ERROR;
                }
            }
            return  E_OPERATOR_SUCCED; 
        }
        //step 2 To construct the maping with these rulerecord datas.
        //If there is no unynchronized data on the local databace server.
        map<std::string, std::string> timeRuleMap;
        char tmpArr[16];
        for(int loopi = 0;loopi<allTimeRule.size();loopi++)
        {
            string tmpIDStr = "";
            memset(tmpArr,'\0',16);
            sprintf(tmpArr,"%d",allTimeRule[loopi].strategyID);
            tmpIDStr = string(tmpArr);
            timeRuleMap.insert(map<std::string, std::string>::value_type (tmpIDStr,allTimeRule[loopi].customTime ));
        }
        //There is data in the database
        std::string  startTime = "";
        std::string  endTime = "";
        std::string  week = "";
        std::string  strategryId = "";
        std::string  oldStrId = "";
        std::string  customTime = "";

        for(int loopNum = 0;loopNum<ruleTimeInfo.size();loopNum++)
        {
            int ivrType=0;
            updateFlag = 0;
            insertFlag = 0;

            map<string ,string>::iterator mapIt;;
            startTime = "";
            endTime = "";
            week ="";
            strategryId = "";
            customTime = "";

            startTime = ruleTimeInfo[loopNum].startTime;
            endTime = ruleTimeInfo[loopNum].endTime;
            week = ruleTimeInfo[loopNum].week;

            memset(tmpArr,'\0',16);
            sprintf(tmpArr,"%d",ruleTimeInfo[loopNum].strategyID);
            strategryId = string(tmpArr);

            memset(tmpArr,'\0',16);
            sprintf(tmpArr,"%d",ruleTimeInfo[loopNum].week);
            week = string(tmpArr);
            customTime = ruleTimeInfo[loopNum].customTime;
            if(ruleTimeInfo[loopNum].strategyID<= TIMELIMITSTRATEGY_NINE)
            {
                ivrType=1;
            }
            int interIvrType;
            int inVecDataSize = allTimeRule.size();
            for(int loopIn = 0;loopIn<inVecDataSize;loopIn++)
            {
                oldStrId = "";
                oldStrId = intToStr(allTimeRule[loopIn].strategyID);
                interIvrType = 0;
                if(allTimeRule[loopIn].strategyID <= TIMELIMITSTRATEGY_NINE)
                {
                    interIvrType = 1;
                }

                if( ivrType==1 && interIvrType ==1)
                {
                    insertFlag =0;
                    if(allTimeRule[loopIn].strategyID != ruleTimeInfo[loopNum].strategyID)
                    {
                        updateFlag =1;
                        break;
                    }
                    if(allTimeRule[loopIn].week != ruleTimeInfo[loopNum].week)
                    {
                        updateFlag =1;
                        break;
                    }
                    if(allTimeRule[loopIn].startTime != ruleTimeInfo[loopNum].startTime)
                    {
                        updateFlag =1;
                        break;
                    }
                    if(allTimeRule[loopIn].endTime != ruleTimeInfo[loopNum].endTime)
                    {
                        updateFlag =1;
                        break;
                    }
                    if(allTimeRule[loopIn].customTime != ruleTimeInfo[loopNum].customTime)
                    {
                        updateFlag =1;
                        break;
                    }        
                    timeRuleMap.erase(strategryId);
                    break;
                }
                else if(ivrType==0 && interIvrType ==0)
                {
                    insertFlag =0;
                    if(allTimeRule[loopIn].strategyID != ruleTimeInfo[loopNum].strategyID)
                    {
                        updateFlag =1;
                        break;
                    }
                    if(allTimeRule[loopIn].week != ruleTimeInfo[loopNum].week)
                    {
                        updateFlag =1;
                        break;
                    }
                    if(allTimeRule[loopIn].startTime != ruleTimeInfo[loopNum].startTime)
                    {
                        updateFlag =1;
                        break;
                    }
                    if(allTimeRule[loopIn].endTime != ruleTimeInfo[loopNum].endTime)
                    {
                        updateFlag =1;
                        break;
                    }
                    if(allTimeRule[loopIn].customTime != ruleTimeInfo[loopNum].customTime)
                    {
                        updateFlag =1;
                        break;
                    }    
                    timeRuleMap.erase(strategryId);
                    break;
                }
                else if((loopIn+1) == inVecDataSize)
                {
                    insertFlag =1;
                }
            }
            if(updateFlag == 1)
            {
                std::string updateSql = "update T_RuleTime set StartTime = '";
                updateSql += ruleTimeInfo[loopNum].startTime + "',EndTime = '";
                updateSql += ruleTimeInfo[loopNum].endTime + "', Week = '";
                updateSql += week + "',StrategyID = '";
                updateSql += strategryId + "',CustomTime = '";
                updateSql += ruleTimeInfo[loopNum].customTime + "' where NumberID = '";
                updateSql += numberId +"'and StrategyID ='";
                updateSql += oldStrId +"';";
                if(0 != dbmysql_->executenonquery(updateSql))
                {    
                    return E_DATABASE_ERROR;
                }
                //Delete the same type record
                timeRuleMap.erase(strategryId);
             }
             if(insertFlag ==1)
             {
                 std::string insertSql = "";
                 std::string ruleID = "";
                 //generate UUID
                 generateUUID(ruleID);
                 //insert T_ServiceCustom
                 insertSql = "insert into T_RuleTime (RuleID,NumberID,StartTime,EndTime,Week,StrategyID,CustomTime) values('";
                 insertSql += ruleID + "','";
                 insertSql += numberId + "','";
                 insertSql += ruleTimeInfo[loopNum].startTime + "','";
                 insertSql += ruleTimeInfo[loopNum].endTime + "','";
                 insertSql += week + "','";
                 insertSql += strategryId + "','";
                 insertSql += ruleTimeInfo[loopNum].customTime + "');";
             }
         }
         map<string ,string >::iterator mapItLeft;
         for(mapItLeft = timeRuleMap.begin();mapItLeft!=timeRuleMap.end();mapItLeft++)
         {
             std::string delSql = "delete from T_RuleTime where NumberID = '";
             delSql += numberId + "' and StrategyID = '";
             delSql += mapItLeft->first + "';";
             if(0 != dbmysql_->executenonquery(delSql))
             {
                 return E_DATABASE_ERROR;
             }
         }
    }
    return  E_OPERATOR_SUCCED; 
}

/*********************************************************************************************
*
* NAME       : filterMajorNumInfo 
*
* FUNCTION   : The function is used to filter the sql query result and get the major number infomation. 
*
* INPUT      : it  DB_RECORDSET_ARRAY::iterator & : The iterator of the query result.
*
* OUTPUT     : mapUserInfo std::map<std::string,std::string> &:It used to keep these major infomation
*                              The map is faster for serching.
*              vecUserInfo vector<std::string> &:  It used for storage these major number infomation 
*                              by sequence of the finding.
*
* RETURN     : E_OPERATOR_SUCCED :  Sucess.
*
**********************************************************************************************/ 
ResultDataUsage DataUsage::filterMajorNumInfo(DB_RECORDSET_ARRAY::iterator &it, std::map<std::string,std::string> & mapUserInfo,vector<std::string> &vecUserInfo )
{
    std::string userNumber = "";
    std::string imsi = "";
    std::string regTime = "";
    std::string zoneCode = "";
    std::string syncTime = "";
    std::string userId = "";
    std::string userName = "";
    std::string passWD = "";
    std::string synchFlag = "";

    DB_RECORDSET_ITEM item_ = (*it);
    DB_RECORDSET_ITEM::iterator it_ = item_.find("userid");
    if(it_ != item_.end())
    {
        userId = (*it_).second;
        mapUserInfo.insert(map<string, string>::value_type("userid",userId));
        vecUserInfo.push_back(userId);
    }

    it_ = item_.find("usernumber");
    if (it_ != item_.end())
    {
        userNumber = (*it_).second;
        mapUserInfo.insert(map<string, string>::value_type("usernumber",userNumber));
        vecUserInfo.push_back(userNumber);
    }

    it_ = item_.find("imsi");
    if(it_ != item_.end())
    {
        imsi = (*it_).second;
        mapUserInfo.insert(map<string, string>::value_type("imsi",imsi));
        vecUserInfo.push_back(imsi);
    }

    it_ = item_.find("username");
    if(it_ != item_.end())
    {
        userName = (*it_).second;
        mapUserInfo.insert(map<string, string>::value_type("username",userName));
        vecUserInfo.push_back(userName);
    }

    it_ = item_.find("pwd");
    if(it_ != item_.end())
    {
        passWD = (*it_).second;
        mapUserInfo.insert(map<string, string>::value_type("pwd",passWD));
        vecUserInfo.push_back(passWD);
    }

    it_ = item_.find("zonecode");
    if(it_ != item_.end())
    {
        zoneCode = (*it_).second;
        mapUserInfo.insert(map<string, string>::value_type("zonecode",zoneCode));
        vecUserInfo.push_back(zoneCode);
    }

    it_ = item_.find("registeredtime");
    if(it_ != item_.end())
    {
        regTime = (*it_).second;
        mapUserInfo.insert(map<string, string>::value_type("registeredtime",regTime));
        vecUserInfo.push_back(regTime);
    }

    it_ = item_.find("issynchronization");
    if(it_ != item_.end())
    {
        synchFlag = (*it_).second;
        mapUserInfo.insert(map<string, string>::value_type("issynchronization",synchFlag));
        vecUserInfo.push_back(synchFlag);
    }

    it_ = item_.find("synchronizationtime");
    if(it_ != item_.end())
    {
        syncTime = (*it_).second ;
        mapUserInfo.insert(map<string, string>::value_type("synctime",syncTime));
        vecUserInfo.push_back(syncTime);
    }
    return E_OPERATOR_SUCCED;
}

/*********************************************************************************************
*
* NAME       : filterMinorNumInfo 
*
* FUNCTION   : The function is used to filter the sql query result and get the minor infomation. 
*
* INPUT      : it  DB_RECORDSET_ARRAY::iterator & : The iterator of the query result.
*
* OUTPUT     :  mapMinorInfo std::map<std::string,std::string> &:It used to keep these minor infomation
*                              The map is faster for serching.
*              vecMinorInfo vector<std::string> &:  It used for storage these minor number infomation 
*                              by sequence of the finding.
*
* RETURN     : E_OPERATOR_SUCCED :  Sucess.
*
**********************************************************************************************/ 
ResultDataUsage DataUsage::filterMinorNumInfo(DB_RECORDSET_ARRAY::iterator &it, std::map<std::string,std::string> & mapMinorInfo,vector<std::string> &vecMinorInfo)
{
    std::string numType = "";
    std::string seqNum = "";
    std::string regType = "";
    std::string stateId = "";
    std::string recordRule = "";

    std::string minorNumber = "";
    std::string imsi = "";
    std::string regTime = "";
    std::string zoneCode = "";
    std::string syncTime = "";
    std::string numberID = "";
    std::string userID = "";

    //LogFileManager::getInstance()->write(Brief,"INFO: Get major info s, UserID-->%s",userId.c_str());
    DB_RECORDSET_ITEM item_ = (*it);
    DB_RECORDSET_ITEM::iterator it_ = item_.find("minornumber");
    if (it_ != item_.end())
    {
        minorNumber = (*it_).second;
        mapMinorInfo.insert(map<string, string>::value_type("minornumber",minorNumber));
        vecMinorInfo.push_back(minorNumber);
    }
    it_ = item_.find("type");
    if(it_ != item_.end())
    {
        numType = (*it_).second;
        mapMinorInfo.insert(map<string, string>::value_type("type",numType));
        vecMinorInfo.push_back(numType);
    }
    it_ = item_.find("sequenceno");
    if(it_ != item_.end())
    {
        seqNum = (*it_).second;
        mapMinorInfo.insert(map<string, string>::value_type("seqNum",seqNum));
        vecMinorInfo.push_back(seqNum);
    }
    it_ = item_.find("registertime");
    if(it_ != item_.end())
    {
        regTime = (*it_).second;
        mapMinorInfo.insert(map<string, string>::value_type("registertime",regTime));
        vecMinorInfo.push_back(regTime);
    }
    it_ = item_.find("registertype");
    if(it_ != item_.end())
    {
        regType = (*it_).second;
        mapMinorInfo.insert(map<string, string>::value_type("registertype",regType));
        vecMinorInfo.push_back(regType);
    }
    it_ = item_.find("stateid");
    if(it_ != item_.end())
    {
        stateId = (*it_).second;
        mapMinorInfo.insert(map<string, string>::value_type("stateid",stateId));
        vecMinorInfo.push_back(stateId);
    }
    it_ = item_.find("imsi");
    if(it_ != item_.end())
    {
        imsi = (*it_).second;
        mapMinorInfo.insert(map<string, string>::value_type("imsi",imsi));
        vecMinorInfo.push_back(imsi);
    }
    it_ = item_.find("recordrule");
    if(it_ != item_.end())
    {
        recordRule = (*it_).second;
        mapMinorInfo.insert(map<string, string>::value_type("recordrule",recordRule));
        vecMinorInfo.push_back(recordRule);
    }
    it_ = item_.find("zonecode");
    if(it_ != item_.end())
    {
        zoneCode = (*it_).second;
        mapMinorInfo.insert(map<string, string>::value_type("zonecode",zoneCode));
        vecMinorInfo.push_back(zoneCode);
    }
    return E_OPERATOR_SUCCED;
}


/*****************************************************************************************************
*
* NAME       : getSyncMajorNumInfo 
*
* FUNCTION   : The function is used to get the major infomation which is no synchronized and order 
*              by synchronized time. 
*
* INPUT      : it  DB_RECORDSET_ARRAY::iterator & : The iterator of the query result.
*
* OUTPUT     :  mapMinorInfo std::map<std::string,std::string> &:It used to keep these minor infomation
*                              The map is faster for serching.
*              vecMinorInfo vector<std::string> &:  It used for storage these minor number infomation 
*                              by sequence of the finding.
*
* RETURN     : E_OPERATOR_SUCCED :  Sucess.
*
******************************************************************************************************/ 
ResultDataUsage DataUsage::getSyncMajorNumInfo(SyncUserNumberInfo & userInfo, IsSynchronized syncFlag, SynchronDataType  dataType,std::string & syncTime)
{
    //Check the major phone number has been registered the service.
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "";
    string userId = "";
    switch(dataType)
    {
    case REGISTERUSER :
        sql = "select UserID, UserNumber ,RegisteredTime, IMSI, ZoneCode ,SynchronizationTime from T_RegisterUser where IsSynchronization = 0 and ZoneCode in ";
        sql +=  zoneCodeStr_ +" order by SynchronizationTime asc limit 1;";
        break;
    case REGISTERUSER_BAK:
        sql = "select UserID, UserNumber, RegisteredTime, IMSI, ZoneCode ,SynchronizationTime from t_registeruser_bak where IsSynchronization = 0 and ZoneCode in ";
        sql +=  zoneCodeStr_ +" order by SynchronizationTime asc limit 1;";
        break;
    default:
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [getSyncMajorNumInfo] Data type is no match!");    
        break;
    }
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [getSyncMajorNumInfo] database operation error!");
        return E_DATABASE_ERROR;
    }
    
    if (dbrecord_.empty())
    {
        return E_NO_DATA_MATCHED; 
    }
    //To check the Synchronization data in the databse..
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        map<string,string> mapUserInfo;
        vector<string> tmpVecInfo;
        filterMajorNumInfo(it,mapUserInfo,tmpVecInfo);
        userInfo.number = mapUserInfo.find("usernumber")->second;
        userInfo.IMSI = mapUserInfo.find("imsi")->second;
        userInfo.registeredTime =  mapUserInfo.find("registeredtime")->second;
        userInfo.zoneCode = mapUserInfo.find("zonecode")->second;
        userId = mapUserInfo.find("userid")->second;
        syncTime = mapUserInfo.find("synctime")->second;
        break;
    }
    //To check whether there is custom service and SMS notify setting with the userID selected unin join two tables.  
    userInfo.SMSNotify.isAccepted = 0;
    userInfo.SMSNotify.deliverCount = 0;
    userInfo.SMSNotify.updateTime = "";
    userInfo.serviceCustom.customTime = "";
    userInfo.serviceCustom.isChangeCallerDisplay = 0;

    ResultDataUsage queryResult;
    SyncSMSNotifyInfo  smsNotifyInfo;
    SyncServiceCustomInfo  customServiceInfo;
    queryResult = getSMSNotifyInfo(smsNotifyInfo, userId);
    if(queryResult == E_DATABASE_ERROR)
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [getSyncMajorNumInfo] database operation error!");    
        return queryResult;
    }
    else if(queryResult == E_OPERATOR_SUCCED)
    {
        userInfo.SMSNotify.isAccepted = smsNotifyInfo.isAccepted;
        userInfo.SMSNotify.deliverCount = smsNotifyInfo.deliverCount;
        userInfo.SMSNotify.updateTime = smsNotifyInfo.updateTime;
    }
    queryResult = getCustomServiceInfo(customServiceInfo,userId);
    if(queryResult == E_DATABASE_ERROR)
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [getSyncMajorNumInfo] database operation error!");    
        return queryResult;
    }
    else if(queryResult == E_OPERATOR_SUCCED)
    {
        userInfo.serviceCustom.isChangeCallerDisplay = customServiceInfo.isChangeCallerDisplay;
        userInfo.serviceCustom.customTime = customServiceInfo.customTime;
    }
    return E_OPERATOR_SUCCED;
}

/*************************************************************************************************
*
* NAME       : getSMSNotifyInfo 
*
* FUNCTION   : The function is used to get the sms infomation about the major number. 
*
* INPUT      : userId  std::string & : The type is note that the data's synchronized flag.
*
* OUTPUT     : smsNotifyInfo  SyncSMSNotifyInfo &: The infomation of the short message setting of the 
*                                 user major number.
*
* RETURN     : E_NO_DATA_MATCHED :  There is no data found with the inputed type.         
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_OPERATOR_SUCCED :  Sucess.
*
*************************************************************************************************/ 
ResultDataUsage DataUsage::getSMSNotifyInfo(SyncSMSNotifyInfo & smsNotifyInfo,std::string & userId)
{
    std::string chkSql = "select * from T_SMSNotify where UserID = '";
    chkSql += userId + "' ;";
    std::string  userID = "";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"The check getSMSNotifyInfo  error");
        return E_DATABASE_ERROR;
    }

    //If there is no unynchronized data on the local databace server.
    if (dbrecord_.empty())
    {
        return E_NO_DATA_MATCHED;
    }
    else
    {
        //There is data in the database
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        std::string  isAccepted = "";
        std::string  deliverCount = "";
        std::string  updateTime = "";
        while (it!=dbrecord_.end())
        {
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("isaccepted");
            if (it_ != item_.end())
            {
                isAccepted  = (*it_).second;
            }
            it_ = item_.find("delivercount");
            if (it_ != item_.end())
            {
                deliverCount  = (*it_).second;
            }
            it_ = item_.find("updatetime");
            if (it_ != item_.end())
            {
                updateTime  = (*it_).second;
            }
            //No need to it++,only there use one data.
            break;
        }
        smsNotifyInfo.isAccepted = atoi(isAccepted.c_str());
        smsNotifyInfo.deliverCount = atoi(deliverCount.c_str());
        smsNotifyInfo.updateTime = updateTime;        
    }
    return E_OPERATOR_SUCCED;
}

/*************************************************************************************************
*
* NAME       : getCustomServiceInfo 
*
* FUNCTION   : The function is used to get the customer service infomation about the major number. 
*
* INPUT      : userId  std::string & : The type is note that the data's synchronized flag.
*
* OUTPUT     : customServiceInfo  SyncServiceCustomInfo &: The infomation of the customer service of the 
*                                 user customer for the minor number.
*
* RETURN     : E_NO_DATA_MATCHED :  There is no data found with the inputed type.         
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_OPERATOR_SUCCED :  Sucess.
*
*************************************************************************************************/ 
ResultDataUsage DataUsage::getCustomServiceInfo(SyncServiceCustomInfo & customServiceInfo,std::string & userId)
{
    std::string chkSql = "select * from T_ServiceCustom where UserID = '";
    chkSql += userId + "' ;";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }

    //If there is no unynchronized data on the local databace server.
    if (dbrecord_.empty())
    {
        return E_NO_DATA_MATCHED;
    }
    else
    {
        //There is data in the database
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        std::string  isDisplay = "";
        std::string  customTime = "";
        while (it!=dbrecord_.end())
        {
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("ischangecallerdisplay");
            if (it_ != item_.end())
            {
                isDisplay  = (*it_).second;
            }
            it_ = item_.find("customtime");
            if (it_ != item_.end())
            {
                customTime  = (*it_).second;
            }
            //No need to it++,only there use one data.
            break;
        }
        customServiceInfo.isChangeCallerDisplay = atoi(isDisplay.c_str());
        customServiceInfo.customTime = customTime;    
    }
    return E_OPERATOR_SUCCED;
}

/*************************************************************************************************
*
* NAME       : getBlackListInfo 
*
* FUNCTION   : The function is used to get the black list number infomation about the minor number. 
*
* INPUT      : numberId  std::string & : The type is note that the data's synchronized flag.
*
* OUTPUT     : blackList  vector<SyncBlackListInfo>  &: The infomation of the black list infomation.
*
* RETURN     : E_NO_DATA_MATCHED :  There is no data found with the inputed type.         
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_OPERATOR_SUCCED :  Sucess.
*
*************************************************************************************************/ 
ResultDataUsage DataUsage::getBlackListInfo(vector<SyncBlackListInfo>  & blackList,std::string & numberId)
{
    std::string chkSql = "select ForbidNumber,CustomTime from T_RuleBlackList where NumberID = '" + numberId +"' ;";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }
    //If there is no unynchronized data on the local databace server.
    if (dbrecord_.empty())
    {
        return E_NO_DATA_MATCHED;
    }
    else
    {
        //There is data in the database
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        while (it!=dbrecord_.end())
        {
            SyncBlackListInfo blackInfoObj;
            std::string forbidNumber = "";
            std::string customTime = "";
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("forbidnumber");
            if (it_ != item_.end())
            {
                forbidNumber = (*it_).second;
            }
            it_ = item_.find("customtime");
            if(it_ != item_.end())
            {
                customTime = (*it_).second;
            }
            blackInfoObj.forbidNumber = forbidNumber;
            blackInfoObj.customTime = customTime;
            blackList.push_back(blackInfoObj);
            ++it;
        }
    }
    return E_OPERATOR_SUCCED;
}

/*************************************************************************************************
*
* NAME       : getWhiteListInfo 
*
* FUNCTION   : The function is used to get the white list number infomation about the minor number. 
*
* INPUT      : numberId  std::string & : The type is note that the data's synchronized flag.
*
* OUTPUT     : whiteList  vector<SyncWhiteListInfo>  &: The infomation of the white list infomation.
*
* RETURN     : E_NO_DATA_MATCHED :  There is no data found with the inputed type.         
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_OPERATOR_SUCCED :  Sucess.
*
*************************************************************************************************/ 
ResultDataUsage DataUsage::getWhiteListInfo(vector<SyncWhiteListInfo>  & whiteList,std::string & numberId)
{
    std::string chkSql = "select AllowNumber,CustomTime from T_RuleWhiteList where NumberID = '" + numberId +"' ;";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }
    //If there is no unynchronized data on the local databace server.
    if (dbrecord_.empty())
    {
        return E_NO_DATA_MATCHED;
    }
    else
    {
        //There is data in the database
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        while (it!=dbrecord_.end())
        {
            SyncWhiteListInfo writeInfoObj;
            std::string allowNum = "";
            std::string customTimeW = "";
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("allownumber");
            if (it_ != item_.end())
            {
                allowNum = (*it_).second;
            }
            it_ = item_.find("customtime");
            if(it_ != item_.end())
            {
                customTimeW = (*it_).second;
            }
            writeInfoObj.allowNumber = allowNum;
            writeInfoObj.customTime = customTimeW;
            whiteList.push_back(writeInfoObj);
            ++it;
        }
    }
    return E_OPERATOR_SUCCED;
}

/*************************************************************************************************
*
* NAME       : getRuleRecordInfo 
*
* FUNCTION   : The function is used to get the number which allowed to call forward to the voice box. 
*
* INPUT      : numberId  std::string & : The type is note that the data's synchronized flag.
*
* OUTPUT     : ruleTimeInfo  vector<SyncRecordRuleInfo>  &: The infomation of the record.
*
* RETURN     : E_NO_DATA_MATCHED :  There is no data found with the inputed type.         
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_OPERATOR_SUCCED :  Sucess.
*
*************************************************************************************************/ 
ResultDataUsage DataUsage::getRuleRecordInfo(vector<SyncRecordRuleInfo>  & ruleRecordInfo,std::string & numberId)
{
    std::string chkSql = "select AllowNumber,CustomTime from T_RuleRecord where NumberID = '" + numberId +"' ;";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }

    //If there is no unynchronized data on the local databace server.
    if (dbrecord_.empty())
    {
        return E_NO_DATA_MATCHED;
    }
    else
    {
        //There is data in the database
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        while (it!=dbrecord_.end())
        {
            SyncRecordRuleInfo recordRuleObj;
            std::string allowNumRuleRecord = "";
            std::string customTimeRR = "";
              
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("allownumber");
            if (it_ != item_.end())
            {
                allowNumRuleRecord = (*it_).second;
            }
            it_ = item_.find("customtime");
            if(it_ != item_.end())
            {
                customTimeRR = (*it_).second;
            }
            recordRuleObj.allowNumber = allowNumRuleRecord;
            recordRuleObj.customTime = customTimeRR;
            ruleRecordInfo.push_back(recordRuleObj);
            ++it;
        }
    }
    return E_OPERATOR_SUCCED;
}

/*************************************************************************************************
*
* NAME       : getRuleTimeInfo 
*
* FUNCTION   : The function is used to get the rule time setting if there is data about minor number. 
*
* INPUT      : numberId  std::string & : The type is note that the data's synchronized flag.
*
* OUTPUT     : ruleTimeInfo  vector<SyncTimeRuleInfo>  &: The infomation of the timerule.
*
* RETURN     : E_NO_DATA_MATCHED :  There is no data found with the inputed type.         
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_OPERATOR_SUCCED :  Sucess.
*
*************************************************************************************************/ 
ResultDataUsage DataUsage::getRuleTimeInfo(vector<SyncTimeRuleInfo>  & ruleTimeInfo,std::string & numberId)
{
    std::string chkSql = "select StartTime,EndTime,Week,StrategyID,CustomTime from T_RuleTime where NumberID = '" + numberId +"' ;";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }
    //If there is no unynchronized data on the local databace server.
    if (dbrecord_.empty())
    {
        return E_NO_DATA_MATCHED;
    }
    else
    {
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        while (it!=dbrecord_.end())
        {
            SyncTimeRuleInfo ruleTimeObj;
            std::string startTime = "";
            std::string endTime = "";
            std::string customTimeR = "";
            int strateryId = 0 ;
            int week = 0;
               
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("starttime");
            if (it_ != item_.end())
            {
                startTime = (*it_).second;
            }
            it_ = item_.find("endtime");
            if(it_ != item_.end())
            {
                endTime = (*it_).second;
            }
            it_ = item_.find("week");
            if (it_ != item_.end())
            {
                week = atoi((*it_).second.c_str());
            }
            it_ = item_.find("strategyid");
            if(it_ != item_.end())
            {
                strateryId = atoi((*it_).second.c_str());
            }
            it_ = item_.find("customtime");
            if(it_ != item_.end())
            {
                customTimeR =  (*it_).second;
            }
            ruleTimeObj.startTime = startTime;
            ruleTimeObj.endTime = endTime;
            ruleTimeObj.customTime = customTimeR;
            ruleTimeObj.strategyID = strateryId;
            ruleTimeObj.week = week;
            ruleTimeInfo.push_back(ruleTimeObj);
            ++it;
        }
    }
    return E_OPERATOR_SUCCED;
}

/*****************************************************************************************************
*
* NAME       : getSyncMinorNumInfo 
*
* FUNCTION   : The function is used to get the minor infomation with the synchronized flag 
*              and type of the data input. 
*
* INPUT      : syncFlag  IsSynchronized& : The type is note that the data's synchronized flag.
*              dataType  SynchronDataType & :The type of the data such as MINORNUMBER,MINORNUMBER_BAK.
*
* OUTPUT     : minorInfo  SyncMinorNumberInfo &: The minor numbers's infomation that we found..
*
* RETURN     : E_NO_DATA_MATCHED :  There is no data found with the inputed type.         
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_OPERATOR_SUCCED :  Sucess.
*              E_MAJORNUMBER_NOT_EXESIT :Need app to set the sync flag.
*
******************************************************************************************************/ 
ResultDataUsage DataUsage::getSyncMinorNumInfo(SyncMinorNumberInfo & minorInfo, IsSynchronized syncFlag, SynchronDataType  dataType ,std::string & syncTime)
{
    std::string numberID = "";
    std::string userID = "";
    std::string sql = "";
    ResultDataUsage result;
    DB_RECORDSET_ARRAY dbrecord_;
    //Construct the sql content to query the matching data in the local database.   
    switch(dataType)
    {
    case MINORNUMBER:
        sql = "select UserID,NumberID,MinorNumber,Type,SequenceNo,RegisterTime,RegisterType,StateID,IMSI,RecordRule,ZoneCode,SynchronizationTime from T_MinorNumber where IsSynchronization = 0";
        //sql += " and ZoneCode = '" + localZoneCode_ ;
        //sql += "' order by SynchronizationTime ASC limit 1;";
        sql += " and ZoneCode in " + zoneCodeStr_ ;
        sql += " order by SynchronizationTime ASC limit 1;";
        break;
    case MINORNUMBER_BAK:
        sql = "select UserID,NumberID,MinorNumber,Type,SequenceNo,RegisterTime,RegisterType,StateID,IMSI,RecordRule,ZoneCode,SynchronizationTime from t_minornumber_bak where IsSynchronization = 0";
        sql += " and ZoneCode in " + zoneCodeStr_ ;
        sql += " order by SynchronizationTime ASC limit 1;";
        break;
    default:
        LogFileManager::getInstance()->write(Brief,"Warn INFO: [getSyncMinorNumInfo] The input number type status is E_NO_DATA_MATCHED!");
        return E_NO_DATA_MATCHED;
    }
    //Check the query rsult.
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [getSyncMinorNumInfo] database operation error!");    
        return E_DATABASE_ERROR;
    }
    
    if (dbrecord_.empty())
    {
        return E_NO_DATA_MATCHED; 
    }

    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        std::map<std::string,std::string>  mapMinorInfo;
        vector<std::string>  vecMinorInfo;
        //Filter the minor number infomation from the sql query result. 
        filterMinorNumInfo(it, mapMinorInfo,vecMinorInfo);
        //To construct the minor infomation as the output value.
        minorInfo.number = mapMinorInfo.find("minornumber")->second;
        minorInfo.type = atoi(mapMinorInfo.find("type")->second.c_str());
        minorInfo.sequenceNo = atoi(mapMinorInfo.find("seqNum")->second.c_str());
        minorInfo.registerTime = mapMinorInfo.find("registertime")->second;
        minorInfo.registerType = atoi(mapMinorInfo.find("registertype")->second.c_str());
        minorInfo.stateID = atoi(mapMinorInfo.find("stateid")->second.c_str());
        minorInfo.IMSI = mapMinorInfo.find("imsi")->second;
        minorInfo.recordRule = atoi(mapMinorInfo.find("recordrule")->second.c_str());
        minorInfo.zoneCode = mapMinorInfo.find("zonecode")->second;

        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("userid");
        if(it_ != item_.end())
        {
            userID =  (*it_).second;
        }
        it_ = item_.find("numberid");
        if(it_ != item_.end())
        {
            numberID =  (*it_).second;
        }
        it_ = item_.find("synchronizationtime");
        if(it_ != item_.end())
        {
            syncTime =  (*it_).second;
        }            
        break;
    }
    if(dataType == MINORNUMBER)
    {
        sql = "";
        sql = "select * from T_RegisterUser where  UserID = '"+userID +"';";
    
        std::map<std::string,std::string>  mapUserInfo;
        vector<string> tmpVecUInfo;
        DB_RECORDSET_ARRAY dbrecordUser_;
        if(0 != dbmysql_->executequery(sql,dbrecordUser_))
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: [getSyncMinorNumInfo] database operation error!");
            return E_DATABASE_ERROR;
        }

        DB_RECORDSET_ARRAY::iterator userIt = dbrecordUser_.begin();
        if(!dbrecordUser_.empty())
        {
            filterMajorNumInfo(userIt, mapUserInfo,tmpVecUInfo);
            minorInfo.userNumber = mapUserInfo.find("usernumber")->second;
        }
        else
        {
            return E_MAJORNUMBER_NOT_EXESIT;
        }
    }
    //To check whether it is need to get the relation table infomation.
    switch(dataType)
    {
    case MINORNUMBER:
        //To get the relation table infomation with the Minornumber .
        result = getBlackListInfo(minorInfo.blackList,numberID);
        if(result == E_DATABASE_ERROR)
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: [getSyncMinorNumInfo] database operation error!");
            return result;
        }
        result = getWhiteListInfo(minorInfo.whiteList,numberID);
        if(result == E_DATABASE_ERROR)
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: [getSyncMinorNumInfo] database operation error!");
            return result;
        }        
        result = getRuleRecordInfo(minorInfo.recordRuleList, numberID);
        if(result == E_DATABASE_ERROR)
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: [getSyncMinorNumInfo] database operation error!");
            return result;
        }        
        result = getRuleTimeInfo(minorInfo.timeRule,numberID);
        if(result == E_DATABASE_ERROR)
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: [getSyncMinorNumInfo] database operation error!");
            return result;
        }        
        break;
    case MINORNUMBER_BAK:
        break;
    default:
        break;
    }
    return E_OPERATOR_SUCCED;
}

/*****************************************************************************************************
 *
 * NAME       : syncMajorNumToCS 
 *
 * FUNCTION   : To synchronize the major numberinfomation to the center database server. . 
 *
 * INPUT      : userInfo   SyncUserNumberInfo& : The data which need to be synchronized.
 *
 * OUTPUT     : None
 *
 * RETURN     : E_OPERATOR_SUCCED: It is sucess to set the data to be synchronized.
 *              other result     : The operation is fail .
 *
 ****************************************************************************************************/ 
ResultDataUsage DataUsage::syncMajorNumToCS(const SyncUserNumberInfo userInfo,std::string & userID)
{
    std::string  tmpMajorNum = userInfo.number;
    if(tmpMajorNum.empty())
    {
        return E_NO_DATA_UNSYNCHRONIZED;
    }
    std::string chkSql = "select UserID from T_RegisterUser where  UserNumber = '";
    chkSql += tmpMajorNum + "';";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [syncMajorNumToCS] database operation error!");
        return E_DATABASE_ERROR;
    }
    if(dbrecord_.empty())
    {
        //To get the userID from the local db server.
        std::string userID_;
        generateUUID(userID_);
        std::string insertSql = "insert into T_RegisterUser(UserID,UserNumber,IMSI,ZoneCode,RegisteredTime) values('";
        insertSql += userID_ + "','";
        insertSql += userInfo.number + "','";
        insertSql += userInfo.IMSI + "','";
        insertSql += userInfo.zoneCode + "','";
        insertSql += userInfo.registeredTime + "');";

        if(0 != dbmysql_->executenonquery(insertSql))
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: [syncMajorNumToCS] database operation error!");
            return E_DATABASE_ERROR;
        }
        userID = userID_;
    }
    else
    {
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        while (it!=dbrecord_.end())
        {
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("userid");
            if (it_ != item_.end())
            {
                userID  = (*it_).second;
            }
            ++it;
        }
        //update T_RegisterUser
        std::string updateSql = "update T_RegisterUser set IMSI = '";
        updateSql += userInfo.IMSI + "',ZoneCode = '";
        updateSql += userInfo.zoneCode + "',RegisteredTime = '";
        updateSql += userInfo.registeredTime + "' where  UserNumber = '";
        updateSql += userInfo.number + "';";    
        if(0 != dbmysql_->executenonquery(updateSql))
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: [syncMajorNumToCS] database operation error!");
            return E_DATABASE_ERROR;
        }
    }
    return E_OPERATOR_SUCCED;
}

/*****************************************************************************************************
 *
 * NAME       : updateMinorInfo 
 *
 * FUNCTION   : To synchronize the SMS notify setting to the center database server. 
 *
 * INPUT      : SyncSMSNotifyInfo   SMSNotifyInfo& : The data which need to be synchronized.
 *              userID    std::string &: The major number's ID.
 *
 * OUTPUT     : None
 *
 * RETURN     : E_OPERATOR_SUCCED: It is sucess to set the data to be synchronized.
 *              other result     : The operation is fail .
 *
 ****************************************************************************************************/ 
ResultDataUsage DataUsage::updateMinorInfo(SyncMinorNumberInfo minorInfo,std::string & userId)
{
    int vectorSize = 1;
    ResultDataUsage result;
    if(minorInfo.registerTime.compare(" ") == 0 && minorInfo.number.compare(" ") == 0)
    {
        vectorSize = 0;
    }
    int updateFlag = 0;
    //Compare 1 : minor infomation :to get local minor infomation except the local zone code.
    std::string chkSql = "select NumberID from T_MinorNumber where MinorNumber = '";
    chkSql += minorInfo.number + "' and UserID ='";
    chkSql += userId + "';";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }
    std::string numberID = "";

    //If there is no unynchronized data on the local databace server.
    if (dbrecord_.empty())
    {
        if(vectorSize == 0)  //no data need to be sync
        {
            return E_OPERATOR_SUCCED;
        }
        //insert T_SMSNotify
        generateUUID(numberID);

        std::string insertSql = "insert into T_MinorNumber ( NumberID,MinorNumber,UserID,Type,SequenceNo,RegisterTime," ;
        insertSql +=  "RegisterType,StateID,IMSI,RecordRule,ZoneCode,IsSynchronization,SynchronizationTime) values('";

        insertSql += numberID + "','";
        insertSql += minorInfo.number + "','";
        insertSql += userId + "','";
        insertSql += intToStr(minorInfo.type) + "','";
        insertSql += intToStr(minorInfo.sequenceNo) + "','";
        insertSql += minorInfo.registerTime + "','";
        insertSql += intToStr(minorInfo.registerType) + "','";
        insertSql += intToStr(minorInfo.stateID) + "','";
        insertSql += minorInfo.IMSI + "','";
        insertSql += intToStr(minorInfo.recordRule) + "','";
        insertSql += minorInfo.zoneCode + "','";
        insertSql += "0','";
        insertSql += syncActionTime_ +"');";
        if(0 != dbmysql_->executenonquery(insertSql))
        {
            return E_DATABASE_ERROR;
        }    
    }
    else
    {
        if(vectorSize == 0)  //no data need to be sync
        {
            result = this->deleteMinorNumberFromCS(minorInfo.number);

            if(result == E_DATABASE_ERROR)
            {
                return E_DATABASE_ERROR;
            }
            return  E_OPERATOR_SUCCED; 
        }

        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        while (it!=dbrecord_.end())
        {
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("numberid");
            if (it_ != item_.end())
            {
                numberID = (*it_).second;
            }
            ++it;
        }
        std::string updateSql = "update T_MinorNumber set MinorNumber = '";
        updateSql += minorInfo.number + "',Type = '";
        updateSql += intToStr(minorInfo.type) + "',SequenceNo = '";
        updateSql += intToStr(minorInfo.sequenceNo) + "',RegisterTime = '";
        updateSql += minorInfo.registerTime + "',RegisterType = '";
        updateSql += intToStr(minorInfo.registerType) + "',StateID = '";
        updateSql += intToStr(minorInfo.stateID) + "', IMSI = '";
        updateSql += minorInfo.IMSI + "',RecordRule ='";
        updateSql += intToStr(minorInfo.recordRule) + "',ZoneCode ='";
        updateSql += minorInfo.zoneCode + "',SynchronizationTime = '";
        updateSql +=  syncActionTime_ +"' where MinorNumber = '";
        updateSql += minorInfo.number +"' ;";
        if(0 != dbmysql_->executenonquery(updateSql))
        {
            return E_DATABASE_ERROR;
        }
    }

    /*
    ResultDataUsage rmpResult = getNumberID(minorInfo.number,numberID);
    if(rmpResult == E_DATABASE_ERROR)
    {
        return rmpResult;
    }
    */
    result = updateBlackListInfo(minorInfo.blackList, numberID);
    if(result == E_DATABASE_ERROR)
    {
        return result;
    }
    result = updateRuleRecordInfo(minorInfo.recordRuleList, numberID);
    if(result == E_DATABASE_ERROR)
    {
        return result;
    }
    result = updateRuleTimeInfo(minorInfo.timeRule,numberID);
    if(result == E_DATABASE_ERROR)
    {
        return result;
    }
    result = updateWhiteListInfo(minorInfo.whiteList, numberID);
    if(result == E_DATABASE_ERROR)
    {
        return result;
    }

    return E_OPERATOR_SUCCED;    
}

/*****************************************************************************************************
*
* NAME       : getCSDataInfo 
*
* FUNCTION   : The function is used to get the number infomation and relation table infomation .
*              At the same time to write these data into the pointed file.
*
* INPUT      : fd  FILE * : The handler of the pointed file that is used for keep these infomation.
*
* OUTPUT     : none.
*
* RETURN     : E_NO_DATA_MATCHED :  There is no data found with the inputed type.         
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_OPERATOR_SUCCED :  Sucess.
*
*****************************************************************************************************/ 
ResultDataUsage DataUsage::getCSDataInfo(FILE *fd)
{      
    //Get the major infomation. 
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "";
    string userId = "";
    sql = "select * from T_RegisterUser order by SynchronizationTime desc ;";
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: E_DATABASE_ERROR");
        return E_DATABASE_ERROR;
    }    
    if (dbrecord_.empty())
    {
        return E_NO_DATA_MATCHED; 
    }
    
    //To check the Synchronization data in the databse..
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it!=dbrecord_.end())
    {
        //To filter the major infomation.
        map<string,string> mapUserInfo;
        vector<string> userRegisterInfo;  
        map<string ,string >::iterator mapItLeft;
        filterMajorNumInfo(it,mapUserInfo,userRegisterInfo);
        userId = mapUserInfo.find("userid")->second;     
        ResultDataUsage result;

        //To get the SMS settting
        SyncSMSNotifyInfo  smsNotifyInfo;
        vector<string> smsInfo;
        result = getSMSNotifyInfo(smsNotifyInfo,userId);
        int smsCount = 0;
        if(result == E_DATABASE_ERROR)
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: E_DATABASE_ERROR");
              return result;
        }
          else if(result == E_OPERATOR_SUCCED)
        {
            smsCount =1;
            char tmpCh[32];
            memset(tmpCh,'\0',32);
            std::string acceptedStr="";
            sprintf(tmpCh,"%d",smsNotifyInfo.isAccepted);
            acceptedStr = string(tmpCh);
            smsInfo.push_back(acceptedStr);

            memset(tmpCh,'\0',32);
            std::string deleiveCountStr="";
            sprintf(tmpCh,"%d",smsNotifyInfo.deliverCount);
            deleiveCountStr = string(tmpCh);
            smsInfo.push_back(deleiveCountStr);
            smsInfo.push_back(smsNotifyInfo.updateTime);
        }
        else if(result == E_NO_DATA_MATCHED)
        {
            smsCount = 0;
        }
  
        //To get the custom service settting
        SyncServiceCustomInfo  serviceInfoObj;
        vector<string> serviceInfo;
        int customSerCount = 0;
        result = getCustomServiceInfo(serviceInfoObj,userId);
        if(result == E_DATABASE_ERROR)
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: E_DATABASE_ERROR");
              return result;
        }
          else if(result == E_OPERATOR_SUCCED)
        {
            customSerCount = 1;
            char tmpCh[8];
            memset(tmpCh,'\0',8);
            std::string isDisplay="";
            sprintf(tmpCh,"%d",serviceInfoObj.isChangeCallerDisplay);
            isDisplay = string(tmpCh);
            serviceInfo.push_back(isDisplay);
            serviceInfo.push_back(serviceInfoObj.customTime);
        }
        else if(result == E_NO_DATA_MATCHED)
        {
            customSerCount = 0;
        }
        //To write the    minor number info.
        sql = "";
        sql = "select * from T_MinorNumber where UserID = '";
        sql += userId + "' ;";
        DB_RECORDSET_ARRAY dbrecordMinorNumber_;

        if(0 != dbmysql_->executequery(sql,dbrecordMinorNumber_))
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: E_DATABASE_ERROR");
            return E_DATABASE_ERROR;
        }
       
        //To check and get the  minor number infomation.
        int minorCount = 0;
        minorCount = dbrecordMinorNumber_.size();

        if(minorCount > 0)
        {
            //.s To write user number infomation by whc 0305
            writeInfoToFile(fd,userRegisterInfo);
            //To write the SMS notify setting info.
            fwrite(&smsCount,sizeof(int),1,fd);
            if(smsCount==1)
            {
                writeInfoToFile(fd,smsInfo);
            }
            fwrite(&customSerCount, sizeof(int), 1, fd);
            if(customSerCount==1)
            {
                writeInfoToFile(fd,serviceInfo);
            }
            //.e To write user number infomation by whc 0305
            fwrite(&minorCount, sizeof(int), 1, fd);

            std::string numberID = "";    
            ResultDataUsage resultTmp;
            DB_RECORDSET_ARRAY::iterator itMinor = dbrecordMinorNumber_.begin();
    
            //To filter the minor number infomation and write these relation infomation into the file.
            while (itMinor != dbrecordMinorNumber_.end())
            {                    
                map<string,string> mapMinorInfo;
                vector<string> vectMinorInfo;
                filterMinorNumInfo(itMinor, mapMinorInfo,vectMinorInfo);
                //To write the minor number info.
                writeInfoToFile(fd,vectMinorInfo);

                DB_RECORDSET_ITEM item_ = (*itMinor);
                DB_RECORDSET_ITEM::iterator it_ = item_.find("numberid");
                if(it_ != item_.end())
                {
                    numberID =  (*it_).second;
                }
                //To get the balck list
                int bListCount = 0;
                vector<string> blackNumberInfo;
                vector<SyncBlackListInfo>  allBlackList;
                resultTmp = getBlackListInfo(allBlackList,numberID);
                if(resultTmp == E_DATABASE_ERROR)
                {
                      return resultTmp;
                }
                else if(resultTmp == E_NO_DATA_MATCHED)
                {
                    bListCount = 0;
                }
                else
                {
                    bListCount = allBlackList.size();
                }
                //To write the black count 
                fwrite(&bListCount, sizeof(int), 1, fd);
                for(int loopBNum = 0;loopBNum<bListCount;loopBNum++)
                {
                    blackNumberInfo.push_back(allBlackList[loopBNum].forbidNumber);
                    blackNumberInfo.push_back(allBlackList[loopBNum].customTime);
                    writeInfoToFile(fd,blackNumberInfo);
                    blackNumberInfo.clear();
                }
                //To get the record rule.
                int rrCount = 0;
                vector<string> recordRuleInfo;
                vector<SyncRecordRuleInfo>  allRecordRule;
                resultTmp = getRuleRecordInfo(allRecordRule,numberID);
                if(resultTmp == E_DATABASE_ERROR)
                {
                      return resultTmp;
                }
                else if(resultTmp == E_NO_DATA_MATCHED)
                {
                    rrCount = 0;
                }
                else
                {
                    rrCount = allRecordRule.size();
                }
                //To write the record rule info
                fwrite(&rrCount, sizeof(int), 1, fd);
                for(int loopBNum = 0;loopBNum<rrCount;loopBNum++)
                {
                    recordRuleInfo.push_back(allRecordRule[loopBNum].allowNumber);
                    recordRuleInfo.push_back(allRecordRule[loopBNum].customTime);
                    writeInfoToFile(fd,recordRuleInfo);
                    recordRuleInfo.clear();
                }
                //To get the rule time.
                int rtCount = 0;
                vector<string> ruleTimeInfo;
                vector<SyncTimeRuleInfo>  allRuleTime;
                resultTmp = getRuleTimeInfo(allRuleTime ,numberID);
                if(resultTmp == E_DATABASE_ERROR)
                {
                      return resultTmp;
                }
                else if(resultTmp == E_NO_DATA_MATCHED)
                {
                    rtCount = 0;
                }
                else
                {
                    rtCount = allRuleTime.size();
                }
                fwrite(&rtCount, sizeof(int), 1, fd);
                for(int loopBNum = 0;loopBNum<rtCount;loopBNum++)
                {
                    char tmpCh[16];
                    memset(tmpCh,'\0',16);
                    std::string tmpWeek="";
                    sprintf(tmpCh,"%d",allRuleTime[loopBNum].week);
                    tmpWeek = string(tmpCh);
                    memset(tmpCh,'\0',16);
                    std::string tmpStrtegyId="";
                    sprintf(tmpCh,"%d",allRuleTime[loopBNum].strategyID);
                    tmpStrtegyId = string(tmpCh);

                    ruleTimeInfo.push_back(allRuleTime[loopBNum].startTime);
                    ruleTimeInfo.push_back(allRuleTime[loopBNum].endTime);
                    ruleTimeInfo.push_back(tmpWeek);
                    ruleTimeInfo.push_back(tmpStrtegyId);
                    ruleTimeInfo.push_back(allRuleTime[loopBNum].customTime);
                    writeInfoToFile(fd,ruleTimeInfo);
                    ruleTimeInfo.clear();
                }                    
                //To get the white list.
                int wListCount = 0;
                vector<string> whiteNumberInfo;
                vector<SyncWhiteListInfo>  allWhiteList;
                resultTmp = getWhiteListInfo(allWhiteList,numberID);
                if(resultTmp == E_DATABASE_ERROR)
                {
                      return resultTmp;
                }
                else if(resultTmp == E_NO_DATA_MATCHED)
                {
                    wListCount = 0;
                }
                else
                {
                    wListCount = allWhiteList.size();
                }       
                //To write the white count 
                fwrite(&wListCount, sizeof(int), 1, fd);
                for(int loopBNum = 0;loopBNum<wListCount;loopBNum++)
                {
                    //construct the whitelist info struct.
                    whiteNumberInfo.push_back(allWhiteList[loopBNum].allowNumber);
                    whiteNumberInfo.push_back(allWhiteList[loopBNum].customTime);
                    //To write these whitelist info into the file.
                    writeInfoToFile(fd,whiteNumberInfo);
                    whiteNumberInfo.clear();
                }
                ++itMinor;
            }    
            //To write these line end flag.
            fwrite("\r\n", 2, 1, fd);
        }
        ++it;            
    }
    return E_OPERATOR_SUCCED;
}

/*****************************************************************************************************
*
* NAME       : writeInfoToFile 
*
* FUNCTION   : The function is used to write these data into the file by size. 
*
* INPUT      : fd  FILE * : The type is note that the data's synchronized flag.
*              dataInfo vector<std::string>& :The data need to be write.
*
* OUTPUT     : none.
*
* RETURN     : E_NO_DATA_MATCHED :  There is no data to be write.         
*              E_OPERATOR_SUCCED :  Sucess.
*
*****************************************************************************************************/ 
ResultDataUsage DataUsage::writeInfoToFile(FILE *fd,vector<std::string>& dataInfo)
{
    int vectorSize = dataInfo.size();
    if(vectorSize == 0)
    {
        return E_NO_DATA_MATCHED;
    }
    int valueSize = 0;
    string tmpStr = "";
    //To write the data by the vector size
    for(int loopNum = 0;loopNum <vectorSize;loopNum++)
    {
        valueSize = dataInfo[loopNum].size();
        tmpStr = intToStr(valueSize);
        //First to write the size of the data.
        fwrite(&valueSize, sizeof(int), 1, fd);
        //Last to write the size of the data.
        fwrite(dataInfo[loopNum].c_str(),dataInfo[loopNum].size(),1,fd);
    }
    return E_OPERATOR_SUCCED;
}

/*****************************************************************************************************
*
* NAME       : intToStr 
*
* FUNCTION   : The function is used to change the data from int type to string type. 
*
* INPUT      : valueInt  int &: The data value that want to change the data type.
*
* OUTPUT     : none.
*
* RETURN     : string value.
*
*****************************************************************************************************/ 
string DataUsage::intToStr(int & valueInt)
{
    char tmpCh[32];
    memset(tmpCh,'\0',32);
    std::string outStr="";
    sprintf(tmpCh,"%d",valueInt);
    outStr = string(tmpCh);
    return outStr;
}

/*****************************************************************************************************
*
* NAME       : isNumberExist 
*
* FUNCTION   : The function is used to execute the sql content to check the result whether is exist. 
*
* INPUT      : sql std::string&  : The sql content that need to check.
*
* OUTPUT     : none.
*
* RETURN     : E_NO_DATA_MATCHED :  There is no data found with the inputed type.         
*              E_DATABASE_ERROR  :  The operation of querying the database fail.
*              E_OPERATOR_SUCCED :  Sucess.
*
*****************************************************************************************************/
ResultDataUsage DataUsage::isNumberExist(std::string & sql)
{
    DB_RECORDSET_ARRAY dbrecord_;
    //Query the database fail.
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }
    
    if (!dbrecord_.empty())
    {
        return E_OPERATOR_SUCCED;
    }
    return E_NO_DATA_MATCHED;
}

/*****************************************************************************************************
*
* NAME       : deleteNoCompData 
*
* FUNCTION   : The function is used to delete these number record that no exist in the bakefile.
*              The number is include minor number and the major number.
*
* INPUT      : none
*
* OUTPUT     : none
*
* RETURN     : E_DATABASE_ERROR  :  The operation about the database is fail.
*              E_OPERATOR_SUCCED :  Sucess.
*
*****************************************************************************************************/
ResultDataUsage DataUsage::deleteNoCompData()
{
    ResultDataUsage result;
    DB_RECORDSET_ARRAY dbrecord_;
    std::string userNumber = "";
    //Check whether there is major number that no be compared according with the sync time and the zone code.
    //.s delete by whc 0226
    /*
    std::string delNoCompSql = "select UserNumber from T_RegisterUser where SynchronizationTime <>  '";
       delNoCompSql += syncActionTime_ + "' and ZoneCode <> '";
    delNoCompSql += localZoneCode_ + "' ;";

    if(0 != dbmysql_->executequery(delNoCompSql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }    
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    //To get the user number 
    while (it!=dbrecord_.end())
    {
        //get the major number and delete it.
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("usernumber");
        if (it_ != item_.end())
        {
            userNumber  = (*it_).second;
            result = this->deleteUserFromDB(userNumber);
            if(result == E_DATABASE_ERROR)
            {
                return result;
            }
        }
        ++it;
    }
    */
    //.e delete by whc 0226
    //Prepare to check whether there is minor number that no be compared with the sync time and the zone code.
    std::string delNoCompSql = "";
    delNoCompSql = "select MinorNumber from T_MinorNumber where SynchronizationTime <>  '";
    //delNoCompSql += syncActionTime_ + "' and ZoneCode <> '";
    delNoCompSql += syncActionTime_ + "' and ZoneCode not in ";
    delNoCompSql += zoneCodeStr_ + " ;";

    if(0 != dbmysql_->executequery(delNoCompSql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }
    DB_RECORDSET_ARRAY::iterator itMinor = dbrecord_.begin();
    std::string minorNumber = "";
    while (itMinor!=dbrecord_.end())
    {
        //get the minor number and delete it.
        DB_RECORDSET_ITEM item_ = (*itMinor);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("minornumber");
        if (it_ != item_.end())
        {
            minorNumber  = (*it_).second;
            this->deleteMinorNumberFromCS(minorNumber);
        }
        ++itMinor;
    }    
    return E_OPERATOR_SUCCED;
}

/***********************************************************************************************
 *
 * NAME       : deleteUserFromDB
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
 *                E_DATABASE_ERROR : The operation is fail .
 *
 ***********************************************************************************************/ 
ResultDataUsage DataUsage::deleteUserFromDB(const string& number)
{
    LogFileManager::getInstance()->write(Debug,"DEBUG INFO: [deleteUserFromDB] !%s",number.c_str());    
    //step1 :To check the number whether is on the center database.
    std::string chkSql = "select UserID from T_RegisterUser where UserNumber = '";
    chkSql += number + "' ;";

    std::string  userID = "";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteUserFromCS] database operation error!");
        return E_DATABASE_ERROR;
    }
    
    //If there is no unynchronized data on the local databace server.
    if (dbrecord_.empty())
    {
        return  E_OPERATOR_SUCCED; 
    }
    else
    {
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        while (it!=dbrecord_.end())
        {
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("userid");
            if (it_ != item_.end())
            {
                userID  = (*it_).second;
            }
            ++it;
        }
    }
    //step2 To check the foreign key of the minor number and delete the relation record of these minor number.
    std::vector<std::string> allMinorNumber;
    ResultDataUsage result = this->getAllMinorNumber(number,allMinorNumber);
    LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteUserFromCS] %d!",allMinorNumber.size());    
    if(result == E_DATABASE_ERROR)
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteUserFromCS] database operation error!");    
        return E_DATABASE_ERROR;
    }
    for(int loop = 0;loop < allMinorNumber.size();loop++)
    {
        this->deleteMinorNumberFromDB(allMinorNumber[loop]);
        if(result == E_DATABASE_ERROR)
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteUserFromCS] database operation error!");    
            return E_DATABASE_ERROR;
        }        
    }
    
    std::string delSql = "";
    //step3  To check whether there is some custom service with the number input.
    chkSql = "";
    chkSql = "select * from T_ServiceCustom where UserID = '" + userID + "';";
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteUserFromCS] database operation error!");    
        return E_DATABASE_ERROR;
    }
    
    if (!dbrecord_.empty())
    {
        delSql = "";
        delSql = "delete from T_ServiceCustom where UserID = '" + userID + "';";
        if(0 != dbmysql_->executenonquery(delSql))
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteUserFromCS] database operation error!");            
            return E_DATABASE_ERROR;
        }
    }
    //step4  To check whether there is some SMS notify infomation with the number input..
    chkSql = "";
    chkSql = "select * from T_SMSNotify where UserID = '" + userID + "';";
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteUserFromCS] database operation error!");        
        return E_DATABASE_ERROR;
    }
    if (!dbrecord_.empty())
    {
        delSql = "";
        delSql = "delete from T_SMSNotify where UserID = '" + userID + "';";
        if(0 != dbmysql_->executenonquery(delSql))
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteUserFromCS] database operation error!");    
            return E_DATABASE_ERROR;
        }
    }
    //Delete the major number from the T_RegisterUser table on the center database server.
    delSql = "";
    delSql = "delete from T_RegisterUser where UserNumber = '" + number + "' and UserID = '"+userID+"';";
    if(0 != dbmysql_->executenonquery(delSql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteUserFromCS] database operation error!");    
        return E_DATABASE_ERROR;
    }
    return E_OPERATOR_SUCCED;
}

/************************************************************************************************
 *
 * NAME     : deleteMinorNumberFromDB 
 *
 * FUNCTION : The function is used to delete the inputed minor number from the T_MinorNumber
 *            table of the center database server.At the same time ,it is necessary to delete 
 *            the relation record from the relation tables with the input number record.
 *
 * INPUT    : number  const string&  : The minor number inputed needs to be deleted.
 *
 * OUTPUT   : None
 *
 * RETURN   : E_OPERATOR_SUCCED: It is sucess to delete data if exist,or no data matched for delete..
 *            E_DATABASE_ERROR : The operation is fail .
 *
 ***********************************************************************************************/ 
ResultDataUsage DataUsage::deleteMinorNumberFromDB(const string& number)
{
    //To check the number whether is on the center database.
    std::string numberId = "";
    std::string chkSql = "select NumberID from T_MinorNumber where MinorNumber = '";
    chkSql += number + "' ;";
    DB_RECORDSET_ARRAY dbrecord_;
 
    if(0 != dbmysql_->executequery(chkSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumberFromCS] database operation error!");        
        return E_DATABASE_ERROR;
    }    
    //If there is no unynchronized data on the local databace server.
    if (dbrecord_.empty())
    {
        return  E_OPERATOR_SUCCED; 
    }
    else
    {
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        while (it!=dbrecord_.end())
        {
            DB_RECORDSET_ITEM item_ = (*it);
            DB_RECORDSET_ITEM::iterator it_ = item_.find("numberid");
            if (it_ != item_.end())
            {
                numberId  = (*it_).second;
                break;
            }
            ++it;
        }
    }
    //step2 To release all the white list number.
    std::string delSql = "";
    delSql = "delete from T_RuleWhiteList where NumberID = '";
    delSql += numberId;
    delSql += "'";
    if( 0 != dbmysql_->executenonquery(delSql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumberFromCS] database operation error!");        
        return E_DATABASE_ERROR ;
    }
    //step3 To release all the Black list number.
    delSql = "delete from T_RuleBlackList where NumberID = '";
    delSql += numberId;
    delSql += "'";
    if( 0 != dbmysql_->executenonquery(delSql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumberFromCS] database operation error!");        
        return E_DATABASE_ERROR ;
    }
    LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumberFromCS] %s!",delSql.c_str());
    //step3 To release the record rule from the record rule table with the minor number.
    delSql = "";
    delSql = "delete from T_RuleRecord where NumberID = '";
    delSql += numberId;
    delSql += "'";
    
    if( 0 != dbmysql_->executenonquery(delSql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumberFromCS] database operation error!");        
        return E_DATABASE_ERROR ;
    }
    // step4 To release the record rule from the record rule table with the minor number.
    delSql = "";
    delSql = "delete from T_RuleTime where NumberID = '";
    delSql += numberId;
    delSql += "'";
    if(0 != dbmysql_->executequery(delSql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumberFromCS] database operation error!");        
        return E_DATABASE_ERROR;
    }
    //Step5 To delete the minor number from the number table.
    delSql = "";
    delSql = "delete from T_MinorNumber where NumberID = '";
    delSql += numberId + "' ;";
    if(0 != dbmysql_->executenonquery(delSql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [deleteMinorNumberFromCS] database operation error!");        
        return E_DATABASE_ERROR;
    }
    return E_OPERATOR_SUCCED;
}

/************************************************************************************************
 *
 * NAME       : setUnSynchronized 
 *
 * FUNCTION   : The function is used to set the synchronized status of the data
 *              which has set into the database according with the data type.
 *
 * INPUT      : dataType   SynchronDataType : The type of the data.
 *              number    const string& : The data is that want to be checked.
 * OUTPUT     : None
 *
 * RETURN     : E_OPERATOR_SUCCED: Get the data synchronized flag sucessfully.
 *              E_DATABASE_ERROR : The database operation is fail.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::setUnSynchronized(SynchronDataType dataType, const string& number)
{
    ResultDataUsage result = E_OPERATOR_SUCCED;
    std::string sql = "update ";
    std::string checkSql = "";
    //Filter the data type ,the different type has different process sql.
    switch(dataType)
    {
    case REGISTERUSER:
        sql += "T_RegisterUser set IsSynchronization  = 0 where UserNumber = '";
        sql +=  number+ "' ;"; 
        break;
    case MINORNUMBER:
        sql += "T_MinorNumber set IsSynchronization  = 0 where MinorNumber = '" ; 
        sql +=  number+ "' ;"; 
        break;  
    default:
        LogFileManager::getInstance()->write(Brief,"Warning INFO: [setIsSynchronized] dataType is no match!");    
        result = E_DATABASE_ERROR;        
        break;
    }
    if(result != E_OPERATOR_SUCCED)
    {
        return result;
    }
    //Execute the setting process.
    if(0 != dbmysql_->executenonquery(sql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: [setUnSynchronized] database operation error!");
        return E_DATABASE_ERROR;
    }
    return E_OPERATOR_SUCCED;
}
//.s New add the interface for the minor number process by IVR  whc 20100412
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
ResultDataUsage DataUsage::cancelMinorNumberByIVR(const string& minorNumber)
{
    ResultDataUsage result;
    //step 1 Update the minor number state.
    MinorNumberState numState = NUMBER_FREEZE;

    //To get the number pool id
    string sqlContent = "select PoolID from T_NumberPool where Number='";
    sqlContent += minorNumber;
    sqlContent += "';";
    string fieldName = "poolid";
    string fieldValue = "";
    this->getDBFieldValue(sqlContent,fieldName,fieldValue);
    if(fieldValue.empty())
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::cancelMinorNumberByIVR operation error!");
        return E_DATABASE_ERROR;
    }
  
    result = this->setMinorNumPoolState(fieldValue,numState);
    //step 1 Delete the minor number.
    if(result == E_OPERATOR_SUCCED)
    {
        string majorNum="";
        string sql = "select UserNumber from T_RegisterUser where UserID=(select UserID from T_MinorNumber where MinorNumber='";
        sql += minorNumber;
        sql +=    "');";
        fieldName = "usernumber";
        this->getDBFieldValue(sql,fieldName,majorNum);
        if(majorNum.empty())
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::cancelMinorNumberByIVR operation error!");
            return E_DATABASE_ERROR;
        }
        result = this->deleteMinorNumber(majorNum,minorNumber);
    }
    return result;
}

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
ResultDataUsage DataUsage::getAreaCode(const string& majorNumber,string& areaCode)
{
    //To get the user area code.
    std::string sql = "select ZoneCode from T_RegisterUser where UserNumber = '";
    sql = sql + majorNumber + "';";
    //General rand data [0,200)
    string  fieldName = "zonecode";
    ResultDataUsage result ;
    result = this->getDBFieldValue(sql,fieldName,areaCode);
    return result;
}
 /************************************************************************************************
  *
  * NAME       : getRandMinorNumber 
  *
  * FUNCTION   : The function is used to rand get one minor number from the minor number pool.
  *
  * INPUT      : majorNumber.const string& :The major number that need to add the minor number.
  *           
  * OUTPUT     : minorNumber   string& : The type of the data.
  *
  * RETURN     : E_OPERATOR_SUCCED: Get one rand number fron the number pool
  *              E_DATABASE_ERROR : The database operation is fail.
   *             E_NUMBER_NOT_EXIST :No number exist in the table.
  *
  ***********************************************************************************************/
ResultDataUsage DataUsage::getRandMinorNumber(const string& majorNumber,string& minorNumber)
{
    string areaCode = "";
    ResultDataUsage result = this->getAreaCode(majorNumber,areaCode);
    if(result!=E_OPERATOR_SUCCED)
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getRandMinorNumber major number no zonecode");
        return result;
    }

    //To get the count of the active number from the number pool
    std::string counter;
    std::string sql;
    sql = "select count(Number) from T_NumberPool where reserved = 0 and StateID = 0 and AreaCode = '" + areaCode +"';";
    string  fieldName = "count(number)";
    result = this->getDBFieldValue(sql,fieldName,counter);
    if(result!=E_OPERATOR_SUCCED)
    {
        return result;
    }
    
    //To get the number from the number pool and construct the query index.
    string poolID = "";
    char buff[8];
    memset(buff,'\0',8);
    long numCount = strtol(counter.c_str(),'\0',10);
    unsigned long randIndex = 0;
    if(numCount == 0)
    {
        return E_NUMBER_NOT_EXIST;
    }
    //Get the number position.
    randIndex = rand()%numCount;
    if(randIndex!=0)
    {
        randIndex = randIndex - 1;
    }
    else
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getRandMinorNumber pool no number is choose able");
        return E_DATABASE_ERROR;
    }
    // Convert the data to char*
    sprintf(buff ,"%d",randIndex);
    sql.clear();
    sql = "select Number ,PoolID from T_NumberPool where reserved = 0 and StateID = 0 and AreaCode = '" + areaCode ;
    sql = sql+"' limit " +buff +",1;";
    
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getRandMinorNumber database error");
        return E_DATABASE_ERROR;
    }
    
    if (dbrecord_.empty())
    {
        return E_NUMBER_NOT_EXIST;
    }
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    if (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("number");
        if (it_ != item_.end())
        {
            minorNumber = (*it_).second;
        }
        else
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getRandMinorNumber the data is no exist");
            return E_NUMBER_NOT_EXIST;
        }
        it_ = item_.find("poolid");
        if (it_ != item_.end())
        {
            poolID = (*it_).second;
        }
        else
        {
            return E_DATABASE_ERROR;
        }
    }
    else
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getRandMinorNumber the data is no exist");
        return E_DATABASE_ERROR;
    }

    //To keep the temp mionr number state into the chooseTmpPool table
    result = this->saveChooseMinorNumber(majorNumber,minorNumber);
    if(result==E_OPERATOR_SUCCED)
    {
        MinorNumberState numState = NUMBER_TMPUSED;
        result = this->setMinorNumPoolState(poolID,numState);
        if(result == E_DATABASE_ERROR)
        {
            //To release the tmpChooseNumber table relation record.
            result = this->deleteChooseTmpMinorNumber(majorNumber,minorNumber);
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getRandMinorNumber setMinorNumPoolState error");
        }
    }
    return result;
}

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
ResultDataUsage DataUsage::commitRandMinorNumber(const string& majorNumber,const string& minorNumber,unsigned int& numberIndex)
{
    std::string  poolID = "";
    if( majorNumber.empty() || minorNumber.empty())
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::commitRandMinorNumber input param null");
        return E_DATABASE_ERROR;
    }
    ResultDataUsage result;
    MinorNumberState numState ;
    string areaCode="";
    //To get the areaCode
    result = this->getAreaCode(majorNumber,areaCode);
    if(result!=E_OPERATOR_SUCCED)
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::commitRandMinorNumber get zonecode fail!");
        return result;
    }
    result = this->addVirtualMinorNumber(majorNumber, minorNumber,numberIndex,areaCode);
    this->getPoolIDByNumber(minorNumber, poolID);
    if(result == E_OPERATOR_SUCCED)
    {
        numState = NUMBER_USED;
        this->setMinorNumPoolState(poolID,numState);
    }
    else
    {
        numState = NUMBER_ABLE;
        this->setMinorNumPoolState(poolID,numState);
    }
    this->deleteChooseTmpMinorNumber(majorNumber,minorNumber);

    return result;
}
 /************************************************************************************************
  *
  * NAME       : addManualMinorNumber 
  *
  * FUNCTION   : The function is used to add one virtual minor number by manual.
  *
  * INPUT      : majorNumber   const string& : The number of the major number.
  *              minorNumber   const string& : The minor number that want to add.
  *              
  * OUTPUT     : numberIndex   unsigned int& : The index of the minor number which has been add in.
  *
  * RETURN     : E_OPERATOR_SUCCED: Add the manual minor number is sucessfully..
  *              E_DATABASE_ERROR : The database operation is fail.
  *              E_NUMBER_NOT_ACTIVE: The number could not be choosed as minor number.
  *              E_NUMBER_NOT_EXIST:  The input number is not exist in the poolNumber table            
  *
  ***********************************************************************************************/
ResultDataUsage DataUsage::addManualMinorNumber(const string& majorNumber,const string& minorNumber,unsigned int& numberIndex)
{
    if(majorNumber.empty() || minorNumber.empty())
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::addManualMinorNumber input paramter null");
        return E_DATABASE_ERROR;
    }
    ResultDataUsage result;
    //To check the number length whether is available
    int numberLenCount=this->virtualNumLength_.size();
    int numberLen=minorNumber.size();
    bool lengFlag=false;
    for(int loop=0;loop<numberLenCount;loop++)
    {
        if(this->virtualNumLength_[loop]==numberLen)
        {
            lengFlag=true;
            break;
        }
    }

    if (!lengFlag)
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::addManualMinorNumber number length error");
        result = E_DATABASE_ERROR ;
        return result;
    }

    //To check the number whether is in the number pool and whether is available
    string areaCode = "";
    DB_RECORDSET_ARRAY dbrecord_;
    // To get the poolid which can be choosed
    string poolIdValue="";
    string  poolStateValue = "";
    string  poolReservedValue = "";
    MinorNumberState numState;

    result = this->getAreaCode(majorNumber,areaCode);
    if(result!=E_OPERATOR_SUCCED)
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::addManualMinorNumber get zonecode fail!");
        return result;
    }
    //std::string sql = "select PoolID from T_NumberPool where StateID = 0 and AreaCode= '";
    std::string sql = "select PoolID,StateID,reserved from T_NumberPool where AreaCode= '";
    sql += areaCode ;
    sql += "' and Number='";
    sql += minorNumber;
    sql += "';";

    //result = this->getDBFieldValue(sql,fieldName,fieldValue);

    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: %s",sql.c_str());
        return E_DATABASE_ERROR;
    }

    if (dbrecord_.empty())
    {
        return E_NUMBER_NOT_EXIST;
    }
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    if (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("poolid");
        if (it_ != item_.end())
        {
            poolIdValue = (*it_).second;
        }
        else
        {        
            return E_DATABASE_ERROR;
        }
        // TO get the state of the number
        it_ = item_.find("stateid");
        if (it_ != item_.end())
        {
            poolStateValue = (*it_).second;
        }
        else
        {        
            return E_DATABASE_ERROR;
        }
        // TO check the number whether is reserved
        it_ = item_.find("reserved");
        if (it_ != item_.end())
        {
            poolReservedValue = (*it_).second;
        }
        else
        {        
            return E_DATABASE_ERROR;
        }
    }
    else
    {
        return E_DATABASE_ERROR;
    }

    if(poolStateValue!="0" || poolReservedValue == "1")
    {
        LogFileManager::getInstance()->write(Debug,"Debug INFO: DataUsage::addManualMinorNumber Input number is not choose able!");
        return E_NUMBER_NOT_ACTIVE;
    }

    //If there exist active number then set the number state to NUMBER_TMPUSED
    numState = NUMBER_TMPUSED;
    this->setMinorNumPoolState(poolIdValue,numState);


    result = this->addVirtualMinorNumber(majorNumber, minorNumber,numberIndex,areaCode);
    if(result == E_OPERATOR_SUCCED)
    {
        numState = NUMBER_USED;
        this->setMinorNumPoolState(poolIdValue,numState);
    }
    else
    {
        numState = NUMBER_ABLE;
        this->setMinorNumPoolState(poolIdValue,numState);
    }
    return result;
}

/************************************************************************************************
*
* NAME       : getMinorNumberOpeCount 
*
* FUNCTION   : The function is used to get the total number of user takes action for delete and 
*              add minor number during one month.
*
* INPUT      : majorNumber   const string& : The number of the major number.
*              
* OUTPUT     : countNumber   unsigned int& : The count number of the operation fro del and add.
*
* RETURN     : E_OPERATOR_SUCCED: Add the manual minor number is sucessfully..
*              E_DATABASE_ERROR : The database operation is fail.
*
***********************************************************************************************/
ResultDataUsage DataUsage::getMinorNumberOpeCount(const string& majorNumber,int& countNumber)
{
    //To get the operation count about delete or add number from minornumber bake table 
    std::string  sql = "select count(MinorNumber) from t_minornumber_bak where UserID in (select UserID  from t_registeruser_bak  where UserNumber = '";
    sql += majorNumber.c_str() ;
    sql += "') and (YEAR(BakTime) = YEAR(now())) and MONTH(BakTime) = MONTH(now());";
    std::string  fieldValue ="";
    int count = 0;
    string fieldName = "count(minornumber)";

    this->getDBFieldValue(sql, fieldName, fieldValue);
    if(fieldValue.empty())
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getMinorNumberOpeCount operation error!");
        return E_DATABASE_ERROR;
    }
    count = atoi(fieldValue.c_str());

    sql.clear();
    fieldValue.clear();
    //To get the operation count about delete or add number from minornumber table 
    sql = "select count(MinorNumber) from T_MinorNumber where UserID=(select UserID  from T_RegisterUser where UserNumber = '";
    sql += majorNumber.c_str() ;
    sql += "') and (YEAR(RegisterTime) = YEAR(now())) and MONTH(RegisterTime) = MONTH(now());";

    getDBFieldValue(sql,fieldName, fieldValue);
    if(fieldValue.empty())
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getMinorNumberOpeCount operation error!");
        return E_DATABASE_ERROR;
    }
    count += atoi(fieldValue.c_str());
    countNumber = count;
    return E_OPERATOR_SUCCED;
}
/************************************************************************************************
 *
 * NAME       : getDBFieldValue 
 *
 * FUNCTION   : The function is used to get the area code according with the major number from the
 *              database.
 *
 * INPUT      : minorNumber   string& : The type of the data.
 *           
 * OUTPUT     : None
 *
 * RETURN     : E_OPERATOR_SUCCED:  sucessfully.
 *              E_DATABASE_ERROR : The database operation is fail.
 *              E_NUMBER_NOT_EXIST :No number exist in the table.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::getDBFieldValue(string& sqlCotent,string& fieldName,string& fieldValue)
{
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sqlCotent,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: %s",sqlCotent.c_str());
        return E_DATABASE_ERROR;
    }

    if (dbrecord_.empty())
    {
        return E_NUMBER_NOT_EXIST;
    }
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    if (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find(fieldName);
        if (it_ != item_.end())
        {
            fieldValue = (*it_).second;
        }
        else
        {        
            return E_DATABASE_ERROR;
        }
    }
    else
    {
        return E_DATABASE_ERROR;
    }
    return E_OPERATOR_SUCCED;
}
/************************************************************************************************
 *
 * NAME       : setMinorNumPoolState 
 *
 * FUNCTION   : The function is used to set the minor number state in the pool .
 *
 * INPUT      : poolID   string& : The zone code of the minor number registered.
 *              state  MinorNumberState& :The useing state of the minor number that to be set.
 *              
 * OUTPUT     : None.
 *
 * RETURN     : E_OPERATOR_SUCCED: sucessfully.
 *              E_DATABASE_ERROR : fail.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::setMinorNumPoolState( const string& poolID,MinorNumberState& state)
{
    char buff[8];
    memset(buff,'\0',8);
    sprintf(buff,"%d",state);
    string numberState=string(buff);

    //Update the pool number state
    std::string sql = "update T_NumberPool set StateID = '";
    sql += numberState;
    sql += "',LastUpdateDate=now()";
    sql += " where PoolID='";
    sql += poolID;
    sql += "';";
    
    if( 0 != dbmysql_->executenonquery(sql))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: %s",sql.c_str());
        return E_DATABASE_ERROR ;
    }
    return E_OPERATOR_SUCCED;
}
/************************************************************************************************
 *
 * NAME       : saveChooseMinorNumber 
 *
 * FUNCTION   : The function is used to set the minor number state in the pool .
 *
 * INPUT      : majorNumber   const string& : The major number.
 *              minorNumber   const string& : The minor number that want to add.
 *              
 * OUTPUT     : None.
 *
 * RETURN     : E_OPERATOR_SUCCED:sucessfully.
 *              E_DATABASE_ERROR :fail.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::saveChooseMinorNumber(const string& majorNumber,const string& minorNumber)
{
    if(majorNumber.empty()||minorNumber.empty())
    {
        LogFileManager::getInstance()->write(Brief,"DataUsage::saveChooseMinorNumber input paramter is null");
        return E_DATABASE_ERROR ;
    }
    std::string tmpChooseID;
    generateUUID(tmpChooseID);
    std::string sql = "insert into T_ChooseTmpPool(Id,UserNumber,MinorNumber,ChooseTime,SeqNo) values( '";
    sql += tmpChooseID;
    sql += "','";
    sql += majorNumber;
    sql += "','";
    sql += minorNumber ;
    sql += "',now(),'0');";
    
    if( 0 != dbmysql_->executenonquery(sql))
    {
        LogFileManager::getInstance()->write(Brief,"sql %s",sql.c_str());
        return E_DATABASE_ERROR ;
    }
    return E_OPERATOR_SUCCED;
}
/************************************************************************************************
 *
 * NAME       : deleteChooseMinorNumber 
 *
 * FUNCTION   : The function is used to delete the record in the ChooseTmpPool table .
 *
 * INPUT      : majorNumber   string& : The user major number registered.
 *              minorNumber   string& : The minor number that want to add.
 *              
 * OUTPUT     : None.
 *
 * RETURN     : E_OPERATOR_SUCCED: Delete the data sucess.
 *              E_DATABASE_ERROR : Delete the data fail.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::deleteChooseTmpMinorNumber(const string& majorNumber,const string& minorNumber)
{
    std::string sql = "delete from T_ChooseTmpPool where UserNumber='";
    sql += majorNumber;
    sql += "' and MinorNumber='";
    sql += minorNumber;
    sql += "' and SeqNo='0';";
    
    if( 0 != dbmysql_->executenonquery(sql))
    {
        LogFileManager::getInstance()->write(Brief,"Error INFO :sql %s",sql.c_str());
        return E_DATABASE_ERROR ;
    }
    return E_OPERATOR_SUCCED;
}
/************************************************************************************************
 *
 * NAME       : addVirtualMinorNumber 
 *
 * FUNCTION   : The function is used to add one minor number into the minornumber table.
 *
 * INPUT      : majorNumber   string& : The number of the major number.
 *              minorNumber   string& : The minor number that want to add.
 *              
 * OUTPUT     : numberIndex   unsigned int& : The index of the minor number which has been add in.
 *
 * RETURN     : E_OPERATOR_SUCCED: Get the data synchronized flag sucessfully.
 *              E_DATABASE_ERROR : The database operation is fail.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::addVirtualMinorNumber(const string& majorNumber,const string& minorNumber,unsigned int& numberIndex,string& areaCode,OperateChannel inChanelID)
{
    int arrayLength = 3;
    //First : To check the available of the number that want to be added as the minor number.
    bool isVaild = VaildDialNumber(minorNumber);
    if (!isVaild)
    {
        return E_NUMBER_LENGTH_ERROR;
    }

    std::string numberType = "3"; //virtual number type

    //Second check the major number whether is exist.
    std::string sql;
    std::string userID;
    ResultDataUsage result = this->getUserID(majorNumber,userID);
    //generate UUID
    std::string seqID;
    generateUUID(seqID);
    if (result == E_OPERATOR_SUCCED)
    {
        //The major number is exist.
        sql.clear();
        int sequenceNo = 1;
        //To get all the minor number sequence.
        std::vector<int> allMinorSeq;
        std::vector<int> tmpAllMinorSeq;
        for(int loop=0;loop<arrayLength;loop++)
        {
            int value = loop+1;
            allMinorSeq.push_back(value);
        }
        sql = "select * from T_MinorNumber where UserID = '"+ userID + "';";
        DB_RECORDSET_ARRAY dbrecord_;
        if(0 != dbmysql_->executequery(sql,dbrecord_))
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::addVirtualMinorNumber db operation fail!");
            return E_DATABASE_ERROR;
        }

        if (!dbrecord_.empty())
        {

            DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
            while (it!=dbrecord_.end())
            {
                DB_RECORDSET_ITEM item_ = (*it);
                DB_RECORDSET_ITEM::iterator it_ = item_.find("sequenceno");
                if (it_ != item_.end())
                {
                    int tmpSeq=atoi((*it_).second.c_str());

                    for(vector<int>::iterator iter=allMinorSeq.begin(); iter!=allMinorSeq.end(); )
                    {        
                        if( *iter == tmpSeq)
                        {
                            iter = allMinorSeq.erase(iter);
                            break;
                        }
                        else
                        {
                            iter ++ ;
                        }
                    }
                }
                it++;
            }
            if(allMinorSeq.size()!=0)
            {
                sequenceNo = allMinorSeq[0];
            }
            else
            {
                LogFileManager::getInstance()->write(Debug,"Debug INFO: DataUsage::addManualMinorNumber The major number has three minor number");
                return E_HAS_ENOUGH_NUMBER;
            }
        }
        char buff[10];
        memset(buff,0,sizeof(buff));
        sprintf(buff,"%d",sequenceNo);
        std::string registerType;

        //The state of the minor number.
        int stateID = (int)STATE_ACTIVE;
        char buffStateID[10];
        memset(buffStateID,0,sizeof(buffStateID));
        sprintf(buffStateID,"%d",stateID);

        //The IMSI of the minor number.
        std::string IMSI = "noimsi";
        char operateChinal[4];
        memset(operateChinal,'\0',4);
        OperateChannel tmpChanelID = this->confirmChannel(inChanelID);
        sprintf(operateChinal,"%d",tmpChanelID);

        //Construct the sql content of the minor number infomation .
        sql = "insert into T_MinorNumber ( NumberID,MinorNumber,UserID,Type,SequenceNo,RegisterTime," ;
        sql +=  "RegisterType,StateID,IMSI,LastUpdateTime,OperateChannelID,RecordRule,ZoneCode) values('";
        sql += seqID;
        sql += "','";
        sql += minorNumber;
        sql += "','";
        sql += userID;
        sql += "','";
        sql += numberType;
        sql += "','";
        sql += buff;
        sql += "', now(),'";
        sql += registerType;
        sql += "','";
        sql += buffStateID;
        sql += "','";
        sql += IMSI;
        sql += "',now(),'";
        sql += operateChinal;
        sql += "','0','";
        sql += areaCode;
        sql += "')";
        //Add the minor number infomation into the database.
        if( 0 != dbmysql_->executenonquery(sql))
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::addVirtualMinorNumber insert DB fail!");
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: %s",sql.c_str());
            return E_DATABASE_ERROR ;
        }
    }
    
    //The major number input is no exist.
    if (result != E_OPERATOR_SUCCED)
    {
        return E_MINORNUMBER_NOT_EXESIT; 
    }
    return E_OPERATOR_SUCCED;
}

/************************************************************************************************
 *
 * NAME       : getPoolIDByNumber 
 *
 * FUNCTION   : The function is used to get the minor number pool ID by choosed number.
 *
 * INPUT      : majorNumber   string& : The number of the major number.
 *              minorNumber   string& : The minor number that want to add.
 *              
 * OUTPUT     : numberIndex   unsigned int& : The index of the minor number which has been add in.
 *
 * RETURN     : E_OPERATOR_SUCCED: sucessfully.
 *              E_DATABASE_ERROR : fail.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::getPoolIDByNumber(const std::string& minorNumber,std::string&  poolID)
{
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "select PoolID from T_NumberPool where Number = '" + minorNumber+"';";
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getPoolIDByNumber database error");
        return E_DATABASE_ERROR;
    }
    
    if (dbrecord_.empty())
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    if (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("poolid");
        if (it_ != item_.end())
        {
            poolID = (*it_).second;
        }
        else
        {
            return E_DATABASE_ERROR;
        }
    }
    else
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getPoolIDByNumber the data is no exist");
        return E_DATABASE_ERROR;
    }
    return E_OPERATOR_SUCCED;
}
/************************************************************************************************
 *
 * NAME       : getNumberFieldByZone 
 *
 * FUNCTION   : The function is used to get number wide from the number pool table according with 
 *              the pointed zone code.
 *
 * INPUT      : zoneCode   const string& : The number's local zone code.
 *              
 * OUTPUT     : firstNum   string& : The first min number.
 *              lastNum    string& : The last max number.
 *
 * RETURN     : E_OPERATOR_SUCCED: Add the manual minor number is sucessfully..
 *              E_DATABASE_ERROR : The database operation is fail.
 *              E_NUMBER_NOT_EXIST:There is no number in the pool with the zone code. 
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::getNumberFieldByZone(const string& zoneCode, string& firstNum,string& lastNum)
{
    if(zoneCode.empty())
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getNumberFieldByZone Input ZoneCode is Null");
        return E_DATABASE_ERROR;
    }
    string minNum = "";
    string maxNum = "";
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "select Min(Number),Max(Number) from T_NumberPool where StateID != 99 and AreaCode='"+ zoneCode+"';";
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getNumberFieldByZone database error");
        return E_DATABASE_ERROR;
    }
    if (dbrecord_.empty())
    {
        LogFileManager::getInstance()->write(Debug,"Debug INFO: DataUsage::getNumberFieldByZone No number exist in the pool");
        return E_NUMBER_NOT_EXIST;
    }
    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    if (it!=dbrecord_.end())
    {
        DB_RECORDSET_ITEM item_ = (*it);
        //To get the first min number from number pool. 
        DB_RECORDSET_ITEM::iterator it_ = item_.find("min(number)");
        if (it_ != item_.end())
        {
            minNum = (*it_).second;            
        }
        else
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getNumberFieldByZone database error");
            return E_NUMBER_NOT_EXIST;
        }
        //To get the last max number from number pool. 
        it_ = item_.find("max(number)");
        if (it_ != item_.end())
        {
            maxNum = (*it_).second;
        }
        else
        {
            LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getNumberFieldByZone database error");
            return E_NUMBER_NOT_EXIST;
        }
    }
    else
    {
        LogFileManager::getInstance()->write(Brief,"ERROR INFO: DataUsage::getPoolIDByNumber the data is no exist");
        return E_NUMBER_NOT_EXIST;
    }
    if(!minNum.empty() && !maxNum.empty())
    {
        LogFileManager::getInstance()->write(Debug,"Debug INFO :DataUsage::getNumberFieldByZone  number wide[%s %s]",minNum.c_str(),maxNum.c_str());
        firstNum = minNum;
        lastNum = maxNum;
    }
    else
    {
        LogFileManager::getInstance()->write(Debug,"Debug INFO: DataUsage::getNumberFieldByZone No number exist in the pool");
        return E_NUMBER_NOT_EXIST;
    }
    return E_OPERATOR_SUCCED;
}
//.e New add the interface for the minor number process by IVR  whc 20100412
//.s 增加对于副号码定制回铃音的数据查询和添加
/************************************************************************************************
 *
 * NAME       : setRingBackTone 
 *
 * FUNCTION   : The function is used to set ring back tone for the minor number.
 *
 * INPUT      : minorNumber const std::string& : The minor number is want to set the ring back tone.
 *              ringBackId const int& : The ring back policy between 1 and 8.
 *
 * OUTPUT     : None
 *
 * RETURN     : E_OPERATOR_SUCCED: sucessfully.
 *              E_DATABASE_ERROR : The database operation is fail.
 *              E_MINORNUMBER_NOT_EXESIT :The minor number is not exist.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::setRingBackTone(const std::string& minorNumber,const int& ringBackId,OperateChannel inChanelID)
{
    //To check the minor number whether is avaliable
    std::string numberID;
    int updateFlag = 0;
    int tmpRingBackId = 0;
    if (this->getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    ResultDataUsage result = E_OPERATOR_SUCCED;

    std::string operatorNote ;
    short int opeResult = 0;

    std::string sql = "select RingBackID from T_RingBackTone where NumberID = '";
    sql += numberID;
    sql += "';";
    
    DB_RECORDSET_ARRAY dbRecord;
    if( 0 != dbmysql_->executequery(sql,dbRecord))
    {
        LogFileManager::getInstance()->write(Debug,"E_DATABASE_ERROR sql (%s)", sql.c_str());
        return E_DATABASE_ERROR;
    }
    //Convert the int type to char[]    
    char buff[8];
    memset(buff,'\0',sizeof(buff));
    sprintf(buff,"%d",ringBackId);
    string strBuf= string(buff);

    if (dbRecord.empty())
    {
        operatorNote = "Add RingBackID "+strBuf;
        //generate UUID
        std::string seqID;
        generateUUID(seqID);

        sql.clear();
        sql = "insert into T_RingBackTone(ItemID,NumberID,RingBackID,CustomTime) values ('";
        sql += seqID;
        sql += "','";
        sql += numberID;
        sql += "','";
        sql += buff;
        sql += "',now()";
        sql += ");";
        if( 0 != dbmysql_->executenonquery(sql))
        {
            opeResult = -1;
            LogFileManager::getInstance()->write(Debug,"E_DATABASE_ERROR sql (%s)", sql.c_str());    
            result = E_DATABASE_ERROR;
        }
        else
        {
            opeResult = 0;    
        }
    }
    else
    {
        operatorNote = "Update RingBackID to "+strBuf;
        sql.clear();
        DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
        if(it != dbRecord.end())
        {
            DB_RECORDSET_ITEM::iterator it_ = (*it).find("ringbackid");
            if(it_!=(*it).end())
            {
                tmpRingBackId = atoi(it_->second.c_str());
                if(tmpRingBackId == ringBackId)
                {
                    updateFlag = 1;
                }
            }
        }
        if(updateFlag == 0)
        {
            sql = "update T_RingBackTone set CustomTime = now(), RingBackID = '";
            sql += buff;
            sql += "' where NumberID = '";
            sql += numberID;
            sql += "';";
            if( 0 != dbmysql_->executenonquery(sql))
            {
                opeResult = -1;
                LogFileManager::getInstance()->write(Debug,"E_DATABASE_ERROR sql (%s)", sql.c_str());                
                result = E_DATABASE_ERROR;
            }    
            else
            {
                opeResult = 0;    
            }
        }
    }

    if( updateFlag == 0 )
    {
        OperateChannel tmpChanelID = this->confirmChannel(inChanelID);
        this->setMinorNumberOpeRecord(numberID, RINGBACKTONE_SET,tmpChanelID,operatorNote,opeResult);
    }
    return result;
}
/************************************************************************************************
 *
 * NAME       : getRingBackTone 
 *
 * FUNCTION   : The function is used to get ring back tone for the minor number from the database.
 *
 * INPUT      : minorNumber const std::string& : The minor number is want to set the ring back tone.
 *
 * OUTPUT     : ringBackId int& : If there is settiing record ,so the value is between 1 and 8.
 *                                If there is no setting about the minor number the value is 0.
 *
 * RETURN     : E_OPERATOR_SUCCED: sucessfully.
 *              E_DATABASE_ERROR : The database operation is fail.
 *              E_MINORNUMBER_NOT_EXESIT :The minor number is not exist.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::getRingBackTone(const std::string& minorNumber,int& ringBackId)
{
    //To check the minor number whether is avaliable
    std::string numberID;
    if (this->getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    std::string sql = "select RingBackID from T_RingBackTone where NumberID = '";
    sql += numberID;
    sql += "';";
    DB_RECORDSET_ARRAY dbRecord;
    if( 0 != dbmysql_->executequery(sql,dbRecord))
    {
        return E_DATABASE_ERROR;
    }
    
    if (dbRecord.empty())
    {
        ringBackId = -1;
    }
    else
    {
        DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
        if(it != dbRecord.end())
        {
            DB_RECORDSET_ITEM::iterator it_ = (*it).find("ringbackid");
            if(it_!=(*it).end())
            {
                ringBackId = atoi(it_->second.c_str());
            }
            else
            {
                ringBackId = -1;
            }
        }
    }
    return E_OPERATOR_SUCCED;
}
/************************************************************************************************
 *
 * NAME       : cancelRingBackTone 
 *
 * FUNCTION   : The function is used to delete the setting about the pointed minor number.
 *
 * INPUT      : minorNumber   const std::string&  : The type of the data.
 *             
 * OUTPUT     : None
 *
 * RETURN     : E_OPERATOR_SUCCED: Delete the record sucessfully.
 *              E_DATABASE_ERROR : Delete the record fail.
 *              E_MINORNUMBER_NOT_EXESIT :The minor number is not exist.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::cancelRingBackTone(const std::string& minorNumber,OperateChannel inChanelID)
{
    std::string numberID;
    
    if (this->getNumberID(minorNumber,numberID) == E_MINORNUMBER_NOT_EXESIT)
    {
        return E_MINORNUMBER_NOT_EXESIT;
    }
    std::string operatorNote = "Delete RingBackID ";
    short int opeResult = 0;

    OperateChannel tmpChanelID = this->confirmChannel(inChanelID);

    std::string sql = "delete from T_RingBackTone where NumberID = '";
    sql += numberID;
    sql += "';";
    DB_RECORDSET_ARRAY dbRecord;
    if( 0 != dbmysql_->executequery(sql,dbRecord))
    {
        opeResult = -1;
        this->setMinorNumberOpeRecord(numberID, RINGBACKTONE_SET,tmpChanelID,operatorNote,opeResult);
        return E_DATABASE_ERROR;
    }
    else
    {
        opeResult = 0;
        this->setMinorNumberOpeRecord(numberID, RINGBACKTONE_SET,tmpChanelID,operatorNote,opeResult);
    }
    return E_OPERATOR_SUCCED;
}
//.e 增加对于副号码定制回铃音的数据查询和添加

//.s 增加对于副号码短信过滤和统计功能

/************************************************************************************************
 *
 * NAME       : getSMSCount 
 *
 * FUNCTION   : The function is used to delete the setting about the pointed minor number.
 *
 * INPUT      : majorNumber   const std::string&  : The type of the data.
 *             
 * OUTPUT     : number unsigned int & :The param is the count of message which the user send during one day.
 *
 * RETURN     : E_OPERATOR_SUCCED: Delete the record sucessfully.
 *              E_DATABASE_ERROR : Delete the record fail.
 *              E_MINORNUMBER_NOT_EXESIT :The minor number is not exist.
 *              E_MAJORNUMBER_NOT_EXESIT :The major number is not exist.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::getSMSCount(const std::string& majorNumber,unsigned int &number)
{
    //Check the major phone number has been registered the service.
    std::string userID;
    if (this->getUserID(majorNumber,userID) == E_MAJORNUMBER_NOT_EXESIT)
    {
        return E_MAJORNUMBER_NOT_EXESIT;
    }

    //Get the minor phone number has been registered the service.
    std::string sql = "select count(ID) from T_SMSDigest where CALL_TYPE = 1 and STARTDATE = curdate() and Msisdn = '";
    sql += majorNumber;
    sql += "'";
    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        number = 0;
        return E_DATABASE_ERROR;
    }

    if (dbrecord_.empty())
    {
        number = 0;
    }
    else
    {
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        if(it != dbrecord_.end())
        {
            DB_RECORDSET_ITEM::iterator it_ = (*it).find("count(id)");
            if(it_!=(*it).end())
            {
                number = atoi(it_->second.c_str());
            }
            else
            {
                number = 0;
            }
        }
    }
    return E_OPERATOR_SUCCED; 
}
/************************************************************************************************
 *
 * NAME       : getSMSCount 
 *
 * FUNCTION   : The function is used to delete the setting about the pointed minor number.
 *
 * INPUT      : majorNumber   const std::string&  : The type of the data.
 *             
 * OUTPUT     : number unsigned int & :The param is the count of message which the user send during one day.
 *
 * RETURN     : E_OPERATOR_SUCCED: Delete the record sucessfully.
 *              E_DATABASE_ERROR : Delete the record fail.
 *              E_MINORNUMBER_NOT_EXESIT :The minor number is not exist.
 *              E_MAJORNUMBER_NOT_EXESIT :The major number is not exist.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::getSMSCount(const std::string& majorNumber,const std::string& msgInfo,unsigned int &number)
{
    //是否需要对输入参数进行判空
    //Check the major phone number has been registered the service.
    std::string userID;
    if (this->getUserID(majorNumber,userID) == E_MAJORNUMBER_NOT_EXESIT)
    {
        return E_MAJORNUMBER_NOT_EXESIT;
    }

    //Get the minor phone number has been registered the service.
    std::string sql = "select count(ID) from T_SMSDigest where CALL_TYPE = 1 and STARTDATE = curdate() and Msisdn = '";
    sql += majorNumber;
    sql += "' and MessageInfo = '";
    sql += msgInfo;
    sql += "';";

    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        number = 0;
        return E_DATABASE_ERROR;
    }

    if (dbrecord_.empty())
    {
        number = 0;
    }
    else
    {
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        if(it != dbrecord_.end())
        {
            DB_RECORDSET_ITEM::iterator it_ = (*it).find("count(id)");
            if(it_!=(*it).end())
            {
                number = atoi(it_->second.c_str());
            }
            else
            {
                number = 0;
            }
        }
    }
    return E_OPERATOR_SUCCED; 
}
/************************************************************************************************
 *
 * NAME       : getSMSCount 
 *
 * FUNCTION   : The function is used to delete the setting about the pointed minor number.
 *
 * INPUT      : majorNumber   const std::string&  : The type of the data.
 *             
 * OUTPUT     : number unsigned int & :The param is the count of message which the user send during one day.
 *
 * RETURN     : E_OPERATOR_SUCCED: Delete the record sucessfully.
 *              E_DATABASE_ERROR : Delete the record fail.
 *              E_MINORNUMBER_NOT_EXESIT :The minor number is not exist.
 *              E_MAJORNUMBER_NOT_EXESIT :The major number is not exist.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::getSMSCount(const std::string& majorNumber,const std::string& calledNumber, const std::string& msgInfo,unsigned int &number)
{
    //是否需要对输入参数进行判空
    //Check the major phone number has been registered the service.
    std::string userID;
    if (this->getUserID(majorNumber,userID) == E_MAJORNUMBER_NOT_EXESIT)
    {
        return E_MAJORNUMBER_NOT_EXESIT;
    }

    //Get the minor phone number has been registered the service.
    std::string sql = "select count(ID) from T_SMSDigest where CALL_TYPE = 1 and STARTDATE = curdate() and Msisdn = '";
    sql += majorNumber;
    sql += "' and MessageInfo = '";
    sql += msgInfo;
    sql += "' and Other_part = '";
    sql += calledNumber;
    sql += "';";

    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        number = 0;
        return E_DATABASE_ERROR;
    }

    if (dbrecord_.empty())
    {
        number = 0;
    }
    else
    {
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        if(it != dbrecord_.end())
        {
            DB_RECORDSET_ITEM::iterator it_ = (*it).find("count(id)");
            if(it_!=(*it).end())
            {
                number = atoi(it_->second.c_str());
            }
            else
            {
                number = 0;
            }
        }
    }
    return E_OPERATOR_SUCCED; 
}

/************************************************************************************************
 *
 * NAME       : saveSMSSummary 
 *
 * FUNCTION   : The function is used to keep the short message relation summary infomation.
 *
 * INPUT      : majorNumber   const std::string&  : The major number of the user.
 *              minorNumber   const std::string&  : The user use the number to send the message.
 *              calledNumber   const std::string&  : The called number.
 *              msgInfo   const std::string&  : The summary of the message infomation.
 *             
 * OUTPUT     : None.
 *
 * RETURN     : E_OPERATOR_SUCCED: Delete the record sucessfully.
 *              E_DATABASE_ERROR : Delete the record fail.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::saveSMSSummary(const std::string& majorNumber,const std::string& minorNumber,
                               const std::string& calledNumber,  int & callType,const std::string& msgInfo)
{
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "";
    sql.clear();
    
    //generate UUID
    char type[8];
    memset(type,'\0',8);
    sprintf(type,"%d",callType);
    std::string ruleID;
    generateUUID(ruleID);
    sql = "insert into T_SMSDigest (ID,CALL_TYPE,MessageInfo,Msisdn,Submsisdn,Other_part,STARTDATE,STARTTIME) values ('";
    sql += ruleID;
    sql += "','";
    sql += type;
    sql += "','";
    sql += msgInfo;
    sql += "','";
    sql += majorNumber;
    sql += "','";
    sql += minorNumber;
    sql += "','";
    sql += calledNumber;
    sql += "',";
    sql += "curdate()";
    sql += ",";
    sql +=" curtime()";
    sql += ")";
    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR ;        
    return E_OPERATOR_SUCCED;
}

/************************************************************************************************
 *
 * NAME       : setSMSStatus 
 *
 * FUNCTION   : The function is used to keep the short message relation summary infomation.
 *
 * INPUT      : majorNumber   const std::string&  : The major number of the user.
 *              minorNumber   const std::string&  : The user use the number to send the message.
 *              state   const std::string&  : The called number.
 *              reason   const std::string&  : The summary of the message infomation.
 *             
 * OUTPUT     : None.
 *
 * RETURN     : E_OPERATOR_SUCCED: Delete the record sucessfully.
 *              E_DATABASE_ERROR : Delete the record fail.
 *              E_MAJORNUMBER_NOT_EXESIT: The user's major number is not exist.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::setSMSStatus(const std::string& majorNumber,const std::string& minorNumber, int & state,int & count,const std::string& reason)
{
    short int opeResult = 1;
    //TO get the userID
    std::string userID;
    std::string numberID;
    if (this->getUserID(majorNumber,userID) == E_MAJORNUMBER_NOT_EXESIT)
    {
        return E_MAJORNUMBER_NOT_EXESIT;
    }    
    if(    this->getNumberID(minorNumber,numberID) != E_OPERATOR_SUCCED)
    {
        return E_DATABASE_ERROR;
    }
    //generate UUID
    char status[8];
    memset(status,'\0',8);
    sprintf(status,"%d",state);

    char sendCount[8];
    memset(sendCount,'\0',8);
    sprintf(sendCount,"%d",count);

    //To check the userName's SMSstate whether has beeb set.
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "select UserNumberID from T_SMSStatus where UserNumberID ='";
    sql += userID;
    sql += "'";
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }

    if (dbrecord_.empty())
    {
        sql = "insert into T_SMSStatus (UserNumberID,SMSState,UpdateTime,DeliverCount) values ('";
        sql += userID;
        sql += "','";
        sql += status;
        sql += "',";
        sql += "now()";
        sql += ",";
		sql += sendCount;
        sql += ")";
    }
    else
    {
        sql = "update T_SMSStatus set SMSState = '";
        sql += status;
        sql += "' ,UpdateTime = ";
        sql += "now()";
        sql += " ,DeliverCount = ";
        sql += sendCount;
        sql += " where UserNumberID='";
        sql += userID;
        sql += "';";
    }
    if( 0 != dbmysql_->executenonquery(sql))
    {
        return E_DATABASE_ERROR ;    
    }
    ResultDataUsage result = DataUsage::instance()->setMinorNumberOpeRecord(numberID, SMSFILTER_SET,PLATFORM_SMS,reason,opeResult);
    if(result != E_OPERATOR_SUCCED)
    {
        return E_DATABASE_ERROR;
    }
    return E_OPERATOR_SUCCED;
}

/************************************************************************************************
 *
 * NAME       : getSMSStatus 
 *
 * FUNCTION   : The function is used to keep the short message relation summary infomation.
 *
 * INPUT      : majorNumber   const std::string&  : The major number of the user.
 *              minorNumber   const std::string&  : The user use the number to send the message.
 *              calledNumber   const std::string&  : The called number.
 *              msgInfo   const std::string&  : The summary of the message infomation.
 *             
 * OUTPUT     : int &smscount
 *              int & state
 *
 * RETURN     : E_OPERATOR_SUCCED: Delete the record sucessfully.
 *              E_DATABASE_ERROR : Delete the record fail.
 *              E_MAJORNUMBER_NOT_EXESIT: The user's major number is not exist.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::getSMSStatus(const std::string& majorNumber,const std::string& minorNumber,int & state,int &smscount)
{
    //TO get the userID
    int limitDay=0;
    std::string userID;
    if (this->getUserID(majorNumber,userID) == E_MAJORNUMBER_NOT_EXESIT)
    {
        return E_MAJORNUMBER_NOT_EXESIT;
    }

    //To check the userName's SMSstate whether has beeb set.
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "select SMSState ,DeliverCount ,To_Days(now())-To_Days(UpdateTime) curtime from T_SMSStatus where UserNumberID ='";
    sql += userID;
    sql += "';";
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }
    state = 0;
    if (dbrecord_.empty())
    {
        state = 0;
    }
    else
    {
        DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
        if(it != dbrecord_.end())
        {
            DB_RECORDSET_ITEM::iterator it_ = (*it).find("smsstate");
            if(it_!=(*it).end())
            {
                state = atoi(it_->second.c_str());
            }

            it_ = (*it).find("delivercount");
            if(it_!=(*it).end())
            {
                smscount = atoi(it_->second.c_str());
            }

            it_ = (*it).find("curtime");
            if(it_!=(*it).end())
            {
                limitDay = atoi(it_->second.c_str());
                if(limitDay >= 1)
                {
                    state = 0;
					int smsCount=0;
                    string reason = "Day send count limit release.";
                    this->setSMSStatus( majorNumber,minorNumber, state,smsCount,reason);
                }
            }
        }
    }    
    return E_OPERATOR_SUCCED;
}

/************************************************************************************************
 *
 * NAME       : getFilterDictionary 
 *
 * FUNCTION   : The function is used to keep the short message relation summary infomation.
 *
 * INPUT      : majorNumber   const std::string&  : The major number of the user.
 *              minorNumber   const std::string&  : The user use the number to send the message.
 *              calledNumber   const std::string&  : The called number.
 *              msgInfo   const std::string&  : The summary of the message infomation.
 *             
 * OUTPUT     : None.
 *
 * RETURN     : E_OPERATOR_SUCCED: Delete the record sucessfully.
 *              E_DATABASE_ERROR : Delete the record fail.
 *              E_MAJORNUMBER_NOT_EXESIT: The user's major number is not exist.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::getFilterDictionary(vector<std::string>& dictionary)
{
    //To check the userName's SMSstate whether has beeb set.
    DB_RECORDSET_ARRAY dbrecord_;
    std::string sql = "select KeyWord from T_SMSFilter;";
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }
    if (dbrecord_.empty())
    {
        //Da filter word
        return E_OPERATOR_SUCCED;
    }

    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
    while (it != dbrecord_.end())
    {
        DB_RECORDSET_ITEM::iterator it_ = (*it).find("keyword");
        if(it_!=(*it).end())
        {
            //std::string minorNumber = it_->second;
            dictionary.push_back(it_->second);    
        }
        ++it;
    }
    LogFileManager::getInstance()->write(Debug,"E_OPERATOR_SUCCED....");
    return E_OPERATOR_SUCCED;
}

/************************************************************************************************
 *
 * NAME       : addFilterDictionary 
 *
 * FUNCTION   : The function is used to keep the short message relation summary infomation.
 *
 * INPUT      : majorNumber   const std::string&  : The major number of the user.
 *              minorNumber   const std::string&  : The user use the number to send the message.
 *              calledNumber   const std::string&  : The called number.
 *              msgInfo   const std::string&  : The summary of the message infomation.
 *             
 * OUTPUT     : None.
 *
 * RETURN     : E_OPERATOR_SUCCED: Delete the record sucessfully.
 *              E_DATABASE_ERROR : Delete the record fail.
 *              E_MAJORNUMBER_NOT_EXESIT: The user's major number is not exist.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::addFilterDictionary(vector<std::string>& dictionary)
{
    //To check the userName's SMSstate whether has beeb set.
    DB_RECORDSET_ARRAY dbrecord_;
    std::string ruleID;
    generateUUID(ruleID);

    std::string sql = "insert into T_SMSFilter(ID,KeyWord,Level) values('";
    for(int loopI=0;loopI<dictionary.size();loopI++)
    {
        sql.clear();
        ruleID.clear();
        generateUUID(ruleID);
        sql = "insert into T_SMSFilter(ID,KeyWord,Level) values('";
        sql += ruleID;
        sql += "','";
        sql += dictionary[loopI];
        sql += "',0)";

        if( 0 != dbmysql_->executenonquery(sql))
        {
			LogFileManager::getInstance()->write(Debug,"E_DATABASE_ERROR sql (%s)", sql.c_str());
            return E_DATABASE_ERROR ;    
        }
    }
    return E_OPERATOR_SUCCED;
}
//.e 增加对于副号码短信过滤和统计功能
//.s 增加对于北京移动用户行为数据设置接口.T_MinorNumberOperateRecord whc 2010-04-08
/************************************************************************************************
 *
 * NAME       : setMinorNumberOpeRecord 
 *
 * FUNCTION   : The function is used to user setting record about his minornumber.
 *              These action need to be record as follow :
 *              BlackList setting ,ringback tone setting ,minornumber sequence setting and so on.
 *
 * INPUT      : numberID   const std::string&  : The identy number of the minor number.
 *              opeActionID    OperateAction&  : The action that user takes on the minor number.
 *              opeChannelID  OperateChannel&  : The service channel that user take action by.
 *              operatorNote  const std::string&  : The description about the setting operation.
 *              opeResult     short int&       : The result of the operation that user take.
 *             
 * OUTPUT     : None
 *
 * RETURN     : E_OPERATOR_SUCCED: Setting the record sucessfully.
 *              E_DATABASE_ERROR : Databace opeartion is failure.
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::setMinorNumberOpeRecord(const std::string& numberID,OperateAction opeActionID,OperateChannel opeChannelID,
                                         const std::string& operatorNote,short int& opeResult)
{
    std::string sql;
    //generate UUID
    std::string seqID;
    generateUUID(seqID);
    //Convert the int to char*
    char opeActionIDStr[4];
    memset(opeActionIDStr,'\0',sizeof(opeActionIDStr));
    sprintf(opeActionIDStr,"%d",opeActionID);

    char opeChannelIDStr[4];
    memset(opeChannelIDStr,'\0',sizeof(opeChannelIDStr));
    sprintf(opeChannelIDStr,"%d",opeChannelID);
    char opeResultStr[4];
    memset(opeResultStr,'\0',sizeof(opeResultStr));
    sprintf(opeResultStr,"%d",opeResult);    

    sql.clear();
    sql = "insert into T_MinorNumberOperateRecord (RecordID,NumberID,OperateActionID,OperateTime,OperateName,OperateChannelID,Result) values ('";
    //sql = "insert into T_MinorNumberOperateRecord (RecordID,NumberID,OperateActionID,OperateTime,OperateName) values ('";
    sql += seqID;
    sql += "','";

    sql += numberID;
    sql += "','";

    sql += opeActionIDStr;
    sql += "',";
    
    sql += "now(),'";

    sql += operatorNote;
    sql += "','";

    sql += opeChannelIDStr;
    sql += "','";

    sql += opeResultStr;
    sql += "');";
    if( 0 != dbmysql_->executenonquery(sql))
    {
        LogFileManager::getInstance()->write(Debug,"E_DATABASE_ERROR sql (%s)", sql.c_str());
        return E_DATABASE_ERROR;
    }
    return E_OPERATOR_SUCCED;
}

/************************************************************************************************
 *
 * NAME       : setOpeChannelID 
 *
 * FUNCTION   : The function is set the operation channel.
 *
 * INPUT      : opeChannelID  OperateChannel&  : The service channel that user take action by.
 *             
 * OUTPUT     : None
 *
 * RETURN     : None
 *
 ***********************************************************************************************/
void DataUsage::setOpeChannelID(OperateChannel opeChannelID)
{
    channelID_ = opeChannelID;
}

/************************************************************************************************
 *
 * NAME       : getLastCalledRecord 
 *
 * FUNCTION   : The function is get the last call record
 *
 * INPUT      : opeChannelID  OperateChannel&  : The service channel that user take action by.
 *             
 * OUTPUT     : callTime :  If the value is '1970-01-01' that indicate there is no callrecord,
 *                          else get the last call time :
 *
 * RETURN     : E_MINORNUMBER_NOT_EXESIT :minornumber is no exist;
 *              E_DATABASE_ERROR  : Databace error
 *              E_OPERATOR_SUCCED  :sucess
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::getLastCalledRecord(const std::string& minorNumber,std::string&  callTime)
{
    //check the minor number available
    std::string  numberID;

    ResultDataUsage ret = getNumberID(minorNumber,numberID);
    if(ret != E_OPERATOR_SUCCED)
    {
        if(ret == E_MINORNUMBER_NOT_EXESIT)
        {
            return E_MINORNUMBER_NOT_EXESIT;
        }
        else
        {
            return E_DATABASE_ERROR;
        }
    }

    std::string sql;
    string startDate = "1970-01-01";
    string startTime = "00:00:00";
    std::string outTime;
    char buff[32];
    memset(buff,0,32);

    //Sensitive Field encrypt -----start----------------------
    unsigned char key[8];
    memset(key, 0 , sizeof(key));
    strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
    //strncpy((char *)&key[0], "SIMMDB", sizeof(key)-1);
    Crypt crypt;
    char preMinorNumber[16] ;
    memset(preMinorNumber,'\0',16);
    strncpy(preMinorNumber,minorNumber.c_str(),minorNumber.size());
    string encrypt_minor = crypt.encrypt(key, preMinorNumber); 

    sql.clear();
    sql = " select * from (select * from T_CallRecord where Other_part='";
    sql += encrypt_minor;

    sql += "' and CALL_TYPE = 2 and STARTDATE = (select max(STARTDATE) from  T_CallRecord where Other_part = '";
    sql += encrypt_minor ;
    sql += "' and CALL_TYPE = 2) ) tt order by STARTTIME desc";

    DB_RECORDSET_ARRAY dbrecord_;
    if(0 != dbmysql_->executequery(sql,dbrecord_))
    {
        return E_DATABASE_ERROR;
    }

    if(dbrecord_.empty())
    {
        sprintf(buff,"%s %s",startDate.c_str(),startTime.c_str());
        callTime = string(buff);
        return E_OPERATOR_SUCCED;
    }

    DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();

    //1970-01-01 00:00:00
    if(it != dbrecord_.end())
    {
        DB_RECORDSET_ITEM& item_ = (*it);
        DB_RECORDSET_ITEM::iterator it_ = item_.find("starttime");
        if(it_!=item_.end())
        {
            startTime = it_->second;
        }
        it_ = item_.find("startdate");
        if(it_!=item_.end())
        {
            startDate = it_->second;
        }
    }

    sprintf(buff,"%s %s",startDate.c_str(),startTime.c_str());
    callTime = string(buff);
    return E_OPERATOR_SUCCED;
}

ResultDataUsage  DataUsage::setSMSNotify(const std::string & userNumber, int opt)
{
    std::string  UserID;
    ResultDataUsage result = getUserID(userNumber, UserID);
    if(result != E_OPERATOR_SUCCED)
    {
		 return result;
	}

    std::string isAccepted;
    if(opt == 1)
    {
		isAccepted = "1";
	}
    else //opt == 0
    {
		isAccepted = "0";
	}

    std::string sql = "select IsAccepted from T_SMSNotify where UserID = '";
    sql += UserID;
    sql += "'";
    
    DB_RECORDSET_ARRAY dbRecord;
    if( 0 != dbmysql_->executequery(sql,dbRecord))
        return E_DATABASE_ERROR;
    
    if (dbRecord.empty())
    {
        //generate UUID
        std::string seqID;
        generateUUID(seqID);

        sql.clear();
        sql = "insert into T_SMSNotify (ItemID,UserID,IsAccepted,DeliverCount,UpdateTime) values ('";
        sql += seqID;
        sql += "','";
        sql += UserID;
        sql += "','";
        sql += isAccepted;
        sql += "','0',now())";
        
        if( 0 != dbmysql_->executenonquery(sql))
        {
            return E_DATABASE_ERROR;
        }
        //for sync the database when there is change.    
        result = this->setUnSynchronized(REGISTERUSER, userNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }        
        return E_OPERATOR_SUCCED;
    }
    else
    {
        sql.clear();

        sql = "update T_SMSNotify set IsAccepted = '";
        sql += isAccepted;
        sql += "', DeliverCount = '0' , UpdateTime = now() where UserID = '";
        sql += UserID;
        sql += "'";
        
        if( 0 != dbmysql_->executenonquery(sql))
        {
            return E_DATABASE_ERROR;
        }
        // for sync the database when there is change.
        result = this->setUnSynchronized(REGISTERUSER, userNumber);
        if(result == E_DATABASE_ERROR)
        {
            return E_DATABASE_ERROR;
        }            
        return E_OPERATOR_SUCCED;
    }
    return E_OPERATOR_SUCCED;
}

/*
 *  notifyFlag  int&:    1:Permit the short message notify.
 *                       0:No need send the short message notify.
 */

ResultDataUsage DataUsage::isSMSNotify(const std::string & userNumber, int& deliverCount,int & notifyFlag)
{

	std::string  UserID;
	notifyFlag = 0;
	ResultDataUsage result = getUserID(userNumber, UserID);
	if(result != E_OPERATOR_SUCCED)
		return E_DATABASE_ERROR;
	
	std::string sql = "select IsAccepted , DeliverCount from T_SMSNotify where UserID = '";
	sql += UserID;
	sql += "'";
	
	DB_RECORDSET_ARRAY dbRecord;
	if( 0 != dbmysql_->executequery(sql,dbRecord))
		return E_DATABASE_ERROR;
	

	if(dbRecord.empty())
	{
		//generate UUID
		std::string seqID;
		generateUUID(seqID);

		sql.clear();
		sql = "insert into T_SMSNotify (ItemID,UserID,IsAccepted,DeliverCount,UpdateTime) values ('";
		sql += seqID;
		sql += "','";
		sql += UserID;
		sql += "','1','0',now())";
		
		if( 0 != dbmysql_->executenonquery(sql))
		{
			return E_DATABASE_ERROR;
		}
		deliverCount = 0;
		notifyFlag = 1;
	}
	else
	{
		DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
		while (it != dbRecord.end())
		{
			DB_RECORDSET_ITEM::iterator it_ = (*it).find("isaccepted");
			std::string value = it_->second;

			it_ = (*it).find("delivercount");
			deliverCount = atoi(it_->second.c_str());

			if( value =="1")
			{
				notifyFlag = 1;
				break;
			}
			else//value == "0"
			{
				notifyFlag = 0;
				break;
			}
			++it;
		}
	}

	return E_OPERATOR_SUCCED;
}

ResultDataUsage DataUsage::deliverSMSCountPlus(const std::string & userNumber,const int & countNum)
{
	std::string  UserID;
	ResultDataUsage result = getUserID(userNumber, UserID);
	if(result != E_OPERATOR_SUCCED)
		return result;


	std::string sql = "select DeliverCount from T_SMSNotify where UserID = '";
	sql += UserID;
	sql += "'";
	
	DB_RECORDSET_ARRAY dbRecord;
	if( 0 != dbmysql_->executequery(sql,dbRecord))
		return E_DATABASE_ERROR;
	
	if (dbRecord.empty())
	{
		return E_MAJORNUMBER_NOT_EXESIT;
	}
	else
	{
		int count=0;

		DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
		while (it != dbRecord.end())
		{
			DB_RECORDSET_ITEM::iterator it_ = (*it).find("delivercount");
			count = atoi(it_->second.c_str());
			if(count <= countNum)
				count++;

			++it;
		}

		char buff[10]={0};
		sprintf(buff,"%d",count);

		sql.clear();

		sql = "update T_SMSNotify set DeliverCount = '";
		sql += buff;
		sql += "' where UserID = '";
		sql += UserID;
		sql += "'";
		
		if( 0 != dbmysql_->executenonquery(sql))	
		{
			return E_DATABASE_ERROR;
		}

	}

	return E_OPERATOR_SUCCED;
}

/************************************************************************************************
 *
 * NAME       : setUndesirableNumberList 
 *
 * FUNCTION   : The function is cancel the short message notify about the platform action.
 *
 * INPUT      : majorNumber  std::string  : The major number that request to forbide the action notify.
 *             
 * OUTPUT     : E_OPERATOR_SUCCED :sucess
 *              E_DATABASE_ERROR  :error
 *              E_MAJORNUMBER_NOT_EXESIT :User number is not exist.
 *
 * RETURN     : None
 *
 ***********************************************************************************************/
ResultDataUsage DataUsage::setUndesirableNumberList(std::string majorNumber)
{
    std::string userID;
    ResultDataUsage reslt = this->getUserID(majorNumber,userID);
    if (reslt == E_MAJORNUMBER_NOT_EXESIT)
    {
        return E_MAJORNUMBER_NOT_EXESIT;
    }
    else if(reslt == E_DATABASE_ERROR)
    {
        return E_DATABASE_ERROR;
    }
    //To check whether has report in the infoReport table.
    std::string numberID;
    std::string sql = "select ListId from T_UndesirableNumberList where UserNumber = '";
    sql += majorNumber;
    sql += "';";
    DB_RECORDSET_ARRAY dbRecord;
    //generate UUID
    std::string seqID;
    generateUUID(seqID);
    if( 0 != dbmysql_->executequery(sql,dbRecord))
    {
        return E_DATABASE_ERROR;
    }
    std::string limitType = "1";
    if (dbRecord.empty())
    {
        sql.clear();
        sql = "insert into T_UndesirableNumberList(ListId,UserNumber,limitType) values('";
        sql += seqID;
        sql += "','";
        sql += majorNumber;
        sql += "','";
        sql += limitType;
        sql += "');";
        if( 0 != dbmysql_->executenonquery(sql))
        {
            LogFileManager::getInstance()->write(Debug,"DataLogic [setUserNumInfoReport]  E_DATABASE_ERROR sql (%s)", sql.c_str());    
            return E_DATABASE_ERROR;
        }
    }
    return E_OPERATOR_SUCCED;
}

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
ResultDataUsage DataUsage::getCallForwardNumber(const std::string& minorNumber,vector<CallForwardInfo>& callForwardNumber)
{
	callForwardNumber.clear();

    //check the minor number available
    std::string  numberID;
    ResultDataUsage ret = getNumberID(minorNumber,numberID);
    if(ret != E_OPERATOR_SUCCED)
    {
        return ret;
    }

    //To get the forward number from database.
    DB_RECORDSET_ARRAY dbRecord;
    std::string sql = "select ForwardNumber,ForwardOrder from T_CallForwardList where MinorNumberID = '";
    sql += numberID;
    sql += "' order by ForwardOrder;";

    if( 0 != dbmysql_->executequery(sql,dbRecord) )
    {
        return E_DATABASE_ERROR;
    }

    if( !dbRecord.empty() )
    {
        DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();
        while (it != dbRecord.end())
        {
			CallForwardInfo callObj;
            DB_RECORDSET_ITEM::iterator it_ = (*it).find("forwardnumber");
			callObj.calledNumber = it_->second;

            it_ = (*it).find("forwardorder");
			callObj.order = atoi(it_->second.c_str());

			callForwardNumber.push_back(callObj);
            ++it;
        }
    }

    return E_OPERATOR_SUCCED;
}

 /************************************************************************************************
  *
  * NAME       : setCallForwardNumber 
  *
  * FUNCTION   : The function is used to set call forward number list with the minor numberID.
  *              you can set one number as the call forward number when the first called is no answer.
  *
  * INPUT      : minorNumber   const string& : The number of the major number.
  *              
  *              callForwardNumber   string& : The call forward number by the user for the minor number.
  *                                 if its value is empty indicate that there is no set for calling forward.
  * OUTPUT     : None
  *
  * RETURN     : E_OPERATOR_SUCCED: Set call forward number is sucessfully..
  *              E_DATABASE_ERROR : The database operation is fail.
  *
  ***********************************************************************************************/
ResultDataUsage DataUsage::setCallForwardNumber(const std::string& minorNumber,CallForwardInfo & info,OperateChannel inChanelID)
{
    //check the minor number available
    std::string  numberID;
    ResultDataUsage ret = getNumberID(minorNumber,numberID);
    if(ret != E_OPERATOR_SUCCED)
    {
        return ret;
    }

    std::string order = "-1";
	if(info.order==0)
		order = "0";

    DB_RECORDSET_ARRAY dbRecord;
    short int opeResult = 0;
    //generate UUID
    std::string seqID;
    generateUUID(seqID);

	OperateChannel tmpChanelID = this->confirmChannel(inChanelID);
	std::string operatorNote = "Add call forward number "+info.calledNumber;
    //To get the forward number.
    std::string sql = "select ForwardNumber from T_CallForwardList where MinorNumberID = '";
    sql += numberID;
    sql += "';";

    if( 0 != dbmysql_->executequery(sql,dbRecord))
    {
        return E_DATABASE_ERROR;
    }
    if (dbRecord.empty())
    {
        sql.clear();
        sql = "insert into T_CallForwardList(RecordID,MinorNumberID,ForwardNumber,ForwardOrder, CustomTime) values('";
        sql += seqID;
        sql += "','";
        sql += numberID;
        sql += "','";
        sql += info.calledNumber;
		sql += "','";
		sql += order;
        sql += "',now());";
        if( 0 != dbmysql_->executenonquery(sql))
        {
            opeResult = -1;
            this->setMinorNumberOpeRecord(numberID, VOICEMESSAGEBOX_SET,tmpChanelID,operatorNote,opeResult);
            return E_DATABASE_ERROR;
        }
    }
	else
	{
        sql.clear();
        sql = "update T_CallForwardList set ForwardNumber='";
        sql += info.calledNumber;
        sql += "',ForwardOrder='";
		sql += order;
		sql += "', CustomTime=now() where MinorNumberID ='";
		sql += numberID;
        sql += "';";
        if( 0 != dbmysql_->executenonquery(sql))
        {
            opeResult = -1;
            this->setMinorNumberOpeRecord(numberID, CALLFORWARD_SET,tmpChanelID,operatorNote,opeResult);
            return E_DATABASE_ERROR;
        }
	}
    this->setMinorNumberOpeRecord(numberID, CALLFORWARD_SET,tmpChanelID,operatorNote,opeResult);
    return E_OPERATOR_SUCCED;
}

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
  *              E_MINORNUMBER_NOT_EXESIT :The minor number is not exist.
  *
  ***********************************************************************************************/
ResultDataUsage DataUsage::cancelCallForwardNumber(const std::string& minorNumber,OperateChannel inChanelID)
{
    //check the minor number available
    std::string  numberID;
    ResultDataUsage ret = getNumberID(minorNumber,numberID);
    if(ret != E_OPERATOR_SUCCED)
    {
        return ret;
    }

    short int opeResult = 0;
	DB_RECORDSET_ARRAY dbRecord;
	OperateChannel tmpChanelID = this->confirmChannel(inChanelID);
    
	std::string  operatorNote = "Cancel the call forward number";
    std::string sql = "delete from T_CallForwardList where MinorNumberID = '";
    sql += numberID;
    sql += "';";

    if( 0 != dbmysql_->executequery(sql,dbRecord))
    {
        opeResult = -1;
        this->setMinorNumberOpeRecord(numberID, CALLFORWARD_SET,tmpChanelID,operatorNote,opeResult);
        return E_DATABASE_ERROR;
    }
    else
    {
        opeResult = 0;
        this->setMinorNumberOpeRecord(numberID, CALLFORWARD_SET,tmpChanelID,operatorNote,opeResult);
    }
    return E_OPERATOR_SUCCED;
}

ResultDataUsage DataUsage::deleteCallForwardNumber(const std::string& minorNumber,const string & number,OperateChannel inChanelID)
{
    //check the minor number available
    std::string  numberID;
    ResultDataUsage ret = getNumberID(minorNumber,numberID);
    if(ret != E_OPERATOR_SUCCED)
    {
        return ret;
    }

    short int opeResult = 0;
	DB_RECORDSET_ARRAY dbRecord;
	OperateChannel tmpChanelID = this->confirmChannel(inChanelID);
    
	std::string  operatorNote = "delete the call forward number" + number;
    std::string sql = "delete from T_CallForwardList where MinorNumberID = '";
    sql += numberID;
    sql += "' and ForwardNumber='";
	sql += number;
	sql +="';";

    if( 0 != dbmysql_->executequery(sql,dbRecord))
    {
        opeResult = -1;
        this->setMinorNumberOpeRecord(numberID, CALLFORWARD_SET,tmpChanelID,operatorNote,opeResult);
        return E_DATABASE_ERROR;
    }
    else
    {
        opeResult = 0;
        this->setMinorNumberOpeRecord(numberID, CALLFORWARD_SET,tmpChanelID,operatorNote,opeResult);
    }
    return E_OPERATOR_SUCCED;
}

ResultDataUsage DataUsage::saveSMSRecord(const string& callType, const string& msIsdn, const string& subIsdn, const string& otherPart)
{
    //Formate the time recorded in call  bill.
    std::string startDate;
    std::string startTime;
	time_t nowTime = time(NULL);
    std::string dateTime = LonglongtoStr(nowTime);
    startDate = dateTime.substr(0,dateTime.find_first_of(" "));
    startTime = dateTime.substr(dateTime.find_first_of(" ") + 1);

    //generate UUID
    std::string seqID;
    generateUUID(seqID);

    //Sensitive Field encrypt -----start----------------------
    unsigned char key[8];
    memset(key, 0 , sizeof(key));
    strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
    Crypt crypt;

	string encrypt_msIsdn = crypt.encrypt(key, (char *)msIsdn.c_str()); 

    string encrypt_subIsdn = crypt.encrypt(key, (char *)subIsdn.c_str()); 

	string encrypt_otherPart = crypt.encrypt(key, (char *)otherPart.c_str());
    //Sensitive Field encrypt -----end----------------------

    string sql = "insert into T_SmsRecord (SEQID,CALL_TYPE,Msisdn,Submsisdn,Other_part,STARTDATE,STARTTIME) values ('";
    sql += seqID;
    sql += "', '";
    sql += callType;
    sql +="', '";
    sql += encrypt_msIsdn;
    sql += "','";
    sql += encrypt_subIsdn;
    sql += "','";
    sql += encrypt_otherPart;
    sql += "','";
    sql += startDate;
    sql += "','";
    sql += startTime;
    sql += "')";

    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR ;
    return E_OPERATOR_SUCCED;
}

ResultDataUsage DataUsage::saveUserInputInfo(const string& userNumber, const string& inputInfo, const string& result, OperateChannel inChanelID)
{
	////
	string characterEncod = "set names 'utf8'";
    dbmysql_->executenonquery(characterEncod);

    //generate UUID
    string seqID;
    generateUUID(seqID);

	char opeChannelIDStr[4];
    memset(opeChannelIDStr, '\0', sizeof(opeChannelIDStr));
	sprintf(opeChannelIDStr,"%d",inChanelID);

    string sql = "insert into T_UserInputInfo (ID,UserNumber,OperateChannelID,InputInfo,result,InputTime) values ('";
    sql += seqID;
    sql += "', '";
    sql += userNumber;
    sql +="', '";
    sql += opeChannelIDStr;
    sql += "','";
    sql += inputInfo;
    sql += "','";
    sql += result;
    sql += "',now())";

    if( 0 != dbmysql_->executenonquery(sql))
        return E_DATABASE_ERROR ;
    return E_OPERATOR_SUCCED;
}


int  DataUsage::getMinorDelayData(const string& majorNumber ,const string & minorNumber)
{
	int iRet = 0  ;
	std::string  sql = "select OpCode from T_DelayData where UserNumber ='";
	sql+=majorNumber ;
	sql+="'";
	sql+=" and DataXml like '%";
	sql+=minorNumber;
	sql+="%'";

	DB_RECORDSET_ARRAY dbrecord_;
	dbmysql_->executequery(sql,dbrecord_);
	if (dbrecord_.empty())
	{
		return 0;
	}

	//当能查到其opcode
	//数据导入参数中
	DB_RECORDSET_ARRAY::iterator it = dbrecord_.begin();
	string  strResult="";
	while (it!=dbrecord_.end())
	{
		DB_RECORDSET_ITEM item_ = (*it);
		strResult = (*(item_.find("opcode"))).second;
		++it;
	}

	if ( strResult == "02")
	{
		return  1 ;
	}
	
	return 0 ;

}


//return 1表示该号码是限制的
int DataUsage::getSMSLimit106Number(const string & calledNumber)
{
		int iRet = 0  ;
		std::string  sql = "select limitnumber from t_limit106_numberpool__fj where limitnumber ='";
	  sql+=calledNumber ;
	  sql+="'";
	
		DB_RECORDSET_ARRAY dbrecord_;
		dbmysql_->executequery(sql,dbrecord_);
		if (dbrecord_.empty())
		{
			return 0;
		}

		return 1;
}




// Dialy Work 2014-02-14
int  DataUsage::getDialyCallRecord(const string & strDay , std::vector<CallRecordInfo *> & vecCallRecord)
{
	 string  strSql ="";
	 strSql +="select CALL_TYPE ,Msisdn ,Submsisdn ,count(Msisdn) as CallCount ,sum(CALL_DURATION) as SumDURATION ,STARTDATE from T_CallRecord ";
	 strSql +="where STARTDATE = '";
	 strSql += strDay ;
	 strSql += "' ";
	 strSql +=" group by Submsisdn ,CALL_TYPE";
	 
	 	DB_RECORDSET_ARRAY dbRecord;
		if( 0 != dbmysql_->executequery(strSql,dbRecord))
		return -1;
	
	 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
	 DB_RECORDSET_ITEM::iterator it_ ;
	 	

	 string strCallType="";
	 string strMsisdn="";
	 string strSubmsisdn="";
	 string strOtherpart="";
	 string strDate="";
	 string strCount="";
	 string strCallDuration="";


	 
	 while (it != dbRecord.end())
	 {
	 		CallRecordInfo *pObj = new CallRecordInfo ;
	 		memset(pObj,0,sizeof(CallRecordInfo));
	 		 string strCallType="";
	  	 strMsisdn="";
	     strSubmsisdn="";
	     strOtherpart="";
	     strDate="";
	     strCount="";
	     strCallDuration="";

		 it_ = (*it).find("call_type");
		 if ( it_ != it->end())
		 {
			 strCallType = it_->second;
		 }

		 it_ = (*it).find("msisdn");
		 if ( it_ != it->end())
		 {
				strMsisdn = it_->second;
		 }
			
		 //
		 it_ = (*it).find("submsisdn");		
		 if ( it_ != it->end())
		 {
			 strSubmsisdn = it_->second;
		 }

		 it_ = (*it).find("callcount");		
		 if ( it_ != it->end())
		 {
			 strCount = it_->second;
		 }
		 

		 it_ = (*it).find("startdate");
		 if ( it_ != it->end())
		 {
			 strDate = it_->second;
		 }

		 it_ = (*it).find("sumduration");
		 if ( it_ != it->end())
		 {
				strCallDuration =it_->second;
		 }
		 
			strncpy(pObj->calltype,strCallType.c_str(),2);
			strncpy(pObj->msisdn,strMsisdn.c_str(),32);
			strncpy(pObj->submsisdn,strSubmsisdn.c_str(),32);
			strncpy(pObj->startdate,strDate.c_str(),11);
			strncpy(pObj->count,strCount.c_str(),8);
			strncpy(pObj->call_duration,strCallDuration.c_str(),12);
		
			vecCallRecord.push_back(pObj);
		
		 	++it;
	 }	
	 
	 
	 return 0;
	 
	
}



int DataUsage::InsertDialyCallRecord(std::vector<CallRecordInfo *> & vecCallRecord )
{
		std::vector<CallRecordInfo *>::iterator it ;
		string sql;
		string strUUID="";
		unsigned char key[8];
    memset(key, 0 , sizeof(key));
    strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
    Crypt crypt;
    string strMajNum="";
    string strMinorNum="";
    string strOriginalNum="";
    string strZone="";
    		
		CallRecordInfo *pTemp ;
		int iLoop = 0 ;
	  int iYear ;
	  std::string strYear="";
	  std::string StrTable="T_CallRecord";
	  time_t rawtime;
	  struct tm* timeinfo;
	  time(&rawtime);
	  timeinfo = localtime(&rawtime);
	  iYear= 1900+timeinfo->tm_year;
	  char szYear[12]={0};
	  sprintf(szYear,"%d",iYear);
	  strYear = szYear;
	  StrTable += strYear ;
	 
	  StrTable= "T_CallRecordReport";
	  

	  std::map<std::string,std::string>::iterator  itMap ;	  
		for ( it= vecCallRecord.begin() ; it!= vecCallRecord.end(); it++)
		{		
				 strUUID ="";
				 generateUUID(strUUID);
				 pTemp = *it ;
				 strMajNum= crypt.decrypt(key, (char *)pTemp->msisdn);
				 strMinorNum = crypt.decrypt(key, (char *)pTemp->submsisdn);

		
				 	itMap = m_mapMinorZoneInfo.find(strMinorNum);
					if ( itMap != m_mapMinorZoneInfo.end())
					{
						strZone=itMap->second ;
					}
					else
					{
						strZone="xxxx";
					}
		
					
				sql = "insert into  ";
				sql += StrTable ;
				sql += " (SEQID,CALL_TYPE,Msisdn,Submsisdn,STARTDATE,SUM_CALL_DURATION,Count,ZoneCode) values ('";
		    sql += strUUID;
		    sql += "', '";
		    sql += pTemp->calltype;
		    sql +="', '";
		    sql += strMajNum;
		    sql += "','";
		    sql += strMinorNum;
		    sql += "','";
		    sql += pTemp->startdate;
		    sql += "','";
		    sql += pTemp->call_duration;
		    sql += "','";
		    sql += pTemp->count;
		    sql += "','";
		    sql += strZone;
		    sql += "')";
		
		    if( 0 != dbmysql_->executenonquery(sql))
				{
						printf("Insert T_CallRecord2014 error! \n");	
				}

		    iLoop++;
		    
		    if (iLoop == 500)
		    {
		    	iLoop = 0 ;
		    	usleep(1000*1); //10ms
		    }
		}		
	
	
	
		return 0 ;

}

int  DataUsage::getDialySmsRecord(const string & strDay , std::vector<SmsRecordInfo *> & vecSmsRecord)
{
	
	 string  strSql ="";
	 strSql +="select CALL_TYPE ,Msisdn ,Submsisdn ,count(Msisdn) as SmsCount ,STARTDATE  from T_SmsRecord ";
	 strSql +="where STARTDATE = '";
	 strSql += strDay ;
	 strSql += "' ";
	 strSql += " group by Submsisdn ,CALL_TYPE";
	 
	 DB_RECORDSET_ARRAY dbRecord;
	 if( 0 != dbmysql_->executequery(strSql,dbRecord))
		return -1;
	
	 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
	 DB_RECORDSET_ITEM::iterator it_ ;

	 string strCallType="";
	 string strMsisdn="";
	 string strSubmsisdn="";
	 string strDate="";
	 string strCount="";

	 while (it != dbRecord.end())
	 {
	 		SmsRecordInfo *pObj = new SmsRecordInfo ;
	 		memset(pObj,0,sizeof(SmsRecordInfo));
	 		string strCallType="";
	  	strMsisdn="";
	    strSubmsisdn="";
	    strDate="";
	    strCount="";

		 it_ = (*it).find("call_type");
		 if ( it_ != it->end())
		 {
			 strCallType = it_->second;
		 }

		 it_ = (*it).find("msisdn");
		 if ( it_ != it->end())
		 {
				strMsisdn = it_->second;
		 }
			
		 //
		 it_ = (*it).find("submsisdn");		
		 if ( it_ != it->end())
		 {
			 strSubmsisdn = it_->second;
		 }

		 it_ = (*it).find("smscount");
		 if ( it_ != it->end())
		 {
				strCount = it_->second;
		 }
		 
		 it_ = (*it).find("startdate");
		 if ( it_ != it->end())
		 {
			 strDate = it_->second;
		 }

		 
			strncpy(pObj->calltype,strCallType.c_str(),2);
			strncpy(pObj->msisdn,strMsisdn.c_str(),32);
			strncpy(pObj->submsisdn,strSubmsisdn.c_str(),32);
			strncpy(pObj->startdate,strDate.c_str(),11);
			strncpy(pObj->count,strCount.c_str(),8);

			vecSmsRecord.push_back(pObj);
		
		 	++it;
	 }	

	 return 0 ;

}


int  DataUsage::InsertDialySmsRecord(std::vector<SmsRecordInfo *> & vecSmsRecord)
{
	
		std::vector<SmsRecordInfo *>::iterator it ;
		string sql;
		string strUUID="";
		unsigned char key[8];
    memset(key, 0 , sizeof(key));
    strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
    Crypt crypt;
    string strMajNum="";
    string strMinorNum="";
    string strOriginalNum="";
    string strZone="";
    
		SmsRecordInfo *pTemp ;
		int iLoop = 0 ;
		
		int iYear ;
	  std::string strYear="";
	  std::string StrTable="T_SmsRecord";
	  time_t rawtime;
	  struct tm* timeinfo;
	  time(&rawtime);
	  timeinfo = localtime(&rawtime);
	  iYear= 1900+timeinfo->tm_year;
	  char szYear[12]={0};
	  sprintf(szYear,"%d",iYear);
	  strYear = szYear;
	  StrTable += strYear ;
		
		
		StrTable = "T_SmsRecordReport" ;		
	  std::map<std::string,std::string>::iterator itMap ;
		for ( it= vecSmsRecord.begin() ; it!= vecSmsRecord.end(); it++)
		{		
				 strUUID ="";
				 generateUUID(strUUID);
				 pTemp = *it ;
				 strMajNum= crypt.decrypt(key, (char *)pTemp->msisdn);
				 strMinorNum = crypt.decrypt(key, (char *)pTemp->submsisdn);

				 
				 itMap = m_mapMinorZoneInfo.find(strMinorNum);
					if ( itMap != m_mapMinorZoneInfo.end())
					{
						strZone=itMap->second ;
					}
					else
					{
						strZone="xxxx";
					}
		
				sql = "insert into ";
				sql += StrTable ;
				sql += " (SEQID,CALL_TYPE,Msisdn,Submsisdn,Count,STARTDATE,ZoneCode) values (' ";
		    sql += strUUID;
		    sql += "', '";
		    sql += pTemp->calltype;
		    sql +="', '";
		    sql += strMajNum;
		    sql += "','";
		    sql += strMinorNum;
		    sql += "','";
		    sql += pTemp->count;
		    sql += "','";
		    sql += pTemp->startdate;
		    sql += "','";
		    sql += strZone;
		    sql += "')";
		
		    if( 0 != dbmysql_->executenonquery(sql))
				{
						printf("Insert T_SmsRecord2014 error! \n");	
				}

		    iLoop++;
		    
		    if (iLoop == 500)
		    {
		    	iLoop = 0 ;
		    	usleep(1000*1); //10ms
		    }
		}		
	
		return 0 ;

}

int  DataUsage::getMinorAndBakInfoByDay(const string & strDay )
{
			m_mapMinorZoneInfo.clear();
			ResultDataUsage ret= getMinorNumberZoneInfo(m_mapMinorZoneInfo);
			string  strSql = "select MinorNumber ,ZoneCode  from t_minornumber_bak where BakTime >'";
			strSql+= strDay ;
			strSql+="'";
			DB_RECORDSET_ARRAY dbRecord;
			if( 0 != dbmysql_->executequery(strSql,dbRecord))
				return -1;
			
			 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
			 DB_RECORDSET_ITEM::iterator it_ ;
			 std::string strMinor="";
			 std::string strZone="";
			 while (it != dbRecord.end())
			 {
				 it_ = (*it).find("minornumber");
				 if ( it_ != it->end())
				 {
					 strMinor = it_->second;
				 }
		
				 it_ = (*it).find("zonecode");
				 if ( it_ != it->end())
				 {
					strZone = it_->second;
				 }
		
				 if( strMinor !="" && strZone != "")
				 {
					m_mapMinorZoneInfo[strMinor]=strZone;
				 }
		
				 ++it;
			 }	
			
			return 0 ;
}


int DataUsage::InsertXiamenUser2Fj(const string & strMajorNum,const string & strMinorNum,const string &strRegTime ,const string &strZone ,const string &strState)
{
	  std::string strSql="";
	  std::string strUser="";
	  std::string strPWD="123456";
	  std::string strIMSI="VIMSI";
	  std::string strOperatorID="100";
	  strUser=strMajorNum;
	  std::string strUserID="";
	  
	  //Check the input number whether has register as major number.
    std::string strSqlFind = "select UserID from T_RegisterUser where UserNumber = '";
    strSqlFind += strMajorNum;
    strSqlFind += "'";
    DB_RECORDSET_ARRAY majorNumberRecord;
    if( 0 != dbmysql_->executequery(strSqlFind,majorNumberRecord))
    {
    	  return -1 ;
    }
        
    if (majorNumberRecord.empty())
    {
    		//major is empty
        //generate UUID
		    std::string seqID;
		    generateUUID(seqID);
		    strUserID= seqID;

				 //next
				 strSql.clear();
				 strSql = "insert into T_RegisterUser(UserID,UserNumber,UserName,PWD,IMSI,ZoneCode,OperateChannelID,RegisteredTime) values('";
				 strSql += strUserID;
				 strSql += "','";
				 strSql += strMajorNum;
				 strSql += "','";
				 strSql += strUser;
				 strSql += "','";
				 strSql += strPWD;
				 strSql += "','";
				 strSql += strIMSI;
				 strSql += "','";
				 strSql += strZone;
				 strSql += "','";
				 strSql += strOperatorID;
				 strSql += "','";
				 strSql += strRegTime;
				 strSql += " ')";
				
				 //insert majorNumber
				 if( 0 != dbmysql_->executenonquery(strSql))
				 {
				 			return -2;	
				 }
    }
	  else
	  {
	  	  //only add minor number
	  	  DB_RECORDSET_ARRAY::iterator it = majorNumberRecord.begin();
		    while (it!=majorNumberRecord.end())
		    {
		        DB_RECORDSET_ITEM item_ = (*it);
		        DB_RECORDSET_ITEM::iterator it_ = item_.begin();
		        while(it_!=item_.end())
		        {
		            strUserID = (*it_).second;
		            ++it_;
		        }
		        ++it;
		    }
		    
	  	
	  }
	


      
		 //insert minorNumber
		 std::string  strNumberID="";
		 std::string  strType="3";
		 std::string  strSeqNo="6";
		 std::string  strVIMSI="VIMSI";
		 std::string  strRegType="3";
		 std::string  strStateID="1";
		 std::string  strRecordRule="0";
		 generateUUID(strNumberID);
		 
		 	if(strState == "0")
		 	{
		 			strSeqNo="6";
		 	}
		 	else
		 	{
		 		   //find seq 61 62
		 		   std::string strFindSeq="select SequenceNo from T_MinorNumber where UserID='";
		 		   strFindSeq += strUserID;
		 		   strFindSeq +="'";
		 		   strFindSeq +=" and SequenceNo='61' ";
		 		   
		 		   DB_RECORDSET_ARRAY seqRecord;
				   if( 0 != dbmysql_->executequery(strFindSeq,seqRecord))
				   {
				    	  return -4 ;
				   }
		 		   
		 		   if(seqRecord.empty())
		 		   {
		 		   		strSeqNo="61";
		 		   }
		 		   else
		 		   {
		 		   		strSeqNo="62";
		 		   }
		 		   
		 		   
		 	}
		 
		 std::string strInsertMinorSql = "insert into T_MinorNumber ( NumberID,MinorNumber,UserID,Type,SequenceNo,RegisterTime," ;
     strInsertMinorSql +=  "StateID,IMSI,RecordRule,ZoneCode,SynchronizationTime) values('";
     strInsertMinorSql += strNumberID + "','";
     strInsertMinorSql += strMinorNum + "','";
     strInsertMinorSql += strUserID + "','";
     strInsertMinorSql += strType + "','";
     strInsertMinorSql += strSeqNo + "','";
     strInsertMinorSql += strRegTime + "','";
     strInsertMinorSql += strStateID + "','";
     strInsertMinorSql += strVIMSI + "','";
     strInsertMinorSql += strRecordRule + "','";
     strInsertMinorSql += strZone + "',";
     strInsertMinorSql += "now())";
     
     if(0 != dbmysql_->executenonquery(strInsertMinorSql))
     {
           return -3;
     }

			return 0;

}



int  DataUsage::getDHTAllRegUser(std::vector<stDHTRegUserInfo *> &vecRegUser)
{
		string  strSql ="";
		strSql += "select T_RegisterUser.UserNumber ,T_MinorNumber.MinorNumber , T_MinorNumber.SequenceNo , T_MinorNumber.RegisterTime ,T_MinorNumber.ZoneCode  ";
		strSql += "from  T_RegisterUser,T_MinorNumber " ;
		strSql += "where T_MinorNumber.UserID =  T_RegisterUser.UserID " ;
	
		DB_RECORDSET_ARRAY dbRecord;
	 	if( 0 != dbmysql_->executequery(strSql,dbRecord))
			return -1;
	
	 	DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
	 	DB_RECORDSET_ITEM::iterator it_ ;
	 	
	 	
	 	string strUserNumber="";
	 	string strMinorNumber="";
	 	string strSeq="";
	 	string strRegTime="";
	 	string strZone="";
	 	bool isTrueMinor=true ;
	 	
	 	while (it != dbRecord.end())
	 	{


			isTrueMinor=true ;
			 it_ = (*it).find("usernumber");
			 if ( it_ != it->end())
			 {
				 strUserNumber = it_->second;
			 }
	
			 it_ = (*it).find("minornumber");
			 if ( it_ != it->end())
			 {
					strMinorNumber = it_->second;
					if( strMinorNumber.length()<10)
					{
							isTrueMinor=false;
					}
					
			 }
				
			 //
			 it_ = (*it).find("sequenceno");		
			 if ( it_ != it->end())
			 {
				 strSeq = it_->second;
			 }
	
			 it_ = (*it).find("registertime");
			 if ( it_ != it->end())
			 {
					strRegTime = it_->second;
			 }
			 
			 it_ = (*it).find("zonecode");
			 if ( it_ != it->end())
			 {
				 strZone = it_->second;
			 }
	
			  if(isTrueMinor)
			  {
			  	
			  	stDHTRegUserInfo *pObj = new stDHTRegUserInfo ;
		 		  memset(pObj,0,sizeof(stDHTRegUserInfo));
			  	strncpy(pObj->msisdn,strUserNumber.c_str(),32);
					strncpy(pObj->submsisdn,strMinorNumber.c_str(),32);
					strncpy(pObj->seq,strSeq.c_str(),8);
					strncpy(pObj->regtime,strRegTime.c_str(),32);
					strncpy(pObj->zonecode,strZone.c_str(),8);
					vecRegUser.push_back(pObj);
			  }

			
			 	++it;
	 	}	
	 	
	 	return 0 ;
	 		
}


int  DataUsage::getDHTRegUserBakByDay(const string & strDay ,std::vector<stDHTUnRegUserInfo *>& vecUnRegUser)
{
		 string  strSql ="";
		 strSql +=" select t_registeruser_bak.UserNumber ,t_minornumber_bak.MinorNumber , t_minornumber_bak.SequenceNo , t_minornumber_bak.RegisterTime , t_minornumber_bak.BakTime , t_minornumber_bak.ZoneCode  " ;
		 strSql +=" from t_registeruser_bak ,t_minornumber_bak ";
		 strSql +=" where  t_minornumber_bak.BakTime > '"; 
		 strSql += strDay ;
		 strSql +="'";
		 strSql +=" and t_registeruser_bak.UserID = t_minornumber_bak.UserID ";
		 
		 DB_RECORDSET_ARRAY dbRecord;
		 if( 0 != dbmysql_->executequery(strSql,dbRecord))
				return -1;
		
		 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
		 DB_RECORDSET_ITEM::iterator it_ ;		 
		 	
	 	 string strUserNumber="";
	 	 string strMinorNumber="";
	 	 string strSeq="";
	 	 string strRegTime="";
	 	 string strBakTime="";
	 	 string strZone="";		 	
		 bool isTrueMinor=true ;
		 
	 	 while (it != dbRecord.end())
	 	 {

	
				 isTrueMinor=true ;
				 it_ = (*it).find("usernumber");
				 if ( it_ != it->end())
				 {
					 strUserNumber = it_->second;
				 }
		
				 it_ = (*it).find("minornumber");
				 if ( it_ != it->end())
				 {
						strMinorNumber = it_->second;
						if( strMinorNumber.length()<10)
						{
								isTrueMinor=false;
						}
				 }
					
				 //
				 it_ = (*it).find("sequenceno");		
				 if ( it_ != it->end())
				 {
					 strSeq = it_->second;
				 }
		
				 it_ = (*it).find("registertime");
				 if ( it_ != it->end())
				 {
						strRegTime = it_->second;
				 }
				 
				 
				 it_ = (*it).find("baktime");
				 if ( it_ != it->end())
				 {
						strBakTime = it_->second;
				 }
				 
				 it_ = (*it).find("zonecode");
				 if ( it_ != it->end())
				 {
					 strZone = it_->second;
				 }
		
				  if(isTrueMinor)
				  {
				  	
				  	stDHTUnRegUserInfo *pObj = new stDHTUnRegUserInfo ;
			 		  memset(pObj,0,sizeof(stDHTUnRegUserInfo));
					  strncpy(pObj->msisdn,strUserNumber.c_str(),32);
						strncpy(pObj->submsisdn,strMinorNumber.c_str(),32);
						strncpy(pObj->seq,strSeq.c_str(),8);
						strncpy(pObj->regtime,strRegTime.c_str(),32);
						strncpy(pObj->unregtime,strBakTime.c_str(),32);
						strncpy(pObj->zonecode,strZone.c_str(),8);
			
						vecUnRegUser.push_back(pObj);
				  }

				
				 	++it;
				 	
	 	 }	
	 			
		 return 0 ; 
		 	
	
}

int  DataUsage::getDHTCallRecordByDay(const string & strDay ,std::vector<stDHTCallRecordInfo *> &vecCallRecord)
{
	 string  strSql ="";
	 strSql +="select CALL_TYPE ,Msisdn ,Submsisdn ,count(Msisdn) as CallCount ,sum(CALL_DURATION) div 60 as SumDURATION ,STARTDATE from T_CallRecord ";
	 strSql +="where STARTDATE = '";
	 strSql += strDay ;
	 strSql += "' ";
	 strSql +=" group by Submsisdn ,CALL_TYPE";
	 
	 DB_RECORDSET_ARRAY dbRecord;
		if( 0 != dbmysql_->executequery(strSql,dbRecord))
		return -1;
	
	 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
	 DB_RECORDSET_ITEM::iterator it_ ;
	 	

	 string strCallType="";
	 string strMsisdn="";
	 string strSubmsisdn="";
	 string strOtherpart="";
	 string strDate="";
	 string strCount="";
	 string strCallDuration="";
	 string strZone="";

	 unsigned char key[8];
   memset(key, 0 , sizeof(key));
   strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
   Crypt crypt;
    
	 std::string seqID;
	 std::map<std::string,std::string>::iterator  itMap ;	 
	 			    
	 			    
	 bool isTrueMinor=true ;
	 			    
	 while (it != dbRecord.end())
	 {

	 		strCallType="";
	  	strMsisdn="";
	    strSubmsisdn="";
	    strOtherpart="";
	    strDate="";
	    strCount="";
	    strCallDuration="";
			strZone="";
			isTrueMinor=true ;
		 it_ = (*it).find("call_type");
		 if ( it_ != it->end())
		 {
			 strCallType = it_->second;
		 }

		 it_ = (*it).find("msisdn");
		 if ( it_ != it->end())
		 {
				strMsisdn = it_->second;
				strMsisdn= crypt.decrypt(key, (char *)strMsisdn.c_str());
		 }
			
		 //
		 it_ = (*it).find("submsisdn");		
		 if ( it_ != it->end())
		 {
			 strSubmsisdn = it_->second;
			 strSubmsisdn= crypt.decrypt(key, (char *)strSubmsisdn.c_str());
			 if( strSubmsisdn.length()<10)
			  {
								isTrueMinor=false;
				}
			 
		 }

		 it_ = (*it).find("callcount");		
		 if ( it_ != it->end())
		 {
			 strCount = it_->second;
		 }
		 

		 it_ = (*it).find("startdate");
		 if ( it_ != it->end())
		 {
			 strDate = it_->second;
		 }

		 it_ = (*it).find("sumduration");
		 if ( it_ != it->end())
		 {
				strCallDuration =it_->second;
		 }
		 
		 
		  generateUUID(seqID);
		  itMap = m_mapMinorZoneInfo.find(strSubmsisdn);
			if ( itMap != m_mapMinorZoneInfo.end())
			{
					strZone=itMap->second ;
			}
			else
			{
					strZone="0591";
			}
		  
		  
		  if(isTrueMinor)
		  {
		  	stDHTCallRecordInfo *pObj = new stDHTCallRecordInfo ;
	 		  memset(pObj,0,sizeof(stDHTCallRecordInfo));
		  	strncpy(pObj->seqid,seqID.c_str(),32);
				strncpy(pObj->calltype,strCallType.c_str(),2);
				strncpy(pObj->msisdn,strMsisdn.c_str(),32);
				strncpy(pObj->submsisdn,strSubmsisdn.c_str(),32);
				strncpy(pObj->startdate,strDate.c_str(),11);
				strncpy(pObj->count,strCount.c_str(),8);
				strncpy(pObj->zonecode,strZone.c_str(),8);
				strncpy(pObj->call_duration,strCallDuration.c_str(),12);
				
				
			
				vecCallRecord.push_back(pObj);
		  }

		
		 	++it;
	 }	
	 
	 
	 return 0;	
	
	
	
}

int  DataUsage::getDHTSMSRecordByDay(const string & strDay  ,std::vector<stDHTSmsRecordInfo *> &vecSmsRecord)
{
	 string  strSql ="";
	 strSql +="select CALL_TYPE ,Msisdn ,Submsisdn ,count(Msisdn) as SmsCount ,STARTDATE  from T_SmsRecord ";
	 strSql +="where STARTDATE = '";
	 strSql += strDay ;
	 strSql += "' ";
	 strSql += " group by Submsisdn ,CALL_TYPE";
	 
	 DB_RECORDSET_ARRAY dbRecord;
	 if( 0 != dbmysql_->executequery(strSql,dbRecord))
		return -1;
	
	 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
	 DB_RECORDSET_ITEM::iterator it_ ;

	 string strCallType="";
	 string strMsisdn="";
	 string strSubmsisdn="";
	 string strDate="";
	 string strCount="";
	 string strZone="";

	 unsigned char key[8];
   memset(key, 0 , sizeof(key));
   strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
   Crypt crypt;
	 std::string seqID;
	 std::map<std::string,std::string>::iterator  itMap ;	 
	 	
	 	bool isTrueMinor=true ;
	 while (it != dbRecord.end())
	 {

	 		string strCallType="";
	  	strMsisdn="";
	    strSubmsisdn="";
	    strDate="";
	    strCount="";
			strZone="";
			isTrueMinor=true ;
			
		 it_ = (*it).find("call_type");
		 if ( it_ != it->end())
		 {
			 strCallType = it_->second;
		 }

		 it_ = (*it).find("msisdn");
		 if ( it_ != it->end())
		 {
				strMsisdn = it_->second;
				strMsisdn= crypt.decrypt(key, (char *)strMsisdn.c_str());
		 }
			
		 //
		 it_ = (*it).find("submsisdn");		
		 if ( it_ != it->end())
		 {
			 strSubmsisdn = it_->second;
			 strSubmsisdn= crypt.decrypt(key, (char *)strSubmsisdn.c_str());
			 	if( strSubmsisdn.length()<10)
			  {
								isTrueMinor=false;
				}
		 }

		 it_ = (*it).find("smscount");
		 if ( it_ != it->end())
		 {
				strCount = it_->second;
		 }
		 
		 it_ = (*it).find("startdate");
		 if ( it_ != it->end())
		 {
			 strDate = it_->second;
		 }

		 	generateUUID(seqID);
		  itMap = m_mapMinorZoneInfo.find(strMsisdn);
			if ( itMap != m_mapMinorZoneInfo.end())
			{
					strZone=itMap->second ;
			}
			else
			{
					strZone="0591";
			}
		  
		  if(isTrueMinor)
		  {
		  	
		  	stDHTSmsRecordInfo *pObj = new stDHTSmsRecordInfo ;
	 			memset(pObj,0,sizeof(stDHTSmsRecordInfo));
		  	strncpy(pObj->seqid,seqID.c_str(),32);
				strncpy(pObj->calltype,strCallType.c_str(),2);
				strncpy(pObj->msisdn,strMsisdn.c_str(),32);
				strncpy(pObj->submsisdn,strSubmsisdn.c_str(),32);
				strncpy(pObj->startdate,strDate.c_str(),11);
				strncpy(pObj->count,strCount.c_str(),8);
				strncpy(pObj->zonecode,strZone.c_str(),8);
				
	
				vecSmsRecord.push_back(pObj);
		  }

		
		 	++it;
	 }	

	 return 0 ;	
	
	
	
	
}

// ------------------for putian0594
ResultDataUsage DataUsage::getMinorNumberZoneInfo0594(std::map<std::string,std::string> & mapInfo )
{
	string  strSql = "select MinorNumber ,ZoneCode  from T_MinorNumber where ZoneCode='0594' ";
	DB_RECORDSET_ARRAY dbRecord;
	if( 0 != dbmysql_->executequery(strSql,dbRecord))
		return E_DATABASE_ERROR;
	
	 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
	 DB_RECORDSET_ITEM::iterator it_ ;
	 std::string strMinor="";
	 std::string strZone="";
	 while (it != dbRecord.end())
	 {
		 it_ = (*it).find("minornumber");
		 if ( it_ != it->end())
		 {
			 strMinor = it_->second;
		 }

		 it_ = (*it).find("zonecode");
		 if ( it_ != it->end())
		 {
			strZone = it_->second;
		 }

		 if( strMinor !="" && strZone != "")
		 {
			mapInfo[strMinor]=strZone;
		 }

		 ++it;
	 }	 


	return E_OPERATOR_SUCCED;


}



ResultDataUsage DataUsage::getBakMinorNumberZoneInfo0594(std::map<std::string,std::string> & mapInfo )
{

	string  strSql = "select MinorNumber ,ZoneCode  from t_minornumber_bak where ZoneCode='0594' ";
	DB_RECORDSET_ARRAY dbRecord;
	if( 0 != dbmysql_->executequery(strSql,dbRecord))
		return E_DATABASE_ERROR;
	
	 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
	 DB_RECORDSET_ITEM::iterator it_ ;
	 std::string strMinor="";
	 std::string strZone="";
	 while (it != dbRecord.end())
	 {
		 it_ = (*it).find("minornumber");
		 if ( it_ != it->end())
		 {
			 strMinor = it_->second;
		 }

		 it_ = (*it).find("zonecode");
		 if ( it_ != it->end())
		 {
			strZone = it_->second;
		 }

		 if( strMinor !="" && strZone != "")
		 {
			mapInfo[strMinor]=strZone;
		 }

		 ++it;
	 }	 


	return E_OPERATOR_SUCCED;

}


int  DataUsage::getDHTCallRecordAfterDay0594(const string & strDay ,std::vector<stDHTCallRecordInfo0594 *> &vecCallRecord)
{
	 string  strSql ="";
	 strSql +="select a.CALL_TYPE ,a.Msisdn ,a.Submsisdn ,a.Other_part,a.STARTTIME,a.CALL_DURATION ,a.STARTDATE from T_CallRecord a ,tmp_minor_pool_fj b ";
	 strSql +="where a.STARTDATE > '";
	 strSql += strDay ;
	 strSql += "' ";
	 strSql +=" and a.Submsisdn = b.MinorNumberCrypt ";
	 
	 DB_RECORDSET_ARRAY dbRecord;
		if( 0 != dbmysql_->executequery(strSql,dbRecord))
		return -1;
	
	 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
	 DB_RECORDSET_ITEM::iterator it_ ;
	 	

	 string strCallType="";
	 string strMsisdn="";
	 string strSubmsisdn="";
	 string strOtherpart="";
	 string strDate="";
	 string strCount="";
	 string strCallDuration="";
	 string strZone="";
	 string strTime="";

	 unsigned char key[8];
   memset(key, 0 , sizeof(key));
   strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
   Crypt crypt;
    
	 std::string seqID;
	 std::map<std::string,std::string>::iterator  itMap ;	 
	 			    
	 			    
	 bool isTrueMinor=true ;
	 			    
	 while (it != dbRecord.end())
	 {

	 		strCallType="";
	  	strMsisdn="";
	    strSubmsisdn="";
	    strOtherpart="";
	    strDate="";
	    strCount="";
	    strCallDuration="";
			strZone="";
			strTime="";
			isTrueMinor=true ;
		 it_ = (*it).find("call_type");
		 if ( it_ != it->end())
		 {
			 strCallType = it_->second;
		 }

		 it_ = (*it).find("msisdn");
		 if ( it_ != it->end())
		 {
				strMsisdn = it_->second;
				strMsisdn= crypt.decrypt(key, (char *)strMsisdn.c_str());
		 }
			
		 //
		 it_ = (*it).find("submsisdn");		
		 if ( it_ != it->end())
		 {
			 strSubmsisdn = it_->second;
			 strSubmsisdn= crypt.decrypt(key, (char *)strSubmsisdn.c_str());			 
		 }

		 it_ = (*it).find("other_part");		
		 if ( it_ != it->end())
		 {
			 strOtherpart = it_->second;
			 strOtherpart= crypt.decrypt(key, (char *)strOtherpart.c_str());	
			 
		 }
		 

		 it_ = (*it).find("starttime");
		 if ( it_ != it->end())
		 {
			 strTime = it_->second;
		 }
		 
		 
		 it_ = (*it).find("startdate");
		 if ( it_ != it->end())
		 {
			 strDate = it_->second;
		 }

		 it_ = (*it).find("call_duration");
		 if ( it_ != it->end())
		 {
				strCallDuration =it_->second;
		 }
		 
		 
		  generateUUID(seqID);
		  strZone="0594";
		  
		  if(isTrueMinor)
		  {
		  	stDHTCallRecordInfo0594 *pObj = new stDHTCallRecordInfo0594 ;
	 		  memset(pObj,0,sizeof(stDHTCallRecordInfo0594));
		  	strncpy(pObj->seqid,seqID.c_str(),32);
				strncpy(pObj->calltype,strCallType.c_str(),2);
				strncpy(pObj->msisdn,strMsisdn.c_str(),32);
				strncpy(pObj->submsisdn,strSubmsisdn.c_str(),32);
				strncpy(pObj->otherpart,strOtherpart.c_str(),32);
				strncpy(pObj->startdate,strDate.c_str(),11);
				strncpy(pObj->starttime,strTime.c_str(),9);
				strncpy(pObj->zonecode,strZone.c_str(),8);
				strncpy(pObj->call_duration,strCallDuration.c_str(),12);
				
				
			
				vecCallRecord.push_back(pObj);
		  }

		
		 	++it;
	 }	
	 
	 
	 return 0;	
	
	
	
}


int  DataUsage::getDHTSMSRecordAfterDay0594(const string & strDay  ,std::vector<stDHTSmsRecordInfo0594 *> &vecSmsRecord)
{
	 string  strSql ="";
	 strSql +="select CALL_TYPE ,Msisdn ,Submsisdn ,Other_part ,STARTDATE,STARTTIME  from T_SmsRecord ,tmp_minor_pool_fj ";
	 strSql +="where STARTDATE > '";
	 strSql += strDay ;
	 strSql += "' ";
	 strSql += " and T_SmsRecord.Submsisdn = tmp_minor_pool_fj.MinorNumberCrypt ";
	 
	 DB_RECORDSET_ARRAY dbRecord;
	 if( 0 != dbmysql_->executequery(strSql,dbRecord))
		return -1;
	
	 DB_RECORDSET_ARRAY::iterator it = dbRecord.begin();	
	 DB_RECORDSET_ITEM::iterator it_ ;

	 string strCallType="";
	 string strMsisdn="";
	 string strSubmsisdn="";
	 string strOtherpart="";
	 string strDate="";
	 string strTime="";
	 string strCount="";
	 string strZone="";

	 unsigned char key[8];
   memset(key, 0 , sizeof(key));
   strncpy((char *)&key[0], dbname_.c_str(), sizeof(key)-1);
   Crypt crypt;
	 std::string seqID;
	 std::map<std::string,std::string>::iterator  itMap ;	 
	 	
	 	bool isTrueMinor=true ;
	 while (it != dbRecord.end())
	 {

	 		string strCallType="";
	  	strMsisdn="";
	    strSubmsisdn="";
	    strOtherpart="";
	    strDate="";
	    strTime="";
			strZone="";
			isTrueMinor=true ;
			
		 it_ = (*it).find("call_type");
		 if ( it_ != it->end())
		 {
			 strCallType = it_->second;
		 }

		 it_ = (*it).find("msisdn");
		 if ( it_ != it->end())
		 {
				strMsisdn = it_->second;
				strMsisdn= crypt.decrypt(key, (char *)strMsisdn.c_str());
		 }
			
		 //
		 it_ = (*it).find("submsisdn");		
		 if ( it_ != it->end())
		 {
			 strSubmsisdn = it_->second;
			 strSubmsisdn= crypt.decrypt(key, (char *)strSubmsisdn.c_str());
		 }

		 it_ = (*it).find("other_part");		
		 if ( it_ != it->end())
		 {
			 strOtherpart = it_->second;
			 strOtherpart= crypt.decrypt(key, (char *)strOtherpart.c_str());
		 }
		 
		 it_ = (*it).find("starttime");
		 if ( it_ != it->end())
		 {
				strTime = it_->second;
		 }
		 
		 it_ = (*it).find("startdate");
		 if ( it_ != it->end())
		 {
			 strDate = it_->second;
		 }

		 	generateUUID(seqID);

		  strZone="0594";
		  if(isTrueMinor)
		  {
		  	
		  	stDHTSmsRecordInfo0594 *pObj = new stDHTSmsRecordInfo0594 ;
	 			memset(pObj,0,sizeof(stDHTSmsRecordInfo0594));
		  	strncpy(pObj->seqid,seqID.c_str(),32);
				strncpy(pObj->calltype,strCallType.c_str(),2);
				strncpy(pObj->msisdn,strMsisdn.c_str(),32);
				strncpy(pObj->submsisdn,strSubmsisdn.c_str(),32);
				strncpy(pObj->otherpart,strOtherpart.c_str(),32);
				strncpy(pObj->startdate,strDate.c_str(),11);
				strncpy(pObj->starttime,strTime.c_str(),9);
				strncpy(pObj->zonecode,strZone.c_str(),8);
				
	
				vecSmsRecord.push_back(pObj);
				
		  }

		
		 	++it;
	 }	

	 return 0 ;	
	
	
	
	
}

