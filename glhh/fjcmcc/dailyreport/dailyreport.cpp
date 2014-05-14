#include <stdio.h>
#include "ConfigFile.h"
#include <pthread.h>
#include <string.h>
#include <string>
#include "LogFileManager.h" 
#include "datausage.h"
#include <unistd.h>
#include <sys/types.h>   
#include <sys/stat.h>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>

using namespace std;

int main(int argc ,char**argv)
{
#ifdef WIN32
	CreateDirectoryA("./log/", NULL);
#else
	mkdir("./log/", 0700);
#endif
#ifdef WIN32
	CreateDirectoryA("./data/", NULL);
#else
	mkdir("./data/", 0700);
#endif
	LogFileManager::initialize("./log/dailyreport.log");
	LogFileManager::getInstance()->setLogLevel(Debug);
	string errorCode= "";
	ConfigFile conf;
	if( conf.SetConfigfile("./config.txt", errorCode) != true)
	{
		LogFileManager::getInstance()->write(Brief, "ERROR : %s",errorCode.c_str());
		return -1;
	}

	int result = 0 ;
	int preResult = 0 ;
	char szBuff[512] = {0};
	FILE * fd;  
	string strProgress="date +%Y-%m-%d";
	fd = popen (strProgress.c_str(), "r"); 
	if (fd)
	{
		fgets (szBuff, sizeof(szBuff), fd);
	}
	fclose(fd);
	
	std::vector<stDHTRegUserInfo *> vecRegUser ;
	std::vector<stDHTUnRegUserInfo *> vecUnRegUser ;
	std::vector<stDHTCallRecordInfo *> vecCallRecord ;
	std::vector<stDHTSmsRecordInfo *> vecSmsRecord ;			

	std::string strDay="2014-01-19";
	strDay = 	szBuff;
	strDay=strDay.substr(0,10);
	if(argc == 2)
	{
			strDay=argv[1];
	}
	
	LogFileManager::getInstance()->write(Brief, "getDialyCallRecord INFO :strDay=%s ",strDay.c_str());
	if ( argc != 2)
	{
	   sleep(63);		
	}
	
	result = DataUsage::instance()->getMinorAndBakInfoByDay(strDay);
	result = DataUsage::instance()->getDHTAllRegUser(vecRegUser);
	LogFileManager::getInstance()->write(Brief, "getDHTAllRegUser INFO :result=  %d ,VecSize = %d ",result,vecRegUser.size());
	result = DataUsage::instance()->getDHTRegUserBakByDay(strDay,vecUnRegUser); 
	LogFileManager::getInstance()->write(Brief, "getDHTRegUserBakByDay INFO :result=  %d ,VecSize = %d ",result,vecUnRegUser.size());
	
	
	result = DataUsage::instance()->getDHTCallRecordByDay(strDay,vecCallRecord);
	LogFileManager::getInstance()->write(Brief, "getDHTCallRecordByDay INFO :result=  %d ,VecSize = %d ",result,vecCallRecord.size());	
	result = DataUsage::instance()->getDHTSMSRecordByDay(strDay,vecSmsRecord); 
	LogFileManager::getInstance()->write(Brief, "getDHTSMSRecordByDay INFO :result=  %d ,VecSize = %d ",result,vecSmsRecord.size());
		
	//print file 
	std::vector<stDHTRegUserInfo *>::iterator itRegUser;
	std::vector<stDHTUnRegUserInfo *>::iterator 	itUnRegUser;
	std::vector<stDHTCallRecordInfo *>::iterator  itCallRecord;
	std::vector<stDHTSmsRecordInfo *>::iterator   itSmsRecord;
		
	//print index and data	
	char szLine[1024]={0};
	stDHTRegUserInfo *pRegUser =NULL;
	stDHTUnRegUserInfo *pUnRegUser = NULL;
	stDHTCallRecordInfo *pCallRecord = NULL;
	stDHTSmsRecordInfo *pSmsRecord = NULL;
	string strDay_=strDay;
	string::iterator   it;
	for (it =strDay_.begin(); it != strDay_.end();   it++)
	{
			if ( *it == '-')
			{
				strDay_.erase(it);
			}
	}
	
	 
	int iFileSeq = 0 ;
	time_t ltime;
	tm* curtime;
	char daybuf1[64]={0};
	char daybuf2[64]={0};
	char fileNameAVL[128]={0};
	char fileNameCHK[128]={0};
	ofstream  fileAVL;
	ofstream  fileCHK;
	unsigned long  fileSize=0;
	struct stat statbuff; 
	time_t  fileEndTime;
	char fileLastTime[64]={0};
	
	ltime = time(0);
	curtime = localtime(&ltime);
	iFileSeq=1;
	strftime(daybuf1, 64, "%Y%m%d", curtime);
	strftime(daybuf2, 64, "%Y%m%d", curtime);
	sprintf(fileNameAVL,"I02582%s%06d.AVL",strDay_.c_str(),iFileSeq);
	sprintf(fileNameCHK,"I02582%s.CHK",strDay_.c_str());
	
	
	
	
	 //regUser
	fileAVL.open(fileNameAVL,ios::out |ios::trunc);
	if( fileAVL.good())
	{
		 	for( itRegUser= vecRegUser.begin() ; itRegUser != vecRegUser.end(); itRegUser++)
			{
					pRegUser = *itRegUser ;
					sprintf(szLine,"%s\t%s\t%s\t%s\t%s\n",pRegUser->msisdn,pRegUser->submsisdn,pRegUser->seq,pRegUser->regtime,pRegUser->zonecode);
					fileAVL.write(szLine,strlen(szLine));	
			}
			
	}	
	fileAVL.close();	
  
	if(stat(fileNameAVL, &statbuff) >=0)
	{
		 fileSize= statbuff.st_size ;
		 fileEndTime = statbuff.st_mtime;
	}  
	
	curtime = localtime(&fileEndTime);
	strftime(fileLastTime, 64, "%Y%m%d%H%M%S", curtime);
  fileCHK.open(fileNameCHK,ios::out |ios::trunc);
  sprintf(szLine,"%s\t%d\t%d\t%s\t%s\n",fileNameAVL,fileSize,vecRegUser.size(),strDay_.c_str(),fileLastTime);
  fileCHK.write(szLine,strlen(szLine));	
  fileCHK.close();	
  
  //unRegUser 
  iFileSeq=1;
  ltime = time(0);
	curtime = localtime(&ltime);
	strftime(daybuf1, 64, "%Y%m%d", curtime);
	strftime(daybuf2, 64, "%Y%m%d", curtime);
	sprintf(fileNameAVL,"A02583%s%06d.AVL",strDay_.c_str(),iFileSeq);
	sprintf(fileNameCHK,"A02583%s.CHK",strDay_.c_str());
		
	fileAVL.open(fileNameAVL,ios::out |ios::trunc);
	if( fileAVL.good())
	{
		 	for( itUnRegUser= vecUnRegUser.begin() ; itUnRegUser != vecUnRegUser.end(); itUnRegUser++)
			{
					pUnRegUser = *itUnRegUser ;
					sprintf(szLine,"%s\t%s\t%s\t%s\t%s\t%s\n",pUnRegUser->msisdn,pUnRegUser->submsisdn,pUnRegUser->seq,pUnRegUser->regtime,pUnRegUser->unregtime,pUnRegUser->zonecode);
					fileAVL.write(szLine,strlen(szLine));	
			}
			
	}	
	fileAVL.close();	
	
	if(stat(fileNameAVL, &statbuff) >=0)
	{
		 fileSize= statbuff.st_size ;
		 fileEndTime = statbuff.st_mtime;
	} 
	curtime = localtime(&fileEndTime);
	strftime(fileLastTime, 64, "%Y%m%d%H%M%S", curtime);
  fileCHK.open(fileNameCHK,ios::out |ios::trunc);
  sprintf(szLine,"%s\t%d\t%d\t%s\t%s\n",fileNameAVL,fileSize,vecUnRegUser.size(),strDay_.c_str(),fileLastTime);
  fileCHK.write(szLine,strlen(szLine));	
  fileCHK.close();	
		
	
	//callRecord
	iFileSeq =1;
	ltime = time(0);
	curtime = localtime(&ltime);
	strftime(daybuf1, 64, "%Y%m%d", curtime);
	strftime(daybuf2, 64, "%Y%m%d", curtime);
	sprintf(fileNameAVL,"A04140%s%06d.AVL",strDay_.c_str(),iFileSeq);
	sprintf(fileNameCHK,"A04140%s.CHK",strDay_.c_str());
		
	fileAVL.open(fileNameAVL,ios::out |ios::trunc);
	if( fileAVL.good())
	{
		 	for( itCallRecord= vecCallRecord.begin() ; itCallRecord != vecCallRecord.end(); itCallRecord++)
			{
					pCallRecord = *itCallRecord ;
					sprintf(szLine,"%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",pCallRecord->seqid,pCallRecord->calltype,pCallRecord->msisdn,pCallRecord->submsisdn,pCallRecord->startdate,pCallRecord->call_duration,pCallRecord->count,pCallRecord->zonecode);
					fileAVL.write(szLine,strlen(szLine));	
			}
			
	}	
	fileAVL.close();	
	if(stat(fileNameAVL, &statbuff) >=0)
	{
		 fileSize= statbuff.st_size ;
		 fileEndTime = statbuff.st_mtime;
	} 
	curtime = localtime(&fileEndTime);
	strftime(fileLastTime, 64, "%Y%m%d%H%M%S", curtime);
  fileCHK.open(fileNameCHK,ios::out |ios::trunc);
  sprintf(szLine,"%s\t%d\t%d\t%s\t%s\n",fileNameAVL,fileSize,vecCallRecord.size(),strDay_.c_str(),fileLastTime);
  fileCHK.write(szLine,strlen(szLine));	
  fileCHK.close();	
	
	
	
	//sms record  
	iFileSeq =1 ;
	ltime = time(0);
	curtime = localtime(&ltime);
	strftime(daybuf1, 64, "%Y%m%d", curtime);
	strftime(daybuf2, 64, "%Y%m%d", curtime);
	sprintf(fileNameAVL,"A04141%s%06d.AVL",strDay_.c_str(),iFileSeq);
	sprintf(fileNameCHK,"A04141%s.CHK",strDay_.c_str());
		
	fileAVL.open(fileNameAVL,ios::out |ios::trunc);
	if( fileAVL.good())
	{
		 	for( itSmsRecord= vecSmsRecord.begin() ; itSmsRecord != vecSmsRecord.end(); itSmsRecord++)
			{
					pSmsRecord = *itSmsRecord ;
					sprintf(szLine,"%s\t%s\t%s\t%s\t%s\t%s\t%s\n",pSmsRecord->seqid,pSmsRecord->calltype,pSmsRecord->msisdn,pSmsRecord->submsisdn,pSmsRecord->startdate,pSmsRecord->count);
					fileAVL.write(szLine,strlen(szLine));	
			}
			
	}	
	fileAVL.close();	
	if(stat(fileNameAVL, &statbuff) >=0)
	{
		 fileSize= statbuff.st_size ;
		 fileEndTime = statbuff.st_mtime;
	} 
	curtime = localtime(&fileEndTime);
	strftime(fileLastTime, 64, "%Y%m%d%H%M%S", curtime);
  fileCHK.open(fileNameCHK,ios::out |ios::trunc);
  sprintf(szLine,"%s\t%d\t%d\t%s\t%s\n",fileNameAVL,fileSize,vecSmsRecord.size(),strDay_.c_str(),fileLastTime);
  fileCHK.write(szLine,strlen(szLine));	
  fileCHK.close();	
	
	system(" . reportftp.sh"); 			
		
	return 0  ;

}

















































