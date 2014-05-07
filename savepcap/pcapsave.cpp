#include "pcapsave.h"
#include <time.h>
#include <sys/time.h>

CPcapSave::CPcapSave()
{
	m_strPath = "";
	m_strFileName = "";
	m_iCurrentPkg = 0  ;
	m_iSeparatedCount = 10000;
	m_pObjWriteStream = NULL ;
	m_isFirstWrite = true ;
	m_isTHDEndFlag = false ;
	pthread_mutex_init(&m_objMutex, NULL);
	
	
}


CPcapSave::~ CPcapSave()
{
	this->fflushData();
	this->m_isTHDEndFlag = true ;
	pthread_mutex_destroy(&m_objMutex);
	
}

bool CPcapSave::Init( std::string strSaveFilePath, std::string strFileName, int iSaveCount)
{
	m_strPath = strSaveFilePath ;
	m_strFileName = strFileName ;
	m_iSeparatedCount = iSaveCount ;
	m_pObjWriteStream = new ofstream ;
	
	if ( NULL == m_pObjWriteStream)
	{
		return false ;
	}

	//construct file name 
	time_t tNow =time(NULL); 
	struct tm ptm = {0};
	localtime_r(&tNow, &ptm);
  char szTmp[50] = {0};  
  strftime(szTmp,50,"%y-%m-%d-%H-%M-%S",&ptm);
	std::string strTmpFile = m_strPath+m_strFileName+szTmp+".pcap" ;
	m_pObjWriteStream->open(strTmpFile.c_str(),ios::binary|ios::out);
	if(m_pObjWriteStream->fail())
	{
		printf("CPcapSave init m_pObjWriteStream failed .\n");
		return false ;
	}

	return true ;

}


bool CPcapSave::InitWithThread( std::string strSaveFilePath, std::string strFileName)
{
	m_strPath = strSaveFilePath ;
	m_strFileName = strFileName ;
	m_pObjWriteStream = new ofstream ;
	
	if ( NULL == m_pObjWriteStream)
	{
		return false ;
	}

	//construct file name 
	time_t tNow =time(NULL); 
	struct tm ptm = {0};
	localtime_r(&tNow, &ptm);
  char szTmp[50] = {0};  
  strftime(szTmp,50,"%y-%m-%d-%H-%M-%S",&ptm);
	std::string strTmpFile = m_strPath+m_strFileName+szTmp+".pcap" ;
	m_pObjWriteStream->open(strTmpFile.c_str(),ios::binary|ios::out);
	if(m_pObjWriteStream->fail())
	{
		printf("CPcapSave init m_pObjWriteStream failed .\n");
		return false ;
	}

		pcap_file_header head ;
    head.magic = 0xa1b2c3d4;
    head.version_major = 0x02;
    head.version_minor = 0x04;
    head.thiszone = 0;
    head.sigfigs = 0;
    head.snaplen = 0xFFFF;
    head.linktype = 141;//0; //WTAP_ENCAP_NULL;   // see pcap-common.c

  

	m_pObjWriteStream->write((unsigned char *)&head,sizeof(head));
		
	//start thread 
	int iRet = pthread_create(&m_iThreadID,NULL,WorkThreadFun,(void*)this);

	return true ;

}


