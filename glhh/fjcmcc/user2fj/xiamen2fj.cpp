#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/types.h>   
#include <sys/stat.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <map>
#include <algorithm>
#include "IncallLoc.h"
#include "LogFileManager.h" 
#include "datausage.h"
#include "ConfigFile.h"

using namespace std;
/*
福建 福州 0591
福建 厦门 0592
福建 莆田 0594
福建 三明 0598
福建 泉州 0595
福建 漳州 0596
福建 南平 0599
福建 龙岩 0597
福建 宁德 0593
*/

void getZoneCode(std::string strIn,std::string  &strZonecode )
{
	 if( strIn == "福建 福州")
	 {
	      	strZonecode="0591";
	      	return ;
	 }
	 else if ( strIn == "福建 厦门")
	 {
	      	strZonecode="0592";
	      	return ;	 	
	 }
	 else if ( strIn == "福建 宁德")
	 {
	      	strZonecode="0593";
	      	return ;	 	
	 }
	 else if ( strIn == "福建 莆田")
	 {
	      	strZonecode="0594";
	      	return ;	 	
	 }
	 else if ( strIn == "福建 泉州")
	 {
	      	strZonecode="0595";
	      	return ;	 	
	 }
	 else if ( strIn == "福建 漳州")
	 {
	      	strZonecode="0596";
	      	return ;	 	
	 }
	 else if ( strIn == "福建 龙岩")
	 {
	      	strZonecode="0597";
	      	return ;	 	
	 }
	 else if ( strIn == "福建 三明")
	 {
	      	strZonecode="0598";
	      	return ;	 	
	 }
	 else if ( strIn == "福建 南平")
	 {
	      	strZonecode="0599";
	      	return ;	 	
	 }
	 
	 strZonecode="0591";
	 
	
}



void ParseLineInfo(std::string strSrcLine ,std::vector<std::string> &vecStrRecord)
{
	//将一行的每个字段都存在数组中
	bool isInQuotes=false;
	char tempItem;
	string strTemp="";
	size_t  iPos = 0 ;
	vecStrRecord.clear();

	while(strSrcLine[iPos]!='\0' && iPos < strSrcLine.length())
	{

		tempItem = strSrcLine[iPos];
		if (!isInQuotes && strTemp.length()==0 && tempItem =='"')
		{
			isInQuotes=true;
		}
		else if (isInQuotes && tempItem =='"')
		{
			if ( (iPos+1 < strSrcLine.length()) && (strSrcLine[iPos+1]=='"') ) 
			{
				strTemp.push_back(tempItem);
				iPos++;
			}
			else
			{
				isInQuotes=false; 
			}
		}
		else if (!isInQuotes && tempItem ==',')
		{
			vecStrRecord.push_back( strTemp );
			strTemp="";
		}
		else if (!isInQuotes && (tempItem =='\r' || tempItem =='\n') )
		{
			vecStrRecord.push_back( strTemp );
			return;
		}
		else
		{
			strTemp.push_back(tempItem);
		}

		iPos++;
	}

	vecStrRecord.push_back( strTemp );		
	
}

int main(int argc ,char**argv)
{
#ifdef WIN32
	CreateDirectoryA("./log/", NULL);
#else
	mkdir("./log/", 0700);
#endif
	LogFileManager::initialize("./log/xiamen2fj.log");
	LogFileManager::getInstance()->setLogLevel(Debug);
	string errorCode= "";
	ConfigFile conf;
	if( conf.SetConfigfile("./config.txt", errorCode) != true)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s",errorCode.c_str());
		return -1;
	}

  	// Load Incall location  file
	if(IncallLoc::initialize() == -1)
	{
		LogFileManager::getInstance()->write(Brief,	"ERROR: IncallLoc::initalize() failed .");
		return (-1);
	}

 	//read csv 
	ifstream  ifRegUserCsv ;
	std::string strTemp="";
	std::vector<std::string> vecStr ;
	std::string strRegUser="";
	std::string strMinor="";
	std::string strRegTime="";
	std::string strState="";
	ifRegUserCsv.open("./RegUser.csv");
	if ( ifRegUserCsv.fail())
	{
		LogFileManager::getInstance()->write(Brief, "open RegUser.csv failed.");
		return -1;
	}

	int result=0;	
	int iUser=0;
	int iInsertUser= 0;
	std::string strZone="";
	int iLoop=0;
	while(getline(ifRegUserCsv,strTemp))
	{
		  ParseLineInfo(strTemp,vecStr);
		  strRegUser=vecStr[1];
		  strMinor="8"+vecStr[2];
		  strRegTime = vecStr[6];
		  strState=vecStr[9];
		  iUser++;
		  iLoop++;
		  iInsertUser++;
		  strZone.clear();
		  if( IncallLoc::getInstance()->findTelLocation(strRegUser, strZone) != 0)
		  {
		  	//default
			  	strZone="0591";
			  	LogFileManager::getInstance()->write(Brief, "User = %s  is default Zone.",strRegUser.c_str());
		  }
		  else
		  {
		  		//get zonecode 
		  		std::string strZoneCode="";
		  		getZoneCode(strZone,strZoneCode);
		  		strZone= strZoneCode;
		  		
		  }
		  
		  result = DataUsage::instance()->InsertXiamenUser2Fj(strRegUser,strMinor,strRegTime,strZone,strState);
		  if( result == -1)
		  {
		  	LogFileManager::getInstance()->write(Brief, "InsertXiamenUser2Fj:User = %s  is insert failed(-1) .",strRegUser.c_str());
		  	iInsertUser--;
		  }
		  else if ( result == -2)
		  {
				LogFileManager::getInstance()->write(Brief, "InsertXiamenUser2Fj:User = %s is insert failed(-2) ,MinorNum= %s .",strRegUser.c_str(),strMinor.c_str());
		  	iInsertUser--;  	
		  }
		  else if ( result == -3)
		  {
		  	LogFileManager::getInstance()->write(Brief, "InsertXiamenUser2Fj:User = %s  is insert failed(-3),MinorNum= %s  .",strRegUser.c_str(),strMinor.c_str());
		  	iInsertUser--;
		  }
		  else if ( result == -4)
		  {
		  	LogFileManager::getInstance()->write(Brief, "InsertXiamenUser2Fj:User = %s  is insert failed(-4),MinorNum= %s  .",strRegUser.c_str(),strMinor.c_str());
		  	iInsertUser--;
		  }
			
			if(iLoop == 10)
			{
					sleep(1);
					iLoop =0 ;	
			}
		  
	}
	LogFileManager::getInstance()->write(Brief, "Insert xiamenUser2fj,AllCount = %d ,InsertCount= %d ",iUser,iInsertUser);
		
	
	

	





	return 0  ;

}

















