bool CPcapSave::SavePcap(char * pData, int iLen)
{
	if(NULL == pData )
	{
		return false ;
	}

	if( m_iCurrentPkg == m_iSeparatedCount)
	{
		//new a pcap file 
		m_iCurrentPkg = 0 ;
		m_pObjWriteStream->close();
		time_t tNow =time(NULL); 
		struct tm ptm = {0};
		localtime_r(&tNow, &ptm);
	  	char szTmp[50] = {0};  
	  	strftime(szTmp,50,"%y-%m-%d-%H-%M-%S",&ptm);
		std::string strTmpFile = m_strPath+m_strFileName+szTmp +".pcap";
		m_pObjWriteStream->open(strTmpFile.c_str(),ios::binary|ios::out);
		
	}

	if(m_pObjWriteStream->fail())
	{
		return false ;
	}

	  pcap_file_header head ;
    head.magic = 0xa1b2c3d4;
    head.version_major = 0x02;
    head.version_minor = 0x04;
    head.thiszone = 0;
    head.sigfigs = 0;
    head.snaplen = 0xFFFF;
    head.linktype = 141;//0; //WTAP_ENCAP_NULL;   // see pcap-common.c

    time_t nowtime = time(NULL);
    struct  timeval now;
    gettimeofday(&now,NULL);
    pcap_pkthdr pkthdr;
    pkthdr.tsSec = nowtime;
    pkthdr.tsMs = now.tv_usec;
    pkthdr.len = iLen;
    pkthdr.caplen = iLen;

		if( m_iCurrentPkg == 0 )
		{
			m_pObjWriteStream->write((unsigned char *)&head,sizeof(head));
		}

	m_pObjWriteStream->write((unsigned char *)&pkthdr,sizeof(pcap_pkthdr));
	m_pObjWriteStream->write((unsigned char *)pData,iLen);
	
	m_pObjWriteStream->flush();
	m_iCurrentPkg++ ;
	return true ;

}



bool CPcapSave::SavePcapForThread(char * pData, int iLen)
{
		if(NULL == pData )
		{
			return false ;
		}

		if(m_pObjWriteStream->fail())
		{
			return false ;
		}

    time_t nowtime = time(NULL);
    struct  timeval now;
    gettimeofday(&now,NULL);
    pcap_pkthdr pkthdr;
    pkthdr.tsSec = nowtime;
    pkthdr.tsMs = now.tv_usec;
    pkthdr.len = iLen;
    pkthdr.caplen = iLen;


		pthread_mutex_lock(&this->m_objMutex);
		m_pObjWriteStream->write((unsigned char *)&pkthdr,sizeof(pcap_pkthdr));
		m_pObjWriteStream->write((unsigned char *)pData,iLen);
		m_pObjWriteStream->flush();
		pthread_mutex_unlock(&this->m_objMutex);
		
		return true ;

}

void* CPcapSave::WorkThreadFun(void * argv)
{
	
		CPcapSave * pSave = (CPcapSave *)argv ;
		while(true)
		{
			sleep(24*60*60);
			//rename file 
			time_t tNow =time(NULL); 
			struct tm ptm = {0};
			localtime_r(&tNow, &ptm);
	  	char szTmp[50] = {0};  
	  	strftime(szTmp,50,"%y-%m-%d-%H-%M-%S",&ptm);
			std::string strTmpFile = pSave->m_strPath+pSave->m_strFileName+szTmp +".pcap";
			pthread_mutex_lock(&pSave->m_objMutex);
			pSave->m_pObjWriteStream->close();
			pSave->m_pObjWriteStream->open(strTmpFile.c_str(),ios::binary|ios::out);
			if(pSave->m_pObjWriteStream->good())
			{
					  pcap_file_header head ;
				    head.magic = 0xa1b2c3d4;
				    head.version_major = 0x02;
				    head.version_minor = 0x04;
				    head.thiszone = 0;
				    head.sigfigs = 0;
				    head.snaplen = 0xFFFF;
				    head.linktype = 141;//0; //WTAP_ENCAP_NULL;   // see pcap-common.c
				    
				    pSave->m_pObjWriteStream->write((unsigned char *)&head,sizeof(head));
				    pSave->m_pObjWriteStream->flush();
			}
			
			pthread_mutex_unlock(&pSave->m_objMutex);
			if(pSave->m_isTHDEndFlag)
			{
				break;
			}

		 }
		 
		return NULL;
}



void CPcapSave::fflushData()
{	
	m_pObjWriteStream->close();
}
























































































